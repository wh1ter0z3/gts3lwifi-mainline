Galaxy Tab S3 Wi-Fi - mainline test, round 2
Nothing is flashed. Same steps as run 1, three new boot images.

  boot-A.img  most likely fix (GPIO protection + debug blocks off)
  boot-B.img  GPIO protection only
  boot-C.img  debug blocks off only

1) Try boot-A.img first:
   fastboot --cmdline "console=ttyMSM0,115200 lk2nd.pass-ramoops=zap" boot boot-A.img

   Note: does it vibrate/come back to fastboot? After how many seconds?
   If it does NOT come back within 2 minutes: on the PC check
   Device Manager (or lsusb / ip a on Linux) for a new USB network
   adapter or USB drive, and tell me what you see.

2) If it came back to fastboot, get the log (don't boot anything first):
   fastboot oem ramoops console
   fastboot get_staged A-console.txt

3) Repeat steps 1-2 with boot-B.img (save B-console.txt),
   then boot-C.img (save C-console.txt).

Send: the seconds for each image, whether it came back, and the txt files.
Do NOT use "fastboot flash".
