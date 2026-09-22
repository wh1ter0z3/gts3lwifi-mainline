# Display: Samsung AMS968HH01 (ANA38401 DDI) on the Galaxy Tab S3

Everything below comes from the downstream tree
(`drivers/video/msm/mdss/samsung/ANA38401_AMS968HH01/`, the board dtsi and
Samsung's `ss_regulator_common.c`). Nothing has been tried on hardware yet.

## Panel facts

| Item | Value |
|---|---|
| Panel / DDI | Samsung AMS968HH01 AMOLED, Anapass ANA38401 driver IC |
| Resolution | 2048 x 1536 (QXGA), 196 x 147 mm, 24 bpp |
| Interface | 2 x MIPI DSI, 4 lanes each, **command mode**, split: each link drives 1024 x 1536 |
| DSI clock | 893.2 MHz per lane (downstream `panel-clockrate`), continuous clock (`force-clock-lane-hs`) |
| Per-link timing | hbp 108, hfp 212, hpw 16, vbp 6, vfp 10, vpw 2, 60 Hz |
| Full mode | 2048x1536, htotal 2720, vtotal 1554, pixel clock 253.6 MHz (msm host halves h-timings per link) |
| Tearing | TE on TLMM GPIO 10 (`mdp_vsync`), tear scanline 1526 |
| Commands | DCS, sent on link 0 only (downstream `qcom,dcs-cmd-by-left`), init in LP mode |
| Brightness | via DCS gamma/AID/ELVSS tables (Samsung "smart dimming"). Not done yet; init leaves the panel at 360 nit |

GPIOs (all TLMM): reset 143 (high = running), TCON_RDY 45 (input, DDI raises it
when ready), LCD_LDO_EN 46 (panel 3.3 V), TE 10.

## Power-on sequence (downstream, verified in code)

1. LCD_LDO_EN (GPIO 46) high, wait 10 ms (`ssreg-vdd`, post-on-sleep 10)
2. DSI clocks on, lanes in LP11 (`lp11-init`)
3. wait 75 ms (`ssreg-panelrst` pre-on-sleep, "requested by HW team")
4. reset GPIO 143 high (`panel-rst-seq = <1 0>`)
5. poll TCON_RDY (GPIO 45) until high, up to 1 s
6. wait 1 ms (`init-delay-us`), send the on-command list in LP mode
7. last register write (B0 D0 / B1 1D, "UPLL_F = 35") is followed by 215 ms
8. DCS 0x29 display on (+50 ms)

Power-off: DCS 0x28 (100 ms), reset low, 5 ms, LCD_LDO_EN low, 10 ms.

## On-command list (decoded)

`B0 xx` selects a register offset ("global parameter"), `B1`/`B2` write it.

| Command | Meaning (downstream comment) |
|---|---|
| B0 34 / B2 64 00 | check fail off |
| B0 75 / B2 28 | INTR setting |
| B0 51 / B1 06 | dual DSI |
| B0 D9 / B1 08 | Anapass compression mode |
| B0 7F / B2 15 | TSP HTE/VTE (touch sync) |
| 44 05 F6 | set tear scanline 1526 |
| B0 BC / B2 04 | gamma offset index |
| 83 + 33 bytes (0x80/0x00 pattern) | brightness condition set, 360 nit |
| 90 00 00 0E | AID, 360 nit |
| B0 67 / B2 0F | ELVSS, 360 nit |
| B0 4D / B2 04 | 16-frame average, ACL off |
| B0 36 / B2 10 | ACL off |
| B0 6D / B2 85 | CAPS on |
| B0 35 / B2 01 | gamma update key |
| B0 D0 / B1 1D, 215 ms | UPLL_F = 35 |
| 29 | display on |

The USA panel variant adds `B0 56 / B1 4C` (porch setting, Htotal 3224) and
uses a 895.4 MHz DSI clock. Panel revision is read from DCS DA/DB/DC.

## Mainline plan

- **DSI supplies** (from `drivers/gpu/drm/msm/dsi/dsi_cfg.c`, msm8996):
  vdda = PM8994 L2 1.25 V, vcca = PM8994 L28 0.925 V, vddio = PM8994 L14 1.8 V.
  PHY (`qcom,dsi-phy-14nm`): vcca = L28.
- **Dual link**: `qcom,dual-dsi-mode` on both controllers, `qcom,master-dsi` on
  dsi0, dsi1 clocked from the dsi0 PHY (as on sdm845-mtp). The panel node lives
  under dsi0 with two input ports.
- **Panel driver**: `kernel/drivers/gpu/drm/panel/panel-samsung-ams968hh01.c`,
  modelled on `panel-truly-nt35597.c` (two DSI devices) with the `_multi`
  helpers. Commands go to dsi[0] only. `prepare_prev_first` makes the DSI host
  power the lanes (LP11) before `prepare()`, which matches step 2.
- **Device tree**: `dts/test/test-display.dts` (compiles; not yet in the main
  DTS because the driver is untested).
- **Build**: the panel driver must be built into the kernel package
  (`pmbootstrap build --force linux-postmarketos-qcom-msm8996` with the extra
  patch); DRM_MSM is a module, so afterwards `msm.ko` can be iterated over ssh.

## First test, before any of this

`fastboot --cmdline "console=tty0 lk2nd.pass-simplefb=autorefresh,xrgb8888 clk_ignore_unused pd_ignore_unused" boot boot.img`
uses lk2nd's framebuffer. If kernel text shows up, the panel keeps running
under Linux and only the driver work remains.

## Open questions

- Whether commands really must go to link 0 only under mainline (the DDI is
  one chip; downstream never talks to link 1). If init fails, try dual writes.
- `qcom,sync-dual-dsi`: not set downstream (`cmd-sync-wait-broadcast` is
  commented out); start without it.
- Brightness control: needs the gamma/AID/ELVSS tables from the downstream DTS
  (`samsung,*_tx_cmds_revA`, `candela_map_table`). A fixed 360 nit is fine for
  bring-up but is bright and warm for an AMOLED; add a simple backlight later.
