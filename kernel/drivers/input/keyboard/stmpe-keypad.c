// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/input/matrix_keypad.h>
#include <linux/mfd/stmpe.h>
#include <linux/workqueue.h>
#include <kunit/visibility.h>

#include "stmpe-keypad.h"

/* These addresses are shared by all keypad variants except STMPE1801. */
#define STMPE_KPC_COL			0x60
#define STMPE_KPC_ROW_MSB		0x61
#define STMPE_KPC_ROW_LSB		0x62
#define STMPE_KPC_CTRL_MSB		0x63
#define STMPE_KPC_CTRL_LSB		0x64
#define STMPE_KPC_COMBI_KEY_0		0x65
#define STMPE_KPC_COMBI_KEY_1		0x66
#define STMPE_KPC_COMBI_KEY_2		0x67
#define STMPE_KPC_DATA_BYTE0		0x68
#define STMPE_KPC_DATA_BYTE1		0x69
#define STMPE_KPC_DATA_BYTE2		0x6a
#define STMPE_KPC_DATA_BYTE3		0x6b
#define STMPE_KPC_DATA_BYTE4		0x6c

#define STMPE_KPC_CTRL_LSB_SCAN		(0x1 << 0)
#define STMPE_KPC_CTRL_LSB_DEBOUNCE	(0x7f << 1)
#define STMPE_KPC_CTRL_MSB_SCAN_COUNT	(0xf << 4)

#define STMPE_KPC_ROW_MSB_ROWS		0xff

#define STMPE_KPC_DATA_UP		(0x1 << 7)
#define STMPE_KPC_DATA_ROW		(0xf << 3)
#define STMPE_KPC_DATA_COL		(0x7 << 0)
#define STMPE_KPC_DATA_NOKEY_MASK	0x78

#define STMPE_KEYPAD_MAX_DEBOUNCE	127
#define STMPE_KEYPAD_MAX_SCAN_COUNT	15

#define STMPE1601_NUM_DATA	5
#define STMPE1801_NUM_DATA	5
#define STMPE2401_NUM_DATA	3
#define STMPE2403_NUM_DATA	5

#define STMPE1801_KPC_ROW		0x30
#define STMPE1801_KPC_COL_LOW		0x31
#define STMPE1801_KPC_COL_HIGH		0x32
#define STMPE1801_KPC_CTRL_LOW		0x33
#define STMPE1801_KPC_CTRL_MID		0x34
#define STMPE1801_KPC_CMD		0x36
#define STMPE1801_KPC_DATA_BYTE0	0x3a

#define STMPE1801_KPC_DATA_ROW		GENMASK(2, 0)
#define STMPE1801_KPC_DATA_COL		GENMASK(6, 3)
#define STMPE1801_KPC_FIFO_DEPTH	10
#define STMPE1801_KPC_DEFAULT_DEBOUNCE	50
#define STMPE1801_KPC_MIN_DEBOUNCE	10

/**
 * struct stmpe_keypad_variant - model-specific attributes
 * @auto_increment: whether the KPC_DATA_BYTE register address
 *		    auto-increments on multiple read
 * @set_pullup: whether the pins need to have their pull-ups set
 * @num_data: number of data bytes
 * @num_normal_data: number of normal keys' data bytes
 * @max_cols: maximum number of columns supported
 * @max_rows: maximum number of rows supported
 * @col_gpios: bitmask of gpios which can be used for columns
 * @row_gpios: bitmask of gpios which can be used for rows
 * @col_lsb_reg: register controlling columns 0 through 7
 * @col_msb_reg: register controlling columns 8 and above, or zero
 * @row_lsb_reg: register controlling rows 0 through 7
 * @row_msb_reg: register controlling rows 8 and above, or zero
 * @scan_count_reg: register containing the scan count field
 * @debounce_reg: register containing the debounce field
 * @command_reg: register containing the scan enable bit
 * @data_reg: first keypad FIFO data register
 * @event_format: normal FIFO data byte format
 * @min_debounce: minimum supported debounce interval
 * @default_debounce: debounce interval used when firmware omits the property
 * @debounce_subtract_one: encode debounce as interval minus one
 * @fifo_depth: number of FIFO entries to drain per interrupt, or one
 */
struct stmpe_keypad_variant {
	bool		auto_increment;
	bool		set_pullup;
	int		num_data;
	int		num_normal_data;
	int		max_cols;
	int		max_rows;
	unsigned int	col_gpios;
	unsigned int	row_gpios;
	u8		col_lsb_reg;
	u8		col_msb_reg;
	u8		row_lsb_reg;
	u8		row_msb_reg;
	u8		scan_count_reg;
	u8		debounce_reg;
	u8		command_reg;
	u8		data_reg;
	struct stmpe_keypad_event_format event_format;
	u8		min_debounce;
	u8		default_debounce;
	bool		debounce_subtract_one;
	u8		fifo_depth;
};

static const struct stmpe_keypad_variant stmpe_keypad_variants[] = {
	[STMPE1601] = {
		.auto_increment		= true,
		.num_data		= STMPE1601_NUM_DATA,
		.num_normal_data	= 3,
		.max_cols		= 8,
		.max_rows		= 8,
		.col_gpios		= 0x000ff,	/* GPIO 0 - 7 */
		.row_gpios		= 0x0ff00,	/* GPIO 8 - 15 */
		.col_lsb_reg		= STMPE_KPC_COL,
		.row_lsb_reg		= STMPE_KPC_ROW_LSB,
		.row_msb_reg		= STMPE_KPC_ROW_MSB,
		.scan_count_reg		= STMPE_KPC_CTRL_MSB,
		.debounce_reg		= STMPE_KPC_CTRL_LSB,
		.command_reg		= STMPE_KPC_CTRL_LSB,
		.data_reg		= STMPE_KPC_DATA_BYTE0,
		.event_format = {
			.row_mask	= STMPE_KPC_DATA_ROW,
			.row_shift	= 3,
			.col_mask	= STMPE_KPC_DATA_COL,
			.no_key_mask	= STMPE_KPC_DATA_NOKEY_MASK,
		},
		.fifo_depth		= 1,
	},
	[STMPE1801] = {
		.auto_increment		= true,
		.num_data		= STMPE1801_NUM_DATA,
		.num_normal_data	= 3,
		.max_cols		= 10,
		.max_rows		= 8,
		.col_gpios		= 0x3ff00,	/* GPIO 8 - 17 */
		.row_gpios		= 0x000ff,	/* GPIO 0 - 7 */
		.col_lsb_reg		= STMPE1801_KPC_COL_LOW,
		.col_msb_reg		= STMPE1801_KPC_COL_HIGH,
		.row_lsb_reg		= STMPE1801_KPC_ROW,
		.scan_count_reg		= STMPE1801_KPC_CTRL_LOW,
		.debounce_reg		= STMPE1801_KPC_CTRL_MID,
		.command_reg		= STMPE1801_KPC_CMD,
		.data_reg		= STMPE1801_KPC_DATA_BYTE0,
		.event_format = {
			.row_mask	= STMPE1801_KPC_DATA_ROW,
			.col_mask	= STMPE1801_KPC_DATA_COL,
			.col_shift	= 3,
			.no_key_mask	= STMPE_KPC_DATA_NOKEY_MASK,
		},
		.min_debounce		= STMPE1801_KPC_MIN_DEBOUNCE,
		.default_debounce	= STMPE1801_KPC_DEFAULT_DEBOUNCE,
		.debounce_subtract_one	= true,
		.fifo_depth		= STMPE1801_KPC_FIFO_DEPTH,
	},
	[STMPE2401] = {
		.auto_increment		= false,
		.set_pullup		= true,
		.num_data		= STMPE2401_NUM_DATA,
		.num_normal_data	= 2,
		.max_cols		= 8,
		.max_rows		= 12,
		.col_gpios		= 0x0000ff,	/* GPIO 0 - 7*/
		.row_gpios		= 0x1f7f00,	/* GPIO 8-14, 16-20 */
		.col_lsb_reg		= STMPE_KPC_COL,
		.row_lsb_reg		= STMPE_KPC_ROW_LSB,
		.row_msb_reg		= STMPE_KPC_ROW_MSB,
		.scan_count_reg		= STMPE_KPC_CTRL_MSB,
		.debounce_reg		= STMPE_KPC_CTRL_LSB,
		.command_reg		= STMPE_KPC_CTRL_LSB,
		.data_reg		= STMPE_KPC_DATA_BYTE0,
		.event_format = {
			.row_mask	= STMPE_KPC_DATA_ROW,
			.row_shift	= 3,
			.col_mask	= STMPE_KPC_DATA_COL,
			.no_key_mask	= STMPE_KPC_DATA_NOKEY_MASK,
		},
		.fifo_depth		= 1,
	},
	[STMPE2403] = {
		.auto_increment		= true,
		.set_pullup		= true,
		.num_data		= STMPE2403_NUM_DATA,
		.num_normal_data	= 3,
		.max_cols		= 8,
		.max_rows		= 12,
		.col_gpios		= 0x0000ff,	/* GPIO 0 - 7*/
		.row_gpios		= 0x1fef00,	/* GPIO 8-14, 16-20 */
		.col_lsb_reg		= STMPE_KPC_COL,
		.row_lsb_reg		= STMPE_KPC_ROW_LSB,
		.row_msb_reg		= STMPE_KPC_ROW_MSB,
		.scan_count_reg		= STMPE_KPC_CTRL_MSB,
		.debounce_reg		= STMPE_KPC_CTRL_LSB,
		.command_reg		= STMPE_KPC_CTRL_LSB,
		.data_reg		= STMPE_KPC_DATA_BYTE0,
		.event_format = {
			.row_mask	= STMPE_KPC_DATA_ROW,
			.row_shift	= 3,
			.col_mask	= STMPE_KPC_DATA_COL,
			.no_key_mask	= STMPE_KPC_DATA_NOKEY_MASK,
		},
		.fifo_depth		= 1,
	},
};

/**
 * struct stmpe_keypad - STMPE keypad state container
 * @stmpe: pointer to parent STMPE device
 * @input: spawned input device
 * @variant: STMPE variant
 * @debounce_ms: debounce interval, in ms.  Maximum is
 *		 %STMPE_KEYPAD_MAX_DEBOUNCE.
 * @scan_count: number of key scanning cycles to confirm key data.
 *		Maximum is %STMPE_KEYPAD_MAX_SCAN_COUNT.
 * @no_autorepeat: disable key autorepeat
 * @rows: bitmask for the rows
 * @cols: bitmask for the columns
 * @num_rows: number of matrix rows
 * @num_cols: number of matrix columns
 * @row_shift: row stride used by the matrix keymap
 * @overflow_irq: FIFO overflow interrupt
 * @keymap: the keymap
 */
struct stmpe_keypad {
	struct stmpe *stmpe;
	struct input_dev *input;
	const struct stmpe_keypad_variant *variant;
	unsigned int debounce_ms;
	unsigned int scan_count;
	bool no_autorepeat;
	unsigned int rows;
	unsigned int cols;
	unsigned int num_rows;
	unsigned int num_cols;
	unsigned int row_shift;
	int overflow_irq;
	unsigned short *keymap;
	struct delayed_work health_work;
	bool present;
};

VISIBLE_IF_KUNIT
int stmpe_keypad_decode_event(u8 data,
			      const struct stmpe_keypad_event_format *format,
			      unsigned int rows, unsigned int cols,
			      unsigned int scan_code_row_shift,
			      struct stmpe_keypad_event *event)
{
	unsigned int row;
	unsigned int col;

	if ((data & format->no_key_mask) == format->no_key_mask)
		return -ENODATA;

	row = (data & format->row_mask) >> format->row_shift;
	col = (data & format->col_mask) >> format->col_shift;
	if (row >= rows || col >= cols)
		return -EINVAL;

	event->row = row;
	event->col = col;
	event->scan_code = MATRIX_SCAN_CODE(row, col, scan_code_row_shift);
	event->pressed = !(data & STMPE_KPC_DATA_UP);

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(stmpe_keypad_decode_event);

static int stmpe_keypad_parse_event(struct stmpe_keypad *keypad, u8 data,
				    struct stmpe_keypad_event *event)
{
	return stmpe_keypad_decode_event(data, &keypad->variant->event_format,
					 keypad->num_rows, keypad->num_cols,
					 keypad->row_shift, event);
}

static int stmpe_keypad_read_data(struct stmpe_keypad *keypad, u8 *data)
{
	const struct stmpe_keypad_variant *variant = keypad->variant;
	struct stmpe *stmpe = keypad->stmpe;
	int ret;
	int i;

	if (variant->auto_increment) {
		return stmpe_block_read(stmpe, variant->data_reg,
					variant->num_data, data);
	}

	for (i = 0; i < variant->num_data; i++) {
		ret = stmpe_reg_read(stmpe, variant->data_reg + i);
		if (ret < 0)
			return ret;

		data[i] = ret;
	}

	return 0;
}

static bool stmpe1801_keypad_fifo_empty(const u8 *fifo)
{
	int i;

	for (i = 0; i < 3; i++)
		if ((fifo[i] & STMPE_KPC_DATA_NOKEY_MASK) !=
		    STMPE_KPC_DATA_NOKEY_MASK)
			return false;

	/* STMPE1801 special-function and dedicated-key idle values. */
	return fifo[3] == 0xff && fifo[4] == 0x0f;
}

VISIBLE_IF_KUNIT
int stmpe_keypad_drain_fifo(const struct stmpe_keypad_fifo_ops *ops,
			    void *context, unsigned int fifo_depth,
			    bool stop_on_idle)
{
	u8 fifo[STMPE_KEYPAD_MAX_DATA];
	int ret;
	int entry;

	for (entry = 0; entry < fifo_depth; entry++) {
		ret = ops->read(context, fifo);
		if (ret < 0)
			return entry ? entry : ret;

		if (stop_on_idle && stmpe1801_keypad_fifo_empty(fifo))
			break;

		ops->report(context, fifo);
	}

	return entry;
}
EXPORT_SYMBOL_IF_KUNIT(stmpe_keypad_drain_fifo);

static int stmpe_keypad_fifo_read(void *context, u8 *data)
{
	return stmpe_keypad_read_data(context, data);
}

static void stmpe_keypad_fifo_report(void *context, const u8 *fifo)
{
	struct stmpe_keypad *keypad = context;
	struct input_dev *input = keypad->input;
	const struct stmpe_keypad_variant *variant = keypad->variant;
	int ret;
	int i;

	for (i = 0; i < variant->num_normal_data; i++) {
		struct stmpe_keypad_event event;

		ret = stmpe_keypad_parse_event(keypad, fifo[i], &event);
		if (ret)
			continue;

		input_event(input, EV_MSC, MSC_SCAN, event.scan_code);
		input_report_key(input, keypad->keymap[event.scan_code],
				 event.pressed);
		input_sync(input);
	}
}

static const struct stmpe_keypad_fifo_ops stmpe_keypad_fifo_ops = {
	.read = stmpe_keypad_fifo_read,
	.report = stmpe_keypad_fifo_report,
};

static irqreturn_t stmpe_keypad_irq(int irq, void *dev)
{
	struct stmpe_keypad *keypad = dev;
	struct input_dev *input = keypad->input;
	const struct stmpe_keypad_variant *variant = keypad->variant;
	int ret;

	if (irq == keypad->overflow_irq)
		dev_warn_ratelimited(&input->dev, "keypad FIFO overflow\n");

	ret = stmpe_keypad_drain_fifo(&stmpe_keypad_fifo_ops, keypad,
				      variant->fifo_depth,
				      keypad->stmpe->partnum == STMPE1801);
	if (ret < 0)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static int stmpe_keypad_altfunc_init(struct stmpe_keypad *keypad)
{
	const struct stmpe_keypad_variant *variant = keypad->variant;
	unsigned int col_gpios = variant->col_gpios;
	unsigned int row_gpios = variant->row_gpios;
	struct stmpe *stmpe = keypad->stmpe;
	u8 pureg = stmpe->regs[STMPE_IDX_GPPUR_LSB];
	unsigned int pins = 0;
	unsigned int pu_pins = 0;
	int ret;
	int i;

	/*
	 * Figure out which pins need to be set to the keypad alternate
	 * function.
	 *
	 * {cols,rows}_gpios are bitmasks of which pins on the chip can be used
	 * for the keypad.
	 *
	 * keypad->{cols,rows} are a bitmask of which pins (of the ones useable
	 * for the keypad) are used on the board.
	 */

	for (i = 0; i < variant->max_cols; i++) {
		int num = __ffs(col_gpios);

		if (keypad->cols & (1 << i)) {
			pins |= 1 << num;
			pu_pins |= 1 << num;
		}

		col_gpios &= ~(1 << num);
	}

	for (i = 0; i < variant->max_rows; i++) {
		int num = __ffs(row_gpios);

		if (keypad->rows & (1 << i))
			pins |= 1 << num;

		row_gpios &= ~(1 << num);
	}

	ret = stmpe_set_altfunc(stmpe, pins, STMPE_BLOCK_KEYPAD);
	if (ret)
		return ret;

	/*
	 * On STMPE24xx, set pin bias to pull-up on all keypad input
	 * pins (columns), this incidentally happen to be maximum 8 pins
	 * and placed at GPIO0-7 so only the LSB of the pull up register
	 * ever needs to be written.
	 */
	if (variant->set_pullup) {
		u8 val;

		ret = stmpe_reg_read(stmpe, pureg);
		if (ret)
			return ret;

		/* Do not touch unused pins, may be used for GPIO */
		val = ret & ~pu_pins;
		val |= pu_pins;

		ret = stmpe_reg_write(stmpe, pureg, val);
	}

	return 0;
}

static void stmpe_keypad_disable(void *context)
{
	struct stmpe_keypad *keypad = context;

	stmpe_disable(keypad->stmpe, STMPE_BLOCK_KEYPAD);
}

static int stmpe_keypad_set_scan(void *context, bool enable)
{
	struct stmpe_keypad *keypad = context;

	return stmpe_set_bits(keypad->stmpe, keypad->variant->command_reg,
			      STMPE_KPC_CTRL_LSB_SCAN,
			      enable ? STMPE_KPC_CTRL_LSB_SCAN : 0);
}

static void stmpe_keypad_report_stop_error(void *context, int error)
{
	struct stmpe_keypad *keypad = context;

	dev_warn(&keypad->input->dev, "failed to stop keypad scan: %d\n", error);
}

static const struct stmpe_keypad_lifecycle_ops stmpe_keypad_lifecycle_ops = {
	.set_scan = stmpe_keypad_set_scan,
	.disable = stmpe_keypad_disable,
	.report_stop_error = stmpe_keypad_report_stop_error,
};

VISIBLE_IF_KUNIT
int stmpe_keypad_start(const struct stmpe_keypad_lifecycle_ops *ops,
		       void *context)
{
	int cleanup_error;
	int error;

	error = ops->set_scan(context, true);
	if (!error)
		return 0;

	cleanup_error = ops->set_scan(context, false);
	if (cleanup_error)
		ops->report_stop_error(context, cleanup_error);
	ops->disable(context);

	return error;
}
EXPORT_SYMBOL_IF_KUNIT(stmpe_keypad_start);

VISIBLE_IF_KUNIT
void stmpe_keypad_stop(const struct stmpe_keypad_lifecycle_ops *ops,
		       void *context)
{
	int error;

	error = ops->set_scan(context, false);
	if (error)
		ops->report_stop_error(context, error);
	ops->disable(context);
}
EXPORT_SYMBOL_IF_KUNIT(stmpe_keypad_stop);

static int stmpe_keypad_chip_init(struct stmpe_keypad *keypad)
{
	const struct stmpe_keypad_variant *variant = keypad->variant;
	struct stmpe *stmpe = keypad->stmpe;
	int ret;

	if (keypad->debounce_ms > STMPE_KEYPAD_MAX_DEBOUNCE)
		return -EINVAL;
	if (keypad->debounce_ms < variant->min_debounce)
		return -EINVAL;

	if (keypad->scan_count > STMPE_KEYPAD_MAX_SCAN_COUNT)
		return -EINVAL;

	ret = stmpe_enable(stmpe, STMPE_BLOCK_KEYPAD);
	if (ret < 0)
		return ret;

	ret = stmpe_keypad_altfunc_init(keypad);
	if (ret < 0)
		goto err_disable;

	ret = stmpe_reg_write(stmpe, variant->col_lsb_reg, keypad->cols);
	if (ret < 0)
		goto err_disable;

	if (variant->col_msb_reg) {
		ret = stmpe_reg_write(stmpe, variant->col_msb_reg,
				      keypad->cols >> 8);
		if (ret < 0)
			goto err_disable;
	}

	ret = stmpe_reg_write(stmpe, variant->row_lsb_reg, keypad->rows);
	if (ret < 0)
		goto err_disable;

	if (variant->row_msb_reg) {
		ret = stmpe_set_bits(stmpe, variant->row_msb_reg,
				     STMPE_KPC_ROW_MSB_ROWS,
				     keypad->rows >> 8);
		if (ret < 0)
			goto err_disable;
	}

	ret = stmpe_set_bits(stmpe, variant->scan_count_reg,
			     STMPE_KPC_CTRL_MSB_SCAN_COUNT,
			     keypad->scan_count << 4);
	if (ret < 0)
		goto err_disable;

	ret = stmpe_set_bits(stmpe, variant->debounce_reg,
			     STMPE_KPC_CTRL_LSB_DEBOUNCE,
			     (keypad->debounce_ms -
			      variant->debounce_subtract_one) << 1);
	if (ret < 0)
		goto err_disable;

	return 0;

err_disable:
	stmpe_keypad_disable(keypad);
	return ret;
}

#define STMPE_KEYPAD_HEALTH_INTERVAL	msecs_to_jiffies(2000)

/*
 * The keypad can sit in a detachable accessory (Samsung's Book Cover
 * Keyboard, for example), where a detach cuts the STMPE's power and a
 * quick reattach can be debounced away by the accessory detection. This
 * driver is then bound to a chip that is either gone or has silently
 * lost all its configuration. Poll a canary register that chip init set
 * to a known non-default value: a read failure means the keypad is
 * detached right now, the reset default means the chip power cycled and
 * has to be set up from scratch.
 */
static void stmpe_keypad_health_work(struct work_struct *work)
{
	struct stmpe_keypad *keypad = container_of(to_delayed_work(work),
						   struct stmpe_keypad,
						   health_work);
	struct device *dev = keypad->input->dev.parent;
	int error;
	int ret;

	ret = stmpe_reg_read(keypad->stmpe, keypad->variant->col_lsb_reg);
	if (ret < 0) {
		if (keypad->present) {
			keypad->present = false;
			dev_info(dev, "keypad unreachable, assuming detached\n");
		}
		goto out;
	}

	if (ret == (keypad->cols & 0xff)) {
		keypad->present = true;
		goto out;
	}

	/* The chip answers but with the reset default: it power cycled. */
	dev_info(dev, "keypad reset detected, reinitializing\n");

	error = stmpe_restore_irq_setup(keypad->stmpe);
	if (!error)
		error = stmpe_keypad_chip_init(keypad);
	if (!error)
		error = stmpe_keypad_start(&stmpe_keypad_lifecycle_ops, keypad);
	if (error) {
		/* Leave present false; the next poll retries. */
		keypad->present = false;
		dev_warn(dev, "keypad reinit failed: %d\n", error);
	} else {
		keypad->present = true;
	}

out:
	schedule_delayed_work(&keypad->health_work,
			      STMPE_KEYPAD_HEALTH_INTERVAL);
}

static void stmpe_keypad_fill_used_pins(struct stmpe_keypad *keypad,
					u32 used_rows, u32 used_cols)
{
	int row, col;

	for (row = 0; row < used_rows; row++) {
		for (col = 0; col < used_cols; col++) {
			int code = MATRIX_SCAN_CODE(row, col, keypad->row_shift);
			if (keypad->keymap[code] != KEY_RESERVED) {
				keypad->rows |= 1 << row;
				keypad->cols |= 1 << col;
			}
		}
	}
}

static int stmpe_keypad_probe(struct platform_device *pdev)
{
	struct stmpe *stmpe = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;
	struct stmpe_keypad *keypad;
	struct input_dev *input;
	u32 rows;
	u32 cols;
	int error;
	int overflow_irq;
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	overflow_irq = platform_get_irq_byname(pdev, "KEYPAD_OVER");
	if (overflow_irq < 0)
		return overflow_irq;

	keypad = devm_kzalloc(&pdev->dev, sizeof(struct stmpe_keypad),
			      GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	keypad->stmpe = stmpe;
	keypad->variant = &stmpe_keypad_variants[stmpe->partnum];
	keypad->overflow_irq = overflow_irq;
	keypad->debounce_ms = keypad->variant->default_debounce;

	of_property_read_u32(np, "debounce-interval", &keypad->debounce_ms);
	of_property_read_u32(np, "st,scan-count", &keypad->scan_count);
	keypad->no_autorepeat = of_property_read_bool(np, "st,no-autorepeat");

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = "STMPE keypad";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &pdev->dev;

	error = matrix_keypad_parse_properties(&pdev->dev, &rows, &cols);
	if (error)
		return error;
	if (rows > keypad->variant->max_rows ||
	    cols > keypad->variant->max_cols)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "matrix dimensions %ux%u exceed %dx%d\n",
				     rows, cols, keypad->variant->max_rows,
				     keypad->variant->max_cols);

	keypad->num_rows = rows;
	keypad->num_cols = cols;
	keypad->row_shift = get_count_order(cols);

	error = matrix_keypad_build_keymap(NULL, NULL, rows, cols,
					   NULL, input);
	if (error)
		return error;
	keypad->keymap = input->keycode;

	input_set_capability(input, EV_MSC, MSC_SCAN);
	if (!keypad->no_autorepeat)
		__set_bit(EV_REP, input->evbit);

	stmpe_keypad_fill_used_pins(keypad, rows, cols);

	keypad->input = input;

	error = stmpe_keypad_chip_init(keypad);
	if (error < 0)
		return error;

	error = devm_request_threaded_irq(&pdev->dev, irq,
					  NULL, stmpe_keypad_irq,
					  IRQF_ONESHOT, "stmpe-keypad", keypad);
	if (error) {
		dev_err(&pdev->dev, "unable to get irq: %d\n", error);
		goto err_disable;
	}

	error = devm_request_threaded_irq(&pdev->dev, overflow_irq,
					  NULL, stmpe_keypad_irq,
					  IRQF_ONESHOT, "stmpe-keypad-overflow",
					  keypad);
	if (error) {
		dev_err(&pdev->dev, "unable to get overflow irq: %d\n", error);
		goto err_disable;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev,
			"unable to register input device: %d\n", error);
		goto err_disable;
	}

	error = stmpe_keypad_start(&stmpe_keypad_lifecycle_ops, keypad);
	if (error)
		return error;

	platform_set_drvdata(pdev, keypad);

	keypad->present = true;
	INIT_DELAYED_WORK(&keypad->health_work, stmpe_keypad_health_work);
	/* The canary needs a non-default value in the low column register. */
	if (keypad->cols & 0xff)
		schedule_delayed_work(&keypad->health_work,
				      STMPE_KEYPAD_HEALTH_INTERVAL);

	return 0;

err_disable:
	stmpe_keypad_disable(keypad);
	return error;
}

static void stmpe_keypad_remove(struct platform_device *pdev)
{
	struct stmpe_keypad *keypad = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&keypad->health_work);
	stmpe_keypad_stop(&stmpe_keypad_lifecycle_ops, keypad);
}

static const struct of_device_id stmpe_keypad_of_match[] = {
	{ .compatible = "st,stmpe-keypad" },
	{ }
};
MODULE_DEVICE_TABLE(of, stmpe_keypad_of_match);

static struct platform_driver stmpe_keypad_driver = {
	.driver = {
		.name		= "stmpe-keypad",
		.of_match_table	= stmpe_keypad_of_match,
	},
	.probe		= stmpe_keypad_probe,
	.remove		= stmpe_keypad_remove,
};
module_platform_driver(stmpe_keypad_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("STMPExxxx keypad driver");
MODULE_AUTHOR("Rabin Vincent <rabin.vincent@stericsson.com>");
