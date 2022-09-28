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
#

LIBRARY =	libpg.a
VERS =		.1

OBJECTS = 	pg_uuid.o pg_host_ident.o

include $(SRC)/lib/Makefile.lib
include ../Makefile.defs

LIBS =		$(DYNLIB) $(LINTLIB)

SRCDIR =	../common

SRCS =	$(OBJECTS:%.o=$(SRCDIR)/%.c)

LDLIBS +=	 -lc -luuid
LDLIBS +=	-L$(SRC)/lib/mms/mms/$(MACH) -lmms

CCFLAGS +=	$(CCVERBOSE)
CCFLAGS +=	$(C_BIGPICFLAGS)

CPPFLAGS +=	-DMMS_OPENSSL
CPPFLAGS +=	-I$(SRCDIR) -I$(SRC)/common/mms/mms
CPPFLAGS +=	-I$(SRC)/lib/mms/mms/common
CPPFLAGS +=	-I/usr/postgres/8.3/include/server

C99MODE = $(C99_ENABLE)

.KEEP_STATE:

all: $(LIBS)

lint: $(LINTLIB) lintcheck

include $(SRC)/lib/Makefile.targ
include ../Makefile.rootdirs

install: all $(ROOTLIBDIR) $(ROOTLIBS) $(ROOTLINKS)
