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
 * Copyright(c) 2007-2008 Intel Corporation. All rights reserved.
 */

/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "igb_sw.h"

int
igb_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;
	igb_stat_t *igb_ks;
	uint32_t low_val, high_val;

	igb_ks = (igb_stat_t *)igb->igb_ks->ks_data;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = igb->link_speed * 1000000ull;
		break;

	case MAC_STAT_MULTIRCV:
		igb_ks->mprc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MPRC);
		*val = igb_ks->mprc.value.ui64;
		break;

	case MAC_STAT_BRDCSTRCV:
		igb_ks->bprc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_BPRC);
		*val = igb_ks->bprc.value.ui64;
		break;

	case MAC_STAT_MULTIXMT:
		igb_ks->mptc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MPTC);
		*val = igb_ks->mptc.value.ui64;
		break;

	case MAC_STAT_BRDCSTXMT:
		igb_ks->bptc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_BPTC);
		*val = igb_ks->bptc.value.ui64;
		break;

	case MAC_STAT_NORCVBUF:
		igb_ks->rnbc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RNBC);
		*val = igb_ks->rnbc.value.ui64;
		break;

	case MAC_STAT_IERRORS:
		igb_ks->rxerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RXERRC);
		igb_ks->algnerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ALGNERRC);
		igb_ks->rlec.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RLEC);
		igb_ks->crcerrs.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CRCERRS);
		igb_ks->cexterr.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CEXTERR);
		*val = igb_ks->rxerrc.value.ui64 +
		    igb_ks->algnerrc.value.ui64 +
		    igb_ks->rlec.value.ui64 +
		    igb_ks->crcerrs.value.ui64 +
		    igb_ks->cexterr.value.ui64;
		break;

	case MAC_STAT_NOXMTBUF:
		*val = 0;
		break;

	case MAC_STAT_OERRORS:
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		*val = igb_ks->ecol.value.ui64;
		break;

	case MAC_STAT_COLLISIONS:
		igb_ks->colc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_COLC);
		*val = igb_ks->colc.value.ui64;
		break;

	case MAC_STAT_RBYTES:
		/*
		 * The 64-bit register will reset whenever the upper
		 * 32 bits are read. So we need to read the lower
		 * 32 bits first, then read the upper 32 bits.
		 */
		low_val = E1000_READ_REG(hw, E1000_TORL);
		high_val = E1000_READ_REG(hw, E1000_TORH);
		igb_ks->tor.value.ui64 +=
		    (uint64_t)high_val << 32 | (uint64_t)low_val;
		*val = igb_ks->tor.value.ui64;
		break;

	case MAC_STAT_IPACKETS:
		igb_ks->tpr.value.ui64 +=
		    E1000_READ_REG(hw, E1000_TPR);
		*val = igb_ks->tpr.value.ui64;
		break;

	case MAC_STAT_OBYTES:
		/*
		 * The 64-bit register will reset whenever the upper
		 * 32 bits are read. So we need to read the lower
		 * 32 bits first, then read the upper 32 bits.
		 */
		low_val = E1000_READ_REG(hw, E1000_TOTL);
		high_val = E1000_READ_REG(hw, E1000_TOTH);
		igb_ks->tot.value.ui64 +=
		    (uint64_t)high_val << 32 | (uint64_t)low_val;
		*val = igb_ks->tot.value.ui64;
		break;

	case MAC_STAT_OPACKETS:
		igb_ks->tpt.value.ui64 +=
		    E1000_READ_REG(hw, E1000_TPT);
		*val = igb_ks->tpt.value.ui64;
		break;

	/* RFC 1643 stats */
	case ETHER_STAT_ALIGN_ERRORS:
		igb_ks->algnerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ALGNERRC);
		*val = igb_ks->algnerrc.value.ui64;
		break;

	case ETHER_STAT_FCS_ERRORS:
		igb_ks->crcerrs.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CRCERRS);
		*val = igb_ks->crcerrs.value.ui64;
		break;

	case ETHER_STAT_FIRST_COLLISIONS:
		igb_ks->scc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_SCC);
		*val = igb_ks->scc.value.ui64;
		break;

	case ETHER_STAT_MULTI_COLLISIONS:
		igb_ks->mcc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_MCC);
		*val = igb_ks->mcc.value.ui64;
		break;

	case ETHER_STAT_SQE_ERRORS:
		igb_ks->sec.value.ui64 +=
		    E1000_READ_REG(hw, E1000_SEC);
		*val = igb_ks->sec.value.ui64;
		break;

	case ETHER_STAT_DEFER_XMTS:
		igb_ks->dc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_DC);
		*val = igb_ks->dc.value.ui64;
		break;

	case ETHER_STAT_TX_LATE_COLLISIONS:
		igb_ks->latecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_LATECOL);
		*val = igb_ks->latecol.value.ui64;
		break;

	case ETHER_STAT_EX_COLLISIONS:
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		*val = igb_ks->ecol.value.ui64;
		break;

	case ETHER_STAT_MACXMT_ERRORS:
		igb_ks->ecol.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ECOL);
		*val = igb_ks->ecol.value.ui64;
		break;

	case ETHER_STAT_CARRIER_ERRORS:
		igb_ks->cexterr.value.ui64 +=
		    E1000_READ_REG(hw, E1000_CEXTERR);
		*val = igb_ks->cexterr.value.ui64;
		break;

	case ETHER_STAT_TOOLONG_ERRORS:
		igb_ks->roc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_ROC);
		*val = igb_ks->roc.value.ui64;
		break;

	case ETHER_STAT_MACRCV_ERRORS:
		igb_ks->rxerrc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RXERRC);
		*val = igb_ks->rxerrc.value.ui64;
		break;

	/* MII/GMII stats */
	case ETHER_STAT_XCVR_ADDR:
		/* The Internal PHY's MDI address for each MAC is 1 */
		*val = 1;
		break;

	case ETHER_STAT_XCVR_ID:
		*val = hw->phy.id | hw->phy.revision;
		break;

	case ETHER_STAT_XCVR_INUSE:
		switch (igb->link_speed) {
		case SPEED_1000:
			*val =
			    (hw->phy.media_type == e1000_media_type_copper) ?
			    XCVR_1000T : XCVR_1000X;
			break;
		case SPEED_100:
			*val =
			    (hw->phy.media_type == e1000_media_type_copper) ?
			    (igb->param_100t4_cap == 1) ?
			    XCVR_100T4 : XCVR_100T2 : XCVR_100X;
			break;
		case SPEED_10:
			*val = XCVR_10;
			break;
		default:
			*val = XCVR_NONE;
			break;
		}
		break;

	case ETHER_STAT_CAP_1000FDX:
		*val = igb->param_1000fdx_cap;
		break;

	case ETHER_STAT_CAP_1000HDX:
		*val = igb->param_1000hdx_cap;
		break;

	case ETHER_STAT_CAP_100FDX:
		*val = igb->param_100fdx_cap;
		break;

	case ETHER_STAT_CAP_100HDX:
		*val = igb->param_100hdx_cap;
		break;

	case ETHER_STAT_CAP_10FDX:
		*val = igb->param_10fdx_cap;
		break;

	case ETHER_STAT_CAP_10HDX:
		*val = igb->param_10hdx_cap;
		break;

	case ETHER_STAT_CAP_ASMPAUSE:
		*val = igb->param_asym_pause_cap;
		break;

	case ETHER_STAT_CAP_PAUSE:
		*val = igb->param_pause_cap;
		break;

	case ETHER_STAT_CAP_AUTONEG:
		*val = igb->param_autoneg_cap;
		break;

	case ETHER_STAT_ADV_CAP_1000FDX:
		*val = igb->param_adv_1000fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_1000HDX:
		*val = igb->param_adv_1000hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_100FDX:
		*val = igb->param_adv_100fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_100HDX:
		*val = igb->param_adv_100hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_10FDX:
		*val = igb->param_adv_10fdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_10HDX:
		*val = igb->param_adv_10hdx_cap;
		break;

	case ETHER_STAT_ADV_CAP_ASMPAUSE:
		*val = igb->param_adv_asym_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_PAUSE:
		*val = igb->param_adv_pause_cap;
		break;

	case ETHER_STAT_ADV_CAP_AUTONEG:
		*val = hw->mac.autoneg;
		break;

	case ETHER_STAT_LP_CAP_1000FDX:
		*val = igb->param_lp_1000fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_1000HDX:
		*val = igb->param_lp_1000hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_100FDX:
		*val = igb->param_lp_100fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_100HDX:
		*val = igb->param_lp_100hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_10FDX:
		*val = igb->param_lp_10fdx_cap;
		break;

	case ETHER_STAT_LP_CAP_10HDX:
		*val = igb->param_lp_10hdx_cap;
		break;

	case ETHER_STAT_LP_CAP_ASMPAUSE:
		*val = igb->param_lp_asym_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_PAUSE:
		*val = igb->param_lp_pause_cap;
		break;

	case ETHER_STAT_LP_CAP_AUTONEG:
		*val = igb->param_lp_autoneg_cap;
		break;

	case ETHER_STAT_LINK_ASMPAUSE:
		*val = igb->param_asym_pause_cap;
		break;

	case ETHER_STAT_LINK_PAUSE:
		*val = igb->param_pause_cap;
		break;

	case ETHER_STAT_LINK_AUTONEG:
		*val = hw->mac.autoneg;
		break;

	case ETHER_STAT_LINK_DUPLEX:
		*val = (igb->link_duplex == FULL_DUPLEX) ?
		    LINK_DUPLEX_FULL : LINK_DUPLEX_HALF;
		break;

	case ETHER_STAT_TOOSHORT_ERRORS:
		igb_ks->ruc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RUC);
		*val = igb_ks->ruc.value.ui64;
		break;

	case ETHER_STAT_CAP_REMFAULT:
		*val = igb->param_rem_fault;
		break;

	case ETHER_STAT_ADV_REMFAULT:
		*val = igb->param_adv_rem_fault;
		break;

	case ETHER_STAT_LP_REMFAULT:
		*val = igb->param_lp_rem_fault;
		break;

	case ETHER_STAT_JABBER_ERRORS:
		igb_ks->rjc.value.ui64 +=
		    E1000_READ_REG(hw, E1000_RJC);
		*val = igb_ks->rjc.value.ui64;
		break;

	case ETHER_STAT_CAP_100T4:
		*val = igb->param_100t4_cap;
		break;

	case ETHER_STAT_ADV_CAP_100T4:
		*val = igb->param_adv_100t4_cap;
		break;

	case ETHER_STAT_LP_CAP_100T4:
		*val = igb->param_lp_100t4_cap;
		break;

	default:
		mutex_exit(&igb->gen_lock);
		return (ENOTSUP);
	}

	mutex_exit(&igb->gen_lock);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * Bring the device out of the reset/quiesced state that it
 * was in when the interface was registered.
 */
int
igb_m_start(void *arg)
{
	igb_t *igb = (igb_t *)arg;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	if (igb_start(igb, B_TRUE) != IGB_SUCCESS) {
		mutex_exit(&igb->gen_lock);
		return (EIO);
	}

	atomic_or_32(&igb->igb_state, IGB_STARTED);

	mutex_exit(&igb->gen_lock);

	/*
	 * Enable and start the watchdog timer
	 */
	igb_enable_watchdog_timer(igb);

	return (0);
}

/*
 * Stop the device and put it in a reset/quiesced state such
 * that the interface can be unregistered.
 */
void
igb_m_stop(void *arg)
{
	igb_t *igb = (igb_t *)arg;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return;
	}

	atomic_and_32(&igb->igb_state, ~IGB_STARTED);

	igb_stop(igb, B_TRUE);

	mutex_exit(&igb->gen_lock);

	/*
	 * Disable and stop the watchdog timer
	 */
	igb_disable_watchdog_timer(igb);
}

/*
 * Set the promiscuity of the device.
 */
int
igb_m_promisc(void *arg, boolean_t on)
{
	igb_t *igb = (igb_t *)arg;
	uint32_t reg_val;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	reg_val = E1000_READ_REG(&igb->hw, E1000_RCTL);

	if (on)
		reg_val |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
	else
		reg_val &= (~(E1000_RCTL_UPE | E1000_RCTL_MPE));

	E1000_WRITE_REG(&igb->hw, E1000_RCTL, reg_val);

	mutex_exit(&igb->gen_lock);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (0);
}

/*
 * Add/remove the addresses to/from the set of multicast
 * addresses for which the device will receive packets.
 */
int
igb_m_multicst(void *arg, boolean_t add, const uint8_t *mcst_addr)
{
	igb_t *igb = (igb_t *)arg;
	int result;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	result = (add) ? igb_multicst_add(igb, mcst_addr)
	    : igb_multicst_remove(igb, mcst_addr);

	mutex_exit(&igb->gen_lock);

	return (result);
}

/*
 * Pass on M_IOCTL messages passed to the DLD, and support
 * private IOCTLs for debugging and ndd.
 */
void
igb_m_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	igb_t *igb = (igb_t *)arg;
	struct iocblk *iocp;
	enum ioc_reply status;

	iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	iocp->ioc_error = 0;

	mutex_enter(&igb->gen_lock);
	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		miocnak(q, mp, 0, EINVAL);
		return;
	}
	mutex_exit(&igb->gen_lock);

	switch (iocp->ioc_cmd) {
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
	case LB_SET_MODE:
		status = igb_loopback_ioctl(igb, iocp, mp);
		break;

	default:
		status = IOC_INVAL;
		break;
	}

	/*
	 * Decide how to reply
	 */
	switch (status) {
	default:
	case IOC_INVAL:
		/*
		 * Error, reply with a NAK and EINVAL or the specified error
		 */
		miocnak(q, mp, 0, iocp->ioc_error == 0 ?
		    EINVAL : iocp->ioc_error);
		break;

	case IOC_DONE:
		/*
		 * OK, reply already sent
		 */
		break;

	case IOC_ACK:
		/*
		 * OK, reply with an ACK
		 */
		miocack(q, mp, 0, 0);
		break;

	case IOC_REPLY:
		/*
		 * OK, send prepared reply as ACK or NAK
		 */
		mp->b_datap->db_type = iocp->ioc_error == 0 ?
		    M_IOCACK : M_IOCNAK;
		qreply(q, mp);
		break;
	}
}

/*
 * Add a MAC address to the target RX group.
 */
static int
igb_addmac(void *arg, const uint8_t *mac_addr)
{
	igb_rx_group_t *rx_group = (igb_rx_group_t *)arg;
	igb_t *igb = rx_group->igb;
	struct e1000_hw *hw = &igb->hw;
	int i, slot;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	if (igb->unicst_avail == 0) {
		/* no slots available */
		mutex_exit(&igb->gen_lock);
		return (ENOSPC);
	}

	/*
	 * The slots from 0 to igb->num_rx_groups are reserved slots which
	 * are 1 to 1 mapped with group index directly. The other slots are
	 * shared between the all of groups. While adding a MAC address,
	 * it will try to set the reserved slots first, then the shared slots.
	 */
	slot = -1;
	if (igb->unicst_addr[rx_group->index].mac.set == 1) {
		/*
		 * The reserved slot for current group is used, find the free
		 * slots in the shared slots.
		 */
		for (i = igb->num_rx_groups; i < igb->unicst_total; i++) {
			if (igb->unicst_addr[i].mac.set == 0) {
				slot = i;
				break;
			}
		}
	} else
		slot = rx_group->index;

	if (slot == -1) {
		/* no slots available in the shared slots */
		mutex_exit(&igb->gen_lock);
		return (ENOSPC);
	}

	/* Set VMDq according to the mode supported by hardware. */
	e1000_rar_set_vmdq(hw, mac_addr, slot, igb->vmdq_mode, rx_group->index);

	bcopy(mac_addr, igb->unicst_addr[slot].mac.addr, ETHERADDRL);
	igb->unicst_addr[slot].mac.group_index = rx_group->index;
	igb->unicst_addr[slot].mac.set = 1;
	igb->unicst_avail--;

	mutex_exit(&igb->gen_lock);

	return (0);
}

/*
 * Remove a MAC address from the specified RX group.
 */
static int
igb_remmac(void *arg, const uint8_t *mac_addr)
{
	igb_rx_group_t *rx_group = (igb_rx_group_t *)arg;
	igb_t *igb = rx_group->igb;
	struct e1000_hw *hw = &igb->hw;
	int slot;

	mutex_enter(&igb->gen_lock);

	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	slot = igb_unicst_find(igb, mac_addr);
	if (slot == -1) {
		mutex_exit(&igb->gen_lock);
		return (EINVAL);
	}

	if (igb->unicst_addr[slot].mac.set == 0) {
		mutex_exit(&igb->gen_lock);
		return (EINVAL);
	}

	/* Clear the MAC ddress in the slot */
	e1000_rar_clear(hw, slot);
	igb->unicst_addr[slot].mac.set = 0;
	igb->unicst_avail++;

	mutex_exit(&igb->gen_lock);

	return (0);
}

/*
 * Enable interrupt on the specificed rx ring.
 */
int
igb_rx_ring_intr_enable(mac_intr_handle_t intrh)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)intrh;
	igb_t *igb = rx_ring->igb;
	struct e1000_hw *hw = &igb->hw;
	uint32_t index = rx_ring->index;

	if (igb->intr_type == DDI_INTR_TYPE_MSIX) {
		/* Interrupt enabling for MSI-X */
		igb->eims_mask |= (E1000_EICR_RX_QUEUE0 << index);
		E1000_WRITE_REG(hw, E1000_EIMS, igb->eims_mask);
		E1000_WRITE_REG(hw, E1000_EIAC, igb->eims_mask);
	} else {
		ASSERT(index == 0);
		/* Interrupt enabling for MSI and legacy */
		igb->ims_mask |= E1000_IMS_RXT0;
		E1000_WRITE_REG(hw, E1000_IMS, igb->ims_mask);
	}

	E1000_WRITE_FLUSH(hw);

	return (0);
}

/*
 * Disable interrupt on the specificed rx ring.
 */
int
igb_rx_ring_intr_disable(mac_intr_handle_t intrh)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)intrh;
	igb_t *igb = rx_ring->igb;
	struct e1000_hw *hw = &igb->hw;
	uint32_t index = rx_ring->index;

	if (igb->intr_type == DDI_INTR_TYPE_MSIX) {
		/* Interrupt disabling for MSI-X */
		igb->eims_mask &= ~(E1000_EICR_RX_QUEUE0 << index);
		E1000_WRITE_REG(hw, E1000_EIMC,
		    (E1000_EICR_RX_QUEUE0 << index));
		E1000_WRITE_REG(hw, E1000_EIAC, igb->eims_mask);
	} else {
		ASSERT(index == 0);
		/* Interrupt disabling for MSI and legacy */
		igb->ims_mask &= ~E1000_IMS_RXT0;
		E1000_WRITE_REG(hw, E1000_IMC, E1000_IMS_RXT0);
	}

	E1000_WRITE_FLUSH(hw);

	return (0);
}

/*
 * Get the global ring index by a ring index within a group.
 */
int
igb_get_rx_ring_index(igb_t *igb, int gindex, int rindex)
{
	igb_rx_ring_t *rx_ring;
	int i;

	for (i = 0; i < igb->num_rx_rings; i++) {
		rx_ring = &igb->rx_rings[i];
		if (rx_ring->group_index == gindex)
			rindex--;
		if (rindex < 0)
			return (i);
	}

	return (-1);
}

static int
igb_ring_start(mac_ring_driver_t rh, uint64_t mr_gen_num)
{
	igb_rx_ring_t *rx_ring = (igb_rx_ring_t *)rh;

	mutex_enter(&rx_ring->rx_lock);
	rx_ring->ring_gen_num = mr_gen_num;
	mutex_exit(&rx_ring->rx_lock);
	return (0);
}

/*
 * Callback funtion for MAC layer to register all rings.
 */
/* ARGSUSED */
void
igb_fill_ring(void *arg, mac_ring_type_t rtype, const int rg_index,
    const int index, mac_ring_info_t *infop, mac_ring_handle_t rh)
{
	igb_t *igb = (igb_t *)arg;
	mac_intr_t *mintr = &infop->mri_intr;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		igb_rx_ring_t *rx_ring;
		int global_index;

		/*
		 * 'index' is the ring index within the group.
		 * We need the global ring index by searching in group.
		 */
		global_index = igb_get_rx_ring_index(igb, rg_index, index);

		ASSERT(global_index >= 0);

		rx_ring = &igb->rx_rings[global_index];
		rx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)rx_ring;
		infop->mri_start = igb_ring_start;
		infop->mri_stop = NULL;
		infop->mri_poll = (mac_ring_poll_t)igb_rx_ring_poll;

		mintr->mi_handle = (mac_intr_handle_t)rx_ring;
		mintr->mi_enable = igb_rx_ring_intr_enable;
		mintr->mi_disable = igb_rx_ring_intr_disable;

		break;
	}
	case MAC_RING_TYPE_TX: {
		ASSERT(index < igb->num_tx_rings);

		igb_tx_ring_t *tx_ring = &igb->tx_rings[index];
		tx_ring->ring_handle = rh;

		infop->mri_driver = (mac_ring_driver_t)tx_ring;
		infop->mri_start = NULL;
		infop->mri_stop = NULL;
		infop->mri_tx = igb_tx_ring_send;

		break;
	}
	default:
		break;
	}
}

void
igb_fill_group(void *arg, mac_ring_type_t rtype, const int index,
    mac_group_info_t *infop, mac_group_handle_t gh)
{
	igb_t *igb = (igb_t *)arg;

	switch (rtype) {
	case MAC_RING_TYPE_RX: {
		igb_rx_group_t *rx_group;

		ASSERT((index >= 0) && (index < igb->num_rx_groups));

		rx_group = &igb->rx_groups[index];
		rx_group->group_handle = gh;

		infop->mgi_driver = (mac_group_driver_t)rx_group;
		infop->mgi_start = NULL;
		infop->mgi_stop = NULL;
		infop->mgi_addmac = igb_addmac;
		infop->mgi_remmac = igb_remmac;
		infop->mgi_count = (igb->num_rx_rings / igb->num_rx_groups);

		break;
	}
	case MAC_RING_TYPE_TX:
		break;
	default:
		break;
	}
}

/*
 * Obtain the MAC's capabilities and associated data from
 * the driver.
 */
boolean_t
igb_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	igb_t *igb = (igb_t *)arg;

	switch (cap) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *tx_hcksum_flags = cap_data;

		/*
		 * We advertise our capabilities only if tx hcksum offload is
		 * enabled.  On receive, the stack will accept checksummed
		 * packets anyway, even if we haven't said we can deliver
		 * them.
		 */
		if (!igb->tx_hcksum_enable)
			return (B_FALSE);

		*tx_hcksum_flags = HCKSUM_INET_PARTIAL | HCKSUM_IPHDRCKSUM;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (igb->lso_enable) {
			cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
			cap_lso->lso_basic_tcp_ipv4.lso_max = IGB_LSO_MAXLEN;
			break;
		} else {
			return (B_FALSE);
		}
	}
	case MAC_CAPAB_RINGS: {
		mac_capab_rings_t *cap_rings = cap_data;

		switch (cap_rings->mr_type) {
		case MAC_RING_TYPE_RX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = igb->num_rx_rings;
			cap_rings->mr_gnum = igb->num_rx_groups;
			cap_rings->mr_rget = igb_fill_ring;
			cap_rings->mr_gget = igb_fill_group;
			cap_rings->mr_gaddring = NULL;
			cap_rings->mr_gremring = NULL;

			break;
		case MAC_RING_TYPE_TX:
			cap_rings->mr_group_type = MAC_GROUP_TYPE_STATIC;
			cap_rings->mr_rnum = igb->num_tx_rings;
			cap_rings->mr_gnum = 0;
			cap_rings->mr_rget = igb_fill_ring;
			cap_rings->mr_gget = NULL;

			break;
		default:
			break;
		}
		break;
	}

	default:
		return (B_FALSE);
	}
	return (B_TRUE);
}

int
igb_m_setprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;
	int err = 0;
	uint32_t flow_control;
	uint32_t cur_mtu, new_mtu;
	uint32_t rx_size;
	uint32_t tx_size;

	mutex_enter(&igb->gen_lock);
	if (igb->igb_state & IGB_SUSPENDED) {
		mutex_exit(&igb->gen_lock);
		return (ECANCELED);
	}

	if (igb->loopback_mode != IGB_LB_NONE && igb_param_locked(pr_num)) {
		/*
		 * All en_* parameters are locked (read-only)
		 * while the device is in any sort of loopback mode.
		 */
		mutex_exit(&igb->gen_lock);
		return (EBUSY);
	}

	switch (pr_num) {
	case MAC_PROP_EN_1000FDX_CAP:
		/* read/write on copper, read-only on serdes */
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_1000fdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_1000fdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_EN_100FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_100fdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_100fdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_EN_100HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_100hdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_100hdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_EN_10FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_10fdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_10fdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_EN_10HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_en_10hdx_cap = *(uint8_t *)pr_val;
		igb->param_adv_10hdx_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_AUTONEG:
		if (hw->phy.media_type != e1000_media_type_copper) {
			err = ENOTSUP;
			break;
		}
		igb->param_adv_autoneg_cap = *(uint8_t *)pr_val;
		goto setup_link;
	case MAC_PROP_FLOWCTRL:
		bcopy(pr_val, &flow_control, sizeof (flow_control));

		switch (flow_control) {
		default:
			err = EINVAL;
			break;
		case LINK_FLOWCTRL_NONE:
			hw->fc.requested_mode = e1000_fc_none;
			break;
		case LINK_FLOWCTRL_RX:
			hw->fc.requested_mode = e1000_fc_rx_pause;
			break;
		case LINK_FLOWCTRL_TX:
			hw->fc.requested_mode = e1000_fc_tx_pause;
			break;
		case LINK_FLOWCTRL_BI:
			hw->fc.requested_mode = e1000_fc_full;
			break;
		}
setup_link:
		if (err == 0) {
			if (igb_setup_link(igb, B_TRUE) != IGB_SUCCESS)
				err = EINVAL;
		}
		break;
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_ADV_1000HDX_CAP:
	case MAC_PROP_ADV_100T4_CAP:
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_ADV_100HDX_CAP:
	case MAC_PROP_ADV_10FDX_CAP:
	case MAC_PROP_ADV_10HDX_CAP:
	case MAC_PROP_EN_1000HDX_CAP:
	case MAC_PROP_EN_100T4_CAP:
	case MAC_PROP_STATUS:
	case MAC_PROP_SPEED:
	case MAC_PROP_DUPLEX:
		err = ENOTSUP; /* read-only prop. Can't set this. */
		break;
	case MAC_PROP_MTU:
		/* adapter must be stopped for an MTU change */
		if (igb->igb_state & IGB_STARTED) {
			err = EBUSY;
			break;
		}

		cur_mtu = igb->default_mtu;
		bcopy(pr_val, &new_mtu, sizeof (new_mtu));
		if (new_mtu == cur_mtu) {
			err = 0;
			break;
		}

		if (new_mtu < MIN_MTU || new_mtu > MAX_MTU) {
			err = EINVAL;
			break;
		}

		err = mac_maxsdu_update(igb->mac_hdl, new_mtu);
		if (err == 0) {
			igb->default_mtu = new_mtu;
			igb->max_frame_size = igb->default_mtu +
			    sizeof (struct ether_vlan_header) + ETHERFCSL;

			/*
			 * Set rx buffer size
			 */
			rx_size = igb->max_frame_size + IPHDR_ALIGN_ROOM;
			igb->rx_buf_size = ((rx_size >> 10) + ((rx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;

			/*
			 * Set tx buffer size
			 */
			tx_size = igb->max_frame_size;
			igb->tx_buf_size = ((tx_size >> 10) + ((tx_size &
			    (((uint32_t)1 << 10) - 1)) > 0 ? 1 : 0)) << 10;
		}
		break;
	case MAC_PROP_PRIVATE:
		err = igb_set_priv_prop(igb, pr_name, pr_valsize, pr_val);
		break;
	default:
		err = EINVAL;
		break;
	}

	mutex_exit(&igb->gen_lock);

	if (igb_check_acc_handle(igb->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igb->dip, DDI_SERVICE_DEGRADED);
		return (EIO);
	}

	return (err);
}

int
igb_m_getprop(void *arg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_flags, uint_t pr_valsize, void *pr_val, uint_t *perm)
{
	igb_t *igb = (igb_t *)arg;
	struct e1000_hw *hw = &igb->hw;
	int err = 0;
	uint32_t flow_control;
	uint64_t tmp = 0;
	mac_propval_range_t range;

	if (pr_valsize == 0)
		return (EINVAL);

	*perm = MAC_PROP_PERM_RW;

	bzero(pr_val, pr_valsize);
	if ((pr_flags & MAC_PROP_DEFAULT) && (pr_num != MAC_PROP_PRIVATE))
		return (igb_get_def_val(igb, pr_num, pr_valsize, pr_val));

	switch (pr_num) {
	case MAC_PROP_DUPLEX:
		*perm = MAC_PROP_PERM_READ;
		if (pr_valsize >= sizeof (link_duplex_t)) {
			bcopy(&igb->link_duplex, pr_val,
			    sizeof (link_duplex_t));
		} else
			err = EINVAL;
		break;
	case MAC_PROP_SPEED:
		*perm = MAC_PROP_PERM_READ;
		if (pr_valsize >= sizeof (uint64_t)) {
			tmp = igb->link_speed * 1000000ull;
			bcopy(&tmp, pr_val, sizeof (tmp));
		} else
			err = EINVAL;
		break;
	case MAC_PROP_AUTONEG:
		if (hw->phy.media_type != e1000_media_type_copper)
			*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_adv_autoneg_cap;
		break;
	case MAC_PROP_FLOWCTRL:
		if (pr_valsize >= sizeof (uint32_t)) {
			switch (hw->fc.requested_mode) {
				case e1000_fc_none:
					flow_control = LINK_FLOWCTRL_NONE;
					break;
				case e1000_fc_rx_pause:
					flow_control = LINK_FLOWCTRL_RX;
					break;
				case e1000_fc_tx_pause:
					flow_control = LINK_FLOWCTRL_TX;
					break;
				case e1000_fc_full:
					flow_control = LINK_FLOWCTRL_BI;
					break;
			}
			bcopy(&flow_control, pr_val, sizeof (flow_control));
		} else
			err = EINVAL;
		break;
	case MAC_PROP_ADV_1000FDX_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_adv_1000fdx_cap;
		break;
	case MAC_PROP_EN_1000FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_en_1000fdx_cap;
		break;
	case MAC_PROP_ADV_1000HDX_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_adv_1000hdx_cap;
		break;
	case MAC_PROP_EN_1000HDX_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_en_1000hdx_cap;
		break;
	case MAC_PROP_ADV_100T4_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_adv_100t4_cap;
		break;
	case MAC_PROP_EN_100T4_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_en_100t4_cap;
		break;
	case MAC_PROP_ADV_100FDX_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_adv_100fdx_cap;
		break;
	case MAC_PROP_EN_100FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_en_100fdx_cap;
		break;
	case MAC_PROP_ADV_100HDX_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_adv_100hdx_cap;
		break;
	case MAC_PROP_EN_100HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_en_100hdx_cap;
		break;
	case MAC_PROP_ADV_10FDX_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_adv_10fdx_cap;
		break;
	case MAC_PROP_EN_10FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_en_10fdx_cap;
		break;
	case MAC_PROP_ADV_10HDX_CAP:
		*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_adv_10hdx_cap;
		break;
	case MAC_PROP_EN_10HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper)
			*perm = MAC_PROP_PERM_READ;
		*(uint8_t *)pr_val = igb->param_en_10hdx_cap;
		break;
	case MAC_PROP_PRIVATE:
		err = igb_get_priv_prop(igb, pr_name,
		    pr_flags, pr_valsize, pr_val, perm);
		break;
	case MAC_PROP_MTU:
		if (!(pr_flags & MAC_PROP_POSSIBLE))
			return (ENOTSUP);
		if (pr_valsize < sizeof (mac_propval_range_t))
			return (EINVAL);
		range.mpr_count = 1;
		range.mpr_type = MAC_PROPVAL_UINT32;
		range.range_uint32[0].mpur_min = MIN_MTU;
		range.range_uint32[0].mpur_max = MAX_MTU;
		bcopy(&range, pr_val, sizeof (range));
		break;
	default:
		err = EINVAL;
		break;
	}
	return (err);
}

int
igb_get_def_val(igb_t *igb, mac_prop_id_t pr_num,
    uint_t pr_valsize, void *pr_val)
{
	uint32_t flow_control;
	struct e1000_hw *hw = &igb->hw;
	uint16_t phy_status;
	uint16_t phy_ext_status;
	int err = 0;

	ASSERT(pr_valsize > 0);
	switch (pr_num) {
	case MAC_PROP_AUTONEG:
		if (hw->phy.media_type != e1000_media_type_copper) {
			*(uint8_t *)pr_val = 0;
		} else {
			(void) e1000_read_phy_reg(hw, PHY_STATUS, &phy_status);
			*(uint8_t *)pr_val =
			    (phy_status & MII_SR_AUTONEG_CAPS) ? 1 : 0;
		}
		break;
	case MAC_PROP_FLOWCTRL:
		if (pr_valsize < sizeof (uint32_t))
			return (EINVAL);
		flow_control = LINK_FLOWCTRL_BI;
		bcopy(&flow_control, pr_val, sizeof (flow_control));
		break;
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_EN_1000FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			*(uint8_t *)pr_val = 1;
		} else {
			(void) e1000_read_phy_reg(hw,
			    PHY_EXT_STATUS, &phy_ext_status);
			*(uint8_t *)pr_val =
			    ((phy_ext_status & IEEE_ESR_1000T_FD_CAPS) ||
			    (phy_ext_status & IEEE_ESR_1000X_FD_CAPS)) ? 1 : 0;
		}
		break;
	case MAC_PROP_ADV_1000HDX_CAP:
	case MAC_PROP_EN_1000HDX_CAP:
	case MAC_PROP_ADV_100T4_CAP:
	case MAC_PROP_EN_100T4_CAP:
		*(uint8_t *)pr_val = 0;
		break;
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			*(uint8_t *)pr_val = 0;
		} else {
			(void) e1000_read_phy_reg(hw, PHY_STATUS, &phy_status);
			*(uint8_t *)pr_val =
			    ((phy_status & MII_SR_100X_FD_CAPS) ||
			    (phy_status & MII_SR_100T2_FD_CAPS)) ? 1 : 0;
		}
		break;
	case MAC_PROP_ADV_100HDX_CAP:
	case MAC_PROP_EN_100HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			*(uint8_t *)pr_val = 0;
		} else {
			(void) e1000_read_phy_reg(hw, PHY_STATUS, &phy_status);
			*(uint8_t *)pr_val =
			    ((phy_status & MII_SR_100X_HD_CAPS) ||
			    (phy_status & MII_SR_100T2_HD_CAPS)) ? 1 : 0;
		}
		break;
	case MAC_PROP_ADV_10FDX_CAP:
	case MAC_PROP_EN_10FDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			*(uint8_t *)pr_val = 0;
		} else {
			(void) e1000_read_phy_reg(hw, PHY_STATUS, &phy_status);
			*(uint8_t *)pr_val =
			    (phy_status & MII_SR_10T_FD_CAPS) ? 1 : 0;
		}
		break;
	case MAC_PROP_ADV_10HDX_CAP:
	case MAC_PROP_EN_10HDX_CAP:
		if (hw->phy.media_type != e1000_media_type_copper) {
			*(uint8_t *)pr_val = 0;
		} else {
			(void) e1000_read_phy_reg(hw, PHY_STATUS, &phy_status);
			*(uint8_t *)pr_val =
			    (phy_status & MII_SR_10T_HD_CAPS) ? 1 : 0;
		}
		break;
	default:
		err = ENOTSUP;
		break;
	}
	return (err);
}

boolean_t
igb_param_locked(mac_prop_id_t pr_num)
{
	/*
	 * All en_* parameters are locked (read-only) while
	 * the device is in any sort of loopback mode ...
	 */
	switch (pr_num) {
		case MAC_PROP_EN_1000FDX_CAP:
		case MAC_PROP_EN_1000HDX_CAP:
		case MAC_PROP_EN_100T4_CAP:
		case MAC_PROP_EN_100FDX_CAP:
		case MAC_PROP_EN_100HDX_CAP:
		case MAC_PROP_EN_10FDX_CAP:
		case MAC_PROP_EN_10HDX_CAP:
		case MAC_PROP_AUTONEG:
		case MAC_PROP_FLOWCTRL:
			return (B_TRUE);
	}
	return (B_FALSE);
}

/* ARGSUSED */
int
igb_set_priv_prop(igb_t *igb, const char *pr_name,
    uint_t pr_valsize, const void *pr_val)
{
	int err = 0;
	long result;
	struct e1000_hw *hw = &igb->hw;
	int i;

	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_TX_COPY_THRESHOLD ||
		    result > MAX_TX_COPY_THRESHOLD)
			err = EINVAL;
		else {
			igb->tx_copy_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_TX_RECYCLE_THRESHOLD ||
		    result > MAX_TX_RECYCLE_THRESHOLD)
			err = EINVAL;
		else {
			igb->tx_recycle_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_TX_OVERLOAD_THRESHOLD ||
		    result > MAX_TX_OVERLOAD_THRESHOLD)
			err = EINVAL;
		else {
			igb->tx_overload_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_TX_RESCHED_THRESHOLD ||
		    result > MAX_TX_RESCHED_THRESHOLD)
			err = EINVAL;
		else {
			igb->tx_resched_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_RX_COPY_THRESHOLD ||
		    result > MAX_RX_COPY_THRESHOLD)
			err = EINVAL;
		else {
			igb->rx_copy_thresh = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		if (result < MIN_RX_LIMIT_PER_INTR ||
		    result > MAX_RX_LIMIT_PER_INTR)
			err = EINVAL;
		else {
			igb->rx_limit_per_intr = (uint32_t)result;
		}
		return (err);
	}
	if (strcmp(pr_name, "_intr_throttling") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		if (result < igb->capab->min_intr_throttle ||
		    result > igb->capab->max_intr_throttle)
			err = EINVAL;
		else {
			igb->intr_throttling[0] = (uint32_t)result;

			for (i = 0; i < MAX_NUM_EITR; i++)
				igb->intr_throttling[i] =
				    igb->intr_throttling[0];

			/* Set interrupt throttling rate */
			for (i = 0; i < igb->intr_cnt; i++)
				E1000_WRITE_REG(hw, E1000_EITR(i),
				    igb->intr_throttling[i]);
		}
		return (err);
	}
	return (ENOTSUP);
}

int
igb_get_priv_prop(igb_t *igb, const char *pr_name,
    uint_t pr_flags, uint_t pr_valsize, void *pr_val, uint_t *perm)
{
	int err = ENOTSUP;
	boolean_t is_default = (pr_flags & MAC_PROP_DEFAULT);
	int value;

	*perm = MAC_PROP_PERM_RW;

	if (strcmp(pr_name, "_adv_pause_cap") == 0) {
		*perm = MAC_PROP_PERM_READ;
		value = (is_default ? 1 : igb->param_adv_pause_cap);
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_adv_asym_pause_cap") == 0) {
		*perm = MAC_PROP_PERM_READ;
		value = (is_default ? 1 : igb->param_adv_asym_pause_cap);
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_copy_thresh") == 0) {
		value = (is_default ? DEFAULT_TX_COPY_THRESHOLD :
		    igb->tx_copy_thresh);
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_recycle_thresh") == 0) {
		value = (is_default ? DEFAULT_TX_RECYCLE_THRESHOLD :
		    igb->tx_recycle_thresh);
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_overload_thresh") == 0) {
		value = (is_default ? DEFAULT_TX_OVERLOAD_THRESHOLD :
		    igb->tx_overload_thresh);
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_tx_resched_thresh") == 0) {
		value = (is_default ? DEFAULT_TX_RESCHED_THRESHOLD :
		    igb->tx_resched_thresh);
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_rx_copy_thresh") == 0) {
		value = (is_default ? DEFAULT_RX_COPY_THRESHOLD :
		    igb->rx_copy_thresh);
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_rx_limit_per_intr") == 0) {
		value = (is_default ? DEFAULT_RX_LIMIT_PER_INTR :
		    igb->rx_limit_per_intr);
		err = 0;
		goto done;
	}
	if (strcmp(pr_name, "_intr_throttling") == 0) {
		value = (is_default ? igb->capab->def_intr_throttle :
		    igb->intr_throttling[0]);
		err = 0;
		goto done;
	}
done:
	if (err == 0) {
		(void) snprintf(pr_val, pr_valsize, "%d", value);
	}
	return (err);
}
