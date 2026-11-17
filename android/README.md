# Android on the Galaxy Tab S3

Android on our mainline kernel, without anything from Samsung in the images:
the tablet's own firmware is read from its partitions at boot.

| Folder | What | Goes to |
|---|---|---|
| [device-gts3/](device-gts3/) | The open vendor: device tree for AOSP 15 (`android-15.0.0_r36`), its vendor-side patches, the [plan](device-gts3/PLAN.md). Any GSI (Android 15, 16 or 17) runs on the vendor image it builds | `device/samsung/gts3` in an AOSP tree |
| [orangefox/](orangefox/) | OrangeFox recovery for the SM-T820 and SM-T825: knows the vendor partition, never writes lk2nd's boot partition, ramdisk under S-Boot's 12 MiB limit | `device/samsung/gts3l-recovery` in an OrangeFox 12.1 tree |

The boot side (gsi-init, the ramdisk that starts Android from image files,
and the installers of the test packages) is on the `gsi` branch, under `gsi/`.
