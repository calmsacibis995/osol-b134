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

LIBRARY= libzfs.a
VERS= .1

OBJS_SHARED=			\
	zfs_comutil.o		\
	zfs_deleg.o		\
	zfs_fletcher.o		\
	zfs_namecheck.o		\
	zfs_prop.o		\
	zpool_prop.o		\
	zprop_common.o

OBJS_COMMON=			\
	libzfs_changelist.o	\
	libzfs_config.o		\
	libzfs_dataset.o	\
	libzfs_fru.o		\
	libzfs_graph.o		\
	libzfs_import.o		\
	libzfs_mount.o		\
	libzfs_pool.o		\
	libzfs_sendrecv.o	\
	libzfs_status.o		\
	libzfs_util.o

OBJECTS= $(OBJS_COMMON) $(OBJS_SHARED)

include ../../Makefile.lib

# libzfs must be installed in the root filesystem for mount(1M)
include ../../Makefile.rootfs

LIBS=	$(DYNLIB) $(LINTLIB)

SRCDIR =	../common

INCS += -I$(SRCDIR)
INCS += -I../../../uts/common/fs/zfs
INCS += -I../../../common/zfs
INCS += -I../../libc/inc

C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all
LDLIBS +=	-lc -lm -ldevid -lgen -lnvpair -luutil -lavl -lefi \
	-ladm -lidmap -ltsol -lmd -lumem
CPPFLAGS +=	$(INCS) -D_REENTRANT

SRCS=	$(OBJS_COMMON:%.o=$(SRCDIR)/%.c)	\
	$(OBJS_SHARED:%.o=$(SRC)/common/zfs/%.c)
$(LINTLIB) := SRCS=	$(SRCDIR)/$(LINTSRC)

.KEEP_STATE:

all: $(LIBS)

lint: lintcheck

pics/%.o: ../../../common/zfs/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include ../../Makefile.targ
