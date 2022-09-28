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

#ifndef	_SMBSRV_SMB_DOOR_SVC_H
#define	_SMBSRV_SMB_DOOR_SVC_H

#include <sys/door.h>
#include <smbsrv/smb_token.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SMB door service (user-space and kernel-space)
 */
#define	SMB_DR_SVC_NAME		"/var/run/smbd_door"
#define	SMB_DR_SVC_VERSION	1
#define	SMB_DR_SVC_COOKIE	((void*)(0xdeadbeef^SMB_DR_SVC_VERSION))

/*
 * Door argument buffer starts off by the four-byte opcode.
 * Door result buffer starts off by the four-byte status.
 * The real data starts at offset 4 of the door buffer.
 */
#define	SMB_DR_DATA_OFFSET	4

/*
 * A smb_dr_op_t exists for each user-space door operation.
 * A smb_kdr_op_t exists for each kernel-space door operation.
 *
 * The first argument to smb_dr_op_t/smb_kdr_op_t is a pointer to the
 * door argument buffer. The second argument indicates the size of
 * the door argument buffer.
 *
 * The user-space door server accepts file descriptors from clients.
 * Thus, door_desc_t and n_desc can be passed to any smb_dr_op_t operation.
 *
 * Returns door result buffer and its size 'rbufsize' upon success.
 * Otherwise, NULL pointer will be returned and appropriate error code
 * will be set.
 */
typedef char *(*smb_dr_op_t)(char *, size_t, door_desc_t *,
    uint_t, size_t *, int *);
typedef char *(*smb_kdr_op_t)(char *, size_t, size_t *, int *);

extern smb_dr_op_t smb_doorsrv_optab[];

/*
 * Door Opcode
 * -------------
 * smb_dr_opcode_t - opcodes for user-space door operations.
 * smb_kdr_opcode_t - opcodes for kernel-space door operations.
 */
enum smb_dr_opcode_t {
	SMB_DR_USER_AUTH_LOGON,
	SMB_DR_USER_NONAUTH_LOGON,
	SMB_DR_USER_AUTH_LOGOFF,
	SMB_DR_LOOKUP_SID,
	SMB_DR_LOOKUP_NAME,
	SMB_DR_JOIN,
	SMB_DR_GET_DCINFO,
	SMB_DR_VSS_GET_COUNT,
	SMB_DR_VSS_GET_SNAPSHOTS,
	SMB_DR_VSS_MAP_GMTTOKEN,
	SMB_DR_ADS_FIND_HOST
};

/*
 * Door result status
 * SMB door servers will pass the following result status along with the
 * requested data back to the clients.
 */
#define	SMB_DR_OP_SUCCESS		0
#define	SMB_DR_OP_ERR			1
#define	SMB_DR_OP_ERR_DECODE		2
#define	SMB_DR_OP_ERR_ENCODE		3
#define	SMB_DR_OP_ERR_EMPTYBUF		4
#define	SMB_DR_OP_ERR_INVALID_OPCODE	5

#ifdef _KERNEL
/*
 * The 2nd argument of the smb_kdoor_srv_callback will be of the
 * following data structure type.
 *
 * rbuf - The pointer to a dynamically allocated door result buffer that
 *	  is required to be freed after the kernel completes the copyout
 *	  operation.
 */
typedef struct smb_kdoor_cb_arg {
	char	*rbuf;
	size_t	rbuf_size;
} smb_kdoor_cb_arg_t;

/*
 * SMB kernel door client
 * ------------------------
 * NOTE: smb_kdoor_clnt_init()/smb_kdoor_clnt_fini() are noops.
 */
void smb_kdoor_clnt_init(void);
void smb_kdoor_clnt_fini(void);
int smb_kdoor_clnt_open(int);
void smb_kdoor_clnt_close(void);
char *smb_kdoor_clnt_upcall(char *, size_t, door_desc_t *, uint_t, size_t *);
void smb_kdoor_clnt_free(char *, size_t, char *, size_t);

/*
 * SMB upcalls
 */
smb_token_t *smb_get_token(netr_client_t *);
void smb_user_nonauth_logon(uint32_t);
void smb_user_auth_logoff(uint32_t);
uint32_t smb_upcall_vss_get_count(char *);
void smb_upcall_vss_get_snapshots(char *, uint32_t,
    smb_dr_return_gmttokens_t *);
void smb_upcall_vss_get_snapshots_free(smb_dr_return_gmttokens_t *);
void smb_upcall_vss_map_gmttoken(char *, char *, char *);
#else /* _KERNEL */

/*
 * SMB user-space door server
 */
int smb_door_srv_start(void);
void smb_door_srv_stop(void);

int smb_dr_is_valid_opcode(int);

/*
 * SMB user-space door client
 */
int smb_dr_clnt_call(int, door_arg_t *);
void smb_dr_clnt_setup(door_arg_t *, char *, size_t);
void smb_dr_clnt_cleanup(door_arg_t *);

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SMBSRV_SMB_DOOR_SVC_H */
