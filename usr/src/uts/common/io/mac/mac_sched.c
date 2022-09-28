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

#include <sys/types.h>
#include <sys/callb.h>
#include <sys/sdt.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/vlan.h>
#include <inet/ipsec_impl.h>
#include <inet/ip_impl.h>
#include <inet/sadb.h>
#include <inet/ipsecesp.h>
#include <inet/ipsecah.h>
#include <inet/ip6.h>

#include <sys/mac_impl.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_soft_ring.h>
#include <sys/mac_flow_impl.h>

static mac_tx_cookie_t mac_tx_single_ring_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);
static mac_tx_cookie_t mac_tx_serializer_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);
static mac_tx_cookie_t mac_tx_fanout_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);
static mac_tx_cookie_t mac_tx_bw_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);

typedef struct mac_tx_mode_s {
	mac_tx_srs_mode_t	mac_tx_mode;
	mac_tx_func_t		mac_tx_func;
} mac_tx_mode_t;

/*
 * There are five modes of operation on the Tx side. These modes get set
 * in mac_tx_srs_setup(). Except for the experimental TX_SERIALIZE mode,
 * none of the other modes are user configurable. They get selected by
 * the system depending upon whether the link (or flow) has multiple Tx
 * rings or a bandwidth configured, etc.
 */
mac_tx_mode_t mac_tx_mode_list[] = {
	{SRS_TX_DEFAULT,	mac_tx_single_ring_mode},
	{SRS_TX_SERIALIZE,	mac_tx_serializer_mode},
	{SRS_TX_FANOUT,		mac_tx_fanout_mode},
	{SRS_TX_BW,		mac_tx_bw_mode},
	{SRS_TX_BW_FANOUT,	mac_tx_bw_mode}
};

/*
 * Soft Ring Set (SRS) - The Run time code that deals with
 * dynamic polling from the hardware, bandwidth enforcement,
 * fanout etc.
 *
 * We try to use H/W classification on NIC and assign traffic for
 * a MAC address to a particular Rx ring or ring group. There is a
 * 1-1 mapping between a SRS and a Rx ring. The SRS dynamically
 * switches the underlying Rx ring between interrupt and
 * polling mode and enforces any specified B/W control.
 *
 * There is always a SRS created and tied to each H/W and S/W rule.
 * Whenever we create a H/W rule, we always add the the same rule to
 * S/W classifier and tie a SRS to it.
 *
 * In case a B/W control is specified, it is broken into bytes
 * per ticks and as soon as the quota for a tick is exhausted,
 * the underlying Rx ring is forced into poll mode for remainder of
 * the tick. The SRS poll thread only polls for bytes that are
 * allowed to come in the SRS. We typically let 4x the configured
 * B/W worth of packets to come in the SRS (to prevent unnecessary
 * drops due to bursts) but only process the specified amount.
 *
 * A MAC client (e.g. a VNIC or aggr) can have 1 or more
 * Rx rings (and corresponding SRSs) assigned to it. The SRS
 * in turn can have softrings to do protocol level fanout or
 * softrings to do S/W based fanout or both. In case the NIC
 * has no Rx rings, we do S/W classification to respective SRS.
 * The S/W classification rule is always setup and ready. This
 * allows the MAC layer to reassign Rx rings whenever needed
 * but packets still continue to flow via the default path and
 * getting S/W classified to correct SRS.
 *
 * The SRS's are used on both Tx and Rx side. They use the same
 * data structure but the processing routines have slightly different
 * semantics due to the fact that Rx side needs to do dynamic
 * polling etc.
 *
 * Dynamic Polling Notes
 * =====================
 *
 * Each Soft ring set is capable of switching its Rx ring between
 * interrupt and poll mode and actively 'polls' for packets in
 * poll mode. If the SRS is implementing a B/W limit, it makes
 * sure that only Max allowed packets are pulled in poll mode
 * and goes to poll mode as soon as B/W limit is exceeded. As
 * such, there are no overheads to implement B/W limits.
 *
 * In poll mode, its better to keep the pipeline going where the
 * SRS worker thread keeps processing packets and poll thread
 * keeps bringing more packets (specially if they get to run
 * on different CPUs). This also prevents the overheads associated
 * by excessive signalling (on NUMA machines, this can be
 * pretty devastating). The exception is latency optimized case
 * where worker thread does no work and interrupt and poll thread
 * are allowed to do their own drain.
 *
 * We use the following policy to control Dynamic Polling:
 * 1) We switch to poll mode anytime the processing
 *    thread causes a backlog to build up in SRS and
 *    its associated Soft Rings (sr_poll_pkt_cnt > 0).
 * 2) As long as the backlog stays under the low water
 *    mark (sr_lowat), we poll the H/W for more packets.
 * 3) If the backlog (sr_poll_pkt_cnt) exceeds low
 *    water mark, we stay in poll mode but don't poll
 *    the H/W for more packets.
 * 4) Anytime in polling mode, if we poll the H/W for
 *    packets and find nothing plus we have an existing
 *    backlog (sr_poll_pkt_cnt > 0), we stay in polling
 *    mode but don't poll the H/W for packets anymore
 *    (let the polling thread go to sleep).
 * 5) Once the backlog is relived (packets are processed)
 *    we reenable polling (by signalling the poll thread)
 *    only when the backlog dips below sr_poll_thres.
 * 6) sr_hiwat is used exclusively when we are not
 *    polling capable and is used to decide when to
 *    drop packets so the SRS queue length doesn't grow
 *    infinitely.
 *
 * NOTE: Also see the block level comment on top of mac_soft_ring.c
 */

/*
 * mac_latency_optimize
 *
 * Controls whether the poll thread can process the packets inline
 * or let the SRS worker thread do the processing. This applies if
 * the SRS was not being processed. For latency sensitive traffic,
 * this needs to be true to allow inline processing. For throughput
 * under load, this should be false.
 *
 * This (and other similar) tunable should be rolled into a link
 * or flow specific workload hint that can be set using dladm
 * linkprop (instead of multiple such tunables).
 */
boolean_t mac_latency_optimize = B_TRUE;

/*
 * MAC_RX_SRS_ENQUEUE_CHAIN and MAC_TX_SRS_ENQUEUE_CHAIN
 *
 * queue a mp or chain in soft ring set and increment the
 * local count (srs_count) for the SRS and the shared counter
 * (srs_poll_pkt_cnt - shared between SRS and its soft rings
 * to track the total unprocessed packets for polling to work
 * correctly).
 *
 * The size (total bytes queued) counters are incremented only
 * if we are doing B/W control.
 */
#define	MAC_SRS_ENQUEUE_CHAIN(mac_srs, head, tail, count, sz) {		\
	ASSERT(MUTEX_HELD(&(mac_srs)->srs_lock));			\
	if ((mac_srs)->srs_last != NULL)				\
		(mac_srs)->srs_last->b_next = (head);			\
	else								\
		(mac_srs)->srs_first = (head);				\
	(mac_srs)->srs_last = (tail);					\
	(mac_srs)->srs_count += count;					\
}

#define	MAC_RX_SRS_ENQUEUE_CHAIN(mac_srs, head, tail, count, sz) {	\
	mac_srs_rx_t	*srs_rx = &(mac_srs)->srs_rx;			\
									\
	MAC_SRS_ENQUEUE_CHAIN(mac_srs, head, tail, count, sz);		\
	srs_rx->sr_poll_pkt_cnt += count;				\
	ASSERT(srs_rx->sr_poll_pkt_cnt > 0);				\
	if ((mac_srs)->srs_type & SRST_BW_CONTROL) {			\
		(mac_srs)->srs_size += (sz);				\
		mutex_enter(&(mac_srs)->srs_bw->mac_bw_lock);		\
		(mac_srs)->srs_bw->mac_bw_sz += (sz);			\
		mutex_exit(&(mac_srs)->srs_bw->mac_bw_lock);		\
	}								\
}

#define	MAC_TX_SRS_ENQUEUE_CHAIN(mac_srs, head, tail, count, sz) {	\
	mac_srs->srs_state |= SRS_ENQUEUED;				\
	MAC_SRS_ENQUEUE_CHAIN(mac_srs, head, tail, count, sz);		\
	if ((mac_srs)->srs_type & SRST_BW_CONTROL) {			\
		(mac_srs)->srs_size += (sz);				\
		(mac_srs)->srs_bw->mac_bw_sz += (sz);			\
	}								\
}

/*
 * Turn polling on routines
 */
#define	MAC_SRS_POLLING_ON(mac_srs) {					\
	ASSERT(MUTEX_HELD(&(mac_srs)->srs_lock));			\
	if (((mac_srs)->srs_state &					\
	    (SRS_POLLING_CAPAB|SRS_POLLING)) == SRS_POLLING_CAPAB) {	\
		(mac_srs)->srs_state |= SRS_POLLING;			\
		(void) mac_hwring_disable_intr((mac_ring_handle_t)	\
		    (mac_srs)->srs_ring);				\
		(mac_srs)->srs_rx.sr_poll_on++;				\
	}								\
}

#define	MAC_SRS_WORKER_POLLING_ON(mac_srs) {				\
	ASSERT(MUTEX_HELD(&(mac_srs)->srs_lock));			\
	if (((mac_srs)->srs_state &					\
	    (SRS_POLLING_CAPAB|SRS_WORKER|SRS_POLLING)) == 		\
	    (SRS_POLLING_CAPAB|SRS_WORKER)) {				\
		(mac_srs)->srs_state |= SRS_POLLING;			\
		(void) mac_hwring_disable_intr((mac_ring_handle_t)	\
		    (mac_srs)->srs_ring);				\
		(mac_srs)->srs_rx.sr_worker_poll_on++;			\
	}								\
}

/*
 * MAC_SRS_POLL_RING
 *
 * Signal the SRS poll thread to poll the underlying H/W ring
 * provided it wasn't already polling (SRS_GET_PKTS was set).
 *
 * Poll thread gets to run only from mac_rx_srs_drain() and only
 * if the drain was being done by the worker thread.
 */
#define	MAC_SRS_POLL_RING(mac_srs) {					\
	mac_srs_rx_t	*srs_rx = &(mac_srs)->srs_rx;			\
									\
	ASSERT(MUTEX_HELD(&(mac_srs)->srs_lock));			\
	srs_rx->sr_poll_thr_sig++;					\
	if (((mac_srs)->srs_state & 					\
	    (SRS_POLLING_CAPAB|SRS_WORKER|SRS_GET_PKTS)) ==		\
		(SRS_WORKER|SRS_POLLING_CAPAB)) {			\
		(mac_srs)->srs_state |= SRS_GET_PKTS;			\
		cv_signal(&(mac_srs)->srs_cv);   			\
	} else {							\
		srs_rx->sr_poll_thr_busy++;				\
	}								\
}

/*
 * MAC_SRS_CHECK_BW_CONTROL
 *
 * Check to see if next tick has started so we can reset the
 * SRS_BW_ENFORCED flag and allow more packets to come in the
 * system.
 */
#define	MAC_SRS_CHECK_BW_CONTROL(mac_srs) {				\
	ASSERT(MUTEX_HELD(&(mac_srs)->srs_lock));			\
	ASSERT(((mac_srs)->srs_type & SRST_TX) ||			\
	    MUTEX_HELD(&(mac_srs)->srs_bw->mac_bw_lock));		\
	clock_t now = ddi_get_lbolt();					\
	if ((mac_srs)->srs_bw->mac_bw_curr_time != now) {		\
		(mac_srs)->srs_bw->mac_bw_curr_time = now;		\
		(mac_srs)->srs_bw->mac_bw_used = 0;	       		\
		if ((mac_srs)->srs_bw->mac_bw_state & SRS_BW_ENFORCED)	\
			(mac_srs)->srs_bw->mac_bw_state &= ~SRS_BW_ENFORCED; \
	}								\
}

/*
 * MAC_SRS_WORKER_WAKEUP
 *
 * Wake up the SRS worker thread to process the queue as long as
 * no one else is processing the queue. If we are optimizing for
 * latency, we wake up the worker thread immediately or else we
 * wait mac_srs_worker_wakeup_ticks before worker thread gets
 * woken up.
 */
int mac_srs_worker_wakeup_ticks = 0;
#define	MAC_SRS_WORKER_WAKEUP(mac_srs) {				\
	ASSERT(MUTEX_HELD(&(mac_srs)->srs_lock));			\
	if (!((mac_srs)->srs_state & SRS_PROC) &&			\
		(mac_srs)->srs_tid == NULL) {				\
		if (((mac_srs)->srs_state & SRS_LATENCY_OPT) ||		\
			(mac_srs_worker_wakeup_ticks == 0))		\
			cv_signal(&(mac_srs)->srs_async);		\
		else							\
			(mac_srs)->srs_tid =				\
				timeout(mac_srs_fire, (mac_srs),	\
					mac_srs_worker_wakeup_ticks);	\
	}								\
}

#define	TX_SINGLE_RING_MODE(mac_srs)				\
	((mac_srs)->srs_tx.st_mode == SRS_TX_DEFAULT || 	\
	    (mac_srs)->srs_tx.st_mode == SRS_TX_SERIALIZE ||	\
	    (mac_srs)->srs_tx.st_mode == SRS_TX_BW)

#define	TX_BANDWIDTH_MODE(mac_srs)				\
	((mac_srs)->srs_tx.st_mode == SRS_TX_BW ||		\
	    (mac_srs)->srs_tx.st_mode == SRS_TX_BW_FANOUT)

#define	TX_SRS_TO_SOFT_RING(mac_srs, head, hint) {			\
	uint_t hash, indx;						\
	hash = HASH_HINT(hint);					\
	indx = COMPUTE_INDEX(hash, mac_srs->srs_oth_ring_count);	\
	softring = mac_srs->srs_oth_soft_rings[indx];			\
	(void) (mac_tx_soft_ring_process(softring, head, 0, NULL));	\
}

/*
 * MAC_TX_SRS_BLOCK
 *
 * Always called from mac_tx_srs_drain() function. SRS_TX_BLOCKED
 * will be set only if srs_tx_woken_up is FALSE. If
 * srs_tx_woken_up is TRUE, it indicates that the wakeup arrived
 * before we grabbed srs_lock to set SRS_TX_BLOCKED. We need to
 * attempt to transmit again and not setting SRS_TX_BLOCKED does
 * that.
 */
#define	MAC_TX_SRS_BLOCK(srs, mp)	{			\
	ASSERT(MUTEX_HELD(&(srs)->srs_lock));			\
	if ((srs)->srs_tx.st_woken_up) {			\
		(srs)->srs_tx.st_woken_up = B_FALSE;		\
	} else {						\
		ASSERT(!((srs)->srs_state & SRS_TX_BLOCKED));	\
		(srs)->srs_state |= SRS_TX_BLOCKED;		\
		(srs)->srs_tx.st_blocked_cnt++;			\
	}							\
}

/*
 * MAC_TX_SRS_TEST_HIWAT
 *
 * Called before queueing a packet onto Tx SRS to test and set
 * SRS_TX_HIWAT if srs_count exceeds srs_tx_hiwat.
 */
#define	MAC_TX_SRS_TEST_HIWAT(srs, mp, tail, cnt, sz, cookie) {		\
	boolean_t enqueue = 1;						\
									\
	if ((srs)->srs_count > (srs)->srs_tx.st_hiwat) {		\
		/*							\
		 * flow-controlled. Store srs in cookie so that it	\
		 * can be returned as mac_tx_cookie_t to client		\
		 */							\
		(srs)->srs_state |= SRS_TX_HIWAT;			\
		cookie = (mac_tx_cookie_t)srs;				\
		(srs)->srs_tx.st_hiwat_cnt++;				\
		if ((srs)->srs_count > (srs)->srs_tx.st_max_q_cnt) {	\
			/* increment freed stats */			\
			(srs)->srs_tx.st_drop_count += cnt;		\
			/*						\
			 * b_prev may be set to the fanout hint		\
			 * hence can't use freemsg directly		\
			 */						\
			mac_pkt_drop(NULL, NULL, mp_chain, B_FALSE);	\
			DTRACE_PROBE1(tx_queued_hiwat,			\
			    mac_soft_ring_set_t *, srs);		\
			enqueue = 0;					\
		}							\
	}								\
	if (enqueue)							\
		MAC_TX_SRS_ENQUEUE_CHAIN(srs, mp, tail, cnt, sz);	\
}

/* Some utility macros */
#define	MAC_SRS_BW_LOCK(srs)						\
	if (!(srs->srs_type & SRST_TX))					\
		mutex_enter(&srs->srs_bw->mac_bw_lock);

#define	MAC_SRS_BW_UNLOCK(srs)						\
	if (!(srs->srs_type & SRST_TX))					\
		mutex_exit(&srs->srs_bw->mac_bw_lock);

#define	MAC_TX_SRS_DROP_MESSAGE(srs, mp, cookie) {		\
	mac_pkt_drop(NULL, NULL, mp, B_FALSE);			\
	/* increment freed stats */				\
	mac_srs->srs_tx.st_drop_count++;			\
	cookie = (mac_tx_cookie_t)srs;				\
}

#define	MAC_TX_SET_NO_ENQUEUE(srs, mp_chain, ret_mp, cookie) {		\
	mac_srs->srs_state |= SRS_TX_WAKEUP_CLIENT;			\
	cookie = (mac_tx_cookie_t)srs;					\
	*ret_mp = mp_chain;						\
}

/*
 * Drop the rx packet and advance to the next one in the chain.
 */
static void
mac_rx_drop_pkt(mac_soft_ring_set_t *srs, mblk_t *mp)
{
	mac_srs_rx_t	*srs_rx = &srs->srs_rx;

	ASSERT(mp->b_next == NULL);
	mutex_enter(&srs->srs_lock);
	MAC_UPDATE_SRS_COUNT_LOCKED(srs, 1);
	MAC_UPDATE_SRS_SIZE_LOCKED(srs, msgdsize(mp));
	mutex_exit(&srs->srs_lock);

	srs_rx->sr_drop_count++;
	freemsg(mp);
}

/* DATAPATH RUNTIME ROUTINES */

/*
 * mac_srs_fire
 *
 * Timer callback routine for waking up the SRS worker thread.
 */
static void
mac_srs_fire(void *arg)
{
	mac_soft_ring_set_t *mac_srs = (mac_soft_ring_set_t *)arg;

	mutex_enter(&mac_srs->srs_lock);
	if (mac_srs->srs_tid == 0) {
		mutex_exit(&mac_srs->srs_lock);
		return;
	}

	mac_srs->srs_tid = 0;
	if (!(mac_srs->srs_state & SRS_PROC))
		cv_signal(&mac_srs->srs_async);

	mutex_exit(&mac_srs->srs_lock);
}

/*
 * 'hint' is fanout_hint (type of uint64_t) which is given by the TCP/IP stack,
 * and it is used on the TX path.
 */
#define	HASH_HINT(hint) \
	((hint) ^ ((hint) >> 24) ^ ((hint) >> 16) ^ ((hint) >> 8))


/*
 * hash based on the src address and the port information.
 */
#define	HASH_ADDR(src, ports)					\
	(ntohl((src)) ^ ((ports) >> 24) ^ ((ports) >> 16) ^	\
	((ports) >> 8) ^ (ports))

#define	COMPUTE_INDEX(key, sz)	(key % sz)

#define	FANOUT_ENQUEUE_MP(head, tail, cnt, bw_ctl, sz, sz0, mp) {	\
	if ((tail) != NULL) {						\
		ASSERT((tail)->b_next == NULL);				\
		(tail)->b_next = (mp);					\
	} else {							\
		ASSERT((head) == NULL);					\
		(head) = (mp);						\
	}								\
	(tail) = (mp);							\
	(cnt)++;							\
	if ((bw_ctl))							\
		(sz) += (sz0);						\
}

#define	MAC_FANOUT_DEFAULT	0
#define	MAC_FANOUT_RND_ROBIN	1
int mac_fanout_type = MAC_FANOUT_DEFAULT;

#define	MAX_SR_TYPES	3
/* fanout types for port based hashing */
enum pkt_type {
	V4_TCP = 0,
	V4_UDP,
	OTH,
	UNDEF
};

/*
 * In general we do port based hashing to spread traffic over different
 * softrings. The below tunable allows to override that behavior. Setting it
 * to B_TRUE allows to do a fanout based on src ipv6 address. This behavior
 * is also the applicable to ipv6 packets carrying multiple optional headers
 * and other uncommon packet types.
 */
boolean_t mac_src_ipv6_fanout = B_FALSE;

/*
 * Pair of local and remote ports in the transport header
 */
#define	PORTS_SIZE 4

/*
 * mac_rx_srs_proto_fanout
 *
 * This routine delivers packets destined to an SRS into one of the
 * protocol soft rings.
 *
 * Given a chain of packets we need to split it up into multiple sub chains
 * destined into TCP, UDP or OTH soft ring. Instead of entering
 * the soft ring one packet at a time, we want to enter it in the form of a
 * chain otherwise we get this start/stop behaviour where the worker thread
 * goes to sleep and then next packets comes in forcing it to wake up etc.
 */
static void
mac_rx_srs_proto_fanout(mac_soft_ring_set_t *mac_srs, mblk_t *head)
{
	struct ether_header		*ehp;
	struct ether_vlan_header	*evhp;
	uint32_t			sap;
	ipha_t				*ipha;
	uint8_t				*dstaddr;
	size_t				hdrsize;
	mblk_t				*mp;
	mblk_t				*headmp[MAX_SR_TYPES];
	mblk_t				*tailmp[MAX_SR_TYPES];
	int				cnt[MAX_SR_TYPES];
	size_t				sz[MAX_SR_TYPES];
	size_t				sz1;
	boolean_t			bw_ctl;
	boolean_t			hw_classified;
	boolean_t			dls_bypass;
	boolean_t			is_ether;
	boolean_t			is_unicast;
	enum pkt_type			type;
	mac_client_impl_t		*mcip = mac_srs->srs_mcip;

	is_ether = (mcip->mci_mip->mi_info.mi_nativemedia == DL_ETHER);
	bw_ctl = ((mac_srs->srs_type & SRST_BW_CONTROL) != 0);

	/*
	 * If we don't have a Rx ring, S/W classification would have done
	 * its job and its a packet meant for us. If we were polling on
	 * the default ring (i.e. there was a ring assigned to this SRS),
	 * then we need to make sure that the mac address really belongs
	 * to us.
	 */
	hw_classified = mac_srs->srs_ring != NULL &&
	    mac_srs->srs_ring->mr_classify_type == MAC_HW_CLASSIFIER;

	/*
	 * Special clients (eg. VLAN, non ether, etc) need DLS
	 * processing in the Rx path. SRST_DLS_BYPASS will be clear for
	 * such SRSs. Another way of disabling bypass is to set the
	 * MCIS_RX_BYPASS_DISABLE flag.
	 */
	dls_bypass = ((mac_srs->srs_type & SRST_DLS_BYPASS) != 0) &&
	    ((mcip->mci_state_flags & MCIS_RX_BYPASS_DISABLE) == 0);

	bzero(headmp, MAX_SR_TYPES * sizeof (mblk_t *));
	bzero(tailmp, MAX_SR_TYPES * sizeof (mblk_t *));
	bzero(cnt, MAX_SR_TYPES * sizeof (int));
	bzero(sz, MAX_SR_TYPES * sizeof (size_t));

	/*
	 * We got a chain from SRS that we need to send to the soft rings.
	 * Since squeues for TCP & IPv4 sap poll their soft rings (for
	 * performance reasons), we need to separate out v4_tcp, v4_udp
	 * and the rest goes in other.
	 */
	while (head != NULL) {
		mp = head;
		head = head->b_next;
		mp->b_next = NULL;

		type = OTH;
		sz1 = (mp->b_cont == NULL) ? MBLKL(mp) : msgdsize(mp);

		if (is_ether) {
			/*
			 * At this point we can be sure the packet at least
			 * has an ether header.
			 */
			if (sz1 < sizeof (struct ether_header)) {
				mac_rx_drop_pkt(mac_srs, mp);
				continue;
			}
			ehp = (struct ether_header *)mp->b_rptr;

			/*
			 * Determine if this is a VLAN or non-VLAN packet.
			 */
			if ((sap = ntohs(ehp->ether_type)) == VLAN_TPID) {
				evhp = (struct ether_vlan_header *)mp->b_rptr;
				sap = ntohs(evhp->ether_type);
				hdrsize = sizeof (struct ether_vlan_header);
				/*
				 * Check if the VID of the packet, if any,
				 * belongs to this client.
				 */
				if (!mac_client_check_flow_vid(mcip,
				    VLAN_ID(ntohs(evhp->ether_tci)))) {
					mac_rx_drop_pkt(mac_srs, mp);
					continue;
				}
			} else {
				hdrsize = sizeof (struct ether_header);
			}
			is_unicast =
			    ((((uint8_t *)&ehp->ether_dhost)[0] & 0x01) == 0);
			dstaddr = (uint8_t *)&ehp->ether_dhost;
		} else {
			mac_header_info_t		mhi;

			if (mac_header_info((mac_handle_t)mcip->mci_mip,
			    mp, &mhi) != 0) {
				mac_rx_drop_pkt(mac_srs, mp);
				continue;
			}
			hdrsize = mhi.mhi_hdrsize;
			sap = mhi.mhi_bindsap;
			is_unicast = (mhi.mhi_dsttype == MAC_ADDRTYPE_UNICAST);
			dstaddr = (uint8_t *)mhi.mhi_daddr;
		}

		if (!dls_bypass) {
			FANOUT_ENQUEUE_MP(headmp[type], tailmp[type],
			    cnt[type], bw_ctl, sz[type], sz1, mp);
			continue;
		}

		if (sap == ETHERTYPE_IP) {
			/*
			 * If we are H/W classified, but we have promisc
			 * on, then we need to check for the unicast address.
			 */
			if (hw_classified && mcip->mci_promisc_list != NULL) {
				mac_address_t		*map;

				rw_enter(&mcip->mci_rw_lock, RW_READER);
				map = mcip->mci_unicast;
				if (bcmp(dstaddr, map->ma_addr,
				    map->ma_len) == 0)
					type = UNDEF;
				rw_exit(&mcip->mci_rw_lock);
			} else if (is_unicast) {
				type = UNDEF;
			}
		}

		/*
		 * This needs to become a contract with the driver for
		 * the fast path.
		 *
		 * In the normal case the packet will have at least the L2
		 * header and the IP + Transport header in the same mblk.
		 * This is usually the case when the NIC driver sends up
		 * the packet. This is also true when the stack generates
		 * a packet that is looped back and when the stack uses the
		 * fastpath mechanism. The normal case is optimized for
		 * performance and may bypass DLS. All other cases go through
		 * the 'OTH' type path without DLS bypass.
		 */

		ipha = (ipha_t *)(mp->b_rptr + hdrsize);
		if ((type != OTH) && MBLK_RX_FANOUT_SLOWPATH(mp, ipha))
			type = OTH;

		if (type == OTH) {
			FANOUT_ENQUEUE_MP(headmp[type], tailmp[type],
			    cnt[type], bw_ctl, sz[type], sz1, mp);
			continue;
		}

		ASSERT(type == UNDEF);
		/*
		 * We look for at least 4 bytes past the IP header to get
		 * the port information. If we get an IP fragment, we don't
		 * have the port information, and we use just the protocol
		 * information.
		 */
		switch (ipha->ipha_protocol) {
		case IPPROTO_TCP:
			type = V4_TCP;
			mp->b_rptr += hdrsize;
			break;
		case IPPROTO_UDP:
			type = V4_UDP;
			mp->b_rptr += hdrsize;
			break;
		default:
			type = OTH;
			break;
		}

		FANOUT_ENQUEUE_MP(headmp[type], tailmp[type], cnt[type],
		    bw_ctl, sz[type], sz1, mp);
	}

	for (type = V4_TCP; type < UNDEF; type++) {
		if (headmp[type] != NULL) {
			mac_soft_ring_t			*softring;

			ASSERT(tailmp[type]->b_next == NULL);
			switch (type) {
			case V4_TCP:
				softring = mac_srs->srs_tcp_soft_rings[0];
				break;
			case V4_UDP:
				softring = mac_srs->srs_udp_soft_rings[0];
				break;
			case OTH:
				softring = mac_srs->srs_oth_soft_rings[0];
			}
			mac_rx_soft_ring_process(mcip, softring,
			    headmp[type], tailmp[type], cnt[type], sz[type]);
		}
	}
}

int	fanout_unalligned = 0;

/*
 * mac_rx_srs_long_fanout
 *
 * The fanout routine for IPv6
 */
static int
mac_rx_srs_long_fanout(mac_soft_ring_set_t *mac_srs, mblk_t *mp,
    uint32_t sap, size_t hdrsize, enum pkt_type *type, uint_t *indx)
{
	ip6_t		*ip6h;
	uint8_t		*whereptr;
	uint_t		hash;
	uint16_t	remlen;
	uint8_t		nexthdr;
	uint16_t	hdr_len;

	if (sap == ETHERTYPE_IPV6) {
		boolean_t	modifiable = B_TRUE;

		ASSERT(MBLKL(mp) >= hdrsize);

		ip6h = (ip6_t *)(mp->b_rptr + hdrsize);
		if ((unsigned char *)ip6h == mp->b_wptr) {
			/*
			 * The first mblk_t only includes the mac header.
			 * Note that it is safe to change the mp pointer here,
			 * as the subsequent operation does not assume mp
			 * points to the start of the mac header.
			 */
			mp = mp->b_cont;

			/*
			 * Make sure ip6h holds the full ip6_t structure.
			 */
			if (mp == NULL)
				return (-1);

			if (MBLKL(mp) < IPV6_HDR_LEN) {
				modifiable = (DB_REF(mp) == 1);

				if (modifiable &&
				    !pullupmsg(mp, IPV6_HDR_LEN)) {
					return (-1);
				}
			}

			ip6h = (ip6_t *)mp->b_rptr;
		}

		if (!modifiable || !(OK_32PTR((char *)ip6h)) ||
		    ((unsigned char *)ip6h + IPV6_HDR_LEN > mp->b_wptr)) {
			/*
			 * If either ip6h is not alligned, or ip6h does not
			 * hold the complete ip6_t structure (a pullupmsg()
			 * is not an option since it would result in an
			 * unalligned ip6h), fanout to the default ring. Note
			 * that this may cause packets reordering.
			 */
			*indx = 0;
			*type = OTH;
			fanout_unalligned++;
			return (0);
		}

		remlen = ntohs(ip6h->ip6_plen);
		nexthdr = ip6h->ip6_nxt;

		if (remlen < MIN_EHDR_LEN)
			return (-1);
		/*
		 * Do src based fanout if below tunable is set to B_TRUE or
		 * when mac_ip_hdr_length_v6() fails because of malformed
		 * packets or because mblk's need to be concatenated using
		 * pullupmsg().
		 */
		if (mac_src_ipv6_fanout || !mac_ip_hdr_length_v6(mp, ip6h,
		    &hdr_len, &nexthdr, NULL, NULL)) {
			goto src_based_fanout;
		}
		whereptr = (uint8_t *)ip6h + hdr_len;

		/* If the transport is one of below, we do port based fanout */
		switch (nexthdr) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
		case IPPROTO_SCTP:
		case IPPROTO_ESP:
			/*
			 * If the ports in the transport header is not part of
			 * the mblk, do src_based_fanout, instead of calling
			 * pullupmsg().
			 */
			if (mp->b_cont != NULL &&
			    whereptr + PORTS_SIZE > mp->b_wptr) {
				goto src_based_fanout;
			}
			break;
		default:
			break;
		}

		switch (nexthdr) {
		case IPPROTO_TCP:
			hash = HASH_ADDR(V4_PART_OF_V6(ip6h->ip6_src),
			    *(uint32_t *)whereptr);
			*indx = COMPUTE_INDEX(hash,
			    mac_srs->srs_tcp_ring_count);
			*type = OTH;
			break;

		case IPPROTO_UDP:
		case IPPROTO_SCTP:
		case IPPROTO_ESP:
			if (mac_fanout_type == MAC_FANOUT_DEFAULT) {
				hash = HASH_ADDR(V4_PART_OF_V6(ip6h->ip6_src),
				    *(uint32_t *)whereptr);
				*indx = COMPUTE_INDEX(hash,
				    mac_srs->srs_udp_ring_count);
			} else {
				*indx = mac_srs->srs_ind %
				    mac_srs->srs_udp_ring_count;
				mac_srs->srs_ind++;
			}
			*type = OTH;
			break;

			/* For all other protocol, do source based fanout */
		default:
			goto src_based_fanout;
		}
	} else {
		*indx = 0;
		*type = OTH;
	}
	return (0);

src_based_fanout:
	hash = HASH_ADDR(V4_PART_OF_V6(ip6h->ip6_src), (uint32_t)0);
	*indx = COMPUTE_INDEX(hash, mac_srs->srs_oth_ring_count);
	*type = OTH;
	return (0);
}

/*
 * mac_rx_srs_fanout
 *
 * This routine delivers packets destined to an SRS into a soft ring member
 * of the set.
 *
 * Given a chain of packets we need to split it up into multiple sub chains
 * destined for one of the TCP, UDP or OTH soft rings. Instead of entering
 * the soft ring one packet at a time, we want to enter it in the form of a
 * chain otherwise we get this start/stop behaviour where the worker thread
 * goes to sleep and then next packets comes in forcing it to wake up etc.
 *
 * Note:
 * Since we know what is the maximum fanout possible, we create a 2D array
 * of 'softring types * MAX_SR_FANOUT' for the head, tail, cnt and sz
 * variables so that we can enter the softrings with chain. We need the
 * MAX_SR_FANOUT so we can allocate the arrays on the stack (a kmem_alloc
 * for each packet would be expensive). If we ever want to have the
 * ability to have unlimited fanout, we should probably declare a head,
 * tail, cnt, sz with each soft ring (a data struct which contains a softring
 * along with these members) and create an array of this uber struct so we
 * don't have to do kmem_alloc.
 */
int	fanout_oth1 = 0;
int	fanout_oth2 = 0;
int	fanout_oth3 = 0;
int	fanout_oth4 = 0;
int	fanout_oth5 = 0;

static void
mac_rx_srs_fanout(mac_soft_ring_set_t *mac_srs, mblk_t *head)
{
	struct ether_header		*ehp;
	struct ether_vlan_header	*evhp;
	uint32_t			sap;
	ipha_t				*ipha;
	uint8_t				*dstaddr;
	uint_t				indx;
	size_t				ports_offset;
	size_t				ipha_len;
	size_t				hdrsize;
	uint_t				hash;
	mblk_t				*mp;
	mblk_t				*headmp[MAX_SR_TYPES][MAX_SR_FANOUT];
	mblk_t				*tailmp[MAX_SR_TYPES][MAX_SR_FANOUT];
	int				cnt[MAX_SR_TYPES][MAX_SR_FANOUT];
	size_t				sz[MAX_SR_TYPES][MAX_SR_FANOUT];
	size_t				sz1;
	boolean_t			bw_ctl;
	boolean_t			hw_classified;
	boolean_t			dls_bypass;
	boolean_t			is_ether;
	boolean_t			is_unicast;
	int				fanout_cnt;
	enum pkt_type			type;
	mac_client_impl_t		*mcip = mac_srs->srs_mcip;

	is_ether = (mcip->mci_mip->mi_info.mi_nativemedia == DL_ETHER);
	bw_ctl = ((mac_srs->srs_type & SRST_BW_CONTROL) != 0);

	/*
	 * If we don't have a Rx ring, S/W classification would have done
	 * its job and its a packet meant for us. If we were polling on
	 * the default ring (i.e. there was a ring assigned to this SRS),
	 * then we need to make sure that the mac address really belongs
	 * to us.
	 */
	hw_classified = mac_srs->srs_ring != NULL &&
	    mac_srs->srs_ring->mr_classify_type == MAC_HW_CLASSIFIER;

	/*
	 * Special clients (eg. VLAN, non ether, etc) need DLS
	 * processing in the Rx path. SRST_DLS_BYPASS will be clear for
	 * such SRSs. Another way of disabling bypass is to set the
	 * MCIS_RX_BYPASS_DISABLE flag.
	 */
	dls_bypass = ((mac_srs->srs_type & SRST_DLS_BYPASS) != 0) &&
	    ((mcip->mci_state_flags & MCIS_RX_BYPASS_DISABLE) == 0);

	/*
	 * Since the softrings are never destroyed and we always
	 * create equal number of softrings for TCP, UDP and rest,
	 * its OK to check one of them for count and use it without
	 * any lock. In future, if soft rings get destroyed because
	 * of reduction in fanout, we will need to ensure that happens
	 * behind the SRS_PROC.
	 */
	fanout_cnt = mac_srs->srs_tcp_ring_count;

	bzero(headmp, MAX_SR_TYPES * MAX_SR_FANOUT * sizeof (mblk_t *));
	bzero(tailmp, MAX_SR_TYPES * MAX_SR_FANOUT * sizeof (mblk_t *));
	bzero(cnt, MAX_SR_TYPES * MAX_SR_FANOUT * sizeof (int));
	bzero(sz, MAX_SR_TYPES * MAX_SR_FANOUT * sizeof (size_t));

	/*
	 * We got a chain from SRS that we need to send to the soft rings.
	 * Since squeues for TCP & IPv4 sap poll their soft rings (for
	 * performance reasons), we need to separate out v4_tcp, v4_udp
	 * and the rest goes in other.
	 */
	while (head != NULL) {
		mp = head;
		head = head->b_next;
		mp->b_next = NULL;

		type = OTH;
		sz1 = (mp->b_cont == NULL) ? MBLKL(mp) : msgdsize(mp);

		if (is_ether) {
			/*
			 * At this point we can be sure the packet at least
			 * has an ether header.
			 */
			if (sz1 < sizeof (struct ether_header)) {
				mac_rx_drop_pkt(mac_srs, mp);
				continue;
			}
			ehp = (struct ether_header *)mp->b_rptr;

			/*
			 * Determine if this is a VLAN or non-VLAN packet.
			 */
			if ((sap = ntohs(ehp->ether_type)) == VLAN_TPID) {
				evhp = (struct ether_vlan_header *)mp->b_rptr;
				sap = ntohs(evhp->ether_type);
				hdrsize = sizeof (struct ether_vlan_header);
				/*
				 * Check if the VID of the packet, if any,
				 * belongs to this client.
				 */
				if (!mac_client_check_flow_vid(mcip,
				    VLAN_ID(ntohs(evhp->ether_tci)))) {
					mac_rx_drop_pkt(mac_srs, mp);
					continue;
				}
			} else {
				hdrsize = sizeof (struct ether_header);
			}
			is_unicast =
			    ((((uint8_t *)&ehp->ether_dhost)[0] & 0x01) == 0);
			dstaddr = (uint8_t *)&ehp->ether_dhost;
		} else {
			mac_header_info_t		mhi;

			if (mac_header_info((mac_handle_t)mcip->mci_mip,
			    mp, &mhi) != 0) {
				mac_rx_drop_pkt(mac_srs, mp);
				continue;
			}
			hdrsize = mhi.mhi_hdrsize;
			sap = mhi.mhi_bindsap;
			is_unicast = (mhi.mhi_dsttype == MAC_ADDRTYPE_UNICAST);
			dstaddr = (uint8_t *)mhi.mhi_daddr;
		}

		if (!dls_bypass) {
			if (mac_rx_srs_long_fanout(mac_srs, mp, sap,
			    hdrsize, &type, &indx) == -1) {
				mac_rx_drop_pkt(mac_srs, mp);
				continue;
			}

			FANOUT_ENQUEUE_MP(headmp[type][indx],
			    tailmp[type][indx], cnt[type][indx], bw_ctl,
			    sz[type][indx], sz1, mp);
			continue;
		}


		/*
		 * If we are using the default Rx ring where H/W or S/W
		 * classification has not happened, we need to verify if
		 * this unicast packet really belongs to us.
		 */
		if (sap == ETHERTYPE_IP) {
			/*
			 * If we are H/W classified, but we have promisc
			 * on, then we need to check for the unicast address.
			 */
			if (hw_classified && mcip->mci_promisc_list != NULL) {
				mac_address_t		*map;

				rw_enter(&mcip->mci_rw_lock, RW_READER);
				map = mcip->mci_unicast;
				if (bcmp(dstaddr, map->ma_addr,
				    map->ma_len) == 0)
					type = UNDEF;
				rw_exit(&mcip->mci_rw_lock);
			} else if (is_unicast) {
				type = UNDEF;
			}
		}

		/*
		 * This needs to become a contract with the driver for
		 * the fast path.
		 */

		ipha = (ipha_t *)(mp->b_rptr + hdrsize);
		if ((type != OTH) && MBLK_RX_FANOUT_SLOWPATH(mp, ipha)) {
			type = OTH;
			fanout_oth1++;
		}

		if (type != OTH) {
			uint16_t	frag_offset_flags;

			switch (ipha->ipha_protocol) {
			case IPPROTO_TCP:
			case IPPROTO_UDP:
			case IPPROTO_SCTP:
			case IPPROTO_ESP:
				ipha_len = IPH_HDR_LENGTH(ipha);
				if ((uchar_t *)ipha + ipha_len + PORTS_SIZE >
				    mp->b_wptr) {
					type = OTH;
					break;
				}
				frag_offset_flags =
				    ntohs(ipha->ipha_fragment_offset_and_flags);
				if ((frag_offset_flags &
				    (IPH_MF | IPH_OFFSET)) != 0) {
					type = OTH;
					fanout_oth3++;
					break;
				}
				ports_offset = hdrsize + ipha_len;
				break;
			default:
				type = OTH;
				fanout_oth4++;
				break;
			}
		}

		if (type == OTH) {
			if (mac_rx_srs_long_fanout(mac_srs, mp, sap,
			    hdrsize, &type, &indx) == -1) {
				mac_rx_drop_pkt(mac_srs, mp);
				continue;
			}

			FANOUT_ENQUEUE_MP(headmp[type][indx],
			    tailmp[type][indx], cnt[type][indx], bw_ctl,
			    sz[type][indx], sz1, mp);
			continue;
		}

		ASSERT(type == UNDEF);

		/*
		 * XXX-Sunay: We should hold srs_lock since ring_count
		 * below can change. But if we are always called from
		 * mac_rx_srs_drain and SRS_PROC is set, then we can
		 * enforce that ring_count can't be changed i.e.
		 * to change fanout type or ring count, the calling
		 * thread needs to be behind SRS_PROC.
		 */
		switch (ipha->ipha_protocol) {
		case IPPROTO_TCP:
			/*
			 * Note that for ESP, we fanout on SPI and it is at the
			 * same offset as the 2x16-bit ports. So it is clumped
			 * along with TCP, UDP and SCTP.
			 */
			hash = HASH_ADDR(ipha->ipha_src,
			    *(uint32_t *)(mp->b_rptr + ports_offset));
			indx = COMPUTE_INDEX(hash, mac_srs->srs_tcp_ring_count);
			type = V4_TCP;
			mp->b_rptr += hdrsize;
			break;
		case IPPROTO_UDP:
		case IPPROTO_SCTP:
		case IPPROTO_ESP:
			if (mac_fanout_type == MAC_FANOUT_DEFAULT) {
				hash = HASH_ADDR(ipha->ipha_src,
				    *(uint32_t *)(mp->b_rptr + ports_offset));
				indx = COMPUTE_INDEX(hash,
				    mac_srs->srs_udp_ring_count);
			} else {
				indx = mac_srs->srs_ind %
				    mac_srs->srs_udp_ring_count;
				mac_srs->srs_ind++;
			}
			type = V4_UDP;
			mp->b_rptr += hdrsize;
			break;
		default:
			indx = 0;
			type = OTH;
		}

		FANOUT_ENQUEUE_MP(headmp[type][indx], tailmp[type][indx],
		    cnt[type][indx], bw_ctl, sz[type][indx], sz1, mp);
	}

	for (type = V4_TCP; type < UNDEF; type++) {
		int	i;

		for (i = 0; i < fanout_cnt; i++) {
			if (headmp[type][i] != NULL) {
				mac_soft_ring_t	*softring;

				ASSERT(tailmp[type][i]->b_next == NULL);
				switch (type) {
				case V4_TCP:
					softring =
					    mac_srs->srs_tcp_soft_rings[i];
					break;
				case V4_UDP:
					softring =
					    mac_srs->srs_udp_soft_rings[i];
					break;
				case OTH:
					softring =
					    mac_srs->srs_oth_soft_rings[i];
					break;
				}
				mac_rx_soft_ring_process(mcip,
				    softring, headmp[type][i], tailmp[type][i],
				    cnt[type][i], sz[type][i]);
			}
		}
	}
}

#define	SRS_BYTES_TO_PICKUP	150000
ssize_t	max_bytes_to_pickup = SRS_BYTES_TO_PICKUP;

/*
 * mac_rx_srs_poll_ring
 *
 * This SRS Poll thread uses this routine to poll the underlying hardware
 * Rx ring to get a chain of packets. It can inline process that chain
 * if mac_latency_optimize is set (default) or signal the SRS worker thread
 * to do the remaining processing.
 *
 * Since packets come in the system via interrupt or poll path, we also
 * update the stats and deal with promiscous clients here.
 */
void
mac_rx_srs_poll_ring(mac_soft_ring_set_t *mac_srs)
{
	kmutex_t 		*lock = &mac_srs->srs_lock;
	kcondvar_t 		*async = &mac_srs->srs_cv;
	mac_srs_rx_t		*srs_rx = &mac_srs->srs_rx;
	mblk_t 			*head, *tail, *mp;
	callb_cpr_t 		cprinfo;
	ssize_t 		bytes_to_pickup;
	size_t 			sz;
	int			count;
	mac_client_impl_t	*smcip;

	CALLB_CPR_INIT(&cprinfo, lock, callb_generic_cpr, "mac_srs_poll");
	mutex_enter(lock);

start:
	for (;;) {
		if (mac_srs->srs_state & SRS_PAUSE)
			goto done;

		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		cv_wait(async, lock);
		CALLB_CPR_SAFE_END(&cprinfo, lock);

		if (mac_srs->srs_state & SRS_PAUSE)
			goto done;

check_again:
		if (mac_srs->srs_type & SRST_BW_CONTROL) {
			/*
			 * We pick as many bytes as we are allowed to queue.
			 * Its possible that we will exceed the total
			 * packets queued in case this SRS is part of the
			 * Rx ring group since > 1 poll thread can be pulling
			 * upto the max allowed packets at the same time
			 * but that should be OK.
			 */
			mutex_enter(&mac_srs->srs_bw->mac_bw_lock);
			bytes_to_pickup =
			    mac_srs->srs_bw->mac_bw_drop_threshold -
			    mac_srs->srs_bw->mac_bw_sz;
			/*
			 * We shouldn't have been signalled if we
			 * have 0 or less bytes to pick but since
			 * some of the bytes accounting is driver
			 * dependant, we do the safety check.
			 */
			if (bytes_to_pickup < 0)
				bytes_to_pickup = 0;
			mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
		} else {
			/*
			 * ToDO: Need to change the polling API
			 * to add a packet count and a flag which
			 * tells the driver whether we want packets
			 * based on a count, or bytes, or all the
			 * packets queued in the driver/HW. This
			 * way, we never have to check the limits
			 * on poll path. We truly let only as many
			 * packets enter the system as we are willing
			 * to process or queue.
			 *
			 * Something along the lines of
			 * pkts_to_pickup = mac_soft_ring_max_q_cnt -
			 *	mac_srs->srs_poll_pkt_cnt
			 */

			/*
			 * Since we are not doing B/W control, pick
			 * as many packets as allowed.
			 */
			bytes_to_pickup = max_bytes_to_pickup;
		}

		/* Poll the underlying Hardware */
		mutex_exit(lock);
		head = MAC_HWRING_POLL(mac_srs->srs_ring, (int)bytes_to_pickup);
		mutex_enter(lock);

		ASSERT((mac_srs->srs_state & SRS_POLL_THR_OWNER) ==
		    SRS_POLL_THR_OWNER);

		mp = tail = head;
		count = 0;
		sz = 0;
		while (mp != NULL) {
			tail = mp;
			sz += msgdsize(mp);
			mp = mp->b_next;
			count++;
		}

		if (head != NULL) {
			tail->b_next = NULL;
			smcip = mac_srs->srs_mcip;

			if ((mac_srs->srs_type & SRST_FLOW) ||
			    (smcip == NULL)) {
				FLOW_STAT_UPDATE(mac_srs->srs_flent,
				    rbytes, sz);
				FLOW_STAT_UPDATE(mac_srs->srs_flent,
				    ipackets, count);
			}

			/*
			 * If there are any promiscuous mode callbacks
			 * defined for this MAC client, pass them a copy
			 * if appropriate and also update the counters.
			 */
			if (smcip != NULL) {
				smcip->mci_stat_ibytes += sz;
				smcip->mci_stat_ipackets += count;

				if (smcip->mci_mip->mi_promisc_list != NULL) {
					mutex_exit(lock);
					mac_promisc_dispatch(smcip->mci_mip,
					    head, NULL);
					mutex_enter(lock);
				}
			}
			if (mac_srs->srs_type & SRST_BW_CONTROL) {
				mutex_enter(&mac_srs->srs_bw->mac_bw_lock);
				mac_srs->srs_bw->mac_bw_polled += sz;
				mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
			}
			srs_rx->sr_poll_count += count;
			MAC_RX_SRS_ENQUEUE_CHAIN(mac_srs, head, tail,
			    count, sz);
			if (count <= 10)
				srs_rx->sr_chain_cnt_undr10++;
			else if (count > 10 && count <= 50)
				srs_rx->sr_chain_cnt_10to50++;
			else
				srs_rx->sr_chain_cnt_over50++;
		}

		/*
		 * We are guaranteed that SRS_PROC will be set if we
		 * are here. Also, poll thread gets to run only if
		 * the drain was being done by a worker thread although
		 * its possible that worker thread is still running
		 * and poll thread was sent down to keep the pipeline
		 * going instead of doing a complete drain and then
		 * trying to poll the NIC.
		 *
		 * So we need to check SRS_WORKER flag to make sure
		 * that the worker thread is not processing the queue
		 * in parallel to us. The flags and conditions are
		 * protected by the srs_lock to prevent any race. We
		 * ensure that we don't drop the srs_lock from now
		 * till the end and similarly we don't drop the srs_lock
		 * in mac_rx_srs_drain() till similar condition check
		 * are complete. The mac_rx_srs_drain() needs to ensure
		 * that SRS_WORKER flag remains set as long as its
		 * processing the queue.
		 */
		if (!(mac_srs->srs_state & SRS_WORKER) &&
		    (mac_srs->srs_first != NULL)) {
			/*
			 * We have packets to process and worker thread
			 * is not running. Check to see if poll thread is
			 * allowed to process.
			 */
			if (mac_srs->srs_state & SRS_LATENCY_OPT) {
				mac_srs->srs_drain_func(mac_srs, SRS_POLL_PROC);
				if (!(mac_srs->srs_state & SRS_PAUSE) &&
				    srs_rx->sr_poll_pkt_cnt <=
				    srs_rx->sr_lowat) {
					srs_rx->sr_poll_again++;
					goto check_again;
				}
				/*
				 * We are already above low water mark
				 * so stay in the polling mode but no
				 * need to poll. Once we dip below
				 * the polling threshold, the processing
				 * thread (soft ring) will signal us
				 * to poll again (MAC_UPDATE_SRS_COUNT)
				 */
				srs_rx->sr_poll_drain_no_poll++;
				mac_srs->srs_state &= ~(SRS_PROC|SRS_GET_PKTS);
				/*
				 * In B/W control case, its possible
				 * that the backlog built up due to
				 * B/W limit being reached and packets
				 * are queued only in SRS. In this case,
				 * we should schedule worker thread
				 * since no one else will wake us up.
				 */
				if ((mac_srs->srs_type & SRST_BW_CONTROL) &&
				    (mac_srs->srs_tid == NULL)) {
					mac_srs->srs_tid =
					    timeout(mac_srs_fire, mac_srs, 1);
					srs_rx->sr_poll_worker_wakeup++;
				}
			} else {
				/*
				 * Wakeup the worker thread for more processing.
				 * We optimize for throughput in this case.
				 */
				mac_srs->srs_state &= ~(SRS_PROC|SRS_GET_PKTS);
				MAC_SRS_WORKER_WAKEUP(mac_srs);
				srs_rx->sr_poll_sig_worker++;
			}
		} else if ((mac_srs->srs_first == NULL) &&
		    !(mac_srs->srs_state & SRS_WORKER)) {
			/*
			 * There is nothing queued in SRS and
			 * no worker thread running. Plus we
			 * didn't get anything from the H/W
			 * as well (head == NULL);
			 */
			ASSERT(head == NULL);
			mac_srs->srs_state &=
			    ~(SRS_PROC|SRS_GET_PKTS);

			/*
			 * If we have a packets in soft ring, don't allow
			 * more packets to come into this SRS by keeping the
			 * interrupts off but not polling the H/W. The
			 * poll thread will get signaled as soon as
			 * srs_poll_pkt_cnt dips below poll threshold.
			 */
			if (srs_rx->sr_poll_pkt_cnt == 0) {
				srs_rx->sr_poll_intr_enable++;
				MAC_SRS_POLLING_OFF(mac_srs);
			} else {
				/*
				 * We know nothing is queued in SRS
				 * since we are here after checking
				 * srs_first is NULL. The backlog
				 * is entirely due to packets queued
				 * in Soft ring which will wake us up
				 * and get the interface out of polling
				 * mode once the backlog dips below
				 * sr_poll_thres.
				 */
				srs_rx->sr_poll_no_poll++;
			}
		} else {
			/*
			 * Worker thread is already running.
			 * Nothing much to do. If the polling
			 * was enabled, worker thread will deal
			 * with that.
			 */
			mac_srs->srs_state &= ~SRS_GET_PKTS;
			srs_rx->sr_poll_goto_sleep++;
		}
	}
done:
	mac_srs->srs_state |= SRS_POLL_THR_QUIESCED;
	cv_signal(&mac_srs->srs_async);
	/*
	 * If this is a temporary quiesce then wait for the restart signal
	 * from the srs worker. Then clear the flags and signal the srs worker
	 * to ensure a positive handshake and go back to start.
	 */
	while (!(mac_srs->srs_state & (SRS_CONDEMNED | SRS_POLL_THR_RESTART)))
		cv_wait(async, lock);
	if (mac_srs->srs_state & SRS_POLL_THR_RESTART) {
		ASSERT(!(mac_srs->srs_state & SRS_CONDEMNED));
		mac_srs->srs_state &=
		    ~(SRS_POLL_THR_QUIESCED | SRS_POLL_THR_RESTART);
		cv_signal(&mac_srs->srs_async);
		goto start;
	} else {
		mac_srs->srs_state |= SRS_POLL_THR_EXITED;
		cv_signal(&mac_srs->srs_async);
		CALLB_CPR_EXIT(&cprinfo);
		thread_exit();
	}
}

/*
 * mac_srs_pick_chain
 *
 * In Bandwidth control case, checks how many packets can be processed
 * and return them in a sub chain.
 */
static mblk_t *
mac_srs_pick_chain(mac_soft_ring_set_t *mac_srs, mblk_t **chain_tail,
    size_t *chain_sz, int *chain_cnt)
{
	mblk_t 			*head = NULL;
	mblk_t 			*tail = NULL;
	size_t			sz;
	size_t 			tsz = 0;
	int			cnt = 0;
	mblk_t 			*mp;

	ASSERT(MUTEX_HELD(&mac_srs->srs_lock));
	mutex_enter(&mac_srs->srs_bw->mac_bw_lock);
	if (((mac_srs->srs_bw->mac_bw_used + mac_srs->srs_size) <=
	    mac_srs->srs_bw->mac_bw_limit) ||
	    (mac_srs->srs_bw->mac_bw_limit == 0)) {
		mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
		head = mac_srs->srs_first;
		mac_srs->srs_first = NULL;
		*chain_tail = mac_srs->srs_last;
		mac_srs->srs_last = NULL;
		*chain_sz = mac_srs->srs_size;
		*chain_cnt = mac_srs->srs_count;
		mac_srs->srs_count = 0;
		mac_srs->srs_size = 0;
		return (head);
	}

	/*
	 * Can't clear the entire backlog.
	 * Need to find how many packets to pick
	 */
	ASSERT(MUTEX_HELD(&mac_srs->srs_bw->mac_bw_lock));
	while ((mp = mac_srs->srs_first) != NULL) {
		sz = msgdsize(mp);
		if ((tsz + sz + mac_srs->srs_bw->mac_bw_used) >
		    mac_srs->srs_bw->mac_bw_limit) {
			if (!(mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED))
				mac_srs->srs_bw->mac_bw_state |=
				    SRS_BW_ENFORCED;
			break;
		}

		/*
		 * The _size & cnt is  decremented from the softrings
		 * when they send up the packet for polling to work
		 * properly.
		 */
		tsz += sz;
		cnt++;
		mac_srs->srs_count--;
		mac_srs->srs_size -= sz;
		if (tail != NULL)
			tail->b_next = mp;
		else
			head = mp;
		tail = mp;
		mac_srs->srs_first = mac_srs->srs_first->b_next;
	}
	mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
	if (mac_srs->srs_first == NULL)
		mac_srs->srs_last = NULL;

	if (tail != NULL)
		tail->b_next = NULL;
	*chain_tail = tail;
	*chain_cnt = cnt;
	*chain_sz = tsz;

	return (head);
}

/*
 * mac_rx_srs_drain
 *
 * The SRS drain routine. Gets to run to clear the queue. Any thread
 * (worker, interrupt, poll) can call this based on processing model.
 * The first thing we do is disable interrupts if possible and then
 * drain the queue. we also try to poll the underlying hardware if
 * there is a dedicated hardware Rx ring assigned to this SRS.
 *
 * There is a equivalent drain routine in bandwidth control mode
 * mac_rx_srs_drain_bw. There is some code duplication between the two
 * routines but they are highly performance sensitive and are easier
 * to read/debug if they stay separate. Any code changes here might
 * also apply to mac_rx_srs_drain_bw as well.
 */
void
mac_rx_srs_drain(mac_soft_ring_set_t *mac_srs, uint_t proc_type)
{
	mblk_t 			*head;
	mblk_t			*tail;
	timeout_id_t 		tid;
	int			cnt = 0;
	mac_client_impl_t	*mcip = mac_srs->srs_mcip;
	mac_srs_rx_t		*srs_rx = &mac_srs->srs_rx;

	ASSERT(MUTEX_HELD(&mac_srs->srs_lock));
	ASSERT(!(mac_srs->srs_type & SRST_BW_CONTROL));

	/* If we are blanked i.e. can't do upcalls, then we are done */
	if (mac_srs->srs_state & (SRS_BLANK | SRS_PAUSE)) {
		ASSERT((mac_srs->srs_type & SRST_NO_SOFT_RINGS) ||
		    (mac_srs->srs_state & SRS_PAUSE));
		goto out;
	}

	if (mac_srs->srs_first == NULL)
		goto out;

	if (!(mac_srs->srs_state & SRS_LATENCY_OPT) &&
	    (srs_rx->sr_poll_pkt_cnt <= srs_rx->sr_lowat)) {
		/*
		 * In the normal case, the SRS worker thread does no
		 * work and we wait for a backlog to build up before
		 * we switch into polling mode. In case we are
		 * optimizing for throughput, we use the worker thread
		 * as well. The goal is to let worker thread process
		 * the queue and poll thread to feed packets into
		 * the queue. As such, we should signal the poll
		 * thread to try and get more packets.
		 *
		 * We could have pulled this check in the POLL_RING
		 * macro itself but keeping it explicit here makes
		 * the architecture more human understandable.
		 */
		MAC_SRS_POLL_RING(mac_srs);
	}

again:
	head = mac_srs->srs_first;
	mac_srs->srs_first = NULL;
	tail = mac_srs->srs_last;
	mac_srs->srs_last = NULL;
	cnt = mac_srs->srs_count;
	mac_srs->srs_count = 0;

	ASSERT(head != NULL);
	ASSERT(tail != NULL);

	if ((tid = mac_srs->srs_tid) != 0)
		mac_srs->srs_tid = 0;

	mac_srs->srs_state |= (SRS_PROC|proc_type);


	/*
	 * mcip is NULL for broadcast and multicast flows. The promisc
	 * callbacks for broadcast and multicast packets are delivered from
	 * mac_rx() and we don't need to worry about that case in this path
	 */
	if (mcip != NULL && mcip->mci_promisc_list != NULL) {
		mutex_exit(&mac_srs->srs_lock);
		mac_promisc_client_dispatch(mcip, head);
		mutex_enter(&mac_srs->srs_lock);
	}

	/*
	 * Check if SRS itself is doing the processing
	 * This direct path does not apply when subflows are present. In this
	 * case, packets need to be dispatched to a soft ring according to the
	 * flow's bandwidth and other resources contraints.
	 */
	if (mac_srs->srs_type & SRST_NO_SOFT_RINGS) {
		mac_direct_rx_t		proc;
		void			*arg1;
		mac_resource_handle_t	arg2;

		/*
		 * This is the case when a Rx is directly
		 * assigned and we have a fully classified
		 * protocol chain. We can deal with it in
		 * one shot.
		 */
		proc = srs_rx->sr_func;
		arg1 = srs_rx->sr_arg1;
		arg2 = srs_rx->sr_arg2;

		mac_srs->srs_state |= SRS_CLIENT_PROC;
		mutex_exit(&mac_srs->srs_lock);
		if (tid != 0) {
			(void) untimeout(tid);
			tid = 0;
		}

		proc(arg1, arg2, head, NULL);
		/*
		 * Decrement the size and count here itelf
		 * since the packet has been processed.
		 */
		mutex_enter(&mac_srs->srs_lock);
		MAC_UPDATE_SRS_COUNT_LOCKED(mac_srs, cnt);
		if (mac_srs->srs_state & SRS_CLIENT_WAIT)
			cv_signal(&mac_srs->srs_client_cv);
		mac_srs->srs_state &= ~SRS_CLIENT_PROC;
	} else {
		/* Some kind of softrings based fanout is required */
		mutex_exit(&mac_srs->srs_lock);
		if (tid != 0) {
			(void) untimeout(tid);
			tid = 0;
		}

		/*
		 * Since the fanout routines can deal with chains,
		 * shoot the entire chain up.
		 */
		if (mac_srs->srs_type & SRST_FANOUT_SRC_IP)
			mac_rx_srs_fanout(mac_srs, head);
		else
			mac_rx_srs_proto_fanout(mac_srs, head);
		mutex_enter(&mac_srs->srs_lock);
	}

	if (!(mac_srs->srs_state & (SRS_BLANK|SRS_PAUSE)) &&
	    (mac_srs->srs_first != NULL)) {
		/*
		 * More packets arrived while we were clearing the
		 * SRS. This can be possible because of one of
		 * three conditions below:
		 * 1) The driver is using multiple worker threads
		 *    to send the packets to us.
		 * 2) The driver has a race in switching
		 *    between interrupt and polling mode or
		 * 3) Packets are arriving in this SRS via the
		 *    S/W classification as well.
		 *
		 * We should switch to polling mode and see if we
		 * need to send the poll thread down. Also, signal
		 * the worker thread to process whats just arrived.
		 */
		MAC_SRS_POLLING_ON(mac_srs);
		if (srs_rx->sr_poll_pkt_cnt <= srs_rx->sr_lowat) {
			srs_rx->sr_drain_poll_sig++;
			MAC_SRS_POLL_RING(mac_srs);
		}

		/*
		 * If we didn't signal the poll thread, we need
		 * to deal with the pending packets ourselves.
		 */
		if (proc_type == SRS_WORKER) {
			srs_rx->sr_drain_again++;
			goto again;
		} else {
			srs_rx->sr_drain_worker_sig++;
			cv_signal(&mac_srs->srs_async);
		}
	}

out:
	if (mac_srs->srs_state & SRS_GET_PKTS) {
		/*
		 * Poll thread is already running. Leave the
		 * SRS_RPOC set and hand over the control to
		 * poll thread.
		 */
		mac_srs->srs_state &= ~proc_type;
		srs_rx->sr_drain_poll_running++;
		return;
	}

	/*
	 * Even if there are no packets queued in SRS, we
	 * need to make sure that the shared counter is
	 * clear and any associated softrings have cleared
	 * all the backlog. Otherwise, leave the interface
	 * in polling mode and the poll thread will get
	 * signalled once the count goes down to zero.
	 *
	 * If someone is already draining the queue (SRS_PROC is
	 * set) when the srs_poll_pkt_cnt goes down to zero,
	 * then it means that drain is already running and we
	 * will turn off polling at that time if there is
	 * no backlog.
	 *
	 * As long as there are packets queued either
	 * in soft ring set or its soft rings, we will leave
	 * the interface in polling mode (even if the drain
	 * was done being the interrupt thread). We signal
	 * the poll thread as well if we have dipped below
	 * low water mark.
	 *
	 * NOTE: We can't use the MAC_SRS_POLLING_ON macro
	 * since that turn polling on only for worker thread.
	 * Its not worth turning polling on for interrupt
	 * thread (since NIC will not issue another interrupt)
	 * unless a backlog builds up.
	 */
	if ((srs_rx->sr_poll_pkt_cnt > 0) &&
	    (mac_srs->srs_state & SRS_POLLING_CAPAB)) {
		mac_srs->srs_state &= ~(SRS_PROC|proc_type);
		srs_rx->sr_drain_keep_polling++;
		MAC_SRS_POLLING_ON(mac_srs);
		if (srs_rx->sr_poll_pkt_cnt <= srs_rx->sr_lowat)
			MAC_SRS_POLL_RING(mac_srs);
		return;
	}

	/* Nothing else to do. Get out of poll mode */
	MAC_SRS_POLLING_OFF(mac_srs);
	mac_srs->srs_state &= ~(SRS_PROC|proc_type);
	srs_rx->sr_drain_finish_intr++;
}

/*
 * mac_rx_srs_drain_bw
 *
 * The SRS BW drain routine. Gets to run to clear the queue. Any thread
 * (worker, interrupt, poll) can call this based on processing model.
 * The first thing we do is disable interrupts if possible and then
 * drain the queue. we also try to poll the underlying hardware if
 * there is a dedicated hardware Rx ring assigned to this SRS.
 *
 * There is a equivalent drain routine in non bandwidth control mode
 * mac_rx_srs_drain. There is some code duplication between the two
 * routines but they are highly performance sensitive and are easier
 * to read/debug if they stay separate. Any code changes here might
 * also apply to mac_rx_srs_drain as well.
 */
void
mac_rx_srs_drain_bw(mac_soft_ring_set_t *mac_srs, uint_t proc_type)
{
	mblk_t 			*head;
	mblk_t			*tail;
	timeout_id_t 		tid;
	size_t			sz = 0;
	int			cnt = 0;
	mac_client_impl_t	*mcip = mac_srs->srs_mcip;
	mac_srs_rx_t		*srs_rx = &mac_srs->srs_rx;
	clock_t			now;

	ASSERT(MUTEX_HELD(&mac_srs->srs_lock));
	ASSERT(mac_srs->srs_type & SRST_BW_CONTROL);
again:
	/* Check if we are doing B/W control */
	mutex_enter(&mac_srs->srs_bw->mac_bw_lock);
	now = ddi_get_lbolt();
	if (mac_srs->srs_bw->mac_bw_curr_time != now) {
		mac_srs->srs_bw->mac_bw_curr_time = now;
		mac_srs->srs_bw->mac_bw_used = 0;
		if (mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED)
			mac_srs->srs_bw->mac_bw_state &= ~SRS_BW_ENFORCED;
	} else if (mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED) {
		mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
		goto done;
	} else if (mac_srs->srs_bw->mac_bw_used >
	    mac_srs->srs_bw->mac_bw_limit) {
		mac_srs->srs_bw->mac_bw_state |= SRS_BW_ENFORCED;
		mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
		goto done;
	}
	mutex_exit(&mac_srs->srs_bw->mac_bw_lock);

	/* If we are blanked i.e. can't do upcalls, then we are done */
	if (mac_srs->srs_state & (SRS_BLANK | SRS_PAUSE)) {
		ASSERT((mac_srs->srs_type & SRST_NO_SOFT_RINGS) ||
		    (mac_srs->srs_state & SRS_PAUSE));
		goto done;
	}

	sz = 0;
	cnt = 0;
	if ((head = mac_srs_pick_chain(mac_srs, &tail, &sz, &cnt)) == NULL) {
		/*
		 * We couldn't pick up a single packet.
		 */
		mutex_enter(&mac_srs->srs_bw->mac_bw_lock);
		if ((mac_srs->srs_bw->mac_bw_used == 0) &&
		    (mac_srs->srs_size != 0) &&
		    !(mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED)) {
			/*
			 * Seems like configured B/W doesn't
			 * even allow processing of 1 packet
			 * per tick.
			 *
			 * XXX: raise the limit to processing
			 * at least 1 packet per tick.
			 */
			mac_srs->srs_bw->mac_bw_limit +=
			    mac_srs->srs_bw->mac_bw_limit;
			mac_srs->srs_bw->mac_bw_drop_threshold +=
			    mac_srs->srs_bw->mac_bw_drop_threshold;
			cmn_err(CE_NOTE, "mac_rx_srs_drain: srs(%p) "
			    "raised B/W limit to %d since not even a "
			    "single packet can be processed per "
			    "tick %d\n", (void *)mac_srs,
			    (int)mac_srs->srs_bw->mac_bw_limit,
			    (int)msgdsize(mac_srs->srs_first));
		}
		mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
		goto done;
	}

	ASSERT(head != NULL);
	ASSERT(tail != NULL);

	/* zero bandwidth: drop all and return to interrupt mode */
	mutex_enter(&mac_srs->srs_bw->mac_bw_lock);
	if (mac_srs->srs_bw->mac_bw_limit == 0) {
		srs_rx->sr_drop_count += cnt;
		ASSERT(mac_srs->srs_bw->mac_bw_sz >= sz);
		mac_srs->srs_bw->mac_bw_sz -= sz;
		mac_srs->srs_bw->mac_bw_drop_bytes += sz;
		mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
		mac_pkt_drop(NULL, NULL, head, B_FALSE);
		goto leave_poll;
	} else {
		mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
	}

	if ((tid = mac_srs->srs_tid) != 0)
		mac_srs->srs_tid = 0;

	mac_srs->srs_state |= (SRS_PROC|proc_type);
	MAC_SRS_WORKER_POLLING_ON(mac_srs);

	/*
	 * mcip is NULL for broadcast and multicast flows. The promisc
	 * callbacks for broadcast and multicast packets are delivered from
	 * mac_rx() and we don't need to worry about that case in this path
	 */
	if (mcip != NULL && mcip->mci_promisc_list != NULL) {
		mutex_exit(&mac_srs->srs_lock);
		mac_promisc_client_dispatch(mcip, head);
		mutex_enter(&mac_srs->srs_lock);
	}

	/*
	 * Check if SRS itself is doing the processing
	 * This direct path does not apply when subflows are present. In this
	 * case, packets need to be dispatched to a soft ring according to the
	 * flow's bandwidth and other resources contraints.
	 */
	if (mac_srs->srs_type & SRST_NO_SOFT_RINGS) {
		mac_direct_rx_t		proc;
		void			*arg1;
		mac_resource_handle_t	arg2;

		/*
		 * This is the case when a Rx is directly
		 * assigned and we have a fully classified
		 * protocol chain. We can deal with it in
		 * one shot.
		 */
		proc = srs_rx->sr_func;
		arg1 = srs_rx->sr_arg1;
		arg2 = srs_rx->sr_arg2;

		mac_srs->srs_state |= SRS_CLIENT_PROC;
		mutex_exit(&mac_srs->srs_lock);
		if (tid != 0) {
			(void) untimeout(tid);
			tid = 0;
		}

		proc(arg1, arg2, head, NULL);
		/*
		 * Decrement the size and count here itelf
		 * since the packet has been processed.
		 */
		mutex_enter(&mac_srs->srs_lock);
		MAC_UPDATE_SRS_COUNT_LOCKED(mac_srs, cnt);
		MAC_UPDATE_SRS_SIZE_LOCKED(mac_srs, sz);

		if (mac_srs->srs_state & SRS_CLIENT_WAIT)
			cv_signal(&mac_srs->srs_client_cv);
		mac_srs->srs_state &= ~SRS_CLIENT_PROC;
	} else {
		/* Some kind of softrings based fanout is required */
		mutex_exit(&mac_srs->srs_lock);
		if (tid != 0) {
			(void) untimeout(tid);
			tid = 0;
		}

		/*
		 * Since the fanout routines can deal with chains,
		 * shoot the entire chain up.
		 */
		if (mac_srs->srs_type & SRST_FANOUT_SRC_IP)
			mac_rx_srs_fanout(mac_srs, head);
		else
			mac_rx_srs_proto_fanout(mac_srs, head);
		mutex_enter(&mac_srs->srs_lock);
	}

	/*
	 * Send the poll thread to pick up any packets arrived
	 * so far. This also serves as the last check in case
	 * nothing else is queued in the SRS. The poll thread
	 * is signalled only in the case the drain was done
	 * by the worker thread and SRS_WORKER is set. The
	 * worker thread can run in parallel as long as the
	 * SRS_WORKER flag is set. We we have nothing else to
	 * process, we can exit while leaving SRS_PROC set
	 * which gives the poll thread control to process and
	 * cleanup once it returns from the NIC.
	 *
	 * If we have nothing else to process, we need to
	 * ensure that we keep holding the srs_lock till
	 * all the checks below are done and control is
	 * handed to the poll thread if it was running.
	 */
	mutex_enter(&mac_srs->srs_bw->mac_bw_lock);
	if (!(mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED)) {
		if (mac_srs->srs_first != NULL) {
			if (proc_type == SRS_WORKER) {
				mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
				if (srs_rx->sr_poll_pkt_cnt <=
				    srs_rx->sr_lowat)
					MAC_SRS_POLL_RING(mac_srs);
				goto again;
			} else {
				cv_signal(&mac_srs->srs_async);
			}
		}
	}
	mutex_exit(&mac_srs->srs_bw->mac_bw_lock);

done:

	if (mac_srs->srs_state & SRS_GET_PKTS) {
		/*
		 * Poll thread is already running. Leave the
		 * SRS_RPOC set and hand over the control to
		 * poll thread.
		 */
		mac_srs->srs_state &= ~proc_type;
		return;
	}

	/*
	 * If we can't process packets because we have exceeded
	 * B/W limit for this tick, just set the timeout
	 * and leave.
	 *
	 * Even if there are no packets queued in SRS, we
	 * need to make sure that the shared counter is
	 * clear and any associated softrings have cleared
	 * all the backlog. Otherwise, leave the interface
	 * in polling mode and the poll thread will get
	 * signalled once the count goes down to zero.
	 *
	 * If someone is already draining the queue (SRS_PROC is
	 * set) when the srs_poll_pkt_cnt goes down to zero,
	 * then it means that drain is already running and we
	 * will turn off polling at that time if there is
	 * no backlog. As long as there are packets queued either
	 * is soft ring set or its soft rings, we will leave
	 * the interface in polling mode.
	 */
	mutex_enter(&mac_srs->srs_bw->mac_bw_lock);
	if ((mac_srs->srs_state & SRS_POLLING_CAPAB) &&
	    ((mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED) ||
	    (srs_rx->sr_poll_pkt_cnt > 0))) {
		MAC_SRS_POLLING_ON(mac_srs);
		mac_srs->srs_state &= ~(SRS_PROC|proc_type);
		if ((mac_srs->srs_first != NULL) &&
		    (mac_srs->srs_tid == NULL))
			mac_srs->srs_tid = timeout(mac_srs_fire,
			    mac_srs, 1);
		mutex_exit(&mac_srs->srs_bw->mac_bw_lock);
		return;
	}
	mutex_exit(&mac_srs->srs_bw->mac_bw_lock);

leave_poll:

	/* Nothing else to do. Get out of poll mode */
	MAC_SRS_POLLING_OFF(mac_srs);
	mac_srs->srs_state &= ~(SRS_PROC|proc_type);
}

/*
 * mac_srs_worker
 *
 * The SRS worker routine. Drains the queue when no one else is
 * processing it.
 */
void
mac_srs_worker(mac_soft_ring_set_t *mac_srs)
{
	kmutex_t 		*lock = &mac_srs->srs_lock;
	kcondvar_t 		*async = &mac_srs->srs_async;
	callb_cpr_t		cprinfo;
	boolean_t		bw_ctl_flag;

	CALLB_CPR_INIT(&cprinfo, lock, callb_generic_cpr, "srs_worker");
	mutex_enter(lock);

start:
	for (;;) {
		bw_ctl_flag = B_FALSE;
		if (mac_srs->srs_type & SRST_BW_CONTROL) {
			MAC_SRS_BW_LOCK(mac_srs);
			MAC_SRS_CHECK_BW_CONTROL(mac_srs);
			if (mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED)
				bw_ctl_flag = B_TRUE;
			MAC_SRS_BW_UNLOCK(mac_srs);
		}
		/*
		 * The SRS_BW_ENFORCED flag may change since we have dropped
		 * the mac_bw_lock. However the drain function can handle both
		 * a drainable SRS or a bandwidth controlled SRS, and the
		 * effect of scheduling a timeout is to wakeup the worker
		 * thread which in turn will call the drain function. Since
		 * we release the srs_lock atomically only in the cv_wait there
		 * isn't a fear of waiting for ever.
		 */
		while (((mac_srs->srs_state & SRS_PROC) ||
		    (mac_srs->srs_first == NULL) || bw_ctl_flag ||
		    (mac_srs->srs_state & SRS_TX_BLOCKED)) &&
		    !(mac_srs->srs_state & SRS_PAUSE)) {
			/*
			 * If we have packets queued and we are here
			 * because B/W control is in place, we better
			 * schedule the worker wakeup after 1 tick
			 * to see if bandwidth control can be relaxed.
			 */
			if (bw_ctl_flag && mac_srs->srs_tid == NULL) {
				/*
				 * We need to ensure that a timer  is already
				 * scheduled or we force  schedule one for
				 * later so that we can continue processing
				 * after this  quanta is over.
				 */
				mac_srs->srs_tid = timeout(mac_srs_fire,
				    mac_srs, 1);
			}
wait:
			CALLB_CPR_SAFE_BEGIN(&cprinfo);
			cv_wait(async, lock);
			CALLB_CPR_SAFE_END(&cprinfo, lock);

			if (mac_srs->srs_state & SRS_PAUSE)
				goto done;
			if (mac_srs->srs_state & SRS_PROC)
				goto wait;

			if (mac_srs->srs_first != NULL &&
			    mac_srs->srs_type & SRST_BW_CONTROL) {
				MAC_SRS_BW_LOCK(mac_srs);
				if (mac_srs->srs_bw->mac_bw_state &
				    SRS_BW_ENFORCED) {
					MAC_SRS_CHECK_BW_CONTROL(mac_srs);
				}
				bw_ctl_flag = mac_srs->srs_bw->mac_bw_state &
				    SRS_BW_ENFORCED;
				MAC_SRS_BW_UNLOCK(mac_srs);
			}
		}

		if (mac_srs->srs_state & SRS_PAUSE)
			goto done;
		mac_srs->srs_drain_func(mac_srs, SRS_WORKER);
	}
done:
	/*
	 * The Rx SRS quiesce logic first cuts off packet supply to the SRS
	 * from both hard and soft classifications and waits for such threads
	 * to finish before signaling the worker. So at this point the only
	 * thread left that could be competing with the worker is the poll
	 * thread. In the case of Tx, there shouldn't be any thread holding
	 * SRS_PROC at this point.
	 */
	if (!(mac_srs->srs_state & SRS_PROC)) {
		mac_srs->srs_state |= SRS_PROC;
	} else {
		ASSERT((mac_srs->srs_type & SRST_TX) == 0);
		/*
		 * Poll thread still owns the SRS and is still running
		 */
		ASSERT((mac_srs->srs_poll_thr == NULL) ||
		    ((mac_srs->srs_state & SRS_POLL_THR_OWNER) ==
		    SRS_POLL_THR_OWNER));
	}
	mac_srs_worker_quiesce(mac_srs);
	/*
	 * Wait for the SRS_RESTART or SRS_CONDEMNED signal from the initiator
	 * of the quiesce operation
	 */
	while (!(mac_srs->srs_state & (SRS_CONDEMNED | SRS_RESTART)))
		cv_wait(&mac_srs->srs_async, &mac_srs->srs_lock);

	if (mac_srs->srs_state & SRS_RESTART) {
		ASSERT(!(mac_srs->srs_state & SRS_CONDEMNED));
		mac_srs_worker_restart(mac_srs);
		mac_srs->srs_state &= ~SRS_PROC;
		goto start;
	}

	if (!(mac_srs->srs_state & SRS_CONDEMNED_DONE))
		mac_srs_worker_quiesce(mac_srs);

	mac_srs->srs_state &= ~SRS_PROC;
	/* The macro drops the srs_lock */
	CALLB_CPR_EXIT(&cprinfo);
	thread_exit();
}

/*
 * mac_rx_srs_subflow_process
 *
 * Receive side routine called from interrupt path when there are
 * sub flows present on this SRS.
 */
/* ARGSUSED */
void
mac_rx_srs_subflow_process(void *arg, mac_resource_handle_t srs,
    mblk_t *mp_chain, boolean_t loopback)
{
	flow_entry_t		*flent = NULL;
	flow_entry_t		*prev_flent = NULL;
	mblk_t			*mp = NULL;
	mblk_t			*tail = NULL;
	mac_soft_ring_set_t	*mac_srs = (mac_soft_ring_set_t *)srs;
	mac_client_impl_t	*mcip;

	mcip = mac_srs->srs_mcip;
	ASSERT(mcip != NULL);

	/*
	 * We need to determine the SRS for every packet
	 * by walking the flow table, if we don't get any,
	 * then we proceed using the SRS we came with.
	 */
	mp = tail = mp_chain;
	while (mp != NULL) {

		/*
		 * We will increment the stats for the mactching subflow.
		 * when we get the bytes/pkt count for the classified packets
		 * later in mac_rx_srs_process.
		 */
		(void) mac_flow_lookup(mcip->mci_subflow_tab, mp,
		    FLOW_INBOUND, &flent);

		if (mp == mp_chain || flent == prev_flent) {
			if (prev_flent != NULL)
				FLOW_REFRELE(prev_flent);
			prev_flent = flent;
			flent = NULL;
			tail = mp;
			mp = mp->b_next;
			continue;
		}
		tail->b_next = NULL;
		/*
		 * A null indicates, this is for the mac_srs itself.
		 * XXX-venu : probably assert for fe_rx_srs_cnt == 0.
		 */
		if (prev_flent == NULL || prev_flent->fe_rx_srs_cnt == 0) {
			mac_rx_srs_process(arg,
			    (mac_resource_handle_t)mac_srs, mp_chain,
			    loopback);
		} else {
			(prev_flent->fe_cb_fn)(prev_flent->fe_cb_arg1,
			    prev_flent->fe_cb_arg2, mp_chain, loopback);
			FLOW_REFRELE(prev_flent);
		}
		prev_flent = flent;
		flent = NULL;
		mp_chain = mp;
		tail = mp;
		mp = mp->b_next;
	}
	/* Last chain */
	ASSERT(mp_chain != NULL);
	if (prev_flent == NULL || prev_flent->fe_rx_srs_cnt == 0) {
		mac_rx_srs_process(arg,
		    (mac_resource_handle_t)mac_srs, mp_chain, loopback);
	} else {
		(prev_flent->fe_cb_fn)(prev_flent->fe_cb_arg1,
		    prev_flent->fe_cb_arg2, mp_chain, loopback);
		FLOW_REFRELE(prev_flent);
	}
}

/*
 * mac_rx_srs_process
 *
 * Receive side routine called from the interrupt path.
 *
 * loopback is set to force a context switch on the loopback
 * path between MAC clients.
 */
/* ARGSUSED */
void
mac_rx_srs_process(void *arg, mac_resource_handle_t srs, mblk_t *mp_chain,
    boolean_t loopback)
{
	mac_soft_ring_set_t	*mac_srs = (mac_soft_ring_set_t *)srs;
	mblk_t			*mp, *tail, *head;
	int			count = 0;
	int			count1;
	size_t			sz = 0;
	size_t			chain_sz, sz1;
	mac_bw_ctl_t		*mac_bw;
	mac_client_impl_t	*smcip;
	mac_srs_rx_t		*srs_rx = &mac_srs->srs_rx;

	/*
	 * Set the tail, count and sz. We set the sz irrespective
	 * of whether we are doing B/W control or not for the
	 * purpose of updating the stats.
	 */
	mp = tail = mp_chain;
	while (mp != NULL) {
		tail = mp;
		count++;
		sz += msgdsize(mp);
		mp = mp->b_next;
	}

	mutex_enter(&mac_srs->srs_lock);
	smcip = mac_srs->srs_mcip;

	if (mac_srs->srs_type & SRST_FLOW || smcip == NULL) {
		FLOW_STAT_UPDATE(mac_srs->srs_flent, rbytes, sz);
		FLOW_STAT_UPDATE(mac_srs->srs_flent, ipackets, count);
	}
	if (smcip != NULL) {
		smcip->mci_stat_ibytes += sz;
		smcip->mci_stat_ipackets += count;
	}

	/*
	 * If the SRS in already being processed; has been blanked;
	 * can be processed by worker thread only; or the B/W limit
	 * has been reached, then queue the chain and check if
	 * worker thread needs to be awakend.
	 */
	if (mac_srs->srs_type & SRST_BW_CONTROL) {
		mac_bw = mac_srs->srs_bw;
		ASSERT(mac_bw != NULL);
		mutex_enter(&mac_bw->mac_bw_lock);
		/* Count the packets and bytes via interrupt */
		srs_rx->sr_intr_count += count;
		mac_bw->mac_bw_intr += sz;
		if (mac_bw->mac_bw_limit == 0) {
			/* zero bandwidth: drop all */
			srs_rx->sr_drop_count += count;
			mac_bw->mac_bw_drop_bytes += sz;
			mutex_exit(&mac_bw->mac_bw_lock);
			mutex_exit(&mac_srs->srs_lock);
			mac_pkt_drop(NULL, NULL, mp_chain, B_FALSE);
			return;
		} else {
			if ((mac_bw->mac_bw_sz + sz) <=
			    mac_bw->mac_bw_drop_threshold) {
				mutex_exit(&mac_bw->mac_bw_lock);
				MAC_RX_SRS_ENQUEUE_CHAIN(mac_srs, mp_chain,
				    tail, count, sz);
			} else {
				mp = mp_chain;
				chain_sz = 0;
				count1 = 0;
				tail = NULL;
				head = NULL;
				while (mp != NULL) {
					sz1 = msgdsize(mp);
					if (mac_bw->mac_bw_sz + chain_sz + sz1 >
					    mac_bw->mac_bw_drop_threshold)
						break;
					chain_sz += sz1;
					count1++;
					tail = mp;
					mp = mp->b_next;
				}
				mutex_exit(&mac_bw->mac_bw_lock);
				if (tail != NULL) {
					head = tail->b_next;
					tail->b_next = NULL;
					MAC_RX_SRS_ENQUEUE_CHAIN(mac_srs,
					    mp_chain, tail, count1, chain_sz);
					sz -= chain_sz;
					count -= count1;
				} else {
					/* Can't pick up any */
					head = mp_chain;
				}
				if (head != NULL) {
					/* Drop any packet over the threshold */
					srs_rx->sr_drop_count += count;
					mutex_enter(&mac_bw->mac_bw_lock);
					mac_bw->mac_bw_drop_bytes += sz;
					mutex_exit(&mac_bw->mac_bw_lock);
					freemsgchain(head);
				}
			}
			MAC_SRS_WORKER_WAKEUP(mac_srs);
			mutex_exit(&mac_srs->srs_lock);
			return;
		}
	}

	/*
	 * If the total number of packets queued in the SRS and
	 * its associated soft rings exceeds the max allowed,
	 * then drop the chain. If we are polling capable, this
	 * shouldn't be happening.
	 */
	if (!(mac_srs->srs_type & SRST_BW_CONTROL) &&
	    (srs_rx->sr_poll_pkt_cnt > srs_rx->sr_hiwat)) {
		mac_bw = mac_srs->srs_bw;
		srs_rx->sr_drop_count += count;
		mutex_enter(&mac_bw->mac_bw_lock);
		mac_bw->mac_bw_drop_bytes += sz;
		mutex_exit(&mac_bw->mac_bw_lock);
		freemsgchain(mp_chain);
		mutex_exit(&mac_srs->srs_lock);
		return;
	}

	MAC_RX_SRS_ENQUEUE_CHAIN(mac_srs, mp_chain, tail, count, sz);
	/* Count the packets entering via interrupt path */
	srs_rx->sr_intr_count += count;

	if (!(mac_srs->srs_state & SRS_PROC)) {
		/*
		 * If we are coming via loopback or if we are not
		 * optimizing for latency, we should signal the
		 * worker thread.
		 */
		if (loopback || !(mac_srs->srs_state & SRS_LATENCY_OPT)) {
			/*
			 * For loopback, We need to let the worker take
			 * over as we don't want to continue in the same
			 * thread even if we can. This could lead to stack
			 * overflows and may also end up using
			 * resources (cpu) incorrectly.
			 */
			cv_signal(&mac_srs->srs_async);
		} else {
			/*
			 * Seems like no one is processing the SRS and
			 * there is no backlog. We also inline process
			 * our packet if its a single packet in non
			 * latency optimized case (in latency optimized
			 * case, we inline process chains of any size).
			 */
			mac_srs->srs_drain_func(mac_srs, SRS_PROC_FAST);
		}
	}
	mutex_exit(&mac_srs->srs_lock);
}

/* TX SIDE ROUTINES (RUNTIME) */

/*
 * mac_tx_srs_no_desc
 *
 * This routine is called by Tx single ring default mode
 * when Tx ring runs out of descs.
 */
mac_tx_cookie_t
mac_tx_srs_no_desc(mac_soft_ring_set_t *mac_srs, mblk_t *mp_chain,
    uint16_t flag, mblk_t **ret_mp)
{
	mac_tx_cookie_t cookie = NULL;
	mac_srs_tx_t *srs_tx = &mac_srs->srs_tx;
	boolean_t wakeup_worker = B_TRUE;
	uint32_t tx_mode = srs_tx->st_mode;
	int cnt, sz;
	mblk_t *tail;

	ASSERT(tx_mode == SRS_TX_DEFAULT || tx_mode == SRS_TX_BW);
	if (flag & MAC_DROP_ON_NO_DESC) {
		MAC_TX_SRS_DROP_MESSAGE(mac_srs, mp_chain, cookie);
	} else {
		if (mac_srs->srs_first != NULL)
			wakeup_worker = B_FALSE;
		MAC_COUNT_CHAIN(mac_srs, mp_chain, tail, cnt, sz);
		if (flag & MAC_TX_NO_ENQUEUE) {
			/*
			 * If TX_QUEUED is not set, queue the
			 * packet and let mac_tx_srs_drain()
			 * set the TX_BLOCKED bit for the
			 * reasons explained above. Otherwise,
			 * return the mblks.
			 */
			if (wakeup_worker) {
				MAC_TX_SRS_ENQUEUE_CHAIN(mac_srs,
				    mp_chain, tail, cnt, sz);
			} else {
				MAC_TX_SET_NO_ENQUEUE(mac_srs,
				    mp_chain, ret_mp, cookie);
			}
		} else {
			MAC_TX_SRS_TEST_HIWAT(mac_srs, mp_chain,
			    tail, cnt, sz, cookie);
		}
		if (wakeup_worker)
			cv_signal(&mac_srs->srs_async);
	}
	return (cookie);
}

/*
 * mac_tx_srs_enqueue
 *
 * This routine is called when Tx SRS is operating in either serializer
 * or bandwidth mode. In serializer mode, a packet will get enqueued
 * when a thread cannot enter SRS exclusively. In bandwidth mode,
 * packets gets queued if allowed byte-count limit for a tick is
 * exceeded. The action that gets taken when MAC_DROP_ON_NO_DESC and
 * MAC_TX_NO_ENQUEUE is set is different than when operaing in either
 * the default mode or fanout mode. Here packets get dropped or
 * returned back to the caller only after hi-watermark worth of data
 * is queued.
 */
static mac_tx_cookie_t
mac_tx_srs_enqueue(mac_soft_ring_set_t *mac_srs, mblk_t *mp_chain,
    uint16_t flag, uintptr_t fanout_hint, mblk_t **ret_mp)
{
	mac_tx_cookie_t cookie = NULL;
	int cnt, sz;
	mblk_t *tail;
	boolean_t wakeup_worker = B_TRUE;

	/*
	 * Ignore fanout hint if we don't have multiple tx rings.
	 */
	if (!TX_MULTI_RING_MODE(mac_srs))
		fanout_hint = 0;

	if (mac_srs->srs_first != NULL)
		wakeup_worker = B_FALSE;
	MAC_COUNT_CHAIN(mac_srs, mp_chain, tail, cnt, sz);
	if (flag & MAC_DROP_ON_NO_DESC) {
		if (mac_srs->srs_count > mac_srs->srs_tx.st_hiwat) {
			MAC_TX_SRS_DROP_MESSAGE(mac_srs, mp_chain, cookie);
		} else {
			MAC_TX_SRS_ENQUEUE_CHAIN(mac_srs,
			    mp_chain, tail, cnt, sz);
		}
	} else if (flag & MAC_TX_NO_ENQUEUE) {
		if ((mac_srs->srs_count > mac_srs->srs_tx.st_hiwat) ||
		    (mac_srs->srs_state & SRS_TX_WAKEUP_CLIENT)) {
			MAC_TX_SET_NO_ENQUEUE(mac_srs, mp_chain,
			    ret_mp, cookie);
		} else {
			mp_chain->b_prev = (mblk_t *)fanout_hint;
			MAC_TX_SRS_ENQUEUE_CHAIN(mac_srs,
			    mp_chain, tail, cnt, sz);
		}
	} else {
		/*
		 * If you are BW_ENFORCED, just enqueue the
		 * packet. srs_worker will drain it at the
		 * prescribed rate. Before enqueueing, save
		 * the fanout hint.
		 */
		mp_chain->b_prev = (mblk_t *)fanout_hint;
		MAC_TX_SRS_TEST_HIWAT(mac_srs, mp_chain,
		    tail, cnt, sz, cookie);
	}
	if (wakeup_worker)
		cv_signal(&mac_srs->srs_async);
	return (cookie);
}

/*
 * There are five tx modes:
 *
 * 1) Default mode (SRS_TX_DEFAULT)
 * 2) Serialization mode (SRS_TX_SERIALIZE)
 * 3) Fanout mode (SRS_TX_FANOUT)
 * 4) Bandwdith mode (SRS_TX_BW)
 * 5) Fanout and Bandwidth mode (SRS_TX_BW_FANOUT)
 *
 * The tx mode in which an SRS operates is decided in mac_tx_srs_setup()
 * based on the number of Tx rings requested for an SRS and whether
 * bandwidth control is requested or not.
 *
 * In the default mode (i.e., no fanout/no bandwidth), the SRS acts as a
 * pass-thru. Packets will go directly to mac_tx_send(). When the underlying
 * Tx ring runs out of Tx descs, it starts queueing up packets in SRS.
 * When flow-control is relieved, the srs_worker drains the queued
 * packets and informs blocked clients to restart sending packets.
 *
 * In the SRS_TX_SERIALIZE mode, all calls to mac_tx() are serialized.
 *
 * In the SRS_TX_FANOUT mode, packets will be fanned out to multiple
 * Tx rings. Each Tx ring will have a soft ring associated with it.
 * These soft rings will be hung off the Tx SRS. Queueing if it happens
 * due to lack of Tx desc will be in individual soft ring (and not srs)
 * associated with Tx ring.
 *
 * In the TX_BW mode, tx srs will allow packets to go down to Tx ring
 * only if bw is available. Otherwise the packets will be queued in
 * SRS. If fanout to multiple Tx rings is configured, the packets will
 * be fanned out among the soft rings associated with the Tx rings.
 *
 * Four flags are used in srs_state for indicating flow control
 * conditions : SRS_TX_BLOCKED, SRS_TX_HIWAT, SRS_TX_WAKEUP_CLIENT.
 * SRS_TX_BLOCKED indicates out of Tx descs. SRS expects a wakeup from the
 * driver below.
 * SRS_TX_HIWAT indicates packet count enqueued in Tx SRS exceeded Tx hiwat
 * and flow-control pressure is applied back to clients. The clients expect
 * wakeup when flow-control is relieved.
 * SRS_TX_WAKEUP_CLIENT get set when (flag == MAC_TX_NO_ENQUEUE) and mblk
 * got returned back to client either due to lack of Tx descs or due to bw
 * control reasons. The clients expect a wakeup when condition is relieved.
 *
 * The fourth argument to mac_tx() is the flag. Normally it will be 0 but
 * some clients set the following values too: MAC_DROP_ON_NO_DESC,
 * MAC_TX_NO_ENQUEUE
 * Mac clients that do not want packets to be enqueued in the mac layer set
 * MAC_DROP_ON_NO_DESC value. The packets won't be queued in the Tx SRS or
 * Tx soft rings but instead get dropped when the NIC runs out of desc. The
 * behaviour of this flag is different when the Tx is running in serializer
 * or bandwidth mode. Under these (Serializer, bandwidth) modes, the packet
 * get dropped when Tx high watermark is reached.
 * There are some mac clients like vsw, aggr that want the mblks to be
 * returned back to clients instead of being queued in Tx SRS (or Tx soft
 * rings) under flow-control (i.e., out of desc or exceeding bw limits)
 * conditions. These clients call mac_tx() with MAC_TX_NO_ENQUEUE flag set.
 * In the default and Tx fanout mode, the un-transmitted mblks will be
 * returned back to the clients when the driver runs out of Tx descs.
 * SRS_TX_WAKEUP_CLIENT (or S_RING_WAKEUP_CLIENT) will be set in SRS (or
 * soft ring) so that the clients can be woken up when Tx desc become
 * available. When running in serializer or bandwidth mode mode,
 * SRS_TX_WAKEUP_CLIENT will be set when tx hi-watermark is reached.
 */

mac_tx_func_t
mac_tx_get_func(uint32_t mode)
{
	return (mac_tx_mode_list[mode].mac_tx_func);
}

/* ARGSUSED */
static mac_tx_cookie_t
mac_tx_single_ring_mode(mac_soft_ring_set_t *mac_srs, mblk_t *mp_chain,
    uintptr_t fanout_hint, uint16_t flag, mblk_t **ret_mp)
{
	mac_srs_tx_t		*srs_tx = &mac_srs->srs_tx;
	boolean_t		is_subflow;
	mac_tx_stats_t		stats;
	mac_tx_cookie_t		cookie = NULL;

	ASSERT(srs_tx->st_mode == SRS_TX_DEFAULT);

	/* Regular case with a single Tx ring */
	/*
	 * SRS_TX_BLOCKED is set when underlying NIC runs
	 * out of Tx descs and messages start getting
	 * queued. It won't get reset until
	 * tx_srs_drain() completely drains out the
	 * messages.
	 */
	if ((mac_srs->srs_state & SRS_ENQUEUED) != 0) {
		/* Tx descs/resources not available */
		mutex_enter(&mac_srs->srs_lock);
		if ((mac_srs->srs_state & SRS_ENQUEUED) != 0) {
			cookie = mac_tx_srs_no_desc(mac_srs, mp_chain,
			    flag, ret_mp);
			mutex_exit(&mac_srs->srs_lock);
			return (cookie);
		}
		/*
		 * While we were computing mblk count, the
		 * flow control condition got relieved.
		 * Continue with the transmission.
		 */
		mutex_exit(&mac_srs->srs_lock);
	}

	is_subflow = ((mac_srs->srs_type & SRST_FLOW) != 0);

	mp_chain = mac_tx_send(srs_tx->st_arg1, srs_tx->st_arg2,
	    mp_chain, (is_subflow ? &stats : NULL));

	/*
	 * Multiple threads could be here sending packets.
	 * Under such conditions, it is not possible to
	 * automically set SRS_TX_BLOCKED bit to indicate
	 * out of tx desc condition. To atomically set
	 * this, we queue the returned packet and do
	 * the setting of SRS_TX_BLOCKED in
	 * mac_tx_srs_drain().
	 */
	if (mp_chain != NULL) {
		mutex_enter(&mac_srs->srs_lock);
		cookie = mac_tx_srs_no_desc(mac_srs, mp_chain, flag, ret_mp);
		mutex_exit(&mac_srs->srs_lock);
		return (cookie);
	}

	if (is_subflow)
		FLOW_TX_STATS_UPDATE(mac_srs->srs_flent, &stats);

	return (NULL);
}

/*
 * mac_tx_serialize_mode
 *
 * This is an experimental mode implemented as per the request of PAE.
 * In this mode, all callers attempting to send a packet to the NIC
 * will get serialized. Only one thread at any time will access the
 * NIC to send the packet out.
 */
/* ARGSUSED */
static mac_tx_cookie_t
mac_tx_serializer_mode(mac_soft_ring_set_t *mac_srs, mblk_t *mp_chain,
    uintptr_t fanout_hint, uint16_t flag, mblk_t **ret_mp)
{
	boolean_t		is_subflow;
	mac_tx_stats_t		stats;
	mac_tx_cookie_t		cookie = NULL;
	mac_srs_tx_t		*srs_tx = &mac_srs->srs_tx;

	/* Single ring, serialize below */
	ASSERT(srs_tx->st_mode == SRS_TX_SERIALIZE);
	mutex_enter(&mac_srs->srs_lock);
	if ((mac_srs->srs_first != NULL) ||
	    (mac_srs->srs_state & SRS_PROC)) {
		/*
		 * In serialization mode, queue all packets until
		 * TX_HIWAT is set.
		 * If drop bit is set, drop if TX_HIWAT is set.
		 * If no_enqueue is set, still enqueue until hiwat
		 * is set and return mblks after TX_HIWAT is set.
		 */
		cookie = mac_tx_srs_enqueue(mac_srs, mp_chain,
		    flag, NULL, ret_mp);
		mutex_exit(&mac_srs->srs_lock);
		return (cookie);
	}
	/*
	 * No packets queued, nothing on proc and no flow
	 * control condition. Fast-path, ok. Do inline
	 * processing.
	 */
	mac_srs->srs_state |= SRS_PROC;
	mutex_exit(&mac_srs->srs_lock);

	is_subflow = ((mac_srs->srs_type & SRST_FLOW) != 0);

	mp_chain = mac_tx_send(srs_tx->st_arg1, srs_tx->st_arg2,
	    mp_chain, (is_subflow ? &stats : NULL));

	mutex_enter(&mac_srs->srs_lock);
	mac_srs->srs_state &= ~SRS_PROC;
	if (mp_chain != NULL) {
		cookie = mac_tx_srs_enqueue(mac_srs,
		    mp_chain, flag, NULL, ret_mp);
	}
	if (mac_srs->srs_first != NULL) {
		/*
		 * We processed inline our packet and a new
		 * packet/s got queued while we were
		 * processing. Wakeup srs worker
		 */
		cv_signal(&mac_srs->srs_async);
	}
	mutex_exit(&mac_srs->srs_lock);

	if (is_subflow && cookie == NULL)
		FLOW_TX_STATS_UPDATE(mac_srs->srs_flent, &stats);

	return (cookie);
}

/*
 * mac_tx_fanout_mode
 *
 * In this mode, the SRS will have access to multiple Tx rings to send
 * the packet out. The fanout hint that is passed as an argument is
 * used to find an appropriate ring to fanout the traffic. Each Tx
 * ring, in turn,  will have a soft ring associated with it. If a Tx
 * ring runs out of Tx desc's the returned packet will be queued in
 * the soft ring associated with that Tx ring. The srs itself will not
 * queue any packets.
 */

#define	MAC_TX_SOFT_RING_PROCESS(chain) {		       		\
	index = COMPUTE_INDEX(hash, mac_srs->srs_oth_ring_count),	\
	softring = mac_srs->srs_oth_soft_rings[index];			\
	cookie = mac_tx_soft_ring_process(softring, chain, flag, ret_mp); \
	DTRACE_PROBE2(tx__fanout, uint64_t, hash, uint_t, index);	\
}

static mac_tx_cookie_t
mac_tx_fanout_mode(mac_soft_ring_set_t *mac_srs, mblk_t *mp_chain,
    uintptr_t fanout_hint, uint16_t flag, mblk_t **ret_mp)
{
	mac_soft_ring_t		*softring;
	uint64_t		hash;
	uint_t			index;
	mac_tx_cookie_t		cookie = NULL;

	ASSERT(mac_srs->srs_tx.st_mode == SRS_TX_FANOUT);
	if (fanout_hint != 0) {
		/*
		 * The hint is specified by the caller, simply pass the
		 * whole chain to the soft ring.
		 */
		hash = HASH_HINT(fanout_hint);
		MAC_TX_SOFT_RING_PROCESS(mp_chain);
	} else {
		mblk_t *last_mp, *cur_mp, *sub_chain;
		uint64_t last_hash = 0;
		uint_t media = mac_srs->srs_mcip->mci_mip->mi_info.mi_media;

		/*
		 * Compute the hash from the contents (headers) of the
		 * packets of the mblk chain. Split the chains into
		 * subchains of the same conversation.
		 *
		 * Since there may be more than one ring used for
		 * sub-chains of the same call, and since the caller
		 * does not maintain per conversation state since it
		 * passed a zero hint, unsent subchains will be
		 * dropped.
		 */

		flag |= MAC_DROP_ON_NO_DESC;
		ret_mp = NULL;

		ASSERT(ret_mp == NULL);

		sub_chain = NULL;
		last_mp = NULL;

		for (cur_mp = mp_chain; cur_mp != NULL;
		    cur_mp = cur_mp->b_next) {
			hash = mac_pkt_hash(media, cur_mp, MAC_PKT_HASH_L4,
			    B_TRUE);
			if (last_hash != 0 && hash != last_hash) {
				/*
				 * Starting a different subchain, send current
				 * chain out.
				 */
				ASSERT(last_mp != NULL);
				last_mp->b_next = NULL;
				MAC_TX_SOFT_RING_PROCESS(sub_chain);
				sub_chain = NULL;
			}

			/* add packet to subchain */
			if (sub_chain == NULL)
				sub_chain = cur_mp;
			last_mp = cur_mp;
			last_hash = hash;
		}

		if (sub_chain != NULL) {
			/* send last subchain */
			ASSERT(last_mp != NULL);
			last_mp->b_next = NULL;
			MAC_TX_SOFT_RING_PROCESS(sub_chain);
		}

		cookie = NULL;
	}

	return (cookie);
}

/*
 * mac_tx_bw_mode
 *
 * In the bandwidth mode, Tx srs will allow packets to go down to Tx ring
 * only if bw is available. Otherwise the packets will be queued in
 * SRS. If the SRS has multiple Tx rings, then packets will get fanned
 * out to a Tx rings.
 */
static mac_tx_cookie_t
mac_tx_bw_mode(mac_soft_ring_set_t *mac_srs, mblk_t *mp_chain,
    uintptr_t fanout_hint, uint16_t flag, mblk_t **ret_mp)
{
	int			cnt, sz;
	mblk_t			*tail;
	mac_tx_cookie_t		cookie = NULL;
	mac_srs_tx_t		*srs_tx = &mac_srs->srs_tx;
	clock_t			now;

	ASSERT(TX_BANDWIDTH_MODE(mac_srs));
	ASSERT(mac_srs->srs_type & SRST_BW_CONTROL);
	mutex_enter(&mac_srs->srs_lock);
	if (mac_srs->srs_bw->mac_bw_limit == 0) {
		/*
		 * zero bandwidth, no traffic is sent: drop the packets,
		 * or return the whole chain if the caller requests all
		 * unsent packets back.
		 */
		if (flag & MAC_TX_NO_ENQUEUE) {
			cookie = (mac_tx_cookie_t)mac_srs;
			*ret_mp = mp_chain;
		} else {
			MAC_TX_SRS_DROP_MESSAGE(mac_srs, mp_chain, cookie);
		}
		mutex_exit(&mac_srs->srs_lock);
		return (cookie);
	} else if ((mac_srs->srs_first != NULL) ||
	    (mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED)) {
		cookie = mac_tx_srs_enqueue(mac_srs, mp_chain, flag,
		    fanout_hint, ret_mp);
		mutex_exit(&mac_srs->srs_lock);
		return (cookie);
	}
	MAC_COUNT_CHAIN(mac_srs, mp_chain, tail, cnt, sz);
	now = ddi_get_lbolt();
	if (mac_srs->srs_bw->mac_bw_curr_time != now) {
		mac_srs->srs_bw->mac_bw_curr_time = now;
		mac_srs->srs_bw->mac_bw_used = 0;
	} else if (mac_srs->srs_bw->mac_bw_used >
	    mac_srs->srs_bw->mac_bw_limit) {
		mac_srs->srs_bw->mac_bw_state |= SRS_BW_ENFORCED;
		MAC_TX_SRS_ENQUEUE_CHAIN(mac_srs,
		    mp_chain, tail, cnt, sz);
		/*
		 * Wakeup worker thread. Note that worker
		 * thread has to be woken up so that it
		 * can fire up the timer to be woken up
		 * on the next tick. Also once
		 * BW_ENFORCED is set, it can only be
		 * reset by srs_worker thread. Until then
		 * all packets will get queued up in SRS
		 * and hence this this code path won't be
		 * entered until BW_ENFORCED is reset.
		 */
		cv_signal(&mac_srs->srs_async);
		mutex_exit(&mac_srs->srs_lock);
		return (cookie);
	}

	mac_srs->srs_bw->mac_bw_used += sz;
	mutex_exit(&mac_srs->srs_lock);

	if (srs_tx->st_mode == SRS_TX_BW_FANOUT) {
		mac_soft_ring_t *softring;
		uint_t indx, hash;

		hash = HASH_HINT(fanout_hint);
		indx = COMPUTE_INDEX(hash,
		    mac_srs->srs_oth_ring_count);
		softring = mac_srs->srs_oth_soft_rings[indx];
		return (mac_tx_soft_ring_process(softring, mp_chain, flag,
		    ret_mp));
	} else {
		boolean_t		is_subflow;
		mac_tx_stats_t		stats;

		is_subflow = ((mac_srs->srs_type & SRST_FLOW) != 0);

		mp_chain = mac_tx_send(srs_tx->st_arg1, srs_tx->st_arg2,
		    mp_chain, (is_subflow ? &stats : NULL));

		if (mp_chain != NULL) {
			mutex_enter(&mac_srs->srs_lock);
			MAC_COUNT_CHAIN(mac_srs, mp_chain, tail, cnt, sz);
			if (mac_srs->srs_bw->mac_bw_used > sz)
				mac_srs->srs_bw->mac_bw_used -= sz;
			else
				mac_srs->srs_bw->mac_bw_used = 0;
			cookie = mac_tx_srs_enqueue(mac_srs, mp_chain, flag,
			    fanout_hint, ret_mp);
			mutex_exit(&mac_srs->srs_lock);
			return (cookie);
		}
		if (is_subflow)
			FLOW_TX_STATS_UPDATE(mac_srs->srs_flent, &stats);

		return (NULL);
	}
}

/* ARGSUSED */
void
mac_tx_srs_drain(mac_soft_ring_set_t *mac_srs, uint_t proc_type)
{
	mblk_t			*head, *tail;
	size_t			sz;
	uint32_t		tx_mode;
	uint_t			saved_pkt_count;
	boolean_t		is_subflow;
	mac_tx_stats_t		stats;
	mac_srs_tx_t		*srs_tx = &mac_srs->srs_tx;
	clock_t			now;

	saved_pkt_count = 0;
	ASSERT(mutex_owned(&mac_srs->srs_lock));
	ASSERT(!(mac_srs->srs_state & SRS_PROC));

	mac_srs->srs_state |= SRS_PROC;

	is_subflow = ((mac_srs->srs_type & SRST_FLOW) != 0);
	tx_mode = srs_tx->st_mode;
	if (tx_mode == SRS_TX_DEFAULT || tx_mode == SRS_TX_SERIALIZE) {
		if (mac_srs->srs_first != NULL) {
			head = mac_srs->srs_first;
			tail = mac_srs->srs_last;
			saved_pkt_count = mac_srs->srs_count;
			mac_srs->srs_first = NULL;
			mac_srs->srs_last = NULL;
			mac_srs->srs_count = 0;
			mutex_exit(&mac_srs->srs_lock);

			head = mac_tx_send(srs_tx->st_arg1, srs_tx->st_arg2,
			    head, &stats);

			mutex_enter(&mac_srs->srs_lock);
			if (head != NULL) {
				/* Device out of tx desc, set block */
				if (head->b_next == NULL)
					VERIFY(head == tail);
				tail->b_next = mac_srs->srs_first;
				mac_srs->srs_first = head;
				mac_srs->srs_count +=
				    (saved_pkt_count - stats.ts_opackets);
				if (mac_srs->srs_last == NULL)
					mac_srs->srs_last = tail;
				MAC_TX_SRS_BLOCK(mac_srs, head);
			} else {
				srs_tx->st_woken_up = B_FALSE;
				if (is_subflow) {
					FLOW_TX_STATS_UPDATE(
					    mac_srs->srs_flent, &stats);
				}
			}
		}
	} else if (tx_mode == SRS_TX_BW) {
		/*
		 * We are here because the timer fired and we have some data
		 * to tranmit. Also mac_tx_srs_worker should have reset
		 * SRS_BW_ENFORCED flag
		 */
		ASSERT(!(mac_srs->srs_bw->mac_bw_state & SRS_BW_ENFORCED));
		head = tail = mac_srs->srs_first;
		while (mac_srs->srs_first != NULL) {
			tail = mac_srs->srs_first;
			tail->b_prev = NULL;
			mac_srs->srs_first = tail->b_next;
			if (mac_srs->srs_first == NULL)
				mac_srs->srs_last = NULL;
			mac_srs->srs_count--;
			sz = msgdsize(tail);
			mac_srs->srs_size -= sz;
			saved_pkt_count++;
			MAC_TX_UPDATE_BW_INFO(mac_srs, sz);

			if (mac_srs->srs_bw->mac_bw_used <
			    mac_srs->srs_bw->mac_bw_limit)
				continue;

			now = ddi_get_lbolt();
			if (mac_srs->srs_bw->mac_bw_curr_time != now) {
				mac_srs->srs_bw->mac_bw_curr_time = now;
				mac_srs->srs_bw->mac_bw_used = sz;
				continue;
			}
			mac_srs->srs_bw->mac_bw_state |= SRS_BW_ENFORCED;
			break;
		}

		ASSERT((head == NULL && tail == NULL) ||
		    (head != NULL && tail != NULL));
		if (tail != NULL) {
			tail->b_next = NULL;
			mutex_exit(&mac_srs->srs_lock);

			head = mac_tx_send(srs_tx->st_arg1, srs_tx->st_arg2,
			    head, &stats);

			mutex_enter(&mac_srs->srs_lock);
			if (head != NULL) {
				uint_t size_sent;

				/* Device out of tx desc, set block */
				if (head->b_next == NULL)
					VERIFY(head == tail);
				tail->b_next = mac_srs->srs_first;
				mac_srs->srs_first = head;
				mac_srs->srs_count +=
				    (saved_pkt_count - stats.ts_opackets);
				if (mac_srs->srs_last == NULL)
					mac_srs->srs_last = tail;
				size_sent = sz - stats.ts_obytes;
				mac_srs->srs_size += size_sent;
				mac_srs->srs_bw->mac_bw_sz += size_sent;
				if (mac_srs->srs_bw->mac_bw_used > size_sent) {
					mac_srs->srs_bw->mac_bw_used -=
					    size_sent;
				} else {
					mac_srs->srs_bw->mac_bw_used = 0;
				}
				MAC_TX_SRS_BLOCK(mac_srs, head);
			} else {
				srs_tx->st_woken_up = B_FALSE;
				if (is_subflow) {
					FLOW_TX_STATS_UPDATE(
					    mac_srs->srs_flent, &stats);
				}
			}
		}
	} else if (tx_mode == SRS_TX_BW_FANOUT) {
		mblk_t *prev;
		mac_soft_ring_t *softring;
		uint64_t hint;

		/*
		 * We are here because the timer fired and we
		 * have some quota to tranmit.
		 */
		prev = NULL;
		head = tail = mac_srs->srs_first;
		while (mac_srs->srs_first != NULL) {
			tail = mac_srs->srs_first;
			mac_srs->srs_first = tail->b_next;
			if (mac_srs->srs_first == NULL)
				mac_srs->srs_last = NULL;
			mac_srs->srs_count--;
			sz = msgdsize(tail);
			mac_srs->srs_size -= sz;
			mac_srs->srs_bw->mac_bw_used += sz;
			if (prev == NULL)
				hint = (ulong_t)tail->b_prev;
			if (hint != (ulong_t)tail->b_prev) {
				prev->b_next = NULL;
				mutex_exit(&mac_srs->srs_lock);
				TX_SRS_TO_SOFT_RING(mac_srs, head, hint);
				head = tail;
				hint = (ulong_t)tail->b_prev;
				mutex_enter(&mac_srs->srs_lock);
			}

			prev = tail;
			tail->b_prev = NULL;
			if (mac_srs->srs_bw->mac_bw_used <
			    mac_srs->srs_bw->mac_bw_limit)
				continue;

			now = ddi_get_lbolt();
			if (mac_srs->srs_bw->mac_bw_curr_time != now) {
				mac_srs->srs_bw->mac_bw_curr_time = now;
				mac_srs->srs_bw->mac_bw_used = 0;
				continue;
			}
			mac_srs->srs_bw->mac_bw_state |= SRS_BW_ENFORCED;
			break;
		}
		ASSERT((head == NULL && tail == NULL) ||
		    (head != NULL && tail != NULL));
		if (tail != NULL) {
			tail->b_next = NULL;
			mutex_exit(&mac_srs->srs_lock);
			TX_SRS_TO_SOFT_RING(mac_srs, head, hint);
			mutex_enter(&mac_srs->srs_lock);
		}
	}
	/*
	 * SRS_TX_FANOUT case not considered here because packets
	 * won't be queued in the SRS for this case. Packets will
	 * be sent directly to soft rings underneath and if there
	 * is any queueing at all, it would be in Tx side soft
	 * rings.
	 */

	/*
	 * When srs_count becomes 0, reset SRS_TX_HIWAT and
	 * SRS_TX_WAKEUP_CLIENT and wakeup registered clients.
	 */
	if (mac_srs->srs_count == 0 && (mac_srs->srs_state &
	    (SRS_TX_HIWAT | SRS_TX_WAKEUP_CLIENT | SRS_ENQUEUED))) {
		mac_tx_notify_cb_t *mtnfp;
		mac_cb_t *mcb;
		mac_client_impl_t *mcip = mac_srs->srs_mcip;
		boolean_t wakeup_required = B_FALSE;

		if (mac_srs->srs_state &
		    (SRS_TX_HIWAT|SRS_TX_WAKEUP_CLIENT)) {
			wakeup_required = B_TRUE;
		}
		mac_srs->srs_state &= ~(SRS_TX_HIWAT |
		    SRS_TX_WAKEUP_CLIENT | SRS_ENQUEUED);
		mutex_exit(&mac_srs->srs_lock);
		if (wakeup_required) {
			/* Wakeup callback registered clients */
			MAC_CALLBACK_WALKER_INC(&mcip->mci_tx_notify_cb_info);
			for (mcb = mcip->mci_tx_notify_cb_list; mcb != NULL;
			    mcb = mcb->mcb_nextp) {
				mtnfp = (mac_tx_notify_cb_t *)mcb->mcb_objp;
				mtnfp->mtnf_fn(mtnfp->mtnf_arg,
				    (mac_tx_cookie_t)mac_srs);
			}
			MAC_CALLBACK_WALKER_DCR(&mcip->mci_tx_notify_cb_info,
			    &mcip->mci_tx_notify_cb_list);
			/*
			 * If the client is not the primary MAC client, then we
			 * need to send the notification to the clients upper
			 * MAC, i.e. mci_upper_mip.
			 */
			mac_tx_notify(mcip->mci_upper_mip != NULL ?
			    mcip->mci_upper_mip : mcip->mci_mip);
		}
		mutex_enter(&mac_srs->srs_lock);
	}
	mac_srs->srs_state &= ~SRS_PROC;
}

/*
 * Given a packet, get the flow_entry that identifies the flow
 * to which that packet belongs. The flow_entry will contain
 * the transmit function to be used to send the packet. If the
 * function returns NULL, the packet should be sent using the
 * underlying NIC.
 */
static flow_entry_t *
mac_tx_classify(mac_impl_t *mip, mblk_t *mp)
{
	flow_entry_t		*flent = NULL;
	mac_client_impl_t	*mcip;
	int	err;

	/*
	 * Do classification on the packet.
	 */
	err = mac_flow_lookup(mip->mi_flow_tab, mp, FLOW_OUTBOUND, &flent);
	if (err != 0)
		return (NULL);

	/*
	 * This flent might just be an additional one on the MAC client,
	 * i.e. for classification purposes (different fdesc), however
	 * the resources, SRS et. al., are in the mci_flent, so if
	 * this isn't the mci_flent, we need to get it.
	 */
	if ((mcip = flent->fe_mcip) != NULL && mcip->mci_flent != flent) {
		FLOW_REFRELE(flent);
		flent = mcip->mci_flent;
		FLOW_TRY_REFHOLD(flent, err);
		if (err != 0)
			return (NULL);
	}

	return (flent);
}

/*
 * This macro is only meant to be used by mac_tx_send().
 */
#define	CHECK_VID_AND_ADD_TAG(mp) {			\
	if (vid_check) {				\
		int err = 0;				\
							\
		MAC_VID_CHECK(src_mcip, (mp), err);	\
		if (err != 0) {				\
			freemsg((mp));			\
			(mp) = next;			\
			oerrors++;			\
			continue;			\
		}					\
	}						\
	if (add_tag) {					\
		(mp) = mac_add_vlan_tag((mp), 0, vid);	\
		if ((mp) == NULL) {			\
			(mp) = next;			\
			oerrors++;			\
			continue;			\
		}					\
	}						\
}

mblk_t *
mac_tx_send(mac_client_handle_t mch, mac_ring_handle_t ring, mblk_t *mp_chain,
    mac_tx_stats_t *stats)
{
	mac_client_impl_t *src_mcip = (mac_client_impl_t *)mch;
	mac_impl_t *mip = src_mcip->mci_mip;
	uint_t obytes = 0, opackets = 0, oerrors = 0;
	mblk_t *mp = NULL, *next;
	boolean_t vid_check, add_tag;
	uint16_t vid = 0;

	if (mip->mi_nclients > 1) {
		vid_check = MAC_VID_CHECK_NEEDED(src_mcip);
		add_tag = MAC_TAG_NEEDED(src_mcip);
		if (add_tag)
			vid = mac_client_vid(mch);
	} else {
		ASSERT(mip->mi_nclients == 1);
		vid_check = add_tag = B_FALSE;
	}

	/*
	 * Fastpath: if there's only one client, and there's no
	 * multicast listeners, we simply send the packet down to the
	 * underlying NIC.
	 */
	if (mip->mi_nactiveclients == 1 && mip->mi_promisc_list == NULL)  {
		DTRACE_PROBE2(fastpath,
		    mac_client_impl_t *, src_mcip, mblk_t *, mp_chain);

		mp = mp_chain;
		while (mp != NULL) {
			next = mp->b_next;
			mp->b_next = NULL;
			opackets++;
			obytes += (mp->b_cont == NULL ? MBLKL(mp) :
			    msgdsize(mp));

			CHECK_VID_AND_ADD_TAG(mp);
			MAC_TX(mip, ring, mp,
			    ((src_mcip->mci_state_flags & MCIS_SHARE_BOUND) !=
			    0));

			/*
			 * If the driver is out of descriptors and does a
			 * partial send it will return a chain of unsent
			 * mblks. Adjust the accounting stats.
			 */
			if (mp != NULL) {
				opackets--;
				obytes -= msgdsize(mp);
				mp->b_next = next;
				break;
			}
			mp = next;
		}
		goto done;
	}

	/*
	 * No fastpath, we either have more than one MAC client
	 * defined on top of the same MAC, or one or more MAC
	 * client promiscuous callbacks.
	 */
	DTRACE_PROBE3(slowpath, mac_client_impl_t *,
	    src_mcip, int, mip->mi_nclients, mblk_t *, mp_chain);

	mp = mp_chain;
	while (mp != NULL) {
		flow_entry_t *dst_flow_ent;
		void *flow_cookie;
		size_t	pkt_size;
		mblk_t *mp1;

		next = mp->b_next;
		mp->b_next = NULL;
		opackets++;
		pkt_size = (mp->b_cont == NULL ? MBLKL(mp) : msgdsize(mp));
		obytes += pkt_size;
		CHECK_VID_AND_ADD_TAG(mp);

		/*
		 * Check if there are promiscuous mode callbacks defined.
		 */
		if (mip->mi_promisc_list != NULL)
			mac_promisc_dispatch(mip, mp, src_mcip);

		/*
		 * Find the destination.
		 */
		dst_flow_ent = mac_tx_classify(mip, mp);

		if (dst_flow_ent != NULL) {
			size_t	hdrsize;
			int	err = 0;

			if (mip->mi_info.mi_nativemedia == DL_ETHER) {
				struct ether_vlan_header *evhp =
				    (struct ether_vlan_header *)mp->b_rptr;

				if (ntohs(evhp->ether_tpid) == ETHERTYPE_VLAN)
					hdrsize = sizeof (*evhp);
				else
					hdrsize = sizeof (struct ether_header);
			} else {
				mac_header_info_t	mhi;

				err = mac_header_info((mac_handle_t)mip,
				    mp, &mhi);
				if (err == 0)
					hdrsize = mhi.mhi_hdrsize;
			}

			/*
			 * Got a matching flow. It's either another
			 * MAC client, or a broadcast/multicast flow.
			 * Make sure the packet size is within the
			 * allowed size. If not drop the packet and
			 * move to next packet.
			 */
			if (err != 0 ||
			    (pkt_size - hdrsize) > mip->mi_sdu_max) {
				oerrors++;
				DTRACE_PROBE2(loopback__drop, size_t, pkt_size,
				    mblk_t *, mp);
				freemsg(mp);
				mp = next;
				FLOW_REFRELE(dst_flow_ent);
				continue;
			}
			flow_cookie = mac_flow_get_client_cookie(dst_flow_ent);
			if (flow_cookie != NULL) {
				/*
				 * The vnic_bcast_send function expects
				 * to receive the sender MAC client
				 * as value for arg2.
				 */
				mac_bcast_send(flow_cookie, src_mcip, mp,
				    B_TRUE);
			} else {
				/*
				 * loopback the packet to a
				 * local MAC client. We force a context
				 * switch if both source and destination
				 * MAC clients are used by IP, i.e. bypass
				 * is set.
				 */
				boolean_t do_switch;
				mac_client_impl_t *dst_mcip =
				    dst_flow_ent->fe_mcip;

				do_switch = ((src_mcip->mci_state_flags &
				    dst_mcip->mci_state_flags &
				    MCIS_CLIENT_POLL_CAPABLE) != 0);

				if ((mp1 = mac_fix_cksum(mp)) != NULL) {
					(dst_flow_ent->fe_cb_fn)(
					    dst_flow_ent->fe_cb_arg1,
					    dst_flow_ent->fe_cb_arg2,
					    mp1, do_switch);
				}
			}
			FLOW_REFRELE(dst_flow_ent);
		} else {
			/*
			 * Unknown destination, send via the underlying
			 * NIC.
			 */
			MAC_TX(mip, ring, mp,
			    ((src_mcip->mci_state_flags & MCIS_SHARE_BOUND) !=
			    0));
			if (mp != NULL) {
				/*
				 * Adjust for the last packet that
				 * could not be transmitted
				 */
				opackets--;
				obytes -= pkt_size;
				mp->b_next = next;
				break;
			}
		}
		mp = next;
	}

done:
	src_mcip->mci_stat_obytes += obytes;
	src_mcip->mci_stat_opackets += opackets;
	src_mcip->mci_stat_oerrors += oerrors;

	if (stats != NULL) {
		stats->ts_opackets = opackets;
		stats->ts_obytes = obytes;
		stats->ts_oerrors = oerrors;
	}
	return (mp);
}

/*
 * mac_tx_srs_ring_present
 *
 * Returns whether the specified ring is part of the specified SRS.
 */
boolean_t
mac_tx_srs_ring_present(mac_soft_ring_set_t *srs, mac_ring_t *tx_ring)
{
	int i;
	mac_soft_ring_t *soft_ring;

	if (srs->srs_tx.st_arg2 == tx_ring)
		return (B_TRUE);

	for (i = 0; i < srs->srs_oth_ring_count; i++) {
		soft_ring =  srs->srs_oth_soft_rings[i];
		if (soft_ring->s_ring_tx_arg2 == tx_ring)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * mac_tx_srs_wakeup
 *
 * Called when Tx desc become available. Wakeup the appropriate worker
 * thread after resetting the SRS_TX_BLOCKED/S_RING_BLOCK bit in the
 * state field.
 */
void
mac_tx_srs_wakeup(mac_soft_ring_set_t *mac_srs, mac_ring_handle_t ring)
{
	int i;
	mac_soft_ring_t *sringp;
	mac_srs_tx_t *srs_tx = &mac_srs->srs_tx;

	mutex_enter(&mac_srs->srs_lock);
	if (TX_SINGLE_RING_MODE(mac_srs)) {
		if (srs_tx->st_arg2 == ring &&
		    mac_srs->srs_state & SRS_TX_BLOCKED) {
			mac_srs->srs_state &= ~SRS_TX_BLOCKED;
			srs_tx->st_unblocked_cnt++;
			cv_signal(&mac_srs->srs_async);
		}
		/*
		 * A wakeup can come before tx_srs_drain() could
		 * grab srs lock and set SRS_TX_BLOCKED. So
		 * always set woken_up flag when we come here.
		 */
		srs_tx->st_woken_up = B_TRUE;
		mutex_exit(&mac_srs->srs_lock);
		return;
	}

	/* If you are here, it is for FANOUT or BW_FANOUT case */
	ASSERT(TX_MULTI_RING_MODE(mac_srs));
	for (i = 0; i < mac_srs->srs_oth_ring_count; i++) {
		sringp = mac_srs->srs_oth_soft_rings[i];
		mutex_enter(&sringp->s_ring_lock);
		if (sringp->s_ring_tx_arg2 == ring) {
			if (sringp->s_ring_state & S_RING_BLOCK) {
				sringp->s_ring_state &= ~S_RING_BLOCK;
				sringp->s_ring_unblocked_cnt++;
				cv_signal(&sringp->s_ring_async);
			}
			sringp->s_ring_tx_woken_up = B_TRUE;
		}
		mutex_exit(&sringp->s_ring_lock);
	}
	mutex_exit(&mac_srs->srs_lock);
}

/*
 * Once the driver is done draining, send a MAC_NOTE_TX notification to unleash
 * the blocked clients again.
 */
void
mac_tx_notify(mac_impl_t *mip)
{
	i_mac_notify(mip, MAC_NOTE_TX);
}

/*
 * RX SOFTRING RELATED FUNCTIONS
 *
 * These functions really belong in mac_soft_ring.c and here for
 * a short period.
 */

#define	SOFT_RING_ENQUEUE_CHAIN(ringp, mp, tail, cnt, sz) {	       	\
	/*								\
	 * Enqueue our mblk chain.					\
	 */								\
	ASSERT(MUTEX_HELD(&(ringp)->s_ring_lock));			\
									\
	if ((ringp)->s_ring_last != NULL)				\
		(ringp)->s_ring_last->b_next = (mp);			\
	else								\
		(ringp)->s_ring_first = (mp);				\
	(ringp)->s_ring_last = (tail);					\
	(ringp)->s_ring_count += (cnt);					\
	ASSERT((ringp)->s_ring_count > 0);				\
	if ((ringp)->s_ring_type & ST_RING_BW_CTL) {			\
		(ringp)->s_ring_size += sz;				\
	}								\
}

/*
 * Default entry point to deliver a packet chain to a MAC client.
 * If the MAC client has flows, do the classification with these
 * flows as well.
 */
/* ARGSUSED */
void
mac_rx_deliver(void *arg1, mac_resource_handle_t mrh, mblk_t *mp_chain,
    mac_header_info_t *arg3)
{
	mac_client_impl_t *mcip = arg1;

	if (mcip->mci_nvids == 1 &&
	    !(mcip->mci_state_flags & MCIS_STRIP_DISABLE)) {
		/*
		 * If the client has exactly one VID associated with it
		 * and striping of VLAN header is not disabled,
		 * remove the VLAN tag from the packet before
		 * passing it on to the client's receive callback.
		 * Note that this needs to be done after we dispatch
		 * the packet to the promiscuous listeners of the
		 * client, since they expect to see the whole
		 * frame including the VLAN headers.
		 */
		mp_chain = mac_strip_vlan_tag_chain(mp_chain);
	}

	mcip->mci_rx_fn(mcip->mci_rx_arg, mrh, mp_chain, B_FALSE);
}

/*
 * mac_rx_soft_ring_process
 *
 * process a chain for a given soft ring. The number of packets queued
 * in the SRS and its associated soft rings (including this one) is
 * very small (tracked by srs_poll_pkt_cnt), then allow the entering
 * thread (interrupt or poll thread) to do inline processing. This
 * helps keep the latency down under low load.
 *
 * The proc and arg for each mblk is already stored in the mblk in
 * appropriate places.
 */
/* ARGSUSED */
void
mac_rx_soft_ring_process(mac_client_impl_t *mcip, mac_soft_ring_t *ringp,
    mblk_t *mp_chain, mblk_t *tail, int cnt, size_t sz)
{
	mac_direct_rx_t		proc;
	void			*arg1;
	mac_resource_handle_t	arg2;
	mac_soft_ring_set_t	*mac_srs = ringp->s_ring_set;

	ASSERT(ringp != NULL);
	ASSERT(mp_chain != NULL);
	ASSERT(tail != NULL);
	ASSERT(MUTEX_NOT_HELD(&ringp->s_ring_lock));

	mutex_enter(&ringp->s_ring_lock);
	ringp->s_ring_total_inpkt += cnt;
	if ((mac_srs->srs_rx.sr_poll_pkt_cnt <= 1) &&
	    !(ringp->s_ring_type & ST_RING_WORKER_ONLY)) {
		/* If on processor or blanking on, then enqueue and return */
		if (ringp->s_ring_state & S_RING_BLANK ||
		    ringp->s_ring_state & S_RING_PROC) {
			SOFT_RING_ENQUEUE_CHAIN(ringp, mp_chain, tail, cnt, sz);
			mutex_exit(&ringp->s_ring_lock);
			return;
		}
		proc = ringp->s_ring_rx_func;
		arg1 = ringp->s_ring_rx_arg1;
		arg2 = ringp->s_ring_rx_arg2;
		/*
		 * See if anything is already queued. If we are the
		 * first packet, do inline processing else queue the
		 * packet and do the drain.
		 */
		if (ringp->s_ring_first == NULL) {
			/*
			 * Fast-path, ok to process and nothing queued.
			 */
			ringp->s_ring_run = curthread;
			ringp->s_ring_state |= (S_RING_PROC);

			mutex_exit(&ringp->s_ring_lock);

			/*
			 * We are the chain of 1 packet so
			 * go through this fast path.
			 */
			ASSERT(mp_chain->b_next == NULL);

			(*proc)(arg1, arg2, mp_chain, NULL);

			ASSERT(MUTEX_NOT_HELD(&ringp->s_ring_lock));
			/*
			 * If we have a soft ring set which is doing
			 * bandwidth control, we need to decrement
			 * srs_size and count so it the SRS can have a
			 * accurate idea of what is the real data
			 * queued between SRS and its soft rings. We
			 * decrement the counters only when the packet
			 * gets processed by both SRS and the soft ring.
			 */
			mutex_enter(&mac_srs->srs_lock);
			MAC_UPDATE_SRS_COUNT_LOCKED(mac_srs, cnt);
			MAC_UPDATE_SRS_SIZE_LOCKED(mac_srs, sz);
			mutex_exit(&mac_srs->srs_lock);

			mutex_enter(&ringp->s_ring_lock);
			ringp->s_ring_run = NULL;
			ringp->s_ring_state &= ~S_RING_PROC;
			if (ringp->s_ring_state & S_RING_CLIENT_WAIT)
				cv_signal(&ringp->s_ring_client_cv);

			if ((ringp->s_ring_first == NULL) ||
			    (ringp->s_ring_state & S_RING_BLANK)) {
				/*
				 * We processed inline our packet and
				 * nothing new has arrived or our
				 * receiver doesn't want to receive
				 * any packets. We are done.
				 */
				mutex_exit(&ringp->s_ring_lock);
				return;
			}
		} else {
			SOFT_RING_ENQUEUE_CHAIN(ringp,
			    mp_chain, tail, cnt, sz);
		}

		/*
		 * We are here because either we couldn't do inline
		 * processing (because something was already
		 * queued), or we had a chain of more than one
		 * packet, or something else arrived after we were
		 * done with inline processing.
		 */
		ASSERT(MUTEX_HELD(&ringp->s_ring_lock));
		ASSERT(ringp->s_ring_first != NULL);

		ringp->s_ring_drain_func(ringp);
		mutex_exit(&ringp->s_ring_lock);
		return;
	} else {
		/* ST_RING_WORKER_ONLY case */
		SOFT_RING_ENQUEUE_CHAIN(ringp, mp_chain, tail, cnt, sz);
		mac_soft_ring_worker_wakeup(ringp);
		mutex_exit(&ringp->s_ring_lock);
	}
}

/*
 * TX SOFTRING RELATED FUNCTIONS
 *
 * These functions really belong in mac_soft_ring.c and here for
 * a short period.
 */

#define	TX_SOFT_RING_ENQUEUE_CHAIN(ringp, mp, tail, cnt, sz) {	       	\
	ASSERT(MUTEX_HELD(&ringp->s_ring_lock));			\
	ringp->s_ring_state |= S_RING_ENQUEUED;				\
	SOFT_RING_ENQUEUE_CHAIN(ringp, mp_chain, tail, cnt, sz);	\
}

/*
 * mac_tx_sring_queued
 *
 * When we are out of transmit descriptors and we already have a
 * queue that exceeds hiwat (or the client called us with
 * MAC_TX_NO_ENQUEUE or MAC_DROP_ON_NO_DESC flag), return the
 * soft ring pointer as the opaque cookie for the client enable
 * flow control.
 */
static mac_tx_cookie_t
mac_tx_sring_enqueue(mac_soft_ring_t *ringp, mblk_t *mp_chain, uint16_t flag,
    mblk_t **ret_mp)
{
	int cnt;
	size_t sz;
	mblk_t *tail;
	mac_soft_ring_set_t *mac_srs = ringp->s_ring_set;
	mac_tx_cookie_t cookie = NULL;
	boolean_t wakeup_worker = B_TRUE;

	ASSERT(MUTEX_HELD(&ringp->s_ring_lock));
	MAC_COUNT_CHAIN(mac_srs, mp_chain, tail, cnt, sz);
	if (flag & MAC_DROP_ON_NO_DESC) {
		mac_pkt_drop(NULL, NULL, mp_chain, B_FALSE);
		/* increment freed stats */
		ringp->s_ring_drops += cnt;
		cookie = (mac_tx_cookie_t)ringp;
	} else {
		if (ringp->s_ring_first != NULL)
			wakeup_worker = B_FALSE;

		if (flag & MAC_TX_NO_ENQUEUE) {
			/*
			 * If QUEUED is not set, queue the packet
			 * and let mac_tx_soft_ring_drain() set
			 * the TX_BLOCKED bit for the reasons
			 * explained above. Otherwise, return the
			 * mblks.
			 */
			if (wakeup_worker) {
				TX_SOFT_RING_ENQUEUE_CHAIN(ringp,
				    mp_chain, tail, cnt, sz);
			} else {
				ringp->s_ring_state |= S_RING_WAKEUP_CLIENT;
				cookie = (mac_tx_cookie_t)ringp;
				*ret_mp = mp_chain;
			}
		} else {
			boolean_t enqueue = B_TRUE;

			if (ringp->s_ring_count > ringp->s_ring_tx_hiwat) {
				/*
				 * flow-controlled. Store ringp in cookie
				 * so that it can be returned as
				 * mac_tx_cookie_t to client
				 */
				ringp->s_ring_state |= S_RING_TX_HIWAT;
				cookie = (mac_tx_cookie_t)ringp;
				ringp->s_ring_hiwat_cnt++;
				if (ringp->s_ring_count >
				    ringp->s_ring_tx_max_q_cnt) {
					/* increment freed stats */
					ringp->s_ring_drops += cnt;
					/*
					 * b_prev may be set to the fanout hint
					 * hence can't use freemsg directly
					 */
					mac_pkt_drop(NULL, NULL,
					    mp_chain, B_FALSE);
					DTRACE_PROBE1(tx_queued_hiwat,
					    mac_soft_ring_t *, ringp);
					enqueue = B_FALSE;
				}
			}
			if (enqueue) {
				TX_SOFT_RING_ENQUEUE_CHAIN(ringp, mp_chain,
				    tail, cnt, sz);
			}
		}
		if (wakeup_worker)
			cv_signal(&ringp->s_ring_async);
	}
	return (cookie);
}


/*
 * mac_tx_soft_ring_process
 *
 * This routine is called when fanning out outgoing traffic among
 * multipe Tx rings.
 * Note that a soft ring is associated with a h/w Tx ring.
 */
mac_tx_cookie_t
mac_tx_soft_ring_process(mac_soft_ring_t *ringp, mblk_t *mp_chain,
    uint16_t flag, mblk_t **ret_mp)
{
	mac_soft_ring_set_t *mac_srs = ringp->s_ring_set;
	int	cnt;
	size_t	sz;
	mblk_t	*tail;
	mac_tx_cookie_t cookie = NULL;

	ASSERT(ringp != NULL);
	ASSERT(mp_chain != NULL);
	ASSERT(MUTEX_NOT_HELD(&ringp->s_ring_lock));
	/*
	 * Only two modes can come here; either it can be
	 * SRS_TX_BW_FANOUT or SRS_TX_FANOUT
	 */
	ASSERT(mac_srs->srs_tx.st_mode == SRS_TX_FANOUT ||
	    mac_srs->srs_tx.st_mode == SRS_TX_BW_FANOUT);

	if (ringp->s_ring_type & ST_RING_WORKER_ONLY) {
		/* Serialization mode */

		mutex_enter(&ringp->s_ring_lock);
		if (ringp->s_ring_count > ringp->s_ring_tx_hiwat) {
			cookie = mac_tx_sring_enqueue(ringp, mp_chain,
			    flag, ret_mp);
			mutex_exit(&ringp->s_ring_lock);
			return (cookie);
		}
		MAC_COUNT_CHAIN(mac_srs, mp_chain, tail, cnt, sz);
		TX_SOFT_RING_ENQUEUE_CHAIN(ringp, mp_chain, tail, cnt, sz);
		if (ringp->s_ring_state & (S_RING_BLOCK | S_RING_PROC)) {
			/*
			 * If ring is blocked due to lack of Tx
			 * descs, just return. Worker thread
			 * will get scheduled when Tx desc's
			 * become available.
			 */
			mutex_exit(&ringp->s_ring_lock);
			return (cookie);
		}
		mac_soft_ring_worker_wakeup(ringp);
		mutex_exit(&ringp->s_ring_lock);
		return (cookie);
	} else {
		/* Default fanout mode */
		/*
		 * S_RING_BLOCKED is set when underlying NIC runs
		 * out of Tx descs and messages start getting
		 * queued. It won't get reset until
		 * tx_srs_drain() completely drains out the
		 * messages.
		 */
		boolean_t		is_subflow;
		mac_tx_stats_t		stats;

		if (ringp->s_ring_state & S_RING_ENQUEUED) {
			/* Tx descs/resources not available */
			mutex_enter(&ringp->s_ring_lock);
			if (ringp->s_ring_state & S_RING_ENQUEUED) {
				cookie = mac_tx_sring_enqueue(ringp, mp_chain,
				    flag, ret_mp);
				mutex_exit(&ringp->s_ring_lock);
				return (cookie);
			}
			/*
			 * While we were computing mblk count, the
			 * flow control condition got relieved.
			 * Continue with the transmission.
			 */
			mutex_exit(&ringp->s_ring_lock);
		}
		is_subflow = ((mac_srs->srs_type & SRST_FLOW) != 0);

		mp_chain = mac_tx_send(ringp->s_ring_tx_arg1,
		    ringp->s_ring_tx_arg2, mp_chain,
		    (is_subflow ? &stats : NULL));

		/*
		 * Multiple threads could be here sending packets.
		 * Under such conditions, it is not possible to
		 * automically set S_RING_BLOCKED bit to indicate
		 * out of tx desc condition. To atomically set
		 * this, we queue the returned packet and do
		 * the setting of S_RING_BLOCKED in
		 * mac_tx_soft_ring_drain().
		 */
		if (mp_chain != NULL) {
			mutex_enter(&ringp->s_ring_lock);
			cookie =
			    mac_tx_sring_enqueue(ringp, mp_chain, flag, ret_mp);
			mutex_exit(&ringp->s_ring_lock);
			return (cookie);
		}
		if (is_subflow) {
			FLOW_TX_STATS_UPDATE(mac_srs->srs_flent, &stats);
		}
		return (NULL);
	}
}
