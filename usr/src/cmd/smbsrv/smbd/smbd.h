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

#ifndef _SMBD_H
#define	_SMBD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <smbsrv/smb_ioctl.h>
#include <smbsrv/smb_token.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libmlsvc.h>

extern int smbd_opipe_dsrv_start(void);
extern void smbd_opipe_dsrv_stop(void);

extern int smb_share_dsrv_start(void);
extern void smb_share_dsrv_stop(void);

extern boolean_t smbd_set_netlogon_cred(void);
extern int smbd_locate_dc_start(void);

extern smb_token_t *smbd_user_auth_logon(netr_client_t *);
extern void smbd_user_nonauth_logon(uint32_t);
extern void smbd_user_auth_logoff(uint32_t);
extern uint32_t smbd_join(smb_joininfo_t *);

extern void smbd_set_secmode(int);

extern int smbd_vss_get_count(const char *, uint32_t *);
extern void smbd_vss_get_snapshots(const char *, uint32_t, uint32_t *,
    uint32_t *, char **);
extern int smbd_vss_map_gmttoken(const char *, char *, char *);

typedef struct smbd {
	const char	*s_version;	/* smbd version string */
	const char	*s_pname;	/* basename to use for messages */
	pid_t		s_pid;		/* process-ID of current daemon */
	uid_t		s_uid;		/* UID of current daemon */
	gid_t		s_gid;		/* GID of current daemon */
	int		s_fg;		/* Run in foreground */
	boolean_t	s_shutting_down; /* shutdown control */
	volatile uint_t	s_sigval;
	volatile uint_t	s_refreshes;
	boolean_t	s_kbound;	/* B_TRUE if bound to kernel */
	int		s_door_lmshr;
	int		s_door_srv;
	int		s_door_opipe;
	int		s_secmode;	/* Current security mode */
	boolean_t	s_nbt_listener_running;
	boolean_t	s_tcp_listener_running;
	pthread_t	s_nbt_listener_id;
	pthread_t	s_tcp_listener_id;
	boolean_t	s_fatal_error;
} smbd_t;

#ifdef __cplusplus
}
#endif

#endif /* _SMBD_H */
