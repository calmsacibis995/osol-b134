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

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/vtrace.h>
#include <sys/kmem.h>
#include <sys/zone.h>
#include <sys/tihdr.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <inet/common.h>
#include <inet/optcom.h>
#include <inet/ipclassifier.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <inet/mib2.h>
#include <inet/tcp.h>
#include <inet/ipsec_impl.h>
#include <inet/ipdrop.h>
#include <inet/tcp_impl.h>

#include <sys/squeue_impl.h>
#include <sys/squeue.h>
#include <inet/kssl/ksslapi.h>

/*
 * For the Kernel SSL proxy
 *
 * Routines in this file are called on tcp's incoming path,
 * tcp_input_data() mainly, and right before the message is
 * to be putnext()'ed upstreams.
 */

static void	tcp_kssl_input_callback(void *, mblk_t *, kssl_cmd_t);
static void	tcp_kssl_input_asynch(void *, mblk_t *, void *,
    ip_recv_attr_t *);

extern void	tcp_output(void *, mblk_t *, void *, ip_recv_attr_t *);
extern void	tcp_send_conn_ind(void *, mblk_t *, void *);

extern int tcp_squeue_flag;

/*
 * tcp_input_data() calls this routine for all packet destined to a
 * connection to the SSL port, when the SSL kernel proxy is configured
 * to intercept and process those packets.
 * A packet may carry multiple SSL records, so the function
 * calls kssl_input() in a loop, until all records are
 * handled.
 * As long as this connection is in handshake, that is until the first
 * time kssl_input() returns a record to be delivered ustreams,
 * we maintain the tcp_kssl_inhandshake, and keep an extra reference on
 * the tcp/connp across the call to kssl_input(). The reason is, that
 * function may return KSSL_CMD_QUEUED after scheduling an asynchronous
 * request and cause tcp_kssl_callback() to be called on a different CPU,
 * which could decrement the conn/tcp reference before we get to increment it.
 */
void
tcp_kssl_input(tcp_t *tcp, mblk_t *mp, cred_t *cr)
{
	struct conn_s	*connp = tcp->tcp_connp;
	tcp_t		*listener;
	mblk_t		*ind_mp;
	kssl_cmd_t	kssl_cmd;
	mblk_t		*outmp;
	struct		T_conn_ind *tci;
	boolean_t	more = B_FALSE;
	boolean_t	conn_held = B_FALSE;
	boolean_t	is_v4;
	void		*addr;

	if (is_system_labeled() && mp != NULL) {
		ASSERT(cr != NULL || msg_getcred(mp, NULL) != NULL);
		/*
		 * Provide for protocols above TCP such as RPC. NOPID leaves
		 * db_cpid unchanged.
		 * The cred could have already been set.
		 */
		if (cr != NULL)
			mblk_setcred(mp, cr, NOPID);
	}

	/* First time here, allocate the SSL context */
	if (tcp->tcp_kssl_ctx == NULL) {
		ASSERT(tcp->tcp_kssl_pending);

		is_v4 = (connp->conn_ipversion == IPV4_VERSION);
		if (is_v4) {
			addr = &connp->conn_faddr_v4;
		} else {
			addr = &connp->conn_faddr_v6;
		}

		if (kssl_init_context(tcp->tcp_kssl_ent,
		    addr, is_v4, tcp->tcp_mss,
		    &(tcp->tcp_kssl_ctx)) != KSSL_STS_OK) {
			tcp->tcp_kssl_pending = B_FALSE;
			kssl_release_ent(tcp->tcp_kssl_ent, NULL,
			    KSSL_NO_PROXY);
			tcp->tcp_kssl_ent = NULL;
			goto no_can_do;
		}
		tcp->tcp_kssl_inhandshake = B_TRUE;

		/* we won't be needing this one after now */
		kssl_release_ent(tcp->tcp_kssl_ent, NULL, KSSL_NO_PROXY);
		tcp->tcp_kssl_ent = NULL;

	}

	if (tcp->tcp_kssl_inhandshake) {
		CONN_INC_REF(connp);
		conn_held = B_TRUE;
	}

	do {
		kssl_cmd = kssl_input(tcp->tcp_kssl_ctx, mp, &outmp,
		    &more, tcp_kssl_input_callback, (void *)tcp);

		switch (kssl_cmd) {
		case KSSL_CMD_SEND:
			DTRACE_PROBE(kssl_cmd_send);
			/*
			 * We need to increment tcp_squeue_bytes to account
			 * for the extra bytes internally injected to the
			 * outgoing flow. tcp_output() will decrement it
			 * as they are sent out.
			 */
			mutex_enter(&tcp->tcp_non_sq_lock);
			tcp->tcp_squeue_bytes += msgdsize(outmp);
			mutex_exit(&tcp->tcp_non_sq_lock);
			tcp_output(connp, outmp, NULL, NULL);

		/* FALLTHROUGH */
		case KSSL_CMD_NONE:
			DTRACE_PROBE(kssl_cmd_none);
			if (tcp->tcp_kssl_pending) {
				mblk_t *ctxmp;

				/*
				 * SSL handshake successfully started -
				 * pass up the T_CONN_IND
				 */

				mp = NULL;

				listener = tcp->tcp_listener;
				tcp->tcp_kssl_pending = B_FALSE;

				ind_mp = tcp->tcp_conn.tcp_eager_conn_ind;
				ASSERT(ind_mp != NULL);

				ctxmp = allocb(sizeof (kssl_ctx_t), BPRI_MED);

				/*
				 * Give this session a chance to fall back to
				 * userland SSL
				 */
				if (ctxmp == NULL)
					goto no_can_do;

				/*
				 * attach the kssl_ctx to the conn_ind and
				 * transform it to a T_SSL_PROXY_CONN_IND.
				 * Hold it so that it stays valid till it
				 * reaches the stream head.
				 */
				kssl_hold_ctx(tcp->tcp_kssl_ctx);
				*((kssl_ctx_t *)ctxmp->b_rptr) =
				    tcp->tcp_kssl_ctx;
				ctxmp->b_wptr = ctxmp->b_rptr +
				    sizeof (kssl_ctx_t);

				ind_mp->b_cont = ctxmp;

				tci = (struct T_conn_ind *)ind_mp->b_rptr;
				tci->PRIM_type = T_SSL_PROXY_CONN_IND;

				/*
				 * The code below is copied from tcp_input_data
				 * delivering the T_CONN_IND on a TCPS_SYN_RCVD,
				 * and all conn ref cnt comments apply.
				 */
				tcp->tcp_conn.tcp_eager_conn_ind = NULL;
				tcp->tcp_tconnind_started = B_TRUE;

				CONN_INC_REF(connp);

				CONN_INC_REF(listener->tcp_connp);
				if (listener->tcp_connp->conn_sqp ==
				    connp->conn_sqp) {
					tcp_send_conn_ind(listener->tcp_connp,
					    ind_mp,
					    listener->tcp_connp->conn_sqp);
					CONN_DEC_REF(listener->tcp_connp);
				} else {
					SQUEUE_ENTER_ONE(
					    listener->tcp_connp->conn_sqp,
					    ind_mp, tcp_send_conn_ind,
					    listener->tcp_connp, NULL, SQ_FILL,
					    SQTAG_TCP_CONN_IND);
				}
			}
			break;

		case KSSL_CMD_QUEUED:
			DTRACE_PROBE(kssl_cmd_queued);
			/*
			 * We hold the conn_t here because an asynchronous
			 * request have been queued and
			 * tcp_kssl_input_callback() will be called later.
			 * It will release the conn_t
			 */
			CONN_INC_REF(connp);
			break;

		case KSSL_CMD_DELIVER_PROXY:
		case KSSL_CMD_DELIVER_SSL:
			DTRACE_PROBE(kssl_cmd_proxy__ssl);
			/*
			 * Keep accumulating if not yet accepted.
			 */
			if (tcp->tcp_listener != NULL) {
				DTRACE_PROBE1(kssl_mblk__input_rcv_enqueue,
				    mblk_t *, outmp);
				tcp_rcv_enqueue(tcp, outmp, msgdsize(outmp),
				    NULL);
			} else {
				DTRACE_PROBE1(kssl_mblk__input_putnext,
				    mblk_t *, outmp);
				putnext(connp->conn_rq, outmp);
			}
			/*
			 * We're at a phase where records are sent upstreams,
			 * past the handshake
			 */
			tcp->tcp_kssl_inhandshake = B_FALSE;
			break;

		case KSSL_CMD_NOT_SUPPORTED:
			DTRACE_PROBE(kssl_cmd_not_supported);
			/*
			 * Stop the SSL processing by the proxy, and
			 * switch to the userland SSL
			 */
			if (tcp->tcp_kssl_pending) {

				tcp->tcp_kssl_pending = B_FALSE;

no_can_do:
				DTRACE_PROBE1(kssl_no_can_do, tcp_t *, tcp);
				listener = tcp->tcp_listener;
				ind_mp = tcp->tcp_conn.tcp_eager_conn_ind;
				ASSERT(ind_mp != NULL);

				if (tcp->tcp_kssl_ctx != NULL) {
					kssl_release_ctx(tcp->tcp_kssl_ctx);
					tcp->tcp_kssl_ctx = NULL;
				}

				/*
				 * Make this a T_SSL_PROXY_CONN_IND, for the
				 * stream head to deliver it to the SSL
				 * fall-back listener
				 */
				tci = (struct T_conn_ind *)ind_mp->b_rptr;
				tci->PRIM_type = T_SSL_PROXY_CONN_IND;

				/*
				 * The code below is copied from tcp_input_data
				 * delivering the T_CONN_IND on a TCPS_SYN_RCVD,
				 * and all conn ref cnt comments apply.
				 */
				tcp->tcp_conn.tcp_eager_conn_ind = NULL;
				tcp->tcp_tconnind_started = B_TRUE;

				CONN_INC_REF(connp);

				CONN_INC_REF(listener->tcp_connp);
				if (listener->tcp_connp->conn_sqp ==
				    connp->conn_sqp) {
					tcp_send_conn_ind(listener->tcp_connp,
					    ind_mp,
					    listener->tcp_connp->conn_sqp);
					CONN_DEC_REF(listener->tcp_connp);
				} else {
					SQUEUE_ENTER_ONE(
					    listener->tcp_connp->conn_sqp,
					    ind_mp, tcp_send_conn_ind,
					    listener->tcp_connp, NULL,
					    SQ_FILL, SQTAG_TCP_CONN_IND);
				}
			}
			if (mp != NULL)
				tcp_rcv_enqueue(tcp, mp, msgdsize(mp), NULL);
			break;
		}
		mp = NULL;
	} while (more);

	if (conn_held) {
		CONN_DEC_REF(connp);
	}
}

/*
 * Callback function for the cases kssl_input() had to submit an asynchronous
 * job and need to come back when done to carry on the input processing.
 * This routine follows the conventions of timeout and interrupt handlers.
 * (no blocking, ...)
 */
static void
tcp_kssl_input_callback(void *arg, mblk_t *mp, kssl_cmd_t kssl_cmd)
{
	tcp_t	*tcp = (tcp_t *)arg;
	conn_t	*connp;
	mblk_t	*sqmp;

	ASSERT(tcp != NULL);

	connp = tcp->tcp_connp;

	ASSERT(connp != NULL);

	switch (kssl_cmd) {
	case KSSL_CMD_SEND:
		/* I'm coming from an outside perimeter */
		if (mp != NULL) {
			/*
			 * See comment in tcp_kssl_input() call to tcp_output()
			 */
			mutex_enter(&tcp->tcp_non_sq_lock);
			tcp->tcp_squeue_bytes += msgdsize(mp);
			mutex_exit(&tcp->tcp_non_sq_lock);
		}
		CONN_INC_REF(connp);
		SQUEUE_ENTER_ONE(connp->conn_sqp, mp, tcp_output, connp,
		    NULL, tcp_squeue_flag, SQTAG_TCP_OUTPUT);

	/* FALLTHROUGH */
	case KSSL_CMD_NONE:
		break;

	case KSSL_CMD_DELIVER_PROXY:
	case KSSL_CMD_DELIVER_SSL:
		/*
		 * Keep accumulating if not yet accepted.
		 */
		if (tcp->tcp_listener != NULL) {
			tcp_rcv_enqueue(tcp, mp, msgdsize(mp), NULL);
		} else {
			putnext(connp->conn_rq, mp);
		}
		break;

	case KSSL_CMD_NOT_SUPPORTED:
		/* Stop the SSL processing */
		kssl_release_ctx(tcp->tcp_kssl_ctx);
		tcp->tcp_kssl_ctx = NULL;
	}
	/*
	 * Process any input that may have accumulated while we're waiting for
	 * the call-back.
	 * We need to re-enter the squeue for this connp, and a new mp is
	 * necessary.
	 */
	if ((sqmp = allocb(1, BPRI_MED)) != NULL) {
		CONN_INC_REF(connp);
		SQUEUE_ENTER_ONE(connp->conn_sqp, sqmp, tcp_kssl_input_asynch,
		    connp, NULL, SQ_FILL, SQTAG_TCP_KSSL_INPUT);
	} else {
		DTRACE_PROBE(kssl_err__allocb_failed);
	}
	CONN_DEC_REF(connp);
}

/*
 * Needed by tcp_kssl_input_callback() to continue processing the incoming
 * flow on a tcp_t after an asynchronous callback call.
 */
/* ARGSUSED */
void
tcp_kssl_input_asynch(void *arg, mblk_t *mp, void *arg2, ip_recv_attr_t *dummy)
{
	conn_t	*connp = (conn_t *)arg;
	tcp_t *tcp = connp->conn_tcp;

	ASSERT(connp != NULL);
	freemsg(mp);

	/*
	 * NULL tcp_kssl_ctx means this connection is getting/was closed
	 * while we're away
	 */
	if (tcp->tcp_kssl_ctx != NULL) {
		tcp_kssl_input(tcp, NULL, NULL);
	}
}
