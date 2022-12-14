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

FSTYPE	  =	nfs
TYPEPROG  =	mountd
ATTMK	  =	$(TYPEPROG)

include		../../Makefile.fstype

COMMON	  =	$(FSLIB) nfs_sec.o sharetab.o daemon.o
LOCAL	  =	mountd.o netgroup.o rmtab.o nfsauth.o \
		nfsauth_xdr.o exportlist.o hashset.o nfs_cmd.o
OBJS	  = 	$(LOCAL) $(COMMON)
SRCS	  =	$(LOCAL:%.o=%.c) $(FSLIBSRC) ../lib/nfs_sec.c \
		../lib/sharetab.c ../lib/daemon.c
DSRC	  =	mountd_dt.d
DOBJ	  =	$(DSRC:%.d=%.o)
LDLIBS	 +=	-lrpcsvc -lnsl -lbsm -lsocket -ltsnet -ltsol
CPPFLAGS +=	-D_REENTRANT

$(TYPEPROG):	$(OBJS)
		$(COMPILE.d) -s $(DSRC) -o $(DOBJ) $(OBJS)
		$(LINK.c) -o $@ $(DOBJ) $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

nfs_sec.o:	../lib/nfs_sec.c
		$(COMPILE.c) ../lib/nfs_sec.c

sharetab.o:	../lib/sharetab.c
		$(COMPILE.c) ../lib/sharetab.c

daemon.o:	../lib/daemon.c
		$(COMPILE.c) ../lib/daemon.c

lint:
	$(LINT.c) $(SRCS) $(LDLIBS)

clean:     
	$(RM) $(OBJS) $(DOBJ)
