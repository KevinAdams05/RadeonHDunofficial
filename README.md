>[!NOTE]
>An LLM was used to aid in development of this code.

**Bug reports (please attach listdev output, syslog and/or screenshots) and PRs welcome! See "Logging Bugs / How to Help" section below**

# RadeonHD (Unofficial) - Haiku Driver

This is an enhanced fork of the Haiku `radeon_hd` graphics driver with bug fixes for Evergreen, Northern Islands, Southern Islands, Sea Islands, Volcanic Islands, Vega, and Polaris GPUs.

>[!NOTE]
> This is **not** an official Haiku project. 
> It is a fork of the in-tree 
> `radeon_hd` driver, distributed as a standalone `.hpkg` for users who want the fixes. 
> There is no active plan to upstream these changes.

---

## Tested Hardware

This driver was physically tested on the following cards and outputs:


| Brand | Model | PCI ID | Family | Codename | Outputs Tested | Status |
|-------|-------|--------|--------|----------|----------------|--------|
| PowerColor AX5450 | Radeon HD 5450 | `0x68f9` | Evergreen | Cedar | HDMI | Working (HDMI verified; VGA / DisplayPort not yet tested) |
| AMD OEM | Radeon HD 7470 / 8470 (OEM rebrand) | `0x6778` | Northern Islands | Caicos XT | DisplayPort, DVI-D | Working at 1920&times;1080@60Hz on both outputs (DisplayPort verified, DVI-D Single Link verified, cold-boot verified). Higher modes (incl. 4K) capped on this chip due to memory-bandwidth limits of the linear-scanout path &mdash; see Phase 4. |


---


## Documentation

- [`docs/technical-documentation.md`](docs/technical-documentation.md) — full
  technical write-up of every fix, including root cause, approach, register
  details, and architectural context.
- [`diagrams/`](diagrams/) — SVG diagrams referenced from the technical docs
  (architecture, MC halt/resume flow, pixel clock validation, VRAM detection,
  spread spectrum bug, PLL routing, DPMS sequence, DP link training).

---

## Logging Bugs / How to Help

Bugs are welcome! To log a bug, [please log it here in github as an issue](https://github.com/KevinAdams05/RadeonHDunofficial/issues), and include as much detail as possible.

From Haiku, attach your syslog file and output of listdev (that will show us the PCI ID which identifes the exact card). Please list what output connector and cable type you used. If you get weird artifacts or hit KDL then please include a picture of the screen.

If you can boot from a Linux USB stick, please include the output of these commands:

- lspci -nn | grep -i radeon
- ls -la /sys/bus/pci/devices/$(lspci -D | grep -i radeon | head -1 | awk '{print $1}')/resource*
- [`scripts/dumpregs.py`](scripts/dumpregs.py) script (should save output to a file on your desktop)

PRs are welcome! However, please test all code changes on physical hardware before opening the PR! On your PR indicate which card or CPU you tested on, and include the PCI ID.


---
### Phase 1 — Cedar / Evergreen HDMI corruption

Fixes a garbled-display regression on Cedar-class cards (HD 5400 / 6300 / 7300
series) connected over HDMI:

- **Pixel clock validation per connector type** — refuses modes the connector
  cannot physically carry (HDMI single-link TMDS = 165 MHz on pre-DCE6,
  340 MHz on DCE6+).
- **Evergreen-specific memory controller halt/resume** — stock driver was using
  AVIVO-era (R500/R600) MC registers on Evergreen hardware, writing to the
  wrong addresses during GPU reset.
- **HDMI vs. DVI encoder mode** — `VIDEO_CONNECTOR_HDMIA` was falling through
  to the DVI default. Now correctly distinguished (with a conservative DVI
  fallback until HDMI infoframes are wired up — see Phase 1.5).
- **PCI ID corrections** — `0x68fa` was misidentified as CAICOS (Northern
  Islands) when it is actually Cedar (Evergreen). Seven additional Cedar PCI
  IDs were missing from the device table entirely.

### Phase 1.5 — HDMI guard-band magenta stripe

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
the data island.

### Phase 2 — APU, PLL, DPMS, and DisplayPort fixes

Six independent fixes targeting bugs across several GPU generations:

| # | Fix | Affects | Resolves |
|---|-----|---------|----------|
| 1 | **APU VRAM detection** — Aruba and later APUs were reading the discrete-VRAM register, getting 0 MB, and crashing app_server | Aruba, Kaveri, Kabini, Mullins, Carrizo, Stoney, Raven | #17664, #18470, #10939 |
| 2 | **Chip flag corrections** — Kaveri/Kabini/Mullins were mis-tagged `CHIP_STD` instead of `CHIP_APU` (58 PCI table entries) | Sea Islands APUs | Prerequisite for Fix 1 |
| 3 | **Spread spectrum constants** — V2 PLL path was using V3 type constants for PPLL2 and DCPLL | DCE 4.x (Evergreen) | #8154, #8339 (likely) |
| 4 | **PLL clock routing** — DCE 6.1 guard, Polaris routing to `pll_set_dce()`, and SetPixelClock v1.7 fallback | Aruba, Polaris (DCE 11.2) | #8485, Polaris display |
| 5 | **DPMS / eDP power sequencing** — three TODO stubs implemented (eDP power on, DP D3 sleep, eDP power off, plus the Travis bridge quirk) | All eDP and DP displays | #8485 |
| 6 | **DisplayPort link training** — HBR2 (5.4 Gbps) enabled, return values now checked, retry-with-rate-fallback added | DCE 5+ DP outputs | #8485, #18470 |

### Phase 3 — EDID range parser hardening and forced linear scanout

Two independent bugs that surfaced together while bringing up an HD 7470
(Caicos XT, Northern Islands DCE 5):

| # | Fix | Affects | Notes |
|---|-----|---------|-------|
| 1 | **EDID range parser** — broken/degenerate range descriptors (e.g. `min_h == max_h`) and unsigned-underflow in the lower-bound test caused `is_mode_supported()` to reject every candidate mode, falling back to VESA | Any card paired with a monitor whose EDID has zero, equal, or inverted range bounds | All Evergreen+ |
| 2 | **Forced linear-aligned scanout** — `display_crtc_fb_set` left the `ARRAY_MODE` field of `GRPH_CONTROL` at whatever VBIOS POST set, which is tiled on some cards. The chip then scans linear app_server pixels as if tiled, producing a flat-color screen with garbled fragments only on the first scanlines | Any Evergreen+ card whose VBIOS leaves a tiled array mode set after POST | All Evergreen+ |

### Phase 4 — Caicos high-bandwidth mode cap

Caps pixel clock at 165 MHz (1080p@75Hz) on the `RADEON_CAICOS` chip
(HD 6450 / HD 7470 / HD 8470 / R5 230/235/310 OEM). This was investigated
as a candidate for a memory-controller display-priority / watermark fix to
make 4K@60Hz work, but experimentation on real hardware narrowed the cause
to the linear-vs-tiled scanout architecture. Linux uses 2D-tiled GPU-managed
framebuffers; Haiku writes a linear PCI-BAR-mapped surface, which the
Caicos 64-bit memory bus cannot sustain at 4K@60Hz. Tiled scanout would
require changes outside the driver tree (app_server framebuffer
allocation), which is out of scope for this standalone-`.hpkg` fork.

The cap targets Caicos specifically because that's the only Northern
Islands chip empirically validated; Cayman / Turks / Barts have wider
memory buses and likely tolerate higher modes. They're left uncapped
pending hardware testing. See
[`docs/technical-documentation.md`](docs/technical-documentation.md#phase-4--caicos-high-bandwidth-mode-cap)
for the full investigation including register diffs against Linux at 4K.

See [`docs/technical-documentation.md`](docs/technical-documentation.md) for
the deep-dive on each fix.

---

## Installation

### Standalone `.hpkg`

Grab `radeon_hd_unofficial-<version>-x86_64.hpkg` from the
[releases page](https://github.com/KevinAdams05/RadeonHDunofficial/releases),
drop it in `~/config/packages/`, and reboot:

```sh
cp radeon_hd_unofficial-*.hpkg ~/config/packages/
shutdown -r
```

Haiku's packagefs union-mounts the package contents at:

```
~/config/add-ons/accelerants/radeon_hd.accelerant
~/config/add-ons/kernel/drivers/bin/radeon_hd
~/config/add-ons/kernel/drivers/dev/graphics/radeon_hd  → bin/
```

The kernel driver and accelerant loaders search user paths before system
paths, so the fork's binaries are picked first at runtime. The stock
`haiku` package's `radeon_hd` stays in place, untouched, at
`/system/add-ons/...` — it just never gets reached. **No conflict with
the haiku package, no `replaces` declaration, no destructive uninstall
needed to undo.**

To revert, remove the .hpkg and reboot:

```sh
rm ~/config/packages/radeon_hd_unofficial-*.hpkg
shutdown -r
```

> **ABI lockstep — kernel driver and accelerant must match**
>
> The kernel driver (`radeon_hd`) and the accelerant
> (`radeon_hd.accelerant`) share an in-memory `accelerant_info` struct via
> `clone_area`. This fork extends that struct (with `evergreen_gpu_state`
> for the Evergreen MC halt/resume path), so its layout differs from the
> stock Haiku build.
>
> **Always install both binaries together, both from this fork's `.hpkg`.**
> Never mix the fork's accelerant with the stock kernel driver (or vice
> versa) — the two sides would disagree on the struct layout and the
> system will crash on first display setup. The `.hpkg` ships both
> binaries atomically, so this is automatic as long as you install via
> the package and don't hand-copy individual files.

### Building from source

End users don't need to build — install the .hpkg above. If you want to
cut your own build (or contribute a fix), see
[`docs/building-and-packaging.md`](docs/building-and-packaging.md) for
the full overlay → jam → package workflow on a Linux build host.

For reference, the committed fixes live on the `KevinMain` branch of
[KevinAdams05/haiku](https://github.com/KevinAdams05/haiku):

```
0dbe85ae56  radeon_hd: Fix garbled display on Cedar/Evergreen HDMI
0d8df24306  radeon_hd: Fix APU VRAM detection, spread spectrum, PLL routing, DPMS, and DP link training
2f542cc5ed  radeon_hd: drive HDMI connectors as DVI until infoframes are implemented
2450f6b833  radeon_hd: harden EDID range parser and force linear scanout
182601b3c9  radeon_hd: cap Caicos pixel clock at 165 MHz for linear scanout
bf2709f8d4  radeon_hd: add Kevin Adams to copyright + authors on touched files
```

---

## Files Touched

### Kernel driver — `src/add-ons/kernel/drivers/graphics/radeon_hd/`
- `driver.cpp` — PCI ID corrections (Cedar), CHIP_APU flag corrections (58 entries)
- `radeon_hd.cpp` — APU VRAM detection within Tahiti+ path

### Accelerant — `src/add-ons/accelerants/radeon_hd/`
- `display.cpp` — HDMI encoder mode, spread spectrum V2/V3 constants, forced linear `ARRAY_MODE` for Evergreen+ scanout (Phase 3), bogus EDID range descriptor rejection (Phase 3)
- `displayport.cpp` — HBR2 enabled, link training retry with rate fallback
- `encoder.cpp` — eDP power on/off, DP receiver D3 sleep, Travis bridge quirk
- `gpu.cpp`, `gpu.h` — Evergreen-specific MC halt/resume
- `mode.cpp` — pixel clock validation per connector; underflow-safe EDID range comparison + diagnostic TRACE (Phase 3); Caicos 165 MHz pixel-clock cap (Phase 4)
- `pll.cpp` — DCE 6.1 guard, Polaris routing, SetPixelClock v1.7 fallback

### Headers — `headers/private/graphics/radeon_hd/`
- `evergreen_reg.h` — new register defines for Evergreen MC sequencing; `EVERGREEN_GRPH_ARRAY_MODE` macro and `LINEAR_GENERAL` / `LINEAR_ALIGNED` / `1D_TILED_THIN1` / `2D_TILED_THIN1` value constants (Phase 3); `DC_LB_MEMORY_SPLIT`, `PRIORITY_A/B_CNT`, `PIPE0_*`, `MC_SHARED_CHMAP` register defines kept as documentation for future per-chip cap logic (Phase 4)
- `ni_reg.h` — DCE 5+ `DPG_PIPE_*` register defines (Phase 4)
- `accelerant.h` — `evergreen_gpu_state` struct

---


## Relationship to Upstream Haiku

This fork tracks Haiku `master` and applies the three commits listed above on
top. It is a focused patch set for users with affected hardware who want the fixes today.

---

## Source Material
The Linux Radeon driver was used as a reference for register
semantics, init sequences, and PCI ID classifications, etc. No Linux code was
copied directly. The implementations are written from scratch against the
AtomBIOS interface and Haiku conventions.


AMD also has several datasheets available on their website. They are slightly helpful.

---

## License

Same as Haiku — MIT.
