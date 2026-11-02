# Device tree validation (dtschema)

Checked 2026-10-29 with dtschema 2026.9 on the 6.19.5 tree with patches
0001-0067 (`make dt_binding_check`, `make CHECK_DTBS=y`, and
`dt-validate` on the test DTs compiled the way the boot images are).

## Bindings

Clean: `display/panel/samsung,ams968hh01.yaml` (patches 0065-0066),
`input/touchscreen/wacom,w9007a-lt03.yaml` (0056, 0058, 0062),
`power/supply/siliconmitus,sm5705-charger.yaml`,
`power/supply/siliconmitus,sm5705-fuel-gauge.yaml`,
`extcon/siliconmitus,sm5705-muic.yaml`, `usb/samsung,s2mm005.yaml`,
`sound/richtek,rt5508.yaml`.

## Main DT (`apq8096-samsung-gts3lwifi.dts`)

Nine findings:

- The board compatible `samsung,gts3lwifi` is not in
  `Documentation/devicetree/bindings/arm/qcom.yaml` (two findings: the
  `oneOf` on `/compatible`, and "failed to match any schema"). The
  binding needs the board added to the msm8996/apq8096 list.
- `domain-idle-states/system-ret`: idle state nodes must be named
  `domain-...` (or `cpu-`/`cluster-`), e.g. `domain-ret`.
- `syscon@4ab000` (`pshold`): a bare `"syscon"` compatible is not allowed;
  it needs a specific compatible before `"syscon"`.
- `s2mm005-int-default-state` and `wcd-intr-default-state` (in `&tlmm`):
  the msm8996 TLMM binding has no `input-enable` (upstream Qualcomm DTs
  dropped it); remove it.
- `codec@1,0` (WCD9335): the binding wants both interrupts, `intr1` and
  `intr2`; the DT gives only `intr1`.
- `syscon@9a10000` (`saw3` in `msm8996.dtsi`): same bare `"syscon"`; this
  one is in the SoC file, not the board DT.

## Test DTs

`dts/test/test-display.dts`, `test-display-temp.dts`, `test-touch.dts`
and `test-spen.dts` add no findings of their own. Before patch 0065 the
panel node had no binding to match.

`test-sdcard.dts`, `test-keys.dts` and `test-keyboard.dts` (2026-10-29)
add none either, checked once against the schemas of the tree with
patches 0001-0067 and once with 0001-0075. Only the second knows
`st,stmpe1801` (patch 0069): before it, the keyboard node matches no
schema and is not checked at all. With it the node validates, and a copy
with 11 columns or a 5 ms debounce is rejected. `dt_binding_check` on
`mfd/st,stmpe.yaml` with patch 0069 is clean. The keyboard nodes moved
into the main DT on 2026-10-31 (`test-keyboard.dts` is gone) and still add
none, against the tree with patches 0001-0104.

The hotplug bus around the keyboard (2026-11-01) adds none either: against
the schemas of the tree with patches 0001-0120 the DT has the same 9
findings as before it. `i2c-hotplug-extcon` needs patch 0117;
`dt_binding_check` on `i2c/i2c-hotplug-gpio.yaml` and
`i2c/i2c-hotplug-extcon.yaml` is clean.

The USB-C nodes merged into the main DT on 2026-10-31 add none either,
against the schemas of the tree with patches 0001-0087. The charger's
`usb-otg-vbus` regulator needs patch 0078; the connector's roles,
`vbus-supply` and `sink-pdos` are `usb-connector.yaml` properties.
`dt_binding_check` on `usb/samsung,s2mm005.yaml` and
`power/supply/siliconmitus,sm5705-charger.yaml` is clean.

The cameras merged into the main DT on 2026-10-31 add none either,
against the schemas of the tree with patches 0001-0103. The front
sensor's `samsung,s5k5e3` needs patch 0094 and the lens's `vdd-supply`
and `vio-supply` patch 0092; the IMX258, CCI and CAMSS nodes use
existing bindings.

dtc also warns about four things in `msm8996.dtsi`, `pm8994.dtsi` and
`pm8004.dtsi` (unneeded `#address-cells`, a duplicate unit address for
`i2c@75ba000`/`spi@75ba000`); none come from the board files.
