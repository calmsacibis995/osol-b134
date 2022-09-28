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

LIBRARY =	libQUANTUM_SDLT600.a
VERS =		
OBJECTS =	dm_QUANTUM_SDLT600.o

include $(SRC)/lib/Makefile.lib

LIBLINKS =

LIBS =		$(DYNLIB) $(LINTLIB)

SRCDIR =	../common

SRCS = 		$(OBJECTS:%.o=$(SRCDIR)/%.c)

LDLIBS +=	-lc
LDLIBS +=	-L$(SRC)/lib/mms/mms/$(MACH) -lmms

CFLAGS +=	$(CCVERBOSE)

CPPFLAGS +=	-DMMS_OPENSSL
CPPFLAGS +=	-I$(SRCDIR) -I$(SRC)/common/mms/mms
CPPFLAGS +=	-I$(SRC)/cmd/mms/dm/common -I../../../mms/common
CPPFLAGS +=	-I$(SRC)/uts/common/io/mms/dmd

.KEEP_STATE:

all: $(LIBS)

lint: $(LINTLIB) lintcheck

include ../../Makefile.rootdirs

install: all $(ROOTLIBDIR) $(ROOTLIBS)
