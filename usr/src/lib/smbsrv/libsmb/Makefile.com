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

LIBRARY= libsmb.a
VERS= .1

OBJS_SHARED = 			\
	smb_common_door_decode.o 	\
	smb_inet.o		\
	smb_match.o 		\
	smb_msgbuf.o		\
	smb_native.o		\
	smb_oem.o		\
	smb_share_door_decode.o	\
	smb_sid.o		\
	smb_status_xlat.o	\
	smb_string.o 		\
	smb_token.o		\
	smb_token_xdr.o		\
	smb_utf8.o		\
	smb_xdr_utils.o

OBJS_COMMON = 			\
	smb_acl.o		\
	smb_api_door_calls.o	\
	smb_auth.o 		\
	smb_cfg.o		\
	smb_crypt.o		\
	smb_ctxbuf.o		\
	smb_domain.o		\
	smb_door_encdec.o	\
	smb_doorclnt.o		\
	smb_ht.o		\
	smb_idmap.o		\
	smb_info.o		\
	smb_kmod.o		\
	smb_list.o		\
	smb_lgrp.o		\
	smb_mac.o		\
	smb_nic.o		\
	smb_nicmon.o		\
	smb_pwdutil.o		\
	smb_privilege.o		\
	smb_sam.o		\
	smb_scfutil.o		\
	smb_sd.o		\
	smb_util.o		\
	smb_wksids.o

OBJECTS=	$(OBJS_COMMON) $(OBJS_SHARED)

include ../../../Makefile.lib
include ../../Makefile.lib

INCS += -I$(SRC)/common/smbsrv

LDLIBS +=	$(MACH_LDLIBS)
LDLIBS +=	-lscf -lmd -luuid -lnsl -lpkcs11 -lsec -lsocket -lresolv
LDLIBS +=	-lidmap -lavl -lc
CPPFLAGS +=	$(INCS) -D_REENTRANT

SRCS=   $(OBJS_COMMON:%.o=$(SRCDIR)/%.c)	\
	$(OBJS_SHARED:%.o=$(SRC)/common/smbsrv/%.c)

include ../../Makefile.targ
include ../../../Makefile.targ
