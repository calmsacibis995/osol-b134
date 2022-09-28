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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

LIBRARY =		libprint.a
VERS =			.2
OBJECTS = \
	list.o ns.o ns_bsd_addr.o ns_cmn_kvp.o \
	ns_cmn_printer.o nss_convert.o nss_ldap.o nss_printer.o nss_write.o

include ../../../Makefile.lib
include ../../../Makefile.rootfs

SRCDIR =	../common

ROOTLIBDIR=	$(ROOT)/usr/lib

LIBS =			$(DYNLIB)

$(LINTLIB):=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(SRCDIR)
CPPFLAGS +=	-I../../head -D_REENTRANT

LDLIBS +=	-lnsl -lsocket -lc -lldap


.KEEP_STATE:

all:	$(LIBS)

lint:	lintcheck

include ../../../Makefile.targ
