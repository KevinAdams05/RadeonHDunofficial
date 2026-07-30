>[!NOTE]
>An LLM was used to aid in development of this code.

# Scanout Watermark / Bandwidth Arbitration — Investigation Prep

**Date:** 2026-06-04, Phase B implemented 2026-07-30
**Status:** Phase A complete — thesis confirmed on hardware (see §8).
Phase B implemented and building clean (see §9); **awaiting hardware
test**. Phase C (retest the ceilings) is blocked on that test.
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

## 5. Discrepancies found in our 0.6.0 groundwork defines (both FIXED 2026-07-30)

1. ✅ **ARB/LATENCY pipe stride is wrong.** Our `evergreen_reg.h` defines
   `EVERGREEN_PIPE_REGISTER_STRIDE 0x20` and the comment says "fixed
   per-pipe stride of 0x20". Linux `evergreen_program_watermarks()`
   uses `pipe_offset = crtc_id * 16` (= `0x10`) for
   `PIPE0_ARBITRATION_CONTROL3` / `PIPE0_LATENCY_CONTROL`. The `0x20`
   stride is correct only for `PIPE0_DMIF_BUFFER_CONTROL`
   (`evergreen_line_buffer_adjust()` uses `crtc_id * 0x20`). We need
   two distinct strides.
   **Fixed:** `EVERGREEN_PIPE_ARBITRATION_STRIDE` (0x10) added alongside
   `EVERGREEN_PIPE_REGISTER_STRIDE` (0x20, now commented as DMIF-only),
   with a note that the two are identical for pipe 0 so a mix-up only
   surfaces once a second head is programmed.
2. ✅ **`ni_reg.h` `NI_DPG_*` registers are mislabeled.** The comment
   says they are the DCE 5+ equivalents; in Linux the `DPG_PIPE_*`
   registers (`0x6cc8`/`0x6ccc`, from `sid.h`) are only used by DCE 6
   (`si.c`). All NI chips — including Cayman — go through the
   Evergreen `PIPE0_*` path. The `NI_DPG_*` defines are dead weight for
   every chip this fork supports; correct the comment (or drop them)
   before they mislead an implementation.
   **Fixed:** dropped. Nothing referenced them, and `si_reg.h` already
   declares the same two registers as `SI_DPG_PIPE_*` where they actually
   apply. A comment in their place records why they are gone, so nobody
   re-adds them from the Linux header.

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

---

## 9. Phase B implementation (2026-07-30) — built, not yet tested

New accelerant file `bandwidth.cpp` / `bandwidth.h`, called from the end
of `radeon_set_display_mode()` and bracketed by the Phase A dump (now
`bandwidth_registers_dump(stage)`, moved out of `mode.cpp`) so one syslog
shows both the VBIOS-left state and what we wrote.

`bandwidth_update()` gates on `dceMajor == 4 || dceMajor == 5`, then for
each CRTC of the first pair:

1. `bandwidth_line_buffer_adjust()` — programs `DC_LB_MEMORY_SPLIT`
   (whole buffer for a lone head, half when both are up, mirrored
   partition for the odd CRTC via `..._SPLIT_SECOND`), drives the DMIF
   allocate-and-poll handshake on DCE 4.1 / 5, and returns the resulting
   latency-hiding depth in pixels.
2. `bandwidth_program_watermarks()` — computes the latency watermark and
   priority mark, writes both watermark slots (identical, since we have no
   DPM), restores the original slot selection, then writes
   `PRIORITY_A/B_CNT`. An inactive CRTC is programmed with zeroes so a
   previous mode's values cannot linger.

### Unit conventions

The reference material mixes fixed-point scales, so the implementation
pins them and says so at the top of the file: **bandwidths in MB/s, times
in ns, clocks and pixel clocks in kHz**, with `uint64` intermediates.
Getting this wrong yields plausible-looking but useless watermarks — the
`512 * 8 * 1000 / available_bw` chunk time, for instance, truncates to
0 ns if `available_bw` is accidentally scaled to kB/s.

### Inputs

- `sclk` / `yclk` — added to `radeon_gpu_probe()` from FirmwareInfo
  `ulDefaultEngineClock` / `ulDefaultMemoryClock` (both sit immediately
  after `ulFirmwareRevision` in every table revision, so the base struct
  view is version-safe), cached in `accelerant_info` as
  `engineClockFrequency` / `memoryClockFrequency` in kHz.
- `disp_clk` — the existing `gInfo->displayClockFrequency`.
- `dram_channels` — `MC_SHARED_CHMAP` bits 13:12, read in `bandwidth.cpp`
  where the DCE gate already guarantees the layout.
- active head count — `attached && currentMode.timing.pixel_clock != 0`,
  deliberately **not** `powered`: `detect_displays()` marks every attached
  display powered before any mode is set, which would inflate the head
  count and needlessly halve the first head's line buffer.

### Deviations from the reference behavior

- **Priority mark is clamped, not masked.** A mark that overflowed the
  15-bit field would wrap to a near-zero lead — the exact opposite of what
  a demanding mode needs. Clamping to `PRIORITY_MARK_MASK` is strictly
  safer.
- **`bytes_per_pixel` stays 4** even for 15/16-bit modes. Overstating the
  pixel size only makes the watermarks more conservative.
- **DMIF poll is bounded** (100 × 10 µs) and logs an `ERROR` rather than
  spinning, so a board that never asserts `ALLOCATED_COMPLETED` cannot
  wedge a mode set.

### Verification so far

- Builds clean on the x86_64 cross-tools with `-Werror`, in **both**
  `TRACE_BANDWIDTH` on and off configurations. Two §18 problems were found
  and fixed by the trace-off build: `_sPrintf` had to be declared outside
  the `#ifdef` (`ERROR()` is always on), and the dump function's body needs
  the `#ifdef` around it or its offset locals go unused.
  **Note:** the same `_sPrintf`-inside-the-`#ifdef` pattern exists in
  `mode.cpp`, `hdmi.cpp`, `gpu.cpp` and friends, so those files also fail a
  trace-off build. Not touched here — pre-existing, and nobody builds that
  configuration today.
- The arithmetic was run through a standalone harness against the real
  test-card configurations (Cedar 2ch/DCE4, Barts 8ch/DCE5, Caicos 2ch,
  Turks 4ch) at 1080p, 1440p, 4K and a hypothetical 4480×1440 span mode.
  No overflow; every result fits its register field. Representative:

  | Config | avail. BW | latency | line time | mark | ALWAYS_ON |
  |---|---|---|---|---|---|
  | Cedar 1080p60, 1 head | 2240 MB/s | 5950 ns | 14814 ns | 55 | no |
  | Barts 4K60 DP, 1 head | 13824 MB/s | 9724 ns | 8251 ns | 324 | no |
  | Caicos 1080p60, 1 head | 4480 MB/s | 4016 ns | 14814 ns | 37 | no |
  | Turks dual 1080p60 | 10080 MB/s | 3392 ns | 14814 ns | 31 | **yes** |
  | Caicos dual 4K60 | 4480 MB/s | 12065 ns | 8251 ns | 402 | **yes** |
  | Cedar 4K60, 1 head | 2240 MB/s | 12463 ns | 8251 ns | 415 | **yes** |

  Worth calling out honestly: **`PRIORITY_ALWAYS_ON` does not fire for
  single-head 4K on Barts** — the mode's average bandwidth (1861 MB/s)
  stays under Barts' display DRAM allocation (10080 MB/s). The change that
  does the work on the 6850 is the latency watermark going from 0 to
  ~9700 ns, not the forced priority. The force path fires where the model
  predicts it should: 2-channel boards and dual-head configurations.
  Caicos at 4K sits at 1861 vs a 1920 MB/s allocation — right on the
  line, which is consistent with Caicos carrying the fork's lowest cap.

### Phase B hardware result (2026-07-30, Turks PRO HD 6570, 1600×900 @ 120.3 MHz, hrev59697)

Accelerant staged in `~/config/non-packaged/add-ons/accelerants/` and
app_server restarted (see the deployment note below). All three register
groups now program correctly:

| Register | VBIOS-left | After update | |
|---|---|---|---|
| `DC_LB_MEMORY_SPLIT` CRTC 0 | bits[2:0] = 0 (half) | **2 — whole buffer** | ✅ |
| `DC_LB_MEMORY_SPLIT` CRTC 1 | 4 | **4 — second half** | ✅ |
| `LATENCY_CONTROL` CRTC 0 | `0x00000000` | **`0x44d21c56`** | ✅ low = 7254 ns, high = 17618 ns |
| `PRIORITY_A/B_CNT` CRTC 0 | 0 | **`0x36` = 54** | ✅ matches computed mark |
| `DMIF_BUFFER_CONTROL` | `0x11` | `0x11` | ✅ COMPLETED acked, no `ERROR` |
| CRTC 1 watermarks | 0 | 0 | ✅ correctly zeroed |

The pipe stride is confirmed at **0x10** — the watermark landed at that
offset and the `/x20` shadow read stayed zero. Re-running the update is
idempotent (the second run's "before" dump shows the first run's values).

### 🐞 Bug found and fixed on hardware: LB split field position

The first build wrote the partition through
`EVERGREEN_DC_LB_MEMORY_CONFIG(x)`, i.e. `(x & 0xf) << 20`. On hardware
that write did **nothing** to bits 23:20 and silently cleared CRTC 1's
real setting in bits 2:0 from 4 to 0 — leaving both CRTCs claiming the
*first* half of a shared line buffer.

Root cause is a **third groundwork discrepancy of the same family as §5**:
`evergreen_reg.h` had the **DCE 6 / Southern Islands** field position
copied into the Evergreen defines. Confirmed against Linux:

- `evergreend.h` defines only `DC_LB_MEMORY_SPLIT 0x6b0c` with **no**
  config macro, and `evergreen_line_buffer_adjust()` writes the bare
  partition number — its comment says "specified in bits 2:0".
- `sid.h` *does* have `DC_LB_MEMORY_CONFIG(x) ((x) << 20)`, used from
  `si.c`. Same register address, field moved on DCE 6.

Fixed by deleting the bogus macro, widening
`EVERGREEN_DC_LB_MEMORY_SPLIT_MASK` to `0x7` (the partition is 0–7 once
the second-controller `+4` is applied — the old `0x3` was too narrow),
and writing the bare value. A comment in the header records why the
shifted form must not come back.

Worth noting this bug was **invisible single-head** — CRTC 0's own
partition happened to stay at "first half", which is survivable. It would
have surfaced as the second monitor starving under Track A, i.e. exactly
the class of bug that would have been blamed on the span code.

### ✅ RESOLVED: the clocks are real, and that changes the thesis

The AtomBIOS `GetEngineClock` / `GetMemoryClock` probe (added to
`radeon_gpu_probe()` as `radeon_gpu_engine_clock_current()` /
`radeon_gpu_memory_clock_current()`) answers it:

```
clocks (FirmwareInfo default): engine 100000 kHz, memory 150000 kHz
clocks (AtomBIOS current):     engine  99990 kHz, memory 149970 kHz
clocks (in use):               engine  99990 kHz, memory 149970 kHz, display 540000 kHz
```

The live query agrees with the table default to within 0.03%. **The card
really is running at ~100 MHz core / ~150 MHz memory** — hypothesis 1
below. The FirmwareInfo value was not a stale artifact; it is the state
the VBIOS posted, and the card sits in it because Haiku has no power
management to raise it. (The 9999-vs-10000 granularity is itself mild
evidence the query computes from PLL dividers rather than echoing the
table — see the caveat at the end of this section.)

What follows from that:

1. **`available = 1679 MB/s` is genuine**, and DRAM is what binds it:
   4 channels × 4 B × 149.97 MHz × 0.7. Not the ≈10 GB/s the card's
   spec sheet implies.
2. **The per-chip pixel-clock caps are physically justified, not
   empirical workarounds for bad arbitration.** 4K@60 needs 1861 MB/s of
   average scanout bandwidth against 1679 MB/s available. No watermark
   programming can make that sustainable.
3. **Phase C as written would fail**, and would fail for a reason that has
   nothing to do with watermarks. Lifting the Barts cap will not deliver
   4K@60 while the card is parked in its lowest power state.
4. **The real unlock is clock / power management**, not arbitration. That
   is a substantially larger piece of work — PowerPlay table parsing,
   `SetEngineClock` / `SetMemoryClock` (both exist in the same AtomBIOS
   master command table), and voltage sequencing, which is the risky part.
   A minimal version — read the highest PowerPlay performance level and
   apply it once at init — may be tractable and would be the highest-value
   next investigation for this driver.
5. **Phase B keeps standalone value.** Scanout previously had *zero*
   starvation protection against a genuinely tight 1679 MB/s ceiling; it
   now has correct watermarks for the clocks the card actually runs at.
   That is a real robustness improvement. It simply is not the lever that
   lifts the caps.

### ✅ Corroborated by the PowerPlay table — and the clocks can be raised

`powerplay_dump_performance_levels()` (new `powerplay.cpp`) reads the
PowerPlay table's performance levels. On the Turks HD 6570, table 5.1:

| Level | Engine | Memory | VDDC | DRAM bw @ 4 ch |
|---|---|---|---|---|
| 0 | 650 MHz | 900 MHz | 1050 mV | 10080 MB/s |
| **1** | **100 MHz** | **150 MHz** | **900 mV** | **1680 MB/s** |
| 2 | 400 MHz | 900 MHz | 1000 mV | 10080 MB/s |

Level 1 is *exactly* what `GetEngineClock` / `GetMemoryClock` reported
(99990 / 149970 kHz). That closes the question from a second, independent
source in the same BIOS: the clock query was correct and live, 100/150 is
a real advertised DPM state, and **the card is sitting in the lowest one**.
No Linux cross-check needed.

It also answers the question that actually matters: **the clocks can be
raised, to 900 MHz memory — 6x the current DRAM bandwidth.** At 10080 MB/s
a 4K@60 mode's 1861 MB/s average is comfortable, so the pixel-clock caps
would become liftable on bandwidth grounds.

Two useful design conclusions for the eventual power-management work:

- **Level 2 (400/900 @ 1000 mV) is the better target than level 0**, not
  level 0. Display bandwidth is bound by DRAM either way: at level 2 the
  data-return path gives 10240 MB/s against DRAM's 10080, so the lower
  engine clock does not bind. Level 2 therefore delivers the *same* 10080
  MB/s of usable scanout bandwidth as level 0 while asking for 250 MHz
  less engine clock and 50 mV less VDDC — materially safer for a driver
  with no thermal management.
- **Voltage sequencing is mandatory, not optional.** Level 1 runs at
  900 mV; both 900 MHz-memory levels need 1000–1050 mV. Raising clocks
  without raising VDDC first would be unstable. A "just call
  SetEngineClock" shortcut is not viable.

### Parse note (cost me a build)

`pptable.h` declares a counted `ClockInfoArray { ucNumEntries,
ucEntrySize, ... }`. **DCE 4/5 boards do not use it.** The clock-info
array is *bare*: entry size comes from `ucClockInfoSize` in the base
table, and the entry count from the span between
`usClockInfoArrayOffset` and `usNonClockInfoArrayOffset`. Assuming the
counted form read the first entry's clock bytes as "232 entries of 253
bytes" — caught by a bounds check rather than a page fault, which is the
argument for bounds-checking every BIOS-derived offset.

### Original framing of the question (kept for the record)

`radeon_gpu_probe` reports, from FirmwareInfo:

```
clocks: engine 100000 kHz, memory 150000 kHz, display 540000 kHz
```

100 MHz engine / 150 MHz memory is **not** the HD 6570's operating spec
(≈650 MHz core, ≈900 MHz DDR3). These look like the VBIOS-posted
low-power boot state. Consequences:

- `available` comes out **1680 MB/s** instead of the ≈10080 MB/s the
  pre-test model assumed — 6× lower, because `dram_bw` binds.
- The error direction is *conservative* (lower ceiling → larger watermark
  → earlier urgent requests), so it cannot corrupt. But every Phase C
  number would rest on the wrong input.

Two hypotheses, and they imply very different projects:

1. **The card really is parked at 150 MHz memory** (plausible — Haiku has
   no power management, so the card sits wherever the VBIOS left it). Then
   1680 MB/s is genuine, 4K@60 needs 1861 MB/s average and *cannot* be
   sustained, the pixel-clock caps are physically correct, and the real
   fix is clock/power management — a much larger piece of work than
   watermarks.
2. **FirmwareInfo's defaults are a boot-state artifact** and the card is
   actually running at spec. Then the watermarks are 6× over-conservative
   and should be recomputed from the true clocks.

`ulDefaultEngineClock` / `ulDefaultMemoryClock` were always a documented
assumption (§3: Linux uses `rdev->pm.current_sclk/current_mclk`, which we
have no equivalent for). The decisive experiment is cheap: the AtomBIOS
master command table exposes **`GetEngineClock`** and **`GetMemoryClock`**
(atombios.h:323–324), which return the *current* clocks rather than table
defaults. Probe those before drawing any Phase C conclusion.

*(Resolved above: hypothesis 1. Kept because the reasoning that led to the
probe is the useful part of the record.)*

### Deployment note (packagefs)

Dropping the `.hpkg` in `~/config/packages/` did **not** activate on
shredder: `~/config/packages/administrative/activated-packages` was
0 bytes and `~/config/add-ons/` empty, with the daemon logging
`Package "aspeed_gfx_unofficial-0.1.4-x86_64.hpkg" from activation file
not in packages directory` — a dangling reference to a package that had
been moved to `~/Desktop/`. Because this change is accelerant-only (no
kernel `.cpp` touched, and the new fields went into `accelerant_info`,
not `radeon_shared_info`), the reliable test path is to drop just the
accelerant into `~/config/non-packaged/add-ons/accelerants/` —
`BPathFinder` searches it ahead of `/boot/system/add-ons/`. Also note
`launch_roster restart|stop|start x-vnd.haiku-app_server` are all no-ops
on this build; `kill <app_server pid>` is what actually recycles it, and
launch_daemon respawns within a second.

### Next

**Phase C as originally written is superseded.** Lifting the caps cannot
work while the card runs at its boot power state (see the resolved clock
question above). Revised plan:

1. ✅ **Confirm the clock reading** — done, via the PowerPlay table
   (above). No Linux cross-check required.
2. **Phase D regression** (unchanged, still worth doing): Cedar 1080p
   HDMI on the DCE 4 path — different LB sizes, no DMIF handshake.
   Verifies Phase B did not break the low-bandwidth case. Needs a card
   swap.
3. **New investigation: clock / power management.** The PowerPlay reader
   is now in place; what remains is the write side — set voltage first,
   then engine and memory clocks, targeting **level 2** (§ above). Needs
   its own prep document covering the AtomBIOS `SetVoltage` /
   `SetEngineClock` / `SetMemoryClock` sequence, the safe ordering, and
   what happens on a board whose table lists no mid level. This is what
   lifts the pixel-clock caps, and it is also what gives Track A span
   modes their bandwidth headroom.
4. **Only then** retest the caps, with watermarks recomputed from the
   raised clocks. Phase B already recomputes automatically — the
   watermarks are derived from `gInfo->memoryClockFrequency`, so raising
   the clocks feeds through with no further change to `bandwidth.cpp`.

Also worth extending: run `powerplay_dump_performance_levels()` on Cedar,
Caicos and Barts during the Phase D swap. If every board is parked at its
lowest level, that is a single root cause behind all three pixel-clock
caps and makes the power-management case decisively.
The syslog should now show non-zero `LATENCY_CONTROL` in the "after"
dump and a `split bits = 2` (whole buffer) `DC_LB_MEMORY_SPLIT` on
CRTC 0. If the watermarks read back as zero, check the pipe stride
first — the dump prints both candidates for CRTC 1 for exactly that
reason.
