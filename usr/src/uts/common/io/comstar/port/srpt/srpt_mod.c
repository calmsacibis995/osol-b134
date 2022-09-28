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

/*
 * Solaris SCSI RDMA Protocol Target (SRP) transport port provider
 * module for the COMSTAR framework.
 */

#include <sys/cpuvar.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/sysmacros.h>
#include <sys/sdt.h>
#include <sys/taskq.h>

#include <stmf.h>
#include <stmf_ioctl.h>
#include <portif.h>

#include "srp.h"
#include "srpt_impl.h"
#include "srpt_ioc.h"
#include "srpt_stp.h"
#include "srpt_cm.h"
#include "srpt_ioctl.h"

#define	SRPT_NAME_VERSION	"COMSTAR SRP Target"

/*
 * srpt_send_msg_depth - Tunable parameter that specifies the
 * maximum messages that could be in flight for a channel.
 */
uint16_t	srpt_send_msg_depth = SRPT_DEFAULT_SEND_MSG_DEPTH;

/*
 * srpt_errlevel -- determine which error conditions are logged
 */
uint_t		srpt_errlevel = SRPT_LOG_DEFAULT_LEVEL;

srpt_ctxt_t	*srpt_ctxt;

/*
 * DDI entry points.
 */
static int srpt_drv_attach(dev_info_t *, ddi_attach_cmd_t);
static int srpt_drv_detach(dev_info_t *, ddi_detach_cmd_t);
static int srpt_drv_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int srpt_drv_open(dev_t *, int, int, cred_t *);
static int srpt_drv_close(dev_t, int, int, cred_t *);
static int srpt_drv_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);

/* helper functions */
static int srpt_disable_srp_services(void);
static int srpt_enable_srp_services(void);
static int srpt_ibdma_ops_load(srpt_ibdma_ops_t *);
static void srpt_ibdma_ops_unload(srpt_ibdma_ops_t *);

extern struct mod_ops mod_miscops;

static struct cb_ops srpt_cb_ops = {
	srpt_drv_open,		/* cb_open */
	srpt_drv_close,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	srpt_drv_ioctl,		/* cb_ioctl */
	nodev,			/* cb_devmap */
	nodev,			/* cb_mmap */
	nodev,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	NULL,			/* cb_streamtab */
	D_MP,			/* cb_flag */
	CB_REV,			/* cb_rev */
	nodev,			/* cb_aread */
	nodev,			/* cb_awrite */
};

static struct dev_ops srpt_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	srpt_drv_getinfo,	/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	srpt_drv_attach,	/* devo_attach */
	srpt_drv_detach,	/* devo_detach */
	nodev,			/* devo_reset */
	&srpt_cb_ops,		/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	NULL,			/* devo_power */
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,
	SRPT_NAME_VERSION,
	&srpt_dev_ops,
};

static struct modlinkage srpt_modlinkage = {
	MODREV_1,
	&modldrv,
	NULL,
};

static char srpt_pp_name[] = "srpt";

/*
 * Prototypes
 */
static void srpt_pp_cb(stmf_port_provider_t *, int, void *, uint32_t);

/*
 * _init()
 */
int
_init(void)
{
	int status;

	/*
	 * Global one time initialization.
	 */
	srpt_ctxt = kmem_zalloc(sizeof (srpt_ctxt_t), KM_SLEEP);
	ASSERT(srpt_ctxt != NULL);
	rw_init(&srpt_ctxt->sc_rwlock, NULL, RW_DRIVER, NULL);

	/* Start-up state is DISABLED.  SMF will tell us if we should enable. */
	srpt_ctxt->sc_svc_state = SRPT_SVC_DISABLED;

	list_create(&srpt_ctxt->sc_ioc_list, sizeof (srpt_ioc_t),
	    offsetof(srpt_ioc_t, ioc_node));

	status = mod_install(&srpt_modlinkage);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_CONT, "_init, failed mod_install %d", status);
		rw_destroy(&srpt_ctxt->sc_rwlock);
		kmem_free(srpt_ctxt, sizeof (srpt_ctxt_t));
		srpt_ctxt = NULL;
	}

	return (status);
}

/*
 * _info()
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&srpt_modlinkage, modinfop));
}

/*
 * _fini()
 */
int
_fini(void)
{
	int status;

	status = mod_remove(&srpt_modlinkage);
	if (status != DDI_SUCCESS) {
		return (status);
	}

	list_destroy(&srpt_ctxt->sc_ioc_list);

	rw_destroy(&srpt_ctxt->sc_rwlock);
	kmem_free(srpt_ctxt, sizeof (srpt_ctxt_t));
	srpt_ctxt = NULL;

	return (status);
}

/*
 * DDI entry points.
 */

/*
 * srpt_getinfo()
 */
/* ARGSUSED */
static int
srpt_drv_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = srpt_ctxt->sc_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = NULL;
		return (DDI_SUCCESS);

	default:
		break;
	}
	return (DDI_FAILURE);
}

/*
 * srpt_drv_attach()
 */
static int
srpt_drv_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int		status;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	/*
	 * We only allow a single instance.
	 */
	if (ddi_get_instance(dip) != 0) {
		SRPT_DPRINTF_L1("drv_attach, error non-zero instance");
		return (DDI_FAILURE);
	}

	/*
	 * Create minor node that might ultimately be used to create
	 * targets outside of srpt.
	 */
	status = ddi_create_minor_node(dip, ddi_get_name(dip),
	    S_IFCHR, 0, DDI_PSEUDO, 0);
	if (status != DDI_SUCCESS) {
		SRPT_DPRINTF_L1("drv_attach, minor node creation error (%d)",
		    status);
		return (DDI_FAILURE);
	}

	rw_enter(&srpt_ctxt->sc_rwlock, RW_WRITER);
	srpt_ctxt->sc_dip = dip;
	rw_exit(&srpt_ctxt->sc_rwlock);

	return (DDI_SUCCESS);
}

/*
 * srpt_enable_srp_services()
 *
 * Caller must be holding the sc_rwlock as RW_WRITER.
 */
static int
srpt_enable_srp_services(void)
{
	int		status;
	srpt_ioc_t	*ioc;

	ASSERT((rw_read_locked(&srpt_ctxt->sc_rwlock)) == 0);

	SRPT_DPRINTF_L3("srpt_enable_srp_services");

	/* Register the port provider */
	srpt_ctxt->sc_pp = (stmf_port_provider_t *)
	    stmf_alloc(STMF_STRUCT_PORT_PROVIDER, 0, 0);
	srpt_ctxt->sc_pp->pp_portif_rev = PORTIF_REV_1;
	srpt_ctxt->sc_pp->pp_name = srpt_pp_name;
	srpt_ctxt->sc_pp->pp_cb   = srpt_pp_cb;
	status = stmf_register_port_provider(srpt_ctxt->sc_pp);
	if (status != STMF_SUCCESS) {
		SRPT_DPRINTF_L1("enable_srp: SRP port_provider registration"
		    " failed(%d)", status);
		goto err_exit_1;
	}

	/*
	 * Initialize IB resources, creating a list of SRP I/O Controllers.
	 */
	status = srpt_ioc_attach();
	if (status != DDI_SUCCESS) {
		SRPT_DPRINTF_L1("enable_srp: error attach I/O"
		    " Controllers (%d)", status);
		goto err_exit_2;
	}

	/* Not an error if no iocs yet; we will listen for ATTACH events */
	if (srpt_ctxt->sc_num_iocs == 0) {
		SRPT_DPRINTF_L2("enable_srp: no IB I/O Controllers found");
		return (DDI_SUCCESS);
	}

	/*
	 * For each I/O Controller register the default SCSI Target Port
	 * with STMF, and prepare profile and services.  SRP will not
	 * start until the associated LPORT is brought on-line.
	 */
	ioc = list_head(&srpt_ctxt->sc_ioc_list);

	while (ioc != NULL) {
		rw_enter(&ioc->ioc_rwlock, RW_WRITER);
		ioc->ioc_tgt_port = srpt_stp_alloc_port(ioc, ioc->ioc_guid);
		if (ioc->ioc_tgt_port == NULL) {
			SRPT_DPRINTF_L1("enable_srp: alloc SCSI"
			    " Target Port error on GUID(%016llx)",
			    (u_longlong_t)ioc->ioc_guid);
		}

		rw_exit(&ioc->ioc_rwlock);
		ioc = list_next(&srpt_ctxt->sc_ioc_list, ioc);
	}

	return (DDI_SUCCESS);

err_exit_2:
	(void) stmf_deregister_port_provider(srpt_ctxt->sc_pp);

err_exit_1:
	stmf_free(srpt_ctxt->sc_pp);
	srpt_ctxt->sc_pp = NULL;

	return (status);
}

/*
 * srpt_drv_detach()
 *
 * Refuse the detach request if we have channels open on
 * any IOC.  Users should use 'svcadm disable' to shutdown
 * active targets.
 */
/*ARGSUSED*/
static int
srpt_drv_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_DETACH:
		rw_enter(&srpt_ctxt->sc_rwlock, RW_WRITER);
		if (srpt_ctxt->sc_svc_state != SRPT_SVC_DISABLED) {
			rw_exit(&srpt_ctxt->sc_rwlock);
			return (DDI_FAILURE);
		}

		ddi_remove_minor_node(dip, NULL);
		srpt_ctxt->sc_dip = NULL;

		rw_exit(&srpt_ctxt->sc_rwlock);

		break;

	case DDI_SUSPEND:
		return (DDI_FAILURE);

	default:
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * srpt_disable_srp_services()
 *
 * Offlines all targets, deregisters all IOCs.  Caller must hold
 * the srpt_ctxt->sc_rwlock as RW_WRITER.
 */
static int
srpt_disable_srp_services(void)
{
	stmf_status_t			stmf_status;
	stmf_change_status_t		cstatus;
	srpt_ioc_t			*ioc;
	srpt_target_port_t		*tgt;
	int				ret_status = 0;

	ASSERT((rw_read_locked(&srpt_ctxt->sc_rwlock)) == 0);

	/*
	 * For each I/O Controller remove all SRP services and de-register
	 * with the associated I/O Unit's IB Device Management Agent.
	 */
	ioc = list_head(&srpt_ctxt->sc_ioc_list);

	while (ioc != NULL) {
		/*
		 * Notify STMF to take the I/O Controller SCSI Target Port(s)
		 * off-line after we mark them as disabled so that they will
		 * stay off-line.
		 */
		rw_enter(&ioc->ioc_rwlock, RW_WRITER);

		tgt = ioc->ioc_tgt_port;
		if (tgt != NULL) {
			mutex_enter(&tgt->tp_lock);
			tgt->tp_drv_disabled = 1;
			mutex_exit(&tgt->tp_lock);

			SRPT_DPRINTF_L2("disable_srp: unbind and de-register"
			    " services for GUID(%016llx)",
			    (u_longlong_t)ioc->ioc_guid);

			cstatus.st_completion_status = STMF_SUCCESS;
			cstatus.st_additional_info = NULL;

			stmf_status = stmf_ctl(STMF_CMD_LPORT_OFFLINE,
			    tgt->tp_lport, &cstatus);

			/*
			 * Wait for asynchronous target off-line operation
			 * to complete and then deregister the target
			 * port.
			 */
			mutex_enter(&tgt->tp_lock);
			while (tgt->tp_state != SRPT_TGT_STATE_OFFLINE) {
				cv_wait(&tgt->tp_offline_complete,
				    &tgt->tp_lock);
			}
			mutex_exit(&tgt->tp_lock);
			ioc->ioc_tgt_port = NULL;

			SRPT_DPRINTF_L3("disable_srp: IOC (0x%016llx) Target"
			    " SRP off-line complete",
			    (u_longlong_t)ioc->ioc_guid);

			stmf_status = srpt_stp_deregister_port(tgt);
			if (stmf_status != STMF_SUCCESS) {
				/* Fails if I/O is pending */
				if (ret_status == 0) {
					ret_status = EBUSY;
				}
				SRPT_DPRINTF_L1("disable_srp: could not"
				    " de-register LPORT, err(0x%llx)",
				    (u_longlong_t)stmf_status);
			} else {
				(void) srpt_stp_free_port(tgt);
			}
		}

		rw_exit(&ioc->ioc_rwlock);
		ioc = list_next(&srpt_ctxt->sc_ioc_list, ioc);
	}

	/* don't release IOCs until all ports are deregistered */
	if (ret_status != 0) {
		return (ret_status);
	}

	/*
	 * Release I/O Controller(s) resources and detach.
	 */
	srpt_ioc_detach();

	/* De-register ourselves as an STMF port provider */
	(void) stmf_deregister_port_provider(srpt_ctxt->sc_pp);
	stmf_free(srpt_ctxt->sc_pp);
	srpt_ctxt->sc_pp = NULL;

	return (0);
}

/*
 * srpt_drv_open()
 */
/* ARGSUSED */
static int
srpt_drv_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	SRPT_DPRINTF_L3("drv_open, invoked");
	return (0);
}

/*
 * srpt_drv_close()
 */
/* ARGSUSED */
static int
srpt_drv_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	SRPT_DPRINTF_L3("drv_close, invoked");
	return (0);
}

/*
 * srpt_drv_ioctl()
 */
/* ARGSUSED */
static int
srpt_drv_ioctl(dev_t drv, int cmd, intptr_t argp, int flag, cred_t *cred,
    int *retval)
{
	int		ret = 0;

	SRPT_DPRINTF_L3("drv_ioctl, invoked, cmd = %d", cmd);

	if (drv_priv(cred) != 0) {
		return (EPERM);
	}

	rw_enter(&srpt_ctxt->sc_rwlock, RW_WRITER);

	switch (cmd) {
		case SRPT_IOC_ENABLE_SVC:
			if (srpt_ctxt->sc_svc_state != SRPT_SVC_DISABLED) {
				break;
			}

			ret = srpt_ibdma_ops_load(&srpt_ctxt->sc_ibdma_ops);
			if (ret != 0) {
				break;
			}

			ret = srpt_enable_srp_services();
			if (ret == 0) {
				srpt_ctxt->sc_svc_state = SRPT_SVC_ENABLED;
			}

			break;

		case SRPT_IOC_DISABLE_SVC:
			if (srpt_ctxt->sc_svc_state != SRPT_SVC_ENABLED) {
				break;
			}

			ret = srpt_disable_srp_services();
			if (ret == 0) {
				srpt_ctxt->sc_svc_state = SRPT_SVC_DISABLED;
			}

			srpt_ibdma_ops_unload(&srpt_ctxt->sc_ibdma_ops);

			break;

		default:
			ret = EFAULT;
			break;
	}

	rw_exit(&srpt_ctxt->sc_rwlock);

	return (ret);
}

/*
 * srpt_pp_cb()
 */
/* ARGSUSED */
static void
srpt_pp_cb(stmf_port_provider_t *pp, int cmd, void *arg, uint32_t flags)
{
	SRPT_DPRINTF_L3("srpt_pp_cb, invoked (%d)", cmd);
	/*
	 * We don't currently utilize the port provider call-back, in the
	 * future we might use it to synchronize provider data via STMF.
	 */
}

static int
srpt_ibdma_ops_load(srpt_ibdma_ops_t *ops)
{
	int			ibdma_err = 0;

	ASSERT(ops != NULL);

	ops->ibdmah = ddi_modopen("ibdma", KRTLD_MODE_FIRST, &ibdma_err);
	if (ops->ibdmah == NULL) {
		SRPT_DPRINTF_L0("failed to open ibdma driver, error = %d",
		    ibdma_err);
		return (ibdma_err);
	}

	ops->ibdma_register = (ibdma_hdl_t (*)())ddi_modsym(ops->ibdmah,
	    "ibdma_ioc_register", &ibdma_err);
	if (ops->ibdma_register == NULL) {
		SRPT_DPRINTF_L0(
		    "failed to modsym ibdma_ioc_register, error = %d",
		    ibdma_err);
		goto done;
	}

	ops->ibdma_unregister = (ibdma_status_t (*)())ddi_modsym(ops->ibdmah,
	    "ibdma_ioc_unregister", &ibdma_err);
	if (ops->ibdma_unregister == NULL) {
		SRPT_DPRINTF_L0(
		    "failed to modsym ibdma_ioc_unregister, error = %d",
		    ibdma_err);
		goto done;
	}

	ops->ibdma_update = (ibdma_status_t (*)())ddi_modsym(ops->ibdmah,
	    "ibdma_ioc_update", &ibdma_err);
	if (ops->ibdma_update == NULL) {
		SRPT_DPRINTF_L0(
		    "failed to modsym ibdma_ioc_update, error = %d",
		    ibdma_err);
	}

done:
	if (ibdma_err != 0) {
		srpt_ibdma_ops_unload(ops);
	}

	return (ibdma_err);
}

static void
srpt_ibdma_ops_unload(srpt_ibdma_ops_t *ops)
{
	if (ops != NULL) {
		if (ops->ibdmah != NULL) {
			(void) ddi_modclose(ops->ibdmah);
		}
		bzero(ops, sizeof (srpt_ibdma_ops_t));
	}
}
