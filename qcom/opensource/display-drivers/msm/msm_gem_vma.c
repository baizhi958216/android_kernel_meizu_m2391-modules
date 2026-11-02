/*
 * Copyright (c) 2018-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016 Red Hat
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

/* SDE address space operations */
static void smmu_aspace_unmap_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, struct sg_table *sgt,
		unsigned int flags)
{
	if (!vma->iova)
		return;

	if (aspace) {
		aspace->mmu->funcs->unmap_dma_buf(aspace->mmu, sgt,
				DMA_BIDIRECTIONAL, flags);
	}

	vma->iova = 0;
}

static int smmu_aspace_map_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, struct sg_table *sgt,
		int npages, unsigned int flags)
{
	int ret = -EINVAL;

	if (!aspace || !aspace->domain_attached)
		return ret;

	ret = aspace->mmu->funcs->map_dma_buf(aspace->mmu, sgt,
			DMA_BIDIRECTIONAL, flags);
	if (ret)
		return ret;

	{
		struct scatterlist *sg;
		dma_addr_t start = sg_dma_address(sgt->sgl), end = start;
		int i;

		for_each_sgtable_dma_sg(sgt, sg, i) {
			if (end != sg_dma_address(sg))
				break;
			end += sg_dma_len(sg);
		}
		if (!start || end - start < ((u64)npages << PAGE_SHIFT) || end - 1 > U32_MAX) {
			aspace->mmu->funcs->unmap_dma_buf(aspace->mmu, sgt, DMA_BIDIRECTIONAL, flags);
			return -ERANGE;
		}
		vma->iova = start;
	}


	return ret;
}

static void smmu_aspace_destroy(struct msm_gem_address_space *aspace)
{
	if (aspace->mmu)
		aspace->mmu->funcs->destroy(aspace->mmu);
}

static int smmu_aspace_register_cb(
		struct msm_gem_address_space *aspace,
		void (*cb)(void *, bool),
		void *cb_data)
{
	struct aspace_client *aclient = NULL;
	struct aspace_client *temp;

	if (!aspace)
		return -EINVAL;


	aclient = kzalloc(sizeof(*aclient), GFP_KERNEL);
	if (!aclient)
		return -ENOMEM;

	aclient->cb = cb;
	aclient->cb_data = cb_data;
	INIT_LIST_HEAD(&aclient->list);

	/* Serialize registration with domain transitions and client callbacks. */
	mutex_lock(&aspace->list_lock);
	if (!aspace->domain_attached) {
		mutex_unlock(&aspace->list_lock);
		kfree(aclient);
		return -EACCES;
	}
	list_for_each_entry(temp, &aspace->clients, list) {
		if ((temp->cb == aclient->cb) &&
			(temp->cb_data == aclient->cb_data)) {
			kfree(aclient);
			mutex_unlock(&aspace->list_lock);
			return -EEXIST;
		}
	}

	list_move_tail(&aclient->list, &aspace->clients);
	mutex_unlock(&aspace->list_lock);

	return 0;
}

static int smmu_aspace_unregister_cb(
		struct msm_gem_address_space *aspace,
		void (*cb)(void *, bool),
		void *cb_data)
{
	struct aspace_client *aclient = NULL;
	int rc = -ENOENT;

	if (!aspace || !cb)
		return -EINVAL;

	mutex_lock(&aspace->list_lock);
	list_for_each_entry(aclient, &aspace->clients, list) {
		if ((aclient->cb == cb) &&
			(aclient->cb_data == cb_data)) {
			list_del(&aclient->list);
			kfree(aclient);
			rc = 0;
			break;
		}
	}
	mutex_unlock(&aspace->list_lock);

	return rc;
}

static const struct msm_gem_aspace_ops smmu_aspace_ops = {
	.map = smmu_aspace_map_vma,
	.unmap = smmu_aspace_unmap_vma,
	.destroy = smmu_aspace_destroy,
	.register_cb = smmu_aspace_register_cb,
	.unregister_cb = smmu_aspace_unregister_cb,
};

struct msm_gem_address_space *
msm_gem_smmu_address_space_create(struct drm_device *dev, struct msm_mmu *mmu,
		const char *name)
{
	struct msm_gem_address_space *aspace;

	if (!mmu)
		return ERR_PTR(-EINVAL);

	aspace = kzalloc(sizeof(*aspace), GFP_KERNEL);
	if (!aspace)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&aspace->lock);
	aspace->dev = dev;
	aspace->name = name;
	aspace->mmu = mmu;
	aspace->ops = &smmu_aspace_ops;
	INIT_LIST_HEAD(&aspace->active_list);
	INIT_LIST_HEAD(&aspace->clients);
	kref_init(&aspace->kref);
	mutex_init(&aspace->list_lock);

	return aspace;
}

static void
msm_gem_address_space_destroy(struct kref *kref)
{
	struct msm_gem_address_space *aspace = container_of(kref,
			struct msm_gem_address_space, kref);

	aspace->ops->destroy(aspace);
	kfree(aspace);
}


void msm_gem_address_space_put(struct msm_gem_address_space *aspace)
{
	if (aspace)
		kref_put(&aspace->kref, msm_gem_address_space_destroy);
}

/* GPU address space operations */
static void iommu_aspace_unmap_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, struct sg_table *sgt,
		unsigned int flags)
{
	if (!aspace || !vma->iova)
		return;

	if (aspace->mmu) {
		unsigned size = vma->node.size << PAGE_SHIFT;
		aspace->mmu->funcs->unmap(aspace->mmu, vma->iova, sgt, size);
	}

	spin_lock(&aspace->lock);
	drm_mm_remove_node(&vma->node);
	spin_unlock(&aspace->lock);

	vma->iova = 0;

}

void msm_gem_unmap_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, struct sg_table *sgt,
		unsigned int flags)
{
	if (aspace && aspace->ops->unmap)
		aspace->ops->unmap(aspace, vma, sgt, flags);
}


static int iommu_aspace_map_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, struct sg_table *sgt,
		int npages, unsigned int flags)
{
	int ret;

	spin_lock(&aspace->lock);
	if (WARN_ON(drm_mm_node_allocated(&vma->node))) {
		spin_unlock(&aspace->lock);
		return 0;
	}

	ret = drm_mm_insert_node(&aspace->mm, &vma->node, npages);
	spin_unlock(&aspace->lock);

	if (ret)
		return ret;

	vma->iova = vma->node.start << PAGE_SHIFT;

	if (aspace->mmu) {
		unsigned size = npages << PAGE_SHIFT;
		ret = aspace->mmu->funcs->map(aspace->mmu, vma->iova, sgt,
				size, IOMMU_READ | IOMMU_WRITE);
		if (ret) {
			spin_lock(&aspace->lock);
			drm_mm_remove_node(&vma->node);
			spin_unlock(&aspace->lock);
			vma->iova = 0;
		}
	}


	return ret;
}

static void iommu_aspace_destroy(struct msm_gem_address_space *aspace)
{
	drm_mm_takedown(&aspace->mm);
	if (aspace->mmu)
		aspace->mmu->funcs->destroy(aspace->mmu);
}

static const struct msm_gem_aspace_ops msm_iommu_aspace_ops = {
	.map = iommu_aspace_map_vma,
	.unmap = iommu_aspace_unmap_vma,
	.destroy = iommu_aspace_destroy,
};

int
msm_gem_map_vma(struct msm_gem_address_space *aspace,
		struct msm_gem_vma *vma, struct sg_table *sgt, int npages,
		unsigned int flags)
{
	if (aspace && aspace->ops->map)
		return aspace->ops->map(aspace, vma, sgt, npages, flags);

	return -EINVAL;
}

struct device *msm_gem_get_aspace_device(struct msm_gem_address_space *aspace)
{
	struct device *client_dev = NULL;

	if (aspace && aspace->mmu && aspace->mmu->funcs->get_dev)
		client_dev = aspace->mmu->funcs->get_dev(aspace->mmu);

	return client_dev;
}

int msm_gem_address_space_register_cb(struct msm_gem_address_space *aspace,
		void (*cb)(void *, bool),
		void *cb_data)
{
	if (aspace && aspace->ops && aspace->ops->register_cb)
		return aspace->ops->register_cb(aspace, cb, cb_data);

	return -EINVAL;
}

int msm_gem_address_space_unregister_cb(struct msm_gem_address_space *aspace,
		void (*cb)(void *, bool),
		void *cb_data)
{
	if (aspace && aspace->ops && aspace->ops->unregister_cb)
		return aspace->ops->unregister_cb(aspace, cb, cb_data);

	return -EINVAL;
}

