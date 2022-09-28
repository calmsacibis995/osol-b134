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
 * Copyright 2009 Emulex.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Source file containing the implementation of the Transmit
 * Path
 */

#include <oce_impl.h>

static void oce_free_wqed(struct oce_wq *wq,  oce_wqe_desc_t *wqed);
static int oce_map_wqe(struct oce_wq *wq, oce_wqe_desc_t *wqed,
    mblk_t *mp);
static int oce_bcopy_wqe(struct oce_wq *wq, oce_wqe_desc_t *wqed, mblk_t *mp,
    uint32_t pkt_len);
static void oce_wqb_dtor(struct oce_wq *wq, oce_wq_bdesc_t *wqbd);
static int oce_wqb_ctor(oce_wq_bdesc_t *wqbd, struct oce_wq *wq,
    size_t size, int flags);
static oce_wq_bdesc_t *oce_wqb_alloc(struct oce_wq *wq);
static void oce_wqb_free(struct oce_wq *wq, oce_wq_bdesc_t *wqbd);

static void oce_wqmd_free(struct oce_wq *wq, oce_wqe_desc_t *wqed);
static void oce_wqm_free(struct oce_wq *wq, oce_wq_mdesc_t *wqmd);
static inline oce_wq_mdesc_t *oce_wqm_alloc(struct oce_wq *wq);
static int oce_wqm_ctor(oce_wq_mdesc_t *wqmd, struct oce_wq *wq);
static void oce_wqm_dtor(struct oce_wq *wq, oce_wq_mdesc_t *wqmd);
static void oce_fill_ring_descs(struct oce_wq *wq, oce_wqe_desc_t *wqed);
static void oce_remove_vtag(mblk_t *mp);
static void oce_insert_vtag(mblk_t  *mp, uint16_t vlan_tag);

static ddi_dma_attr_t tx_map_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,	/* low address */
	0xFFFFFFFFFFFFFFFFull,	/* high address */
	0x0000000000010000ull,	/* dma counter max */
	OCE_TXMAP_ALIGN,	/* alignment */
	0x1,			/* burst sizes */
	0x00000001,		/* minimum transfer size */
	0x00000000FFFFFFFFull,	/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,	/* maximum segment size */
	OCE_MAX_TXDMA_COOKIES,	/* scatter/gather list length */
	0x00000001,		/* granularity */
	0			/* DMA flags */
};

/*
 * WQ map handle destructor
 *
 * wq - Pointer to WQ structure
 * wqmd - pointer to WQE mapping handle descriptor
 *
 * return none
 */

static void
oce_wqm_dtor(struct oce_wq *wq, oce_wq_mdesc_t *wqmd)
{
	_NOTE(ARGUNUSED(wq));
	/* Free the DMA handle */
	if (wqmd->dma_handle != NULL)
		(void) ddi_dma_free_handle(&(wqmd->dma_handle));
	wqmd->dma_handle = NULL;
} /* oce_wqm_dtor */

/*
 * WQ map handles contructor
 *
 * wqmd - pointer to WQE mapping handle descriptor
 * wq - Pointer to WQ structure
 *
 * return DDI_SUCCESS=>success, DDI_FAILURE=>error
 */
static int
oce_wqm_ctor(oce_wq_mdesc_t *wqmd, struct oce_wq *wq)
{
	struct oce_dev *dev;
	int ret;

	dev = wq->parent;
	/* Allocate DMA handle */
	ret = ddi_dma_alloc_handle(dev->dip, &tx_map_dma_attr,
	    KM_SLEEP, NULL, &wqmd->dma_handle);

	return (ret);
} /* oce_wqm_ctor */

/*
 * function to create WQ mapping handles cache
 *
 * wq - pointer to WQ structure
 *
 * return DDI_SUCCESS=>success, DDI_FAILURE=>error
 */
int
oce_wqm_cache_create(struct oce_wq *wq)
{
	struct oce_dev *dev = wq->parent;
	int size;
	int cnt;
	int ret;

	size = wq->cfg.nhdl * sizeof (oce_wq_mdesc_t);
	wq->wq_mdesc_array = kmem_zalloc(size, KM_SLEEP);

	/* Create the free buffer list */
	OCE_LIST_CREATE(&wq->wq_mdesc_list, DDI_INTR_PRI(dev->intr_pri));

	for (cnt = 0; cnt < wq->cfg.nhdl; cnt++) {
		ret = oce_wqm_ctor(&wq->wq_mdesc_array[cnt], wq);
		if (ret != DDI_SUCCESS) {
			goto wqm_fail;
		}
		OCE_LIST_INSERT_TAIL(&wq->wq_mdesc_list,
		    &wq->wq_mdesc_array[cnt]);
	}
	return (DDI_SUCCESS);

wqm_fail:
	oce_wqm_cache_destroy(wq);
	return (DDI_FAILURE);
}

/*
 * function to destroy WQ mapping handles cache
 *
 * wq - pointer to WQ structure
 *
 * return none
 */
void
oce_wqm_cache_destroy(struct oce_wq *wq)
{
	oce_wq_mdesc_t *wqmd;

	while ((wqmd = OCE_LIST_REM_HEAD(&wq->wq_mdesc_list)) != NULL) {
		oce_wqm_dtor(wq, wqmd);
	}

	kmem_free(wq->wq_mdesc_array,
	    wq->cfg.nhdl * sizeof (oce_wq_mdesc_t));

	OCE_LIST_DESTROY(&wq->wq_mdesc_list);
}

/*
 * function to create  WQ buffer cache
 *
 * wq - pointer to WQ structure
 * buf_size - size of the buffer
 *
 * return DDI_SUCCESS=>success, DDI_FAILURE=>error
 */
int
oce_wqb_cache_create(struct oce_wq *wq, size_t buf_size)
{
	struct oce_dev *dev = wq->parent;
	int size;
	int cnt;
	int ret;

	size = wq->cfg.nbufs * sizeof (oce_wq_bdesc_t);
	wq->wq_bdesc_array = kmem_zalloc(size, KM_SLEEP);

	/* Create the free buffer list */
	OCE_LIST_CREATE(&wq->wq_buf_list, DDI_INTR_PRI(dev->intr_pri));

	for (cnt = 0; cnt <  wq->cfg.nbufs; cnt++) {
		ret = oce_wqb_ctor(&wq->wq_bdesc_array[cnt],
		    wq, buf_size, DDI_DMA_STREAMING);
		if (ret != DDI_SUCCESS) {
			goto wqb_fail;
		}
		OCE_LIST_INSERT_TAIL(&wq->wq_buf_list,
		    &wq->wq_bdesc_array[cnt]);
	}
	return (DDI_SUCCESS);

wqb_fail:
	oce_wqb_cache_destroy(wq);
	return (DDI_FAILURE);
}

/*
 * function to destroy WQ buffer cache
 *
 * wq - pointer to WQ structure
 *
 * return none
 */
void
oce_wqb_cache_destroy(struct oce_wq *wq)
{
	oce_wq_bdesc_t *wqbd;
	while ((wqbd = OCE_LIST_REM_HEAD(&wq->wq_buf_list)) != NULL) {
		oce_wqb_dtor(wq, wqbd);
	}
	kmem_free(wq->wq_bdesc_array,
	    wq->cfg.nbufs * sizeof (oce_wq_bdesc_t));
	OCE_LIST_DESTROY(&wq->wq_buf_list);
}

/*
 * WQ buffer constructor
 *
 * wqbd - pointer to WQ buffer descriptor
 * wq - pointer to WQ structure
 * size - size of the buffer
 * flags - KM_SLEEP or KM_NOSLEEP
 *
 * return DDI_SUCCESS=>success, DDI_FAILURE=>error
 */
static int
oce_wqb_ctor(oce_wq_bdesc_t *wqbd, struct oce_wq *wq, size_t size, int flags)
{
	struct oce_dev *dev;
	dev = wq->parent;
	wqbd->wqb = oce_alloc_dma_buffer(dev, size, flags);
	if (wqbd->wqb == NULL) {
		return (DDI_FAILURE);
	}
	wqbd->frag_addr.dw.addr_lo = ADDR_LO(wqbd->wqb->addr);
	wqbd->frag_addr.dw.addr_hi = ADDR_HI(wqbd->wqb->addr);
	return (DDI_SUCCESS);
}

/*
 * WQ buffer destructor
 *
 * wq - pointer to WQ structure
 * wqbd - pointer to WQ buffer descriptor
 *
 * return none
 */
static void
oce_wqb_dtor(struct oce_wq *wq, oce_wq_bdesc_t *wqbd)
{
	oce_free_dma_buffer(wq->parent, wqbd->wqb);
}

/*
 * function to alloc   WQE buffer descriptor
 *
 * wq - pointer to WQ structure
 *
 * return pointer to WQE buffer descriptor
 */
static inline oce_wq_bdesc_t *
oce_wqb_alloc(struct oce_wq *wq)
{
	oce_wq_bdesc_t *wqbd;
	wqbd = OCE_LIST_REM_HEAD(&wq->wq_buf_list);
	return (wqbd);
}

/*
 * function to free   WQE buffer descriptor
 *
 * wq - pointer to WQ structure
 * wqbd - pointer to WQ buffer descriptor
 *
 * return none
 */
static inline void
oce_wqb_free(struct oce_wq *wq, oce_wq_bdesc_t *wqbd)
{
	OCE_LIST_INSERT_TAIL(&wq->wq_buf_list, wqbd);
} /* oce_wqb_free */

/*
 * function to allocate   WQE mapping descriptor
 *
 * wq - pointer to WQ structure
 *
 * return pointer to WQE mapping descriptor
 */
static inline oce_wq_mdesc_t *
oce_wqm_alloc(struct oce_wq *wq)
{
	oce_wq_mdesc_t *wqmd;
	wqmd = OCE_LIST_REM_HEAD(&wq->wq_mdesc_list);
	return (wqmd);

} /* oce_wqm_alloc */

/*
 * function to insert	WQE mapping descriptor to the list
 *
 * wq - pointer to WQ structure
 * wqmd - Pointer to WQ mapping descriptor
 *
 * return none
 */
static inline void
oce_wqm_free(struct oce_wq *wq, oce_wq_mdesc_t *wqmd)
{
	OCE_LIST_INSERT_TAIL(&wq->wq_mdesc_list, wqmd);
}

/*
 * function to free  WQE mapping descriptor
 *
 * wq - pointer to WQ structure
 * wqmd - Pointer to WQ mapping descriptor
 *
 * return none
 */
static void
oce_wqmd_free(struct oce_wq *wq, oce_wqe_desc_t *wqed)
{
	int ndesc;
	oce_wq_mdesc_t *wqmd;

	if (wqed == NULL) {
		return;
	}
	for (ndesc = 0; ndesc < wqed->nhdl; ndesc++) {
		wqmd = wqed->hdesc[ndesc].hdl;
		(void) ddi_dma_unbind_handle(wqmd->dma_handle);
		oce_wqm_free(wq, wqmd);
	}
}

/*
 * WQED kmem_cache constructor
 *
 * buf - pointer to WQE descriptor
 *
 * return DDI_SUCCESS
 */
int
oce_wqe_desc_ctor(void *buf, void *arg, int kmflags)
{
	oce_wqe_desc_t *wqed = (oce_wqe_desc_t *)buf;

	_NOTE(ARGUNUSED(arg));
	_NOTE(ARGUNUSED(kmflags));

	bzero(wqed, sizeof (oce_wqe_desc_t));
	return (DDI_SUCCESS);
}

/*
 * WQED kmem_cache destructor
 *
 * buf - pointer to WQE descriptor
 *
 * return none
 */
void
oce_wqe_desc_dtor(void *buf, void *arg)
{
	_NOTE(ARGUNUSED(buf));
	_NOTE(ARGUNUSED(arg));
}

/*
 * function to choose a WQ given a mblk depending on priority, flowID etc.
 *
 * dev - software handle to device
 * pkt - the mblk to send
 *
 * return pointer to the WQ selected
 */
struct oce_wq *
oce_get_wq(struct oce_dev *dev, mblk_t *pkt)
{
	_NOTE(ARGUNUSED(pkt));
	/* for the time being hardcode */
	return (dev->wq[0]);
} /* oce_get_wq */

/*
 * function to populate the single WQE
 *
 * wq - pointer to wq
 * wqed - pointer to WQ entry  descriptor
 *
 * return none
 */
#pragma inline(oce_fill_ring_descs)
static void
oce_fill_ring_descs(struct oce_wq *wq, oce_wqe_desc_t *wqed)
{

	struct oce_nic_frag_wqe *wqe;
	int i;
	/* Copy the precreate WQE descs to the ring desc */
	for (i = 0; i < wqed->wqe_cnt; i++) {
		wqe = RING_GET_PRODUCER_ITEM_VA(wq->ring,
		    struct oce_nic_frag_wqe);

		bcopy(&wqed->frag[i], wqe, NIC_WQE_SIZE);
		RING_PUT(wq->ring, 1);
	}
} /* oce_fill_ring_descs */

/*
 * function to copy the packet to preallocated Tx buffer
 *
 * wq - pointer to WQ
 * wqed - Pointer to WQE descriptor
 * mp - Pointer to packet chain
 * pktlen - Size of the packet
 *
 * return 0=>success, error code otherwise
 */
static int
oce_bcopy_wqe(struct oce_wq *wq, oce_wqe_desc_t *wqed, mblk_t *mp,
    uint32_t pkt_len)
{
	oce_wq_bdesc_t *wqbd;
	caddr_t buf_va;
	struct oce_dev *dev = wq->parent;

	wqbd = oce_wqb_alloc(wq);
	if (wqbd == NULL) {
		atomic_inc_32(&dev->tx_noxmtbuf);
		oce_log(dev, CE_WARN, MOD_TX, "%s",
		    "wqb pool empty");
		return (ENOMEM);
	}

	/* create a fragment wqe for the packet */
	wqed->frag[1].u0.s.frag_pa_hi = wqbd->frag_addr.dw.addr_hi;
	wqed->frag[1].u0.s.frag_pa_lo = wqbd->frag_addr.dw.addr_lo;
	buf_va = DBUF_VA(wqbd->wqb);

	/* copy pkt into buffer */
	for (; mp != NULL; mp = mp->b_cont) {
		bcopy(mp->b_rptr, buf_va, MBLKL(mp));
		buf_va += MBLKL(mp);
	}

	wqed->frag[1].u0.s.frag_len   =  pkt_len;
	wqed->hdesc[0].hdl = (void *)(wqbd);

	(void) ddi_dma_sync(DBUF_DHDL(wqbd->wqb), 0, pkt_len,
	    DDI_DMA_SYNC_FORDEV);

	if (oce_fm_check_dma_handle(dev, DBUF_DHDL(wqbd->wqb))) {
		ddi_fm_service_impact(dev->dip, DDI_SERVICE_DEGRADED);
		/* Free the buffer */
		oce_wqb_free(wq, wqbd);
		return (EIO);
	}
	wqed->frag_cnt = 2;
	wqed->nhdl = 1;
	wqed->type = COPY_WQE;
	return (0);
} /* oce_bcopy_wqe */

/*
 * function to copy the packet or dma map on the fly depending on size
 *
 * wq - pointer to WQ
 * wqed - Pointer to WQE descriptor
 * mp - Pointer to packet chain
 *
 * return DDI_SUCCESS=>success, DDI_FAILURE=>error
 */
static  int
oce_map_wqe(struct oce_wq *wq, oce_wqe_desc_t *wqed, mblk_t *mp)
{
	ddi_dma_cookie_t cookie;
	oce_wq_mdesc_t *wqmd;
	int32_t nfrag = 1;
	uint32_t ncookies;
	int ret;
	uint32_t len;
	struct oce_dev *dev = wq->parent;

	wqed->nhdl = 0;
	wqed->mp = mp;

	for (; mp != NULL; mp = mp->b_cont) {
		len = MBLKL(mp);
		if (len == 0) {
			oce_log(dev, CE_NOTE, MOD_TX, "%s",
			    "Zero len MBLK ");
			continue;
		}

		wqmd = oce_wqm_alloc(wq);
		if (wqmd == NULL) {
			oce_log(dev, CE_WARN, MOD_TX, "%s",
			    "wqm pool empty");
			ret = ENOMEM;
			goto map_fail;
		}

		ret = ddi_dma_addr_bind_handle(wqmd->dma_handle,
		    (struct as *)0, (caddr_t)mp->b_rptr,
		    len, DDI_DMA_WRITE | DDI_DMA_STREAMING,
		    DDI_DMA_DONTWAIT, NULL, &cookie, &ncookies);
		if (ret != DDI_DMA_MAPPED) {
			oce_log(dev, CE_WARN, MOD_TX, "%s",
			    "Failed to Map SGL");
			/* free the last one */
			oce_wqm_free(wq, wqmd);
			goto map_fail;
		}

		do {
			wqed->frag[nfrag].u0.s.frag_pa_hi =
			    ADDR_HI(cookie.dmac_laddress);
			wqed->frag[nfrag].u0.s.frag_pa_lo =
			    ADDR_LO(cookie.dmac_laddress);
			wqed->frag[nfrag].u0.s.frag_len =
			    (uint32_t)cookie.dmac_size;
			nfrag++;
			if (--ncookies > 0)
				ddi_dma_nextcookie(wqmd->dma_handle,
				    &cookie);
			else break;
		} while (ncookies > 0);

		wqed->hdesc[wqed->nhdl].hdl = (void *)wqmd;
		wqed->nhdl++;
	}
	wqed->frag_cnt = nfrag;
	wqed->type = MAPPED_WQE;
	return (0);

map_fail:
	wqed->mp = NULL;
	oce_wqmd_free(wq, wqed);
	return (ret);
} /* oce_map_wqe */

/*
 * function to drain a TxCQ and process its CQEs
 *
 * dev - software handle to the device
 * cq - pointer to the cq to drain
 *
 * return the number of CQEs processed
 */
uint16_t
oce_drain_wq_cq(void *arg)
{
	struct oce_nic_tx_cqe *cqe;
	uint16_t num_cqe = 0;
	struct oce_dev *dev;
	struct oce_wq *wq;
	struct oce_cq *cq;
	oce_wqe_desc_t *wqed;
	int wqe_freed = 0;
	boolean_t is_update = B_FALSE;

	wq = (struct oce_wq *)arg;
	cq  = wq->cq;
	dev = wq->parent;

	/* do while we do not reach a cqe that is not valid */
	mutex_enter(&cq->lock);
	cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_tx_cqe);
	while (WQ_CQE_VALID(cqe)) {

		DW_SWAP(u32ptr(cqe), sizeof (struct oce_nic_tx_cqe));

		/* update stats */
		if (cqe->u0.s.status != 0) {
			atomic_inc_32(&dev->tx_errors);
		}

		/* complete the WQEs */
		wqed = OCE_LIST_REM_HEAD(&wq->wqe_desc_list);

		wqe_freed = wqed->wqe_cnt;
		oce_free_wqed(wq, wqed);
		RING_GET(wq->ring, wqe_freed);
		atomic_add_32(&wq->wq_free, wqe_freed);
		/* clear the valid bit and progress cqe */
		WQ_CQE_INVALIDATE(cqe);
		RING_GET(cq->ring, 1);
		cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring,
		    struct oce_nic_tx_cqe);
		num_cqe++;
	} /* for all valid CQE */

	if (wq->resched && num_cqe) {
		wq->resched = B_FALSE;
		is_update = B_TRUE;
	}

	mutex_exit(&cq->lock);

	oce_arm_cq(dev, cq->cq_id, num_cqe, B_TRUE);

	/* check if we need to restart Tx */
	mutex_enter(&wq->lock);
	if (wq->resched && num_cqe) {
		wq->resched = B_FALSE;
		is_update = B_TRUE;
	}
	mutex_exit(&wq->lock);

	if (is_update)
		mac_tx_update(dev->mac_handle);

	return (num_cqe);
} /* oce_process_wq_cqe */

/*
 * function to insert vtag to packet
 *
 * mp - mblk pointer
 * vlan_tag - tag to be inserted
 *
 * return none
 */
static inline void
oce_insert_vtag(mblk_t *mp, uint16_t vlan_tag)
{
	struct ether_vlan_header  *evh;
	(void) memmove(mp->b_rptr - VLAN_TAGSZ,
	    mp->b_rptr, 2 * ETHERADDRL);
	mp->b_rptr -= VLAN_TAGSZ;
	evh = (struct ether_vlan_header *)(void *)mp->b_rptr;
	evh->ether_tpid = htons(VLAN_TPID);
	evh->ether_tci = htons(vlan_tag);
}

/*
 * function to strip  vtag from packet
 *
 * mp - mblk pointer
 *
 * return none
 */

static inline void
oce_remove_vtag(mblk_t *mp)
{
	(void) memmove(mp->b_rptr + VLAN_TAGSZ, mp->b_rptr,
	    ETHERADDRL * 2);
	mp->b_rptr += VLAN_TAGSZ;
}

/*
 * function to xmit  Single packet over the wire
 *
 * wq - pointer to WQ
 * mp - Pointer to packet chain
 *
 * return pointer to the packet
 */
mblk_t *
oce_send_packet(struct oce_wq *wq, mblk_t *mp)
{

	struct oce_nic_hdr_wqe *wqeh;
	struct oce_dev *dev;
	struct ether_header *eh;
	struct ether_vlan_header *evh;
	int32_t num_wqes;
	uint16_t etype;
	uint32_t ip_offset;
	uint32_t csum_flags;
	boolean_t use_copy = B_FALSE;
	boolean_t tagged   = B_FALSE;
	uint16_t  vlan_tag;
	uint32_t  reg_value = 0;
	oce_wqe_desc_t *wqed = NULL;
	mblk_t *nmp = NULL;
	mblk_t *tmp = NULL;
	uint32_t pkt_len = 0;
	int num_mblks = 0;
	int ret = 0;

	/* retrieve the adap priv struct ptr */
	dev = wq->parent;

	/* check if we should copy */
	for (tmp = mp; tmp != NULL; tmp = tmp->b_cont) {
		pkt_len += MBLKL(tmp);
		num_mblks++;
	}
	/* get the offload flags */
	hcksum_retrieve(mp, NULL, NULL, NULL, NULL, NULL,
	    NULL, &csum_flags);

	/* Limit should be always less than Tx Buffer Size */
	if (pkt_len < dev->bcopy_limit) {
		use_copy = B_TRUE;
	} else {
		/* restrict the mapped segment to wat we support */
		if (num_mblks  > OCE_MAX_TX_HDL) {
			nmp = msgpullup(mp, -1);
			if (nmp == NULL) {
				atomic_inc_32(&wq->pkt_drops);
				freemsg(mp);
				return (NULL);
			}
			/* Reset it to new collapsed mp */
			freemsg(mp);
			mp = nmp;
		}
	}

	/* Get the packet descriptor for Tx */
	wqed = kmem_cache_alloc(wq->wqed_cache, KM_NOSLEEP);
	if (wqed == NULL) {
		atomic_inc_32(&wq->pkt_drops);
		freemsg(mp);
		return (NULL);
	}
	eh = (struct ether_header *)(void *)mp->b_rptr;
	if (ntohs(eh->ether_type) == VLAN_TPID) {
		evh = (struct ether_vlan_header *)(void *)mp->b_rptr;
		tagged = B_TRUE;
		etype = ntohs(evh->ether_type);
		ip_offset = sizeof (struct ether_vlan_header);
		pkt_len -= VLAN_TAGSZ;
		vlan_tag = ntohs(evh->ether_tci);
		oce_remove_vtag(mp);
	} else {
		etype = ntohs(eh->ether_type);
		ip_offset = sizeof (struct ether_header);
	}
	bzero(wqed, sizeof (oce_wqe_desc_t));

	/* Save the WQ pointer */
	wqed->wq = wq;
	if (use_copy == B_TRUE) {
		ret = oce_bcopy_wqe(wq, wqed, mp, pkt_len);
	} else {
		ret = oce_map_wqe(wq, wqed, mp);
	}

	/*
	 * Any failure other than insufficient Q entries
	 * drop the packet
	 */
	if (ret != 0) {
		kmem_cache_free(wq->wqed_cache, wqed);
		/* drop the packet */
		atomic_inc_32(&wq->pkt_drops);
		freemsg(mp);
		return (NULL);
	}

	/* increment pending wqed to be scheduled */
	wqeh = (struct oce_nic_hdr_wqe *)&wqed->frag[0];

	/* fill rest of wqe header fields based on packet */
	if (DB_CKSUMFLAGS(mp) & HW_LSO) {
		wqeh->u0.s.lso = B_TRUE;
		wqeh->u0.s.lso_mss = DB_LSOMSS(mp);
	}

	if (csum_flags & HCK_FULLCKSUM) {
		uint8_t *proto;
		if (etype == ETHERTYPE_IP) {
			proto = (uint8_t *)(void *)
			    (mp->b_rptr + ip_offset);
			if (proto[9] == 6)
				/* IPPROTO_TCP */
				wqeh->u0.s.tcpcs = B_TRUE;
			else if (proto[9] == 17)
				/* IPPROTO_UDP */
				wqeh->u0.s.udpcs = B_TRUE;
		}
	}

	if (csum_flags & HCK_IPV4_HDRCKSUM)
		wqeh->u0.s.ipcs = B_TRUE;
	if (tagged) {
		wqeh->u0.s.vlan = B_TRUE;
		wqeh->u0.s.vlan_tag = vlan_tag;
	}

	wqeh->u0.s.complete = B_TRUE;
	wqeh->u0.s.event = B_TRUE;
	wqeh->u0.s.crc = B_TRUE;
	wqeh->u0.s.total_length = pkt_len;

	num_wqes = wqed->frag_cnt;

	/* h/w expects even no. of WQEs */
	if (num_wqes & 0x1)
		num_wqes++;
	wqed->wqe_cnt = (uint16_t)num_wqes;
	wqeh->u0.s.num_wqe = num_wqes;
	DW_SWAP(u32ptr(&wqed->frag[0]), (wqed->wqe_cnt * NIC_WQE_SIZE));

	mutex_enter(&wq->lock);
	if (num_wqes > wq->wq_free) {
		atomic_inc_32(&wq->tx_deferd);
		wq->resched = B_TRUE;
		mutex_exit(&wq->lock);
		goto wqe_fail;
	}
	atomic_add_32(&wq->wq_free, -num_wqes);
	wqed->wq_start_idx = wq->ring->pidx;

	/* fill the wq for adapter */
	oce_fill_ring_descs(wq, wqed);

	/* Add the packet desc to list to be retrieved during cmpl */
	OCE_LIST_INSERT_TAIL(&wq->wqe_desc_list,  wqed);

	/* ring tx doorbell */
	reg_value = (num_wqes << 16) | wq->wq_id;
	/* Ring the door bell  */
	OCE_DB_WRITE32(dev, PD_TXULP_DB, reg_value);
	mutex_exit(&wq->lock);

	/* free mp if copied or packet chain collapsed */
	if (use_copy == B_TRUE) {
		freemsg(mp);
	}
	return (NULL);

wqe_fail:

	if (tagged) {
		oce_insert_vtag(mp, vlan_tag);
	}

	/* set it to null in case map_wqe has set it */
	wqed->mp = NULL;
	oce_free_wqed(wq, wqed);
	return (mp);
} /* oce_send_packet */

/*
 * function to free the WQE descriptor
 *
 * wq - pointer to WQ
 * wqed - Pointer to WQE descriptor
 *
 * return none
 */
#pragma inline(oce_free_wqed)
static void
oce_free_wqed(struct oce_wq *wq, oce_wqe_desc_t *wqed)
{
	if (wqed == NULL) {
		return;
	}

	if (wqed->type == COPY_WQE) {
		oce_wqb_free(wq, wqed->hdesc[0].hdl);
	} else if (wqed->type == MAPPED_WQE) {
		oce_wqmd_free(wq, wqed);
	} else ASSERT(0);

	if (wqed->mp)
		freemsg(wqed->mp);
	kmem_cache_free(wq->wqed_cache, wqed);
} /* oce_free_wqed */

/*
 * function to start the WQ
 *
 * wq - pointer to WQ
 *
 * return DDI_SUCCESS
 */

int
oce_start_wq(struct oce_wq *wq)
{
	oce_arm_cq(wq->parent, wq->cq->cq_id, 0, B_TRUE);
	return (DDI_SUCCESS);
} /* oce_start_wq */

/*
 * function to stop  the WQ
 *
 * wq - pointer to WQ
 *
 * return none
 */
void
oce_stop_wq(struct oce_wq *wq)
{
	oce_wqe_desc_t *wqed;

	/* Max time for already posted TX to complete */
	drv_usecwait(150 * 1000); /* 150 mSec */

	/* Wait for already posted Tx to complete */
	while ((OCE_LIST_EMPTY(&wq->wqe_desc_list) == B_FALSE)	||
	    (OCE_LIST_SIZE(&wq->wq_buf_list) != wq->cfg.nbufs) ||
	    (OCE_LIST_SIZE(&wq->wq_mdesc_list) != wq->cfg.nhdl)) {
		(void) oce_drain_wq_cq(wq);
	}

	/* Free the remaining descriptors */
	while ((wqed = OCE_LIST_REM_HEAD(&wq->wqe_desc_list)) != NULL) {
		atomic_add_32(&wq->wq_free, wqed->wqe_cnt);
		oce_free_wqed(wq, wqed);
	}
	oce_drain_eq(wq->cq->eq);
} /* oce_stop_wq */

/*
 * function to set the tx mapping handle fma attr
 *
 * fm_caps - capability flags
 *
 * return none
 */

void
oce_set_tx_map_dma_fma_flags(int fm_caps)
{
	if (fm_caps == DDI_FM_NOT_CAPABLE) {
		return;
	}

	if (DDI_FM_DMA_ERR_CAP(fm_caps)) {
		tx_map_dma_attr.dma_attr_flags |= DDI_DMA_FLAGERR;
	} else {
		tx_map_dma_attr.dma_attr_flags &= ~DDI_DMA_FLAGERR;
	}
} /* oce_set_tx_map_dma_fma_flags */
