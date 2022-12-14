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

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= ibd
OBJECTS		= $(IBD_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(IBD_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/io/ib/clients/ibd
LDFLAGS		+= -dy -Nmisc/mac -Nmisc/ibtl -Nmisc/ibcm -Nmisc/ibmf -Ndrv/ip
WARLOCK_OUT     = $(IBD_OBJS:%.o=%.ll)
WARLOCK_OK	= $(MODULE).ok
WLCMD_DIR	= $(UTSBASE)/common/io/warlock
#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Define targets
#
ALL_TARGET	= $(BINARY) $(SRC_CONFFILE)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Overrides
#
ALL_BUILDS	= $(ALL_BUILDSONLY64)
DEF_BUILDS	= $(DEF_BUILDSONLY64)
CLEANLINTFILES	+= $(LINT32_FILES)

# 
# lint pass one enforcement 
# 
CFLAGS += $(CCVERBOSE)

#
# For now, disable these lint checks; maintainers should endeavor
# to investigate and remove these for maximum lint coverage.
# Please do not carry these forward to new Makefiles.
#
LINTTAGS	+= -erroff=E_BAD_PTR_CAST_ALIGN
LINTTAGS	+= -erroff=E_PTRDIFF_OVERFLOW

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)
		$(RM) $(WARLOCK_OUT) $(WARLOCK_OK)

clobber:	$(CLOBBER_DEPS)
		$(RM) $(WARLOCK_OUT) $(WARLOCK_OK)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS) lint32

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ

#
#       Defines for local commands.
#
WARLOCK         = warlock
WLCC            = wlcc
TOUCH           = touch
TEST            = test

warlock: $(WARLOCK_OK) $(WARLOCK_OUT)

$(WARLOCK_OK): $(WARLOCK_OUT) $(WLCMD_DIR)/ibd.wlcmd warlock_ddi.files
	$(WARLOCK) -c $(WLCMD_DIR)/ibd.wlcmd $(WARLOCK_OUT) \
		-l ../warlock/ddi_dki_impl.ll
	$(TOUCH) $@

%.ll: $(UTSBASE)/common/io/ib/clients/ibd/%.c \
	$(UTSBASE)/common/sys/ib/clients/ibd/ibd.h
	$(WLCC)  $(CPPFLAGS) -DDEBUG -o $@ $<

warlock_ddi.files:
	@cd ../warlock; pwd; $(MAKE) warlock
