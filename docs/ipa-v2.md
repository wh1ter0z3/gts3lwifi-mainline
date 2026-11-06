# Mobile data on the SM-T825: IPA v2.5 port (work in progress)

Status: **started 2026-11-06, not compile-tested, not in any package.** The patches are in
`wip/ipa-v2/` (for Linux 7.3.1 on top of this repo's kernel patches). Nothing here has run on
hardware yet.

## Why

The SM-T825's modem hands mobile data to Linux through the IPA (IP Accelerator) block. The
MSM8996 has IPA v2.5 (stock DT: `qcom,ipa@680000`, `qcom,ipa-hw-ver = <5>`); mainline's IPA
driver (`drivers/net/ipa`) starts at IPA v3.0. SIM, registration and SMS don't need IPA
(`docs/lte.md`); data does.

## Starting point

Sireesh Kodali's RFC "net: ipa: Add support for IPA v2.x" (2021-09-20, 17 patches, with
patches by Vladimir Lypak) and its BAM DMA prerequisites (immediate commands, 3 patches;
metadata, 1 patch). Tested by its author on 5.13: an MSM8996 phone got an IP address through
ModemManager; traffic was not tested. All 21 patches apply on v5.15. Alex Elder reviewed the
series. Reference for the hardware: the downstream IPA v2 driver
(`drivers/platform/msm/ipa/ipa_v2` in Qualcomm's msm-3.18 based kernels).

Since 2021 the upstream driver changed a lot (per-version register files, transaction rings
instead of lists, endpoint and power rework), so the RFC is redone on 7.3, not rebased.

## Done

| Patch | What |
|---|---|
| 0001-0004 | BAM DMA: immediate commands (`DMA_PREP_IMM_CMD`, BAM descriptor flag IMM) and transfer-size metadata (Sireesh Kodali, applied unchanged) |
| 0005 | IPA v2.0/2.5/2.6L versions; IPA v2.5 registers (`reg/ipa_reg-v2.5.c`); registers IPA v2.x lacks marked invalid |
| 0006 | IPA v2.5 packet status: opcodes and the different layout of words 3-7 |
| 0007 | IPA v2.x interrupt numbering, translated both ways |
| 0008 | pipeline clear timeout (RFC, Vladimir Lypak / Sireesh Kodali) |

Fixed against the RFC: the uC interrupt offset (`1000 * ee`, should be `0x1000`); status
opcodes, which the RFC changed for every IPA version (wrong for v3.0+, as the review noted);
interrupt numbers shifted only when enabling, so a v2 TX_SUSPEND would have been handled as
BAM_GSI_IDLE. Its BAM transport (`bam.c`) also has problems to avoid in the port: it can use the
list head as a transaction when nothing completed, reports completions twice (DMA callback and
NAPI), and attaches the RX length metadata after submitting the descriptor.

## Next

1. BAM transport. Keep 7.3's `gsi_*` names as the shared interface (not the RFC's rename to
   `ipa_dma`, for a smaller diff) and add BAM as a second backend: one dmaengine descriptor per
   buffer, command opcodes as immediate commands, RX length through metadata attached before
   submit, in-order completion from the DMA callback into the existing transaction ring and
   NAPI; channels requested by name from the BAM at 0x684000.
2. 32-bit command payloads and table init, endpoints, memory map, QMI init differences,
   microcontroller (RFC 10-14).
3. `data/ipa_data-v2.5.c` (MSM8996 endpoints) and the `qcom,msm8996-ipa` compatible.
4. DT node for the SM-T825 (registers 0x40000 into the IPA wrapper, BAM, interrupts, clock,
   modem handshake), with the modem from `docs/lte.md` running.
5. Test on an SM-T825 with a SIM: probe, modem handshake, `rmnet` interface, ModemManager
   connection, then real traffic.
