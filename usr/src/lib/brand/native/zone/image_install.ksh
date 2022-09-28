#!/bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. /usr/lib/brand/shared/common.ksh

# Restrict executables to /bin, /usr/bin and /usr/sfw/bin
PATH=/bin:/usr/bin:/usr/sbin:/usr/sfw/bin
export PATH

zone_initfail=$(gettext "Attempt to initialize zone '%s' FAILED.")
path_abs=$(gettext "Pathname specified to -a '%s' must be absolute.")

both_modes=$(gettext "%s: cannot select both silent and verbose modes")

both_choices=$(gettext "%s: cannot select both preserve and unconfigure options")

both_kinds=$(gettext "%s: cannot specify both archive and directory")

no_install=$(gettext "Could not create install directory '%s'")
no_log=$(gettext "Could not create log directory '%s'")

media_taste=$(gettext   "    Media Type: %s")

product_vers=$(gettext  "       Product: %s")
install_vers=$(gettext  "     Installer: %s")
install_zone=$(gettext  "          Zone: %s")
install_path=$(gettext  "          Path: %s")
install_from=$(gettext  "        Source: %s")
installing=$(gettext    "    Installing: This may take several minutes...")
no_installing=$(gettext "    Installing: Using pre-existing data in zonepath")
install_prog=$(gettext  "    Installing: %s")

install_fail=$(gettext  "        Result: *** Installation FAILED ***")
install_log=$(gettext   "      Log File: %s")

install_abort=$(gettext "        Result: Installation aborted.")
install_good=$(gettext  "        Result: Installation completed successfully.")

not_native_image=$(gettext  "  Sanity Check: %s doesn't look like a native image.")
sanity_ok=$(gettext     "  Sanity Check: Passed.  Looks like a native system.")
sanity_fail_detail=$(gettext  "  Sanity Check: Missing %s at %s")
sanity_fail_vers=$(gettext  "  Sanity Check: image release version %s does not match system release version %s, the zone is not usable on this system.")
sanity_fail=$(gettext   "  Sanity Check: FAILED (see log for details).")


p2ving=$(gettext        "Postprocessing: This may take a while...")
p2v_prog=$(gettext      "   Postprocess: ")
p2v_done=$(gettext      "        Result: Postprocessing complete.")
p2v_fail=$(gettext      "        Result: Postprocessing failed.")

media_missing=\
$(gettext "%s: you must specify an installation source using '-a' or '-d'.")

cfgchoice_missing=\
$(gettext "%s: you must specify -u (sys-unconfig) or -p (preserve identity).")

mount_failed=$(gettext "ERROR: zonecfg(1M) 'fs' mount failed")

e_baddir=$(gettext "Invalid '%s' directory within the zone")

# Clean up on interrupt
trap_cleanup()
{
	msg=$(gettext "Installation cancelled due to interrupt.")
	log "$msg"

	# umount IPDs
	umnt_fs

	exit $EXIT_CODE
}

sanity_check()
{
	typeset dir="$1"
	shift
	res=0

	# These checks must work with a sparse zone.
	checks="etc etc/svc usr sbin lib var var/svc"
	for x in $checks; do
		if [[ ! -e $dir/$x ]]; then
			vlog "$sanity_fail_detail" "$x" "$dir"
			res=1
		fi
	done

	#
	# Check image release against system release.  We only work on the
	# same minor release as the system is running.
	#
	sys_vers=0
	image_vers=-1
	if [[ -f /var/sadm/system/admin/INST_RELEASE ]]; then
		sys_vers=$(nawk -F= '{if ($1 == "VERSION") print $2}' \
		    /var/sadm/system/admin/INST_RELEASE)
	fi

	if [[ -f $dir/var/sadm/system/admin/INST_RELEASE ]]; then
		image_vers=$(nawk -F= '{if ($1 == "VERSION") print $2}' \
		    $dir/var/sadm/system/admin/INST_RELEASE)
	fi

	if (( $sys_vers != $image_vers )); then
		vlog "$sanity_fail_vers" "$image_vers" "$sys_vers"
		res=1
	fi
	
	if (( $res != 0 )); then
		log "$sanity_fail"
		log ""
		log "$install_log" "$LOGFILE"
		fatal "$install_fail" "$zonename"
	fi

	vlog "$sanity_ok"
}

#
# The main body of the script starts here.
#
# This script should never be called directly by a user but rather should
# only be called by zoneadm to install a native system image into a zone.
#

#
# Exit code to return if install is interrupted or exit code is otherwise
# unspecified.
#
EXIT_CODE=$ZONE_SUBPROC_USAGE

trap trap_cleanup INT

# If we weren't passed at least two arguments, exit now.
(( $# < 2 )) && exit $ZONE_SUBPROC_USAGE

zonename="$1"
zonepath="$2"

ZONEROOT="$zonepath/root"
logdir="$ZONEROOT/var/log"

shift; shift	# remove zonename and zonepath from arguments array

unset backout
unset inst_type
unset msg
unset silent_mode
unset OPT_V

#
# It is worth noting here that we require the end user to pick one of
# -u (sys-unconfig) or -p (preserve config).  This is because we can't
# really know in advance which option makes a better default.  Forcing
# the user to pick one or the other means that they will consider their
# choice and hopefully not be surprised or disappointed with the result.
#
unset unconfig_zone
unset preserve_zone

while getopts "a:b:d:psuv" opt
do
	case "$opt" in
		a)
			if [[ -n "$inst_type" ]]; then
				fatal "$both_kinds" "zoneadm install"
			fi
		 	inst_type="archive"
			install_media="$OPTARG"
			;;

		b)	if [[ -n "$backout" ]]; then
				backout="$backout -b $OPTARG"
			else
				backout="-b $OPTARG"
			fi
			;;
		d)
			if [[ -n "$inst_type" ]]; then
				fatal "$both_kinds" "zoneadm install"
			fi
		 	inst_type="directory"
			install_media="$OPTARG"
			;;
		p)	preserve_zone="-p";;
		s)	silent_mode=1;;
		u)	unconfig_zone="-u";;
		v)	OPT_V="-v";;
		*)	exit $ZONE_SUBPROC_USAGE;;
	esac
done
shift OPTIND-1

# The install can't be both verbose AND silent...
if [[ -n $silent_mode && -n $OPT_V ]]; then
	fatal "$both_modes" "zoneadm install"
fi

if [[ -z $install_media ]]; then
	fatal "$media_missing" "zoneadm install"
fi

# The install can't both preserve and unconfigure
if [[ -n $unconfig_zone && -n $preserve_zone ]]; then
	fatal "$both_choices" "zoneadm install"
fi

# Must pick one or the other.
if [[ -z $unconfig_zone && -z $preserve_zone ]]; then
	fatal "$cfgchoice_missing" "zoneadm install"
fi

LOGFILE=$(/usr/bin/mktemp -t -p /var/tmp $zonename.install_log.XXXXXX)
if [[ -z "$LOGFILE" ]]; then
	fatal "$e_tmpfile"
fi
zone_logfile="${logdir}/$zonename.install$$.log"
exec 2>>"$LOGFILE"
log "$install_log" "$LOGFILE"

vlog "Starting pre-installation tasks."

#
# From here on out, an unspecified exit or interrupt should exit with
# ZONE_SUBPROC_NOTCOMPLETE, meaning a user will need to do an uninstall before
# attempting another install, as we've modified the directories we were going
# to install to in some way.
#
EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE

if [[ ! -d "$ZONEROOT" ]]
then
	if ! mkdir -p "$ZONEROOT" 2>/dev/null; then
		fatal "$no_install" "$ZONEROOT"
	fi
fi

vlog "Installation started for zone \"$zonename\""
install_image "$inst_type" "$install_media"

log "$p2ving"
vlog "running: p2v $OPT_V $unconfig_zone $backout $zonename $zonepath"

#
# Run p2v.
#
# Getting the output to the right places is a little tricky because what
# we want is for p2v to output in the same way the installer does: verbose
# messages to the log file always, and verbose messages printed to the
# user if the user passes -v.  This rules out simple redirection.  And
# we can't use tee or other tricks because they cause us to lose the
# return value from the p2v script due to the way shell pipelines work.
#
# The simplest way to do this seems to be to hand off the management of
# the log file to the p2v script.  So we run p2v with -l to tell it where
# to find the log file and then reopen the log (O_APPEND) when p2v is done.
#
/usr/lib/brand/native/p2v -l "$LOGFILE" -m "$p2v_prog" \
     $OPT_V $unconfig_zone $backout $zonename $zonepath
p2v_result=$?
exec 2>>$LOGFILE

if (( $p2v_result == 0 )); then
	vlog "$p2v_done"
else
	log "$p2v_fail"
	log ""
	log "$install_fail"
	log "$install_log" "$LOGFILE"
	exit $ZONE_SUBPROC_FATAL
fi

EXIT_CODE=$ZONE_SUBPROC_OK

log ""
log "$install_good" "$zonename"

if [[ -h $ZONEROOT/var || ! -d $ZONEROOT/var || -h $ZONEROOT/var/log ]]; then
	log "$e_baddir" "/var/log"
	exit $ZONE_SUBPROC_FATAL
fi

# Just in case the log directory isn't present...
if [[ ! -d "$logdir" ]]; then
	if ! mkdir -p "$logdir" 2>/dev/null; then
		log "$no_log" "$logdir"
	fi
fi

if [[ ! -h $zone_logfile && ! -d $zone_logfile ]]; then
	cp $LOGFILE $zone_logfile
fi
log "$install_log" "$zone_logfile"
rm -f $LOGFILE

exit 0
