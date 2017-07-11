/*
 * Driver for the IMX185 camera sensor.
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 By Tech Design S.L. All Rights Reserved.
 * Copyright (C) 2012-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Based on:
 * - the IMX185 driver from QC msm-3.10 kernel on codeaurora.org:
 *   https://us.codeaurora.org/cgit/quic/la/kernel/msm-3.10/tree/drivers/
 *       media/platform/msm/camera_v2/sensor/imx185.c?h=LA.BR.1.2.4_rb1.41
 * - the OV5640 driver posted on linux-media:
 *   https://www.mail-archive.com/linux-media%40vger.kernel.org/msg92671.html
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-of.h>
#include <media/v4l2-subdev.h>

static DEFINE_MUTEX(imx185_lock);

/* HACKs here! */

#include <../drivers/media/platform/msm/cci/msm_cci.h>

#ifdef dev_dbg
	#undef dev_dbg
	#define dev_dbg dev_err
#endif

#define IMX185_VOLTAGE_ANALOG               2800000
#define IMX185_VOLTAGE_DIGITAL_CORE         1500000
#define IMX185_VOLTAGE_DIGITAL_IO           1800000


#define IMX185_SYSTEM_CTRL0		0x3000
#define	IMX185_SYSTEM_CTRL0_START	0x00
#define IMX185_SYSTEM_CTRL0_STOP 0x01 //Standby

#define IMX185_CHIP_ID_HIGH		0x3384
#define		IMX185_CHIP_ID_HIGH_BYTE	0x85
#define IMX185_CHIP_ID_LOW		0x3385
#define		IMX185_CHIP_ID_LOW_BYTE		0x01


#define IMX185_AWB_MANUAL_CONTROL	0x3406
#define		IMX185_AWB_MANUAL_ENABLE	BIT(0)
#define IMX185_AEC_PK_MANUAL		0x3503
#define		IMX185_AEC_MANUAL_ENABLE	BIT(0)
#define		IMX185_AGC_MANUAL_ENABLE	BIT(1)
#define IMX185_TIMING_TC_REG20		0x3820
#define		IMX185_SENSOR_VFLIP		BIT(1)
#define		IMX185_ISP_VFLIP		BIT(2)
#define IMX185_TIMING_TC_REG21		0x3821
#define		IMX185_SENSOR_MIRROR		BIT(1)
#define IMX185_PRE_ISP_TEST_SETTING_1	0x503d
#define		IMX185_TEST_PATTERN_MASK	0x3
#define		IMX185_SET_TEST_PATTERN(x)	((x) & IMX185_TEST_PATTERN_MASK)
#define		IMX185_TEST_PATTERN_ENABLE	BIT(7)
#define IMX185_SDE_SAT_U		0x5583
#define IMX185_SDE_SAT_V		0x5584

enum imx185_mode {
	IMX185_MODE_MIN = 0,
	IMX185_MODE_1080P = 0,
	IMX185_MODE_MAX = 0
};

struct reg_value {
	u16 reg;
	u8 val;
};

struct imx185_mode_info {
	enum imx185_mode mode;
	u32 width;
	u32 height;
	struct reg_value *data;
	u32 data_size;
};

struct imx185 {
	struct i2c_client *i2c_client;
	struct device *dev;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_of_endpoint ep;
	struct v4l2_mbus_framefmt fmt;
	struct v4l2_rect crop;
	struct clk *xclk;
	/* External clock frequency currently supported is 23880000Hz */
	u32 xclk_freq;

	struct regulator *io_regulator;
	struct regulator *core_regulator;
	struct regulator *analog_regulator;

	enum imx185_mode current_mode;

	/* Cached control values */
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *autogain;
	struct v4l2_ctrl *autoexposure;
	struct v4l2_ctrl *awb;
	struct v4l2_ctrl *pattern;

	struct mutex power_lock; /* lock to protect power state */
	bool power;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *rst_gpio;

	struct v4l2_subdev *cci;
};

static inline struct imx185 *to_imx185(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx185, sd);
}

static struct reg_value imx185_global_init_setting[] = {
	{ 0x3103, 0x11 },
	{ 0x3008, 0x82 },
	{ 0x3008, 0x42 },
	{ 0x3103, 0x03 },
	{ 0x3503, 0x07 },
	{ 0x3002, 0x1c },
	{ 0x3006, 0xc3 },
	{ 0x300e, 0x45 },
	{ 0x3017, 0x00 },
	{ 0x3018, 0x00 },
	{ 0x302e, 0x0b },
	{ 0x3037, 0x13 },
	{ 0x3108, 0x01 },
	{ 0x3611, 0x06 },
	{ 0x3500, 0x00 },
	{ 0x3501, 0x01 },
	{ 0x3502, 0x00 },
	{ 0x350a, 0x00 },
	{ 0x350b, 0x3f },
	{ 0x3620, 0x33 },
	{ 0x3621, 0xe0 },
	{ 0x3622, 0x01 },
	{ 0x3630, 0x2e },
	{ 0x3631, 0x00 },
	{ 0x3632, 0x32 },
	{ 0x3633, 0x52 },
	{ 0x3634, 0x70 },
	{ 0x3635, 0x13 },
	{ 0x3636, 0x03 },
	{ 0x3703, 0x5a },
	{ 0x3704, 0xa0 },
	{ 0x3705, 0x1a },
	{ 0x3709, 0x12 },
	{ 0x370b, 0x61 },
	{ 0x370f, 0x10 },
	{ 0x3715, 0x78 },
	{ 0x3717, 0x01 },
	{ 0x371b, 0x20 },
	{ 0x3731, 0x12 },
	{ 0x3901, 0x0a },
	{ 0x3905, 0x02 },
	{ 0x3906, 0x10 },
	{ 0x3719, 0x86 },
	{ 0x3810, 0x00 },
	{ 0x3811, 0x10 },
	{ 0x3812, 0x00 },
	{ 0x3821, 0x01 },
	{ 0x3824, 0x01 },
	{ 0x3826, 0x03 },
	{ 0x3828, 0x08 },
	{ 0x3a19, 0xf8 },
	{ 0x3c01, 0x34 },
	{ 0x3c04, 0x28 },
	{ 0x3c05, 0x98 },
	{ 0x3c07, 0x07 },
	{ 0x3c09, 0xc2 },
	{ 0x3c0a, 0x9c },
	{ 0x3c0b, 0x40 },
	{ 0x3c01, 0x34 },
	{ 0x4001, 0x02 },
	{ 0x4514, 0x00 },
	{ 0x4520, 0xb0 },
	{ 0x460b, 0x37 },
	{ 0x460c, 0x20 },
	{ 0x4818, 0x01 },
	{ 0x481d, 0xf0 },
	{ 0x481f, 0x50 },
	{ 0x4823, 0x70 },
	{ 0x4831, 0x14 },
	{ 0x5000, 0xa7 },
	{ 0x5001, 0x83 },
	{ 0x501d, 0x00 },
	{ 0x501f, 0x00 },
	{ 0x503d, 0x00 },
	{ 0x505c, 0x30 },
	{ 0x5181, 0x59 },
	{ 0x5183, 0x00 },
	{ 0x5191, 0xf0 },
	{ 0x5192, 0x03 },
	{ 0x5684, 0x10 },
	{ 0x5685, 0xa0 },
	{ 0x5686, 0x0c },
	{ 0x5687, 0x78 },
	{ 0x5a00, 0x08 },
	{ 0x5a21, 0x00 },
	{ 0x5a24, 0x00 },
	{ 0x3008, 0x02 },
	{ 0x3503, 0x00 },
	{ 0x5180, 0xff },
	{ 0x5181, 0xf2 },
	{ 0x5182, 0x00 },
	{ 0x5183, 0x14 },
	{ 0x5184, 0x25 },
	{ 0x5185, 0x24 },
	{ 0x5186, 0x09 },
	{ 0x5187, 0x09 },
	{ 0x5188, 0x0a },
	{ 0x5189, 0x75 },
	{ 0x518a, 0x52 },
	{ 0x518b, 0xea },
	{ 0x518c, 0xa8 },
	{ 0x518d, 0x42 },
	{ 0x518e, 0x38 },
	{ 0x518f, 0x56 },
	{ 0x5190, 0x42 },
	{ 0x5191, 0xf8 },
	{ 0x5192, 0x04 },
	{ 0x5193, 0x70 },
	{ 0x5194, 0xf0 },
	{ 0x5195, 0xf0 },
	{ 0x5196, 0x03 },
	{ 0x5197, 0x01 },
	{ 0x5198, 0x04 },
	{ 0x5199, 0x12 },
	{ 0x519a, 0x04 },
	{ 0x519b, 0x00 },
	{ 0x519c, 0x06 },
	{ 0x519d, 0x82 },
	{ 0x519e, 0x38 },
	{ 0x5381, 0x1e },
	{ 0x5382, 0x5b },
	{ 0x5383, 0x08 },
	{ 0x5384, 0x0a },
	{ 0x5385, 0x7e },
	{ 0x5386, 0x88 },
	{ 0x5387, 0x7c },
	{ 0x5388, 0x6c },
	{ 0x5389, 0x10 },
	{ 0x538a, 0x01 },
	{ 0x538b, 0x98 },
	{ 0x5300, 0x08 },
	{ 0x5301, 0x30 },
	{ 0x5302, 0x10 },
	{ 0x5303, 0x00 },
	{ 0x5304, 0x08 },
	{ 0x5305, 0x30 },
	{ 0x5306, 0x08 },
	{ 0x5307, 0x16 },
	{ 0x5309, 0x08 },
	{ 0x530a, 0x30 },
	{ 0x530b, 0x04 },
	{ 0x530c, 0x06 },
	{ 0x5480, 0x01 },
	{ 0x5481, 0x08 },
	{ 0x5482, 0x14 },
	{ 0x5483, 0x28 },
	{ 0x5484, 0x51 },
	{ 0x5485, 0x65 },
	{ 0x5486, 0x71 },
	{ 0x5487, 0x7d },
	{ 0x5488, 0x87 },
	{ 0x5489, 0x91 },
	{ 0x548a, 0x9a },
	{ 0x548b, 0xaa },
	{ 0x548c, 0xb8 },
	{ 0x548d, 0xcd },
	{ 0x548e, 0xdd },
	{ 0x548f, 0xea },
	{ 0x5490, 0x1d },
	{ 0x5580, 0x02 },
	{ 0x5583, 0x40 },
	{ 0x5584, 0x10 },
	{ 0x5589, 0x10 },
	{ 0x558a, 0x00 },
	{ 0x558b, 0xf8 },
	{ 0x5800, 0x3f },
	{ 0x5801, 0x16 },
	{ 0x5802, 0x0e },
	{ 0x5803, 0x0d },
	{ 0x5804, 0x17 },
	{ 0x5805, 0x3f },
	{ 0x5806, 0x0b },
	{ 0x5807, 0x06 },
	{ 0x5808, 0x04 },
	{ 0x5809, 0x04 },
	{ 0x580a, 0x06 },
	{ 0x580b, 0x0b },
	{ 0x580c, 0x09 },
	{ 0x580d, 0x03 },
	{ 0x580e, 0x00 },
	{ 0x580f, 0x00 },
	{ 0x5810, 0x03 },
	{ 0x5811, 0x08 },
	{ 0x5812, 0x0a },
	{ 0x5813, 0x03 },
	{ 0x5814, 0x00 },
	{ 0x5815, 0x00 },
	{ 0x5816, 0x04 },
	{ 0x5817, 0x09 },
	{ 0x5818, 0x0f },
	{ 0x5819, 0x08 },
	{ 0x581a, 0x06 },
	{ 0x581b, 0x06 },
	{ 0x581c, 0x08 },
	{ 0x581d, 0x0c },
	{ 0x581e, 0x3f },
	{ 0x581f, 0x1e },
	{ 0x5820, 0x12 },
	{ 0x5821, 0x13 },
	{ 0x5822, 0x21 },
	{ 0x5823, 0x3f },
	{ 0x5824, 0x68 },
	{ 0x5825, 0x28 },
	{ 0x5826, 0x2c },
	{ 0x5827, 0x28 },
	{ 0x5828, 0x08 },
	{ 0x5829, 0x48 },
	{ 0x582a, 0x64 },
	{ 0x582b, 0x62 },
	{ 0x582c, 0x64 },
	{ 0x582d, 0x28 },
	{ 0x582e, 0x46 },
	{ 0x582f, 0x62 },
	{ 0x5830, 0x60 },
	{ 0x5831, 0x62 },
	{ 0x5832, 0x26 },
	{ 0x5833, 0x48 },
	{ 0x5834, 0x66 },
	{ 0x5835, 0x44 },
	{ 0x5836, 0x64 },
	{ 0x5837, 0x28 },
	{ 0x5838, 0x66 },
	{ 0x5839, 0x48 },
	{ 0x583a, 0x2c },
	{ 0x583b, 0x28 },
	{ 0x583c, 0x26 },
	{ 0x583d, 0xae },
	{ 0x5025, 0x00 },
	{ 0x3a0f, 0x30 },
	{ 0x3a10, 0x28 },
	{ 0x3a1b, 0x30 },
	{ 0x3a1e, 0x26 },
	{ 0x3a11, 0x60 },
	{ 0x3a1f, 0x14 },
	{ 0x0601, 0x02 },
	{ 0x3008, 0x42 },
	{ 0x3008, 0x02 }
};


static struct reg_value imx185_setting_1080p[] = {

	{0x3002, 0x01},
	{0x3005, 0x00},/*10BIT*/
	{0x3006, 0x00},
	{0x3007, 0x50},
	{0x3009, 0x01},
	{0x300a, 0x3c},/*10BIT*/
	{0x300f, 0x01},
	{0x3018, 0x65},
	{0x3019, 0x04},
	{0x301b, 0x4c},
	{0x301c, 0x04},
	{0x301d, 0x08},
	{0x301e, 0x02},

	{0x3036, 0x06},
	{0x3038, 0x08},
	{0x3039, 0x00},
	{0x303a, 0x40},
	{0x303b, 0x04},
	{0x303c, 0x0c},
	{0x303d, 0x00},
	{0x303e, 0x7c},
	{0x303f, 0x07},

	{0x3044, 0xe1},
	{0x3048, 0x33},

	{0x305C, 0x20},
	{0x305D, 0x00},
	{0x305E, 0x18},
	{0x305F, 0x00},
	{0x3063, 0x74},

	{0x3084, 0x0f},

	{0x3086, 0x10},
	{0x30A1, 0x44},
	{0x30cf, 0xe1},
	{0x30d0, 0x29},
	{0x30d2, 0x9b},
	{0x30d3, 0x01},

	{0x311d, 0x0a},
	{0x3123, 0x0f},
	{0x3126, 0xdf},
	{0x3147, 0x87},
	{0x31e0, 0x01},
	{0x31e1, 0x9e},
	{0x31e2, 0x01},
	{0x31e5, 0x05},
	{0x31e6, 0x05},
	{0x31e7, 0x3a},
	{0x31e8, 0x3a},

	{0x3203, 0xc8},
	{0x3207, 0x54},
	{0x3213, 0x16},
	{0x3215, 0xf6},
	{0x321a, 0x14},
	{0x321b, 0x51},
	{0x3229, 0xe7},
	{0x322a, 0xf0},
	{0x322b, 0x10},
	{0x3231, 0xe7},
	{0x3232, 0xf0},
	{0x3233, 0x10},
	{0x323c, 0xe8},
	{0x323d, 0x70},
	{0x3243, 0x08},
	{0x3244, 0xe1},
	{0x3245, 0x10},
	{0x3247, 0xe7},
	{0x3248, 0x60},
	{0x3249, 0x1e},
	{0x324b, 0x00},
	{0x324c, 0x41},
	{0x3250, 0x30},
	{0x3251, 0x0a},
	{0x3252, 0xff},
	{0x3253, 0xff},
	{0x3254, 0xff},
	{0x3255, 0x02},
	{0x3257, 0xf0},
	{0x325a, 0xa6},
	{0x325d, 0x14},
	{0x325e, 0x51},
	{0x3260, 0x00},
	{0x3261, 0x61},
	{0x3266, 0x30},
	{0x3267, 0x05},
	{0x3275, 0xe7},
	{0x3281, 0xea},
	{0x3282, 0x70},
	{0x3285, 0xff},
	{0x328a, 0xf0},
	{0x328d, 0xb6},
	{0x328e, 0x40},
	{0x3290, 0x42},
	{0x3291, 0x51},
	{0x3292, 0x1e},
	{0x3294, 0xc4},
	{0x3295, 0x20},
	{0x3297, 0x50},
	{0x3298, 0x31},
	{0x3299, 0x1f},
	{0x329b, 0xc0},
	{0x329c, 0x60},
	{0x329e, 0x4c},
	{0x329f, 0x71},
	{0x32a0, 0x1f},
	{0x32a2, 0xb6},
	{0x32a3, 0xc0},
	{0x32a4, 0x0b},
	{0x32a9, 0x24},
	{0x32aa, 0x41},
	{0x32b0, 0x25},
	{0x32b1, 0x51},
	{0x32b7, 0x1c},
	{0x32b8, 0xc1},
	{0x32b9, 0x12},
	{0x32be, 0x1d},
	{0x32bf, 0xd1},
	{0x32c0, 0x12},
	{0x32c2, 0xa8},
	{0x32c3, 0xc0},
	{0x32c4, 0x0a},
	{0x32c5, 0x1e},
	{0x32c6, 0x21},
	{0x32c9, 0xb0},
	{0x32ca, 0x40},
	{0x32cc, 0x26},
	{0x32cd, 0xa1},
	{0x32d0, 0xb6},
	{0x32d1, 0xc0},
	{0x32d2, 0x0b},
	{0x32d4, 0xe2},
	{0x32d5, 0x40},
	{0x32d8, 0x4e},
	{0x32d9, 0xa1},
	{0x32ec, 0xf0},

	{0x3303, 0x00},
	{0x3305, 0x03},
	{0x3314, 0x04},
	{0x3315, 0x01},
	{0x3316, 0x04},
	{0x3317, 0x04},
	{0x3318, 0x38},
	{0x3319, 0x04},
	{0x332c, 0x40},
	{0x332d, 0x20},
	{0x332e, 0x03},
	{0x333e, 0x0a},/*10BIT*/
	{0x333f, 0x0a},/*10BIT*/
	{0x3340, 0x03},
	{0x3341, 0x20},
	{0x3342, 0x25},
	{0x3343, 0x68},
	{0x3344, 0x20},
	{0x3345, 0x40},
	{0x3346, 0x28},
	{0x3347, 0x20},
	{0x3348, 0x18},
	{0x3349, 0x78},
	{0x334a, 0x28},
	{0x334e, 0xb4},
	{0x334f, 0x01},


};


static struct imx185_mode_info imx185_mode_info_data[IMX185_MODE_MAX + 1] = {
	{
		.mode = IMX185_MODE_1080P,
		.width = 1920,
		.height = 1080,
		.data = imx185_setting_1080p,
		.data_size = ARRAY_SIZE(imx185_setting_1080p)
	},

};

static int imx185_regulators_enable(struct imx185 *imx185)
{
	int ret;

	ret = regulator_enable(imx185->io_regulator);
	if (ret < 0) {
		dev_err(imx185->dev, "set io voltage failed\n");
		return ret;
	}

	ret = regulator_enable(imx185->core_regulator);
	if (ret) {
		dev_err(imx185->dev, "set core voltage failed\n");
		goto err_disable_io;
	}

	ret = regulator_enable(imx185->analog_regulator);
	if (ret) {
		dev_err(imx185->dev, "set analog voltage failed\n");
		goto err_disable_core;
	}

	return 0;

err_disable_core:
	regulator_disable(imx185->core_regulator);
err_disable_io:
	regulator_disable(imx185->io_regulator);

	return ret;
}

static void imx185_regulators_disable(struct imx185 *imx185)
{
	int ret;

	ret = regulator_disable(imx185->analog_regulator);
	if (ret < 0)
		dev_err(imx185->dev, "analog regulator disable failed\n");

	ret = regulator_disable(imx185->core_regulator);
	if (ret < 0)
		dev_err(imx185->dev, "core regulator disable failed\n");

	ret = regulator_disable(imx185->io_regulator);
	if (ret < 0)
		dev_err(imx185->dev, "io regulator disable failed\n");
}

static int imx185_write_reg_to(struct imx185 *imx185, u16 reg, u8 val, u16 i2c_addr)
{
	int ret;

	ret = msm_cci_ctrl_write(i2c_addr, reg, &val, 1);
	if (ret < 0)
		dev_err(imx185->dev,
			"%s: write reg error %d on addr 0x%x: reg=0x%x, val=0x%x\n",
			__func__, ret, i2c_addr, reg, val);

	return ret;
}

static int imx185_write_reg(struct imx185 *imx185, u16 reg, u8 val)
{
	int ret;
	u16 i2c_addr = 0x34;

	ret = msm_cci_ctrl_write(i2c_addr, reg, &val, 1);
	if (ret < 0)
		dev_err(imx185->dev,
			"%s: write reg error %d on addr 0x%x: reg=0x%x, val=0x%x\n",
			__func__, ret, i2c_addr, reg, val);

	return ret;
}

static int imx185_read_reg(struct imx185 *imx185, u16 reg, u8 *val)
{
	u8 tmpval;
	int ret;
	u16 i2c_addr = imx185->i2c_client->addr;

	ret = msm_cci_ctrl_read(i2c_addr, reg, &tmpval, 1);
	if (ret < 0) {
		dev_err(imx185->dev,
			"%s: read reg error %d on addr 0x%x: reg=0x%x\n",
			__func__, ret, i2c_addr, reg);
		return ret;
	}

	*val = tmpval;

	return 0;
}

static int imx185_set_aec_mode(struct imx185 *imx185, u32 mode)
{
	u8 val;
	int ret;

	ret = imx185_read_reg(imx185, IMX185_AEC_PK_MANUAL, &val);
	if (ret < 0)
		return ret;

	if (mode == V4L2_EXPOSURE_AUTO)
		val &= ~IMX185_AEC_MANUAL_ENABLE;
	else /* V4L2_EXPOSURE_MANUAL */
		val |= IMX185_AEC_MANUAL_ENABLE;

	return imx185_write_reg(imx185, IMX185_AEC_PK_MANUAL, val);
}

static int imx185_set_agc_mode(struct imx185 *imx185, u32 enable)
{
	u8 val;
	int ret;

	ret = imx185_read_reg(imx185, IMX185_AEC_PK_MANUAL, &val);
	if (ret < 0)
		return ret;

	if (enable)
		val &= ~IMX185_AGC_MANUAL_ENABLE;
	else
		val |= IMX185_AGC_MANUAL_ENABLE;

	return imx185_write_reg(imx185, IMX185_AEC_PK_MANUAL, val);
}

static int imx185_set_register_array(struct imx185 *imx185,
				     struct reg_value *settings,
				     u32 num_settings)
{
	u16 reg;
	u8 val;
	u32 i;
	int ret;

	for (i = 0; i < num_settings; ++i, ++settings) {
		reg = settings->reg;
		val = settings->val;

		ret = imx185_write_reg(imx185, reg, val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int imx185_init(struct imx185 *imx185)
{
	struct reg_value *settings;
	u32 num_settings;

	settings = imx185_global_init_setting;
	num_settings = ARRAY_SIZE(imx185_global_init_setting);

	return imx185_set_register_array(imx185, settings, num_settings);
}

static int imx185_change_mode(struct imx185 *imx185, enum imx185_mode mode)
{
	struct reg_value *settings;
	u32 num_settings;

	settings = imx185_mode_info_data[mode].data;
	num_settings = imx185_mode_info_data[mode].data_size;

	return imx185_set_register_array(imx185, settings, num_settings);
}

static int imx185_set_power_on(struct imx185 *imx185)
{
	int ret;

	clk_set_rate(imx185->xclk, imx185->xclk_freq);

	ret = clk_prepare_enable(imx185->xclk);
	if (ret < 0) {
		dev_err(imx185->dev, "clk prepare enable failed\n");
		return ret;
	}

	ret = imx185_regulators_enable(imx185);
	if (ret < 0) {
		clk_disable_unprepare(imx185->xclk);
		return ret;
	}

	usleep_range(5000, 15000);
	gpiod_set_value_cansleep(imx185->enable_gpio, 0);

	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(imx185->rst_gpio, 0);

	msleep(20);

	return ret;
}

static void imx185_set_power_off(struct imx185 *imx185)
{
	gpiod_set_value_cansleep(imx185->rst_gpio, 0);//TO TEST
	gpiod_set_value_cansleep(imx185->enable_gpio, 0);//
	imx185_regulators_disable(imx185);
	clk_disable_unprepare(imx185->xclk);
}

static int imx185_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx185 *imx185 = to_imx185(sd);
	int ret = 0;

	mutex_lock(&imx185->power_lock);

	if (on) {
		ret = msm_cci_ctrl_init();
		if (ret < 0)
			goto exit;
	}

	if (imx185->power == !on) {
		/* Power state changes. */
		if (on) {
			mutex_lock(&imx185_lock);

			ret = imx185_set_power_on(imx185);
			if (ret < 0) {
				dev_err(imx185->dev, "could not set power %s\n",
					on ? "on" : "off");
				goto exit;
			}

//don't change sensor i2c address for this time

//			ret = imx185_write_reg_to(imx185, 0x0109,
//					       imx185->i2c_client->addr, 0xc0);


			if (ret < 0) {
				dev_err(imx185->dev,
					"could not change i2c address\n");
				imx185_set_power_off(imx185);
				mutex_unlock(&imx185_lock);
				goto exit;
			}

			mutex_unlock(&imx185_lock);
/*
			ret = imx185_init(imx185);
			if (ret < 0) {
				dev_err(imx185->dev,
					"could not set init registers\n");
				imx185_set_power_off(imx185);
				goto exit;
			}
*/
			ret = imx185_write_reg(imx185, IMX185_SYSTEM_CTRL0,
					       IMX185_SYSTEM_CTRL0_STOP);
			if (ret < 0) {
				imx185_set_power_off(imx185);
				goto exit;
			}
		} else {
			imx185_set_power_off(imx185);
		}

		/* Update the power state. */
		imx185->power = on ? true : false;
	}

exit:
	if (!on)
		msm_cci_ctrl_release();

	mutex_unlock(&imx185->power_lock);

	return ret;
}


static int imx185_set_saturation(struct imx185 *imx185, s32 value)
{
	u32 reg_value = (value * 0x10) + 0x40;
	int ret;

	ret = imx185_write_reg(imx185, IMX185_SDE_SAT_U, reg_value);
	if (ret < 0)
		return ret;

	ret = imx185_write_reg(imx185, IMX185_SDE_SAT_V, reg_value);

	return ret;
}

static int imx185_set_hflip(struct imx185 *imx185, s32 value)
{
	u8 val;
	int ret;

	ret = imx185_read_reg(imx185, IMX185_TIMING_TC_REG21, &val);
	if (ret < 0)
		return ret;

	if (value == 0)
		val &= ~(IMX185_SENSOR_MIRROR);
	else
		val |= (IMX185_SENSOR_MIRROR);

	return imx185_write_reg(imx185, IMX185_TIMING_TC_REG21, val);
}

static int imx185_set_vflip(struct imx185 *imx185, s32 value)
{
	u8 val;
	int ret;

	ret = imx185_read_reg(imx185, IMX185_TIMING_TC_REG20, &val);
	if (ret < 0)
		return ret;

	if (value == 0)
		val |= (IMX185_SENSOR_VFLIP | IMX185_ISP_VFLIP);
	else
		val &= ~(IMX185_SENSOR_VFLIP | IMX185_ISP_VFLIP);

	return imx185_write_reg(imx185, IMX185_TIMING_TC_REG20, val);
}

static int imx185_set_test_pattern(struct imx185 *imx185, s32 value)
{
	u8 val;
	int ret;

	ret = imx185_read_reg(imx185, IMX185_PRE_ISP_TEST_SETTING_1, &val);
	if (ret < 0)
		return ret;

	if (value) {
		val &= ~IMX185_SET_TEST_PATTERN(IMX185_TEST_PATTERN_MASK);
		val |= IMX185_SET_TEST_PATTERN(value - 1);
		val |= IMX185_TEST_PATTERN_ENABLE;
	} else {
		val &= ~IMX185_TEST_PATTERN_ENABLE;
	}

	return imx185_write_reg(imx185, IMX185_PRE_ISP_TEST_SETTING_1, val);
}

static const char * const imx185_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
	"Pseudo-Random Data",
	"Color Square",
	"Black Image",
};

static int imx185_set_awb(struct imx185 *imx185, s32 enable_auto)
{
	u8 val;
	int ret;

	ret = imx185_read_reg(imx185, IMX185_AWB_MANUAL_CONTROL, &val);
	if (ret < 0)
		return ret;

	if (enable_auto)
		val &= ~IMX185_AWB_MANUAL_ENABLE;
	else
		val |= IMX185_AWB_MANUAL_ENABLE;

	return imx185_write_reg(imx185, IMX185_AWB_MANUAL_CONTROL, val);
}

static int imx185_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx185 *imx185 = container_of(ctrl->handler,
					     struct imx185, ctrls);

	int ret = -EINVAL;

	return 0;	
	mutex_lock(&imx185->power_lock);
	if (imx185->power == 0) {
		mutex_unlock(&imx185->power_lock);
		return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_SATURATION:
		ret = imx185_set_saturation(imx185, ctrl->val);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = imx185_set_awb(imx185, ctrl->val);
		break;
	case V4L2_CID_AUTOGAIN:
		ret = imx185_set_agc_mode(imx185, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ret = imx185_set_aec_mode(imx185, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx185_set_test_pattern(imx185, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = imx185_set_hflip(imx185, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = imx185_set_vflip(imx185, ctrl->val);
		break;
	}

	mutex_unlock(&imx185->power_lock);

	return ret;
}

static struct v4l2_ctrl_ops imx185_ctrl_ops = {
	.s_ctrl = imx185_s_ctrl,
};

static int imx185_entity_init_cfg(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_subdev_format fmt = { 0 };
	struct imx185 *imx185 = to_imx185(subdev);

	dev_err(imx185->dev, "%s: Enter\n", __func__);


	fmt.which = cfg ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.width = 1920;
	fmt.format.height = 1080;

	v4l2_subdev_call(subdev, pad, set_fmt, cfg, &fmt);

	return 0;
}

static int imx185_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx185 *imx185 = to_imx185(sd);

	if (code->index > 0)
		return -EINVAL;

	code->code = imx185->fmt.code;

	return 0;
}

static int imx185_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > IMX185_MODE_MAX)
		return -EINVAL;

	fse->min_width = imx185_mode_info_data[fse->index].width;
	fse->max_width = imx185_mode_info_data[fse->index].width;
	fse->min_height = imx185_mode_info_data[fse->index].height;
	fse->max_height = imx185_mode_info_data[fse->index].height;

	return 0;
}

static struct v4l2_mbus_framefmt *
__imx185_get_pad_format(struct imx185 *imx185,
			struct v4l2_subdev_pad_config *cfg,
			unsigned int pad,
			enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&imx185->sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &imx185->fmt;
	default:
		return NULL;
	}
}

static int imx185_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *format)
{
	struct imx185 *imx185 = to_imx185(sd);

	format->format = *__imx185_get_pad_format(imx185, cfg, format->pad,
						  format->which);
	return 0;
}

static struct v4l2_rect *
__imx185_get_pad_crop(struct imx185 *imx185, struct v4l2_subdev_pad_config *cfg,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&imx185->sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &imx185->crop;
	default:
		return NULL;
	}
}

static enum imx185_mode imx185_find_nearest_mode(struct imx185 *imx185,
						 int width, int height)
{
	int i;

	for (i = IMX185_MODE_MAX; i >= 0; i--) {
		if (imx185_mode_info_data[i].width <= width &&
		    imx185_mode_info_data[i].height <= height)
			break;
	}

	if (i < 0)
		i = 0;

	return (enum imx185_mode)i;
}

static int imx185_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *format)
{
	struct imx185 *imx185 = to_imx185(sd);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_rect *__crop;
	enum imx185_mode new_mode;

	__crop = __imx185_get_pad_crop(imx185, cfg, format->pad,
			format->which);

	new_mode = imx185_find_nearest_mode(imx185,
			format->format.width, format->format.height);
	
	//printk("set format,new mode index:%d",new_mode);

	
	__crop->width = imx185_mode_info_data[new_mode].width;
	__crop->height = imx185_mode_info_data[new_mode].height;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		imx185->current_mode = new_mode;

	__format = __imx185_get_pad_format(imx185, cfg, format->pad,
			format->which);
	__format->width = __crop->width;
	__format->height = __crop->height;
	__format->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	__format->field = V4L2_FIELD_NONE;
	__format->colorspace = V4L2_COLORSPACE_SRGB;

	format->format = *__format;

	return 0;
}

static int imx185_get_selection(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_selection *sel)
{
	struct imx185 *imx185 = to_imx185(sd);

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sel->r = *__imx185_get_pad_crop(imx185, cfg, sel->pad,
					sel->which);
	return 0;
}

static int imx185_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct imx185 *imx185 = to_imx185(subdev);
	int ret;

	if (enable) {
		ret = imx185_change_mode(imx185, imx185->current_mode);//Do the non-match test
		if (ret < 0) {
			dev_err(imx185->dev, "could not set mode %d\n",
				imx185->current_mode);
			return ret;
		}else
			{
			printk("new mode index:%d",imx185->current_mode);
		}

/*		
		ret = v4l2_ctrl_handler_setup(&imx185->ctrls);
		if (ret < 0) {
			dev_err(imx185->dev, "could not sync v4l2 controls\n");
			return ret;
		}
//		ret = imx185_write_reg(imx185, IMX185_SYSTEM_CTRL0,
//				       IMX185_SYSTEM_CTRL0_START);
*/
		ret = imx185_write_reg(imx185, IMX185_SYSTEM_CTRL0,
				       IMX185_SYSTEM_CTRL0_START);

		if (ret < 0)
			return ret;
	} else {
		ret = imx185_write_reg(imx185, IMX185_SYSTEM_CTRL0,
				       IMX185_SYSTEM_CTRL0_STOP);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static struct v4l2_subdev_core_ops imx185_core_ops = {
	.s_power = imx185_s_power,
};

static struct v4l2_subdev_video_ops imx185_video_ops = {
	.s_stream = imx185_s_stream,
};

static struct v4l2_subdev_pad_ops imx185_subdev_pad_ops = {
	.enum_mbus_code = imx185_enum_mbus_code,
	.enum_frame_size = imx185_enum_frame_size,
	.get_fmt = imx185_get_format,
	.set_fmt = imx185_set_format,
	.get_selection = imx185_get_selection,
};

static struct v4l2_subdev_ops imx185_subdev_ops = {
	.core = &imx185_core_ops,
	.video = &imx185_video_ops,
	.pad = &imx185_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx185_subdev_internal_ops = {
};

static int imx185_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *endpoint;
	struct imx185 *imx185;
	u8 chip_id_high, chip_id_low;
	int ret;

	dev_dbg(dev, "%s: Enter, i2c addr = 0x%x\n", __func__, client->addr);

	client->addr = 0x34;
	
	imx185 = devm_kzalloc(dev, sizeof(struct imx185), GFP_KERNEL);
	if (!imx185)
		return -ENOMEM;

	imx185->i2c_client = client;
	imx185->dev = dev;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_of_parse_endpoint(endpoint, &imx185->ep);
	if (ret < 0) {
		dev_err(dev, "parsing endpoint node failed\n");
		return ret;
	}
	if (imx185->ep.bus_type != V4L2_MBUS_CSI2) {
		dev_err(dev, "invalid bus type, must be CSI2\n");
		of_node_put(endpoint);
		return -EINVAL;
	}
	of_node_put(endpoint);

	/* get system clock (xclk) */
	imx185->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(imx185->xclk)) {
		dev_err(dev, "could not get xclk");
		return PTR_ERR(imx185->xclk);
	}

	ret = of_property_read_u32(dev->of_node, "clock-frequency",
				    &imx185->xclk_freq);
	if (ret) {
		dev_err(dev, "could not get xclk frequency\n");
		return ret;
	}

	imx185->io_regulator = devm_regulator_get(dev, "vdddo");
	if (IS_ERR(imx185->io_regulator)) {
		dev_err(dev, "cannot get io regulator\n");
		return PTR_ERR(imx185->io_regulator);
	}

	ret = regulator_set_voltage(imx185->io_regulator,
				    IMX185_VOLTAGE_DIGITAL_IO,
				    IMX185_VOLTAGE_DIGITAL_IO);
	if (ret < 0) {
		dev_err(dev, "cannot set io voltage\n");
		return ret;
	}

	imx185->core_regulator = devm_regulator_get(dev, "vddd");
	if (IS_ERR(imx185->core_regulator)) {
		dev_err(dev, "cannot get core regulator\n");
		return PTR_ERR(imx185->core_regulator);
	}

	ret = regulator_set_voltage(imx185->core_regulator,
				    IMX185_VOLTAGE_DIGITAL_CORE,
				    IMX185_VOLTAGE_DIGITAL_CORE);
	if (ret < 0) {
		dev_err(dev, "cannot set core voltage\n");
		return ret;
	}

	imx185->analog_regulator = devm_regulator_get(dev, "vdda");
	if (IS_ERR(imx185->analog_regulator)) {
		dev_err(dev, "cannot get analog regulator\n");
		return PTR_ERR(imx185->analog_regulator);
	}

	ret = regulator_set_voltage(imx185->analog_regulator,
				    IMX185_VOLTAGE_ANALOG,
				    IMX185_VOLTAGE_ANALOG);
	if (ret < 0) {
		dev_err(dev, "cannot set analog voltage\n");
		return ret;
	}

	imx185->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(imx185->enable_gpio)) {
		dev_err(dev, "cannot get enable gpio\n");
		return PTR_ERR(imx185->enable_gpio);
	}

	imx185->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(imx185->rst_gpio)) {
		dev_err(dev, "cannot get reset gpio\n");
		return PTR_ERR(imx185->rst_gpio);
	}

	mutex_init(&imx185->power_lock);

	v4l2_ctrl_handler_init(&imx185->ctrls, 7);
	imx185->saturation = v4l2_ctrl_new_std(&imx185->ctrls, &imx185_ctrl_ops,
				V4L2_CID_SATURATION, -4, 4, 1, 0);
	imx185->hflip = v4l2_ctrl_new_std(&imx185->ctrls, &imx185_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);
	imx185->vflip = v4l2_ctrl_new_std(&imx185->ctrls, &imx185_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	imx185->autogain = v4l2_ctrl_new_std(&imx185->ctrls, &imx185_ctrl_ops,
				V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	imx185->autoexposure = v4l2_ctrl_new_std_menu(&imx185->ctrls,
				&imx185_ctrl_ops, V4L2_CID_EXPOSURE_AUTO,
				V4L2_EXPOSURE_MANUAL, 0, V4L2_EXPOSURE_AUTO);
	imx185->awb = v4l2_ctrl_new_std(&imx185->ctrls, &imx185_ctrl_ops,
				V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);
	imx185->pattern = v4l2_ctrl_new_std_menu_items(&imx185->ctrls,
				&imx185_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(imx185_test_pattern_menu) - 1, 0, 0,
				imx185_test_pattern_menu);

	imx185->sd.ctrl_handler = &imx185->ctrls;

	if (imx185->ctrls.error) {
		dev_err(dev, "%s: control initialization error %d\n",
		       __func__, imx185->ctrls.error);
		ret = imx185->ctrls.error;
		goto free_ctrl;
	}

	v4l2_i2c_subdev_init(&imx185->sd, client, &imx185_subdev_ops);
	imx185->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx185->pad.flags = MEDIA_PAD_FL_SOURCE;
	imx185->sd.internal_ops = &imx185_subdev_internal_ops;

	ret = media_entity_init(&imx185->sd.entity, 1, &imx185->pad, 0);
	if (ret < 0) {
		dev_err(dev, "could not register media entity\n");
		goto free_ctrl;
	}

	imx185->sd.dev = &client->dev;
	ret = v4l2_async_register_subdev(&imx185->sd);
	if (ret < 0) {
		dev_err(dev, "could not register v4l2 device\n");
		goto free_entity;
	}

	ret = imx185_s_power(&imx185->sd, true);
	if (ret < 0) {
		dev_err(dev, "could not power up IMX185\n");
		goto unregister_subdev;
	}

	ret = imx185_read_reg(imx185, IMX185_CHIP_ID_LOW, &chip_id_high);
	if (ret < 0 || chip_id_high != IMX185_CHIP_ID_LOW_BYTE) {
		dev_err(dev, "could not read ID high\n");
		ret = -ENODEV;
		goto power_down;
	}
	ret = imx185_read_reg(imx185, IMX185_CHIP_ID_HIGH, &chip_id_low);
	if (ret < 0 || chip_id_low != IMX185_CHIP_ID_HIGH_BYTE) {
		dev_err(dev, "could not read ID low\n");
		ret = -ENODEV;
		goto power_down;
	}

	dev_info(dev, "Sony IMX185 detected at address 0x%x,ID:0x%x\n", client->addr,chip_id_high<<8|chip_id_low);

	imx185_s_power(&imx185->sd, false);

	imx185_entity_init_cfg(&imx185->sd, NULL);

	return 0;

power_down:
	imx185_s_power(&imx185->sd, false);
unregister_subdev:
	v4l2_async_unregister_subdev(&imx185->sd);
free_entity:
	media_entity_cleanup(&imx185->sd.entity);
free_ctrl:
	v4l2_ctrl_handler_free(&imx185->ctrls);
	mutex_destroy(&imx185->power_lock);

	return ret;
}


static int imx185_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx185 *imx185 = to_imx185(sd);

	v4l2_async_unregister_subdev(&imx185->sd);
	media_entity_cleanup(&imx185->sd.entity);
	v4l2_ctrl_handler_free(&imx185->ctrls);
	mutex_destroy(&imx185->power_lock);

	return 0;
}


static const struct i2c_device_id imx185_id[] = {
	{ "imx185", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, imx185_id);

static const struct of_device_id imx185_of_match[] = {
	{ .compatible = "sony,imx185" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx185_of_match);

static struct i2c_driver imx185_i2c_driver = {
	.driver = {
		.of_match_table = of_match_ptr(imx185_of_match),
		.name  = "imx185",
	},
	.probe  = imx185_probe,
	.remove = imx185_remove,
	.id_table = imx185_id,
};

module_i2c_driver(imx185_i2c_driver);

MODULE_DESCRIPTION("Sony IMX185 Camera Driver");
MODULE_AUTHOR("Todor Tomov <todor.tomov@linaro.org>");
MODULE_LICENSE("GPL v2");
