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
 * UNTESTED DRAFT - not yet built or run on hardware.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <video/mipi_display.h>

struct ams968hh01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	struct regulator *vdd;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *tcon_ready_gpio;
};

static inline struct ams968hh01 *to_ams968hh01(struct drm_panel *panel)
{
	return container_of(panel, struct ams968hh01, panel);
}

/*
 * Full-panel mode. The msm DSI host halves the horizontal timings per link
 * in bonded mode, so these are twice the downstream per-link values
 * (1024 active, hfp 212, hpw 16, hbp 108; vfp 10, vpw 2, vbp 6).
 */
static const struct drm_display_mode ams968hh01_mode = {
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

/* Downstream qcom,mdss-dsi-on-command. B0 = register offset, B1/B2 = write. */
static void ams968hh01_init(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x34);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x64, 0x00);	/* check fail off */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x75);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x28);		/* INTR setting */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x51);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb1, 0x06);		/* dual DSI */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0xd9);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb1, 0x08);		/* Anapass compression mode */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x7f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x15);		/* TSP HTE / VTE */

	mipi_dsi_dcs_set_tear_on_multi(ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_set_tear_scanline_multi(ctx, 1526);

	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0xbc);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x04);		/* gamma offset index */

	/* Brightness condition set: fixed 360 nit gamma for now */
	mipi_dsi_dcs_write_seq_multi(ctx, 0x83,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00);

	mipi_dsi_dcs_write_seq_multi(ctx, 0x90, 0x00, 0x00, 0x0e);	/* AID, 360 nit */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x67);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x0f);		/* ELVSS, 360 nit */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x4d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x04);		/* 16 frame avg, ACL off */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x36);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x10);		/* ACL off */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x6d);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x85);		/* CAPS on */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x35);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb2, 0x01);		/* gamma update key */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0xd0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb1, 0x1d);		/* UPLL_F = 35 */
	mipi_dsi_msleep(ctx, 215);
}

static void ams968hh01_power_off(struct ams968hh01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	regulator_disable(ctx->vdd);
	msleep(10);
}

static int ams968hh01_prepare(struct drm_panel *panel)
{
	struct ams968hh01 *ctx = to_ams968hh01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi[0] };
	int val, ret;

	ret = regulator_enable(ctx->vdd);
	if (ret)
		return ret;
	msleep(10);

	/*
	 * The DSI lanes are already in LP11 here (prepare_prev_first). The
	 * DDI wants 70 ms plus margin between LP11 and reset.
	 */
	msleep(75);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	/* The DDI raises TCON_RDY once it accepts commands (downstream: up to 1 s) */
	ret = read_poll_timeout(gpiod_get_value_cansleep, val, val,
				1000, 1000000, false, ctx->tcon_ready_gpio);
	if (ret)
		dev_warn(panel->dev, "TCON_RDY not asserted, continuing anyway\n");
	usleep_range(1000, 2000);

	ams968hh01_init(&dsi_ctx);
	if (dsi_ctx.accum_err) {
		ams968hh01_power_off(ctx);
		return dsi_ctx.accum_err;
	}

	return 0;
}

static int ams968hh01_enable(struct drm_panel *panel)
{
	struct ams968hh01 *ctx = to_ams968hh01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi[0] };

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	return dsi_ctx.accum_err;
}

static int ams968hh01_disable(struct drm_panel *panel)
{
	struct ams968hh01 *ctx = to_ams968hh01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi[0] };

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
	return drm_connector_helper_get_modes_fixed(connector, &ams968hh01_mode);
}

static const struct drm_panel_funcs ams968hh01_panel_funcs = {
	.prepare = ams968hh01_prepare,
	.enable = ams968hh01_enable,
	.disable = ams968hh01_disable,
	.unprepare = ams968hh01_unprepare,
	.get_modes = ams968hh01_get_modes,
};

static int ams968hh01_probe(struct mipi_dsi_device *dsi)
{
	const struct mipi_dsi_device_info info = {
		.type = "ams968hh01",
		.channel = 0,
		.node = NULL,
	};
	struct device *dev = &dsi->dev;
	struct mipi_dsi_host *dsi1_host;
	struct device_node *dsi1;
	struct ams968hh01 *ctx;
	int ret, i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

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

	ctx->dsi[0] = dsi;
	ctx->dsi[1] = mipi_dsi_device_register_full(dsi1_host, &info);
	if (IS_ERR(ctx->dsi[1]))
		return dev_err_probe(dev, PTR_ERR(ctx->dsi[1]), "failed to register second DSI device\n");

	mipi_dsi_set_drvdata(dsi, ctx);

	drm_panel_init(&ctx->panel, dev, &ams968hh01_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;
	drm_panel_add(&ctx->panel);

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		ctx->dsi[i]->lanes = 4;
		ctx->dsi[i]->format = MIPI_DSI_FMT_RGB888;
		/* command mode, init commands in LP, continuous clock */
		ctx->dsi[i]->mode_flags = MIPI_DSI_MODE_LPM;

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

	mipi_dsi_detach(ctx->dsi[0]);
	mipi_dsi_detach(ctx->dsi[1]);
	mipi_dsi_device_unregister(ctx->dsi[1]);
	drm_panel_remove(&ctx->panel);
}

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
	},
};
module_mipi_dsi_driver(ams968hh01_driver);

MODULE_DESCRIPTION("Samsung AMS968HH01 (ANA38401) dual-DSI command mode panel");
MODULE_LICENSE("GPL");
