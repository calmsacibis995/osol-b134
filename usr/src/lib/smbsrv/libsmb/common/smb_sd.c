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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This module provides Security Descriptor handling functions.
 */

#include <strings.h>
#include <assert.h>
#include <smbsrv/ntifs.h>
#include <smbsrv/smb_idmap.h>
#include <smbsrv/ntstatus.h>
#include <smbsrv/libsmb.h>

#define	SMB_SHR_ACE_READ_PERMS	(ACE_READ_PERMS | ACE_EXECUTE | ACE_SYNCHRONIZE)
#define	SMB_SHR_ACE_CONTROL_PERMS	(ACE_MODIFY_PERMS & (~ACE_DELETE_CHILD))

#define	SMB_SHR_ACE_MODIFY_PERMS	(ACE_MODIFY_PERMS &		\
	(~(ACE_READ_DATA | ACE_READ_ATTRIBUTES | ACE_READ_NAMED_ATTRS |	\
	ACE_EXECUTE | ACE_DELETE_CHILD)))

static struct {
	int am_ace_perms;
	int am_share_perms;
} smb_ace_map[] = {
	{ ACE_ALL_PERMS,	ACE_ALL_PERMS },
	{ ACE_MODIFY_PERMS,	SMB_SHR_ACE_CONTROL_PERMS },
	{ ACE_MODIFY_PERMS,	SMB_SHR_ACE_MODIFY_PERMS },
	{ ACE_READ_PERMS,	SMB_SHR_ACE_READ_PERMS }
};

#define	SMB_ACE_MASK_MAP_SIZE	(sizeof (smb_ace_map)/sizeof (smb_ace_map[0]))

static void smb_sd_set_sacl(smb_sd_t *, smb_acl_t *, boolean_t, int);
static void smb_sd_set_dacl(smb_sd_t *, smb_acl_t *, boolean_t, int);

void
smb_sd_init(smb_sd_t *sd, uint8_t revision)
{
	bzero(sd, sizeof (smb_sd_t));
	sd->sd_revision = revision;
}

/*
 * smb_sd_term
 *
 * Free non-NULL members of 'sd' which has to be in
 * absolute (pointer) form.
 */
void
smb_sd_term(smb_sd_t *sd)
{
	assert(sd);
	assert((sd->sd_control & SE_SELF_RELATIVE) == 0);

	smb_sid_free(sd->sd_owner);
	smb_sid_free(sd->sd_group);
	smb_acl_free(sd->sd_dacl);
	smb_acl_free(sd->sd_sacl);

	bzero(sd, sizeof (smb_sd_t));
}

uint32_t
smb_sd_len(smb_sd_t *sd, uint32_t secinfo)
{
	uint32_t length = SMB_SD_HDRSIZE;

	if (secinfo & SMB_OWNER_SECINFO)
		length += smb_sid_len(sd->sd_owner);

	if (secinfo & SMB_GROUP_SECINFO)
		length += smb_sid_len(sd->sd_group);

	if (secinfo & SMB_DACL_SECINFO)
		length += smb_acl_len(sd->sd_dacl);

	if (secinfo & SMB_SACL_SECINFO)
		length += smb_acl_len(sd->sd_sacl);

	return (length);
}

/*
 * smb_sd_get_secinfo
 *
 * Return the security information mask for the specified security
 * descriptor.
 */
uint32_t
smb_sd_get_secinfo(smb_sd_t *sd)
{
	uint32_t sec_info = 0;

	if (sd == NULL)
		return (0);

	if (sd->sd_owner)
		sec_info |= SMB_OWNER_SECINFO;

	if (sd->sd_group)
		sec_info |= SMB_GROUP_SECINFO;

	if (sd->sd_dacl)
		sec_info |= SMB_DACL_SECINFO;

	if (sd->sd_sacl)
		sec_info |= SMB_SACL_SECINFO;

	return (sec_info);
}

/*
 * Adjust the Access Mask so that ZFS ACE mask and Windows ACE read mask match.
 */
static int
smb_sd_adjust_read_mask(int mask)
{
	int i;

	for (i = 0; i < SMB_ACE_MASK_MAP_SIZE; ++i) {
		if (smb_ace_map[i].am_ace_perms == mask)
			return (smb_ace_map[i].am_share_perms);
	}

	return (-1);
}

/*
 * Get ZFS acl from the share path via acl_get() method.
 */
static uint32_t
smb_sd_read_acl(char *path, smb_fssd_t *fs_sd)
{
	acl_t *z_acl;
	ace_t *z_ace;
	int mask;

	fs_sd->sd_gid = fs_sd->sd_uid = 0;
	if (acl_trivial(path) != 1)
		return (NT_STATUS_INTERNAL_ERROR);

	if (acl_get(path, ACL_NO_TRIVIAL, &z_acl) != 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if ((z_ace = (ace_t *)z_acl->acl_aclp) == NULL)
		return (NT_STATUS_INVALID_ACL);

	for (int i = 0; i < z_acl->acl_cnt; i++, z_ace++) {
		mask = smb_sd_adjust_read_mask(z_ace->a_access_mask);
		if (mask == -1)
			return (NT_STATUS_INVALID_ACL);
		z_ace->a_access_mask = mask;
	}

	fs_sd->sd_zdacl = z_acl;
	fs_sd->sd_zsacl = NULL;
	return (NT_STATUS_SUCCESS);
}

/*
 * smb_sd_read
 *
 * Reads ZFS acl from filesystem using acl_get() method. Convert the ZFS acl to
 * a Win SD and return the Win SD in absolute form.
 *
 * NOTE: upon successful return caller MUST free the memory allocated
 * for the returned SD by calling smb_sd_term().
 */
uint32_t
smb_sd_read(char *path, smb_sd_t *sd, uint32_t secinfo)
{
	smb_fssd_t fs_sd;
	uint32_t status = NT_STATUS_SUCCESS;
	uint32_t sd_flags;
	int error;

	sd_flags = SMB_FSSD_FLAGS_DIR;
	smb_fssd_init(&fs_sd, secinfo, sd_flags);

	error = smb_sd_read_acl(path, &fs_sd);
	if (error != NT_STATUS_SUCCESS) {
		smb_fssd_term(&fs_sd);
		return (error);
	}

	status = smb_sd_fromfs(&fs_sd, sd);
	smb_fssd_term(&fs_sd);

	return (status);
}

/*
 * Adjust the Access Mask so that ZFS ACE mask and Windows ACE write mask match.
 */
static int
smb_sd_adjust_write_mask(int mask)
{
	int i;

	for (i = 0; i < SMB_ACE_MASK_MAP_SIZE; ++i) {
		if (smb_ace_map[i].am_share_perms == mask)
			return (smb_ace_map[i].am_ace_perms);
	}

	return (-1);
}

/*
 * Apply ZFS acl to the share path via acl_set() method.
 */
static uint32_t
smb_sd_write_acl(char *path, smb_fssd_t *fs_sd)
{
	acl_t *z_acl;
	ace_t *z_ace;
	int mask;
	uint32_t status = NT_STATUS_SUCCESS;

	if ((z_acl = fs_sd->sd_zdacl) == NULL)
		return (NT_STATUS_INVALID_ACL);

	if ((z_ace = (ace_t *)z_acl->acl_aclp) == NULL)
		return (NT_STATUS_INVALID_ACL);

	for (int i = 0; i < z_acl->acl_cnt; i++, z_ace++) {
		mask = smb_sd_adjust_write_mask(z_ace->a_access_mask);
		if (mask == -1)
			return (NT_STATUS_INVALID_ACL);
		z_ace->a_access_mask = mask;
	}

	fs_sd->sd_gid = fs_sd->sd_uid = 0;
	if (acl_set(path, z_acl) != 0)
		status = NT_STATUS_INTERNAL_ERROR;

	return (status);
}

/*
 * smb_sd_write
 *
 * Takes a Win SD in absolute form, converts it to
 * ZFS acl and applies the acl to the share path via acl_set() method.
 */
uint32_t
smb_sd_write(char *path, smb_sd_t *sd, uint32_t secinfo)
{
	smb_fssd_t fs_sd;
	uint32_t status = NT_STATUS_SUCCESS;
	uint32_t sd_flags;
	int error;

	sd_flags = SMB_FSSD_FLAGS_DIR;
	smb_fssd_init(&fs_sd, secinfo, sd_flags);

	error = smb_sd_tofs(sd, &fs_sd);
	if (error != NT_STATUS_SUCCESS) {
		smb_fssd_term(&fs_sd);
		return (error);
	}

	status = smb_sd_write_acl(path, &fs_sd);
	smb_fssd_term(&fs_sd);

	return (status);
}

/*
 * smb_sd_tofs
 *
 * Creates a filesystem security structure based on the given
 * Windows security descriptor.
 */
uint32_t
smb_sd_tofs(smb_sd_t *sd, smb_fssd_t *fs_sd)
{
	smb_sid_t *sid;
	uint32_t status = NT_STATUS_SUCCESS;
	uint16_t sd_control;
	idmap_stat idm_stat;
	int idtype;
	int flags = 0;

	sd_control = sd->sd_control;

	/*
	 * ZFS only has one set of flags so for now only
	 * Windows DACL flags are taken into account.
	 */
	if (sd_control & SE_DACL_DEFAULTED)
		flags |= ACL_DEFAULTED;
	if (sd_control & SE_DACL_AUTO_INHERITED)
		flags |= ACL_AUTO_INHERIT;
	if (sd_control & SE_DACL_PROTECTED)
		flags |= ACL_PROTECTED;

	if (fs_sd->sd_flags & SMB_FSSD_FLAGS_DIR)
		flags |= ACL_IS_DIR;

	/* Owner */
	if (fs_sd->sd_secinfo & SMB_OWNER_SECINFO) {
		sid = sd->sd_owner;
		if (!smb_sid_isvalid(sid))
			return (NT_STATUS_INVALID_SID);

		idtype = SMB_IDMAP_USER;
		idm_stat = smb_idmap_getid(sid, &fs_sd->sd_uid, &idtype);
		if (idm_stat != IDMAP_SUCCESS) {
			return (NT_STATUS_NONE_MAPPED);
		}
	}

	/* Group */
	if (fs_sd->sd_secinfo & SMB_GROUP_SECINFO) {
		sid = sd->sd_group;
		if (!smb_sid_isvalid(sid))
			return (NT_STATUS_INVALID_SID);

		idtype = SMB_IDMAP_GROUP;
		idm_stat = smb_idmap_getid(sid, &fs_sd->sd_gid, &idtype);
		if (idm_stat != IDMAP_SUCCESS) {
			return (NT_STATUS_NONE_MAPPED);
		}
	}

	/* DACL */
	if (fs_sd->sd_secinfo & SMB_DACL_SECINFO) {
		if (sd->sd_control & SE_DACL_PRESENT) {
			status = smb_acl_to_zfs(sd->sd_dacl, flags,
			    SMB_DACL_SECINFO, &fs_sd->sd_zdacl);
			if (status != NT_STATUS_SUCCESS)
				return (status);
		}
		else
			return (NT_STATUS_INVALID_ACL);
	}

	/* SACL */
	if (fs_sd->sd_secinfo & SMB_SACL_SECINFO) {
		if (sd->sd_control & SE_SACL_PRESENT) {
			status = smb_acl_to_zfs(sd->sd_sacl, flags,
			    SMB_SACL_SECINFO, &fs_sd->sd_zsacl);
			if (status != NT_STATUS_SUCCESS) {
				return (status);
			}
		} else {
			return (NT_STATUS_INVALID_ACL);
		}
	}

	return (status);
}

/*
 * smb_sd_fromfs
 *
 * Makes an Windows style security descriptor in absolute form
 * based on the given filesystem security information.
 *
 * Should call smb_sd_term() for the returned sd to free allocated
 * members.
 */
uint32_t
smb_sd_fromfs(smb_fssd_t *fs_sd, smb_sd_t *sd)
{
	uint32_t status = NT_STATUS_SUCCESS;
	smb_acl_t *acl = NULL;
	smb_sid_t *sid;
	idmap_stat idm_stat;

	assert(fs_sd);
	assert(sd);

	smb_sd_init(sd, SECURITY_DESCRIPTOR_REVISION);

	/* Owner */
	if (fs_sd->sd_secinfo & SMB_OWNER_SECINFO) {
		idm_stat = smb_idmap_getsid(fs_sd->sd_uid,
		    SMB_IDMAP_USER, &sid);

		if (idm_stat != IDMAP_SUCCESS) {
			smb_sd_term(sd);
			return (NT_STATUS_NONE_MAPPED);
		}

		sd->sd_owner = sid;
	}

	/* Group */
	if (fs_sd->sd_secinfo & SMB_GROUP_SECINFO) {
		idm_stat = smb_idmap_getsid(fs_sd->sd_gid,
		    SMB_IDMAP_GROUP, &sid);

		if (idm_stat != IDMAP_SUCCESS) {
			smb_sd_term(sd);
			return (NT_STATUS_NONE_MAPPED);
		}

		sd->sd_group = sid;
	}

	/* DACL */
	if (fs_sd->sd_secinfo & SMB_DACL_SECINFO) {
		if (fs_sd->sd_zdacl != NULL) {
			acl = smb_acl_from_zfs(fs_sd->sd_zdacl);
			if (acl == NULL) {
				smb_sd_term(sd);
				return (NT_STATUS_INTERNAL_ERROR);
			}

			/*
			 * Need to sort the ACL before send it to Windows
			 * clients. Winodws GUI is sensitive about the order
			 * of ACEs.
			 */
			smb_acl_sort(acl);
			smb_sd_set_dacl(sd, acl, B_TRUE,
			    fs_sd->sd_zdacl->acl_flags);
		} else {
			smb_sd_set_dacl(sd, NULL, B_FALSE, 0);
		}
	}

	/* SACL */
	if (fs_sd->sd_secinfo & SMB_SACL_SECINFO) {
		if (fs_sd->sd_zsacl != NULL) {
			acl = smb_acl_from_zfs(fs_sd->sd_zsacl);
			if (acl == NULL) {
				smb_sd_term(sd);
				return (NT_STATUS_INTERNAL_ERROR);
			}

			smb_sd_set_sacl(sd, acl, B_TRUE,
			    fs_sd->sd_zsacl->acl_flags);
		} else {
			smb_sd_set_sacl(sd, NULL, B_FALSE, 0);
		}
	}

	return (status);
}

static void
smb_sd_set_dacl(smb_sd_t *sd, smb_acl_t *acl, boolean_t present, int flags)
{
	assert((sd->sd_control & SE_SELF_RELATIVE) == 0);

	sd->sd_dacl = acl;

	if (flags & ACL_DEFAULTED)
		sd->sd_control |= SE_DACL_DEFAULTED;
	if (flags & ACL_AUTO_INHERIT)
		sd->sd_control |= SE_DACL_AUTO_INHERITED;
	if (flags & ACL_PROTECTED)
		sd->sd_control |= SE_DACL_PROTECTED;

	if (present)
		sd->sd_control |= SE_DACL_PRESENT;
}

static void
smb_sd_set_sacl(smb_sd_t *sd, smb_acl_t *acl, boolean_t present, int flags)
{
	assert((sd->sd_control & SE_SELF_RELATIVE) == 0);

	sd->sd_sacl = acl;

	if (flags & ACL_DEFAULTED)
		sd->sd_control |= SE_SACL_DEFAULTED;
	if (flags & ACL_AUTO_INHERIT)
		sd->sd_control |= SE_SACL_AUTO_INHERITED;
	if (flags & ACL_PROTECTED)
		sd->sd_control |= SE_SACL_PROTECTED;

	if (present)
		sd->sd_control |= SE_SACL_PRESENT;
}

/*
 * smb_fssd_init
 *
 * Initializes the given FS SD structure.
 */
void
smb_fssd_init(smb_fssd_t *fs_sd, uint32_t secinfo, uint32_t flags)
{
	bzero(fs_sd, sizeof (smb_fssd_t));
	fs_sd->sd_secinfo = secinfo;
	fs_sd->sd_flags = flags;
}

/*
 * smb_fssd_term
 *
 * Frees allocated memory for acl fields.
 */
void
smb_fssd_term(smb_fssd_t *fs_sd)
{
	assert(fs_sd);

	acl_free(fs_sd->sd_zdacl);
	acl_free(fs_sd->sd_zsacl);

	bzero(fs_sd, sizeof (smb_fssd_t));
}
