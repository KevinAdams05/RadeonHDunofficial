>[!NOTE]
>An LLM was used to aid in development of this code.

# RadeonHD (Unofficial) — Fixes by Version

Short summary of each shipped version. For the full technical write-up
of every fix — root cause, register details, experiments, Linux
references — see [`technical-documentation.md`](technical-documentation.md).
For the changelog-format release notes, see
[`../CHANGELOG.md`](../CHANGELOG.md).

---

## 0.1.0 — Cedar / Evergreen HDMI corruption

Initial fork. Fixes a garbled-display regression on Cedar-class cards
(HD 5400 / 6300 / 7300 series) connected over HDMI:

- **Pixel clock validation per connector type** — refuses modes the connector
  cannot physically carry (HDMI single-link TMDS = 165 MHz on pre-DCE6,
  340 MHz on DCE6+).
- **Evergreen-specific memory controller halt/resume** — stock driver was using
  AVIVO-era (R500/R600) MC registers on Evergreen hardware, writing to the
  wrong addresses during GPU reset.
- **HDMI vs. DVI encoder mode** — `VIDEO_CONNECTOR_HDMIA` was falling through
  to the DVI default. Now correctly distinguished (with a conservative DVI
  fallback added in 0.2.0 until HDMI infoframes fully work — see 0.6.0).
- **PCI ID corrections** — `0x68fa` was misidentified as CAICOS (Northern
  Islands) when it is actually Cedar (Evergreen). Seven additional Cedar PCI
  IDs were missing from the device table entirely.

## 0.2.0 — HDMI guard-band magenta stripe workaround

`ATOM_ENCODER_MODE_HDMI` causes the encoder to emit data-island guard bands
during HBLANK on Cedar/Evergreen even when the HDMI infoframe and audio packet
registers (`HDMI_INFOFRAME_CONTROL0`, `HDMI_GENERIC_PACKET_CONTROL`,
`HDMI_VBI_PACKET_CONTROL`, `AFMT_AUDIO_PACKET_CONTROL`) are not yet programmed.
Receivers decode those guard bands as visible pixels, producing a magenta
stripe along the left edge of the active region.

Linux only returns `ATOM_ENCODER_MODE_HDMI` when audio is actually enabled.
This fork mirrors that conservative default — HDMI connectors are driven as
DVI until full HDMI infoframe + audio support lands for DCE4+. HDMI is
electrically backward-compatible with DVI, so video works correctly without
the data island. The 0.6.0 release lays the AVI-infoframe groundwork but
does not yet retire this workaround.

## 0.3.0 — APU, PLL, DPMS, and DisplayPort fixes

Six independent fixes targeting bugs across several GPU generations:

| # | Fix | Affects | Resolves |
|---|-----|---------|----------|
| 1 | **APU VRAM detection** — Aruba and later APUs were reading the discrete-VRAM register, getting 0 MB, and crashing app_server | Aruba, Kaveri, Kabini, Mullins, Carrizo, Stoney, Raven | #17664, #18470, #10939 |
| 2 | **Chip flag corrections** — Kaveri/Kabini/Mullins were mis-tagged `CHIP_STD` instead of `CHIP_APU` (58 PCI table entries) | Sea Islands APUs | Prerequisite for Fix 1 |
| 3 | **Spread spectrum constants** — V2 PLL path was using V3 type constants for PPLL2 and DCPLL | DCE 4.x (Evergreen) | #8154, #8339 (likely) |
| 4 | **PLL clock routing** — DCE 6.1 guard, Polaris routing to `pll_set_dce()`, and SetPixelClock v1.7 fallback | Aruba, Polaris (DCE 11.2) | #8485, Polaris display |
| 5 | **DPMS / eDP power sequencing** — three TODO stubs implemented (eDP power on, DP D3 sleep, eDP power off, plus the Travis bridge quirk) | All eDP and DP displays | #8485 |
| 6 | **DisplayPort link training** — HBR2 (5.4 Gbps) enabled, return values now checked, retry-with-rate-fallback added | DCE 5+ DP outputs | #8485, #18470 |

## 0.4.0 — EDID parser hardening, forced linear scanout, Caicos cap

First standalone `.hpkg` release. Bundles three fixes that surfaced
together while bringing up an HD 7470 (Caicos XT, Northern Islands DCE 5):

| # | Fix | Affects | Notes |
|---|-----|---------|-------|
| 1 | **EDID range parser** — broken/degenerate range descriptors (e.g. `min_h == max_h`) and unsigned-underflow in the lower-bound test caused `is_mode_supported()` to reject every candidate mode, falling back to VESA | Any card paired with a monitor whose EDID has zero, equal, or inverted range bounds | All Evergreen+ |
| 2 | **Forced linear-aligned scanout** — `display_crtc_fb_set` left the `ARRAY_MODE` field of `GRPH_CONTROL` at whatever VBIOS POST set, which is tiled on some cards. The chip then scans linear app_server pixels as if tiled, producing a flat-color screen with garbled fragments only on the first scanlines | Any Evergreen+ card whose VBIOS leaves a tiled array mode set after POST | All Evergreen+ |
| 3 | **Caicos pixel-clock cap at 165 MHz** — investigated as a candidate for a memory-controller display-priority / watermark fix to make 4K@60Hz work, but experimentation on real hardware narrowed the cause to the linear-vs-tiled scanout architecture. Linux uses 2D-tiled GPU-managed framebuffers; Haiku writes a linear PCI-BAR-mapped surface, which the Caicos 64-bit memory bus cannot sustain at 4K@60Hz | `RADEON_CAICOS` (HD 6450 / HD 7470 / HD 8470 / R5 230/235/310 OEM) | Caps at 1080p@75Hz |

Tiled scanout would require changes outside the driver tree (app_server
framebuffer allocation), which is out of scope for this
standalone-`.hpkg` fork.

## 0.5.0 — Turks cap and square-mode filter

Generalizes the per-chip cap framework and adds a second chip:

- **Turks PRO pixel-clock cap at 250 MHz** for `RADEON_TURKS`
  (HD 6570 / HD 7570 / HD 8550 / R5 230 OEM variants). Validated against an
  HD 6570/7570 OEM card: 1680×1680 at 240 MHz scans clean; 4K@60Hz at
  533 MHz produces the same stride-aliased corruption seen on Caicos.
  Same architectural cause (linear scanout exceeds memory bandwidth on
  the 128-bit bus), same fix shape — driver-only cap.
- **Square-mode filter** in `is_mode_supported()`. Rejects modes where
  `virtual_width == virtual_height` and width > 1024. These 1:1 modes
  (1280×1280, 1440×1440, 1680×1680) come from Haiku's shared
  `create_display_modes()` helper when an EDID Standard Timing Identifier's
  aspect-ratio bits decode to "unknown" and the parser falls through to
  width-equals-height. No real consumer monitor advertises those timings.
- **Cap framework refactored** to a generic per-chip lookup table — adding
  more chips later (Barts, Cayman, IGPs) is one `else if` clause instead
  of a chip-specific code path.

## 0.6.0 — DP/PLL polish + HDMI infoframe groundwork

- **HDMI AVI infoframe groundwork** (`hdmi.cpp`, `hdmi.h`, register
  defines in `evergreen_reg.h`). Builds a CTA-861 AVI infoframe, packs
  into `AFMT_AVI_INFO0..3`, programs `HDMI_KEEPOUT_MODE`, and clears
  unused packet generators (VBI / ACR / GENERIC / AUDIO). **Currently
  disabled** at the encoder-mode level — verified to fire correctly on
  Cedar but does not suppress the magenta-stripe data-island bleed by
  itself. Kept on disk as the foundation for a future fix once the
  missing Cedar-specific register or sequence is identified. The 0.2.0
  DVI-fallback workaround remains in effect.
- **`displayport.cpp`** — replaced two speculative
  `TODO: This isn't correct for big endian systems!` comments with
  clarifying notes. AtomBIOS `DP_TRANSACTION` operates on byte-oriented
  buffers, so no host-to-LE swap is needed; matches Linux radeon's
  behavior. No runtime change.
- **`pll.cpp`** — improved the unsupported `SetPixelClock` table version
  log message. Was a bare `TODO`; now spells out which versions are
  supported (1.1, 1.2, 1.3, 1.5, 1.6, 1.7) and explains that 1.4 is
  intentionally absent (AMD skipped that table version).
