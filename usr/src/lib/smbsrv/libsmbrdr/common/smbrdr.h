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

#ifndef _SMBRDR_H_
#define	_SMBRDR_H_

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <synch.h>
#include <sys/types.h>

#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbrdr.h>
#include <smbsrv/smb.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/smb.h>
#include <smbsrv/wintypes.h>

#define	SMBRDR_REQ_BUFSZ	4096

#define	MAX_ACCOUNT_NAME	32
#define	MAX_SHARE_NAME		32
#define	MAX_SCOPE_NAME		64
#define	MAX_FILE_PATH		128

/*
 * The number of shares and pipes is limited to 48 based on the note
 * below. This really shouldn't cause a problem because we always
 * our shares and named pipes are always opened and closed round every
 * RPC transaction. This also tends to limit the number of active
 * logons because we (currently) need two named pipes per logon.
 *
 * Q141709 Limit of 49 named pipe connections from a single workstation.
 * If a named pipe server creates more than 49 distincly named pipes, a
 * single client cannot connect more than 49 pipes on the named pipe
 * server. Chapter 4, p113. Network Programming for Microsoft Windows
 * Anthony Jones and Jim Ohlund, Microsoft Press, ISBN: 0-7356-0560-2
 */
#define	N_NETUSE_TABLE		256
#define	N_OFILE_TABLE		256

/*
 * Logon's states
 */
#define	SDB_LSTATE_START	0
#define	SDB_LSTATE_INIT		1
#define	SDB_LSTATE_LOGGING_OFF	2
#define	SDB_LSTATE_SETUP	3

#define	SDB_LOGON_NONE		0
#define	SDB_LOGON_GUEST		1
#define	SDB_LOGON_ANONYMOUS	2
#define	SDB_LOGON_USER		3

typedef struct sdb_logon {
	struct sdb_session *session;
	char username[MAX_ACCOUNT_NAME];
	unsigned short uid;
	unsigned int type;
	unsigned short state;
	smb_auth_info_t auth;
	unsigned char ssn_key[SMBAUTH_SESSION_KEY_SZ];
} sdb_logon_t;

/*
 * Session's states
 *
 *   SDB_SSTATE_START             ready to be used
 *   SDB_SSTATE_INIT              initialized
 *   SDB_SSTATE_STALE             lost transport connection
 *   SDB_SSTATE_DISCONNECTING     disconnecting: logoff the user
 *                                disconnect trees, close files
 *   SDB_SSTATE_CLEANING          was in STALE state now just
 *                                cleaning up
 *   SDB_SSTATE_CONNECTED         got transport connection
 *   SDB_SSTATE_NEGOTIATED        did SMB negotiate
 */
#define	SDB_SSTATE_START		0
#define	SDB_SSTATE_INIT			1
#define	SDB_SSTATE_STALE		2
#define	SDB_SSTATE_DISCONNECTING	3
#define	SDB_SSTATE_CLEANING		4
#define	SDB_SSTATE_CONNECTED		5
#define	SDB_SSTATE_NEGOTIATED		6

#define	SDB_SLCK_READ   1
#define	SDB_SLCK_WRITE  2

struct sdb_session {
	char srv_name[MAXHOSTNAMELEN];
	smb_inaddr_t srv_ipaddr;
	char domain[MAXHOSTNAMELEN];
	char scope[SMB_PI_MAX_SCOPE];
	char native_os[SMB_PI_MAX_NATIVE_OS];
	char native_lanman[SMB_PI_MAX_LANMAN];
	int sock;
	short port;
	uint16_t secmode;
	uint32_t sesskey;
	uint32_t challenge_len;
	uint8_t challenge_key[32];
	uint8_t smb_flags;
	uint16_t smb_flags2;
	uint16_t vc;
	uint32_t remote_caps;
	uint8_t state;
	uint32_t sid;	/* session id */
	int remote_os;
	int remote_lm;
	int pdc_type;
	smb_sign_ctx_t sign_ctx;
	sdb_logon_t logon;
	rwlock_t rwl;
};

/*
 * Netuse's states
 */
#define	SDB_NSTATE_START		0
#define	SDB_NSTATE_INIT			1
#define	SDB_NSTATE_DISCONNECTING	2
#define	SDB_NSTATE_CONNECTED		3

struct sdb_netuse {
	struct sdb_session *session;
	unsigned short state;
	int letter;		/* local identity */
	unsigned int sid;
	unsigned short uid;
	unsigned short tid;		/* remote identity */
	char share[MAX_SHARE_NAME];
	mutex_t mtx;
};

/*
 * Ofile's states
 */
#define	SDB_FSTATE_START	0
#define	SDB_FSTATE_INIT		1
#define	SDB_FSTATE_CLOSING	2
#define	SDB_FSTATE_OPEN		3

struct sdb_ofile {
	struct sdb_session *session;
	struct sdb_netuse *netuse;
	unsigned short state;
	unsigned int sid;
	unsigned short uid;
	unsigned short tid;
	unsigned short fid;		/* remote identity */
	char path[MAX_FILE_PATH];
	mutex_t mtx;
};

typedef struct smbrdr_handle {
	unsigned char *srh_buf;
	smb_msgbuf_t srh_mbuf;
	unsigned int srh_mbflags;
	unsigned char srh_cmd;
	struct sdb_session *srh_session;
	struct sdb_logon *srh_user;
	struct sdb_netuse *srh_tree;
} smbrdr_handle_t;

typedef struct smb_nt_negotiate_rsp {
	uint8_t word_count;
	uint16_t dialect_index;
	uint8_t security_mode;
	uint16_t max_mpx;
	uint16_t max_vc;
	uint32_t max_buffer_size;
	uint32_t max_raw_size;
	uint32_t session_key;
	uint32_t capabilities;
	uint32_t time_low;
	uint32_t time_high;
	uint16_t server_tz;
	uint8_t security_len;
	uint16_t byte_count;
	uint8_t *guid;
	uint8_t *challenge;
	uint8_t *oem_domain;
} smb_nt_negotiate_rsp_t;

/*
 * SMB_COM_TRANSACTION
 */
typedef struct smb_transact_rsp {
	uint8_t WordCount;		/* Count of data bytes */
					/* value = 10 + SetupCount */
	uint16_t TotalParamCount;	/* Total parameter bytes being sent */
	uint16_t TotalDataCount;	/* Total data bytes being sent */
	uint16_t Reserved;
	uint16_t ParamCount;		/* Parameter bytes sent this buffer */
	uint16_t ParamOffset;		/* Offset (from hdr start) to params */
	uint16_t ParamDisplacement;	/* Displacement of these param bytes */
	uint16_t DataCount;		/* Data bytes sent this buffer */
	uint16_t DataOffset;		/* Offset (from hdr start) to data */
	uint16_t DataDisplacement;	/* Displacement of these data bytes */
	uint8_t SetupCount;		/* Count of setup words */
	uint16_t BCC;
#if 0
	uint8_t Reserved2;		/* Reserved (pad above to word) */
	uint8_t Buffer[1];		/* Buffer containing: */
	uint16_t Setup[];		/*  Setup words (# = SetupWordCount) */
	uint16_t ByteCount;		/*  Count of data bytes */
	uint8_t Pad[];			/*  Pad to SHORT or LONG */
	uint8_t Params[];		/*  Param. bytes (# = ParamCount) */
	uint8_t Pad1[];			/*  Pad to SHORT or LONG */
	uint8_t Data[];			/*  Data bytes (# = DataCount) */
#endif
} smb_transact_rsp_t;

/*
 * SMBreadX
 */
typedef struct smb_read_andx_rsp {
	uint8_t WordCount;
	uint8_t AndXCmd;
	uint8_t AndXReserved;
	uint16_t AndXOffset;
	uint16_t Remaining;
	uint16_t DataCompactionMode;
	uint16_t Reserved;
	uint16_t DataLength;
	uint16_t DataOffset;
	uint32_t DataLengthHigh;
	uint16_t Reserved2[3];
	uint16_t ByteCount;
#if 0
	uint8_t Pad[];
	uint8_t Data[];
#endif
} smb_read_andx_rsp_t;

/*
 * smbrdr_netbios.c
 */
void nb_lock(void);
void nb_unlock(void);
void nb_close(int);
int nb_keep_alive(int, short);

int nb_send(int, unsigned char *, unsigned);
int nb_rcv(int, unsigned char *, unsigned, long);
int nb_exchange(int, unsigned char *, unsigned,
    unsigned char *, unsigned, long);
int nb_session_request(int, char *, char *, char *, char *);

/*
 * smbrdr_session.c
 */
int smbrdr_negotiate(char *, char *);
struct sdb_session *smbrdr_session_lock(const char *, const char *, int);
void smbrdr_session_unlock(struct sdb_session *);

/*
 * smbrdr_logon.c
 */
int smbrdr_logoffx(struct sdb_logon *);

/* smbrdr_netuse.c */
void smbrdr_netuse_logoff(unsigned short);
struct sdb_netuse *smbrdr_netuse_get(int);
DWORD smbrdr_tree_connect(char *, char *, char *, unsigned short *);
int smbrdr_tree_disconnect(unsigned short);
void smbrdr_netuse_put(struct sdb_netuse *);
int smbrdr_tdcon(struct sdb_netuse *);

/*
 * smbrdr_rpcpipe.c
 */
void smbrdr_ofile_end_of_share(unsigned short);
struct sdb_ofile *smbrdr_ofile_get(int);
void smbrdr_ofile_put(struct sdb_ofile *);

/* smbrdr_lib.c */
DWORD smbrdr_request_init(smbrdr_handle_t *, unsigned char,
    struct sdb_session *, struct sdb_logon *, struct sdb_netuse *);
DWORD smbrdr_send(smbrdr_handle_t *);
DWORD smbrdr_rcv(smbrdr_handle_t *, int);
DWORD smbrdr_exchange(smbrdr_handle_t *, smb_hdr_t *, long);
void smbrdr_handle_free(smbrdr_handle_t *);
int smbrdr_sign_init(struct sdb_session *, struct sdb_logon *);
void smbrdr_sign_fini(struct sdb_session *);
void smbrdr_sign_unset_key(struct sdb_session *);

void smbrdr_lock_transport(void);
void smbrdr_unlock_transport(void);

#endif /* _SMBRDR_H_ */
