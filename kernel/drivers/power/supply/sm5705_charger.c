// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silicon Mitus SM5705 charger
 *
 * Reports whether USB power is present and what the charger does, and
 * applies the charge voltage, charge current and termination current of
 * the monitored battery. With an extcon device (the SM5705 MUIC) it sets
 * the input current limit for the detected USB charger type, including
 * Samsung AFC chargers at 9 V. Without one
 * the limit stays as the bootloader set it. Either way the charger's AICL
 * lowers the input current when the source sags.
 *
 * The boost converter, which otherwise runs the flash LED from the battery,
 * can also drive VBUS: a regulator provides that as the USB OTG supply.
 *
 * The same I2C device also holds the flash LED controller, which is not
 * handled here. Register layout from the Samsung vendor driver
 * (sm5705_charger.c).
 */

#include <linux/bitfield.h>
#include <linux/devm-helpers.h>
#include <linux/extcon.h>
#include <linux/extcon/sm5705-muic.h>
#include <linux/fixp-arith.h>
#include <linux/i2c.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define SM5705_REG_INT1			0x00
#define SM5705_REG_INTMSK1		0x04
#define SM5705_REG_STATUS1		0x08
#define SM5705_REG_STATUS2		0x09
#define SM5705_REG_STATUS3		0x0a
#define SM5705_REG_CNTL			0x0c
#define SM5705_REG_VBUSCNTL		0x0d
#define SM5705_REG_CHGCNTL2		0x10
#define SM5705_REG_CHGCNTL4		0x12
#define SM5705_REG_CHGCNTL5		0x13
#define SM5705_REG_CHGCNTL6		0x14
#define SM5705_REG_FLED1CNTL1		0x17
#define SM5705_REG_FLED1CNTL4		0x1a
#define SM5705_REG_FLEDCNTL6		0x20
#define SM5705_REG_DEVICE_ID		0x37

/* STATUS1 / INT1 */
#define SM5705_VBUSPOK			BIT(0)
#define SM5705_VBUSUVLO			BIT(1)
#define SM5705_VBUSOVP			BIT(2)
/* STATUS2 / INT2 */
#define SM5705_BATOVP			BIT(1)
#define SM5705_CHGON			BIT(3)
#define SM5705_TOPOFF			BIT(5)
#define SM5705_DONE			BIT(6)
/* STATUS3 / INT3 */
#define SM5705_THEMSHDN			BIT(1)
#define SM5705_OTGFAIL			BIT(2)
#define SM5705_PRETMROFF		BIT(4)
#define SM5705_FASTTMROFF		BIT(5)

#define SM5705_CNTL_MODE_MASK		GENMASK(2, 0)
#define SM5705_CNTL_MODE_CHG_OFF	4
#define SM5705_CNTL_MODE_CHG_ON		5
#define SM5705_CNTL_MODE_FLASH_BOOST	6
#define SM5705_CNTL_MODE_USB_OTG	7

/* FLED1: the camera flash, used as a torch ("movie mode") over I2C */
#define SM5705_FLEDEN_MASK		GENMASK(1, 0)
#define SM5705_FLEDEN_OFF		0
#define SM5705_FLEDEN_MOVIE		1
#define SM5705_TORCH_MASK		GENMASK(4, 0)
#define SM5705_TORCH_STEP_UA		10000	/* 10 mA + 10 mA steps */
#define SM5705_TORCH_MAX_UA		320000
#define SM5705_TORCH_DEFAULT_UA		180000	/* stock's camera torch */

/* USB OTG: the boost onto VBUS, at Samsung's settings for an OTG cable */
#define SM5705_BSTOUT_MASK		GENMASK(3, 0)	/* 4.0 V + 0.1 V steps */
#define SM5705_BSTOUT_OTG		0xb		/* 5.1 V */
#define SM5705_OTG_UV			5100000
#define SM5705_OTG_CURRENT_MASK		GENMASK(3, 2)
#define SM5705_OTG_CURRENT_1500MA	3

#define SM5705_VBUSLIMIT_MASK		GENMASK(6, 0)
#define SM5705_FASTCHG_MASK		GENMASK(5, 0)
#define SM5705_BATREG_MASK		GENMASK(5, 0)
#define SM5705_TOPOFF_MASK		GENMASK(3, 0)

/* Input current limits per USB charger type */
#define SM5705_SDP_INPUT_LIMIT_UA	500000	/* USB 2.0 */
#define SM5705_CDP_INPUT_LIMIT_UA	1500000	/* BC1.2 */
#define SM5705_DCP_INPUT_LIMIT_UA	1800000	/* Samsung's wall charger setting */
#define SM5705_FAST_INPUT_LIMIT_UA	1650000	/* Samsung's 9 V AFC setting */
#define SM5705_TYPEC_INPUT_LIMIT_UA	3000000	/* USB-C at 5 V: 3 A at most */
#define SM5705_HV_MIN_UV		5500000	/* above this, the 9 V limit */

/*
 * Charge current with a 15 W source (Samsung: 9 V AFC), else the default
 * (the bootloader's setting) unless the battery node gives one
 */
#define SM5705_FAST_CHARGE_UA		2700000
#define SM5705_DEFAULT_CHARGE_UA	2000000
#define SM5705_FAST_SOURCE_MW		14000	/* 9 V x 1.65 A counts too */

/*
 * Battery temperature guard, in tenths of a degree, after Samsung's
 * firmware: no charging outside the battery's alert range (0-50 C unless
 * the battery node says otherwise), 4.10 V and 1.35 A when warm, 0.9 A
 * when cool. Each zone is left only past its recovery point.
 */
#define SM5705_TEMP_ALERT_MIN		0
#define SM5705_TEMP_ALERT_MAX		500
#define SM5705_TEMP_ALERT_HYST		50
#define SM5705_TEMP_WARM		410
#define SM5705_TEMP_WARM_HYST		20
#define SM5705_TEMP_COOL		150
#define SM5705_TEMP_COOL_HYST		50
#define SM5705_WARM_VOLTAGE_UV		4100000
#define SM5705_WARM_CURRENT_UA		1350000
#define SM5705_COOL_CURRENT_UA		900000
#define SM5705_TEMP_POLL		(30 * HZ)

/*
 * A board thermistor next to the charger, after Samsung's battery,chg_high_temp,
 * _recovery and battery,chg_charging_limit_current, in tenths of a degree.
 */
#define SM5705_THERM_HOT		500
#define SM5705_THERM_RECOVERY		470
#define SM5705_THERM_CURRENT_UA		1400000

enum sm5705_temp_zone {
	SM5705_ZONE_NORMAL,
	SM5705_ZONE_COOL,
	SM5705_ZONE_WARM,
	SM5705_ZONE_COLD,
	SM5705_ZONE_HOT,
};

/* BATREG: 0 is 3.8 V, 1 is 4.0 V, then 4.01 V in 10 mV steps */
#define SM5705_BATREG_MIN_UV		4010000
#define SM5705_BATREG_MAX_UV		4620000

struct sm5705_chg {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *psy;
	struct power_supply_battery_info *bat;
	struct extcon_dev *edev;
	struct notifier_block extcon_nb;
	struct work_struct cable_work;
	int usb_type;
	bool fast;			/* a 15 W source is attached */
	bool hv;			/* VBUS above 5 V (9 V AFC) */
	bool usb_c;			/* a USB-C source (not a legacy cable) */
	struct device_node *muic_np;	/* for holding VBUS at 5 V */
	bool hold_5v;

	struct led_classdev torch;
	bool torch_on;

	struct regulator_dev *otg;
	bool otg_on;
	unsigned int bstout;		/* the flash boost's voltage, at probe */

	/* battery temperature guard */
	struct mutex lock;
	struct delayed_work temp_work;
	enum sm5705_temp_zone zone;
	int temp_min, temp_max;		/* tenths of a degree */
	int cfg_voltage, cfg_current;	/* limits outside the guard */

	/* thermistor next to the charger */
	struct iio_channel *therm;
	s32 *therm_table;		/* millidegrees and ADC code, code rising */
	int therm_points;
	bool therm_hot;
};

/* VBUS input current limit: 100 mA + 25 mA steps */
static int sm5705_chg_get_input_limit(struct sm5705_chg *chg, int *ua)
{
	unsigned int val;
	int ret;

	ret = regmap_read(chg->regmap, SM5705_REG_VBUSCNTL, &val);
	if (ret)
		return ret;

	*ua = 100000 + (val & SM5705_VBUSLIMIT_MASK) * 25000;
	return 0;
}

static int sm5705_chg_set_input_limit(struct sm5705_chg *chg, int ua)
{
	unsigned int val;

	if (ua < 100000)
		return -EINVAL;

	val = min((ua - 100000) / 25000, 0x7f);
	return regmap_update_bits(chg->regmap, SM5705_REG_VBUSCNTL,
				  SM5705_VBUSLIMIT_MASK, val);
}

/* fast charge current: 100 mA + 50 mA steps, at least 200 mA */
static int sm5705_chg_get_charge_current(struct sm5705_chg *chg, int *ua)
{
	unsigned int val;
	int ret;

	ret = regmap_read(chg->regmap, SM5705_REG_CHGCNTL2, &val);
	if (ret)
		return ret;

	*ua = 100000 + (val & SM5705_FASTCHG_MASK) * 50000;
	return 0;
}

static int sm5705_chg_set_charge_current(struct sm5705_chg *chg, int ua)
{
	unsigned int val;

	if (chg->bat && chg->bat->constant_charge_current_max_ua > 0 &&
	    ua > chg->bat->constant_charge_current_max_ua)
		return -EINVAL;
	if (ua < 200000)
		return -EINVAL;

	val = min((ua - 100000) / 50000, 0x3f);
	return regmap_update_bits(chg->regmap, SM5705_REG_CHGCNTL2,
				  SM5705_FASTCHG_MASK, val);
}

static int sm5705_chg_get_charge_voltage(struct sm5705_chg *chg, int *uv)
{
	unsigned int val;
	int ret;

	ret = regmap_read(chg->regmap, SM5705_REG_CHGCNTL4, &val);
	if (ret)
		return ret;

	val &= SM5705_BATREG_MASK;
	if (val == 0)
		*uv = 3800000;
	else if (val == 1)
		*uv = 4000000;
	else
		*uv = SM5705_BATREG_MIN_UV + (val - 2) * 10000;
	return 0;
}

static int sm5705_chg_set_charge_voltage(struct sm5705_chg *chg, int uv)
{
	unsigned int val;

	if (chg->bat && chg->bat->constant_charge_voltage_max_uv > 0 &&
	    uv > chg->bat->constant_charge_voltage_max_uv)
		return -EINVAL;
	if (uv < SM5705_BATREG_MIN_UV || uv > SM5705_BATREG_MAX_UV)
		return -EINVAL;

	val = (uv - SM5705_BATREG_MIN_UV) / 10000 + 2;
	return regmap_update_bits(chg->regmap, SM5705_REG_CHGCNTL4,
				  SM5705_BATREG_MASK, val);
}

/* top-off (termination) current: 100 mA + 25 mA steps */
static int sm5705_chg_get_term_current(struct sm5705_chg *chg, int *ua)
{
	unsigned int val;
	int ret;

	ret = regmap_read(chg->regmap, SM5705_REG_CHGCNTL5, &val);
	if (ret)
		return ret;

	*ua = 100000 + (val & SM5705_TOPOFF_MASK) * 25000;
	return 0;
}

static int sm5705_chg_set_term_current(struct sm5705_chg *chg, int ua)
{
	unsigned int val;

	if (ua < 100000)
		return -EINVAL;

	val = min((ua - 100000) / 25000, 0xf);
	return regmap_update_bits(chg->regmap, SM5705_REG_CHGCNTL5,
				  SM5705_TOPOFF_MASK, val);
}

static int sm5705_chg_read_status(struct sm5705_chg *chg, u8 status[3])
{
	return regmap_bulk_read(chg->regmap, SM5705_REG_STATUS1, status, 3);
}

static int sm5705_chg_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct sm5705_chg *chg = power_supply_get_drvdata(psy);
	u8 st[3];
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return sm5705_chg_get_input_limit(chg, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return sm5705_chg_get_charge_current(chg, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return sm5705_chg_get_charge_voltage(chg, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return sm5705_chg_get_term_current(chg, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = chg->usb_type;
		return 0;
	default:
		break;
	}

	ret = sm5705_chg_read_status(chg, st);
	if (ret)
		return ret;

	/* while OTG is on, VBUS is the boost output, not an input */
	if (READ_ONCE(chg->otg_on))
		st[0] &= ~SM5705_VBUSPOK;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !!(st[0] & SM5705_VBUSPOK);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (!(st[0] & SM5705_VBUSPOK))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (chg->zone == SM5705_ZONE_HOT ||
			 chg->zone == SM5705_ZONE_COLD)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (st[1] & (SM5705_TOPOFF | SM5705_DONE))
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (st[1] & SM5705_CHGON)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if ((st[0] & SM5705_VBUSOVP) || (st[1] & SM5705_BATOVP))
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else if (st[2] & SM5705_THEMSHDN)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (st[2] & (SM5705_PRETMROFF | SM5705_FASTTMROFF))
			val->intval = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
		else if (chg->zone == SM5705_ZONE_HOT)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (chg->zone == SM5705_ZONE_COLD)
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		else if (chg->zone == SM5705_ZONE_WARM)
			val->intval = POWER_SUPPLY_HEALTH_WARM;
		else if (chg->zone == SM5705_ZONE_COOL)
			val->intval = POWER_SUPPLY_HEALTH_COOL;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sm5705_chg_apply_zone(struct sm5705_chg *chg);

static int sm5705_chg_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct sm5705_chg *chg = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return sm5705_chg_set_input_limit(chg, val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return sm5705_chg_set_term_current(chg, val->intval);
	default:
		break;
	}

	/* charge voltage and current: the guard may hold them lower */
	mutex_lock(&chg->lock);
	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sm5705_chg_set_charge_current(chg, val->intval);
		if (!ret)
			chg->cfg_current = val->intval;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sm5705_chg_set_charge_voltage(chg, val->intval);
		if (!ret)
			chg->cfg_voltage = val->intval;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (!ret)
		sm5705_chg_apply_zone(chg);
	mutex_unlock(&chg->lock);

	return ret;
}

static int sm5705_chg_property_is_writeable(struct power_supply *psy,
					    enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return 1;
	default:
		return 0;
	}
}

static const enum power_supply_property sm5705_chg_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
};

static void sm5705_chg_external_power_changed(struct power_supply *psy)
{
	struct sm5705_chg *chg = power_supply_get_drvdata(psy);

	schedule_work(&chg->cable_work);
}

static const struct power_supply_desc sm5705_chg_desc = {
	.name = "sm5705-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) |
		     BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_C) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD),
	.properties = sm5705_chg_props,
	.num_properties = ARRAY_SIZE(sm5705_chg_props),
	.get_property = sm5705_chg_get_property,
	.set_property = sm5705_chg_set_property,
	.property_is_writeable = sm5705_chg_property_is_writeable,
	.external_power_changed = sm5705_chg_external_power_changed,
};

/*
 * USB OTG supply. sm5705_chg_apply_zone() picks the mode; the boost runs at
 * 5.1 V for VBUS, then at the voltage it had at probe for the flash LED.
 */
static int sm5705_otg_enable(struct regulator_dev *rdev)
{
	struct sm5705_chg *chg = rdev_get_drvdata(rdev);
	int ret;

	guard(mutex)(&chg->lock);

	ret = regmap_update_bits(chg->regmap, SM5705_REG_CHGCNTL6,
				 SM5705_OTG_CURRENT_MASK,
				 FIELD_PREP(SM5705_OTG_CURRENT_MASK,
					    SM5705_OTG_CURRENT_1500MA));
	if (!ret)
		ret = regmap_update_bits(chg->regmap, SM5705_REG_FLEDCNTL6,
					 SM5705_BSTOUT_MASK, SM5705_BSTOUT_OTG);
	if (ret)
		return ret;

	chg->otg_on = true;
	ret = sm5705_chg_apply_zone(chg);
	if (ret) {
		chg->otg_on = false;
		regmap_update_bits(chg->regmap, SM5705_REG_FLEDCNTL6,
				   SM5705_BSTOUT_MASK, chg->bstout);
	}

	return ret;
}

/* with chg->lock held */
static int sm5705_chg_otg_off(struct sm5705_chg *chg)
{
	int ret;

	chg->otg_on = false;
	ret = sm5705_chg_apply_zone(chg);
	if (ret)
		return ret;

	/* the flash boost's voltage again */
	return regmap_update_bits(chg->regmap, SM5705_REG_FLEDCNTL6,
				  SM5705_BSTOUT_MASK, chg->bstout);
}

static int sm5705_otg_disable(struct regulator_dev *rdev)
{
	struct sm5705_chg *chg = rdev_get_drvdata(rdev);

	guard(mutex)(&chg->lock);

	return sm5705_chg_otg_off(chg);
}

static int sm5705_otg_is_enabled(struct regulator_dev *rdev)
{
	struct sm5705_chg *chg = rdev_get_drvdata(rdev);
	unsigned int cntl;
	int ret;

	ret = regmap_read(chg->regmap, SM5705_REG_CNTL, &cntl);
	if (ret)
		return ret;

	return (cntl & SM5705_CNTL_MODE_MASK) == SM5705_CNTL_MODE_USB_OTG;
}

static const struct regulator_ops sm5705_otg_ops = {
	.enable = sm5705_otg_enable,
	.disable = sm5705_otg_disable,
	.is_enabled = sm5705_otg_is_enabled,
};

static const struct regulator_desc sm5705_otg_desc = {
	.name = "usb-otg-vbus",
	.of_match = "usb-otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &sm5705_otg_ops,
	.n_voltages = 1,
	.fixed_uV = SM5705_OTG_UV,
};

/* The boost could not hold VBUS: like Samsung's driver, turn OTG off */
static void sm5705_chg_otg_fail(struct sm5705_chg *chg)
{
	scoped_guard(mutex, &chg->lock) {
		if (!chg->otg_on)
			return;
		sm5705_chg_otg_off(chg);
	}

	dev_err(chg->dev, "USB OTG overcurrent, VBUS off\n");
	regulator_notifier_call_chain(chg->otg, REGULATOR_EVENT_OVER_CURRENT |
				      REGULATOR_EVENT_DISABLE, NULL);
}

static irqreturn_t sm5705_chg_irq(int irq, void *data)
{
	struct sm5705_chg *chg = data;
	u8 ints[4];

	/* reading the interrupt registers clears them */
	if (regmap_bulk_read(chg->regmap, SM5705_REG_INT1, ints, sizeof(ints)))
		return IRQ_NONE;

	dev_dbg(chg->dev, "interrupt %*ph\n", (int)sizeof(ints), ints);
	if (ints[2] & SM5705_OTGFAIL)
		sm5705_chg_otg_fail(chg);
	power_supply_changed(chg->psy);

	return IRQ_HANDLED;
}

/* Ask the MUIC to keep VBUS at 5 V, or to allow 9 V; with chg->lock held */
static int sm5705_chg_hold_5v(struct sm5705_chg *chg, bool hold)
{
	int ret;

	if (!chg->muic_np || hold == chg->hold_5v)
		return 0;

	chg->hold_5v = hold;
	ret = sm5705_muic_hold_5v(chg->muic_np, hold);
	if (ret)
		dev_warn(chg->dev, "MUIC did not %s: %d\n",
			 hold ? "return to 5 V" : "allow 9 V", ret);

	return ret;
}

/*
 * What a USB-C source allows, from the Type-C controller in "power-supplies":
 * its Type-C current or PD contract, capped to what Samsung's firmware uses.
 */
static int sm5705_chg_typec_limit(struct sm5705_chg *chg, int *type,
				  bool *fast, bool *hv, bool *usb_c)
{
	union power_supply_propval online, uv, ua, usb_type;
	int limit;

	if (power_supply_get_property_from_supplier(chg->psy,
			POWER_SUPPLY_PROP_ONLINE, &online) || !online.intval)
		return 0;
	if (power_supply_get_property_from_supplier(chg->psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &uv) ||
	    power_supply_get_property_from_supplier(chg->psy,
			POWER_SUPPLY_PROP_CURRENT_MAX, &ua))
		return 0;
	if (power_supply_get_property_from_supplier(chg->psy,
			POWER_SUPPLY_PROP_USB_TYPE, &usb_type))
		usb_type.intval = POWER_SUPPLY_USB_TYPE_C;

	limit = min(ua.intval, uv.intval > SM5705_HV_MIN_UV ?
			       SM5705_FAST_INPUT_LIMIT_UA :
			       SM5705_TYPEC_INPUT_LIMIT_UA);
	*type = usb_type.intval;
	*fast = (uv.intval / 1000) * (limit / 1000) / 1000 >=
		SM5705_FAST_SOURCE_MW;
	*hv = uv.intval > SM5705_HV_MIN_UV;
	/* more than default USB power: a USB-C source, not an A-to-C cable */
	*usb_c = usb_type.intval == POWER_SUPPLY_USB_TYPE_PD ||
		 ua.intval > SM5705_SDP_INPUT_LIMIT_UA;

	return limit;
}

static void sm5705_chg_cable_work(struct work_struct *work)
{
	struct sm5705_chg *chg = container_of(work, struct sm5705_chg,
					      cable_work);
	int type, limit = 0, typec_type, typec_limit;
	bool fast = false, typec_fast = false, hv = false, typec_hv = false;
	bool usb_c = false;

	if (extcon_get_state(chg->edev, EXTCON_CHG_USB_FAST) > 0) {
		type = POWER_SUPPLY_USB_TYPE_DCP;
		limit = SM5705_FAST_INPUT_LIMIT_UA;
		fast = true;
		hv = true;
	} else if (extcon_get_state(chg->edev, EXTCON_CHG_USB_DCP) > 0) {
		type = POWER_SUPPLY_USB_TYPE_DCP;
		limit = SM5705_DCP_INPUT_LIMIT_UA;
	} else if (extcon_get_state(chg->edev, EXTCON_CHG_USB_CDP) > 0) {
		type = POWER_SUPPLY_USB_TYPE_CDP;
		limit = SM5705_CDP_INPUT_LIMIT_UA;
	} else if (extcon_get_state(chg->edev, EXTCON_CHG_USB_SDP) > 0) {
		type = POWER_SUPPLY_USB_TYPE_SDP;
		limit = SM5705_SDP_INPUT_LIMIT_UA;
	} else {
		type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	}

	/*
	 * a USB-C source may allow more than its BC1.2 type says, and the
	 * BC1.2 limits are for 5 V: a PD contract above that decides
	 */
	typec_limit = sm5705_chg_typec_limit(chg, &typec_type, &typec_fast,
					     &typec_hv, &usb_c);
	if (typec_limit > limit || typec_hv) {
		type = typec_type;
		limit = typec_limit;
		fast = typec_fast;
		hv = typec_hv;
	}

	/* nothing attached: keep the limit for the next cable */
	if (limit && sm5705_chg_set_input_limit(chg, limit))
		dev_err(chg->dev, "failed to set input limit %d uA\n", limit);

	dev_dbg(chg->dev, "usb type %d, input limit %d uA\n", type, limit);
	chg->usb_type = type;

	mutex_lock(&chg->lock);
	/*
	 * Like Samsung's firmware: AFC and QC 2.0 only on legacy cables, and
	 * not while the flash LED is on.
	 */
	chg->usb_c = usb_c;
	sm5705_chg_hold_5v(chg, usb_c || chg->torch_on);

	chg->hv = hv;
	if (hv && chg->torch_on) {
		/* Samsung's firmware never runs the flash LED from 9 V */
		regmap_update_bits(chg->regmap, SM5705_REG_FLED1CNTL1,
				   SM5705_FLEDEN_MASK, SM5705_FLEDEN_OFF);
		chg->torch_on = false;
		chg->torch.brightness = LED_OFF;
		dev_warn(chg->dev, "9 V charger: torch off\n");
	}
	if (fast != chg->fast) {
		chg->fast = fast;
		dev_info(chg->dev, "%s source, charging at up to %d mA\n",
			 fast ? "15 W" : "standard",
			 (fast ? SM5705_FAST_CHARGE_UA : chg->cfg_current) / 1000);
	}
	/* also picks the mode for the torch with or without VBUS */
	sm5705_chg_apply_zone(chg);
	mutex_unlock(&chg->lock);

	power_supply_changed(chg->psy);
}

static int sm5705_chg_extcon_notifier(struct notifier_block *nb,
				      unsigned long event, void *ptr)
{
	struct sm5705_chg *chg = container_of(nb, struct sm5705_chg,
					      extcon_nb);

	schedule_work(&chg->cable_work);
	return NOTIFY_OK;
}

static const char * const sm5705_zone_names[] = {
	[SM5705_ZONE_NORMAL] = "normal",
	[SM5705_ZONE_COOL] = "cool, 0.9 A",
	[SM5705_ZONE_WARM] = "warm, 4.10 V and 1.35 A",
	[SM5705_ZONE_COLD] = "too cold, not charging",
	[SM5705_ZONE_HOT] = "too hot, not charging",
};

static bool sm5705_chg_vbus_ok(struct sm5705_chg *chg)
{
	unsigned int st;

	return !regmap_read(chg->regmap, SM5705_REG_STATUS1, &st) &&
	       (st & SM5705_VBUSPOK);
}

/*
 * Write the limits for the current zone and pick the operation mode;
 * called with chg->lock held
 */
static int sm5705_chg_apply_zone(struct sm5705_chg *chg)
{
	int voltage = chg->cfg_voltage, current_ua = chg->cfg_current;

	if (chg->fast)
		current_ua = SM5705_FAST_CHARGE_UA;
	unsigned int mode, cntl;
	bool charge = true;
	int ret;

	switch (chg->zone) {
	case SM5705_ZONE_HOT:
	case SM5705_ZONE_COLD:
		charge = false;
		break;
	case SM5705_ZONE_WARM:
		voltage = min(voltage, SM5705_WARM_VOLTAGE_UV);
		current_ua = min(current_ua, SM5705_WARM_CURRENT_UA);
		break;
	case SM5705_ZONE_COOL:
		current_ua = min(current_ua, SM5705_COOL_CURRENT_UA);
		break;
	default:
		break;
	}

	if (chg->therm_hot)
		current_ua = min(current_ua, SM5705_THERM_CURRENT_UA);

	if (voltage > 0)
		sm5705_chg_set_charge_voltage(chg, voltage);
	if (current_ua > 0)
		sm5705_chg_set_charge_current(chg, current_ua);

	/*
	 * switch between charging on, off, flash boost and USB OTG; leave
	 * other modes
	 */
	ret = regmap_read(chg->regmap, SM5705_REG_CNTL, &cntl);
	if (ret)
		return ret;
	mode = cntl & SM5705_CNTL_MODE_MASK;
	if (mode != SM5705_CNTL_MODE_CHG_ON && mode != SM5705_CNTL_MODE_CHG_OFF &&
	    mode != SM5705_CNTL_MODE_FLASH_BOOST &&
	    mode != SM5705_CNTL_MODE_USB_OTG)
		return -EBUSY;

	/*
	 * Like Samsung's operation mode table: with OTG the boost drives VBUS
	 * and the torch runs from it; otherwise the torch runs from VBUS while
	 * charging, else from the battery through the flash boost.
	 */
	if (chg->otg_on)
		mode = SM5705_CNTL_MODE_USB_OTG;
	else if (chg->torch_on && (!charge || !sm5705_chg_vbus_ok(chg)))
		mode = SM5705_CNTL_MODE_FLASH_BOOST;
	else
		mode = charge ? SM5705_CNTL_MODE_CHG_ON : SM5705_CNTL_MODE_CHG_OFF;

	return regmap_update_bits(chg->regmap, SM5705_REG_CNTL,
				  SM5705_CNTL_MODE_MASK, mode);
}

static enum sm5705_temp_zone sm5705_chg_temp_zone(struct sm5705_chg *chg,
						  int temp)
{
	enum sm5705_temp_zone zone = chg->zone;

	if (temp >= chg->temp_max ||
	    (zone == SM5705_ZONE_HOT &&
	     temp >= chg->temp_max - SM5705_TEMP_ALERT_HYST))
		return SM5705_ZONE_HOT;
	if (temp < chg->temp_min ||
	    (zone == SM5705_ZONE_COLD &&
	     temp <= chg->temp_min + SM5705_TEMP_ALERT_HYST))
		return SM5705_ZONE_COLD;
	if (temp >= SM5705_TEMP_WARM ||
	    ((zone == SM5705_ZONE_WARM || zone == SM5705_ZONE_HOT) &&
	     temp >= SM5705_TEMP_WARM - SM5705_TEMP_WARM_HYST))
		return SM5705_ZONE_WARM;
	if (temp < SM5705_TEMP_COOL ||
	    ((zone == SM5705_ZONE_COOL || zone == SM5705_ZONE_COLD) &&
	     temp <= SM5705_TEMP_COOL + SM5705_TEMP_COOL_HYST))
		return SM5705_ZONE_COOL;

	return SM5705_ZONE_NORMAL;
}

struct sm5705_bat_temp {
	struct sm5705_chg *chg;
	int temp;
};

/* Battery temperature from the battery this charger supplies */
static int sm5705_chg_read_bat_temp(struct power_supply *psy, void *data)
{
	struct sm5705_bat_temp *bt = data;
	union power_supply_propval val;
	size_t i;

	if (psy->desc->type != POWER_SUPPLY_TYPE_BATTERY)
		return 0;

	for (i = 0; i < psy->num_supplies; i++) {
		if (strcmp(psy->supplied_from[i], bt->chg->psy->desc->name))
			continue;
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_TEMP, &val))
			return 0;
		bt->temp = val.intval;
		return 1;
	}

	return 0;
}

/* Clamped to the table; tenths of a degree */
static int sm5705_chg_therm_temp(struct sm5705_chg *chg, int *val)
{
	const s32 *t = chg->therm_table;
	int i, code, ret;

	ret = iio_read_channel_raw(chg->therm, &code);
	if (ret < 0)
		return ret;

	for (i = 1; i < chg->therm_points - 1 && code > t[2 * i + 1]; i++)
		;
	code = clamp(code, t[1], t[2 * chg->therm_points - 1]);
	*val = fixp_linear_interpolate(t[2 * i - 1], t[2 * i - 2],
				       t[2 * i + 1], t[2 * i], code) / 100;
	return 0;
}

static void sm5705_chg_check_therm(struct sm5705_chg *chg)
{
	int temp;
	bool hot;

	if (!chg->therm || sm5705_chg_therm_temp(chg, &temp))
		return;

	hot = chg->therm_hot ? temp >= SM5705_THERM_RECOVERY : temp > SM5705_THERM_HOT;
	if (hot == chg->therm_hot)
		return;

	dev_info(chg->dev, "charger area at %d.%d C: %s\n", temp / 10, abs(temp % 10),
		 hot ? "at most 1.4 A" : "limit off");

	mutex_lock(&chg->lock);
	chg->therm_hot = hot;
	sm5705_chg_apply_zone(chg);
	mutex_unlock(&chg->lock);
}

static void sm5705_chg_temp_work(struct work_struct *work)
{
	struct sm5705_chg *chg = container_of(work, struct sm5705_chg,
					      temp_work.work);
	struct sm5705_bat_temp bt = { .chg = chg };
	enum sm5705_temp_zone zone;
	bool found;

	sm5705_chg_check_therm(chg);

	found = power_supply_for_each_psy(&bt, sm5705_chg_read_bat_temp) > 0;
	if (found) {
		mutex_lock(&chg->lock);
		zone = sm5705_chg_temp_zone(chg, bt.temp);
		if (zone != chg->zone) {
			dev_info(chg->dev, "battery at %d.%d C: %s\n",
				 bt.temp / 10, abs(bt.temp % 10),
				 sm5705_zone_names[zone]);
			chg->zone = zone;
			sm5705_chg_apply_zone(chg);
			mutex_unlock(&chg->lock);
			power_supply_changed(chg->psy);
		} else {
			mutex_unlock(&chg->lock);
		}
	}

	/* the battery may register after the charger */
	schedule_delayed_work(&chg->temp_work, found ? SM5705_TEMP_POLL : 5 * HZ);
}

/* Battery limits from the monitored-battery node, as stock sets them */
static int sm5705_chg_apply_battery_info(struct sm5705_chg *chg)
{
	struct power_supply_battery_info *bat = chg->bat;
	int ret;

	if (bat->constant_charge_voltage_max_uv > 0) {
		ret = sm5705_chg_set_charge_voltage(chg,
				bat->constant_charge_voltage_max_uv);
		if (ret)
			return ret;
	}

	if (bat->constant_charge_current_max_ua > 0) {
		ret = sm5705_chg_set_charge_current(chg,
				bat->constant_charge_current_max_ua);
		if (ret)
			return ret;
	}

	if (bat->charge_term_current_ua > 0) {
		ret = sm5705_chg_set_term_current(chg,
				bat->charge_term_current_ua);
		if (ret)
			return ret;
	}

	return 0;
}

/* brightness n is n * 10 mA */
static int sm5705_torch_set(struct led_classdev *led,
			    enum led_brightness brightness)
{
	struct sm5705_chg *chg = container_of(led, struct sm5705_chg, torch);
	int ret;

	guard(mutex)(&chg->lock);

	if (!brightness) {
		ret = regmap_update_bits(chg->regmap, SM5705_REG_FLED1CNTL1,
					 SM5705_FLEDEN_MASK, SM5705_FLEDEN_OFF);
		chg->torch_on = false;
		sm5705_chg_apply_zone(chg);
		/* 9 V again, unless a USB-C source keeps it at 5 V */
		sm5705_chg_hold_5v(chg, chg->usb_c);
		return ret;
	}

	/* like Samsung's firmware, first bring a 9 V charger back to 5 V */
	if (!sm5705_chg_hold_5v(chg, true))
		chg->hv = false;
	if (chg->hv)
		return -EBUSY;

	ret = regmap_update_bits(chg->regmap, SM5705_REG_FLED1CNTL4,
				 SM5705_TORCH_MASK, brightness - 1);
	if (ret)
		return ret;

	ret = regmap_update_bits(chg->regmap, SM5705_REG_FLED1CNTL1,
				 SM5705_FLEDEN_MASK, SM5705_FLEDEN_MOVIE);
	if (ret)
		return ret;

	chg->torch_on = true;
	sm5705_chg_apply_zone(chg);

	return 0;
}

static int sm5705_torch_register(struct sm5705_chg *chg)
{
	struct led_init_data init = { };
	struct fwnode_handle *node;
	u32 max_ua = SM5705_TORCH_DEFAULT_UA;
	int ret;

	node = device_get_named_child_node(chg->dev, "led");
	if (!node)
		return 0;

	fwnode_property_read_u32(node, "led-max-microamp", &max_ua);
	max_ua = clamp(max_ua, SM5705_TORCH_STEP_UA, SM5705_TORCH_MAX_UA);

	/* off, whatever an earlier run left */
	ret = regmap_update_bits(chg->regmap, SM5705_REG_FLED1CNTL1,
				 SM5705_FLEDEN_MASK, SM5705_FLEDEN_OFF);
	if (ret)
		goto out;

	chg->torch.max_brightness = max_ua / SM5705_TORCH_STEP_UA;
	chg->torch.brightness_set_blocking = sm5705_torch_set;
	init.fwnode = node;
	ret = devm_led_classdev_register_ext(chg->dev, &chg->torch, &init);
out:
	fwnode_handle_put(node);
	return ret;
}

static void sm5705_chg_shutdown(struct i2c_client *client)
{
	struct sm5705_chg *chg = i2c_get_clientdata(client);

	regmap_update_bits(chg->regmap, SM5705_REG_FLED1CNTL1,
			   SM5705_FLEDEN_MASK, SM5705_FLEDEN_OFF);

	/* the bootloader does not expect VBUS from the boost */
	guard(mutex)(&chg->lock);
	if (chg->otg_on)
		sm5705_chg_otg_off(chg);
}

static void sm5705_chg_put_node(void *np)
{
	of_node_put(np);
}

static const struct regmap_config sm5705_chg_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x3e,
};

static int sm5705_chg_get_therm(struct sm5705_chg *chg)
{
	static const char prop[] = "temperature-lookup-table";
	struct device *dev = chg->dev;
	int i, n, ret;

	chg->therm = devm_iio_channel_get(dev, "therm");
	if (IS_ERR(chg->therm)) {
		ret = PTR_ERR(chg->therm);
		chg->therm = NULL;
		if (ret == -ENODEV)
			return 0;
		return dev_err_probe(dev, ret, "failed to get the thermistor\n");
	}

	n = device_property_count_u32(dev, prop);
	if (n < 4 || n % 2)
		return dev_err_probe(dev, -EINVAL, "bad %s\n", prop);

	chg->therm_table = devm_kcalloc(dev, n, sizeof(*chg->therm_table), GFP_KERNEL);
	if (!chg->therm_table)
		return -ENOMEM;

	ret = device_property_read_u32_array(dev, prop, (u32 *)chg->therm_table, n);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read %s\n", prop);

	for (i = 3; i < n; i += 2)
		if (chg->therm_table[i] <= chg->therm_table[i - 2])
			return dev_err_probe(dev, -EINVAL, "%s: codes must rise\n", prop);

	chg->therm_points = n / 2;
	return 0;
}

static int sm5705_chg_probe(struct i2c_client *client)
{
	/*
	 * VBUS good/UVLO/OVP; battery OVP, charging, top-off, done; thermal,
	 * OTG failure, timers
	 */
	static const u8 int_mask[4] = { 0xf8, 0x95, 0xc9, 0xff };
	struct regulator_config otg_cfg = {};
	struct power_supply_config cfg = {};
	struct device *dev = &client->dev;
	struct sm5705_chg *chg;
	unsigned int id;
	u8 ints[4];
	int ret;

	chg = devm_kzalloc(dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;
	chg->dev = dev;
	i2c_set_clientdata(client, chg);
	chg->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	chg->temp_min = SM5705_TEMP_ALERT_MIN;
	chg->temp_max = SM5705_TEMP_ALERT_MAX;

	ret = sm5705_chg_get_therm(chg);
	if (ret)
		return ret;

	ret = devm_mutex_init(dev, &chg->lock);
	if (ret)
		return ret;

	if (of_property_present(dev->of_node, "extcon")) {
		chg->edev = extcon_get_edev_by_phandle(dev, 0);
		if (IS_ERR(chg->edev))
			return dev_err_probe(dev, PTR_ERR(chg->edev),
					     "failed to get extcon device\n");

		/* the SM5705 MUIC also holds VBUS at 5 V for us */
		chg->muic_np = of_parse_phandle(dev->of_node, "extcon", 0);
		ret = devm_add_action_or_reset(dev, sm5705_chg_put_node,
					       chg->muic_np);
		if (ret)
			return ret;
	}

	chg->regmap = devm_regmap_init_i2c(client, &sm5705_chg_regmap_config);
	if (IS_ERR(chg->regmap))
		return dev_err_probe(dev, PTR_ERR(chg->regmap),
				     "failed to init regmap\n");

	ret = regmap_read(chg->regmap, SM5705_REG_DEVICE_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read device id\n");

	ret = regmap_read(chg->regmap, SM5705_REG_FLEDCNTL6, &chg->bstout);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read boost voltage\n");
	chg->bstout &= SM5705_BSTOUT_MASK;

	/* external_power_changed() can run as soon as the supply exists */
	ret = devm_work_autocancel(dev, &chg->cable_work, sm5705_chg_cable_work);
	if (ret)
		return ret;

	cfg.drv_data = chg;
	cfg.fwnode = dev_fwnode(dev);
	chg->psy = devm_power_supply_register(dev, &sm5705_chg_desc, &cfg);
	if (IS_ERR(chg->psy))
		return dev_err_probe(dev, PTR_ERR(chg->psy),
				     "failed to register power supply\n");

	ret = power_supply_get_battery_info(chg->psy, &chg->bat);
	if (ret) {
		dev_warn(dev, "no battery info, keeping the charger settings: %d\n",
			 ret);
		chg->bat = NULL;
	} else {
		ret = sm5705_chg_apply_battery_info(chg);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to apply battery limits\n");
		/* alert-celsius in the battery node, in degrees */
		if (chg->bat->temp_alert_min > INT_MIN)
			chg->temp_min = chg->bat->temp_alert_min * 10;
		if (chg->bat->temp_alert_max < INT_MAX)
			chg->temp_max = chg->bat->temp_alert_max * 10;
	}

	/* the hardware may still hold the fast setting of an earlier run */
	if (!chg->bat || chg->bat->constant_charge_current_max_ua <= 0) {
		ret = sm5705_chg_set_charge_current(chg,
						    SM5705_DEFAULT_CHARGE_UA);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to set charge current\n");
	}

	ret = sm5705_chg_get_charge_voltage(chg, &chg->cfg_voltage);
	if (!ret)
		ret = sm5705_chg_get_charge_current(chg, &chg->cfg_current);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read charge limits\n");

	/* start from the normal zone, whatever an earlier run left */
	mutex_lock(&chg->lock);
	sm5705_chg_apply_zone(chg);
	mutex_unlock(&chg->lock);

	ret = devm_delayed_work_autocancel(dev, &chg->temp_work,
					   sm5705_chg_temp_work);
	if (ret)
		return ret;
	schedule_delayed_work(&chg->temp_work, 0);

	ret = sm5705_torch_register(chg);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register torch\n");

	otg_cfg.dev = dev;
	otg_cfg.driver_data = chg;
	chg->otg = devm_regulator_register(dev, &sm5705_otg_desc, &otg_cfg);
	if (IS_ERR(chg->otg))
		return dev_err_probe(dev, PTR_ERR(chg->otg),
				     "failed to register OTG supply\n");

	if (client->irq) {
		/* clear what the bootloader left, then unmask our events */
		ret = regmap_bulk_read(chg->regmap, SM5705_REG_INT1, ints,
				       sizeof(ints));
		if (!ret)
			ret = regmap_bulk_write(chg->regmap, SM5705_REG_INTMSK1,
						int_mask, sizeof(int_mask));
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to set up interrupts\n");

		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sm5705_chg_irq, IRQF_ONESHOT,
						"sm5705-charger", chg);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to request interrupt\n");
	}

	if (chg->edev) {
		chg->extcon_nb.notifier_call = sm5705_chg_extcon_notifier;
		ret = devm_extcon_register_notifier_all(dev, chg->edev,
							&chg->extcon_nb);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to register extcon notifier\n");

		schedule_work(&chg->cable_work);
	}

	dev_info(dev, "SM5705 charger, id %#04x\n", id);
	return 0;
}

static const struct of_device_id sm5705_chg_of_match[] = {
	{ .compatible = "siliconmitus,sm5705-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, sm5705_chg_of_match);

static struct i2c_driver sm5705_chg_driver = {
	.driver = {
		.name = "sm5705-charger",
		.of_match_table = sm5705_chg_of_match,
	},
	.probe = sm5705_chg_probe,
	.shutdown = sm5705_chg_shutdown,
};
module_i2c_driver(sm5705_chg_driver);

MODULE_DESCRIPTION("Silicon Mitus SM5705 charger driver");
MODULE_IMPORT_NS("IIO_CONSUMER");
MODULE_LICENSE("GPL");
