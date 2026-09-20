Galaxy Tab S3 Wi-Fi - mainline test, round 4: which GPIOs are protected?
Nothing is flashed. Same as before: RAM boot only.

Result so far: boot-B (all GPIOs protected) works, boot-C reboots. So the
reset is caused by protected GPIO pins. These images find which ones.

For EACH image: boot it, and note one of two outcomes:
  WORKS   = screen stays frozen, USB devices (network/serial/drive) appear
  REBOOTS = black screen + vibration after ~9 s, back in fastboot

Between images: if it WORKS, hold Power + Volume Down ~10 s to get back
to fastboot. If it REBOOTS you are already there.

  fastboot boot boot-D.img      (protects only fingerprint + eSE pins)
  fastboot boot boot-E.img      (protects GPIO 0-74)
  fastboot boot boot-F.img      (protects GPIO 75-149)

boot-main.img is the current good image (same as boot-B) if you want a
known-working one.

Please send: D = works/reboots, E = works/reboots, F = works/reboots.
