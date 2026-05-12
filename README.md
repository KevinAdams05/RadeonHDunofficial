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

Release history and what changed in each version: see [`CHANGELOG.md`](CHANGELOG.md).

---

## Tested Hardware

Tested on Haiku hrev59697.

**Legend:** ✅ Pass &middot; ❌ Fail &middot; 🟡 In Progress &middot; ⬜ Not Yet Tested &middot; ➖ Output not present on this card

| Brand | Model | PCI ID | Family | Codename | VGA | DVI | HDMI | DP |
|-------|-------|--------|--------|----------|:---:|:---:|:----:|:--:|
| PowerColor AX5450 | Radeon HD 5450 | `0x68f9` | Evergreen | Cedar | ⬜ | ➖ | ✅ | ⬜ |
| AMD OEM | Radeon HD 7470 / 8470 (OEM rebrand) | `0x6778` | Northern Islands | Caicos XT | ➖ | ✅ | ➖ | ✅ |
| AMD OEM | Radeon HD 6570 / 7570 / 8550 / R5 230 (OEM rebrand) | `0x6759` | Northern Islands | Turks PRO | ⬜ | ✅ | ⬜ | ✅ |

### Notes

- **AX5450 (Cedar)** — 1080p@60Hz on HDMI verified. HDMI connector is
  driven as DVI per the 0.2.0 fallback until the magenta-stripe
  data-island bleed is fully solved.
- **HD 7470 / 8470 (Caicos XT)** — 1080p@60Hz on DisplayPort and
  DVI-D Single Link, cold-boot verified. Higher modes including
  4K@60Hz are capped at 165 MHz pixel clock due to memory-bandwidth
  limits of the linear-scanout path on this 64-bit-bus chip (see
  0.4.0).
- **HD 6570 / 7570 / 8550 / R5 230 (Turks PRO)** — 1080p@60Hz on DVI-I
  and DisplayPort verified. The 128-bit memory bus tolerates higher
  pixel clocks than Caicos's 64-bit (1680×1680 @ 240 MHz scans clean)
  but still not 4K@60Hz. Capped at 250 MHz (see 0.5.0).

---


## Documentation

- [`CHANGELOG.md`](CHANGELOG.md) — per-version summary of what changed,
  in Keep-a-Changelog format.
- [`docs/fixes-by-version.md`](docs/fixes-by-version.md) — prose
  summary of each shipped version's fixes (what shipped, why it matters,
  affected chips).
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
## Installation

### Standalone Package

Grab `radeon_hd_unofficial-<version>-x86_64.hpkg` from the
[releases page](https://github.com/KevinAdams05/RadeonHDunofficial/releases),
drop it in `~/config/packages/`, and reboot:

```sh
cp radeon_hd_unofficial-*.hpkg ~/config/packages/
shutdown -r
```

To revert, remove the .hpkg and reboot:

```sh
rm ~/config/packages/radeon_hd_unofficial-*.hpkg
shutdown -r
```

## Building from source

Building from the source is not required, you can install the .hpkg above. However if you want to
cut your own build (or contribute a fix), see
[`docs/building-and-packaging.md`](docs/building-and-packaging.md) for
the full overlay → jam → package workflow on a Linux build host.


---
## Fixes by Version

One-line summary of each shipped release. For the prose summary of each
version see [`docs/fixes-by-version.md`](docs/fixes-by-version.md); for
the full technical write-up (root cause, register details, experiments,
Linux references) see
[`docs/technical-documentation.md`](docs/technical-documentation.md);
for the changelog-format release notes see
[`CHANGELOG.md`](CHANGELOG.md).

| Version | Date | Summary |
|---------|------|---------|
| [0.1.0](docs/fixes-by-version.md#010--cedar--evergreen-hdmi-corruption) | 2026-04-14 | Initial fork — Cedar / Evergreen HDMI corruption fix (pixel-clock validation, Evergreen MC halt/resume, HDMI encoder mode, PCI ID corrections) |
| [0.2.0](docs/fixes-by-version.md#020--hdmi-guard-band-magenta-stripe-workaround) | 2026-04-25 | HDMI guard-band magenta-stripe workaround — drive HDMI as DVI on DCE4+ until full infoframe + audio path lands |
| [0.3.0](docs/fixes-by-version.md#030--apu-pll-dpms-and-displayport-fixes) | 2026-04-30 | APU VRAM detection, CHIP_APU flag corrections, spread-spectrum V2/V3, PLL clock routing, DPMS / eDP power sequencing, DisplayPort link training (HBR2 + retry) |
| [0.4.0](docs/fixes-by-version.md#040--edid-parser-hardening-forced-linear-scanout-caicos-cap) | 2026-05-05 | First standalone `.hpkg` — EDID range parser hardening, forced linear-aligned scanout, Caicos pixel-clock cap at 165 MHz |
| [0.5.0](docs/fixes-by-version.md#050--turks-cap-and-square-mode-filter) | 2026-05-12 | Turks pixel-clock cap at 250 MHz, square-mode filter, per-chip cap framework refactor |
| [0.6.0](docs/fixes-by-version.md#060--dppll-polish--hdmi-infoframe-groundwork) | 2026-05-12 | DP/PLL polish (no runtime change) + HDMI AVI infoframe groundwork (dormant — 0.2.0 DVI fallback still in effect) |

---


### ABI lockstep — kernel driver and accelerant must match

The kernel driver (`radeon_hd`) and the accelerant
(`radeon_hd.accelerant`) share an in-memory `accelerant_info` struct via
`clone_area`. This fork extends that struct (with `evergreen_gpu_state`
for the Evergreen MC halt/resume path), so its layout differs from the
stock Haiku build.

Always install both binaries together, both from this fork's `.hpkg`.
Never mix the fork's accelerant with the stock kernel driver (or vice
versa) — the two sides would disagree on the struct layout and the
system will crash on first display setup. 

**The `.hpkg` ships both
binaries atomically**, so this is automatic as long as you install via
the package and don't hand-copy individual files.

---

## Files Touched

### Kernel driver — `src/add-ons/kernel/drivers/graphics/radeon_hd/`
- `driver.cpp` — PCI ID corrections (Cedar), CHIP_APU flag corrections (58 entries)
- `radeon_hd.cpp` — APU VRAM detection within Tahiti+ path

### Accelerant — `src/add-ons/accelerants/radeon_hd/`
- `display.cpp` — HDMI encoder mode, spread spectrum V2/V3 constants, forced linear `ARRAY_MODE` for Evergreen+ scanout (0.4.0), bogus EDID range descriptor rejection (0.4.0)
- `displayport.cpp` — HBR2 enabled, link training retry with rate fallback; speculative big-endian TODO comments clarified (0.6.0)
- `encoder.cpp` — eDP power on/off, DP receiver D3 sleep, Travis bridge quirk
- `gpu.cpp`, `gpu.h` — Evergreen-specific MC halt/resume
- `hdmi.cpp`, `hdmi.h` — AVI infoframe builder + AFMT register packing (0.6.0, currently dormant — call site disabled pending magenta-stripe investigation)
- `mode.cpp` — pixel clock validation per connector; underflow-safe EDID range comparison + diagnostic TRACE (0.4.0); per-chip pixel-clock cap framework — Caicos at 165 MHz (0.4.0), Turks at 250 MHz (0.5.0); square-mode filter (0.5.0)
- `pll.cpp` — DCE 6.1 guard, Polaris routing, SetPixelClock v1.7 fallback; unsupported-table-version log clarified (0.6.0)

### Headers — `headers/private/graphics/radeon_hd/`
- `evergreen_reg.h` — new register defines for Evergreen MC sequencing; `EVERGREEN_GRPH_ARRAY_MODE` macro and `LINEAR_GENERAL` / `LINEAR_ALIGNED` / `1D_TILED_THIN1` / `2D_TILED_THIN1` value constants (0.4.0); `DC_LB_MEMORY_SPLIT`, `PRIORITY_A/B_CNT`, `PIPE0_*`, `MC_SHARED_CHMAP` register defines kept as documentation for future per-chip cap logic (0.4.0); AFMT_AVI_INFO0..3 and HDMI_* register defines for the AVI infoframe path (0.6.0)
- `ni_reg.h` — DCE 5+ `DPG_PIPE_*` register defines (0.4.0)
- `accelerant.h` — `evergreen_gpu_state` struct

---


## Relationship to Upstream Haiku

This fork tracks Haiku `master` and applies a focused patch set on
top. It is intended for users with affected hardware who want the fixes
today; there is no active plan to upstream the changes.

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
