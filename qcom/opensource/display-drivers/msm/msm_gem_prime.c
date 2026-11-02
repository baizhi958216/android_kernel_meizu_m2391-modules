/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_mmu.h"
#include "msm_kms.h"

#include <drm/drm_drv.h>

#include <linux/dma-buf.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
#include <linux/ion.h>
#include <linux/msm_ion.h>
#endif

struct sg_table *msm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	int npages = obj->size >> PAGE_SHIFT;

	if (WARN_ON(!msm_obj->pages))  /* should have already pinned! */
		return NULL;

	return drm_prime_pages_to_sg(obj->dev, msm_obj->pages, npages);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
int msm_gem_prime_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	void *addr;

	if (obj->import_attach)
		return dma_buf_vmap(obj->import_attach->dmabuf, map);

	addr = msm_gem_get_vaddr(obj);

	if (IS_ERR_OR_NULL(addr))
		return addr ? PTR_ERR(addr) : -ENOMEM;
	iosys_map_set_vaddr(map, addr);
	return 0;
}
#else
void *msm_gem_prime_vmap(struct drm_gem_object *obj)
{
	return msm_gem_get_vaddr(obj);
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
void msm_gem_prime_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
#else
void msm_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
#endif
{
	if (obj->import_attach)
		dma_buf_vunmap(obj->import_attach->dmabuf, map);
	else
		msm_gem_put_vaddr(obj);
}

struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		struct dma_buf_attachment *attach, struct sg_table *sg)
{
	return msm_gem_import(dev, attach->dmabuf, sg);
}

int msm_gem_prime_pin(struct drm_gem_object *obj)
{
	if (!obj->import_attach)
		return PTR_ERR_OR_ZERO(msm_gem_get_pages(obj));
	return 0;
}

void msm_gem_prime_unpin(struct drm_gem_object *obj)
{
	if (!obj->import_attach)
		msm_gem_put_pages(obj);
}

struct drm_gem_object *msm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct drm_gem_object *obj;

	if (drm_gem_is_prime_exported_dma_buf(dev, dma_buf)) {
		obj = dma_buf->priv;
		drm_gem_object_get(obj);
		return obj;
	}

	/* Keep the import alive without mapping it into the nonsecure bank.
	 * Each scanout VMA attaches to the context bank chosen by its plane.
	 */
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);
	obj = msm_gem_import(dev, dma_buf, NULL);
	if (IS_ERR(obj)) {
		dma_buf_detach(dma_buf, attach);
		return obj;
	}
	get_dma_buf(dma_buf);
	obj->import_attach = attach;
	obj->resv = dma_buf->resv;
	return obj;
}
