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
# lib/cfgadm_plugins/Makefile
#

include $(SRC)/Makefile.master

COMMON_SUBDIRS= scsi sdcard pci usb ib fp shp
sparc_SUBDIRS=	sbd ac sysctrl

i386_SUBDIRS= sata

SUBDIRS= $(COMMON_SUBDIRS) $($(MACH)_SUBDIRS)

ALL_SUBDIRS= $(COMMON_SUBDIRS) $(sparc_SUBDIRS) $(i386_SUBDIRS)

MSGSUBDIRS= $(ALL_SUBDIRS)

all:= 		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint
_msg:=		TARGET= _msg

.KEEP_STATE:

all clean clobber lint: $(SUBDIRS)

install: all $(SUBDIRS)

_msg: $(MSGSUBDIRS)

$(ALL_SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC: