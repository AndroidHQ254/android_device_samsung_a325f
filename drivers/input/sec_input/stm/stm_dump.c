/* drivers/input/sec_input/stm/stm_dump.c
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * Core file for Samsung TSC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "stm_dev.h"
#include "stm_reg.h"

static ssize_t stm_ts_get_cmoffset_dump(struct stm_ts_data *ts, char *buf, u8 position)
{
	u8 regaddr[4] = { 0 };
	u8 *rbuff;
	int ret, i, j, size = ts->rx_count * ts->tx_count;
	u32 signature;

	if (ts->plat_data->power_state == SEC_INPUT_STATE_POWER_OFF) {
		input_err(true, &ts->client->dev, "%s: Touch is stopped\n", __func__);
		return -EPERM;
	}
	if (ts->plat_data->power_state == SEC_INPUT_STATE_LPM) {
		input_err(true, &ts->client->dev, "%s: Touch is LP mode\n", __func__);
		return -EPERM;
	}

	if (ts->reset_is_on_going) {
		input_err(true, &ts->client->dev, "%s: Reset is ongoing!\n", __func__);
		return -EPERM;
	}

	if (ts->sec.cmd_is_running) {
		input_err(true, &ts->client->dev, "%s: cmd is running\n", __func__);
		return -EBUSY;
	}

	rbuff = kzalloc(size, GFP_KERNEL);
	if (!rbuff) {
		input_err(true, &ts->client->dev, "%s: alloc failed\n", __func__);
		return -ENOMEM;
	}

	ts->stm_ts_command(ts, STM_TS_CMD_CLEAR_ALL_EVENT, true);

	stm_ts_release_all_finger(ts);

	/* Request SEC factory debug data from flash */
	regaddr[0] = 0xA4;
	regaddr[1] = 0x06;
	regaddr[2] = 0x92;
	regaddr[3] = position;
	ret = stm_ts_wait_for_echo_event(ts, regaddr, 4, 0);
	if (ret < 0) {
		snprintf(buf, ts->proc_cmoffset_size, "NG, failed to request data, %d", ret);
		goto out;
	}

	/* read header info */
	regaddr[0] = 0xA6;
	regaddr[1] = 0x00;
	regaddr[2] = 0x00;
	ret = ts->stm_ts_i2c_read(ts, &regaddr[0], 3, rbuff, 8);
	if (ret < 0) {
		input_err(true, &ts->client->dev,
				"%s: read header failed. ret: %d\n", __func__, ret);
		snprintf(buf, ts->proc_cmoffset_size, "NG, failed to read header, %d", ret);
		goto out;
	}

	signature = rbuff[3] << 24 | rbuff[2] << 16 | rbuff[1] << 8 | rbuff[0];
	input_info(true, &ts->client->dev,
			"%s: position:%d, signature:%08X (%X), validation:%X, try count:%X\n",
			__func__, position, signature, SEC_OFFSET_SIGNATURE, rbuff[4], rbuff[5]);

	if (signature != SEC_OFFSET_SIGNATURE) {
		input_err(true, &ts->client->dev, "%s: cmoffset[%d], signature is mismatched\n",
				__func__, position);
		snprintf(buf, ts->proc_cmoffset_size, "signature mismatched %08X\n", signature);
		goto out;
	}

	/* read history data */
	regaddr[0] = 0xA6;
	regaddr[1] = 0x00;
	regaddr[2] = (u8)ts->rx_count;
	ret = ts->stm_ts_i2c_read(ts, &regaddr[0], 3, rbuff, size);
	if (ret < 0) {
		input_err(true, &ts->client->dev,
				"%s: read data failed. ret: %d\n", __func__, ret);
		snprintf(buf, ts->proc_cmoffset_size, "NG, failed to read data %d", ret);
		goto out;
	}

	memset(buf, 0x00, ts->proc_cmoffset_size);
	for (i = 0; i < ts->tx_count; i++) {
		char buff[4] = { 0 };

		for (j = 0; j < ts->rx_count; j++) {
			snprintf(buff, sizeof(buff), " %d", rbuff[i * ts->rx_count + j]);
			strlcat(buf, buff, ts->proc_cmoffset_size);
		}
		snprintf(buff, sizeof(buff), "\n");
		strlcat(buf, buff, ts->proc_cmoffset_size);
	}

out:
	input_err(true, &ts->client->dev, "%s: pos:%d, buf size:%zu\n", __func__, position, strlen(buf));

	kfree(rbuff);
	return strlen(buf);
}

static ssize_t stm_ts_tsp_cmoffset_all_read(struct file *file, char __user *buf,
					size_t len, loff_t *offset)
{
	struct stm_ts_data *ts = dev_get_drvdata(ptsp);
	static ssize_t retlen = 0;
	ssize_t retlen_sdc = 0, retlen_sub = 0, retlen_main = 0;
	ssize_t count;
	loff_t pos = *offset;
#if IS_ENABLED(CONFIG_SEC_FACTORY)
	int ret;
#endif

	if (!ts) {
		pr_err("%s %s: dev is null\n", SECLOG, __func__);
		return 0;
	}

	if (pos == 0) {
#if IS_ENABLED(CONFIG_SEC_FACTORY)
		ret = stm_ts_get_cmoffset_dump(ts, ts->cmoffset_sdc_proc, OFFSET_FW_SDC);
		if (ret < 0)
			input_err(true, &ts->client->dev,
				"%s: SDC fail use boot time value\n", __func__);
		ret = stm_ts_get_cmoffset_dump(ts, ts->cmoffset_sub_proc, OFFSET_FW_SUB);
		if (ret < 0)
			input_err(true, &ts->client->dev,
				"%s: SUB fail use boot time value\n", __func__);
		ret = stm_ts_get_cmoffset_dump(ts, ts->cmoffset_main_proc, OFFSET_FW_MAIN);
		if (ret < 0)
			input_err(true, &ts->client->dev,
				"%s: MAIN fail use boot time value\n", __func__);
#endif

		retlen_sdc = strlen(ts->cmoffset_sdc_proc);
		retlen_sub = strlen(ts->cmoffset_sub_proc);
		retlen_main = strlen(ts->cmoffset_main_proc);

		ts->cmoffset_all_proc = kzalloc(ts->proc_cmoffset_all_size, GFP_KERNEL);
		if (!ts->cmoffset_all_proc) {
			input_err(true, &ts->client->dev, "%s: kzalloc fail (cmoffset_all_proc)\n", __func__);
			return 0;
		}

		strlcat(ts->cmoffset_all_proc, ts->cmoffset_sdc_proc, ts->proc_cmoffset_all_size);
		strlcat(ts->cmoffset_all_proc, ts->cmoffset_sub_proc, ts->proc_cmoffset_all_size);
		strlcat(ts->cmoffset_all_proc, ts->cmoffset_main_proc, ts->proc_cmoffset_all_size);

		retlen = strlen(ts->cmoffset_all_proc);

		input_info(true, &ts->client->dev, "%s: retlen[%ld], retlen_sdc[%ld], retlen_sub[%ld], retlen_main[%ld]\n",
						__func__, retlen, retlen_sdc, retlen_sub, retlen_main);
	}

	if (pos >= retlen)
		return 0;

	count = min(len, (size_t)(retlen - pos));

	input_info(true, &ts->client->dev, "%s: total:%ld count:%ld\n", __func__, retlen, count);

	if (copy_to_user(buf, ts->cmoffset_all_proc + pos, count)) {
		input_err(true, &ts->client->dev, "%s: copy_to_user error!\n", __func__);
		return -EFAULT;
	}

	*offset += count;

	if (count < len) {
		input_info(true, &ts->client->dev, "%s: print all & free cmoffset_all_proc\n", __func__);
		if (ts->cmoffset_all_proc)
			kfree(ts->cmoffset_all_proc);
		retlen = 0;
	}

	return count;
}

#if IS_ENABLED(CONFIG_TOUCHSCREEN_DUMP_MODE)
void stm_ts_check_rawdata(struct work_struct *work)
{
	struct stm_ts_data *ts = container_of(work, struct stm_ts_data, check_rawdata.work);

	if (ts->tsp_dump_lock == 1) {
		input_err(true, &ts->client->dev, "%s: ignored ## already checking..\n", __func__);
		return;
	}
	if (ts->plat_data->power_state == SEC_INPUT_STATE_POWER_OFF) {
		input_err(true, &ts->client->dev, "%s: ignored ## IC is power off\n", __func__);
		return;
	}

	stm_ts_run_rawdata_all(ts);
}

void dump_tsp_log(struct device *dev)
{
	struct stm_ts_data *ts = dev_get_drvdata(dev);

	pr_info("%s: %s %s: start\n", STM_TS_I2C_NAME, SECLOG, __func__);

#if IS_ENABLED(CONFIG_BATTERY_SAMSUNG)
	if (lpcharge == 1) {
		pr_err("%s: %s %s: ignored ## lpm charging Mode!!\n", STM_TS_I2C_NAME, SECLOG, __func__);
		return;
	}
#endif
	if (!ts) {
		pr_err("%s: %s %s: ignored ## tsp probe fail!!\n", STM_TS_I2C_NAME, SECLOG, __func__);
		return;
	}

	schedule_delayed_work(&ts->check_rawdata, msecs_to_jiffies(100));
}

void stm_ts_sponge_dump_flush(struct stm_ts_data *ts, int dump_area)
{
	int i, ret;
	unsigned char *sec_spg_dat;
	input_info(true, &ts->client->dev, "%s: ++\n", __func__);

	sec_spg_dat = vmalloc(SEC_TS_MAX_SPONGE_DUMP_BUFFER);
	if (!sec_spg_dat) {
		input_err(true, &ts->client->dev, "%s : Failed!!\n", __func__);
		return;
	}
	memset(sec_spg_dat, 0, SEC_TS_MAX_SPONGE_DUMP_BUFFER);

	input_info(true, &ts->client->dev, "%s: dump area=%d\n", __func__, dump_area);

	/* check dump area */
	if (dump_area == 0) {
		sec_spg_dat[0] = SEC_TS_CMD_SPONGE_LP_DUMP_EVENT;
		sec_spg_dat[1] = 0;
	} else {
		sec_spg_dat[0] = (u8)ts->sponge_dump_border & 0xff;
		sec_spg_dat[1] = (u8)(ts->sponge_dump_border >> 8);
	}

	/* dump all events at once */

	if (ts->sponge_dump_event * ts->sponge_dump_format > SEC_TS_MAX_SPONGE_DUMP_BUFFER) {
		input_err(true, &ts->client->dev, "%s: wrong sponge dump read size (%d)\n",
					__func__, ts->sponge_dump_event * ts->sponge_dump_format);
		vfree(sec_spg_dat);
		return;
	}

	ret = ts->stm_ts_read_sponge(ts, sec_spg_dat, ts->sponge_dump_event * ts->sponge_dump_format);
	if (ret < 0) {
		input_err(true, &ts->client->dev, "%s: Failed to read sponge\n", __func__);
		vfree(sec_spg_dat);
		return;
	}

	for (i = 0 ; i < ts->sponge_dump_event ; i++) {
		int e_offset = i * ts->sponge_dump_format;
		char buff[30] = {0, };
		u16 edata[5];
		edata[0] = (sec_spg_dat[1 + e_offset] & 0xFF) << 8 | (sec_spg_dat[0 + e_offset] & 0xFF);
		edata[1] = (sec_spg_dat[3 + e_offset] & 0xFF) << 8 | (sec_spg_dat[2 + e_offset] & 0xFF);
		edata[2] = (sec_spg_dat[5 + e_offset] & 0xFF) << 8 | (sec_spg_dat[4 + e_offset] & 0xFF);
		edata[3] = (sec_spg_dat[7 + e_offset] & 0xFF) << 8 | (sec_spg_dat[6 + e_offset] & 0xFF);
		edata[4] = (sec_spg_dat[9 + e_offset] & 0xFF) << 8 | (sec_spg_dat[8 + e_offset] & 0xFF);

		if (edata[0] || edata[1] || edata[2] || edata[3] || edata[4]) {
			snprintf(buff, sizeof(buff), "%03d: %04x%04x%04x%04x%04x\n",
					i + (ts->sponge_dump_event * dump_area),
					edata[0], edata[1], edata[2], edata[3], edata[4]);
#if IS_ENABLED(CONFIG_SEC_DEBUG_TSP_LOG)
			sec_tsp_sponge_log(buff);
#endif
		}
	}

	vfree(sec_spg_dat);
	input_info(true, &ts->client->dev, "%s: --\n", __func__);
	return;
}
EXPORT_SYMBOL(stm_ts_sponge_dump_flush);
#endif

ssize_t get_cmoffset_dump(struct stm_ts_data *ts, char *buf, u8 position)
{
	u8 *rbuff;
	int ret, i, j, size = ts->tx_count * ts->rx_count;
	u32 signature;
	u8 address[4] = { 0 };

	if (ts->plat_data->power_state == SEC_INPUT_STATE_POWER_OFF) {
		input_err(true, &ts->client->dev, "%s: [ERROR] Touch is stopped\n", __func__);
		return -EPERM;
	}

	if (ts->plat_data->power_state == SEC_INPUT_STATE_LPM) {
		input_err(true, &ts->client->dev, "%s: [ERROR] Touch is LP mode\n", __func__);
		return -EPERM;
	}

	if (ts->reset_is_on_going) {
		input_err(true, &ts->client->dev, "%s: Reset is ongoing!\n", __func__);
		return -EPERM;
	}

	if (ts->sec.cmd_is_running) {
		input_err(true, &ts->client->dev, "%s: cmd is running\n", __func__);
		return -EBUSY;
	}

	rbuff = kzalloc(size, GFP_KERNEL);
	if (!rbuff) {
		input_err(true, &ts->client->dev, "%s: alloc failed\n", __func__);
		return -ENOMEM;
	}

	ts->stm_ts_command(ts, STM_TS_CMD_CLEAR_ALL_EVENT, true);

	stm_ts_release_all_finger(ts);

	/* Request SEC factory debug data from flash */
	address[0] = 0xA4;
	address[1] = 0x06;
	address[2] = 0x92;
	address[3] = position;
	ret = stm_ts_wait_for_echo_event(ts, address, 4, 0);
	if (ret < 0) {
		snprintf(buf, ts->proc_cmoffset_size, "NG, failed to request data, %d", ret);
		goto out;
	}

	/* read header info */
	address[0] = 0xA6;
	address[1] = 0x00;
	address[2] = 0x00;
	ret = ts->stm_ts_i2c_read(ts, &address[0], 3, rbuff, 8);
	if (ret < 0) {
		input_err(true, &ts->client->dev,
				"%s: read header failed. ret: %d\n", __func__, ret);
		snprintf(buf, ts->proc_cmoffset_size, "NG, failed to read header, %d", ret);
		goto out;
	}

	signature = rbuff[3] << 24 | rbuff[2] << 16 | rbuff[1] << 8 | rbuff[0];
	input_info(true, &ts->client->dev,
			"%s: position:%d, signature:%08X (%X), validation:%X, try count:%X\n",
			__func__, position, signature, SEC_OFFSET_SIGNATURE, rbuff[4], rbuff[5]);

	if (signature != SEC_OFFSET_SIGNATURE) {
		input_err(true, &ts->client->dev, "%s: cmoffset[%d], signature is mismatched\n",
				__func__, position);
		snprintf(buf, ts->proc_cmoffset_size, "signature mismatched %08X\n", signature);
		goto out;
	}

	/* read history data */
	address[0] = 0xA6;
	address[1] = 0x00;
	address[2] = (u8)ts->tx_count;
	ret = ts->stm_ts_i2c_read(ts, &address[0], 3, rbuff, size);
	if (ret < 0) {
		input_err(true, &ts->client->dev,
				"%s: read data failed. ret: %d\n", __func__, ret);
		snprintf(buf, ts->proc_cmoffset_size, "NG, failed to read data %d", ret);
		goto out;
	}

	memset(buf, 0x00, ts->proc_cmoffset_size);
	for (i = 0; i < ts->rx_count; i++) {
		char buff[4] = { 0 };

		for (j = 0; j < ts->tx_count; j++) {
			snprintf(buff, sizeof(buff), " %d", rbuff[i * ts->tx_count + j]);
			strlcat(buf, buff, ts->proc_cmoffset_size);
		}
		snprintf(buff, sizeof(buff), "\n");
		strlcat(buf, buff, ts->proc_cmoffset_size);
	}
out:
	input_err(true, &ts->client->dev, "%s: pos:%d, buf size:%zu\n", __func__, position, strlen(buf));
	
	kfree(rbuff);
	return strlen(buf);

}
static ssize_t stm_ts_tsp_cmoffset_read(struct file *file, char __user *buf,
					size_t len, loff_t *offset)
{
	pr_info("[sec_input] %s called offset:%d\n", __func__, (int)*offset);
	return stm_ts_tsp_cmoffset_all_read(file, buf, len, offset);
}

static const struct file_operations tsp_cmoffset_all_file_ops = {
	.owner = THIS_MODULE,
	.read = stm_ts_tsp_cmoffset_read,
	.llseek = generic_file_llseek,
};

void stm_ts_init_proc(struct stm_ts_data *ts)
{
	struct proc_dir_entry *entry_cmoffset_all;

	ts->proc_cmoffset_size = (ts->tx_count * ts->rx_count * 4 + 100) * 4;
	ts->proc_cmoffset_all_size = ts->proc_cmoffset_size * 2;

	ts->cmoffset_sdc_proc = kzalloc(ts->proc_cmoffset_size, GFP_KERNEL);
	if (!ts->cmoffset_sdc_proc)
		return;

	ts->cmoffset_sub_proc = kzalloc(ts->proc_cmoffset_size, GFP_KERNEL);
	if (!ts->cmoffset_sub_proc)
		goto err_alloc_sub;

	ts->cmoffset_main_proc = kzalloc(ts->proc_cmoffset_size, GFP_KERNEL);
	if (!ts->cmoffset_main_proc)
		goto err_alloc_main;

	entry_cmoffset_all = proc_create("tsp_cmoffset_all", S_IFREG | S_IRUGO, NULL, &tsp_cmoffset_all_file_ops);
	if (!entry_cmoffset_all) {
		input_err(true, &ts->client->dev, "%s: failed to create /proc/tsp_cmoffset_all\n", __func__);
		goto err_cmoffset_proc_create;
	}

	input_info(true, &ts->client->dev, "%s: done\n", __func__);
	return;

err_cmoffset_proc_create:
	kfree(ts->cmoffset_main_proc);
err_alloc_main:
	kfree(ts->cmoffset_sub_proc);
err_alloc_sub:
	kfree(ts->cmoffset_sdc_proc);

	ts->cmoffset_sdc_proc = NULL;
	ts->cmoffset_sub_proc = NULL;
	ts->cmoffset_main_proc = NULL;

	input_err(true, &ts->client->dev, "%s: failed\n", __func__);
}

MODULE_LICENSE("GPL");

