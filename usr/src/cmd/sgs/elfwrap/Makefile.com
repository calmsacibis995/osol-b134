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

PROG=		elfwrap

include		$(SRC)/cmd/Makefile.cmd
include		$(SRC)/cmd/sgs/Makefile.com

COMOBJ =	main.o

MACHOBJ =	machine.sparc.o	machine.sparcv9.o \
		machine.i386.o	machine.amd64.o

COMOBJ32 =	elfwrap32.o

COMOBJ64 =	elfwrap64.o

TOOLOBJS =	alist.o

BLTOBJ =	msg.o

OBJS=		$(BLTOBJ) $(COMOBJ) $(MACHOBJ) $(COMOBJ32) $(COMOBJ64) \
		$(TOOLOBJS)

MAPFILES =	$(MAPFILE.NGB)
MAPOPTS =	$(MAPFILES:%=-M%)

CPPFLAGS =	-I. -I../common -I../../include $(CPPFLAGS.master)
LLDFLAGS =
LLDFLAGS64 =
LDFLAGS +=	$(VERSREF) $(USE_PROTO) $(MAPOPTS) $(LLDFLAGS)
LDLIBS +=	$(ELFLIBDIR) -lelf $(CONVLIBDIR) $(CONV_LIB)

LINTFLAGS +=	-x
LINTFLAGS64 +=	-x

BLTDEFS =	msg.h
BLTDATA =	msg.c
BLTMESG =	$(SGSMSGDIR)/elfwrap

BLTFILES =	$(BLTDEFS) $(BLTDATA) $(BLTMESG)

SGSMSGCOM =	../common/elfwrap.msg
SGSMSGTARG =	$(SGSMSGCOM)
SGSMSGALL =	$(SGSMSGCOM)
SGSMSGFLAGS +=	-h $(BLTDEFS) -d $(BLTDATA) -m $(BLTMESG) -n elfwrap_msg

SRCS =		$(COMOBJ:%.o=../common/%.c) ../common/machine.c \
		$(COMOBJ32:%32.o=../common/%.c) \
		$(TOOLOBJ:%.o=../../tools/common/%.c) $(BLTDATA)
LINTSRCS =	$(SRCS) ../common/lintsup.c

CLEANFILES +=	$(LINTOUTS) $(BLTFILES)
