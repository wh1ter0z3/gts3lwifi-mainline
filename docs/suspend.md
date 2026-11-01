# Suspend (s2idle)

## Status (2026-10-28): works without Wi-Fi; Wi-Fi fix written, not tested

The kernel offers only suspend-to-idle (`/sys/power/mem_sleep`: `[s2idle]`;
no PSCI `SYSTEM_SUSPEND`). Wake-up sources: the PM8994 RTC (`rtcwake`) and
the power key (`pm8941_pwrkey`); since 2026-11-01 also the keyboard
cover, while it is attached (docs/input.md).

| Test (20 s, `rtcwake -m freeze`) | Result |
|---|---|
| everything loaded | reset during resume |
| Venus unloaded | reset |
| Venus and ath10k (Wi-Fi) unloaded | sleeps 22 s, resumes, `suspend_stats` success |
| ath10k unloaded, Venus loaded | sleeps, resumes |
| ath10k reloaded after a suspend | device inaccessible, bus hangs, reset |

So the Wi-Fi on PCIe is the problem. With `no_console_suspend`, lk2nd's RAM
console (`lk2nd.pass-ramoops=zap`, then `fastboot oem ramoops console` and
`fastboot get_staged`) and dynamic debug in `drivers/base/power/main.c`
(each device's suspend and resume callback, with `dmesg -n 8`), the link is
fine through all suspend phases until genpd switches the PCIe controller's
power domain (PCIE0_GDSC) off in the noirq phase. The first access on resume
fails: `ath10k_pci 0000:01:00.0: Unable to change power state from D3hot to
D0, device inaccessible`, and a later one hangs the bus.

pcie-qcom keeps the controller's resources on while a device is linked up,
but on msm8996 the PCIe GDSCs are `PWRSTS_OFF_ON`, so genpd really turns
them off. Newer SoCs (sm8250 and others) use `PWRSTS_RET_ON`: the GDSC stays
on and the hardware puts it in retention. Kernel patch 0037 does the same
for msm8996. It is in the built-in clock driver, so it needs the full r6
kernel to test.

## Not yet looked at

- During the 20 s suspends without Wi-Fi, the RPM sleep counters
  (`/sys/kernel/debug/qcom_stats/vlow` and `vmin`) stayed at 0: the SoC
  never reached XO shutdown or VDD minimum, so the saving over plain idle is
  probably small.
- With everything loaded, the s2idle loop ended 1.5 ms after it started
  (some wake-up was pending, maybe the PCIe link going down).
- `qrtr 5-19: PM: parent remoteproc0:smd-edge.IPCRTR.-1.-1 should not be
  sleeping`: a device-link warning from the ADSP's QRTR, harmless so far.
