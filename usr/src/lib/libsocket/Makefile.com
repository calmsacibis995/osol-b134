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

LIBRARY	=	libsocket.a
VERS =		.1

INETOBJS =	bindresvport.o bootparams_getbyname.o ether_addr.o \
	   	getaddrinfo.o getnameinfo.o getnetent.o getnetent_r.o \
		getprotoent.o getprotoent_r.o getservbyname_r.o getservent.o \
		getservent_r.o inet_lnaof.o inet_mkaddr.o inet_network.o \
		inet6_opt.o inet6_rthdr.o interface_id.o link_addr.o \
		netmasks.o rcmd.o rexec.o ruserpass.o sourcefilter.o
SOCKOBJS =	_soutil.o sockatmark.o socket.o socketpair.o weaks.o
OBJECTS	=	$(INETOBJS) $(SOCKOBJS)

include ../../Makefile.lib

# install this library in the root filesystem
include ../../Makefile.rootfs

LIBS =		$(DYNLIB) $(LINTLIB)

SRCS =		$(INETOBJS:%.o=../inet/%.c) $(SOCKOBJS:%.o=../socket/%.c)
LDLIBS +=	-lnsl -lc

SRCDIR =	../common
$(LINTLIB):=	SRCS = $(SRCDIR)/$(LINTSRC)

MAPFILES +=	mapfile-vers

# Make string literals read-only to save memory.
CFLAGS +=	$(XSTRCONST)
CFLAGS64 +=	$(XSTRCONST)

CPPFLAGS +=	-DSYSV -D_REENTRANT -I../../common/inc
%/rcmd.o :=	CPPFLAGS += -DNIS

.KEEP_STATE:

all:

lint:	lintcheck

# libsocket build rules
pics/%.o: ../inet/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: ../socket/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include ../../Makefile.targ
