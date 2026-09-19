# Galaxy Tab S3 Wi-Fi (SM-T820) – postmarketOS test, round 1

Goal: check that mainline Linux boots and USB works. This round writes
**nothing** except lk2nd to the tablet, and it can be undone.

Needs: an SM-T820 with OEM unlock enabled, a USB cable, a Linux PC is best
(Windows works with Odin + platform-tools, but USB networking may need a driver).
Files in this package: `boot.img`, `lk2nd.img`, `lk2nd-odin.tar`,
`lk2nd-wipe-odin.tar`, `SHA256SUMS`.

## 1. Install lk2nd (skip if you already have this lk2nd)
1. Power off. Hold **Power + Volume Down + Home** to enter Download mode.
2. Flash `lk2nd-odin.tar` in Odin (AP slot), or on Linux:
   `heimdall flash --BOOT lk2nd.img`
3. The tablet reboots. After the Samsung logo it should stay in **lk2nd fastboot**.
   If you only see the Samsung logo or it reboots in a loop, stop and report that.

## 2. Send the lk2nd log
```
fastboot devices
fastboot oem log
fastboot get_staged lk2nd.log
fastboot getvar all 2> getvar.txt
```
Send `lk2nd.log` and `getvar.txt`.

## 3. Boot Linux from RAM (writes nothing)
```
fastboot boot boot.img
```
Wait 2 minutes, then on the PC check for new USB devices:
```
lsusb
ip -br addr
dmesg | tail -40
```
Expected: a USB network interface with IP 172.16.42.2, and a small USB
drive with log files. Then:
```
telnet 172.16.42.1
dmesg > /tmp/d.txt; cat /tmp/d.txt
```
Send: `lsusb` output, host `dmesg | tail -40`, the log files from the USB
drive, and the tablet `dmesg` if telnet worked.
"Could not find root partition" inside the tablet is **expected** in this round.

If nothing appears, hold **Power + Volume Down** ~10 s to force a reboot.
Nothing was written, so you are back in lk2nd fastboot. Report "no USB".

## Undo everything
Download mode → flash the stock firmware in Odin, or flash
`lk2nd-wipe-odin.tar` to go back to plain lk2nd fastboot.
