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
	void *addr = msm_gem_get_vaddr(obj);

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
	msm_gem_put_vaddr(obj);
}

int msm_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	return msm_gem_mmap_obj(vma->vm_private_data, vma);
}

struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		struct dma_buf_attachment *attach, struct sg_table *sg)
{
	return msm_gem_import(dev, attach->dmabuf, sg);
}

int msm_gem_prime_pin(struct drm_gem_object *obj)
{
	if (!obj->import_attach)
		msm_gem_get_pages(obj);
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
	struct msm_drm_private *priv = dev->dev_private;
	struct device *attach_dev = dev->dev;
	struct msm_kms *kms;

	if (!dma_buf || !priv)
		return ERR_PTR(-EINVAL);

	kms = priv->kms;
	if (!kms || !kms->funcs->get_address_space_device)
		return ERR_PTR(-ENODEV);

	/*
	 * A secure transition can temporarily take the nonsecure context bank
	 * offline. The GEM mapping path attaches to the requested SDE domain
	 * before supplying an IOVA to the display engine.
	 */
	if (priv->has_iommu) {
		struct device *cb;

		cb = kms->funcs->get_address_space_device(kms, MSM_SMMU_DOMAIN_UNSECURE);
		if (cb)
			attach_dev = cb;
	}

	return drm_gem_prime_import_dev(dev, dma_buf, attach_dev);
}
