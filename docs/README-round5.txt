Galaxy Tab S3 Wi-Fi - optional quick test (RAM boot only, nothing flashed)

Round 4 result: only 8 protected pins need to be left alone. This optional
test tells us which of the two pin groups is the protected one.

  fastboot boot boot-G.img     (protects fingerprint pins only)
  fastboot boot boot-H.img     (protects secure-chip pins only)

For each: WORKS (frozen screen + USB devices) or REBOOTS (~9 s).
boot.img is the current good image.
