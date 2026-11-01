// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silicon Mitus SM5705 MUIC (USB charger type detection)
 *
 * The MUIC of the SM5705 charger and fuel gauge IC runs BC1.2 detection on
 * the USB data lines and reports the result in its CHGTYPE register. This
 * driver turns that into extcon USB and charger cable states. The register
 * layout differs from the SM5502 family: a third interrupt register at 0x05
 * moves the masks, ADC and device type registers.
 *
 * With the afc module parameter it also negotiates 9 V with Samsung AFC
 * (Adaptive Fast Charging) wall chargers, or with Quick Charge 2.0 ones
 * when AFC fails, and reports them as EXTCON_CHG_USB_FAST. That is off by
 * default: Samsung's firmware only does this once the Type-C controller
 * has seen the 56 kOhm pull-up of a legacy USB-A to C cable. The charger
 * driver, which sees the Type-C controller, can hold VBUS at 5 V through
 * sm5705_muic_hold_5v(): for a USB-C source, and while the flash LED is on.
 *
 * Of the accessories that the ID pin identifies by their resistor, only
 * Samsung's keyboard covers are reported, as EXTCON_DOCK, also while VBUS is
 * present. The manual switch is not supported. Register layout and the AFC
 * sequence follow the Samsung vendor driver (muic_regmap_sm5705.c).
 */

#include <linux/delay.h>
#include <linux/extcon-provider.h>
#include <linux/extcon/sm5705-muic.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define SM5705_MUIC_REG_DEVICE_ID	0x01
#define SM5705_MUIC_REG_CONTROL		0x02
#define SM5705_MUIC_REG_INT1		0x03
#define SM5705_MUIC_REG_INTMASK1	0x06
#define SM5705_MUIC_REG_ADC		0x09
#define SM5705_MUIC_REG_RSVD_ID1	0x15
#define SM5705_MUIC_REG_RSVD_ID2	0x16
#define SM5705_MUIC_REG_CHG_TYPE	0x17
#define SM5705_MUIC_REG_AFC_CNTL	0x18
#define SM5705_MUIC_REG_AFC_TXD		0x19
#define SM5705_MUIC_REG_AFC_STATUS	0x1a
#define SM5705_MUIC_REG_VBUS_STATUS	0x1b

/* INT3 */
#define SM5705_MUIC_INT3_AFC_TA_ATTACHED	BIT(0)
#define SM5705_MUIC_INT3_AFC_ACCEPTED		BIT(1)
#define SM5705_MUIC_INT3_VBUS_UPDATE		BIT(2)
#define SM5705_MUIC_INT3_AFC_ERROR		BIT(5)

/* AFC_CNTL */
#define SM5705_MUIC_AFC_CNTL_ENAFC	BIT(0)
#define SM5705_MUIC_AFC_CNTL_VBUS_READ	BIT(3)
#define SM5705_MUIC_AFC_CNTL_DIS_AFC	BIT(5)	/* D+ reset: back to 5 V */
#define SM5705_MUIC_AFC_CNTL_ENQC20	BIT(6)	/* QC 2.0 at 9 V */

/* AFC_TXD: requested voltage in bits 7:4, current in 3:0; 0x46 is 9 V, 1.65 A */
#define SM5705_MUIC_AFC_TXD_9V		0x46
#define SM5705_MUIC_AFC_VOLTAGE(txd)	(((txd) >> 4) & 0xf)
#define SM5705_MUIC_VBUS_VOLTAGE(st)	((st) & 0xf)
#define SM5705_MUIC_AFC_RETRIES		5

#define SM5705_MUIC_DEVICE_ID_QC20	BIT(3)

#define SM5705_MUIC_CONTROL_MASK_INT	BIT(0)
#define SM5705_MUIC_RSVD_ID1_VBUS_VALID	BIT(0)
#define SM5705_MUIC_RSVD_ID1_RID_VBUS	BIT(7)	/* ID resistor attach with VBUS */
#define SM5705_MUIC_RSVD_ID2_BCD_RESCAN	BIT(4)	/* rerun charger detection */
#define SM5705_MUIC_CHG_TYPE_MASK	GENMASK(4, 0)
#define SM5705_MUIC_ADC_MASK		GENMASK(4, 0)

/* ID pin ADC: 49.9 kOhm, Samsung's keyboard covers (ADC_KEYBOARDDOCK) */
#define SM5705_MUIC_ADC_KEYBOARD_DOCK	0x11

/* CHG_TYPE values */
#define SM5705_MUIC_CHG_TYPE_DCP	0x01
#define SM5705_MUIC_CHG_TYPE_CDP	0x02
#define SM5705_MUIC_CHG_TYPE_SDP	0x04
#define SM5705_MUIC_CHG_TYPE_DCD_OUT	0x08	/* data contact timeout */
#define SM5705_MUIC_CHG_TYPE_U200	0x10
#define SM5705_MUIC_CHG_TYPE_AFC	0x11
#define SM5705_MUIC_CHG_TYPE_QC20	0x13

static const unsigned int sm5705_muic_cables[] = {
	EXTCON_USB,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_FAST,
	EXTCON_DOCK,
	EXTCON_NONE,
};

static bool afc;
module_param(afc, bool, 0444);
MODULE_PARM_DESC(afc, "Negotiate 9 V with Samsung AFC or QC 2.0 chargers (USB-A to C cable only)");

struct sm5705_muic {
	struct device *dev;
	struct regmap *regmap;
	struct extcon_dev *edev;
	struct mutex lock;
	bool afc_9v;
	bool qc20;
	bool afc_ta;		/* an AFC or QC 2.0 charger answered */
	bool hold_5v;		/* the charger driver needs 5 V */
	bool chgdet_rerun;	/* charger detection rerun for the cover */
	unsigned int afc_retries;
	unsigned int vbus_retries;
};

static struct i2c_driver sm5705_muic_driver;

/*
 * With a keyboard cover on the ID pin the MUIC does not run charger
 * detection when VBUS comes, so CHG_TYPE stays empty. Rerun it once; the
 * RID charger interrupt follows with the result.
 */
static void sm5705_muic_rerun_chgdet(struct sm5705_muic *muic, bool vbus,
				     unsigned int type, unsigned int adc)
{
	if (!vbus || adc != SM5705_MUIC_ADC_KEYBOARD_DOCK) {
		muic->chgdet_rerun = false;
		return;
	}
	if (type || muic->chgdet_rerun)
		return;

	muic->chgdet_rerun = true;
	regmap_set_bits(muic->regmap, SM5705_MUIC_REG_RSVD_ID2,
			SM5705_MUIC_RSVD_ID2_BCD_RESCAN);
	fsleep(1000);
	regmap_clear_bits(muic->regmap, SM5705_MUIC_REG_RSVD_ID2,
			  SM5705_MUIC_RSVD_ID2_BCD_RESCAN);
}

static int sm5705_muic_detect(struct sm5705_muic *muic)
{
	bool usb = false, sdp = false, dcp = false, cdp = false;
	unsigned int vbus, type, adc;
	int ret;

	ret = regmap_read(muic->regmap, SM5705_MUIC_REG_RSVD_ID1, &vbus);
	if (ret)
		return ret;
	ret = regmap_read(muic->regmap, SM5705_MUIC_REG_CHG_TYPE, &type);
	if (ret)
		return ret;
	type &= SM5705_MUIC_CHG_TYPE_MASK;
	ret = regmap_read(muic->regmap, SM5705_MUIC_REG_ADC, &adc);
	if (ret)
		return ret;
	adc &= SM5705_MUIC_ADC_MASK;

	if (vbus & SM5705_MUIC_RSVD_ID1_VBUS_VALID) {
		switch (type) {
		case SM5705_MUIC_CHG_TYPE_SDP:
		case SM5705_MUIC_CHG_TYPE_DCD_OUT:
			usb = sdp = true;
			break;
		case SM5705_MUIC_CHG_TYPE_CDP:
			usb = cdp = true;
			break;
		case SM5705_MUIC_CHG_TYPE_DCP:
		case SM5705_MUIC_CHG_TYPE_U200:
		case SM5705_MUIC_CHG_TYPE_AFC:
		case SM5705_MUIC_CHG_TYPE_QC20:
			dcp = true;
			break;
		default:
			/* detection still running, the attach interrupt follows */
			break;
		}
	}

	if (!(vbus & SM5705_MUIC_RSVD_ID1_VBUS_VALID)) {
		muic->afc_9v = false;
		muic->afc_ta = false;
	}

	dev_dbg(muic->dev, "vbus %#04x chg type %#04x adc %#04x afc %d\n", vbus,
		type, adc, muic->afc_9v);

	sm5705_muic_rerun_chgdet(muic, vbus & SM5705_MUIC_RSVD_ID1_VBUS_VALID,
				 type, adc);

	extcon_set_state_sync(muic->edev, EXTCON_DOCK,
			      adc == SM5705_MUIC_ADC_KEYBOARD_DOCK);

	/* charger type first, so consumers of EXTCON_USB see it */
	extcon_set_state_sync(muic->edev, EXTCON_CHG_USB_FAST,
			      dcp && muic->afc_9v);
	extcon_set_state_sync(muic->edev, EXTCON_CHG_USB_SDP, sdp);
	extcon_set_state_sync(muic->edev, EXTCON_CHG_USB_DCP, dcp);
	extcon_set_state_sync(muic->edev, EXTCON_CHG_USB_CDP, cdp);
	extcon_set_state_sync(muic->edev, EXTCON_USB, usb);

	return 0;
}

static int sm5705_muic_afc_enable(struct sm5705_muic *muic, bool on)
{
	return regmap_assign_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
				  SM5705_MUIC_AFC_CNTL_ENAFC, on);
}

static void sm5705_muic_afc_read_vbus(struct sm5705_muic *muic)
{
	regmap_set_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
			SM5705_MUIC_AFC_CNTL_VBUS_READ);
	regmap_clear_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
			  SM5705_MUIC_AFC_CNTL_VBUS_READ);
}

static void sm5705_muic_afc_stop(struct sm5705_muic *muic)
{
	regmap_clear_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
			  SM5705_MUIC_AFC_CNTL_ENAFC |
			  SM5705_MUIC_AFC_CNTL_ENQC20);
	muic->qc20 = false;
}

/* The charger answered the AFC ping: ask it for 9 V */
static void sm5705_muic_afc_start(struct sm5705_muic *muic)
{
	unsigned int status;

	regmap_read(muic->regmap, SM5705_MUIC_REG_AFC_STATUS, &status);
	muic->afc_retries = 0;
	muic->vbus_retries = 0;
	muic->qc20 = false;
	msleep(120);
	regmap_write(muic->regmap, SM5705_MUIC_REG_AFC_TXD,
		     SM5705_MUIC_AFC_TXD_9V);
	sm5705_muic_afc_enable(muic, true);
	dev_dbg(muic->dev, "AFC: requesting 9 V\n");
}

/* The charger accepted the request: measure VBUS */
static void sm5705_muic_afc_accepted(struct sm5705_muic *muic)
{
	unsigned int type;

	regmap_read(muic->regmap, SM5705_MUIC_REG_CHG_TYPE, &type);
	if ((type & SM5705_MUIC_CHG_TYPE_MASK) == SM5705_MUIC_CHG_TYPE_AFC)
		regmap_set_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
				SM5705_MUIC_AFC_CNTL_VBUS_READ);
	else
		sm5705_muic_afc_enable(muic, false);
}

/* VBUS measured: done if it is within one step of the requested voltage */
static void sm5705_muic_afc_vbus_update(struct sm5705_muic *muic)
{
	unsigned int st, txd;
	int diff;

	regmap_read(muic->regmap, SM5705_MUIC_REG_VBUS_STATUS, &st);
	regmap_read(muic->regmap, SM5705_MUIC_REG_AFC_TXD, &txd);
	/* the 9 V request in AFC_TXD also stands for QC 2.0's 9 V */
	diff = SM5705_MUIC_VBUS_VOLTAGE(st) - SM5705_MUIC_AFC_VOLTAGE(txd);

	if (abs(diff) <= 1) {
		/* QC 2.0 holds 9 V only while ENQC20 stays set */
		sm5705_muic_afc_enable(muic, false);
		muic->afc_9v = true;
		dev_info(muic->dev, "%s: charger at 9 V\n",
			 muic->qc20 ? "QC 2.0" : "AFC");
	} else if (muic->vbus_retries++ < SM5705_MUIC_AFC_RETRIES) {
		sm5705_muic_afc_read_vbus(muic);
		msleep(100);
	} else {
		sm5705_muic_afc_stop(muic);
		dev_info(muic->dev, "AFC: VBUS stayed at %#x, using 5 V\n", st);
	}
}

/* AFC failed: switch a QC 2.0 charger to 9 V, otherwise try AFC again */
static void sm5705_muic_afc_error(struct sm5705_muic *muic)
{
	unsigned int status, id, type;

	regmap_read(muic->regmap, SM5705_MUIC_REG_AFC_STATUS, &status);
	regmap_read(muic->regmap, SM5705_MUIC_REG_DEVICE_ID, &id);
	regmap_read(muic->regmap, SM5705_MUIC_REG_CHG_TYPE, &type);
	dev_dbg(muic->dev, "AFC: error, status %#04x chg type %#04x\n",
		status, type);

	if (muic->afc_retries++ >= SM5705_MUIC_AFC_RETRIES) {
		sm5705_muic_afc_stop(muic);
		dev_info(muic->dev, "AFC: no agreement, using 5 V\n");
	} else if ((id & SM5705_MUIC_DEVICE_ID_QC20) &&
		   (type & SM5705_MUIC_CHG_TYPE_MASK) == SM5705_MUIC_CHG_TYPE_QC20) {
		sm5705_muic_afc_enable(muic, false);
		regmap_set_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
				SM5705_MUIC_AFC_CNTL_ENQC20);
		muic->qc20 = true;
		dev_dbg(muic->dev, "QC 2.0: requesting 9 V\n");
		regmap_set_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
				SM5705_MUIC_AFC_CNTL_VBUS_READ);
	} else {
		msleep(100);
		sm5705_muic_afc_enable(muic, true);
	}
}

static void sm5705_muic_afc(struct sm5705_muic *muic, u8 int3)
{
	unsigned int vbus;

	if (regmap_read(muic->regmap, SM5705_MUIC_REG_RSVD_ID1, &vbus) ||
	    !(vbus & SM5705_MUIC_RSVD_ID1_VBUS_VALID)) {
		sm5705_muic_afc_stop(muic);
		return;
	}

	if (int3 & SM5705_MUIC_INT3_AFC_TA_ATTACHED)
		muic->afc_ta = true;
	if (muic->hold_5v)
		return;

	if (int3 & SM5705_MUIC_INT3_AFC_TA_ATTACHED)
		sm5705_muic_afc_start(muic);
	if (int3 & SM5705_MUIC_INT3_AFC_ACCEPTED)
		sm5705_muic_afc_accepted(muic);
	if (int3 & SM5705_MUIC_INT3_VBUS_UPDATE)
		sm5705_muic_afc_vbus_update(muic);
	if (int3 & SM5705_MUIC_INT3_AFC_ERROR)
		sm5705_muic_afc_error(muic);
}

/* Back to 5 V: stop AFC or QC 2.0 and reset D+, as Samsung's firmware does */
static int sm5705_muic_afc_drop(struct sm5705_muic *muic)
{
	unsigned int cntl, st;
	int i;

	if (regmap_read(muic->regmap, SM5705_MUIC_REG_AFC_CNTL, &cntl))
		return -EIO;
	if (!muic->afc_9v && !(cntl & (SM5705_MUIC_AFC_CNTL_ENAFC |
				       SM5705_MUIC_AFC_CNTL_ENQC20)))
		return 0;

	regmap_clear_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
			  SM5705_MUIC_AFC_CNTL_ENAFC);
	msleep(50);
	regmap_clear_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
			  SM5705_MUIC_AFC_CNTL_ENQC20);
	regmap_set_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
			SM5705_MUIC_AFC_CNTL_DIS_AFC);
	msleep(60);
	regmap_clear_bits(muic->regmap, SM5705_MUIC_REG_AFC_CNTL,
			  SM5705_MUIC_AFC_CNTL_DIS_AFC);
	muic->afc_9v = false;
	muic->qc20 = false;

	/* the VBUS update interrupt that follows is ignored while held */
	sm5705_muic_afc_read_vbus(muic);
	for (i = 0; i < 20; i++) {
		msleep(20);
		if (!regmap_read(muic->regmap, SM5705_MUIC_REG_VBUS_STATUS, &st) &&
		    !SM5705_MUIC_VBUS_VOLTAGE(st))
			return 0;
	}

	return -ETIMEDOUT;
}

/**
 * sm5705_muic_hold_5v() - keep VBUS at 5 V, or allow AFC and QC 2.0 again
 * @np: the MUIC's device node
 * @hold: true to stop 9 V and keep it off, false to negotiate it again
 *
 * Holding brings a 9 V charger back to 5 V before returning.
 *
 * Return: 0, or -ETIMEDOUT if VBUS did not come back to 5 V.
 */
int sm5705_muic_hold_5v(struct device_node *np, bool hold)
{
	struct sm5705_muic *muic;
	struct i2c_client *client;
	unsigned int vbus;
	int ret = 0;

	client = of_find_i2c_device_by_node(np);
	if (!client)
		return -ENODEV;

	muic = i2c_get_clientdata(client);
	if (client->dev.driver != &sm5705_muic_driver.driver || !muic) {
		put_device(&client->dev);
		return -ENODEV;
	}

	mutex_lock(&muic->lock);
	if (hold != muic->hold_5v) {
		muic->hold_5v = hold;
		if (afc && hold) {
			ret = sm5705_muic_afc_drop(muic);
		} else if (afc && muic->afc_ta &&
			   !regmap_read(muic->regmap, SM5705_MUIC_REG_RSVD_ID1,
					&vbus) &&
			   (vbus & SM5705_MUIC_RSVD_ID1_VBUS_VALID)) {
			sm5705_muic_afc_start(muic);
		}
		sm5705_muic_detect(muic);
		dev_dbg(muic->dev, "%s\n", hold ? "holding 5 V" : "9 V allowed");
	}
	mutex_unlock(&muic->lock);

	put_device(&client->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(sm5705_muic_hold_5v);

static irqreturn_t sm5705_muic_irq(int irq, void *data)
{
	struct sm5705_muic *muic = data;
	u8 ints[3];
	int ret;

	mutex_lock(&muic->lock);

	/* reading the interrupt registers clears them */
	ret = regmap_bulk_read(muic->regmap, SM5705_MUIC_REG_INT1, ints,
			       sizeof(ints));
	if (!ret) {
		dev_dbg(muic->dev, "interrupt %*ph\n", (int)sizeof(ints), ints);
		/* also on detach, to drop AFC/QC 2.0 mode with VBUS */
		if (afc)
			sm5705_muic_afc(muic, ints[2]);
		sm5705_muic_detect(muic);
	}

	mutex_unlock(&muic->lock);

	return ret ? IRQ_NONE : IRQ_HANDLED;
}

static const struct regmap_config sm5705_muic_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x3b,
};

static int sm5705_muic_probe(struct i2c_client *client)
{
	/*
	 * attach/detach; VBUS on, charger detected with an ID resistor, ADC
	 * change, VBUS off; with afc also AFC TA attached, accepted, VBUS
	 * update and error
	 */
	u8 int_mask[3] = { 0xfc, 0x3a, afc ? 0xd8 : 0xff };
	struct device *dev = &client->dev;
	struct sm5705_muic *muic;
	unsigned int id;
	u8 ints[3];
	int ret;

	muic = devm_kzalloc(dev, sizeof(*muic), GFP_KERNEL);
	if (!muic)
		return -ENOMEM;
	muic->dev = dev;
	i2c_set_clientdata(client, muic);

	ret = devm_mutex_init(dev, &muic->lock);
	if (ret)
		return ret;

	muic->regmap = devm_regmap_init_i2c(client, &sm5705_muic_regmap_config);
	if (IS_ERR(muic->regmap))
		return dev_err_probe(dev, PTR_ERR(muic->regmap),
				     "failed to init regmap\n");

	ret = regmap_read(muic->regmap, SM5705_MUIC_REG_DEVICE_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read device id\n");

	muic->edev = devm_extcon_dev_allocate(dev, sm5705_muic_cables);
	if (IS_ERR(muic->edev))
		return dev_err_probe(dev, PTR_ERR(muic->edev),
				     "failed to allocate extcon device\n");

	ret = devm_extcon_dev_register(dev, muic->edev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register extcon device\n");

	/*
	 * report a cover attached while VBUS is present, as Samsung's
	 * firmware does; clear what the bootloader left, then unmask our
	 * events
	 */
	ret = regmap_set_bits(muic->regmap, SM5705_MUIC_REG_RSVD_ID1,
			      SM5705_MUIC_RSVD_ID1_RID_VBUS);
	if (!ret)
		ret = regmap_bulk_read(muic->regmap, SM5705_MUIC_REG_INT1, ints,
				       sizeof(ints));
	if (!ret)
		ret = regmap_bulk_write(muic->regmap, SM5705_MUIC_REG_INTMASK1,
					int_mask, sizeof(int_mask));
	if (!ret)
		ret = regmap_clear_bits(muic->regmap, SM5705_MUIC_REG_CONTROL,
					SM5705_MUIC_CONTROL_MASK_INT);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set up interrupts\n");

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					sm5705_muic_irq, IRQF_ONESHOT,
					"sm5705-muic", muic);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request interrupt\n");

	mutex_lock(&muic->lock);
	ret = sm5705_muic_detect(muic);
	mutex_unlock(&muic->lock);
	if (ret)
		return dev_err_probe(dev, ret, "failed to detect cable\n");

	dev_info(dev, "SM5705 MUIC, id %#04x%s\n", id, afc ? ", AFC on" : "");
	return 0;
}

static const struct of_device_id sm5705_muic_of_match[] = {
	{ .compatible = "siliconmitus,sm5705-muic" },
	{ }
};
MODULE_DEVICE_TABLE(of, sm5705_muic_of_match);

static struct i2c_driver sm5705_muic_driver = {
	.driver = {
		.name = "sm5705-muic",
		.of_match_table = sm5705_muic_of_match,
	},
	.probe = sm5705_muic_probe,
};
module_i2c_driver(sm5705_muic_driver);

MODULE_DESCRIPTION("Silicon Mitus SM5705 MUIC extcon driver");
MODULE_LICENSE("GPL");
