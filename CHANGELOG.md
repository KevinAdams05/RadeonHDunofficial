>[!NOTE]
>An LLM was used to aid in development of this code.

# Changelog

All notable changes to the RadeonHD (unofficial) fork are recorded here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the project follows [Semantic Versioning](https://semver.org/).

For the full deep-dive on each change — root cause, register details,
experiments, and Linux references — see
[`docs/technical-documentation.md`](docs/technical-documentation.md).

---

## [0.6.2] — 2026-05-27

### Added

- **Barts pixel-clock cap of 340 MHz** in `mode.cpp`'s per-chip
  linear-scanout cap framework, matching the existing Caicos
  (0.4.0, 165 MHz) and Turks (0.5.0, 250 MHz) caps. Triggered by the
  HD 6850 (Barts PRO) 0.6.1 bring-up: the wider 256-bit memory bus
  raises the bandwidth ceiling but doesn't eliminate it — 4K@60Hz
  over DisplayPort (533 MHz pixel clock) produces the same
  stride-aliased scanout corruption seen on the narrower-bus
  Caicos and Turks chips. The 340 MHz cap matches the card's native
  HDMI 1.4a single-link TMDS ceiling, allows 4K@30Hz / 1440p@60Hz /
  1080p@144Hz / 3440×1440@60Hz, and rejects 4K@60Hz cleanly before
  the corruption occurs. Cayman remains uncapped pending hardware
  testing.

### Fixed

- **Phase 4 narrative correction in
  `docs/technical-documentation.md`.** Prior text framed Linux's
  fix for high-pixel-clock linear scanout as "Linux uses 2D-tiled
  scanout" — that's only true for userspace GEM clients. Linux's
  fbdev path uses linear scanout same as Haiku
  (`drivers/gpu/drm/radeon/radeon_fbdev.c:63` hardcodes
  `fb_tiled = false`). The actual load-bearing fix on Linux is
  **display-watermark / line-buffer programming** in
  `evergreen_bandwidth_update` /  `dce6_bandwidth_update` /
  `dce8_bandwidth_update`, which lives entirely inside the radeon
  driver and is therefore in-scope for this driver-only fork. A
  proper port (with corrected register-family targeting per the
  Phase 4 register-comparison table) is the path to lifting the
  per-chip caps. New 0.6.2 doc section added covering this.

---

## [0.6.1] — 2026-05-27

### Added

- **Radeon HD 6850 (Barts PRO, `0x6739`) re-enabled** in the supported
  device table (`driver.cpp:280-281`). The entry had been wrapped in
  `#if 0` carrying a "Not working: #8765" comment from a 14-year-old
  Haiku Trac ticket against hrev44378 (black on DVI, vertical stripes
  on VGA-via-DVI-converter). Driver code has evolved enormously
  since; modern code initializes the card cleanly. Verified on a
  Barts PRO card at 1080p@60Hz on DVI-I, cold-boot. HDMI, DP, and
  the second DVI-I have not yet been tested. See README tested-hardware
  table for the full row.

---

## [0.6.0] — 2026-05-12

### Added

- **HDMI AVI infoframe groundwork** (`hdmi.cpp`, `hdmi.h`, register
  defines in `evergreen_reg.h`). Builds a CTA-861 AVI infoframe,
  packs into `AFMT_AVI_INFO0..3`, programs `HDMI_KEEPOUT_MODE`, and
  clears unused packet generators (VBI / ACR / GENERIC / AUDIO).
  **Currently disabled** at the encoder-mode level — verified to fire
  correctly on Cedar but does not suppress the magenta-stripe
  data-island bleed by itself. Kept on disk as the foundation for a
  future fix once the missing Cedar-specific register or sequence is
  identified. The 0.2.0 DVI-fallback workaround remains in effect.

### Changed

- **`displayport.cpp`** — replaced two speculative `TODO: This isn't
  correct for big endian systems!` comments with clarifying notes.
  AtomBIOS `DP_TRANSACTION` operates on byte-oriented buffers, so no
  host-to-LE swap is needed; matches Linux radeon's behavior. No
  runtime change.
- **`pll.cpp`** — improved the unsupported `SetPixelClock` table
  version log message. Was a bare `TODO`; now spells out which
  versions are supported (1.1, 1.2, 1.3, 1.5, 1.6, 1.7) and explains
  that 1.4 is intentionally absent (AMD skipped that table version).

---

## [0.5.0] — 2026-05-12

### Added

- **Turks PRO pixel-clock cap at 250 MHz** for `RADEON_TURKS`
  (HD 6570 / HD 6570 / HD 7570 / HD 8550 / R5 230 OEM variants).
  Empirically validated against an HD 6570/7570 OEM card: 1680×1680 at
  240 MHz scans clean; 4K@60Hz at 533 MHz produces the same
  stride-aliased corruption seen on Caicos. Same architectural cause
  (linear scanout exceeds memory bandwidth on the 128-bit bus), same
  fix shape — driver-only cap.
- **Square-mode filter** in `is_mode_supported()`. Rejects modes
  where `virtual_width == virtual_height` and width > 1024. These
  1:1 modes (1280×1280 / 1440×1440 / 1680×1680) come out of Haiku's
  shared `create_display_modes()` helper when an EDID Standard Timing
  Identifier's aspect-ratio bits decode to "unknown" and the parser
  falls through to width-equals-height. No real consumer monitor
  advertises those timings; filtering them at the driver level
  prevents a 16:9 monitor from defaulting to a square mode after the
  per-chip cap. Generic — applies to every chip, not just NI.

### Changed

- **Cap framework refactored** to a generic per-chip lookup table.
  Adding more chips later (Barts, Cayman, IGPs) is one `else if` clause
  instead of a chip-specific code path. Caicos behavior (165 MHz cap,
  0.4.0) is unchanged.

---

## [0.4.0] — 2026-05-05

First standalone `.hpkg` release.

### Added

- **EDID range parser hardening** in `detect_crt_ranges()`. Now
  rejects MONITOR_RANGES descriptors with zero, equal, or inverted
  bounds — those `foundRanges = true` paths were causing
  `is_mode_supported()` to reject every candidate mode and forcing
  the desktop to fall back to VESA. Surfaced in the wild on an HD 7470
  whose monitor emits a degenerate `h = 160-160` kHz range.
  Underflow-safe lower-bound comparison (`freq + 1 < min` instead of
  `freq < min - 1`) so a zero EDID minimum can't make every mode appear
  out-of-range either.
- **Forced linear-aligned `ARRAY_MODE` in `GRPH_CONTROL`** for all
  Evergreen+ scanout. Some VBIOSes leave a tiled mode set in
  `ARRAY_MODE` after POST; previously the driver wrote only the depth
  and format bits, leaving the tile bits to be interpreted as
  hardware-tiled when scanning out a linear app_server buffer. The
  result was a flat-color screen with garbled scanlines on the first
  few rows. Now writes `ARRAY_MODE_LINEAR_ALIGNED` explicitly.
- **Caicos pixel-clock cap at 165 MHz** for `RADEON_CAICOS`
  (HD 6450 / HD 7470 / HD 8470 / R5 230/235/310 OEM variants).
  Investigated as a candidate for a memory-controller display-priority
  / watermark fix to make 4K@60Hz work; experimentation on real hardware
  narrowed the cause to the linear-vs-tiled scanout architecture. Linux
  uses 2D-tiled GPU-managed framebuffers; Haiku writes a linear
  PCI-BAR-mapped surface, which Caicos's 64-bit memory bus cannot
  sustain at 4K@60Hz. Tiled scanout would require changes outside the
  driver tree (app_server framebuffer allocation), which is out of
  scope for this standalone-`.hpkg` fork. Caps at 1080p@75Hz instead.
- **Per-chip cap re-validated inside `radeon_set_display_mode`** so
  that saved app_server screen preferences (from a prior driver
  version that allowed the now-capped mode) can't bypass the cap.

---

## [0.3.0] — 2026-04-30

### Added

- **APU VRAM detection** for Aruba (Trinity / Richland), Kaveri,
  Kabini, Mullins, Carrizo, Stoney, Raven. The Tahiti+ branch was
  reading a discrete-GPU register that returns 0 on UMA APUs, which
  crashed app_server. Now branches on `CHIP_APU` flag and reads the
  byte-denominated UMA register.
- **CHIP_APU flag corrections** for 58 PCI table entries. Kaveri,
  Kabini, and Mullins were tagged `CHIP_STD` instead of `CHIP_APU`,
  which prevented the APU VRAM-detection path from triggering.
- **PLL clock routing improvements**: DCE 6.1 guard in `pll_pick()`,
  Polaris routing to `pll_set_dce()`, and a `SetPixelClock` v1.7
  fallback path so Polaris-era cards stop hitting the unsupported-table
  branch.
- **DPMS / eDP power sequencing** — three previously-stubbed TODOs
  implemented: eDP panel power on, DP receiver D3 sleep on DPMS-off,
  eDP panel power off, plus the Travis bridge quirk for laptop panels.
- **DisplayPort link training improvements**: HBR2 (5.4 Gbps) enabled,
  link-training return values now checked, retry-with-rate-fallback
  added so a failed HBR2 attempt drops to HBR1 instead of producing
  a blank screen.

### Changed

- **Spread spectrum V2 / V3 constants** corrected. V2 PLL path was
  using V3-typed constants for PPLL2 and DCPLL — wrong values on
  DCE 4.x (Evergreen) hardware.

---

## [0.2.0] — 2026-04-25

### Added

- **HDMI guard-band magenta-stripe workaround**. `ATOM_ENCODER_MODE_HDMI`
  causes the encoder to emit data-island guard bands during HBLANK on
  Cedar/Evergreen even when the HDMI infoframe / packet-control
  registers aren't programmed. Receivers decode those guard bands as
  visible pixels — observed as a magenta stripe along the left edge
  of the active region. Linux only returns `ATOM_ENCODER_MODE_HDMI`
  when audio is enabled; this fork mirrors that conservative default
  by driving HDMI connectors as DVI until full HDMI infoframe + audio
  support lands. HDMI is electrically backward-compatible with DVI,
  so video works correctly without the data island.

---

## [0.1.0] — 2026-04-14

Initial fork of the in-tree `radeon_hd` driver on the `KevinMain`
branch (`KevinAdams05/haiku`). Not released as a `.hpkg` — that
shipped starting with 0.4.0.

### Added

- **Pixel clock validation per connector type** in
  `is_mode_supported()`. HDMI single-link TMDS limit (165 MHz on
  pre-DCE6, 340 MHz on DCE6+) and single-link DVI limit (165 MHz)
  enforced before reaching the PLL programming path, so a 533 MHz
  mode (4K@60Hz) on a Cedar GPU over HDMI is rejected up front
  instead of producing garbled output.
- **Evergreen-specific memory-controller halt/resume** functions
  (`evergreen_gpu_mc_halt` / `_resume`) that use the correct Evergreen
  register addresses (CRTC base at `0x6e70`) instead of the AVIVO-era
  `0x6080` the driver was using on all generations. Handles all six
  Evergreen CRTCs, locks both `GRPH_UPDATE_LOCK` and
  `MASTER_UPDATE_LOCK`, and writes 64-bit surface addresses
  (high word first).
- **HDMI vs DVI encoder mode** distinction in
  `display_get_encoder_mode()` — `VIDEO_CONNECTOR_HDMIA` used to fall
  through to the DVI default. Now correctly distinguished (with a
  conservative DVI fallback added in 0.2.0 until HDMI infoframes
  fully work — see 0.6.0).

### Fixed

- **PCI ID `0x68fa` misidentification.** Was listed as `RADEON_CAICOS`
  (Northern Islands) when it's actually a Cedar (Evergreen) chip.
  Driver was loading the wrong chipset profile.
- **Seven missing Cedar PCI IDs** added to the device table: `0x68e5`,
  `0x68e8`, `0x68e9`, `0x68f1`, `0x68f2`, `0x68f8`, `0x68fe`. Cards
  with these IDs were not being claimed by the driver at all.

---

[0.6.0]: https://github.com/KevinAdams05/RadeonHDunofficial/releases/tag/v0.6.0
[0.5.0]: https://github.com/KevinAdams05/RadeonHDunofficial/releases/tag/v0.5.0
[0.4.0]: https://github.com/KevinAdams05/RadeonHDunofficial/releases/tag/v0.4.0
[0.3.0]: https://github.com/KevinAdams05/RadeonHDunofficial/releases/tag/v0.3.0
[0.2.0]: https://github.com/KevinAdams05/RadeonHDunofficial/releases/tag/v0.2.0
[0.1.0]: https://github.com/KevinAdams05/RadeonHDunofficial/releases/tag/v0.1.0
