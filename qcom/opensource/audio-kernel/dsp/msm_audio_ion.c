// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/mem-buf.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/msm_audio.h>
#include <soc/qcom/secure_buffer.h>
#include <dsp/msm_audio_ion.h>

struct audio_bank {
	struct device dev;
	struct cdev cdev;
	u64 sid;
	bool smmu;
	bool online;
};

struct audio_mapping {
	struct list_head node;
	struct file *owner;
	struct audio_bank *bank;
	struct dma_buf *buf;
	struct dma_buf_attachment *attach;
	struct sg_table *table;
	struct iosys_map vmap;
	dma_addr_t addr;
	bool lent;
};

static LIST_HEAD(mappings);
static DEFINE_MUTEX(mapping_lock);

static struct audio_mapping *find_mapping(struct dma_buf *buf)
{
	struct audio_mapping *m;

	list_for_each_entry(m, &mappings, node)
		if (m->buf == buf)
			return m;
	return NULL;
}

/* AudioReach transmits one address and length for this entire buffer. */
static int audio_dma_address(struct audio_mapping *m)
{
	struct scatterlist *sg;
	dma_addr_t base, end;
	int i;

	base = sg_dma_address(m->table->sgl);
	end = base;
	for_each_sgtable_dma_sg(m->table, sg, i) {
		if (sg_dma_address(sg) != end ||
		    check_add_overflow(end, (dma_addr_t)sg_dma_len(sg), &end))
			return -ERANGE;
	}
	if (!m->buf->size || m->buf->size > end - base)
		return -ERANGE;

	if (m->bank->smmu) {
		if (upper_32_bits(base + m->buf->size - 1))
			return -ERANGE;
	} else {
		/* The hardware accelerator consumes a physical address. */
		end = base;
		for_each_sgtable_sg(m->table, sg, i) {
			if (sg_phys(sg) != end ||
			    check_add_overflow(end, (dma_addr_t)sg->length, &end))
				return -ERANGE;
		}
		if (m->buf->size > end - base)
			return -ERANGE;
	}
	m->addr = base | m->bank->sid;
	return 0;
}

static void audio_dma_unmap(struct audio_mapping *m)
{
	if (!iosys_map_is_null(&m->vmap)) {
		dma_buf_vunmap_unlocked(m->buf, &m->vmap);
		iosys_map_clear(&m->vmap);
		dma_buf_end_cpu_access(m->buf, DMA_BIDIRECTIONAL);
	}
	if (m->table) {
		dma_buf_unmap_attachment_unlocked(m->attach, m->table, DMA_BIDIRECTIONAL);
		m->table = NULL;
	}
	if (m->attach) {
		dma_buf_detach(m->buf, m->attach);
		m->attach = NULL;
	}
}

static int audio_dma_map(struct audio_mapping *m)
{
	struct dma_buf_attachment *attach;
	struct sg_table *table;
	int ret;

	attach = dma_buf_attach(m->buf, m->bank->dev.parent);
	if (IS_ERR(attach))
		return PTR_ERR(attach);
	m->attach = attach;
	table = dma_buf_map_attachment_unlocked(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(table)) {
		ret = PTR_ERR(table);
		goto unmap;
	}
	m->table = table;
	ret = audio_dma_address(m);
	if (!ret)
		return 0;
unmap:
	audio_dma_unmap(m);
	return ret;
}

static int audio_lookup(int fd, dma_addr_t *addr, size_t *len, void **vaddr)
{
	struct audio_mapping *m;
	struct dma_buf *buf;
	int ret = -ENOENT;

	if (!addr || !len)
		return -EINVAL;
	buf = dma_buf_get(fd);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	mutex_lock(&mapping_lock);
	m = find_mapping(buf);
	if (!m || (!m->table && !m->lent))
		goto unlock;
	if (vaddr) {
		if (m->lent) {
			ret = -EACCES;
			goto unlock;
		}
		if (iosys_map_is_null(&m->vmap)) {
			ret = dma_buf_begin_cpu_access(buf, DMA_BIDIRECTIONAL);
			if (ret)
				goto unlock;
			ret = dma_buf_vmap_unlocked(buf, &m->vmap);
			if (ret || m->vmap.is_iomem) {
				if (!ret) {
					dma_buf_vunmap_unlocked(buf, &m->vmap);
					ret = -EOPNOTSUPP;
				}
				iosys_map_clear(&m->vmap);
				dma_buf_end_cpu_access(buf, DMA_BIDIRECTIONAL);
				goto unlock;
			}
		}
		*vaddr = m->vmap.vaddr;
	}
	*addr = m->addr;
	*len = buf->size;
	ret = 0;
unlock:
	mutex_unlock(&mapping_lock);
	dma_buf_put(buf);
	return ret;
}

int msm_audio_get_phy_addr(int fd, dma_addr_t *addr, size_t *len)
{
	return audio_lookup(fd, addr, len, NULL);
}
EXPORT_SYMBOL(msm_audio_get_phy_addr);

int msm_audio_get_buf_addr(int fd, dma_addr_t *addr, void **vaddr, size_t *len)
{
	if (!vaddr)
		return -EINVAL;
	return audio_lookup(fd, addr, len, vaddr);
}
EXPORT_SYMBOL(msm_audio_get_buf_addr);

static int audio_import(struct audio_bank *bank, struct file *owner, struct dma_buf *buf)
{
	struct audio_mapping *m;
	int ret;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->bank = bank;
	m->buf = buf;
	m->owner = owner;
	ret = audio_dma_map(m);
	if (ret) {
		kfree(m);
		return ret;
	}
	get_dma_buf(buf);
	list_add(&m->node, &mappings);
	return 0;
}

static int audio_lend(struct audio_mapping *m)
{
	int vmids[] = { VMID_LPASS, VMID_ADSP_HEAP };
	int perms[] = { PERM_READ | PERM_WRITE, PERM_READ | PERM_WRITE };
	struct mem_buf_lend_kernel_arg arg = {
		.nr_acl_entries = ARRAY_SIZE(vmids),
		.vmids = vmids,
		.perms = perms,
	};
	int ret;

	/* The AudioReach accelerator ABI assigns its contiguous CMA bank. */
	if (m->bank->smmu)
		return -EOPNOTSUPP;
	if (m->lent)
		return 0;
	if (!m->table)
		return -ENXIO;
	audio_dma_unmap(m);
	ret = mem_buf_lend(m->buf, &arg);
	if (ret) {
		if (ret != -EADDRNOTAVAIL && audio_dma_map(m))
			dev_err(&m->bank->dev, "could not restore DMA mapping after failed lend\n");
		return ret;
	}
	m->lent = true;
	return 0;
}

static int audio_reclaim(struct audio_mapping *m)
{
	int ret;

	if (m->lent) {
		ret = mem_buf_reclaim(m->buf);
		if (ret)
			return ret;
		m->lent = false;
	}
	return m->table ? 0 : audio_dma_map(m);
}

static int audio_drop(struct audio_mapping *m)
{
	int ret = 0;

	if (m->lent) {
		ret = mem_buf_reclaim(m->buf);
		if (ret)
			dev_err(&m->bank->dev, "CMA buffer quarantined after reclaim failure: %d\n", ret);
	}
	audio_dma_unmap(m);
	list_del(&m->node);
	dma_buf_put(m->buf);
	kfree(m);
	return ret;
}

void msm_audio_ion_crash_handler(void)
{
	struct audio_mapping *m, *tmp;

	mutex_lock(&mapping_lock);
	list_for_each_entry_safe(m, tmp, &mappings, node)
		audio_drop(m);
	mutex_unlock(&mapping_lock);
}
EXPORT_SYMBOL(msm_audio_ion_crash_handler);

static long audio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct audio_bank *bank = file->private_data;
	struct audio_mapping *m;
	struct dma_buf *buf;
	int ret;

	/* All four original _IOW commands pass the fd itself, including compat. */
	if (arg > INT_MAX)
		return -EBADF;
	buf = dma_buf_get(arg);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	mutex_lock(&mapping_lock);
	if (!bank->online) {
		ret = -ENODEV;
		goto unlock;
	}
	m = find_mapping(buf);
	if (m && m->owner != file) {
		ret = -EBUSY;
		goto unlock;
	}
	switch (cmd) {
	case IOCTL_MAP_PHYS_ADDR:
		if (!m)
			ret = audio_import(bank, file, buf);
		else if (!m->table && !m->lent)
			ret = audio_dma_map(m);
		else
			ret = 0;
		break;
	case IOCTL_UNMAP_PHYS_ADDR:
		ret = m ? audio_drop(m) : -ENOENT;
		break;
	case IOCTL_MAP_HYP_ASSIGN:
		ret = m ? audio_lend(m) : -ENOENT;
		break;
	case IOCTL_UNMAP_HYP_ASSIGN:
		ret = m ? audio_reclaim(m) : -ENOENT;
		break;
	default:
		ret = -ENOTTY;
	}
unlock:
	mutex_unlock(&mapping_lock);
	dma_buf_put(buf);
	return ret;
}

static int audio_open(struct inode *inode, struct file *file)
{
	struct audio_bank *bank = container_of(inode->i_cdev, struct audio_bank, cdev);
	int ret = -ENODEV;

	mutex_lock(&mapping_lock);
	if (bank->online) {
		get_device(&bank->dev);
		file->private_data = bank;
		ret = 0;
	}
	mutex_unlock(&mapping_lock);
	return ret;
}

static int audio_release(struct inode *inode, struct file *file)
{
	struct audio_bank *bank = file->private_data;
	struct audio_mapping *m, *tmp;

	mutex_lock(&mapping_lock);
	list_for_each_entry_safe(m, tmp, &mappings, node)
		if (m->owner == file)
			audio_drop(m);
	mutex_unlock(&mapping_lock);
	put_device(&bank->dev);
	return 0;
}

static const struct file_operations audio_fops = {
	.owner = THIS_MODULE,
	.open = audio_open,
	.release = audio_release,
	.unlocked_ioctl = audio_ioctl,
	.compat_ioctl = audio_ioctl,
};

static void audio_bank_release(struct device *dev)
{
	kfree(container_of(dev, struct audio_bank, dev));
}

static struct class smmu_class = { .name = "msm_audio_ion" };
static struct class cma_class = { .name = "msm_audio_ion_cma" };

static int audio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_args args;
	struct audio_bank *bank;
	struct class *class;
	u64 mask = U32_MAX;
	int ret;

	bank = kzalloc(sizeof(*bank), GFP_KERNEL);
	if (!bank)
		return -ENOMEM;
	bank->smmu = of_property_read_bool(np, "qcom,smmu-enabled");
	if (bank->smmu) {
		ret = of_parse_phandle_with_args(np, "iommus", "#iommu-cells", 0, &args);
		if (ret)
			goto free_bank;
		of_node_put(args.np);
		if (!args.args_count) {
			ret = -EINVAL;
			goto free_bank;
		}
		of_property_read_u64(np, "qcom,smmu-sid-mask", &mask);
		bank->sid = (mask & args.args[0]) << 32;
		/* The upper 32 bits of the GPR address carry the stream ID. */
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret)
			goto free_bank;
	} else {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
		if (ret)
			goto free_bank;
	}

	class = bank->smmu ? &smmu_class : &cma_class;
	device_initialize(&bank->dev);
	bank->dev.release = audio_bank_release;
	bank->dev.parent = &pdev->dev;
	bank->dev.class = class;
	ret = dev_set_name(&bank->dev, "%s", class->name);
	if (ret)
		goto put;
	ret = alloc_chrdev_region(&bank->dev.devt, 0, 1, class->name);
	if (ret)
		goto put;
	cdev_init(&bank->cdev, &audio_fops);
	bank->cdev.owner = THIS_MODULE;
	ret = cdev_device_add(&bank->cdev, &bank->dev);
	if (ret) {
		unregister_chrdev_region(bank->dev.devt, 1);
		goto put;
	}
	platform_set_drvdata(pdev, bank);
	mutex_lock(&mapping_lock);
	bank->online = true;
	mutex_unlock(&mapping_lock);
	return 0;
put:
	put_device(&bank->dev);
	return ret;
free_bank:
	kfree(bank);
	return ret;
}

static void audio_remove(struct platform_device *pdev)
{
	struct audio_bank *bank = platform_get_drvdata(pdev);
	struct audio_mapping *m, *tmp;

	mutex_lock(&mapping_lock);
	bank->online = false;
	list_for_each_entry_safe(m, tmp, &mappings, node)
		if (m->bank == bank)
			audio_drop(m);
	mutex_unlock(&mapping_lock);
	cdev_device_del(&bank->cdev, &bank->dev);
	unregister_chrdev_region(bank->dev.devt, 1);
	put_device(&bank->dev);
}

static const struct of_device_id audio_match[] = {
	{ .compatible = "qcom,msm-audio-ion" },
	{ .compatible = "qcom,msm-audio-ion-cma" },
	{}
};
MODULE_DEVICE_TABLE(of, audio_match);

static struct platform_driver audio_driver = {
	.probe = audio_probe,
	.remove = audio_remove,
	.driver = {
		.name = "msm-audio-ion",
		.of_match_table = audio_match,
		.suppress_bind_attrs = true,
	},
};

static int __init audio_init(void)
{
	int ret;

	ret = class_register(&smmu_class);
	if (ret)
		return ret;
	ret = class_register(&cma_class);
	if (ret)
		goto unregister_smmu;
	ret = platform_driver_register(&audio_driver);
	if (!ret)
		return 0;
	class_unregister(&cma_class);
unregister_smmu:
	class_unregister(&smmu_class);
	return ret;
}

static void __exit audio_exit(void)
{
	platform_driver_unregister(&audio_driver);
	class_unregister(&cma_class);
	class_unregister(&smmu_class);
}
module_init(audio_init);
module_exit(audio_exit);
MODULE_IMPORT_NS("DMA_BUF");
MODULE_DESCRIPTION("Qualcomm AudioReach DMA-BUF context banks");
MODULE_LICENSE("GPL v2");
