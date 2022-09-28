/*
 * Copyright (c) 2000-2001, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: smb_subr.h,v 1.13 2004/09/14 22:59:08 lindak Exp $
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _NETSMB_SMB_SUBR_H_
#define	_NETSMB_SMB_SUBR_H_

#include <sys/cmn_err.h>
#include <sys/lock.h>
#include <sys/note.h>

/* Helper function for SMBERROR */
/*PRINTFLIKE3*/
extern void smb_errmsg(int, const char *, const char *, ...)
	__KPRINTFLIKE(3);
void m_dumpm(mblk_t *m);

/*
 * Let's use C99 standard variadic macros!
 * Also the C99 __func__ (function name) feature.
 */
#define	SMBERROR(...) \
	smb_errmsg(CE_NOTE, __func__, __VA_ARGS__)
#define	SMBPANIC(...) \
	smb_errmsg(CE_PANIC, __func__, __VA_ARGS__)
#define	SMBSDEBUG(...) \
	smb_errmsg(CE_CONT, __func__, __VA_ARGS__)
#define	SMBIODEBUG(...) \
	smb_errmsg(CE_CONT, __func__, __VA_ARGS__)
#define	NBDEBUG(...) \
	smb_errmsg(CE_CONT, __func__, __VA_ARGS__)

#if defined(DEBUG) || defined(lint)

#define	DEBUG_ENTER(str) debug_enter(str)

#else /* DEBUG or lint */

#define	DEBUG_ENTER(str) ((void)0)

#endif /* DEBUG or lint */

typedef uint16_t	smb_unichar;
typedef	smb_unichar	*smb_uniptr;

extern smb_unichar smb_unieol;

struct mbchain;
struct smb_rq;
struct smb_vc;

/*
 * Tunable timeout values.  See: smb_smb.c
 */
extern int smb_timo_notice;
extern int smb_timo_default;
extern int smb_timo_open;
extern int smb_timo_read;
extern int smb_timo_write;
extern int smb_timo_append;

#define	EMOREDATA (0x7fff)

void smb_credinit(struct smb_cred *scred, cred_t *cr);
void smb_credrele(struct smb_cred *scred);

void smb_oldlm_hash(const char *apwd, uchar_t *hash);
void smb_ntlmv1hash(const char *apwd, uchar_t *hash);
void smb_ntlmv2hash(const uchar_t *v1hash, const char *user,
	const char *destination, uchar_t *v2hash);

int  smb_lmresponse(const uchar_t *hash, const uchar_t *C8, uchar_t *RN);
int  smb_ntlmv2response(const uchar_t *hash, const uchar_t *C8,
    const uchar_t *blob, size_t bloblen, uchar_t **RN, size_t *RNlen);
int  smb_maperror(int eclass, int eno);
uint32_t  smb_maperr32(uint32_t eno);
int  smb_put_dmem(struct mbchain *mbp, struct smb_vc *vcp,
    const char *src, int len, int caseopt, int *lenp);
int  smb_put_dstring(struct mbchain *mbp, struct smb_vc *vcp,
    const char *src, int caseopt);
int  smb_put_string(struct smb_rq *rqp, const char *src);
int  smb_put_asunistring(struct smb_rq *rqp, const char *src);
int  smb_checksmp(void);

int smb_cmp_sockaddr(struct sockaddr *, struct sockaddr *);
struct sockaddr *smb_dup_sockaddr(struct sockaddr *sa);
void smb_free_sockaddr(struct sockaddr *sa);
int smb_toupper(const char *, char *, size_t);

void smb_rq_sign(struct smb_rq *);
int smb_rq_verify(struct smb_rq *);
int smb_calcv2mackey(struct smb_vc *, const uchar_t *,
	const uchar_t *, size_t);
int smb_calcmackey(struct smb_vc *, const uchar_t *,
	const uchar_t *, size_t);
void smb_crypto_mech_init(void);

void smb_time_init(void);
void smb_time_fini(void);

void  smb_time_local2server(struct timespec *tsp, int tzoff, long *seconds);
void  smb_time_server2local(ulong_t seconds, int tzoff, struct timespec *tsp);
void  smb_time_NT2local(uint64_t nsec, struct timespec *tsp);
void  smb_time_local2NT(struct timespec *tsp, uint64_t *nsec);
void  smb_time_unix2dos(struct timespec *tsp, int tzoff, uint16_t *ddp,
	uint16_t *dtp, uint8_t *dhp);
void smb_dos2unixtime(uint_t dd, uint_t dt, uint_t dh, int tzoff,
	struct timespec *tsp);

#endif /* !_NETSMB_SMB_SUBR_H_ */
