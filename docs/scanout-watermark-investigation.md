>[!NOTE]
>An LLM was used to aid in development of this code.

# Scanout Watermark / Bandwidth Arbitration — Investigation Prep

**Date:** 2026-06-04
**Status:** Phase A complete — thesis confirmed on hardware (see §8);
Phase B (implementation) is a go
**Target bug:** stride-aliased scanout corruption above the per-chip
pixel-clock caps (Caicos 165 MHz, Turks 250 MHz, Barts 340 MHz; see
`mode.cpp` and CHANGELOG 0.4.0 / 0.5.0 / 0.6.2)
**Test hardware:** HD 6850 (Barts PRO, 4K@60 DP = 533 MHz reproduces),
HD 6570 (Turks), HD 7470 (Caicos), HD 5450 (Cedar)

Diagram: [`../diagrams/scanout-watermark-arbitration.svg`](../diagrams/scanout-watermark-arbitration.svg)

---

## 1. Thesis

The per-chip caps are empirical workarounds for one underlying cause:
**we never program the display-engine bandwidth arbitration**. Three
register groups control whether scanout DMA survives contention with
other memory clients:

1. **Line buffer split** (`DC_LB_MEMORY_SPLIT`, `0x6b0c` + CRTC
   offset) — how much line-buffer each CRTC gets. With one display
   active, the whole buffer can hide much more latency than the
   default half split.
2. **Latency watermarks** (`PIPE0_ARBITRATION_CONTROL3` /
   `PIPE0_LATENCY_CONTROL`, `0x0bf0` / `0x0bf4` + pipe offset) — how
   early the DMIF issues urgent requests.
3. **Priority marks** (`PRIORITY_A_CNT` / `PRIORITY_B_CNT`, `0x6b18` /
   `0x6b1c` + CRTC offset) — when the MC elevates scanout priority,
   including a `PRIORITY_ALWAYS_ON` override for bandwidth-tight
   configurations.

The register defines already exist in our `evergreen_reg.h` (added as
0.6.0 groundwork — see the comment block at the "Display Bandwidth /
Watermark / Priority registers" section), but **nothing calls them**.

Supporting observation: the VBIOS/GOP posts the console mode and
programs watermarks for *that* mode. When we set a higher mode, the
stale low-mode watermarks under-request — which is exactly when the
corruption appears.

## 2. How Linux does it (reference only — no code copying)

Reference: `drivers/gpu/drm/radeon/evergreen.c` —
`evergreen_bandwidth_update()` (line ~2326),
`evergreen_program_watermarks()` (~2156),
`evergreen_line_buffer_adjust()` (~1827),
`evergreen_get_number_of_dram_channels()` (~1918).

**Confirmed via `radeon_asic.c`: every chip we support uses this same
path.** `evergreen_asic`, `btc_asic` (Barts/Turks/Caicos), and
`cayman_asic` all set `.bandwidth_update = evergreen_bandwidth_update`.
The `DPG_PIPE_*` register set (`0x6cc8`/`0x6ccc`) is used only from
`si.c` (DCE 6, Southern Islands) — see §5 for the implications for our
`ni_reg.h`.

Flow, per mode set (Linux runs it for every CRTC on any mode change):

1. **Line buffer**: single head on the pair → whole LB (`split = 2`);
   both heads → half each (`split = 0`/`4`). On DCE 4.1/5 also program
   `PIPE0_DMIF_BUFFER_CONTROL` (`0x0ca0`) and poll the
   `ALLOCATED_COMPLETED` bit. Resulting `lb_size` (pixels of latency
   hiding): DCE 4 whole = 7680×2, half = 3840×2; DCE 5 whole = 8192×2,
   half = 4096×2.

2. **Bandwidth ceilings** (all in MB/s-scale integer math):
   - `dram_bw = dram_channels × 4 bytes × yclk × 0.7` (efficiency)
   - `data_return_bw = 32 × sclk × 0.8`
   - `dmif_bw = 32 × disp_clk × 0.8`
   - `available_bw = min` of the three
   - display's *average* share of DRAM is conservatively 0.3 ×
     `dram_bw`

3. **Latency watermark (ns)** =
   `mc_latency (2000 ns) + dc_latency (40 000 000 / disp_clk kHz) +
   other-heads return time`, where other-heads time =
   `(num_heads + 1) × worst_chunk + num_heads × cursor_pair`,
   `worst_chunk = 512 × 8 × 1000 / available_bw`,
   `cursor_pair = 128 × 4 × 1000 / available_bw`. If the line-fill
   time at the LB fill rate exceeds the active time, the overflow is
   added.

4. **Mode's average bandwidth** =
   `src_width × bytes_per_pixel × vsc / line_time`.

5. **Priority mark** =
   `latency_wm(ns) × pixel_clock(MHz) × hsc / 16`, masked to 15 bits
   into `PRIORITY_x_CNT`. **Force `PRIORITY_ALWAYS_ON`** when the mode's
   average bandwidth exceeds the per-head display DRAM allocation, or
   exceeds `available_bw / num_heads`, or latency hiding fails
   (`latency_wm > latency_tolerant_lines × line_time + blank_time`).
   The groundwork comment in our `evergreen_reg.h` flags this forced
   path as the likely fix for bandwidth-tight cards.

6. **Write sequence**: select watermark slot 1 in
   `ARBITRATION_CONTROL3`, write `LATENCY_CONTROL`
   (low = latency wm, high = line time, both 16-bit, capped 65535);
   select slot 2, write again; restore the original slot selection;
   then write `PRIORITY_A_CNT` / `PRIORITY_B_CNT`.

Watermarks A and B exist for DPM clock switching. We have no DPM, so
**program both slots with the same value** computed from the default
clocks.

## 3. Inputs we need, and where to get them

| Input | Linux source | Our source |
|---|---|---|
| `yclk` (memory clock) | `rdev->pm.current_mclk` | AtomBIOS `FirmwareInfo.ulDefaultMemoryClock` (10 kHz units) — same table-parse pattern as `pll.cpp` (`pll_usage_mask`) and `gpu.cpp` |
| `sclk` (engine clock) | `rdev->pm.current_sclk` | `FirmwareInfo.ulDefaultEngineClock` (10 kHz units) |
| `dram_channels` | `MC_SHARED_CHMAP` (`0x2004`) `NOOFCHAN` bits 13:12 (mask `0x3000`) → 1/2/4/8 | same register read via `read32()`; define needs adding |
| `disp_clk` / mode timing | DRM mode | `display_mode` we're setting (pixel_clock kHz, h/v totals) |
| `num_heads` | enabled CRTC count | `gDisplay[]` active count |
| `lb_size` | `evergreen_line_buffer_adjust` return | computed from the split we program |
| `bytes_per_pixel` | hardcoded 4 (XXX in Linux too) | 4 — we scan out 32-bit |
| `vsc` / `vtaps` / `hsc` (scaler) | per-CRTC scaler state | we run no scaler: vsc = hsc = 1, vtaps = 1 — simplifies the math considerably |
| `interlaced` | mode flags | mode flags (rarely relevant) |

The fixed-point `fixed20_12` chains in Linux reduce to plain 64-bit
integer math once vsc/hsc are pinned at 1 — implement original integer
versions (Linux-reference policy: logic only, never code).

## 4. Where it hooks in

End of `radeon_set_display_mode()` in `mode.cpp` — after the CRTC,
PLL, and encoder programming, alongside (or just before) where the
0.6.x cap framework lives. Like Linux, recompute for **all** CRTCs on
any mode change (a second head changes `num_heads` and the LB split
for the first head).

`gInfo->shared_info` already exposes what the accelerant needs;
everything stays accelerant-side (`mode.cpp` + a new
`bandwidth.cpp`/`.h` or a section in `mode.cpp` — decide at
implementation time). New file ⇒ remember the Jamfile **and**
`scripts/build.sh` (overlay build, STYLE_GUIDE §1.2).

## 5. Discrepancies found in our 0.6.0 groundwork defines (fix first)

1. **ARB/LATENCY pipe stride is wrong.** Our `evergreen_reg.h` defines
   `EVERGREEN_PIPE_REGISTER_STRIDE 0x20` and the comment says "fixed
   per-pipe stride of 0x20". Linux `evergreen_program_watermarks()`
   uses `pipe_offset = crtc_id * 16` (= `0x10`) for
   `PIPE0_ARBITRATION_CONTROL3` / `PIPE0_LATENCY_CONTROL`. The `0x20`
   stride is correct only for `PIPE0_DMIF_BUFFER_CONTROL`
   (`evergreen_line_buffer_adjust()` uses `crtc_id * 0x20`). We need
   two distinct strides.
2. **`ni_reg.h` `NI_DPG_*` registers are mislabeled.** The comment
   says they are the DCE 5+ equivalents; in Linux the `DPG_PIPE_*`
   registers (`0x6cc8`/`0x6ccc`, from `sid.h`) are only used by DCE 6
   (`si.c`). All NI chips — including Cayman — go through the
   Evergreen `PIPE0_*` path. The `NI_DPG_*` defines are dead weight for
   every chip this fork supports; correct the comment (or drop them)
   before they mislead an implementation.

## 6. Plan of attack

**Phase A — instrument (no behavior change).** At mode-set time, TRACE
read-backs of `DC_LB_MEMORY_SPLIT`, `PRIORITY_A/B_CNT`, and both
watermark slots of `PIPE0_LATENCY_CONTROL`, per CRTC. Boot the 6850,
set a known-good mode and (cap temporarily raised) the failing
4K@60 DP mode. This confirms the thesis: expect VBIOS-era or reset
values that don't scale with the mode.

**Phase B — implement.** LB split + latency watermark + priority
marks per §2–§4, both watermark slots, integer math, original code.
TRACE every computed intermediate (bandwidths, latency, marks) so
field debugging needs no rebuild.

**Phase C — retest the ceilings.** With watermarks live and the Barts
cap lifted: 4K@60 DP on the 6850. Then walk Turks (above 250) and
Caicos (above 165). Outcomes:
- corruption gone → convert caps to true link-limit checks (HDMI
  1.4a 340 MHz etc.) and update README matrix + CHANGELOG;
- corruption shifted upward → the caps were partially masking
  arbitration starvation; re-derive each cap empirically with
  watermarks active;
- no change → thesis wrong for this symptom; next suspects are DMIF
  request size / urgency programming or genuine MC bandwidth limits;
  revisit `MC_ARB_*` timing and `evergreen_mc_program()`.

**Phase D — Cedar regression.** Cedar (DCE 4) shares the path with
different LB sizes (7680×2/3840×2) and no DMIF handshake. Verify
1080p HDMI still clean after the change (card is going back into the
test box for the magenta-stripe work anyway).

## 7. Register quick reference (per CRTC unless noted)

```
DC_LB_MEMORY_SPLIT        0x6b0c + crtc_offset   split[1:0], CONFIG[23:20]
PRIORITY_A_CNT            0x6b18 + crtc_offset   mark[14:0], OFF(16), ALWAYS_ON(20)
PRIORITY_B_CNT            0x6b1c + crtc_offset   same layout
PIPE0_ARBITRATION_CONTROL3  0x0bf0 + crtc_id*0x10  wm select [17:16]
PIPE0_LATENCY_CONTROL       0x0bf4 + crtc_id*0x10  low wm [15:0], high wm [31:16]
PIPE0_DMIF_BUFFER_CONTROL   0x0ca0 + crtc_id*0x20  alloc[3:0], COMPLETED(4) — DCE4.1/5 only
MC_SHARED_CHMAP           0x2004 (global)         NOOFCHAN [13:12]
```

`crtc_offset` = `EVERGREEN_CRTCn_REGISTER_OFFSET` (already in
`evergreen_reg.h`). Note the two different pipe strides (§5.1).

---

## 8. Phase A results (2026-06-04, Cedar HD 5450, 1080p HDMI, hrev59697)

`bandwidth_registers_dump()` (0.6.3~pre2) after the first mode set:

```
CRTC 0:  DC_LB_MEMORY_SPLIT        0x00040000   (split bits = 0: half/half)
         PRIORITY_A_CNT            0x00000000   (mark 0, no ALWAYS_ON)
         PRIORITY_B_CNT            0x00000000
         ARBITRATION_CONTROL3      0x00030002   (reset default)
         LATENCY_CONTROL           0x00000000   (both watermarks zero)
         DMIF_BUFFER_CONTROL       0x00000000   (n/a on DCE 4 — matches Linux gating)
CRTC 1:  same, split bits = 4 (second half)
```

**Thesis confirmed.** The VBIOS/GOP leaves the entire arbitration
block at reset values: zero latency watermarks, zero priority marks,
and a half/half line-buffer split even with one active display.
Scanout has no starvation protection whatsoever — consistent with
corruption appearing exactly at the high-bandwidth modes behind the
Caicos/Turks/Barts caps, and surviving at low pixel clocks only
because demand is low.

Caveats: the §5.1 stride disambiguation was inconclusive — both
candidate strides read identical values because everything is at reset
defaults. Linux's `0x10` remains authoritative.

**DCE 5 confirmation (same day, HD 7470 Caicos, DVI 1080p):** identical
reset-state picture — `PRIORITY_A/B_CNT = 0`, `LATENCY_CONTROL = 0`,
half/half LB split — with one difference: `DMIF_BUFFER_CONTROL = 0x11`
(1 buffer allocated, COMPLETED set). So the VBIOS does program the
DMIF allocation on DCE 5 (it reads 0 on DCE 4 Cedar), exactly matching
Linux gating the DMIF handshake to DCE 4.1/5 in
`evergreen_line_buffer_adjust()`. Phase B must preserve/extend that
allocation when reprogramming the LB split on NI cards. Note: the
Turks dump from the same day read `DMIF_BUFFER_CONTROL = 0x0` — so
even among DCE 5 boards the VBIOS-left state varies by vendor, and
Phase B must do the full allocate-and-poll handshake itself rather
than trusting whatever it finds.

Next: Phase B per §6.
