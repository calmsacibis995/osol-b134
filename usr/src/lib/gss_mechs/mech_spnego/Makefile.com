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
# This make file will build mech_spnego.so.1. This shared object
# contains all the functionality needed to support the SPNEGO GSS-API
# mechanism. 
#

LIBRARY = 	mech_spnego.a
VERS = 		.1
OBJECTS = 	spnego_mech.o

# include library definitions
include ../../../Makefile.lib

LIBS = 		$(DYNLIB)
ROOTLIBDIR =	$(ROOT)/usr/lib/gss
ROOTLIBDIR64 = 	$(ROOT)/usr/lib/$(MACH64)/gss
SRCDIR =	../mech

MAPFILE_EXPORT = ../mapfile-vers-clean
$(EXPORT_RELEASE_BUILD)MAPFILE_EXPORT = \
		$(CLOSED)/lib/gss_mechs/mech_spnego/mapfile-vers-export
MAPFILES =	../mapfile-vers $(MAPFILE_EXPORT)

CPPFLAGS += -I$(SRC)/uts/common/gssapi/include $(DEBUG) -I$(SRC)/lib/gss_mechs/mech_krb5/include -I$(SRC)/uts/common/gssapi/mechs/krb5/include

MAKEFILE_EXPORT = $(CLOSED)/lib/gss_mechs/mech_spnego/Makefile.export
$(EXPORT_RELEASE_BUILD)include $(MAKEFILE_EXPORT)

.KEEP_STATE:

all: $(LIBS)

lint: lintcheck

# include library targets
include ../../../Makefile.targ

