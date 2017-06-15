/*
 * TI LP855x Backlight Driver
 *
 *			Copyright (C) 2011 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_data/lp855x.h>
#include <linux/pwm.h>

#include <linux/htc_flashlight.h>

#define LP855X_BRIGHTNESS_CTRL		0x00
#define LP855X_DEVICE_CTRL		0x01
#define LP855X_EEPROM_START		0xA0
#define LP855X_EEPROM_END		0xA7
#define LP8556_EPROM_START		0xA0
#define LP8556_EPROM_END		0xAF

#define LP8557_BL_CMD			0x00
#define LP8557_BL_MASK			0x01
#define LP8557_BL_ON			0x01
#define LP8557_BL_OFF			0x00
#define LP8557_BRIGHTNESS_CTRL_LSB	0x03
#define LP8557_BRIGHTNESS_CTRL_MSB	0x04
#define LP8557_CONFIG			0x10
#define LP8555_EPROM_START		0x10
#define LP8555_EPROM_END		0x7A
#define LP8557_EPROM_START		0x10
#define LP8557_EPROM_END		0x1E

#define DEFAULT_BL_NAME		"lcd-backlight"
#define MAX_BRIGHTNESS_8BIT	255
#define MAX_BRIGHTNESS_12BIT	4095

enum lp855x_brightness_ctrl_mode {
	PWM_BASED = 1,
	REGISTER_BASED,
};

struct lp855x;

struct lp855x_device_config {
	int (*pre_init_device)(struct lp855x *);
	u8 reg_brightness_msb;
	u8 reg_brightness_lsb;
	int max_brightness;
	u8 reg_devicectrl;
	int (*post_init_device)(struct lp855x *);
};

struct lp855x {
	const char *chipname;
	enum lp855x_chip_id chip_id;
	enum lp855x_brightness_ctrl_mode mode;
	struct lp855x_device_config *cfg;
	struct i2c_client *client;
	struct backlight_device *bl;
	struct device *dev;
	struct lp855x_platform_data *pdata;
	struct pwm_device *pwm;

	
	bool prev_state;
	struct delayed_work flash_work;
	bool flash_enabled;
	struct htc_flashlight_dev flash_dev;
	int torch_brightness;
};

static int lp855x_write_byte(struct lp855x *lp, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(lp->client, reg, data);
}

static int lp855x_update_bit(struct lp855x *lp, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	ret = i2c_smbus_read_byte_data(lp->client, reg);
	if (ret < 0) {
		dev_err(lp->dev, "failed to read 0x%.2x\n", reg);
		return ret;
	}

	tmp = (u8)ret;
	tmp &= ~mask;
	tmp |= data & mask;

	return lp855x_write_byte(lp, reg, tmp);
}

static int lp855x_bl_update_brightness(struct lp855x *lp, int val)
{
	int ret = 0;

	if (lp->cfg->max_brightness == MAX_BRIGHTNESS_12BIT) {
		ret = lp855x_write_byte(lp, lp->cfg->reg_brightness_lsb, (val & 0xf) << 4);
		val = val >> 4;
	}
	if (!ret)
		ret = lp855x_write_byte(lp, lp->cfg->reg_brightness_msb, val);
	return ret;
}

static bool lp855x_is_valid_rom_area(struct lp855x *lp, u8 addr)
{
	u8 start, end;

	switch (lp->chip_id) {
	case LP8550:
	case LP8551:
	case LP8552:
	case LP8553:
		start = LP855X_EEPROM_START;
		end = LP855X_EEPROM_END;
		break;
	case LP8556:
		start = LP8556_EPROM_START;
		end = LP8556_EPROM_END;
		break;
	case LP8555:
		start = LP8555_EPROM_START;
		end = LP8555_EPROM_END;
		break;
	case LP8557:
		start = LP8557_EPROM_START;
		end = LP8557_EPROM_END;
		break;
	default:
		return false;
	}

	return addr >= start && addr <= end;
}

static int lp8557_bl_off(struct lp855x *lp)
{
	
	return lp855x_update_bit(lp, LP8557_BL_CMD, LP8557_BL_MASK,
				LP8557_BL_OFF);
}

static int lp8557_bl_on(struct lp855x *lp)
{
	
	return lp855x_update_bit(lp, LP8557_BL_CMD, LP8557_BL_MASK,
				LP8557_BL_ON);
}

static struct lp855x_device_config lp855x_dev_cfg = {
	.reg_brightness_msb = LP855X_BRIGHTNESS_CTRL,
	.max_brightness     = MAX_BRIGHTNESS_8BIT,
	.reg_devicectrl     = LP855X_DEVICE_CTRL,
};

static struct lp855x_device_config lp8557_dev_cfg = {
	.reg_brightness_msb = LP8557_BRIGHTNESS_CTRL_MSB,
	.reg_brightness_lsb = LP8557_BRIGHTNESS_CTRL_LSB,
	.max_brightness     = MAX_BRIGHTNESS_12BIT,
	.reg_devicectrl     = LP8557_CONFIG,
	.pre_init_device = lp8557_bl_off,
	.post_init_device = lp8557_bl_on,
};

static int lp855x_configure(struct lp855x *lp)
{
	u8 val, addr;
	int i, ret;
	struct lp855x_platform_data *pd = lp->pdata;

	switch (lp->chip_id) {
	case LP8550:
	case LP8551:
	case LP8552:
	case LP8553:
	case LP8556:
		lp->cfg = &lp855x_dev_cfg;
		break;
	case LP8555:
	case LP8557:
		lp->cfg = &lp8557_dev_cfg;
		break;
	default:
		return -EINVAL;
	}

	if (!pd->boot_on && lp->cfg->pre_init_device) {
		ret = lp->cfg->pre_init_device(lp);
		if (ret) {
			dev_err(lp->dev, "pre init device err: %d\n", ret);
			goto err;
		}
	}

	ret = lp855x_bl_update_brightness(lp, pd->initial_brightness);
	if (ret)
		goto err;

	val = pd->device_control;
	ret = lp855x_write_byte(lp, lp->cfg->reg_devicectrl, val);
	if (ret)
		goto err;

	if (pd->size_program > 0) {
		for (i = 0; i < pd->size_program; i++) {
			addr = pd->rom_data[i].addr;
			val = pd->rom_data[i].val;
			if (!lp855x_is_valid_rom_area(lp, addr))
				continue;

			ret = lp855x_write_byte(lp, addr, val);
			if (ret)
				goto err;
		}
	}

	if (!pd->boot_on && lp->cfg->post_init_device) {
		ret = lp->cfg->post_init_device(lp);
		if (ret) {
			dev_err(lp->dev, "post init device err: %d\n", ret);
			goto err;
		}
	}

	return 0;

err:
	return ret;
}

static void lp855x_pwm_ctrl(struct lp855x *lp, int br, int max_br)
{
	unsigned int period = lp->pdata->period_ns;
	unsigned int duty = br * period / max_br;
	struct pwm_device *pwm;

	
	if (!lp->pwm) {
		pwm = devm_pwm_get(lp->dev, lp->chipname);
		if (IS_ERR(pwm))
			return;

		lp->pwm = pwm;
	}

	pwm_config(lp->pwm, duty, period);
	if (duty)
		pwm_enable(lp->pwm);
	else
		pwm_disable(lp->pwm);
}

static int lp855x_bl_update_status(struct backlight_device *bl)
{
	struct lp855x *lp = bl_get_data(bl);
	bool new_state;
	int ret = 0;
	int brightness = bl->props.brightness;

	if (lp->flash_enabled)
		return -EBUSY;

	if (bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;
	new_state = !!brightness;

	if (lp->mode == PWM_BASED) {
		int max_br = bl->props.max_brightness;

		lp855x_pwm_ctrl(lp, brightness, max_br);

	} else if (lp->mode == REGISTER_BASED) {
		lp855x_bl_update_brightness(lp, brightness);
	}

	if (lp->prev_state != new_state) {
		if (lp->chip_id == LP8557) {
			if (new_state)
		                ret = lp8557_bl_on(lp);
			else
		                ret = lp8557_bl_off(lp);
		}
		if (ret)
			dev_err(lp->dev, "control lp8557 COMMAND.ON=%d failed", new_state);
		lp->prev_state = new_state;
	}

	return 0;
}

static const struct backlight_ops lp855x_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = lp855x_bl_update_status,
};

static int lp855x_backlight_register(struct lp855x *lp)
{
	struct backlight_device *bl;
	struct backlight_properties props;
	struct lp855x_platform_data *pdata = lp->pdata;
	const char *name = pdata->name ? : DEFAULT_BL_NAME;

	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = lp->cfg->max_brightness;

	if (pdata->initial_brightness > props.max_brightness)
		pdata->initial_brightness = props.max_brightness;

	props.brightness = pdata->initial_brightness;

	bl = devm_backlight_device_register(lp->dev, name, lp->dev, lp,
				       &lp855x_bl_ops, &props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	lp->bl = bl;

	return 0;
}

static int lp855x_flash_en_locked(struct lp855x *lp, int en, int duration, int level)
{
	int rc = 0;

	if (en) {
		if (lp->flash_enabled) {
			pr_info("%s: already enabled\n", __func__);
			rc = -EBUSY;
			goto exit;
		}

		if (level <= 0 || level > lp->cfg->max_brightness)
			level = lp->cfg->max_brightness;

		if (duration < 10)
			duration = 10;
		schedule_delayed_work(&lp->flash_work, msecs_to_jiffies(duration));
	} else {
		level = lp->bl->props.brightness;
	}
	pr_info("%s: (%d, %d) [level=%d]\n",
		__func__, en, duration, level);

	lp855x_bl_update_brightness(lp, level);

	lp->flash_enabled = en;
exit:
	pr_info("%s: (%d, %d) done, rc=%d\n", __func__, en, duration, rc);

	return rc;
}

static int lp855x_flash_en(struct lp855x *lp, int en, int duration, int level)
{
	int rc = 0;

	if (!en) {
		cancel_delayed_work_sync(&lp->flash_work);
	}

	mutex_lock(&lp->bl->update_lock);
	rc = lp855x_flash_en_locked(lp, en, duration, level);
	mutex_unlock(&lp->bl->update_lock);

	return rc;
}

static void lp855x_flash_off_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct lp855x *lp = container_of(dwork, struct lp855x, flash_work);

	mutex_lock(&lp->bl->update_lock);
	lp855x_flash_en_locked(lp, 0, 0, 0);
	mutex_unlock(&lp->bl->update_lock);
}

static int lp855x_flash_mode(struct htc_flashlight_dev *fl_dev, int mode1, int mode2)
{
	const int max_flash_ms = 500;
	struct lp855x *lp = container_of(fl_dev, struct lp855x, flash_dev);

	return lp855x_flash_en(lp, mode1, max_flash_ms, lp->cfg->max_brightness);
}

static int lp855x_torch_mode(struct htc_flashlight_dev *fl_dev, int mode1, int mode2)
{
	const int max_torch_ms = 10000;
	struct lp855x *lp = container_of(fl_dev, struct lp855x, flash_dev);

	return lp855x_flash_en(lp, mode1, max_torch_ms, lp->torch_brightness);
}

struct lp855x *g_lp; 
static void lp855x_strobe(int en)
{
	struct lp855x *lp = g_lp;

	if (!lp)
		return;

	pr_info("%s(%d)\n", __func__, en);
	if (en)
		lp8557_bl_on(lp);
	else
		lp8557_bl_off(lp);
}


static ssize_t lp855x_get_chip_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lp855x *lp = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", lp->chipname);
}

static ssize_t lp855x_get_bl_ctl_mode(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct lp855x *lp = dev_get_drvdata(dev);
	char *strmode = NULL;

	if (lp->mode == PWM_BASED)
		strmode = "pwm based";
	else if (lp->mode == REGISTER_BASED)
		strmode = "register based";

	return scnprintf(buf, PAGE_SIZE, "%s\n", strmode);
}

static ssize_t lp855x_get_flash_en(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lp855x *lp = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", lp->flash_enabled);
}

static ssize_t lp855x_set_flash_en(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct lp855x *lp = dev_get_drvdata(dev);
	int data = 0, level = 0, rc = 0;

	if (sscanf(buf, "%d %d", &data, &level) < 1)
		return -EINVAL;

	rc = lp855x_flash_en(lp, !!data, data, level);
	if (rc)
		return rc;

	return count;
}

static DEVICE_ATTR(chip_id, S_IRUGO, lp855x_get_chip_id, NULL);
static DEVICE_ATTR(bl_ctl_mode, S_IRUGO, lp855x_get_bl_ctl_mode, NULL);

static struct attribute *lp855x_attributes[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_bl_ctl_mode.attr,
	NULL,
};

static const struct attribute_group lp855x_attr_group = {
	.attrs = lp855x_attributes,
};

static DEVICE_ATTR(flash_en, (S_IRUGO | S_IWUSR | S_IWGRP),
	lp855x_get_flash_en, lp855x_set_flash_en);
static struct attribute *lp855x_flash_attributes[] = {
	&dev_attr_flash_en.attr,
	NULL,
};

static const struct attribute_group lp855x_flash_attr_group = {
	.attrs = lp855x_flash_attributes,
};

#ifdef CONFIG_OF
static int lp855x_parse_dt(struct device *dev, struct device_node *node)
{
	struct lp855x_platform_data *pdata;
	int rom_length;

	if (!node) {
		dev_err(dev, "no platform data\n");
		return -EINVAL;
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	of_property_read_string(node, "bl-name", &pdata->name);
	of_property_read_u8(node, "dev-ctrl", &pdata->device_control);
	of_property_read_u32(node, "init-brt", &pdata->initial_brightness);
	of_property_read_u32(node, "pwm-period", &pdata->period_ns);
	pdata->boot_on = of_property_read_bool(node, "boot-on");
	of_property_read_u32(node, "torch-brt", &pdata->torch_brightness);
	pdata->use_htc_strobe = of_property_read_bool(node, "htc,flashlight-strobe");

	
	rom_length = of_get_child_count(node);
	if (rom_length > 0) {
		struct lp855x_rom_data *rom;
		struct device_node *child;
		int i = 0;

		rom = devm_kzalloc(dev, sizeof(*rom) * rom_length, GFP_KERNEL);
		if (!rom)
			return -ENOMEM;

		for_each_child_of_node(node, child) {
			of_property_read_u8(child, "rom-addr", &rom[i].addr);
			of_property_read_u8(child, "rom-val", &rom[i].val);
			i++;
		}

		pdata->size_program = rom_length;
		pdata->rom_data = &rom[0];
	}

	dev->platform_data = pdata;

	return 0;
}
#else
static int lp855x_parse_dt(struct device *dev, struct device_node *node)
{
	return -EINVAL;
}
#endif

static int lp855x_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct lp855x *lp;
	struct lp855x_platform_data *pdata = dev_get_platdata(&cl->dev);
	struct device_node *node = cl->dev.of_node;
	int ret;

	if (!pdata) {
		ret = lp855x_parse_dt(&cl->dev, node);
		if (ret < 0)
			return ret;

		pdata = dev_get_platdata(&cl->dev);
	}

	if (!i2c_check_functionality(cl->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

	lp = devm_kzalloc(&cl->dev, sizeof(struct lp855x), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	if (pdata->period_ns > 0)
		lp->mode = PWM_BASED;
	else
		lp->mode = REGISTER_BASED;

	lp->client = cl;
	lp->dev = &cl->dev;
	lp->pdata = pdata;
	lp->chipname = id->name;
	lp->chip_id = id->driver_data;
	INIT_DELAYED_WORK(&lp->flash_work, lp855x_flash_off_work);
	lp->torch_brightness = pdata->torch_brightness;
	i2c_set_clientdata(cl, lp);

	ret = lp855x_configure(lp);
	if (ret) {
		dev_err(lp->dev, "device config err: %d", ret);
		return ret;
	}

	ret = lp855x_backlight_register(lp);
	if (ret) {
		dev_err(lp->dev,
			"failed to register backlight. err: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&lp->dev->kobj, &lp855x_attr_group);
	if (ret) {
		dev_err(lp->dev, "failed to register sysfs. err: %d\n", ret);
		return ret;
	}

	lp->flash_dev.id = 1;
	lp->flash_dev.flash_func = lp855x_flash_mode;
	lp->flash_dev.torch_func = lp855x_torch_mode;

	if (register_htc_flashlight(&lp->flash_dev)) {
		pr_err("%s: register htc_flashlight failed!\n", __func__);
		lp->flash_dev.id = -1;
	} else {
		ret = sysfs_create_group(&lp->bl->dev.kobj, &lp855x_flash_attr_group);
		dev_info(lp->dev, "create flash attrs, ret=%d\n", ret);
	}

	if (pdata->use_htc_strobe) {
		g_lp = lp;
		backlight_callback_register(lp855x_strobe);
	}

	backlight_update_status(lp->bl);
	return 0;
}

static int lp855x_remove(struct i2c_client *cl)
{
	struct lp855x *lp = i2c_get_clientdata(cl);

	if (lp->flash_dev.id >= 0) {
		sysfs_remove_group(&lp->bl->dev.kobj, &lp855x_attr_group);
		unregister_htc_flashlight(&lp->flash_dev);
		lp->flash_dev.id = -1;
	}

	lp->bl->props.brightness = 0;
	backlight_update_status(lp->bl);
	sysfs_remove_group(&lp->dev->kobj, &lp855x_attr_group);

	return 0;
}

static const struct of_device_id lp855x_dt_ids[] = {
	{ .compatible = "ti,lp8550", },
	{ .compatible = "ti,lp8551", },
	{ .compatible = "ti,lp8552", },
	{ .compatible = "ti,lp8553", },
	{ .compatible = "ti,lp8555", },
	{ .compatible = "ti,lp8556", },
	{ .compatible = "ti,lp8557", },
	{ }
};
MODULE_DEVICE_TABLE(of, lp855x_dt_ids);

static const struct i2c_device_id lp855x_ids[] = {
	{"lp8550", LP8550},
	{"lp8551", LP8551},
	{"lp8552", LP8552},
	{"lp8553", LP8553},
	{"lp8555", LP8555},
	{"lp8556", LP8556},
	{"lp8557", LP8557},
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp855x_ids);

static struct i2c_driver lp855x_driver = {
	.driver = {
		   .name = "lp855x",
		   .of_match_table = of_match_ptr(lp855x_dt_ids),
		   },
	.probe = lp855x_probe,
	.remove = lp855x_remove,
	.id_table = lp855x_ids,
};

module_i2c_driver(lp855x_driver);

MODULE_DESCRIPTION("Texas Instruments LP855x Backlight driver");
MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_LICENSE("GPL");
