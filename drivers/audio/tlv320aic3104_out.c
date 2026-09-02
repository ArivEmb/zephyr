/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>

#include "tlv320aic3104_bus.h"
#include "tlv320aic3104_fault.h"
#include "tlv320aic3104_out.h"
#include "tlv320aic3104_priv.h"
#include "tlv320aic3104_regs.h"

#define LOG_LEVEL CONFIG_AUDIO_CODEC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(tlv320aic3104);

#define TLV320AIC3104_CHIP_DB_MAX 0
#define TLV320AIC3104_CHIP_DB_MIN (-117)

void tlv320aic3104_out_start(const struct device *dev)
{
	struct tlv320aic3104_data *data = dev->data;

	data->output_running = true;

	tlv320aic3104_bus_write_reg(dev, 0, LEFT_DAC_VOL, data->last_dac_vol);
	tlv320aic3104_bus_write_reg(dev, 0, RIGHT_DAC_VOL, data->last_dac_vol);
	tlv320aic3104_bus_write_reg(dev, 0, DAC_L1_TO_HPLOUT_VOL, data->last_routing_vol);
	tlv320aic3104_bus_write_reg(dev, 0, DAC_R1_TO_HPROUT_VOL, data->last_routing_vol);

	(void)tlv320aic3104_fault_check(dev);
}

void tlv320aic3104_out_stop(const struct device *dev)
{
	struct tlv320aic3104_data *data = dev->data;

	data->output_running = false;

	tlv320aic3104_bus_write_reg(dev, 0, DAC_L1_TO_HPLOUT_VOL, DAC_TO_OUT_ROUTED_MUTE);
	tlv320aic3104_bus_write_reg(dev, 0, DAC_R1_TO_HPROUT_VOL, DAC_TO_OUT_ROUTED_MUTE);

	tlv320aic3104_bus_write_reg(dev, 0, DAC_L1_TO_LEFT_LOP_VOL, DAC_TO_OUT_ROUTED_MUTE);
	tlv320aic3104_bus_write_reg(dev, 0, DAC_R1_TO_RIGHT_LOP_VOL, DAC_TO_OUT_ROUTED_MUTE);
	tlv320aic3104_bus_write_reg(dev, 0, LEFT_DAC_VOL, DAC_VOL_MUTE);
	tlv320aic3104_bus_write_reg(dev, 0, RIGHT_DAC_VOL, DAC_VOL_MUTE);
}

static void apply_headphone_volume_regs(const struct device *dev, uint8_t dac_vol,
					uint8_t routing_vol)
{
	struct tlv320aic3104_data *data = dev->data;

	data->last_dac_vol = dac_vol;
	data->last_routing_vol = routing_vol;

	if (!data->output_running) {
		return;
	}

	tlv320aic3104_bus_write_reg(dev, 0, LEFT_DAC_VOL, dac_vol);
	tlv320aic3104_bus_write_reg(dev, 0, RIGHT_DAC_VOL, dac_vol);
	tlv320aic3104_bus_write_reg(dev, 0, DAC_L1_TO_HPLOUT_VOL, routing_vol);
	tlv320aic3104_bus_write_reg(dev, 0, DAC_R1_TO_HPROUT_VOL, routing_vol);
}

int tlv320aic3104_out_set_volume(const struct device *dev, int vol)
{
	uint8_t dac_vol;
	uint8_t routing_vol;
	int db;

	if (vol > TLV320AIC3104_CHIP_DB_MAX || vol < TLV320AIC3104_CHIP_DB_MIN) {
		return -EINVAL;
	}

	db = -vol;
	if (db > CODEC_VOLUME_ATTEN_MAX) {
		db = CODEC_VOLUME_ATTEN_MAX;
	}
	if (db < 0) {
		db = 0;
	}

	dac_vol = (uint8_t)db;
	routing_vol = 0x80 | (uint8_t)db;

	apply_headphone_volume_regs(dev, dac_vol, routing_vol);

	return 0;
}

void tlv320aic3104_out_volume_up(const struct device *dev)
{
	uint8_t dac_vol, routing_vol;

	const struct tlv320aic3104_data *data = dev->data;

	dac_vol = data->last_dac_vol;
	routing_vol = data->last_routing_vol;

	uint8_t dac_level = dac_vol & 0x7F;
	uint8_t routing_level = routing_vol & 0x7F;

	if (routing_level >= 0x76) {
		routing_vol = 0x80;
		dac_vol = 0x00;
	} else {
		if (routing_level >= 0x06) {
			routing_level -= 0x06;
		} else {
			routing_level = 0x00;
		}
		routing_vol = 0x80 | routing_level;

		if (dac_level >= 0x06) {
			dac_level -= 0x06;
			dac_vol = dac_level;
		} else {
			dac_vol = 0x00;
		}
	}

	apply_headphone_volume_regs(dev, dac_vol, routing_vol);
}

void tlv320aic3104_out_volume_down(const struct device *dev)
{
	uint8_t dac_vol, routing_vol;

	const struct tlv320aic3104_data *data = dev->data;

	dac_vol = data->last_dac_vol;
	routing_vol = data->last_routing_vol;

	uint8_t dac_level = dac_vol & 0x7F;
	uint8_t routing_level = routing_vol & 0x7F;

	if (routing_level >= 0x76) {
		return;
	}

	if (routing_level < 0x70) {
		routing_level += 0x06;
	} else {
		routing_level = 0x76;
	}
	routing_vol = 0x80 | routing_level;

	if (dac_level < 0x70) {
		dac_level += 0x06;
		dac_vol = dac_level;
	} else {
		dac_vol = 0x76;
	}

	apply_headphone_volume_regs(dev, dac_vol, routing_vol);
}

static int set_route_bit(const struct device *dev, uint8_t addr, bool routed)
{
	uint8_t val;
	int ret;

	ret = tlv320aic3104_bus_read_reg(dev, 0, addr, &val);
	if (ret < 0) {
		return ret;
	}

	if (routed) {
		val |= 0x80U;
	} else {
		val &= 0x7FU;
	}

	return tlv320aic3104_bus_write_reg(dev, 0, addr, val);
}

int tlv320aic3104_out_route_output(const struct device *dev, audio_channel_t channel,
				   uint32_t output)
{
	uint8_t selected_left;
	uint8_t other_left;
	uint8_t selected_right;
	uint8_t other_right;
	bool do_left;
	bool do_right;
	int ret;

	switch ((enum tlv320aic3104_output)output) {
	case TLV320AIC3104_OUTPUT_HP:
		selected_left = DAC_L1_TO_HPLOUT_VOL;
		other_left = DAC_L1_TO_LEFT_LOP_VOL;
		selected_right = DAC_R1_TO_HPROUT_VOL;
		other_right = DAC_R1_TO_RIGHT_LOP_VOL;
		break;
	case TLV320AIC3104_OUTPUT_LOP:
		selected_left = DAC_L1_TO_LEFT_LOP_VOL;
		other_left = DAC_L1_TO_HPLOUT_VOL;
		selected_right = DAC_R1_TO_RIGHT_LOP_VOL;
		other_right = DAC_R1_TO_HPROUT_VOL;
		break;
	default:
		return -ENOTSUP;
	}

	switch (channel) {
	case AUDIO_CHANNEL_FRONT_LEFT:
		do_left = true;
		do_right = false;
		break;
	case AUDIO_CHANNEL_FRONT_RIGHT:
		do_left = false;
		do_right = true;
		break;
	case AUDIO_CHANNEL_ALL:
		do_left = true;
		do_right = true;
		break;
	default:
		return -ENOTSUP;
	}

	if (do_left) {
		ret = set_route_bit(dev, selected_left, true);
		if (ret < 0) {
			return ret;
		}
		ret = set_route_bit(dev, other_left, false);
		if (ret < 0) {
			return ret;
		}
	}
	if (do_right) {
		ret = set_route_bit(dev, selected_right, true);
		if (ret < 0) {
			return ret;
		}
		ret = set_route_bit(dev, other_right, false);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

int tlv320aic3104_out_set_channel_mode(const struct device *dev, bool is_mono)
{
	uint8_t r7;
	int ret;

	ret = tlv320aic3104_bus_read_reg(dev, 0, CODEC_DATAPATH_SETUP, &r7);
	if (ret < 0) {
		return ret;
	}

	r7 &= (uint8_t)~CODEC_DATAPATH_MODE_FIELD_MASK;
	r7 |= is_mono ? CODEC_DATAPATH_DAC_MONO : CODEC_DATAPATH_DAC_STEREO;

	return tlv320aic3104_bus_write_reg(dev, 0, CODEC_DATAPATH_SETUP, r7);
}
