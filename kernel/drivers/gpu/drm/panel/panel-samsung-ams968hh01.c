// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung AMS968HH01 QXGA (2048x1536) AMOLED panel, Anapass ANA38401 DDI,
 * as used in the Samsung Galaxy Tab S3 (SM-T820 / SM-T825).
 *
 * Dual-link (bonded) MIPI DSI, command mode. The DDI takes DCS commands on
 * the first link only (downstream "qcom,dcs-cmd-by-left"); the second link
 * carries pixel data only.
 *
 * Derived from the downstream Samsung display driver and its device tree
 * (drivers/video/msm/mdss/samsung/ANA38401_AMS968HH01).
 *
 * Tested on hardware; colour processing (mDNIe) programmed to bypass.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <video/mipi_display.h>

#define AMS968HH01_GAMMA_LEN	33
#define AMS968HH01_NUM_STEPS	65
#define AMS968HH01_MAX_LEVEL	255
#define AMS968HH01_POINTS	9	/* gamma points V255 ... V3 */
#define AMS968HH01_HBM_STEPS	8
#define AMS968HH01_TEMP_BANDS	3	/* above 0 C, above -20 C, colder */
#define AMS968HH01_TEMP_PERIOD	(30 * HZ)

struct ams968hh01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	struct backlight_device *bl;
	struct regulator *vdd;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *tcon_ready_gpio;
	enum drm_panel_orientation orientation;
	bool mtp_valid;
	/* register 0x83 and its table, per luminance step */
	u8 gamma[AMS968HH01_NUM_STEPS][1 + AMS968HH01_GAMMA_LEN];
	/* high brightness mode: read from the panel with the MTP */
	bool hbm_valid;
	unsigned int hbm;	/* HBM step used at the top level, 0 = off */
	bool hbm_on;		/* HBM ELVSS (B0 70) set since the last reset */
	u8 elvss_normal;	/* B0 70 outside HBM */
	u8 elvss_hbm;		/* B0 70 in HBM */
	u8 hbm_gamma[AMS968HH01_HBM_STEPS][1 + AMS968HH01_GAMMA_LEN];
	/* temperature compensation: optional channel, current band */
	struct iio_channel *temp;
	struct delayed_work temp_work;
	unsigned int temp_band;
};

static inline struct ams968hh01 *to_ams968hh01(struct drm_panel *panel)
{
	return container_of(panel, struct ams968hh01, panel);
}

/*
 * Full-panel mode. In command mode the porches never reach the wire; they
 * only set the link rate (and the tear-check counter via htotal x vtotal x
 * refresh). The vendor kernel runs each link at 893.2 Mbit/s per lane
 * (pclk 148.867 MHz, byte clock 111.65 MHz per link, measured on a running
 * SM-T820), and the DDI's UPLL setting (B0 D0 / B1 1D) is chosen for that
 * rate. Match it by widening the front porch; vrefresh stays 60.
 */
static const struct drm_display_mode ams968hh01_mode = {
	.clock = 297733,		/* 3194 * 1554 * 60 Hz / 1000 */
	.hdisplay = 2048,
	.hsync_start = 2048 + 898,
	.hsync_end = 2048 + 898 + 32,
	.htotal = 2048 + 898 + 32 + 216,
	.vdisplay = 1536,
	.vsync_start = 1536 + 10,
	.vsync_end = 1536 + 10 + 2,
	.vtotal = 1536 + 10 + 2 + 6,
	.width_mm = 196,
	.height_mm = 147,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

/* Previous mode (760.8 Mbit/s per lane), kept for comparison: slow_link=1 */
static const struct drm_display_mode ams968hh01_mode_slow = {
	.clock = 253613,		/* 2720 * 1554 * 60 Hz / 1000 */
	.hdisplay = 2048,
	.hsync_start = 2048 + 424,
	.hsync_end = 2048 + 424 + 32,
	.htotal = 2048 + 424 + 32 + 216,
	.vdisplay = 1536,
	.vsync_start = 1536 + 10,
	.vsync_end = 1536 + 10 + 2,
	.vtotal = 1536 + 10 + 2 + 6,
	.width_mm = 196,
	.height_mm = 147,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static bool slow_link;
module_param(slow_link, bool, 0444);
MODULE_PARM_DESC(slow_link, "Use the old 760.8 Mbit/s mode instead of the vendor 893.2 Mbit/s");

/*
 * mDNIe (the DDI's colour processing block) "bypass" tables from the
 * downstream driver: identity ASCR colour matrix, LCE/NR/DE/CS off, linear
 * curves. Downstream programs this block at every panel-on; the power-on
 * state of the block is not an identity mapping.
 */
static const u8 ams968hh01_mdnie_addr1[] = { 0xb0, 0xa8 };
static const u8 ams968hh01_mdnie_bypass1[] = {
	0xb9, 0x00, 0x67, 0xa9, 0x0c, 0x0c, 0x0c, 0x0c, 0x00, 0xaa, 0xab, 0x00,
	0xaa, 0xab, 0x00, 0xaa, 0xab, 0x00, 0xaa, 0xab, 0xd5, 0x2c, 0x2a, 0xff,
	0xf5, 0x63, 0xfe, 0x4a, 0xff, 0xff, 0xf9, 0xf8, 0x00, 0xff, 0xff, 0x00,
	0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static const u8 ams968hh01_mdnie_addr2[] = { 0xb0, 0x42 };
static const u8 ams968hh01_mdnie_bypass2[] = {
	0xb9, 0x01, 0x00, 0x00, 0x98, 0x24, 0x00, 0xb3, 0x01, 0x0e, 0x01, 0x00,
	0x66, 0x17, 0x03, 0x96, 0x00, 0xff, 0x00, 0x00, 0x07, 0xff, 0x07, 0xff,
	0x14, 0x00, 0x0a, 0x00, 0x32, 0x01, 0xf4, 0x0b, 0x8a, 0x20, 0x2d, 0x6e,
	0x99, 0x1b, 0x17, 0x14, 0x1e, 0x02, 0x5f, 0x02, 0xc8, 0x03, 0x33, 0x02,
	0x22, 0x10, 0x10, 0x07, 0x07, 0x01, 0x00, 0x00, 0x20, 0x00, 0x20, 0x00,
	0x20, 0x00, 0x20, 0x02, 0x1b, 0x02, 0x1b, 0x02, 0x1b, 0x02, 0x1b, 0x09,
	0xa6, 0x09, 0xa6, 0x09, 0xa6, 0x09, 0xa6, 0x00, 0x20, 0x00, 0x20, 0x00,
	0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00,
	0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xff,
};

static void ams968hh01_mode_regs(struct mipi_dsi_multi_context *ctx);

static bool init_in_enable;
module_param(init_in_enable, bool, 0444);
MODULE_PARM_DESC(init_in_enable, "Send the init sequence from enable() (link fully up) instead of prepare()");

static bool late_init;
module_param(late_init, bool, 0444);
MODULE_PARM_DESC(late_init, "After display-on, send the whole init sequence again and display-on once more");

static unsigned int init_delay_ms;
module_param(init_delay_ms, uint, 0444);
MODULE_PARM_DESC(init_delay_ms, "Extra wait after TCON_RDY before sending the init sequence");

static int dual_dsi = 0x06;
module_param(dual_dsi, int, 0444);
MODULE_PARM_DESC(dual_dsi, "Value written to B1 at offset 0x51 (vendor: 0x06, 'dual dsi setting')");

static int cmode = 0x08;
module_param(cmode, int, 0444);
MODULE_PARM_DESC(cmode, "Value written to B1 at offset 0xD9 (vendor: 0x08, 'Anapass compression mode')");

static bool late_mode;
module_param(late_mode, bool, 0444);
MODULE_PARM_DESC(late_mode, "Also rewrite the two mode registers after display-on, in HS mode");

static bool long_writes = true;
module_param(long_writes, bool, 0444);
MODULE_PARM_DESC(long_writes, "Send the 1-2 byte register writes as DCS long packets like the vendor driver (default), 0 = short packets");

/*
 * The vendor init table sends every B1/B2 register write as a DCS long
 * write (type 0x39) even for one-byte payloads. The generic helpers use
 * short writes for payloads up to two bytes; keep the vendor packet type.
 */
static void ams968hh01_reg_write(struct mipi_dsi_multi_context *ctx,
				 const u8 *buf, size_t len)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_DCS_LONG_WRITE,
		.tx_buf = buf,
		.tx_len = len,
	};
	ssize_t ret;

	if (ctx->accum_err)
		return;

	if (!long_writes) {
		mipi_dsi_dcs_write_buffer_multi(ctx, buf, len);
		return;
	}

	if (dsi->mode_flags & MIPI_DSI_MODE_LPM)
		msg.flags |= MIPI_DSI_MSG_USE_LPM;

	ret = dsi->host->ops->transfer(dsi->host, &msg);
	if (ret < 0) {
		dev_err(&dsi->dev, "long write %02x failed: %zd\n", buf[0], ret);
		ctx->accum_err = ret;
	}
}

#define ams968hh01_reg(ctx, seq...)					\
	do {								\
		static const u8 d[] = { seq };				\
		ams968hh01_reg_write(ctx, d, ARRAY_SIZE(d));		\
	} while (0)

static int colmod = -1;
module_param(colmod, int, 0444);
MODULE_PARM_DESC(colmod, "Send DCS set_pixel_format with this value at init (e.g. 0x77 for 24 bpp); -1 = don't (default)");

static bool mdnie_bypass = true;
module_param(mdnie_bypass, bool, 0644);
MODULE_PARM_DESC(mdnie_bypass, "Program the DDI mDNIe block with the identity (bypass) tables at init");

/* Bring-up aid: does the DDI answer reads, and did the register writes land? */
/*
 * Bring-up aid: read DDI registers back. The vendor reads with "B0 <offset>"
 * (short write) followed by a GENERIC read with two parameters (type 0x24:
 * register, length), e.g. MTP = B0 5A + read(D8, 8). DCS reads (0x06) of the
 * B1/B2 write ports are not answered (round 17 got the previous byte back).
 */
static void ams968hh01_readback(struct ams968hh01 *ctx, const char *when)
{
	struct mipi_dsi_device *dsi = ctx->dsi[0];
	static const struct { u8 off, reg, len; const char *what; } regs[] = {
		{ 0x5a, 0xd8, 8, "MTP via D8 (vendor method)" },
	};
	u8 id[3] = {}, buf[8];
	int i, r0, r1, r2, ret;

	r0 = mipi_dsi_dcs_read(dsi, 0xda, &id[0], 1);
	r1 = mipi_dsi_dcs_read(dsi, 0xdb, &id[1], 1);
	r2 = mipi_dsi_dcs_read(dsi, 0xdc, &id[2], 1);
	dev_info(&dsi->dev, "readback %s: ID %02x %02x %02x (ret %d %d %d)\n",
		 when, id[0], id[1], id[2], r0, r1, r2);

	mipi_dsi_set_maximum_return_packet_size(dsi, sizeof(buf));
	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		u8 sel[] = { 0xb0, regs[i].off };
		u8 rd[] = { regs[i].reg, regs[i].len };

		memset(buf, 0xee, sizeof(buf));
		mipi_dsi_dcs_write_buffer(dsi, sel, sizeof(sel));
		ret = mipi_dsi_generic_read(dsi, rd, sizeof(rd), buf, regs[i].len);
		dev_info(&dsi->dev, "readback %s: %02x@%02x = %*ph (ret %d) %s\n",
			 when, regs[i].reg, regs[i].off, (int)regs[i].len, buf, ret,
			 regs[i].what);
	}
}

/*
 * The vendor driver sends the mDNIe tables after display-on, in HS mode,
 * on the first link only.
 */
static void ams968hh01_mdnie_init(struct ams968hh01 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi[0];
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	if (!mdnie_bypass)
		return;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	if (late_mode) {
		ams968hh01_mode_regs(&dsi_ctx);
		dev_info(&dsi->dev, "mode registers rewritten after display-on (dual %#x, cmode %#x)\n",
			 dual_dsi, cmode);
	}
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, ams968hh01_mdnie_addr1, sizeof(ams968hh01_mdnie_addr1));
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, ams968hh01_mdnie_bypass1, sizeof(ams968hh01_mdnie_bypass1));
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, ams968hh01_mdnie_addr2, sizeof(ams968hh01_mdnie_addr2));
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, ams968hh01_mdnie_bypass2, sizeof(ams968hh01_mdnie_bypass2));
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	if (dsi_ctx.accum_err)
		dev_err(&dsi->dev, "mDNIe bypass failed: %d\n", dsi_ctx.accum_err);
	else
		dev_info(&dsi->dev, "mDNIe bypass programmed (v14, %s link)\n", slow_link ? "760.8" : "893.2");

	ams968hh01_readback(ctx, "after on");
}

/* Downstream qcom,mdss-dsi-on-command. B0 = register offset, B1/B2 = write. */
/* "dual DSI" and "Anapass compression mode": the two writes the Qualcomm models add */
static void ams968hh01_mode_regs(struct mipi_dsi_multi_context *ctx)
{
	u8 d1[] = { 0xb1, dual_dsi }, d2[] = { 0xb1, cmode };

	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x51);
	ams968hh01_reg_write(ctx, d1, sizeof(d1));		/* dual DSI */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0xd9);
	ams968hh01_reg_write(ctx, d2, sizeof(d2));		/* Anapass compression mode */
}

/*
 * Brightness condition set (gamma), register 0x83. The DDI takes 11 bytes
 * per channel, red then green then blue: V11, V23, V35, V51, V87, V151,
 * V203, V255 bits 7:0, V3, V255 bit 8 (as 0x80) and VT. This is the
 * vendor's nominal table (samsung,gamma_tx_cmds_revA). The vendor smart
 * dimming code builds its tables in another order and converts them to
 * this one before sending (gamma_command_sorting_post()).
 */
static const u8 ams968hh01_gamma_nominal[11] = {
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00,
};

/*
 * Brightness. As in the vendor driver, backlight levels 0 to 255 map onto
 * 65 luminance steps from 2 to 360 cd/m2 (HBM goes above that, see
 * below). Each level has its own AID value (register 0x90, the emission
 * duty cycle); each step has its own ELVSS value and gamma table. A step's
 * gamma table is cut for its base luminance, and AID dims that to the
 * step's luminance. ELVSS also depends on the panel temperature, in three
 * bands (see ams968hh01_temp_band()). The levels, AID and ELVSS values
 * are from the vendor device tree (samsung,candela_map_table,
 * aid_subdivision_tx_cmds and, as ACL is on, smart_acl_elvss_tx_cmds,
 * smart_acl_elvss_lowtemp_tx_cmds and smart_acl_elvss_lowtemp2_tx_cmds,
 * through smart_acl_elvss_map_table, revA), the rest from the vendor
 * smart dimming code (ss_dsi_smart_dimming_ANA38401_AMS968HH01.h and
 * ss_dsi_smart_dimming_common.c, Copyright (C) 2012 Samsung).
 */
struct ams968hh01_step {
	u16 cd;			/* luminance, cd/m2 */
	u16 base;		/* base luminance, cd/m2 */
	u8 level;		/* first backlight level */
	u8 elvss[AMS968HH01_TEMP_BANDS];	/* with ACL on, per temperature band */
	s8 gray_offset[AMS968HH01_POINTS];		/* V255 ... V3 */
	s8 code_offset[3 * (AMS968HH01_POINTS - 1)];	/* R G B of V255 ... V11 */
};

static const struct ams968hh01_step ams968hh01_steps[AMS968HH01_NUM_STEPS] = {
	/* cd/m2, base cd/m2, first level, ELVSS (ACL on) per band, gray offsets, code offsets */
	{ 2, 112, 0, { 0x19, 0x0f, 0x0a }, { 0, 8, 16, 26, 35, 41, 45, 47, 49 },
	  {-5, 0, 0, -3, 0, -3, -7, 0, -4, -6, 3, -5, -5, 2, -7, -3, 0, -5, 0, 0, -4, -3, 0, -7} },
	{ 3, 112, 1, { 0x19, 0x0f, 0x0a }, { 0, 7, 14, 21, 28, 30, 36, 40, 42 },
	  {-3, 0, 0, -2, 0, -1, -6, 0, -3, -6, 2, -5, -5, 3, -7, -3, 2, -4, -2, 0, -4, -4, 0, -6} },
	{ 4, 112, 2, { 0x19, 0x0f, 0x0a }, { 0, 6, 10, 16, 18, 22, 27, 31, 34 },
	  {-1, 0, 1, -1, 0, 0, -5, 0, -3, -6, 2, -4, -5, 3, -6, -4, 2, -4, -2, 1, -5, -4, 1, -6} },
	{ 5, 112, 3, { 0x1a, 0x10, 0x0b }, { 0, 5, 9, 14, 17, 20, 25, 29, 31 },
	  {-1, 0, 1, -1, 0, 0, -5, 0, -3, -5, 2, -3, -5, 2, -6, -3, 2, -3, -2, 1, -4, -4, 1, -6} },
	{ 6, 112, 4, { 0x1a, 0x10, 0x0b }, { 0, 5, 9, 13, 16, 19, 23, 27, 28 },
	  {0, 0, 1, -1, 0, 0, -4, 0, -2, -5, 1, -4, -6, 3, -6, -3, 2, -4, -2, 2, -4, -3, 0, -6} },
	{ 7, 112, 5, { 0x1a, 0x10, 0x0b }, { 0, 5, 8, 11, 13, 15, 19, 24, 26 },
	  {0, 0, 1, -1, 0, 0, -3, 0, -1, -5, 1, -4, -6, 3, -5, -4, 2, -5, 0, 2, -5, -3, 0, -6} },
	{ 8, 112, 6, { 0x1a, 0x10, 0x0b }, { 0, 5, 7, 10, 11, 14, 17, 21, 23 },
	  {0, 0, 1, -1, 0, 0, -1, 0, -1, -5, 1, -4, -6, 4, -6, -4, 2, -4, -1, 2, -3, -2, 0, -4} },
	{ 9, 112, 7, { 0x1a, 0x10, 0x0b }, { 0, 5, 7, 9, 10, 11, 15, 19, 22 },
	  {0, 0, 1, 0, 0, 0, -2, 0, 0, -4, 1, -3, -6, 4, -6, -4, 2, -3, -1, 2, -3, -2, 0, -5} },
	{ 10, 112, 8, { 0x1a, 0x10, 0x0b }, { 0, 4, 6, 8, 9, 10, 13, 17, 19 },
	  {0, 0, 1, 0, 0, 0, -2, 0, 0, -2, 1, -3, -7, 4, -6, -4, 2, -3, -1, 2, -3, -2, 0, -4} },
	{ 11, 112, 9, { 0x1b, 0x11, 0x0c }, { 0, 4, 6, 7, 9, 10, 13, 17, 18 },
	  {0, 0, 1, 0, 0, 0, -2, 0, 0, -2, 1, -3, -7, 3, -6, -4, 2, -3, -1, 2, -3, -2, 0, -4} },
	{ 12, 112, 10, { 0x1b, 0x11, 0x0c }, { 0, 4, 6, 7, 8, 10, 13, 17, 17 },
	  {0, 0, 1, 0, 0, 0, -2, 0, 0, -2, 1, -3, -7, 3, -6, -4, 2, -3, -1, 1, -3, -2, 0, -4} },
	{ 13, 112, 11, { 0x1b, 0x11, 0x0c }, { 0, 4, 6, 7, 8, 10, 13, 16, 16 },
	  {0, 0, 1, 0, 0, 0, -2, 0, 0, -2, 1, -3, -6, 2, -5, -5, 2, -3, -1, 2, -3, -2, 0, -4} },
	{ 14, 112, 12, { 0x1b, 0x11, 0x0c }, { 0, 4, 5, 6, 7, 9, 12, 15, 16 },
	  {0, 0, 1, 0, 0, 0, -2, 0, 0, -2, 1, -2, -6, 3, -6, -5, 2, -3, -1, 2, -3, -2, 1, -4} },
	{ 15, 112, 13, { 0x1b, 0x11, 0x0c }, { 0, 4, 5, 6, 6, 8, 11, 14, 15 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, -3, 1, -2, -6, 2, -5, -5, 2, -3, -1, 2, -3, -2, 1, -4} },
	{ 16, 112, 14, { 0x1c, 0x12, 0x0d }, { 0, 4, 5, 6, 6, 7, 10, 14, 14 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, -3, 1, -2, -6, 2, -5, -5, 1, -3, -1, 2, -2, -2, 2, -4} },
	{ 17, 112, 15, { 0x1c, 0x12, 0x0d }, { 0, 4, 5, 6, 6, 7, 10, 14, 14 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, -1, 1, -2, -6, 2, -5, -5, 1, -4, 0, 2, -2, -1, 2, -4} },
	{ 19, 112, 16, { 0x1c, 0x12, 0x0d }, { 0, 4, 5, 6, 6, 7, 10, 13, 13 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, -2, 0, -2, -6, 2, -4, -5, 1, -4, 0, 2, -2, -1, 3, -5} },
	{ 20, 112, 17, { 0x1c, 0x12, 0x0d }, { 0, 4, 4, 5, 5, 6, 9, 12, 12 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, -1, 0, -2, -5, 2, -4, -5, 1, -4, 0, 2, -2, -1, 4, -5} },
	{ 21, 112, 18, { 0x1d, 0x13, 0x0e }, { 0, 4, 4, 5, 5, 6, 9, 12, 12 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, -1, 0, -2, -5, 1, -4, -5, 1, -4, 0, 2, -2, -1, 4, -5} },
	{ 22, 112, 19, { 0x1d, 0x13, 0x0e }, { 0, 4, 4, 5, 5, 6, 8, 11, 12 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -5, 1, -4, -3, 1, -3, 0, 2, -2, -1, 4, -5} },
	{ 24, 112, 20, { 0x1d, 0x13, 0x0e }, { 0, 4, 4, 4, 4, 5, 8, 11, 12 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -5, 1, -4, -3, 1, -3, 0, 2, -2, -1, 4, -5} },
	{ 25, 112, 21, { 0x1d, 0x13, 0x0e }, { 0, 4, 4, 4, 4, 5, 8, 11, 12 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -5, 1, -4, -3, 1, -3, -1, 2, -2, -1, 3, -5} },
	{ 27, 112, 22, { 0x1e, 0x14, 0x0f }, { 0, 4, 4, 4, 4, 5, 7, 10, 12 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -4, 1, -4, -3, 1, -3, -1, 2, -3, -1, 4, -6} },
	{ 29, 112, 24, { 0x1e, 0x14, 0x0f }, { 0, 4, 4, 4, 4, 5, 7, 10, 11 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -3, 1, -3, -3, 1, -2, -1, 2, -3, -1, 4, -6} },
	{ 30, 112, 26, { 0x1e, 0x14, 0x0f }, { 0, 4, 4, 4, 4, 4, 7, 9, 11 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -3, 1, -3, -3, 0, -2, -1, 2, -3, -1, 4, -6} },
	{ 32, 112, 28, { 0x1f, 0x15, 0x10 }, { 0, 3, 4, 4, 4, 4, 7, 9, 11 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -3, 1, -3, -3, 0, -2, -1, 1, -4, -1, 4, -7} },
	{ 34, 112, 30, { 0x1f, 0x15, 0x10 }, { 0, 3, 4, 4, 4, 4, 6, 8, 11 },
	  {0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -3, 1, -3, -2, 0, -2, -2, 1, -4, -1, 4, -7} },
	{ 37, 112, 32, { 0x20, 0x16, 0x11 }, { 0, 3, 4, 4, 4, 4, 6, 8, 11 },
	  {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -3, 1, -3, -2, 0, -1, -2, 1, -4, -1, 4, -7} },
	{ 39, 112, 34, { 0x20, 0x16, 0x11 }, { 0, 3, 4, 4, 3, 4, 6, 8, 10 },
	  {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -3, 1, -3, -2, 0, -1, -2, 1, -4, -1, 4, -7} },
	{ 41, 112, 36, { 0x20, 0x16, 0x11 }, { 0, 3, 4, 4, 3, 3, 5, 8, 9 },
	  {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -2, 1, -2, -2, 0, -1, -2, 1, -4, -2, 4, -8} },
	{ 44, 112, 38, { 0x21, 0x17, 0x12 }, { 0, 3, 3, 4, 3, 3, 5, 7, 9 },
	  {1, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, -1, -1, 1, -2, -3, 0, -1, -2, 1, -4, -2, 4, -8} },
	{ 47, 112, 40, { 0x21, 0x17, 0x12 }, { 0, 3, 3, 3, 3, 3, 5, 7, 8 },
	  {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -2, 1, -3, -3, 0, -1, -2, 1, -4, -3, 4, -8} },
	{ 50, 112, 42, { 0x21, 0x17, 0x12 }, { 0, 3, 3, 3, 3, 3, 5, 7, 8 },
	  {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -2, 1, -3, -3, 0, -1, -2, 1, -4, -3, 3, -9} },
	{ 53, 112, 44, { 0x21, 0x17, 0x12 }, { 0, 3, 3, 3, 3, 2, 5, 6, 8 },
	  {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 1, -2, -4, 0, -1, -2, 0, -4, -4, 3, -9} },
	{ 56, 112, 46, { 0x21, 0x17, 0x12 }, { 0, 3, 3, 3, 3, 2, 5, 6, 7 },
	  {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 1, -2, -4, 0, -1, -2, 0, -4, -4, 3, -9} },
	{ 60, 112, 49, { 0x21, 0x17, 0x12 }, { 0, 3, 3, 3, 3, 2, 4, 6, 6 },
	  {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 1, -2, -4, 0, -2, -2, 0, -4, -5, 3, -9} },
	{ 64, 112, 52, { 0x21, 0x17, 0x12 }, { 0, 3, 3, 3, 2, 2, 4, 6, 6 },
	  {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1, -1, 0, -2, -4, 0, -2, -2, 0, -4, -5, 3, -9} },
	{ 68, 112, 55, { 0x21, 0x17, 0x12 }, { 0, 2, 2, 3, 2, 2, 4, 5, 6 },
	  {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1, -1, 0, -2, -4, 1, -3, -2, 1, -4, -5, 5, -11} },
	{ 72, 112, 58, { 0x21, 0x17, 0x12 }, { 0, 2, 2, 3, 2, 2, 4, 5, 5 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, -2, -3, 1, -2, -2, 1, -4, -5, 5, -10} },
	{ 77, 120, 61, { 0x21, 0x17, 0x12 }, { 0, 3, 3, 3, 2, 2, 3, 5, 5 },
	  {1, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1, -2, -2, 1, -2, -2, 1, -3, -5, 5, -10} },
	{ 82, 127, 64, { 0x21, 0x17, 0x12 }, { 0, 3, 4, 2, 2, 2, 3, 5, 5 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, -1, -4, 1, -3, -3, 1, -4, -4, 5, -10} },
	{ 87, 135, 67, { 0x21, 0x17, 0x12 }, { 0, 1, 3, 3, 2, 3, 3, 4, 5 },
	  {1, 0, 1, 0, 0, 0, 0, 0, 0, -1, 0, -1, -1, 0, -2, -3, 1, -2, -3, 1, -3, -5, 5, -11} },
	{ 93, 143, 70, { 0x21, 0x17, 0x12 }, { 0, 3, 4, 3, 2, 2, 3, 4, 5 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, -1, -3, 1, -2, -4, 1, -4, -4, 5, -10} },
	{ 98, 152, 73, { 0x21, 0x17, 0x12 }, { 0, 1, 4, 3, 2, 2, 3, 4, 4 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, -1, -2, 1, -2, -3, 1, -3, -5, 5, -10} },
	{ 105, 162, 77, { 0x21, 0x17, 0x12 }, { 0, 1, 3, 3, 2, 3, 3, 5, 4 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -4, 0, -2, -3, 1, -4, -4, 4, -9} },
	{ 111, 170, 81, { 0x21, 0x17, 0x12 }, { 0, 1, 3, 4, 2, 2, 3, 4, 2 },
	  {0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 1, -2, -2, 1, -3, -5, 4, -10} },
	{ 119, 181, 87, { 0x21, 0x17, 0x12 }, { 0, 2, 4, 4, 2, 2, 3, 4, 2 },
	  {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, -1, -2, 0, -2, -3, 1, -2, -3, 4, -8} },
	{ 126, 192, 92, { 0x21, 0x17, 0x12 }, { 0, 2, 4, 4, 2, 2, 3, 4, 2 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, -1, -2, 0, -2, -3, 1, -2, -4, 4, -8} },
	{ 134, 203, 98, { 0x21, 0x17, 0x12 }, { 0, 2, 4, 3, 2, 2, 2, 4, 2 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, -1, -2, 0, -2, -3, 1, -3, -4, 4, -8} },
	{ 143, 216, 105, { 0x21, 0x17, 0x12 }, { 0, 1, 3, 3, 2, 2, 3, 4, 2 },
	  {0, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, -2, 0, -2, -3, 1, -3, -5, 3, -8} },
	{ 152, 227, 111, { 0x20, 0x16, 0x11 }, { 0, 2, 4, 4, 2, 2, 2, 3, 2 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, -2, -4, 1, -2, -4, 3, -8} },
	{ 162, 240, 119, { 0x20, 0x16, 0x11 }, { 0, 1, 3, 3, 2, 2, 2, 3, 2 },
	  {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, -2, -3, 0, -2, -5, 3, -8} },
	{ 172, 254, 126, { 0x1f, 0x15, 0x10 }, { 0, 2, 4, 3, 2, 2, 1, 3, 1 },
	  {-1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -4, 1, -2, -4, 3, -7} },
	/* blue V11 +6 as in the vendor table (and as the vendor kernel sends it) */
	{ 183, 254, 134, { 0x1f, 0x15, 0x10 }, { 0, 2, 4, 3, 2, 2, 1, 3, 1 },
	  {-1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -4, 0, -2, -3, 2, 6} },
	{ 195, 254, 143, { 0x1f, 0x15, 0x10 }, { 0, 1, 3, 2, 1, 1, 1, 2, 1 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, -2, -3, 2, -5} },
	{ 207, 254, 151, { 0x1f, 0x15, 0x10 }, { 0, 1, 3, 2, 1, 1, 1, 2, 1 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, -2, -3, 1, -4} },
	{ 220, 254, 161, { 0x1e, 0x14, 0x0f }, { 0, 1, 2, 2, 1, 1, 0, 1, 1 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, -2, -3, 1, -4} },
	{ 234, 254, 171, { 0x1e, 0x14, 0x0f }, { 0, 1, 2, 1, 1, 1, 0, 1, 0 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -4, 1, -4} },
	{ 249, 254, 182, { 0x1e, 0x14, 0x0f }, { 0, 0, 1, 1, 0, 0, 0, 0, 0 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{ 265, 272, 194, { 0x1e, 0x14, 0x0e }, { 0, -1, -1, 1, 0, 0, 0, 1, 0 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{ 282, 288, 206, { 0x1e, 0x14, 0x0e }, { 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{ 300, 307, 219, { 0x1d, 0x13, 0x0d }, { 0, -1, 0, 0, 0, -1, -1, 0, 0 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{ 316, 321, 231, { 0x1c, 0x12, 0x0c }, { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{ 333, 336, 243, { 0x1c, 0x12, 0x0b }, { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{ 360, 360, 254, { 0x1b, 0x11, 0x0a }, { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
};

/* AID per backlight level, sent as 90 <bit 8> 00 <bits 7:0> */
static const u16 ams968hh01_aor[AMS968HH01_MAX_LEVEL + 1] = {
	0x17f, 0x17d, 0x17a, 0x178, 0x175, 0x172, 0x16f, 0x16c,
	0x169, 0x167, 0x164, 0x161, 0x15e, 0x15a, 0x158, 0x156,
	0x150, 0x14d, 0x148, 0x145, 0x13f, 0x13c, 0x139, 0x136,
	0x133, 0x130, 0x12e, 0x12c, 0x129, 0x126, 0x122, 0x11f,
	0x11b, 0x116, 0x113, 0x10f, 0x10c, 0x109, 0x104, 0x0ff,
	0x0fa, 0x0f5, 0x0f0, 0x0eb, 0x0e6, 0x0e1, 0x0de, 0x0da,
	0x0d7, 0x0d2, 0x0ce, 0x0c9, 0x0c4, 0x0c0, 0x0bb, 0x0b6,
	0x0b2, 0x0ad, 0x0a6, 0x0a0, 0x099, 0x0a3, 0x09e, 0x099,
	0x0a3, 0x09e, 0x099, 0x0a2, 0x09d, 0x099, 0x0a3, 0x09e,
	0x099, 0x0a2, 0x09f, 0x09c, 0x099, 0x0a5, 0x0a1, 0x09d,
	0x099, 0x0a4, 0x0a1, 0x09f, 0x09d, 0x09b, 0x099, 0x0a6,
	0x0a2, 0x09f, 0x09c, 0x099, 0x0a4, 0x0a2, 0x0a0, 0x09d,
	0x09b, 0x099, 0x0a5, 0x0a3, 0x0a1, 0x09f, 0x09d, 0x09b,
	0x099, 0x0a5, 0x0a3, 0x0a0, 0x09e, 0x09b, 0x099, 0x0a5,
	0x0a3, 0x0a2, 0x0a0, 0x09e, 0x09c, 0x09b, 0x099, 0x0a5,
	0x0a3, 0x0a1, 0x09f, 0x09d, 0x09b, 0x099, 0x0a5, 0x0a3,
	0x0a2, 0x0a0, 0x09e, 0x09c, 0x09b, 0x099, 0x097, 0x095,
	0x093, 0x091, 0x08e, 0x08c, 0x08a, 0x088, 0x086, 0x083,
	0x081, 0x07e, 0x07c, 0x079, 0x076, 0x074, 0x071, 0x06f,
	0x06d, 0x06b, 0x069, 0x067, 0x064, 0x062, 0x060, 0x05e,
	0x05c, 0x05a, 0x057, 0x055, 0x053, 0x051, 0x04e, 0x04c,
	0x04a, 0x047, 0x045, 0x043, 0x040, 0x03e, 0x03c, 0x039,
	0x037, 0x034, 0x032, 0x030, 0x02d, 0x02b, 0x029, 0x026,
	0x024, 0x021, 0x01f, 0x01d, 0x01a, 0x018, 0x015, 0x013,
	0x010, 0x00e, 0x023, 0x021, 0x01f, 0x01d, 0x01b, 0x019,
	0x017, 0x016, 0x014, 0x012, 0x010, 0x00e, 0x023, 0x021,
	0x01f, 0x01e, 0x01c, 0x01a, 0x018, 0x017, 0x015, 0x013,
	0x011, 0x010, 0x00e, 0x023, 0x021, 0x01f, 0x01d, 0x01b,
	0x019, 0x017, 0x015, 0x014, 0x012, 0x010, 0x00e, 0x01f,
	0x01e, 0x01c, 0x01b, 0x019, 0x017, 0x016, 0x014, 0x013,
	0x011, 0x010, 0x00e, 0x01f, 0x01e, 0x01c, 0x01a, 0x018,
	0x017, 0x015, 0x013, 0x011, 0x010, 0x00e, 0x00e, 0x00e,
};

/* Luminance of each gray level at 360 cd/m2 (gamma 2.2), in 2^-22 cd/m2 */
static const u32 ams968hh01_curve[AMS968HH01_MAX_LEVEL + 1] = {
	0, 7666, 35224, 85949, 161847, 264428,
	394918, 554357, 743656, 963624, 1214994, 1498435,
	1814566, 2163961, 2547157, 2964659, 3416945, 3904466,
	4427652, 4986915, 5582645, 6215219, 6884999, 7592333,
	8337556, 9120991, 9942953, 10803744, 11703659, 12642984,
	13621997, 14640966, 15700156, 16799822, 17940214, 19121577,
	20344148, 21608161, 22913842, 24261416, 25651100, 27083107,
	28557649, 30074929, 31635150, 33238509, 34885202, 36575418,
	38309345, 40087169, 41909069, 43775226, 45685814, 47641006,
	49640972, 51685881, 53775898, 55911184, 58091902, 60318208,
	62590261, 64908212, 67272215, 69682419, 72138972, 74642022,
	77191711, 79788183, 82431579, 85122037, 87859696, 90644692,
	93477158, 96357229, 99285035, 102260706, 105284372, 108356159,
	111476194, 114644602, 117861504, 121127025, 124441284, 127804402,
	131216496, 134677686, 138188086, 141747812, 145356979, 149015699,
	152724084, 156482246, 160290295, 164148340, 168056489, 172014850,
	176023528, 180082630, 184192259, 188352521, 192563517, 196825349,
	201138120, 205501929, 209916876, 214383060, 218900580, 223469532,
	228090013, 232762120, 237485947, 242261589, 247089140, 251968693,
	256900341, 261884176, 266920288, 272008769, 277149707, 282343194,
	287589317, 292888165, 298239825, 303644384, 309101929, 314612546,
	320176319, 325793334, 331463675, 337187426, 342964669, 348795488,
	354679965, 360618181, 366610218, 372656155, 378756075, 384910055,
	391118176, 397380516, 403697153, 410068166, 416493632, 422973628,
	429508231, 436097516, 442741560, 449440438, 456194224, 463002994,
	469866820, 476785778, 483759940, 490789380, 497874169, 505014380,
	512210085, 519461355, 526768261, 534130874, 541549264, 549023502,
	556553657, 564139797, 571781993, 579480313, 587234824, 595045596,
	602912695, 610836188, 618816144, 626852629, 634945708, 643095448,
	651301914, 659565173, 667885289, 676262327, 684696351, 693187426,
	701735616, 710340985, 719003595, 727723510, 736500793, 745335507,
	754227713, 763177473, 772184850, 781249905, 790372699, 799553293,
	808791748, 818088124, 827442482, 836854881, 846325381, 855854042,
	865440922, 875086082, 884789579, 894551472, 904371820, 914250680,
	924188110, 934184168, 944238911, 954352397, 964524681, 974755822,
	985045875, 995394897, 1005802944, 1016270071, 1026796335, 1037381790,
	1048026492, 1058730496, 1069493857, 1080316629, 1091198867, 1102140624,
	1113141956, 1124202915, 1135323556, 1146503931, 1157744095, 1169044100,
	1180403998, 1191823843, 1203303688, 1214843583, 1226443583, 1238103738,
	1249824100, 1261604721, 1273445652, 1285346945, 1297308650, 1309330819,
	1321413502, 1333556750, 1345760613, 1358025142, 1370350386, 1382736395,
	1395183219, 1407690908, 1420259511, 1432889077, 1445579655, 1458331295,
	1471144045, 1484017953, 1496953069, 1509949440,
};

/* Relative luminance (gamma 2.15) of the gamma points V255 ... V3, in 2^-22 */
static const u32 ams968hh01_coeff[AMS968HH01_POINTS] = {
	4194304, 2568710, 1359562, 415494, 131788, 58660, 23786, 4871, 298,
};

/* Gray levels of the gamma points from the top, then V0 */
static const u8 ams968hh01_points[AMS968HH01_POINTS + 1] = {
	255, 203, 151, 87, 51, 35, 23, 11, 3, 0,
};

/* Position of each point's code in a channel's 11 bytes of register 0x83 */
static const u8 ams968hh01_point_reg[AMS968HH01_POINTS] = {
	7, 6, 5, 4, 3, 2, 1, 0, 8,
};

/* VT for each VT offset (MTP), in 1/600 of VREF_L below VREF_L */
static const u8 ams968hh01_vt_coeff[16] = {
	0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 138, 148, 158, 168, 178, 186,
};

/* DDI reference voltages (vendor VREG0_REF_L and _H), in 2^-22 V */
/*
 * High brightness mode: eight steps from 378 to 500 cd/m2 above the
 * 360 cd/m2 of level 255. The vendor driver takes them as backlight levels
 * 256 and up, which Android only asks for in its outdoor mode and from
 * automatic brightness in bright light. Here they stay out of the backlight
 * range, whose top a desktop takes for 100%: the hbm attribute selects one
 * for the top level. The 500 cd/m2 gamma table is in the panel
 * (D4 from offset 0x5b, plus the MTP gamma offsets); the steps between are
 * interpolated from the 360 cd/m2 table. AID stays at 360 cd/m2's, ELVSS
 * comes from this table (per temperature band) and the panel (D8 at
 * 0x81), ACL is lighter (8%
 * from a 60% average). Values from the vendor device tree
 * (samsung,hbm_candela_map_table, hbm_etc_tx_cmds, hbm_off_tx_cmds,
 * hbm_rx_cmds, hbm2_rx_cmds and elvss_rx_cmds, revA) and driver
 * (mdss_hbm_etc(), hbm_interpolation_init()).
 */
static const struct {
	u16 cd;
	u8 elvss[AMS968HH01_TEMP_BANDS];	/* ELVSS dim offset, B0 67 */
} ams968hh01_hbm_steps[AMS968HH01_HBM_STEPS] = {
	{ 378, { 0x25, 0x1b, 0x17 } }, { 395, { 0x23, 0x19, 0x17 } },
	{ 413, { 0x21, 0x17, 0x17 } }, { 430, { 0x1f, 0x17, 0x17 } },
	{ 448, { 0x1e, 0x17, 0x17 } }, { 465, { 0x1c, 0x17, 0x17 } },
	{ 483, { 0x1a, 0x17, 0x17 } }, { 500, { 0x17, 0x17, 0x17 } },
};

#define AMS968HH01_VREF_L	28521267	/* 6.8 V */
#define AMS968HH01_VREF_H	6291456		/* 1.5 V */
#define AMS968HH01_SHIFT	22

/* Voltages of one channel: the gamma points (V255 ... V3, V0) and VT */
struct ams968hh01_channel {
	s32 v[AMS968HH01_POINTS + 1];
	s32 vt;
};

static s32 ams968hh01_scale(s64 span, unsigned int num, unsigned int den)
{
	return (span * div_s64((s64)num << AMS968HH01_SHIFT, den)) >> AMS968HH01_SHIFT;
}

/*
 * The point voltages of the calibrated 360 cd/m2 curve, from one
 * channel's 11 MTP bytes (register order, sign and magnitude, with
 * V255's sign in its bit 8 byte).
 */
static void ams968hh01_channel_init(struct ams968hh01_channel *ch, const u8 *mtp)
{
	int k, off;

	off = mtp[9] ? -mtp[7] : mtp[7];
	ch->v[0] = AMS968HH01_VREF_L -
		   ams968hh01_scale(AMS968HH01_VREF_L - AMS968HH01_VREF_H,
				    129 + 0x100 + off, 640);
	ch->vt = AMS968HH01_VREF_L -
		 ams968hh01_scale(AMS968HH01_VREF_L, ams968hh01_vt_coeff[mtp[10] & 0xf], 600);

	/* V203 ... V11 between VT and the point above, V3 between VREF_L and V11 */
	for (k = 1; k < AMS968HH01_POINTS; k++) {
		s32 top = k < AMS968HH01_POINTS - 1 ? ch->vt : AMS968HH01_VREF_L;
		u8 m = mtp[ams968hh01_point_reg[k]];

		off = m & 0x80 ? -(m & 0x7f) : m;
		ch->v[k] = top - ams968hh01_scale(top - ch->v[k - 1], 64 + 0x80 + off, 320);
	}
	ch->v[AMS968HH01_POINTS] = AMS968HH01_VREF_L;
}

/* Voltage of a gray level: linear between the gamma points */
static s32 ams968hh01_gray_v(const struct ams968hh01_channel *ch, unsigned int level)
{
	unsigned int hi, lo;
	int k = 1;

	while (level < ams968hh01_points[k])
		k++;
	hi = ams968hh01_points[k - 1];
	lo = ams968hh01_points[k];

	return ch->v[k - 1] + div_s64((s64)(ch->v[k] - ch->v[k - 1]) * (hi - level), hi - lo);
}

/* The gray level whose 360 cd/m2 luminance is nearest to lum (ties go down) */
static unsigned int ams968hh01_nearest_gray(u32 lum)
{
	unsigned int i;

	for (i = 1; i <= AMS968HH01_MAX_LEVEL; i++)
		if (ams968hh01_curve[i] > lum)
			return lum - ams968hh01_curve[i - 1] <= ams968hh01_curve[i] - lum ?
			       i - 1 : i;

	return AMS968HH01_MAX_LEVEL;
}

/*
 * One channel's 11 bytes of register 0x83 for the gray levels chosen for
 * the points: each point's voltage as a fraction of the range the DDI
 * places it in (V255 between the reference voltages, V203 ... V11 between
 * VT and the point above, V3 between VREF_L and V11), less the nominal
 * code, plus the step's code offset. VT stays 0.
 */
static void ams968hh01_channel_codes(const struct ams968hh01_channel *ch,
				     const unsigned int *gray,
				     const struct ams968hh01_step *step,
				     int c, u8 *reg)
{
	int k;

	memset(reg, 0, 11);
	for (k = 0; k < AMS968HH01_POINTS; k++) {
		s64 top = AMS968HH01_VREF_L, bottom = AMS968HH01_VREF_H;
		int code = 0;

		if (k > 0) {
			if (k < AMS968HH01_POINTS - 1)
				top = ch->vt;
			bottom = ams968hh01_gray_v(ch, gray[k - 1]);
		}
		if (top > bottom)
			code = div64_s64((top - ams968hh01_gray_v(ch, gray[k])) * (k ? 320 : 640),
					 top - bottom);

		if (k == 0) {
			code = clamp(code - 129 + step->code_offset[c], 0, 0x1ff);
			reg[9] = code > 0xff ? 0x80 : 0;
		} else if (k < AMS968HH01_POINTS - 1) {
			code = clamp(code - 64 + step->code_offset[3 * k + c], 0, 0xff);
		} else {
			code = clamp(code - 64, 0, 0xff);
		}
		reg[ams968hh01_point_reg[k]] = code & 0xff;
	}
}

/*
 * Smart dimming, after the vendor driver: the 360 cd/m2 table is the
 * nominal table plus the MTP (any sum above 0xff sent as 0x80). For the
 * other steps, each gamma point takes the gray level whose luminance on
 * the calibrated 360 cd/m2 curve is nearest the point's target (gamma
 * 2.15 from the step's base luminance), moved by the step's gray offset,
 * and the table encodes that level's voltage.
 */
static void ams968hh01_gamma_tables(struct ams968hh01 *ctx, const u8 *mtp)
{
	struct ams968hh01_channel ch[3];
	unsigned int gray[AMS968HH01_POINTS];
	int s, k, c;

	for (c = 0; c < 3; c++)
		ams968hh01_channel_init(&ch[c], mtp + 11 * c);

	for (s = 0; s < AMS968HH01_NUM_STEPS; s++) {
		const struct ams968hh01_step *step = &ams968hh01_steps[s];
		u8 *reg = &ctx->gamma[s][1];

		ctx->gamma[s][0] = 0x83;
		if (s == AMS968HH01_NUM_STEPS - 1) {
			for (k = 0; k < AMS968HH01_GAMMA_LEN; k++) {
				unsigned int v = ams968hh01_gamma_nominal[k % 11] + mtp[k];

				reg[k] = v > 0xff ? 0x80 : v;
			}
		} else {
			for (k = 0; k < AMS968HH01_POINTS; k++) {
				int g = ams968hh01_nearest_gray(ams968hh01_coeff[k] * step->base);

				gray[k] = clamp(g + step->gray_offset[k], 1, AMS968HH01_MAX_LEVEL);
			}
			for (c = 0; c < 3; c++)
				ams968hh01_channel_codes(&ch[c], gray, step, c, reg + 11 * c);
		}
		dev_dbg(&ctx->dsi[0]->dev, "gamma %3u cd/m2: %*ph\n", step->cd,
			AMS968HH01_GAMMA_LEN, reg);
	}
}

/*
 * The HBM tables: the 500 cd/m2 table is the panel's (with the MTP gamma
 * offsets added, byte by byte), the others lie on the straight line from
 * the 360 cd/m2 table to it, rounded towards that table. VT stays 0.
 */
static void ams968hh01_hbm_tables(struct ams968hh01 *ctx, const u8 *hbm, const u8 *mtp)
{
	const struct ams968hh01_step *top = &ams968hh01_steps[AMS968HH01_NUM_STEPS - 1];
	const u8 *max = &ctx->gamma[AMS968HH01_NUM_STEPS - 1][1];
	int cd_hbm = ams968hh01_hbm_steps[AMS968HH01_HBM_STEPS - 1].cd;
	int h, k;

	for (h = 0; h < AMS968HH01_HBM_STEPS; h++) {
		s32 rate = ((ams968hh01_hbm_steps[h].cd - top->cd) << AMS968HH01_SHIFT) /
			   (cd_hbm - top->cd);
		u8 *reg = &ctx->hbm_gamma[h][1];

		ctx->hbm_gamma[h][0] = 0x83;
		for (k = 0; k < AMS968HH01_GAMMA_LEN; k++) {
			int d = (u8)(hbm[k] + mtp[k]) - max[k];

			reg[k] = max[k] + d * rate / (1 << AMS968HH01_SHIFT);
		}
		for (k = 10; k < AMS968HH01_GAMMA_LEN; k += 11)
			reg[k] = 0;
		dev_dbg(&ctx->dsi[0]->dev, "hbm %3u cd/m2: %*ph\n",
			ams968hh01_hbm_steps[h].cd, AMS968HH01_GAMMA_LEN, reg);
	}
}

/* Read DDI data the vendor way: B0 <offset>, then a generic read of <reg> */
static int ams968hh01_read(struct ams968hh01 *ctx, u8 reg, u8 offset, u8 *buf, int len)
{
	struct mipi_dsi_device *dsi = ctx->dsi[0];
	int i, n, ret;

	for (i = 0; i < len; i += n) {
		u8 sel[] = { 0xb0, offset + i };
		u8 rd[2];

		n = min(8, len - i);
		rd[0] = reg;
		rd[1] = n;
		ret = mipi_dsi_dcs_write_buffer(dsi, sel, sizeof(sel));
		if (ret < 0)
			return ret;
		ret = mipi_dsi_generic_read(dsi, rd, sizeof(rd), buf + i, n);
		if (ret != n)
			return ret < 0 ? ret : -EIO;
	}

	return 0;
}

/*
 * The panel's own gamma offsets (MTP): 33 bytes in register 0x83 order,
 * from D8 at offset 0x5a. The DDI only answers reads before the init
 * sequence when the link clock starts after the panel reset
 * (MIPI_DSI_CLOCK_DEFERRED_HS); without the offsets the tables stay those
 * of a panel with zero offsets. Then the HBM data: the 500 cd/m2 table
 * (D4 from 0x5b) and the two values of ELVSS register 70 (D2 at 0x70
 * outside HBM, D8 at 0x81 in HBM); without them the HBM levels act as
 * level 255.
 */
static int ams968hh01_read_mtp(struct ams968hh01 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi[0];
	u8 mtp[AMS968HH01_GAMMA_LEN], hbm[AMS968HH01_GAMMA_LEN];
	int ret;

	ret = mipi_dsi_set_maximum_return_packet_size(dsi, 8);
	if (ret < 0)
		return ret;

	ret = ams968hh01_read(ctx, 0xd8, 0x5a, mtp, sizeof(mtp));
	if (ret)
		return ret;

	ams968hh01_gamma_tables(ctx, mtp);
	ctx->mtp_valid = true;
	dev_info(&dsi->dev, "MTP %*ph\n", (int)sizeof(mtp), mtp);

	ret = ams968hh01_read(ctx, 0xd4, 0x5b, hbm, sizeof(hbm));
	if (!ret)
		ret = ams968hh01_read(ctx, 0xd2, 0x70, &ctx->elvss_normal, 1);
	if (!ret)
		ret = ams968hh01_read(ctx, 0xd8, 0x81, &ctx->elvss_hbm, 1);
	if (ret) {
		dev_warn(&dsi->dev, "HBM data read failed (%d), no HBM\n", ret);
		return 0;
	}

	ams968hh01_hbm_tables(ctx, hbm, mtp);
	ctx->hbm_valid = true;
	dev_info(&dsi->dev, "HBM %*ph, ELVSS %02x/%02x\n", (int)sizeof(hbm), hbm,
		 ctx->elvss_normal, ctx->elvss_hbm);

	return 0;
}

/* The luminance step of a backlight level */
static unsigned int ams968hh01_step(unsigned int level)
{
	unsigned int s = AMS968HH01_NUM_STEPS - 1;

	while (s && ams968hh01_steps[s].level > level)
		s--;

	return s;
}

/* Write one DDI parameter: B0 <offset>, then B2 <value> */
static void ams968hh01_param(struct mipi_dsi_multi_context *ctx, u8 offset, u8 value)
{
	u8 sel[] = { 0xb0, offset }, d[] = { 0xb2, value };

	mipi_dsi_dcs_write_buffer_multi(ctx, sel, sizeof(sel));
	ams968hh01_reg_write(ctx, d, sizeof(d));
}

static void ams968hh01_aid(struct mipi_dsi_multi_context *ctx, unsigned int level)
{
	u16 aor = ams968hh01_aor[level];
	u8 d[] = { 0x90, aor >> 8, 0x00, aor & 0xff };

	ams968hh01_reg_write(ctx, d, sizeof(d));
}

/* CAPS (B0 6D): 0x85 from 41 cd/m2 up, 0x81 below */
static u8 ams968hh01_caps(unsigned int s)
{
	return ams968hh01_steps[s].cd >= 41 ? 0x85 : 0x81;
}

/*
 * Everything that depends on the backlight level, then the update key:
 * gamma table, AID, ELVSS, ACL and CAPS.
 *
 * ELVSS. The vendor on-sequence writes 0x0f, but the brightness update
 * the vendor sends after display-on replaces it with the step's value
 * from its ELVSS table for ACL on (samsung,smart_acl_elvss_tx_cmds_revA).
 *
 * ACL (automatic current limiting): the DDI lowers the emission current
 * when the average picture level is high, which caps the panel's total
 * current. The vendor on-sequence turns it off, and the brightness update
 * after display-on turns it on with these values
 * (samsung,acl_on_tx_cmds_revA, with the vendor's default gradual_acl_val
 * 0x12 at offset 0x36).
 */
static void ams968hh01_brightness(struct ams968hh01 *panel,
				  struct mipi_dsi_multi_context *ctx,
				  unsigned int level)
{
	unsigned int hbm = READ_ONCE(panel->hbm);
	unsigned int s;

	if (level == AMS968HH01_MAX_LEVEL && hbm && panel->hbm_valid) {
		unsigned int h = hbm - 1;

		ams968hh01_reg_write(ctx, panel->hbm_gamma[h], sizeof(panel->hbm_gamma[h]));
		ams968hh01_aid(ctx, AMS968HH01_MAX_LEVEL);
		ams968hh01_param(ctx, 0x67, ams968hh01_hbm_steps[h].elvss[panel->temp_band]);
		ams968hh01_param(ctx, 0x70, panel->elvss_hbm);
		ams968hh01_param(ctx, 0x4d, 0x05);	/* 32 frame avg */
		ams968hh01_param(ctx, 0x49, 0x99);	/* start at 60% */
		ams968hh01_param(ctx, 0x36, 0x11);	/* ACL on, 8% */
		ams968hh01_param(ctx, 0x6d, 0x85);	/* CAPS */
		panel->hbm_on = true;
	} else {
		s = ams968hh01_step(level);
		if (panel->hbm_on) {
			ams968hh01_param(ctx, 0x70, panel->elvss_normal);
			panel->hbm_on = false;
		}
		ams968hh01_reg_write(ctx, panel->gamma[s], sizeof(panel->gamma[s]));
		ams968hh01_aid(ctx, level);
		ams968hh01_param(ctx, 0x67, ams968hh01_steps[s].elvss[panel->temp_band]);
		ams968hh01_param(ctx, 0x4d, 0x05);	/* 32 frame avg */
		ams968hh01_param(ctx, 0x49, 0x7f);	/* start at 50% */
		ams968hh01_param(ctx, 0x36, 0x12);	/* ACL on, 15% */
		ams968hh01_param(ctx, 0x6d, ams968hh01_caps(s));
	}
	ams968hh01_param(ctx, 0x35, 0x01);		/* gamma update key */
}

/*
 * Temperature compensation. Above 0 C, above -20 C and below that, the
 * vendor driver takes ELVSS from separate tables (mdss_elvss() and
 * mdss_hbm_etc()); stock Android writes the temperature to it from
 * userspace. Here it comes from an optional IIO channel, read at power-on
 * and every 30 s while the panel is on. Without one the panel counts as
 * warm, as with the vendor driver's 20 C default.
 */
static unsigned int ams968hh01_temp_band(struct ams968hh01 *ctx)
{
	int mdeg;

	if (!ctx->temp || iio_read_channel_processed(ctx->temp, &mdeg) < 0)
		return 0;
	if (mdeg > 0)
		return 0;

	return mdeg > -20000 ? 1 : 2;
}

static void ams968hh01_temp_work(struct work_struct *work)
{
	struct ams968hh01 *ctx = container_of(to_delayed_work(work),
					      struct ams968hh01, temp_work);
	unsigned int band = ams968hh01_temp_band(ctx);

	if (band != ctx->temp_band) {
		dev_dbg(ctx->panel.dev, "temperature band %u\n", band);
		ctx->temp_band = band;
		backlight_update_status(ctx->bl);
	}
	schedule_delayed_work(&ctx->temp_work, AMS968HH01_TEMP_PERIOD);
}

static void ams968hh01_init(struct mipi_dsi_multi_context *ctx)
{
	struct ams968hh01 *panel = mipi_dsi_get_drvdata(ctx->dsi);

	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x34);
	ams968hh01_reg(ctx, 0xb2, 0x64, 0x00);	/* check fail off */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x75);
	ams968hh01_reg(ctx, 0xb2, 0x28);		/* INTR setting */
	ams968hh01_mode_regs(ctx);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x7f);
	ams968hh01_reg(ctx, 0xb2, 0x15);		/* TSP HTE / VTE */

	if (!long_writes)
		mipi_dsi_dcs_set_tear_on_multi(ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	ams968hh01_reg(ctx, 0x44, 0x05, 0xf6);		/* tear scanline 1526 */

	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0xbc);
	ams968hh01_reg(ctx, 0xb2, 0x04);		/* gamma offset index */

	ams968hh01_brightness(panel, ctx, panel->bl->props.brightness);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0xd0);
	ams968hh01_reg(ctx, 0xb1, 0x1d);		/* UPLL_F = 35 */
	mipi_dsi_msleep(ctx, 215);

	/* bring-up aid: interface pixel format (DCS COLMOD), not sent by the vendor driver */
	if (colmod >= 0)
		mipi_dsi_dcs_set_pixel_format_multi(ctx, colmod);
}

static void ams968hh01_power_off(struct ams968hh01 *ctx)
{
	ctx->hbm_on = false;
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	regulator_disable(ctx->vdd);
	msleep(10);
}

static int ams968hh01_prepare(struct drm_panel *panel)
{
	struct ams968hh01 *ctx = to_ams968hh01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi[0] };
	int val, ret, before;
	ktime_t t0;

	ctx->temp_band = ams968hh01_temp_band(ctx);

	ret = regulator_enable(ctx->vdd);
	if (ret)
		return ret;
	msleep(10);

	/*
	 * The DSI lanes are already in LP11 here (prepare_prev_first), the
	 * clock lane too (MIPI_DSI_CLOCK_DEFERRED_HS). The DDI wants 70 ms
	 * plus margin between LP11 and reset.
	 */
	msleep(75);
	before = gpiod_get_value_cansleep(ctx->tcon_ready_gpio);
	t0 = ktime_get();
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	/* The DDI raises TCON_RDY once it accepts commands (downstream: up to 1 s) */
	ret = read_poll_timeout(gpiod_get_value_cansleep, val, val,
				1000, 1000000, false, ctx->tcon_ready_gpio);
	dev_info(panel->dev, "TCON_RDY: %d before reset release, %s after %lld ms\n",
		 before, ret ? "NOT high" : "high", ktime_ms_delta(ktime_get(), t0));
	usleep_range(1000, 2000);
	if (init_delay_ms) {
		msleep(init_delay_ms);
		dev_info(panel->dev, "extra %u ms before init\n", init_delay_ms);
	}

	ams968hh01_readback(ctx, "prepare, before init");
	if (!ctx->mtp_valid) {
		ret = ams968hh01_read_mtp(ctx);
		if (ret)
			dev_warn(panel->dev, "MTP read failed (%d), using the nominal gamma table\n",
				 ret);
	}
	if (init_in_enable)
		return 0;

	ams968hh01_init(&dsi_ctx);
	if (dsi_ctx.accum_err) {
		ams968hh01_power_off(ctx);
		return dsi_ctx.accum_err;
	}

	ams968hh01_readback(ctx, "prepare, after init");

	return 0;
}

static int ams968hh01_enable(struct drm_panel *panel)
{
	struct ams968hh01 *ctx = to_ams968hh01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi[0] };

	/* Bring-up aid: does the DDI answer once the display pipeline runs? */
	if (!init_in_enable)
		ams968hh01_readback(ctx, "enable, before display-on");

	if (init_in_enable) {
		ams968hh01_readback(ctx, "enable, before init");
		ams968hh01_init(&dsi_ctx);
		if (dsi_ctx.accum_err)
			return dsi_ctx.accum_err;
		dev_info(panel->dev, "init sent from enable()\n");
		ams968hh01_readback(ctx, "enable, after init");
	}

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	if (dsi_ctx.accum_err)
		return dsi_ctx.accum_err;

	ams968hh01_mdnie_init(ctx);

	if (late_init) {
		ams968hh01_init(&dsi_ctx);
		mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
		mipi_dsi_msleep(&dsi_ctx, 50);
		dev_info(panel->dev, "init sent again after display-on (err %d)\n",
			 dsi_ctx.accum_err);
		dsi_ctx.accum_err = 0;
		ams968hh01_readback(ctx, "after late init");
	}

	if (ctx->temp)
		schedule_delayed_work(&ctx->temp_work, AMS968HH01_TEMP_PERIOD);

	return 0;
}

static int ams968hh01_disable(struct drm_panel *panel)
{
	struct ams968hh01 *ctx = to_ams968hh01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi[0] };

	cancel_delayed_work_sync(&ctx->temp_work);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	return dsi_ctx.accum_err;
}

static int ams968hh01_unprepare(struct drm_panel *panel)
{
	ams968hh01_power_off(to_ams968hh01(panel));
	return 0;
}

static int ams968hh01_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, slow_link ?
						   &ams968hh01_mode_slow : &ams968hh01_mode);
}

/* The vendor brightness update, in HS mode on the first link */
static int ams968hh01_bl_update_status(struct backlight_device *bl)
{
	struct ams968hh01 *ctx = bl_get_data(bl);
	struct mipi_dsi_device *dsi = ctx->dsi[0];
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	/* The panel is off, or not on yet: init sends the current level */
	if (backlight_is_blank(bl))
		return 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ams968hh01_brightness(ctx, &dsi_ctx, bl->props.brightness);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

static const struct backlight_ops ams968hh01_bl_ops = {
	.update_status = ams968hh01_bl_update_status,
};

static enum drm_panel_orientation ams968hh01_get_orientation(struct drm_panel *panel)
{
	return to_ams968hh01(panel)->orientation;
}

static const struct drm_panel_funcs ams968hh01_panel_funcs = {
	.prepare = ams968hh01_prepare,
	.enable = ams968hh01_enable,
	.disable = ams968hh01_disable,
	.unprepare = ams968hh01_unprepare,
	.get_modes = ams968hh01_get_modes,
	.get_orientation = ams968hh01_get_orientation,
};

static int ams968hh01_probe(struct mipi_dsi_device *dsi)
{
	const struct mipi_dsi_device_info info = {
		.type = "ams968hh01",
		.channel = 0,
		.node = NULL,
	};
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = AMS968HH01_MAX_LEVEL,
		.max_brightness = AMS968HH01_MAX_LEVEL,
		.power = BACKLIGHT_POWER_OFF,
	};
	static const u8 no_mtp[AMS968HH01_GAMMA_LEN];
	struct device *dev = &dsi->dev;
	struct mipi_dsi_host *dsi1_host;
	struct device_node *dsi1;
	struct ams968hh01 *ctx;
	int ret, i;

	ctx = devm_drm_panel_alloc(dev, struct ams968hh01, panel, &ams968hh01_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return dev_err_probe(dev, PTR_ERR(ctx->vdd), "failed to get vdd\n");

	/* Logical 1 = held in reset (reset-gpios is GPIO_ACTIVE_LOW in DT) */
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "failed to get reset gpio\n");

	ctx->tcon_ready_gpio = devm_gpiod_get(dev, "tcon-ready", GPIOD_IN);
	if (IS_ERR(ctx->tcon_ready_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->tcon_ready_gpio), "failed to get tcon-ready gpio\n");

	ret = drm_of_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret)
		return dev_err_probe(dev, ret, "invalid rotation\n");

	ctx->temp = devm_iio_channel_get(dev, "temperature");
	if (IS_ERR(ctx->temp)) {
		if (PTR_ERR(ctx->temp) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(ctx->temp),
					     "failed to get the temperature channel\n");
		ctx->temp = NULL;
	}
	INIT_DELAYED_WORK(&ctx->temp_work, ams968hh01_temp_work);

	/*
	 * The panel node sits under the master DSI controller and has a second
	 * input port fed by the other controller. Register a DSI device there.
	 */
	dsi1 = of_graph_get_remote_node(dev->of_node, 1, -1);
	if (!dsi1)
		return dev_err_probe(dev, -ENODEV, "no second DSI port\n");
	dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
	of_node_put(dsi1);
	if (!dsi1_host)
		return dev_err_probe(dev, -EPROBE_DEFER, "second DSI host not ready\n");

	ctx->bl = devm_backlight_device_register(dev, dev_name(dev), dev, ctx,
						 &ams968hh01_bl_ops, &props);
	if (IS_ERR(ctx->bl))
		return dev_err_probe(dev, PTR_ERR(ctx->bl), "failed to register backlight\n");

	ctx->dsi[0] = dsi;
	ctx->dsi[1] = mipi_dsi_device_register_full(dsi1_host, &info);
	if (IS_ERR(ctx->dsi[1]))
		return dev_err_probe(dev, PTR_ERR(ctx->dsi[1]), "failed to register second DSI device\n");

	mipi_dsi_set_drvdata(dsi, ctx);
	/* Until the MTP is read: the tables of a panel with zero offsets */
	ams968hh01_gamma_tables(ctx, no_mtp);

	ctx->panel.prepare_prev_first = true;
	ctx->panel.backlight = ctx->bl;
	drm_panel_add(&ctx->panel);

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		ctx->dsi[i]->lanes = 4;
		ctx->dsi[i]->format = MIPI_DSI_FMT_RGB888;
		/*
		 * Command mode, init commands in LP. The DDI ignores
		 * everything when the HS clock runs through its reset, and
		 * it wants a continuous clock: LP-11 on all lanes through
		 * the reset, then HS, as the vendor driver does
		 * (qcom,mdss-dsi-lp11-init, qcom,mdss-dsi-force-clock-lane-hs).
		 */
		ctx->dsi[i]->mode_flags = MIPI_DSI_MODE_LPM |
					  MIPI_DSI_CLOCK_DEFERRED_HS;

		ret = mipi_dsi_attach(ctx->dsi[i]);
		if (ret < 0) {
			dev_err(dev, "dsi%d attach failed: %d\n", i, ret);
			goto err_attach;
		}
	}

	return 0;

err_attach:
	while (--i >= 0)
		mipi_dsi_detach(ctx->dsi[i]);
	drm_panel_remove(&ctx->panel);
	mipi_dsi_device_unregister(ctx->dsi[1]);
	return ret;
}

static void ams968hh01_remove(struct mipi_dsi_device *dsi)
{
	struct ams968hh01 *ctx = mipi_dsi_get_drvdata(dsi);

	cancel_delayed_work_sync(&ctx->temp_work);
	mipi_dsi_detach(ctx->dsi[0]);
	mipi_dsi_detach(ctx->dsi[1]);
	mipi_dsi_device_unregister(ctx->dsi[1]);
	drm_panel_remove(&ctx->panel);
}

static ssize_t hbm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ams968hh01 *ctx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(ctx->hbm));
}

static ssize_t hbm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct ams968hh01 *ctx = dev_get_drvdata(dev);
	unsigned int hbm;
	int ret;

	ret = kstrtouint(buf, 0, &hbm);
	if (ret)
		return ret;
	if (hbm > AMS968HH01_HBM_STEPS)
		return -EINVAL;

	WRITE_ONCE(ctx->hbm, hbm);
	ret = backlight_update_status(ctx->bl);

	return ret ?: count;
}
static DEVICE_ATTR_RW(hbm);

static struct attribute *ams968hh01_attrs[] = {
	&dev_attr_hbm.attr,
	NULL
};
ATTRIBUTE_GROUPS(ams968hh01);

static const struct of_device_id ams968hh01_of_match[] = {
	{ .compatible = "samsung,ams968hh01" },
	{ }
};
MODULE_DEVICE_TABLE(of, ams968hh01_of_match);

static struct mipi_dsi_driver ams968hh01_driver = {
	.probe = ams968hh01_probe,
	.remove = ams968hh01_remove,
	.driver = {
		.name = "panel-samsung-ams968hh01",
		.of_match_table = ams968hh01_of_match,
		.dev_groups = ams968hh01_groups,
	},
};
module_mipi_dsi_driver(ams968hh01_driver);

MODULE_DESCRIPTION("Samsung AMS968HH01 (ANA38401) dual-DSI command mode panel");
MODULE_IMPORT_NS("IIO_CONSUMER");
MODULE_LICENSE("GPL");
