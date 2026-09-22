Galaxy Tab S3 Wi-Fi - mainline test, round 3: full postmarketOS boot

WARNING: step 1 ERASES the Android userdata partition (all apps/data).
Only do this if that is OK. The boot partition is NOT touched; lk2nd stays.
Do NOT run "fastboot flash boot" - it would overwrite lk2nd.

1) In lk2nd fastboot, write the root filesystem (about 600 MB, takes a while):
   fastboot flash userdata rootfs.img

2) Boot the kernel from RAM (plain command, no --cmdline this time):
   fastboot boot boot.img

3) Screen stays frozen - normal. Wait ~2 minutes (first boot resizes the
   filesystem), then on the PC:
   ip -br addr                 (look for 172.16.42.2)
   ssh test@172.16.42.1        (ask psigod for the password)
   If ssh fails, try:  telnet 172.16.42.1   and send  cat /pmOS_init.log

4) Once logged in, please send the output of:
   uname -a
   df -h /
   sudo dmesg | tail -60
   systemctl --failed

Every boot needs step 2 again, UNLESS you run the new lk2nd
(lk2nd-gts3lwifi.img / "T3"). With that one you may install the kernel
permanently, and the tablet then boots postmarketOS on its own:
   fastboot flash boot boot.img
(Do NOT do this with the old 72 MB lk2nd - it would overwrite lk2nd.)
To go back to Android: flash stock firmware with Odin.
