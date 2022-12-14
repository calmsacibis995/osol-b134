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
 * audio810 Audio Driver
 *
 * The driver is primarily targeted at providing audio support for the
 * W1100z and W2100z systems, which use the AMD 8111 audio core and
 * the Realtek ALC 655 codec. The ALC 655 chip supports only fixed 48k
 * sample rate. However, the audio core of AMD 8111 is completely
 * compatible to the Intel ICHx chips (Intel 8x0 chipsets), so the
 * driver can work for the ICHx.  We only support the 48k maximum
 * rate, since we only have a single PCM out channel.
 *
 * The AMD 8111 audio core, as an AC'97 controller, has independent
 * channels for PCM in, PCM out, mic in, modem in, and modem out.
 * The AC'97 controller is a PCI bus master with scatter/gather
 * support. Each channel has a DMA engine. Currently, we use only
 * the PCM in and PCM out channels. Each DMA engine uses one buffer
 * descriptor list. And the buffer descriptor list is an array of up
 * to 32 entries, each of which describes a data buffer. Each entry
 * contains a pointer to a data buffer, control bits, and the length
 * of the buffer being pointed to, where the length is expressed as
 * the number of samples. This, combined with the 16-bit sample size,
 * gives the actual physical length of the buffer.
 *
 * A workaround for the AD1980 and AD1985 codec:
 *	Most vendors connect the surr-out of the codecs to the line-out jack.
 *	So far we haven't found which vendors don't do that. So we assume that
 *	all vendors swap the surr-out and the line-out outputs. So we need swap
 *	the two outputs. But we still internally process the
 *	"ad198x-swap-output" property. If someday some vendors do not swap the
 *	outputs, we would set "ad198x-swap-output = 0" in the
 *	/kernel/drv/audio810.conf file, and unload and reload the audio810
 *	driver (or reboot).
 *
 * 	NOTE:
 * 	This driver depends on the drv/audio and misc/ac97
 * 	modules being loaded first.
 */
#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/note.h>
#include <sys/audio/audio_driver.h>
#include <sys/audio/ac97.h>
#include "audio810.h"

/*
 * Module linkage routines for the kernel
 */
static int audio810_ddi_attach(dev_info_t *, ddi_attach_cmd_t);
static int audio810_ddi_detach(dev_info_t *, ddi_detach_cmd_t);
static int audio810_ddi_quiesce(dev_info_t *);

/*
 * Entry point routine prototypes
 */
static int audio810_open(void *, int, unsigned *, unsigned *, caddr_t *);
static void audio810_close(void *);
static int audio810_start(void *);
static void audio810_stop(void *);
static int audio810_format(void *);
static int audio810_channels(void *);
static int audio810_rate(void *);
static uint64_t audio810_count(void *);
static void audio810_sync(void *, unsigned);
static unsigned audio810_playahead(void *);

static audio_engine_ops_t audio810_engine_ops = {
	AUDIO_ENGINE_VERSION,
	audio810_open,
	audio810_close,
	audio810_start,
	audio810_stop,
	audio810_count,
	audio810_format,
	audio810_channels,
	audio810_rate,
	audio810_sync,
	NULL,
	NULL,
	audio810_playahead
};

/*
 * interrupt handler
 */
static uint_t	audio810_intr(caddr_t);

/*
 * Local Routine Prototypes
 */
static int audio810_attach(dev_info_t *);
static int audio810_resume(dev_info_t *);
static int audio810_detach(dev_info_t *);
static int audio810_suspend(dev_info_t *);

static int audio810_alloc_port(audio810_state_t *, int, uint8_t);
static void audio810_start_port(audio810_port_t *);
static void audio810_stop_port(audio810_port_t *);
static void audio810_reset_port(audio810_port_t *);
static void audio810_update_port(audio810_port_t *);
static int audio810_codec_sync(audio810_state_t *);
static void audio810_write_ac97(void *, uint8_t, uint16_t);
static uint16_t audio810_read_ac97(void *, uint8_t);
static int audio810_map_regs(dev_info_t *, audio810_state_t *);
static void audio810_unmap_regs(audio810_state_t *);
static void audio810_stop_dma(audio810_state_t *);
static int audio810_chip_init(audio810_state_t *);
static void audio810_destroy(audio810_state_t *);

/*
 * Global variables, but used only by this file.
 */

/* driver name, so we don't have to call ddi_driver_name() or hard code strs */
static char	*audio810_name = I810_NAME;


/*
 * DDI Structures
 */

/* Device operations structure */
static struct dev_ops audio810_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	NULL,			/* devo_getinfo */
	nulldev,		/* devo_identify - obsolete */
	nulldev,		/* devo_probe */
	audio810_ddi_attach,	/* devo_attach */
	audio810_ddi_detach,	/* devo_detach */
	nodev,			/* devo_reset */
	NULL,			/* devi_cb_ops */
	NULL,			/* devo_bus_ops */
	NULL,			/* devo_power */
	audio810_ddi_quiesce,	/* devo_quiesce */
};

/* Linkage structure for loadable drivers */
static struct modldrv audio810_modldrv = {
	&mod_driverops,		/* drv_modops */
	I810_MOD_NAME,		/* drv_linkinfo */
	&audio810_dev_ops,	/* drv_dev_ops */
};

/* Module linkage structure */
static struct modlinkage audio810_modlinkage = {
	MODREV_1,			/* ml_rev */
	(void *)&audio810_modldrv,	/* ml_linkage */
	NULL				/* NULL terminates the list */
};

/*
 * device access attributes for register mapping
 */
static struct ddi_device_acc_attr dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static struct ddi_device_acc_attr buf_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * DMA attributes of buffer descriptor list
 */
static ddi_dma_attr_t	bdlist_dma_attr = {
	DMA_ATTR_V0,	/* version */
	0,		/* addr_lo */
	0xffffffff,	/* addr_hi */
	0x0000ffff,	/* count_max */
	8,		/* align, BDL must be aligned on a 8-byte boundary */
	0x3c,		/* burstsize */
	8,		/* minxfer, set to the size of a BDlist entry */
	0x0000ffff,	/* maxxfer */
	0x00000fff,	/* seg, set to the RAM pagesize of intel platform */
	1,		/* sgllen, there's no scatter-gather list */
	8,		/* granular, set to the value of minxfer */
	0		/* flags, use virtual address */
};

/*
 * DMA attributes of buffers to be used to receive/send audio data
 */
static ddi_dma_attr_t	sample_buf_dma_attr = {
	DMA_ATTR_V0,
	0,		/* addr_lo */
	0xffffffff,	/* addr_hi */
	0x0001ffff,	/* count_max */
	4,		/* align, data buffer is aligned on a 4-byte boundary */
	0x3c,		/* burstsize */
	4,		/* minxfer, set to the size of a sample data */
	0x0001ffff,	/* maxxfer */
	0x0001ffff,	/* seg */
	1,		/* sgllen, no scatter-gather */
	4,		/* granular, set to the value of minxfer */
	0,		/* flags, use virtual address */
};

/*
 * _init()
 *
 * Description:
 *	Driver initialization, called when driver is first loaded.
 *	This is how access is initially given to all the static structures.
 *
 * Arguments:
 *	None
 *
 * Returns:
 *	mod_install() status, see mod_install(9f)
 */
int
_init(void)
{
	int	error;

	audio_init_ops(&audio810_dev_ops, I810_NAME);

	if ((error = mod_install(&audio810_modlinkage)) != 0) {
		audio_fini_ops(&audio810_dev_ops);
	}

	return (error);
}

/*
 * _fini()
 *
 * Description:
 *	Module de-initialization, called when the driver is to be unloaded.
 *
 * Arguments:
 *	None
 *
 * Returns:
 *	mod_remove() status, see mod_remove(9f)
 */
int
_fini(void)
{
	int		error;

	if ((error = mod_remove(&audio810_modlinkage)) != 0) {
		return (error);
	}

	/* clean up ops */
	audio_fini_ops(&audio810_dev_ops);

	return (0);
}

/*
 * _info()
 *
 * Description:
 *	Module information, returns information about the driver.
 *
 * Arguments:
 *	modinfo		*modinfop	Pointer to the opaque modinfo structure
 *
 * Returns:
 *	mod_info() status, see mod_info(9f)
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&audio810_modlinkage, modinfop));
}


/* ******************* Driver Entry Points ********************************* */

/*
 * audio810_ddi_attach()
 *
 * Description:
 *	Implements the DDI attach(9e) entry point.
 *
 * Arguments:
 *	dev_info_t	*dip	Pointer to the device's dev_info struct
 *	ddi_attach_cmd_t cmd	Attach command
 *
 * Returns:
 *	DDI_SUCCESS		The driver was initialized properly
 *	DDI_FAILURE		The driver couldn't be initialized properly
 */
static int
audio810_ddi_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_ATTACH:
		return (audio810_attach(dip));

	case DDI_RESUME:
		return (audio810_resume(dip));
	}
	return (DDI_FAILURE);
}

/*
 * audio810_ddi_detach()
 *
 * Description:
 *	Implements the detach(9e) entry point.
 *
 * Arguments:
 *	dev_info_t		*dip	Pointer to the device's dev_info struct
 *	ddi_detach_cmd_t	cmd	Detach command
 *
 * Returns:
 *	DDI_SUCCESS	The driver was detached
 *	DDI_FAILURE	The driver couldn't be detached
 */
static int
audio810_ddi_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_DETACH:
		return (audio810_detach(dip));

	case DDI_SUSPEND:
		return (audio810_suspend(dip));
	}
	return (DDI_FAILURE);
}

/*
 * audio810_ddi_quiesce()
 *
 * Description:
 *	Implements the quiesce(9e) entry point.
 *
 * Arguments:
 *	dev_info_t		*dip	Pointer to the device's dev_info struct
 *
 * Returns:
 *	DDI_SUCCESS	The driver was quiesced
 *	DDI_FAILURE	The driver couldn't be quiesced
 */
static int
audio810_ddi_quiesce(dev_info_t *dip)
{
	audio810_state_t	*statep;

	if ((statep = ddi_get_driver_private(dip)) == NULL)
		return (DDI_FAILURE);

	audio810_stop_dma(statep);
	return (DDI_SUCCESS);
}

/*
 * audio810_intr()
 *
 * Description:
 *	Interrupt service routine for both play and record. For play we
 *	get the next buffers worth of audio. For record we send it on to
 *	the mixer.
 *
 *	Each of buffer descriptor has a field IOC(interrupt on completion)
 *	When both this and the IOC bit of correspondent dma control register
 *	is set, it means that the controller should issue an interrupt upon
 *	completion of this buffer.
 *	(AMD 8111 hypertransport I/O hub data sheet. 3.8.3 page 71)
 *
 * Arguments:
 *	caddr_t		arg	Pointer to the interrupting device's state
 *				structure
 *
 * Returns:
 *	DDI_INTR_CLAIMED	Interrupt claimed and processed
 *	DDI_INTR_UNCLAIMED	Interrupt not claimed, and thus ignored
 */
static uint_t
audio810_intr(caddr_t arg)
{
	audio810_state_t	*statep;
	uint16_t		gsr;

	statep = (void *)arg;
	mutex_enter(&statep->inst_lock);

	if (statep->suspended) {
		mutex_exit(&statep->inst_lock);
		return (DDI_INTR_UNCLAIMED);
	}

	gsr = I810_BM_GET32(I810_REG_GSR);

	/* check if device is interrupting */
	if ((gsr & I810_GSR_USE_INTR) == 0) {
		mutex_exit(&statep->inst_lock);
		return (DDI_INTR_UNCLAIMED);
	}

	for (int pnum = 0; pnum < I810_NUM_PORTS; pnum++) {
		audio810_port_t	*port;
		uint8_t		regoff, index;

		port = statep->ports[pnum];
		if (port == NULL) {
			continue;
		}
		regoff = port->regoff;

		if (!(I810_BM_GET8(port->stsoff) & I810_BM_SR_BCIS))
			continue;

		/* update the LVI -- we just set it to the current value - 1 */
		index = I810_BM_GET8(regoff + I810_OFFSET_CIV);
		index = (index - 1) % I810_BD_NUMS;

		I810_BM_PUT8(regoff + I810_OFFSET_LVI, index);

		/* clear any interrupt */
		I810_BM_PUT8(port->stsoff,
		    I810_BM_SR_LVBCI | I810_BM_SR_BCIS | I810_BM_SR_FIFOE);
	}

	/* update the kernel interrupt statistics */
	if (statep->ksp) {
		I810_KIOP(statep)->intrs[KSTAT_INTR_HARD]++;
	}

	mutex_exit(&statep->inst_lock);

	/* notify the framework */
	if (gsr & I810_GSR_INTR_PIN) {
		audio_engine_produce(statep->ports[I810_PCM_IN]->engine);
	}
	if (gsr & I810_GSR_INTR_POUT) {
		audio_engine_consume(statep->ports[I810_PCM_OUT]->engine);
	}

	return (DDI_INTR_CLAIMED);
}

/*
 * audio810_open()
 *
 * Description:
 *	Opens a DMA engine for use.
 *
 * Arguments:
 *	void		*arg		The DMA engine to set up
 *	int		flag		Open flags
 *	unsigned	*fragfrp	Receives number of frames per fragment
 *	unsigned	*nfragsp	Receives number of fragments
 *	caddr_t		*bufp		Receives kernel data buffer
 *
 * Returns:
 *	0	on success
 *	errno	on failure
 */
static int
audio810_open(void *arg, int flag, unsigned *fragfrp, unsigned *nfragsp,
    caddr_t *bufp)
{
	audio810_port_t	*port = arg;

	_NOTE(ARGUNUSED(flag));

	port->started = B_FALSE;
	port->count = 0;
	*fragfrp = port->fragfr;
	*nfragsp = port->nfrag;
	*bufp = port->samp_kaddr;

	mutex_enter(&port->statep->inst_lock);
	audio810_reset_port(port);
	mutex_exit(&port->statep->inst_lock);

	return (0);
}

/*
 * audio810_close()
 *
 * Description:
 *	Closes an audio DMA engine that was previously opened.  Since
 *	nobody is using it, we take this opportunity to possibly power
 *	down the entire device.
 *
 * Arguments:
 *	void	*arg		The DMA engine to shut down
 */
static void
audio810_close(void *arg)
{
	audio810_port_t		*port = arg;
	audio810_state_t	*statep = port->statep;

	mutex_enter(&statep->inst_lock);
	audio810_stop_port(port);
	port->started = B_FALSE;
	mutex_exit(&statep->inst_lock);
}

/*
 * audio810_stop()
 *
 * Description:
 *	This is called by the framework to stop a port that is
 *	transferring data.
 *
 * Arguments:
 *	void	*arg		The DMA engine to stop
 */
static void
audio810_stop(void *arg)
{
	audio810_port_t		*port = arg;
	audio810_state_t	*statep = port->statep;

	mutex_enter(&statep->inst_lock);
	if (port->started) {
		audio810_stop_port(port);
	}
	port->started = B_FALSE;
	mutex_exit(&statep->inst_lock);
}

/*
 * audio810_start()
 *
 * Description:
 *	This is called by the framework to start a port transferring data.
 *
 * Arguments:
 *	void	*arg		The DMA engine to start
 *
 * Returns:
 *	0 	on success (never fails, errno if it did)
 */
static int
audio810_start(void *arg)
{
	audio810_port_t		*port = arg;
	audio810_state_t	*statep = port->statep;

	mutex_enter(&statep->inst_lock);
	if (!port->started) {
		audio810_start_port(port);
		port->started = B_TRUE;
	}
	mutex_exit(&statep->inst_lock);
	return (0);
}

/*
 * audio810_format()
 *
 * Description:
 *	This is called by the framework to query the format of the device.
 *
 * Arguments:
 *	void	*arg		The DMA engine to query
 *
 * Returns:
 *	Format of the device (fixed at AUDIO_FORMAT_S16_LE)
 */
static int
audio810_format(void *arg)
{
	_NOTE(ARGUNUSED(arg));

	return (AUDIO_FORMAT_S16_LE);
}

/*
 * audio810_channels()
 *
 * Description:
 *	This is called by the framework to query the num channels of
 *	the device.
 *
 * Arguments:
 *	void	*arg		The DMA engine to query
 *
 * Returns:
 *	0 number of channels for device
 */
static int
audio810_channels(void *arg)
{
	audio810_port_t	*port = arg;

	return (port->nchan);
}

/*
 * audio810_rate()
 *
 * Description:
 *	This is called by the framework to query the rate of the device.
 *
 * Arguments:
 *	void	*arg		The DMA engine to query
 *
 * Returns:
 *	Rate of device (fixed at 48000 Hz)
 */
static int
audio810_rate(void *arg)
{
	_NOTE(ARGUNUSED(arg));

	return (48000);
}

/*
 * audio810_count()
 *
 * Description:
 *	This is called by the framework to get the engine's frame counter
 *
 * Arguments:
 *	void	*arg		The DMA engine to query
 *
 * Returns:
 *	frame count for current engine
 */
static uint64_t
audio810_count(void *arg)
{
	audio810_port_t		*port = arg;
	audio810_state_t	*statep = port->statep;
	uint64_t		val;
	uint16_t		picb;
	uint64_t		count;
	uint8_t			nchan;

	mutex_enter(&statep->inst_lock);
	audio810_update_port(port);
	count = port->count;
	picb = port->picb;
	nchan = port->nchan;
	mutex_exit(&statep->inst_lock);

	if (statep->quirk == QUIRK_SIS7012) {
		val = count + picb / (2 * nchan);
	} else {
		val = count + (picb / nchan);
	}

	return (val);
}

/*
 * audio810_sync()
 *
 * Description:
 *	This is called by the framework to synchronize DMA caches.
 *
 * Arguments:
 *	void	*arg		The DMA engine to sync
 */
static void
audio810_sync(void *arg, unsigned nframes)
{
	audio810_port_t *port = arg;
	_NOTE(ARGUNUSED(nframes));

	(void) ddi_dma_sync(port->samp_dmah, 0, 0, port->sync_dir);
}

/*
 * audio810_playahead()
 *
 * Description:
 *	This is called by the framework to determine how much data it
 *	should queue up.  We desire a deeper playahead than most to
 *	allow for virtualized devices which have less "regular"
 *	interrupt scheduling.
 *
 * Arguments:
 *	void	*arg		The DMA engine to query
 *
 * Returns:
 *	Play ahead in frames (4 fragments).
 */
static unsigned
audio810_playahead(void *arg)
{
	audio810_port_t *port = arg;

	return (4 * port->fragfr);
}



/* *********************** Local Routines *************************** */

/*
 * audio810_start_port()
 *
 * Description:
 *	This routine starts the DMA engine.
 *
 * Arguments:
 *	audio810_port_t	*port		Port of DMA engine to start.
 */
static void
audio810_start_port(audio810_port_t *port)
{
	audio810_state_t	*statep = port->statep;
	uint8_t			cr;

	ASSERT(mutex_owned(&statep->inst_lock));

	/* if suspended, then do nothing else */
	if (statep->suspended) {
		return;
	}

	cr = I810_BM_GET8(port->regoff + I810_OFFSET_CR);
	cr |= I810_BM_CR_IOCE;
	I810_BM_PUT8(port->regoff + I810_OFFSET_CR, cr);
	cr |= I810_BM_CR_RUN;
	I810_BM_PUT8(port->regoff + I810_OFFSET_CR, cr);
}

/*
 * audio810_stop_port()
 *
 * Description:
 *	This routine stops the DMA engine.
 *
 * Arguments:
 *	audio810_port_t	*port		Port of DMA engine to stop.
 */
static void
audio810_stop_port(audio810_port_t *port)
{
	audio810_state_t	*statep = port->statep;
	uint8_t			cr;

	ASSERT(mutex_owned(&statep->inst_lock));

	/* if suspended, then do nothing else */
	if (statep->suspended) {
		return;
	}

	cr = I810_BM_GET8(port->regoff + I810_OFFSET_CR);
	cr &= ~I810_BM_CR_RUN;
	I810_BM_PUT8(port->regoff + I810_OFFSET_CR, cr);
}

/*
 * audio810_reset_port()
 *
 * Description:
 *	This routine resets the DMA engine pareparing it for work.
 *
 * Arguments:
 *	audio810_port_t	*port		Port of DMA engine to reset.
 */
static void
audio810_reset_port(audio810_port_t *port)
{
	audio810_state_t	*statep = port->statep;
	uint32_t		gcr;

	ASSERT(mutex_owned(&statep->inst_lock));

	port->civ = 0;
	port->picb = 0;

	if (statep->suspended)
		return;

	/*
	 * Make sure we put once in stereo, to ensure we always start from
	 * front left.
	 */
	if (port->num == I810_PCM_OUT) {

		if (statep->quirk == QUIRK_SIS7012) {
			/*
			 * SiS 7012 needs its own special multichannel config.
			 */
			gcr = I810_BM_GET32(I810_REG_GCR);
			gcr &= ~I810_GCR_SIS_CHANNELS_MASK;
			I810_BM_PUT32(I810_REG_GCR, gcr);
			delay(drv_usectohz(50000));	/* 50 msec */

			switch (statep->maxch) {
			case 2:
				gcr |= I810_GCR_SIS_2_CHANNELS;
				break;
			case 4:
				gcr |= I810_GCR_SIS_4_CHANNELS;
				break;
			case 6:
				gcr |= I810_GCR_SIS_6_CHANNELS;
				break;
			}
			I810_BM_PUT32(I810_REG_GCR, gcr);
			delay(drv_usectohz(50000));	/* 50 msec */

			/*
			 * SiS 7012 has special unmute bit.
			 */
			I810_BM_PUT8(I810_REG_SISCTL, I810_SISCTL_UNMUTE);

		} else {

			/*
			 * All other devices work the same.
			 */
			gcr = I810_BM_GET32(I810_REG_GCR);
			gcr &= ~I810_GCR_CHANNELS_MASK;

			I810_BM_PUT32(I810_REG_GCR, gcr);
			delay(drv_usectohz(50000));	/* 50 msec */

			switch (statep->maxch) {
			case 2:
				gcr |= I810_GCR_2_CHANNELS;
				break;
			case 4:
				gcr |= I810_GCR_4_CHANNELS;
				break;
			case 6:
				gcr |= I810_GCR_6_CHANNELS;
				break;
			}
			I810_BM_PUT32(I810_REG_GCR, gcr);
			delay(drv_usectohz(50000));	/* 50 msec */
		}
	}

	/*
	 * Perform full reset of the engine, but leave it turned off.
	 */
	I810_BM_PUT8(port->regoff + I810_OFFSET_CR, 0);
	I810_BM_PUT8(port->regoff + I810_OFFSET_CR, I810_BM_CR_RST);

	/* program the offset of the BD list */
	I810_BM_PUT32(port->regoff + I810_OFFSET_BD_BASE, port->bdl_paddr);

	/* we set the last index to the full count -- all buffers are valid */
	I810_BM_PUT8(port->regoff + I810_OFFSET_LVI, I810_BD_NUMS - 1);
}

/*
 * audio810_update_port()
 *
 * Description:
 *	This routine updates the ports frame counter from hardware, and
 *	gracefully handles wraps.
 *
 * Arguments:
 *	audio810_port_t	*port		The port to update.
 */
static void
audio810_update_port(audio810_port_t *port)
{
	audio810_state_t	*statep = port->statep;
	uint8_t			regoff = port->regoff;
	uint8_t			civ;
	uint16_t		picb;
	unsigned		n;

	if (statep->suspended) {
		civ = 0;
		picb = 0;
	} else {
		/*
		 * We read the position counters, but we're careful to avoid
		 * the situation where the position counter resets at the end
		 * of a buffer.
		 */
		for (int i = 0; i < 2; i++) {
			civ = I810_BM_GET8(regoff + I810_OFFSET_CIV);
			picb = I810_BM_GET16(port->picboff);
			if (I810_BM_GET8(regoff + I810_OFFSET_CIV) == civ) {
				/*
				 * Chip did not start a new index,
				 * so the picb is valid.
				 */
				break;
			}
		}

		if (civ >= port->civ) {
			n = civ - port->civ;
		} else {
			n = civ + (I810_BD_NUMS - port->civ);
		}
		port->count += (n * port->fragfr);
	}
	port->civ = civ;
	port->picb = picb;
}

/*
 * audio810_attach()
 *
 * Description:
 *	Attach an instance of the audio810 driver. This routine does the
 * 	device dependent attach tasks, and registers with the audio framework.
 *
 * Arguments:
 *	dev_info_t	*dip	Pointer to the device's dev_info struct
 *	ddi_attach_cmd_t cmd	Attach command
 *
 * Returns:
 *	DDI_SUCCESS		The driver was initialized properly
 *	DDI_FAILURE		The driver couldn't be initialized properly
 */
static int
audio810_attach(dev_info_t *dip)
{
	uint16_t		cmdreg;
	audio810_state_t	*statep;
	audio_dev_t		*adev;
	ddi_acc_handle_t	pcih;
	uint32_t		devid;
	uint32_t		gsr;
	const char		*name;
	const char		*vers;
	uint8_t			nch;
	int			maxch;

	/* we don't support high level interrupts in the driver */
	if (ddi_intr_hilevel(dip, 0) != 0) {
		cmn_err(CE_WARN, "!%s: unsupported high level interrupt",
		    audio810_name);
		return (DDI_FAILURE);
	}

	/* allocate the soft state structure */
	statep = kmem_zalloc(sizeof (*statep), KM_SLEEP);
	ddi_set_driver_private(dip, statep);

	/* get iblock cookie information */
	if (ddi_get_iblock_cookie(dip, 0, &statep->iblock) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "!%s: cannot get iblock cookie",
		    audio810_name);
		kmem_free(statep, sizeof (*statep));
		return (DDI_FAILURE);
	}
	mutex_init(&statep->inst_lock, NULL, MUTEX_DRIVER, statep->iblock);
	mutex_init(&statep->ac_lock, NULL, MUTEX_DRIVER, statep->iblock);

	if ((adev = audio_dev_alloc(dip, 0)) == NULL) {
		cmn_err(CE_WARN, "!%s: unable to allocate audio dev",
		    audio810_name);
		goto error;
	}
	statep->adev = adev;
	statep->dip = dip;

	/* map in the registers, allocate DMA buffers, etc. */
	if (audio810_map_regs(dip, statep) != DDI_SUCCESS) {
		audio_dev_warn(adev, "couldn't map registers");
		goto error;
	}

	/* set PCI command register */
	if (pci_config_setup(dip, &pcih) != DDI_SUCCESS) {
		audio_dev_warn(adev, "pci conf mapping failed");
		goto error;
	}
	cmdreg = pci_config_get16(pcih, PCI_CONF_COMM);
	pci_config_put16(pcih, PCI_CONF_COMM,
	    cmdreg | PCI_COMM_IO | PCI_COMM_MAE | PCI_COMM_ME);
	devid = pci_config_get16(pcih, PCI_CONF_VENID);
	devid <<= 16;
	devid |= pci_config_get16(pcih, PCI_CONF_DEVID);
	pci_config_teardown(&pcih);

	name = "Unknown AC'97";
	vers = "";

	statep->quirk = QUIRK_NONE;
	switch (devid) {
	case 0x80862415:
		name = "Intel AC'97";
		vers = "ICH";
		break;
	case 0x80862425:
		name = "Intel AC'97";
		vers = "ICH0";
		break;
	case 0x80867195:
		name = "Intel AC'97";
		vers = "440MX";
		break;
	case 0x80862445:
		name = "Intel AC'97";
		vers = "ICH2";
		break;
	case 0x80862485:
		name = "Intel AC'97";
		vers = "ICH3";
		break;
	case 0x808624C5:
		name = "Intel AC'97";
		vers = "ICH4";
		break;
	case 0x808624D5:
		name = "Intel AC'97";
		vers = "ICH5";
		break;
	case 0x8086266E:
		name = "Intel AC'97";
		vers = "ICH6";
		break;
	case 0x808627DE:
		name = "Intel AC'97";
		vers = "ICH7";
		break;
	case 0x808625A6:
		name = "Intel AC'97";
		vers = "6300ESB";
		break;
	case 0x80862698:
		name = "Intel AC'97";
		vers = "ESB2";
		break;
	case 0x10397012:
		name = "SiS AC'97";
		vers = "7012";
		statep->quirk = QUIRK_SIS7012;
		break;
	case 0x10de01b1:	/* nForce */
		name = "NVIDIA AC'97";
		vers = "MCP1";
		break;
	case 0x10de006a:	/* nForce 2 */
		name = "NVIDIA AC'97";
		vers = "MCP2";
		break;
	case 0x10de00da:	/* nForce 3 */
		name = "NVIDIA AC'97";
		vers = "MCP3";
		break;
	case 0x10de00ea:
		name = "NVIDIA AC'97";
		vers = "CK8S";
		break;
	case 0x10de0059:
		name = "NVIDIA AC'97";
		vers = "CK804";
		break;
	case 0x10de008a:
		name = "NVIDIA AC'97";
		vers = "CK8";
		break;
	case 0x10de003a:	/* nForce 4 */
		name = "NVIDIA AC'97";
		vers = "MCP4";
		break;
	case 0x10de026b:
		name = "NVIDIA AC'97";
		vers = "MCP51";
		break;
	case 0x1022746d:
		name = "AMD AC'97";
		vers = "8111";
		break;
	case 0x10227445:
		name = "AMD AC'97";
		vers = "AMD768";
		break;
	}
	/* set device information */
	audio_dev_set_description(adev, name);
	audio_dev_set_version(adev, vers);

	/* initialize audio controller and AC97 codec */
	if (audio810_chip_init(statep) != DDI_SUCCESS) {
		audio_dev_warn(adev, "failed to init chip");
		goto error;
	}

	/* allocate ac97 handle */
	statep->ac97 = ac97_alloc(dip, audio810_read_ac97, audio810_write_ac97,
	    statep);
	if (statep->ac97 == NULL) {
		audio_dev_warn(adev, "failed to allocate ac97 handle");
		goto error;
	}

	/* initialize the AC'97 part */
	if (ac97_init(statep->ac97, adev) != DDI_SUCCESS) {
		audio_dev_warn(adev, "ac'97 initialization failed");
		goto error;
	}

	/*
	 * Override "max-channels" property to prevent configuration
	 * of 4 or 6 (or possibly even 8!) channel audio.  The default
	 * is to support as many channels as the hardware can do.
	 *
	 * (Hmmm... perhaps this should be driven in the common
	 * framework.  The framework could even offer simplistic upmix
	 * and downmix for various standard configs.)
	 */
	maxch = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "max-channels", ac97_num_channels(statep->ac97));
	if (maxch < 2) {
		maxch = 2;
	}

	gsr = I810_BM_GET32(I810_REG_GSR);
	if (gsr & I810_GSR_CAP6CH) {
		nch = 6;
	} else if (gsr & I810_GSR_CAP4CH) {
		nch = 4;
	} else {
		nch = 2;
	}

	statep->maxch = (uint8_t)min(nch, maxch);
	statep->maxch &= ~1;

	/* allocate port structures */
	if ((audio810_alloc_port(statep, I810_PCM_OUT, statep->maxch) !=
	    DDI_SUCCESS) ||
	    (audio810_alloc_port(statep, I810_PCM_IN, 2) != DDI_SUCCESS)) {
		goto error;
	}

	/* set up kernel statistics */
	if ((statep->ksp = kstat_create(I810_NAME, ddi_get_instance(dip),
	    I810_NAME, "controller", KSTAT_TYPE_INTR, 1,
	    KSTAT_FLAG_PERSISTENT)) != NULL) {
		kstat_install(statep->ksp);
	}

	/* set up the interrupt handler */
	if (ddi_add_intr(dip, 0, &statep->iblock,
	    NULL, audio810_intr, (caddr_t)statep) != DDI_SUCCESS) {
		audio_dev_warn(adev, "bad interrupt specification");
		goto error;
	}
	statep->intr_added = B_TRUE;

	if (audio_dev_register(adev) != DDI_SUCCESS) {
		audio_dev_warn(adev, "unable to register with framework");
		goto error;
	}

	ddi_report_dev(dip);

	return (DDI_SUCCESS);

error:
	audio810_destroy(statep);

	return (DDI_FAILURE);
}


/*
 * audio810_resume()
 *
 * Description:
 *	Resume operation of the device after sleeping or hibernating.
 *	Note that this should never fail, even if hardware goes wonky,
 *	because the current PM framework will panic if it does.
 *
 * Arguments:
 *	dev_info_t	*dip	Pointer to the device's dev_info struct
 *
 * Returns:
 *	DDI_SUCCESS		The driver was resumed.
 */
static int
audio810_resume(dev_info_t *dip)
{
	audio810_state_t	*statep;
	audio_dev_t		*adev;

	/* this should always be valid */
	statep = ddi_get_driver_private(dip);
	adev = statep->adev;

	ASSERT(statep != NULL);
	ASSERT(dip == statep->dip);

	/* Restore the audio810 chip's state */
	if (audio810_chip_init(statep) != DDI_SUCCESS) {
		/*
		 * Note that PM gurus say we should return
		 * success here.  Failure of audio shouldn't
		 * be considered FATAL to the system.  The
		 * upshot is that audio will not progress.
		 */
		audio_dev_warn(adev, "DDI_RESUME failed to init chip");
		return (DDI_SUCCESS);
	}

	/* allow ac97 operations again */
	ac97_resume(statep->ac97);

	mutex_enter(&statep->inst_lock);

	ASSERT(statep->suspended);
	statep->suspended = B_FALSE;

	for (int i = 0; i < I810_NUM_PORTS; i++) {

		audio810_port_t *port = statep->ports[i];

		if (port != NULL) {
			/* reset framework DMA engine buffer */
			if (port->engine != NULL) {
				audio_engine_reset(port->engine);
			}

			/* reset and initialize hardware ports */
			audio810_reset_port(port);
			if (port->started) {
				audio810_start_port(port);
			} else {
				audio810_stop_port(port);
			}
		}
	}
	mutex_exit(&statep->inst_lock);

	return (DDI_SUCCESS);
}

/*
 * audio810_detach()
 *
 * Description:
 *	Detach an instance of the audio810 driver.
 *
 * Arguments:
 *	dev_info_t		*dip	Pointer to the device's dev_info struct
 *
 * Returns:
 *	DDI_SUCCESS	The driver was detached
 *	DDI_FAILURE	The driver couldn't be detached
 */
static int
audio810_detach(dev_info_t *dip)
{
	audio810_state_t	*statep;

	statep = ddi_get_driver_private(dip);
	ASSERT(statep != NULL);

	/* don't detach us if we are still in use */
	if (audio_dev_unregister(statep->adev) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	audio810_destroy(statep);
	return (DDI_SUCCESS);
}

/*
 * audio810_suspend()
 *
 * Description:
 *	Suspend an instance of the audio810 driver, in preparation for
 *	sleep or hibernation.
 *
 * Arguments:
 *	dev_info_t		*dip	Pointer to the device's dev_info struct
 *
 * Returns:
 *	DDI_SUCCESS	The driver was suspended
 */
static int
audio810_suspend(dev_info_t *dip)
{
	audio810_state_t	*statep;

	statep = ddi_get_driver_private(dip);
	ASSERT(statep != NULL);

	ac97_suspend(statep->ac97);

	mutex_enter(&statep->inst_lock);

	ASSERT(statep->suspended == B_FALSE);

	statep->suspended = B_TRUE; /* stop new ops */

	/* stop DMA engines */
	audio810_stop_dma(statep);

	mutex_exit(&statep->inst_lock);

	return (DDI_SUCCESS);
}

/*
 * audio810_alloc_port()
 *
 * Description:
 *	This routine allocates the DMA handles and the memory for the
 *	DMA engines to use.  It also configures the BDL lists properly
 *	for use.
 *
 * Arguments:
 *	dev_info_t	*dip	Pointer to the device's devinfo
 *
 * Returns:
 *	DDI_SUCCESS		Registers successfully mapped
 *	DDI_FAILURE		Registers not successfully mapped
 */
static int
audio810_alloc_port(audio810_state_t *statep, int num, uint8_t nchan)
{
	ddi_dma_cookie_t	cookie;
	uint_t			count;
	int			dir;
	unsigned		caps;
	char			*prop;
	char			*nfprop;
	audio_dev_t		*adev;
	audio810_port_t		*port;
	uint32_t		paddr;
	int			rc;
	dev_info_t		*dip;
	i810_bd_entry_t		*bdentry;

	adev = statep->adev;
	dip = statep->dip;

	port = kmem_zalloc(sizeof (*port), KM_SLEEP);
	statep->ports[num] = port;
	port->statep = statep;
	port->started = B_FALSE;
	port->nchan = nchan;
	port->num = num;

	switch (num) {
	case I810_PCM_IN:
		prop = "record-interrupts";
		nfprop = "record-fragments";
		dir = DDI_DMA_READ;
		caps = ENGINE_INPUT_CAP;
		port->sync_dir = DDI_DMA_SYNC_FORKERNEL;
		port->regoff = I810_BASE_PCM_IN;
		break;
	case I810_PCM_OUT:
		prop = "play-interrupts";
		nfprop = "play-fragments";
		dir = DDI_DMA_WRITE;
		caps = ENGINE_OUTPUT_CAP;
		port->sync_dir = DDI_DMA_SYNC_FORDEV;
		port->regoff = I810_BASE_PCM_OUT;
		break;
	default:
		audio_dev_warn(adev, "bad port number (%d)!", num);
		return (DDI_FAILURE);
	}

	/*
	 * SiS 7012 swaps status and picb registers.
	 */
	if (statep->quirk == QUIRK_SIS7012) {
		port->stsoff = port->regoff + I810_OFFSET_PICB;
		port->picboff = port->regoff + I810_OFFSET_SR;
	} else {
		port->stsoff = port->regoff + I810_OFFSET_SR;
		port->picboff = port->regoff + I810_OFFSET_PICB;
	}

	port->intrs = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, prop, I810_INTS);

	/* make sure the values are good */
	if (port->intrs < I810_MIN_INTS) {
		audio_dev_warn(adev, "%s too low, %d, resetting to %d",
		    prop, port->intrs, I810_INTS);
		port->intrs = I810_INTS;
	} else if (port->intrs > I810_MAX_INTS) {
		audio_dev_warn(adev, "%s too high, %d, resetting to %d",
		    prop, port->intrs, I810_INTS);
		port->intrs = I810_INTS;
	}

	port->nfrag = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, nfprop, I810_NFRAGS);

	/*
	 * Note that fragments must divide evenly into I810_BD_NUMS (32).
	 * We also insist that the value be larger than our "playahead".
	 */
	if (port->nfrag <= 8) {
		port->nfrag = 8;
	} else if (port->nfrag <= 16) {
		port->nfrag = 16;
	} else {
		port->nfrag = I810_BD_NUMS;
	}

	/*
	 * Figure out how much space we need.  Sample rate is 48kHz, and
	 * we need to store 32 chunks.  (Note that this means that low
	 * interrupt frequencies will require more RAM.  We could probably
	 * do some cleverness to use a shorter BD list.)
	 */
	port->fragfr = 48000 / port->intrs;
	port->fragfr = I810_ROUNDUP(port->fragfr, I810_MOD_SIZE);
	port->fragsz = port->fragfr * port->nchan * 2;
	port->samp_size = port->fragsz * port->nfrag;

	/* allocate dma handle */
	rc = ddi_dma_alloc_handle(dip, &sample_buf_dma_attr, DDI_DMA_SLEEP,
	    NULL, &port->samp_dmah);
	if (rc != DDI_SUCCESS) {
		audio_dev_warn(adev, "ddi_dma_alloc_handle failed: %d", rc);
		return (DDI_FAILURE);
	}
	/* allocate DMA buffer */
	rc = ddi_dma_mem_alloc(port->samp_dmah, port->samp_size, &buf_attr,
	    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL, &port->samp_kaddr,
	    &port->samp_size, &port->samp_acch);
	if (rc == DDI_FAILURE) {
		audio_dev_warn(adev, "dma_mem_alloc (%d) failed: %d",
		    port->samp_size, rc);
		return (DDI_FAILURE);
	}

	/* bind DMA buffer */
	rc = ddi_dma_addr_bind_handle(port->samp_dmah, NULL,
	    port->samp_kaddr, port->samp_size, dir|DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &cookie, &count);
	if ((rc != DDI_DMA_MAPPED) || (count != 1)) {
		audio_dev_warn(adev,
		    "ddi_dma_addr_bind_handle failed: %d", rc);
		return (DDI_FAILURE);
	}
	port->samp_paddr = cookie.dmac_address;

	/*
	 * now, from here we allocate DMA memory for buffer descriptor list.
	 * we allocate adjacent DMA memory for all DMA engines.
	 */
	rc = ddi_dma_alloc_handle(dip, &bdlist_dma_attr, DDI_DMA_SLEEP,
	    NULL, &port->bdl_dmah);
	if (rc != DDI_SUCCESS) {
		audio_dev_warn(adev, "ddi_dma_alloc_handle(bdlist) failed");
		return (DDI_FAILURE);
	}

	/*
	 * we allocate all buffer descriptors lists in continuous dma memory.
	 */
	port->bdl_size = sizeof (i810_bd_entry_t) * I810_BD_NUMS;
	rc = ddi_dma_mem_alloc(port->bdl_dmah, port->bdl_size,
	    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &port->bdl_kaddr, &port->bdl_size, &port->bdl_acch);
	if (rc != DDI_SUCCESS) {
		audio_dev_warn(adev, "ddi_dma_mem_alloc(bdlist) failed");
		return (DDI_FAILURE);
	}

	rc = ddi_dma_addr_bind_handle(port->bdl_dmah, NULL, port->bdl_kaddr,
	    port->bdl_size, DDI_DMA_WRITE|DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &cookie, &count);
	if ((rc != DDI_DMA_MAPPED) || (count != 1)) {
		audio_dev_warn(adev, "addr_bind_handle failed");
		return (DDI_FAILURE);
	}
	port->bdl_paddr = cookie.dmac_address;

	/*
	 * Wire up the BD list.
	 */
	paddr = port->samp_paddr;
	bdentry = (void *)port->bdl_kaddr;
	for (int i = 0; i < I810_BD_NUMS; i++) {

		/* set base address of buffer */
		ddi_put32(port->bdl_acch, &bdentry->buf_base, paddr);
		/*
		 * SiS 7012 counts samples in bytes, all other count
		 * in words.
		 */
		ddi_put16(port->bdl_acch, &bdentry->buf_len,
		    statep->quirk == QUIRK_SIS7012 ? port->fragsz :
		    port->fragsz / 2);
		ddi_put16(port->bdl_acch, &bdentry->buf_cmd,
		    BUF_CMD_IOC | BUF_CMD_BUP);
		paddr += port->fragsz;
		if ((i % port->nfrag) == (port->nfrag - 1)) {
			/* handle wrap */
			paddr = port->samp_paddr;
		}
		bdentry++;
	}
	(void) ddi_dma_sync(port->bdl_dmah, 0, 0, DDI_DMA_SYNC_FORDEV);

	port->engine = audio_engine_alloc(&audio810_engine_ops, caps);
	if (port->engine == NULL) {
		audio_dev_warn(adev, "audio_engine_alloc failed");
		return (DDI_FAILURE);
	}

	audio_engine_set_private(port->engine, port);
	audio_dev_add_engine(adev, port->engine);

	return (DDI_SUCCESS);
}

/*
 * audio810_free_port()
 *
 * Description:
 *	This routine unbinds the DMA cookies, frees the DMA buffers,
 *	deallocates the DMA handles.
 *
 * Arguments:
 *	audio810_port_t	*port	The port structure for a DMA engine.
 */
static void
audio810_free_port(audio810_port_t *port)
{
	if (port == NULL)
		return;

	if (port->engine) {
		audio_dev_remove_engine(port->statep->adev, port->engine);
		audio_engine_free(port->engine);
	}
	if (port->bdl_paddr) {
		(void) ddi_dma_unbind_handle(port->bdl_dmah);
	}
	if (port->bdl_acch) {
		ddi_dma_mem_free(&port->bdl_acch);
	}
	if (port->bdl_dmah) {
		ddi_dma_free_handle(&port->bdl_dmah);
	}
	if (port->samp_paddr) {
		(void) ddi_dma_unbind_handle(port->samp_dmah);
	}
	if (port->samp_acch) {
		ddi_dma_mem_free(&port->samp_acch);
	}
	if (port->samp_dmah) {
		ddi_dma_free_handle(&port->samp_dmah);
	}
	kmem_free(port, sizeof (*port));
}

/*
 * audio810_map_regs()
 *
 * Description:
 *	The registers are mapped in.
 *
 * Arguments:
 *	dev_info_t	*dip	Pointer to the device's devinfo
 *
 * Returns:
 *	DDI_SUCCESS		Registers successfully mapped
 *	DDI_FAILURE		Registers not successfully mapped
 */
static int
audio810_map_regs(dev_info_t *dip, audio810_state_t *statep)
{
	uint_t			nregs = 0;
	int			*regs_list;
	int			i;
	int			pciBar1 = 0;
	int			pciBar2 = 0;
	int			pciBar3 = 0;
	int			pciBar4 = 0;

	/* check the "reg" property to get the length of memory-mapped I/O */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (int **)&regs_list, &nregs) != DDI_PROP_SUCCESS) {
		audio_dev_warn(statep->adev, "inquire regs property failed");
		goto error;
	}
	/*
	 * Some hardwares, such as Intel ICH0/ICH and AMD 8111, use PCI 0x10
	 * and 0x14 BAR separately for native audio mixer BAR and native bus
	 * mastering BAR. More advanced hardwares, such as Intel ICH4 and ICH5,
	 * support PCI memory BAR, via PCI 0x18 and 0x1C BAR, that allows for
	 * higher performance access to the controller register. All features
	 * can be accessed via this BAR making the I/O BAR (PCI 0x10 and 0x14
	 * BAR) capabilities obsolete. However, these controller maintain the
	 * I/O BAR capability to allow for the reuse of legacy code maintaining
	 * backward compatibility. The I/O BAR is disabled unless system BIOS
	 * enables the simultaneous backward compatible capability on the 0x41
	 * register.
	 *
	 * When I/O BAR is enabled, the value of "reg" property should be like
	 * this,
	 *	phys_hi   phys_mid  phys_lo   size_hi   size_lo
	 * --------------------------------------------------------
	 *	0000fd00  00000000  00000000  00000000  00000000
	 *	0100fd10  00000000  00000000  00000000  00000100
	 *	0100fd14  00000000  00000000  00000000  00000040
	 *	0200fd18  00000000  00000000  00000000  00000200
	 *	0200fd1c  00000000  00000000  00000000  00000100
	 *
	 * When I/O BAR is disabled, the "reg" property of the device node does
	 * not consist of the description for the I/O BAR. The following example
	 * illustrates the vaule of "reg" property,
	 *
	 *	phys_hi   phys_mid  phys_lo   size_hi   size_lo
	 * --------------------------------------------------------
	 *	0000fd00  00000000  00000000  00000000  00000000
	 *	0200fd18  00000000  00000000  00000000  00000200
	 *	0200fd1c  00000000  00000000  00000000  00000100
	 *
	 * If the hardware has memory-mapped I/O access, first try to use
	 * this facility, otherwise we will try I/O access.
	 */
	for (i = 1; i < nregs/I810_INTS_PER_REG_PROP; i++) {
		switch (regs_list[I810_INTS_PER_REG_PROP * i] & 0x000000ff) {
			case 0x10:
				pciBar1 = i;
				break;
			case 0x14:
				pciBar2 = i;
				break;
			case 0x18:
				pciBar3 = i;
				break;
			case 0x1c:
				pciBar4 = i;
				break;
			default:	/* we don't care others */
				break;
		}
	}

	if ((pciBar3 != 0) && (pciBar4 != 0)) {
		/* map audio mixer registers */
		if ((ddi_regs_map_setup(dip, pciBar3, &statep->am_regs_base, 0,
		    0, &dev_attr, &statep->am_regs_handle)) != DDI_SUCCESS) {
			audio_dev_warn(statep->adev,
			    "memory am mapping failed");
			goto error;
		}

		/* map bus master register */
		if ((ddi_regs_map_setup(dip, pciBar4, &statep->bm_regs_base, 0,
		    0, &dev_attr, &statep->bm_regs_handle)) != DDI_SUCCESS) {
			audio_dev_warn(statep->adev,
			    "memory bm mapping failed");
			goto error;
		}

	} else if ((pciBar1 != 0) && (pciBar2 != 0)) {
		/* map audio mixer registers */
		if ((ddi_regs_map_setup(dip, pciBar1, &statep->am_regs_base, 0,
		    0, &dev_attr, &statep->am_regs_handle)) != DDI_SUCCESS) {
			audio_dev_warn(statep->adev, "I/O am mapping failed");
			goto error;
		}

		/* map bus master register */
		if ((ddi_regs_map_setup(dip, pciBar2, &statep->bm_regs_base, 0,
		    0, &dev_attr, &statep->bm_regs_handle)) != DDI_SUCCESS) {
			audio_dev_warn(statep->adev, "I/O bm mapping failed");
			goto error;
		}
	} else {
		audio_dev_warn(statep->adev, "map_regs() pci BAR error");
		goto error;
	}

	ddi_prop_free(regs_list);

	return (DDI_SUCCESS);

error:
	if (nregs > 0) {
		ddi_prop_free(regs_list);
	}
	audio810_unmap_regs(statep);

	return (DDI_FAILURE);
}

/*
 * audio810_unmap_regs()
 *
 * Description:
 *	This routine unmaps control registers.
 *
 * Arguments:
 *	audio810_state_t	*state	The device's state structure
 */
static void
audio810_unmap_regs(audio810_state_t *statep)
{
	if (statep->bm_regs_handle) {
		ddi_regs_map_free(&statep->bm_regs_handle);
	}

	if (statep->am_regs_handle) {
		ddi_regs_map_free(&statep->am_regs_handle);
	}
}

/*
 * audio810_chip_init()
 *
 * Description:
 *	This routine initializes the AMD 8111 audio controller.
 *	codec.
 *
 * Arguments:
 *	audio810_state_t	*state		The device's state structure
 *
 * Returns:
 *	DDI_SUCCESS	The hardware was initialized properly
 *	DDI_FAILURE	The hardware couldn't be initialized properly
 */
static int
audio810_chip_init(audio810_state_t *statep)
{
	uint32_t	gcr;
	uint32_t	gsr;
	uint32_t	codec_ready;
	int 		loop;
	clock_t		ticks;

	gcr = I810_BM_GET32(I810_REG_GCR);
	ticks = drv_usectohz(100);

	/*
	 * Clear the channels bits for now.  We'll set them later in
	 * reset port.
	 */
	if (statep->quirk == QUIRK_SIS7012) {
		gcr &= ~(I810_GCR_ACLINK_OFF | I810_GCR_SIS_CHANNELS_MASK);
	} else {
		gcr &= ~(I810_GCR_ACLINK_OFF | I810_GCR_CHANNELS_MASK);
	}

	/*
	 * Datasheet(ICH5, document number of Intel: 252751-001):
	 * 3.6.5.5(page 37)
	 * 	if reset bit(bit1) is "0", driver must set it
	 * 	to "1" to de-assert the AC_RESET# signal in AC
	 * 	link, thus completing a cold reset. But if the
	 * 	bit is "1", then a warm reset is required.
	 */
	gcr |= (gcr & I810_GCR_COLD_RST) == 0 ?
	    I810_GCR_COLD_RST:I810_GCR_WARM_RST;
	I810_BM_PUT32(I810_REG_GCR, gcr);

	/* according AC'97 spec, wait for codec reset */
	for (loop = 6000; --loop >= 0; ) {
		delay(ticks);
		gcr = I810_BM_GET32(I810_REG_GCR);
		if ((gcr & I810_GCR_WARM_RST) == 0) {
			break;
		}
	}

	/* codec reset failed */
	if (loop < 0) {
		audio_dev_warn(statep->adev, "Failed to reset codec");
		return (DDI_FAILURE);
	}

	/*
	 * Wait for codec ready. The hardware can provide the state of
	 * codec ready bit on SDATA_IN[0], SDATA_IN[1] or SDATA_IN[2]
	 */
	codec_ready =
	    I810_GSR_PRI_READY | I810_GSR_SEC_READY | I810_GSR_TRI_READY;
	for (loop = 7000; --loop >= 0; ) {
		delay(ticks);
		gsr = I810_BM_GET32(I810_REG_GSR);
		if ((gsr & codec_ready) != 0) {
			break;
		}
	}
	if (loop < 0) {
		audio_dev_warn(statep->adev, "No codec ready signal received");
		return (DDI_FAILURE);
	}

	/*
	 * put the audio controller into quiet state, everything off
	 */
	audio810_stop_dma(statep);

	return (DDI_SUCCESS);
}

/*
 * audio810_stop_dma()
 *
 * Description:
 *	This routine is used to put each DMA engine into the quiet state.
 *
 * Arguments:
 *	audio810_state_t	*state		The device's state structure
 */
static void
audio810_stop_dma(audio810_state_t *statep)
{
	if (statep->bm_regs_handle == NULL) {
		return;
	}
	/* pause bus master (needed for the following reset register) */
	I810_BM_PUT8(I810_BASE_PCM_IN + I810_OFFSET_CR, 0x0);
	I810_BM_PUT8(I810_BASE_PCM_OUT + I810_OFFSET_CR, 0x0);
	I810_BM_PUT8(I810_BASE_MIC + I810_OFFSET_CR, 0x0);

	/* and then reset the bus master registers for a three DMA engines */
	I810_BM_PUT8(I810_BASE_PCM_IN + I810_OFFSET_CR, I810_BM_CR_RST);
	I810_BM_PUT8(I810_BASE_PCM_OUT + I810_OFFSET_CR, I810_BM_CR_RST);
	I810_BM_PUT8(I810_BASE_MIC + I810_OFFSET_CR, I810_BM_CR_RST);
}


/*
 * audio810_codec_sync()
 *
 * Description:
 *	Serialize access to the AC97 audio mixer registers.
 *
 * Arguments:
 *	audio810_state_t	*state		The device's state structure
 *
 * Returns:
 *	DDI_SUCCESS		Ready for an I/O access to the codec
 *	DDI_FAILURE		An I/O access is currently in progress, can't
 *				perform another I/O access.
 */
static int
audio810_codec_sync(audio810_state_t *statep)
{
	int 		i;
	uint16_t	casr;

	for (i = 0; i < 300; i++) {
		casr = I810_BM_GET8(I810_REG_CASR);
		if ((casr & 1) == 0) {
			return (DDI_SUCCESS);
		}
		drv_usecwait(10);
	}

	return (DDI_FAILURE);
}

/*
 * audio810_write_ac97()
 *
 * Description:
 *	Set the specific AC97 Codec register.
 *
 * Arguments:
 *	void		*arg		The device's state structure
 *	uint8_t		reg		AC97 register number
 *	uint16_t	data		The data want to be set
 */
static void
audio810_write_ac97(void *arg, uint8_t reg, uint16_t data)
{
	audio810_state_t	*statep = arg;

	mutex_enter(&statep->ac_lock);
	if (audio810_codec_sync(statep) == DDI_SUCCESS) {
		I810_AM_PUT16(reg, data);
	}
	mutex_exit(&statep->ac_lock);

	(void) audio810_read_ac97(statep, reg);
}

/*
 * audio810_read_ac97()
 *
 * Description:
 *	Get the specific AC97 Codec register.
 *
 * Arguments:
 *	void		*arg		The device's state structure
 *	uint8_t		reg		AC97 register number
 *
 * Returns:
 *	The register value.
 */
static uint16_t
audio810_read_ac97(void *arg, uint8_t reg)
{
	audio810_state_t	*statep = arg;
	uint16_t		val = 0xffff;

	mutex_enter(&statep->ac_lock);
	if (audio810_codec_sync(statep) == DDI_SUCCESS) {
		val = I810_AM_GET16(reg);
	}
	mutex_exit(&statep->ac_lock);
	return (val);
}

/*
 * audio810_destroy()
 *
 * Description:
 *	This routine releases all resources held by the device instance,
 *	as part of either detach or a failure in attach.
 *
 * Arguments:
 *	audio810_state_t	*state	The device soft state.
 */
void
audio810_destroy(audio810_state_t *statep)
{
	/* stop DMA engines */
	audio810_stop_dma(statep);

	if (statep->intr_added) {
		ddi_remove_intr(statep->dip, 0, NULL);
	}

	if (statep->ksp) {
		kstat_delete(statep->ksp);
	}

	for (int i = 0; i < I810_NUM_PORTS; i++) {
		audio810_free_port(statep->ports[i]);
	}

	audio810_unmap_regs(statep);

	if (statep->ac97)
		ac97_free(statep->ac97);

	if (statep->adev)
		audio_dev_free(statep->adev);

	mutex_destroy(&statep->inst_lock);
	mutex_destroy(&statep->ac_lock);
	kmem_free(statep, sizeof (*statep));
}
