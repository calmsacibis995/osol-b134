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
# Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#
# This Makefile builds
# the Intel Core Architecture Performance Counter BackEnd (PCBE).
#

UTSBASE = ../..

#
#	Define module and object file sets.
#
MODULE		= pcbe.GenuineIntel.6.15
OBJECTS		= $(CORE_PCBE_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(CORE_PCBE_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_PCBE_DIR)/$(MODULE)
SOFTLINKS	= pcbe.GenuineIntel.6.23 \
			pcbe.GenuineIntel.6.26 \
			pcbe.GenuineIntel.6.28 \
			pcbe.GenuineIntel.6.29 \
			pcbe.GenuineIntel.6.30 \
			pcbe.GenuineIntel.6.31 \
			pcbe.GenuineIntel.6.37 \
			pcbe.GenuineIntel.6.44 \
			pcbe.GenuineIntel.6.46
ROOTSOFTLINKS	= $(SOFTLINKS:%=$(USR_PCBE_DIR)/%)

#
#	Include common rules.
#
include $(UTSBASE)/intel/Makefile.intel

#
#	Define targets.
#
ALL_TARGET	= $(BINARY)
LINT_MODULE	= core_pcbe
LINT_TARGET	= $(LINT_MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOTSOFTLINKS)

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS)

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

$(ROOTSOFTLINKS):	$(ROOTMODULE)
	-$(RM) $@; $(SYMLINK) $(MODULE) $@

#
#	Include common targets.
#
include $(UTSBASE)/intel/Makefile.targ
