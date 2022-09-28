#!/bin/sh
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. /lib/svc/share/smf_include.sh
. /lib/svc/share/ipf_include.sh

YPDIR=/usr/lib/netsvc/yp

create_client_ipf_rules()
{
	FMRI=$1
	file=`fmri_to_file $FMRI $IPF_SUFFIX`
	iana_name=`svcprop -p $FW_CONTEXT_PG/name $FMRI`
	domain=`domainname`

	if [ -z "$domain" ]; then
		return 0
	fi

	if [ ! -d /var/yp/binding/$domain ]; then
		return
	fi
	echo "# $FMRI" >$file

	ypfile="/var/yp/binding/$domain/ypservers"
	if [ -f $ypfile ]; then
		tports=`$SERVINFO -R -p -t -s $iana_name 2>/dev/null`
		uports=`$SERVINFO -R -p -u -s $iana_name 2>/dev/null`

		server_addrs=""
		for ypsvr in `grep -v '^[ ]*#' $ypfile`; do
			#
			# Get corresponding IPv4 address in /etc/hosts
			#
			servers=`grep -v '^[ ]*#' /etc/hosts | awk ' {
			    if ($1 !~/:/) {
				for (i=2; i<=NF; i++) {
				    if (s == $i) printf("%s ", $1);
				} }
			    }' s="$ypsvr"`

			[ -z "$servers"  ] && continue
			server_addrs="$server_addrs $servers"
		done

		[ -z "$server_addrs"  ] && return 0
		for s in $server_addrs; do
			if [ -n "$tports" ]; then
				for tport in $tports; do
					echo "pass in log quick proto tcp" \
					    "from $s to any port = $tport" >>$file
				done
			fi

			if [ -n "$uports" ]; then
				for uport in $uports; do
					echo "pass in log quick proto udp" \
					    "from $s to any port = $uport" >>$file
				done
			fi
		done
	else
		#
		# How do we handle the client broadcast case? Server replies
		# to the outgoing port that sent the broadcast, but there's
		# no way the client know a packet is the reply.
		#
		# Nis server should be specified and clients shouldn't be
		# doing broadcasts but if it does, no choice but to allow
		# all traffic.
		#
		echo "pass in log quick proto udp from any to any" \
		    "port > 32768" >>$file
	fi
}

#
# Ipfilter method
#
if [ -n "$1" -a "$1" = "ipfilter" ]; then
	create_client_ipf_rules $2
	exit $SMF_EXIT_OK
fi

case $SMF_FMRI in
	'svc:/network/nis/client:default')
		domain=`domainname`

		if [ -z "$domain" ]; then
			echo "$0: domainname not set"
			exit $SMF_EXIT_ERR_CONFIG
		fi

		if [ ! -d /var/yp/binding/$domain ]; then
			echo "$0: /var/yp/binding/$domain is not a directory"
			exit $SMF_EXIT_ERR_CONFIG
		fi

		# Since two ypbinds will cause ypwhich to hang...
		if pgrep -z `/sbin/zonename` ypbind >/dev/null; then
			echo "$0: ypbind is already running."
			exit $SMF_EXIT_ERR_CONFIG
		fi

		if [ -f /var/yp/binding/$domain/ypservers ]; then
			$YPDIR/ypbind > /dev/null 2>&1
		else
			$YPDIR/ypbind -broadcast > /dev/null 2>&1
		fi

		rc=$?
		if [ $rc != 0 ]; then
			echo "$0: ypbind failed with $rc"
			exit 1
		fi
		;;

	'svc:/network/nis/server:default')
		domain=`domainname`

		if [ -z "$domain" ]; then
			echo "$0: domainname not set"
			exit $SMF_EXIT_ERR_CONFIG
		fi

		if [ ! -d /var/yp/$domain ]; then
			echo "$0: domain directory missing"
			exit $SMF_EXIT_ERR_CONFIG
		fi

		if [ -f /etc/resolv.conf ]; then
			$YPDIR/ypserv -d
		else
			$YPDIR/ypserv
		fi

		rc=$?
		if [ $rc != 0 ]; then
			echo "$0: ypserv failed with $rc"
			exit 1
		fi
		;;

	'svc:/network/nis/passwd:default')
		PWDIR=`grep "^PWDIR" /var/yp/Makefile 2> /dev/null` \
		    && PWDIR=`expr "$PWDIR" : '.*=[ 	]*\([^ 	]*\)'`
		if [ "$PWDIR" ]; then
			if [ "$PWDIR" = "/etc" ]; then
				unset PWDIR
			else
				PWDIR="-D $PWDIR"
			fi
		fi
		$YPDIR/rpc.yppasswdd $PWDIR -m

		rc=$?
		if [ $rc != 0 ]; then
			echo "$0: rpc.yppasswdd failed with $rc"
			exit 1
		fi
		;;

	*)
		echo "$0: Unknown service \"$SMF_FMRI\"."
		exit $SMF_EXIT_ERR_CONFIG
		;;
esac
exit $SMF_EXIT_OK
