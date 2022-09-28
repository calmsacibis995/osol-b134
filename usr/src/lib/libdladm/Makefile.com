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

LIBRARY = libdladm.a
VERS    = .1
OBJECTS = libdladm.o secobj.o linkprop.o libdllink.o libdlaggr.o \
	libdlwlan.o libdlvnic.o libdlmgmt.o libdlvlan.o \
	flowattr.o flowprop.o propfuncs.o libdlflow.o libdlstat.o \
	usage.o libdlether.o libdlsim.o libdlbridge.o libdliptun.o

include ../../Makefile.lib

# install this library in the root filesystem
include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)
LDLIBS +=	-ldevinfo -lc -linetutil -lsocket -lscf -lrcm -lnvpair \
		-lexacct -lnsl -lkstat -lcurses

SRCDIR =	../common
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS +=	-I$(SRCDIR) -D_REENTRANT

.KEEP_STATE:

all:		$(LIBS)

lint:		lintcheck

include $(SRC)/lib/Makefile.targ
