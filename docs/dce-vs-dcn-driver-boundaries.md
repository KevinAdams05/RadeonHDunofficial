>[!NOTE]
>An LLM was used to aid in development of this code.

# DCE vs DCN — and what marks the boundaries between Haiku's AMD graphics drivers

Companion reference for [`technical-documentation.md`](technical-documentation.md).
This document explains the architectural distinctions between AMD's
display engine generations and how they map to driver-family boundaries
in Haiku.

## DCE vs DCN — what they are

Both are AMD's **Display Controller Engine** — the hardware block on
the GPU die that drives display output (CRTCs, encoders, transcoders,
display PLLs, scanout DMA). They sit between the framebuffer in VRAM
and the physical connector pins.

| | DCE (Display Controller Engine) | DCN (Display Core Next) |
|---|---|---|
| **Era** | 2007–2017 | 2017–present |
| **First chip** | R600 (HD 2000-series) | Raven Ridge / Vega APU |
| **Last chip** | Polaris (RX 4xx/5xx, 2016–17) | Ongoing — Navi, Vega 20+, RDNA, all current |
| **Versions** | DCE 1.0 → DCE 12.0 | DCN 1.0 → DCN 3.x |
| **Programming model** | AtomBIOS command tables (`SetPixelClock`, `SetCRTC_Timing`, `EnableScaler`, etc.) — driver issues high-level commands, AtomBIOS firmware translates to register writes | Direct register I/O via DRM atomic state machine — driver writes registers directly, no AtomBIOS abstraction |
| **PHY** | Per-output PHY blocks (DP, HDMI, LVDS as separate hardware) | Unified DIO PHY shared across outputs |
| **Pipe count** | Usually 2–6 CRTCs | 4–6 pipes, more flexible plane composition |
| **Linux driver** | `radeon` (DCE 1–6) and `amdgpu` legacy path (DCE 8–12) | `amdgpu` with `amd/display/dc` subsystem (DAL) |

The **architectural wall** between them is: DCE programs through
AtomBIOS (which acts as a hardware abstraction layer baked into the
GPU's BIOS ROM); DCN bypasses AtomBIOS and writes registers directly.
Drivers built around AtomBIOS calls (Haiku's `radeon_hd`, Linux's old
`radeon`) cannot drive DCN at all without a fundamentally different
display backend.

## The driver boundaries (technically)

There are three architectural cliffs in AMD GPU history. Each one
corresponds to a driver-family boundary in Haiku.

### Cliff 1 — R500 → R600 (2007)

**Boundary:** introduction of unified shaders + AtomBIOS-driven DCE.

Before R600, GPUs had separate vertex/pixel shader hardware and no
formal "DCE" — display was programmed through chip-specific MMIO
(RS690, R520, etc.). Each chip had its own register layout.

R600 introduced:

- Unified shader array
- AtomBIOS as the canonical command-table interface
- DCE 1.0 — the first formal Display Controller Engine generation
- Standardized PCI ID classification by chip family

This is **the boundary between Haiku's `radeon` and `radeon_hd`**.

| Side | Coverage | Why |
|---|---|---|
| `radeon` | R100 / R200 / R300 / R400 / R500 (HD 0 / Radeon 7000–X1xxx era, ~2000–2007) | Pre-AtomBIOS or early-AtomBIOS, chip-specific register layouts |
| `radeon_hd` | R600 onward | AtomBIOS-driven, DCE-versioned, common code paths via DCE-version dispatch |

You can't merge them — `radeon` does direct MMIO programming on chips
that don't even speak the AtomBIOS protocol the same way; `radeon_hd`
is built around a DCE-version dispatch table that doesn't exist for
those chips. They share a name and a vendor, but architecturally
they're two separate drivers.

This is why bugs like [#8082] (RS690M X1200), [#8436] (mixed pre-R600
list), and [#17330] (RS690MC Radeon Xpress 1200) are in the "hardware
too old for radeon_hd" bucket — the chips predate the architecture
`radeon_hd` was built around.

### Cliff 2 — DCE 12 → DCN 1.0 (2017)

**Boundary:** Raven Ridge (Vega APU) replaced AtomBIOS-driven DCE 12.0
with direct-register DCN 1.0.

This is the wall that **separates `radeon_hd` from any future Haiku
driver for modern AMD GPUs**. Several open bugs hit this exact cliff:

- [#16393] (Picasso) — Raven-class APU, DCN 1.0
- [#17377] (Navi10) — first RDNA dGPU, DCN 1.0
- [#17516] (Lucienne), [#17525] (Raven), [#17939] (Renoir) — DCN APUs
- [#14800], [#17660] (Vega 56) — actually still DCE 12.0, but late
  DCE 12.0 chips also drift away from the older programming model

A future `radeon_dcn` (or whatever Haiku calls it) would need:

1. Full DCN register-block knowledge per generation (DCN 1, 2, 2.1,
   3.0, 3.1, 3.2…), since each DCN revision has different pipe layout
   and PHY plumbing.
2. An equivalent of Linux's **DAL** (Display Abstraction Layer) — a
   thick library that abstracts the DCN versions behind a uniform API,
   since direct-register programming would otherwise require massive
   copy-paste across DCN revisions. DAL is ~100 K LOC in Linux.
3. Modern bandwidth/voltage/clock state-machine logic. DCE could
   mostly be programmed with "set this PLL, set this timing, go."
   DCN needs constant runtime negotiation between the display
   hardware, DPM (power management), and SMU (system management unit)
   firmware to avoid underruns.

This is why DCN-era bugs are categorized as "out of scope" in the
fork's bug-tracker cross-reference — `radeon_hd`'s entire architecture
is the wrong shape to handle them.

### Cliff 3 — DCE 6 → DCE 8 (2013)

**Sub-boundary, not a driver boundary.** GCN architecture transition
(Sea Islands / Bonaire). The driver still uses AtomBIOS, but several
command tables changed signatures.

Phase 2.4's "Polaris SetDCEClock routing via `dceVersion >= 1102`"
handles one of these. There's also a sub-cliff at DCE 11.0 → 11.2
(Polaris) where the `SetDCEClock` table was added.

This is **inside `radeon_hd`'s coverage**, just requires DCE-version
dispatch in the driver's logic. Not architectural — it's just version
compatibility.

## The full Haiku picture

```
~2000           ~2007          ~2013         ~2017          ~present
  │               │              │             │              │
  │   radeon ─────┤              │             │              │
  │   (R100–R500) │              │             │              │
  │               │              │             │              │
  │               ├── radeon_hd ─┼─────────────┤              │
  │               │  DCE 1.0 → DCE 6           │              │
  │               │              │  DCE 8 → DCE 12             │
  │               │              │             │              │
  │               │              │             ├── radeon_dcn ┤
  │               │              │             │  DCN 1 → DCN 3+
  │               │              │             │              │
```

Haiku currently has the first two boxes (`radeon` and `radeon_hd`).
The third doesn't exist — and porting it is a project on the order of
magnitude of porting an entire new Linux driver subsystem (DAL is
~100 K LOC), which is why "Vega 56 / Raven / Navi" tickets are the
consistent "not happening soon" category.

The fork you're working on (`KevinAdams05/RadeonHDunofficial`) is
firmly in the middle box — DCE-era, AtomBIOS-driven, R600 through
Polaris. That's a tractable chip range covering ~10 years of cards
(~2007–2017), which is a reasonable scope for one-person hobby driver
work.

## DCE version → chip family quick reference

For when reading a syslog and trying to figure out which phase of
fork work is relevant:

| DCE | First chip | Family | Examples |
|-----|-----------|--------|----------|
| 1.0 | R600 | R600 | HD 2400, HD 2600, HD 2900 |
| 2.0 | RV610/630 | R600 | HD 2350/2400, HD 2600 |
| 3.0–3.2 | RV620/RV710/RV730/RV770 | R700 | HD 3470, HD 4670, HD 4870 |
| 4.0 | Cedar/Redwood/Juniper/Cypress | Evergreen | HD 5450, HD 5670, HD 5870 |
| 4.1 | Sumo/Wrestler/Ontario | Evergreen APU | HD 6250, HD 6520G |
| 5.0 | Caicos/Turks/Barts/Cayman | Northern Islands | HD 6450, HD 6570, HD 6870, HD 6950 |
| 6.0 | Cape Verde/Pitcairn/Tahiti | Southern Islands | HD 7770, HD 7870, HD 7970, W4100 |
| 6.1 | Aruba | SI APU | A10-5800K (Trinity), A10-6800K (Richland) |
| 8.0 | Bonaire/Hawaii | Sea Islands | R7 260X, R9 290/290X |
| 8.x | Kabini/Kaveri/Mullins | Sea Islands APU | A10-7860K, Athlon 5350, A4 Micro-6400T |
| 10.0 | Tonga/Fiji | Volcanic Islands | R9 285, R9 380, R9 Fury |
| 11.0 | Carrizo | Volcanic Islands APU | FX-8800P, A10-8700P |
| 11.1 | Stoney | Volcanic Islands APU | A9-9410, A6-9220 |
| 11.2 | Polaris10/11/12 | Polaris (GCN4) | RX 470, RX 480, RX 550, RX 560, RX 580 |
| 12.0 | Vega10 | Vega (GCN5) | RX Vega 56, RX Vega 64 |
| **— DCN starts here —** |  |  |  |
| DCN 1.0 | Raven/Picasso | Vega APU | Ryzen 2200G, 2400G, 3200G, 3400G |
| DCN 1.0 | Vega M (Polaris22) | Kaby-G hybrid | Intel/AMD Kaby Lake-G CPUs |
| DCN 1.0 | Navi10 | RDNA1 | RX 5500, RX 5600, RX 5700 |
| DCN 2.x | Renoir/Lucienne | RDNA2 APU | Ryzen 4000U/5000U series |
| DCN 2.x | Navi2x | RDNA2 | RX 6600, RX 6700, RX 6800, RX 6900 |
| DCN 3.x | Navi3x | RDNA3 | RX 7600, RX 7700, RX 7800, RX 7900 |

Anything from DCN 1.0 down is `radeon_hd` territory in Haiku terms.
Anything DCN-era is currently unsupported.

## When you see a ticket

A useful debugging-time cheat sheet for triaging new bug reports:

1. **Pull the chip's PCI ID from `listdev`.** Look up the family in
   the table above (or query `https://pci-ids.ucw.cz/`).
2. **Pre-R600?** → out of `radeon_hd`'s scope, route to `radeon`.
3. **DCN-era?** → out of *both* drivers' scope, mark "not happening
   without a new Haiku DCN driver."
4. **R600-Polaris?** → in `radeon_hd` scope. Then map by DCE version:
   - DCE 1–3 → Phase 6 (R600/R700) territory
   - DCE 4 → Phase 1, 1.5, 2.1, 2.2 territory
   - DCE 5 → Phase 4 (Caicos cap) and Phase 7 (NI extensions)
   - DCE 6 → Phase 2.3, 2.4 (Aruba PLL, Trinity)
   - DCE 8 → Phase 2.1, 2.2 (Kabini/Kaveri/Mullins APU)
   - DCE 11 → Phase 2.3 (spread spectrum) for Carrizo/Stoney
   - DCE 11.2 → Phase 2.4 (Polaris SetDCEClock routing)
   - DCE 12 → Vega10 — out of practical scope

---

## Appendix — Citeable Sources

When commenting on a Haiku Trac ticket and you want to back up claims
about DCE/DCN/AtomBIOS architecture, these are the durable URLs to
use. Tier 1 is verified by direct fetch as of writing; Tier 2 are
high-confidence but were behind anti-bot gates at fetch time and
should be checked in a browser before pasting.

### Tier 1 — Verified, official, durable

These are **`docs.kernel.org`** pages — the official Linux kernel
documentation site, maintained by the AMD display team upstream.
Citation-grade.

| URL | What it establishes |
|---|---|
| https://docs.kernel.org/gpu/amdgpu/display/index.html | DCN and DCE both exist as test targets; landing page for AMD display docs |
| https://docs.kernel.org/gpu/amdgpu/display/dcn-overview.html | Defines DCN architecture, FE/BE breakdown, current display pipeline model |
| https://docs.kernel.org/gpu/amdgpu/display/display-manager.html | DRM lifecycle integration for AMD display |
| https://docs.kernel.org/gpu/amdgpu/display/dcn-blocks.html | DCN hardware blocks (DCHUBBUB, HUBP, DPP, MPC, OPP, DIO) |
| https://docs.kernel.org/gpu/amdgpu/display/programming-model-dcn.html | How DCN is programmed (direct register I/O via `dc` library) |
| https://docs.kernel.org/gpu/amdgpu/display/dc-debug.html | DC debugging tools |

**Quotable passage from DCN overview** (use verbatim with attribution):

> Display pipeline can be broken down into two components that are
> usually referred as **Front End (FE)** and **Back End (BE)**, where
> FE consists of: DCHUB (Mainly referring to a subcomponent named
> HUBP), DPP, MPC. On the other hand, BE consist of OPP, OPTC, DIO
> (DP/HDMI stream encoder and link encoder).
>
> — *Linux kernel documentation, "Display Core Next (DCN) overview"*

### Tier 2 — Linux source tree (GitHub mirror)

The kernel.org cgit views are sometimes Anubis-gated; the
`github.com/torvalds/linux` mirror is a stable read-only mirror
without that gate. Useful for "this exists, look at the code"
arguments.

| URL | What it establishes |
|---|---|
| https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/display/dc | DC source tree — directory layout (`dcn10/`, `dcn20/`, `dcn30/`, `dcn31/`, `dcn32/`) shows DCN versions are independent code paths |
| https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/display/dc/dc.h | Main DC header — copyright "Advanced Micro Devices, Inc." establishes AMD authorship |
| https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/radeon | Old radeon driver — for contrast, this is the AtomBIOS-driven path |
| https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/atombios.h | The AtomBIOS interface header (ATI/AMD copyright, ~2007) — primary source for "what AtomBIOS is" |
| https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/radeon/atombios_*.c (search) | AtomBIOS-heavy programming files — contrast against the DC tree to demonstrate the architectural difference |

### Tier 3 — Historical context (verify in browser before citing)

These exist and are durable but were behind Anubis at fetch time. Use
when you need historical/origin context on DC and DCN.

| URL | What it establishes |
|---|---|
| https://lore.kernel.org/dri-devel/1495551649-23193-1-git-send-email-Harry.Wentland@amd.com/ | Harry Wentland's RFC introducing AMD DC to dri-devel (May 2017). The canonical "why DC exists" mail. |
| https://www.x.org/wiki/Events/XDC2017/ | XDC 2017 program — Harry Wentland's "DC: Display Core Next" talk. Slides linked from program. |
| https://www.phoronix.com — search "AMDGPU DC mainline 4.15" | Phoronix coverage of DC's December 2017 merge into Linux 4.15. Multiple articles. Journalism-grade for forum/ticket comments. |


### Things to avoid asserting without a primary source

A few framings I've used in chat that *aren't* in any single
authoritative source word-for-word — be careful citing these as if
AMD said them:

- **"DCN bypasses AtomBIOS"** — true and observable from contrasting
  the source trees, but no AMD doc says this in those words. If
  pressed, point to the absence of `atombios_*` calls in
  `drivers/gpu/drm/amd/display/dc/` versus their presence in
  `drivers/gpu/drm/radeon/`.
- **"R500 → R600 cliff in 2007"** — chip launch dates are factual
  (Wikipedia / AMD product pages), but calling it an "architectural
  cliff" is editorial.
- **"DCN driver is roughly 100 K LOC"** — order of magnitude correct
  (`drivers/gpu/drm/amd/display/dc` is large), but for an exact
  claim, run `cloc` against the directory and quote the result with
  date.

### Notes on link rot and gating

- `docs.kernel.org` and `github.com/torvalds/linux` are the most
  durable URLs in this space — kernel.org has 25+ years of stability,
  GitHub has been a reliable mirror for a decade.
- `lore.kernel.org` archives are also designed to be permanent but
  have started gating with Anubis in 2024–2025; mail-archive content
  itself is stable, just access path varies.
- Phoronix articles persist indefinitely but are blocked from naive
  scraping; they're fine to cite by URL since human readers can load
  them.
- AMD's GPUOpen blog has rearranged URLs at least once historically,
  so direct article links there have a higher rot risk than kernel
  docs.
