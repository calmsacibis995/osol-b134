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

#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dld.h>
#include <sys/zone.h>
#include <fcntl.h>
#include <unistd.h>
#include <libdevinfo.h>
#include <zone.h>
#include <libdllink.h>
#include <libdladm_impl.h>
#include <libdlwlan_impl.h>
#include <libdlwlan.h>
#include <libdlvlan.h>
#include <libdlvnic.h>
#include <libintl.h>
#include <dlfcn.h>
#include <link.h>
#include <inet/wifi_ioctl.h>
#include <libdladm.h>
#include <libdlstat.h>
#include <sys/param.h>
#include <sys/debug.h>
#include <sys/dld.h>
#include <inttypes.h>
#include <sys/ethernet.h>
#include <inet/iptun.h>
#include <net/wpa.h>
#include <sys/sysmacros.h>
#include <sys/vlan.h>
#include <libdlbridge.h>
#include <stp_in.h>

/*
 * The linkprop get() callback.
 * - pd: 	pointer to the prop_desc_t
 * - propstrp:	a property string array to keep the returned property.
 *		Caller allocated.
 * - cntp:	number of returned properties.
 *		Caller also uses it to indicate how many it expects.
 */
struct prop_desc;
typedef struct prop_desc prop_desc_t;

typedef dladm_status_t	pd_getf_t(dladm_handle_t, prop_desc_t *pdp,
			datalink_id_t, char **propstp, uint_t *cntp,
			datalink_media_t, uint_t, uint_t *);

/*
 * The linkprop set() callback.
 * - propval:	a val_desc_t array which keeps the property values to be set.
 * - cnt:	number of properties to be set.
 * - flags: 	additional flags passed down the system call.
 *
 * pd_set takes val_desc_t given by pd_check(), translates it into
 * a format suitable for kernel consumption. This may require allocation
 * of ioctl buffers etc. pd_set() may call another common routine (used
 * by all other pd_sets) which invokes the ioctl.
 */
typedef dladm_status_t	pd_setf_t(dladm_handle_t, prop_desc_t *, datalink_id_t,
			    val_desc_t *propval, uint_t cnt, uint_t flags,
			    datalink_media_t);

/*
 * The linkprop check() callback.
 * - propstrp:	property string array which keeps the property to be checked.
 * - cnt:	number of properties.
 * - propval:	return value; the property values of the given property strings.
 *
 * pd_check checks that the input values are valid. It does so by
 * iteraring through the pd_modval list for the property. If
 * the modifiable values cannot be expressed as a list, a pd_check
 * specific to this property can be used. If the input values are
 * verified to be valid, pd_check allocates a val_desc_t and fills it
 * with either a val_desc_t found on the pd_modval list or something
 * generated on the fly.
 */
typedef dladm_status_t	pd_checkf_t(dladm_handle_t, prop_desc_t *pdp,
			    datalink_id_t, char **propstrp, uint_t cnt,
			    val_desc_t *propval, datalink_media_t);

typedef struct link_attr_s {
	mac_prop_id_t	pp_id;
	size_t		pp_valsize;
	char		*pp_name;
} link_attr_t;

static dld_ioc_macprop_t *i_dladm_buf_alloc_by_name(size_t, datalink_id_t,
			    const char *, uint_t, dladm_status_t *);
static dld_ioc_macprop_t *i_dladm_buf_alloc_by_id(size_t, datalink_id_t,
			    mac_prop_id_t, uint_t, dladm_status_t *);
static dld_ioc_macprop_t *i_dladm_get_public_prop(dladm_handle_t, datalink_id_t,
			    char *, uint_t, dladm_status_t *, uint_t *);

static dladm_status_t i_dladm_set_private_prop(dladm_handle_t, datalink_id_t,
			    const char *, char **, uint_t, uint_t);
static dladm_status_t i_dladm_get_priv_prop(dladm_handle_t, datalink_id_t,
			    const char *, char **, uint_t *, dladm_prop_type_t,
			    uint_t);
static link_attr_t *dladm_name2prop(const char *);
static link_attr_t *dladm_id2prop(mac_prop_id_t);

static pd_getf_t	do_get_zone, do_get_autopush, do_get_rate_mod,
			do_get_rate_prop, do_get_channel_prop,
			do_get_powermode_prop, do_get_radio_prop,
			i_dladm_duplex_get, i_dladm_status_get,
			i_dladm_binary_get, i_dladm_uint32_get,
			i_dladm_flowctl_get, i_dladm_maxbw_get,
			i_dladm_cpus_get, i_dladm_priority_get,
			i_dladm_tagmode_get, i_dladm_range_get,
			get_stp_prop, get_bridge_forward,
			get_bridge_pvid,
			/* the above need to be renamed to "do_get_xxx" */
			do_get_protection;

static pd_setf_t	do_set_zone, do_set_rate_prop,
			do_set_powermode_prop, do_set_radio_prop,
			i_dladm_set_public_prop, do_set_res, do_set_cpus,
			set_stp_prop, set_bridge_forward, set_bridge_pvid,
			do_set_protection;

static pd_checkf_t	do_check_zone, do_check_autopush, do_check_rate,
			do_check_hoplimit, do_check_encaplim,
			i_dladm_uint32_check, do_check_maxbw, do_check_cpus,
			do_check_priority, check_stp_prop, check_bridge_pvid,
			do_check_allowedips, do_check_prop;

static dladm_status_t	i_dladm_speed_get(dladm_handle_t, prop_desc_t *,
			    datalink_id_t, char **, uint_t *, uint_t, uint_t *);
static dladm_status_t	i_dladm_macprop(dladm_handle_t, void *, boolean_t);
static const char	*dladm_perm2str(uint_t, char *);

struct prop_desc {
	/*
	 * link property name
	 */
	char			*pd_name;

	/*
	 * default property value, can be set to { "", NULL }
	 */
	val_desc_t		pd_defval;

	/*
	 * list of optional property values, can be NULL.
	 *
	 * This is set to non-NULL if there is a list of possible property
	 * values.  pd_optval would point to the array of possible values.
	 */
	val_desc_t		*pd_optval;

	/*
	 * count of the above optional property values. 0 if pd_optval is NULL.
	 */
	uint_t			pd_noptval;

	/*
	 * callback to set link property; set to NULL if this property is
	 * read-only and may be called before or after permanent update; see
	 * flags.
	 */
	pd_setf_t		*pd_set;

	/*
	 * callback to get modifiable link property
	 */
	pd_getf_t		*pd_getmod;

	/*
	 * callback to get current link property
	 */
	pd_getf_t		*pd_get;

	/*
	 * callback to validate link property value, set to NULL if pd_optval
	 * is not NULL. In that case, validate the value by comparing it with
	 * the pd_optval. Return a val_desc_t array pointer if the value is
	 * valid.
	 */
	pd_checkf_t		*pd_check;

	uint_t			pd_flags;
#define	PD_TEMPONLY	0x1	/* property is temporary only */
#define	PD_CHECK_ALLOC	0x2	/* alloc vd_val as part of pd_check */
#define	PD_AFTER_PERM	0x4	/* pd_set after db update; no temporary */
	/*
	 * indicate link classes this property applies to.
	 */
	datalink_class_t	pd_class;

	/*
	 * indicate link media type this property applies to.
	 */
	datalink_media_t	pd_dmedia;
};

#define	MAC_PROP_BUFSIZE(v)	sizeof (dld_ioc_macprop_t) + (v) - 1

/*
 * Supported link properties enumerated in the prop_table[] array are
 * computed using the callback functions in that array. To compute the
 * property value, multiple distinct system calls may be needed (e.g.,
 * for wifi speed, we need to issue system calls to get desired/supported
 * rates). The link_attr[] table enumerates the interfaces to the kernel,
 * and the type/size of the data passed in the user-kernel interface.
 */
static link_attr_t link_attr[] = {
	{ MAC_PROP_DUPLEX,	sizeof (link_duplex_t),	"duplex"},

	{ MAC_PROP_SPEED,	sizeof (uint64_t),	"speed"},

	{ MAC_PROP_STATUS,	sizeof (link_state_t),	"state"},

	{ MAC_PROP_AUTONEG,	sizeof (uint8_t),	"adv_autoneg_cap"},

	{ MAC_PROP_MTU,		sizeof (uint32_t),	"mtu"},

	{ MAC_PROP_FLOWCTRL,	sizeof (link_flowctrl_t), "flowctrl"},

	{ MAC_PROP_ZONE,	sizeof (dld_ioc_zid_t),	"zone"},

	{ MAC_PROP_AUTOPUSH,	sizeof (struct dlautopush), "autopush"},

	{ MAC_PROP_ADV_10GFDX_CAP, sizeof (uint8_t),	"adv_10gfdx_cap"},

	{ MAC_PROP_EN_10GFDX_CAP, sizeof (uint8_t),	"en_10gfdx_cap"},

	{ MAC_PROP_ADV_1000FDX_CAP, sizeof (uint8_t),	"adv_1000fdx_cap"},

	{ MAC_PROP_EN_1000FDX_CAP, sizeof (uint8_t),	"en_1000fdx_cap"},

	{ MAC_PROP_ADV_1000HDX_CAP, sizeof (uint8_t),	"adv_1000hdx_cap"},

	{ MAC_PROP_EN_1000HDX_CAP, sizeof (uint8_t),	"en_1000hdx_cap"},

	{ MAC_PROP_ADV_100FDX_CAP, sizeof (uint8_t),	"adv_100fdx_cap"},

	{ MAC_PROP_EN_100FDX_CAP, sizeof (uint8_t),	"en_100fdx_cap"},

	{ MAC_PROP_ADV_100HDX_CAP, sizeof (uint8_t),	"adv_100hdx_cap"},

	{ MAC_PROP_EN_100HDX_CAP, sizeof (uint8_t),	"en_100hdx_cap"},

	{ MAC_PROP_ADV_10FDX_CAP, sizeof (uint8_t),	"adv_10fdx_cap"},

	{ MAC_PROP_EN_10FDX_CAP, sizeof (uint8_t),	"en_10fdx_cap"},

	{ MAC_PROP_ADV_10HDX_CAP, sizeof (uint8_t),	"adv_10hdx_cap"},

	{ MAC_PROP_EN_10HDX_CAP, sizeof (uint8_t),	"en_10hdx_cap"},

	{ MAC_PROP_WL_ESSID,	sizeof (wl_linkstatus_t), "essid"},

	{ MAC_PROP_WL_BSSID,	sizeof (wl_bssid_t),	"bssid"},

	{ MAC_PROP_WL_BSSTYPE,	sizeof (wl_bss_type_t),	"bsstype"},

	{ MAC_PROP_WL_LINKSTATUS, sizeof (wl_linkstatus_t), "wl_linkstatus"},

	/* wl_rates_t has variable length */
	{ MAC_PROP_WL_DESIRED_RATES, sizeof (wl_rates_t), "desired_rates"},

	/* wl_rates_t has variable length */
	{ MAC_PROP_WL_SUPPORTED_RATES, sizeof (wl_rates_t), "supported_rates"},

	{ MAC_PROP_WL_AUTH_MODE, sizeof (wl_authmode_t), "authmode"},

	{ MAC_PROP_WL_ENCRYPTION, sizeof (wl_encryption_t), "encryption"},

	{ MAC_PROP_WL_RSSI,	sizeof (wl_rssi_t),	"signal"},

	{ MAC_PROP_WL_PHY_CONFIG, sizeof (wl_phy_conf_t), "phy_conf"},

	{ MAC_PROP_WL_CAPABILITY, sizeof (wl_capability_t), "capability"},

	{ MAC_PROP_WL_WPA,	sizeof (wl_wpa_t),	"wpa"},

	/*  wl_wpa_ess_t has variable length */
	{ MAC_PROP_WL_SCANRESULTS, sizeof (wl_wpa_ess_t), "scan_results"},

	{ MAC_PROP_WL_POWER_MODE, sizeof (wl_ps_mode_t), "powermode"},

	{ MAC_PROP_WL_RADIO,	sizeof (dladm_wlan_radio_t), "wl_radio"},

	{ MAC_PROP_WL_ESS_LIST, sizeof (wl_ess_list_t),	"wl_ess_list"},

	{ MAC_PROP_WL_KEY_TAB,	sizeof (wl_wep_key_tab_t), "wl_wep_key"},

	{ MAC_PROP_WL_CREATE_IBSS, sizeof (wl_create_ibss_t), "createibss"},

	/* wl_wpa_ie_t has variable length */
	{ MAC_PROP_WL_SETOPTIE,	sizeof (wl_wpa_ie_t),	"set_ie"},

	{ MAC_PROP_WL_DELKEY,	sizeof (wl_del_key_t),	"wpa_del_key"},

	{ MAC_PROP_WL_KEY,	sizeof (wl_key_t),	"wl_key"},

	{ MAC_PROP_WL_MLME,	sizeof (wl_mlme_t),	"mlme"},

	{ MAC_PROP_MAXBW,	sizeof (mac_resource_props_t),	"maxbw"},

	{ MAC_PROP_PRIO,	sizeof (mac_resource_props_t),	"priority"},

	{ MAC_PROP_BIND_CPU,	sizeof (mac_resource_props_t),	"cpus"},

	{ MAC_PROP_TAGMODE,	sizeof (link_tagmode_t),	"tagmode"},

	{ MAC_PROP_IPTUN_HOPLIMIT, sizeof (uint32_t),	"hoplimit"},

	{ MAC_PROP_IPTUN_ENCAPLIMIT, sizeof (uint32_t),	"encaplimit"},

	{ MAC_PROP_PVID,	sizeof (uint16_t),	"default_tag"},

	{ MAC_PROP_LLIMIT,	sizeof (uint32_t),	"learn_limit"},

	{ MAC_PROP_LDECAY,	sizeof (uint32_t),	"learn_decay"},

	{ MAC_PROP_PROTECT,	sizeof (mac_resource_props_t),	"protection"},

	{ MAC_PROP_PRIVATE,	0,			"driver-private"}
};

typedef struct bridge_public_prop_s {
	const char	*bpp_name;
	int		bpp_code;
} bridge_public_prop_t;

static const bridge_public_prop_t bridge_prop[] = {
	{ "stp", PT_CFG_NON_STP },
	{ "stp_priority", PT_CFG_PRIO },
	{ "stp_cost", PT_CFG_COST },
	{ "stp_edge", PT_CFG_EDGE },
	{ "stp_p2p", PT_CFG_P2P },
	{ "stp_mcheck", PT_CFG_MCHECK },
	{ NULL, 0 }
};

static  val_desc_t	link_duplex_vals[] = {
	{ "half", 	LINK_DUPLEX_HALF	},
	{ "full", 	LINK_DUPLEX_HALF	}
};
static  val_desc_t	link_status_vals[] = {
	{ "up",		LINK_STATE_UP		},
	{ "down",	LINK_STATE_DOWN		}
};
static  val_desc_t	link_01_vals[] = {
	{ "1",		1			},
	{ "0",		0			}
};
static  val_desc_t	link_flow_vals[] = {
	{ "no",		LINK_FLOWCTRL_NONE	},
	{ "tx",		LINK_FLOWCTRL_TX	},
	{ "rx",		LINK_FLOWCTRL_RX	},
	{ "bi",		LINK_FLOWCTRL_BI	}
};
static  val_desc_t	link_priority_vals[] = {
	{ "low",	MPL_LOW	},
	{ "medium",	MPL_MEDIUM	},
	{ "high",	MPL_HIGH	}
};

static val_desc_t	link_tagmode_vals[] = {
	{ "normal",	LINK_TAGMODE_NORMAL	},
	{ "vlanonly",	LINK_TAGMODE_VLANONLY	}
};

static  val_desc_t	link_protect_vals[] = {
	{ "mac-nospoof",	MPT_MACNOSPOOF	},
	{ "ip-nospoof",		MPT_IPNOSPOOF	},
	{ "restricted",		MPT_RESTRICTED	}
};

static val_desc_t	dladm_wlan_radio_vals[] = {
	{ "on",		DLADM_WLAN_RADIO_ON	},
	{ "off",	DLADM_WLAN_RADIO_OFF	}
};

static val_desc_t	dladm_wlan_powermode_vals[] = {
	{ "off",	DLADM_WLAN_PM_OFF	},
	{ "fast",	DLADM_WLAN_PM_FAST	},
	{ "max",	DLADM_WLAN_PM_MAX	}
};

static  val_desc_t	stp_p2p_vals[] = {
	{ "true",	P2P_FORCE_TRUE		},
	{ "false",	P2P_FORCE_FALSE		},
	{ "auto",	P2P_AUTO		}
};

#define	VALCNT(vals)    (sizeof ((vals)) / sizeof (val_desc_t))
#define	RESET_VAL	((uintptr_t)-1)

static prop_desc_t	prop_table[] = {
	{ "channel",	{ NULL, 0 },
	    NULL, 0, NULL, NULL,
	    do_get_channel_prop, NULL, 0,
	    DATALINK_CLASS_PHYS, DL_WIFI },

	{ "powermode",	{ "off", DLADM_WLAN_PM_OFF },
	    dladm_wlan_powermode_vals, VALCNT(dladm_wlan_powermode_vals),
	    do_set_powermode_prop, NULL,
	    do_get_powermode_prop, NULL, 0,
	    DATALINK_CLASS_PHYS, DL_WIFI },

	{ "radio",	{ "on", DLADM_WLAN_RADIO_ON },
	    dladm_wlan_radio_vals, VALCNT(dladm_wlan_radio_vals),
	    do_set_radio_prop, NULL,
	    do_get_radio_prop, NULL, 0,
	    DATALINK_CLASS_PHYS, DL_WIFI },

	{ "speed",	{ "", 0 }, NULL, 0,
	    do_set_rate_prop, do_get_rate_mod,
	    do_get_rate_prop, do_check_rate, 0,
	    DATALINK_CLASS_PHYS, DATALINK_ANY_MEDIATYPE },

	{ "autopush",	{ "", 0 }, NULL, 0,
	    i_dladm_set_public_prop, NULL,
	    do_get_autopush, do_check_autopush, PD_CHECK_ALLOC,
	    DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE },

	{ "zone",	{ "", 0 }, NULL, 0,
	    do_set_zone, NULL,
	    do_get_zone, do_check_zone, PD_TEMPONLY|PD_CHECK_ALLOC,
	    DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE },

	{ "duplex",	{ "", 0 },
	    link_duplex_vals, VALCNT(link_duplex_vals),
	    NULL, NULL, i_dladm_duplex_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "state",	{ "up", LINK_STATE_UP },
	    link_status_vals, VALCNT(link_status_vals),
	    NULL, NULL, i_dladm_status_get, NULL,
	    0, DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE },

	{ "adv_autoneg_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "mtu", { "", 0 }, NULL, 0,
	    i_dladm_set_public_prop, i_dladm_range_get,
	    i_dladm_uint32_get, i_dladm_uint32_check, 0, DATALINK_CLASS_ALL,
	    DATALINK_ANY_MEDIATYPE },

	{ "flowctrl", { "", 0 },
	    link_flow_vals, VALCNT(link_flow_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_flowctl_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "adv_10gfdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    NULL, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "en_10gfdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "adv_1000fdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    NULL, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "en_1000fdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "adv_1000hdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    NULL, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "en_1000hdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "adv_100fdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    NULL, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "en_100fdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "adv_100hdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    NULL, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "en_100hdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "adv_10fdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    NULL, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "en_10fdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "adv_10hdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    NULL, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "en_10hdx_cap", { "", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_binary_get, NULL,
	    0, DATALINK_CLASS_PHYS, DL_ETHER },

	{ "maxbw", { "--", RESET_VAL }, NULL, 0,
	    do_set_res, NULL,
	    i_dladm_maxbw_get, do_check_maxbw, PD_CHECK_ALLOC,
	    DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE },

	{ "cpus", { "--", RESET_VAL }, NULL, 0,
	    do_set_cpus, NULL,
	    i_dladm_cpus_get, do_check_cpus, 0,
	    DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE },

	{ "priority", { "high", RESET_VAL },
	    link_priority_vals, VALCNT(link_priority_vals), do_set_res, NULL,
	    i_dladm_priority_get, do_check_priority, PD_CHECK_ALLOC,
	    DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE },

	{ "tagmode", { "vlanonly", LINK_TAGMODE_VLANONLY },
	    link_tagmode_vals, VALCNT(link_tagmode_vals),
	    i_dladm_set_public_prop, NULL, i_dladm_tagmode_get,
	    NULL, 0,
	    DATALINK_CLASS_PHYS | DATALINK_CLASS_AGGR | DATALINK_CLASS_VNIC,
	    DL_ETHER },

	{ "hoplimit", { "", 0 }, NULL, 0,
	    i_dladm_set_public_prop, i_dladm_range_get, i_dladm_uint32_get,
	    do_check_hoplimit, 0, DATALINK_CLASS_IPTUN, DATALINK_ANY_MEDIATYPE},

	{ "encaplimit", { "", 0 }, NULL, 0,
	    i_dladm_set_public_prop, i_dladm_range_get, i_dladm_uint32_get,
	    do_check_encaplim, 0, DATALINK_CLASS_IPTUN, DL_IPV6},

	{ "forward", { "1", 1 },
	    link_01_vals, VALCNT(link_01_vals),
	    set_bridge_forward, NULL, get_bridge_forward, NULL, PD_AFTER_PERM,
	    DATALINK_CLASS_ALL & ~DATALINK_CLASS_VNIC, DL_ETHER },

	{ "default_tag", { "1", 1 }, NULL, 0,
	    set_bridge_pvid, NULL, get_bridge_pvid, check_bridge_pvid,
	    0, DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "learn_limit", { "1000", 1000 }, NULL, 0,
	    i_dladm_set_public_prop, NULL, i_dladm_uint32_get,
	    i_dladm_uint32_check, 0,
	    DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "learn_decay", { "200", 200 }, NULL, 0,
	    i_dladm_set_public_prop, NULL, i_dladm_uint32_get,
	    i_dladm_uint32_check, 0,
	    DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "stp", { "1", 1 },
	    link_01_vals, VALCNT(link_01_vals),
	    set_stp_prop, NULL, get_stp_prop, NULL, PD_AFTER_PERM,
	    DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "stp_priority", { "128", 128 }, NULL, 0,
	    set_stp_prop, NULL, get_stp_prop, check_stp_prop, PD_AFTER_PERM,
	    DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "stp_cost", { "auto", 0 }, NULL, 0,
	    set_stp_prop, NULL, get_stp_prop, check_stp_prop, PD_AFTER_PERM,
	    DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "stp_edge", { "1", 1 },
	    link_01_vals, VALCNT(link_01_vals),
	    set_stp_prop, NULL, get_stp_prop, NULL, PD_AFTER_PERM,
	    DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "stp_p2p", { "auto", P2P_AUTO },
	    stp_p2p_vals, VALCNT(stp_p2p_vals),
	    set_stp_prop, NULL, get_stp_prop, NULL, PD_AFTER_PERM,
	    DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "stp_mcheck", { "0", 0 },
	    link_01_vals, VALCNT(link_01_vals),
	    set_stp_prop, NULL, get_stp_prop, check_stp_prop, PD_AFTER_PERM,
	    DATALINK_CLASS_PHYS|DATALINK_CLASS_AGGR|
	    DATALINK_CLASS_ETHERSTUB|DATALINK_CLASS_SIMNET, DL_ETHER },

	{ "protection", { "--", RESET_VAL },
	    link_protect_vals, VALCNT(link_protect_vals),
	    do_set_protection, NULL, do_get_protection, do_check_prop, 0,
	    DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE },

	{ "allowed-ips", { "--", 0 },
	    NULL, 0, do_set_protection, NULL,
	    do_get_protection, do_check_allowedips, 0,
	    DATALINK_CLASS_ALL, DATALINK_ANY_MEDIATYPE },
};

#define	DLADM_MAX_PROPS	(sizeof (prop_table) / sizeof (prop_desc_t))

static resource_prop_t rsrc_prop_table[] = {
	{"maxbw",	do_extract_maxbw},
	{"priority",	do_extract_priority},
	{"cpus",	do_extract_cpus},
	{"protection",	do_extract_protection},
	{"allowed-ips",	do_extract_allowedips}
};
#define	DLADM_MAX_RSRC_PROP (sizeof (rsrc_prop_table) / \
	sizeof (resource_prop_t))

/*
 * when retrieving  private properties, we pass down a buffer with
 * DLADM_PROP_BUF_CHUNK of space for the driver to return the property value.
 */
#define	DLADM_PROP_BUF_CHUNK	1024

static dladm_status_t	i_dladm_set_linkprop_db(dladm_handle_t, datalink_id_t,
			    const char *, char **, uint_t);
static dladm_status_t	i_dladm_get_linkprop_db(dladm_handle_t, datalink_id_t,
			    const char *, char **, uint_t *);
static dladm_status_t	i_dladm_walk_linkprop_priv_db(dladm_handle_t,
			    datalink_id_t, void *, int (*)(dladm_handle_t,
			    datalink_id_t, const char *, void *));
static dladm_status_t	i_dladm_set_single_prop(dladm_handle_t, datalink_id_t,
			    datalink_class_t, uint32_t, prop_desc_t *, char **,
			    uint_t, uint_t);
static dladm_status_t	i_dladm_set_linkprop(dladm_handle_t, datalink_id_t,
			    const char *, char **, uint_t, uint_t);
static dladm_status_t	i_dladm_getset_defval(dladm_handle_t, prop_desc_t *,
			    datalink_id_t, datalink_media_t, uint_t);

/*
 * Unfortunately, MAX_SCAN_SUPPORT_RATES is too small to allow all
 * rates to be retrieved. However, we cannot increase it at this
 * time because it will break binary compatibility with unbundled
 * WiFi drivers and utilities. So for now we define an additional
 * constant, MAX_SUPPORT_RATES, to allow all rates to be retrieved.
 */
#define	MAX_SUPPORT_RATES	64

#define	AP_ANCHOR	"[anchor]"
#define	AP_DELIMITER	'.'

/* ARGSUSED */
static dladm_status_t
do_check_prop(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t val_cnt, val_desc_t *vdp, datalink_media_t media)
{
	int		i, j;

	for (j = 0; j < val_cnt; j++) {
		for (i = 0; i < pdp->pd_noptval; i++) {
			if (strcasecmp(prop_val[j],
			    pdp->pd_optval[i].vd_name) == 0) {
				break;
			}
		}
		if (i == pdp->pd_noptval)
			return (DLADM_STATUS_BADVAL);

		(void) memcpy(&vdp[j], &pdp->pd_optval[i], sizeof (val_desc_t));
	}
	return (DLADM_STATUS_OK);
}

static dladm_status_t
i_dladm_set_single_prop(dladm_handle_t handle, datalink_id_t linkid,
    datalink_class_t class, uint32_t media, prop_desc_t *pdp, char **prop_val,
    uint_t val_cnt, uint_t flags)
{
	dladm_status_t	status = DLADM_STATUS_OK;
	val_desc_t	*vdp = NULL;
	boolean_t	needfree = B_FALSE;
	uint_t		cnt, i;

	if (!(pdp->pd_class & class))
		return (DLADM_STATUS_BADARG);

	if (!DATALINK_MEDIA_ACCEPTED(pdp->pd_dmedia, media))
		return (DLADM_STATUS_BADARG);

	if ((flags & DLADM_OPT_PERSIST) && (pdp->pd_flags & PD_TEMPONLY))
		return (DLADM_STATUS_TEMPONLY);

	if (!(flags & DLADM_OPT_ACTIVE))
		return (DLADM_STATUS_OK);

	if (pdp->pd_set == NULL)
		return (DLADM_STATUS_PROPRDONLY);

	if (prop_val != NULL) {
		vdp = malloc(sizeof (val_desc_t) * val_cnt);
		if (vdp == NULL)
			return (DLADM_STATUS_NOMEM);

		if (pdp->pd_check != NULL) {
			needfree = ((pdp->pd_flags & PD_CHECK_ALLOC) != 0);
			status = pdp->pd_check(handle, pdp, linkid, prop_val,
			    val_cnt, vdp, media);
		} else if (pdp->pd_optval != NULL) {
			status = do_check_prop(handle, pdp, linkid, prop_val,
			    val_cnt, vdp, media);
		} else {
			status = DLADM_STATUS_BADARG;
		}

		if (status != DLADM_STATUS_OK)
			goto done;

		cnt = val_cnt;
	} else {
		boolean_t	defval = B_FALSE;

		if (pdp->pd_defval.vd_name == NULL)
			return (DLADM_STATUS_NOTSUP);

		cnt = 1;
		defval = (strlen(pdp->pd_defval.vd_name) > 0);
		if ((pdp->pd_flags & PD_CHECK_ALLOC) != 0 || defval) {
			if ((vdp = malloc(sizeof (val_desc_t))) == NULL)
				return (DLADM_STATUS_NOMEM);

			if (defval) {
				(void) memcpy(vdp, &pdp->pd_defval,
				    sizeof (val_desc_t));
			} else if (pdp->pd_check != NULL) {
				status = pdp->pd_check(handle, pdp, linkid,
				    prop_val, cnt, vdp, media);
				if (status != DLADM_STATUS_OK)
					goto done;
			}
		} else {
			status = i_dladm_getset_defval(handle, pdp, linkid,
			    media, flags);
			return (status);
		}
	}
	if (pdp->pd_flags & PD_AFTER_PERM)
		status = (flags & DLADM_OPT_PERSIST) ? DLADM_STATUS_OK :
		    DLADM_STATUS_PERMONLY;
	else
		status = pdp->pd_set(handle, pdp, linkid, vdp, cnt, flags,
		    media);
	if (needfree) {
		for (i = 0; i < cnt; i++)
			free((void *)((val_desc_t *)vdp + i)->vd_val);
	}
done:
	free(vdp);
	return (status);
}

static dladm_status_t
i_dladm_set_linkprop(dladm_handle_t handle, datalink_id_t linkid,
    const char *prop_name, char **prop_val, uint_t val_cnt, uint_t flags)
{
	int			i;
	boolean_t		found = B_FALSE;
	datalink_class_t	class;
	uint32_t		media;
	dladm_status_t		status = DLADM_STATUS_OK;

	status = dladm_datalink_id2info(handle, linkid, NULL, &class, &media,
	    NULL, 0);
	if (status != DLADM_STATUS_OK)
		return (status);

	for (i = 0; i < DLADM_MAX_PROPS; i++) {
		prop_desc_t	*pdp = &prop_table[i];
		dladm_status_t	s;

		if (prop_name != NULL &&
		    (strcasecmp(prop_name, pdp->pd_name) != 0))
			continue;
		found = B_TRUE;
		s = i_dladm_set_single_prop(handle, linkid, class, media, pdp,
		    prop_val, val_cnt, flags);

		if (prop_name != NULL) {
			status = s;
			break;
		} else {
			if (s != DLADM_STATUS_OK &&
			    s != DLADM_STATUS_NOTSUP)
				status = s;
		}
	}
	if (!found) {
		if (prop_name[0] == '_') {
			/* other private properties */
			status = i_dladm_set_private_prop(handle, linkid,
			    prop_name, prop_val, val_cnt, flags);
		} else  {
			status = DLADM_STATUS_NOTFOUND;
		}
	}

	return (status);
}

/*
 * Set/reset link property for specific link
 */
dladm_status_t
dladm_set_linkprop(dladm_handle_t handle, datalink_id_t linkid,
    const char *prop_name, char **prop_val, uint_t val_cnt, uint_t flags)
{
	dladm_status_t	status = DLADM_STATUS_OK;

	if ((linkid == DATALINK_INVALID_LINKID) || (flags == 0) ||
	    (prop_val == NULL && val_cnt > 0) ||
	    (prop_val != NULL && val_cnt == 0) ||
	    (prop_name == NULL && prop_val != NULL)) {
		return (DLADM_STATUS_BADARG);
	}

	/*
	 * Check for valid link property against the flags passed
	 * and set the link property when active flag is passed.
	 */
	status = i_dladm_set_linkprop(handle, linkid, prop_name, prop_val,
	    val_cnt, flags);
	if (status != DLADM_STATUS_OK)
		return (status);

	if (flags & DLADM_OPT_PERSIST) {
		status = i_dladm_set_linkprop_db(handle, linkid, prop_name,
		    prop_val, val_cnt);

		if (status == DLADM_STATUS_OK && (flags & DLADM_OPT_ACTIVE)) {
			prop_desc_t *pdp = prop_table;
			int i;

			for (i = 0; i < DLADM_MAX_PROPS; i++, pdp++) {
				if (!(pdp->pd_flags & PD_AFTER_PERM))
					continue;
				if (prop_name != NULL &&
				    strcasecmp(prop_name, pdp->pd_name) != 0)
					continue;
				status = pdp->pd_set(handle, pdp, linkid, NULL,
				    0, flags, 0);
			}
		}
	}
	return (status);
}

/*
 * Walk all link properties of the given specific link.
 *
 * Note: this function currently lacks the ability to walk _all_ private
 * properties if the link, because there is no kernel interface to
 * retrieve all known private property names. Once such an interface
 * is added, this function should be fixed accordingly.
 */
dladm_status_t
dladm_walk_linkprop(dladm_handle_t handle, datalink_id_t linkid, void *arg,
    int (*func)(dladm_handle_t, datalink_id_t, const char *, void *))
{
	dladm_status_t		status;
	datalink_class_t	class;
	uint_t			media;
	int			i;

	if (linkid == DATALINK_INVALID_LINKID || func == NULL)
		return (DLADM_STATUS_BADARG);

	status = dladm_datalink_id2info(handle, linkid, NULL, &class, &media,
	    NULL, 0);
	if (status != DLADM_STATUS_OK)
		return (status);

	/* public */
	for (i = 0; i < DLADM_MAX_PROPS; i++) {
		if (!(prop_table[i].pd_class & class))
			continue;

		if (!DATALINK_MEDIA_ACCEPTED(prop_table[i].pd_dmedia, media))
			continue;

		if (func(handle, linkid, prop_table[i].pd_name, arg) ==
		    DLADM_WALK_TERMINATE) {
			break;
		}
	}

	/* private */
	status = i_dladm_walk_linkprop_priv_db(handle, linkid, arg, func);

	return (status);
}

/*
 * Get linkprop of the given specific link.
 */
dladm_status_t
dladm_get_linkprop(dladm_handle_t handle, datalink_id_t linkid,
    dladm_prop_type_t type, const char *prop_name, char **prop_val,
    uint_t *val_cntp)
{
	dladm_status_t		status = DLADM_STATUS_OK;
	datalink_class_t	class;
	uint_t			media;
	prop_desc_t		*pdp;
	uint_t			cnt, dld_flags = 0;
	int			i;
	uint_t			perm_flags;

	if (type == DLADM_PROP_VAL_DEFAULT)
		dld_flags |= MAC_PROP_DEFAULT;
	else if (type == DLADM_PROP_VAL_MODIFIABLE)
		dld_flags |= MAC_PROP_POSSIBLE;

	if (linkid == DATALINK_INVALID_LINKID || prop_name == NULL ||
	    prop_val == NULL || val_cntp == NULL || *val_cntp == 0)
		return (DLADM_STATUS_BADARG);

	for (i = 0; i < DLADM_MAX_PROPS; i++)
		if (strcasecmp(prop_name, prop_table[i].pd_name) == 0)
			break;

	if (i == DLADM_MAX_PROPS) {
		if (prop_name[0] == '_') {
			/*
			 * private property.
			 */
			if (type == DLADM_PROP_VAL_PERSISTENT)
				return (i_dladm_get_linkprop_db(handle, linkid,
				    prop_name, prop_val, val_cntp));
			else
				return (i_dladm_get_priv_prop(handle, linkid,
				    prop_name, prop_val, val_cntp, type,
				    dld_flags));
		} else {
			return (DLADM_STATUS_NOTFOUND);
		}
	}

	pdp = &prop_table[i];

	status = dladm_datalink_id2info(handle, linkid, NULL, &class, &media,
	    NULL, 0);
	if (status != DLADM_STATUS_OK)
		return (status);

	if (!(pdp->pd_class & class))
		return (DLADM_STATUS_BADARG);

	if (!DATALINK_MEDIA_ACCEPTED(pdp->pd_dmedia, media))
		return (DLADM_STATUS_BADARG);

	switch (type) {
	case DLADM_PROP_VAL_CURRENT:
		status = pdp->pd_get(handle, pdp, linkid, prop_val, val_cntp,
		    media, dld_flags, &perm_flags);
		break;

	case DLADM_PROP_VAL_PERM:
		if (pdp->pd_set == NULL) {
			perm_flags = MAC_PROP_PERM_READ;
		} else {
			status = pdp->pd_get(handle, pdp, linkid, prop_val,
			    val_cntp, media, dld_flags, &perm_flags);
		}

		*prop_val[0] = '\0';
		*val_cntp = 1;
		if (status == DLADM_STATUS_OK)
			(void) dladm_perm2str(perm_flags, *prop_val);
		break;

	case DLADM_PROP_VAL_DEFAULT:
		/*
		 * If defaults are not defined for the property,
		 * pd_defval.vd_name should be null. If the driver
		 * has to be contacted for the value, vd_name should
		 * be the empty string (""). Otherwise, dladm will
		 * just print whatever is in the table.
		 */
		if (pdp->pd_defval.vd_name == NULL) {
			status = DLADM_STATUS_NOTSUP;
			break;
		}

		if (strlen(pdp->pd_defval.vd_name) == 0) {
			status = pdp->pd_get(handle, pdp, linkid, prop_val,
			    val_cntp, media, dld_flags, &perm_flags);
		} else {
			(void) strcpy(*prop_val, pdp->pd_defval.vd_name);
		}
		*val_cntp = 1;
		break;

	case DLADM_PROP_VAL_MODIFIABLE:
		if (pdp->pd_getmod != NULL) {
			status = pdp->pd_getmod(handle, pdp, linkid, prop_val,
			    val_cntp, media, dld_flags, &perm_flags);
			break;
		}
		cnt = pdp->pd_noptval;
		if (cnt == 0) {
			status = DLADM_STATUS_NOTSUP;
		} else if (cnt > *val_cntp) {
			status = DLADM_STATUS_TOOSMALL;
		} else {
			for (i = 0; i < cnt; i++) {
				(void) strcpy(prop_val[i],
				    pdp->pd_optval[i].vd_name);
			}
			*val_cntp = cnt;
		}
		break;
	case DLADM_PROP_VAL_PERSISTENT:
		if (pdp->pd_flags & PD_TEMPONLY)
			return (DLADM_STATUS_TEMPONLY);
		status = i_dladm_get_linkprop_db(handle, linkid, prop_name,
		    prop_val, val_cntp);
		break;
	default:
		status = DLADM_STATUS_BADARG;
		break;
	}

	return (status);
}

/*
 * Get linkprop of the given specific link and run any possible conversion
 * of the values using the check function for the property. Fails if the
 * check function doesn't succeed for the property value.
 */
dladm_status_t
dladm_get_linkprop_values(dladm_handle_t handle, datalink_id_t linkid,
    dladm_prop_type_t type, const char *prop_name, uint_t *ret_val,
    uint_t *val_cntp)
{
	dladm_status_t		status;
	datalink_class_t	class;
	uint_t			media;
	prop_desc_t		*pdp;
	uint_t			dld_flags;
	int			valc, i;
	char			**prop_val;
	uint_t			perm_flags;

	if (linkid == DATALINK_INVALID_LINKID || prop_name == NULL ||
	    ret_val == NULL || val_cntp == NULL || *val_cntp == 0)
		return (DLADM_STATUS_BADARG);

	for (pdp = prop_table; pdp < prop_table + DLADM_MAX_PROPS; pdp++)
		if (strcasecmp(prop_name, pdp->pd_name) == 0)
			break;

	if (pdp == prop_table + DLADM_MAX_PROPS)
		return (DLADM_STATUS_NOTFOUND);

	if (pdp->pd_flags & PD_CHECK_ALLOC)
		return (DLADM_STATUS_BADARG);

	status = dladm_datalink_id2info(handle, linkid, NULL, &class, &media,
	    NULL, 0);
	if (status != DLADM_STATUS_OK)
		return (status);

	if (!(pdp->pd_class & class))
		return (DLADM_STATUS_BADARG);

	if (!DATALINK_MEDIA_ACCEPTED(pdp->pd_dmedia, media))
		return (DLADM_STATUS_BADARG);

	prop_val = malloc(*val_cntp * sizeof (*prop_val) +
	    *val_cntp * DLADM_PROP_VAL_MAX);
	if (prop_val == NULL)
		return (DLADM_STATUS_NOMEM);
	for (valc = 0; valc < *val_cntp; valc++)
		prop_val[valc] = (char *)(prop_val + *val_cntp) +
		    valc * DLADM_PROP_VAL_MAX;

	dld_flags = (type == DLADM_PROP_VAL_DEFAULT) ? MAC_PROP_DEFAULT : 0;

	switch (type) {
	case DLADM_PROP_VAL_CURRENT:
		status = pdp->pd_get(handle, pdp, linkid, prop_val, val_cntp,
		    media, dld_flags, &perm_flags);
		break;

	case DLADM_PROP_VAL_DEFAULT:
		/*
		 * If defaults are not defined for the property,
		 * pd_defval.vd_name should be null. If the driver
		 * has to be contacted for the value, vd_name should
		 * be the empty string (""). Otherwise, dladm will
		 * just print whatever is in the table.
		 */
		if (pdp->pd_defval.vd_name == NULL) {
			status = DLADM_STATUS_NOTSUP;
			break;
		}

		if (pdp->pd_defval.vd_name[0] != '\0') {
			*val_cntp = 1;
			*ret_val = pdp->pd_defval.vd_val;
			free(prop_val);
			return (DLADM_STATUS_OK);
		}
		status = pdp->pd_get(handle, pdp, linkid, prop_val, val_cntp,
		    media, dld_flags, &perm_flags);
		break;

	case DLADM_PROP_VAL_PERSISTENT:
		if (pdp->pd_flags & PD_TEMPONLY)
			status = DLADM_STATUS_TEMPONLY;
		else
			status = i_dladm_get_linkprop_db(handle, linkid,
			    prop_name, prop_val, val_cntp);
		break;

	default:
		status = DLADM_STATUS_BADARG;
		break;
	}

	if (status == DLADM_STATUS_OK) {
		if (pdp->pd_check != NULL) {
			val_desc_t *vdp;

			vdp = malloc(sizeof (val_desc_t) * *val_cntp);
			if (vdp == NULL)
				status = DLADM_STATUS_NOMEM;
			else
				status = pdp->pd_check(handle, pdp, linkid,
				    prop_val, *val_cntp, vdp, media);
			if (status == DLADM_STATUS_OK) {
				for (valc = 0; valc < *val_cntp; valc++)
					ret_val[valc] = vdp[valc].vd_val;
			}
			free(vdp);
		} else {
			for (valc = 0; valc < *val_cntp; valc++) {
				for (i = 0; i < pdp->pd_noptval; i++) {
					if (strcmp(pdp->pd_optval[i].vd_name,
					    prop_val[valc]) == 0) {
						ret_val[valc] =
						    pdp->pd_optval[i].vd_val;
						break;
					}
				}
				if (i == pdp->pd_noptval) {
					status = DLADM_STATUS_FAILED;
					break;
				}
			}
		}
	}

	free(prop_val);

	return (status);
}

/*ARGSUSED*/
static int
i_dladm_init_one_prop(dladm_handle_t handle, datalink_id_t linkid,
    const char *prop_name, void *arg)
{
	char		*buf, **propvals;
	uint_t		i, valcnt = DLADM_MAX_PROP_VALCNT;
	dladm_status_t	status, *retval = arg;

	if ((buf = malloc((sizeof (char *) + DLADM_PROP_VAL_MAX) *
	    DLADM_MAX_PROP_VALCNT)) == NULL) {
		return (DLADM_WALK_CONTINUE);
	}

	propvals = (char **)(void *)buf;
	for (i = 0; i < valcnt; i++) {
		propvals[i] = buf +
		    sizeof (char *) * DLADM_MAX_PROP_VALCNT +
		    i * DLADM_PROP_VAL_MAX;
	}

	if (dladm_get_linkprop(handle, linkid, DLADM_PROP_VAL_PERSISTENT,
	    prop_name, propvals, &valcnt) != DLADM_STATUS_OK) {
		goto done;
	}

	status = dladm_set_linkprop(handle, linkid, prop_name, propvals,
	    valcnt, DLADM_OPT_ACTIVE);
	if (status != DLADM_STATUS_OK)
		*retval = status;

done:
	if (buf != NULL)
		free(buf);

	return (DLADM_WALK_CONTINUE);
}

/*ARGSUSED*/
static int
i_dladm_init_linkprop(dladm_handle_t handle, datalink_id_t linkid, void *arg)
{
	datalink_class_t	class;
	dladm_status_t		status;

	status = dladm_datalink_id2info(handle, linkid, NULL, &class, NULL,
	    NULL, 0);
	if (status != DLADM_STATUS_OK)
		return (DLADM_WALK_TERMINATE);

	if ((class & (DATALINK_CLASS_VNIC | DATALINK_CLASS_VLAN)) == 0)
		(void) dladm_init_linkprop(handle, linkid, B_TRUE);

	return (DLADM_WALK_CONTINUE);
}

dladm_status_t
dladm_init_linkprop(dladm_handle_t handle, datalink_id_t linkid,
    boolean_t any_media)
{
	dladm_status_t		status = DLADM_STATUS_OK;
	datalink_media_t	dmedia;
	uint32_t		media;

	dmedia = any_media ? DATALINK_ANY_MEDIATYPE : DL_WIFI;

	if (linkid == DATALINK_ALL_LINKID) {
		(void) dladm_walk_datalink_id(i_dladm_init_linkprop, handle,
		    NULL, DATALINK_CLASS_ALL, dmedia, DLADM_OPT_PERSIST);
	} else if (any_media ||
	    ((dladm_datalink_id2info(handle, linkid, NULL, NULL, &media, NULL,
	    0) == DLADM_STATUS_OK) &&
	    DATALINK_MEDIA_ACCEPTED(dmedia, media))) {
		(void) dladm_walk_linkprop(handle, linkid, &status,
		    i_dladm_init_one_prop);
	}
	return (status);
}

/* ARGSUSED */
static dladm_status_t
do_get_zone(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, datalink_media_t media,
    uint_t flags, uint_t *perm_flags)
{
	char			zone_name[ZONENAME_MAX];
	zoneid_t		zid;
	dladm_status_t		status;
	char			*cp;
	dld_ioc_macprop_t	*dip;

	if (flags != 0)
		return (DLADM_STATUS_NOTSUP);

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (status != DLADM_STATUS_OK)
		return (status);

	cp = dip->pr_val;
	(void) memcpy(&zid, cp, sizeof (zid));
	free(dip);

	*val_cnt = 1;
	if (zid != GLOBAL_ZONEID) {
		if (getzonenamebyid(zid, zone_name, sizeof (zone_name)) < 0) {
			return (dladm_errno2status(errno));
		}

		(void) strncpy(*prop_val, zone_name, DLADM_PROP_VAL_MAX);
	} else {
		*prop_val[0] = '\0';
	}

	return (DLADM_STATUS_OK);
}

typedef int (*zone_get_devroot_t)(char *, char *, size_t);

static int
i_dladm_get_zone_dev(char *zone_name, char *dev, size_t devlen)
{
	char			root[MAXPATHLEN];
	zone_get_devroot_t	real_zone_get_devroot;
	void			*dlhandle;
	void			*sym;
	int			ret;

	if ((dlhandle = dlopen("libzonecfg.so.1", RTLD_LAZY)) == NULL)
		return (-1);

	if ((sym = dlsym(dlhandle, "zone_get_devroot")) == NULL) {
		(void) dlclose(dlhandle);
		return (-1);
	}

	real_zone_get_devroot = (zone_get_devroot_t)sym;

	if ((ret = real_zone_get_devroot(zone_name, root, sizeof (root))) == 0)
		(void) snprintf(dev, devlen, "%s%s", root, "/dev");
	(void) dlclose(dlhandle);
	return (ret);
}

static dladm_status_t
i_dladm_update_deventry(dladm_handle_t handle, zoneid_t zid,
    datalink_id_t linkid, boolean_t add)
{
	char		path[MAXPATHLEN];
	char		name[MAXLINKNAMELEN];
	di_prof_t	prof = NULL;
	char		zone_name[ZONENAME_MAX];
	dladm_status_t	status;
	int		ret;

	if (getzonenamebyid(zid, zone_name, sizeof (zone_name)) < 0)
		return (dladm_errno2status(errno));
	if (i_dladm_get_zone_dev(zone_name, path, sizeof (path)) != 0)
		return (dladm_errno2status(errno));
	if (di_prof_init(path, &prof) != 0)
		return (dladm_errno2status(errno));

	status = dladm_linkid2legacyname(handle, linkid, name, MAXLINKNAMELEN);
	if (status != DLADM_STATUS_OK)
		goto cleanup;

	if (add)
		ret = di_prof_add_dev(prof, name);
	else
		ret = di_prof_add_exclude(prof, name);

	if (ret != 0) {
		status = dladm_errno2status(errno);
		goto cleanup;
	}

	if (di_prof_commit(prof) != 0)
		status = dladm_errno2status(errno);
cleanup:
	if (prof)
		di_prof_fini(prof);

	return (status);
}

/* ARGSUSED */
static dladm_status_t
do_set_zone(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    val_desc_t *vdp, uint_t val_cnt, uint_t flags, datalink_media_t media)
{
	dladm_status_t		status = DLADM_STATUS_OK;
	zoneid_t		zid_old, zid_new;
	char			*cp;
	dld_ioc_macprop_t	*dip;
	dld_ioc_zid_t		*dzp;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	dzp = (dld_ioc_zid_t *)vdp->vd_val;

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, NULL);
	if (status != DLADM_STATUS_OK)
		return (status);

	cp = dip->pr_val;
	(void) memcpy(&zid_old, cp, sizeof (zid_old));
	free(dip);

	zid_new = dzp->diz_zid;
	if (zid_new == zid_old)
		return (DLADM_STATUS_OK);

	if ((status = i_dladm_set_public_prop(handle, pdp, linkid, vdp, val_cnt,
	    flags, media)) != DLADM_STATUS_OK)
		return (status);

	/*
	 * It is okay to fail to update the /dev entry (some vanity-named
	 * links do not have a /dev entry).
	 */
	if (zid_old != GLOBAL_ZONEID) {
		(void) i_dladm_update_deventry(handle, zid_old, linkid,
		    B_FALSE);
	}
	if (zid_new != GLOBAL_ZONEID)
		(void) i_dladm_update_deventry(handle, zid_new, linkid, B_TRUE);

	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_check_zone(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t val_cnt, val_desc_t *vdp, datalink_media_t media)
{
	char		*zone_name;
	zoneid_t	zoneid;
	dladm_status_t	status = DLADM_STATUS_OK;
	dld_ioc_zid_t	*dzp;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	dzp = malloc(sizeof (dld_ioc_zid_t));
	if (dzp == NULL)
		return (DLADM_STATUS_NOMEM);

	zone_name = (prop_val != NULL) ? *prop_val : GLOBAL_ZONENAME;
	if ((zoneid = getzoneidbyname(zone_name)) == -1) {
		status = DLADM_STATUS_BADVAL;
		goto done;
	}

	if (zoneid != GLOBAL_ZONEID) {
		ushort_t	flags;

		if (zone_getattr(zoneid, ZONE_ATTR_FLAGS, &flags,
		    sizeof (flags)) < 0) {
			status = dladm_errno2status(errno);
			goto done;
		}

		if (!(flags & ZF_NET_EXCL)) {
			status = DLADM_STATUS_BADVAL;
			goto done;
		}
	}

	(void) memset(dzp, 0, sizeof (dld_ioc_zid_t));

	dzp->diz_zid = zoneid;
	dzp->diz_linkid = linkid;

	vdp->vd_val = (uintptr_t)dzp;
	return (DLADM_STATUS_OK);
done:
	free(dzp);
	return (status);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_maxbw_get(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, datalink_media_t media,
    uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t	*dip;
	mac_resource_props_t	mrp;
	dladm_status_t		status;

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (dip == NULL)
		return (status);

	bcopy(dip->pr_val, &mrp, sizeof (mac_resource_props_t));
	free(dip);

	if ((mrp.mrp_mask & MRP_MAXBW) == 0) {
		(*prop_val)[0] = '\0';
	} else {
		(void) dladm_bw2str(mrp.mrp_maxbw, prop_val[0]);
	}
	*val_cnt = 1;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_check_maxbw(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t val_cnt, val_desc_t *vdp, datalink_media_t media)
{
	uint64_t	*maxbw;
	dladm_status_t	status = DLADM_STATUS_OK;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	maxbw = malloc(sizeof (uint64_t));
	if (maxbw == NULL)
		return (DLADM_STATUS_NOMEM);

	status = dladm_str2bw(*prop_val, maxbw);
	if (status != DLADM_STATUS_OK) {
		free(maxbw);
		return (status);
	}

	if ((*maxbw < MRP_MAXBW_MINVAL) && (*maxbw != 0)) {
		free(maxbw);
		return (DLADM_STATUS_MINMAXBW);
	}

	vdp->vd_val = (uintptr_t)maxbw;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
dladm_status_t
do_extract_maxbw(val_desc_t *vdp, uint_t cnt, void *arg)
{
	mac_resource_props_t *mrp = arg;

	bcopy((char *)vdp->vd_val, &mrp->mrp_maxbw, sizeof (uint64_t));
	mrp->mrp_mask |= MRP_MAXBW;

	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_cpus_get(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, datalink_media_t media,
    uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t	*dip;
	mac_resource_props_t	mrp;
	int			i;
	uint32_t		ncpus;
	uchar_t			*cp;
	dladm_status_t		status;

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (dip == NULL)
		return (status);

	cp = (uchar_t *)dip->pr_val;
	(void) memcpy(&mrp, cp, sizeof (mac_resource_props_t));
	free(dip);

	ncpus = mrp.mrp_ncpus;

	if (ncpus > *val_cnt)
		return (DLADM_STATUS_TOOSMALL);

	if (ncpus == 0) {
		(*prop_val)[0] = '\0';
		*val_cnt = 1;
		return (DLADM_STATUS_OK);
	}

	*val_cnt = ncpus;
	for (i = 0; i < ncpus; i++) {
		(void) snprintf(prop_val[i], DLADM_PROP_VAL_MAX,
		    "%u", mrp.mrp_cpu[i]);
	}
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_set_res(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    val_desc_t *vdp, uint_t val_cnt, uint_t flags, datalink_media_t media)
{
	mac_resource_props_t	mrp;
	dladm_status_t		status = DLADM_STATUS_OK;
	dld_ioc_macprop_t	*dip;

	bzero(&mrp, sizeof (mac_resource_props_t));
	dip = i_dladm_buf_alloc_by_name(0, linkid, pdp->pd_name,
	    flags, &status);

	if (dip == NULL)
		return (status);

	if (vdp->vd_val == RESET_VAL) {
		switch (dip->pr_num) {
		case MAC_PROP_MAXBW:
			mrp.mrp_maxbw = MRP_MAXBW_RESETVAL;
			mrp.mrp_mask = MRP_MAXBW;
			break;
		case MAC_PROP_PRIO:
			mrp.mrp_priority = MPL_RESET;
			mrp.mrp_mask = MRP_PRIORITY;
			break;
		default:
			free(dip);
			return (DLADM_STATUS_BADARG);
		}
	} else {
		switch (dip->pr_num) {
		case MAC_PROP_MAXBW:
			bcopy((void *)vdp->vd_val, &mrp.mrp_maxbw,
			    sizeof (uint64_t));
			mrp.mrp_mask = MRP_MAXBW;
			break;
		case MAC_PROP_PRIO:
			bcopy((void *)vdp->vd_val, &mrp.mrp_priority,
			    sizeof (mac_priority_level_t));
			mrp.mrp_mask = MRP_PRIORITY;
			break;
		default:
			free(dip);
			return (DLADM_STATUS_BADARG);
		}
	}

	(void) memcpy(dip->pr_val, &mrp, dip->pr_valsize);
	status = i_dladm_macprop(handle, dip, B_TRUE);
	free(dip);
	return (status);
}

/* ARGSUSED */
static dladm_status_t
do_set_cpus(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    val_desc_t *vdp, uint_t val_cnt, uint_t flags, datalink_media_t media)
{
	mac_resource_props_t	mrp;
	dladm_status_t		status;
	dld_ioc_macprop_t	*dip;
	datalink_class_t	class;

	/*
	 * CPU bindings can be set on VNIC and regular physical links.
	 * However VNICs fails the dladm_phys_info test(). So apply
	 * the phys_info test only on physical links.
	 */
	if ((status = dladm_datalink_id2info(handle, linkid, NULL, &class,
	    NULL, NULL, 0)) != DLADM_STATUS_OK) {
		return (status);
	}

	/*
	 * We set intr_cpu to -1. The interrupt will be retargetted,
	 * if possible when the setup is complete in MAC.
	 */
	bzero(&mrp, sizeof (mac_resource_props_t));
	mrp.mrp_mask = MRP_CPUS;
	if (vdp != NULL && vdp->vd_val != RESET_VAL) {
		mac_resource_props_t	*vmrp;

		vmrp = (mac_resource_props_t *)vdp->vd_val;
		if (vmrp->mrp_ncpus > 0) {
			bcopy(vmrp, &mrp, sizeof (mac_resource_props_t));
			mrp.mrp_mask = MRP_CPUS;
		}
		mrp.mrp_mask |= MRP_CPUS_USERSPEC;
		mrp.mrp_fanout_mode = MCM_CPUS;
		mrp.mrp_intr_cpu = -1;
	}

	dip = i_dladm_buf_alloc_by_name(0, linkid, pdp->pd_name,
	    flags, &status);
	if (dip == NULL)
		return (status);

	(void) memcpy(dip->pr_val, &mrp, dip->pr_valsize);
	status = i_dladm_macprop(handle, dip, B_TRUE);
	free(dip);
	return (status);
}

/* ARGSUSED */
static dladm_status_t
do_check_cpus(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t val_cnt, val_desc_t *vdp, datalink_media_t media)
{
	uint32_t		cpuid;
	int			i, j, rc;
	long			nproc = sysconf(_SC_NPROCESSORS_CONF);
	mac_resource_props_t	*mrp;

	mrp = malloc(sizeof (mac_resource_props_t));
	if (mrp == NULL)
		return (DLADM_STATUS_NOMEM);

	for (i = 0; i < val_cnt; i++) {
		errno = 0;
		cpuid = strtol(prop_val[i], (char **)NULL, 10);
		if (errno != 0 || cpuid >= nproc) {
			free(mrp);
			return (DLADM_STATUS_CPUMAX);
		}
		rc = p_online(cpuid, P_STATUS);
		if (rc < 1) {
			free(mrp);
			return (DLADM_STATUS_CPUERR);
		}
		if (rc != P_ONLINE) {
			free(mrp);
			return (DLADM_STATUS_CPUNOTONLINE);
		}
		mrp->mrp_cpu[i] = cpuid;
	}
	mrp->mrp_ncpus = (uint32_t)val_cnt;

	/* Check for duplicates */
	for (i = 0; i < val_cnt; i++) {
		for (j = 0; j < val_cnt; j++) {
			if (i != j && mrp->mrp_cpu[i] == mrp->mrp_cpu[j]) {
				free(mrp);
				return (DLADM_STATUS_BADARG);
			}
		}
	}
	vdp->vd_val = (uintptr_t)mrp;

	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
dladm_status_t
do_extract_cpus(val_desc_t *vdp, uint_t cnt, void *arg)
{
	mac_resource_props_t	*mrp = arg;
	mac_resource_props_t	*vmrp = (mac_resource_props_t *)vdp->vd_val;
	int			i;

	for (i = 0; i < vmrp->mrp_ncpus; i++) {
		mrp->mrp_cpu[i] = vmrp->mrp_cpu[i];
	}
	mrp->mrp_ncpus = vmrp->mrp_ncpus;
	mrp->mrp_mask |= (MRP_CPUS|MRP_CPUS_USERSPEC);
	mrp->mrp_fanout_mode = MCM_CPUS;
	mrp->mrp_intr_cpu = -1;

	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_priority_get(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t	*dip;
	mac_resource_props_t	mrp;
	mac_priority_level_t	pri;
	dladm_status_t		status;

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (dip == NULL)
		return (status);

	bcopy(dip->pr_val, &mrp, sizeof (mac_resource_props_t));
	free(dip);

	pri = ((mrp.mrp_mask & MRP_PRIORITY) == 0) ? MPL_HIGH :
	    mrp.mrp_priority;

	(void) dladm_pri2str(pri, prop_val[0]);
	*val_cnt = 1;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_check_priority(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t val_cnt, val_desc_t *vdp, datalink_media_t media)
{
	mac_priority_level_t	*pri;
	dladm_status_t	status = DLADM_STATUS_OK;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	pri = malloc(sizeof (mac_priority_level_t));
	if (pri == NULL)
		return (DLADM_STATUS_NOMEM);

	status = dladm_str2pri(*prop_val, pri);
	if (status != DLADM_STATUS_OK) {
		free(pri);
		return (status);
	}

	if (*pri < MPL_LOW || *pri > MPL_HIGH) {
		free(pri);
		return (DLADM_STATUS_BADVAL);
	}

	vdp->vd_val = (uintptr_t)pri;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
dladm_status_t
do_extract_priority(val_desc_t *vdp, uint_t cnt, void *arg)
{
	mac_resource_props_t *mrp = arg;

	bcopy((char *)vdp->vd_val, &mrp->mrp_priority,
	    sizeof (mac_priority_level_t));
	mrp->mrp_mask |= MRP_PRIORITY;

	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_set_protection(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, val_desc_t *vdp, uint_t val_cnt,
    uint_t flags, datalink_media_t media)
{
	mac_resource_props_t	mrp;
	dladm_status_t		status = DLADM_STATUS_OK;
	dld_ioc_macprop_t	*dip;

	bzero(&mrp, sizeof (mac_resource_props_t));
	dip = i_dladm_buf_alloc_by_name(0, linkid, "protection",
	    flags, &status);

	if (dip == NULL)
		return (status);

	if (strcmp(pdp->pd_name, "protection") == 0) {
		status = do_extract_protection(vdp, val_cnt, &mrp);
		if (status != DLADM_STATUS_OK)
			goto done;

	} else if (strcmp(pdp->pd_name, "allowed-ips") == 0) {
		status = do_extract_allowedips(vdp, val_cnt, &mrp);
		if (status != DLADM_STATUS_OK)
			goto done;
	} else {
		status = DLADM_STATUS_BADARG;
		goto done;
	}

	(void) memcpy(dip->pr_val, &mrp, dip->pr_valsize);
	status = i_dladm_macprop(handle, dip, B_TRUE);

done:
	free(dip);
	return (status);
}

/* ARGSUSED */
static dladm_status_t
do_get_protection(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t	*dip;
	mac_resource_props_t	mrp;
	mac_protect_t		*p;
	dladm_status_t		status;
	int			i;

	dip = i_dladm_get_public_prop(handle, linkid, "protection", flags,
	    &status, perm_flags);
	if (dip == NULL)
		return (status);

	bcopy(dip->pr_val, &mrp, sizeof (mac_resource_props_t));
	free(dip);

	p = &mrp.mrp_protect;
	if ((mrp.mrp_mask & MRP_PROTECT) != 0 &&
	    strcmp(pdp->pd_name, "protection") == 0) {
		uint32_t	cnt = 0, setbits[32];

		dladm_find_setbits32(p->mp_types, setbits, &cnt);
		if (cnt > *val_cnt)
			return (DLADM_STATUS_BADVALCNT);

		for (i = 0; i < cnt; i++)
			(void) dladm_protect2str(setbits[i], prop_val[i]);

		*val_cnt = cnt;
		return (DLADM_STATUS_OK);
	}

	if (p->mp_ipaddrcnt > 0 &&
	    strcmp(pdp->pd_name, "allowed-ips") == 0) {
		if (p->mp_ipaddrcnt > *val_cnt)
			return (DLADM_STATUS_BADVALCNT);

		for (i = 0; i < p->mp_ipaddrcnt; i++) {
			(void) dladm_ipv4addr2str(&p->mp_ipaddrs[i],
			    prop_val[i]);
		}
		*val_cnt = p->mp_ipaddrcnt;
		return (DLADM_STATUS_OK);
	}

	*val_cnt = 0;
	return (DLADM_STATUS_OK);
}

dladm_status_t
do_extract_protection(val_desc_t *vdp, uint_t cnt, void *arg)
{
	mac_resource_props_t	*mrp = arg;
	uint32_t		types = 0;
	int			i;

	for (i = 0; i < cnt; i++)
		types |= (uint32_t)vdp[i].vd_val;

	mrp->mrp_protect.mp_types = types;
	mrp->mrp_mask |= MRP_PROTECT;
	return (DLADM_STATUS_OK);
}

dladm_status_t
do_extract_allowedips(val_desc_t *vdp, uint_t cnt, void *arg)
{
	mac_resource_props_t	*mrp = arg;
	mac_protect_t		*p = &mrp->mrp_protect;
	int			i;

	if (vdp->vd_val == 0) {
		cnt = (uint_t)-1;
	} else {
		for (i = 0; i < cnt; i++)
			p->mp_ipaddrs[i] = (ipaddr_t)vdp[i].vd_val;
	}
	p->mp_ipaddrcnt = cnt;
	mrp->mrp_mask |= MRP_PROTECT;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_check_allowedips(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t val_cnt,
    val_desc_t *vdp, datalink_media_t media)
{
	dladm_status_t	status;
	ipaddr_t	addr;
	int		i;

	if (val_cnt > MPT_MAXIPADDR)
		return (DLADM_STATUS_BADVALCNT);

	for (i = 0; i < val_cnt; i++) {
		status = dladm_str2ipv4addr(prop_val[i], &addr);
		if (status != DLADM_STATUS_OK)
			return (status);

		if (addr == 0)
			return (DLADM_STATUS_BADVAL);

		vdp[i].vd_val = (uintptr_t)addr;
	}
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_get_autopush(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, datalink_media_t media,
    uint_t flags, uint_t *perm_flags)
{
	struct		dlautopush dlap;
	int		i, len;
	dladm_status_t	status;
	dld_ioc_macprop_t	*dip;

	if (flags & MAC_PROP_DEFAULT)
		return (DLADM_STATUS_NOTDEFINED);

	*val_cnt = 1;
	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (dip == NULL) {
		(*prop_val)[0] = '\0';
		return (DLADM_STATUS_OK);
	}
	(void) memcpy(&dlap, dip->pr_val, sizeof (dlap));

	for (i = 0, len = 0; i < dlap.dap_npush; i++) {
		if (i != 0) {
			(void) snprintf(*prop_val + len,
			    DLADM_PROP_VAL_MAX - len, "%c", AP_DELIMITER);
			len += 1;
		}
		(void) snprintf(*prop_val + len, DLADM_PROP_VAL_MAX - len,
		    "%s", dlap.dap_aplist[i]);
		len += strlen(dlap.dap_aplist[i]);
		if (dlap.dap_anchor - 1 == i) {
			(void) snprintf(*prop_val + len,
			    DLADM_PROP_VAL_MAX - len, "%c%s", AP_DELIMITER,
			    AP_ANCHOR);
			len += (strlen(AP_ANCHOR) + 1);
		}
	}
	free(dip);
done:
	return (DLADM_STATUS_OK);
}

/*
 * Add the specified module to the dlautopush structure; returns a
 * DLADM_STATUS_* code.
 */
dladm_status_t
i_dladm_add_ap_module(const char *module, struct dlautopush *dlap)
{
	if ((strlen(module) == 0) || (strlen(module) > FMNAMESZ))
		return (DLADM_STATUS_BADVAL);

	if (strncasecmp(module, AP_ANCHOR, strlen(AP_ANCHOR)) == 0) {
		/*
		 * We don't allow multiple anchors, and the anchor must
		 * be after at least one module.
		 */
		if (dlap->dap_anchor != 0)
			return (DLADM_STATUS_BADVAL);
		if (dlap->dap_npush == 0)
			return (DLADM_STATUS_BADVAL);

		dlap->dap_anchor = dlap->dap_npush;
		return (DLADM_STATUS_OK);
	}
	if (dlap->dap_npush >= MAXAPUSH)
		return (DLADM_STATUS_BADVALCNT);

	(void) strlcpy(dlap->dap_aplist[dlap->dap_npush++], module,
	    FMNAMESZ + 1);

	return (DLADM_STATUS_OK);
}

/*
 * Currently, both '.' and ' '(space) can be used as the delimiters between
 * autopush modules. The former is used in dladm set-linkprop, and the
 * latter is used in the autopush(1M) file.
 */
/* ARGSUSED */
static dladm_status_t
do_check_autopush(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t val_cnt, val_desc_t *vdp, datalink_media_t media)
{
	char			*module;
	struct dlautopush	*dlap;
	dladm_status_t		status;
	char			val[DLADM_PROP_VAL_MAX];
	char			delimiters[4];

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	if (prop_val != NULL) {
		dlap = malloc(sizeof (struct dlautopush));
		if (dlap == NULL)
			return (DLADM_STATUS_NOMEM);

		(void) memset(dlap, 0, sizeof (struct dlautopush));
		(void) snprintf(delimiters, 4, " %c\n", AP_DELIMITER);
		bcopy(*prop_val, val, DLADM_PROP_VAL_MAX);
		module = strtok(val, delimiters);
		while (module != NULL) {
			status = i_dladm_add_ap_module(module, dlap);
			if (status != DLADM_STATUS_OK)
				return (status);
			module = strtok(NULL, delimiters);
		}

		vdp->vd_val = (uintptr_t)dlap;
	} else {
		vdp->vd_val = 0;
	}
	return (DLADM_STATUS_OK);
}

#define	WLDP_BUFSIZE (MAX_BUF_LEN - WIFI_BUF_OFFSET)

/* ARGSUSED */
static dladm_status_t
do_get_rate_common(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt, uint_t id,
    uint_t *perm_flags)
{
	wl_rates_t	*wrp;
	uint_t		i;
	dladm_status_t	status = DLADM_STATUS_OK;

	wrp = malloc(WLDP_BUFSIZE);
	if (wrp == NULL)
		return (DLADM_STATUS_NOMEM);

	status = i_dladm_wlan_param(handle, linkid, wrp, id, WLDP_BUFSIZE,
	    B_FALSE);
	if (status != DLADM_STATUS_OK)
		goto done;

	if (wrp->wl_rates_num > *val_cnt) {
		status = DLADM_STATUS_TOOSMALL;
		goto done;
	}

	if (wrp->wl_rates_rates[0] == 0) {
		prop_val[0][0] = '\0';
		*val_cnt = 1;
		goto done;
	}

	for (i = 0; i < wrp->wl_rates_num; i++) {
		(void) snprintf(prop_val[i], DLADM_STRSIZE, "%.*f",
		    wrp->wl_rates_rates[i] % 2,
		    (float)wrp->wl_rates_rates[i] / 2);
	}
	*val_cnt = wrp->wl_rates_num;
	*perm_flags = MAC_PROP_PERM_RW;

done:
	free(wrp);
	return (status);
}

static dladm_status_t
do_get_rate_prop(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, datalink_media_t media,
    uint_t flags, uint_t *perm_flags)
{
	if (media != DL_WIFI) {
		return (i_dladm_speed_get(handle, pdp, linkid, prop_val,
		    val_cnt, flags, perm_flags));
	}

	return (do_get_rate_common(handle, pdp, linkid, prop_val, val_cnt,
	    MAC_PROP_WL_DESIRED_RATES, perm_flags));
}

/* ARGSUSED */
static dladm_status_t
do_get_rate_mod(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, datalink_media_t media,
    uint_t flags, uint_t *perm_flags)
{
	switch (media) {
	case DL_ETHER:
		/*
		 * Speed for ethernet links is unbounded. E.g., 802.11b
		 * links can have a speed of 5.5 Gbps.
		 */
		return (DLADM_STATUS_NOTSUP);

	case DL_WIFI:
		return (do_get_rate_common(handle, pdp, linkid, prop_val,
		    val_cnt, MAC_PROP_WL_SUPPORTED_RATES, perm_flags));
	default:
		return (DLADM_STATUS_BADARG);
	}
}

static dladm_status_t
do_set_rate(dladm_handle_t handle, datalink_id_t linkid,
    dladm_wlan_rates_t *rates)
{
	int		i;
	uint_t		len;
	wl_rates_t	*wrp;
	dladm_status_t	status = DLADM_STATUS_OK;

	wrp = malloc(WLDP_BUFSIZE);
	if (wrp == NULL)
		return (DLADM_STATUS_NOMEM);

	bzero(wrp, WLDP_BUFSIZE);
	for (i = 0; i < rates->wr_cnt; i++)
		wrp->wl_rates_rates[i] = rates->wr_rates[i];
	wrp->wl_rates_num = rates->wr_cnt;

	len = offsetof(wl_rates_t, wl_rates_rates) +
	    (rates->wr_cnt * sizeof (char)) + WIFI_BUF_OFFSET;
	status = i_dladm_wlan_param(handle, linkid, wrp,
	    MAC_PROP_WL_DESIRED_RATES, len, B_TRUE);

	free(wrp);
	return (status);
}

/* ARGSUSED */
static dladm_status_t
do_set_rate_prop(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    val_desc_t *vdp, uint_t val_cnt, uint_t flags, datalink_media_t media)
{
	dladm_wlan_rates_t	rates;
	dladm_status_t		status;

	/*
	 * can currently set rate on WIFI links only.
	 */
	if (media != DL_WIFI)
		return (DLADM_STATUS_PROPRDONLY);

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	rates.wr_cnt = 1;
	rates.wr_rates[0] = vdp[0].vd_val;

	status = do_set_rate(handle, linkid, &rates);

done:
	return (status);
}

/* ARGSUSED */
static dladm_status_t
do_check_rate(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t val_cnt, val_desc_t *vdp, datalink_media_t media)
{
	int		i;
	uint_t		modval_cnt = MAX_SUPPORT_RATES;
	char		*buf, **modval;
	dladm_status_t	status;
	uint_t 		perm_flags;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	buf = malloc((sizeof (char *) + DLADM_STRSIZE) *
	    MAX_SUPPORT_RATES);
	if (buf == NULL) {
		status = DLADM_STATUS_NOMEM;
		goto done;
	}

	modval = (char **)(void *)buf;
	for (i = 0; i < MAX_SUPPORT_RATES; i++) {
		modval[i] = buf + sizeof (char *) * MAX_SUPPORT_RATES +
		    i * DLADM_STRSIZE;
	}

	status = do_get_rate_mod(handle, NULL, linkid, modval, &modval_cnt,
	    media, 0, &perm_flags);
	if (status != DLADM_STATUS_OK)
		goto done;

	for (i = 0; i < modval_cnt; i++) {
		if (strcasecmp(*prop_val, modval[i]) == 0) {
			vdp->vd_val = (uintptr_t)(uint_t)
			    (atof(*prop_val) * 2);
			status = DLADM_STATUS_OK;
			break;
		}
	}
	if (i == modval_cnt)
		status = DLADM_STATUS_BADVAL;
done:
	free(buf);
	return (status);
}

static dladm_status_t
do_get_phyconf(dladm_handle_t handle, datalink_id_t linkid, void *buf,
    int buflen)
{
	return (i_dladm_wlan_param(handle, linkid, buf, MAC_PROP_WL_PHY_CONFIG,
	    buflen, B_FALSE));
}

/* ARGSUSED */
static dladm_status_t
do_get_channel_prop(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	uint32_t	channel;
	char		buf[WLDP_BUFSIZE];
	dladm_status_t	status = DLADM_STATUS_OK;
	wl_phy_conf_t	wl_phy_conf;

	if ((status = do_get_phyconf(handle, linkid, buf, sizeof (buf)))
	    != DLADM_STATUS_OK)
		goto done;

	(void) memcpy(&wl_phy_conf, buf, sizeof (wl_phy_conf));
	if (!i_dladm_wlan_convert_chan(&wl_phy_conf, &channel)) {
		status = DLADM_STATUS_NOTFOUND;
		goto done;
	}

	(void) snprintf(*prop_val, DLADM_STRSIZE, "%u", channel);
	*val_cnt = 1;
	*perm_flags = MAC_PROP_PERM_READ;
done:
	return (status);
}

static dladm_status_t
do_get_powermode(dladm_handle_t handle, datalink_id_t linkid, void *buf,
    int buflen)
{
	return (i_dladm_wlan_param(handle, linkid, buf, MAC_PROP_WL_POWER_MODE,
	    buflen, B_FALSE));
}

/* ARGSUSED */
static dladm_status_t
do_get_powermode_prop(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	wl_ps_mode_t	mode;
	const char	*s;
	char		buf[WLDP_BUFSIZE];
	dladm_status_t	status = DLADM_STATUS_OK;

	if ((status = do_get_powermode(handle, linkid, buf, sizeof (buf)))
	    != DLADM_STATUS_OK)
		goto done;

	(void) memcpy(&mode, buf, sizeof (mode));
	switch (mode.wl_ps_mode) {
	case WL_PM_AM:
		s = "off";
		break;
	case WL_PM_MPS:
		s = "max";
		break;
	case WL_PM_FAST:
		s = "fast";
		break;
	default:
		status = DLADM_STATUS_NOTFOUND;
		goto done;
	}
	(void) snprintf(*prop_val, DLADM_STRSIZE, "%s", s);
	*val_cnt = 1;
	*perm_flags = MAC_PROP_PERM_RW;
done:
	return (status);
}

static dladm_status_t
do_set_powermode(dladm_handle_t handle, datalink_id_t linkid,
    dladm_wlan_powermode_t *pm)
{
	wl_ps_mode_t    ps_mode;

	(void) memset(&ps_mode, 0xff, sizeof (ps_mode));

	switch (*pm) {
	case DLADM_WLAN_PM_OFF:
		ps_mode.wl_ps_mode = WL_PM_AM;
		break;
	case DLADM_WLAN_PM_MAX:
		ps_mode.wl_ps_mode = WL_PM_MPS;
		break;
	case DLADM_WLAN_PM_FAST:
		ps_mode.wl_ps_mode = WL_PM_FAST;
		break;
	default:
		return (DLADM_STATUS_NOTSUP);
	}
	return (i_dladm_wlan_param(handle, linkid, &ps_mode,
	    MAC_PROP_WL_POWER_MODE, sizeof (ps_mode), B_TRUE));
}

/* ARGSUSED */
static dladm_status_t
do_set_powermode_prop(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, val_desc_t *vdp, uint_t val_cnt, uint_t flags,
    datalink_media_t media)
{
	dladm_wlan_powermode_t powermode = (dladm_wlan_powermode_t)vdp->vd_val;
	dladm_status_t status;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	status = do_set_powermode(handle, linkid, &powermode);

	return (status);
}

static dladm_status_t
do_get_radio(dladm_handle_t handle, datalink_id_t linkid, void *buf, int buflen)
{
	return (i_dladm_wlan_param(handle, linkid, buf, MAC_PROP_WL_RADIO,
	    buflen, B_FALSE));
}

/* ARGSUSED */
static dladm_status_t
do_get_radio_prop(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, datalink_media_t media,
    uint_t flags, uint_t *perm_flags)
{
	wl_radio_t	radio;
	const char	*s;
	char		buf[WLDP_BUFSIZE];
	dladm_status_t	status = DLADM_STATUS_OK;

	if ((status = do_get_radio(handle, linkid, buf, sizeof (buf)))
	    != DLADM_STATUS_OK)
		goto done;

	(void) memcpy(&radio, buf, sizeof (radio));
	switch (radio) {
	case B_TRUE:
		s = "on";
		break;
	case B_FALSE:
		s = "off";
		break;
	default:
		status = DLADM_STATUS_NOTFOUND;
		goto done;
	}
	(void) snprintf(*prop_val, DLADM_STRSIZE, "%s", s);
	*val_cnt = 1;
	*perm_flags = MAC_PROP_PERM_RW;
done:
	return (status);
}

static dladm_status_t
do_set_radio(dladm_handle_t handle, datalink_id_t linkid,
    dladm_wlan_radio_t *radio)
{
	wl_radio_t r;

	switch (*radio) {
	case DLADM_WLAN_RADIO_ON:
		r = B_TRUE;
		break;
	case DLADM_WLAN_RADIO_OFF:
		r = B_FALSE;
		break;
	default:
		return (DLADM_STATUS_NOTSUP);
	}
	return (i_dladm_wlan_param(handle, linkid, &r, MAC_PROP_WL_RADIO,
	    sizeof (r), B_TRUE));
}

/* ARGSUSED */
static dladm_status_t
do_set_radio_prop(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    val_desc_t *vdp, uint_t val_cnt, uint_t flags, datalink_media_t media)
{
	dladm_wlan_radio_t radio = (dladm_wlan_radio_t)vdp->vd_val;
	dladm_status_t status;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	status = do_set_radio(handle, linkid, &radio);

	return (status);
}

/* ARGSUSED */
static dladm_status_t
do_check_hoplimit(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t val_cnt, val_desc_t *vdp,
    datalink_media_t media)
{
	int32_t	hlim;
	char	*ep;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	errno = 0;
	hlim = strtol(*prop_val, &ep, 10);
	if (errno != 0 || ep == *prop_val || hlim < 1 ||
	    hlim > (int32_t)UINT8_MAX)
		return (DLADM_STATUS_BADVAL);
	vdp->vd_val = hlim;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
do_check_encaplim(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t val_cnt, val_desc_t *vdp, datalink_media_t media)
{
	int32_t	elim;
	char	*ep;

	if (media != DL_IPV6)
		return (DLADM_STATUS_BADARG);

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	errno = 0;
	elim = strtol(*prop_val, &ep, 10);
	if (errno != 0 || ep == *prop_val || elim < 0 ||
	    elim > (int32_t)UINT8_MAX)
		return (DLADM_STATUS_BADVAL);
	vdp->vd_val = elim;
	return (DLADM_STATUS_OK);
}

static dladm_status_t
i_dladm_set_linkprop_db(dladm_handle_t handle, datalink_id_t linkid,
    const char *prop_name, char **prop_val, uint_t val_cnt)
{
	char		buf[MAXLINELEN];
	int		i;
	dladm_conf_t	conf;
	dladm_status_t	status;

	status = dladm_read_conf(handle, linkid, &conf);
	if (status != DLADM_STATUS_OK)
		return (status);

	/*
	 * reset case.
	 */
	if (val_cnt == 0) {
		status = dladm_unset_conf_field(handle, conf, prop_name);
		if (status == DLADM_STATUS_OK)
			status = dladm_write_conf(handle, conf);
		goto done;
	}

	buf[0] = '\0';
	for (i = 0; i < val_cnt; i++) {
		(void) strlcat(buf, prop_val[i], MAXLINELEN);
		if (i != val_cnt - 1)
			(void) strlcat(buf, ",", MAXLINELEN);
	}

	status = dladm_set_conf_field(handle, conf, prop_name, DLADM_TYPE_STR,
	    buf);
	if (status == DLADM_STATUS_OK)
		status = dladm_write_conf(handle, conf);

done:
	dladm_destroy_conf(handle, conf);
	return (status);
}

static dladm_status_t
i_dladm_get_linkprop_db(dladm_handle_t handle, datalink_id_t linkid,
    const char *prop_name, char **prop_val, uint_t *val_cntp)
{
	char		buf[MAXLINELEN], *str;
	uint_t		cnt = 0;
	dladm_conf_t	conf;
	dladm_status_t	status;

	status = dladm_read_conf(handle, linkid, &conf);
	if (status != DLADM_STATUS_OK)
		return (status);

	status = dladm_get_conf_field(handle, conf, prop_name, buf, MAXLINELEN);
	if (status != DLADM_STATUS_OK)
		goto done;

	str = strtok(buf, ",");
	while (str != NULL) {
		if (cnt == *val_cntp) {
			status = DLADM_STATUS_TOOSMALL;
			goto done;
		}
		(void) strlcpy(prop_val[cnt++], str, DLADM_PROP_VAL_MAX);
		str = strtok(NULL, ",");
	}

	*val_cntp = cnt;

done:
	dladm_destroy_conf(handle, conf);
	return (status);
}

/*
 * Walk persistent private link properties of a link.
 */
static dladm_status_t
i_dladm_walk_linkprop_priv_db(dladm_handle_t handle, datalink_id_t linkid,
    void *arg, int (*func)(dladm_handle_t, datalink_id_t, const char *, void *))
{
	dladm_status_t		status;
	dladm_conf_t		conf;
	char			last_attr[MAXLINKATTRLEN];
	char			attr[MAXLINKATTRLEN];
	char			attrval[MAXLINKATTRVALLEN];
	size_t			attrsz;

	if (linkid == DATALINK_INVALID_LINKID || func == NULL)
		return (DLADM_STATUS_BADARG);

	status = dladm_read_conf(handle, linkid, &conf);
	if (status != DLADM_STATUS_OK)
		return (status);

	last_attr[0] = '\0';
	while ((status = dladm_getnext_conf_linkprop(handle, conf, last_attr,
	    attr, attrval, MAXLINKATTRVALLEN, &attrsz)) == DLADM_STATUS_OK) {
		if (attr[0] == '_') {
			if (func(handle, linkid, attr, arg) ==
			    DLADM_WALK_TERMINATE)
				break;
		}
		(void) strlcpy(last_attr, attr, MAXLINKATTRLEN);
	}

	dladm_destroy_conf(handle, conf);
	return (DLADM_STATUS_OK);
}

static link_attr_t *
dladm_name2prop(const char *prop_name)
{
	link_attr_t *p;

	for (p = link_attr; p->pp_id != MAC_PROP_PRIVATE; p++) {
		if (strcmp(p->pp_name, prop_name) == 0)
			break;
	}
	return (p);
}

static link_attr_t *
dladm_id2prop(mac_prop_id_t propid)
{
	link_attr_t *p;

	for (p = link_attr; p->pp_id != MAC_PROP_PRIVATE; p++) {
		if (p->pp_id == propid)
			break;
	}
	return (p);
}

static dld_ioc_macprop_t *
i_dladm_buf_alloc_impl(size_t valsize, datalink_id_t linkid,
    const char *prop_name, mac_prop_id_t propid, uint_t flags,
    dladm_status_t *status)
{
	int dsize;
	dld_ioc_macprop_t *dip;

	*status = DLADM_STATUS_OK;
	dsize = MAC_PROP_BUFSIZE(valsize);
	dip = malloc(dsize);
	if (dip == NULL) {
		*status = DLADM_STATUS_NOMEM;
		return (NULL);
	}
	bzero(dip, dsize);
	dip->pr_valsize = valsize;
	(void) strlcpy(dip->pr_name, prop_name, sizeof (dip->pr_name));
	dip->pr_version = MAC_PROP_VERSION;
	dip->pr_linkid = linkid;
	dip->pr_num = propid;
	dip->pr_flags = flags;
	return (dip);
}

static dld_ioc_macprop_t *
i_dladm_buf_alloc_by_name(size_t valsize, datalink_id_t linkid,
    const char *prop_name, uint_t flags, dladm_status_t *status)
{
	link_attr_t *p;

	p = dladm_name2prop(prop_name);
	valsize = MAX(p->pp_valsize, valsize);
	return (i_dladm_buf_alloc_impl(valsize, linkid, prop_name, p->pp_id,
	    flags, status));
}

static dld_ioc_macprop_t *
i_dladm_buf_alloc_by_id(size_t valsize, datalink_id_t linkid,
    mac_prop_id_t propid, uint_t flags, dladm_status_t *status)
{
	link_attr_t *p;

	p = dladm_id2prop(propid);
	valsize = MAX(p->pp_valsize, valsize);
	return (i_dladm_buf_alloc_impl(valsize, linkid, p->pp_name, propid,
	    flags, status));
}

/* ARGSUSED */
static dladm_status_t
i_dladm_set_public_prop(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, val_desc_t *vdp, uint_t val_cnt, uint_t flags,
    datalink_media_t media)
{
	dld_ioc_macprop_t	*dip;
	dladm_status_t	status = DLADM_STATUS_OK;
	uint8_t		u8;
	uint16_t	u16;
	uint32_t	u32;
	void		*val;

	dip = i_dladm_buf_alloc_by_name(0, linkid, pdp->pd_name, 0, &status);
	if (dip == NULL)
		return (status);

	if (pdp->pd_flags & PD_CHECK_ALLOC)
		val = (void *)vdp->vd_val;
	else {
		/*
		 * Currently all 1/2/4-byte size properties are byte/word/int.
		 * No need (yet) to distinguish these from arrays of same size.
		 */
		switch (dip->pr_valsize) {
		case 1:
			u8 = vdp->vd_val;
			val = &u8;
			break;
		case 2:
			u16 = vdp->vd_val;
			val = &u16;
			break;
		case 4:
			u32 = vdp->vd_val;
			val = &u32;
			break;
		default:
			val = &vdp->vd_val;
			break;
		}
	}

	if (val != NULL)
		(void) memcpy(dip->pr_val, val, dip->pr_valsize);
	else
		dip->pr_valsize = 0;

	status = i_dladm_macprop(handle, dip, B_TRUE);

done:
	free(dip);
	return (status);
}

dladm_status_t
i_dladm_macprop(dladm_handle_t handle, void *dip, boolean_t set)
{
	dladm_status_t status = DLADM_STATUS_OK;

	if (ioctl(dladm_dld_fd(handle),
	    (set ? DLDIOC_SETMACPROP : DLDIOC_GETMACPROP), dip))
		status = dladm_errno2status(errno);

	return (status);
}

static dld_ioc_macprop_t *
i_dladm_get_public_prop(dladm_handle_t handle, datalink_id_t linkid,
    char *prop_name, uint_t flags, dladm_status_t *status, uint_t *perm_flags)
{
	dld_ioc_macprop_t *dip = NULL;

	dip = i_dladm_buf_alloc_by_name(0, linkid, prop_name, flags, status);
	if (dip == NULL)
		return (NULL);

	*status = i_dladm_macprop(handle, dip, B_FALSE);
	if (*status != DLADM_STATUS_OK) {
		free(dip);
		return (NULL);
	}
	if (perm_flags != NULL)
		*perm_flags = dip->pr_perm_flags;

	return (dip);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_uint32_check(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t val_cnt, val_desc_t *v,
    datalink_media_t media)
{
	if (val_cnt != 1)
		return (DLADM_STATUS_BADVAL);
	v->vd_val = strtoul(prop_val[0], NULL, 0);
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_duplex_get(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	link_duplex_t   link_duplex;
	dladm_status_t  status;

	if ((status = dladm_get_single_mac_stat(handle, linkid, "link_duplex",
	    KSTAT_DATA_UINT32, &link_duplex)) != 0)
		return (status);

	switch (link_duplex) {
	case LINK_DUPLEX_FULL:
		(void) strcpy(*prop_val, "full");
		break;
	case LINK_DUPLEX_HALF:
		(void) strcpy(*prop_val, "half");
		break;
	default:
		(void) strcpy(*prop_val, "unknown");
		break;
	}
	*val_cnt = 1;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_speed_get(dladm_handle_t handle, prop_desc_t *pdp, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, uint_t flags, uint_t *perm_flags)
{
	uint64_t	ifspeed = 0;
	dladm_status_t status;

	if ((status = dladm_get_single_mac_stat(handle, linkid, "ifspeed",
	    KSTAT_DATA_UINT64, &ifspeed)) != 0)
		return (status);

	if ((ifspeed % 1000000) != 0) {
		(void) snprintf(*prop_val, DLADM_PROP_VAL_MAX,
		    "%llf", ifspeed / (float)1000000); /* Mbps */
	} else {
		(void) snprintf(*prop_val, DLADM_PROP_VAL_MAX,
		    "%llu", ifspeed / 1000000); /* Mbps */
	}
	*val_cnt = 1;
	*perm_flags = MAC_PROP_PERM_READ;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_status_get(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	link_state_t		link_state;
	dladm_status_t		status;

	status = i_dladm_get_state(handle, linkid, &link_state);
	if (status != DLADM_STATUS_OK)
		return (status);

	switch (link_state) {
	case LINK_STATE_UP:
		(void) strcpy(*prop_val, "up");
		break;
	case LINK_STATE_DOWN:
		(void) strcpy(*prop_val, "down");
		break;
	default:
		(void) strcpy(*prop_val, "unknown");
		break;
	}
	*val_cnt = 1;
	*perm_flags = MAC_PROP_PERM_READ;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_binary_get(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t *dip;
	dladm_status_t status;

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (dip == NULL)
		return (status);

	(void) snprintf(*prop_val, DLADM_PROP_VAL_MAX, "%x", dip->pr_val[0]);
	free(dip);
	*val_cnt = 1;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_uint32_get(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t *dip;
	uint32_t v = 0;
	uchar_t *cp;
	dladm_status_t status;

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (dip == NULL)
		return (status);

	cp = (uchar_t *)dip->pr_val;
	(void) memcpy(&v, cp, sizeof (v));
	(void) snprintf(*prop_val, DLADM_PROP_VAL_MAX, "%ld", v);
	free(dip);
	*val_cnt = 1;
	return (DLADM_STATUS_OK);
}

/*
 * Determines the size of the structure that needs to be sent to drivers
 * for retrieving the property range values.
 */
static int
i_dladm_range_size(mac_propval_range_t *r, size_t *sz)
{
	uint_t count = r->mpr_count;

	*sz = sizeof (mac_propval_range_t);
	--count;

	switch (r->mpr_type) {
	case MAC_PROPVAL_UINT32:
		*sz += (count * sizeof (mac_propval_uint32_range_t));
		return (0);
	default:
		break;
	}
	*sz = 0;
	return (EINVAL);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_range_get(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t *dip;
	dladm_status_t status = DLADM_STATUS_OK;
	size_t	sz;
	mac_propval_range_t *rangep;

	sz = sizeof (mac_propval_range_t);

	/*
	 * As caller we don't know number of value ranges, the driver
	 * supports. To begin with we assume that number to be 1. If the
	 * buffer size is insufficient, driver returns back with the
	 * actual count of value ranges. See mac.h for more details.
	 */
retry:
	if ((dip = i_dladm_buf_alloc_by_name(sz, linkid, pdp->pd_name, flags,
	    &status)) == NULL)
		return (status);

	status = i_dladm_macprop(handle, dip, B_FALSE);
	if (status != DLADM_STATUS_OK) {
		if (status == DLADM_STATUS_TOOSMALL) {
			int err;

			rangep = (mac_propval_range_t *)(void *)&dip->pr_val;
			if ((err = i_dladm_range_size(rangep, &sz)) == 0) {
				free(dip);
				goto retry;
			} else {
				status = dladm_errno2status(err);
			}
		}
		free(dip);
		return (status);
	}
	rangep = (mac_propval_range_t *)(void *)&dip->pr_val;

	switch (rangep->mpr_type) {
	case MAC_PROPVAL_UINT32: {
		mac_propval_uint32_range_t *ur;
		uint_t	count = rangep->mpr_count, i;

		ur = &rangep->range_uint32[0];

		for (i = 0; i < count; i++, ur++) {
			if (ur->mpur_min == ur->mpur_max) {
				(void) snprintf(prop_val[i], DLADM_PROP_VAL_MAX,
				    "%ld", ur->mpur_min);
			} else {
				(void) snprintf(prop_val[i], DLADM_PROP_VAL_MAX,
				    "%ld-%ld", ur->mpur_min, ur->mpur_max);
			}
		}
		*val_cnt = count;
		break;
	}
	default:
		status = DLADM_STATUS_BADARG;
		break;
	}
	free(dip);
	return (status);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_tagmode_get(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t	*dip;
	link_tagmode_t		mode;
	dladm_status_t		status;

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (dip == NULL)
		return (status);
	(void) memcpy(&mode, dip->pr_val, sizeof (mode));
	free(dip);

	switch (mode) {
	case LINK_TAGMODE_NORMAL:
		(void) strlcpy(*prop_val, "normal", DLADM_PROP_VAL_MAX);
		break;
	case LINK_TAGMODE_VLANONLY:
		(void) strlcpy(*prop_val, "vlanonly", DLADM_PROP_VAL_MAX);
		break;
	default:
		(void) strlcpy(*prop_val, "unknown", DLADM_PROP_VAL_MAX);
	}
	*val_cnt = 1;
	return (DLADM_STATUS_OK);
}

/* ARGSUSED */
static dladm_status_t
i_dladm_flowctl_get(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dld_ioc_macprop_t *dip;
	link_flowctrl_t v;
	dladm_status_t status;
	uchar_t *cp;

	dip = i_dladm_get_public_prop(handle, linkid, pdp->pd_name, flags,
	    &status, perm_flags);
	if (dip == NULL)
		return (status);

	cp = (uchar_t *)dip->pr_val;
	(void) memcpy(&v, cp, sizeof (v));
	switch (v) {
	case LINK_FLOWCTRL_NONE:
		(void) sprintf(*prop_val, "no");
		break;
	case LINK_FLOWCTRL_RX:
		(void) sprintf(*prop_val, "rx");
		break;
	case LINK_FLOWCTRL_TX:
		(void) sprintf(*prop_val, "tx");
		break;
	case LINK_FLOWCTRL_BI:
		(void) sprintf(*prop_val, "bi");
		break;
	}
	free(dip);
	*val_cnt = 1;
	return (DLADM_STATUS_OK);
}


/* ARGSUSED */
static dladm_status_t
i_dladm_set_private_prop(dladm_handle_t handle, datalink_id_t linkid,
    const char *prop_name, char **prop_val, uint_t val_cnt, uint_t flags)

{
	int		i, slen;
	int 		bufsize = 0;
	dld_ioc_macprop_t *dip = NULL;
	uchar_t 	*dp;
	link_attr_t *p;
	dladm_status_t	status = DLADM_STATUS_OK;

	if ((prop_name == NULL && prop_val != NULL) ||
	    (prop_val != NULL && val_cnt == 0))
		return (DLADM_STATUS_BADARG);
	p = dladm_name2prop(prop_name);
	if (p->pp_id != MAC_PROP_PRIVATE)
		return (DLADM_STATUS_BADARG);

	if (!(flags & DLADM_OPT_ACTIVE))
		return (DLADM_STATUS_OK);

	/*
	 * private properties: all parsing is done in the kernel.
	 * allocate a enough space for each property + its separator (',').
	 */
	for (i = 0; i < val_cnt; i++) {
		bufsize += strlen(prop_val[i]) + 1;
	}

	if (prop_val == NULL) {
		/*
		 * getting default value. so use more buffer space.
		 */
		bufsize += DLADM_PROP_BUF_CHUNK;
	}

	dip = i_dladm_buf_alloc_by_name(bufsize + 1, linkid, prop_name,
	    (prop_val != NULL ? 0 : MAC_PROP_DEFAULT), &status);
	if (dip == NULL)
		return (status);

	dp = (uchar_t *)dip->pr_val;
	slen = 0;

	if (prop_val == NULL) {
		status = i_dladm_macprop(handle, dip, B_FALSE);
		dip->pr_flags = 0;
	} else {
		for (i = 0; i < val_cnt; i++) {
			int plen = 0;

			plen = strlen(prop_val[i]);
			bcopy(prop_val[i], dp, plen);
			slen += plen;
			/*
			 * add a "," separator and update dp.
			 */
			if (i != (val_cnt -1))
				dp[slen++] = ',';
			dp += (plen + 1);
		}
	}
	if (status == DLADM_STATUS_OK)
		status = i_dladm_macprop(handle, dip, B_TRUE);

	free(dip);
	return (status);
}

static dladm_status_t
i_dladm_get_priv_prop(dladm_handle_t handle, datalink_id_t linkid,
    const char *prop_name, char **prop_val, uint_t *val_cnt,
    dladm_prop_type_t type, uint_t dld_flags)
{
	dladm_status_t	status = DLADM_STATUS_OK;
	dld_ioc_macprop_t *dip = NULL;
	link_attr_t *p;

	if ((prop_name == NULL && prop_val != NULL) ||
	    (prop_val != NULL && val_cnt == 0))
		return (DLADM_STATUS_BADARG);

	p = dladm_name2prop(prop_name);
	if (p->pp_id != MAC_PROP_PRIVATE)
		return (DLADM_STATUS_BADARG);

	/*
	 * private properties: all parsing is done in the kernel.
	 */
	dip = i_dladm_buf_alloc_by_name(DLADM_PROP_BUF_CHUNK, linkid, prop_name,
	    dld_flags, &status);
	if (dip == NULL)
		return (status);

	if ((status = i_dladm_macprop(handle, dip, B_FALSE)) ==
	    DLADM_STATUS_OK) {
		if (type == DLADM_PROP_VAL_PERM) {
			(void) dladm_perm2str(dip->pr_perm_flags, *prop_val);
		} else if (type == DLADM_PROP_VAL_MODIFIABLE) {
			*prop_val[0] = '\0';
		} else {
			(void) strncpy(*prop_val, dip->pr_val,
			    DLADM_PROP_VAL_MAX);
		}
		*val_cnt = 1;
	} else if ((status == DLADM_STATUS_NOTSUP) &&
	    (type == DLADM_PROP_VAL_CURRENT)) {
		status = DLADM_STATUS_NOTFOUND;
	}
	free(dip);
	return (status);
}


static dladm_status_t
i_dladm_getset_defval(dladm_handle_t handle, prop_desc_t *pdp,
    datalink_id_t linkid, datalink_media_t media, uint_t flags)
{
	dladm_status_t status;
	char **prop_vals = NULL, *buf;
	size_t bufsize;
	uint_t cnt;
	int i;
	uint_t perm_flags;

	/*
	 * Allocate buffer needed for prop_vals array. We can have at most
	 * DLADM_MAX_PROP_VALCNT char *prop_vals[] entries, where
	 * each entry has max size DLADM_PROP_VAL_MAX
	 */
	bufsize =
	    (sizeof (char *) + DLADM_PROP_VAL_MAX) * DLADM_MAX_PROP_VALCNT;
	buf = malloc(bufsize);
	prop_vals = (char **)(void *)buf;
	for (i = 0; i < DLADM_MAX_PROP_VALCNT; i++) {
		prop_vals[i] = buf +
		    sizeof (char *) * DLADM_MAX_PROP_VALCNT +
		    i * DLADM_PROP_VAL_MAX;
	}

	/*
	 * For properties which have pdp->pd_defval.vd_name as a non-empty
	 * string, the "" itself is used to reset the property (exceptions
	 * are zone and autopush, which populate vdp->vd_val). So
	 * libdladm can copy pdp->pd_defval over to the val_desc_t passed
	 * down on the setprop using the global values in the table. For
	 * other cases (vd_name is ""), doing reset-linkprop will cause
	 * libdladm to do a getprop to find the default value and then do
	 * a setprop to reset the value to default.
	 */
	status = pdp->pd_get(handle, pdp, linkid, prop_vals, &cnt, media,
	    MAC_PROP_DEFAULT, &perm_flags);
	if (status == DLADM_STATUS_OK) {
		if (perm_flags == MAC_PROP_PERM_RW) {
			status = i_dladm_set_single_prop(handle, linkid,
			    pdp->pd_class, media, pdp, prop_vals, cnt, flags);
		}
		else
			status = DLADM_STATUS_NOTSUP;
	}
	free(buf);
	return (status);
}

/* ARGSUSED */
static dladm_status_t
get_stp_prop(dladm_handle_t handle, struct prop_desc *pd, datalink_id_t linkid,
    char **prop_val, uint_t *val_cnt, datalink_media_t media, uint_t flags,
    uint_t *perm_flags)
{
	const bridge_public_prop_t *bpp;
	dladm_status_t retv;
	int val, i;

	if (flags != 0)
		return (DLADM_STATUS_NOTSUP);
	*perm_flags = MAC_PROP_PERM_RW;
	*val_cnt = 1;
	for (bpp = bridge_prop; bpp->bpp_name != NULL; bpp++)
		if (strcmp(bpp->bpp_name, pd->pd_name) == 0)
			break;
	retv = dladm_bridge_get_port_cfg(handle, linkid, bpp->bpp_code, &val);
	/* If the daemon isn't running, then return the persistent value */
	if (retv == DLADM_STATUS_NOTFOUND) {
		if (i_dladm_get_linkprop_db(handle, linkid, pd->pd_name,
		    prop_val, val_cnt) != DLADM_STATUS_OK)
			(void) strlcpy(*prop_val, pd->pd_defval.vd_name,
			    DLADM_PROP_VAL_MAX);
		return (DLADM_STATUS_OK);
	}
	if (retv != DLADM_STATUS_OK) {
		(void) strlcpy(*prop_val, "?", DLADM_PROP_VAL_MAX);
		return (retv);
	}
	if (val == pd->pd_defval.vd_val && pd->pd_defval.vd_name[0] != '\0') {
		(void) strlcpy(*prop_val, pd->pd_defval.vd_name,
		    DLADM_PROP_VAL_MAX);
		return (DLADM_STATUS_OK);
	}
	for (i = 0; i < pd->pd_noptval; i++) {
		if (val == pd->pd_optval[i].vd_val) {
			(void) strlcpy(*prop_val, pd->pd_optval[i].vd_name,
			    DLADM_PROP_VAL_MAX);
			return (DLADM_STATUS_OK);
		}
	}
	(void) snprintf(*prop_val, DLADM_PROP_VAL_MAX, "%u", (unsigned)val);
	return (DLADM_STATUS_OK);
}

/* ARGSUSED1 */
static dladm_status_t
set_stp_prop(dladm_handle_t handle, prop_desc_t *pd, datalink_id_t linkid,
    val_desc_t *vdp, uint_t val_cnt, uint_t flags, datalink_media_t media)
{
	/*
	 * Special case for mcheck: the daemon resets the value to zero, and we
	 * don't want the daemon to refresh itself; it leads to deadlock.
	 */
	if (flags & DLADM_OPT_NOREFRESH)
		return (DLADM_STATUS_OK);

	/* Tell the running daemon, if any */
	return (dladm_bridge_refresh(handle, linkid));
}

/*
 * This is used only for stp_priority, stp_cost, and stp_mcheck.
 */
/* ARGSUSED */
static dladm_status_t
check_stp_prop(dladm_handle_t handle, struct prop_desc *pd,
    datalink_id_t linkid, char **prop_val, uint_t val_cnt, val_desc_t *vdp,
    datalink_media_t media)
{
	char *cp;
	boolean_t iscost;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	if (prop_val == NULL) {
		vdp->vd_val = 0;
	} else {
		/* Only stp_priority and stp_cost use this function */
		iscost = strcmp(pd->pd_name, "stp_cost") == 0;

		if (iscost && strcmp(prop_val[0], "auto") == 0) {
			/* Illegal value 0 is allowed to mean "automatic" */
			vdp->vd_val = 0;
		} else {
			errno = 0;
			vdp->vd_val = strtoul(prop_val[0], &cp, 0);
			if (errno != 0 || *cp != '\0')
				return (DLADM_STATUS_BADVAL);
		}
	}

	if (iscost) {
		return (vdp->vd_val > 65535 ? DLADM_STATUS_BADVAL :
		    DLADM_STATUS_OK);
	} else {
		if (vdp->vd_val > 255)
			return (DLADM_STATUS_BADVAL);
		/*
		 * If the user is setting stp_mcheck non-zero, then (per the
		 * IEEE management standards and UNH testing) we need to check
		 * whether this link is part of a bridge that is running RSTP.
		 * If it's not, then setting the flag is an error.  Note that
		 * errors are intentionally discarded here; it's the value
		 * that's the problem -- it's not a bad value, merely one that
		 * can't be used now.
		 */
		if (strcmp(pd->pd_name, "stp_mcheck") == 0 &&
		    vdp->vd_val != 0) {
			char bridge[MAXLINKNAMELEN];
			UID_STP_CFG_T cfg;
			dladm_bridge_prot_t brprot;

			if (dladm_bridge_getlink(handle, linkid, bridge,
			    sizeof (bridge)) != DLADM_STATUS_OK ||
			    dladm_bridge_get_properties(bridge, &cfg,
			    &brprot) != DLADM_STATUS_OK)
				return (DLADM_STATUS_FAILED);
			if (cfg.force_version <= 1)
				return (DLADM_STATUS_FAILED);
		}
		return (DLADM_STATUS_OK);
	}
}

/* ARGSUSED */
static dladm_status_t
get_bridge_forward(dladm_handle_t handle, struct prop_desc *pd,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dladm_status_t retv;
	uint_t val;

	if (flags != 0)
		return (DLADM_STATUS_NOTSUP);
	*perm_flags = MAC_PROP_PERM_RW;
	*val_cnt = 1;
	retv = dladm_bridge_get_forwarding(handle, linkid, &val);
	if (retv == DLADM_STATUS_NOTFOUND) {
		if (i_dladm_get_linkprop_db(handle, linkid, pd->pd_name,
		    prop_val, val_cnt) != DLADM_STATUS_OK)
			(void) strlcpy(*prop_val, pd->pd_defval.vd_name,
			    DLADM_PROP_VAL_MAX);
		return (DLADM_STATUS_OK);
	}
	if (retv == DLADM_STATUS_OK)
		(void) snprintf(*prop_val, DLADM_PROP_VAL_MAX, "%u", val);
	else
		(void) strlcpy(*prop_val, "?", DLADM_PROP_VAL_MAX);
	return (retv);
}

/* ARGSUSED */
static dladm_status_t
set_bridge_forward(dladm_handle_t handle, prop_desc_t *pd, datalink_id_t linkid,
    val_desc_t *vdp, uint_t val_cnt, uint_t flags, datalink_media_t media)
{
	/* Tell the running daemon, if any */
	return (dladm_bridge_refresh(handle, linkid));
}

/* ARGSUSED */
static dladm_status_t
get_bridge_pvid(dladm_handle_t handle, struct prop_desc *pd,
    datalink_id_t linkid, char **prop_val, uint_t *val_cnt,
    datalink_media_t media, uint_t flags, uint_t *perm_flags)
{
	dladm_status_t status;
	dld_ioc_macprop_t *dip;
	uint16_t pvid;

	if (flags != 0)
		return (DLADM_STATUS_NOTSUP);
	*perm_flags = MAC_PROP_PERM_RW;
	*val_cnt = 1;
	dip = i_dladm_buf_alloc_by_id(sizeof (uint16_t), linkid, MAC_PROP_PVID,
	    0, &status);
	if (dip == NULL)
		return (status);
	status = i_dladm_macprop(handle, dip, B_FALSE);
	if (status == DLADM_STATUS_OK) {
		(void) memcpy(&pvid, dip->pr_val, sizeof (pvid));
		(void) snprintf(*prop_val, DLADM_PROP_VAL_MAX, "%u", pvid);
	} else {
		(void) strlcpy(*prop_val, "?", DLADM_PROP_VAL_MAX);
	}
	free(dip);
	return (status);
}

/* ARGSUSED */
static dladm_status_t
set_bridge_pvid(dladm_handle_t handle, prop_desc_t *pd, datalink_id_t linkid,
    val_desc_t *vdp, uint_t val_cnt, uint_t flags, datalink_media_t media)
{
	dladm_status_t status;
	dld_ioc_macprop_t *dip;
	uint16_t pvid;

	dip = i_dladm_buf_alloc_by_id(sizeof (uint16_t), linkid, MAC_PROP_PVID,
	    0, &status);
	if (dip == NULL)
		return (status);
	pvid = vdp->vd_val;
	(void) memcpy(dip->pr_val, &pvid, sizeof (pvid));
	status = i_dladm_macprop(handle, dip, B_TRUE);
	free(dip);
	if (status != DLADM_STATUS_OK)
		return (status);

	/* Tell the running daemon, if any */
	return (dladm_bridge_refresh(handle, linkid));
}

/* ARGSUSED */
static dladm_status_t
check_bridge_pvid(dladm_handle_t handle, struct prop_desc *pd,
    datalink_id_t linkid, char **prop_val, uint_t val_cnt, val_desc_t *vdp,
    datalink_media_t media)
{
	char *cp;

	if (val_cnt != 1)
		return (DLADM_STATUS_BADVALCNT);

	if (prop_val == NULL) {
		vdp->vd_val = 1;
	} else {
		errno = 0;
		vdp->vd_val = strtoul(prop_val[0], &cp, 0);
		if (errno != 0 || *cp != '\0')
			return (DLADM_STATUS_BADVAL);
	}

	return (vdp->vd_val > VLAN_ID_MAX ? DLADM_STATUS_BADVAL :
	    DLADM_STATUS_OK);
}

dladm_status_t
i_dladm_wlan_param(dladm_handle_t handle, datalink_id_t linkid, void *buf,
    mac_prop_id_t cmd, size_t len, boolean_t set)
{
	uint32_t		flags;
	dladm_status_t		status;
	uint32_t		media;
	dld_ioc_macprop_t	*dip;
	void			*dp;

	if ((status = dladm_datalink_id2info(handle, linkid, &flags, NULL,
	    &media, NULL, 0)) != DLADM_STATUS_OK) {
		return (status);
	}

	if (media != DL_WIFI)
		return (DLADM_STATUS_BADARG);

	if (!(flags & DLADM_OPT_ACTIVE))
		return (DLADM_STATUS_TEMPONLY);

	if (len == (MAX_BUF_LEN - WIFI_BUF_OFFSET))
		len = MAX_BUF_LEN - sizeof (dld_ioc_macprop_t) - 1;

	dip = i_dladm_buf_alloc_by_id(len, linkid, cmd, 0, &status);
	if (dip == NULL)
		return (DLADM_STATUS_NOMEM);

	dp = (uchar_t *)dip->pr_val;
	if (set)
		(void) memcpy(dp, buf, len);

	status = i_dladm_macprop(handle, dip, set);
	if (status == DLADM_STATUS_OK) {
		if (!set)
			(void) memcpy(buf, dp, len);
	}

	free(dip);
	return (status);
}

dladm_status_t
dladm_parse_link_props(char *str, dladm_arg_list_t **listp, boolean_t novalues)
{
	return (dladm_parse_args(str, listp, novalues));
}

/*
 * Retrieve the one link property from the database
 */
/*ARGSUSED*/
static int
i_dladm_get_one_prop(dladm_handle_t handle, datalink_id_t linkid,
    const char *prop_name, void *arg)
{
	dladm_arg_list_t	*proplist = arg;
	dladm_arg_info_t	*aip = NULL;

	aip = &proplist->al_info[proplist->al_count];
	/*
	 * it is fine to point to prop_name since prop_name points to the
	 * prop_table[n].pd_name.
	 */
	aip->ai_name = prop_name;

	(void) dladm_get_linkprop(handle, linkid, DLADM_PROP_VAL_PERSISTENT,
	    prop_name, aip->ai_val, &aip->ai_count);

	if (aip->ai_count != 0)
		proplist->al_count++;

	return (DLADM_WALK_CONTINUE);
}


/*
 * Retrieve all link properties for a link from the database and
 * return a property list.
 */
dladm_status_t
dladm_link_get_proplist(dladm_handle_t handle, datalink_id_t linkid,
    dladm_arg_list_t **listp)
{
	dladm_arg_list_t	*list;
	dladm_status_t		status = DLADM_STATUS_OK;

	list = calloc(1, sizeof (dladm_arg_list_t));
	if (list == NULL)
		return (dladm_errno2status(errno));

	status = dladm_walk_linkprop(handle, linkid, list,
	    i_dladm_get_one_prop);

	*listp = list;
	return (status);
}

/*
 * Retrieve the named property from a proplist, check the value and
 * convert to a kernel structure.
 */
static dladm_status_t
i_dladm_link_proplist_extract_one(dladm_handle_t handle,
    dladm_arg_list_t *proplist, const char *name, void *arg)
{
	dladm_status_t		status;
	dladm_arg_info_t	*aip = NULL;
	int			i, j;

	/* Find named property in proplist */
	for (i = 0; i < proplist->al_count; i++) {
		aip = &proplist->al_info[i];
		if (strcasecmp(aip->ai_name, name) == 0)
			break;
	}

	/* Property not in list */
	if (i == proplist->al_count)
		return (DLADM_STATUS_OK);

	for (i = 0; i < DLADM_MAX_PROPS; i++) {
		prop_desc_t	*pdp = &prop_table[i];
		val_desc_t	*vdp;

		vdp = malloc(sizeof (val_desc_t) * aip->ai_count);
		if (vdp == NULL)
			return (DLADM_STATUS_NOMEM);

		if (strcasecmp(aip->ai_name, pdp->pd_name) != 0)
			continue;

		if (aip->ai_val == NULL)
			return (DLADM_STATUS_BADARG);

		/* Check property value */
		if (pdp->pd_check != NULL) {
			status = pdp->pd_check(handle, pdp, 0, aip->ai_val,
			    aip->ai_count, vdp, 0);
		} else {
			status = DLADM_STATUS_BADARG;
		}

		if (status != DLADM_STATUS_OK)
			return (status);

		for (j = 0; j < DLADM_MAX_RSRC_PROP; j++) {
			resource_prop_t	*rpp = &rsrc_prop_table[j];

			if (strcasecmp(aip->ai_name, rpp->rp_name) != 0)
				continue;

			/* Extract kernel structure */
			if (rpp->rp_extract != NULL) {
				status = rpp->rp_extract(vdp,
				    aip->ai_count, arg);
			} else {
				status = DLADM_STATUS_BADARG;
			}
			break;
		}

		if (status != DLADM_STATUS_OK)
			return (status);

		break;
	}
	return (status);
}

/*
 * Extract properties from a proplist and convert to mac_resource_props_t.
 */
dladm_status_t
dladm_link_proplist_extract(dladm_handle_t handle, dladm_arg_list_t *proplist,
    mac_resource_props_t *mrp)
{
	dladm_status_t	status;
	int		i;

	for (i = 0; i < DLADM_MAX_RSRC_PROP; i++) {
		status = i_dladm_link_proplist_extract_one(handle,
		    proplist, rsrc_prop_table[i].rp_name, mrp);
		if (status != DLADM_STATUS_OK)
			return (status);
	}
	return (status);
}

static const char *
dladm_perm2str(uint_t perm, char *buf)
{
	(void) snprintf(buf, DLADM_STRSIZE, "%c%c",
	    ((perm & MAC_PROP_PERM_READ) != 0) ? 'r' : '-',
	    ((perm & MAC_PROP_PERM_WRITE) != 0) ? 'w' : '-');
	return (buf);
}

dladm_status_t
i_dladm_get_state(dladm_handle_t handle, datalink_id_t linkid,
    link_state_t *state)
{
	dld_ioc_macprop_t	*dip;
	dladm_status_t		status;
	uint_t			perms;

	dip = i_dladm_get_public_prop(handle, linkid, "state", 0, &status,
	    &perms);
	if (status != DLADM_STATUS_OK)
		return (status);
	(void) memcpy(state, dip->pr_val, sizeof (*state));
	free(dip);
	return (status);
}

boolean_t
dladm_attr_is_linkprop(const char *name)
{
	/* non-property attribute names */
	const char *nonprop[] = {
		/* dlmgmtd core attributes */
		"name",
		"class",
		"media",
		FPHYMAJ,
		FPHYINST,
		FDEVNAME,

		/* other attributes for vlan, aggr, etc */
		DLADM_ATTR_NAMES
	};
	boolean_t	is_nonprop = B_FALSE;
	int		i;

	for (i = 0; i < sizeof (nonprop) / sizeof (nonprop[0]); i++) {
		if (strcmp(name, nonprop[i]) == 0) {
			is_nonprop = B_TRUE;
			break;
		}
	}

	return (!is_nonprop);
}
