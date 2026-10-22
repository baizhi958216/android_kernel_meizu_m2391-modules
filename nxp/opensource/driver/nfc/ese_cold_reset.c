// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *
 ***************************************************************************/
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 ***************************************************************************/

#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include "common.h"

/*
 * Power management of the eSE
 * eSE and NFCC both are powered using VEN gpio,
 * VEN HIGH - eSE and NFCC both are powered on
 * VEN LOW - eSE and NFCC both are power down
 */
int nfc_ese_pwr(struct nfc_dev *nfc_dev, unsigned long arg)
{
	int ret = 0;

	if (arg == ESE_POWER_ON) {
		/*
		 * Let's store the NFC VEN pin state
		 * will check stored value in case of eSE power off request,
		 * to find out if NFC MW also sent request to set VEN HIGH
		 * VEN state will remain HIGH if NFC is enabled otherwise
		 * it will be set as LOW
		 */
		nfc_dev->nfc_ven_enabled = gpio_get_value(nfc_dev->configs.gpio.ven);
		if (!nfc_dev->nfc_ven_enabled) {
			pr_debug("eSE HAL service setting ven HIGH\n");
			gpio_set_ven(nfc_dev, 1);
		} else {
			pr_debug("ven already HIGH\n");
		}
		nfc_dev->is_ese_session_active = true;
	} else if (arg == ESE_POWER_OFF) {
		if (!nfc_dev->nfc_ven_enabled) {
			pr_debug("NFC not enabled, disabling ven\n");
			gpio_set_ven(nfc_dev, 0);
		} else {
			pr_debug("keep ven high as NFC is enabled\n");
		}
		nfc_dev->is_ese_session_active = false;
		if (nfc_dev->configs.CNSS_NFC_HW_SECURE_ENABLE == true) {
			if(chk_eSE_pwr_off)
			  up(&sem_eSE_pwr_off);
		}

	} else if (arg == ESE_POWER_STATE) {
		/* get VEN gpio state for eSE, as eSE also enabled through same GPIO */
		ret = gpio_get_value(nfc_dev->configs.gpio.ven);
	} else {
		pr_err("%s bad arg %lu\n", __func__, arg);
		ret = -ENOIOCTLCMD;
	}
	return ret;
}

/**
 * send_ese_cmd() - Send eSE command to NFC controller.
 * @nfc_dev: NFC device handle.
 *
 * Return: 0 on pass and negative value on failure.
 */
static int send_ese_cmd(struct nfc_dev *nfc_dev)
{
	int ret;

	if (nfc_dev->nfc_state == NFC_STATE_FW_DWL) {
		dev_err(nfc_dev->nfc_device,
			"cannot send ese cmd as FW download is in-progress\n");
		return -EBUSY;
	}
	if (!gpio_get_value(nfc_dev->configs.gpio.ven)) {
		dev_err(nfc_dev->nfc_device,
				"cannot send ese cmd as NFCC powered off\n");
		return -ENODEV;
	}
	if (nfc_dev->cold_reset.cmd_buf == NULL)
		return -EFAULT;

	ret = nfc_dev->nfc_write(nfc_dev, nfc_dev->cold_reset.cmd_buf,
						nfc_dev->cold_reset.cmd_len,
						MAX_RETRY_COUNT);
	if (ret <= 0)
		dev_err(nfc_dev->nfc_device,
				"%s: write failed after max retry, ret %d\n",
							__func__, ret);

	return ret;
}

/**
 * read_cold_reset_rsp() - Read response of the cold reset command.
 * @nfc_dev: NFC device handle.
 * @header:  Pointer to NCI header if it is already read.
 *
 * Return: 0 on pass and negative value on failure.
 */
int read_cold_reset_rsp(struct nfc_dev *chip, char *header, u32 generation)
{
	unsigned long flags;
	u16 capacity;
	u8 expected_oid;
	bool nfc_enabled, complete = false;
	u8 *response;
	int rc = -EIO;
	u8 payload_length;

	/* Snapshot the transaction before allocating. An interrupted ioctl can
	 * retire it and a later ioctl can change shared sizes while this read is
	 * still performing I2C. Only the captured generation may receive its ACK.
	 */
	spin_lock_irqsave(&chip->cold_reset_lock, flags);
	if (!chip->cold_reset.rsp_pending ||
			chip->cold_reset_generation != generation) {
		spin_unlock_irqrestore(&chip->cold_reset_lock, flags);
		return -ECANCELED;
	}
	capacity = chip->cold_reset.rsp_len;
	expected_oid = chip->cold_reset_expected_oid;
	nfc_enabled = READ_ONCE(chip->cold_reset.is_nfc_enabled);
	spin_unlock_irqrestore(&chip->cold_reset_lock, flags);
	if (capacity < COLD_RESET_RSP_LEN) {
		rc = -EINVAL;
		goto finish;
	}
	response = kzalloc(capacity, GFP_DMA | GFP_KERNEL);
	if (!response) {
		rc = -ENOMEM;
		goto finish;
	}
	if (!nfc_enabled && chip->interface == PLATFORM_IF_I2C) {
		rc = i2c_master_recv(chip->i2c_dev.client, response, NCI_HDR_LEN);
		if (rc != NCI_HDR_LEN) {
			rc = -EIO;
			goto free_response;
		}
		if (!(response[0] & NCI_RSP_PKT_TYPE) ||
				response[1] != expected_oid) {
			rc = -EINVAL;
			goto free_response;
		}
	} else if (header) {
		memcpy(response, header, NCI_HDR_LEN);
	} else {
		rc = -EINVAL;
		goto free_response;
	}
	payload_length = response[NCI_PAYLOAD_LEN_IDX];
	if (!payload_length || payload_length > capacity - NCI_HDR_LEN) {
		rc = -ENOMEM;
		goto free_response;
	}
	if (chip->interface == PLATFORM_IF_I2C) {
		rc = chip->nfc_read(chip, response + NCI_PAYLOAD_IDX,
			payload_length, NCI_CMD_RSP_TIMEOUT_MS);
		if (rc != payload_length) {
			rc = -EIO;
			goto free_response;
		}
		rc = response[NCI_PAYLOAD_IDX];
		pr_debug("nfc ese rsp hdr 0x%x 0x%x 0x%x, payload byte0 0x%x\n",
			response[0], response[1], response[2], response[3]);
	}
free_response:
	kfree(response);
finish:
	spin_lock_irqsave(&chip->cold_reset_lock, flags);
	if (chip->cold_reset_generation == generation &&
			chip->cold_reset.rsp_pending) {
		if (rc >= 0)
			chip->cold_reset.status = rc;
		chip->cold_reset_response_error = rc < 0 ? rc : 0;
		WRITE_ONCE(chip->cold_reset.rsp_pending, false);
		complete = true;
	} else {
		rc = -ECANCELED;
	}
	spin_unlock_irqrestore(&chip->cold_reset_lock, flags);
	if (complete)
		wake_up_interruptible(&chip->cold_reset.read_wq);
	return rc;
}


/**
 * ese_cold_reset_ioctl() - This function handles the eSE cold reset ioctls.
 * @nfc_dev: NFC device handle.
 * @arg: ioctl argument.
 *
 * Return: 0 on pass and negative value on failure.
 */

int ese_cold_reset_ioctl(struct nfc_dev *nfc_dev, unsigned long arg)
{
	int ret, response_status;
	u16 response_length = 0;
	u32 generation = 0;
	unsigned long flags;
	struct ese_ioctl_arg ioctl_arg;
	struct ese_cold_reset_arg *cold_reset_arg = NULL;

	if (!arg) {
		dev_err(nfc_dev->nfc_device, "arg is invalid\n");
		return -EINVAL;
	}

	ret = copy_from_user((void *)&ioctl_arg, (const void *)arg,
						sizeof(ioctl_arg));
	if (ret) {
		dev_err(nfc_dev->nfc_device,
				"ese ioctl arg copy from user failed\n");
		return -EFAULT;
	}

	cold_reset_arg = kzalloc(sizeof(struct ese_cold_reset_arg), GFP_KERNEL);
	if (!cold_reset_arg)
		return -ENOMEM;

	mutex_lock(&nfc_dev->write_mutex);
	ret = copy_struct_from_user(cold_reset_arg,
				sizeof(struct ese_cold_reset_arg),
				u64_to_user_ptr(ioctl_arg.buf),
				sizeof(struct ese_cold_reset_arg));
	if (ret) {
		dev_err(nfc_dev->nfc_device,
			"ese ioctl arg buffer copy from user failed\n");

		ret = -EFAULT;
		goto err;
	}

	switch (cold_reset_arg->sub_cmd) {

	case ESE_COLD_RESET_DO:

		/*
		 * cold reset allowed during protection enable, only if the
		 * source is same as the one which enabled protection.
		 */
		if (nfc_dev->cold_reset.is_crp_en &&
			(cold_reset_arg->src !=
				nfc_dev->cold_reset.last_src_ese_prot)) {
			dev_err(nfc_dev->nfc_device,
				"cold reset from %d denied, protection is on\n",
				cold_reset_arg->src);
			ret = -EACCES;
			goto err;
		}

		nfc_dev->cold_reset.cmd_buf = kzalloc(COLD_RESET_CMD_LEN,
							GFP_DMA | GFP_KERNEL);
		if (!nfc_dev->cold_reset.cmd_buf) {
			ret = -ENOMEM;
			goto err;
		}

		nfc_dev->cold_reset.cmd_buf[0] = PROP_NCI_CMD_GID;
		nfc_dev->cold_reset.cmd_buf[1] = COLD_RESET_OID;
		nfc_dev->cold_reset.cmd_buf[2] = COLD_RESET_CMD_PL_LEN;
		nfc_dev->cold_reset.cmd_len = NCI_HDR_LEN +
						COLD_RESET_CMD_PL_LEN;
		response_length = COLD_RESET_RSP_LEN;
		break;

	case ESE_COLD_RESET_PROTECT_EN:

		if (nfc_dev->cold_reset.is_crp_en) {
			if (cold_reset_arg->src !=
				nfc_dev->cold_reset.last_src_ese_prot) {
				dev_err(nfc_dev->nfc_device,
					"ese protection enable denied\n");
				ret = -EACCES;
				goto err;
			}
			pr_warn("ese protection already enabled\n");

			ret = 0;
			/* free buffers and exit with pass */
			goto err;
		}

	case ESE_COLD_RESET_PROTECT_DIS:

		if (nfc_dev->cold_reset.is_crp_en &&
			cold_reset_arg->src !=
				nfc_dev->cold_reset.last_src_ese_prot) {
			pr_err("ese cold reset protection disable denied\n");
			ret = -EACCES;
			goto err;
		}
		nfc_dev->cold_reset.cmd_buf = kzalloc(COLD_RESET_PROT_CMD_LEN,
							GFP_DMA | GFP_KERNEL);
		if (!nfc_dev->cold_reset.cmd_buf) {
			ret = -ENOMEM;
			goto err;
		}

		nfc_dev->cold_reset.cmd_buf[0] = PROP_NCI_CMD_GID;
		nfc_dev->cold_reset.cmd_buf[1] = COLD_RESET_PROT_OID;
		nfc_dev->cold_reset.cmd_buf[2] = COLD_RESET_PROT_CMD_PL_LEN;
		nfc_dev->cold_reset.cmd_len = NCI_HDR_LEN +
						COLD_RESET_PROT_CMD_PL_LEN;
		response_length = COLD_RESET_PROT_RSP_LEN;
		if (cold_reset_arg->sub_cmd == ESE_COLD_RESET_PROTECT_EN)
			nfc_dev->cold_reset.cmd_buf[3] = 0x1;
		else
			nfc_dev->cold_reset.cmd_buf[3] = 0x0;

		break;

	default:
		pr_err("%s invalid ese ioctl sub cmd %d\n", __func__,
					cold_reset_arg->sub_cmd);
		ret = -ENOIOCTLCMD;
		goto err;
	}

	pr_debug("nfc ese cmd hdr 0x%x 0x%x 0x%x\n",
				nfc_dev->cold_reset.cmd_buf[0],
				nfc_dev->cold_reset.cmd_buf[1],
				nfc_dev->cold_reset.cmd_buf[2]);

	spin_lock_irqsave(&nfc_dev->cold_reset_lock, flags);
	generation = ++nfc_dev->cold_reset_generation;
	if (!generation)
		generation = ++nfc_dev->cold_reset_generation;
	nfc_dev->cold_reset.rsp_len = response_length;
	nfc_dev->cold_reset_expected_oid = nfc_dev->cold_reset.cmd_buf[1];
	nfc_dev->cold_reset_response_error = 0;
	WRITE_ONCE(nfc_dev->cold_reset.rsp_pending, true);
	spin_unlock_irqrestore(&nfc_dev->cold_reset_lock, flags);
	ret = send_ese_cmd(nfc_dev);
	if (ret <= 0) {
		pr_err("failed to send ese command\n");
		goto err;
	}


	/* check if NFC is enabled */
	if (nfc_dev->cold_reset.is_nfc_enabled) {
		/*
		 * nfc_read thread will initiate cold reset response
		 * and it will signal for data available
		 */
		ret = wait_event_interruptible(nfc_dev->cold_reset.read_wq,
			!READ_ONCE(nfc_dev->cold_reset.rsp_pending) ||
			READ_ONCE(nfc_dev->removing));
		if (ret)
			goto err;
	} else {

		/*
		 * Read data as NFC read thread is not active
		 */

		if (nfc_dev->interface == PLATFORM_IF_I2C) {
			ret =  is_nfc_data_available_for_read(nfc_dev);
			if (ret <= 0) {
				nfc_dev->nfc_disable_intr(nfc_dev);
				goto err;
			}

			ret = read_cold_reset_rsp(nfc_dev, NULL, generation);
			if (ret < 0) {
				pr_err("%s rsp read err\n", __func__);
				goto err;
			}
		} else {
			/*
			 * Enable intr as it is disabled when NFC is in disable
			 * state
			 */
			nfc_dev->nfc_enable_intr(nfc_dev);

			ret = wait_event_interruptible(
				nfc_dev->cold_reset.read_wq,
				!READ_ONCE(nfc_dev->cold_reset.rsp_pending) ||
			READ_ONCE(nfc_dev->removing));
			if (ret)
				goto err;
		}

		nfc_dev->nfc_disable_intr(nfc_dev);
	}

	if (READ_ONCE(nfc_dev->removing)) {
		ret = -ENODEV;
		goto err;
	}

	spin_lock_irqsave(&nfc_dev->cold_reset_lock, flags);
	response_status = nfc_dev->cold_reset_generation != generation ?
		-ECANCELED : nfc_dev->cold_reset_response_error ?
		nfc_dev->cold_reset_response_error : nfc_dev->cold_reset.status;
	spin_unlock_irqrestore(&nfc_dev->cold_reset_lock, flags);
	if (response_status < 0) {
		ret = response_status;
		goto err;
	}

	if (cold_reset_arg->sub_cmd == ESE_COLD_RESET_PROTECT_EN) {
		nfc_dev->cold_reset.is_crp_en = true;
		nfc_dev->cold_reset.last_src_ese_prot = cold_reset_arg->src;
	} else if (cold_reset_arg->sub_cmd == ESE_COLD_RESET_PROTECT_DIS) {
		nfc_dev->cold_reset.is_crp_en = false;
		nfc_dev->cold_reset.last_src_ese_prot =
						ESE_COLD_RESET_ORIGIN_NONE;
	} else
		pr_debug("ese cmd is %d\n", cold_reset_arg->sub_cmd);

	ret = response_status;

err:
	spin_lock_irqsave(&nfc_dev->cold_reset_lock, flags);
	if (generation && nfc_dev->cold_reset_generation == generation) {
		++nfc_dev->cold_reset_generation;
		WRITE_ONCE(nfc_dev->cold_reset.rsp_pending, false);
	}
	spin_unlock_irqrestore(&nfc_dev->cold_reset_lock, flags);
	if (nfc_dev->cold_reset.cmd_buf != NULL) {
		kfree(nfc_dev->cold_reset.cmd_buf);
		nfc_dev->cold_reset.cmd_buf = NULL;
	}
	if (cold_reset_arg != NULL) {
		kfree(cold_reset_arg);
		cold_reset_arg = NULL;
	}

	mutex_unlock(&nfc_dev->write_mutex);

	return ret;
}
