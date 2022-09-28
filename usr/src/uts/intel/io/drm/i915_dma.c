/* BEGIN CSTYLED */

/* i915_dma.c -- DMA support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright (c) 2009, Intel Corporation.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"



/* Really want an OS-independent resettable timer.  Would like to have
 * this loop run for (eg) 3 sec, but have the timer reset every time
 * the head pointer changes, so that EBUSY only happens if the ring
 * actually stalls for (eg) 3 seconds.
 */
/*ARGSUSED*/
int i915_wait_ring(drm_device_t * dev, int n, const char *caller)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);
	u32 last_head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
	u32 acthd_reg = IS_I965G(dev) ? ACTHD_I965 : ACTHD;
	u32 last_acthd = I915_READ(acthd_reg);
	u32 acthd;
	int i;

	for (i = 0; i < 100000; i++) {
		ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
		acthd = I915_READ(acthd_reg);
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;
		if (ring->space >= n)
			return 0;

		if (ring->head != last_head)
			i = 0;

		if (acthd != last_acthd)
			i = 0;

		last_head = ring->head;
		last_acthd = acthd;
		DRM_UDELAY(10);
	}

	return (EBUSY);
}

int i915_init_hardware_status(drm_device_t *dev)
{
       drm_i915_private_t *dev_priv = dev->dev_private;
       drm_dma_handle_t *dmah;

       /* Program Hardware Status Page */
       dmah = drm_pci_alloc(dev, PAGE_SIZE, PAGE_SIZE, 0xffffffff,1);

       if (!dmah) {
               DRM_ERROR("Can not allocate hardware status page\n");
               return -ENOMEM;
       }

       dev_priv->status_page_dmah = dmah;
       dev_priv->hw_status_page = (void *)dmah->vaddr;
       dev_priv->dma_status_page = dmah->paddr;

       (void) memset(dev_priv->hw_status_page, 0, PAGE_SIZE);

       I915_WRITE(HWS_PGA, dev_priv->dma_status_page);
       (void) I915_READ(HWS_PGA);

       DRM_DEBUG("Enabled hardware status page add 0x%lx read GEM HWS 0x%x\n",dev_priv->hw_status_page, READ_HWSP(dev_priv, 0x20));
       return 0;
}

void i915_free_hardware_status(drm_device_t *dev)
{
       drm_i915_private_t *dev_priv = dev->dev_private;
	if (!I915_NEED_GFX_HWS(dev)) {
		if (dev_priv->status_page_dmah) {
			DRM_DEBUG("free status_page_dmal %x", dev_priv->status_page_dmah);
			drm_pci_free(dev, dev_priv->status_page_dmah);
			dev_priv->status_page_dmah = NULL;
			/* Need to rewrite hardware status page */
			I915_WRITE(HWS_PGA, 0x1ffff000);
		}
       	} else {
		if (dev_priv->status_gfx_addr) {
			DRM_DEBUG("free status_gfx_addr %x", dev_priv->status_gfx_addr);
			dev_priv->status_gfx_addr = 0;
			drm_core_ioremapfree(&dev_priv->hws_map, dev);
			I915_WRITE(HWS_PGA, 0x1ffff000);
		}
	}

}

void i915_kernel_lost_context(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);

       ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
       ring->tail = I915_READ(PRB0_TAIL) & TAIL_ADDR;
	ring->space = ring->head - (ring->tail + 8);
	if (ring->space < 0)
		ring->space += ring->Size;

}

static int i915_dma_cleanup(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv =
		    (drm_i915_private_t *) dev->dev_private;

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		(void) drm_irq_uninstall(dev);

	if (dev_priv->ring.virtual_start) {
		drm_core_ioremapfree(&dev_priv->ring.map, dev);
		dev_priv->ring.virtual_start = 0;
		dev_priv->ring.map.handle = 0;
		dev_priv->ring.map.size = 0;
	}

#ifdef I915_HAVE_GEM
	if (I915_NEED_GFX_HWS(dev))    
#endif
	i915_free_hardware_status(dev);

	dev_priv->sarea = NULL;
	dev_priv->sarea_priv = NULL;

	return 0;
}

static int i915_initialize(drm_device_t * dev,
			   drm_i915_init_t * init)
{
	drm_i915_private_t *dev_priv =
	    (drm_i915_private_t *)dev->dev_private;

	DRM_GETSAREA();
	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		dev->dev_private = (void *)dev_priv;
		(void) i915_dma_cleanup(dev);
		return (EINVAL);
	}

	dev_priv->sarea_priv = (drm_i915_sarea_t *)(uintptr_t)
			((u8 *) dev_priv->sarea->handle +
			 init->sarea_priv_offset);

	if (init->ring_size != 0) {
		if (dev_priv->ring.ring_obj != NULL) {
			(void) i915_dma_cleanup(dev);
			DRM_ERROR("Client tried to initialize ringbuffer in "
				  "GEM mode\n");
			return -EINVAL;
		}

		dev_priv->ring.Size = init->ring_size;
		dev_priv->ring.tail_mask = dev_priv->ring.Size - 1;

		dev_priv->ring.map.offset = (u_offset_t)init->ring_start;
		dev_priv->ring.map.size = init->ring_size;
		dev_priv->ring.map.type = 0;
		dev_priv->ring.map.flags = 0;
		dev_priv->ring.map.mtrr = 0;

		drm_core_ioremap(&dev_priv->ring.map, dev);

		if (dev_priv->ring.map.handle == NULL) {
			(void) i915_dma_cleanup(dev);
			DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
			return (ENOMEM);
		}
	}

	dev_priv->ring.virtual_start = (u8 *)dev_priv->ring.map.dev_addr;
	dev_priv->cpp = init->cpp;
	dev_priv->back_offset = init->back_offset;
	dev_priv->front_offset = init->front_offset;
	dev_priv->current_page = 0;
	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;

	/* Allow hardware batchbuffers unless told otherwise.
	 */
	dev_priv->allow_batchbuffer = 1;
	return 0;
}

static int i915_dma_resume(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		return (EINVAL);
	}

	if (dev_priv->ring.map.handle == NULL) {
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return (ENOMEM);
	}

	/* Program Hardware Status Page */
	if (!dev_priv->hw_status_page) {
		DRM_ERROR("Can not find hardware status page\n");
		return (EINVAL);
	}
	DRM_DEBUG("i915_dma_resume hw status page @ %p\n", dev_priv->hw_status_page);

	if (!I915_NEED_GFX_HWS(dev))
		I915_WRITE(HWS_PGA, dev_priv->dma_status_page);
	else
		I915_WRITE(HWS_PGA, dev_priv->status_gfx_addr);
	DRM_DEBUG("Enabled hardware status page\n");

	return 0;
}

/*ARGSUSED*/
static int i915_dma_init(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_init_t init;
	int retcode = 0;

	DRM_COPYFROM_WITH_RETURN(&init, (drm_i915_init_t *)data, sizeof(init));

	switch (init.func) {
	case I915_INIT_DMA:
		retcode = i915_initialize(dev, &init);
		break;
	case I915_CLEANUP_DMA:
		retcode = i915_dma_cleanup(dev);
		break;
	case I915_RESUME_DMA:
		retcode = i915_dma_resume(dev);
		break;
	default:
		retcode = EINVAL;
		break;
	}

	return retcode;
}

/* Implement basically the same security restrictions as hardware does
 * for MI_BATCH_NON_SECURE.  These can be made stricter at any time.
 *
 * Most of the calculations below involve calculating the size of a
 * particular instruction.  It's important to get the size right as
 * that tells us where the next instruction to check is.  Any illegal
 * instruction detected will be given a size of zero, which is a
 * signal to abort the rest of the buffer.
 */
static int do_validate_cmd(int cmd)
{
	switch (((cmd >> 29) & 0x7)) {
	case 0x0:
		switch ((cmd >> 23) & 0x3f) {
		case 0x0:
			return 1;	/* MI_NOOP */
		case 0x4:
			return 1;	/* MI_FLUSH */
		default:
			return 0;	/* disallow everything else */
		}
#ifndef __SUNPRO_C
		break;
#endif
	case 0x1:
		return 0;	/* reserved */
	case 0x2:
		return (cmd & 0xff) + 2;	/* 2d commands */
	case 0x3:
		if (((cmd >> 24) & 0x1f) <= 0x18)
			return 1;

		switch ((cmd >> 24) & 0x1f) {
		case 0x1c:
			return 1;
		case 0x1d:
			switch ((cmd >> 16) & 0xff) {
			case 0x3:
				return (cmd & 0x1f) + 2;
			case 0x4:
				return (cmd & 0xf) + 2;
			default:
				return (cmd & 0xffff) + 2;
			}
		case 0x1e:
			if (cmd & (1 << 23))
				return (cmd & 0xffff) + 1;
			else
				return 1;
		case 0x1f:
			if ((cmd & (1 << 23)) == 0)	/* inline vertices */
				return (cmd & 0x1ffff) + 2;
			else if (cmd & (1 << 17))	/* indirect random */
				if ((cmd & 0xffff) == 0)
					return 0;	/* unknown length, too hard */
				else
					return (((cmd & 0xffff) + 1) / 2) + 1;
			else
				return 2;	/* indirect sequential */
		default:
			return 0;
		}
	default:
		return 0;
	}

#ifndef __SUNPRO_C
	return 0;
#endif
}

static int validate_cmd(int cmd)
{
	int ret = do_validate_cmd(cmd);

/* 	printk("validate_cmd( %x ): %d\n", cmd, ret); */

	return ret;
}

static int i915_emit_cmds(drm_device_t * dev, int __user * buffer, int dwords)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;
	RING_LOCALS;

	if ((dwords+1) * sizeof(int) >= dev_priv->ring.Size - 8) {
		DRM_ERROR(" emit cmds invalid arg");
		return (EINVAL);
	}
	BEGIN_LP_RING((dwords+1)&~1);

	for (i = 0; i < dwords;) {
		int cmd, sz;

		if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i], sizeof(cmd))) {
			DRM_ERROR("emit cmds failed to get cmd from user");
			return (EINVAL);
		}

		if ((sz = validate_cmd(cmd)) == 0 || i + sz > dwords) {
			DRM_ERROR("emit cmds invalid");
			return (EINVAL);
		}
		OUT_RING(cmd);

		while (++i, --sz) {
			if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i],
							 sizeof(cmd))) {
				DRM_ERROR("emit cmds failed get cmds");
				return (EINVAL);
			}
			OUT_RING(cmd);
		}
	}

	if (dwords & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();
		
	return 0;
}

int i915_emit_box(drm_device_t * dev,
			 drm_clip_rect_t __user * boxes,
			 int i, int DR1, int DR4)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t box;
	RING_LOCALS;

	if (DRM_COPY_FROM_USER_UNCHECKED(&box, &boxes[i], sizeof(box))) {
		DRM_ERROR("emit box failed to copy from user");
		return (EFAULT);
	}

	if (box.y2 <= box.y1 || box.x2 <= box.x1) {
		DRM_ERROR("Bad box %d,%d..%d,%d\n",
			  box.x1, box.y1, box.x2, box.y2);
		return (EINVAL);
	}

	if (IS_I965G(dev)) {
		BEGIN_LP_RING(4);
		OUT_RING(GFX_OP_DRAWRECT_INFO_I965);
		OUT_RING((box.x1 & 0xffff) | (box.y1 << 16));
		OUT_RING(((box.x2 - 1) & 0xffff) | ((box.y2 - 1) << 16));
		OUT_RING(DR4);
		ADVANCE_LP_RING();
	} else {
		BEGIN_LP_RING(6);
		OUT_RING(GFX_OP_DRAWRECT_INFO);
		OUT_RING(DR1);
		OUT_RING((box.x1 & 0xffff) | (box.y1 << 16));
		OUT_RING(((box.x2 - 1) & 0xffff) | ((box.y2 - 1) << 16));
		OUT_RING(DR4);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}

	return 0;
}

/* XXX: Emitting the counter should really be moved to part of the IRQ
 * emit.  For now, do it in both places:
 */

void i915_emit_breadcrumb(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	dev_priv->counter++;
	if (dev_priv->counter > 0x7FFFFFFFUL)
		dev_priv->counter = 0;
	if (dev_priv->sarea_priv)
		dev_priv->sarea_priv->last_enqueue = dev_priv->counter;


	BEGIN_LP_RING(4);
	OUT_RING(MI_STORE_DWORD_INDEX);
	OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_LP_RING();

}

static int i915_dispatch_cmdbuffer(drm_device_t * dev,
				   drm_i915_cmdbuffer_t * cmd)
{
	int nbox = cmd->num_cliprects;
	int i = 0, count, ret;

	if (cmd->sz & 0x3) {
		DRM_ERROR("alignment");
		return (EINVAL);
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, cmd->cliprects, i,
					    cmd->DR1, cmd->DR4);
			if (ret)
				return ret;
		}

		ret = i915_emit_cmds(dev, (int __user *)(void *)cmd->buf, cmd->sz / 4);
		if (ret)
			return ret;
	}

	i915_emit_breadcrumb( dev );
	return 0;
}

static int i915_dispatch_batchbuffer(drm_device_t * dev,
				     drm_i915_batchbuffer_t * batch)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t __user *boxes = batch->cliprects;
	int nbox = batch->num_cliprects;
	int i = 0, count;
	RING_LOCALS;

	if ((batch->start | batch->used) & 0x7) {
		DRM_ERROR("alignment");
		return (EINVAL);
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			int ret = i915_emit_box(dev, boxes, i,
						batch->DR1, batch->DR4);
			if (ret)
				return ret;
		}

		if (IS_I830(dev) || IS_845G(dev)) {
			BEGIN_LP_RING(4);
			OUT_RING(MI_BATCH_BUFFER);
			OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			OUT_RING(batch->start + batch->used - 4);
			OUT_RING(0);
			ADVANCE_LP_RING();
		} else {
			BEGIN_LP_RING(2);
			if (IS_I965G(dev)) {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6) | MI_BATCH_NON_SECURE_I965);
				OUT_RING(batch->start);
			} else {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
				OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			}
			ADVANCE_LP_RING();
		}
	}

	i915_emit_breadcrumb( dev );

	return 0;
}

static int i915_dispatch_flip(struct drm_device * dev, int planes)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	if (!dev_priv->sarea_priv)
		return -EINVAL;

	DRM_DEBUG("planes=0x%x pfCurrentPage=%d\n",
		planes, dev_priv->sarea_priv->pf_current_page);

	i915_kernel_lost_context(dev);

	BEGIN_LP_RING(2);
	OUT_RING(MI_FLUSH | MI_READ_FLUSH);
	OUT_RING(0);
	ADVANCE_LP_RING();

	BEGIN_LP_RING(6);
	OUT_RING(CMD_OP_DISPLAYBUFFER_INFO | ASYNC_FLIP);
	OUT_RING(0);
	if (dev_priv->current_page == 0) {
		OUT_RING(dev_priv->back_offset);
		dev_priv->current_page = 1;
	} else {
		OUT_RING(dev_priv->front_offset);
		dev_priv->current_page = 0;
	}
	OUT_RING(0);
	ADVANCE_LP_RING();

	BEGIN_LP_RING(2);
	OUT_RING(MI_WAIT_FOR_EVENT | MI_WAIT_FOR_PLANE_A_FLIP);
	OUT_RING(0);
	ADVANCE_LP_RING();

	dev_priv->sarea_priv->last_enqueue = dev_priv->counter++;

	BEGIN_LP_RING(4);
	OUT_RING(MI_STORE_DWORD_INDEX);
	OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_LP_RING();

	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;
	return 0;
}

static int i915_quiescent(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;
	i915_kernel_lost_context(dev);
	ret = i915_wait_ring(dev, dev_priv->ring.Size - 8, __FUNCTION__);

	if (ret)
	{
		i915_kernel_lost_context (dev);
		DRM_ERROR ("not quiescent head %08x tail %08x space %08x\n",
			   dev_priv->ring.head,
			   dev_priv->ring.tail,
			   dev_priv->ring.space);
	}
	return ret;
}

/*ARGSUSED*/
static int i915_flush_ioctl(DRM_IOCTL_ARGS)
{
	int ret;
	DRM_DEVICE;

	LOCK_TEST_WITH_RETURN(dev, fpriv);

	spin_lock(&dev->struct_mutex);
	ret = i915_quiescent(dev);
	spin_unlock(&dev->struct_mutex);

	return ret;
}

/*ARGSUSED*/
static int i915_batchbuffer(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i915_batchbuffer_t batch;
	int ret;

	if (!dev_priv->allow_batchbuffer) {
		DRM_ERROR("Batchbuffer ioctl disabled\n");
		return (EINVAL);
	}

	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		drm_i915_batchbuffer32_t batchbuffer32_t;

		DRM_COPYFROM_WITH_RETURN(&batchbuffer32_t,
			(void *) data, sizeof (batchbuffer32_t));

		batch.start = batchbuffer32_t.start;
		batch.used = batchbuffer32_t.used;
		batch.DR1 = batchbuffer32_t.DR1;
		batch.DR4 = batchbuffer32_t.DR4;
		batch.num_cliprects = batchbuffer32_t.num_cliprects;
		batch.cliprects = (drm_clip_rect_t __user *)
			(uintptr_t)batchbuffer32_t.cliprects;
	} else
		DRM_COPYFROM_WITH_RETURN(&batch, (void *) data,
			sizeof(batch));

	DRM_DEBUG("i915 batchbuffer, start %x used %d cliprects %d, counter %d\n",
		  batch.start, batch.used, batch.num_cliprects, dev_priv->counter);

	LOCK_TEST_WITH_RETURN(dev, fpriv);
	
/*
	if (batch.num_cliprects && DRM_VERIFYAREA_READ(batch.cliprects,
						       batch.num_cliprects *
						       sizeof(drm_clip_rect_t)))
		return (EFAULT);
		
*/

	spin_lock(&dev->struct_mutex);
	ret = i915_dispatch_batchbuffer(dev, &batch);
	spin_unlock(&dev->struct_mutex);
	sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

	return ret;
}

/*ARGSUSED*/
static int i915_cmdbuffer(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i915_cmdbuffer_t cmdbuf;
	int ret;

	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		drm_i915_cmdbuffer32_t cmdbuffer32_t;

		DRM_COPYFROM_WITH_RETURN(&cmdbuffer32_t,
			(drm_i915_cmdbuffer32_t __user *) data,
			sizeof (drm_i915_cmdbuffer32_t));

		cmdbuf.buf = (char __user *)(uintptr_t)cmdbuffer32_t.buf;
		cmdbuf.sz = cmdbuffer32_t.sz;
		cmdbuf.DR1 = cmdbuffer32_t.DR1;
		cmdbuf.DR4 = cmdbuffer32_t.DR4;
		cmdbuf.num_cliprects = cmdbuffer32_t.num_cliprects;
		cmdbuf.cliprects = (drm_clip_rect_t __user *)
			(uintptr_t)cmdbuffer32_t.cliprects;
	} else
		DRM_COPYFROM_WITH_RETURN(&cmdbuf, (void *) data,
			sizeof(cmdbuf));

	DRM_DEBUG("i915 cmdbuffer, buf %p sz %d cliprects %d\n",
		  cmdbuf.buf, cmdbuf.sz, cmdbuf.num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, fpriv);
	
/*
	if (cmdbuf.num_cliprects &&
	    DRM_VERIFYAREA_READ(cmdbuf.cliprects,
				cmdbuf.num_cliprects *
				sizeof(drm_clip_rect_t))) {
		DRM_ERROR("Fault accessing cliprects\n");
		return (EFAULT);
	}
*/	

	spin_lock(&dev->struct_mutex);
	ret = i915_dispatch_cmdbuffer(dev, &cmdbuf);
	spin_unlock(&dev->struct_mutex);
	if (ret) {
		DRM_ERROR("i915_dispatch_cmdbuffer failed\n");
		return ret;
	}

	sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
	return 0;
}

/*ARGSUSED*/
static int i915_flip_bufs(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_flip_t param;
	int ret;
        DRM_COPYFROM_WITH_RETURN(&param, (drm_i915_flip_t *) data,
                                 sizeof(param));

	DRM_DEBUG("i915_flip_bufs\n");

	LOCK_TEST_WITH_RETURN(dev, fpriv);

	spin_lock(&dev->struct_mutex);
	ret = i915_dispatch_flip(dev, param.pipes);
	spin_unlock(&dev->struct_mutex);
	return ret; 
}

/*ARGSUSED*/
static int i915_getparam(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_getparam_t param;
	int value;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return (EINVAL);
	}

	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		drm_i915_getparam32_t getparam32_t;

		DRM_COPYFROM_WITH_RETURN(&getparam32_t,
			(drm_i915_getparam32_t __user *) data,
			sizeof (drm_i915_getparam32_t));

		param.param = getparam32_t.param;
		param.value = (int __user *)(uintptr_t)getparam32_t.value;
	} else
		DRM_COPYFROM_WITH_RETURN(&param,
		    (drm_i915_getparam_t *) data, sizeof(param));

	switch (param.param) {
	case I915_PARAM_IRQ_ACTIVE:
		value = dev->irq_enabled ? 1 : 0;
		break;
	case I915_PARAM_ALLOW_BATCHBUFFER:
		value = dev_priv->allow_batchbuffer ? 1 : 0;
		break;
	case I915_PARAM_LAST_DISPATCH:
		value = READ_BREADCRUMB(dev_priv);
		break;
	case I915_PARAM_CHIPSET_ID:
		value = dev->pci_device;
		break;
	case I915_PARAM_HAS_GEM:
		value = dev->driver->use_gem;
		break;
	default:
		DRM_ERROR("Unknown get parameter %d\n", param.param);
		return (EINVAL);
	}

	if (DRM_COPY_TO_USER(param.value, &value, sizeof(int))) {
		DRM_ERROR("i915_getparam failed\n");
		return (EFAULT);
	}
	return 0;
}

/*ARGSUSED*/
static int i915_setparam(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_setparam_t param;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return (EINVAL);
	}

	DRM_COPYFROM_WITH_RETURN(&param, (drm_i915_setparam_t *) data,
				 sizeof(param));

	switch (param.param) {
	case I915_SETPARAM_USE_MI_BATCHBUFFER_START:
		break;
	case I915_SETPARAM_TEX_LRU_LOG_GRANULARITY:
		dev_priv->tex_lru_log_granularity = param.value;
		break;
	case I915_SETPARAM_ALLOW_BATCHBUFFER:
		dev_priv->allow_batchbuffer = param.value;
		break;
	default:
		DRM_ERROR("unknown set parameter %d\n", param.param);
		return (EINVAL);
	}

	return 0;
}

/*ARGSUSED*/
static int i915_set_status_page(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_hws_addr_t hws;

	if (!I915_NEED_GFX_HWS(dev))
		return (EINVAL);

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return (EINVAL);
	}
	DRM_COPYFROM_WITH_RETURN(&hws, (drm_i915_hws_addr_t __user *) data,
			sizeof(hws));
DRM_ERROR("i915_set_status_page set status page addr 0x%08x\n", (u32)hws.addr);

	dev_priv->status_gfx_addr = hws.addr & (0x1ffff<<12);
	DRM_DEBUG("set gfx_addr 0x%08x\n", dev_priv->status_gfx_addr);

	dev_priv->hws_map.offset =
	    (u_offset_t)dev->agp->agp_info.agpi_aperbase + hws.addr;
	dev_priv->hws_map.size = 4 * 1024; /* 4K pages */
	dev_priv->hws_map.type = 0;
	dev_priv->hws_map.flags = 0;
	dev_priv->hws_map.mtrr = 0;

	DRM_DEBUG("set status page: i915_set_status_page: mapoffset 0x%llx\n",
	    dev_priv->hws_map.offset);
	drm_core_ioremap(&dev_priv->hws_map, dev);
	if (dev_priv->hws_map.handle == NULL) {
		dev->dev_private = (void *)dev_priv;
		(void) i915_dma_cleanup(dev);
		dev_priv->status_gfx_addr = 0;
		DRM_ERROR("can not ioremap virtual address for"
				" G33 hw status page\n");
		return (ENOMEM);
	}
	dev_priv->hw_status_page = dev_priv->hws_map.dev_addr;

	(void) memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
	I915_WRITE(HWS_PGA, dev_priv->status_gfx_addr);
	DRM_DEBUG("load hws 0x2080 with gfx mem 0x%x\n",
			dev_priv->status_gfx_addr);
	DRM_DEBUG("load hws at %p\n", dev_priv->hw_status_page);
	return 0;
}

/*ARGSUSED*/
int i915_driver_load(drm_device_t *dev, unsigned long flags)
{
	struct drm_i915_private *dev_priv;
	unsigned long base, size;
	int ret = 0, mmio_bar = IS_I9XX(dev) ? 0 : 1;

	/* i915 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	dev_priv = drm_alloc(sizeof(drm_i915_private_t), DRM_MEM_DRIVER);
	if (dev_priv == NULL)
		return ENOMEM;
		
	(void) memset(dev_priv, 0, sizeof(drm_i915_private_t));
	dev->dev_private = (void *)dev_priv;
	dev_priv->dev = dev;

	/* Add register map (needed for suspend/resume) */

	base = drm_get_resource_start(dev, mmio_bar);
	size = drm_get_resource_len(dev, mmio_bar);
	dev_priv->mmio_map = drm_alloc(sizeof (drm_local_map_t), DRM_MEM_MAPS);
	dev_priv->mmio_map->offset = base;
	dev_priv->mmio_map->size = size;
	dev_priv->mmio_map->type = _DRM_REGISTERS;
	dev_priv->mmio_map->flags = _DRM_REMOVABLE;
	(void) drm_ioremap(dev, dev_priv->mmio_map);

	DRM_DEBUG("i915_driverload mmio %p mmio_map->dev_addr %x", dev_priv->mmio_map, dev_priv->mmio_map->dev_addr);

#if defined(__i386)
	dev->driver->use_gem = 0;
#else
	if (IS_I965G(dev)) {
		dev->driver->use_gem = 1;
	} else {
		dev->driver->use_gem = 0;
	}
#endif	/* __i386 */

	dev->driver->get_vblank_counter = i915_get_vblank_counter;
	dev->max_vblank_count = 0xffffff; /* only 24 bits of frame count */
#if defined(__i386)
	if (IS_G4X(dev) || IS_IGDNG(dev) || IS_GM45(dev))
#else
	if (IS_G4X(dev) || IS_IGDNG(dev))
#endif
	{
		dev->max_vblank_count = 0xffffffff; /* full 32 bit counter */
		dev->driver->get_vblank_counter = gm45_get_vblank_counter;
	}


#ifdef I915_HAVE_GEM
	i915_gem_load(dev);
#endif

	if (!I915_NEED_GFX_HWS(dev)) {
		ret = i915_init_hardware_status(dev);
		if(ret)
			return ret;
	}

	mutex_init(&dev_priv->user_irq_lock, "userirq", MUTEX_DRIVER, NULL);
	mutex_init(&dev_priv->error_lock, "error_lock", MUTEX_DRIVER, NULL);

	ret = drm_vblank_init(dev, I915_NUM_PIPE);
	if (ret) {
		(void) i915_driver_unload(dev);
		return ret;
	}

	return ret;
}

int i915_driver_unload(struct drm_device *dev)
{
       drm_i915_private_t *dev_priv = dev->dev_private;

       i915_free_hardware_status(dev);

	drm_rmmap(dev, dev_priv->mmio_map);

        mutex_destroy(&dev_priv->user_irq_lock);

	drm_free(dev->dev_private, sizeof(drm_i915_private_t),
	    DRM_MEM_DRIVER);
	dev->dev_private = NULL;

	return 0;
}

/*ARGSUSED*/
int i915_driver_open(drm_device_t * dev, struct drm_file *file_priv)
{
        struct drm_i915_file_private *i915_file_priv;

        DRM_DEBUG("\n");
        i915_file_priv = (struct drm_i915_file_private *)
            drm_alloc(sizeof(*i915_file_priv), DRM_MEM_FILES);

        if (!i915_file_priv)
                return -ENOMEM;

        file_priv->driver_priv = i915_file_priv;

        i915_file_priv->mm.last_gem_seqno = 0;
        i915_file_priv->mm.last_gem_throttle_seqno = 0;

        return 0;
}

void i915_driver_lastclose(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	/* agp off can use this to get called before dev_priv */
	if (!dev_priv)
		return;

#ifdef I915_HAVE_GEM
	i915_gem_lastclose(dev);
#endif

	DRM_GETSAREA();
	if (dev_priv->agp_heap)
		i915_mem_takedown(&(dev_priv->agp_heap));
	(void) i915_dma_cleanup(dev);
}

void i915_driver_preclose(drm_device_t * dev, drm_file_t *fpriv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	i915_mem_release(dev, fpriv, dev_priv->agp_heap);
}

/*ARGSUSED*/
void i915_driver_postclose(drm_device_t * dev, struct drm_file *file_priv)
{
	struct drm_i915_file_private *i915_file_priv = file_priv->driver_priv;

	drm_free(i915_file_priv, sizeof(*i915_file_priv), DRM_MEM_FILES);
}

drm_ioctl_desc_t i915_ioctls[] = {
	[DRM_IOCTL_NR(DRM_I915_INIT)] =
	    {i915_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_FLUSH)] =
	    {i915_flush_ioctl, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_FLIP)] =
	    {i915_flip_bufs, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_BATCHBUFFER)] =
	    {i915_batchbuffer, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_IRQ_EMIT)] =
	    {i915_irq_emit, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_IRQ_WAIT)] =
	    {i915_irq_wait, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_GETPARAM)] =
	    {i915_getparam, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_SETPARAM)] =
	    {i915_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_ALLOC)] =
	    {i915_mem_alloc, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_FREE)] =
	    {i915_mem_free, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_INIT_HEAP)] =
	    {i915_mem_init_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_CMDBUFFER)] =
	    {i915_cmdbuffer, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_DESTROY_HEAP)] =
	    {i915_mem_destroy_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_SET_VBLANK_PIPE)] =
	    {i915_vblank_pipe_set, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
	[DRM_IOCTL_NR(DRM_I915_GET_VBLANK_PIPE)] =
	    {i915_vblank_pipe_get, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_VBLANK_SWAP)] =
	    {i915_vblank_swap, DRM_AUTH},
	[DRM_IOCTL_NR(DRM_I915_HWS_ADDR)] =
	    {i915_set_status_page, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY},
#ifdef I915_HAVE_GEM
        [DRM_IOCTL_NR(DRM_I915_GEM_INIT)] =
            {i915_gem_init_ioctl, DRM_AUTH},
        [DRM_IOCTL_NR(DRM_I915_GEM_EXECBUFFER)] =
            {i915_gem_execbuffer, DRM_AUTH},
        [DRM_IOCTL_NR(DRM_I915_GEM_PIN)] =
            {i915_gem_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY},
        [DRM_IOCTL_NR(DRM_I915_GEM_UNPIN)] =
            {i915_gem_unpin_ioctl, DRM_AUTH|DRM_ROOT_ONLY},
        [DRM_IOCTL_NR(DRM_I915_GEM_BUSY)] =
            {i915_gem_busy_ioctl, DRM_AUTH},
        [DRM_IOCTL_NR(DRM_I915_GEM_THROTTLE)] =
            {i915_gem_throttle_ioctl, DRM_AUTH},
        [DRM_IOCTL_NR(DRM_I915_GEM_ENTERVT)] =
            {i915_gem_entervt_ioctl, DRM_AUTH},
        [DRM_IOCTL_NR(DRM_I915_GEM_LEAVEVT)] =
            {i915_gem_leavevt_ioctl, DRM_AUTH},
        [DRM_IOCTL_NR(DRM_I915_GEM_CREATE)] =
            {i915_gem_create_ioctl, 0},
        [DRM_IOCTL_NR(DRM_I915_GEM_PREAD)] =
            {i915_gem_pread_ioctl, 0},
        [DRM_IOCTL_NR(DRM_I915_GEM_PWRITE)] =
            {i915_gem_pwrite_ioctl, 0},
        [DRM_IOCTL_NR(DRM_I915_GEM_MMAP)] =
            {i915_gem_mmap_ioctl, 0},
        [DRM_IOCTL_NR(DRM_I915_GEM_SET_DOMAIN)] =
            {i915_gem_set_domain_ioctl, 0},
        [DRM_IOCTL_NR(DRM_I915_GEM_SW_FINISH)] =
            {i915_gem_sw_finish_ioctl, 0},
        [DRM_IOCTL_NR(DRM_I915_GEM_SET_TILING)] =
            {i915_gem_set_tiling, 0},
        [DRM_IOCTL_NR(DRM_I915_GEM_GET_TILING)] =
            {i915_gem_get_tiling, 0},
        [DRM_IOCTL_NR(DRM_I915_GEM_GET_APERTURE)] =
            {i915_gem_get_aperture_ioctl, 0},
#endif
};

int i915_max_ioctl = DRM_ARRAY_SIZE(i915_ioctls);

/**
 * Determine if the device really is AGP or not.
 *
 * All Intel graphics chipsets are treated as AGP, even if they are really
 * PCI-e.
 *
 * \param dev   The device to be tested.
 *
 * \returns
 * A value of 1 is always retured to indictate every i9x5 is AGP.
 */
/*ARGSUSED*/
int i915_driver_device_is_agp(drm_device_t * dev)
{
	return 1;
}

