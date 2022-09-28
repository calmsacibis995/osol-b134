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
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/ksynch.h>
#include <sys/stat.h>
#include <sys/modctl.h>
#include <sys/debug.h>
#include <sys/ethernet.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/strsun.h>
#include <sys/note.h>
#include <sys/mac_provider.h>
#include <sys/mac_ether.h>
#include <sys/ldc.h>
#include <sys/mach_descrip.h>
#include <sys/mdeg.h>
#include <net/if.h>
#include <sys/vnet.h>
#include <sys/vio_mailbox.h>
#include <sys/vio_common.h>
#include <sys/vnet_common.h>
#include <sys/vnet_mailbox.h>
#include <sys/vio_util.h>
#include <sys/vnet_gen.h>
#include <sys/atomic.h>
#include <sys/callb.h>
#include <sys/sdt.h>
#include <sys/intr.h>
#include <sys/pattr.h>
#include <sys/vlan.h>

/*
 * Implementation of the mac functionality for vnet using the
 * generic(default) transport layer of sun4v Logical Domain Channels(LDC).
 */

/*
 * Function prototypes.
 */
/* vgen proxy entry points */
int vgen_init(void *vnetp, uint64_t regprop, dev_info_t *vnetdip,
    const uint8_t *macaddr, void **vgenhdl);
int vgen_init_mdeg(void *arg);
void vgen_uninit(void *arg);
int vgen_dds_tx(void *arg, void *dmsg);
void vgen_mod_init(void);
int vgen_mod_cleanup(void);
void vgen_mod_fini(void);
int vgen_enable_intr(void *arg);
int vgen_disable_intr(void *arg);
mblk_t *vgen_poll(void *arg, int bytes_to_pickup);
static int vgen_start(void *arg);
static void vgen_stop(void *arg);
static mblk_t *vgen_tx(void *arg, mblk_t *mp);
static int vgen_multicst(void *arg, boolean_t add,
	const uint8_t *mca);
static int vgen_promisc(void *arg, boolean_t on);
static int vgen_unicst(void *arg, const uint8_t *mca);
static int vgen_stat(void *arg, uint_t stat, uint64_t *val);
static void vgen_ioctl(void *arg, queue_t *q, mblk_t *mp);
#ifdef	VNET_IOC_DEBUG
static int vgen_force_link_state(vgen_port_t *portp, int link_state);
#endif

/* vgen internal functions */
static int vgen_read_mdprops(vgen_t *vgenp);
static void vgen_update_md_prop(vgen_t *vgenp, md_t *mdp, mde_cookie_t mdex);
static void vgen_read_pri_eth_types(vgen_t *vgenp, md_t *mdp,
	mde_cookie_t node);
static void vgen_mtu_read(vgen_t *vgenp, md_t *mdp, mde_cookie_t node,
	uint32_t *mtu);
static void vgen_linkprop_read(vgen_t *vgenp, md_t *mdp, mde_cookie_t node,
	boolean_t *pls);
static void vgen_detach_ports(vgen_t *vgenp);
static void vgen_port_detach(vgen_port_t *portp);
static void vgen_port_list_insert(vgen_port_t *portp);
static void vgen_port_list_remove(vgen_port_t *portp);
static vgen_port_t *vgen_port_lookup(vgen_portlist_t *plistp,
	int port_num);
static int vgen_mdeg_reg(vgen_t *vgenp);
static void vgen_mdeg_unreg(vgen_t *vgenp);
static int vgen_mdeg_cb(void *cb_argp, mdeg_result_t *resp);
static int vgen_mdeg_port_cb(void *cb_argp, mdeg_result_t *resp);
static int vgen_add_port(vgen_t *vgenp, md_t *mdp, mde_cookie_t mdex);
static int vgen_port_read_props(vgen_port_t *portp, vgen_t *vgenp, md_t *mdp,
	mde_cookie_t mdex);
static int vgen_remove_port(vgen_t *vgenp, md_t *mdp, mde_cookie_t mdex);
static int vgen_port_attach(vgen_port_t *portp);
static void vgen_port_detach_mdeg(vgen_port_t *portp);
static int vgen_update_port(vgen_t *vgenp, md_t *curr_mdp,
	mde_cookie_t curr_mdex, md_t *prev_mdp, mde_cookie_t prev_mdex);
static uint64_t	vgen_port_stat(vgen_port_t *portp, uint_t stat);
static void vgen_port_reset(vgen_port_t *portp);
static void vgen_reset_vsw_port(vgen_t *vgenp);
static void vgen_ldc_reset(vgen_ldc_t *ldcp);
static int vgen_ldc_attach(vgen_port_t *portp, uint64_t ldc_id);
static void vgen_ldc_detach(vgen_ldc_t *ldcp);
static int vgen_alloc_tx_ring(vgen_ldc_t *ldcp);
static void vgen_free_tx_ring(vgen_ldc_t *ldcp);
static void vgen_init_ports(vgen_t *vgenp);
static void vgen_port_init(vgen_port_t *portp);
static void vgen_uninit_ports(vgen_t *vgenp);
static void vgen_port_uninit(vgen_port_t *portp);
static void vgen_init_ldcs(vgen_port_t *portp);
static void vgen_uninit_ldcs(vgen_port_t *portp);
static int vgen_ldc_init(vgen_ldc_t *ldcp);
static void vgen_ldc_uninit(vgen_ldc_t *ldcp);
static int vgen_init_tbufs(vgen_ldc_t *ldcp);
static void vgen_uninit_tbufs(vgen_ldc_t *ldcp);
static void vgen_clobber_tbufs(vgen_ldc_t *ldcp);
static void vgen_clobber_rxds(vgen_ldc_t *ldcp);
static uint64_t	vgen_ldc_stat(vgen_ldc_t *ldcp, uint_t stat);
static uint_t vgen_ldc_cb(uint64_t event, caddr_t arg);
static int vgen_portsend(vgen_port_t *portp, mblk_t *mp);
static int vgen_ldcsend(void *arg, mblk_t *mp);
static void vgen_ldcsend_pkt(void *arg, mblk_t *mp);
static int vgen_ldcsend_dring(void *arg, mblk_t *mp);
static void vgen_reclaim(vgen_ldc_t *ldcp);
static void vgen_reclaim_dring(vgen_ldc_t *ldcp);
static int vgen_num_txpending(vgen_ldc_t *ldcp);
static int vgen_tx_dring_full(vgen_ldc_t *ldcp);
static int vgen_ldc_txtimeout(vgen_ldc_t *ldcp);
static void vgen_ldc_watchdog(void *arg);
static mblk_t *vgen_ldc_poll(vgen_ldc_t *ldcp, int bytes_to_pickup);

/* vgen handshake functions */
static vgen_ldc_t *vh_nextphase(vgen_ldc_t *ldcp);
static int vgen_sendmsg(vgen_ldc_t *ldcp, caddr_t msg,  size_t msglen,
	boolean_t caller_holds_lock);
static int vgen_send_version_negotiate(vgen_ldc_t *ldcp);
static int vgen_send_attr_info(vgen_ldc_t *ldcp);
static int vgen_send_dring_reg(vgen_ldc_t *ldcp);
static int vgen_send_rdx_info(vgen_ldc_t *ldcp);
static int vgen_send_dring_data(vgen_ldc_t *ldcp, uint32_t start, int32_t end);
static int vgen_send_mcast_info(vgen_ldc_t *ldcp);
static int vgen_handshake_phase2(vgen_ldc_t *ldcp);
static void vgen_handshake_reset(vgen_ldc_t *ldcp);
static void vgen_reset_hphase(vgen_ldc_t *ldcp);
static void vgen_handshake(vgen_ldc_t *ldcp);
static int vgen_handshake_done(vgen_ldc_t *ldcp);
static void vgen_handshake_retry(vgen_ldc_t *ldcp);
static int vgen_handle_version_negotiate(vgen_ldc_t *ldcp,
	vio_msg_tag_t *tagp);
static int vgen_handle_attr_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_handle_dring_reg(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_handle_rdx_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_handle_mcast_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_handle_ctrlmsg(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static void vgen_handle_pkt_data_nop(void *arg1, void *arg2, uint32_t msglen);
static void vgen_handle_pkt_data(void *arg1, void *arg2, uint32_t msglen);
static int vgen_handle_dring_data(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_handle_dring_data_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_process_dring_data(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_handle_dring_data_ack(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_handle_dring_data_nack(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_send_dring_ack(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp,
	uint32_t start, int32_t end, uint8_t pstate);
static int vgen_handle_datamsg(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp,
	uint32_t msglen);
static void vgen_handle_errmsg(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static void vgen_handle_evt_up(vgen_ldc_t *ldcp);
static void vgen_handle_evt_reset(vgen_ldc_t *ldcp);
static int vgen_check_sid(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static int vgen_check_datamsg_seq(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);
static caddr_t vgen_print_ethaddr(uint8_t *a, char *ebuf);
static void vgen_hwatchdog(void *arg);
static void vgen_print_attr_info(vgen_ldc_t *ldcp, int endpoint);
static void vgen_print_hparams(vgen_hparams_t *hp);
static void vgen_print_ldcinfo(vgen_ldc_t *ldcp);
static void vgen_stop_rcv_thread(vgen_ldc_t *ldcp);
static void vgen_drain_rcv_thread(vgen_ldc_t *ldcp);
static void vgen_ldc_rcv_worker(void *arg);
static void vgen_handle_evt_read(vgen_ldc_t *ldcp);
static void vgen_rx(vgen_ldc_t *ldcp, mblk_t *bp, mblk_t *bpt);
static void vgen_set_vnet_proto_ops(vgen_ldc_t *ldcp);
static void vgen_reset_vnet_proto_ops(vgen_ldc_t *ldcp);
static void vgen_link_update(vgen_t *vgenp, link_state_t link_state);

/* VLAN routines */
static void vgen_vlan_read_ids(void *arg, int type, md_t *mdp,
	mde_cookie_t node, uint16_t *pvidp, uint16_t **vidspp,
	uint16_t *nvidsp, uint16_t *default_idp);
static void vgen_vlan_create_hash(vgen_port_t *portp);
static void vgen_vlan_destroy_hash(vgen_port_t *portp);
static void vgen_vlan_add_ids(vgen_port_t *portp);
static void vgen_vlan_remove_ids(vgen_port_t *portp);
static boolean_t vgen_vlan_lookup(mod_hash_t *vlan_hashp, uint16_t vid);
static boolean_t vgen_frame_lookup_vid(vnet_t *vnetp, struct ether_header *ehp,
	uint16_t *vidp);
static mblk_t *vgen_vlan_frame_fixtag(vgen_port_t *portp, mblk_t *mp,
	boolean_t is_tagged, uint16_t vid);
static void vgen_vlan_unaware_port_reset(vgen_port_t *portp);
static void vgen_reset_vlan_unaware_ports(vgen_t *vgenp);
static int vgen_dds_rx(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp);

/* externs */
extern void vnet_dds_rx(void *arg, void *dmsg);
extern void vnet_dds_cleanup_hio(vnet_t *vnetp);
extern int vnet_mtu_update(vnet_t *vnetp, uint32_t mtu);
extern void vnet_link_update(vnet_t *vnetp, link_state_t link_state);

/*
 * The handshake process consists of 5 phases defined below, with VH_PHASE0
 * being the pre-handshake phase and VH_DONE is the phase to indicate
 * successful completion of all phases.
 * Each phase may have one to several handshake states which are required
 * to complete successfully to move to the next phase.
 * Refer to the functions vgen_handshake() and vgen_handshake_done() for
 * more details.
 */
/* handshake phases */
enum {	VH_PHASE0, VH_PHASE1, VH_PHASE2, VH_PHASE3, VH_DONE = 0x80 };

/* handshake states */
enum {

	VER_INFO_SENT	=	0x1,
	VER_ACK_RCVD	=	0x2,
	VER_INFO_RCVD	=	0x4,
	VER_ACK_SENT	=	0x8,
	VER_NEGOTIATED	=	(VER_ACK_RCVD | VER_ACK_SENT),

	ATTR_INFO_SENT	=	0x10,
	ATTR_ACK_RCVD	=	0x20,
	ATTR_INFO_RCVD	=	0x40,
	ATTR_ACK_SENT	=	0x80,
	ATTR_INFO_EXCHANGED	=	(ATTR_ACK_RCVD | ATTR_ACK_SENT),

	DRING_INFO_SENT	=	0x100,
	DRING_ACK_RCVD	=	0x200,
	DRING_INFO_RCVD	=	0x400,
	DRING_ACK_SENT	=	0x800,
	DRING_INFO_EXCHANGED	=	(DRING_ACK_RCVD | DRING_ACK_SENT),

	RDX_INFO_SENT	=	0x1000,
	RDX_ACK_RCVD	=	0x2000,
	RDX_INFO_RCVD	=	0x4000,
	RDX_ACK_SENT	=	0x8000,
	RDX_EXCHANGED	=	(RDX_ACK_RCVD | RDX_ACK_SENT)

};

#define	VGEN_PRI_ETH_DEFINED(vgenp)	((vgenp)->pri_num_types != 0)

#define	LDC_LOCK(ldcp)	\
				mutex_enter(&((ldcp)->cblock));\
				mutex_enter(&((ldcp)->rxlock));\
				mutex_enter(&((ldcp)->wrlock));\
				mutex_enter(&((ldcp)->txlock));\
				mutex_enter(&((ldcp)->tclock));
#define	LDC_UNLOCK(ldcp)	\
				mutex_exit(&((ldcp)->tclock));\
				mutex_exit(&((ldcp)->txlock));\
				mutex_exit(&((ldcp)->wrlock));\
				mutex_exit(&((ldcp)->rxlock));\
				mutex_exit(&((ldcp)->cblock));

#define	VGEN_VER_EQ(ldcp, major, minor)	\
	((ldcp)->local_hparams.ver_major == (major) &&	\
	    (ldcp)->local_hparams.ver_minor == (minor))

#define	VGEN_VER_LT(ldcp, major, minor)	\
	(((ldcp)->local_hparams.ver_major < (major)) ||	\
	    ((ldcp)->local_hparams.ver_major == (major) &&	\
	    (ldcp)->local_hparams.ver_minor < (minor)))

#define	VGEN_VER_GTEQ(ldcp, major, minor)	\
	(((ldcp)->local_hparams.ver_major > (major)) ||	\
	    ((ldcp)->local_hparams.ver_major == (major) &&	\
	    (ldcp)->local_hparams.ver_minor >= (minor)))

static struct ether_addr etherbroadcastaddr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
/*
 * MIB II broadcast/multicast packets
 */
#define	IS_BROADCAST(ehp) \
		(ether_cmp(&ehp->ether_dhost, &etherbroadcastaddr) == 0)
#define	IS_MULTICAST(ehp) \
		((ehp->ether_dhost.ether_addr_octet[0] & 01) == 1)

/*
 * Property names
 */
static char macaddr_propname[] = "mac-address";
static char rmacaddr_propname[] = "remote-mac-address";
static char channel_propname[] = "channel-endpoint";
static char reg_propname[] = "reg";
static char port_propname[] = "port";
static char swport_propname[] = "switch-port";
static char id_propname[] = "id";
static char vdev_propname[] = "virtual-device";
static char vnet_propname[] = "network";
static char pri_types_propname[] = "priority-ether-types";
static char vgen_pvid_propname[] = "port-vlan-id";
static char vgen_vid_propname[] = "vlan-id";
static char vgen_dvid_propname[] = "default-vlan-id";
static char port_pvid_propname[] = "remote-port-vlan-id";
static char port_vid_propname[] = "remote-vlan-id";
static char vgen_mtu_propname[] = "mtu";
static char vgen_linkprop_propname[] = "linkprop";

/*
 * VIO Protocol Version Info:
 *
 * The version specified below represents the version of protocol currently
 * supported in the driver. It means the driver can negotiate with peers with
 * versions <= this version. Here is a summary of the feature(s) that are
 * supported at each version of the protocol:
 *
 * 1.0			Basic VIO protocol.
 * 1.1			vDisk protocol update (no virtual network update).
 * 1.2			Support for priority frames (priority-ether-types).
 * 1.3			VLAN and HybridIO support.
 * 1.4			Jumbo Frame support.
 * 1.5			Link State Notification support with optional support
 * 			for Physical Link information.
 */
static vgen_ver_t vgen_versions[VGEN_NUM_VER] =  { {1, 5} };

/* Tunables */
uint32_t vgen_hwd_interval = 5;		/* handshake watchdog freq in sec */
uint32_t vgen_max_hretries = VNET_NUM_HANDSHAKES; /* # of handshake retries */
uint32_t vgen_ldcwr_retries = 10;	/* max # of ldc_write() retries */
uint32_t vgen_ldcup_retries = 5;	/* max # of ldc_up() retries */
uint32_t vgen_ldccl_retries = 5;	/* max # of ldc_close() retries */
uint32_t vgen_recv_delay = 1;		/* delay when rx descr not ready */
uint32_t vgen_recv_retries = 10;	/* retry when rx descr not ready */
uint32_t vgen_tx_retries = 0x4;		/* retry when tx descr not available */
uint32_t vgen_tx_delay = 0x30;		/* delay when tx descr not available */

int vgen_rcv_thread_enabled = 1;	/* Enable Recieve thread */

static vio_mblk_pool_t	*vgen_rx_poolp = NULL;
static krwlock_t	vgen_rw;

/*
 * max # of packets accumulated prior to sending them up. It is best
 * to keep this at 60% of the number of recieve buffers.
 */
uint32_t vgen_chain_len = (VGEN_NRBUFS * 0.6);

/*
 * Internal tunables for receive buffer pools, that is,  the size and number of
 * mblks for each pool. At least 3 sizes must be specified if these are used.
 * The sizes must be specified in increasing order. Non-zero value of the first
 * size will be used as a hint to use these values instead of the algorithm
 * that determines the sizes based on MTU.
 */
uint32_t vgen_rbufsz1 = 0;
uint32_t vgen_rbufsz2 = 0;
uint32_t vgen_rbufsz3 = 0;
uint32_t vgen_rbufsz4 = 0;

uint32_t vgen_nrbufs1 = VGEN_NRBUFS;
uint32_t vgen_nrbufs2 = VGEN_NRBUFS;
uint32_t vgen_nrbufs3 = VGEN_NRBUFS;
uint32_t vgen_nrbufs4 = VGEN_NRBUFS;

/*
 * In the absence of "priority-ether-types" property in MD, the following
 * internal tunable can be set to specify a single priority ethertype.
 */
uint64_t vgen_pri_eth_type = 0;

/*
 * Number of transmit priority buffers that are preallocated per device.
 * This number is chosen to be a small value to throttle transmission
 * of priority packets. Note: Must be a power of 2 for vio_create_mblks().
 */
uint32_t vgen_pri_tx_nmblks = 64;

uint32_t	vgen_vlan_nchains = 4;	/* # of chains in vlan id hash table */

#ifdef DEBUG
/* flags to simulate error conditions for debugging */
int vgen_trigger_txtimeout = 0;
int vgen_trigger_rxlost = 0;
#endif

/*
 * Matching criteria passed to the MDEG to register interest
 * in changes to 'virtual-device' nodes (i.e. vnet nodes) identified
 * by their 'name' and 'cfg-handle' properties.
 */
static md_prop_match_t vdev_prop_match[] = {
	{ MDET_PROP_STR,    "name"   },
	{ MDET_PROP_VAL,    "cfg-handle" },
	{ MDET_LIST_END,    NULL    }
};

static mdeg_node_match_t vdev_match = { "virtual-device",
						vdev_prop_match };

/* MD update matching structure */
static md_prop_match_t	vport_prop_match[] = {
	{ MDET_PROP_VAL,	"id" },
	{ MDET_LIST_END,	NULL }
};

static mdeg_node_match_t vport_match = { "virtual-device-port",
					vport_prop_match };

/* template for matching a particular vnet instance */
static mdeg_prop_spec_t vgen_prop_template[] = {
	{ MDET_PROP_STR,	"name",		"network" },
	{ MDET_PROP_VAL,	"cfg-handle",	NULL },
	{ MDET_LIST_END,	NULL,		NULL }
};

#define	VGEN_SET_MDEG_PROP_INST(specp, val)	(specp)[1].ps_val = (val)

static int vgen_mdeg_port_cb(void *cb_argp, mdeg_result_t *resp);

#ifdef	VNET_IOC_DEBUG
#define	VGEN_M_CALLBACK_FLAGS	(MC_IOCTL)
#else
#define	VGEN_M_CALLBACK_FLAGS	(0)
#endif

static mac_callbacks_t vgen_m_callbacks = {
	VGEN_M_CALLBACK_FLAGS,
	vgen_stat,
	vgen_start,
	vgen_stop,
	vgen_promisc,
	vgen_multicst,
	vgen_unicst,
	vgen_tx,
	vgen_ioctl,
	NULL,
	NULL
};

/* externs */
extern pri_t	maxclsyspri;
extern proc_t	p0;
extern uint32_t vnet_ntxds;
extern uint32_t vnet_ldcwd_interval;
extern uint32_t vnet_ldcwd_txtimeout;
extern uint32_t vnet_ldc_mtu;
extern uint32_t vnet_nrbufs;
extern uint32_t	vnet_ethermtu;
extern uint16_t	vnet_default_vlan_id;
extern boolean_t vnet_jumbo_rxpools;

#ifdef DEBUG

extern int vnet_dbglevel;
static void debug_printf(const char *fname, vgen_t *vgenp,
	vgen_ldc_t *ldcp, const char *fmt, ...);

/* -1 for all LDCs info, or ldc_id for a specific LDC info */
int vgendbg_ldcid = -1;

/* simulate handshake error conditions for debug */
uint32_t vgen_hdbg;
#define	HDBG_VERSION	0x1
#define	HDBG_TIMEOUT	0x2
#define	HDBG_BAD_SID	0x4
#define	HDBG_OUT_STATE	0x8

#endif

/*
 * vgen_init() is called by an instance of vnet driver to initialize the
 * corresponding generic proxy transport layer. The arguments passed by vnet
 * are - an opaque pointer to the vnet instance, pointers to dev_info_t and
 * the mac address of the vnet device, and a pointer to vgen_t is passed
 * back as a handle to vnet.
 */
int
vgen_init(void *vnetp, uint64_t regprop, dev_info_t *vnetdip,
    const uint8_t *macaddr, void **vgenhdl)
{
	vgen_t *vgenp;
	int instance;
	int rv;

	if ((vnetp == NULL) || (vnetdip == NULL))
		return (DDI_FAILURE);

	instance = ddi_get_instance(vnetdip);

	DBG1(NULL, NULL, "vnet(%d): enter\n", instance);

	vgenp = kmem_zalloc(sizeof (vgen_t), KM_SLEEP);

	vgenp->vnetp = vnetp;
	vgenp->instance = instance;
	vgenp->regprop = regprop;
	vgenp->vnetdip = vnetdip;
	bcopy(macaddr, &(vgenp->macaddr), ETHERADDRL);
	vgenp->phys_link_state = LINK_STATE_UNKNOWN;

	/* allocate multicast table */
	vgenp->mctab = kmem_zalloc(VGEN_INIT_MCTAB_SIZE *
	    sizeof (struct ether_addr), KM_SLEEP);
	vgenp->mccount = 0;
	vgenp->mcsize = VGEN_INIT_MCTAB_SIZE;

	mutex_init(&vgenp->lock, NULL, MUTEX_DRIVER, NULL);
	rw_init(&vgenp->vgenports.rwlock, NULL, RW_DRIVER, NULL);

	rv = vgen_read_mdprops(vgenp);
	if (rv != 0) {
		goto vgen_init_fail;
	}
	*vgenhdl = (void *)vgenp;

	DBG1(NULL, NULL, "vnet(%d): exit\n", instance);
	return (DDI_SUCCESS);

vgen_init_fail:
	rw_destroy(&vgenp->vgenports.rwlock);
	mutex_destroy(&vgenp->lock);
	kmem_free(vgenp->mctab, VGEN_INIT_MCTAB_SIZE *
	    sizeof (struct ether_addr));
	if (VGEN_PRI_ETH_DEFINED(vgenp)) {
		kmem_free(vgenp->pri_types,
		    sizeof (uint16_t) * vgenp->pri_num_types);
		(void) vio_destroy_mblks(vgenp->pri_tx_vmp);
	}
	KMEM_FREE(vgenp);
	return (DDI_FAILURE);
}

int
vgen_init_mdeg(void *arg)
{
	vgen_t	*vgenp = (vgen_t *)arg;

	/* register with MD event generator */
	return (vgen_mdeg_reg(vgenp));
}

/*
 * Called by vnet to undo the initializations done by vgen_init().
 * The handle provided by generic transport during vgen_init() is the argument.
 */
void
vgen_uninit(void *arg)
{
	vgen_t		*vgenp = (vgen_t *)arg;
	vio_mblk_pool_t	*rp;
	vio_mblk_pool_t	*nrp;

	if (vgenp == NULL) {
		return;
	}

	DBG1(vgenp, NULL, "enter\n");

	/* unregister with MD event generator */
	vgen_mdeg_unreg(vgenp);

	mutex_enter(&vgenp->lock);

	/* detach all ports from the device */
	vgen_detach_ports(vgenp);

	/*
	 * free any pending rx mblk pools,
	 * that couldn't be freed previously during channel detach.
	 */
	rp = vgenp->rmp;
	while (rp != NULL) {
		nrp = vgenp->rmp = rp->nextp;
		if (vio_destroy_mblks(rp)) {
			WRITE_ENTER(&vgen_rw);
			rp->nextp = vgen_rx_poolp;
			vgen_rx_poolp = rp;
			RW_EXIT(&vgen_rw);
		}
		rp = nrp;
	}

	/* free multicast table */
	kmem_free(vgenp->mctab, vgenp->mcsize * sizeof (struct ether_addr));

	/* free pri_types table */
	if (VGEN_PRI_ETH_DEFINED(vgenp)) {
		kmem_free(vgenp->pri_types,
		    sizeof (uint16_t) * vgenp->pri_num_types);
		(void) vio_destroy_mblks(vgenp->pri_tx_vmp);
	}

	mutex_exit(&vgenp->lock);

	rw_destroy(&vgenp->vgenports.rwlock);
	mutex_destroy(&vgenp->lock);

	DBG1(vgenp, NULL, "exit\n");
	KMEM_FREE(vgenp);
}

/*
 * module specific initialization common to all instances of vnet/vgen.
 */
void
vgen_mod_init(void)
{
	rw_init(&vgen_rw, NULL, RW_DRIVER, NULL);
}

/*
 * module specific cleanup common to all instances of vnet/vgen.
 */
int
vgen_mod_cleanup(void)
{
	vio_mblk_pool_t	*poolp, *npoolp;

	/*
	 * If any rx mblk pools are still in use, return
	 * error and stop the module from unloading.
	 */
	WRITE_ENTER(&vgen_rw);
	poolp = vgen_rx_poolp;
	while (poolp != NULL) {
		npoolp = vgen_rx_poolp = poolp->nextp;
		if (vio_destroy_mblks(poolp) != 0) {
			vgen_rx_poolp = poolp;
			RW_EXIT(&vgen_rw);
			return (EBUSY);
		}
		poolp = npoolp;
	}
	RW_EXIT(&vgen_rw);

	return (0);
}

/*
 * module specific uninitialization common to all instances of vnet/vgen.
 */
void
vgen_mod_fini(void)
{
	rw_destroy(&vgen_rw);
}

/* enable transmit/receive for the device */
int
vgen_start(void *arg)
{
	vgen_port_t	*portp = (vgen_port_t *)arg;
	vgen_t		*vgenp = portp->vgenp;

	DBG1(vgenp, NULL, "enter\n");
	mutex_enter(&portp->lock);
	vgen_port_init(portp);
	portp->flags |= VGEN_STARTED;
	mutex_exit(&portp->lock);
	DBG1(vgenp, NULL, "exit\n");

	return (DDI_SUCCESS);
}

/* stop transmit/receive */
void
vgen_stop(void *arg)
{
	vgen_port_t	*portp = (vgen_port_t *)arg;
	vgen_t		*vgenp = portp->vgenp;

	DBG1(vgenp, NULL, "enter\n");

	mutex_enter(&portp->lock);
	if (portp->flags & VGEN_STARTED) {
		vgen_port_uninit(portp);
		portp->flags &= ~(VGEN_STARTED);
	}
	mutex_exit(&portp->lock);
	DBG1(vgenp, NULL, "exit\n");

}

/* vgen transmit function */
static mblk_t *
vgen_tx(void *arg, mblk_t *mp)
{
	int i;
	vgen_port_t *portp;
	int status = VGEN_FAILURE;

	portp = (vgen_port_t *)arg;
	/*
	 * Retry so that we avoid reporting a failure
	 * to the upper layer. Returning a failure may cause the
	 * upper layer to go into single threaded mode there by
	 * causing performance degradation, especially for a large
	 * number of connections.
	 */
	for (i = 0; i < vgen_tx_retries; ) {
		status = vgen_portsend(portp, mp);
		if (status == VGEN_SUCCESS) {
			break;
		}
		if (++i < vgen_tx_retries)
			delay(drv_usectohz(vgen_tx_delay));
	}
	if (status != VGEN_SUCCESS) {
		/* failure */
		return (mp);
	}
	/* success */
	return (NULL);
}

/*
 * This function provides any necessary tagging/untagging of the frames
 * that are being transmitted over the port. It first verifies the vlan
 * membership of the destination(port) and drops the packet if the
 * destination doesn't belong to the given vlan.
 *
 * Arguments:
 *   portp:     port over which the frames should be transmitted
 *   mp:        frame to be transmitted
 *   is_tagged:
 *              B_TRUE: indicates frame header contains the vlan tag already.
 *              B_FALSE: indicates frame is untagged.
 *   vid:       vlan in which the frame should be transmitted.
 *
 * Returns:
 *              Sucess: frame(mblk_t *) after doing the necessary tag/untag.
 *              Failure: NULL
 */
static mblk_t *
vgen_vlan_frame_fixtag(vgen_port_t *portp, mblk_t *mp, boolean_t is_tagged,
	uint16_t vid)
{
	vgen_t				*vgenp;
	boolean_t			dst_tagged;
	int				rv;

	vgenp = portp->vgenp;

	/*
	 * If the packet is going to a vnet:
	 *   Check if the destination vnet is in the same vlan.
	 *   Check the frame header if tag or untag is needed.
	 *
	 * We do not check the above conditions if the packet is going to vsw:
	 *   vsw must be present implicitly in all the vlans that a vnet device
	 *   is configured into; even if vsw itself is not assigned to those
	 *   vlans as an interface. For instance, the packet might be destined
	 *   to another vnet(indirectly through vsw) or to an external host
	 *   which is in the same vlan as this vnet and vsw itself may not be
	 *   present in that vlan. Similarly packets going to vsw must be
	 *   always tagged(unless in the default-vlan) if not already tagged,
	 *   as we do not know the final destination. This is needed because
	 *   vsw must always invoke its switching function only after tagging
	 *   the packet; otherwise after switching function determines the
	 *   destination we cannot figure out if the destination belongs to the
	 *   the same vlan that the frame originated from and if it needs tag/
	 *   untag. Note that vsw will tag the packet itself when it receives
	 *   it over the channel from a client if needed. However, that is
	 *   needed only in the case of vlan unaware clients such as obp or
	 *   earlier versions of vnet.
	 *
	 */
	if (portp != vgenp->vsw_portp) {
		/*
		 * Packet going to a vnet. Check if the destination vnet is in
		 * the same vlan. Then check the frame header if tag/untag is
		 * needed.
		 */
		rv = vgen_vlan_lookup(portp->vlan_hashp, vid);
		if (rv == B_FALSE) {
			/* drop the packet */
			freemsg(mp);
			return (NULL);
		}

		/* is the destination tagged or untagged in this vlan? */
		(vid == portp->pvid) ? (dst_tagged = B_FALSE) :
		    (dst_tagged = B_TRUE);

		if (is_tagged == dst_tagged) {
			/* no tagging/untagging needed */
			return (mp);
		}

		if (is_tagged == B_TRUE) {
			/* frame is tagged; destination needs untagged */
			mp = vnet_vlan_remove_tag(mp);
			return (mp);
		}

		/* (is_tagged == B_FALSE): fallthru to tag tx packet: */
	}

	/*
	 * Packet going to a vnet needs tagging.
	 * OR
	 * If the packet is going to vsw, then it must be tagged in all cases:
	 * unknown unicast, broadcast/multicast or to vsw interface.
	 */

	if (is_tagged == B_FALSE) {
		mp = vnet_vlan_insert_tag(mp, vid);
	}

	return (mp);
}

/* transmit packets over the given port */
static int
vgen_portsend(vgen_port_t *portp, mblk_t *mp)
{
	vgen_ldclist_t		*ldclp;
	vgen_ldc_t		*ldcp;
	int			status;
	int			rv = VGEN_SUCCESS;
	vgen_t			*vgenp = portp->vgenp;
	vnet_t			*vnetp = vgenp->vnetp;
	boolean_t		is_tagged;
	boolean_t		dec_refcnt = B_FALSE;
	uint16_t		vlan_id;
	struct ether_header	*ehp;

	if (portp->use_vsw_port) {
		(void) atomic_inc_32(&vgenp->vsw_port_refcnt);
		portp = portp->vgenp->vsw_portp;
		dec_refcnt = B_TRUE;
	}
	if (portp == NULL) {
		return (VGEN_FAILURE);
	}

	/*
	 * Determine the vlan id that the frame belongs to.
	 */
	ehp = (struct ether_header *)mp->b_rptr;
	is_tagged = vgen_frame_lookup_vid(vnetp, ehp, &vlan_id);

	if (vlan_id == vnetp->default_vlan_id) {

		/* Frames in default vlan must be untagged */
		ASSERT(is_tagged == B_FALSE);

		/*
		 * If the destination is a vnet-port verify it belongs to the
		 * default vlan; otherwise drop the packet. We do not need
		 * this check for vsw-port, as it should implicitly belong to
		 * this vlan; see comments in vgen_vlan_frame_fixtag().
		 */
		if (portp != vgenp->vsw_portp &&
		    portp->pvid != vnetp->default_vlan_id) {
			freemsg(mp);
			goto portsend_ret;
		}

	} else {	/* frame not in default-vlan */

		mp = vgen_vlan_frame_fixtag(portp, mp, is_tagged, vlan_id);
		if (mp == NULL) {
			goto portsend_ret;
		}

	}

	ldclp = &portp->ldclist;
	READ_ENTER(&ldclp->rwlock);
	/*
	 * NOTE: for now, we will assume we have a single channel.
	 */
	if (ldclp->headp == NULL) {
		RW_EXIT(&ldclp->rwlock);
		rv = VGEN_FAILURE;
		goto portsend_ret;
	}
	ldcp = ldclp->headp;

	status = ldcp->tx(ldcp, mp);

	RW_EXIT(&ldclp->rwlock);

	if (status != VGEN_TX_SUCCESS) {
		rv = VGEN_FAILURE;
	}

portsend_ret:
	if (dec_refcnt == B_TRUE) {
		(void) atomic_dec_32(&vgenp->vsw_port_refcnt);
	}
	return (rv);
}

/*
 * Wrapper function to transmit normal and/or priority frames over the channel.
 */
static int
vgen_ldcsend(void *arg, mblk_t *mp)
{
	vgen_ldc_t		*ldcp = (vgen_ldc_t *)arg;
	int			status;
	struct ether_header	*ehp;
	vgen_t			*vgenp = LDC_TO_VGEN(ldcp);
	uint32_t		num_types;
	uint16_t		*types;
	int			i;

	ASSERT(VGEN_PRI_ETH_DEFINED(vgenp));

	num_types = vgenp->pri_num_types;
	types = vgenp->pri_types;
	ehp = (struct ether_header *)mp->b_rptr;

	for (i = 0; i < num_types; i++) {

		if (ehp->ether_type == types[i]) {
			/* priority frame, use pri tx function */
			vgen_ldcsend_pkt(ldcp, mp);
			return (VGEN_SUCCESS);
		}

	}

	status  = vgen_ldcsend_dring(ldcp, mp);

	return (status);
}

/*
 * This functions handles ldc channel reset while in the context
 * of transmit routines: vgen_ldcsend_pkt() or vgen_ldcsend_dring().
 */
static void
vgen_ldcsend_process_reset(vgen_ldc_t *ldcp)
{
	ldc_status_t	istatus;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	if (mutex_tryenter(&ldcp->cblock)) {
		if (ldc_status(ldcp->ldc_handle, &istatus) != 0) {
			DWARN(vgenp, ldcp, "ldc_status() error\n");
		} else {
			ldcp->ldc_status = istatus;
		}
		if (ldcp->ldc_status != LDC_UP) {
			vgen_handle_evt_reset(ldcp);
		}
		mutex_exit(&ldcp->cblock);
	}
}

/*
 * This function transmits the frame in the payload of a raw data
 * (VIO_PKT_DATA) message. Thus, it provides an Out-Of-Band path to
 * send special frames with high priorities, without going through
 * the normal data path which uses descriptor ring mechanism.
 */
static void
vgen_ldcsend_pkt(void *arg, mblk_t *mp)
{
	vgen_ldc_t		*ldcp = (vgen_ldc_t *)arg;
	vio_raw_data_msg_t	*pkt;
	mblk_t			*bp;
	mblk_t			*nmp = NULL;
	caddr_t			dst;
	uint32_t		mblksz;
	uint32_t		size;
	uint32_t		nbytes;
	int			rv;
	vgen_t			*vgenp = LDC_TO_VGEN(ldcp);
	vgen_stats_t		*statsp = &ldcp->stats;

	/* drop the packet if ldc is not up or handshake is not done */
	if (ldcp->ldc_status != LDC_UP) {
		(void) atomic_inc_32(&statsp->tx_pri_fail);
		DWARN(vgenp, ldcp, "status(%d), dropping packet\n",
		    ldcp->ldc_status);
		goto send_pkt_exit;
	}

	if (ldcp->hphase != VH_DONE) {
		(void) atomic_inc_32(&statsp->tx_pri_fail);
		DWARN(vgenp, ldcp, "hphase(%x), dropping packet\n",
		    ldcp->hphase);
		goto send_pkt_exit;
	}

	size = msgsize(mp);

	/* frame size bigger than available payload len of raw data msg ? */
	if (size > (size_t)(ldcp->msglen - VIO_PKT_DATA_HDRSIZE)) {
		(void) atomic_inc_32(&statsp->tx_pri_fail);
		DWARN(vgenp, ldcp, "invalid size(%d)\n", size);
		goto send_pkt_exit;
	}

	if (size < ETHERMIN)
		size = ETHERMIN;

	/* alloc space for a raw data message */
	nmp = vio_allocb(vgenp->pri_tx_vmp);
	if (nmp == NULL) {
		(void) atomic_inc_32(&statsp->tx_pri_fail);
		DWARN(vgenp, ldcp, "vio_allocb failed\n");
		goto send_pkt_exit;
	}
	pkt = (vio_raw_data_msg_t *)nmp->b_rptr;

	/* copy frame into the payload of raw data message */
	dst = (caddr_t)pkt->data;
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		mblksz = MBLKL(bp);
		bcopy(bp->b_rptr, dst, mblksz);
		dst += mblksz;
	}

	/* setup the raw data msg */
	pkt->tag.vio_msgtype = VIO_TYPE_DATA;
	pkt->tag.vio_subtype = VIO_SUBTYPE_INFO;
	pkt->tag.vio_subtype_env = VIO_PKT_DATA;
	pkt->tag.vio_sid = ldcp->local_sid;
	nbytes = VIO_PKT_DATA_HDRSIZE + size;

	/* send the msg over ldc */
	rv = vgen_sendmsg(ldcp, (caddr_t)pkt, nbytes, B_FALSE);
	if (rv != VGEN_SUCCESS) {
		(void) atomic_inc_32(&statsp->tx_pri_fail);
		DWARN(vgenp, ldcp, "Error sending priority frame\n");
		if (rv == ECONNRESET) {
			vgen_ldcsend_process_reset(ldcp);
		}
		goto send_pkt_exit;
	}

	/* update stats */
	(void) atomic_inc_64(&statsp->tx_pri_packets);
	(void) atomic_add_64(&statsp->tx_pri_bytes, size);

send_pkt_exit:
	if (nmp != NULL)
		freemsg(nmp);
	freemsg(mp);
}

/*
 * This function transmits normal (non-priority) data frames over
 * the channel. It queues the frame into the transmit descriptor ring
 * and sends a VIO_DRING_DATA message if needed, to wake up the
 * peer to (re)start processing.
 */
static int
vgen_ldcsend_dring(void *arg, mblk_t *mp)
{
	vgen_ldc_t		*ldcp = (vgen_ldc_t *)arg;
	vgen_private_desc_t	*tbufp;
	vgen_private_desc_t	*rtbufp;
	vnet_public_desc_t	*rtxdp;
	vgen_private_desc_t	*ntbufp;
	vnet_public_desc_t	*txdp;
	vio_dring_entry_hdr_t	*hdrp;
	vgen_stats_t		*statsp;
	struct ether_header	*ehp;
	boolean_t		is_bcast = B_FALSE;
	boolean_t		is_mcast = B_FALSE;
	size_t			mblksz;
	caddr_t			dst;
	mblk_t			*bp;
	size_t			size;
	int			rv = 0;
	vgen_t			*vgenp = LDC_TO_VGEN(ldcp);
	vgen_hparams_t		*lp = &ldcp->local_hparams;

	statsp = &ldcp->stats;
	size = msgsize(mp);

	DBG1(vgenp, ldcp, "enter\n");

	if (ldcp->ldc_status != LDC_UP) {
		DWARN(vgenp, ldcp, "status(%d), dropping packet\n",
		    ldcp->ldc_status);
		/* retry ldc_up() if needed */
#ifdef	VNET_IOC_DEBUG
		if (ldcp->flags & CHANNEL_STARTED && !ldcp->link_down_forced) {
#else
		if (ldcp->flags & CHANNEL_STARTED) {
#endif
			(void) ldc_up(ldcp->ldc_handle);
		}
		goto send_dring_exit;
	}

	/* drop the packet if ldc is not up or handshake is not done */
	if (ldcp->hphase != VH_DONE) {
		DWARN(vgenp, ldcp, "hphase(%x), dropping packet\n",
		    ldcp->hphase);
		goto send_dring_exit;
	}

	if (size > (size_t)lp->mtu) {
		DWARN(vgenp, ldcp, "invalid size(%d)\n", size);
		goto send_dring_exit;
	}
	if (size < ETHERMIN)
		size = ETHERMIN;

	ehp = (struct ether_header *)mp->b_rptr;
	is_bcast = IS_BROADCAST(ehp);
	is_mcast = IS_MULTICAST(ehp);

	mutex_enter(&ldcp->txlock);
	/*
	 * allocate a descriptor
	 */
	tbufp = ldcp->next_tbufp;
	ntbufp = NEXTTBUF(ldcp, tbufp);
	if (ntbufp == ldcp->cur_tbufp) { /* out of tbufs/txds */

		mutex_enter(&ldcp->tclock);
		/* Try reclaiming now */
		vgen_reclaim_dring(ldcp);
		ldcp->reclaim_lbolt = ddi_get_lbolt();

		if (ntbufp == ldcp->cur_tbufp) {
			/* Now we are really out of tbuf/txds */
			ldcp->need_resched = B_TRUE;
			mutex_exit(&ldcp->tclock);

			statsp->tx_no_desc++;
			mutex_exit(&ldcp->txlock);

			return (VGEN_TX_NORESOURCES);
		}
		mutex_exit(&ldcp->tclock);
	}
	/* update next available tbuf in the ring and update tx index */
	ldcp->next_tbufp = ntbufp;
	INCR_TXI(ldcp->next_txi, ldcp);

	/* Mark the buffer busy before releasing the lock */
	tbufp->flags = VGEN_PRIV_DESC_BUSY;
	mutex_exit(&ldcp->txlock);

	/* copy data into pre-allocated transmit buffer */
	dst = tbufp->datap + VNET_IPALIGN;
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		mblksz = MBLKL(bp);
		bcopy(bp->b_rptr, dst, mblksz);
		dst += mblksz;
	}

	tbufp->datalen = size;

	/* initialize the corresponding public descriptor (txd) */
	txdp = tbufp->descp;
	hdrp = &txdp->hdr;
	txdp->nbytes = size;
	txdp->ncookies = tbufp->ncookies;
	bcopy((tbufp->memcookie), (txdp->memcookie),
	    tbufp->ncookies * sizeof (ldc_mem_cookie_t));

	mutex_enter(&ldcp->wrlock);
	/*
	 * If the flags not set to BUSY, it implies that the clobber
	 * was done while we were copying the data. In such case,
	 * discard the packet and return.
	 */
	if (tbufp->flags != VGEN_PRIV_DESC_BUSY) {
		statsp->oerrors++;
		mutex_exit(&ldcp->wrlock);
		goto send_dring_exit;
	}
	hdrp->dstate = VIO_DESC_READY;

	/* update stats */
	statsp->opackets++;
	statsp->obytes += size;
	if (is_bcast)
		statsp->brdcstxmt++;
	else if (is_mcast)
		statsp->multixmt++;

	/* send dring datamsg to the peer */
	if (ldcp->resched_peer) {

		rtbufp = &ldcp->tbufp[ldcp->resched_peer_txi];
		rtxdp = rtbufp->descp;

		if (rtxdp->hdr.dstate == VIO_DESC_READY) {

			rv = vgen_send_dring_data(ldcp,
			    (uint32_t)ldcp->resched_peer_txi, -1);
			if (rv != 0) {
				/* error: drop the packet */
				DWARN(vgenp, ldcp, "vgen_send_dring_data "
				    "failed: rv(%d) len(%d)\n",
				    ldcp->ldc_id, rv, size);
				statsp->oerrors++;
			} else {
				ldcp->resched_peer = B_FALSE;
			}

		}

	}

	mutex_exit(&ldcp->wrlock);

send_dring_exit:
	if (rv == ECONNRESET) {
		vgen_ldcsend_process_reset(ldcp);
	}
	freemsg(mp);
	DBG1(vgenp, ldcp, "exit\n");
	return (VGEN_TX_SUCCESS);
}

/*
 * enable/disable a multicast address
 * note that the cblock of the ldc channel connected to the vsw is used for
 * synchronization of the mctab.
 */
int
vgen_multicst(void *arg, boolean_t add, const uint8_t *mca)
{
	vgen_t			*vgenp;
	vnet_mcast_msg_t	mcastmsg;
	vio_msg_tag_t		*tagp;
	vgen_port_t		*portp;
	vgen_ldc_t		*ldcp;
	vgen_ldclist_t		*ldclp;
	struct ether_addr	*addrp;
	int			rv = DDI_FAILURE;
	uint32_t		i;

	portp = (vgen_port_t *)arg;
	vgenp = portp->vgenp;

	if (portp->is_vsw_port != B_TRUE) {
		return (DDI_SUCCESS);
	}

	addrp = (struct ether_addr *)mca;
	tagp = &mcastmsg.tag;
	bzero(&mcastmsg, sizeof (mcastmsg));

	ldclp = &portp->ldclist;

	READ_ENTER(&ldclp->rwlock);

	ldcp = ldclp->headp;
	if (ldcp == NULL) {
		RW_EXIT(&ldclp->rwlock);
		return (DDI_FAILURE);
	}

	mutex_enter(&ldcp->cblock);

	if (ldcp->hphase == VH_DONE) {
		/*
		 * If handshake is done, send a msg to vsw to add/remove
		 * the multicast address. Otherwise, we just update this
		 * mcast address in our table and the table will be sync'd
		 * with vsw when handshake completes.
		 */
		tagp->vio_msgtype = VIO_TYPE_CTRL;
		tagp->vio_subtype = VIO_SUBTYPE_INFO;
		tagp->vio_subtype_env = VNET_MCAST_INFO;
		tagp->vio_sid = ldcp->local_sid;
		bcopy(mca, &(mcastmsg.mca), ETHERADDRL);
		mcastmsg.set = add;
		mcastmsg.count = 1;
		if (vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (mcastmsg),
		    B_FALSE) != VGEN_SUCCESS) {
			DWARN(vgenp, ldcp, "vgen_sendmsg failed\n");
			rv = DDI_FAILURE;
			goto vgen_mcast_exit;
		}
	}

	if (add) {

		/* expand multicast table if necessary */
		if (vgenp->mccount >= vgenp->mcsize) {
			struct ether_addr	*newtab;
			uint32_t		newsize;


			newsize = vgenp->mcsize * 2;

			newtab = kmem_zalloc(newsize *
			    sizeof (struct ether_addr), KM_NOSLEEP);
			if (newtab == NULL)
				goto vgen_mcast_exit;
			bcopy(vgenp->mctab, newtab, vgenp->mcsize *
			    sizeof (struct ether_addr));
			kmem_free(vgenp->mctab,
			    vgenp->mcsize * sizeof (struct ether_addr));

			vgenp->mctab = newtab;
			vgenp->mcsize = newsize;
		}

		/* add address to the table */
		vgenp->mctab[vgenp->mccount++] = *addrp;

	} else {

		/* delete address from the table */
		for (i = 0; i < vgenp->mccount; i++) {
			if (ether_cmp(addrp, &(vgenp->mctab[i])) == 0) {

				/*
				 * If there's more than one address in this
				 * table, delete the unwanted one by moving
				 * the last one in the list over top of it;
				 * otherwise, just remove it.
				 */
				if (vgenp->mccount > 1) {
					vgenp->mctab[i] =
					    vgenp->mctab[vgenp->mccount-1];
				}
				vgenp->mccount--;
				break;
			}
		}
	}

	rv = DDI_SUCCESS;

vgen_mcast_exit:
	mutex_exit(&ldcp->cblock);
	RW_EXIT(&ldclp->rwlock);

	return (rv);
}

/* set or clear promiscuous mode on the device */
static int
vgen_promisc(void *arg, boolean_t on)
{
	_NOTE(ARGUNUSED(arg, on))
	return (DDI_SUCCESS);
}

/* set the unicast mac address of the device */
static int
vgen_unicst(void *arg, const uint8_t *mca)
{
	_NOTE(ARGUNUSED(arg, mca))
	return (DDI_SUCCESS);
}

/* get device statistics */
int
vgen_stat(void *arg, uint_t stat, uint64_t *val)
{
	vgen_port_t	*portp = (vgen_port_t *)arg;

	*val = vgen_port_stat(portp, stat);

	return (0);
}

/* vgen internal functions */
/* detach all ports from the device */
static void
vgen_detach_ports(vgen_t *vgenp)
{
	vgen_port_t	*portp;
	vgen_portlist_t	*plistp;

	plistp = &(vgenp->vgenports);
	WRITE_ENTER(&plistp->rwlock);
	while ((portp = plistp->headp) != NULL) {
		vgen_port_detach(portp);
	}
	RW_EXIT(&plistp->rwlock);
}

/*
 * detach the given port.
 */
static void
vgen_port_detach(vgen_port_t *portp)
{
	vgen_t		*vgenp;
	vgen_ldclist_t	*ldclp;
	int		port_num;

	vgenp = portp->vgenp;
	port_num = portp->port_num;

	DBG1(vgenp, NULL, "port(%d):enter\n", port_num);

	/*
	 * If this port is connected to the vswitch, then
	 * potentially there could be ports that may be using
	 * this port to transmit packets. To address this do
	 * the following:
	 *	- First set vgenp->vsw_portp to NULL, so that
	 *	  its not used after that.
	 *	- Then wait for the refcnt to go down to 0.
	 *	- Now we can safely detach this port.
	 */
	if (vgenp->vsw_portp == portp) {
		vgenp->vsw_portp = NULL;
		while (vgenp->vsw_port_refcnt > 0) {
			delay(drv_usectohz(vgen_tx_delay));
		}
		(void) atomic_swap_32(&vgenp->vsw_port_refcnt, 0);
	}

	if (portp->vhp != NULL) {
		vio_net_resource_unreg(portp->vhp);
		portp->vhp = NULL;
	}

	vgen_vlan_destroy_hash(portp);

	/* remove it from port list */
	vgen_port_list_remove(portp);

	/* detach channels from this port */
	ldclp = &portp->ldclist;
	WRITE_ENTER(&ldclp->rwlock);
	while (ldclp->headp) {
		vgen_ldc_detach(ldclp->headp);
	}
	RW_EXIT(&ldclp->rwlock);
	rw_destroy(&ldclp->rwlock);

	if (portp->num_ldcs != 0) {
		kmem_free(portp->ldc_ids, portp->num_ldcs * sizeof (uint64_t));
		portp->num_ldcs = 0;
	}

	mutex_destroy(&portp->lock);
	KMEM_FREE(portp);

	DBG1(vgenp, NULL, "port(%d):exit\n", port_num);
}

/* add a port to port list */
static void
vgen_port_list_insert(vgen_port_t *portp)
{
	vgen_portlist_t *plistp;
	vgen_t *vgenp;

	vgenp = portp->vgenp;
	plistp = &(vgenp->vgenports);

	if (plistp->headp == NULL) {
		plistp->headp = portp;
	} else {
		plistp->tailp->nextp = portp;
	}
	plistp->tailp = portp;
	portp->nextp = NULL;
}

/* remove a port from port list */
static void
vgen_port_list_remove(vgen_port_t *portp)
{
	vgen_port_t *prevp;
	vgen_port_t *nextp;
	vgen_portlist_t *plistp;
	vgen_t *vgenp;

	vgenp = portp->vgenp;

	plistp = &(vgenp->vgenports);

	if (plistp->headp == NULL)
		return;

	if (portp == plistp->headp) {
		plistp->headp = portp->nextp;
		if (portp == plistp->tailp)
			plistp->tailp = plistp->headp;
	} else {
		for (prevp = plistp->headp;
		    ((nextp = prevp->nextp) != NULL) && (nextp != portp);
		    prevp = nextp)
			;
		if (nextp == portp) {
			prevp->nextp = portp->nextp;
		}
		if (portp == plistp->tailp)
			plistp->tailp = prevp;
	}
}

/* lookup a port in the list based on port_num */
static vgen_port_t *
vgen_port_lookup(vgen_portlist_t *plistp, int port_num)
{
	vgen_port_t *portp = NULL;

	for (portp = plistp->headp; portp != NULL; portp = portp->nextp) {
		if (portp->port_num == port_num) {
			break;
		}
	}

	return (portp);
}

/* enable ports for transmit/receive */
static void
vgen_init_ports(vgen_t *vgenp)
{
	vgen_port_t	*portp;
	vgen_portlist_t	*plistp;

	plistp = &(vgenp->vgenports);
	READ_ENTER(&plistp->rwlock);

	for (portp = plistp->headp; portp != NULL; portp = portp->nextp) {
		vgen_port_init(portp);
	}

	RW_EXIT(&plistp->rwlock);
}

static void
vgen_port_init(vgen_port_t *portp)
{
	/* Add the port to the specified vlans */
	vgen_vlan_add_ids(portp);

	/* Bring up the channels of this port */
	vgen_init_ldcs(portp);
}

/* disable transmit/receive on ports */
static void
vgen_uninit_ports(vgen_t *vgenp)
{
	vgen_port_t	*portp;
	vgen_portlist_t	*plistp;

	plistp = &(vgenp->vgenports);
	READ_ENTER(&plistp->rwlock);

	for (portp = plistp->headp; portp != NULL; portp = portp->nextp) {
		vgen_port_uninit(portp);
	}

	RW_EXIT(&plistp->rwlock);
}

static void
vgen_port_uninit(vgen_port_t *portp)
{
	vgen_uninit_ldcs(portp);

	/* remove the port from vlans it has been assigned to */
	vgen_vlan_remove_ids(portp);
}

/*
 * Scan the machine description for this instance of vnet
 * and read its properties. Called only from vgen_init().
 * Returns: 0 on success, 1 on failure.
 */
static int
vgen_read_mdprops(vgen_t *vgenp)
{
	vnet_t		*vnetp = vgenp->vnetp;
	md_t		*mdp = NULL;
	mde_cookie_t	rootnode;
	mde_cookie_t	*listp = NULL;
	uint64_t	cfgh;
	char		*name;
	int		rv = 1;
	int		num_nodes = 0;
	int		num_devs = 0;
	int		listsz = 0;
	int		i;

	if ((mdp = md_get_handle()) == NULL) {
		return (rv);
	}

	num_nodes = md_node_count(mdp);
	ASSERT(num_nodes > 0);

	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = (mde_cookie_t *)kmem_zalloc(listsz, KM_SLEEP);

	rootnode = md_root_node(mdp);

	/* search for all "virtual_device" nodes */
	num_devs = md_scan_dag(mdp, rootnode,
	    md_find_name(mdp, vdev_propname),
	    md_find_name(mdp, "fwd"), listp);
	if (num_devs <= 0) {
		goto vgen_readmd_exit;
	}

	/*
	 * Now loop through the list of virtual-devices looking for
	 * devices with name "network" and for each such device compare
	 * its instance with what we have from the 'reg' property to
	 * find the right node in MD and then read all its properties.
	 */
	for (i = 0; i < num_devs; i++) {

		if (md_get_prop_str(mdp, listp[i], "name", &name) != 0) {
			goto vgen_readmd_exit;
		}

		/* is this a "network" device? */
		if (strcmp(name, vnet_propname) != 0)
			continue;

		if (md_get_prop_val(mdp, listp[i], "cfg-handle", &cfgh) != 0) {
			goto vgen_readmd_exit;
		}

		/* is this the required instance of vnet? */
		if (vgenp->regprop != cfgh)
			continue;

		/*
		 * Read the 'linkprop' property to know if this vnet
		 * device should get physical link updates from vswitch.
		 */
		vgen_linkprop_read(vgenp, mdp, listp[i],
		    &vnetp->pls_update);

		/*
		 * Read the mtu. Note that we set the mtu of vnet device within
		 * this routine itself, after validating the range.
		 */
		vgen_mtu_read(vgenp, mdp, listp[i], &vnetp->mtu);
		if (vnetp->mtu < ETHERMTU || vnetp->mtu > VNET_MAX_MTU) {
			vnetp->mtu = ETHERMTU;
		}
		vgenp->max_frame_size = vnetp->mtu +
		    sizeof (struct ether_header) + VLAN_TAGSZ;

		/* read priority ether types */
		vgen_read_pri_eth_types(vgenp, mdp, listp[i]);

		/* read vlan id properties of this vnet instance */
		vgen_vlan_read_ids(vgenp, VGEN_LOCAL, mdp, listp[i],
		    &vnetp->pvid, &vnetp->vids, &vnetp->nvids,
		    &vnetp->default_vlan_id);

		rv = 0;
		break;
	}

vgen_readmd_exit:

	kmem_free(listp, listsz);
	(void) md_fini_handle(mdp);
	return (rv);
}

/*
 * Read vlan id properties of the given MD node.
 * Arguments:
 *   arg:          device argument(vnet device or a port)
 *   type:         type of arg; VGEN_LOCAL(vnet device) or VGEN_PEER(port)
 *   mdp:          machine description
 *   node:         md node cookie
 *
 * Returns:
 *   pvidp:        port-vlan-id of the node
 *   vidspp:       list of vlan-ids of the node
 *   nvidsp:       # of vlan-ids in the list
 *   default_idp:  default-vlan-id of the node(if node is vnet device)
 */
static void
vgen_vlan_read_ids(void *arg, int type, md_t *mdp, mde_cookie_t node,
	uint16_t *pvidp, uint16_t **vidspp, uint16_t *nvidsp,
	uint16_t *default_idp)
{
	vgen_t		*vgenp;
	vnet_t		*vnetp;
	vgen_port_t	*portp;
	char		*pvid_propname;
	char		*vid_propname;
	uint_t		nvids;
	uint32_t	vids_size;
	int		rv;
	int		i;
	uint64_t	*data;
	uint64_t	val;
	int		size;
	int		inst;

	if (type == VGEN_LOCAL) {

		vgenp = (vgen_t *)arg;
		vnetp = vgenp->vnetp;
		pvid_propname = vgen_pvid_propname;
		vid_propname = vgen_vid_propname;
		inst = vnetp->instance;

	} else if (type == VGEN_PEER) {

		portp = (vgen_port_t *)arg;
		vgenp = portp->vgenp;
		vnetp = vgenp->vnetp;
		pvid_propname = port_pvid_propname;
		vid_propname = port_vid_propname;
		inst = portp->port_num;

	} else {
		return;
	}

	if (type == VGEN_LOCAL && default_idp != NULL) {
		rv = md_get_prop_val(mdp, node, vgen_dvid_propname, &val);
		if (rv != 0) {
			DWARN(vgenp, NULL, "prop(%s) not found",
			    vgen_dvid_propname);

			*default_idp = vnet_default_vlan_id;
		} else {
			*default_idp = val & 0xFFF;
			DBG2(vgenp, NULL, "%s(%d): (%d)\n", vgen_dvid_propname,
			    inst, *default_idp);
		}
	}

	rv = md_get_prop_val(mdp, node, pvid_propname, &val);
	if (rv != 0) {
		DWARN(vgenp, NULL, "prop(%s) not found", pvid_propname);
		*pvidp = vnet_default_vlan_id;
	} else {

		*pvidp = val & 0xFFF;
		DBG2(vgenp, NULL, "%s(%d): (%d)\n",
		    pvid_propname, inst, *pvidp);
	}

	rv = md_get_prop_data(mdp, node, vid_propname, (uint8_t **)&data,
	    &size);
	if (rv != 0) {
		DBG2(vgenp, NULL, "prop(%s) not found", vid_propname);
		size = 0;
	} else {
		size /= sizeof (uint64_t);
	}
	nvids = size;

	if (nvids != 0) {
		DBG2(vgenp, NULL, "%s(%d): ", vid_propname, inst);
		vids_size = sizeof (uint16_t) * nvids;
		*vidspp = kmem_zalloc(vids_size, KM_SLEEP);
		for (i = 0; i < nvids; i++) {
			(*vidspp)[i] = data[i] & 0xFFFF;
			DBG2(vgenp, NULL, " %d ", (*vidspp)[i]);
		}
		DBG2(vgenp, NULL, "\n");
	}

	*nvidsp = nvids;
}

/*
 * Create a vlan id hash table for the given port.
 */
static void
vgen_vlan_create_hash(vgen_port_t *portp)
{
	char		hashname[MAXNAMELEN];

	(void) snprintf(hashname, MAXNAMELEN, "port%d-vlan-hash",
	    portp->port_num);

	portp->vlan_nchains = vgen_vlan_nchains;
	portp->vlan_hashp = mod_hash_create_idhash(hashname,
	    portp->vlan_nchains, mod_hash_null_valdtor);
}

/*
 * Destroy the vlan id hash table in the given port.
 */
static void
vgen_vlan_destroy_hash(vgen_port_t *portp)
{
	if (portp->vlan_hashp != NULL) {
		mod_hash_destroy_hash(portp->vlan_hashp);
		portp->vlan_hashp = NULL;
		portp->vlan_nchains = 0;
	}
}

/*
 * Add a port to the vlans specified in its port properites.
 */
static void
vgen_vlan_add_ids(vgen_port_t *portp)
{
	int		rv;
	int		i;

	rv = mod_hash_insert(portp->vlan_hashp,
	    (mod_hash_key_t)VLAN_ID_KEY(portp->pvid),
	    (mod_hash_val_t)B_TRUE);
	ASSERT(rv == 0);

	for (i = 0; i < portp->nvids; i++) {
		rv = mod_hash_insert(portp->vlan_hashp,
		    (mod_hash_key_t)VLAN_ID_KEY(portp->vids[i]),
		    (mod_hash_val_t)B_TRUE);
		ASSERT(rv == 0);
	}
}

/*
 * Remove a port from the vlans it has been assigned to.
 */
static void
vgen_vlan_remove_ids(vgen_port_t *portp)
{
	int		rv;
	int		i;
	mod_hash_val_t	vp;

	rv = mod_hash_remove(portp->vlan_hashp,
	    (mod_hash_key_t)VLAN_ID_KEY(portp->pvid),
	    (mod_hash_val_t *)&vp);
	ASSERT(rv == 0);

	for (i = 0; i < portp->nvids; i++) {
		rv = mod_hash_remove(portp->vlan_hashp,
		    (mod_hash_key_t)VLAN_ID_KEY(portp->vids[i]),
		    (mod_hash_val_t *)&vp);
		ASSERT(rv == 0);
	}
}

/*
 * Lookup the vlan id of the given tx frame. If it is a vlan-tagged frame,
 * then the vlan-id is available in the tag; otherwise, its vlan id is
 * implicitly obtained from the port-vlan-id of the vnet device.
 * The vlan id determined is returned in vidp.
 * Returns: B_TRUE if it is a tagged frame; B_FALSE if it is untagged.
 */
static boolean_t
vgen_frame_lookup_vid(vnet_t *vnetp, struct ether_header *ehp, uint16_t *vidp)
{
	struct ether_vlan_header	*evhp;

	/* If it's a tagged frame, get the vlan id from vlan header */
	if (ehp->ether_type == ETHERTYPE_VLAN) {

		evhp = (struct ether_vlan_header *)ehp;
		*vidp = VLAN_ID(ntohs(evhp->ether_tci));
		return (B_TRUE);
	}

	/* Untagged frame, vlan-id is the pvid of vnet device */
	*vidp = vnetp->pvid;
	return (B_FALSE);
}

/*
 * Find the given vlan id in the hash table.
 * Return: B_TRUE if the id is found; B_FALSE if not found.
 */
static boolean_t
vgen_vlan_lookup(mod_hash_t *vlan_hashp, uint16_t vid)
{
	int		rv;
	mod_hash_val_t	vp;

	rv = mod_hash_find(vlan_hashp, VLAN_ID_KEY(vid), (mod_hash_val_t *)&vp);

	if (rv != 0)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * This function reads "priority-ether-types" property from md. This property
 * is used to enable support for priority frames. Applications which need
 * guaranteed and timely delivery of certain high priority frames to/from
 * a vnet or vsw within ldoms, should configure this property by providing
 * the ether type(s) for which the priority facility is needed.
 * Normal data frames are delivered over a ldc channel using the descriptor
 * ring mechanism which is constrained by factors such as descriptor ring size,
 * the rate at which the ring is processed at the peer ldc end point, etc.
 * The priority mechanism provides an Out-Of-Band path to send/receive frames
 * as raw pkt data (VIO_PKT_DATA) messages over the channel, avoiding the
 * descriptor ring path and enables a more reliable and timely delivery of
 * frames to the peer.
 */
static void
vgen_read_pri_eth_types(vgen_t *vgenp, md_t *mdp, mde_cookie_t node)
{
	int		rv;
	uint16_t	*types;
	uint64_t	*data;
	int		size;
	int		i;
	size_t		mblk_sz;

	rv = md_get_prop_data(mdp, node, pri_types_propname,
	    (uint8_t **)&data, &size);
	if (rv != 0) {
		/*
		 * Property may not exist if we are running pre-ldoms1.1 f/w.
		 * Check if 'vgen_pri_eth_type' has been set in that case.
		 */
		if (vgen_pri_eth_type != 0) {
			size = sizeof (vgen_pri_eth_type);
			data = &vgen_pri_eth_type;
		} else {
			DBG2(vgenp, NULL,
			    "prop(%s) not found", pri_types_propname);
			size = 0;
		}
	}

	if (size == 0) {
		vgenp->pri_num_types = 0;
		return;
	}

	/*
	 * we have some priority-ether-types defined;
	 * allocate a table of these types and also
	 * allocate a pool of mblks to transmit these
	 * priority packets.
	 */
	size /= sizeof (uint64_t);
	vgenp->pri_num_types = size;
	vgenp->pri_types = kmem_zalloc(size * sizeof (uint16_t), KM_SLEEP);
	for (i = 0, types = vgenp->pri_types; i < size; i++) {
		types[i] = data[i] & 0xFFFF;
	}
	mblk_sz = (VIO_PKT_DATA_HDRSIZE + vgenp->max_frame_size + 7) & ~7;
	(void) vio_create_mblks(vgen_pri_tx_nmblks, mblk_sz,
	    &vgenp->pri_tx_vmp);
}

static void
vgen_mtu_read(vgen_t *vgenp, md_t *mdp, mde_cookie_t node, uint32_t *mtu)
{
	int		rv;
	uint64_t	val;
	char		*mtu_propname;

	mtu_propname = vgen_mtu_propname;

	rv = md_get_prop_val(mdp, node, mtu_propname, &val);
	if (rv != 0) {
		DWARN(vgenp, NULL, "prop(%s) not found", mtu_propname);
		*mtu = vnet_ethermtu;
	} else {

		*mtu = val & 0xFFFF;
		DBG2(vgenp, NULL, "%s(%d): (%d)\n", mtu_propname,
		    vgenp->instance, *mtu);
	}
}

static void
vgen_linkprop_read(vgen_t *vgenp, md_t *mdp, mde_cookie_t node,
	boolean_t *pls)
{
	int		rv;
	uint64_t	val;
	char		*linkpropname;

	linkpropname = vgen_linkprop_propname;

	rv = md_get_prop_val(mdp, node, linkpropname, &val);
	if (rv != 0) {
		DWARN(vgenp, NULL, "prop(%s) not found", linkpropname);
		*pls = B_FALSE;
	} else {

		*pls = (val & 0x1) ?  B_TRUE : B_FALSE;
		DBG2(vgenp, NULL, "%s(%d): (%d)\n", linkpropname,
		    vgenp->instance, *pls);
	}
}

/* register with MD event generator */
static int
vgen_mdeg_reg(vgen_t *vgenp)
{
	mdeg_prop_spec_t	*pspecp;
	mdeg_node_spec_t	*parentp;
	uint_t			templatesz;
	int			rv;
	mdeg_handle_t		dev_hdl = NULL;
	mdeg_handle_t		port_hdl = NULL;

	templatesz = sizeof (vgen_prop_template);
	pspecp = kmem_zalloc(templatesz, KM_NOSLEEP);
	if (pspecp == NULL) {
		return (DDI_FAILURE);
	}
	parentp = kmem_zalloc(sizeof (mdeg_node_spec_t), KM_NOSLEEP);
	if (parentp == NULL) {
		kmem_free(pspecp, templatesz);
		return (DDI_FAILURE);
	}

	bcopy(vgen_prop_template, pspecp, templatesz);

	/*
	 * NOTE: The instance here refers to the value of "reg" property and
	 * not the dev_info instance (ddi_get_instance()) of vnet.
	 */
	VGEN_SET_MDEG_PROP_INST(pspecp, vgenp->regprop);

	parentp->namep = "virtual-device";
	parentp->specp = pspecp;

	/* save parentp in vgen_t */
	vgenp->mdeg_parentp = parentp;

	/*
	 * Register an interest in 'virtual-device' nodes with a
	 * 'name' property of 'network'
	 */
	rv = mdeg_register(parentp, &vdev_match, vgen_mdeg_cb, vgenp, &dev_hdl);
	if (rv != MDEG_SUCCESS) {
		DERR(vgenp, NULL, "mdeg_register failed\n");
		goto mdeg_reg_fail;
	}

	/* Register an interest in 'port' nodes */
	rv = mdeg_register(parentp, &vport_match, vgen_mdeg_port_cb, vgenp,
	    &port_hdl);
	if (rv != MDEG_SUCCESS) {
		DERR(vgenp, NULL, "mdeg_register failed\n");
		goto mdeg_reg_fail;
	}

	/* save mdeg handle in vgen_t */
	vgenp->mdeg_dev_hdl = dev_hdl;
	vgenp->mdeg_port_hdl = port_hdl;

	return (DDI_SUCCESS);

mdeg_reg_fail:
	if (dev_hdl != NULL) {
		(void) mdeg_unregister(dev_hdl);
	}
	KMEM_FREE(parentp);
	kmem_free(pspecp, templatesz);
	vgenp->mdeg_parentp = NULL;
	return (DDI_FAILURE);
}

/* unregister with MD event generator */
static void
vgen_mdeg_unreg(vgen_t *vgenp)
{
	if (vgenp->mdeg_dev_hdl != NULL) {
		(void) mdeg_unregister(vgenp->mdeg_dev_hdl);
		vgenp->mdeg_dev_hdl = NULL;
	}
	if (vgenp->mdeg_port_hdl != NULL) {
		(void) mdeg_unregister(vgenp->mdeg_port_hdl);
		vgenp->mdeg_port_hdl = NULL;
	}

	if (vgenp->mdeg_parentp != NULL) {
		kmem_free(vgenp->mdeg_parentp->specp,
		    sizeof (vgen_prop_template));
		KMEM_FREE(vgenp->mdeg_parentp);
		vgenp->mdeg_parentp = NULL;
	}
}

/* mdeg callback function for the port node */
static int
vgen_mdeg_port_cb(void *cb_argp, mdeg_result_t *resp)
{
	int idx;
	int vsw_idx = -1;
	uint64_t val;
	vgen_t *vgenp;

	if ((resp == NULL) || (cb_argp == NULL)) {
		return (MDEG_FAILURE);
	}

	vgenp = (vgen_t *)cb_argp;
	DBG1(vgenp, NULL, "enter\n");

	mutex_enter(&vgenp->lock);

	DBG1(vgenp, NULL, "ports: removed(%x), "
	"added(%x), updated(%x)\n", resp->removed.nelem,
	    resp->added.nelem, resp->match_curr.nelem);

	for (idx = 0; idx < resp->removed.nelem; idx++) {
		(void) vgen_remove_port(vgenp, resp->removed.mdp,
		    resp->removed.mdep[idx]);
	}

	if (vgenp->vsw_portp == NULL) {
		/*
		 * find vsw_port and add it first, because other ports need
		 * this when adding fdb entry (see vgen_port_init()).
		 */
		for (idx = 0; idx < resp->added.nelem; idx++) {
			if (!(md_get_prop_val(resp->added.mdp,
			    resp->added.mdep[idx], swport_propname, &val))) {
				if (val == 0) {
					/*
					 * This port is connected to the
					 * vsw on service domain.
					 */
					vsw_idx = idx;
					if (vgen_add_port(vgenp,
					    resp->added.mdp,
					    resp->added.mdep[idx]) !=
					    DDI_SUCCESS) {
						cmn_err(CE_NOTE, "vnet%d Could "
						    "not initialize virtual "
						    "switch port.",
						    vgenp->instance);
						mutex_exit(&vgenp->lock);
						return (MDEG_FAILURE);
					}
					break;
				}
			}
		}
		if (vsw_idx == -1) {
			DWARN(vgenp, NULL, "can't find vsw_port\n");
			mutex_exit(&vgenp->lock);
			return (MDEG_FAILURE);
		}
	}

	for (idx = 0; idx < resp->added.nelem; idx++) {
		if ((vsw_idx != -1) && (vsw_idx == idx)) /* skip vsw_port */
			continue;

		/* If this port can't be added just skip it. */
		(void) vgen_add_port(vgenp, resp->added.mdp,
		    resp->added.mdep[idx]);
	}

	for (idx = 0; idx < resp->match_curr.nelem; idx++) {
		(void) vgen_update_port(vgenp, resp->match_curr.mdp,
		    resp->match_curr.mdep[idx],
		    resp->match_prev.mdp,
		    resp->match_prev.mdep[idx]);
	}

	mutex_exit(&vgenp->lock);
	DBG1(vgenp, NULL, "exit\n");
	return (MDEG_SUCCESS);
}

/* mdeg callback function for the vnet node */
static int
vgen_mdeg_cb(void *cb_argp, mdeg_result_t *resp)
{
	vgen_t		*vgenp;
	vnet_t		*vnetp;
	md_t		*mdp;
	mde_cookie_t	node;
	uint64_t	inst;
	char		*node_name = NULL;

	if ((resp == NULL) || (cb_argp == NULL)) {
		return (MDEG_FAILURE);
	}

	vgenp = (vgen_t *)cb_argp;
	vnetp = vgenp->vnetp;

	DBG1(vgenp, NULL, "added %d : removed %d : curr matched %d"
	    " : prev matched %d", resp->added.nelem, resp->removed.nelem,
	    resp->match_curr.nelem, resp->match_prev.nelem);

	mutex_enter(&vgenp->lock);

	/*
	 * We get an initial callback for this node as 'added' after
	 * registering with mdeg. Note that we would have already gathered
	 * information about this vnet node by walking MD earlier during attach
	 * (in vgen_read_mdprops()). So, there is a window where the properties
	 * of this node might have changed when we get this initial 'added'
	 * callback. We handle this as if an update occured and invoke the same
	 * function which handles updates to the properties of this vnet-node
	 * if any. A non-zero 'match' value indicates that the MD has been
	 * updated and that a 'network' node is present which may or may not
	 * have been updated. It is up to the clients to examine their own
	 * nodes and determine if they have changed.
	 */
	if (resp->added.nelem != 0) {

		if (resp->added.nelem != 1) {
			cmn_err(CE_NOTE, "!vnet%d: number of nodes added "
			    "invalid: %d\n", vnetp->instance,
			    resp->added.nelem);
			goto vgen_mdeg_cb_err;
		}

		mdp = resp->added.mdp;
		node = resp->added.mdep[0];

	} else if (resp->match_curr.nelem != 0) {

		if (resp->match_curr.nelem != 1) {
			cmn_err(CE_NOTE, "!vnet%d: number of nodes updated "
			    "invalid: %d\n", vnetp->instance,
			    resp->match_curr.nelem);
			goto vgen_mdeg_cb_err;
		}

		mdp = resp->match_curr.mdp;
		node = resp->match_curr.mdep[0];

	} else {
		goto vgen_mdeg_cb_err;
	}

	/* Validate name and instance */
	if (md_get_prop_str(mdp, node, "name", &node_name) != 0) {
		DERR(vgenp, NULL, "unable to get node name\n");
		goto vgen_mdeg_cb_err;
	}

	/* is this a virtual-network device? */
	if (strcmp(node_name, vnet_propname) != 0) {
		DERR(vgenp, NULL, "%s: Invalid node name: %s\n", node_name);
		goto vgen_mdeg_cb_err;
	}

	if (md_get_prop_val(mdp, node, "cfg-handle", &inst)) {
		DERR(vgenp, NULL, "prop(cfg-handle) not found\n");
		goto vgen_mdeg_cb_err;
	}

	/* is this the right instance of vnet? */
	if (inst != vgenp->regprop) {
		DERR(vgenp, NULL,  "Invalid cfg-handle: %lx\n", inst);
		goto vgen_mdeg_cb_err;
	}

	vgen_update_md_prop(vgenp, mdp, node);

	mutex_exit(&vgenp->lock);
	return (MDEG_SUCCESS);

vgen_mdeg_cb_err:
	mutex_exit(&vgenp->lock);
	return (MDEG_FAILURE);
}

/*
 * Check to see if the relevant properties in the specified node have
 * changed, and if so take the appropriate action.
 */
static void
vgen_update_md_prop(vgen_t *vgenp, md_t *mdp, mde_cookie_t mdex)
{
	uint16_t	pvid;
	uint16_t	*vids;
	uint16_t	nvids;
	vnet_t		*vnetp = vgenp->vnetp;
	uint32_t	mtu;
	boolean_t	pls_update;
	enum		{ MD_init = 0x1,
			    MD_vlans = 0x2,
			    MD_mtu = 0x4,
			    MD_pls = 0x8 } updated;
	int		rv;

	updated = MD_init;

	/* Read the vlan ids */
	vgen_vlan_read_ids(vgenp, VGEN_LOCAL, mdp, mdex, &pvid, &vids,
	    &nvids, NULL);

	/* Determine if there are any vlan id updates */
	if ((pvid != vnetp->pvid) ||		/* pvid changed? */
	    (nvids != vnetp->nvids) ||		/* # of vids changed? */
	    ((nvids != 0) && (vnetp->nvids != 0) &&	/* vids changed? */
	    bcmp(vids, vnetp->vids, sizeof (uint16_t) * nvids))) {
		updated |= MD_vlans;
	}

	/* Read mtu */
	vgen_mtu_read(vgenp, mdp, mdex, &mtu);
	if (mtu != vnetp->mtu) {
		if (mtu >= ETHERMTU && mtu <= VNET_MAX_MTU) {
			updated |= MD_mtu;
		} else {
			cmn_err(CE_NOTE, "!vnet%d: Unable to process mtu update"
			    " as the specified value:%d is invalid\n",
			    vnetp->instance, mtu);
		}
	}

	/*
	 * Read the 'linkprop' property.
	 */
	vgen_linkprop_read(vgenp, mdp, mdex, &pls_update);
	if (pls_update != vnetp->pls_update) {
		updated |= MD_pls;
	}

	/* Now process the updated props */

	if (updated & MD_vlans) {

		/* save the new vlan ids */
		vnetp->pvid = pvid;
		if (vnetp->nvids != 0) {
			kmem_free(vnetp->vids,
			    sizeof (uint16_t) * vnetp->nvids);
			vnetp->nvids = 0;
		}
		if (nvids != 0) {
			vnetp->nvids = nvids;
			vnetp->vids = vids;
		}

		/* reset vlan-unaware peers (ver < 1.3) and restart handshake */
		vgen_reset_vlan_unaware_ports(vgenp);

	} else {

		if (nvids != 0) {
			kmem_free(vids, sizeof (uint16_t) * nvids);
		}
	}

	if (updated & MD_mtu) {

		DBG2(vgenp, NULL, "curr_mtu(%d) new_mtu(%d)\n",
		    vnetp->mtu, mtu);

		rv = vnet_mtu_update(vnetp, mtu);
		if (rv == 0) {
			vgenp->max_frame_size = mtu +
			    sizeof (struct ether_header) + VLAN_TAGSZ;
		}
	}

	if (updated & MD_pls) {
		/* enable/disable physical link state updates */
		vnetp->pls_update = pls_update;
		mutex_exit(&vgenp->lock);

		/* reset vsw-port to re-negotiate with the updated prop. */
		vgen_reset_vsw_port(vgenp);

		mutex_enter(&vgenp->lock);
	}
}

/* add a new port to the device */
static int
vgen_add_port(vgen_t *vgenp, md_t *mdp, mde_cookie_t mdex)
{
	vgen_port_t	*portp;
	int		rv;

	portp = kmem_zalloc(sizeof (vgen_port_t), KM_SLEEP);

	rv = vgen_port_read_props(portp, vgenp, mdp, mdex);
	if (rv != DDI_SUCCESS) {
		KMEM_FREE(portp);
		return (DDI_FAILURE);
	}

	rv = vgen_port_attach(portp);
	if (rv != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/* read properties of the port from its md node */
static int
vgen_port_read_props(vgen_port_t *portp, vgen_t *vgenp, md_t *mdp,
	mde_cookie_t mdex)
{
	uint64_t		port_num;
	uint64_t		*ldc_ids;
	uint64_t		macaddr;
	uint64_t		val;
	int			num_ldcs;
	int			i;
	int			addrsz;
	int			num_nodes = 0;
	int			listsz = 0;
	mde_cookie_t		*listp = NULL;
	uint8_t			*addrp;
	struct ether_addr	ea;

	/* read "id" property to get the port number */
	if (md_get_prop_val(mdp, mdex, id_propname, &port_num)) {
		DWARN(vgenp, NULL, "prop(%s) not found\n", id_propname);
		return (DDI_FAILURE);
	}

	/*
	 * Find the channel endpoint node(s) under this port node.
	 */
	if ((num_nodes = md_node_count(mdp)) <= 0) {
		DWARN(vgenp, NULL, "invalid number of nodes found (%d)",
		    num_nodes);
		return (DDI_FAILURE);
	}

	/* allocate space for node list */
	listsz = num_nodes * sizeof (mde_cookie_t);
	listp = kmem_zalloc(listsz, KM_NOSLEEP);
	if (listp == NULL)
		return (DDI_FAILURE);

	num_ldcs = md_scan_dag(mdp, mdex,
	    md_find_name(mdp, channel_propname),
	    md_find_name(mdp, "fwd"), listp);

	if (num_ldcs <= 0) {
		DWARN(vgenp, NULL, "can't find %s nodes", channel_propname);
		kmem_free(listp, listsz);
		return (DDI_FAILURE);
	}

	DBG2(vgenp, NULL, "num_ldcs %d", num_ldcs);

	ldc_ids = kmem_zalloc(num_ldcs * sizeof (uint64_t), KM_NOSLEEP);
	if (ldc_ids == NULL) {
		kmem_free(listp, listsz);
		return (DDI_FAILURE);
	}

	for (i = 0; i < num_ldcs; i++) {
		/* read channel ids */
		if (md_get_prop_val(mdp, listp[i], id_propname, &ldc_ids[i])) {
			DWARN(vgenp, NULL, "prop(%s) not found\n",
			    id_propname);
			kmem_free(listp, listsz);
			kmem_free(ldc_ids, num_ldcs * sizeof (uint64_t));
			return (DDI_FAILURE);
		}
		DBG2(vgenp, NULL, "ldc_id 0x%llx", ldc_ids[i]);
	}

	kmem_free(listp, listsz);

	if (md_get_prop_data(mdp, mdex, rmacaddr_propname, &addrp,
	    &addrsz)) {
		DWARN(vgenp, NULL, "prop(%s) not found\n", rmacaddr_propname);
		kmem_free(ldc_ids, num_ldcs * sizeof (uint64_t));
		return (DDI_FAILURE);
	}

	if (addrsz < ETHERADDRL) {
		DWARN(vgenp, NULL, "invalid address size (%d)\n", addrsz);
		kmem_free(ldc_ids, num_ldcs * sizeof (uint64_t));
		return (DDI_FAILURE);
	}

	macaddr = *((uint64_t *)addrp);

	DBG2(vgenp, NULL, "remote mac address 0x%llx\n", macaddr);

	for (i = ETHERADDRL - 1; i >= 0; i--) {
		ea.ether_addr_octet[i] = macaddr & 0xFF;
		macaddr >>= 8;
	}

	if (!(md_get_prop_val(mdp, mdex, swport_propname, &val))) {
		if (val == 0) {
			/* This port is connected to the vswitch */
			portp->is_vsw_port = B_TRUE;
		} else {
			portp->is_vsw_port = B_FALSE;
		}
	}

	/* now update all properties into the port */
	portp->vgenp = vgenp;
	portp->port_num = port_num;
	ether_copy(&ea, &portp->macaddr);
	portp->ldc_ids = kmem_zalloc(sizeof (uint64_t) * num_ldcs, KM_SLEEP);
	bcopy(ldc_ids, portp->ldc_ids, sizeof (uint64_t) * num_ldcs);
	portp->num_ldcs = num_ldcs;

	/* read vlan id properties of this port node */
	vgen_vlan_read_ids(portp, VGEN_PEER, mdp, mdex, &portp->pvid,
	    &portp->vids, &portp->nvids, NULL);

	kmem_free(ldc_ids, num_ldcs * sizeof (uint64_t));

	return (DDI_SUCCESS);
}

/* remove a port from the device */
static int
vgen_remove_port(vgen_t *vgenp, md_t *mdp, mde_cookie_t mdex)
{
	uint64_t	port_num;
	vgen_port_t	*portp;
	vgen_portlist_t	*plistp;

	/* read "id" property to get the port number */
	if (md_get_prop_val(mdp, mdex, id_propname, &port_num)) {
		DWARN(vgenp, NULL, "prop(%s) not found\n", id_propname);
		return (DDI_FAILURE);
	}

	plistp = &(vgenp->vgenports);

	WRITE_ENTER(&plistp->rwlock);
	portp = vgen_port_lookup(plistp, (int)port_num);
	if (portp == NULL) {
		DWARN(vgenp, NULL, "can't find port(%lx)\n", port_num);
		RW_EXIT(&plistp->rwlock);
		return (DDI_FAILURE);
	}

	vgen_port_detach_mdeg(portp);
	RW_EXIT(&plistp->rwlock);

	return (DDI_SUCCESS);
}

/* attach a port to the device based on mdeg data */
static int
vgen_port_attach(vgen_port_t *portp)
{
	int			i;
	vgen_portlist_t		*plistp;
	vgen_t			*vgenp;
	uint64_t		*ldcids;
	uint32_t		num_ldcs;
	mac_register_t		*macp;
	vio_net_res_type_t	type;
	int			rv;

	ASSERT(portp != NULL);

	vgenp = portp->vgenp;
	ldcids = portp->ldc_ids;
	num_ldcs = portp->num_ldcs;

	DBG1(vgenp, NULL, "port_num(%d)\n", portp->port_num);

	mutex_init(&portp->lock, NULL, MUTEX_DRIVER, NULL);
	rw_init(&portp->ldclist.rwlock, NULL, RW_DRIVER, NULL);
	portp->ldclist.headp = NULL;

	for (i = 0; i < num_ldcs; i++) {
		DBG2(vgenp, NULL, "ldcid (%lx)\n", ldcids[i]);
		if (vgen_ldc_attach(portp, ldcids[i]) == DDI_FAILURE) {
			vgen_port_detach(portp);
			return (DDI_FAILURE);
		}
	}

	/* create vlan id hash table */
	vgen_vlan_create_hash(portp);

	if (portp->is_vsw_port == B_TRUE) {
		/* This port is connected to the switch port */
		(void) atomic_swap_32(&portp->use_vsw_port, B_FALSE);
		type = VIO_NET_RES_LDC_SERVICE;
	} else {
		(void) atomic_swap_32(&portp->use_vsw_port, B_TRUE);
		type = VIO_NET_RES_LDC_GUEST;
	}

	if ((macp = mac_alloc(MAC_VERSION)) == NULL) {
		vgen_port_detach(portp);
		return (DDI_FAILURE);
	}
	macp->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macp->m_driver = portp;
	macp->m_dip = vgenp->vnetdip;
	macp->m_src_addr = (uint8_t *)&(vgenp->macaddr);
	macp->m_callbacks = &vgen_m_callbacks;
	macp->m_min_sdu = 0;
	macp->m_max_sdu = ETHERMTU;

	mutex_enter(&portp->lock);
	rv = vio_net_resource_reg(macp, type, vgenp->macaddr,
	    portp->macaddr, &portp->vhp, &portp->vcb);
	mutex_exit(&portp->lock);
	mac_free(macp);

	if (rv == 0) {
		/* link it into the list of ports */
		plistp = &(vgenp->vgenports);
		WRITE_ENTER(&plistp->rwlock);
		vgen_port_list_insert(portp);
		RW_EXIT(&plistp->rwlock);

		if (portp->is_vsw_port == B_TRUE) {
			/* We now have the vswitch port attached */
			vgenp->vsw_portp = portp;
			(void) atomic_swap_32(&vgenp->vsw_port_refcnt, 0);
		}
	} else {
		DERR(vgenp, NULL, "vio_net_resource_reg failed for portp=0x%p",
		    portp);
		vgen_port_detach(portp);
	}

	DBG1(vgenp, NULL, "exit: port_num(%d)\n", portp->port_num);
	return (DDI_SUCCESS);
}

/* detach a port from the device based on mdeg data */
static void
vgen_port_detach_mdeg(vgen_port_t *portp)
{
	vgen_t *vgenp = portp->vgenp;

	DBG1(vgenp, NULL, "enter: port_num(%d)\n", portp->port_num);

	mutex_enter(&portp->lock);

	/* stop the port if needed */
	if (portp->flags & VGEN_STARTED) {
		vgen_port_uninit(portp);
		portp->flags &= ~(VGEN_STARTED);
	}

	mutex_exit(&portp->lock);
	vgen_port_detach(portp);

	DBG1(vgenp, NULL, "exit: port_num(%d)\n", portp->port_num);
}

static int
vgen_update_port(vgen_t *vgenp, md_t *curr_mdp, mde_cookie_t curr_mdex,
	md_t *prev_mdp, mde_cookie_t prev_mdex)
{
	uint64_t	cport_num;
	uint64_t	pport_num;
	vgen_portlist_t	*plistp;
	vgen_port_t	*portp;
	boolean_t	updated_vlans = B_FALSE;
	uint16_t	pvid;
	uint16_t	*vids;
	uint16_t	nvids;

	/*
	 * For now, we get port updates only if vlan ids changed.
	 * We read the port num and do some sanity check.
	 */
	if (md_get_prop_val(curr_mdp, curr_mdex, id_propname, &cport_num)) {
		DWARN(vgenp, NULL, "prop(%s) not found\n", id_propname);
		return (DDI_FAILURE);
	}

	if (md_get_prop_val(prev_mdp, prev_mdex, id_propname, &pport_num)) {
		DWARN(vgenp, NULL, "prop(%s) not found\n", id_propname);
		return (DDI_FAILURE);
	}
	if (cport_num != pport_num)
		return (DDI_FAILURE);

	plistp = &(vgenp->vgenports);

	READ_ENTER(&plistp->rwlock);

	portp = vgen_port_lookup(plistp, (int)cport_num);
	if (portp == NULL) {
		DWARN(vgenp, NULL, "can't find port(%lx)\n", cport_num);
		RW_EXIT(&plistp->rwlock);
		return (DDI_FAILURE);
	}

	/* Read the vlan ids */
	vgen_vlan_read_ids(portp, VGEN_PEER, curr_mdp, curr_mdex, &pvid, &vids,
	    &nvids, NULL);

	/* Determine if there are any vlan id updates */
	if ((pvid != portp->pvid) ||		/* pvid changed? */
	    (nvids != portp->nvids) ||		/* # of vids changed? */
	    ((nvids != 0) && (portp->nvids != 0) &&	/* vids changed? */
	    bcmp(vids, portp->vids, sizeof (uint16_t) * nvids))) {
		updated_vlans = B_TRUE;
	}

	if (updated_vlans == B_FALSE) {
		RW_EXIT(&plistp->rwlock);
		return (DDI_FAILURE);
	}

	/* remove the port from vlans it has been assigned to */
	vgen_vlan_remove_ids(portp);

	/* save the new vlan ids */
	portp->pvid = pvid;
	if (portp->nvids != 0) {
		kmem_free(portp->vids, sizeof (uint16_t) * portp->nvids);
		portp->nvids = 0;
	}
	if (nvids != 0) {
		portp->vids = kmem_zalloc(sizeof (uint16_t) * nvids, KM_SLEEP);
		bcopy(vids, portp->vids, sizeof (uint16_t) * nvids);
		portp->nvids = nvids;
		kmem_free(vids, sizeof (uint16_t) * nvids);
	}

	/* add port to the new vlans */
	vgen_vlan_add_ids(portp);

	/* reset the port if it is vlan unaware (ver < 1.3) */
	vgen_vlan_unaware_port_reset(portp);

	RW_EXIT(&plistp->rwlock);

	return (DDI_SUCCESS);
}

static uint64_t
vgen_port_stat(vgen_port_t *portp, uint_t stat)
{
	vgen_ldclist_t	*ldclp;
	vgen_ldc_t *ldcp;
	uint64_t	val;

	val = 0;
	ldclp = &portp->ldclist;

	READ_ENTER(&ldclp->rwlock);
	for (ldcp = ldclp->headp; ldcp != NULL; ldcp = ldcp->nextp) {
		val += vgen_ldc_stat(ldcp, stat);
	}
	RW_EXIT(&ldclp->rwlock);

	return (val);
}

/* allocate receive resources */
static int
vgen_init_multipools(vgen_ldc_t *ldcp)
{
	size_t		data_sz;
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	int		status;
	uint32_t	sz1 = 0;
	uint32_t	sz2 = 0;
	uint32_t	sz3 = 0;
	uint32_t	sz4 = 0;

	/*
	 * We round up the mtu specified to be a multiple of 2K.
	 * We then create rx pools based on the rounded up size.
	 */
	data_sz = vgenp->max_frame_size + VNET_IPALIGN + VNET_LDCALIGN;
	data_sz = VNET_ROUNDUP_2K(data_sz);

	/*
	 * If pool sizes are specified, use them. Note that the presence of
	 * the first tunable will be used as a hint.
	 */
	if (vgen_rbufsz1 != 0) {

		sz1 = vgen_rbufsz1;
		sz2 = vgen_rbufsz2;
		sz3 = vgen_rbufsz3;
		sz4 = vgen_rbufsz4;

		if (sz4 == 0) { /* need 3 pools */

			ldcp->max_rxpool_size = sz3;
			status = vio_init_multipools(&ldcp->vmp,
			    VGEN_NUM_VMPOOLS, sz1, sz2, sz3, vgen_nrbufs1,
			    vgen_nrbufs2, vgen_nrbufs3);

		} else {

			ldcp->max_rxpool_size = sz4;
			status = vio_init_multipools(&ldcp->vmp,
			    VGEN_NUM_VMPOOLS + 1, sz1, sz2, sz3, sz4,
			    vgen_nrbufs1, vgen_nrbufs2, vgen_nrbufs3,
			    vgen_nrbufs4);
		}
		return (status);
	}

	/*
	 * Pool sizes are not specified. We select the pool sizes based on the
	 * mtu if vnet_jumbo_rxpools is enabled.
	 */
	if (vnet_jumbo_rxpools == B_FALSE || data_sz == VNET_2K) {
		/*
		 * Receive buffer pool allocation based on mtu is disabled.
		 * Use the default mechanism of standard size pool allocation.
		 */
		sz1 = VGEN_DBLK_SZ_128;
		sz2 = VGEN_DBLK_SZ_256;
		sz3 = VGEN_DBLK_SZ_2048;
		ldcp->max_rxpool_size = sz3;

		status = vio_init_multipools(&ldcp->vmp, VGEN_NUM_VMPOOLS,
		    sz1, sz2, sz3,
		    vgen_nrbufs1, vgen_nrbufs2, vgen_nrbufs3);

		return (status);
	}

	switch (data_sz) {

	case VNET_4K:

		sz1 = VGEN_DBLK_SZ_128;
		sz2 = VGEN_DBLK_SZ_256;
		sz3 = VGEN_DBLK_SZ_2048;
		sz4 = sz3 << 1;			/* 4K */
		ldcp->max_rxpool_size = sz4;

		status = vio_init_multipools(&ldcp->vmp, VGEN_NUM_VMPOOLS + 1,
		    sz1, sz2, sz3, sz4,
		    vgen_nrbufs1, vgen_nrbufs2, vgen_nrbufs3, vgen_nrbufs4);
		break;

	default:	/* data_sz:  4K+ to 16K */

		sz1 = VGEN_DBLK_SZ_256;
		sz2 = VGEN_DBLK_SZ_2048;
		sz3 = data_sz >> 1;	/* Jumbo-size/2 */
		sz4 = data_sz;		/* Jumbo-size  */
		ldcp->max_rxpool_size = sz4;

		status = vio_init_multipools(&ldcp->vmp, VGEN_NUM_VMPOOLS + 1,
		    sz1, sz2, sz3, sz4,
		    vgen_nrbufs1, vgen_nrbufs2, vgen_nrbufs3, vgen_nrbufs4);
		break;

	}

	return (status);
}

/* attach the channel corresponding to the given ldc_id to the port */
static int
vgen_ldc_attach(vgen_port_t *portp, uint64_t ldc_id)
{
	vgen_t 		*vgenp;
	vgen_ldclist_t	*ldclp;
	vgen_ldc_t 	*ldcp, **prev_ldcp;
	ldc_attr_t 	attr;
	int 		status;
	ldc_status_t	istatus;
	char		kname[MAXNAMELEN];
	int		instance;
	enum	{AST_init = 0x0, AST_ldc_alloc = 0x1,
		AST_mutex_init = 0x2, AST_ldc_init = 0x4,
		AST_ldc_reg_cb = 0x8, AST_alloc_tx_ring = 0x10,
		AST_create_rxmblks = 0x20,
		AST_create_rcv_thread = 0x40} attach_state;

	attach_state = AST_init;
	vgenp = portp->vgenp;
	ldclp = &portp->ldclist;

	ldcp = kmem_zalloc(sizeof (vgen_ldc_t), KM_NOSLEEP);
	if (ldcp == NULL) {
		goto ldc_attach_failed;
	}
	ldcp->ldc_id = ldc_id;
	ldcp->portp = portp;

	attach_state |= AST_ldc_alloc;

	mutex_init(&ldcp->txlock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&ldcp->cblock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&ldcp->tclock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&ldcp->wrlock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&ldcp->rxlock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&ldcp->pollq_lock, NULL, MUTEX_DRIVER, NULL);

	attach_state |= AST_mutex_init;

	attr.devclass = LDC_DEV_NT;
	attr.instance = vgenp->instance;
	attr.mode = LDC_MODE_UNRELIABLE;
	attr.mtu = vnet_ldc_mtu;
	status = ldc_init(ldc_id, &attr, &ldcp->ldc_handle);
	if (status != 0) {
		DWARN(vgenp, ldcp, "ldc_init failed,rv (%d)\n", status);
		goto ldc_attach_failed;
	}
	attach_state |= AST_ldc_init;

	if (vgen_rcv_thread_enabled) {
		ldcp->rcv_thr_flags = 0;

		mutex_init(&ldcp->rcv_thr_lock, NULL, MUTEX_DRIVER, NULL);
		cv_init(&ldcp->rcv_thr_cv, NULL, CV_DRIVER, NULL);
		ldcp->rcv_thread = thread_create(NULL, 2 * DEFAULTSTKSZ,
		    vgen_ldc_rcv_worker, ldcp, 0, &p0, TS_RUN, maxclsyspri);

		attach_state |= AST_create_rcv_thread;
		if (ldcp->rcv_thread == NULL) {
			DWARN(vgenp, ldcp, "Failed to create worker thread");
			goto ldc_attach_failed;
		}
	}

	status = ldc_reg_callback(ldcp->ldc_handle, vgen_ldc_cb, (caddr_t)ldcp);
	if (status != 0) {
		DWARN(vgenp, ldcp, "ldc_reg_callback failed, rv (%d)\n",
		    status);
		goto ldc_attach_failed;
	}
	/*
	 * allocate a message for ldc_read()s, big enough to hold ctrl and
	 * data msgs, including raw data msgs used to recv priority frames.
	 */
	ldcp->msglen = VIO_PKT_DATA_HDRSIZE + vgenp->max_frame_size;
	ldcp->ldcmsg = kmem_alloc(ldcp->msglen, KM_SLEEP);
	attach_state |= AST_ldc_reg_cb;

	(void) ldc_status(ldcp->ldc_handle, &istatus);
	ASSERT(istatus == LDC_INIT);
	ldcp->ldc_status = istatus;

	/* allocate transmit resources */
	status = vgen_alloc_tx_ring(ldcp);
	if (status != 0) {
		goto ldc_attach_failed;
	}
	attach_state |= AST_alloc_tx_ring;

	/* allocate receive resources */
	status = vgen_init_multipools(ldcp);
	if (status != 0) {
		/*
		 * We do not return failure if receive mblk pools can't be
		 * allocated; instead allocb(9F) will be used to dynamically
		 * allocate buffers during receive.
		 */
		DWARN(vgenp, ldcp,
		    "vnet%d: status(%d), failed to allocate rx mblk pools for "
		    "channel(0x%lx)\n",
		    vgenp->instance, status, ldcp->ldc_id);
	} else {
		attach_state |= AST_create_rxmblks;
	}

	/* Setup kstats for the channel */
	instance = vgenp->instance;
	(void) sprintf(kname, "vnetldc0x%lx", ldcp->ldc_id);
	ldcp->ksp = vgen_setup_kstats("vnet", instance, kname, &ldcp->stats);
	if (ldcp->ksp == NULL) {
		goto ldc_attach_failed;
	}

	/* initialize vgen_versions supported */
	bcopy(vgen_versions, ldcp->vgen_versions, sizeof (ldcp->vgen_versions));
	vgen_reset_vnet_proto_ops(ldcp);

	/* link it into the list of channels for this port */
	WRITE_ENTER(&ldclp->rwlock);
	prev_ldcp = (vgen_ldc_t **)(&ldclp->headp);
	ldcp->nextp = *prev_ldcp;
	*prev_ldcp = ldcp;
	RW_EXIT(&ldclp->rwlock);

	ldcp->link_state = LINK_STATE_UNKNOWN;
#ifdef	VNET_IOC_DEBUG
	ldcp->link_down_forced = B_FALSE;
#endif
	ldcp->flags |= CHANNEL_ATTACHED;
	return (DDI_SUCCESS);

ldc_attach_failed:
	if (attach_state & AST_ldc_reg_cb) {
		(void) ldc_unreg_callback(ldcp->ldc_handle);
		kmem_free(ldcp->ldcmsg, ldcp->msglen);
	}
	if (attach_state & AST_create_rcv_thread) {
		if (ldcp->rcv_thread != NULL) {
			vgen_stop_rcv_thread(ldcp);
		}
		mutex_destroy(&ldcp->rcv_thr_lock);
		cv_destroy(&ldcp->rcv_thr_cv);
	}
	if (attach_state & AST_create_rxmblks) {
		vio_mblk_pool_t *fvmp = NULL;
		vio_destroy_multipools(&ldcp->vmp, &fvmp);
		ASSERT(fvmp == NULL);
	}
	if (attach_state & AST_alloc_tx_ring) {
		vgen_free_tx_ring(ldcp);
	}
	if (attach_state & AST_ldc_init) {
		(void) ldc_fini(ldcp->ldc_handle);
	}
	if (attach_state & AST_mutex_init) {
		mutex_destroy(&ldcp->tclock);
		mutex_destroy(&ldcp->txlock);
		mutex_destroy(&ldcp->cblock);
		mutex_destroy(&ldcp->wrlock);
		mutex_destroy(&ldcp->rxlock);
		mutex_destroy(&ldcp->pollq_lock);
	}
	if (attach_state & AST_ldc_alloc) {
		KMEM_FREE(ldcp);
	}
	return (DDI_FAILURE);
}

/* detach a channel from the port */
static void
vgen_ldc_detach(vgen_ldc_t *ldcp)
{
	vgen_port_t	*portp;
	vgen_t 		*vgenp;
	vgen_ldc_t 	*pldcp;
	vgen_ldc_t	**prev_ldcp;
	vgen_ldclist_t	*ldclp;

	portp = ldcp->portp;
	vgenp = portp->vgenp;
	ldclp = &portp->ldclist;

	prev_ldcp =  (vgen_ldc_t **)&ldclp->headp;
	for (; (pldcp = *prev_ldcp) != NULL; prev_ldcp = &pldcp->nextp) {
		if (pldcp == ldcp) {
			break;
		}
	}

	if (pldcp == NULL) {
		/* invalid ldcp? */
		return;
	}

	if (ldcp->ldc_status != LDC_INIT) {
		DWARN(vgenp, ldcp, "ldc_status is not INIT\n");
	}

	if (ldcp->flags & CHANNEL_ATTACHED) {
		ldcp->flags &= ~(CHANNEL_ATTACHED);

		(void) ldc_unreg_callback(ldcp->ldc_handle);
		if (ldcp->rcv_thread != NULL) {
			/* First stop the receive thread */
			vgen_stop_rcv_thread(ldcp);
			mutex_destroy(&ldcp->rcv_thr_lock);
			cv_destroy(&ldcp->rcv_thr_cv);
		}
		kmem_free(ldcp->ldcmsg, ldcp->msglen);

		vgen_destroy_kstats(ldcp->ksp);
		ldcp->ksp = NULL;

		/*
		 * if we cannot reclaim all mblks, put this
		 * on the list of pools(vgenp->rmp) to be reclaimed when the
		 * device gets detached (see vgen_uninit()).
		 */
		vio_destroy_multipools(&ldcp->vmp, &vgenp->rmp);

		/* free transmit resources */
		vgen_free_tx_ring(ldcp);

		(void) ldc_fini(ldcp->ldc_handle);
		mutex_destroy(&ldcp->tclock);
		mutex_destroy(&ldcp->txlock);
		mutex_destroy(&ldcp->cblock);
		mutex_destroy(&ldcp->wrlock);
		mutex_destroy(&ldcp->rxlock);
		mutex_destroy(&ldcp->pollq_lock);

		/* unlink it from the list */
		*prev_ldcp = ldcp->nextp;
		KMEM_FREE(ldcp);
	}
}

/*
 * This function allocates transmit resources for the channel.
 * The resources consist of a transmit descriptor ring and an associated
 * transmit buffer ring.
 */
static int
vgen_alloc_tx_ring(vgen_ldc_t *ldcp)
{
	void *tbufp;
	ldc_mem_info_t minfo;
	uint32_t txdsize;
	uint32_t tbufsize;
	int status;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	ldcp->num_txds = vnet_ntxds;
	txdsize = sizeof (vnet_public_desc_t);
	tbufsize = sizeof (vgen_private_desc_t);

	/* allocate transmit buffer ring */
	tbufp = kmem_zalloc(ldcp->num_txds * tbufsize, KM_NOSLEEP);
	if (tbufp == NULL) {
		return (DDI_FAILURE);
	}

	/* create transmit descriptor ring */
	status = ldc_mem_dring_create(ldcp->num_txds, txdsize,
	    &ldcp->tx_dhandle);
	if (status) {
		DWARN(vgenp, ldcp, "ldc_mem_dring_create() failed\n");
		kmem_free(tbufp, ldcp->num_txds * tbufsize);
		return (DDI_FAILURE);
	}

	/* get the addr of descripror ring */
	status = ldc_mem_dring_info(ldcp->tx_dhandle, &minfo);
	if (status) {
		DWARN(vgenp, ldcp, "ldc_mem_dring_info() failed\n");
		kmem_free(tbufp, ldcp->num_txds * tbufsize);
		(void) ldc_mem_dring_destroy(ldcp->tx_dhandle);
		ldcp->tbufp = NULL;
		return (DDI_FAILURE);
	}
	ldcp->txdp = (vnet_public_desc_t *)(minfo.vaddr);
	ldcp->tbufp = tbufp;

	ldcp->txdendp = &((ldcp->txdp)[ldcp->num_txds]);
	ldcp->tbufendp = &((ldcp->tbufp)[ldcp->num_txds]);

	return (DDI_SUCCESS);
}

/* Free transmit resources for the channel */
static void
vgen_free_tx_ring(vgen_ldc_t *ldcp)
{
	int tbufsize = sizeof (vgen_private_desc_t);

	/* free transmit descriptor ring */
	(void) ldc_mem_dring_destroy(ldcp->tx_dhandle);

	/* free transmit buffer ring */
	kmem_free(ldcp->tbufp, ldcp->num_txds * tbufsize);
	ldcp->txdp = ldcp->txdendp = NULL;
	ldcp->tbufp = ldcp->tbufendp = NULL;
}

/* enable transmit/receive on the channels for the port */
static void
vgen_init_ldcs(vgen_port_t *portp)
{
	vgen_ldclist_t	*ldclp = &portp->ldclist;
	vgen_ldc_t	*ldcp;

	READ_ENTER(&ldclp->rwlock);
	ldcp =  ldclp->headp;
	for (; ldcp  != NULL; ldcp = ldcp->nextp) {
		(void) vgen_ldc_init(ldcp);
	}
	RW_EXIT(&ldclp->rwlock);
}

/* stop transmit/receive on the channels for the port */
static void
vgen_uninit_ldcs(vgen_port_t *portp)
{
	vgen_ldclist_t	*ldclp = &portp->ldclist;
	vgen_ldc_t	*ldcp;

	READ_ENTER(&ldclp->rwlock);
	ldcp =  ldclp->headp;
	for (; ldcp  != NULL; ldcp = ldcp->nextp) {
		vgen_ldc_uninit(ldcp);
	}
	RW_EXIT(&ldclp->rwlock);
}

/* enable transmit/receive on the channel */
static int
vgen_ldc_init(vgen_ldc_t *ldcp)
{
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	ldc_status_t	istatus;
	int		rv;
	uint32_t	retries = 0;
	enum	{ ST_init = 0x0, ST_ldc_open = 0x1,
		ST_init_tbufs = 0x2, ST_cb_enable = 0x4} init_state;
	init_state = ST_init;

	DBG1(vgenp, ldcp, "enter\n");
	LDC_LOCK(ldcp);

	rv = ldc_open(ldcp->ldc_handle);
	if (rv != 0) {
		DWARN(vgenp, ldcp, "ldc_open failed: rv(%d)\n", rv);
		goto ldcinit_failed;
	}
	init_state |= ST_ldc_open;

	(void) ldc_status(ldcp->ldc_handle, &istatus);
	if (istatus != LDC_OPEN && istatus != LDC_READY) {
		DWARN(vgenp, ldcp, "status(%d) is not OPEN/READY\n", istatus);
		goto ldcinit_failed;
	}
	ldcp->ldc_status = istatus;

	rv = vgen_init_tbufs(ldcp);
	if (rv != 0) {
		DWARN(vgenp, ldcp, "vgen_init_tbufs() failed\n");
		goto ldcinit_failed;
	}
	init_state |= ST_init_tbufs;

	rv = ldc_set_cb_mode(ldcp->ldc_handle, LDC_CB_ENABLE);
	if (rv != 0) {
		DWARN(vgenp, ldcp, "ldc_set_cb_mode failed: rv(%d)\n", rv);
		goto ldcinit_failed;
	}

	init_state |= ST_cb_enable;

	do {
		rv = ldc_up(ldcp->ldc_handle);
		if ((rv != 0) && (rv == EWOULDBLOCK)) {
			DBG2(vgenp, ldcp, "ldc_up err rv(%d)\n", rv);
			drv_usecwait(VGEN_LDC_UP_DELAY);
		}
		if (retries++ >= vgen_ldcup_retries)
			break;
	} while (rv == EWOULDBLOCK);

	(void) ldc_status(ldcp->ldc_handle, &istatus);
	if (istatus == LDC_UP) {
		DWARN(vgenp, ldcp, "status(%d) is UP\n", istatus);
	}

	ldcp->ldc_status = istatus;

	/* initialize transmit watchdog timeout */
	ldcp->wd_tid = timeout(vgen_ldc_watchdog, (caddr_t)ldcp,
	    drv_usectohz(vnet_ldcwd_interval * 1000));

	ldcp->hphase = -1;
	ldcp->flags |= CHANNEL_STARTED;

	/* if channel is already UP - start handshake */
	if (istatus == LDC_UP) {
		vgen_t *vgenp = LDC_TO_VGEN(ldcp);
		if (ldcp->portp != vgenp->vsw_portp) {
			/*
			 * As the channel is up, use this port from now on.
			 */
			(void) atomic_swap_32(
			    &ldcp->portp->use_vsw_port, B_FALSE);
		}

		/* Initialize local session id */
		ldcp->local_sid = ddi_get_lbolt();

		/* clear peer session id */
		ldcp->peer_sid = 0;
		ldcp->hretries = 0;

		/* Initiate Handshake process with peer ldc endpoint */
		vgen_reset_hphase(ldcp);

		mutex_exit(&ldcp->tclock);
		mutex_exit(&ldcp->txlock);
		mutex_exit(&ldcp->wrlock);
		mutex_exit(&ldcp->rxlock);
		vgen_handshake(vh_nextphase(ldcp));
		mutex_exit(&ldcp->cblock);
	} else {
		LDC_UNLOCK(ldcp);
	}

	return (DDI_SUCCESS);

ldcinit_failed:
	if (init_state & ST_cb_enable) {
		(void) ldc_set_cb_mode(ldcp->ldc_handle, LDC_CB_DISABLE);
	}
	if (init_state & ST_init_tbufs) {
		vgen_uninit_tbufs(ldcp);
	}
	if (init_state & ST_ldc_open) {
		(void) ldc_close(ldcp->ldc_handle);
	}
	LDC_UNLOCK(ldcp);
	DBG1(vgenp, ldcp, "exit\n");
	return (DDI_FAILURE);
}

/* stop transmit/receive on the channel */
static void
vgen_ldc_uninit(vgen_ldc_t *ldcp)
{
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	int	rv;
	uint_t	retries = 0;

	DBG1(vgenp, ldcp, "enter\n");
	LDC_LOCK(ldcp);

	if ((ldcp->flags & CHANNEL_STARTED) == 0) {
		LDC_UNLOCK(ldcp);
		DWARN(vgenp, ldcp, "CHANNEL_STARTED flag is not set\n");
		return;
	}

	/* disable further callbacks */
	rv = ldc_set_cb_mode(ldcp->ldc_handle, LDC_CB_DISABLE);
	if (rv != 0) {
		DWARN(vgenp, ldcp, "ldc_set_cb_mode failed\n");
	}

	/*
	 * clear handshake done bit and wait for pending tx and cb to finish.
	 * release locks before untimeout(9F) is invoked to cancel timeouts.
	 */
	ldcp->hphase &= ~(VH_DONE);
	LDC_UNLOCK(ldcp);

	if (vgenp->vsw_portp == ldcp->portp) {
		vio_net_report_err_t rep_err =
		    ldcp->portp->vcb.vio_net_report_err;
		rep_err(ldcp->portp->vhp, VIO_NET_RES_DOWN);
	}

	/* cancel handshake watchdog timeout */
	if (ldcp->htid) {
		(void) untimeout(ldcp->htid);
		ldcp->htid = 0;
	}

	if (ldcp->cancel_htid) {
		(void) untimeout(ldcp->cancel_htid);
		ldcp->cancel_htid = 0;
	}

	/* cancel transmit watchdog timeout */
	if (ldcp->wd_tid) {
		(void) untimeout(ldcp->wd_tid);
		ldcp->wd_tid = 0;
	}

	drv_usecwait(1000);

	if (ldcp->rcv_thread != NULL) {
		/*
		 * Note that callbacks have been disabled already(above). The
		 * drain function takes care of the condition when an already
		 * executing callback signals the worker to start processing or
		 * the worker has already been signalled and is in the middle of
		 * processing.
		 */
		vgen_drain_rcv_thread(ldcp);
	}

	/* acquire locks again; any pending transmits and callbacks are done */
	LDC_LOCK(ldcp);

	vgen_reset_hphase(ldcp);

	vgen_uninit_tbufs(ldcp);

	/* close the channel - retry on EAGAIN */
	while ((rv = ldc_close(ldcp->ldc_handle)) == EAGAIN) {
		if (++retries > vgen_ldccl_retries) {
			break;
		}
		drv_usecwait(VGEN_LDC_CLOSE_DELAY);
	}
	if (rv != 0) {
		cmn_err(CE_NOTE,
		    "!vnet%d: Error(%d) closing the channel(0x%lx)\n",
		    vgenp->instance, rv, ldcp->ldc_id);
	}

	ldcp->ldc_status = LDC_INIT;
	ldcp->flags &= ~(CHANNEL_STARTED);

	LDC_UNLOCK(ldcp);

	DBG1(vgenp, ldcp, "exit\n");
}

/* Initialize the transmit buffer ring for the channel */
static int
vgen_init_tbufs(vgen_ldc_t *ldcp)
{
	vgen_private_desc_t	*tbufp;
	vnet_public_desc_t	*txdp;
	vio_dring_entry_hdr_t		*hdrp;
	int 			i;
	int 			rv;
	caddr_t			datap = NULL;
	int			ci;
	uint32_t		ncookies;
	size_t			data_sz;
	vgen_t			*vgenp;

	vgenp = LDC_TO_VGEN(ldcp);

	bzero(ldcp->tbufp, sizeof (*tbufp) * (ldcp->num_txds));
	bzero(ldcp->txdp, sizeof (*txdp) * (ldcp->num_txds));

	/*
	 * In order to ensure that the number of ldc cookies per descriptor is
	 * limited to be within the default MAX_COOKIES (2), we take the steps
	 * outlined below:
	 *
	 * Align the entire data buffer area to 8K and carve out per descriptor
	 * data buffers starting from this 8K aligned base address.
	 *
	 * We round up the mtu specified to be a multiple of 2K or 4K.
	 * For sizes up to 12K we round up the size to the next 2K.
	 * For sizes > 12K we round up to the next 4K (otherwise sizes such as
	 * 14K could end up needing 3 cookies, with the buffer spread across
	 * 3 8K pages:  8K+6K, 2K+8K+2K, 6K+8K, ...).
	 */
	data_sz = vgenp->max_frame_size + VNET_IPALIGN + VNET_LDCALIGN;
	if (data_sz <= VNET_12K) {
		data_sz = VNET_ROUNDUP_2K(data_sz);
	} else {
		data_sz = VNET_ROUNDUP_4K(data_sz);
	}

	/* allocate extra 8K bytes for alignment */
	ldcp->tx_data_sz = (data_sz * ldcp->num_txds) + VNET_8K;
	datap = kmem_zalloc(ldcp->tx_data_sz, KM_SLEEP);
	ldcp->tx_datap = datap;


	/* align the starting address of the data area to 8K */
	datap = (caddr_t)VNET_ROUNDUP_8K((uintptr_t)datap);

	/*
	 * for each private descriptor, allocate a ldc mem_handle which is
	 * required to map the data during transmit, set the flags
	 * to free (available for use by transmit routine).
	 */

	for (i = 0; i < ldcp->num_txds; i++) {

		tbufp = &(ldcp->tbufp[i]);
		rv = ldc_mem_alloc_handle(ldcp->ldc_handle,
		    &(tbufp->memhandle));
		if (rv) {
			tbufp->memhandle = 0;
			goto init_tbufs_failed;
		}

		/*
		 * bind ldc memhandle to the corresponding transmit buffer.
		 */
		ci = ncookies = 0;
		rv = ldc_mem_bind_handle(tbufp->memhandle,
		    (caddr_t)datap, data_sz, LDC_SHADOW_MAP,
		    LDC_MEM_R, &(tbufp->memcookie[ci]), &ncookies);
		if (rv != 0) {
			goto init_tbufs_failed;
		}

		/*
		 * successful in binding the handle to tx data buffer.
		 * set datap in the private descr to this buffer.
		 */
		tbufp->datap = datap;

		if ((ncookies == 0) ||
		    (ncookies > MAX_COOKIES)) {
			goto init_tbufs_failed;
		}

		for (ci = 1; ci < ncookies; ci++) {
			rv = ldc_mem_nextcookie(tbufp->memhandle,
			    &(tbufp->memcookie[ci]));
			if (rv != 0) {
				goto init_tbufs_failed;
			}
		}

		tbufp->ncookies = ncookies;
		datap += data_sz;

		tbufp->flags = VGEN_PRIV_DESC_FREE;
		txdp = &(ldcp->txdp[i]);
		hdrp = &txdp->hdr;
		hdrp->dstate = VIO_DESC_FREE;
		hdrp->ack = B_FALSE;
		tbufp->descp = txdp;

	}

	/* reset tbuf walking pointers */
	ldcp->next_tbufp = ldcp->tbufp;
	ldcp->cur_tbufp = ldcp->tbufp;

	/* initialize tx seqnum and index */
	ldcp->next_txseq = VNET_ISS;
	ldcp->next_txi = 0;

	ldcp->resched_peer = B_TRUE;
	ldcp->resched_peer_txi = 0;

	return (DDI_SUCCESS);

init_tbufs_failed:;
	vgen_uninit_tbufs(ldcp);
	return (DDI_FAILURE);
}

/* Uninitialize transmit buffer ring for the channel */
static void
vgen_uninit_tbufs(vgen_ldc_t *ldcp)
{
	vgen_private_desc_t	*tbufp = ldcp->tbufp;
	int 			i;

	/* for each tbuf (priv_desc), free ldc mem_handle */
	for (i = 0; i < ldcp->num_txds; i++) {

		tbufp = &(ldcp->tbufp[i]);

		if (tbufp->datap) { /* if bound to a ldc memhandle */
			(void) ldc_mem_unbind_handle(tbufp->memhandle);
			tbufp->datap = NULL;
		}
		if (tbufp->memhandle) {
			(void) ldc_mem_free_handle(tbufp->memhandle);
			tbufp->memhandle = 0;
		}
	}

	if (ldcp->tx_datap) {
		/* prealloc'd tx data buffer */
		kmem_free(ldcp->tx_datap, ldcp->tx_data_sz);
		ldcp->tx_datap = NULL;
		ldcp->tx_data_sz = 0;
	}

	bzero(ldcp->tbufp, sizeof (vgen_private_desc_t) * (ldcp->num_txds));
	bzero(ldcp->txdp, sizeof (vnet_public_desc_t) * (ldcp->num_txds));
}

/* clobber tx descriptor ring */
static void
vgen_clobber_tbufs(vgen_ldc_t *ldcp)
{
	vnet_public_desc_t	*txdp;
	vgen_private_desc_t	*tbufp;
	vio_dring_entry_hdr_t	*hdrp;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	int i;
#ifdef DEBUG
	int ndone = 0;
#endif

	for (i = 0; i < ldcp->num_txds; i++) {

		tbufp = &(ldcp->tbufp[i]);
		txdp = tbufp->descp;
		hdrp = &txdp->hdr;

		if (tbufp->flags & VGEN_PRIV_DESC_BUSY) {
			tbufp->flags = VGEN_PRIV_DESC_FREE;
#ifdef DEBUG
			if (hdrp->dstate == VIO_DESC_DONE)
				ndone++;
#endif
			hdrp->dstate = VIO_DESC_FREE;
			hdrp->ack = B_FALSE;
		}
	}
	/* reset tbuf walking pointers */
	ldcp->next_tbufp = ldcp->tbufp;
	ldcp->cur_tbufp = ldcp->tbufp;

	/* reset tx seqnum and index */
	ldcp->next_txseq = VNET_ISS;
	ldcp->next_txi = 0;

	ldcp->resched_peer = B_TRUE;
	ldcp->resched_peer_txi = 0;

	DBG2(vgenp, ldcp, "num descrs done (%d)\n", ndone);
}

/* clobber receive descriptor ring */
static void
vgen_clobber_rxds(vgen_ldc_t *ldcp)
{
	ldcp->rx_dhandle = 0;
	bzero(&ldcp->rx_dcookie, sizeof (ldcp->rx_dcookie));
	ldcp->rxdp = NULL;
	ldcp->next_rxi = 0;
	ldcp->num_rxds = 0;
	ldcp->next_rxseq = VNET_ISS;
}

/* initialize receive descriptor ring */
static int
vgen_init_rxds(vgen_ldc_t *ldcp, uint32_t num_desc, uint32_t desc_size,
	ldc_mem_cookie_t *dcookie, uint32_t ncookies)
{
	int rv;
	ldc_mem_info_t minfo;

	rv = ldc_mem_dring_map(ldcp->ldc_handle, dcookie, ncookies, num_desc,
	    desc_size, LDC_DIRECT_MAP, &(ldcp->rx_dhandle));
	if (rv != 0) {
		return (DDI_FAILURE);
	}

	/*
	 * sucessfully mapped, now try to
	 * get info about the mapped dring
	 */
	rv = ldc_mem_dring_info(ldcp->rx_dhandle, &minfo);
	if (rv != 0) {
		(void) ldc_mem_dring_unmap(ldcp->rx_dhandle);
		return (DDI_FAILURE);
	}

	/*
	 * save ring address, number of descriptors.
	 */
	ldcp->rxdp = (vnet_public_desc_t *)(minfo.vaddr);
	bcopy(dcookie, &(ldcp->rx_dcookie), sizeof (*dcookie));
	ldcp->num_rxdcookies = ncookies;
	ldcp->num_rxds = num_desc;
	ldcp->next_rxi = 0;
	ldcp->next_rxseq = VNET_ISS;
	ldcp->dring_mtype = minfo.mtype;

	return (DDI_SUCCESS);
}

/* get channel statistics */
static uint64_t
vgen_ldc_stat(vgen_ldc_t *ldcp, uint_t stat)
{
	vgen_stats_t *statsp;
	uint64_t val;

	val = 0;
	statsp = &ldcp->stats;
	switch (stat) {

	case MAC_STAT_MULTIRCV:
		val = statsp->multircv;
		break;

	case MAC_STAT_BRDCSTRCV:
		val = statsp->brdcstrcv;
		break;

	case MAC_STAT_MULTIXMT:
		val = statsp->multixmt;
		break;

	case MAC_STAT_BRDCSTXMT:
		val = statsp->brdcstxmt;
		break;

	case MAC_STAT_NORCVBUF:
		val = statsp->norcvbuf;
		break;

	case MAC_STAT_IERRORS:
		val = statsp->ierrors;
		break;

	case MAC_STAT_NOXMTBUF:
		val = statsp->noxmtbuf;
		break;

	case MAC_STAT_OERRORS:
		val = statsp->oerrors;
		break;

	case MAC_STAT_COLLISIONS:
		break;

	case MAC_STAT_RBYTES:
		val = statsp->rbytes;
		break;

	case MAC_STAT_IPACKETS:
		val = statsp->ipackets;
		break;

	case MAC_STAT_OBYTES:
		val = statsp->obytes;
		break;

	case MAC_STAT_OPACKETS:
		val = statsp->opackets;
		break;

	/* stats not relevant to ldc, return 0 */
	case MAC_STAT_IFSPEED:
	case ETHER_STAT_ALIGN_ERRORS:
	case ETHER_STAT_FCS_ERRORS:
	case ETHER_STAT_FIRST_COLLISIONS:
	case ETHER_STAT_MULTI_COLLISIONS:
	case ETHER_STAT_DEFER_XMTS:
	case ETHER_STAT_TX_LATE_COLLISIONS:
	case ETHER_STAT_EX_COLLISIONS:
	case ETHER_STAT_MACXMT_ERRORS:
	case ETHER_STAT_CARRIER_ERRORS:
	case ETHER_STAT_TOOLONG_ERRORS:
	case ETHER_STAT_XCVR_ADDR:
	case ETHER_STAT_XCVR_ID:
	case ETHER_STAT_XCVR_INUSE:
	case ETHER_STAT_CAP_1000FDX:
	case ETHER_STAT_CAP_1000HDX:
	case ETHER_STAT_CAP_100FDX:
	case ETHER_STAT_CAP_100HDX:
	case ETHER_STAT_CAP_10FDX:
	case ETHER_STAT_CAP_10HDX:
	case ETHER_STAT_CAP_ASMPAUSE:
	case ETHER_STAT_CAP_PAUSE:
	case ETHER_STAT_CAP_AUTONEG:
	case ETHER_STAT_ADV_CAP_1000FDX:
	case ETHER_STAT_ADV_CAP_1000HDX:
	case ETHER_STAT_ADV_CAP_100FDX:
	case ETHER_STAT_ADV_CAP_100HDX:
	case ETHER_STAT_ADV_CAP_10FDX:
	case ETHER_STAT_ADV_CAP_10HDX:
	case ETHER_STAT_ADV_CAP_ASMPAUSE:
	case ETHER_STAT_ADV_CAP_PAUSE:
	case ETHER_STAT_ADV_CAP_AUTONEG:
	case ETHER_STAT_LP_CAP_1000FDX:
	case ETHER_STAT_LP_CAP_1000HDX:
	case ETHER_STAT_LP_CAP_100FDX:
	case ETHER_STAT_LP_CAP_100HDX:
	case ETHER_STAT_LP_CAP_10FDX:
	case ETHER_STAT_LP_CAP_10HDX:
	case ETHER_STAT_LP_CAP_ASMPAUSE:
	case ETHER_STAT_LP_CAP_PAUSE:
	case ETHER_STAT_LP_CAP_AUTONEG:
	case ETHER_STAT_LINK_ASMPAUSE:
	case ETHER_STAT_LINK_PAUSE:
	case ETHER_STAT_LINK_AUTONEG:
	case ETHER_STAT_LINK_DUPLEX:
	default:
		val = 0;
		break;

	}
	return (val);
}

/*
 * LDC channel is UP, start handshake process with peer.
 */
static void
vgen_handle_evt_up(vgen_ldc_t *ldcp)
{
	vgen_t	*vgenp = LDC_TO_VGEN(ldcp);

	DBG1(vgenp, ldcp, "enter\n");

	ASSERT(MUTEX_HELD(&ldcp->cblock));

	if (ldcp->portp != vgenp->vsw_portp) {
		/*
		 * As the channel is up, use this port from now on.
		 */
		(void) atomic_swap_32(&ldcp->portp->use_vsw_port, B_FALSE);
	}

	/* Initialize local session id */
	ldcp->local_sid = ddi_get_lbolt();

	/* clear peer session id */
	ldcp->peer_sid = 0;
	ldcp->hretries = 0;

	if (ldcp->hphase != VH_PHASE0) {
		vgen_handshake_reset(ldcp);
	}

	/* Initiate Handshake process with peer ldc endpoint */
	vgen_handshake(vh_nextphase(ldcp));

	DBG1(vgenp, ldcp, "exit\n");
}

/*
 * LDC channel is Reset, terminate connection with peer and try to
 * bring the channel up again.
 */
static void
vgen_handle_evt_reset(vgen_ldc_t *ldcp)
{
	ldc_status_t istatus;
	vgen_t	*vgenp = LDC_TO_VGEN(ldcp);
	int	rv;

	DBG1(vgenp, ldcp, "enter\n");

	ASSERT(MUTEX_HELD(&ldcp->cblock));

	if ((ldcp->portp != vgenp->vsw_portp) &&
	    (vgenp->vsw_portp != NULL)) {
		/*
		 * As the channel is down, use the switch port until
		 * the channel becomes ready to be used.
		 */
		(void) atomic_swap_32(&ldcp->portp->use_vsw_port, B_TRUE);
	}

	if (vgenp->vsw_portp == ldcp->portp) {
		vio_net_report_err_t rep_err =
		    ldcp->portp->vcb.vio_net_report_err;

		/* Post a reset message */
		rep_err(ldcp->portp->vhp, VIO_NET_RES_DOWN);
	}

	if (ldcp->hphase != VH_PHASE0) {
		vgen_handshake_reset(ldcp);
	}

	/* try to bring the channel up */
#ifdef	VNET_IOC_DEBUG
	if (ldcp->link_down_forced == B_FALSE) {
		rv = ldc_up(ldcp->ldc_handle);
		if (rv != 0) {
			DWARN(vgenp, ldcp, "ldc_up err rv(%d)\n", rv);
		}
	}
#else
	rv = ldc_up(ldcp->ldc_handle);
	if (rv != 0) {
		DWARN(vgenp, ldcp, "ldc_up err rv(%d)\n", rv);
	}
#endif

	if (ldc_status(ldcp->ldc_handle, &istatus) != 0) {
		DWARN(vgenp, ldcp, "ldc_status err\n");
	} else {
		ldcp->ldc_status = istatus;
	}

	/* if channel is already UP - restart handshake */
	if (ldcp->ldc_status == LDC_UP) {
		vgen_handle_evt_up(ldcp);
	}

	DBG1(vgenp, ldcp, "exit\n");
}

/* Interrupt handler for the channel */
static uint_t
vgen_ldc_cb(uint64_t event, caddr_t arg)
{
	_NOTE(ARGUNUSED(event))
	vgen_ldc_t	*ldcp;
	vgen_t		*vgenp;
	ldc_status_t 	istatus;
	vgen_stats_t	*statsp;
	timeout_id_t	cancel_htid = 0;
	uint_t		ret = LDC_SUCCESS;

	ldcp = (vgen_ldc_t *)arg;
	vgenp = LDC_TO_VGEN(ldcp);
	statsp = &ldcp->stats;

	DBG1(vgenp, ldcp, "enter\n");

	mutex_enter(&ldcp->cblock);
	statsp->callbacks++;
	if ((ldcp->ldc_status == LDC_INIT) || (ldcp->ldc_handle == NULL)) {
		DWARN(vgenp, ldcp, "status(%d) is LDC_INIT\n",
		    ldcp->ldc_status);
		mutex_exit(&ldcp->cblock);
		return (LDC_SUCCESS);
	}

	/*
	 * cache cancel_htid before the events specific
	 * code may overwrite it. Do not clear ldcp->cancel_htid
	 * as it is also used to indicate the timer to quit immediately.
	 */
	cancel_htid = ldcp->cancel_htid;

	/*
	 * NOTE: not using switch() as event could be triggered by
	 * a state change and a read request. Also the ordering	of the
	 * check for the event types is deliberate.
	 */
	if (event & LDC_EVT_UP) {
		if (ldc_status(ldcp->ldc_handle, &istatus) != 0) {
			DWARN(vgenp, ldcp, "ldc_status err\n");
			/* status couldn't be determined */
			ret = LDC_FAILURE;
			goto ldc_cb_ret;
		}
		ldcp->ldc_status = istatus;
		if (ldcp->ldc_status != LDC_UP) {
			DWARN(vgenp, ldcp, "LDC_EVT_UP received "
			    " but ldc status is not UP(0x%x)\n",
			    ldcp->ldc_status);
			/* spurious interrupt, return success */
			goto ldc_cb_ret;
		}
		DWARN(vgenp, ldcp, "event(%lx) UP, status(%d)\n",
		    event, ldcp->ldc_status);

		vgen_handle_evt_up(ldcp);

		ASSERT((event & (LDC_EVT_RESET | LDC_EVT_DOWN)) == 0);
	}

	/* Handle RESET/DOWN before READ event */
	if (event & (LDC_EVT_RESET | LDC_EVT_DOWN)) {
		if (ldc_status(ldcp->ldc_handle, &istatus) != 0) {
			DWARN(vgenp, ldcp, "ldc_status error\n");
			/* status couldn't be determined */
			ret = LDC_FAILURE;
			goto ldc_cb_ret;
		}
		ldcp->ldc_status = istatus;
		DWARN(vgenp, ldcp, "event(%lx) RESET/DOWN, status(%d)\n",
		    event, ldcp->ldc_status);

		vgen_handle_evt_reset(ldcp);

		/*
		 * As the channel is down/reset, ignore READ event
		 * but print a debug warning message.
		 */
		if (event & LDC_EVT_READ) {
			DWARN(vgenp, ldcp,
			    "LDC_EVT_READ set along with RESET/DOWN\n");
			event &= ~LDC_EVT_READ;
		}
	}

	if (event & LDC_EVT_READ) {
		DBG2(vgenp, ldcp, "event(%lx) READ, status(%d)\n",
		    event, ldcp->ldc_status);

		ASSERT((event & (LDC_EVT_RESET | LDC_EVT_DOWN)) == 0);

		if (ldcp->rcv_thread != NULL) {
			/*
			 * If the receive thread is enabled, then
			 * wakeup the receive thread to process the
			 * LDC messages.
			 */
			mutex_exit(&ldcp->cblock);
			mutex_enter(&ldcp->rcv_thr_lock);
			if (!(ldcp->rcv_thr_flags & VGEN_WTHR_DATARCVD)) {
				ldcp->rcv_thr_flags |= VGEN_WTHR_DATARCVD;
				cv_signal(&ldcp->rcv_thr_cv);
			}
			mutex_exit(&ldcp->rcv_thr_lock);
			mutex_enter(&ldcp->cblock);
		} else  {
			vgen_handle_evt_read(ldcp);
		}
	}

ldc_cb_ret:
	/*
	 * Check to see if the status of cancel_htid has
	 * changed. If another timer needs to be cancelled,
	 * then let the next callback to clear it.
	 */
	if (cancel_htid == 0) {
		cancel_htid = ldcp->cancel_htid;
	}
	mutex_exit(&ldcp->cblock);

	if (cancel_htid) {
		/*
		 * Cancel handshake timer.
		 * untimeout(9F) will not return until the pending callback is
		 * cancelled or has run. No problems will result from calling
		 * untimeout if the handler has already completed.
		 * If the timeout handler did run, then it would just
		 * return as cancel_htid is set.
		 */
		DBG2(vgenp, ldcp, "cancel_htid =0x%X \n", cancel_htid);
		(void) untimeout(cancel_htid);
		mutex_enter(&ldcp->cblock);
		/* clear it only if its the same as the one we cancelled */
		if (ldcp->cancel_htid == cancel_htid) {
			ldcp->cancel_htid = 0;
		}
		mutex_exit(&ldcp->cblock);
	}
	DBG1(vgenp, ldcp, "exit\n");
	return (ret);
}

static void
vgen_handle_evt_read(vgen_ldc_t *ldcp)
{
	int		rv;
	uint64_t	*ldcmsg;
	size_t		msglen;
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	vio_msg_tag_t	*tagp;
	ldc_status_t 	istatus;
	boolean_t 	has_data;

	DBG1(vgenp, ldcp, "enter\n");

	ldcmsg = ldcp->ldcmsg;
	/*
	 * If the receive thread is enabled, then the cblock
	 * need to be acquired here. If not, the vgen_ldc_cb()
	 * calls this function with cblock held already.
	 */
	if (ldcp->rcv_thread != NULL) {
		mutex_enter(&ldcp->cblock);
	} else {
		ASSERT(MUTEX_HELD(&ldcp->cblock));
	}

vgen_evt_read:
	do {
		msglen = ldcp->msglen;
		rv = ldc_read(ldcp->ldc_handle, (caddr_t)ldcmsg, &msglen);

		if (rv != 0) {
			DWARN(vgenp, ldcp, "err rv(%d) len(%d)\n",
			    rv, msglen);
			if (rv == ECONNRESET)
				goto vgen_evtread_error;
			break;
		}
		if (msglen == 0) {
			DBG2(vgenp, ldcp, "ldc_read NODATA");
			break;
		}
		DBG2(vgenp, ldcp, "ldc_read msglen(%d)", msglen);

		tagp = (vio_msg_tag_t *)ldcmsg;

		if (ldcp->peer_sid) {
			/*
			 * check sid only after we have received peer's sid
			 * in the version negotiate msg.
			 */
#ifdef DEBUG
			if (vgen_hdbg & HDBG_BAD_SID) {
				/* simulate bad sid condition */
				tagp->vio_sid = 0;
				vgen_hdbg &= ~(HDBG_BAD_SID);
			}
#endif
			rv = vgen_check_sid(ldcp, tagp);
			if (rv != VGEN_SUCCESS) {
				/*
				 * If sid mismatch is detected,
				 * reset the channel.
				 */
				goto vgen_evtread_error;
			}
		}

		switch (tagp->vio_msgtype) {
		case VIO_TYPE_CTRL:
			rv = vgen_handle_ctrlmsg(ldcp, tagp);
			break;

		case VIO_TYPE_DATA:
			rv = vgen_handle_datamsg(ldcp, tagp, msglen);
			break;

		case VIO_TYPE_ERR:
			vgen_handle_errmsg(ldcp, tagp);
			break;

		default:
			DWARN(vgenp, ldcp, "Unknown VIO_TYPE(%x)\n",
			    tagp->vio_msgtype);
			break;
		}

		/*
		 * If an error is encountered, stop processing and
		 * handle the error.
		 */
		if (rv != 0) {
			goto vgen_evtread_error;
		}

	} while (msglen);

	/* check once more before exiting */
	rv = ldc_chkq(ldcp->ldc_handle, &has_data);
	if ((rv == 0) && (has_data == B_TRUE)) {
		DTRACE_PROBE(vgen_chkq);
		goto vgen_evt_read;
	}

vgen_evtread_error:
	if (rv == ECONNRESET) {
		if (ldc_status(ldcp->ldc_handle, &istatus) != 0) {
			DWARN(vgenp, ldcp, "ldc_status err\n");
		} else {
			ldcp->ldc_status = istatus;
		}
		vgen_handle_evt_reset(ldcp);
	} else if (rv) {
		vgen_ldc_reset(ldcp);
	}

	/*
	 * If the receive thread is enabled, then cancel the
	 * handshake timeout here.
	 */
	if (ldcp->rcv_thread != NULL) {
		timeout_id_t cancel_htid = ldcp->cancel_htid;

		mutex_exit(&ldcp->cblock);
		if (cancel_htid) {
			/*
			 * Cancel handshake timer. untimeout(9F) will
			 * not return until the pending callback is cancelled
			 * or has run. No problems will result from calling
			 * untimeout if the handler has already completed.
			 * If the timeout handler did run, then it would just
			 * return as cancel_htid is set.
			 */
			DBG2(vgenp, ldcp, "cancel_htid =0x%X \n", cancel_htid);
			(void) untimeout(cancel_htid);

			/*
			 * clear it only if its the same as the one we
			 * cancelled
			 */
			mutex_enter(&ldcp->cblock);
			if (ldcp->cancel_htid == cancel_htid) {
				ldcp->cancel_htid = 0;
			}
			mutex_exit(&ldcp->cblock);
		}
	}

	DBG1(vgenp, ldcp, "exit\n");
}

/* vgen handshake functions */

/* change the hphase for the channel to the next phase */
static vgen_ldc_t *
vh_nextphase(vgen_ldc_t *ldcp)
{
	if (ldcp->hphase == VH_PHASE3) {
		ldcp->hphase = VH_DONE;
	} else {
		ldcp->hphase++;
	}
	return (ldcp);
}

/*
 * wrapper routine to send the given message over ldc using ldc_write().
 */
static int
vgen_sendmsg(vgen_ldc_t *ldcp, caddr_t msg,  size_t msglen,
    boolean_t caller_holds_lock)
{
	int			rv;
	size_t			len;
	uint32_t		retries = 0;
	vgen_t			*vgenp = LDC_TO_VGEN(ldcp);
	vio_msg_tag_t		*tagp = (vio_msg_tag_t *)msg;
	vio_dring_msg_t		*dmsg;
	vio_raw_data_msg_t	*rmsg;
	boolean_t		data_msg = B_FALSE;

	len = msglen;
	if ((len == 0) || (msg == NULL))
		return (VGEN_FAILURE);

	if (!caller_holds_lock) {
		mutex_enter(&ldcp->wrlock);
	}

	if (tagp->vio_subtype == VIO_SUBTYPE_INFO) {
		if (tagp->vio_subtype_env == VIO_DRING_DATA) {
			dmsg = (vio_dring_msg_t *)tagp;
			dmsg->seq_num = ldcp->next_txseq;
			data_msg = B_TRUE;
		} else if (tagp->vio_subtype_env == VIO_PKT_DATA) {
			rmsg = (vio_raw_data_msg_t *)tagp;
			rmsg->seq_num = ldcp->next_txseq;
			data_msg = B_TRUE;
		}
	}

	do {
		len = msglen;
		rv = ldc_write(ldcp->ldc_handle, (caddr_t)msg, &len);
		if (retries++ >= vgen_ldcwr_retries)
			break;
	} while (rv == EWOULDBLOCK);

	if (rv == 0 && data_msg == B_TRUE) {
		ldcp->next_txseq++;
	}

	if (!caller_holds_lock) {
		mutex_exit(&ldcp->wrlock);
	}

	if (rv != 0) {
		DWARN(vgenp, ldcp, "ldc_write failed: rv(%d)\n",
		    rv, msglen);
		return (rv);
	}

	if (len != msglen) {
		DWARN(vgenp, ldcp, "ldc_write failed: rv(%d) msglen (%d)\n",
		    rv, msglen);
		return (VGEN_FAILURE);
	}

	return (VGEN_SUCCESS);
}

/* send version negotiate message to the peer over ldc */
static int
vgen_send_version_negotiate(vgen_ldc_t *ldcp)
{
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	vio_ver_msg_t	vermsg;
	vio_msg_tag_t	*tagp = &vermsg.tag;
	int		rv;

	bzero(&vermsg, sizeof (vermsg));

	tagp->vio_msgtype = VIO_TYPE_CTRL;
	tagp->vio_subtype = VIO_SUBTYPE_INFO;
	tagp->vio_subtype_env = VIO_VER_INFO;
	tagp->vio_sid = ldcp->local_sid;

	/* get version msg payload from ldcp->local */
	vermsg.ver_major = ldcp->local_hparams.ver_major;
	vermsg.ver_minor = ldcp->local_hparams.ver_minor;
	vermsg.dev_class = ldcp->local_hparams.dev_class;

	rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (vermsg), B_FALSE);
	if (rv != VGEN_SUCCESS) {
		DWARN(vgenp, ldcp, "vgen_sendmsg failed\n");
		return (rv);
	}

	ldcp->hstate |= VER_INFO_SENT;
	DBG2(vgenp, ldcp, "VER_INFO_SENT ver(%d,%d)\n",
	    vermsg.ver_major, vermsg.ver_minor);

	return (VGEN_SUCCESS);
}

/* send attr info message to the peer over ldc */
static int
vgen_send_attr_info(vgen_ldc_t *ldcp)
{
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	vnet_attr_msg_t	attrmsg;
	vio_msg_tag_t	*tagp = &attrmsg.tag;
	int		rv;

	bzero(&attrmsg, sizeof (attrmsg));

	tagp->vio_msgtype = VIO_TYPE_CTRL;
	tagp->vio_subtype = VIO_SUBTYPE_INFO;
	tagp->vio_subtype_env = VIO_ATTR_INFO;
	tagp->vio_sid = ldcp->local_sid;

	/* get attr msg payload from ldcp->local */
	attrmsg.mtu = ldcp->local_hparams.mtu;
	attrmsg.addr = ldcp->local_hparams.addr;
	attrmsg.addr_type = ldcp->local_hparams.addr_type;
	attrmsg.xfer_mode = ldcp->local_hparams.xfer_mode;
	attrmsg.ack_freq = ldcp->local_hparams.ack_freq;
	attrmsg.physlink_update = ldcp->local_hparams.physlink_update;

	rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (attrmsg), B_FALSE);
	if (rv != VGEN_SUCCESS) {
		DWARN(vgenp, ldcp, "vgen_sendmsg failed\n");
		return (rv);
	}

	ldcp->hstate |= ATTR_INFO_SENT;
	DBG2(vgenp, ldcp, "ATTR_INFO_SENT\n");

	return (VGEN_SUCCESS);
}

/* send descriptor ring register message to the peer over ldc */
static int
vgen_send_dring_reg(vgen_ldc_t *ldcp)
{
	vgen_t			*vgenp = LDC_TO_VGEN(ldcp);
	vio_dring_reg_msg_t	msg;
	vio_msg_tag_t		*tagp = &msg.tag;
	int		rv;

	bzero(&msg, sizeof (msg));

	tagp->vio_msgtype = VIO_TYPE_CTRL;
	tagp->vio_subtype = VIO_SUBTYPE_INFO;
	tagp->vio_subtype_env = VIO_DRING_REG;
	tagp->vio_sid = ldcp->local_sid;

	/* get dring info msg payload from ldcp->local */
	bcopy(&(ldcp->local_hparams.dring_cookie), (msg.cookie),
	    sizeof (ldc_mem_cookie_t));
	msg.ncookies = ldcp->local_hparams.num_dcookies;
	msg.num_descriptors = ldcp->local_hparams.num_desc;
	msg.descriptor_size = ldcp->local_hparams.desc_size;

	/*
	 * dring_ident is set to 0. After mapping the dring, peer sets this
	 * value and sends it in the ack, which is saved in
	 * vgen_handle_dring_reg().
	 */
	msg.dring_ident = 0;

	rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (msg), B_FALSE);
	if (rv != VGEN_SUCCESS) {
		DWARN(vgenp, ldcp, "vgen_sendmsg failed\n");
		return (rv);
	}

	ldcp->hstate |= DRING_INFO_SENT;
	DBG2(vgenp, ldcp, "DRING_INFO_SENT \n");

	return (VGEN_SUCCESS);
}

static int
vgen_send_rdx_info(vgen_ldc_t *ldcp)
{
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	vio_rdx_msg_t	rdxmsg;
	vio_msg_tag_t	*tagp = &rdxmsg.tag;
	int		rv;

	bzero(&rdxmsg, sizeof (rdxmsg));

	tagp->vio_msgtype = VIO_TYPE_CTRL;
	tagp->vio_subtype = VIO_SUBTYPE_INFO;
	tagp->vio_subtype_env = VIO_RDX;
	tagp->vio_sid = ldcp->local_sid;

	rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (rdxmsg), B_FALSE);
	if (rv != VGEN_SUCCESS) {
		DWARN(vgenp, ldcp, "vgen_sendmsg failed\n");
		return (rv);
	}

	ldcp->hstate |= RDX_INFO_SENT;
	DBG2(vgenp, ldcp, "RDX_INFO_SENT\n");

	return (VGEN_SUCCESS);
}

/* send descriptor ring data message to the peer over ldc */
static int
vgen_send_dring_data(vgen_ldc_t *ldcp, uint32_t start, int32_t end)
{
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	vio_dring_msg_t	dringmsg, *msgp = &dringmsg;
	vio_msg_tag_t	*tagp = &msgp->tag;
	vgen_stats_t	*statsp = &ldcp->stats;
	int		rv;

	bzero(msgp, sizeof (*msgp));

	tagp->vio_msgtype = VIO_TYPE_DATA;
	tagp->vio_subtype = VIO_SUBTYPE_INFO;
	tagp->vio_subtype_env = VIO_DRING_DATA;
	tagp->vio_sid = ldcp->local_sid;

	msgp->dring_ident = ldcp->local_hparams.dring_ident;
	msgp->start_idx = start;
	msgp->end_idx = end;

	rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (dringmsg), B_TRUE);
	if (rv != VGEN_SUCCESS) {
		DWARN(vgenp, ldcp, "vgen_sendmsg failed\n");
		return (rv);
	}

	statsp->dring_data_msgs++;

	DBG2(vgenp, ldcp, "DRING_DATA_SENT \n");

	return (VGEN_SUCCESS);
}

/* send multicast addr info message to vsw */
static int
vgen_send_mcast_info(vgen_ldc_t *ldcp)
{
	vnet_mcast_msg_t	mcastmsg;
	vnet_mcast_msg_t	*msgp;
	vio_msg_tag_t		*tagp;
	vgen_t			*vgenp;
	struct ether_addr	*mca;
	int			rv;
	int			i;
	uint32_t		size;
	uint32_t		mccount;
	uint32_t		n;

	msgp = &mcastmsg;
	tagp = &msgp->tag;
	vgenp = LDC_TO_VGEN(ldcp);

	mccount = vgenp->mccount;
	i = 0;

	do {
		tagp->vio_msgtype = VIO_TYPE_CTRL;
		tagp->vio_subtype = VIO_SUBTYPE_INFO;
		tagp->vio_subtype_env = VNET_MCAST_INFO;
		tagp->vio_sid = ldcp->local_sid;

		n = ((mccount >= VNET_NUM_MCAST) ? VNET_NUM_MCAST : mccount);
		size = n * sizeof (struct ether_addr);

		mca = &(vgenp->mctab[i]);
		bcopy(mca, (msgp->mca), size);
		msgp->set = B_TRUE;
		msgp->count = n;

		rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (*msgp),
		    B_FALSE);
		if (rv != VGEN_SUCCESS) {
			DWARN(vgenp, ldcp, "vgen_sendmsg err(%d)\n", rv);
			return (rv);
		}

		mccount -= n;
		i += n;

	} while (mccount);

	return (VGEN_SUCCESS);
}

/* Initiate Phase 2 of handshake */
static int
vgen_handshake_phase2(vgen_ldc_t *ldcp)
{
	int rv;
	uint32_t ncookies = 0;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

#ifdef DEBUG
	if (vgen_hdbg & HDBG_OUT_STATE) {
		/* simulate out of state condition */
		vgen_hdbg &= ~(HDBG_OUT_STATE);
		rv = vgen_send_rdx_info(ldcp);
		return (rv);
	}
	if (vgen_hdbg & HDBG_TIMEOUT) {
		/* simulate timeout condition */
		vgen_hdbg &= ~(HDBG_TIMEOUT);
		return (VGEN_SUCCESS);
	}
#endif
	rv = vgen_send_attr_info(ldcp);
	if (rv != VGEN_SUCCESS) {
		return (rv);
	}

	/* Bind descriptor ring to the channel */
	if (ldcp->num_txdcookies == 0) {
		rv = ldc_mem_dring_bind(ldcp->ldc_handle, ldcp->tx_dhandle,
		    LDC_DIRECT_MAP | LDC_SHADOW_MAP, LDC_MEM_RW,
		    &ldcp->tx_dcookie, &ncookies);
		if (rv != 0) {
			DWARN(vgenp, ldcp, "ldc_mem_dring_bind failed "
			    "rv(%x)\n", rv);
			return (rv);
		}
		ASSERT(ncookies == 1);
		ldcp->num_txdcookies = ncookies;
	}

	/* update local dring_info params */
	bcopy(&(ldcp->tx_dcookie), &(ldcp->local_hparams.dring_cookie),
	    sizeof (ldc_mem_cookie_t));
	ldcp->local_hparams.num_dcookies = ldcp->num_txdcookies;
	ldcp->local_hparams.num_desc = ldcp->num_txds;
	ldcp->local_hparams.desc_size = sizeof (vnet_public_desc_t);

	rv = vgen_send_dring_reg(ldcp);
	if (rv != VGEN_SUCCESS) {
		return (rv);
	}

	return (VGEN_SUCCESS);
}

/*
 * Set vnet-protocol-version dependent functions based on version.
 */
static void
vgen_set_vnet_proto_ops(vgen_ldc_t *ldcp)
{
	vgen_hparams_t	*lp = &ldcp->local_hparams;
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);

	if (VGEN_VER_GTEQ(ldcp, 1, 5)) {
		vgen_port_t	*portp = ldcp->portp;
		vnet_t		*vnetp = vgenp->vnetp;
		/*
		 * If the version negotiated with vswitch is >= 1.5 (link
		 * status update support), set the required bits in our
		 * attributes if this vnet device has been configured to get
		 * physical link state updates.
		 */
		if (portp == vgenp->vsw_portp && vnetp->pls_update == B_TRUE) {
			lp->physlink_update = PHYSLINK_UPDATE_STATE;
		} else {
			lp->physlink_update = PHYSLINK_UPDATE_NONE;
		}
	}

	if (VGEN_VER_GTEQ(ldcp, 1, 4)) {
		/*
		 * If the version negotiated with peer is >= 1.4(Jumbo Frame
		 * Support), set the mtu in our attributes to max_frame_size.
		 */
		lp->mtu = vgenp->max_frame_size;
	} else  if (VGEN_VER_EQ(ldcp, 1, 3)) {
		/*
		 * If the version negotiated with peer is == 1.3 (Vlan Tag
		 * Support) set the attr.mtu to ETHERMAX + VLAN_TAGSZ.
		 */
		lp->mtu = ETHERMAX + VLAN_TAGSZ;
	} else {
		vgen_port_t	*portp = ldcp->portp;
		vnet_t		*vnetp = vgenp->vnetp;
		/*
		 * Pre-1.3 peers expect max frame size of ETHERMAX.
		 * We can negotiate that size with those peers provided the
		 * following conditions are true:
		 * - Only pvid is defined for our peer and there are no vids.
		 * - pvids are equal.
		 * If the above conditions are true, then we can send/recv only
		 * untagged frames of max size ETHERMAX.
		 */
		if (portp->nvids == 0 && portp->pvid == vnetp->pvid) {
			lp->mtu = ETHERMAX;
		}
	}

	if (VGEN_VER_GTEQ(ldcp, 1, 2)) {
		/* Versions >= 1.2 */

		if (VGEN_PRI_ETH_DEFINED(vgenp)) {
			/*
			 * enable priority routines and pkt mode only if
			 * at least one pri-eth-type is specified in MD.
			 */

			ldcp->tx = vgen_ldcsend;
			ldcp->rx_pktdata = vgen_handle_pkt_data;

			/* set xfer mode for vgen_send_attr_info() */
			lp->xfer_mode = VIO_PKT_MODE | VIO_DRING_MODE_V1_2;

		} else {
			/* no priority eth types defined in MD */

			ldcp->tx = vgen_ldcsend_dring;
			ldcp->rx_pktdata = vgen_handle_pkt_data_nop;

			/* set xfer mode for vgen_send_attr_info() */
			lp->xfer_mode = VIO_DRING_MODE_V1_2;

		}
	} else {
		/* Versions prior to 1.2  */

		vgen_reset_vnet_proto_ops(ldcp);
	}
}

/*
 * Reset vnet-protocol-version dependent functions to pre-v1.2.
 */
static void
vgen_reset_vnet_proto_ops(vgen_ldc_t *ldcp)
{
	vgen_hparams_t	*lp = &ldcp->local_hparams;

	ldcp->tx = vgen_ldcsend_dring;
	ldcp->rx_pktdata = vgen_handle_pkt_data_nop;

	/* set xfer mode for vgen_send_attr_info() */
	lp->xfer_mode = VIO_DRING_MODE_V1_0;
}

static void
vgen_vlan_unaware_port_reset(vgen_port_t *portp)
{
	vgen_ldclist_t	*ldclp;
	vgen_ldc_t	*ldcp;
	vgen_t		*vgenp = portp->vgenp;
	vnet_t		*vnetp = vgenp->vnetp;

	ldclp = &portp->ldclist;

	READ_ENTER(&ldclp->rwlock);

	/*
	 * NOTE: for now, we will assume we have a single channel.
	 */
	if (ldclp->headp == NULL) {
		RW_EXIT(&ldclp->rwlock);
		return;
	}
	ldcp = ldclp->headp;

	mutex_enter(&ldcp->cblock);

	/*
	 * If the peer is vlan_unaware(ver < 1.3), reset channel and terminate
	 * the connection. See comments in vgen_set_vnet_proto_ops().
	 */
	if (ldcp->hphase == VH_DONE && VGEN_VER_LT(ldcp, 1, 3) &&
	    (portp->nvids != 0 || portp->pvid != vnetp->pvid)) {
		vgen_ldc_reset(ldcp);
	}

	mutex_exit(&ldcp->cblock);

	RW_EXIT(&ldclp->rwlock);
}

static void
vgen_port_reset(vgen_port_t *portp)
{
	vgen_ldclist_t	*ldclp;
	vgen_ldc_t	*ldcp;

	ldclp = &portp->ldclist;

	READ_ENTER(&ldclp->rwlock);

	/*
	 * NOTE: for now, we will assume we have a single channel.
	 */
	if (ldclp->headp == NULL) {
		RW_EXIT(&ldclp->rwlock);
		return;
	}
	ldcp = ldclp->headp;

	mutex_enter(&ldcp->cblock);

	vgen_ldc_reset(ldcp);

	mutex_exit(&ldcp->cblock);

	RW_EXIT(&ldclp->rwlock);
}

static void
vgen_reset_vlan_unaware_ports(vgen_t *vgenp)
{
	vgen_port_t	*portp;
	vgen_portlist_t	*plistp;

	plistp = &(vgenp->vgenports);
	READ_ENTER(&plistp->rwlock);

	for (portp = plistp->headp; portp != NULL; portp = portp->nextp) {

		vgen_vlan_unaware_port_reset(portp);

	}

	RW_EXIT(&plistp->rwlock);
}

static void
vgen_reset_vsw_port(vgen_t *vgenp)
{
	vgen_port_t	*portp;

	if ((portp = vgenp->vsw_portp) != NULL) {
		vgen_port_reset(portp);
	}
}

/*
 * This function resets the handshake phase to VH_PHASE0(pre-handshake phase).
 * This can happen after a channel comes up (status: LDC_UP) or
 * when handshake gets terminated due to various conditions.
 */
static void
vgen_reset_hphase(vgen_ldc_t *ldcp)
{
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	ldc_status_t istatus;
	int rv;

	DBG1(vgenp, ldcp, "enter\n");
	/* reset hstate and hphase */
	ldcp->hstate = 0;
	ldcp->hphase = VH_PHASE0;

	vgen_reset_vnet_proto_ops(ldcp);

	/*
	 * Save the id of pending handshake timer in cancel_htid.
	 * This will be checked in vgen_ldc_cb() and the handshake timer will
	 * be cancelled after releasing cblock.
	 */
	if (ldcp->htid) {
		ldcp->cancel_htid = ldcp->htid;
		ldcp->htid = 0;
	}

	if (ldcp->local_hparams.dring_ready) {
		ldcp->local_hparams.dring_ready = B_FALSE;
	}

	/* Unbind tx descriptor ring from the channel */
	if (ldcp->num_txdcookies) {
		rv = ldc_mem_dring_unbind(ldcp->tx_dhandle);
		if (rv != 0) {
			DWARN(vgenp, ldcp, "ldc_mem_dring_unbind failed\n");
		}
		ldcp->num_txdcookies = 0;
	}

	if (ldcp->peer_hparams.dring_ready) {
		ldcp->peer_hparams.dring_ready = B_FALSE;
		/* Unmap peer's dring */
		(void) ldc_mem_dring_unmap(ldcp->rx_dhandle);
		vgen_clobber_rxds(ldcp);
	}

	vgen_clobber_tbufs(ldcp);

	/*
	 * clear local handshake params and initialize.
	 */
	bzero(&(ldcp->local_hparams), sizeof (ldcp->local_hparams));

	/* set version to the highest version supported */
	ldcp->local_hparams.ver_major =
	    ldcp->vgen_versions[0].ver_major;
	ldcp->local_hparams.ver_minor =
	    ldcp->vgen_versions[0].ver_minor;
	ldcp->local_hparams.dev_class = VDEV_NETWORK;

	/* set attr_info params */
	ldcp->local_hparams.mtu = vgenp->max_frame_size;
	ldcp->local_hparams.addr =
	    vnet_macaddr_strtoul(vgenp->macaddr);
	ldcp->local_hparams.addr_type = ADDR_TYPE_MAC;
	ldcp->local_hparams.xfer_mode = VIO_DRING_MODE_V1_0;
	ldcp->local_hparams.ack_freq = 0;	/* don't need acks */
	ldcp->local_hparams.physlink_update = PHYSLINK_UPDATE_NONE;

	/*
	 * Note: dring is created, but not bound yet.
	 * local dring_info params will be updated when we bind the dring in
	 * vgen_handshake_phase2().
	 * dring_ident is set to 0. After mapping the dring, peer sets this
	 * value and sends it in the ack, which is saved in
	 * vgen_handle_dring_reg().
	 */
	ldcp->local_hparams.dring_ident = 0;

	/* clear peer_hparams */
	bzero(&(ldcp->peer_hparams), sizeof (ldcp->peer_hparams));

	/* reset the channel if required */
#ifdef	VNET_IOC_DEBUG
	if (ldcp->need_ldc_reset && !ldcp->link_down_forced) {
#else
	if (ldcp->need_ldc_reset) {
#endif
		DWARN(vgenp, ldcp, "Doing Channel Reset...\n");
		ldcp->need_ldc_reset = B_FALSE;
		(void) ldc_down(ldcp->ldc_handle);
		(void) ldc_status(ldcp->ldc_handle, &istatus);
		DBG2(vgenp, ldcp, "Reset Done,ldc_status(%x)\n", istatus);
		ldcp->ldc_status = istatus;

		/* clear sids */
		ldcp->local_sid = 0;
		ldcp->peer_sid = 0;

		/* try to bring the channel up */
		rv = ldc_up(ldcp->ldc_handle);
		if (rv != 0) {
			DWARN(vgenp, ldcp, "ldc_up err rv(%d)\n", rv);
		}

		if (ldc_status(ldcp->ldc_handle, &istatus) != 0) {
			DWARN(vgenp, ldcp, "ldc_status err\n");
		} else {
			ldcp->ldc_status = istatus;
		}
	}
}

/* wrapper function for vgen_reset_hphase */
static void
vgen_handshake_reset(vgen_ldc_t *ldcp)
{
	vgen_t  *vgenp = LDC_TO_VGEN(ldcp);

	ASSERT(MUTEX_HELD(&ldcp->cblock));
	mutex_enter(&ldcp->rxlock);
	mutex_enter(&ldcp->wrlock);
	mutex_enter(&ldcp->txlock);
	mutex_enter(&ldcp->tclock);

	vgen_reset_hphase(ldcp);

	mutex_exit(&ldcp->tclock);
	mutex_exit(&ldcp->txlock);
	mutex_exit(&ldcp->wrlock);
	mutex_exit(&ldcp->rxlock);

	/*
	 * As the connection is now reset, mark the channel
	 * link_state as 'down' and notify the stack if needed.
	 */
	if (ldcp->link_state != LINK_STATE_DOWN) {
		ldcp->link_state = LINK_STATE_DOWN;

		if (ldcp->portp == vgenp->vsw_portp) { /* vswitch port ? */
			/*
			 * As the channel link is down, mark physical link also
			 * as down. After the channel comes back up and
			 * handshake completes, we will get an update on the
			 * physlink state from vswitch (if this device has been
			 * configured to get phys link updates).
			 */
			vgenp->phys_link_state = LINK_STATE_DOWN;

			/* Now update the stack */
			mutex_exit(&ldcp->cblock);
			vgen_link_update(vgenp, ldcp->link_state);
			mutex_enter(&ldcp->cblock);
		}
	}
}

/*
 * Initiate handshake with the peer by sending various messages
 * based on the handshake-phase that the channel is currently in.
 */
static void
vgen_handshake(vgen_ldc_t *ldcp)
{
	uint32_t	hphase = ldcp->hphase;
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	ldc_status_t	istatus;
	int		rv = 0;

	switch (hphase) {

	case VH_PHASE1:

		/*
		 * start timer, for entire handshake process, turn this timer
		 * off if all phases of handshake complete successfully and
		 * hphase goes to VH_DONE(below) or
		 * vgen_reset_hphase() gets called or
		 * channel is reset due to errors or
		 * vgen_ldc_uninit() is invoked(vgen_stop).
		 */
		ASSERT(ldcp->htid == 0);
		ldcp->htid = timeout(vgen_hwatchdog, (caddr_t)ldcp,
		    drv_usectohz(vgen_hwd_interval * MICROSEC));

		/* Phase 1 involves negotiating the version */
		rv = vgen_send_version_negotiate(ldcp);
		break;

	case VH_PHASE2:
		rv = vgen_handshake_phase2(ldcp);
		break;

	case VH_PHASE3:
		rv = vgen_send_rdx_info(ldcp);
		break;

	case VH_DONE:
		/*
		 * Save the id of pending handshake timer in cancel_htid.
		 * This will be checked in vgen_ldc_cb() and the handshake
		 * timer will be cancelled after releasing cblock.
		 */
		if (ldcp->htid) {
			ldcp->cancel_htid = ldcp->htid;
			ldcp->htid = 0;
		}
		ldcp->hretries = 0;
		DBG1(vgenp, ldcp, "Handshake Done\n");

		/*
		 * The channel is up and handshake is done successfully. Now we
		 * can mark the channel link_state as 'up'. We also notify the
		 * stack if the channel is connected to vswitch.
		 */
		ldcp->link_state = LINK_STATE_UP;

		if (ldcp->portp == vgenp->vsw_portp) {
			/*
			 * If this channel(port) is connected to vsw,
			 * need to sync multicast table with vsw.
			 */
			rv = vgen_send_mcast_info(ldcp);
			if (rv != VGEN_SUCCESS) {
				break;
			}

			if (vgenp->pls_negotiated == B_FALSE) {
				/*
				 * We haven't negotiated with vswitch to get
				 * physical link state updates. We can update
				 * update the stack at this point as the
				 * channel to vswitch is up and the handshake
				 * is done successfully.
				 *
				 * If we have negotiated to get physical link
				 * state updates, then we won't notify the
				 * the stack here; we do that as soon as
				 * vswitch sends us the initial phys link state
				 * (see vgen_handle_physlink_info()).
				 */
				mutex_exit(&ldcp->cblock);
				vgen_link_update(vgenp, ldcp->link_state);
				mutex_enter(&ldcp->cblock);
			}

		}

		/*
		 * Check if mac layer should be notified to restart
		 * transmissions. This can happen if the channel got
		 * reset and vgen_clobber_tbufs() is called, while
		 * need_resched is set.
		 */
		mutex_enter(&ldcp->tclock);
		if (ldcp->need_resched) {
			vio_net_tx_update_t vtx_update =
			    ldcp->portp->vcb.vio_net_tx_update;

			ldcp->need_resched = B_FALSE;
			vtx_update(ldcp->portp->vhp);
		}
		mutex_exit(&ldcp->tclock);

		break;

	default:
		break;
	}

	if (rv == ECONNRESET) {
		if (ldc_status(ldcp->ldc_handle, &istatus) != 0) {
			DWARN(vgenp, ldcp, "ldc_status err\n");
		} else {
			ldcp->ldc_status = istatus;
		}
		vgen_handle_evt_reset(ldcp);
	} else if (rv) {
		vgen_handshake_reset(ldcp);
	}
}

/*
 * Check if the current handshake phase has completed successfully and
 * return the status.
 */
static int
vgen_handshake_done(vgen_ldc_t *ldcp)
{
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	uint32_t	hphase = ldcp->hphase;
	int 		status = 0;

	switch (hphase) {

	case VH_PHASE1:
		/*
		 * Phase1 is done, if version negotiation
		 * completed successfully.
		 */
		status = ((ldcp->hstate & VER_NEGOTIATED) ==
		    VER_NEGOTIATED);
		break;

	case VH_PHASE2:
		/*
		 * Phase 2 is done, if attr info and dring info
		 * have been exchanged successfully.
		 */
		status = (((ldcp->hstate & ATTR_INFO_EXCHANGED) ==
		    ATTR_INFO_EXCHANGED) &&
		    ((ldcp->hstate & DRING_INFO_EXCHANGED) ==
		    DRING_INFO_EXCHANGED));
		break;

	case VH_PHASE3:
		/* Phase 3 is done, if rdx msg has been exchanged */
		status = ((ldcp->hstate & RDX_EXCHANGED) ==
		    RDX_EXCHANGED);
		break;

	default:
		break;
	}

	if (status == 0) {
		return (VGEN_FAILURE);
	}
	DBG2(vgenp, ldcp, "PHASE(%d)\n", hphase);
	return (VGEN_SUCCESS);
}

/* retry handshake on failure */
static void
vgen_handshake_retry(vgen_ldc_t *ldcp)
{
	/* reset handshake phase */
	vgen_handshake_reset(ldcp);

	/* handshake retry is specified and the channel is UP */
	if (vgen_max_hretries && (ldcp->ldc_status == LDC_UP)) {
		if (ldcp->hretries++ < vgen_max_hretries) {
			ldcp->local_sid = ddi_get_lbolt();
			vgen_handshake(vh_nextphase(ldcp));
		}
	}
}


/*
 * Link State Update Notes:
 * The link state of the channel connected to vswitch is reported as the link
 * state of the vnet device, by default. If the channel is down or reset, then
 * the link state is marked 'down'. If the channel is 'up' *and* handshake
 * between the vnet and vswitch is successful, then the link state is marked
 * 'up'. If physical network link state is desired, then the vnet device must
 * be configured to get physical link updates and the 'linkprop' property
 * in the virtual-device MD node indicates this. As part of attribute exchange
 * the vnet device negotiates with the vswitch to obtain physical link state
 * updates. If it successfully negotiates, vswitch sends an initial physlink
 * msg once the handshake is done and further whenever the physical link state
 * changes. Currently we don't have mac layer interfaces to report two distinct
 * link states - virtual and physical. Thus, if the vnet has been configured to
 * get physical link updates, then the link status will be reported as 'up'
 * only when both the virtual and physical links are up.
 */
static void
vgen_link_update(vgen_t *vgenp, link_state_t link_state)
{
	vnet_link_update(vgenp->vnetp, link_state);
}

/*
 * Handle a version info msg from the peer or an ACK/NACK from the peer
 * to a version info msg that we sent.
 */
static int
vgen_handle_version_negotiate(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	vgen_t		*vgenp;
	vio_ver_msg_t	*vermsg = (vio_ver_msg_t *)tagp;
	int		ack = 0;
	int		failed = 0;
	int		idx;
	vgen_ver_t	*versions = ldcp->vgen_versions;
	int		rv = 0;

	vgenp = LDC_TO_VGEN(ldcp);
	DBG1(vgenp, ldcp, "enter\n");
	switch (tagp->vio_subtype) {
	case VIO_SUBTYPE_INFO:

		/*  Cache sid of peer if this is the first time */
		if (ldcp->peer_sid == 0) {
			DBG2(vgenp, ldcp, "Caching peer_sid(%x)\n",
			    tagp->vio_sid);
			ldcp->peer_sid = tagp->vio_sid;
		}

		if (ldcp->hphase != VH_PHASE1) {
			/*
			 * If we are not already in VH_PHASE1, reset to
			 * pre-handshake state, and initiate handshake
			 * to the peer too.
			 */
			vgen_handshake_reset(ldcp);
			vgen_handshake(vh_nextphase(ldcp));
		}
		ldcp->hstate |= VER_INFO_RCVD;

		/* save peer's requested values */
		ldcp->peer_hparams.ver_major = vermsg->ver_major;
		ldcp->peer_hparams.ver_minor = vermsg->ver_minor;
		ldcp->peer_hparams.dev_class = vermsg->dev_class;

		if ((vermsg->dev_class != VDEV_NETWORK) &&
		    (vermsg->dev_class != VDEV_NETWORK_SWITCH)) {
			/* unsupported dev_class, send NACK */

			DWARN(vgenp, ldcp, "Version Negotiation Failed\n");

			tagp->vio_subtype = VIO_SUBTYPE_NACK;
			tagp->vio_sid = ldcp->local_sid;
			/* send reply msg back to peer */
			rv = vgen_sendmsg(ldcp, (caddr_t)tagp,
			    sizeof (*vermsg), B_FALSE);
			if (rv != VGEN_SUCCESS) {
				return (rv);
			}
			return (VGEN_FAILURE);
		}

		DBG2(vgenp, ldcp, "VER_INFO_RCVD, ver(%d,%d)\n",
		    vermsg->ver_major,  vermsg->ver_minor);

		idx = 0;

		for (;;) {

			if (vermsg->ver_major > versions[idx].ver_major) {

				/* nack with next lower version */
				tagp->vio_subtype = VIO_SUBTYPE_NACK;
				vermsg->ver_major = versions[idx].ver_major;
				vermsg->ver_minor = versions[idx].ver_minor;
				break;
			}

			if (vermsg->ver_major == versions[idx].ver_major) {

				/* major version match - ACK version */
				tagp->vio_subtype = VIO_SUBTYPE_ACK;
				ack = 1;

				/*
				 * lower minor version to the one this endpt
				 * supports, if necessary
				 */
				if (vermsg->ver_minor >
				    versions[idx].ver_minor) {
					vermsg->ver_minor =
					    versions[idx].ver_minor;
					ldcp->peer_hparams.ver_minor =
					    versions[idx].ver_minor;
				}
				break;
			}

			idx++;

			if (idx == VGEN_NUM_VER) {

				/* no version match - send NACK */
				tagp->vio_subtype = VIO_SUBTYPE_NACK;
				vermsg->ver_major = 0;
				vermsg->ver_minor = 0;
				failed = 1;
				break;
			}

		}

		tagp->vio_sid = ldcp->local_sid;

		/* send reply msg back to peer */
		rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (*vermsg),
		    B_FALSE);
		if (rv != VGEN_SUCCESS) {
			return (rv);
		}

		if (ack) {
			ldcp->hstate |= VER_ACK_SENT;
			DBG2(vgenp, ldcp, "VER_ACK_SENT, ver(%d,%d) \n",
			    vermsg->ver_major, vermsg->ver_minor);
		}
		if (failed) {
			DWARN(vgenp, ldcp, "Negotiation Failed\n");
			return (VGEN_FAILURE);
		}
		if (vgen_handshake_done(ldcp) == VGEN_SUCCESS) {

			/*  VER_ACK_SENT and VER_ACK_RCVD */

			/* local and peer versions match? */
			ASSERT((ldcp->local_hparams.ver_major ==
			    ldcp->peer_hparams.ver_major) &&
			    (ldcp->local_hparams.ver_minor ==
			    ldcp->peer_hparams.ver_minor));

			vgen_set_vnet_proto_ops(ldcp);

			/* move to the next phase */
			vgen_handshake(vh_nextphase(ldcp));
		}

		break;

	case VIO_SUBTYPE_ACK:

		if (ldcp->hphase != VH_PHASE1) {
			/*  This should not happen. */
			DWARN(vgenp, ldcp, "Invalid Phase(%u)\n", ldcp->hphase);
			return (VGEN_FAILURE);
		}

		/* SUCCESS - we have agreed on a version */
		ldcp->local_hparams.ver_major = vermsg->ver_major;
		ldcp->local_hparams.ver_minor = vermsg->ver_minor;
		ldcp->hstate |= VER_ACK_RCVD;

		DBG2(vgenp, ldcp, "VER_ACK_RCVD, ver(%d,%d) \n",
		    vermsg->ver_major,  vermsg->ver_minor);

		if (vgen_handshake_done(ldcp) == VGEN_SUCCESS) {

			/*  VER_ACK_SENT and VER_ACK_RCVD */

			/* local and peer versions match? */
			ASSERT((ldcp->local_hparams.ver_major ==
			    ldcp->peer_hparams.ver_major) &&
			    (ldcp->local_hparams.ver_minor ==
			    ldcp->peer_hparams.ver_minor));

			vgen_set_vnet_proto_ops(ldcp);

			/* move to the next phase */
			vgen_handshake(vh_nextphase(ldcp));
		}
		break;

	case VIO_SUBTYPE_NACK:

		if (ldcp->hphase != VH_PHASE1) {
			/*  This should not happen.  */
			DWARN(vgenp, ldcp, "VER_NACK_RCVD Invalid "
			"Phase(%u)\n", ldcp->hphase);
			return (VGEN_FAILURE);
		}

		DBG2(vgenp, ldcp, "VER_NACK_RCVD next ver(%d,%d)\n",
		    vermsg->ver_major, vermsg->ver_minor);

		/* check if version in NACK is zero */
		if (vermsg->ver_major == 0 && vermsg->ver_minor == 0) {
			/*
			 * Version Negotiation has failed.
			 */
			DWARN(vgenp, ldcp, "Version Negotiation Failed\n");
			return (VGEN_FAILURE);
		}

		idx = 0;

		for (;;) {

			if (vermsg->ver_major > versions[idx].ver_major) {
				/* select next lower version */

				ldcp->local_hparams.ver_major =
				    versions[idx].ver_major;
				ldcp->local_hparams.ver_minor =
				    versions[idx].ver_minor;
				break;
			}

			if (vermsg->ver_major == versions[idx].ver_major) {
				/* major version match */

				ldcp->local_hparams.ver_major =
				    versions[idx].ver_major;

				ldcp->local_hparams.ver_minor =
				    versions[idx].ver_minor;
				break;
			}

			idx++;

			if (idx == VGEN_NUM_VER) {
				/*
				 * no version match.
				 * Version Negotiation has failed.
				 */
				DWARN(vgenp, ldcp,
				    "Version Negotiation Failed\n");
				return (VGEN_FAILURE);
			}

		}

		rv = vgen_send_version_negotiate(ldcp);
		if (rv != VGEN_SUCCESS) {
			return (rv);
		}

		break;
	}

	DBG1(vgenp, ldcp, "exit\n");
	return (VGEN_SUCCESS);
}

/* Check if the attributes are supported */
static int
vgen_check_attr_info(vgen_ldc_t *ldcp, vnet_attr_msg_t *msg)
{
	vgen_hparams_t	*lp = &ldcp->local_hparams;

	if ((msg->addr_type != ADDR_TYPE_MAC) ||
	    (msg->ack_freq > 64) ||
	    (msg->xfer_mode != lp->xfer_mode)) {
		return (VGEN_FAILURE);
	}

	if (VGEN_VER_LT(ldcp, 1, 4)) {
		/* versions < 1.4, mtu must match */
		if (msg->mtu != lp->mtu) {
			return (VGEN_FAILURE);
		}
	} else {
		/* Ver >= 1.4, validate mtu of the peer is at least ETHERMAX */
		if (msg->mtu < ETHERMAX) {
			return (VGEN_FAILURE);
		}
	}

	return (VGEN_SUCCESS);
}

/*
 * Handle an attribute info msg from the peer or an ACK/NACK from the peer
 * to an attr info msg that we sent.
 */
static int
vgen_handle_attr_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);
	vnet_attr_msg_t	*msg = (vnet_attr_msg_t *)tagp;
	vgen_hparams_t	*lp = &ldcp->local_hparams;
	vgen_hparams_t	*rp = &ldcp->peer_hparams;
	int		ack = 1;
	int		rv = 0;
	uint32_t	mtu;

	DBG1(vgenp, ldcp, "enter\n");
	if (ldcp->hphase != VH_PHASE2) {
		DWARN(vgenp, ldcp, "Rcvd ATTR_INFO subtype(%d),"
		" Invalid Phase(%u)\n",
		    tagp->vio_subtype, ldcp->hphase);
		return (VGEN_FAILURE);
	}
	switch (tagp->vio_subtype) {
	case VIO_SUBTYPE_INFO:

		DBG2(vgenp, ldcp, "ATTR_INFO_RCVD \n");
		ldcp->hstate |= ATTR_INFO_RCVD;

		/* save peer's values */
		rp->mtu = msg->mtu;
		rp->addr = msg->addr;
		rp->addr_type = msg->addr_type;
		rp->xfer_mode = msg->xfer_mode;
		rp->ack_freq = msg->ack_freq;

		rv = vgen_check_attr_info(ldcp, msg);
		if (rv == VGEN_FAILURE) {
			/* unsupported attr, send NACK */
			ack = 0;
		} else {

			if (VGEN_VER_GTEQ(ldcp, 1, 4)) {

				/*
				 * Versions >= 1.4:
				 * The mtu is negotiated down to the
				 * minimum of our mtu and peer's mtu.
				 */
				mtu = MIN(msg->mtu, vgenp->max_frame_size);

				/*
				 * If we have received an ack for the attr info
				 * that we sent, then check if the mtu computed
				 * above matches the mtu that the peer had ack'd
				 * (saved in local hparams). If they don't
				 * match, we fail the handshake.
				 */
				if (ldcp->hstate & ATTR_ACK_RCVD) {
					if (mtu != lp->mtu) {
						/* send NACK */
						ack = 0;
					}
				} else {
					/*
					 * Save the mtu computed above in our
					 * attr parameters, so it gets sent in
					 * the attr info from us to the peer.
					 */
					lp->mtu = mtu;
				}

				/* save the MIN mtu in the msg to be replied */
				msg->mtu = mtu;

			}
		}


		if (ack) {
			tagp->vio_subtype = VIO_SUBTYPE_ACK;
		} else {
			tagp->vio_subtype = VIO_SUBTYPE_NACK;
		}
		tagp->vio_sid = ldcp->local_sid;

		/* send reply msg back to peer */
		rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (*msg),
		    B_FALSE);
		if (rv != VGEN_SUCCESS) {
			return (rv);
		}

		if (ack) {
			ldcp->hstate |= ATTR_ACK_SENT;
			DBG2(vgenp, ldcp, "ATTR_ACK_SENT \n");
		} else {
			/* failed */
			DWARN(vgenp, ldcp, "ATTR_NACK_SENT \n");
			return (VGEN_FAILURE);
		}

		if (vgen_handshake_done(ldcp) == VGEN_SUCCESS) {
			vgen_handshake(vh_nextphase(ldcp));
		}

		break;

	case VIO_SUBTYPE_ACK:

		if (VGEN_VER_GTEQ(ldcp, 1, 5) &&
		    ldcp->portp == vgenp->vsw_portp) {
			/*
			 * Versions >= 1.5:
			 * If the vnet device has been configured to get
			 * physical link state updates, check the corresponding
			 * bits in the ack msg, if the peer is vswitch.
			 */
			if (((lp->physlink_update &
			    PHYSLINK_UPDATE_STATE_MASK) ==
			    PHYSLINK_UPDATE_STATE) &&

			    ((msg->physlink_update &
			    PHYSLINK_UPDATE_STATE_MASK) ==
			    PHYSLINK_UPDATE_STATE_ACK)) {
				vgenp->pls_negotiated = B_TRUE;
			} else {
				vgenp->pls_negotiated = B_FALSE;
			}
		}

		if (VGEN_VER_GTEQ(ldcp, 1, 4)) {
			/*
			 * Versions >= 1.4:
			 * The ack msg sent by the peer contains the minimum of
			 * our mtu (that we had sent in our attr info) and the
			 * peer's mtu.
			 *
			 * If we have sent an ack for the attr info msg from
			 * the peer, check if the mtu that was computed then
			 * (saved in local hparams) matches the mtu that the
			 * peer has ack'd. If they don't match, we fail the
			 * handshake.
			 */
			if (ldcp->hstate & ATTR_ACK_SENT) {
				if (lp->mtu != msg->mtu) {
					return (VGEN_FAILURE);
				}
			} else {
				/*
				 * If the mtu ack'd by the peer is > our mtu
				 * fail handshake. Otherwise, save the mtu, so
				 * we can validate it when we receive attr info
				 * from our peer.
				 */
				if (msg->mtu > lp->mtu) {
					return (VGEN_FAILURE);
				}
				if (msg->mtu <= lp->mtu) {
					lp->mtu = msg->mtu;
				}
			}
		}

		ldcp->hstate |= ATTR_ACK_RCVD;

		DBG2(vgenp, ldcp, "ATTR_ACK_RCVD \n");

		if (vgen_handshake_done(ldcp) == VGEN_SUCCESS) {
			vgen_handshake(vh_nextphase(ldcp));
		}
		break;

	case VIO_SUBTYPE_NACK:

		DBG2(vgenp, ldcp, "ATTR_NACK_RCVD \n");
		return (VGEN_FAILURE);
	}
	DBG1(vgenp, ldcp, "exit\n");
	return (VGEN_SUCCESS);
}

/* Check if the dring info msg is ok */
static int
vgen_check_dring_reg(vio_dring_reg_msg_t *msg)
{
	/* check if msg contents are ok */
	if ((msg->num_descriptors < 128) || (msg->descriptor_size <
	    sizeof (vnet_public_desc_t))) {
		return (VGEN_FAILURE);
	}
	return (VGEN_SUCCESS);
}

/*
 * Handle a descriptor ring register msg from the peer or an ACK/NACK from
 * the peer to a dring register msg that we sent.
 */
static int
vgen_handle_dring_reg(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	vio_dring_reg_msg_t *msg = (vio_dring_reg_msg_t *)tagp;
	ldc_mem_cookie_t dcookie;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	int ack = 0;
	int rv = 0;

	DBG1(vgenp, ldcp, "enter\n");
	if (ldcp->hphase < VH_PHASE2) {
		/* dring_info can be rcvd in any of the phases after Phase1 */
		DWARN(vgenp, ldcp,
		    "Rcvd DRING_INFO Subtype (%d), Invalid Phase(%u)\n",
		    tagp->vio_subtype, ldcp->hphase);
		return (VGEN_FAILURE);
	}
	switch (tagp->vio_subtype) {
	case VIO_SUBTYPE_INFO:

		DBG2(vgenp, ldcp, "DRING_INFO_RCVD \n");
		ldcp->hstate |= DRING_INFO_RCVD;
		bcopy((msg->cookie), &dcookie, sizeof (dcookie));

		ASSERT(msg->ncookies == 1);

		if (vgen_check_dring_reg(msg) == VGEN_SUCCESS) {
			/*
			 * verified dring info msg to be ok,
			 * now try to map the remote dring.
			 */
			rv = vgen_init_rxds(ldcp, msg->num_descriptors,
			    msg->descriptor_size, &dcookie,
			    msg->ncookies);
			if (rv == DDI_SUCCESS) {
				/* now we can ack the peer */
				ack = 1;
			}
		}
		if (ack == 0) {
			/* failed, send NACK */
			tagp->vio_subtype = VIO_SUBTYPE_NACK;
		} else {
			if (!(ldcp->peer_hparams.dring_ready)) {

				/* save peer's dring_info values */
				bcopy(&dcookie,
				    &(ldcp->peer_hparams.dring_cookie),
				    sizeof (dcookie));
				ldcp->peer_hparams.num_desc =
				    msg->num_descriptors;
				ldcp->peer_hparams.desc_size =
				    msg->descriptor_size;
				ldcp->peer_hparams.num_dcookies =
				    msg->ncookies;

				/* set dring_ident for the peer */
				ldcp->peer_hparams.dring_ident =
				    (uint64_t)ldcp->rxdp;
				/* return the dring_ident in ack msg */
				msg->dring_ident =
				    (uint64_t)ldcp->rxdp;

				ldcp->peer_hparams.dring_ready = B_TRUE;
			}
			tagp->vio_subtype = VIO_SUBTYPE_ACK;
		}
		tagp->vio_sid = ldcp->local_sid;
		/* send reply msg back to peer */
		rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (*msg),
		    B_FALSE);
		if (rv != VGEN_SUCCESS) {
			return (rv);
		}

		if (ack) {
			ldcp->hstate |= DRING_ACK_SENT;
			DBG2(vgenp, ldcp, "DRING_ACK_SENT");
		} else {
			DWARN(vgenp, ldcp, "DRING_NACK_SENT");
			return (VGEN_FAILURE);
		}

		if (vgen_handshake_done(ldcp) == VGEN_SUCCESS) {
			vgen_handshake(vh_nextphase(ldcp));
		}

		break;

	case VIO_SUBTYPE_ACK:

		ldcp->hstate |= DRING_ACK_RCVD;

		DBG2(vgenp, ldcp, "DRING_ACK_RCVD");

		if (!(ldcp->local_hparams.dring_ready)) {
			/* local dring is now ready */
			ldcp->local_hparams.dring_ready = B_TRUE;

			/* save dring_ident acked by peer */
			ldcp->local_hparams.dring_ident =
			    msg->dring_ident;
		}

		if (vgen_handshake_done(ldcp) == VGEN_SUCCESS) {
			vgen_handshake(vh_nextphase(ldcp));
		}

		break;

	case VIO_SUBTYPE_NACK:

		DBG2(vgenp, ldcp, "DRING_NACK_RCVD");
		return (VGEN_FAILURE);
	}
	DBG1(vgenp, ldcp, "exit\n");
	return (VGEN_SUCCESS);
}

/*
 * Handle a rdx info msg from the peer or an ACK/NACK
 * from the peer to a rdx info msg that we sent.
 */
static int
vgen_handle_rdx_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	int rv = 0;
	vgen_t	*vgenp = LDC_TO_VGEN(ldcp);

	DBG1(vgenp, ldcp, "enter\n");
	if (ldcp->hphase != VH_PHASE3) {
		DWARN(vgenp, ldcp,
		    "Rcvd RDX_INFO Subtype (%d), Invalid Phase(%u)\n",
		    tagp->vio_subtype, ldcp->hphase);
		return (VGEN_FAILURE);
	}
	switch (tagp->vio_subtype) {
	case VIO_SUBTYPE_INFO:

		DBG2(vgenp, ldcp, "RDX_INFO_RCVD \n");
		ldcp->hstate |= RDX_INFO_RCVD;

		tagp->vio_subtype = VIO_SUBTYPE_ACK;
		tagp->vio_sid = ldcp->local_sid;
		/* send reply msg back to peer */
		rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (vio_rdx_msg_t),
		    B_FALSE);
		if (rv != VGEN_SUCCESS) {
			return (rv);
		}

		ldcp->hstate |= RDX_ACK_SENT;
		DBG2(vgenp, ldcp, "RDX_ACK_SENT \n");

		if (vgen_handshake_done(ldcp) == VGEN_SUCCESS) {
			vgen_handshake(vh_nextphase(ldcp));
		}

		break;

	case VIO_SUBTYPE_ACK:

		ldcp->hstate |= RDX_ACK_RCVD;

		DBG2(vgenp, ldcp, "RDX_ACK_RCVD \n");

		if (vgen_handshake_done(ldcp) == VGEN_SUCCESS) {
			vgen_handshake(vh_nextphase(ldcp));
		}
		break;

	case VIO_SUBTYPE_NACK:

		DBG2(vgenp, ldcp, "RDX_NACK_RCVD \n");
		return (VGEN_FAILURE);
	}
	DBG1(vgenp, ldcp, "exit\n");
	return (VGEN_SUCCESS);
}

/* Handle ACK/NACK from vsw to a set multicast msg that we sent */
static int
vgen_handle_mcast_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	vnet_mcast_msg_t *msgp = (vnet_mcast_msg_t *)tagp;
	struct ether_addr *addrp;
	int count;
	int i;

	DBG1(vgenp, ldcp, "enter\n");
	switch (tagp->vio_subtype) {

	case VIO_SUBTYPE_INFO:

		/* vnet shouldn't recv set mcast msg, only vsw handles it */
		DWARN(vgenp, ldcp, "rcvd SET_MCAST_INFO \n");
		break;

	case VIO_SUBTYPE_ACK:

		/* success adding/removing multicast addr */
		DBG1(vgenp, ldcp, "rcvd SET_MCAST_ACK \n");
		break;

	case VIO_SUBTYPE_NACK:

		DWARN(vgenp, ldcp, "rcvd SET_MCAST_NACK \n");
		if (!(msgp->set)) {
			/* multicast remove request failed */
			break;
		}

		/* multicast add request failed */
		for (count = 0; count < msgp->count; count++) {
			addrp = &(msgp->mca[count]);

			/* delete address from the table */
			for (i = 0; i < vgenp->mccount; i++) {
				if (ether_cmp(addrp,
				    &(vgenp->mctab[i])) == 0) {
					if (vgenp->mccount > 1) {
						int t = vgenp->mccount - 1;
						vgenp->mctab[i] =
						    vgenp->mctab[t];
					}
					vgenp->mccount--;
					break;
				}
			}
		}
		break;

	}
	DBG1(vgenp, ldcp, "exit\n");

	return (VGEN_SUCCESS);
}

/*
 * Physical link information message from the peer. Only vswitch should send
 * us this message; if the vnet device has been configured to get physical link
 * state updates. Note that we must have already negotiated this with the
 * vswitch during attribute exchange phase of handshake.
 */
static int
vgen_handle_physlink_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	vgen_t			*vgenp = LDC_TO_VGEN(ldcp);
	vnet_physlink_msg_t	*msgp = (vnet_physlink_msg_t *)tagp;
	link_state_t		link_state;
	int			rv;

	if (ldcp->portp != vgenp->vsw_portp) {
		/*
		 * drop the message and don't process; as we should
		 * receive physlink_info message from only vswitch.
		 */
		return (VGEN_SUCCESS);
	}

	if (vgenp->pls_negotiated == B_FALSE) {
		/*
		 * drop the message and don't process; as we should receive
		 * physlink_info message only if physlink update is enabled for
		 * the device and negotiated with vswitch.
		 */
		return (VGEN_SUCCESS);
	}

	switch (tagp->vio_subtype) {

	case VIO_SUBTYPE_INFO:

		if ((msgp->physlink_info & VNET_PHYSLINK_STATE_MASK) ==
		    VNET_PHYSLINK_STATE_UP) {
			link_state = LINK_STATE_UP;
		} else {
			link_state = LINK_STATE_DOWN;
		}

		if (vgenp->phys_link_state != link_state) {
			vgenp->phys_link_state = link_state;
			mutex_exit(&ldcp->cblock);

			/* Now update the stack */
			vgen_link_update(vgenp, link_state);

			mutex_enter(&ldcp->cblock);
		}

		tagp->vio_subtype = VIO_SUBTYPE_ACK;
		tagp->vio_sid = ldcp->local_sid;

		/* send reply msg back to peer */
		rv = vgen_sendmsg(ldcp, (caddr_t)tagp,
		    sizeof (vnet_physlink_msg_t), B_FALSE);
		if (rv != VGEN_SUCCESS) {
			return (rv);
		}
		break;

	case VIO_SUBTYPE_ACK:

		/* vnet shouldn't recv physlink acks */
		DWARN(vgenp, ldcp, "rcvd PHYSLINK_ACK \n");
		break;

	case VIO_SUBTYPE_NACK:

		/* vnet shouldn't recv physlink nacks */
		DWARN(vgenp, ldcp, "rcvd PHYSLINK_NACK \n");
		break;

	}
	DBG1(vgenp, ldcp, "exit\n");

	return (VGEN_SUCCESS);
}

/* handler for control messages received from the peer ldc end-point */
static int
vgen_handle_ctrlmsg(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	int rv = 0;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	DBG1(vgenp, ldcp, "enter\n");
	switch (tagp->vio_subtype_env) {

	case VIO_VER_INFO:
		rv = vgen_handle_version_negotiate(ldcp, tagp);
		break;

	case VIO_ATTR_INFO:
		rv = vgen_handle_attr_info(ldcp, tagp);
		break;

	case VIO_DRING_REG:
		rv = vgen_handle_dring_reg(ldcp, tagp);
		break;

	case VIO_RDX:
		rv = vgen_handle_rdx_info(ldcp, tagp);
		break;

	case VNET_MCAST_INFO:
		rv = vgen_handle_mcast_info(ldcp, tagp);
		break;

	case VIO_DDS_INFO:
		/*
		 * If we are in the process of resetting the vswitch channel,
		 * drop the dds message. A new handshake will be initiated
		 * when the channel comes back up after the reset and dds
		 * negotiation can then continue.
		 */
		if (ldcp->need_ldc_reset == B_TRUE) {
			break;
		}
		rv = vgen_dds_rx(ldcp, tagp);
		break;

	case VNET_PHYSLINK_INFO:
		rv = vgen_handle_physlink_info(ldcp, tagp);
		break;
	}

	DBG1(vgenp, ldcp, "exit rv(%d)\n", rv);
	return (rv);
}

/* handler for data messages received from the peer ldc end-point */
static int
vgen_handle_datamsg(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp, uint32_t msglen)
{
	int rv = 0;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	DBG1(vgenp, ldcp, "enter\n");

	if (ldcp->hphase != VH_DONE)
		return (rv);

	if (tagp->vio_subtype == VIO_SUBTYPE_INFO) {
		rv = vgen_check_datamsg_seq(ldcp, tagp);
		if (rv != 0) {
			return (rv);
		}
	}

	switch (tagp->vio_subtype_env) {
	case VIO_DRING_DATA:
		rv = vgen_handle_dring_data(ldcp, tagp);
		break;

	case VIO_PKT_DATA:
		ldcp->rx_pktdata((void *)ldcp, (void *)tagp, msglen);
		break;
	default:
		break;
	}

	DBG1(vgenp, ldcp, "exit rv(%d)\n", rv);
	return (rv);
}

/*
 * dummy pkt data handler function for vnet protocol version 1.0
 */
static void
vgen_handle_pkt_data_nop(void *arg1, void *arg2, uint32_t msglen)
{
	_NOTE(ARGUNUSED(arg1, arg2, msglen))
}

/*
 * This function handles raw pkt data messages received over the channel.
 * Currently, only priority-eth-type frames are received through this mechanism.
 * In this case, the frame(data) is present within the message itself which
 * is copied into an mblk before sending it up the stack.
 */
static void
vgen_handle_pkt_data(void *arg1, void *arg2, uint32_t msglen)
{
	vgen_ldc_t		*ldcp = (vgen_ldc_t *)arg1;
	vio_raw_data_msg_t	*pkt	= (vio_raw_data_msg_t *)arg2;
	uint32_t		size;
	mblk_t			*mp;
	vgen_t			*vgenp = LDC_TO_VGEN(ldcp);
	vgen_stats_t		*statsp = &ldcp->stats;
	vgen_hparams_t		*lp = &ldcp->local_hparams;
	vio_net_rx_cb_t		vrx_cb;

	ASSERT(MUTEX_HELD(&ldcp->cblock));

	mutex_exit(&ldcp->cblock);

	size = msglen - VIO_PKT_DATA_HDRSIZE;
	if (size < ETHERMIN || size > lp->mtu) {
		(void) atomic_inc_32(&statsp->rx_pri_fail);
		goto exit;
	}

	mp = vio_multipool_allocb(&ldcp->vmp, size);
	if (mp == NULL) {
		mp = allocb(size, BPRI_MED);
		if (mp == NULL) {
			(void) atomic_inc_32(&statsp->rx_pri_fail);
			DWARN(vgenp, ldcp, "allocb failure, "
			    "unable to process priority frame\n");
			goto exit;
		}
	}

	/* copy the frame from the payload of raw data msg into the mblk */
	bcopy(pkt->data, mp->b_rptr, size);
	mp->b_wptr = mp->b_rptr + size;

	/* update stats */
	(void) atomic_inc_64(&statsp->rx_pri_packets);
	(void) atomic_add_64(&statsp->rx_pri_bytes, size);

	/* send up; call vrx_cb() as cblock is already released */
	vrx_cb = ldcp->portp->vcb.vio_net_rx_cb;
	vrx_cb(ldcp->portp->vhp, mp);

exit:
	mutex_enter(&ldcp->cblock);
}

static int
vgen_send_dring_ack(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp, uint32_t start,
    int32_t end, uint8_t pstate)
{
	int rv = 0;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	vio_dring_msg_t *msgp = (vio_dring_msg_t *)tagp;

	tagp->vio_subtype = VIO_SUBTYPE_ACK;
	tagp->vio_sid = ldcp->local_sid;
	msgp->start_idx = start;
	msgp->end_idx = end;
	msgp->dring_process_state = pstate;
	rv = vgen_sendmsg(ldcp, (caddr_t)tagp, sizeof (*msgp), B_FALSE);
	if (rv != VGEN_SUCCESS) {
		DWARN(vgenp, ldcp, "vgen_sendmsg failed\n");
	}
	return (rv);
}

static int
vgen_handle_dring_data(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	int rv = 0;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);


	DBG1(vgenp, ldcp, "enter\n");
	switch (tagp->vio_subtype) {

	case VIO_SUBTYPE_INFO:
		/*
		 * To reduce the locking contention, release the
		 * cblock here and re-acquire it once we are done
		 * receiving packets.
		 */
		mutex_exit(&ldcp->cblock);
		mutex_enter(&ldcp->rxlock);
		rv = vgen_handle_dring_data_info(ldcp, tagp);
		mutex_exit(&ldcp->rxlock);
		mutex_enter(&ldcp->cblock);
		break;

	case VIO_SUBTYPE_ACK:
		rv = vgen_handle_dring_data_ack(ldcp, tagp);
		break;

	case VIO_SUBTYPE_NACK:
		rv = vgen_handle_dring_data_nack(ldcp, tagp);
		break;
	}
	DBG1(vgenp, ldcp, "exit rv(%d)\n", rv);
	return (rv);
}

static int
vgen_handle_dring_data_info(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	uint32_t start;
	int32_t end;
	int rv = 0;
	vio_dring_msg_t *dringmsg = (vio_dring_msg_t *)tagp;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
#ifdef VGEN_HANDLE_LOST_PKTS
	vgen_stats_t *statsp = &ldcp->stats;
	uint32_t rxi;
	int n;
#endif

	DBG1(vgenp, ldcp, "enter\n");

	start = dringmsg->start_idx;
	end = dringmsg->end_idx;
	/*
	 * received a data msg, which contains the start and end
	 * indices of the descriptors within the rx ring holding data,
	 * the seq_num of data packet corresponding to the start index,
	 * and the dring_ident.
	 * We can now read the contents of each of these descriptors
	 * and gather data from it.
	 */
	DBG1(vgenp, ldcp, "INFO: start(%d), end(%d)\n",
	    start, end);

	/* validate rx start and end indeces */
	if (!(CHECK_RXI(start, ldcp)) || ((end != -1) &&
	    !(CHECK_RXI(end, ldcp)))) {
		DWARN(vgenp, ldcp, "Invalid Rx start(%d) or end(%d)\n",
		    start, end);
		/* drop the message if invalid index */
		return (rv);
	}

	/* validate dring_ident */
	if (dringmsg->dring_ident != ldcp->peer_hparams.dring_ident) {
		DWARN(vgenp, ldcp, "Invalid dring ident 0x%x\n",
		    dringmsg->dring_ident);
		/* invalid dring_ident, drop the msg */
		return (rv);
	}
#ifdef DEBUG
	if (vgen_trigger_rxlost) {
		/* drop this msg to simulate lost pkts for debugging */
		vgen_trigger_rxlost = 0;
		return (rv);
	}
#endif

#ifdef	VGEN_HANDLE_LOST_PKTS

	/* receive start index doesn't match expected index */
	if (ldcp->next_rxi != start) {
		DWARN(vgenp, ldcp, "next_rxi(%d) != start(%d)\n",
		    ldcp->next_rxi, start);

		/* calculate the number of pkts lost */
		if (start >= ldcp->next_rxi) {
			n = start - ldcp->next_rxi;
		} else  {
			n = ldcp->num_rxds - (ldcp->next_rxi - start);
		}

		statsp->rx_lost_pkts += n;
		tagp->vio_subtype = VIO_SUBTYPE_NACK;
		tagp->vio_sid = ldcp->local_sid;
		/* indicate the range of lost descriptors */
		dringmsg->start_idx = ldcp->next_rxi;
		rxi = start;
		DECR_RXI(rxi, ldcp);
		dringmsg->end_idx = rxi;
		/* dring ident is left unchanged */
		rv = vgen_sendmsg(ldcp, (caddr_t)tagp,
		    sizeof (*dringmsg), B_FALSE);
		if (rv != VGEN_SUCCESS) {
			DWARN(vgenp, ldcp,
			    "vgen_sendmsg failed, stype:NACK\n");
			return (rv);
		}
		/*
		 * treat this range of descrs/pkts as dropped
		 * and set the new expected value of next_rxi
		 * and continue(below) to process from the new
		 * start index.
		 */
		ldcp->next_rxi = start;
	}

#endif	/* VGEN_HANDLE_LOST_PKTS */

	/* Now receive messages */
	rv = vgen_process_dring_data(ldcp, tagp);

	DBG1(vgenp, ldcp, "exit rv(%d)\n", rv);
	return (rv);
}

static int
vgen_process_dring_data(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	boolean_t set_ack_start = B_FALSE;
	uint32_t start;
	uint32_t ack_end;
	uint32_t next_rxi;
	uint32_t rxi;
	int count = 0;
	int rv = 0;
	uint32_t retries = 0;
	vgen_stats_t *statsp;
	vnet_public_desc_t rxd;
	vio_dring_entry_hdr_t *hdrp;
	mblk_t *bp = NULL;
	mblk_t *bpt = NULL;
	uint32_t ack_start;
	boolean_t rxd_err = B_FALSE;
	mblk_t *mp = NULL;
	size_t nbytes;
	boolean_t ack_needed = B_FALSE;
	size_t nread;
	uint64_t off = 0;
	struct ether_header *ehp;
	vio_dring_msg_t *dringmsg = (vio_dring_msg_t *)tagp;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	vgen_hparams_t	*lp = &ldcp->local_hparams;

	DBG1(vgenp, ldcp, "enter\n");

	statsp = &ldcp->stats;
	start = dringmsg->start_idx;

	/*
	 * start processing the descriptors from the specified
	 * start index, up to the index a descriptor is not ready
	 * to be processed or we process the entire descriptor ring
	 * and wrap around upto the start index.
	 */

	/* need to set the start index of descriptors to be ack'd */
	set_ack_start = B_TRUE;

	/* index upto which we have ack'd */
	ack_end = start;
	DECR_RXI(ack_end, ldcp);

	next_rxi = rxi =  start;
	do {
vgen_recv_retry:
		rv = vnet_dring_entry_copy(&(ldcp->rxdp[rxi]), &rxd,
		    ldcp->dring_mtype, ldcp->rx_dhandle, rxi, rxi);
		if (rv != 0) {
			DWARN(vgenp, ldcp, "ldc_mem_dring_acquire() failed"
			    " rv(%d)\n", rv);
			statsp->ierrors++;
			return (rv);
		}

		hdrp = &rxd.hdr;

		if (hdrp->dstate != VIO_DESC_READY) {
			/*
			 * Before waiting and retry here, send up
			 * the packets that are received already
			 */
			if (bp != NULL) {
				DTRACE_PROBE1(vgen_rcv_msgs, int, count);
				vgen_rx(ldcp, bp, bpt);
				count = 0;
				bp = bpt = NULL;
			}
			/*
			 * descriptor is not ready.
			 * retry descriptor acquire, stop processing
			 * after max # retries.
			 */
			if (retries == vgen_recv_retries)
				break;
			retries++;
			drv_usecwait(vgen_recv_delay);
			goto vgen_recv_retry;
		}
		retries = 0;

		if (set_ack_start) {
			/*
			 * initialize the start index of the range
			 * of descriptors to be ack'd.
			 */
			ack_start = rxi;
			set_ack_start = B_FALSE;
		}

		if ((rxd.nbytes < ETHERMIN) ||
		    (rxd.nbytes > lp->mtu) ||
		    (rxd.ncookies == 0) ||
		    (rxd.ncookies > MAX_COOKIES)) {
			rxd_err = B_TRUE;
		} else {
			/*
			 * Try to allocate an mblk from the free pool
			 * of recv mblks for the channel.
			 * If this fails, use allocb().
			 */
			nbytes = (VNET_IPALIGN + rxd.nbytes + 7) & ~7;
			if (nbytes > ldcp->max_rxpool_size) {
				mp = allocb(VNET_IPALIGN + rxd.nbytes + 8,
				    BPRI_MED);
			} else {
				mp = vio_multipool_allocb(&ldcp->vmp, nbytes);
				if (mp == NULL) {
					statsp->rx_vio_allocb_fail++;
					/*
					 * Data buffer returned by allocb(9F)
					 * is 8byte aligned. We allocate extra
					 * 8 bytes to ensure size is multiple
					 * of 8 bytes for ldc_mem_copy().
					 */
					mp = allocb(VNET_IPALIGN +
					    rxd.nbytes + 8, BPRI_MED);
				}
			}
		}
		if ((rxd_err) || (mp == NULL)) {
			/*
			 * rxd_err or allocb() failure,
			 * drop this packet, get next.
			 */
			if (rxd_err) {
				statsp->ierrors++;
				rxd_err = B_FALSE;
			} else {
				statsp->rx_allocb_fail++;
			}

			ack_needed = hdrp->ack;

			/* set descriptor done bit */
			rv = vnet_dring_entry_set_dstate(&(ldcp->rxdp[rxi]),
			    ldcp->dring_mtype, ldcp->rx_dhandle, rxi, rxi,
			    VIO_DESC_DONE);
			if (rv != 0) {
				DWARN(vgenp, ldcp,
				    "vnet_dring_entry_set_dstate err rv(%d)\n",
				    rv);
				return (rv);
			}

			if (ack_needed) {
				ack_needed = B_FALSE;
				/*
				 * sender needs ack for this packet,
				 * ack pkts upto this index.
				 */
				ack_end = rxi;

				rv = vgen_send_dring_ack(ldcp, tagp,
				    ack_start, ack_end,
				    VIO_DP_ACTIVE);
				if (rv != VGEN_SUCCESS) {
					goto error_ret;
				}

				/* need to set new ack start index */
				set_ack_start = B_TRUE;
			}
			goto vgen_next_rxi;
		}

		nread = nbytes;
		rv = ldc_mem_copy(ldcp->ldc_handle,
		    (caddr_t)mp->b_rptr, off, &nread,
		    rxd.memcookie, rxd.ncookies, LDC_COPY_IN);

		/* if ldc_mem_copy() failed */
		if (rv) {
			DWARN(vgenp, ldcp, "ldc_mem_copy err rv(%d)\n", rv);
			statsp->ierrors++;
			freemsg(mp);
			goto error_ret;
		}

		ack_needed = hdrp->ack;

		rv = vnet_dring_entry_set_dstate(&(ldcp->rxdp[rxi]),
		    ldcp->dring_mtype, ldcp->rx_dhandle, rxi, rxi,
		    VIO_DESC_DONE);
		if (rv != 0) {
			DWARN(vgenp, ldcp,
			    "vnet_dring_entry_set_dstate err rv(%d)\n", rv);
			goto error_ret;
		}

		mp->b_rptr += VNET_IPALIGN;

		if (ack_needed) {
			ack_needed = B_FALSE;
			/*
			 * sender needs ack for this packet,
			 * ack pkts upto this index.
			 */
			ack_end = rxi;

			rv = vgen_send_dring_ack(ldcp, tagp,
			    ack_start, ack_end, VIO_DP_ACTIVE);
			if (rv != VGEN_SUCCESS) {
				goto error_ret;
			}

			/* need to set new ack start index */
			set_ack_start = B_TRUE;
		}

		if (nread != nbytes) {
			DWARN(vgenp, ldcp,
			    "ldc_mem_copy nread(%lx), nbytes(%lx)\n",
			    nread, nbytes);
			statsp->ierrors++;
			freemsg(mp);
			goto vgen_next_rxi;
		}

		/* point to the actual end of data */
		mp->b_wptr = mp->b_rptr + rxd.nbytes;

		/* update stats */
		statsp->ipackets++;
		statsp->rbytes += rxd.nbytes;
		ehp = (struct ether_header *)mp->b_rptr;
		if (IS_BROADCAST(ehp))
			statsp->brdcstrcv++;
		else if (IS_MULTICAST(ehp))
			statsp->multircv++;

		/* build a chain of received packets */
		if (bp == NULL) {
			/* first pkt */
			bp = mp;
			bpt = bp;
			bpt->b_next = NULL;
		} else {
			mp->b_next = NULL;
			bpt->b_next = mp;
			bpt = mp;
		}

		if (count++ > vgen_chain_len) {
			DTRACE_PROBE1(vgen_rcv_msgs, int, count);
			vgen_rx(ldcp, bp, bpt);
			count = 0;
			bp = bpt = NULL;
		}

vgen_next_rxi:
		/* update end index of range of descrs to be ack'd */
		ack_end = rxi;

		/* update the next index to be processed */
		INCR_RXI(next_rxi, ldcp);
		if (next_rxi == start) {
			/*
			 * processed the entire descriptor ring upto
			 * the index at which we started.
			 */
			break;
		}

		rxi = next_rxi;

	_NOTE(CONSTCOND)
	} while (1);

	/*
	 * send an ack message to peer indicating that we have stopped
	 * processing descriptors.
	 */
	if (set_ack_start) {
		/*
		 * We have ack'd upto some index and we have not
		 * processed any descriptors beyond that index.
		 * Use the last ack'd index as both the start and
		 * end of range of descrs being ack'd.
		 * Note: This results in acking the last index twice
		 * and should be harmless.
		 */
		ack_start = ack_end;
	}

	rv = vgen_send_dring_ack(ldcp, tagp, ack_start, ack_end,
	    VIO_DP_STOPPED);
	if (rv != VGEN_SUCCESS) {
		goto error_ret;
	}

	/* save new recv index of next dring msg */
	ldcp->next_rxi = next_rxi;

error_ret:
	/* send up packets received so far */
	if (bp != NULL) {
		DTRACE_PROBE1(vgen_rcv_msgs, int, count);
		vgen_rx(ldcp, bp, bpt);
		bp = bpt = NULL;
	}
	DBG1(vgenp, ldcp, "exit rv(%d)\n", rv);
	return (rv);

}

static int
vgen_handle_dring_data_ack(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	int rv = 0;
	uint32_t start;
	int32_t end;
	uint32_t txi;
	boolean_t ready_txd = B_FALSE;
	vgen_stats_t *statsp;
	vgen_private_desc_t *tbufp;
	vnet_public_desc_t *txdp;
	vio_dring_entry_hdr_t *hdrp;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	vio_dring_msg_t *dringmsg = (vio_dring_msg_t *)tagp;

	DBG1(vgenp, ldcp, "enter\n");
	start = dringmsg->start_idx;
	end = dringmsg->end_idx;
	statsp = &ldcp->stats;

	/*
	 * received an ack corresponding to a specific descriptor for
	 * which we had set the ACK bit in the descriptor (during
	 * transmit). This enables us to reclaim descriptors.
	 */

	DBG2(vgenp, ldcp, "ACK:  start(%d), end(%d)\n", start, end);

	/* validate start and end indeces in the tx ack msg */
	if (!(CHECK_TXI(start, ldcp)) || !(CHECK_TXI(end, ldcp))) {
		/* drop the message if invalid index */
		DWARN(vgenp, ldcp, "Invalid Tx ack start(%d) or end(%d)\n",
		    start, end);
		return (rv);
	}
	/* validate dring_ident */
	if (dringmsg->dring_ident != ldcp->local_hparams.dring_ident) {
		/* invalid dring_ident, drop the msg */
		DWARN(vgenp, ldcp, "Invalid dring ident 0x%x\n",
		    dringmsg->dring_ident);
		return (rv);
	}
	statsp->dring_data_acks++;

	/* reclaim descriptors that are done */
	vgen_reclaim(ldcp);

	if (dringmsg->dring_process_state != VIO_DP_STOPPED) {
		/*
		 * receiver continued processing descriptors after
		 * sending us the ack.
		 */
		return (rv);
	}

	statsp->dring_stopped_acks++;

	/* receiver stopped processing descriptors */
	mutex_enter(&ldcp->wrlock);
	mutex_enter(&ldcp->tclock);

	/*
	 * determine if there are any pending tx descriptors
	 * ready to be processed by the receiver(peer) and if so,
	 * send a message to the peer to restart receiving.
	 */
	ready_txd = B_FALSE;

	/*
	 * using the end index of the descriptor range for which
	 * we received the ack, check if the next descriptor is
	 * ready.
	 */
	txi = end;
	INCR_TXI(txi, ldcp);
	tbufp = &ldcp->tbufp[txi];
	txdp = tbufp->descp;
	hdrp = &txdp->hdr;
	if (hdrp->dstate == VIO_DESC_READY) {
		ready_txd = B_TRUE;
	} else {
		/*
		 * descr next to the end of ack'd descr range is not
		 * ready.
		 * starting from the current reclaim index, check
		 * if any descriptor is ready.
		 */

		txi = ldcp->cur_tbufp - ldcp->tbufp;
		tbufp = &ldcp->tbufp[txi];

		txdp = tbufp->descp;
		hdrp = &txdp->hdr;
		if (hdrp->dstate == VIO_DESC_READY) {
			ready_txd = B_TRUE;
		}

	}

	if (ready_txd) {
		/*
		 * we have tx descriptor(s) ready to be
		 * processed by the receiver.
		 * send a message to the peer with the start index
		 * of ready descriptors.
		 */
		rv = vgen_send_dring_data(ldcp, txi, -1);
		if (rv != VGEN_SUCCESS) {
			ldcp->resched_peer = B_TRUE;
			ldcp->resched_peer_txi = txi;
			mutex_exit(&ldcp->tclock);
			mutex_exit(&ldcp->wrlock);
			return (rv);
		}
	} else {
		/*
		 * no ready tx descriptors. set the flag to send a
		 * message to peer when tx descriptors are ready in
		 * transmit routine.
		 */
		ldcp->resched_peer = B_TRUE;
		ldcp->resched_peer_txi = ldcp->cur_tbufp - ldcp->tbufp;
	}

	mutex_exit(&ldcp->tclock);
	mutex_exit(&ldcp->wrlock);
	DBG1(vgenp, ldcp, "exit rv(%d)\n", rv);
	return (rv);
}

static int
vgen_handle_dring_data_nack(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	int rv = 0;
	uint32_t start;
	int32_t end;
	uint32_t txi;
	vnet_public_desc_t *txdp;
	vio_dring_entry_hdr_t *hdrp;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);
	vio_dring_msg_t *dringmsg = (vio_dring_msg_t *)tagp;

	DBG1(vgenp, ldcp, "enter\n");
	start = dringmsg->start_idx;
	end = dringmsg->end_idx;

	/*
	 * peer sent a NACK msg to indicate lost packets.
	 * The start and end correspond to the range of descriptors
	 * for which the peer didn't receive a dring data msg and so
	 * didn't receive the corresponding data.
	 */
	DWARN(vgenp, ldcp, "NACK: start(%d), end(%d)\n", start, end);

	/* validate start and end indeces in the tx nack msg */
	if (!(CHECK_TXI(start, ldcp)) || !(CHECK_TXI(end, ldcp))) {
		/* drop the message if invalid index */
		DWARN(vgenp, ldcp, "Invalid Tx nack start(%d) or end(%d)\n",
		    start, end);
		return (rv);
	}
	/* validate dring_ident */
	if (dringmsg->dring_ident != ldcp->local_hparams.dring_ident) {
		/* invalid dring_ident, drop the msg */
		DWARN(vgenp, ldcp, "Invalid dring ident 0x%x\n",
		    dringmsg->dring_ident);
		return (rv);
	}
	mutex_enter(&ldcp->txlock);
	mutex_enter(&ldcp->tclock);

	if (ldcp->next_tbufp == ldcp->cur_tbufp) {
		/* no busy descriptors, bogus nack ? */
		mutex_exit(&ldcp->tclock);
		mutex_exit(&ldcp->txlock);
		return (rv);
	}

	/* we just mark the descrs as done so they can be reclaimed */
	for (txi = start; txi <= end; ) {
		txdp = &(ldcp->txdp[txi]);
		hdrp = &txdp->hdr;
		if (hdrp->dstate == VIO_DESC_READY)
			hdrp->dstate = VIO_DESC_DONE;
		INCR_TXI(txi, ldcp);
	}
	mutex_exit(&ldcp->tclock);
	mutex_exit(&ldcp->txlock);
	DBG1(vgenp, ldcp, "exit rv(%d)\n", rv);
	return (rv);
}

static void
vgen_reclaim(vgen_ldc_t *ldcp)
{
	mutex_enter(&ldcp->tclock);

	vgen_reclaim_dring(ldcp);
	ldcp->reclaim_lbolt = ddi_get_lbolt();

	mutex_exit(&ldcp->tclock);
}

/*
 * transmit reclaim function. starting from the current reclaim index
 * look for descriptors marked DONE and reclaim the descriptor and the
 * corresponding buffers (tbuf).
 */
static void
vgen_reclaim_dring(vgen_ldc_t *ldcp)
{
	int count = 0;
	vnet_public_desc_t *txdp;
	vgen_private_desc_t *tbufp;
	vio_dring_entry_hdr_t	*hdrp;

#ifdef DEBUG
	if (vgen_trigger_txtimeout)
		return;
#endif

	tbufp = ldcp->cur_tbufp;
	txdp = tbufp->descp;
	hdrp = &txdp->hdr;

	while ((hdrp->dstate == VIO_DESC_DONE) &&
	    (tbufp != ldcp->next_tbufp)) {
		tbufp->flags = VGEN_PRIV_DESC_FREE;
		hdrp->dstate = VIO_DESC_FREE;
		hdrp->ack = B_FALSE;

		tbufp = NEXTTBUF(ldcp, tbufp);
		txdp = tbufp->descp;
		hdrp = &txdp->hdr;
		count++;
	}

	ldcp->cur_tbufp = tbufp;

	/*
	 * Check if mac layer should be notified to restart transmissions
	 */
	if ((ldcp->need_resched) && (count > 0)) {
		vio_net_tx_update_t vtx_update =
		    ldcp->portp->vcb.vio_net_tx_update;

		ldcp->need_resched = B_FALSE;
		vtx_update(ldcp->portp->vhp);
	}
}

/* return the number of pending transmits for the channel */
static int
vgen_num_txpending(vgen_ldc_t *ldcp)
{
	int n;

	if (ldcp->next_tbufp >= ldcp->cur_tbufp) {
		n = ldcp->next_tbufp - ldcp->cur_tbufp;
	} else  {
		/* cur_tbufp > next_tbufp */
		n = ldcp->num_txds - (ldcp->cur_tbufp - ldcp->next_tbufp);
	}

	return (n);
}

/* determine if the transmit descriptor ring is full */
static int
vgen_tx_dring_full(vgen_ldc_t *ldcp)
{
	vgen_private_desc_t	*tbufp;
	vgen_private_desc_t	*ntbufp;

	tbufp = ldcp->next_tbufp;
	ntbufp = NEXTTBUF(ldcp, tbufp);
	if (ntbufp == ldcp->cur_tbufp) { /* out of tbufs/txds */
		return (VGEN_SUCCESS);
	}
	return (VGEN_FAILURE);
}

/* determine if timeout condition has occured */
static int
vgen_ldc_txtimeout(vgen_ldc_t *ldcp)
{
	if (((ddi_get_lbolt() - ldcp->reclaim_lbolt) >
	    drv_usectohz(vnet_ldcwd_txtimeout * 1000)) &&
	    (vnet_ldcwd_txtimeout) &&
	    (vgen_tx_dring_full(ldcp) == VGEN_SUCCESS)) {
		return (VGEN_SUCCESS);
	} else {
		return (VGEN_FAILURE);
	}
}

/* transmit watchdog timeout handler */
static void
vgen_ldc_watchdog(void *arg)
{
	vgen_ldc_t *ldcp;
	vgen_t *vgenp;
	int rv;

	ldcp = (vgen_ldc_t *)arg;
	vgenp = LDC_TO_VGEN(ldcp);

	rv = vgen_ldc_txtimeout(ldcp);
	if (rv == VGEN_SUCCESS) {
		DWARN(vgenp, ldcp, "transmit timeout\n");
#ifdef DEBUG
		if (vgen_trigger_txtimeout) {
			/* tx timeout triggered for debugging */
			vgen_trigger_txtimeout = 0;
		}
#endif
		mutex_enter(&ldcp->cblock);
		vgen_ldc_reset(ldcp);
		mutex_exit(&ldcp->cblock);
		if (ldcp->need_resched) {
			vio_net_tx_update_t vtx_update =
			    ldcp->portp->vcb.vio_net_tx_update;

			ldcp->need_resched = B_FALSE;
			vtx_update(ldcp->portp->vhp);
		}
	}

	ldcp->wd_tid = timeout(vgen_ldc_watchdog, (caddr_t)ldcp,
	    drv_usectohz(vnet_ldcwd_interval * 1000));
}

/* handler for error messages received from the peer ldc end-point */
static void
vgen_handle_errmsg(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	_NOTE(ARGUNUSED(ldcp, tagp))
}

static int
vgen_check_datamsg_seq(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	vio_raw_data_msg_t	*rmsg;
	vio_dring_msg_t		*dmsg;
	uint64_t		seq_num;
	vgen_t			*vgenp = LDC_TO_VGEN(ldcp);

	if (tagp->vio_subtype_env == VIO_DRING_DATA) {
		dmsg = (vio_dring_msg_t *)tagp;
		seq_num = dmsg->seq_num;
	} else if (tagp->vio_subtype_env == VIO_PKT_DATA) {
		rmsg = (vio_raw_data_msg_t *)tagp;
		seq_num = rmsg->seq_num;
	} else {
		return (EINVAL);
	}

	if (seq_num != ldcp->next_rxseq) {

		/* seqnums don't match */
		DWARN(vgenp, ldcp,
		    "next_rxseq(0x%lx) != seq_num(0x%lx)\n",
		    ldcp->next_rxseq, seq_num);

		return (EINVAL);

	}

	ldcp->next_rxseq++;

	return (0);
}

/* Check if the session id in the received message is valid */
static int
vgen_check_sid(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	if (tagp->vio_sid != ldcp->peer_sid) {
		DWARN(vgenp, ldcp, "sid mismatch: expected(%x), rcvd(%x)\n",
		    ldcp->peer_sid, tagp->vio_sid);
		return (VGEN_FAILURE);
	}
	else
		return (VGEN_SUCCESS);
}

static caddr_t
vgen_print_ethaddr(uint8_t *a, char *ebuf)
{
	(void) sprintf(ebuf,
	    "%x:%x:%x:%x:%x:%x", a[0], a[1], a[2], a[3], a[4], a[5]);
	return (ebuf);
}

/* Handshake watchdog timeout handler */
static void
vgen_hwatchdog(void *arg)
{
	vgen_ldc_t *ldcp = (vgen_ldc_t *)arg;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	DWARN(vgenp, ldcp, "handshake timeout phase(%x) state(%x)\n",
	    ldcp->hphase, ldcp->hstate);

	mutex_enter(&ldcp->cblock);
	if (ldcp->cancel_htid) {
		ldcp->cancel_htid = 0;
		mutex_exit(&ldcp->cblock);
		return;
	}
	ldcp->htid = 0;
	vgen_ldc_reset(ldcp);
	mutex_exit(&ldcp->cblock);
}

static void
vgen_print_hparams(vgen_hparams_t *hp)
{
	uint8_t	addr[6];
	char	ea[6];
	ldc_mem_cookie_t *dc;

	cmn_err(CE_CONT, "version_info:\n");
	cmn_err(CE_CONT,
	    "\tver_major: %d, ver_minor: %d, dev_class: %d\n",
	    hp->ver_major, hp->ver_minor, hp->dev_class);

	vnet_macaddr_ultostr(hp->addr, addr);
	cmn_err(CE_CONT, "attr_info:\n");
	cmn_err(CE_CONT, "\tMTU: %lx, addr: %s\n", hp->mtu,
	    vgen_print_ethaddr(addr, ea));
	cmn_err(CE_CONT,
	    "\taddr_type: %x, xfer_mode: %x, ack_freq: %x\n",
	    hp->addr_type, hp->xfer_mode, hp->ack_freq);

	dc = &hp->dring_cookie;
	cmn_err(CE_CONT, "dring_info:\n");
	cmn_err(CE_CONT,
	    "\tlength: %d, dsize: %d\n", hp->num_desc, hp->desc_size);
	cmn_err(CE_CONT,
	    "\tldc_addr: 0x%lx, ldc_size: %ld\n",
	    dc->addr, dc->size);
	cmn_err(CE_CONT, "\tdring_ident: 0x%lx\n", hp->dring_ident);
}

static void
vgen_print_ldcinfo(vgen_ldc_t *ldcp)
{
	vgen_hparams_t *hp;

	cmn_err(CE_CONT, "Channel Information:\n");
	cmn_err(CE_CONT,
	    "\tldc_id: 0x%lx, ldc_status: 0x%x\n",
	    ldcp->ldc_id, ldcp->ldc_status);
	cmn_err(CE_CONT,
	    "\tlocal_sid: 0x%x, peer_sid: 0x%x\n",
	    ldcp->local_sid, ldcp->peer_sid);
	cmn_err(CE_CONT,
	    "\thphase: 0x%x, hstate: 0x%x\n",
	    ldcp->hphase, ldcp->hstate);

	cmn_err(CE_CONT, "Local handshake params:\n");
	hp = &ldcp->local_hparams;
	vgen_print_hparams(hp);

	cmn_err(CE_CONT, "Peer handshake params:\n");
	hp = &ldcp->peer_hparams;
	vgen_print_hparams(hp);
}

/*
 * Send received packets up the stack.
 */
static void
vgen_rx(vgen_ldc_t *ldcp, mblk_t *bp, mblk_t *bpt)
{
	vio_net_rx_cb_t vrx_cb = ldcp->portp->vcb.vio_net_rx_cb;
	vgen_t		*vgenp = LDC_TO_VGEN(ldcp);

	if (ldcp->rcv_thread != NULL) {
		ASSERT(MUTEX_HELD(&ldcp->rxlock));
	} else {
		ASSERT(MUTEX_HELD(&ldcp->cblock));
	}

	mutex_enter(&ldcp->pollq_lock);

	if (ldcp->polling_on == B_TRUE) {
		/*
		 * If we are in polling mode, simply queue
		 * the packets onto the poll queue and return.
		 */
		if (ldcp->pollq_headp == NULL) {
			ldcp->pollq_headp = bp;
			ldcp->pollq_tailp = bpt;
		} else {
			ldcp->pollq_tailp->b_next = bp;
			ldcp->pollq_tailp = bpt;
		}

		mutex_exit(&ldcp->pollq_lock);
		return;
	}

	/*
	 * Prepend any pending mblks in the poll queue, now that we
	 * are in interrupt mode, before sending up the chain of pkts.
	 */
	if (ldcp->pollq_headp != NULL) {
		DBG2(vgenp, ldcp, "vgen_rx(%lx), pending pollq_headp\n",
		    (uintptr_t)ldcp);
		ldcp->pollq_tailp->b_next = bp;
		bp = ldcp->pollq_headp;
		ldcp->pollq_headp = ldcp->pollq_tailp = NULL;
	}

	mutex_exit(&ldcp->pollq_lock);

	if (ldcp->rcv_thread != NULL) {
		mutex_exit(&ldcp->rxlock);
	} else {
		mutex_exit(&ldcp->cblock);
	}

	/* Send up the packets */
	vrx_cb(ldcp->portp->vhp, bp);

	if (ldcp->rcv_thread != NULL) {
		mutex_enter(&ldcp->rxlock);
	} else {
		mutex_enter(&ldcp->cblock);
	}
}

/*
 * vgen_ldc_rcv_worker -- A per LDC worker thread to receive data.
 * This thread is woken up by the LDC interrupt handler to process
 * LDC packets and receive data.
 */
static void
vgen_ldc_rcv_worker(void *arg)
{
	callb_cpr_t	cprinfo;
	vgen_ldc_t *ldcp = (vgen_ldc_t *)arg;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	DBG1(vgenp, ldcp, "enter\n");
	CALLB_CPR_INIT(&cprinfo, &ldcp->rcv_thr_lock, callb_generic_cpr,
	    "vnet_rcv_thread");
	mutex_enter(&ldcp->rcv_thr_lock);
	while (!(ldcp->rcv_thr_flags & VGEN_WTHR_STOP)) {

		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		/*
		 * Wait until the data is received or a stop
		 * request is received.
		 */
		while (!(ldcp->rcv_thr_flags &
		    (VGEN_WTHR_DATARCVD | VGEN_WTHR_STOP))) {
			cv_wait(&ldcp->rcv_thr_cv, &ldcp->rcv_thr_lock);
		}
		CALLB_CPR_SAFE_END(&cprinfo, &ldcp->rcv_thr_lock)

		/*
		 * First process the stop request.
		 */
		if (ldcp->rcv_thr_flags & VGEN_WTHR_STOP) {
			DBG2(vgenp, ldcp, "stopped\n");
			break;
		}
		ldcp->rcv_thr_flags &= ~VGEN_WTHR_DATARCVD;
		ldcp->rcv_thr_flags |= VGEN_WTHR_PROCESSING;
		mutex_exit(&ldcp->rcv_thr_lock);
		DBG2(vgenp, ldcp, "calling vgen_handle_evt_read\n");
		vgen_handle_evt_read(ldcp);
		mutex_enter(&ldcp->rcv_thr_lock);
		ldcp->rcv_thr_flags &= ~VGEN_WTHR_PROCESSING;
	}

	/*
	 * Update the run status and wakeup the thread that
	 * has sent the stop request.
	 */
	ldcp->rcv_thr_flags &= ~VGEN_WTHR_STOP;
	ldcp->rcv_thread = NULL;
	CALLB_CPR_EXIT(&cprinfo);

	thread_exit();
	DBG1(vgenp, ldcp, "exit\n");
}

/* vgen_stop_rcv_thread -- Co-ordinate with receive thread to stop it */
static void
vgen_stop_rcv_thread(vgen_ldc_t *ldcp)
{
	kt_did_t	tid = 0;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	DBG1(vgenp, ldcp, "enter\n");
	/*
	 * Send a stop request by setting the stop flag and
	 * wait until the receive thread stops.
	 */
	mutex_enter(&ldcp->rcv_thr_lock);
	if (ldcp->rcv_thread != NULL) {
		tid = ldcp->rcv_thread->t_did;
		ldcp->rcv_thr_flags |= VGEN_WTHR_STOP;
		cv_signal(&ldcp->rcv_thr_cv);
	}
	mutex_exit(&ldcp->rcv_thr_lock);

	if (tid != 0) {
		thread_join(tid);
	}
	DBG1(vgenp, ldcp, "exit\n");
}

/*
 * Wait for the channel rx-queue to be drained by allowing the receive
 * worker thread to read all messages from the rx-queue of the channel.
 * Assumption: further callbacks are disabled at this time.
 */
static void
vgen_drain_rcv_thread(vgen_ldc_t *ldcp)
{
	clock_t	tm;
	clock_t	wt;
	clock_t	rv;

	/*
	 * If there is data in ldc rx queue, wait until the rx
	 * worker thread runs and drains all msgs in the queue.
	 */
	wt = drv_usectohz(MILLISEC);

	mutex_enter(&ldcp->rcv_thr_lock);

	tm = ddi_get_lbolt() + wt;

	/*
	 * We need to check both bits - DATARCVD and PROCESSING, to be cleared.
	 * If DATARCVD is set, that means the callback has signalled the worker
	 * thread, but the worker hasn't started processing yet. If PROCESSING
	 * is set, that means the thread is awake and processing. Note that the
	 * DATARCVD state can only be seen once, as the assumption is that
	 * further callbacks have been disabled at this point.
	 */
	while (ldcp->rcv_thr_flags &
	    (VGEN_WTHR_DATARCVD | VGEN_WTHR_PROCESSING)) {
		rv = cv_timedwait(&ldcp->rcv_thr_cv, &ldcp->rcv_thr_lock, tm);
		if (rv == -1) {	/* timeout */
			/*
			 * Note that the only way we return is due to a timeout;
			 * we set the new time to wait, before we go back and
			 * check the condition. The other(unlikely) possibility
			 * is a premature wakeup(see cv_timedwait(9F)) in which
			 * case we just continue to use the same time to wait.
			 */
			tm = ddi_get_lbolt() + wt;
		}
	}

	mutex_exit(&ldcp->rcv_thr_lock);
}

/*
 * vgen_dds_rx -- post DDS messages to vnet.
 */
static int
vgen_dds_rx(vgen_ldc_t *ldcp, vio_msg_tag_t *tagp)
{
	vio_dds_msg_t *dmsg = (vio_dds_msg_t *)tagp;
	vgen_t *vgenp = LDC_TO_VGEN(ldcp);

	if (dmsg->dds_class != DDS_VNET_NIU) {
		DWARN(vgenp, ldcp, "Unknown DDS class, dropping");
		return (EBADMSG);
	}
	vnet_dds_rx(vgenp->vnetp, dmsg);
	return (0);
}

/*
 * vgen_dds_tx -- an interface called by vnet to send DDS messages.
 */
int
vgen_dds_tx(void *arg, void *msg)
{
	vgen_t *vgenp = arg;
	vio_dds_msg_t *dmsg = msg;
	vgen_portlist_t *plistp = &vgenp->vgenports;
	vgen_ldc_t *ldcp;
	vgen_ldclist_t *ldclp;
	int rv = EIO;


	READ_ENTER(&plistp->rwlock);
	ldclp = &(vgenp->vsw_portp->ldclist);
	READ_ENTER(&ldclp->rwlock);
	ldcp = ldclp->headp;
	if ((ldcp == NULL) || (ldcp->hphase != VH_DONE)) {
		goto vgen_dsend_exit;
	}

	dmsg->tag.vio_sid = ldcp->local_sid;
	rv = vgen_sendmsg(ldcp, (caddr_t)dmsg, sizeof (vio_dds_msg_t), B_FALSE);
	if (rv != VGEN_SUCCESS) {
		rv = EIO;
	} else {
		rv = 0;
	}

vgen_dsend_exit:
	RW_EXIT(&ldclp->rwlock);
	RW_EXIT(&plistp->rwlock);
	return (rv);

}

static void
vgen_ldc_reset(vgen_ldc_t *ldcp)
{
	vnet_t	*vnetp = LDC_TO_VNET(ldcp);
	vgen_t	*vgenp = LDC_TO_VGEN(ldcp);

	ASSERT(MUTEX_HELD(&ldcp->cblock));

	if (ldcp->need_ldc_reset == B_TRUE) {
		/* another thread is already in the process of resetting */
		return;
	}

	/* Set the flag to indicate reset is in progress */
	ldcp->need_ldc_reset = B_TRUE;

	if (ldcp->portp == vgenp->vsw_portp) {
		mutex_exit(&ldcp->cblock);
		/*
		 * Now cleanup any HIO resources; the above flag also tells
		 * the code that handles dds messages to drop any new msgs
		 * that arrive while we are cleaning up and resetting the
		 * channel.
		 */
		vnet_dds_cleanup_hio(vnetp);
		mutex_enter(&ldcp->cblock);
	}

	vgen_handshake_retry(ldcp);
}

int
vgen_enable_intr(void *arg)
{
	vgen_port_t		*portp = (vgen_port_t *)arg;
	vgen_ldclist_t		*ldclp;
	vgen_ldc_t		*ldcp;

	ldclp = &portp->ldclist;
	READ_ENTER(&ldclp->rwlock);
	/*
	 * NOTE: for now, we will assume we have a single channel.
	 */
	if (ldclp->headp == NULL) {
		RW_EXIT(&ldclp->rwlock);
		return (1);
	}
	ldcp = ldclp->headp;

	mutex_enter(&ldcp->pollq_lock);
	ldcp->polling_on = B_FALSE;
	mutex_exit(&ldcp->pollq_lock);

	RW_EXIT(&ldclp->rwlock);

	return (0);
}

int
vgen_disable_intr(void *arg)
{
	vgen_port_t		*portp = (vgen_port_t *)arg;
	vgen_ldclist_t		*ldclp;
	vgen_ldc_t		*ldcp;

	ldclp = &portp->ldclist;
	READ_ENTER(&ldclp->rwlock);
	/*
	 * NOTE: for now, we will assume we have a single channel.
	 */
	if (ldclp->headp == NULL) {
		RW_EXIT(&ldclp->rwlock);
		return (1);
	}
	ldcp = ldclp->headp;


	mutex_enter(&ldcp->pollq_lock);
	ldcp->polling_on = B_TRUE;
	mutex_exit(&ldcp->pollq_lock);

	RW_EXIT(&ldclp->rwlock);

	return (0);
}

mblk_t *
vgen_poll(void *arg, int bytes_to_pickup)
{
	vgen_port_t		*portp = (vgen_port_t *)arg;
	vgen_ldclist_t		*ldclp;
	vgen_ldc_t		*ldcp;
	mblk_t			*mp = NULL;

	ldclp = &portp->ldclist;
	READ_ENTER(&ldclp->rwlock);
	/*
	 * NOTE: for now, we will assume we have a single channel.
	 */
	if (ldclp->headp == NULL) {
		RW_EXIT(&ldclp->rwlock);
		return (NULL);
	}
	ldcp = ldclp->headp;

	mp = vgen_ldc_poll(ldcp, bytes_to_pickup);

	RW_EXIT(&ldclp->rwlock);
	return (mp);
}

static mblk_t *
vgen_ldc_poll(vgen_ldc_t *ldcp, int bytes_to_pickup)
{
	mblk_t	*bp = NULL;
	mblk_t	*bpt = NULL;
	mblk_t	*mp = NULL;
	size_t	mblk_sz = 0;
	size_t	sz = 0;
	uint_t	count = 0;

	mutex_enter(&ldcp->pollq_lock);

	bp = ldcp->pollq_headp;
	while (bp != NULL) {
		/* get the size of this packet */
		mblk_sz = msgdsize(bp);

		/* if adding this pkt, exceeds the size limit, we are done. */
		if (sz + mblk_sz >  bytes_to_pickup) {
			break;
		}

		/* we have room for this packet */
		sz += mblk_sz;

		/* increment the # of packets being sent up */
		count++;

		/* track the last processed pkt */
		bpt = bp;

		/* get the next pkt */
		bp = bp->b_next;
	}

	if (count != 0) {
		/*
		 * picked up some packets; save the head of pkts to be sent up.
		 */
		mp = ldcp->pollq_headp;

		/* move the pollq_headp to skip over the pkts being sent up */
		ldcp->pollq_headp = bp;

		/* picked up all pending pkts in the queue; reset tail also */
		if (ldcp->pollq_headp == NULL) {
			ldcp->pollq_tailp = NULL;
		}

		/* terminate the tail of pkts to be sent up */
		bpt->b_next = NULL;
	}

	mutex_exit(&ldcp->pollq_lock);

	DTRACE_PROBE1(vgen_poll_pkts, uint_t, count);
	return (mp);
}

#if DEBUG

/*
 * Print debug messages - set to 0xf to enable all msgs
 */
static void
debug_printf(const char *fname, vgen_t *vgenp,
    vgen_ldc_t *ldcp, const char *fmt, ...)
{
	char    buf[256];
	char    *bufp = buf;
	va_list ap;

	if ((vgenp != NULL) && (vgenp->vnetp != NULL)) {
		(void) sprintf(bufp, "vnet%d:",
		    ((vnet_t *)(vgenp->vnetp))->instance);
		bufp += strlen(bufp);
	}
	if (ldcp != NULL) {
		(void) sprintf(bufp, "ldc(%ld):", ldcp->ldc_id);
		bufp += strlen(bufp);
	}
	(void) sprintf(bufp, "%s: ", fname);
	bufp += strlen(bufp);

	va_start(ap, fmt);
	(void) vsprintf(bufp, fmt, ap);
	va_end(ap);

	if ((ldcp == NULL) ||(vgendbg_ldcid == -1) ||
	    (vgendbg_ldcid == ldcp->ldc_id)) {
		cmn_err(CE_CONT, "%s\n", buf);
	}
}
#endif

#ifdef	VNET_IOC_DEBUG

static void
vgen_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	struct iocblk	*iocp;
	vgen_port_t	*portp;
	enum		ioc_reply {
			IOC_INVAL = -1,		/* bad, NAK with EINVAL */
			IOC_ACK			/* OK, just send ACK    */
	}		status;
	int		rv;

	iocp = (struct iocblk *)(uintptr_t)mp->b_rptr;
	iocp->ioc_error = 0;
	portp = (vgen_port_t *)arg;

	if (portp == NULL) {
		status = IOC_INVAL;
		goto vgen_ioc_exit;
	}

	mutex_enter(&portp->lock);

	switch (iocp->ioc_cmd) {

	case VNET_FORCE_LINK_DOWN:
	case VNET_FORCE_LINK_UP:
		rv = vgen_force_link_state(portp, iocp->ioc_cmd);
		(rv == 0) ? (status = IOC_ACK) : (status = IOC_INVAL);
		break;

	default:
		status = IOC_INVAL;
		break;

	}

	mutex_exit(&portp->lock);

vgen_ioc_exit:

	switch (status) {
	default:
	case IOC_INVAL:
		/* Error, reply with a NAK and EINVAL error */
		miocnak(q, mp, 0, EINVAL);
		break;
	case IOC_ACK:
		/* OK, reply with an ACK */
		miocack(q, mp, 0, 0);
		break;
	}
}

static int
vgen_force_link_state(vgen_port_t *portp, int cmd)
{
	ldc_status_t	istatus;
	vgen_ldclist_t	*ldclp;
	vgen_ldc_t	*ldcp;
	vgen_t		*vgenp = portp->vgenp;
	int		rv;

	ldclp = &portp->ldclist;
	READ_ENTER(&ldclp->rwlock);

	/*
	 * NOTE: for now, we will assume we have a single channel.
	 */
	if (ldclp->headp == NULL) {
		RW_EXIT(&ldclp->rwlock);
		return (1);
	}
	ldcp = ldclp->headp;
	mutex_enter(&ldcp->cblock);

	switch (cmd) {

	case VNET_FORCE_LINK_DOWN:
		(void) ldc_down(ldcp->ldc_handle);
		ldcp->link_down_forced = B_TRUE;
		break;

	case VNET_FORCE_LINK_UP:
		rv = ldc_up(ldcp->ldc_handle);
		if (rv != 0) {
			DWARN(vgenp, ldcp, "ldc_up err rv(%d)\n", rv);
		}
		ldcp->link_down_forced = B_FALSE;

		if (ldc_status(ldcp->ldc_handle, &istatus) != 0) {
			DWARN(vgenp, ldcp, "ldc_status err\n");
		} else {
			ldcp->ldc_status = istatus;
		}

		/* if channel is already UP - restart handshake */
		if (ldcp->ldc_status == LDC_UP) {
			vgen_handle_evt_up(ldcp);
		}
		break;

	}

	mutex_exit(&ldcp->cblock);
	RW_EXIT(&ldclp->rwlock);

	return (0);
}

#else

static void
vgen_ioctl(void *arg, queue_t *q, mblk_t *mp)
{
	vgen_port_t	*portp;

	portp = (vgen_port_t *)arg;

	if (portp == NULL) {
		miocnak(q, mp, 0, EINVAL);
		return;
	}

	miocnak(q, mp, 0, ENOTSUP);
}

#endif
