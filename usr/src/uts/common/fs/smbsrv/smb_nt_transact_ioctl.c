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

#include <smbsrv/smb_kproto.h>
#include <smbsrv/winioctl.h>


static uint32_t smb_nt_trans_ioctl_noop(smb_request_t *, smb_xa_t *);
static uint32_t smb_nt_trans_ioctl_invalid_parm(smb_request_t *,
    smb_xa_t *);

/*
 * This table defines the list of FSCTL values for which we'll
 * call a funtion to perform specific processing.
 */
static struct {
	uint32_t fcode;
	uint32_t (*ioctl_func)(smb_request_t *sr, smb_xa_t *xa);
} ioctl_ret_tbl[] = {
	{ FSCTL_GET_OBJECT_ID, smb_nt_trans_ioctl_invalid_parm },
	{ FSCTL_QUERY_ALLOCATED_RANGES, smb_nt_trans_ioctl_invalid_parm },
	{ FSCTL_SRV_ENUMERATE_SNAPSHOTS, smb_vss_ioctl_enumerate_snaps },
	{ FSCTL_SET_SPARSE, smb_nt_trans_ioctl_noop }
};

/*
 * smb_nt_transact_ioctl
 *
 * This command allows device and file system control functions to be
 * transferred transparently from client to server.
 *
 * Setup Words Encoding        Description
 * =========================== =========================================
 * ULONG FunctionCode;         NT device or file system control code
 * USHORT Fid;                 Handle for io or fs control. Unless BIT0
 *                             of ISFLAGS is set.
 * BOOLEAN IsFsctl;            Indicates whether the command is a device
 *                             control (FALSE) or a file system control
 *                             (TRUE).
 * UCHAR   IsFlags;            BIT0 - command is to be applied to share
 *                             root handle. Share must be a DFS share.
 *
 * Data Block Encoding         Description
 * =========================== =========================================
 * Data[ TotalDataCount ]      Passed to the Fsctl or Ioctl
 *
 * Server Response             Description
 * =========================== ==================================
 * SetupCount                  1
 * Setup[0]                    Length of information returned by
 *                             io or fs control.
 * DataCount                   Length of information returned by
 *                             io or fs control.
 * Data[ DataCount ]           The results of the io or fs control.
 */
smb_sdrc_t
smb_nt_transact_ioctl(smb_request_t *sr, smb_xa_t *xa)
{
	uint32_t status = NT_STATUS_NOT_SUPPORTED;
	uint32_t fcode;
	unsigned short fid;
	unsigned char is_fsctl;
	unsigned char is_flags;
	int i;

	if (smb_mbc_decodef(&xa->req_setup_mb, "lwbb",
	    &fcode, &fid, &is_fsctl, &is_flags) != 0) {
		smbsr_error(sr, NT_STATUS_INVALID_PARAMETER, 0, 0);
		return (SDRC_ERROR);
	}

	/*
	 * Invoke handler if specified, otherwise the default
	 * behavior is to return NT_STATUS_NOT_SUPPORTED
	 */
	for (i = 0; i < sizeof (ioctl_ret_tbl) / sizeof (ioctl_ret_tbl[0]);
	    i++) {
		if (ioctl_ret_tbl[i].fcode == fcode) {
			status = ioctl_ret_tbl[i].ioctl_func(sr, xa);
			break;
		}
	}

	if (status != NT_STATUS_SUCCESS) {
		smbsr_error(sr, status, 0, 0);
		return (SDRC_ERROR);
	}

	(void) smb_mbc_encodef(&xa->rep_param_mb, "l", 0);
	return (SDRC_SUCCESS);
}

/* ARGSUSED */
static uint32_t
smb_nt_trans_ioctl_noop(smb_request_t *sr, smb_xa_t *xa)
{
	return (NT_STATUS_SUCCESS);
}

/* ARGSUSED */
static uint32_t
smb_nt_trans_ioctl_invalid_parm(smb_request_t *sr, smb_xa_t *xa)
{
	return (NT_STATUS_INVALID_PARAMETER);
}
