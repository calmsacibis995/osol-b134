/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SMB_TOKEN_H
#define	_SMB_TOKEN_H

#include <smbsrv/netrauth.h>
#include <smbsrv/smb_privilege.h>
#include <smbsrv/smb_sid.h>
#include <smbsrv/smb_xdr.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * User Session Key
 *
 * This is part of the MAC key which is required for signing SMB messages.
 */
typedef struct smb_session_key {
	uint8_t data[16];
} smb_session_key_t;

/*
 * Access Token
 *
 * An access token identifies a user, the user's privileges and the
 * list of groups of which the user is a member. This information is
 * used when access is requested to an object by comparing this
 * information with the DACL in the object's security descriptor.
 *
 * There should be one unique token per user per session per client.
 *
 * Access Token Flags
 *
 * SMB_ATF_GUEST	Token belongs to guest user
 * SMB_ATF_ANON		Token belongs to anonymous user
 * 			and it's only good for IPC Connection.
 * SMB_ATF_POWERUSER	Token belongs to a Power User member
 * SMB_ATF_BACKUPOP	Token belongs to a Power User member
 * SMB_ATF_ADMIN	Token belongs to a Domain Admins member
 */
#define	SMB_ATF_GUEST		0x00000001
#define	SMB_ATF_ANON		0x00000002
#define	SMB_ATF_POWERUSER	0x00000004
#define	SMB_ATF_BACKUPOP	0x00000008
#define	SMB_ATF_ADMIN		0x00000010

#define	SMB_POSIX_GRPS_SIZE(n) \
	(sizeof (smb_posix_grps_t) + (n - 1) * sizeof (gid_t))
/*
 * It consists of the primary and supplementary POSIX groups.
 */
typedef struct smb_posix_grps {
	uint32_t	pg_ngrps;
	gid_t		pg_grps[ANY_SIZE_ARRAY];
} smb_posix_grps_t;

typedef struct smb_token {
	smb_id_t	tkn_user;
	smb_id_t	tkn_owner;
	smb_id_t	tkn_primary_grp;
	smb_ids_t	tkn_win_grps;
	smb_privset_t	*tkn_privileges;
	char		*tkn_account_name;
	char		*tkn_domain_name;
	uint32_t	tkn_flags;
	uint32_t	tkn_audit_sid;
	smb_session_key_t *tkn_session_key;
	smb_posix_grps_t *tkn_posix_grps;
} smb_token_t;

/* XDR routines */
extern bool_t xdr_netr_client_t();
extern bool_t xdr_smb_token_t();


#ifndef _KERNEL
smb_token_t *smb_logon(netr_client_t *clnt);
void smb_token_destroy(smb_token_t *token);
uint8_t *smb_token_mkselfrel(smb_token_t *obj, uint32_t *len);
netr_client_t *netr_client_mkabsolute(uint8_t *buf, uint32_t len);
void netr_client_xfree(netr_client_t *);
void smb_token_log(smb_token_t *token);
#else /* _KERNEL */
smb_token_t *smb_token_mkabsolute(uint8_t *buf, uint32_t len);
void smb_token_free(smb_token_t *token);
uint8_t *netr_client_mkselfrel(netr_client_t *obj, uint32_t *len);
#endif /* _KERNEL */

int smb_token_query_privilege(smb_token_t *token, int priv_id);

#ifdef __cplusplus
}
#endif


#endif /* _SMB_TOKEN_H */
