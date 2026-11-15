// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qti (or) Qualcomm Technologies Inc CE device driver.
 *
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-mapping.h>
#include <linux/list.h>
#include "linux/qcedev.h"
#include "qcedevi.h"
#include "qcedev_smmu.h"
#include "soc/qcom/secure_buffer.h"
#include <linux/mem-buf.h>

static int qcedev_setup_context_bank(struct context_bank_info *cb,
				struct device *dev)
{
	if (!dev || !cb) {
		pr_err("%s err: invalid input params\n", __func__);
		return -EINVAL;
	}
	cb->dev = dev;

	if (!dev->dma_parms) {
		dev->dma_parms = devm_kzalloc(dev,
				sizeof(*dev->dma_parms), GFP_KERNEL);
		if (!dev->dma_parms)
			return -ENOMEM;
	}
	dma_set_max_seg_size(dev, DMA_BIT_MASK(32));
	dma_set_seg_boundary(dev, (unsigned long)DMA_BIT_MASK(64));

	return 0;
}

int qcedev_parse_context_bank(struct platform_device *pdev)
{
	struct qcedev_control *podev;
	struct context_bank_info *cb = NULL;
	struct device_node *np = NULL;
	int rc = 0;

	if (!pdev) {
		pr_err("%s err: invalid platform devices\n", __func__);
		return -EINVAL;
	}
	if (!pdev->dev.parent) {
		pr_err("%s err: failed to find a parent for %s\n",
			__func__, dev_name(&pdev->dev));
		return -EINVAL;
	}

	podev = dev_get_drvdata(pdev->dev.parent);
	if (!podev)
		return -EPROBE_DEFER;
	np = pdev->dev.of_node;
	cb = devm_kzalloc(&pdev->dev, sizeof(*cb), GFP_KERNEL);
	if (!cb) {
		pr_err("%s ERROR = Failed to allocate cb\n", __func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&cb->list);
	

	rc = of_property_read_string(np, "label", &cb->name);
	if (rc)
		pr_debug("%s ERROR = Unable to read label\n", __func__);

	cb->is_secure = of_property_read_bool(np, "qcom,secure-context-bank");

	rc = qcedev_setup_context_bank(cb, &pdev->dev);
	if (rc) {
		pr_err("%s err: cannot setup context bank %d\n", __func__, rc);
		goto err_setup_cb;
	}

	list_add_tail(&cb->list, &podev->context_banks);
	platform_set_drvdata(pdev, cb);
	return 0;

err_setup_cb:
	devm_kfree(&pdev->dev, cb);
	return rc;
}

struct qcedev_mem_client *qcedev_mem_new_client(enum qcedev_mem_type mtype)
{
	struct qcedev_mem_client *mem_client = NULL;

	if (mtype != MEM_ION) {
		pr_err("%s: err: Mem type not supported\n", __func__);
		goto err;
	}

	mem_client = kzalloc(sizeof(*mem_client), GFP_KERNEL);
	if (!mem_client)
		goto err;
	mem_client->mtype = mtype;

	return mem_client;
err:
	return NULL;
}

void qcedev_mem_delete_client(struct qcedev_mem_client *mem_client)
{
	kfree(mem_client);
}

static bool is_iommu_present(struct qcedev_handle *qce_hndl)
{
	return !list_empty(&qce_hndl->cntl->context_banks);
}

static struct context_bank_info *get_context_bank(
		struct qcedev_handle *qce_hndl, bool is_secure)
{
	struct qcedev_control *podev = qce_hndl->cntl;
	struct context_bank_info *cb = NULL, *match = NULL;

	list_for_each_entry(cb, &podev->context_banks, list) {
		if (cb->is_secure == is_secure) {
			match = cb;
			break;
		}
	}
	return match;
}

static int ion_map_buffer(struct qcedev_handle *qce_hndl,
		struct dma_buf *buf, int fd,
		unsigned int fd_size, struct qcedev_reg_buf_info *binfo)
{
	int rc = 0;
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *table = NULL;
	struct context_bank_info *cb = NULL;

	if (is_iommu_present(qce_hndl)) {
		cb = get_context_bank(qce_hndl, !mem_buf_dma_buf_exclusive_owner(buf));
		if (!cb) {
			pr_err("%s: err: failed to get context bank info\n",
				__func__);
			rc = -EIO;
			goto map_err;
		}

		/* Prepare a dma buf for dma on the given device */
		attach = dma_buf_attach(buf, cb->dev);
		if (IS_ERR_OR_NULL(attach)) {
			rc = PTR_ERR(attach) ?: -ENOMEM;
			pr_err("%s: err: failed to attach dmabuf\n", __func__);
			goto map_err;
		}

		/* Get the scatterlist for the given attachment */
		table = dma_buf_map_attachment_unlocked(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR_OR_NULL(table)) {
			rc = PTR_ERR(table) ?: -ENOMEM;
			pr_err("%s: err: failed to map table\n", __func__);
			goto map_table_err;
		}

		if (table->sgl) {
			binfo->ion_buf.iova = sg_dma_address(table->sgl);
			binfo->ion_buf.mapped_buf_size = sg_dma_len(table->sgl);
			if (binfo->ion_buf.mapped_buf_size < fd_size) {
				pr_err("%s: err: mapping failed, size mismatch\n",
						__func__);
				rc = -ENOMEM;
				goto map_sg_err;
			}
		} else {
			pr_err("%s: err: sg list is NULL\n", __func__);
			rc = -ENOMEM;
			goto map_sg_err;
		}

		binfo->ion_buf.mapping_info.dev = cb->dev;
		binfo->ion_buf.mapping_info.table = table;
		binfo->ion_buf.mapping_info.attach = attach;
		get_dma_buf(buf);
		binfo->ion_buf.mapping_info.buf = buf;
		binfo->ion_buf.ion_fd = fd;
	} else {
		pr_err("%s: err: smmu not enabled\n", __func__);
		rc = -EIO;
		goto map_err;
	}

	return 0;

map_sg_err:
	dma_buf_unmap_attachment_unlocked(attach, table, DMA_BIDIRECTIONAL);
map_table_err:
	dma_buf_detach(buf, attach);
map_err:
	return rc;
}

static int ion_unmap_buffer(struct qcedev_handle *qce_hndl,
		struct qcedev_reg_buf_info *binfo)
{
	struct dma_mapping_info *mapping_info = &binfo->ion_buf.mapping_info;

	if (is_iommu_present(qce_hndl)) {
		dma_buf_unmap_attachment_unlocked(mapping_info->attach,
			mapping_info->table, DMA_BIDIRECTIONAL);
		dma_buf_detach(mapping_info->buf, mapping_info->attach);
		dma_buf_put(mapping_info->buf);

	}
	return 0;
}

static int qcedev_unmap_buffer(struct qcedev_handle *qce_hndl,
		struct qcedev_mem_client *mem_client,
		struct qcedev_reg_buf_info *binfo)
{
	int rc = -1;

	switch (mem_client->mtype) {
	case MEM_ION:
		rc = ion_unmap_buffer(qce_hndl, binfo);
		break;
	default:
		pr_err("%s: err: Mem type not supported\n", __func__);
		break;
	}

	if (rc)
		pr_err("%s: err: failed to unmap buffer\n", __func__);

	return rc;
}

int qcedev_check_and_map_buffer(void *handle, int fd, unsigned int offset,
		unsigned int fd_size, unsigned long long *vaddr)
{
	struct qcedev_handle *client = handle;
	struct qcedev_reg_buf_info *entry;
	struct dma_buf *buf;
	int ret = 0;

	if (!client || !client->cntl || !client->cntl->mem_client ||
	    !vaddr || offset >= fd_size)
		return -EINVAL;
	buf = dma_buf_get(fd);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	if (fd_size > buf->size) {
		ret = -ERANGE;
		goto put_buf;
	}

	mutex_lock(&client->registeredbufs.lock);
	list_for_each_entry(entry, &client->registeredbufs.list, list) {
		if (entry->ion_buf.ion_fd != fd)
			continue;
		if (entry->ion_buf.mapping_info.buf != buf) {
			ret = -EBUSY;
			goto unlock;
		}
		if (fd_size > entry->ion_buf.mapped_buf_size) {
			ret = -ERANGE;
			goto unlock;
		}
		refcount_inc(&entry->ref_count);
		goto mapped;
	}
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto unlock;
	}
	ret = ion_map_buffer(client, buf, fd, fd_size, entry);
	if (ret) {
		kfree(entry);
		goto unlock;
	}
	refcount_set(&entry->ref_count, 1);
	list_add_tail(&entry->list, &client->registeredbufs.list);
mapped:
	*vaddr = entry->ion_buf.iova + offset;
unlock:
	mutex_unlock(&client->registeredbufs.lock);
put_buf:
	dma_buf_put(buf);
	return ret;
}

int qcedev_check_and_unmap_buffer(void *handle, int fd)
{
	struct qcedev_reg_buf_info *binfo = NULL, *dummy = NULL;
	struct qcedev_mem_client *mem_client = NULL;
	struct qcedev_handle *qce_hndl = handle;
	bool found = false;

	if (!handle || fd < 0) {
		pr_err("%s: err: invalid input arguments\n", __func__);
		return -EINVAL;
	}

	if (!qce_hndl->cntl || !qce_hndl->cntl->mem_client) {
		pr_err("%s: err: invalid qcedev handle\n", __func__);
		return -EINVAL;
	}
	mem_client = qce_hndl->cntl->mem_client;

	if (mem_client->mtype != MEM_ION)
		return -EPERM;

	/* Check if the buffer fd is mapped and present in the regd list. */
	mutex_lock(&qce_hndl->registeredbufs.lock);
	list_for_each_entry_safe(binfo, dummy,
		&qce_hndl->registeredbufs.list, list) {
		if (binfo->ion_buf.ion_fd == fd) {
			found = true;

			/* Unmap only if there are no more references */
			if (refcount_dec_and_test(&binfo->ref_count)) {
				qcedev_unmap_buffer(qce_hndl,
					mem_client, binfo);
				list_del(&binfo->list);
				kfree(binfo);
			}
			break;
		}
	}
	mutex_unlock(&qce_hndl->registeredbufs.lock);

	if (!found) {
		pr_err("%s: err: calling unmap on unknown fd %d\n",
			__func__, fd);
		return -EINVAL;
	}

	return 0;
}

int qcedev_unmap_all_buffers(void *handle)
{
	struct qcedev_reg_buf_info *binfo = NULL;
	struct qcedev_mem_client *mem_client = NULL;
	struct qcedev_handle *qce_hndl = handle;
	struct list_head *pos;

	if (!handle) {
		pr_err("%s: err: invalid input arguments\n", __func__);
		return -EINVAL;
	}

	if (!qce_hndl->cntl || !qce_hndl->cntl->mem_client) {
		pr_err("%s: err: invalid qcedev handle\n", __func__);
		return -EINVAL;
	}
	mem_client = qce_hndl->cntl->mem_client;

	if (mem_client->mtype != MEM_ION)
		return -EPERM;

	mutex_lock(&qce_hndl->registeredbufs.lock);
	while (!list_empty(&qce_hndl->registeredbufs.list)) {
		pos = qce_hndl->registeredbufs.list.next;
		binfo = list_entry(pos, struct qcedev_reg_buf_info, list);
		if (binfo)
			qcedev_unmap_buffer(qce_hndl, mem_client, binfo);
		list_del(pos);
		kfree(binfo);
	}
	mutex_unlock(&qce_hndl->registeredbufs.lock);

	return 0;
}

