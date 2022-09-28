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

/*
 * This file was originally generated using rpcgen.
 */

#ifndef _KERNEL
#include <stdlib.h>
#endif /* !_KERNEL */
#include <smbsrv/wintypes.h>
#include <smbsrv/smb_sid.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/smb_token.h>

static bool_t xdr_smb_privset_t(XDR *, smb_privset_t *);
static bool_t xdr_smb_sid_t(XDR *, smb_sid_t *);

static bool_t
xdr_smb_privset_helper(xdrs, privs)
	XDR *xdrs;
	char **privs;
{
	uint32_t pos, len;
	uint32_t cnt;
	bool_t rc;
	smb_privset_t *p;

	if (xdrs->x_op == XDR_DECODE) {
		pos = xdr_getpos(xdrs);

		if (!xdr_bool(xdrs, &rc))
			return (FALSE);

		if (!xdr_uint32_t(xdrs, &cnt))
			return (FALSE);

		rc = xdr_setpos(xdrs, pos);

		if (rc == FALSE)
			return (FALSE);
	} else {
		if (*privs == NULL)
			return (FALSE);

		p = (smb_privset_t *)(uintptr_t)*privs;
		cnt = p->priv_cnt;
	}

	len = sizeof (smb_privset_t)
	    - sizeof (smb_luid_attrs_t)
	    + (cnt * sizeof (smb_luid_attrs_t));

	if (!xdr_pointer(xdrs, privs, len, (xdrproc_t)xdr_smb_privset_t))
		return (FALSE);

	return (TRUE);
}

static bool_t
xdr_smb_id_t(xdrs, objp)
	XDR *xdrs;
	smb_id_t *objp;
{
	uint8_t len;

	if ((xdrs->x_op == XDR_ENCODE) || (xdrs->x_op == XDR_FREE))
		len = smb_sid_len(objp->i_sid);

	if (!xdr_uint32_t(xdrs, &objp->i_attrs))
		return (FALSE);

	if (!xdr_uint8_t(xdrs, &len))
		return (FALSE);

	if (!xdr_pointer(xdrs, (char **)&objp->i_sid, len,
	    (xdrproc_t)xdr_smb_sid_t))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, (uint32_t *)&objp->i_id))
		return (FALSE);

	return (TRUE);
}

static bool_t
xdr_smb_ids_t(xdrs, objp)
	XDR *xdrs;
	smb_ids_t *objp;
{
	if (!xdr_array(xdrs, (char **)&objp->i_ids, (uint32_t *)&objp->i_cnt,
	    ~0, sizeof (smb_id_t), (xdrproc_t)xdr_smb_id_t))
		return (FALSE);

	return (TRUE);
}

static bool_t
xdr_smb_posix_grps_t(xdrs, objp)
	XDR *xdrs;
	smb_posix_grps_t *objp;
{
	if (!xdr_uint32_t(xdrs, &objp->pg_ngrps))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->pg_grps, objp->pg_ngrps,
		sizeof (uint32_t), (xdrproc_t)xdr_uint32_t))
		return (FALSE);
	return (TRUE);
}

static bool_t
xdr_smb_posix_grps_helper(xdrs, identity)
	XDR *xdrs;
	char **identity;
{
	uint32_t pos, len;
	uint32_t cnt;
	bool_t rc;

	if (xdrs->x_op == XDR_DECODE) {
		pos = xdr_getpos(xdrs);

		if (!xdr_bool(xdrs, &rc))
			return (FALSE);

		if (!xdr_uint32_t(xdrs, &cnt))
			return (FALSE);

		rc = xdr_setpos(xdrs, pos);
		if (rc == FALSE)
			return (FALSE);
	} else {
		if (*identity == NULL)
			return (FALSE);
		cnt = ((smb_posix_grps_t *)(uintptr_t)*identity)->pg_ngrps;
	}

	len = SMB_POSIX_GRPS_SIZE(cnt);

	if (!xdr_pointer(xdrs, identity, len, (xdrproc_t)xdr_smb_posix_grps_t))
		return (FALSE);
	return (TRUE);
}

static bool_t
xdr_smb_session_key_t(xdrs, objp)
	XDR *xdrs;
	smb_session_key_t *objp;
{
	if (!xdr_vector(xdrs, (char *)objp->data, 16,
	    sizeof (uint8_t), (xdrproc_t)xdr_uint8_t))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_netr_client_t(xdrs, objp)
	XDR *xdrs;
	netr_client_t *objp;
{
	if (!xdr_uint16_t(xdrs, &objp->logon_level))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->username, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->domain, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->e_username, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->e_domain, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->workstation, ~0))
		return (FALSE);
	if (!xdr_smb_inaddr_t(xdrs, &objp->ipaddr))
		return (FALSE);
	if (!xdr_array(xdrs, (char **)&objp->challenge_key.challenge_key_val,
	    (uint32_t *)&objp->challenge_key.challenge_key_len, ~0,
	    sizeof (uint8_t), (xdrproc_t)xdr_uint8_t))
		return (FALSE);
	if (!xdr_array(xdrs, (char **)&objp->nt_password.nt_password_val,
	    (uint32_t *)&objp->nt_password.nt_password_len, ~0,
	    sizeof (uint8_t), (xdrproc_t)xdr_uint8_t))
		return (FALSE);
	if (!xdr_array(xdrs, (char **)&objp->lm_password.lm_password_val,
	    (uint32_t *)&objp->lm_password.lm_password_len, ~0,
	    sizeof (uint8_t), (xdrproc_t)xdr_uint8_t))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->logon_id))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->native_os))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->native_lm))
		return (FALSE);
	if (!xdr_smb_inaddr_t(xdrs, &objp->local_ipaddr))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->local_port))
		return (FALSE);
	return (TRUE);
}

static bool_t
xdr_smb_sid_t(xdrs, objp)
	XDR *xdrs;
	smb_sid_t *objp;
{
	if (!xdr_uint8_t(xdrs, &objp->sid_revision))
		return (FALSE);
	if (!xdr_uint8_t(xdrs, &objp->sid_subauthcnt))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->sid_authority, NT_SID_AUTH_MAX,
	    sizeof (uint8_t), (xdrproc_t)xdr_uint8_t))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->sid_subauth, objp->sid_subauthcnt,
	    sizeof (uint32_t), (xdrproc_t)xdr_uint32_t))
		return (FALSE);
	return (TRUE);
}

static bool_t
xdr_smb_luid_t(xdrs, objp)
	XDR *xdrs;
	smb_luid_t *objp;
{
	if (!xdr_uint32_t(xdrs, &objp->lo_part))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->hi_part))
		return (FALSE);
	return (TRUE);
}

static bool_t
xdr_smb_luid_attrs_t(xdrs, objp)
	XDR *xdrs;
	smb_luid_attrs_t *objp;
{
	if (!xdr_smb_luid_t(xdrs, &objp->luid))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->attrs))
		return (FALSE);
	return (TRUE);
}

static bool_t
xdr_smb_privset_t(xdrs, objp)
	XDR *xdrs;
	smb_privset_t *objp;
{
	if (!xdr_uint32_t(xdrs, &objp->priv_cnt))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->control))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->priv, objp->priv_cnt,
	    sizeof (smb_luid_attrs_t),
	    (xdrproc_t)xdr_smb_luid_attrs_t))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_smb_token_t(xdrs, objp)
	XDR *xdrs;
	smb_token_t *objp;
{
	if (!xdr_smb_id_t(xdrs, &objp->tkn_user))
		return (FALSE);
	if (!xdr_smb_id_t(xdrs, &objp->tkn_owner))
		return (FALSE);
	if (!xdr_smb_id_t(xdrs, &objp->tkn_primary_grp))
		return (FALSE);
	if (!xdr_smb_ids_t(xdrs, &objp->tkn_win_grps))
		return (FALSE);
	if (!xdr_smb_privset_helper(xdrs, (char **)&objp->tkn_privileges))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->tkn_account_name, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->tkn_domain_name, ~0))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->tkn_flags))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->tkn_audit_sid))
		return (FALSE);
	if (!xdr_pointer(xdrs, (char **)&objp->tkn_session_key,
	    sizeof (smb_session_key_t), (xdrproc_t)xdr_smb_session_key_t))
		return (FALSE);
	if (!xdr_smb_posix_grps_helper(xdrs, (char **)&objp->tkn_posix_grps))
		return (FALSE);
	return (TRUE);
}
