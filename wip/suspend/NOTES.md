# Suspend and deep idle: work in progress (2026-11-02)

Status of the suspend work on the SM-T820 (headless board, RAM-booted test
kernels on top of kernel r9 / patches 0001-0123), for whoever continues it.

## Results

| | Idle current at 4.3 V, Wi-Fi connected |
|---|---|
| r9 (CPU state + L2 GDHS + system retention) | floor 133 mA, average 141 mA |
| + L2 and system power collapse (0001 + `test-idle-fpc.dts`) | floor 88 mA, average 126 mA |
| + unused RPM clocks off (0002, without `clk_ignore_unused`) | floor 78 mA, average 119 mA |

- s2idle with Wi-Fi works on battery (patch 0037 confirmed): 6/6 suspends of
  1-10 min; a 21-cycle soak (suspend, Wi-Fi, audio, 4-core burst) passed.
- RPM `vlow`/`vmin` (`/sys/kernel/debug/qcom_stats`) never counted, in any
  configuration: XO shutdown and VDD minimization are not reached yet.
- The fuel gauge's fine SOC (`i2cget -f -y 2 0x71 0x05 w`, 1/256 %) is held near
  100 % on a full battery; measure drain mid-range.

## Patches (`patches/`, on top of 0001-0123)

1. **psci: save the context for MSM8996 L2 power collapse.** The firmware is
   OSI with the extended StateID format and composes StateIDs like downstream
   lpm-levels (CPU mode bits 3:0, L2 7:4, system 15:8, affinity at 24). It
   refuses bit 30 for the CPU alone (NOT_SUPPORTED) and DENIES it for L2 PC
   (0x41000044). Downstream never sets bit 30 (`qcom,is-reset` is only taken
   from the CPU level, and msm8996-pm.dtsi has none there): L2 PC 0x01000044,
   system FPC 0x02003444, always with the context saved. The quirk treats L2
   mode 4 as context-losing. Ready to test on more boards.
2. **clk: qcom: smd-rpm: drop the handoff votes of unused clocks.** The handoff
   votes every RPM clock on in the active and the sleep set, without
   `.is_prepared`, so `clk_disable_unused()` never drops the unused XO buffers
   (bb_clk, rf_clk, div_clk, ln_bb_clk_a). IPA keeps its vote: dropping it
   resets the SoC in suspend right after dwc3 suspends (bisected). Needs the
   boot without `clk_ignore_unused`; boots, Wi-Fi, BT, DSPs and audio were fine
   on the headless board. **Untested with a display.**
3. **cpuidle: psci: init after the deferred probes** (WIP, needed for 4).
   With the vMPM as parent of the system domain, the PSCI domains defer, and
   cpuidle-psci's faux device cannot defer: CPUs fall back to WFI.
4. **irqchip: qcom-mpm: ack the doorbell, program the wakeup timer** (WIP).
   The APCS doorbell has no TX-done IRQ; without `mbox_client_txdone()` the
   queue overflows after the first message and every system power-off fails.
   The timer words (arch counter of the next wakeup, ~0 in s2idle) follow
   downstream `mpm-of.c`.
5. **TEST ONLY: qcom-wdt keep_in_suspend.** Not for packaging.

## Test DTs (`dts/test/`)

- `test-energy.dts`: energy model (opp-microwatt from downstream busy-cost-data); EAS works.
- `test-suspend.dts`: energy model + display off (headless board).
- `test-idle-fpc.dts`: cluster_fpc 0x01000044, system_fpc 0x02003444, `local-timer-stop`.
- `test-wdt.dts`: the APSS watchdog (`qcom,kpss-wdt` at 0x9830000, SPI 28/29); none of our DTs had one.
- `test-mpm.dts`: the vMPM (downstream msm8996-pm.dtsi `qcom,mpm@681b8`): `sram@1b8` in
  rpm_msg_ram, `mboxes = <&apcs_glb 1>`, SPI 171, 96 pins, pin 87 = SPMI arbiter
  (downstream gic-map lists SPI + 32), `system_pd` with `power-domains = <&mpm>`,
  the SPMI arbiter's interrupt through the MPM. Needs `CONFIG_QCOM_MPM=y`.
- `test-mpm-nopcie.dts`: the same without Wi-Fi's PCIe link.

## How the tests were run

`scripts/make-test-image.sh` builds a RAM-boot image from the installed
boot.img with another kernel (`KERNEL=`), extra modules (`EXTRA_RAMDISK=`, only
`usr/lib/modules/<release>`; with all modules, ath10k and the remoteprocs load
before the rootfs and miss their firmware) and cmdline changes
(`EXTRA_CMDLINE=`, `CMDLINE_DROP=`). Useful cmdline: `panic=10
lk2nd.pass-ramoops=zap no_console_suspend systemd.watchdog_sec=120
qcom_wdt.keep_in_suspend=1`. After a warm reset, with the lk2nd slot header
cleared: `fastboot oem ramoops console` and `fastboot get_staged`. A battery
pull loses RAM. Never `modprobe -r ath10k_pci` (hard hang).

`tools/`: `soak.sh` (suspend/Wi-Fi/audio/CPU cycles), `mpm-test.sh` (waits for
USB unplug, two RTC-woken suspends, logs vlow/vmin, the system domain and the
MPM/RTC interrupts), `susp-usb.sh`, `audio-test.sh`.

## Where it stands with the vMPM (patches 3-4, test-mpm*.dts)

- With Wi-Fi: 40 s s2idle unplugged, the RTC alarm wakes through MPM pin 87,
  2/2; with USB plugged a suspend also lasts its full time now (the SPMI
  interrupt only wakes when wake-enabled). vlow/vmin 0 (PCIe holds XO).
- Without PCIe: the first suspend fails at once (reason not captured), the
  second works; vlow/vmin still 0; minutes later, in plain idle, the tablet
  hangs until the battery is pulled (no ramoops, no self-reset, the watchdog
  does not bite). It also hung without the vMPM (no wake path at all).
  `qcom_mpm` fired 19 times in idle before USB was unplugged, so the RPM does
  hand back through it.

## Next steps

1. Never write ~0 into the vMPM timer outside s2idle (cap it to 1-2 s), to rule
   out a missing timer wakeup as the idle hang.
2. GPIO wakeup through the vMPM (downstream `qcom,gpio-map`; pinctrl-msm8996
   has no wakeirq map yet), so that plugging USB (SM5705 MUIC/charger IRQ on
   TLMM) wakes a stuck tablet: recoverable and diagnosable tests.
3. Capture the first-suspend failure without PCIe (`last_failed_dev`, dmesg).
4. Find what still holds XO without PCIe and USB (sleep-set votes: RPM
   regulators/rpmpd with `pd_ignore_unused`, interconnect votes, GCC branches
   under bi_tcxo, the ADSP/SLPI).
5. PCIe link teardown in suspend (downstream msm_pcie suspends the link for
   QCA6174), so that Wi-Fi does not hold XO.

Downstream references (Samsung GPL kernel): `arch/arm/boot/dts/qcom/msm8996-pm.dtsi`
(lpm-levels, `qcom,mpm@681b8` with gic-map and gpio-map),
`drivers/cpuidle/lpm-levels.c` (`psci_enter_sleep`, `get_cluster_id`),
`drivers/soc/qcom/mpm-of.c` (`msm_mpm_enter_sleep`, timer words).
