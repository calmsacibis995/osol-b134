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

/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/visual_io.h>
#include <sys/font.h>
#include <sys/fbio.h>

#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/modctl.h>
#include <sys/vgareg.h>
#include <sys/vgasubr.h>
#include <sys/pci.h>
#include <sys/kd.h>
#include <sys/ddi_impldefs.h>
#include <sys/sunldi.h>
#include <sys/agpgart.h>
#include <sys/agp/agpdefs.h>
#include <sys/agp/agpmaster_io.h>

#define	MYNAME	"vgatext"

/*
 * Each instance of this driver has 2 minor nodes:
 * 0: for common graphics operations
 * 1: for agpmaster operations
 */
#define	GFX_MINOR		0
#define	AGPMASTER_MINOR		1

#define	MY_NBITSMINOR		1
#define	DEV2INST(dev)		(getminor(dev) >> MY_NBITSMINOR)
#define	DEV2MINOR(dev)		(getminor(dev) & ((1 << MY_NBITSMINOR) - 1))
#define	INST2NODE1(inst)	((inst) << MY_NBITSMINOR + GFX_MINOR)
#define	INST2NODE2(inst)	(((inst) << MY_NBITSMINOR) + AGPMASTER_MINOR)

/* I don't know exactly where these should be defined, but this is a	*/
/* heck of a lot better than constants in the code.			*/
#define	TEXT_ROWS		25
#define	TEXT_COLS		80

#define	VGA_BRIGHT_WHITE	0x0f
#define	VGA_BLACK		0x00

#define	VGA_REG_ADDR		0x3c0
#define	VGA_REG_SIZE		0x20

#define	VGA_MEM_ADDR		0xa0000
#define	VGA_MEM_SIZE		0x20000

#define	VGA_MMAP_FB_BASE	VGA_MEM_ADDR

/*
 * This variable allows for this driver to suspend even if it
 * shouldn't.  Note that by setting it, the framebuffer will probably
 * not come back.  So use it with a serial console, or with serial
 * line debugging (say, for example, if this driver is being modified
 * to support _some_ hardware doing suspend and resume).
 */
int vgatext_force_suspend = 0;

static int vgatext_open(dev_t *, int, int, cred_t *);
static int vgatext_close(dev_t, int, int, cred_t *);
static int vgatext_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
static int vgatext_devmap(dev_t, devmap_cookie_t, offset_t, size_t,
			    size_t *, uint_t);

static 	struct cb_ops cb_vgatext_ops = {
	vgatext_open,		/* cb_open */
	vgatext_close,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	vgatext_ioctl,		/* cb_ioctl */
	vgatext_devmap,		/* cb_devmap */
	nodev,			/* cb_mmap */
	ddi_devmap_segmap,	/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* cb_stream */
	D_NEW | D_MTSAFE	/* cb_flag */
};

static int vgatext_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int vgatext_attach(dev_info_t *, ddi_attach_cmd_t);
static int vgatext_detach(dev_info_t *, ddi_detach_cmd_t);

static struct vis_identifier text_ident = { "SUNWtext" };

static struct dev_ops vgatext_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	vgatext_info,		/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	vgatext_attach,		/* devo_attach */
	vgatext_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&cb_vgatext_ops,	/* devo_cb_ops */
	(struct bus_ops *)NULL,	/* devo_bus_ops */
	NULL,			/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

struct vgatext_softc {
	struct vgaregmap 	regs;
	struct vgaregmap 	fb;
	off_t			fb_size;
	int			fb_regno;
	dev_info_t		*devi;
	int			mode;	/* KD_TEXT or KD_GRAPHICS */
	caddr_t			text_base;	/* hardware text base */
	char			shadow[TEXT_ROWS*TEXT_COLS*2];
	caddr_t			current_base;	/* hardware or shadow */
	struct {
		boolean_t visible;
		int row;
		int col;
	}			cursor;
	struct vis_polledio	polledio;
	struct {
		unsigned char red;
		unsigned char green;
		unsigned char blue;
	}			colormap[VGA8_CMAP_ENTRIES];
	unsigned char attrib_palette[VGA_ATR_NUM_PLT];
	agp_master_softc_t	*agp_master; /* NULL means not PCI, for AGP */
	ddi_acc_handle_t	*pci_cfg_hdlp;	/* PCI conf handle */
	unsigned int flags;
	kmutex_t lock;
};

#define	VGATEXT_FLAG_CONSOLE 0x00000001
#define	VGATEXT_IS_CONSOLE(softc) ((softc)->flags & VGATEXT_FLAG_CONSOLE)

static int vgatext_devinit(struct vgatext_softc *, struct vis_devinit *data);
static void	vgatext_cons_copy(struct vgatext_softc *,
			struct vis_conscopy *);
static void	vgatext_cons_display(struct vgatext_softc *,
			struct vis_consdisplay *);
static void	vgatext_cons_cursor(struct vgatext_softc *,
			struct vis_conscursor *);
static void	vgatext_polled_copy(struct vis_polledio_arg *,
			struct vis_conscopy *);
static void	vgatext_polled_display(struct vis_polledio_arg *,
			struct vis_consdisplay *);
static void	vgatext_polled_cursor(struct vis_polledio_arg *,
			struct vis_conscursor *);
static void	vgatext_init(struct vgatext_softc *);
static void	vgatext_set_text(struct vgatext_softc *);
#if	defined(USE_BORDERS)
static void	vgatext_init_graphics(struct vgatext_softc *);
#endif
static int vgatext_kdsetmode(struct vgatext_softc *softc, int mode);
static void vgatext_setfont(struct vgatext_softc *softc);
static void vgatext_get_cursor(struct vgatext_softc *softc,
		screen_pos_t *row, screen_pos_t *col);
static void vgatext_set_cursor(struct vgatext_softc *softc, int row, int col);
static void vgatext_hide_cursor(struct vgatext_softc *softc);
static void vgatext_save_colormap(struct vgatext_softc *softc);
static void vgatext_restore_colormap(struct vgatext_softc *softc);
static int vgatext_get_pci_reg_index(dev_info_t *const devi,
		unsigned long himask, unsigned long hival, unsigned long addr,
		off_t *offset);
static int vgatext_get_isa_reg_index(dev_info_t *const devi,
		unsigned long hival, unsigned long addr, off_t *offset);
static void	*vgatext_softc_head;
static char	vgatext_silent;
static char	happyface_boot;

/* Loadable Driver stuff */

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"VGA text driver",	/* Name of the module. */
	&vgatext_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *) &modldrv, NULL
};

typedef enum pc_colors {
	pc_black	= 0,
	pc_blue		= 1,
	pc_green	= 2,
	pc_cyan		= 3,
	pc_red		= 4,
	pc_magenta	= 5,
	pc_brown	= 6,
	pc_white	= 7,
	pc_grey		= 8,
	pc_brt_blue	= 9,
	pc_brt_green	= 10,
	pc_brt_cyan	= 11,
	pc_brt_red	= 12,
	pc_brt_magenta	= 13,
	pc_yellow	= 14,
	pc_brt_white	= 15
} pc_colors_t;

static const unsigned char solaris_color_to_pc_color[16] = {
	pc_brt_white,		/*  0 - brt_white	*/
	pc_black,		/*  1 - black		*/
	pc_blue,		/*  2 - blue		*/
	pc_green,		/*  3 - green		*/
	pc_cyan,		/*  4 - cyan		*/
	pc_red,			/*  5 - red		*/
	pc_magenta,		/*  6 - magenta		*/
	pc_brown,		/*  7 - brown		*/
	pc_white,		/*  8 - white		*/
	pc_grey,		/*  9 - gery		*/
	pc_brt_blue,		/* 10 - brt_blue	*/
	pc_brt_green,		/* 11 - brt_green	*/
	pc_brt_cyan,		/* 12 - brt_cyan	*/
	pc_brt_red,		/* 13 - brt_red		*/
	pc_brt_magenta,		/* 14 - brt_magenta	*/
	pc_yellow		/* 15 - yellow		*/
};

static ddi_device_acc_attr_t i8xx_dev_access = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

static ddi_device_acc_attr_t dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
};

int
_init(void)
{
	int e;

	if ((e = ddi_soft_state_init(&vgatext_softc_head,
		    sizeof (struct vgatext_softc), 1)) != 0) {
	    return (e);
	}

	e = mod_install(&modlinkage);

	if (e) {
		ddi_soft_state_fini(&vgatext_softc_head);
	}
	return (e);
}

int
_fini(void)
{
	int e;

	if ((e = mod_remove(&modlinkage)) != 0)
		return (e);

	ddi_soft_state_fini(&vgatext_softc_head);

	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/* default structure for FBIOGATTR ioctl */
static struct fbgattr vgatext_attr =  {
/*	real_type	owner */
	FBTYPE_SUNFAST_COLOR, 0,
/* fbtype: type		h  w  depth cms  size */
	{ FBTYPE_SUNFAST_COLOR, TEXT_ROWS, TEXT_COLS, 1,    256,  0 },
/* fbsattr: flags emu_type	dev_specific */
	{ 0, FBTYPE_SUN4COLOR, { 0 } },
/*	emu_types */
	{ -1 }
};

/*
 * handy macros
 */

#define	getsoftc(instance) ((struct vgatext_softc *)	\
			ddi_get_soft_state(vgatext_softc_head, (instance)))

#define	STREQ(a, b)	(strcmp((a), (b)) == 0)

static void
vgatext_check_for_console(dev_info_t *devi, struct vgatext_softc *softc,
	int pci_pcie_bus)
{
	ddi_acc_handle_t pci_conf;
	dev_info_t *pdevi;
	uint16_t data16;

	/*
	 * Based on Section 11.3, "PCI Display Subsystem Initialization",
	 * of the 1.1 PCI-to-PCI Bridge Architecture Specification
	 * determine if this is the boot console device.  First, see
	 * if the SBIOS has turned on PCI I/O for this device.  Then if
	 * this is PCI/PCI-E, verify the parent bridge has VGAEnable set.
	 */

	if (pci_config_setup(devi, &pci_conf) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    MYNAME ": can't get PCI conf handle");
		return;
	}

	data16 = pci_config_get16(pci_conf, PCI_CONF_COMM);
	if (data16 & PCI_COMM_IO)
		softc->flags |= VGATEXT_FLAG_CONSOLE;

	pci_config_teardown(&pci_conf);

	/* If IO not enabled or ISA/EISA, just return */
	if (!(softc->flags & VGATEXT_FLAG_CONSOLE) || !pci_pcie_bus)
		return;

	/*
	 * Check for VGA Enable in the Bridge Control register for all
	 * PCI/PCIEX parents.  If not set all the way up the chain,
	 * this cannot be the boot console.
	 */

	pdevi = ddi_get_parent(devi);
	while (pdevi) {
		int	error;
		ddi_acc_handle_t ppci_conf;
		char	*parent_type = NULL;

		error = ddi_prop_lookup_string(DDI_DEV_T_ANY, pdevi,
		    DDI_PROP_DONTPASS, "device_type", &parent_type);
		if (error != DDI_SUCCESS) {
			return;
		}

		/* Verify still on the PCI/PCIEX parent tree */
		if (!STREQ(parent_type, "pci") &&
		    !STREQ(parent_type, "pciex")) {
			ddi_prop_free(parent_type);
			return;
		}

		ddi_prop_free(parent_type);
		parent_type = NULL;

		if (pci_config_setup(pdevi, &ppci_conf) != DDI_SUCCESS) {
			/* No registers on root node, done with check */
			return;
		}

		data16 = pci_config_get16(ppci_conf, PCI_BCNF_BCNTRL);
		pci_config_teardown(&ppci_conf);

		if (!(data16 & PCI_BCNF_BCNTRL_VGA_ENABLE)) {
			softc->flags &= ~VGATEXT_FLAG_CONSOLE;
			return;
		}

		pdevi = ddi_get_parent(pdevi);
	}
}

static int
vgatext_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct vgatext_softc *softc;
	int	unit = ddi_get_instance(devi);
	int	error;
	char	*parent_type = NULL;
	int	reg_rnumber;
	int	agpm = 0;
	off_t	reg_offset;
	off_t	mem_offset;
	char	buf[80], *cons;
	int pci_pcie_bus = 0;


	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		/*
		 * Though vgatext doesn't really know how to resume
		 * on a generic framebuffer, we should succeed, as
		 * it is far better to have no console, than potentiall
		 * have no machine.
		 */
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	/* DDI_ATTACH */

	/* Allocate softc struct */
	if (ddi_soft_state_zalloc(vgatext_softc_head, unit) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	softc = getsoftc(unit);

	/* link it in */
	softc->devi = devi;
	ddi_set_driver_private(devi, softc);

	softc->polledio.arg = (struct vis_polledio_arg *)softc;
	softc->polledio.display = vgatext_polled_display;
	softc->polledio.copy = vgatext_polled_copy;
	softc->polledio.cursor = vgatext_polled_cursor;

	mutex_init(&(softc->lock), NULL, MUTEX_DRIVER, NULL);

	error = ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_get_parent(devi),
	    DDI_PROP_DONTPASS, "device_type", &parent_type);
	if (error != DDI_SUCCESS) {
		cmn_err(CE_WARN, MYNAME ": can't determine parent type.");
		goto fail;
	}

	if (STREQ(parent_type, "isa") || STREQ(parent_type, "eisa")) {
		reg_rnumber = vgatext_get_isa_reg_index(devi, 1, VGA_REG_ADDR,
		    &reg_offset);
		if (reg_rnumber < 0) {
			cmn_err(CE_WARN,
			    MYNAME ": can't find reg entry for registers");
			error = DDI_FAILURE;
			goto fail;
		}
		softc->fb_regno = vgatext_get_isa_reg_index(devi, 0,
		    VGA_MEM_ADDR, &mem_offset);
		if (softc->fb_regno < 0) {
			cmn_err(CE_WARN,
			    MYNAME ": can't find reg entry for memory");
			error = DDI_FAILURE;
			goto fail;
		}
	} else if (STREQ(parent_type, "pci") || STREQ(parent_type, "pciex")) {
		pci_pcie_bus = 1;
		reg_rnumber = vgatext_get_pci_reg_index(devi,
		    PCI_REG_ADDR_M|PCI_REG_REL_M,
		    PCI_ADDR_IO|PCI_RELOCAT_B, VGA_REG_ADDR,
		    &reg_offset);
		if (reg_rnumber < 0) {
			cmn_err(CE_WARN,
			    MYNAME ": can't find reg entry for registers");
			error = DDI_FAILURE;
			goto fail;
		}
		softc->fb_regno = vgatext_get_pci_reg_index(devi,
		    PCI_REG_ADDR_M|PCI_REG_REL_M,
		    PCI_ADDR_MEM32|PCI_RELOCAT_B, VGA_MEM_ADDR,
		    &mem_offset);
		if (softc->fb_regno < 0) {
			cmn_err(CE_WARN,
			    MYNAME ": can't find reg entry for memory");
			error = DDI_FAILURE;
			goto fail;
		}
		agpm = 1;	/* should have AGP master support */
	} else {
		cmn_err(CE_WARN, MYNAME ": unknown parent type \"%s\".",
		    parent_type);
		error = DDI_FAILURE;
		goto fail;
	}
	ddi_prop_free(parent_type);
	parent_type = NULL;

	error = ddi_regs_map_setup(devi, reg_rnumber,
	    (caddr_t *)&softc->regs.addr, reg_offset, VGA_REG_SIZE,
	    &dev_attr, &softc->regs.handle);
	if (error != DDI_SUCCESS)
		goto fail;
	softc->regs.mapped = B_TRUE;

	softc->fb_size = VGA_MEM_SIZE;

	error = ddi_regs_map_setup(devi, softc->fb_regno,
	    (caddr_t *)&softc->fb.addr,
	    mem_offset, softc->fb_size,
	    &dev_attr, &softc->fb.handle);
	if (error != DDI_SUCCESS)
		goto fail;
	softc->fb.mapped = B_TRUE;

	if (ddi_get8(softc->regs.handle,
	    softc->regs.addr + VGA_MISC_R) & VGA_MISC_IOA_SEL)
		softc->text_base = (caddr_t)softc->fb.addr + VGA_COLOR_BASE;
	else
		softc->text_base = (caddr_t)softc->fb.addr + VGA_MONO_BASE;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, "console", &cons) == DDI_SUCCESS) {
		if (strcmp(cons, "graphics") == 0) {
			happyface_boot = 1;
			vgatext_silent = 1;
			softc->current_base = softc->shadow;
		} else {
			softc->current_base = softc->text_base;
		}
		ddi_prop_free(cons);
	} else {
		softc->current_base = softc->text_base;
	}

	(void) sprintf(buf, "text-%d", unit);
	error = ddi_create_minor_node(devi, buf, S_IFCHR,
	    INST2NODE1(unit), DDI_NT_DISPLAY, NULL);
	if (error != DDI_SUCCESS)
		goto fail;

	error = ddi_prop_create(makedevice(DDI_MAJOR_T_UNKNOWN, unit),
	    devi, DDI_PROP_CANSLEEP, DDI_KERNEL_IOCTL, NULL, 0);
	if (error != DDI_SUCCESS)
		goto fail;

	vgatext_check_for_console(devi, softc, pci_pcie_bus);

	/* only do this if not in graphics mode */
	if ((vgatext_silent == 0) && (VGATEXT_IS_CONSOLE(softc))) {
		vgatext_init(softc);
		vgatext_save_colormap(softc);
	}

	if (agpm != 0) { /* try AGP master attach */
		/* setup mapping for PCI config space access */
		softc->pci_cfg_hdlp = (ddi_acc_handle_t *)
		    kmem_zalloc(sizeof (ddi_acc_handle_t), KM_SLEEP);
		error = pci_config_setup(devi, softc->pci_cfg_hdlp);
		if (error != DDI_SUCCESS) {
			cmn_err(CE_WARN, "vgatext_attach: "
			    "PCI configuration space setup failed");
			goto fail;
		}

		(void) agpmaster_attach(softc->devi, &softc->agp_master,
		    *softc->pci_cfg_hdlp, INST2NODE2(unit));
	}

	return (DDI_SUCCESS);

fail:
	if (parent_type != NULL)
		ddi_prop_free(parent_type);
	(void) vgatext_detach(devi, DDI_DETACH);
	return (error);
}

static int
vgatext_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int instance = ddi_get_instance(devi);
	struct vgatext_softc *softc = getsoftc(instance);


	switch (cmd) {
	case DDI_DETACH:
		if (softc->agp_master != NULL) { /* agp initiated */
			agpmaster_detach(&softc->agp_master);
			pci_config_teardown(softc->pci_cfg_hdlp);
		}

		if (softc->fb.mapped)
			ddi_regs_map_free(&softc->fb.handle);
		if (softc->regs.mapped)
			ddi_regs_map_free(&softc->regs.handle);
		mutex_destroy(&(softc->lock));
		ddi_remove_minor_node(devi, NULL);
		(void) ddi_soft_state_free(vgatext_softc_head, instance);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		/*
		 * This is a generic VGA file, and therefore, cannot
		 * understand how to deal with suspend and resume on
		 * a generic interface.  So we fail any attempt to
		 * suspend.  At some point in the future, we might use
		 * this as an entrypoint for display drivers and this
		 * assumption may change.
		 *
		 * However, from a platform development perspective,
		 * it is important that this driver suspend if a
		 * developer is using a serial console and/or working
		 * on a framebuffer driver that will support suspend
		 * and resume.  Therefore, we have this module tunable
		 * (purposely using a long name) that will allow for
		 * suspend it it is set.  Otherwise we fail.
		 */
		if (vgatext_force_suspend != 0)
			return (DDI_SUCCESS);
		else
			return (DDI_FAILURE);

	default:
		cmn_err(CE_WARN, "vgatext_detach: unknown cmd 0x%x\n", cmd);
		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
static int
vgatext_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	dev_t dev;
	int error;
	int instance;
	struct vgatext_softc *softc;

	error = DDI_SUCCESS;

	dev = (dev_t)arg;
	instance = DEV2INST(dev);
	softc = getsoftc(instance);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (softc == NULL || softc->devi == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) softc->devi;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(uintptr_t)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
		break;
	}
	return (error);
}


/*ARGSUSED*/
static int
vgatext_open(dev_t *devp, int flag, int otyp, cred_t *cred)
{
	struct vgatext_softc *softc = getsoftc(DEV2INST(*devp));

	if (softc == NULL || otyp == OTYP_BLK)
		return (ENXIO);

	return (0);
}

/*ARGSUSED*/
static int
vgatext_close(dev_t devp, int flag, int otyp, cred_t *cred)
{
	return (0);
}

static int
do_gfx_ioctl(int cmd, intptr_t data, int mode, struct vgatext_softc *softc)
{
	static char kernel_only[] =
	    "do_gfx_ioctl: %s is a kernel only ioctl";
	int err;
	int kd_mode;

	switch (cmd) {
	case KDSETMODE:
		return (vgatext_kdsetmode(softc, (int)data));

	case KDGETMODE:
		kd_mode = softc->mode;
		if (ddi_copyout(&kd_mode, (void *)data, sizeof (int), mode))
			return (EFAULT);
		break;

	case VIS_GETIDENTIFIER:
		if (ddi_copyout(&text_ident, (void *)data,
		    sizeof (struct vis_identifier), mode))
			return (EFAULT);
		break;

	case VIS_DEVINIT:

		if (!(mode & FKIOCTL)) {
			cmn_err(CE_CONT, kernel_only, "VIS_DEVINIT");
			return (ENXIO);
		}

		err = vgatext_devinit(softc, (struct vis_devinit *)data);
		if (err != 0) {
			cmn_err(CE_WARN,
			    "vgatext_ioctl:  could not initialize console");
			return (err);
		}
		break;

	case VIS_CONSCOPY:	/* move */
		{
		struct vis_conscopy pma;

		if (ddi_copyin((void *)data, &pma,
		    sizeof (struct vis_conscopy), mode))
			return (EFAULT);

		vgatext_cons_copy(softc, &pma);
		break;
		}

	case VIS_CONSDISPLAY:	/* display */
		{
		struct vis_consdisplay display_request;

		if (ddi_copyin((void *)data, &display_request,
		    sizeof (display_request), mode))
			return (EFAULT);

		vgatext_cons_display(softc, &display_request);
		break;
		}

	case VIS_CONSCURSOR:
		{
		struct vis_conscursor cursor_request;

		if (ddi_copyin((void *)data, &cursor_request,
		    sizeof (cursor_request), mode))
			return (EFAULT);

		vgatext_cons_cursor(softc, &cursor_request);

		if (cursor_request.action == VIS_GET_CURSOR &&
		    ddi_copyout(&cursor_request, (void *)data,
		    sizeof (cursor_request), mode))
			return (EFAULT);
		break;
		}

	case VIS_GETCMAP:
	case VIS_PUTCMAP:
	case FBIOPUTCMAP:
	case FBIOGETCMAP:
		/*
		 * At the moment, text mode is not considered to have
		 * a color map.
		 */
		return (EINVAL);

	case FBIOGATTR:
		if (copyout(&vgatext_attr, (void *)data,
		    sizeof (struct fbgattr)))
			return (EFAULT);
		break;

	case FBIOGTYPE:
		if (copyout(&vgatext_attr.fbtype, (void *)data,
		    sizeof (struct fbtype)))
			return (EFAULT);
		break;

	default:
		return (ENXIO);
	}
	return (0);
}


/*ARGSUSED*/
static int
vgatext_ioctl(
    dev_t dev,
    int cmd,
    intptr_t data,
    int mode,
    cred_t *cred,
    int *rval)
{
	struct vgatext_softc *softc = getsoftc(DEV2INST(dev));
	int err;

	switch (DEV2MINOR(dev)) {
	case GFX_MINOR:
		mutex_enter(&(softc->lock));
		err = do_gfx_ioctl(cmd, data, mode, softc);
		mutex_exit(&(softc->lock));
		break;

	case AGPMASTER_MINOR:
		err = agpmaster_ioctl(dev, cmd, data, mode, cred, rval,
		    softc->agp_master);
		break;

	default:
		/* not a valid minor node */
		return (EBADF);
	}
	return (err);
}

static void
vgatext_save_text(struct vgatext_softc *softc)
{
	unsigned i;

	for (i = 0; i < sizeof (softc->shadow); i++)
		softc->shadow[i] = softc->current_base[i];
}

static void
vgatext_progressbar_stop()
{
	extern void progressbar_stop(void);

	if (vgatext_silent == 1) {
		vgatext_silent = 0;
		progressbar_stop();
	}
}

static void
vgatext_kdsettext(struct vgatext_softc *softc)
{
	int i;

	vgatext_init(softc);
	for (i = 0; i < sizeof (softc->shadow); i++) {
		softc->text_base[i] = softc->shadow[i];
	}
	softc->current_base = softc->text_base;
	if (softc->cursor.visible) {
		vgatext_set_cursor(softc,
		    softc->cursor.row, softc->cursor.col);
	}
	vgatext_restore_colormap(softc);
}

static void
vgatext_kdsetgraphics(struct vgatext_softc *softc)
{
	vgatext_progressbar_stop();
	vgatext_save_text(softc);
	softc->current_base = softc->shadow;
#if	defined(USE_BORDERS)
	vgatext_init_graphics(softc);
#endif
}

static int
vgatext_kdsetmode(struct vgatext_softc *softc, int mode)
{
	if ((mode == softc->mode) || (!VGATEXT_IS_CONSOLE(softc)))
		return (0);

	switch (mode) {
	case KD_TEXT:
		vgatext_kdsettext(softc);
		break;

	case KD_GRAPHICS:
		vgatext_kdsetgraphics(softc);
		break;

	case KD_RESETTEXT:
		/*
		 * In order to avoid racing with a starting X server,
		 * this needs to be a test and set that is performed in
		 * a single (softc->lock protected) ioctl into this driver.
		 */
		if (softc->mode == KD_TEXT && vgatext_silent == 1) {
			vgatext_progressbar_stop();
			vgatext_kdsettext(softc);
		}
		break;

	default:
		return (EINVAL);
	}
	softc->mode = mode;
	return (0);
}

/*ARGSUSED*/
static int
vgatext_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
		size_t *maplen, uint_t model)
{
	struct vgatext_softc *softc;
	int err;
	size_t length;


	softc = getsoftc(DEV2INST(dev));
	if (softc == NULL) {
		cmn_err(CE_WARN, "vgatext: Can't find softstate");
		return (-1);
	}

	if (!(off >= VGA_MMAP_FB_BASE &&
	    off < VGA_MMAP_FB_BASE + softc->fb_size)) {
		cmn_err(CE_WARN, "vgatext: Can't map offset 0x%llx", off);
		return (-1);
	}

	if (off + len > VGA_MMAP_FB_BASE + softc->fb_size)
		length = VGA_MMAP_FB_BASE + softc->fb_size - off;
	else
		length = len;

	if ((err = devmap_devmem_setup(dhp, softc->devi, NULL, softc->fb_regno,
	    off - VGA_MMAP_FB_BASE,
	    length, PROT_ALL, 0, &dev_attr)) < 0) {
		return (err);
	}


	*maplen = length;
	return (0);
}


static int
vgatext_devinit(struct vgatext_softc *softc, struct vis_devinit *data)
{
	/* initialize console instance */
	data->version = VIS_CONS_REV;
	data->width = TEXT_COLS;
	data->height = TEXT_ROWS;
	data->linebytes = TEXT_COLS;
	data->depth = 4;
	data->mode = VIS_TEXT;
	data->polledio = &softc->polledio;

	return (0);
}

/*
 * display a string on the screen at (row, col)
 *	 assume it has been cropped to fit.
 */

static void
vgatext_cons_display(struct vgatext_softc *softc, struct vis_consdisplay *da)
{
	unsigned char	*string;
	int	i;
	unsigned char	attr;
	struct cgatext {
		unsigned char ch;
		unsigned char attr;
	};
	struct cgatext *addr;

	/*
	 * Sanity checks.  This is a last-ditch effort to avoid damage
	 * from brokenness or maliciousness above.
	 */
	if (da->row < 0 || da->row >= TEXT_ROWS ||
	    da->col < 0 || da->col >= TEXT_COLS ||
	    da->col + da->width > TEXT_COLS)
		return;

	/*
	 * To be fully general, we should copyin the data.  This is not
	 * really relevant for this text-only driver, but a graphical driver
	 * should support these ioctls from userland to enable simple
	 * system startup graphics.
	 */
	attr = (solaris_color_to_pc_color[da->bg_color & 0xf] << 4)
	    | solaris_color_to_pc_color[da->fg_color & 0xf];
	string = da->data;
	addr = (struct cgatext *)softc->current_base
	    +  (da->row * TEXT_COLS + da->col);
	for (i = 0; i < da->width; i++) {
		addr->ch = string[i];
		addr->attr = attr;
		addr++;
	}
}

static void
vgatext_polled_display(
	struct vis_polledio_arg *arg,
	struct vis_consdisplay *da)
{
	vgatext_cons_display((struct vgatext_softc *)arg, da);
}

/*
 * screen-to-screen copy
 */

static void
vgatext_cons_copy(struct vgatext_softc *softc, struct vis_conscopy *ma)
{
	unsigned short	*from;
	unsigned short	*to;
	int		cnt;
	screen_size_t chars_per_row;
	unsigned short	*to_row_start;
	unsigned short	*from_row_start;
	screen_size_t	rows_to_move;
	unsigned short	*base;

	/*
	 * Sanity checks.  Note that this is a last-ditch effort to avoid
	 * damage caused by broken-ness or maliciousness above.
	 */
	if (ma->s_col < 0 || ma->s_col >= TEXT_COLS ||
	    ma->s_row < 0 || ma->s_row >= TEXT_ROWS ||
	    ma->e_col < 0 || ma->e_col >= TEXT_COLS ||
	    ma->e_row < 0 || ma->e_row >= TEXT_ROWS ||
	    ma->t_col < 0 || ma->t_col >= TEXT_COLS ||
	    ma->t_row < 0 || ma->t_row >= TEXT_ROWS ||
	    ma->s_col > ma->e_col ||
	    ma->s_row > ma->e_row)
		return;

	/*
	 * Remember we're going to copy shorts because each
	 * character/attribute pair is 16 bits.
	 */
	chars_per_row = ma->e_col - ma->s_col + 1;
	rows_to_move = ma->e_row - ma->s_row + 1;

	/* More sanity checks. */
	if (ma->t_row + rows_to_move > TEXT_ROWS ||
	    ma->t_col + chars_per_row > TEXT_COLS)
		return;

	base = (unsigned short *)softc->current_base;

	to_row_start = base + ((ma->t_row * TEXT_COLS) + ma->t_col);
	from_row_start = base + ((ma->s_row * TEXT_COLS) + ma->s_col);

	if (to_row_start < from_row_start) {
		while (rows_to_move-- > 0) {
			to = to_row_start;
			from = from_row_start;
			to_row_start += TEXT_COLS;
			from_row_start += TEXT_COLS;
			for (cnt = chars_per_row; cnt-- > 0; )
				*to++ = *from++;
		}
	} else {
		/*
		 * Offset to the end of the region and copy backwards.
		 */
		cnt = rows_to_move * TEXT_COLS + chars_per_row;
		to_row_start += cnt;
		from_row_start += cnt;

		while (rows_to_move-- > 0) {
			to_row_start -= TEXT_COLS;
			from_row_start -= TEXT_COLS;
			to = to_row_start;
			from = from_row_start;
			for (cnt = chars_per_row; cnt-- > 0; )
				*--to = *--from;
		}
	}
}

static void
vgatext_polled_copy(
	struct vis_polledio_arg *arg,
	struct vis_conscopy *ca)
{
	vgatext_cons_copy((struct vgatext_softc *)arg, ca);
}


static void
vgatext_cons_cursor(struct vgatext_softc *softc, struct vis_conscursor *ca)
{
	if (vgatext_silent)
		return;

	switch (ca->action) {
	case VIS_HIDE_CURSOR:
		softc->cursor.visible = B_FALSE;
		if (softc->current_base == softc->text_base)
			vgatext_hide_cursor(softc);
		break;
	case VIS_DISPLAY_CURSOR:
		/*
		 * Sanity check.  This is a last-ditch effort to avoid
		 * damage from brokenness or maliciousness above.
		 */
		if (ca->col < 0 || ca->col >= TEXT_COLS ||
		    ca->row < 0 || ca->row >= TEXT_ROWS)
			return;

		softc->cursor.visible = B_TRUE;
		softc->cursor.col = ca->col;
		softc->cursor.row = ca->row;
		if (softc->current_base == softc->text_base)
			vgatext_set_cursor(softc, ca->row, ca->col);
		break;
	case VIS_GET_CURSOR:
		if (softc->current_base == softc->text_base) {
			vgatext_get_cursor(softc, &ca->row, &ca->col);
		}
		break;
	}
}

static void
vgatext_polled_cursor(
	struct vis_polledio_arg *arg,
	struct vis_conscursor *ca)
{
	vgatext_cons_cursor((struct vgatext_softc *)arg, ca);
}



/*ARGSUSED*/
static void
vgatext_hide_cursor(struct vgatext_softc *softc)
{
	/* Nothing at present */
}

static void
vgatext_set_cursor(struct vgatext_softc *softc, int row, int col)
{
	short	addr;

	if (vgatext_silent)
		return;

	addr = row * TEXT_COLS + col;

	vga_set_crtc(&softc->regs, VGA_CRTC_CLAH, addr >> 8);
	vga_set_crtc(&softc->regs, VGA_CRTC_CLAL, addr & 0xff);
}

static int vga_row, vga_col;

static void
vgatext_get_cursor(struct vgatext_softc *softc,
    screen_pos_t *row, screen_pos_t *col)
{
	short   addr;

	addr = (vga_get_crtc(&softc->regs, VGA_CRTC_CLAH) << 8) +
	    vga_get_crtc(&softc->regs, VGA_CRTC_CLAL);

	vga_row = *row = addr / TEXT_COLS;
	vga_col = *col = addr % TEXT_COLS;
}

/*
 * This code is experimental. It's only enabled if console is
 * set to graphics, a preliminary implementation of happyface boot.
 */
static void
vgatext_set_text(struct vgatext_softc *softc)
{
	int i;

	if (happyface_boot == 0)
		return;

	/* we are in graphics mode, set to text 80X25 mode */

	/* set misc registers */
	vga_set_reg(&softc->regs, VGA_MISC_W, VGA_MISC_TEXT);

	/* set sequencer registers */
	vga_set_seq(&softc->regs, VGA_SEQ_RST_SYN,
	    (vga_get_seq(&softc->regs, VGA_SEQ_RST_SYN) &
	    ~VGA_SEQ_RST_SYN_NO_SYNC_RESET));
	for (i = 1; i < NUM_SEQ_REG; i++) {
		vga_set_seq(&softc->regs, i, VGA_SEQ_TEXT[i]);
	}
	vga_set_seq(&softc->regs, VGA_SEQ_RST_SYN,
	    (vga_get_seq(&softc->regs, VGA_SEQ_RST_SYN) |
	    VGA_SEQ_RST_SYN_NO_ASYNC_RESET |
	    VGA_SEQ_RST_SYN_NO_SYNC_RESET));

	/* set crt controller registers */
	vga_set_crtc(&softc->regs, VGA_CRTC_VRE,
	    (vga_get_crtc(&softc->regs, VGA_CRTC_VRE) &
	    ~VGA_CRTC_VRE_LOCK));
	for (i = 0; i < NUM_CRTC_REG; i++) {
		vga_set_crtc(&softc->regs, i, VGA_CRTC_TEXT[i]);
	}

	/* set graphics controller registers */
	for (i = 0; i < NUM_GRC_REG; i++) {
		vga_set_grc(&softc->regs, i, VGA_GRC_TEXT[i]);
	}

	/* set attribute registers */
	for (i = 0; i < NUM_ATR_REG; i++) {
		vga_set_atr(&softc->regs, i, VGA_ATR_TEXT[i]);
	}

	/* set palette */
	for (i = 0; i < VGA_TEXT_CMAP_ENTRIES; i++) {
		vga_put_cmap(&softc->regs, i, VGA_TEXT_PALETTES[i][0] << 2,
		    VGA_TEXT_PALETTES[i][1] << 2,
		    VGA_TEXT_PALETTES[i][2] << 2);
	}
	for (i = VGA_TEXT_CMAP_ENTRIES; i < VGA8_CMAP_ENTRIES; i++) {
		vga_put_cmap(&softc->regs, i, 0, 0, 0);
	}

	vgatext_save_colormap(softc);
}

static void
vgatext_init(struct vgatext_softc *softc)
{
	unsigned char atr_mode;

	atr_mode = vga_get_atr(&softc->regs, VGA_ATR_MODE);
	if (atr_mode & VGA_ATR_MODE_GRAPH)
		vgatext_set_text(softc);
	atr_mode = vga_get_atr(&softc->regs, VGA_ATR_MODE);
	atr_mode &= ~VGA_ATR_MODE_BLINK;
	atr_mode &= ~VGA_ATR_MODE_9WIDE;
	vga_set_atr(&softc->regs, VGA_ATR_MODE, atr_mode);
#if	defined(USE_BORDERS)
	vga_set_atr(&softc->regs, VGA_ATR_BDR_CLR,
	    vga_get_atr(&softc->regs, VGA_BRIGHT_WHITE));
#else
	vga_set_atr(&softc->regs, VGA_ATR_BDR_CLR,
	    vga_get_atr(&softc->regs, VGA_BLACK));
#endif
	vgatext_setfont(softc);	/* need selectable font? */
}

#if	defined(USE_BORDERS)
static void
vgatext_init_graphics(struct vgatext_softc *softc)
{
	vga_set_atr(&softc->regs, VGA_ATR_BDR_CLR,
	    vga_get_atr(&softc->regs, VGA_BLACK));
}
#endif

static char vga_fontslot = 0;

static void
vgatext_setfont(struct vgatext_softc *softc)
{
	static uchar_t fsreg[8] = {0x0, 0x30, 0x5, 0x35, 0xa, 0x3a, 0xf, 0x3f};

	extern unsigned char *ENCODINGS[];
	uchar_t *from;
	uchar_t volatile *to;
	int	i, j, s;
	int	bpc, f_offset;

	/* Sync-reset the sequencer registers */
	vga_set_seq(&softc->regs, 0x00, 0x01);
	/*
	 *  enable write to plane2, since fonts
	 * could only be loaded into plane2
	 */
	vga_set_seq(&softc->regs, 0x02, 0x04);
	/*
	 *  sequentially access data in the bit map being
	 * selected by MapMask register (index 0x02)
	 */
	vga_set_seq(&softc->regs, 0x04, 0x07);
	/* Sync-reset ended, and allow the sequencer to operate */
	vga_set_seq(&softc->regs, 0x00, 0x03);

	/*
	 *  select plane 2 on Read Mode 0
	 */
	vga_set_grc(&softc->regs, 0x04, 0x02);
	/*
	 *  system addresses sequentially access data, follow
	 * Memory Mode register bit 2 in the sequencer
	 */
	vga_set_grc(&softc->regs, 0x05, 0x00);
	/*
	 * set range of host memory addresses decoded by VGA
	 * hardware -- A0000h-BFFFFh (128K region)
	 */
	vga_set_grc(&softc->regs, 0x06, 0x00);

	/*
	 * This assumes 8x16 characters, which yield the traditional 80x25
	 * screen.  It really should support other character heights.
	 */
	bpc = 16;
	s = vga_fontslot;
	f_offset = s * 8 * 1024;
	for (i = 0; i < 256; i++) {
		from = ENCODINGS[i];
		to = (unsigned char *)softc->fb.addr + f_offset + i * 0x20;
		for (j = 0; j < bpc; j++)
			*to++ = *from++;
	}

	/* Sync-reset the sequencer registers */
	vga_set_seq(&softc->regs, 0x00, 0x01);
	/* enable write to plane 0 and 1 */
	vga_set_seq(&softc->regs, 0x02, 0x03);
	/*
	 * enable character map selection
	 * and odd/even addressing
	 */
	vga_set_seq(&softc->regs, 0x04, 0x03);
	/*
	 * select font map
	 */
	vga_set_seq(&softc->regs, 0x03, fsreg[s]);
	/* Sync-reset ended, and allow the sequencer to operate */
	vga_set_seq(&softc->regs, 0x00, 0x03);

	/* restore graphic registers */

	/* select plane 0 */
	vga_set_grc(&softc->regs, 0x04, 0x00);
	/* enable odd/even addressing mode */
	vga_set_grc(&softc->regs, 0x05, 0x10);
	/*
	 * range of host memory addresses decoded by VGA
	 * hardware -- B8000h-BFFFFh (32K region)
	 */
	vga_set_grc(&softc->regs, 0x06, 0x0e);
	/* enable all color plane */
	vga_set_atr(&softc->regs, 0x12, 0x0f);

}

static void
vgatext_save_colormap(struct vgatext_softc *softc)
{
	int i;

	for (i = 0; i < VGA_ATR_NUM_PLT; i++) {
		softc->attrib_palette[i] = vga_get_atr(&softc->regs, i);
	}
	for (i = 0; i < VGA8_CMAP_ENTRIES; i++) {
		vga_get_cmap(&softc->regs, i,
		    &softc->colormap[i].red,
		    &softc->colormap[i].green,
		    &softc->colormap[i].blue);
	}
}

static void
vgatext_restore_colormap(struct vgatext_softc *softc)
{
	int i;

	for (i = 0; i < VGA_ATR_NUM_PLT; i++) {
		vga_set_atr(&softc->regs, i, softc->attrib_palette[i]);
	}
	for (i = 0; i < VGA8_CMAP_ENTRIES; i++) {
		vga_put_cmap(&softc->regs, i,
		    softc->colormap[i].red,
		    softc->colormap[i].green,
		    softc->colormap[i].blue);
	}
}

/*
 * search the entries of the "reg" property for one which has the desired
 * combination of phys_hi bits and contains the desired address.
 *
 * This version searches a PCI-style "reg" property.  It was prompted by
 * issues surrounding the presence or absence of an entry for the ROM:
 * (a) a transition problem with PowerPC Virtual Open Firmware
 * (b) uncertainty as to whether an entry will be included on a device
 *     with ROM support (and so an "active" ROM base address register),
 *     but no ROM actually installed.
 *
 * See the note below on vgatext_get_isa_reg_index for the reasons for
 * returning the offset.
 *
 * Note that this routine may not be fully general; it is intended for the
 * specific purpose of finding a couple of particular VGA reg entries and
 * may not be suitable for all reg-searching purposes.
 */
static int
vgatext_get_pci_reg_index(
	dev_info_t *const devi,
	unsigned long himask,
	unsigned long hival,
	unsigned long addr,
	off_t *offset)
{

	int			length, index;
	pci_regspec_t	*reg;

	if (ddi_getlongprop(DDI_DEV_T_ANY, devi, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&reg, &length) != DDI_PROP_SUCCESS) {
		return (-1);
	}

	for (index = 0; index < length / sizeof (pci_regspec_t); index++) {
		if ((reg[index].pci_phys_hi & himask) != hival)
			continue;
		if (reg[index].pci_size_hi != 0)
			continue;
		if (reg[index].pci_phys_mid != 0)
			continue;
		if (reg[index].pci_phys_low > addr)
			continue;
		if (reg[index].pci_phys_low + reg[index].pci_size_low <= addr)
			continue;

		*offset = addr - reg[index].pci_phys_low;
		kmem_free(reg, (size_t)length);
		return (index);
	}
	kmem_free(reg, (size_t)length);

	return (-1);
}

/*
 * search the entries of the "reg" property for one which has the desired
 * combination of phys_hi bits and contains the desired address.
 *
 * This version searches a ISA-style "reg" property.  It was prompted by
 * issues surrounding 8514/A support.  By IEEE 1275 compatibility conventions,
 * 8514/A registers should have been added after all standard VGA registers.
 * Unfortunately, the Solaris/Intel device configuration framework
 * (a) lists the 8514/A registers before the video memory, and then
 * (b) also sorts the entries so that I/O entries come before memory
 *     entries.
 *
 * It returns the "reg" index and offset into that register set.
 * The offset is needed because there exist (broken?) BIOSes that
 * report larger ranges enclosing the standard ranges.  One reports
 * 0x3bf for 0x21 instead of 0x3c0 for 0x20, for instance.  Using the
 * offset adjusts for this difference in the base of the register set.
 *
 * Note that this routine may not be fully general; it is intended for the
 * specific purpose of finding a couple of particular VGA reg entries and
 * may not be suitable for all reg-searching purposes.
 */
static int
vgatext_get_isa_reg_index(
	dev_info_t *const devi,
	unsigned long hival,
	unsigned long addr,
	off_t *offset)
{

	int		length, index;
	struct regspec	*reg;

	if (ddi_getlongprop(DDI_DEV_T_ANY, devi, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&reg, &length) != DDI_PROP_SUCCESS) {
		return (-1);
	}

	for (index = 0; index < length / sizeof (struct regspec); index++) {
		if (reg[index].regspec_bustype != hival)
			continue;
		if (reg[index].regspec_addr > addr)
			continue;
		if (reg[index].regspec_addr + reg[index].regspec_size <= addr)
			continue;

		*offset = addr - reg[index].regspec_addr;
		kmem_free(reg, (size_t)length);
		return (index);
	}
	kmem_free(reg, (size_t)length);

	return (-1);
}
