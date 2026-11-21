# Hardware encryption (eMMC inline crypto engine)

The eMMC controller of the MSM8996 has an inline crypto engine (ICE): the
data of a request is encrypted or decrypted on its way to or from the eMMC,
so the CPU does no crypto work. Samsung's stock kernel uses it for Android's
full disk encryption. Since kernel r16 our mainline kernel drives it too,
through the standard blk-crypto interface, so fscrypt (file-based
encryption on ext4 and f2fs) uses it with the `inlinecrypt` mount option.

**Nura (2026-11-22):** the dual boot installer prepares every install for
it (ext4's `encrypt` feature on Nura's root, mounted with `inlinecrypt`),
Nura's partitions pass the engine through (device package r22), and the PC
setup can make an **encrypted folder, `~/Private`**, with its own passphrase
(below). Encrypting the whole home, unlocked with the login password, needs
fscrypt's PAM module, which Alpine doesn't ship; not done yet. Our Android
doesn't set up encryption.

## What works

Tested on an SM-T820 (2026-11-21), on a scratch ext4 in Samsung's `cache`
partition (backed up first, written back after):

- the engine: ICE v2.1.44, AES-256-XTS, data units of 512 bytes to 4 KiB,
  a 64-bit DUN, 30 key slots;
- fscrypt with `inlinecrypt`: "AES-256-XTS using blk-crypto (native)", with
  the default v2 policy and with `IV_INO_LBLK_64` (what Android uses);
- no plaintext of the test files anywhere on the raw partition;
- **the standard format**: files ICE wrote read correctly with fscrypt's
  software implementation (mounted without `inlinecrypt`), and files the CPU
  wrote read correctly through ICE;
- keys survive suspend and resume; unencrypted data (the engine in bypass)
  reads and writes byte for byte as before;
- deep sleep is unchanged: the engine's clock follows the eMMC's runtime PM.

## How it fits together

Mainline supports ICE v3 and later (SDCC v5, UFS). MSM8996 has ICE v2.1,
which differs in four places (patches 0226-0230):

| | ICE v3 (mainline) | ICE v2 (MSM8996) |
|---|---|---|
| Key call to TrustZone | ES 0x4: the 64-byte key, cipher, data unit size | ES 0x2: the key's two 32-byte halves separately, and the storage type (20 = eMMC); key words big endian in both |
| Engine start | ready after the enable sequence | starts in **global bypass**: clear `ICE_RESET` and bit 0 of `ICE_CONTROL`, check `BYPASS_STATUS` |
| Per request | CQE crypto or the SDCC v5 non-CQE registers | host controller vendor registers `CORE_VENDOR_SPEC_ICE_CTRL_INFO_1..3` (0x304..0x30c): DUN, key index, data unit size, bypass bit |
| Key slots | all of them | 2 to 31: TrustZone keeps 0 and 1 (full disk encryption) |

The engine is reset with the host controller, as in Samsung's driver; after
a reset it is set up again and gets its keys back before the next request.
TrustZone accepts the standard invalidate call; if a TrustZone refused it,
the slot is overwritten with a random key instead.

How the key format was found: with fscrypt's documented key derivation
(HKDF-SHA512, checked against the kernel's own key identifier) and the raw
blocks of a file ICE had written, only big endian key words with a little
endian DUN decrypt to the plaintext.

## In Nura

**The layout.** Nura's partitions sit inside the userdata partition. The
initramfs hook used to attach userdata as a loop device, which hides the
engine (fscrypt then falls back to the CPU). Since device package r22 it
maps each partition with device-mapper (`/dev/mapper/gts3-gts3_boot`,
`gts3-android_data`, `gts3-nura`), which passes inline encryption through:
every one of them advertises AES-256-XTS in
`/sys/block/dm-*/queue/crypto/modes`. A read-only loop device only lets the
kernel read the layout's GPT; if device-mapper fails, the hook falls back to
the loop device.

**The installer** turns on ext4's `encrypt` feature on Nura's root and adds
`inlinecrypt` to its mount options (`pmos_rootfsopts=` on the `nura` boot
entry). Neither changes anything until something is encrypted. On an
existing install the same by hand:
`sudo tune2fs -O encrypt /dev/mapper/gts3-nura`, and `,inlinecrypt` after
`pmos_rootfsopts=defaults` in `/boot/extlinux/extlinux.conf`.

**`~/Private`.** `setup.sh` / `setup.ps1` ask "Make an encrypted folder,
~/Private?" and its passphrase; the install creates the folder with
Google's `fscrypt` (in every image since device package r23) and locks it.
The passphrase stays in the tablet's RAM during the install. After logging
in:

    fscrypt unlock ~/Private     # asks for the passphrase
    fscrypt lock ~/Private       # or restart the tablet

Tested on an SM-T820 (2026-11-22): fscrypt reports "AES-256-XTS using
blk-crypto (native)" for files in Nura's root; reading the blocks under the
test files straight from `/dev/mapper/gts3-nura` showed only ciphertext;
locked, the names are scrambled and the files can't be opened; it survives a
restart locked, and unlocks again.

## Using it

On ext4 with the `encrypt` feature (`tune2fs -O encrypt`; `stable_inodes`
too for `IV_INO_LBLK_64`), mount with `-o inlinecrypt`, then add a key and
set a policy with `fscryptctl` or `fscrypt`. Without `inlinecrypt` the same
files are encrypted by the CPU (the ARMv8 crypto extensions), in the same
format.

What a full setup still needs:
- **Nura:** the whole home encrypted and unlocked with the login password
  (fscrypt's PAM module, `pam_fscrypt.so`, which Alpine's fscrypt package
  doesn't include).
- **Android:** file-based encryption with `inlinecrypt` in the vendor's
  fstab. A software Keymaster works; hardware-backed keys need the parked
  TrustZone work ([trustzone](trustzone.md)).

## Limits

- ICE 2.0 isn't covered (it needs one more workaround than 2.1).
- Without CQE (command queueing), as the eMMC runs today.
