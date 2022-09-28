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
#	Create default so empty rules don't
#	confuse make
#

LIBRARY		= libc_psr.a
VERS		= .1

include $(SRC)/lib/Makefile.lib
include $(SRC)/Makefile.psm

#
# Since libc_psr is strictly assembly, deactivate the CTF build logic.
#
CTFCONVERT_POST	= :
CTFMERGE_LIB	= :

LIBS		= $(DYNLIB)
IFLAGS		= -I$(SRC)/uts/$(PLATFORM) \
		  -I$(ROOT)/usr/platform/$(PLATFORM)/include
CPPFLAGS	= -D_REENTRANT -D$(MACH) $(IFLAGS) $(CPPFLAGS.master)
ASDEFS		= -D__STDC__ -D_ASM $(CPPFLAGS)
ASFLAGS		= -P $(ASDEFS)

MAPFILES	= ../../sun4u/mapfile-vers ../../sun4u/mapfile-memcpy \
			$(MAPFILE.FLT)

#
# Used when building links in /platform/$(PLATFORM)/lib
#
LINKED_PLATFORMS	= SUNW,Ultra-2
LINKED_PLATFORMS	+= SUNW,Ultra-4
LINKED_PLATFORMS	+= SUNW,Ultra-5_10
LINKED_PLATFORMS	+= SUNW,Ultra-30
LINKED_PLATFORMS	+= SUNW,Ultra-60
LINKED_PLATFORMS	+= SUNW,Ultra-80
LINKED_PLATFORMS	+= SUNW,Ultra-250
LINKED_PLATFORMS	+= SUNW,Ultra-Enterprise
LINKED_PLATFORMS	+= SUNW,Ultra-Enterprise-10000
LINKED_PLATFORMS	+= SUNW,UltraAX-i2
LINKED_PLATFORMS	+= SUNW,UltraSPARC-IIi-Netract
LINKED_PLATFORMS	+= SUNW,UltraSPARC-IIe-NetraCT-40
LINKED_PLATFORMS	+= SUNW,UltraSPARC-IIe-NetraCT-60
LINKED_PLATFORMS	+= SUNW,Sun-Blade-100
LINKED_PLATFORMS	+= SUNW,Serverblade1
LINKED_PLATFORMS	+= SUNW,Netra-CP2300

#
# install rule
#
$(ROOT_PSM_LIB_DIR)/%: % $(ROOT_PSM_LIB_DIR)
	$(INS.file)

#
# build rules
#
pics/%.o: ../../$(PLATFORM)/common/%.s
	$(AS) $(ASFLAGS) $< -o $@
	$(POST_PROCESS_O)
