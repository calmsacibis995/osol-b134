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

include ../Makefile.master

# Bootstrap problem --
# 'cw' must be built before anything else can be built.

BOOT_SUBDIRS= \
	cw

COMMON_SUBDIRS= \
	bfuld \
	codereview \
	codesign \
	cscope-fast \
	ctf \
	depcheck \
	env \
	fastfs \
	findunref \
	ndrgen \
	onbld \
	pmodes \
	gk \
	install.bin \
	lintdump \
	protocmp \
	protolist \
	scripts

#
#  special versions of commands for use only in build
#
UNSHIPPED_SUBDIRS = \
	elfsign

sparc_SUBDIRS= \
	chk4ubin \
	stabs \
	tokenize

i386_SUBDIRS=		\
	aw		\
	elfextract	\
	mbh_patch

LINTSUBDIRS= \
	codereview \
	ctf \
	cw \
	findunref \
	lintdump \
	ndrgen \
	protocmp \
	protolist

SUBDIRS= \
	$($(MACH)_SUBDIRS) \
	$(COMMON_SUBDIRS) \
	$(UNSHIPPED_SUBDIRS)

#
# Packages built here
#
PKG_SUBDIRS= SUNWonbld

include Makefile.tools

ROOTDIRS= \
	$(ROOTOPT) \
	$(ROOTONBLD) \
	$(ROOTONBLD)/bin \
	$(ROOTONBLD)/bin/$(MACH) \
	$(ROOTONBLD)/lib \
	$(ROOTONBLD)/lib/$(MACH) \
	$(ROOTONBLD)/lib/perl \
	$(ROOTONBLD)/lib/python \
	$(ROOTONBLD)/lib/python/onbld \
	$(ROOTONBLD)/lib/python/onbld/Checks \
	$(ROOTONBLD)/lib/python/onbld/hgext \
	$(ROOTONBLD)/lib/python/onbld/Scm \
	$(ROOTONBLD)/env \
	$(ROOTONBLD)/etc \
	$(ROOTONBLD)/etc/exception_lists \
	$(ROOTONBLD)/gk \
	$(ROOTONBLD)/man \
	$(ROOTONBLD)/man/man1 \
	$(ROOTONBLD)/man/sman1

all :=		TARGET= install
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
_msg :=		TARGET= _msg

.KEEP_STATE:

all install: $(SUBDIRS)

clean: $(SUBDIRS)

clobber: $(SUBDIRS)
	$(RM) -rf $(TOOLS_PROTO)

pkg: install .WAIT $(PKG_SUBDIRS)

lint: $(LINTSUBDIRS)

_msg: $(MSGSUBDIRS)

.PARALLEL: $(SUBDIRS) $(PKG_SUBDIRS) $(CLOSED_SUBDIRS)

$(SUBDIRS) $(CLOSED_SUBDIRS): $(BOOT_SUBDIRS)

$(BOOT_SUBDIRS) $(SUBDIRS): $(ROOTDIRS) FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

$(PKG_SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) install

FRC:

$(ROOTDIRS):
	$(INS.dir)
