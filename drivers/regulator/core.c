/*
 * core.c  --  Voltage/Current Regulator framework.
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 * Copyright 2008 SlimLogic Ltd.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/async.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/regulator.h>

#include "dummy.h"
#include "internal.h"

#define rdev_crit(rdev, fmt, ...)					\
	pr_crit("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)
#define rdev_err(rdev, fmt, ...)					\
	pr_err("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)
#define rdev_warn(rdev, fmt, ...)					\
	pr_warn("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)
#define rdev_info(rdev, fmt, ...)					\
	pr_info("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)
#define rdev_dbg(rdev, fmt, ...)					\
	pr_debug("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)

static DEFINE_MUTEX(regulator_list_mutex);
static LIST_HEAD(regulator_list);
static LIST_HEAD(regulator_map_list);
static LIST_HEAD(regulator_ena_gpio_list);
static LIST_HEAD(regulator_supply_alias_list);
static bool has_full_constraints;

static struct dentry *debugfs_root;

struct regulator_map {
	struct list_head list;
	const char *dev_name;   
	const char *supply;
	struct regulator_dev *regulator;
};

struct regulator_enable_gpio {
	struct list_head list;
	struct gpio_desc *gpiod;
	u32 enable_count;	
	u32 request_count;	
	unsigned int ena_gpio_invert:1;
};

struct regulator_supply_alias {
	struct list_head list;
	struct device *src_dev;
	const char *src_supply;
	struct device *alias_dev;
	const char *alias_supply;
};

static int _regulator_is_enabled(struct regulator_dev *rdev);
static int _regulator_disable(struct regulator_dev *rdev);
static int _regulator_get_voltage(struct regulator_dev *rdev);
static int _regulator_get_current_limit(struct regulator_dev *rdev);
static unsigned int _regulator_get_mode(struct regulator_dev *rdev);
static int _notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data);
static int _regulator_do_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV);
static struct regulator *create_regulator(struct regulator_dev *rdev,
					  struct device *dev,
					  const char *supply_name);

static const char *rdev_get_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

static bool have_full_constraints(void)
{
	return has_full_constraints || of_have_populated_dt();
}

static struct device_node *of_get_regulator(struct device *dev, const char *supply)
{
	struct device_node *regnode = NULL;
	char prop_name[32]; 

	dev_dbg(dev, "Looking up %s-supply from device tree\n", supply);

	snprintf(prop_name, 32, "%s-supply", supply);
	regnode = of_parse_phandle(dev->of_node, prop_name, 0);

	if (!regnode) {
		dev_dbg(dev, "Looking up %s property in node %s failed",
				prop_name, dev->of_node->full_name);
		return NULL;
	}
	return regnode;
}

static int _regulator_can_change_status(struct regulator_dev *rdev)
{
	if (!rdev->constraints)
		return 0;

	if (rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_STATUS)
		return 1;
	else
		return 0;
}

static int regulator_check_voltage(struct regulator_dev *rdev,
				   int *min_uV, int *max_uV)
{
	BUG_ON(*min_uV > *max_uV);

	if (!rdev->constraints) {
		rdev_err(rdev, "no constraints\n");
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE)) {
		rdev_err(rdev, "operation not allowed\n");
		return -EPERM;
	}

	
	if (*max_uV < rdev->constraints->min_uV ||
	    *min_uV > rdev->constraints->max_uV) {
		rdev_err(rdev, "requested voltage range [%d, %d] does not fit "
			"within constraints: [%d, %d]\n", *min_uV, *max_uV,
			rdev->constraints->min_uV, rdev->constraints->max_uV);
		return -EINVAL;
	}

	if (*max_uV > rdev->constraints->max_uV)
		*max_uV = rdev->constraints->max_uV;
	if (*min_uV < rdev->constraints->min_uV)
		*min_uV = rdev->constraints->min_uV;

	if (*min_uV > *max_uV) {
		rdev_err(rdev, "unsupportable voltage range: %d-%duV\n",
			 *min_uV, *max_uV);
		return -EINVAL;
	}

	return 0;
}

static int regulator_check_consumers(struct regulator_dev *rdev,
				     int *min_uV, int *max_uV)
{
	struct regulator *regulator;
	int init_min_uV = *min_uV;
	int init_max_uV = *max_uV;

	list_for_each_entry(regulator, &rdev->consumer_list, list) {
		if (!regulator->min_uV && !regulator->max_uV)
			continue;

		if (init_max_uV < regulator->min_uV
		    || init_min_uV > regulator->max_uV)
			rdev_err(rdev, "requested voltage range [%d, %d] does "
				"not fit within previously voted range: "
				"[%d, %d]\n", init_min_uV, init_max_uV,
				regulator->min_uV, regulator->max_uV);

		if (*max_uV > regulator->max_uV)
			*max_uV = regulator->max_uV;
		if (*min_uV < regulator->min_uV)
			*min_uV = regulator->min_uV;
	}

	if (*min_uV > *max_uV) {
		rdev_err(rdev, "Restricting voltage, %u-%uuV\n",
			*min_uV, *max_uV);
		return -EINVAL;
	}

	return 0;
}

static int regulator_check_current_limit(struct regulator_dev *rdev,
					int *min_uA, int *max_uA)
{
	BUG_ON(*min_uA > *max_uA);

	if (!rdev->constraints) {
		rdev_err(rdev, "no constraints\n");
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_CURRENT)) {
		rdev_err(rdev, "operation not allowed\n");
		return -EPERM;
	}

	if (*max_uA > rdev->constraints->max_uA)
		*max_uA = rdev->constraints->max_uA;
	if (*min_uA < rdev->constraints->min_uA)
		*min_uA = rdev->constraints->min_uA;

	if (*min_uA > *max_uA) {
		rdev_err(rdev, "unsupportable current range: %d-%duA\n",
			 *min_uA, *max_uA);
		return -EINVAL;
	}

	return 0;
}

static int regulator_mode_constrain(struct regulator_dev *rdev, int *mode)
{
	switch (*mode) {
	case REGULATOR_MODE_FAST:
	case REGULATOR_MODE_NORMAL:
	case REGULATOR_MODE_IDLE:
	case REGULATOR_MODE_STANDBY:
		break;
	default:
		rdev_err(rdev, "invalid mode %x specified\n", *mode);
		return -EINVAL;
	}

	if (!rdev->constraints) {
		rdev_err(rdev, "no constraints\n");
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_MODE)) {
		rdev_err(rdev, "operation not allowed\n");
		return -EPERM;
	}

	while (*mode) {
		if (rdev->constraints->valid_modes_mask & *mode)
			return 0;
		*mode /= 2;
	}

	return -EINVAL;
}

static int regulator_check_drms(struct regulator_dev *rdev)
{
	if (!rdev->constraints) {
		rdev_dbg(rdev, "no constraints\n");
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_DRMS)) {
		rdev_dbg(rdev, "operation not allowed\n");
		return -EPERM;
	}
	return 0;
}

static ssize_t regulator_uV_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&rdev->mutex);
	ret = sprintf(buf, "%d\n", _regulator_get_voltage(rdev));
	mutex_unlock(&rdev->mutex);

	return ret;
}
static DEVICE_ATTR(microvolts, 0444, regulator_uV_show, NULL);

static ssize_t regulator_uA_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", _regulator_get_current_limit(rdev));
}
static DEVICE_ATTR(microamps, 0444, regulator_uA_show, NULL);

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", rdev_get_name(rdev));
}
static DEVICE_ATTR_RO(name);

static ssize_t regulator_print_opmode(char *buf, int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return sprintf(buf, "fast\n");
	case REGULATOR_MODE_NORMAL:
		return sprintf(buf, "normal\n");
	case REGULATOR_MODE_IDLE:
		return sprintf(buf, "idle\n");
	case REGULATOR_MODE_STANDBY:
		return sprintf(buf, "standby\n");
	}
	return sprintf(buf, "unknown\n");
}

static ssize_t regulator_opmode_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf, _regulator_get_mode(rdev));
}
static DEVICE_ATTR(opmode, 0444, regulator_opmode_show, NULL);

static ssize_t regulator_print_state(char *buf, int state)
{
	if (state > 0)
		return sprintf(buf, "enabled\n");
	else if (state == 0)
		return sprintf(buf, "disabled\n");
	else
		return sprintf(buf, "unknown\n");
}

static ssize_t regulator_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&rdev->mutex);
	ret = regulator_print_state(buf, _regulator_is_enabled(rdev));
	mutex_unlock(&rdev->mutex);

	return ret;
}
static DEVICE_ATTR(state, 0444, regulator_state_show, NULL);

static ssize_t regulator_status_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	int status;
	char *label;

	status = rdev->desc->ops->get_status(rdev);
	if (status < 0)
		return status;

	switch (status) {
	case REGULATOR_STATUS_OFF:
		label = "off";
		break;
	case REGULATOR_STATUS_ON:
		label = "on";
		break;
	case REGULATOR_STATUS_ERROR:
		label = "error";
		break;
	case REGULATOR_STATUS_FAST:
		label = "fast";
		break;
	case REGULATOR_STATUS_NORMAL:
		label = "normal";
		break;
	case REGULATOR_STATUS_IDLE:
		label = "idle";
		break;
	case REGULATOR_STATUS_STANDBY:
		label = "standby";
		break;
	case REGULATOR_STATUS_BYPASS:
		label = "bypass";
		break;
	case REGULATOR_STATUS_UNDEFINED:
		label = "undefined";
		break;
	default:
		return -ERANGE;
	}

	return sprintf(buf, "%s\n", label);
}
static DEVICE_ATTR(status, 0444, regulator_status_show, NULL);

static ssize_t regulator_min_uA_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->min_uA);
}
static DEVICE_ATTR(min_microamps, 0444, regulator_min_uA_show, NULL);

static ssize_t regulator_max_uA_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->max_uA);
}
static DEVICE_ATTR(max_microamps, 0444, regulator_max_uA_show, NULL);

static ssize_t regulator_min_uV_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->min_uV);
}
static DEVICE_ATTR(min_microvolts, 0444, regulator_min_uV_show, NULL);

static ssize_t regulator_max_uV_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->max_uV);
}
static DEVICE_ATTR(max_microvolts, 0444, regulator_max_uV_show, NULL);

static ssize_t regulator_total_uA_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct regulator *regulator;
	int uA = 0;

	mutex_lock(&rdev->mutex);
	list_for_each_entry(regulator, &rdev->consumer_list, list)
		uA += regulator->uA_load;
	mutex_unlock(&rdev->mutex);
	return sprintf(buf, "%d\n", uA);
}
static DEVICE_ATTR(requested_microamps, 0444, regulator_total_uA_show, NULL);

static ssize_t num_users_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", rdev->use_count);
}
static DEVICE_ATTR_RO(num_users);

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	switch (rdev->desc->type) {
	case REGULATOR_VOLTAGE:
		return sprintf(buf, "voltage\n");
	case REGULATOR_CURRENT:
		return sprintf(buf, "current\n");
	}
	return sprintf(buf, "unknown\n");
}
static DEVICE_ATTR_RO(type);

static ssize_t regulator_suspend_mem_uV_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_mem.uV);
}
static DEVICE_ATTR(suspend_mem_microvolts, 0444,
		regulator_suspend_mem_uV_show, NULL);

static ssize_t regulator_suspend_disk_uV_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_disk.uV);
}
static DEVICE_ATTR(suspend_disk_microvolts, 0444,
		regulator_suspend_disk_uV_show, NULL);

static ssize_t regulator_suspend_standby_uV_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_standby.uV);
}
static DEVICE_ATTR(suspend_standby_microvolts, 0444,
		regulator_suspend_standby_uV_show, NULL);

static ssize_t regulator_suspend_mem_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_mem.mode);
}
static DEVICE_ATTR(suspend_mem_mode, 0444,
		regulator_suspend_mem_mode_show, NULL);

static ssize_t regulator_suspend_disk_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_disk.mode);
}
static DEVICE_ATTR(suspend_disk_mode, 0444,
		regulator_suspend_disk_mode_show, NULL);

static ssize_t regulator_suspend_standby_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_standby.mode);
}
static DEVICE_ATTR(suspend_standby_mode, 0444,
		regulator_suspend_standby_mode_show, NULL);

static ssize_t regulator_suspend_mem_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_mem.enabled);
}
static DEVICE_ATTR(suspend_mem_state, 0444,
		regulator_suspend_mem_state_show, NULL);

static ssize_t regulator_suspend_disk_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_disk.enabled);
}
static DEVICE_ATTR(suspend_disk_state, 0444,
		regulator_suspend_disk_state_show, NULL);

static ssize_t regulator_suspend_standby_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_standby.enabled);
}
static DEVICE_ATTR(suspend_standby_state, 0444,
		regulator_suspend_standby_state_show, NULL);

static ssize_t regulator_bypass_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	const char *report;
	bool bypass;
	int ret;

	ret = rdev->desc->ops->get_bypass(rdev, &bypass);

	if (ret != 0)
		report = "unknown";
	else if (bypass)
		report = "enabled";
	else
		report = "disabled";

	return sprintf(buf, "%s\n", report);
}
static DEVICE_ATTR(bypass, 0444,
		   regulator_bypass_show, NULL);

static struct attribute *regulator_dev_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_num_users.attr,
	&dev_attr_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(regulator_dev);

static void regulator_dev_release(struct device *dev)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	kfree(rdev);
}

static struct class regulator_class = {
	.name = "regulator",
	.dev_release = regulator_dev_release,
	.dev_groups = regulator_dev_groups,
};

static void drms_uA_update(struct regulator_dev *rdev)
{
	struct regulator *sibling;
	int current_uA = 0, output_uV, input_uV, err;
	unsigned int regulator_curr_mode, mode;

	err = regulator_check_drms(rdev);
	if (err < 0 || !rdev->desc->ops->get_optimum_mode ||
	    (!rdev->desc->ops->get_voltage &&
	     !rdev->desc->ops->get_voltage_sel) ||
	    !rdev->desc->ops->set_mode)
		return;

	
	output_uV = _regulator_get_voltage(rdev);
	if (output_uV <= 0)
		return;

	
	input_uV = 0;
	if (rdev->supply)
		input_uV = regulator_get_voltage(rdev->supply);
	if (input_uV <= 0)
		input_uV = rdev->constraints->input_uV;
	if (input_uV <= 0)
		return;

	
	list_for_each_entry(sibling, &rdev->consumer_list, list)
		current_uA += sibling->uA_load;

	
	mode = rdev->desc->ops->get_optimum_mode(rdev, input_uV,
						  output_uV, current_uA);

	
	err = regulator_mode_constrain(rdev, &mode);
	
	if (rdev->desc->ops->get_mode) {
		regulator_curr_mode = rdev->desc->ops->get_mode(rdev);
		if (regulator_curr_mode == mode)
			return;
	} else
		return;

	if (err == 0)
		rdev->desc->ops->set_mode(rdev, mode);
}

static int suspend_set_state(struct regulator_dev *rdev,
	struct regulator_state *rstate)
{
	int ret = 0;

	if (!rstate->enabled && !rstate->disabled) {
		if (rdev->desc->ops->set_suspend_voltage ||
		    rdev->desc->ops->set_suspend_mode)
			rdev_warn(rdev, "No configuration\n");
		return 0;
	}

	if (rstate->enabled && rstate->disabled) {
		rdev_err(rdev, "invalid configuration\n");
		return -EINVAL;
	}

	if (rstate->enabled && rdev->desc->ops->set_suspend_enable)
		ret = rdev->desc->ops->set_suspend_enable(rdev);
	else if (rstate->disabled && rdev->desc->ops->set_suspend_disable)
		ret = rdev->desc->ops->set_suspend_disable(rdev);
	else 
		ret = 0;

	if (ret < 0) {
		rdev_err(rdev, "failed to enabled/disable\n");
		return ret;
	}

	if (rdev->desc->ops->set_suspend_voltage && rstate->uV > 0) {
		ret = rdev->desc->ops->set_suspend_voltage(rdev, rstate->uV);
		if (ret < 0) {
			rdev_err(rdev, "failed to set voltage\n");
			return ret;
		}
	}

	if (rdev->desc->ops->set_suspend_mode && rstate->mode > 0) {
		ret = rdev->desc->ops->set_suspend_mode(rdev, rstate->mode);
		if (ret < 0) {
			rdev_err(rdev, "failed to set mode\n");
			return ret;
		}
	}
	return ret;
}

static int suspend_prepare(struct regulator_dev *rdev, suspend_state_t state)
{
	if (!rdev->constraints)
		return -EINVAL;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		return suspend_set_state(rdev,
			&rdev->constraints->state_standby);
	case PM_SUSPEND_MEM:
		return suspend_set_state(rdev,
			&rdev->constraints->state_mem);
	case PM_SUSPEND_MAX:
		return suspend_set_state(rdev,
			&rdev->constraints->state_disk);
	default:
		return -EINVAL;
	}
}

static void print_constraints(struct regulator_dev *rdev)
{
	struct regulation_constraints *constraints = rdev->constraints;
	char buf[160] = "";
	int count = 0;
	int ret;

	if (constraints->min_uV && constraints->max_uV) {
		if (constraints->min_uV == constraints->max_uV)
			count += sprintf(buf + count, "%d mV ",
					 constraints->min_uV / 1000);
		else
			count += sprintf(buf + count, "%d <--> %d mV ",
					 constraints->min_uV / 1000,
					 constraints->max_uV / 1000);
	}

	if (!constraints->min_uV ||
	    constraints->min_uV != constraints->max_uV) {
		ret = _regulator_get_voltage(rdev);
		if (ret > 0)
			count += sprintf(buf + count, "at %d mV ", ret / 1000);
	}

	if (constraints->uV_offset)
		count += sprintf(buf, "%dmV offset ",
				 constraints->uV_offset / 1000);

	if (constraints->min_uA && constraints->max_uA) {
		if (constraints->min_uA == constraints->max_uA)
			count += sprintf(buf + count, "%d mA ",
					 constraints->min_uA / 1000);
		else
			count += sprintf(buf + count, "%d <--> %d mA ",
					 constraints->min_uA / 1000,
					 constraints->max_uA / 1000);
	}

	if (!constraints->min_uA ||
	    constraints->min_uA != constraints->max_uA) {
		ret = _regulator_get_current_limit(rdev);
		if (ret > 0)
			count += sprintf(buf + count, "at %d mA ", ret / 1000);
	}

	if (constraints->valid_modes_mask & REGULATOR_MODE_FAST)
		count += sprintf(buf + count, "fast ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_NORMAL)
		count += sprintf(buf + count, "normal ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_IDLE)
		count += sprintf(buf + count, "idle ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_STANDBY)
		count += sprintf(buf + count, "standby");

	if (!count)
		sprintf(buf, "no parameters");

	rdev_info(rdev, "%s\n", buf);

	if ((constraints->min_uV != constraints->max_uV) &&
	    !(constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE))
		rdev_warn(rdev,
			  "Voltage range but no REGULATOR_CHANGE_VOLTAGE\n");
}

static int machine_constraints_voltage(struct regulator_dev *rdev,
	struct regulation_constraints *constraints)
{
	const struct regulator_ops *ops = rdev->desc->ops;
	int ret;

	
	if (rdev->constraints->apply_uV &&
	    rdev->constraints->min_uV == rdev->constraints->max_uV) {
		int current_uV = _regulator_get_voltage(rdev);
		if (current_uV < 0) {
			rdev_err(rdev,
				 "failed to get the current voltage(%d)\n",
				 current_uV);
			return current_uV;
		}
		if (current_uV < rdev->constraints->min_uV ||
		    current_uV > rdev->constraints->max_uV) {
			ret = _regulator_do_set_voltage(
				rdev, rdev->constraints->min_uV,
				rdev->constraints->max_uV);
			if (ret < 0) {
				rdev_err(rdev,
					"failed to apply %duV constraint(%d)\n",
					rdev->constraints->min_uV, ret);
				return ret;
			}
		}
	}

	if (ops->list_voltage && rdev->desc->n_voltages) {
		int	count = rdev->desc->n_voltages;
		int	i;
		int	min_uV = INT_MAX;
		int	max_uV = INT_MIN;
		int	cmin = constraints->min_uV;
		int	cmax = constraints->max_uV;

		if (count == 1 && !cmin) {
			cmin = 1;
			cmax = INT_MAX;
			constraints->min_uV = cmin;
			constraints->max_uV = cmax;
		}

		
		if ((cmin == 0) && (cmax == 0))
			return 0;

		
		if (cmin <= 0 || cmax <= 0 || cmax < cmin) {
			rdev_err(rdev, "invalid voltage constraints\n");
			return -EINVAL;
		}

		
		for (i = 0; i < count; i++) {
			int	value;

			value = ops->list_voltage(rdev, i);
			if (value <= 0)
				continue;

			
			if (value >= cmin && value < min_uV)
				min_uV = value;
			if (value <= cmax && value > max_uV)
				max_uV = value;
		}

		
		if (max_uV < min_uV) {
			rdev_err(rdev,
				 "unsupportable voltage constraints %u-%uuV\n",
				 min_uV, max_uV);
			return -EINVAL;
		}

		
		if (constraints->min_uV < min_uV) {
			rdev_dbg(rdev, "override min_uV, %d -> %d\n",
				 constraints->min_uV, min_uV);
			constraints->min_uV = min_uV;
		}
		if (constraints->max_uV > max_uV) {
			rdev_dbg(rdev, "override max_uV, %d -> %d\n",
				 constraints->max_uV, max_uV);
			constraints->max_uV = max_uV;
		}
	}

	return 0;
}

static int machine_constraints_current(struct regulator_dev *rdev,
	struct regulation_constraints *constraints)
{
	const struct regulator_ops *ops = rdev->desc->ops;
	int ret;

	if (!constraints->min_uA && !constraints->max_uA)
		return 0;

	if (constraints->min_uA > constraints->max_uA) {
		rdev_err(rdev, "Invalid current constraints\n");
		return -EINVAL;
	}

	if (!ops->set_current_limit || !ops->get_current_limit) {
		rdev_warn(rdev, "Operation of current configuration missing\n");
		return 0;
	}

	
	ret = ops->set_current_limit(rdev, constraints->min_uA,
			constraints->max_uA);
	if (ret < 0) {
		rdev_err(rdev, "Failed to set current constraint, %d\n", ret);
		return ret;
	}

	return 0;
}

static int _regulator_do_enable(struct regulator_dev *rdev);

static int set_machine_constraints(struct regulator_dev *rdev,
	const struct regulation_constraints *constraints)
{
	int ret = 0;
	const struct regulator_ops *ops = rdev->desc->ops;

	if (constraints)
		rdev->constraints = kmemdup(constraints, sizeof(*constraints),
					    GFP_KERNEL);
	else
		rdev->constraints = kzalloc(sizeof(*constraints),
					    GFP_KERNEL);
	if (!rdev->constraints)
		return -ENOMEM;

	ret = machine_constraints_voltage(rdev, rdev->constraints);
	if (ret != 0)
		goto out;

	ret = machine_constraints_current(rdev, rdev->constraints);
	if (ret != 0)
		goto out;

	
	if (rdev->constraints->initial_state) {
		ret = suspend_prepare(rdev, rdev->constraints->initial_state);
		if (ret < 0) {
			rdev_err(rdev, "failed to set suspend state\n");
			goto out;
		}
	}

	if (rdev->constraints->initial_mode) {
		if (!ops->set_mode) {
			rdev_err(rdev, "no set_mode operation\n");
			ret = -EINVAL;
			goto out;
		}

		ret = ops->set_mode(rdev, rdev->constraints->initial_mode);
		if (ret < 0) {
			rdev_err(rdev, "failed to set initial mode: %d\n", ret);
			goto out;
		}
	}

	if (rdev->constraints->always_on || rdev->constraints->boot_on) {
		ret = _regulator_do_enable(rdev);
		if (ret < 0 && ret != -EINVAL) {
			rdev_err(rdev, "failed to enable\n");
			goto out;
		}
	}

	if ((rdev->constraints->ramp_delay || rdev->constraints->ramp_disable)
		&& ops->set_ramp_delay) {
		ret = ops->set_ramp_delay(rdev, rdev->constraints->ramp_delay);
		if (ret < 0) {
			rdev_err(rdev, "failed to set ramp_delay\n");
			goto out;
		}
	}

	print_constraints(rdev);
	return 0;
out:
	kfree(rdev->constraints);
	rdev->constraints = NULL;
	return ret;
}

static int set_supply(struct regulator_dev *rdev,
		      struct regulator_dev *supply_rdev)
{
	int err;

	rdev_info(rdev, "supplied by %s\n", rdev_get_name(supply_rdev));

	rdev->supply = create_regulator(supply_rdev, &rdev->dev, "SUPPLY");
	if (rdev->supply == NULL) {
		err = -ENOMEM;
		return err;
	}
	supply_rdev->open_count++;

	return 0;
}

static int set_consumer_device_supply(struct regulator_dev *rdev,
				      const char *consumer_dev_name,
				      const char *supply)
{
	struct regulator_map *node;
	int has_dev;

	if (supply == NULL)
		return -EINVAL;

	if (consumer_dev_name != NULL)
		has_dev = 1;
	else
		has_dev = 0;

	list_for_each_entry(node, &regulator_map_list, list) {
		if (node->dev_name && consumer_dev_name) {
			if (strcmp(node->dev_name, consumer_dev_name) != 0)
				continue;
		} else if (node->dev_name || consumer_dev_name) {
			continue;
		}

		if (strcmp(node->supply, supply) != 0)
			continue;

		pr_debug("%s: %s/%s is '%s' supply; fail %s/%s\n",
			 consumer_dev_name,
			 dev_name(&node->regulator->dev),
			 node->regulator->desc->name,
			 supply,
			 dev_name(&rdev->dev), rdev_get_name(rdev));
		return -EBUSY;
	}

	node = kzalloc(sizeof(struct regulator_map), GFP_KERNEL);
	if (node == NULL)
		return -ENOMEM;

	node->regulator = rdev;
	node->supply = supply;

	if (has_dev) {
		node->dev_name = kstrdup(consumer_dev_name, GFP_KERNEL);
		if (node->dev_name == NULL) {
			kfree(node);
			return -ENOMEM;
		}
	}

	list_add(&node->list, &regulator_map_list);
	return 0;
}

static void unset_regulator_supplies(struct regulator_dev *rdev)
{
	struct regulator_map *node, *n;

	list_for_each_entry_safe(node, n, &regulator_map_list, list) {
		if (rdev == node->regulator) {
			list_del(&node->list);
			kfree(node->dev_name);
			kfree(node);
		}
	}
}

#define REG_STR_SIZE	64

static struct regulator *create_regulator(struct regulator_dev *rdev,
					  struct device *dev,
					  const char *supply_name)
{
	struct regulator *regulator;
	char buf[REG_STR_SIZE];
	int err, size;

	regulator = kzalloc(sizeof(*regulator), GFP_KERNEL);
	if (regulator == NULL)
		return NULL;

	mutex_lock(&rdev->mutex);
	regulator->rdev = rdev;
	list_add(&regulator->list, &rdev->consumer_list);

	if (dev) {
		regulator->dev = dev;

		
		size = scnprintf(buf, REG_STR_SIZE, "%s-%s",
				 dev->kobj.name, supply_name);
		if (size >= REG_STR_SIZE)
			goto overflow_err;

		regulator->supply_name = kstrdup(buf, GFP_KERNEL);
		if (regulator->supply_name == NULL)
			goto overflow_err;

		err = sysfs_create_link(&rdev->dev.kobj, &dev->kobj,
					buf);
		if (err) {
			rdev_warn(rdev, "could not add device link %s err %d\n",
				  dev->kobj.name, err);
			
		}
	} else {
		regulator->supply_name = kstrdup(supply_name, GFP_KERNEL);
		if (regulator->supply_name == NULL)
			goto overflow_err;
	}

	regulator->debugfs = debugfs_create_dir(regulator->supply_name,
						rdev->debugfs);
	if (!regulator->debugfs) {
		rdev_warn(rdev, "Failed to create debugfs directory\n");
	} else {
		debugfs_create_u32("uA_load", 0444, regulator->debugfs,
				   &regulator->uA_load);
		debugfs_create_u32("min_uV", 0444, regulator->debugfs,
				   &regulator->min_uV);
		debugfs_create_u32("max_uV", 0444, regulator->debugfs,
				   &regulator->max_uV);
	}

	if (!_regulator_can_change_status(rdev) &&
	    _regulator_is_enabled(rdev))
		regulator->always_on = true;

	mutex_unlock(&rdev->mutex);
	return regulator;
overflow_err:
	list_del(&regulator->list);
	kfree(regulator);
	mutex_unlock(&rdev->mutex);
	return NULL;
}

static int _regulator_get_enable_time(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->enable_time)
		return rdev->constraints->enable_time;
	if (!rdev->desc->ops->enable_time)
		return rdev->desc->enable_time;
	return rdev->desc->ops->enable_time(rdev);
}

static struct regulator_supply_alias *regulator_find_supply_alias(
		struct device *dev, const char *supply)
{
	struct regulator_supply_alias *map;

	list_for_each_entry(map, &regulator_supply_alias_list, list)
		if (map->src_dev == dev && strcmp(map->src_supply, supply) == 0)
			return map;

	return NULL;
}

static void regulator_supply_alias(struct device **dev, const char **supply)
{
	struct regulator_supply_alias *map;

	map = regulator_find_supply_alias(*dev, *supply);
	if (map) {
		dev_dbg(*dev, "Mapping supply %s to %s,%s\n",
				*supply, map->alias_supply,
				dev_name(map->alias_dev));
		*dev = map->alias_dev;
		*supply = map->alias_supply;
	}
}

static struct regulator_dev *regulator_dev_lookup(struct device *dev,
						  const char *supply,
						  int *ret)
{
	struct regulator_dev *r;
	struct device_node *node;
	struct regulator_map *map;
	const char *devname = NULL;

	regulator_supply_alias(&dev, &supply);

	
	if (dev && dev->of_node) {
		node = of_get_regulator(dev, supply);
		if (node) {
			list_for_each_entry(r, &regulator_list, list)
				if (r->dev.parent &&
					node == r->dev.of_node)
					return r;
			*ret = -EPROBE_DEFER;
			return NULL;
		} else {
			*ret = -ENODEV;
		}
	}

	
	if (dev)
		devname = dev_name(dev);

	list_for_each_entry(r, &regulator_list, list)
		if (strcmp(rdev_get_name(r), supply) == 0)
			return r;

	list_for_each_entry(map, &regulator_map_list, list) {
		
		if (map->dev_name &&
		    (!devname || strcmp(map->dev_name, devname)))
			continue;

		if (strcmp(map->supply, supply) == 0)
			return map->regulator;
	}


	return NULL;
}

static struct regulator *_regulator_get(struct device *dev, const char *id,
					bool exclusive, bool allow_dummy)
{
	struct regulator_dev *rdev;
	struct regulator *regulator = ERR_PTR(-EPROBE_DEFER);
	const char *devname = NULL;
	int ret;

	if (id == NULL) {
		pr_err("get() with no identifier\n");
		return ERR_PTR(-EINVAL);
	}

	if (dev)
		devname = dev_name(dev);

	if (have_full_constraints())
		ret = -ENODEV;
	else
		ret = -EPROBE_DEFER;

	mutex_lock(&regulator_list_mutex);

	rdev = regulator_dev_lookup(dev, id, &ret);
	if (rdev)
		goto found;

	regulator = ERR_PTR(ret);

	if (ret && ret != -ENODEV)
		goto out;

	if (!devname)
		devname = "deviceless";

	if (have_full_constraints() && allow_dummy) {
		pr_warn("%s supply %s not found, using dummy regulator\n",
			devname, id);

		rdev = dummy_regulator_rdev;
		goto found;
	
	} else if (!have_full_constraints() || exclusive) {
		dev_warn(dev, "dummy supplies not allowed\n");
	}

	mutex_unlock(&regulator_list_mutex);
	return regulator;

found:
	if (rdev->exclusive) {
		regulator = ERR_PTR(-EPERM);
		goto out;
	}

	if (exclusive && rdev->open_count) {
		regulator = ERR_PTR(-EBUSY);
		goto out;
	}

	if (!try_module_get(rdev->owner))
		goto out;

	regulator = create_regulator(rdev, dev, id);
	if (regulator == NULL) {
		regulator = ERR_PTR(-ENOMEM);
		module_put(rdev->owner);
		goto out;
	}

	rdev->open_count++;
	if (exclusive) {
		rdev->exclusive = 1;

		ret = _regulator_is_enabled(rdev);
		if (ret > 0)
			rdev->use_count = 1;
		else
			rdev->use_count = 0;
	}

out:
	mutex_unlock(&regulator_list_mutex);

	return regulator;
}

struct regulator *regulator_get(struct device *dev, const char *id)
{
	return _regulator_get(dev, id, false, true);
}
EXPORT_SYMBOL_GPL(regulator_get);

struct regulator *regulator_get_exclusive(struct device *dev, const char *id)
{
	return _regulator_get(dev, id, true, false);
}
EXPORT_SYMBOL_GPL(regulator_get_exclusive);

struct regulator *regulator_get_optional(struct device *dev, const char *id)
{
	return _regulator_get(dev, id, false, false);
}
EXPORT_SYMBOL_GPL(regulator_get_optional);

static void _regulator_put(struct regulator *regulator)
{
	struct regulator_dev *rdev;

	if (regulator == NULL || IS_ERR(regulator))
		return;

	rdev = regulator->rdev;

	debugfs_remove_recursive(regulator->debugfs);

	
	if (regulator->dev)
		sysfs_remove_link(&rdev->dev.kobj, regulator->supply_name);
	mutex_lock(&rdev->mutex);
	kfree(regulator->supply_name);
	list_del(&regulator->list);
	kfree(regulator);

	rdev->open_count--;
	rdev->exclusive = 0;
	mutex_unlock(&rdev->mutex);

	module_put(rdev->owner);
}

void regulator_put(struct regulator *regulator)
{
	mutex_lock(&regulator_list_mutex);
	_regulator_put(regulator);
	mutex_unlock(&regulator_list_mutex);
}
EXPORT_SYMBOL_GPL(regulator_put);

int regulator_register_supply_alias(struct device *dev, const char *id,
				    struct device *alias_dev,
				    const char *alias_id)
{
	struct regulator_supply_alias *map;

	map = regulator_find_supply_alias(dev, id);
	if (map)
		return -EEXIST;

	map = kzalloc(sizeof(struct regulator_supply_alias), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->src_dev = dev;
	map->src_supply = id;
	map->alias_dev = alias_dev;
	map->alias_supply = alias_id;

	list_add(&map->list, &regulator_supply_alias_list);

	pr_info("Adding alias for supply %s,%s -> %s,%s\n",
		id, dev_name(dev), alias_id, dev_name(alias_dev));

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_register_supply_alias);

void regulator_unregister_supply_alias(struct device *dev, const char *id)
{
	struct regulator_supply_alias *map;

	map = regulator_find_supply_alias(dev, id);
	if (map) {
		list_del(&map->list);
		kfree(map);
	}
}
EXPORT_SYMBOL_GPL(regulator_unregister_supply_alias);

int regulator_bulk_register_supply_alias(struct device *dev,
					 const char *const *id,
					 struct device *alias_dev,
					 const char *const *alias_id,
					 int num_id)
{
	int i;
	int ret;

	for (i = 0; i < num_id; ++i) {
		ret = regulator_register_supply_alias(dev, id[i], alias_dev,
						      alias_id[i]);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	dev_err(dev,
		"Failed to create supply alias %s,%s -> %s,%s\n",
		id[i], dev_name(dev), alias_id[i], dev_name(alias_dev));

	while (--i >= 0)
		regulator_unregister_supply_alias(dev, id[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_register_supply_alias);

void regulator_bulk_unregister_supply_alias(struct device *dev,
					    const char *const *id,
					    int num_id)
{
	int i;

	for (i = 0; i < num_id; ++i)
		regulator_unregister_supply_alias(dev, id[i]);
}
EXPORT_SYMBOL_GPL(regulator_bulk_unregister_supply_alias);


static int regulator_ena_gpio_request(struct regulator_dev *rdev,
				const struct regulator_config *config)
{
	struct regulator_enable_gpio *pin;
	struct gpio_desc *gpiod;
	int ret;

	gpiod = gpio_to_desc(config->ena_gpio);

	list_for_each_entry(pin, &regulator_ena_gpio_list, list) {
		if (pin->gpiod == gpiod) {
			rdev_dbg(rdev, "GPIO %d is already used\n",
				config->ena_gpio);
			goto update_ena_gpio_to_rdev;
		}
	}

	ret = gpio_request_one(config->ena_gpio,
				GPIOF_DIR_OUT | config->ena_gpio_flags,
				rdev_get_name(rdev));
	if (ret)
		return ret;

	pin = kzalloc(sizeof(struct regulator_enable_gpio), GFP_KERNEL);
	if (pin == NULL) {
		gpio_free(config->ena_gpio);
		return -ENOMEM;
	}

	pin->gpiod = gpiod;
	pin->ena_gpio_invert = config->ena_gpio_invert;
	list_add(&pin->list, &regulator_ena_gpio_list);

update_ena_gpio_to_rdev:
	pin->request_count++;
	rdev->ena_pin = pin;
	return 0;
}

static void regulator_ena_gpio_free(struct regulator_dev *rdev)
{
	struct regulator_enable_gpio *pin, *n;

	if (!rdev->ena_pin)
		return;

	
	list_for_each_entry_safe(pin, n, &regulator_ena_gpio_list, list) {
		if (pin->gpiod == rdev->ena_pin->gpiod) {
			if (pin->request_count <= 1) {
				pin->request_count = 0;
				gpiod_put(pin->gpiod);
				list_del(&pin->list);
				kfree(pin);
				rdev->ena_pin = NULL;
				return;
			} else {
				pin->request_count--;
			}
		}
	}
}

static int regulator_ena_gpio_ctrl(struct regulator_dev *rdev, bool enable)
{
	struct regulator_enable_gpio *pin = rdev->ena_pin;

	if (!pin)
		return -EINVAL;

	if (enable) {
		
		if (pin->enable_count == 0)
			gpiod_set_value_cansleep(pin->gpiod,
						 !pin->ena_gpio_invert);

		pin->enable_count++;
	} else {
		if (pin->enable_count > 1) {
			pin->enable_count--;
			return 0;
		}

		
		if (pin->enable_count <= 1) {
			gpiod_set_value_cansleep(pin->gpiod,
						 pin->ena_gpio_invert);
			pin->enable_count = 0;
		}
	}

	return 0;
}

static void _regulator_enable_delay(unsigned int delay)
{
	unsigned int ms = delay / 1000;
	unsigned int us = delay % 1000;

	if (ms > 0) {
		if (ms < 20)
			us += ms * 1000;
		else
			msleep(ms);
	}

	if (us >= 10)
		usleep_range(us, us + 100);
	else
		udelay(us);
}

static int _regulator_do_enable(struct regulator_dev *rdev)
{
	int ret, delay;

	
	ret = _regulator_get_enable_time(rdev);
	if (ret >= 0) {
		delay = ret;
	} else {
		rdev_warn(rdev, "enable_time() failed: %d\n", ret);
		delay = 0;
	}

	trace_regulator_enable(rdev_get_name(rdev));

	if (rdev->desc->off_on_delay) {
		unsigned long start_jiffy = jiffies;
		unsigned long intended, max_delay, remaining;

		max_delay = usecs_to_jiffies(rdev->desc->off_on_delay);
		intended = rdev->last_off_jiffy + max_delay;

		if (time_before(start_jiffy, intended)) {
			remaining = intended - start_jiffy;
			if (remaining <= max_delay)
				_regulator_enable_delay(
						jiffies_to_usecs(remaining));
		}
	}

	if (rdev->ena_pin) {
		if (!rdev->ena_gpio_state) {
			ret = regulator_ena_gpio_ctrl(rdev, true);
			if (ret < 0)
				return ret;
			rdev->ena_gpio_state = 1;
		}
	} else if (rdev->desc->ops->enable) {
		ret = rdev->desc->ops->enable(rdev);
		if (ret < 0)
			return ret;
	} else {
		return -EINVAL;
	}

	trace_regulator_enable_delay(rdev_get_name(rdev));

	_regulator_enable_delay(delay);

	trace_regulator_enable_complete(rdev_get_name(rdev));

	return 0;
}

static int _regulator_enable(struct regulator_dev *rdev)
{
	int ret;

	
	if (rdev->constraints &&
	    (rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_DRMS))
		drms_uA_update(rdev);

	if (rdev->use_count == 0) {
		
		ret = _regulator_is_enabled(rdev);
		if (ret == -EINVAL || ret == 0) {
			if (!_regulator_can_change_status(rdev))
				return -EPERM;

			ret = _regulator_do_enable(rdev);
			if (ret < 0)
				return ret;

			_notifier_call_chain(rdev, REGULATOR_EVENT_ENABLE,
						NULL);
		} else if (ret < 0) {
			rdev_err(rdev, "is_enabled() failed: %d\n", ret);
			return ret;
		}
		
	}

	rdev->use_count++;

	return 0;
}

int regulator_enable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret = 0;

	if (regulator->always_on)
		return 0;

	if (rdev->supply) {
		ret = regulator_enable(rdev->supply);
		if (ret != 0)
			return ret;
	}

	mutex_lock(&rdev->mutex);

	ret = _regulator_enable(rdev);
	if (ret == 0)
		regulator->enabled++;

	mutex_unlock(&rdev->mutex);

	if (ret != 0 && rdev->supply)
		regulator_disable(rdev->supply);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_enable);

static int _regulator_do_disable(struct regulator_dev *rdev)
{
	int ret;

	trace_regulator_disable(rdev_get_name(rdev));

	if (rdev->ena_pin) {
		if (rdev->ena_gpio_state) {
			ret = regulator_ena_gpio_ctrl(rdev, false);
			if (ret < 0)
				return ret;
			rdev->ena_gpio_state = 0;
		}

	} else if (rdev->desc->ops->disable) {
		ret = rdev->desc->ops->disable(rdev);
		if (ret != 0)
			return ret;
	}

	if (rdev->desc->off_on_delay)
		rdev->last_off_jiffy = jiffies;

	trace_regulator_disable_complete(rdev_get_name(rdev));

	return 0;
}

static int _regulator_disable(struct regulator_dev *rdev)
{
	int ret = 0;

	if (WARN(rdev->use_count <= 0,
		 "unbalanced disables for %s\n", rdev_get_name(rdev)))
		return -EIO;

	
	if (rdev->use_count == 1 &&
	    (rdev->constraints && !rdev->constraints->always_on)) {

		
		if (_regulator_can_change_status(rdev)) {
			ret = _notifier_call_chain(rdev,
						   REGULATOR_EVENT_PRE_DISABLE,
						   NULL);
			if (ret & NOTIFY_STOP_MASK)
				return -EINVAL;

			ret = _regulator_do_disable(rdev);
			if (ret < 0) {
				rdev_err(rdev, "failed to disable\n");
				_notifier_call_chain(rdev,
						REGULATOR_EVENT_ABORT_DISABLE,
						NULL);
				return ret;
			}
			_notifier_call_chain(rdev, REGULATOR_EVENT_DISABLE,
					NULL);
		}

		rdev->use_count = 0;
	} else if (rdev->use_count > 1) {

		if (rdev->constraints &&
			(rdev->constraints->valid_ops_mask &
			REGULATOR_CHANGE_DRMS))
			drms_uA_update(rdev);

		rdev->use_count--;
	}

	return ret;
}

int regulator_disable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret = 0;

	if (regulator->always_on)
		return 0;

	mutex_lock(&rdev->mutex);
	ret = _regulator_disable(rdev);
	if (ret == 0)
		regulator->enabled--;
	mutex_unlock(&rdev->mutex);

	if (ret == 0 && rdev->supply)
		regulator_disable(rdev->supply);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_disable);

static int _regulator_force_disable(struct regulator_dev *rdev)
{
	int ret = 0;

	ret = _notifier_call_chain(rdev, REGULATOR_EVENT_FORCE_DISABLE |
			REGULATOR_EVENT_PRE_DISABLE, NULL);
	if (ret & NOTIFY_STOP_MASK)
		return -EINVAL;

	ret = _regulator_do_disable(rdev);
	if (ret < 0) {
		rdev_err(rdev, "failed to force disable\n");
		_notifier_call_chain(rdev, REGULATOR_EVENT_FORCE_DISABLE |
				REGULATOR_EVENT_ABORT_DISABLE, NULL);
		return ret;
	}

	_notifier_call_chain(rdev, REGULATOR_EVENT_FORCE_DISABLE |
			REGULATOR_EVENT_DISABLE, NULL);

	return 0;
}

int regulator_force_disable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret;

	mutex_lock(&rdev->mutex);
	regulator->uA_load = 0;
	ret = _regulator_force_disable(regulator->rdev);
	mutex_unlock(&rdev->mutex);

	if (rdev->supply)
		while (rdev->open_count--)
			regulator_disable(rdev->supply);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_force_disable);

static void regulator_disable_work(struct work_struct *work)
{
	struct regulator_dev *rdev = container_of(work, struct regulator_dev,
						  disable_work.work);
	int count, i, ret;

	mutex_lock(&rdev->mutex);

	BUG_ON(!rdev->deferred_disables);

	count = rdev->deferred_disables;
	rdev->deferred_disables = 0;

	for (i = 0; i < count; i++) {
		ret = _regulator_disable(rdev);
		if (ret != 0)
			rdev_err(rdev, "Deferred disable failed: %d\n", ret);
	}

	mutex_unlock(&rdev->mutex);

	if (rdev->supply) {
		for (i = 0; i < count; i++) {
			ret = regulator_disable(rdev->supply);
			if (ret != 0) {
				rdev_err(rdev,
					 "Supply disable failed: %d\n", ret);
			}
		}
	}
}

int regulator_disable_deferred(struct regulator *regulator, int ms)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret;

	if (regulator->always_on)
		return 0;

	if (!ms)
		return regulator_disable(regulator);

	mutex_lock(&rdev->mutex);
	rdev->deferred_disables++;
	mutex_unlock(&rdev->mutex);

	ret = queue_delayed_work(system_power_efficient_wq,
				 &rdev->disable_work,
				 msecs_to_jiffies(ms));
	if (ret < 0)
		return ret;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(regulator_disable_deferred);

static int _regulator_is_enabled(struct regulator_dev *rdev)
{
	
	if (rdev->ena_pin)
		return rdev->ena_gpio_state;

	
	if (!rdev->desc->ops->is_enabled)
		return 1;

	return rdev->desc->ops->is_enabled(rdev);
}

int regulator_is_enabled(struct regulator *regulator)
{
	int ret;

	if (regulator->always_on)
		return 1;

	mutex_lock(&regulator->rdev->mutex);
	ret = _regulator_is_enabled(regulator->rdev);
	mutex_unlock(&regulator->rdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_is_enabled);

int regulator_can_change_voltage(struct regulator *regulator)
{
	struct regulator_dev	*rdev = regulator->rdev;

	if (rdev->constraints &&
	    (rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE)) {
		if (rdev->desc->n_voltages - rdev->desc->linear_min_sel > 1)
			return 1;

		if (rdev->desc->continuous_voltage_range &&
		    rdev->constraints->min_uV && rdev->constraints->max_uV &&
		    rdev->constraints->min_uV != rdev->constraints->max_uV)
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_can_change_voltage);

int regulator_count_voltages(struct regulator *regulator)
{
	struct regulator_dev	*rdev = regulator->rdev;

	if (rdev->desc->n_voltages)
		return rdev->desc->n_voltages;

	if (!rdev->supply)
		return -EINVAL;

	return regulator_count_voltages(rdev->supply);
}
EXPORT_SYMBOL_GPL(regulator_count_voltages);

int regulator_list_voltage(struct regulator *regulator, unsigned selector)
{
	struct regulator_dev *rdev = regulator->rdev;
	const struct regulator_ops *ops = rdev->desc->ops;
	int ret;

	if (rdev->desc->fixed_uV && rdev->desc->n_voltages == 1 && !selector)
		return rdev->desc->fixed_uV;

	if (ops->list_voltage) {
		if (selector >= rdev->desc->n_voltages)
			return -EINVAL;
		mutex_lock(&rdev->mutex);
		ret = ops->list_voltage(rdev, selector);
		mutex_unlock(&rdev->mutex);
	} else if (rdev->supply) {
		ret = regulator_list_voltage(rdev->supply, selector);
	} else {
		return -EINVAL;
	}

	if (ret > 0) {
		if (ret < rdev->constraints->min_uV)
			ret = 0;
		else if (ret > rdev->constraints->max_uV)
			ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_list_voltage);

struct regmap *regulator_get_regmap(struct regulator *regulator)
{
	struct regmap *map = regulator->rdev->regmap;

	return map ? map : ERR_PTR(-EOPNOTSUPP);
}

int regulator_get_hardware_vsel_register(struct regulator *regulator,
					 unsigned *vsel_reg,
					 unsigned *vsel_mask)
{
	struct regulator_dev *rdev = regulator->rdev;
	const struct regulator_ops *ops = rdev->desc->ops;

	if (ops->set_voltage_sel != regulator_set_voltage_sel_regmap)
		return -EOPNOTSUPP;

	 *vsel_reg = rdev->desc->vsel_reg;
	 *vsel_mask = rdev->desc->vsel_mask;

	 return 0;
}
EXPORT_SYMBOL_GPL(regulator_get_hardware_vsel_register);

/**
 * regulator_list_hardware_vsel - get the HW-specific register value for a selector
 * @regulator: regulator source
 * @selector: identify voltage to list
 *
 * Converts the selector to a hardware-specific voltage selector that can be
 * directly written to the regulator registers. The address of the voltage
 * register can be determined by calling @regulator_get_hardware_vsel_register.
 *
 * On error a negative errno is returned.
 */
int regulator_list_hardware_vsel(struct regulator *regulator,
				 unsigned selector)
{
	struct regulator_dev *rdev = regulator->rdev;
	const struct regulator_ops *ops = rdev->desc->ops;

	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;
	if (ops->set_voltage_sel != regulator_set_voltage_sel_regmap)
		return -EOPNOTSUPP;

	return selector;
}
EXPORT_SYMBOL_GPL(regulator_list_hardware_vsel);

int regulator_list_corner_voltage(struct regulator *regulator, int corner)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret;

	if (corner < rdev->constraints->min_uV ||
	    corner > rdev->constraints->max_uV ||
	    !rdev->desc->ops->list_corner_voltage)
		return -EINVAL;

	mutex_lock(&rdev->mutex);
	ret = rdev->desc->ops->list_corner_voltage(rdev, corner);
	mutex_unlock(&rdev->mutex);

	return ret;
}
EXPORT_SYMBOL(regulator_list_corner_voltage);

unsigned int regulator_get_linear_step(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;

	return rdev->desc->uV_step;
}
EXPORT_SYMBOL_GPL(regulator_get_linear_step);

int regulator_is_supported_voltage(struct regulator *regulator,
				   int min_uV, int max_uV)
{
	struct regulator_dev *rdev = regulator->rdev;
	int i, voltages, ret;

	
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE)) {
		ret = regulator_get_voltage(regulator);
		if (ret >= 0)
			return min_uV <= ret && ret <= max_uV;
		else
			return ret;
	}

	
	if (rdev->desc->continuous_voltage_range)
		return min_uV >= rdev->constraints->min_uV &&
				max_uV <= rdev->constraints->max_uV;

	ret = regulator_count_voltages(regulator);
	if (ret < 0)
		return ret;
	voltages = ret;

	for (i = 0; i < voltages; i++) {
		ret = regulator_list_voltage(regulator, i);

		if (ret >= min_uV && ret <= max_uV)
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_is_supported_voltage);

static int _regulator_call_set_voltage(struct regulator_dev *rdev,
				       int min_uV, int max_uV,
				       unsigned *selector)
{
	struct pre_voltage_change_data data;
	int ret;

	data.old_uV = _regulator_get_voltage(rdev);
	data.min_uV = min_uV;
	data.max_uV = max_uV;
	ret = _notifier_call_chain(rdev, REGULATOR_EVENT_PRE_VOLTAGE_CHANGE,
				   &data);
	if (ret & NOTIFY_STOP_MASK)
		return -EINVAL;

	ret = rdev->desc->ops->set_voltage(rdev, min_uV, max_uV, selector);
	if (ret >= 0)
		return ret;

	_notifier_call_chain(rdev, REGULATOR_EVENT_ABORT_VOLTAGE_CHANGE,
			     (void *)data.old_uV);

	return ret;
}

static int _regulator_call_set_voltage_sel(struct regulator_dev *rdev,
					   int uV, unsigned selector)
{
	struct pre_voltage_change_data data;
	int ret;

	data.old_uV = _regulator_get_voltage(rdev);
	data.min_uV = uV;
	data.max_uV = uV;
	ret = _notifier_call_chain(rdev, REGULATOR_EVENT_PRE_VOLTAGE_CHANGE,
				   &data);
	if (ret & NOTIFY_STOP_MASK)
		return -EINVAL;

	ret = rdev->desc->ops->set_voltage_sel(rdev, selector);
	if (ret >= 0)
		return ret;

	_notifier_call_chain(rdev, REGULATOR_EVENT_ABORT_VOLTAGE_CHANGE,
			     (void *)data.old_uV);

	return ret;
}

static int _regulator_do_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV)
{
	int ret;
	int delay = 0;
	int best_val = 0;
	unsigned int selector;
	int old_selector = -1;

	trace_regulator_set_voltage(rdev_get_name(rdev), min_uV, max_uV);

	min_uV += rdev->constraints->uV_offset;
	max_uV += rdev->constraints->uV_offset;

	if (_regulator_is_enabled(rdev) &&
	    rdev->desc->ops->set_voltage_time_sel &&
	    rdev->desc->ops->get_voltage_sel) {
		old_selector = rdev->desc->ops->get_voltage_sel(rdev);
		if (old_selector < 0)
			return old_selector;
	}

	if (rdev->desc->ops->set_voltage) {
		ret = _regulator_call_set_voltage(rdev, min_uV, max_uV,
						  &selector);

		if (ret >= 0) {
			if (rdev->desc->ops->list_voltage)
				best_val = rdev->desc->ops->list_voltage(rdev,
									 selector);
			else
				best_val = _regulator_get_voltage(rdev);
		}

	} else if (rdev->desc->ops->set_voltage_sel) {
		if (rdev->desc->ops->map_voltage) {
			ret = rdev->desc->ops->map_voltage(rdev, min_uV,
							   max_uV);
		} else {
			if (rdev->desc->ops->list_voltage ==
			    regulator_list_voltage_linear)
				ret = regulator_map_voltage_linear(rdev,
								min_uV, max_uV);
			else if (rdev->desc->ops->list_voltage ==
				 regulator_list_voltage_linear_range)
				ret = regulator_map_voltage_linear_range(rdev,
								min_uV, max_uV);
			else
				ret = regulator_map_voltage_iterate(rdev,
								min_uV, max_uV);
		}

		if (ret >= 0) {
			best_val = rdev->desc->ops->list_voltage(rdev, ret);
			if (min_uV <= best_val && max_uV >= best_val) {
				selector = ret;
				if (old_selector == selector)
					ret = 0;
				else
					ret = _regulator_call_set_voltage_sel(
						rdev, best_val, selector);
			} else {
				ret = -EINVAL;
			}
		}
	} else {
		ret = -EINVAL;
	}

	
	if (ret == 0 && !rdev->constraints->ramp_disable && old_selector >= 0
		&& old_selector != selector) {

		delay = rdev->desc->ops->set_voltage_time_sel(rdev,
						old_selector, selector);
		if (delay < 0) {
			rdev_warn(rdev, "set_voltage_time_sel() failed: %d\n",
				  delay);
			delay = 0;
		}

		
		if (delay >= 1000) {
			mdelay(delay / 1000);
			udelay(delay % 1000);
		} else if (delay) {
			udelay(delay);
		}
	}

	if (ret == 0 && best_val >= 0) {
		unsigned long data = best_val;

		_notifier_call_chain(rdev, REGULATOR_EVENT_VOLTAGE_CHANGE,
				     (void *)data);
	}

	trace_regulator_set_voltage_complete(rdev_get_name(rdev), best_val);

	return ret;
}

int regulator_set_voltage(struct regulator *regulator, int min_uV, int max_uV)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret = 0;
	int old_min_uV, old_max_uV;
	int current_uV;

	mutex_lock(&rdev->mutex);

	if (regulator->min_uV == min_uV && regulator->max_uV == max_uV)
		goto out;

	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE)) {
		current_uV = _regulator_get_voltage(rdev);
		if (min_uV <= current_uV && current_uV <= max_uV) {
			regulator->min_uV = min_uV;
			regulator->max_uV = max_uV;
			goto out;
		}
	}

	
	if (!rdev->desc->ops->set_voltage &&
	    !rdev->desc->ops->set_voltage_sel) {
		ret = -EINVAL;
		goto out;
	}

	
	ret = regulator_check_voltage(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;

	
	old_min_uV = regulator->min_uV;
	old_max_uV = regulator->max_uV;
	regulator->min_uV = min_uV;
	regulator->max_uV = max_uV;

	ret = regulator_check_consumers(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out2;

	ret = _regulator_do_set_voltage(rdev, min_uV, max_uV);
	if (ret < 0)
		goto out2;

out:
	mutex_unlock(&rdev->mutex);
	return ret;
out2:
	regulator->min_uV = old_min_uV;
	regulator->max_uV = old_max_uV;
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_voltage);

int regulator_set_voltage_time(struct regulator *regulator,
			       int old_uV, int new_uV)
{
	struct regulator_dev *rdev = regulator->rdev;
	const struct regulator_ops *ops = rdev->desc->ops;
	int old_sel = -1;
	int new_sel = -1;
	int voltage;
	int i;

	
	if (!ops->list_voltage || !ops->set_voltage_time_sel
	    || !rdev->desc->n_voltages)
		return -EINVAL;

	for (i = 0; i < rdev->desc->n_voltages; i++) {
		
		voltage = regulator_list_voltage(regulator, i);
		if (voltage < 0)
			return -EINVAL;
		if (voltage == 0)
			continue;
		if (voltage == old_uV)
			old_sel = i;
		if (voltage == new_uV)
			new_sel = i;
	}

	if (old_sel < 0 || new_sel < 0)
		return -EINVAL;

	return ops->set_voltage_time_sel(rdev, old_sel, new_sel);
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_time);

int regulator_set_voltage_time_sel(struct regulator_dev *rdev,
				   unsigned int old_selector,
				   unsigned int new_selector)
{
	unsigned int ramp_delay = 0;
	int old_volt, new_volt;

	if (rdev->constraints->ramp_delay)
		ramp_delay = rdev->constraints->ramp_delay;
	else if (rdev->desc->ramp_delay)
		ramp_delay = rdev->desc->ramp_delay;

	if (ramp_delay == 0) {
		rdev_warn(rdev, "ramp_delay not set\n");
		return 0;
	}

	
	if (!rdev->desc->ops->list_voltage)
		return -EINVAL;

	old_volt = rdev->desc->ops->list_voltage(rdev, old_selector);
	new_volt = rdev->desc->ops->list_voltage(rdev, new_selector);

	return DIV_ROUND_UP(abs(new_volt - old_volt), ramp_delay);
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_time_sel);

int regulator_sync_voltage(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret, min_uV, max_uV;

	mutex_lock(&rdev->mutex);

	if (!rdev->desc->ops->set_voltage &&
	    !rdev->desc->ops->set_voltage_sel) {
		ret = -EINVAL;
		goto out;
	}

	
	if (!regulator->min_uV && !regulator->max_uV) {
		ret = -EINVAL;
		goto out;
	}

	min_uV = regulator->min_uV;
	max_uV = regulator->max_uV;

	
	ret = regulator_check_voltage(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;

	ret = regulator_check_consumers(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;

	ret = _regulator_do_set_voltage(rdev, min_uV, max_uV);

out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_sync_voltage);

static int _regulator_get_voltage(struct regulator_dev *rdev)
{
	int sel, ret;

	if (rdev->desc->ops->get_voltage_sel) {
		sel = rdev->desc->ops->get_voltage_sel(rdev);
		if (sel < 0)
			return sel;
		ret = rdev->desc->ops->list_voltage(rdev, sel);
	} else if (rdev->desc->ops->get_voltage) {
		ret = rdev->desc->ops->get_voltage(rdev);
	} else if (rdev->desc->ops->list_voltage) {
		ret = rdev->desc->ops->list_voltage(rdev, 0);
	} else if (rdev->desc->fixed_uV && (rdev->desc->n_voltages == 1)) {
		ret = rdev->desc->fixed_uV;
	} else if (rdev->supply) {
		ret = regulator_get_voltage(rdev->supply);
	} else {
		return -EINVAL;
	}

	if (ret < 0)
		return ret;
	return ret - rdev->constraints->uV_offset;
}

int regulator_get_voltage(struct regulator *regulator)
{
	int ret;

	mutex_lock(&regulator->rdev->mutex);

	ret = _regulator_get_voltage(regulator->rdev);

	mutex_unlock(&regulator->rdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_get_voltage);

int regulator_set_current_limit(struct regulator *regulator,
			       int min_uA, int max_uA)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret;

	mutex_lock(&rdev->mutex);

	
	if (!rdev->desc->ops->set_current_limit) {
		ret = -EINVAL;
		goto out;
	}

	
	ret = regulator_check_current_limit(rdev, &min_uA, &max_uA);
	if (ret < 0)
		goto out;

	ret = rdev->desc->ops->set_current_limit(rdev, min_uA, max_uA);
out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_current_limit);

static int _regulator_get_current_limit(struct regulator_dev *rdev)
{
	int ret;

	mutex_lock(&rdev->mutex);

	
	if (!rdev->desc->ops->get_current_limit) {
		ret = -EINVAL;
		goto out;
	}

	ret = rdev->desc->ops->get_current_limit(rdev);
out:
	mutex_unlock(&rdev->mutex);
	return ret;
}

int regulator_get_current_limit(struct regulator *regulator)
{
	return _regulator_get_current_limit(regulator->rdev);
}
EXPORT_SYMBOL_GPL(regulator_get_current_limit);

int regulator_set_mode(struct regulator *regulator, unsigned int mode)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret;
	int regulator_curr_mode;

	mutex_lock(&rdev->mutex);

	
	if (!rdev->desc->ops->set_mode) {
		ret = -EINVAL;
		goto out;
	}

	
	if (rdev->desc->ops->get_mode) {
		regulator_curr_mode = rdev->desc->ops->get_mode(rdev);
		if (regulator_curr_mode == mode) {
			ret = 0;
			goto out;
		}
	}

	
	ret = regulator_mode_constrain(rdev, &mode);
	if (ret < 0)
		goto out;

	ret = rdev->desc->ops->set_mode(rdev, mode);
out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_mode);

static unsigned int _regulator_get_mode(struct regulator_dev *rdev)
{
	int ret;

	mutex_lock(&rdev->mutex);

	
	if (!rdev->desc->ops->get_mode) {
		ret = -EINVAL;
		goto out;
	}

	ret = rdev->desc->ops->get_mode(rdev);
out:
	mutex_unlock(&rdev->mutex);
	return ret;
}

unsigned int regulator_get_mode(struct regulator *regulator)
{
	return _regulator_get_mode(regulator->rdev);
}
EXPORT_SYMBOL_GPL(regulator_get_mode);

int regulator_set_optimum_mode(struct regulator *regulator, int uA_load)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct regulator *consumer;
	int ret, output_uV, input_uV = 0, total_uA_load = 0;
	unsigned int mode;

	if (rdev->supply)
		input_uV = regulator_get_voltage(rdev->supply);

	mutex_lock(&rdev->mutex);

	regulator->uA_load = uA_load;
	ret = regulator_check_drms(rdev);
	if (ret < 0) {
		ret = 0;
		goto out;
	}

	if (!rdev->desc->ops->get_optimum_mode)
		goto out;

	ret = -EINVAL;

	if (!rdev->desc->ops->set_mode)
		goto out;

	
	output_uV = _regulator_get_voltage(rdev);
	if (output_uV <= 0) {
		rdev_err(rdev, "invalid output voltage found\n");
		goto out;
	}

	
	if (input_uV <= 0)
		input_uV = rdev->constraints->input_uV;
	if (input_uV <= 0) {
		rdev_err(rdev, "invalid input voltage found\n");
		goto out;
	}

	
	list_for_each_entry(consumer, &rdev->consumer_list, list)
		total_uA_load += consumer->uA_load;

	mode = rdev->desc->ops->get_optimum_mode(rdev,
						 input_uV, output_uV,
						 total_uA_load);
	ret = regulator_mode_constrain(rdev, &mode);
	if (ret < 0) {
		rdev_err(rdev, "failed to get optimum mode @ %d uA %d -> %d uV\n",
			 total_uA_load, input_uV, output_uV);
		goto out;
	}

	ret = rdev->desc->ops->set_mode(rdev, mode);
	if (ret < 0) {
		rdev_err(rdev, "failed to set optimum mode %x\n", mode);
		goto out;
	}
	ret = mode;
out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_optimum_mode);

int regulator_allow_bypass(struct regulator *regulator, bool enable)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret = 0;

	if (!rdev->desc->ops->set_bypass)
		return 0;

	if (rdev->constraints &&
	    !(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_BYPASS))
		return 0;

	mutex_lock(&rdev->mutex);

	if (enable && !regulator->bypass) {
		rdev->bypass_count++;

		if (rdev->bypass_count == rdev->open_count -
		    rdev->open_offset) {
			ret = rdev->desc->ops->set_bypass(rdev, enable);
			if (ret != 0)
				rdev->bypass_count--;
		}

	} else if (!enable && regulator->bypass) {
		rdev->bypass_count--;

		if (rdev->bypass_count != rdev->open_count -
		    rdev->open_offset) {
			ret = rdev->desc->ops->set_bypass(rdev, enable);
			if (ret != 0)
				rdev->bypass_count++;
		}
	}

	if (ret == 0)
		regulator->bypass = enable;

	mutex_unlock(&rdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_allow_bypass);

int regulator_register_notifier(struct regulator *regulator,
			      struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&regulator->rdev->notifier,
						nb);
}
EXPORT_SYMBOL_GPL(regulator_register_notifier);

int regulator_unregister_notifier(struct regulator *regulator,
				struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&regulator->rdev->notifier,
						  nb);
}
EXPORT_SYMBOL_GPL(regulator_unregister_notifier);

static int _notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data)
{
	
	return blocking_notifier_call_chain(&rdev->notifier, event, data);
}

int regulator_bulk_get(struct device *dev, int num_consumers,
		       struct regulator_bulk_data *consumers)
{
	int i;
	int ret;

	for (i = 0; i < num_consumers; i++)
		consumers[i].consumer = NULL;

	for (i = 0; i < num_consumers; i++) {
		consumers[i].consumer = regulator_get(dev,
						      consumers[i].supply);
		if (IS_ERR(consumers[i].consumer)) {
			ret = PTR_ERR(consumers[i].consumer);
			dev_err(dev, "Failed to get supply '%s': %d\n",
				consumers[i].supply, ret);
			consumers[i].consumer = NULL;
			goto err;
		}
	}

	return 0;

err:
	while (--i >= 0)
		regulator_put(consumers[i].consumer);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_get);

static void regulator_bulk_enable_async(void *data, async_cookie_t cookie)
{
	struct regulator_bulk_data *bulk = data;

	bulk->ret = regulator_enable(bulk->consumer);
}

int regulator_bulk_enable(int num_consumers,
			  struct regulator_bulk_data *consumers)
{
	ASYNC_DOMAIN_EXCLUSIVE(async_domain);
	int i;
	int ret = 0;

	for (i = 0; i < num_consumers; i++) {
		if (consumers[i].consumer->always_on)
			consumers[i].ret = 0;
		else
			async_schedule_domain(regulator_bulk_enable_async,
					      &consumers[i], &async_domain);
	}

	async_synchronize_full_domain(&async_domain);

	
	for (i = 0; i < num_consumers; i++) {
		if (consumers[i].ret != 0) {
			ret = consumers[i].ret;
			goto err;
		}
	}

	return 0;

err:
	for (i = 0; i < num_consumers; i++) {
		if (consumers[i].ret < 0)
			pr_err("Failed to enable %s: %d\n", consumers[i].supply,
			       consumers[i].ret);
		else
			regulator_disable(consumers[i].consumer);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_enable);

int regulator_bulk_set_voltage(int num_consumers,
			       struct regulator_bulk_data *consumers)
{
	int i;
	int rc;

	for (i = 0; i < num_consumers; i++) {
		if (!consumers[i].min_uV && !consumers[i].max_uV)
			continue;
		rc = regulator_set_voltage(consumers[i].consumer,
				consumers[i].min_uV,
				consumers[i].max_uV);
		if (rc)
			goto err;
	}

	return 0;

err:
	pr_err("Failed to set voltage for %s: %d\n", consumers[i].supply, rc);
	return rc;
}
EXPORT_SYMBOL_GPL(regulator_bulk_set_voltage);

int regulator_bulk_disable(int num_consumers,
			   struct regulator_bulk_data *consumers)
{
	int i;
	int ret, r;

	for (i = num_consumers - 1; i >= 0; --i) {
		ret = regulator_disable(consumers[i].consumer);
		if (ret != 0)
			goto err;
	}

	return 0;

err:
	pr_err("Failed to disable %s: %d\n", consumers[i].supply, ret);
	for (++i; i < num_consumers; ++i) {
		r = regulator_enable(consumers[i].consumer);
		if (r != 0)
			pr_err("Failed to reename %s: %d\n",
			       consumers[i].supply, r);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_disable);

int regulator_bulk_force_disable(int num_consumers,
			   struct regulator_bulk_data *consumers)
{
	int i;
	int ret;

	for (i = 0; i < num_consumers; i++)
		consumers[i].ret =
			    regulator_force_disable(consumers[i].consumer);

	for (i = 0; i < num_consumers; i++) {
		if (consumers[i].ret != 0) {
			ret = consumers[i].ret;
			goto out;
		}
	}

	return 0;
out:
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_force_disable);

void regulator_bulk_free(int num_consumers,
			 struct regulator_bulk_data *consumers)
{
	int i;

	for (i = 0; i < num_consumers; i++) {
		regulator_put(consumers[i].consumer);
		consumers[i].consumer = NULL;
	}
}
EXPORT_SYMBOL_GPL(regulator_bulk_free);

int regulator_notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data)
{
	_notifier_call_chain(rdev, event, data);
	return NOTIFY_DONE;

}
EXPORT_SYMBOL_GPL(regulator_notifier_call_chain);

int regulator_mode_to_status(unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return REGULATOR_STATUS_FAST;
	case REGULATOR_MODE_NORMAL:
		return REGULATOR_STATUS_NORMAL;
	case REGULATOR_MODE_IDLE:
		return REGULATOR_STATUS_IDLE;
	case REGULATOR_MODE_STANDBY:
		return REGULATOR_STATUS_STANDBY;
	default:
		return REGULATOR_STATUS_UNDEFINED;
	}
}
EXPORT_SYMBOL_GPL(regulator_mode_to_status);

static int add_regulator_attributes(struct regulator_dev *rdev)
{
	struct device *dev = &rdev->dev;
	const struct regulator_ops *ops = rdev->desc->ops;
	int status = 0;

	
	if ((ops->get_voltage && ops->get_voltage(rdev) >= 0) ||
	    (ops->get_voltage_sel && ops->get_voltage_sel(rdev) >= 0) ||
	    (ops->list_voltage && ops->list_voltage(rdev, 0) >= 0) ||
		(rdev->desc->fixed_uV && (rdev->desc->n_voltages == 1))) {
		status = device_create_file(dev, &dev_attr_microvolts);
		if (status < 0)
			return status;
	}
	if (ops->get_current_limit) {
		status = device_create_file(dev, &dev_attr_microamps);
		if (status < 0)
			return status;
	}
	if (ops->get_mode) {
		status = device_create_file(dev, &dev_attr_opmode);
		if (status < 0)
			return status;
	}
	if (rdev->ena_pin || ops->is_enabled) {
		status = device_create_file(dev, &dev_attr_state);
		if (status < 0)
			return status;
	}
	if (ops->get_status) {
		status = device_create_file(dev, &dev_attr_status);
		if (status < 0)
			return status;
	}
	if (ops->get_bypass) {
		status = device_create_file(dev, &dev_attr_bypass);
		if (status < 0)
			return status;
	}

	
	if (rdev->desc->type == REGULATOR_CURRENT) {
		status = device_create_file(dev, &dev_attr_requested_microamps);
		if (status < 0)
			return status;
	}

	if (!rdev->constraints)
		return status;

	
	if (ops->set_voltage || ops->set_voltage_sel) {
		status = device_create_file(dev, &dev_attr_min_microvolts);
		if (status < 0)
			return status;
		status = device_create_file(dev, &dev_attr_max_microvolts);
		if (status < 0)
			return status;
	}
	if (ops->set_current_limit) {
		status = device_create_file(dev, &dev_attr_min_microamps);
		if (status < 0)
			return status;
		status = device_create_file(dev, &dev_attr_max_microamps);
		if (status < 0)
			return status;
	}

	status = device_create_file(dev, &dev_attr_suspend_standby_state);
	if (status < 0)
		return status;
	status = device_create_file(dev, &dev_attr_suspend_mem_state);
	if (status < 0)
		return status;
	status = device_create_file(dev, &dev_attr_suspend_disk_state);
	if (status < 0)
		return status;

	if (ops->set_suspend_voltage) {
		status = device_create_file(dev,
				&dev_attr_suspend_standby_microvolts);
		if (status < 0)
			return status;
		status = device_create_file(dev,
				&dev_attr_suspend_mem_microvolts);
		if (status < 0)
			return status;
		status = device_create_file(dev,
				&dev_attr_suspend_disk_microvolts);
		if (status < 0)
			return status;
	}

	if (ops->set_suspend_mode) {
		status = device_create_file(dev,
				&dev_attr_suspend_standby_mode);
		if (status < 0)
			return status;
		status = device_create_file(dev,
				&dev_attr_suspend_mem_mode);
		if (status < 0)
			return status;
		status = device_create_file(dev,
				&dev_attr_suspend_disk_mode);
		if (status < 0)
			return status;
	}

	return status;
}

#ifdef CONFIG_DEBUG_FS

#define MAX_DEBUG_BUF_LEN 50

static DEFINE_MUTEX(debug_buf_mutex);
static char debug_buf[MAX_DEBUG_BUF_LEN];

static int reg_debug_enable_set(void *data, u64 val)
{
	int err_info;
	if (IS_ERR(data) || data == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(data));
		return -ENOMEM;
	}

	if (val)
		err_info = regulator_enable(data);
	else
		err_info = regulator_disable(data);

	return err_info;
}

static int reg_debug_enable_get(void *data, u64 *val)
{
	if (IS_ERR(data) || data == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(data));
		return -ENOMEM;
	}

	*val = regulator_is_enabled(data);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(reg_enable_fops, reg_debug_enable_get,
			reg_debug_enable_set, "%llu\n");

static int reg_debug_bypass_enable_get(void *data, u64 *val)
{
	struct regulator *regulator = data;
	struct regulator_dev *rdev = regulator->rdev;
	bool enable = false;
	int rc = 0;

	mutex_lock(&rdev->mutex);
	if (rdev->desc->ops->get_bypass) {
		rc = rdev->desc->ops->get_bypass(rdev, &enable);
		if (rc)
			pr_err("get_bypass() failed, rc=%d\n", rc);
	} else {
		enable = (rdev->bypass_count == rdev->open_count
			  - rdev->open_offset);
	}
	mutex_unlock(&rdev->mutex);

	*val = enable;

	return rc;
}

static int reg_debug_bypass_enable_set(void *data, u64 val)
{
	struct regulator *regulator = data;
	struct regulator_dev *rdev = regulator->rdev;
	int ret = 0;

	mutex_lock(&rdev->mutex);
	rdev->open_offset = 0;
	mutex_unlock(&rdev->mutex);

	ret = regulator_allow_bypass(data, val);

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(reg_bypass_enable_fops, reg_debug_bypass_enable_get,
			reg_debug_bypass_enable_set, "%llu\n");

static int reg_debug_fdisable_set(void *data, u64 val)
{
	int err_info;
	if (IS_ERR(data) || data == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(data));
		return -ENOMEM;
	}

	if (val > 0)
		err_info = regulator_force_disable(data);
	else
		err_info = 0;

	return err_info;
}

DEFINE_SIMPLE_ATTRIBUTE(reg_fdisable_fops, reg_debug_enable_get,
			reg_debug_fdisable_set, "%llu\n");

static ssize_t reg_debug_volt_set(struct file *file, const char __user *buf,
					size_t count, loff_t *ppos)
{
	int err_info, filled;
	int min, max = -1;
	if (IS_ERR(file) || file == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(file));
		return -ENOMEM;
	}

	if (count < MAX_DEBUG_BUF_LEN) {
		mutex_lock(&debug_buf_mutex);

		if (copy_from_user(debug_buf, (void __user *) buf, count))
			return -EFAULT;

		debug_buf[count] = '\0';
		filled = sscanf(debug_buf, "%d %d", &min, &max);

		mutex_unlock(&debug_buf_mutex);
		
		if (filled < 2 || min < 0 || max < min) {
			pr_info("Error, correct format: 'echo \"min max\""
				" > voltage");
			return -ENOMEM;
		} else {
			err_info = regulator_set_voltage(file->private_data,
							min, max);
		}
	} else {
		pr_err("Error-Input voltage pair"
				" string exceeds maximum buffer length");

		return -ENOMEM;
	}

	return count;
}

static ssize_t reg_debug_volt_get(struct file *file, char __user *buf,
					size_t count, loff_t *ppos)
{
	int voltage, output, rc;
	if (IS_ERR(file) || file == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(file));
		return -ENOMEM;
	}

	voltage = regulator_get_voltage(file->private_data);
	mutex_lock(&debug_buf_mutex);

	output = snprintf(debug_buf, MAX_DEBUG_BUF_LEN-1, "%d\n", voltage);
	rc = simple_read_from_buffer((void __user *) buf, output, ppos,
					(void *) debug_buf, output);

	mutex_unlock(&debug_buf_mutex);

	return rc;
}

static int reg_debug_volt_open(struct inode *inode, struct file *file)
{
	if (IS_ERR(file) || file == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(file));
		return -ENOMEM;
	}

	file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations reg_volt_fops = {
	.write	= reg_debug_volt_set,
	.open   = reg_debug_volt_open,
	.read	= reg_debug_volt_get,
};

static int reg_debug_mode_set(void *data, u64 val)
{
	int err_info;
	if (IS_ERR(data) || data == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(data));
		return -ENOMEM;
	}

	err_info = regulator_set_mode(data, (unsigned int)val);

	return err_info;
}

static int reg_debug_mode_get(void *data, u64 *val)
{
	int err_info;
	if (IS_ERR(data) || data == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(data));
		return -ENOMEM;
	}

	err_info = regulator_get_mode(data);

	if (err_info < 0) {
		pr_err("Regulator_get_mode returned an error!\n");
		return -ENOMEM;
	} else {
		*val = err_info;
		return 0;
	}
}

DEFINE_SIMPLE_ATTRIBUTE(reg_mode_fops, reg_debug_mode_get,
			reg_debug_mode_set, "%llu\n");

static int reg_debug_optimum_mode_set(void *data, u64 val)
{
	int err_info;
	if (IS_ERR(data) || data == NULL) {
		pr_err("Function Input Error %ld\n", PTR_ERR(data));
		return -ENOMEM;
	}

	err_info = regulator_set_optimum_mode(data, (unsigned int)val);

	if (err_info < 0) {
		pr_err("Regulator_set_optimum_mode returned an error!\n");
		return err_info;
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(reg_optimum_mode_fops, reg_debug_mode_get,
			reg_debug_optimum_mode_set, "%llu\n");

static int reg_debug_consumers_show(struct seq_file *m, void *v)
{
	struct regulator_dev *rdev = m->private;
	struct regulator *reg;
	char *supply_name;

	if (!rdev) {
		pr_err("regulator device missing");
		return -EINVAL;
	}

	mutex_lock(&rdev->mutex);

	
	if (rdev->open_count)
		seq_printf(m, "Device-Supply                    "
			"EN    Min_uV   Max_uV  load_uA\n");

	list_for_each_entry(reg, &rdev->consumer_list, list) {
		if (reg->supply_name)
			supply_name = reg->supply_name;
		else
			supply_name = "(null)-(null)";

		seq_printf(m, "%-32s %c   %8d %8d %8d\n", supply_name,
			(reg->enabled ? 'Y' : 'N'), reg->min_uV, reg->max_uV,
			reg->uA_load);
	}

	mutex_unlock(&rdev->mutex);

	return 0;
}

static int reg_debug_consumers_open(struct inode *inode, struct file *file)
{
	return single_open(file, reg_debug_consumers_show, inode->i_private);
}

static const struct file_operations reg_consumers_fops = {
	.owner		= THIS_MODULE,
	.open		= reg_debug_consumers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void rdev_deinit_debugfs(struct regulator_dev *rdev)
{
	if (!IS_ERR_OR_NULL(rdev)) {
		debugfs_remove_recursive(rdev->debugfs);
		rdev->debug_consumer->debugfs = NULL;
		regulator_put(rdev->debug_consumer);
	}
}

static void rdev_init_debugfs(struct regulator_dev *rdev)
{
	struct dentry *err_ptr = NULL;
	struct regulator *reg;
	const struct regulator_ops *reg_ops;
	mode_t mode;

	if (IS_ERR(rdev) || rdev == NULL ||
		IS_ERR(debugfs_root) || debugfs_root == NULL) {
		pr_err("Error-Bad Function Input\n");
		goto error;
	}

	rdev->debugfs = debugfs_create_dir(rdev_get_name(rdev), debugfs_root);
	if (IS_ERR(rdev->debugfs) || !rdev->debugfs) {
		rdev_warn(rdev, "Failed to create debugfs directory\n");
		rdev->debugfs = NULL;
		goto error;
	}

	debugfs_create_u32("use_count", 0444, rdev->debugfs,
			   &rdev->use_count);
	debugfs_create_u32("open_count", 0444, rdev->debugfs,
			   &rdev->open_count);
	debugfs_create_u32("bypass_count", 0444, rdev->debugfs,
			   &rdev->bypass_count);
	debugfs_create_file("consumers", 0444, rdev->debugfs, rdev,
			    &reg_consumers_fops);

	reg = regulator_get(NULL, rdev->desc->name);
	if (IS_ERR(reg) || reg == NULL) {
		pr_err("Error-Bad Function Input\n");
		goto error;
	}
	rdev->debug_consumer = reg;

	rdev->open_offset = 1;
	reg_ops = rdev->desc->ops;
	mode = S_IRUGO | S_IWUSR;
	
	if (mode)
		err_ptr = debugfs_create_file("enable", mode, rdev->debugfs,
						reg, &reg_enable_fops);
	if (IS_ERR(err_ptr)) {
		pr_err("Error-Could not create enable file\n");
		goto error;
	}

	mode = 0;
	
	if (reg_ops->set_bypass)
		mode = S_IWUSR | S_IRUGO;

	if (mode)
		err_ptr = debugfs_create_file("bypass", mode,
					      rdev->debugfs, reg,
					      &reg_bypass_enable_fops);
	if (IS_ERR(err_ptr)) {
		pr_err("Error-Could not create bypass enable file\n");
		goto error;
	}

	mode = 0;
	
	if (reg_ops->is_enabled)
		mode |= S_IRUGO;
	if (reg_ops->enable || reg_ops->disable)
		mode |= S_IWUSR;
	if (mode)
		err_ptr = debugfs_create_file("force_disable", mode,
					rdev->debugfs, reg, &reg_fdisable_fops);
	if (IS_ERR(err_ptr)) {
		pr_err("Error-Could not create force_disable file\n");
		goto error;
	}

	mode = 0;
	
	if (reg_ops->get_voltage)
		mode |= S_IRUGO;
	if (reg_ops->set_voltage)
		mode |= S_IWUSR;
	if (mode)
		err_ptr = debugfs_create_file("voltage", mode, rdev->debugfs,
						reg, &reg_volt_fops);
	if (IS_ERR(err_ptr)) {
		pr_err("Error-Could not create voltage file\n");
		goto error;
	}

	mode = 0;
	
	if (reg_ops->get_mode)
		mode |= S_IRUGO;
	if (reg_ops->set_mode)
		mode |= S_IWUSR;
	if (mode)
		err_ptr = debugfs_create_file("mode", mode, rdev->debugfs,
						reg, &reg_mode_fops);
	if (IS_ERR(err_ptr)) {
		pr_err("Error-Could not create mode file\n");
		goto error;
	}

	mode = 0;
	
	if (reg_ops->get_mode)
		mode |= S_IRUGO;
	if (reg_ops->set_mode)
		mode |= S_IWUSR;
	if (mode)
		err_ptr = debugfs_create_file("optimum_mode", mode,
				rdev->debugfs, reg, &reg_optimum_mode_fops);
	if (IS_ERR(err_ptr)) {
		pr_err("Error-Could not create optimum_mode file\n");
		goto error;
	}

	return;

error:
	rdev_deinit_debugfs(rdev);
	return;
}

#else

static inline void rdev_deinit_debugfs(struct regulator_dev *rdev)
{
}

static inline void rdev_init_debugfs(struct regulator_dev *rdev)
{
}

#endif

struct regulator_dev *
regulator_register(const struct regulator_desc *regulator_desc,
		   const struct regulator_config *config)
{
	const struct regulation_constraints *constraints = NULL;
	const struct regulator_init_data *init_data;
	static atomic_t regulator_no = ATOMIC_INIT(0);
	struct regulator_dev *rdev;
	struct device *dev;
	int ret, i;
	const char *supply = NULL;

	if (regulator_desc == NULL || config == NULL)
		return ERR_PTR(-EINVAL);

	dev = config->dev;
	WARN_ON(!dev);

	if (regulator_desc->name == NULL || regulator_desc->ops == NULL)
		return ERR_PTR(-EINVAL);

	if (regulator_desc->type != REGULATOR_VOLTAGE &&
	    regulator_desc->type != REGULATOR_CURRENT)
		return ERR_PTR(-EINVAL);

	
	WARN_ON(regulator_desc->ops->get_voltage &&
		regulator_desc->ops->get_voltage_sel);
	WARN_ON(regulator_desc->ops->set_voltage &&
		regulator_desc->ops->set_voltage_sel);

	
	if (regulator_desc->ops->get_voltage_sel &&
	    !regulator_desc->ops->list_voltage) {
		return ERR_PTR(-EINVAL);
	}
	if (regulator_desc->ops->set_voltage_sel &&
	    !regulator_desc->ops->list_voltage) {
		return ERR_PTR(-EINVAL);
	}

	rdev = kzalloc(sizeof(struct regulator_dev), GFP_KERNEL);
	if (rdev == NULL)
		return ERR_PTR(-ENOMEM);

	init_data = regulator_of_get_init_data(dev, regulator_desc,
					       &rdev->dev.of_node);
	if (!init_data) {
		init_data = config->init_data;
		rdev->dev.of_node = of_node_get(config->of_node);
	}

	mutex_lock(&regulator_list_mutex);

	mutex_init(&rdev->mutex);
	rdev->reg_data = config->driver_data;
	rdev->owner = regulator_desc->owner;
	rdev->desc = regulator_desc;
	if (config->regmap)
		rdev->regmap = config->regmap;
	else if (dev_get_regmap(dev, NULL))
		rdev->regmap = dev_get_regmap(dev, NULL);
	else if (dev->parent)
		rdev->regmap = dev_get_regmap(dev->parent, NULL);
	INIT_LIST_HEAD(&rdev->consumer_list);
	INIT_LIST_HEAD(&rdev->list);
	BLOCKING_INIT_NOTIFIER_HEAD(&rdev->notifier);
	INIT_DELAYED_WORK(&rdev->disable_work, regulator_disable_work);

	
	if (init_data && init_data->regulator_init) {
		ret = init_data->regulator_init(rdev->reg_data);
		if (ret < 0)
			goto clean;
	}

	
	rdev->dev.class = &regulator_class;
	rdev->dev.parent = dev;
	dev_set_name(&rdev->dev, "regulator.%d",
		     atomic_inc_return(&regulator_no) - 1);
	ret = device_register(&rdev->dev);
	if (ret != 0) {
		put_device(&rdev->dev);
		goto clean;
	}

	dev_set_drvdata(&rdev->dev, rdev);

	if (config->ena_gpio && gpio_is_valid(config->ena_gpio)) {
		ret = regulator_ena_gpio_request(rdev, config);
		if (ret != 0) {
			rdev_err(rdev, "Failed to request enable GPIO%d: %d\n",
				 config->ena_gpio, ret);
			goto wash;
		}
	}

	
	if (init_data)
		constraints = &init_data->constraints;

	ret = set_machine_constraints(rdev, constraints);
	if (ret < 0)
		goto scrub;

	
	ret = add_regulator_attributes(rdev);
	if (ret < 0)
		goto scrub;

	if (init_data && init_data->supply_regulator)
		supply = init_data->supply_regulator;
	else if (regulator_desc->supply_name)
		supply = regulator_desc->supply_name;

	if (supply) {
		struct regulator_dev *r;

		r = regulator_dev_lookup(dev, supply, &ret);

		if (ret == -ENODEV) {
			ret = 0;
			goto add_dev;
		} else if (!r) {
			dev_err(dev, "Failed to find supply %s\n", supply);
			ret = -EPROBE_DEFER;
			goto scrub;
		}

		ret = set_supply(rdev, r);
		if (ret < 0)
			goto scrub;
	}

add_dev:
	
	if (init_data) {
		for (i = 0; i < init_data->num_consumer_supplies; i++) {
			ret = set_consumer_device_supply(rdev,
				init_data->consumer_supplies[i].dev_name,
				init_data->consumer_supplies[i].supply);
			if (ret < 0) {
				dev_err(dev, "Failed to set supply %s\n",
					init_data->consumer_supplies[i].supply);
				goto unset_supplies;
			}
		}
	}

	list_add(&rdev->list, &regulator_list);

	mutex_unlock(&regulator_list_mutex);
	rdev_init_debugfs(rdev);
	rdev->proxy_consumer = regulator_proxy_consumer_register(dev,
							config->of_node);
	return rdev;

out:
	mutex_unlock(&regulator_list_mutex);
	return rdev;

unset_supplies:
	unset_regulator_supplies(rdev);

scrub:
	if (rdev->supply)
		_regulator_put(rdev->supply);
	regulator_ena_gpio_free(rdev);
	kfree(rdev->constraints);
wash:
	device_unregister(&rdev->dev);
	
	rdev = ERR_PTR(ret);
	goto out;

clean:
	kfree(rdev);
	rdev = ERR_PTR(ret);
	goto out;
}
EXPORT_SYMBOL_GPL(regulator_register);

void regulator_unregister(struct regulator_dev *rdev)
{
	if (rdev == NULL)
		return;

	if (rdev->supply) {
		while (rdev->use_count--)
			regulator_disable(rdev->supply);
		regulator_put(rdev->supply);
	}
	regulator_proxy_consumer_unregister(rdev->proxy_consumer);
	rdev_deinit_debugfs(rdev);
	mutex_lock(&regulator_list_mutex);
	flush_work(&rdev->disable_work.work);
	WARN_ON(rdev->open_count);
	unset_regulator_supplies(rdev);
	list_del(&rdev->list);
	kfree(rdev->constraints);
	regulator_ena_gpio_free(rdev);
	of_node_put(rdev->dev.of_node);
	device_unregister(&rdev->dev);
	mutex_unlock(&regulator_list_mutex);
}
EXPORT_SYMBOL_GPL(regulator_unregister);

int regulator_suspend_prepare(suspend_state_t state)
{
	struct regulator_dev *rdev;
	int ret = 0;

	
	if (state == PM_SUSPEND_ON)
		return -EINVAL;

	mutex_lock(&regulator_list_mutex);
	list_for_each_entry(rdev, &regulator_list, list) {

		mutex_lock(&rdev->mutex);
		ret = suspend_prepare(rdev, state);
		mutex_unlock(&rdev->mutex);

		if (ret < 0) {
			rdev_err(rdev, "failed to prepare\n");
			goto out;
		}
	}
out:
	mutex_unlock(&regulator_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_suspend_prepare);

int regulator_suspend_finish(void)
{
	struct regulator_dev *rdev;
	int ret = 0, error;

	mutex_lock(&regulator_list_mutex);
	list_for_each_entry(rdev, &regulator_list, list) {
		mutex_lock(&rdev->mutex);
		if (rdev->use_count > 0  || rdev->constraints->always_on) {
			if (!_regulator_is_enabled(rdev)) {
				error = _regulator_do_enable(rdev);
				if (error)
					ret = error;
			}
		} else {
			if (!have_full_constraints())
				goto unlock;
			if (!_regulator_is_enabled(rdev))
				goto unlock;

			error = _regulator_do_disable(rdev);
			if (error)
				ret = error;
		}
unlock:
		mutex_unlock(&rdev->mutex);
	}
	mutex_unlock(&regulator_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_suspend_finish);

void regulator_has_full_constraints(void)
{
	has_full_constraints = 1;
}
EXPORT_SYMBOL_GPL(regulator_has_full_constraints);

void *rdev_get_drvdata(struct regulator_dev *rdev)
{
	return rdev->reg_data;
}
EXPORT_SYMBOL_GPL(rdev_get_drvdata);

void *regulator_get_drvdata(struct regulator *regulator)
{
	return regulator->rdev->reg_data;
}
EXPORT_SYMBOL_GPL(regulator_get_drvdata);

void regulator_set_drvdata(struct regulator *regulator, void *data)
{
	regulator->rdev->reg_data = data;
}
EXPORT_SYMBOL_GPL(regulator_set_drvdata);

int rdev_get_id(struct regulator_dev *rdev)
{
	return rdev->desc->id;
}
EXPORT_SYMBOL_GPL(rdev_get_id);

struct device *rdev_get_dev(struct regulator_dev *rdev)
{
	return &rdev->dev;
}
EXPORT_SYMBOL_GPL(rdev_get_dev);

void *regulator_get_init_drvdata(struct regulator_init_data *reg_init_data)
{
	return reg_init_data->driver_data;
}
EXPORT_SYMBOL_GPL(regulator_get_init_drvdata);

#ifdef CONFIG_DEBUG_FS
static ssize_t supply_map_read_file(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	ssize_t len, ret = 0;
	struct regulator_map *map;

	if (!buf)
		return -ENOMEM;

	list_for_each_entry(map, &regulator_map_list, list) {
		len = snprintf(buf + ret, PAGE_SIZE - ret,
			       "%s -> %s.%s\n",
			       rdev_get_name(map->regulator), map->dev_name,
			       map->supply);
		if (len >= 0)
			ret += len;
		if (ret > PAGE_SIZE) {
			ret = PAGE_SIZE;
			break;
		}
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return ret;
}
#endif

static const struct file_operations supply_map_fops = {
#ifdef CONFIG_DEBUG_FS
	.read = supply_map_read_file,
	.llseek = default_llseek,
#endif
};

static int __init regulator_init(void)
{
	int ret;

	ret = class_register(&regulator_class);

	debugfs_root = debugfs_create_dir("regulator", NULL);
	if (!debugfs_root)
		pr_warn("regulator: Failed to create debugfs directory\n");

	debugfs_create_file("supply_map", 0444, debugfs_root, NULL,
			    &supply_map_fops);

	regulator_dummy_init();

	return ret;
}

core_initcall(regulator_init);

static int __init regulator_init_complete(void)
{
	struct regulator_dev *rdev;
	const struct regulator_ops *ops;
	struct regulation_constraints *c;
	int enabled, ret;

	if (of_have_populated_dt())
		has_full_constraints = true;

	mutex_lock(&regulator_list_mutex);

	list_for_each_entry(rdev, &regulator_list, list) {
		ops = rdev->desc->ops;
		c = rdev->constraints;

		if (c && c->always_on)
			continue;

		if (c && !(c->valid_ops_mask & REGULATOR_CHANGE_STATUS))
			continue;

		mutex_lock(&rdev->mutex);

		if (rdev->use_count)
			goto unlock;

		enabled = _regulator_is_enabled(rdev);

		if (!enabled)
			goto unlock;
		else if (ops->is_enabled && !ops->is_enabled(rdev))
			rdev_info(rdev, "actually enabled, but unused regulator\n");

		if (have_full_constraints()) {
			rdev_info(rdev, "disabling\n");
			ret = _regulator_do_disable(rdev);
			if (ret != 0)
				rdev_err(rdev, "couldn't disable: %d\n", ret);
		} else {
			rdev_warn(rdev, "incomplete constraints, leaving on\n");
		}

unlock:
		mutex_unlock(&rdev->mutex);
	}

	mutex_unlock(&regulator_list_mutex);

	return 0;
}
late_initcall_sync(regulator_init_complete);
