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
| PowerColor AX5450 | Radeon HD 5450 | `0x68f9` | Evergreen | Cedar | ✅  | ✅ | ✅ | ➖ |
| AMD OEM | Radeon HD 7470 / 8470 (OEM rebrand) | `0x6778` | Northern Islands | Caicos XT | ➖ | ✅ | ➖ | ✅ |
| AMD OEM | Radeon HD 6570 / 7570 / 8550 / R5 230 (OEM rebrand) | `0x6759` | Northern Islands | Turks PRO | ➖ | ✅ | ➖ | ✅ |
| Sapphire | Radeon HD 6850 | `0x6739` | Northern Islands | Barts PRO | ➖ | ✅ | ✅ | ✅ |
| MSI | Radeon HD 7770 / 8760 / R7 250X | `0x683d` | Southern Islands | Cape Verde XT | ➖ | ✅ | ✅ | ✅ |
| MSI | Radeon R7 260X / 360 | `0x6658` | Sea Islands | Bonaire XTX | ➖ | ✅ | ✅ | ✅ |


### Notes
- **4K@60 over HDMI needs an HDMI 2.0 card — none of these qualify.**
  Full-RGB 4K@60 over HDMI requires a ~594 MHz TMDS clock (HDMI 2.0).
  Every card in the table above is HDMI 1.4a, whose transmitter tops out
  at **340 MHz** — a limit of the GPU silicon, not the cable (HDMI cables
  carry no "version"; any *High Speed* cable already exceeds 340 MHz). The
  only way to fit 4K@60 into HDMI 1.4 is YCbCr **4:2:0**, which halves the
  chroma data to ~266 MHz — an `amdgpu`/DC-era feature the old Linux
  `radeon` driver never implemented, and neither does this fork. So the
  driver caps HDMI at 340 MHz, exactly matching Linux `radeon`
  (`radeon_dvi_mode_valid` returns `MODE_CLOCK_HIGH` above 340 MHz on
  DCE6+ and has no 4:2:0 fallback). For full-RGB 4K@60 on these cards use
  **DisplayPort** (DP 1.2/HBR2 has the bandwidth); over HDMI, 4K@60 needs
  a newer HDMI-2.0 card.
- **4K DVI not tested** - my only 4K monitor does not have a DVI port, so I am unable to test 4K over DVI
- **AX5450 (Cedar)** — 1080p@60Hz, 1600×1200, and 1024×768 over HDMI
  verified on 0.6.3 in **real HDMI encoder mode** — the magenta-stripe
  data-island bleed is fixed (root cause: the AFMT packet generator
  was programmed on the wrong DIG instance; see the 0.6.3 section in
  [`docs/technical-documentation.md`](docs/technical-documentation.md)).
  The 0.2.0 DVI fallback is retired. DPMS off→on cycle verified —
  colors and infoframe state survive a monitor power-down (the AFMT
  block is preserved across the AtomBIOS DPMS sequence on Cedar).
  VGA and DVI outputs also verified on 0.6.3.
- **HD 7470 / 8470 (Caicos XT)** — 1080p@60Hz on DisplayPort and
  DVI-D Single Link, cold-boot verified. Higher modes including
  4K@60Hz are capped at 165 MHz pixel clock due to memory-bandwidth
  limits of the linear-scanout path on this 64-bit-bus chip (see
  0.4.0).
- **HD 6570 / 7570 / 8550 / R5 230 (Turks PRO)** — 1080p@60Hz on DVI-I
  and DisplayPort re-verified on 0.6.3 (DP link trains clean at
  270 MHz × 2 lanes). The 128-bit memory bus tolerates higher
  pixel clocks than Caicos's 64-bit (1680×1680 @ 240 MHz scans clean)
  but still not 4K@60Hz. Capped at 250 MHz (see 0.5.0). Known
  cosmetic issue: probing the *unconnected* second DP port logs
  harmless `dp_aux` timeout noise at boot (see `docs/TODO.md`).
- **HD 6850 (Barts PRO)** — 1080p@60Hz verified on DVI-I,
  HDMI A, DisplayPort, and the second DVI-I port. This card was
  previously gated out of the driver entirely via an `#if 0` block in
  `driver.cpp` carrying a stale "Not working: #8765" comment from
  Haiku Trac ticket [#8765][8765] (filed 14 years ago against
  hrev44378 — black on DVI, vertical stripes on VGA). The block was
  removed in 0.6.1. 4K@60Hz over DisplayPort (533 MHz pixel clock) produced scanout corruption. Added a pixel cap @ 340 MHz in 0.6.2 (matching the card's
  native HDMI 1.4a ceiling). Will try to fix that (or determine true highest resolution) in the next version.
  DVI, HDMI, and DP all re-verified on 0.6.3 — HDMI now in real HDMI
  encoder mode on this card's UNIPHY2-linkB→DIG5 topology (a different
  DIG than Cedar's, confirming the 0.6.3 DIG-routing fix is general);
  DP link trains clean at 270 MHz × 2 lanes.
- **R7 260X / 360 (Bonaire XTX, MSI board)** — the **first Sea Islands / GCN 2.0
  (CIK) part tested** in this fork; every card above is TeraScale
  (Evergreen / Northern Islands). The driver loads Bonaire registers
  (`init_registers … chipset Bonaire`) and drives the DCE CRTC path
  cleanly. A **pre-release** 0.6.4 build briefly showed 4K@60 over HDMI —
  the mode slipped past validation and AtomBIOS 4:2:0-halved the TMDS
  clock (533 → 266 MHz) to fit under the HDMI 1.4a ceiling — but the
  shipping driver caps HDMI at 340 MHz to match Linux `radeon` (see the
  *4K@60 over HDMI* note above), so 4K@60 on Bonaire is a **DisplayPort**
  mode, not HDMI. **DisplayPort now works too**
  (1920×1200@60 verified) after fixing two Sea-Islands-specific driver
  bugs: (1) the DCE8 HPD-id lookup used a dword register index where a
  byte offset was expected (`SEA_mmDC_GPIO_HPD_A`), so every DP AUX
  transaction was issued with `HPD_NONE` and failed with "flags not
  zero" — no EDID, framebuffer fallback; (2) the DP CRTC pixel clock was
  programmed from the AdjustDisplayPll *link* frequency instead of the
  real mode clock, scanning the panel out below its refresh range
  ("out of range"). **DVI-I** also verified (1920×1080@60 over single-link
  TMDS; uses a normal PPLL, unaffected by the DP clock fix).
  **Note:** this card needs a PCIe aux-power cable — with it unplugged
  the card does not POST and does not even enumerate on the PCI bus.

[8765]: https://dev.haiku-os.org/ticket/8765

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
| [0.6.1](https://github.com/KevinAdams05/RadeonHDunofficial/blob/main/docs/fixes-by-version.md#061--hd-6850-barts-re-enabled) | 2025-05-27 | Re-enabled Radeon HD 6850 (0x6739) |
| [0.6.2](https://github.com/KevinAdams05/RadeonHDunofficial/blob/main/docs/fixes-by-version.md#062--barts-pixel-clock-cap) | 2025-05-27 |  Radeon HD 6850: 4K@60hz is not working yet, added a pixel cap for now. |
| [0.6.3](docs/fixes-by-version.md#063--hdmi-magenta-stripe-root-cause-fixed-dcn-guard-kernel-hardening) | 2026-06-04 | HDMI magenta stripe fixed. DVI fallback retired, real HDMI mode on DCE4+. DCN GPUs refused gracefully with framebuffer fallback, PCI BAR-assignment guard, etc... |
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
