# Getting onto the BlackBox

This page tracks what we know about the M-VAVE ANN BlackBox and what has to be
true before any BlackboxFX build goes onto a real unit.

**Current status: do not flash anything.** No recovery path exists yet.

## What the community has established

Source: [jakino0/mvave-blackbox-reverse-engineering](https://github.com/jakino0/mvave-blackbox-reverse-engineering)
(firmware V20). Their confidence labels are kept here.

| Fact | Status there |
| --- | --- |
| SoC is a JieLi AC79-family part (AC791N reported), PI32v2 core | strong inference (AC791N named by [entitymar's SDK](https://github.com/entitymar/annblackbox-firmware-engineering-SDK), WIP) |
| USB Audio: stereo, 24-bit (S24_3LE), 44.1 kHz only; EP `0x02` OUT playback, EP `0x83` IN capture | verified |
| Firmware ships encrypted (SFC, 32-byte blocks from flash `0x4000`); app runtime = offset + `0x02000120` | verified for V20 |
| Update image has a CRC chain (app CRC16, JLFS header CRC16, outer flash CRC, UFW header and entry-list CRCs) | verified |
| A version-only modified package passed the desktop updater but the device rejected it and rebooted as V20 | verified |
| A `usb_update_mode` (UBOOT1.00) boot-marker writer exists but nothing in V20 calls it | proven statically |
| No known software route into a recovery bootloader | current conclusion |
| App control protocol (M-EFCS over BLE/USB) can read the preset bank, current patch (92 bytes) and a few state bytes, not arbitrary RAM | verified |

### Cross-checked against the official package

The official `BlackBox.fwsc` update (version tag `BlackBox_020`, SHA-256
`c0fef191...26cb08`) was checked offline against the facts above. The JLFS
`app` entry is 655,712 bytes, `audio_dev` sits at app offset `0x5BC1A`, and
both USB Audio endpoint descriptors are at `0x5C3F0` / `0x5C424`, exactly as
documented. Vendor firmware and anything derived from it stay out of this
repository.

## What that means for custom firmware

BlackboxFX replaces the whole audio application. That is only sensible once
all of these are true:

1. **Recovery works without the normal firmware.** On JieLi parts this is
   usually a ROM/UBOOT download mode reached by a hardware strap (a test pad,
   a key held at power-on, or USB D+/D- conditions) used by JieLi's own
   production tools. It must be found on the BlackBox board and **proven by
   entering it and reading back flash** before anything is written.
2. **A full flash backup exists** and has been read back twice with identical
   hashes.
3. **The memory budget is known.** BlackboxFX's device profile needs about
   1.1 MB of RAM (`bfx --list` prints the exact figure). The AC79 family's
   internal SRAM may be smaller than that; whether the BlackBox has PSRAM is
   unknown. If it doesn't fit, the device profile shrinks (shorter cloud and
   loop buffers).
4. **Codec, ADC/DAC, knobs and footswitch are mapped.** The new firmware has
   to drive the audio codec, read the BlackBox's controls and keep USB Audio
   working. That comes from JieLi's AC79 SDK plus the RE map above.
5. **The image is signed/encrypted the way the bootloader expects**, or the
   bootloader is shown to accept a plain image in download mode.

## Plan

| Step | Who | Risk to device |
| --- | --- | --- |
| Run `tools/probe/blackbox_probe.py`, share output | you | none (read-only descriptors) |
| Photograph the board (both sides, chip markings, test pads) | you | low (opening the case) |
| Identify the SoC, flash chip, PSRAM and download-mode strap from photos | me | none |
| Enter download mode and read flash with JieLi tools, verify twice | you, guided | low |
| Port the engine onto the AC79 SDK as a standalone app | me | none (builds only) |
| First flash, only with steps 1-2 proven | you, guided | real, but recoverable |

## Engine portability notes

- `src/` is plain C99: no malloc, no I/O, no threads. One caller-provided
  arena holds every buffer, sized by `bfx_memory_required()`.
- Long buffers are int16 to halve RAM. Processing is float32. The PI32v2's
  floating-point support is not yet confirmed; if it lacks an FPU, the hot
  paths (cloud, space) need a fixed-point pass.
- The per-sample path avoids libm except `floorf`; `powf`, `expf` and `tanf`
  run once per 32-sample block, apart from `tanf` in the auto-filter mode,
  which is per-sample and the first thing to table-ise on the device.
