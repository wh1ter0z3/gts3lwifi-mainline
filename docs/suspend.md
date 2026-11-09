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

## Suspending more than once

Without patch 0148, the second suspend after a boot fails at `prepare` with -EINVAL
from `mdp5_pipe_release`. After resume the plane runs on a pipe the MDP5 global state
calls free, so disabling it again fails. With the patch, four 20 s cycles in a row
all sleep and resume, and Wi-Fi and Bluetooth still work (SM-T820, 2026-11-02).

## Linux 7.3

Patch 0037 is not enough on 7.3: mainline now cuts vddpe-3v3 during suspend, so
patch 0147 keeps the link up. See [kernel-7.3](kernel-7.3.md).

## Not yet looked at

- During the 20 s suspends without Wi-Fi, the RPM sleep counters
  (`/sys/kernel/debug/qcom_stats/vlow` and `vmin`) stayed at 0: the SoC
  never reached XO shutdown or VDD minimum, so the saving over plain idle is
  probably small.
- With everything loaded, the s2idle loop ended 1.5 ms after it started
  (some wake-up was pending, maybe the PCIe link going down).
- `qrtr 5-19: PM: parent remoteproc0:smd-edge.IPCRTR.-1.-1 should not be
  sleeping`: a device-link warning from the ADSP's QRTR, harmless so far.

## Deep sleep (work in progress, 2026-11-09)

Goal: the RPM turning XO off (vlow) and lowering the rails (vmin) while the tablet sleeps. Before this
work its counters (`/sys/kernel/debug/qcom_stats/vlow`, `vmin`) never moved. Tested on an SM-T820 from
RAM boots, battery only (cable out), display blanked.

| Step | State |
|---|---|
| L2 and system power collapse | **Granted** by Samsung's TrustZone as the stock kernel encodes them: 0x01000044 and 0x02003444, without the reset bit 30 (stock takes that bit from the CPU level only, `psci_enter_sleep()` in downstream lpm-levels.c; with it the firmware answers NOT_SUPPORTED). `dts/test/test-deepidle.dts` |
| MPM | Described from the stock DTs (vMPM at 0x681b8 in the RPM message RAM, IPC bit 1, SPI 171, pin 87 = the SPMI PMIC arbiter) for mainline's `irq-qcom-mpm`; power key, RTC and PMIC GPIOs work through it. `dts/test/test-deepidle-mpm.dts`, needs `CONFIG_QCOM_MPM` |
| MPM wake-up timer | **Patch 0183**: mainline never wrote the vMPM timer; without it a suspend with XO released never woke up. Now the earliest timer of the CPU domains, or none in suspend-to-idle, as downstream does |
| Modem holding XO | **Patch 0184**: the modem's GPLL0 vote had GPLL0 as its parent, so the running modem kept GPLL0 and XO on from the AP's side; stock has it as a plain vote bit |
| A modem that never boots | keeps the RPM's MPSS "active" forever and blocks XO shutdown: the modem has to run (rmtfs) for the SoC to sleep |
| All four RPM masters release XO | Yes (RPM master stats at 0x68150, read from `/dev/mem`), once USB and the Bluetooth UART are idle |
| RPM turns XO off | **Not yet**: vlow/vmin stay 0. Its sleep set still has bus bandwidth/clock votes (the RPM log shows `bmas`, `bslv`, `clk1` around every sleep); zeroing all of them hangs the system, so it is per device |
| Idle current | 129 mA with XO held, 90-97 mA (lowest 72) with all masters releasing XO |

Still holding XO in daily use: USB (the T820 does not notice an unplug: `s2mm005 ... failed to set USB
role device: -95`), the Bluetooth UART (open even when Bluetooth is off), the PCIe link during suspend
(patch 0147), and the boot options `clk_ignore_unused`/`pd_ignore_unused`. The test DTs are not for daily
use: `test-deepidle-mpm-nopcie.dts` drops Wi-Fi, and a deep suspend can leave the tablet asleep until a
power-key reset.

