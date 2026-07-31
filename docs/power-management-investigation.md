# Clock / Power Management — Investigation Prep

**Date:** 2026-07-31
**Status:** **Investigation complete for what the legacy AtomBIOS path can
do.** The engine-clock raise works and ships behind a default-off driver
setting (Barts 99990 -> 299980 kHz at unchanged voltage, display bandwidth
2559 -> 3360 MB/s, §9). **Memory reclocking does NOT work on Northern
Islands** (§10). **The pixel-clock caps are genuine bandwidth limits at the
clocks these boards are parked at, and the engine raise does not lift them**
(§11) — that needs memory reclocking, i.e. the DPM/SMC path. PowerPlay sweep
is 4/4: every capped card is parked at or below its lowest memory clock, the
one uncapped card runs flat out.
**Target bug:** the per-chip pixel-clock caps in `mode.cpp`
(Caicos 165 MHz, Turks 250 MHz, Barts 340 MHz), now understood to be a
consequence of the card sitting in its lowest power state rather than of
unprogrammed display arbitration.
**Test hardware:** HD 6850 (Barts PRO), HD 6570 (Turks), HD 5450 (Cedar),
HD 7470 (Caicos, untested)
**Predecessor:** [`scanout-watermark-investigation.md`](scanout-watermark-investigation.md)
— read §9 there first; this document starts where that one ends.

Diagram: [`../diagrams/powerplay-level-targets.svg`](../diagrams/powerplay-level-targets.svg)

---

## 1. Why

The watermark work (Phase B, shipped) programmed display bandwidth
arbitration correctly and **did not lift the caps**, because the caps are
not an arbitration problem. The PowerPlay sweep on 2026-07-31 showed why:

| Card | Cap | Clocks in use | PowerPlay memory range | Sitting at |
|---|---|---|---|---|
| Turks HD 6570 | 250 MHz | 100 / **150** MHz | 200 – 900 MHz | **lowest** |
| Barts HD 6850 | 340 MHz | 100 / **150** MHz | 150 – **1000** MHz | **lowest** |
| Cedar HD 5450 | **none** | 650 / **400** MHz | 200 – 400 MHz | **highest** |

Both capped cards are parked at their lowest memory clock; the only card
without a cap is already flat out. Haiku has no power management, so each
card stays wherever its VBIOS posted it — and that differs per board.

Raising the clocks is therefore the actual lever. The goal of this work is
narrow and deliberately unambitious:

> **Apply one static, higher performance level at init. No dynamic power
> management, no thermal management, no clock gating, no SMC firmware.**

That is enough to lift the caps and to give Track A span modes their
bandwidth headroom, and it is a far smaller undertaking than DPM.

## 2. What the bandwidth math says the gain is

Using the same formulas `bandwidth.cpp` already implements
(`dram_bw = channels × 4 × yclk × 0.7`,
`data_return_bw = 32 × sclk × 0.8`, `dmif_bw = 32 × disp_clk × 0.8`,
`available = min`):

**Barts (8 channels, disp_clk 540 MHz)**

| Level | Engine / Memory | dram | data-return | available | vs now |
|---|---|---|---|---|---|
| L1 *(current)* | 100 / 150 | 3360 | **2559** | **2559** | — |
| L3 | 300 / 1000 | 22400 | **7680** | **7680** | **3.0x** |
| L2 | 600 / 1000 | 22400 | 15360 | **13824** | **5.4x** |
| L0 | 775 / 1000 | 22400 | 19840 | **13824** | 5.4x |

Note **Barts is engine-clock-bound at every level below L2** — its
`data_return_bw` is the binding constraint, not DRAM. Raising memory alone
would achieve nothing on this board. Above L2 the DMIF ceiling (13824)
binds, so L0 buys nothing over L2 for display purposes.

4K@60 needs 1861 MB/s average and a 9600 MB/s display share at full memory
clock, so **any of L3/L2/L0 makes the 4K@60 reproducer comfortable**.

**Turks (4 channels)**: L1 (100/150) = 1679 MB/s → L2 (400/900) = 10080
MB/s, a 6.0x gain.

**Cedar**: already at its top level. This feature must be a **no-op** here.

## 3. Interfaces available

All three needed command tables exist in our AtomBIOS header and are
"Function Tables" (i.e. intended for driver use), and the heavy lifting for
memory clock changes lives inside AtomBIOS — `SetMemoryClock` internally
calls `ResetMemoryDLL`, `AdjustMemoryController`, `MC_Synchronization`,
`DynamicMemorySettings` and `MemoryDeviceInit`, none of which we have to
reimplement.

| Table | Params struct | Notes |
|---|---|---|
| `SetEngineClock` | `SET_ENGINE_CLOCK_PARAMETERS { ULONG ulTargetEngineClock; }` | 10 kHz units |
| `SetMemoryClock` | `SET_MEMORY_CLOCK_PARAMETERS { ULONG ulTargetMemoryClock; }` | 10 kHz units |
| `SetVoltage` | version-dependent, see §4 | **must be dispatched on table crev** |
| `GetEngineClock` / `GetMemoryClock` | single `ULONG` out | already used by `powerplay.cpp`; our verification channel |

Reading the current clocks back after a change is already implemented and
proven on three cards, which gives us a cheap post-condition check.

## 4. The voltage problem — two distinct traps

### 4.1 `SetVoltage` semantics depend on the table revision

```c
SET_VOLTAGE_PARAMETERS      { ucVoltageType; ucVoltageMode; ucVoltageIndex; }   // crev 1: INDEX
SET_VOLTAGE_PARAMETERS_V2   { ucVoltageType; ucVoltageMode; usVoltageLevel; }   // crev 2: millivolts
SET_VOLTAGE_PARAMETERS_V1_3 { ucVoltageType; ucVoltageMode; usVoltageLevel; }   // crev 3/4: mV *or phase*
```

crev 1 wants an **index**; crev 2+ want a **real level**, and the v1_3
comment explicitly says "in unit of mv **or Voltage Phase (0, 1, 2, ..)**".
So the same number means different things on different boards.
`atom_parse_cmd_header()` must be consulted and **an unknown crev must abort
rather than guess** — a wrong write here is a wrong voltage.

Voltage type is selected by `ucVoltageType`:
`SET_VOLTAGE_TYPE_ASIC_VDDC` (1) and `SET_VOLTAGE_TYPE_ASIC_VDDCI` (4) are
the two rails PowerPlay reports. **They are independent** — see §5.

### 4.2 `usVDDC` is sometimes not a voltage at all

Barts level 0 reports `VDDC 65281`, which is `0xff01` =
**`ATOM_VIRTUAL_VOLTAGE_ID0`** (`atombios.h:2655`). Values `0xff01`–`0xff08`
are *virtual voltage IDs*: placeholders meaning "this level's real voltage
is leakage-binned per die and must be resolved from `ASIC_ProfilingInfo`"
(data table, `atombios.h:2819`).

The reference driver does not attempt to resolve it in the simple path — it
refuses outright, with the comment *"0xff01 is a flag rather then an actual
voltage"*, and returns without programming anything.

**Consequence: Barts L0 is not directly usable, and must not be.** This is
one reason the target-selection policy in §5 deliberately does not pick the
top level.

## 5. Target-level policy

The naive choice — "apply the highest level" — is both riskier and no
better. Two rules give a much better target:

1. **Pick the lowest level that reaches the maximum memory clock.** Display
   bandwidth is capped by DRAM or DMIF, not by engine clock, once memory is
   at full speed. A lower level means less voltage, less heat and less
   risk for identical display bandwidth.
2. **Prefer a level whose voltages are unchanged from the current one.**
   Then no `SetVoltage` call is needed at all, and the entire voltage risk
   class disappears for that transition.

Applying those to the measured tables:

**Barts** — current L1 is `VDDC 950 / VDDCI 950`.

| Level | Engine / Memory | VDDC | VDDCI | Voltage change needed |
|---|---|---|---|---|
| L1 *(current)* | 100 / 150 | 950 | 950 | — |
| **L3** | 300 / 1000 | **950** | 1100 | **VDDC unchanged**, VDDCI +150 |
| L2 | 600 / 1000 | 1100 | 1100 | both +150 |
| L0 | 775 / 1000 | `0xff01` | 1100 | **unusable** (virtual ID) |

**L3 is the recommended Barts target**: it reaches full memory clock, gives
3.0x the display bandwidth, and needs **no VDDC change** — only VDDCI.
L2 is the follow-up if more headroom is wanted.

Better still for a *first* write: L3 specifies 300 MHz engine at
**VDDC 950, the voltage already applied**. So raising the **engine clock
alone** to 300 MHz is sanctioned by the card's own table at the current
voltage — a write with no voltage change whatsoever. That alone takes Barts
from 2559 to 3360 MB/s (now DRAM-bound), a 31% gain, and proves the whole
mechanism at essentially zero risk. That is Phase B.

**Turks** — current L1 is `VDDC 900`; L2 (400/900) needs `VDDC 1000`. There
is no same-voltage high-memory level, so Turks genuinely requires a voltage
raise. Do Barts first.

**Cedar** — already at its top level. The code must detect this and do
nothing.

## 6. Safety rules

Non-negotiable, and each maps to a specific failure mode:

1. **Voltage up before clocks up; clocks down before voltage down.**
   Running a clock faster than its voltage supports is the classic
   instability/hang.
2. **Never program a virtual voltage ID.** Refuse `0xff01`–`0xff08`
   explicitly (§4.2).
3. **Abort on an unrecognised `SetVoltage` table crev.** Do not guess
   between index, millivolts and phase.
4. **No-op if already at or above the target.** Cedar must be untouched,
   and re-entry (e.g. a second mode set) must not re-apply anything.
5. **Verify by read-back.** After the change, call
   `GetEngineClock`/`GetMemoryClock`; if the values did not move as
   expected, log loudly and do not proceed to further steps.
6. **Gate the whole feature behind a driver setting, default OFF.** A
   recovery path that requires editing one text file is worth a great deal
   the first time a board hangs; a recovery path that requires
   reinstalling a package is not.
7. **Never raise beyond the highest level the table advertises.** The table
   is the only statement of what this board and its cooling can sustain.

### Why this belongs in the accelerant, not the kernel driver

`powerplay.cpp` is accelerant-side, and that is a genuine safety property:
the accelerant runs inside `app_server`, so a hang or crash takes down the
desktop but **leaves SSH alive**, making the override removable remotely. A
kernel driver that faults during attach can leave the machine unbootable
with no shell — the rtl8125 work ran into exactly that consideration. Keep
the clock/voltage writes in the accelerant.

Thermal caveat worth stating plainly: we implement no thermal management. A
sustained higher clock on a card with a marginal or dust-clogged cooler is a
real risk, and this is a further argument for rule 1 in §5 (lowest level
that does the job) over "maximum performance".

## 7. Phased plan

**Phase A — read-only probe. No writes at all.**
Extend `powerplay.cpp` to log, per card:
- `SetVoltage` / `SetEngineClock` / `SetMemoryClock` table `frev`/`crev`
  (via `atom_parse_cmd_header`), so §4.1's dispatch can be written against
  facts rather than assumption;
- whether `ASIC_ProfilingInfo` is present, for the record;
- which level the current clocks correspond to, and which level the §5
  policy would select — **printed, not applied.**

Run on Barts, Turks, Cedar and Caicos. This is the analogue of the watermark
work's Phase A and carries the same near-zero risk.

**Phase B — engine clock only, no voltage change.**
Barts L1 → 300 MHz engine at unchanged VDDC (§5). Behind the setting from
rule 6. Verify by read-back and by `bandwidth_update` reporting a higher
`available`. Expected: 2559 → 3360 MB/s.

**Phase C — memory clock, with VDDCI.**
Barts → full L3 (300/1000, VDDCI 950 → 1100). This is the first genuine
voltage write; do it with SOL attached and IPMI power control to hand.
Expected: available → 7680 MB/s.

**Phase D — Turks, which needs a VDDC raise.**
L1 → L2 (400/900, VDDC 900 → 1000). Expected: 1679 → 10080 MB/s.

**Phase E — retest the caps.**
Only now does the original Phase C of the watermark investigation become
meaningful. `bandwidth.cpp` needs no change: its watermarks derive from
`gInfo->memoryClockFrequency` and follow the raised clocks automatically.
Then re-derive or remove the per-chip caps and retest 4K@60 on Barts —
noting that the 4K@60 DP test remains blocked on a certified DP cable.

## 8. Phase A results — Barts HD 6850, 2026-07-31

Implemented in `powerplay.cpp` as `powerplay_dump_control_tables()` and
`powerplay_dump_target_selection()`, with the level parsing factored out
into `powerplay_read_levels()` so the future write path can reuse it. Both
new functions are read-only; both build warning-clean with
`TRACE_POWERPLAY` on and off.

```
SetEngineClock   frev 1, crev 1
SetMemoryClock   frev 1, crev 1
SetVoltage       frev 1, crev 2
  SetVoltage crev 2 takes usVoltageLevel - millivolts or a phase number
ASIC_Profiling   ABSENT - virtual voltage IDs cannot be resolved

currently on level 1 (99990 / 150000 kHz)
  level 0 reaches max memory but its VDDC is a virtual ID - skipped
WOULD target level 3 (300000 / 1000000 kHz, VDDC 950 mV, VDDCI 1100 mV)
voltage delta: VDDC 950 -> 950 (unchanged), VDDCI 950 -> 1100 (CHANGE)
target needs only a VDDCI write; VDDC stays put
engine-only raise to 300000 kHz is valid at the current VDDC
nothing programmed - Phase A is read-only
```

### What this settles

- **Trap 1 (§4.1) is resolved for this board: `SetVoltage` is crev 2**, so
  the argument is `SET_VOLTAGE_PARAMETERS_V2::usVoltageLevel` — a real
  level, not the crev-1 index. The write path can use the millivolt figures
  the PowerPlay table reports, with
  `ucVoltageMode = SET_ASIC_VOLTAGE_MODE_SET_VOLTAGE`.
- **`SetEngineClock` and `SetMemoryClock` are both crev 1**, i.e. the plain
  `ulTarget*Clock` in 10 kHz units. No version dispatch needed for those.
- **`ASIC_ProfilingInfo` is ABSENT on this board.** This is stronger than
  expected: a virtual voltage ID here is not merely awkward to resolve, it
  is **unresolvable** — the data simply is not present. Skipping
  virtual-ID levels is therefore the only correct behaviour, not just the
  cautious one, and **Barts L0 is permanently out of reach**. Since L0
  offered no display-bandwidth gain over L2 anyway (§2), nothing is lost.
- **The §5 target policy reproduces on hardware**: it lands on level 3,
  correctly skips level 0, and independently identifies the
  engine-only-at-current-VDDC step as the safe first write.

The level dump also now annotates a virtual ID explicitly rather than
printing it as `65281 mV` — the exact presentation that caused it to be
misread when first seen.

### Loose ends from Phase A

- `powerplay_read_levels()` traces its `PowerPlay table X.Y, N bytes` line
  once per caller, so it appears twice per probe. Harmless, and it does
  document provenance; worth collapsing if the output ever gets noisier.
- Two early returns in `read_levels()` were originally silent (the DCE gate
  and bad arguments), which made an unrelated build mishap look like a
  parsing failure. Both now trace. **Diagnostics that can decline should
  say why.**

### Still unanswered, and still gating Phase C

Phase A deliberately cannot answer whether `SetMemoryClock` is safe to call
on a live display. That remains the main risk in Phase C.

## 9. Phase B result — Barts, 2026-07-31: PASS

Engine clock only, no voltage write, gated behind the driver setting.

```
clocks (in use): engine 99990 kHz, memory 150000 kHz, display 540000 kHz
WOULD target level 3 (300000 / 1000000 kHz, VDDC 950 mV, VDDCI 1100 mV)
powerplay_apply_engine_clock: raising engine clock 99990 -> 300000 kHz at
    unchanged VDDC 950 mV
powerplay_apply_engine_clock: engine clock now 299980 kHz (verified by read-back)
clocks after raise: engine 299980 kHz, memory 150000 kHz
  bandwidth: available 3360 MB/s, display share 1440 MB/s, mode average 518 MB/s
```

- **Engine clock 3x, from 99990 to 299980 kHz**, with **no voltage change**
  — the target level states the same VDDC that was already applied, so the
  board's own table sanctions the clock.
- **`available` 2559 -> 3360 MB/s (+31%)**, and it is now DRAM-bound
  (8 x 4 x 150000 x 0.7 = 3360) rather than engine-bound. That is the
  predicted figure exactly, and it confirms the mechanism end to end: the
  raise updates `gInfo->engineClockFrequency`, and `bandwidth.cpp`
  recomputes its watermarks from it with no change of its own.
- Read-back reported 299980 against a requested 300000 — the same PLL
  rounding seen in every clock query (0.007%), well inside the 0.5%
  tolerance `powerplay_clock_matches()` allows.
- Desktop stable at 1920x1080 @ 60 Hz for the soak period; app_server
  untouched; no panics or corruption.
- The DP AUX errors visible in the syslog are **pre-existing** — 68
  occurrences before the gate was ever enabled versus 17 after, one batch
  per accelerant init from probing the empty mini-DP connectors. Unrelated
  to the raise; see TODO "Medium" item 0.

### The gate works in both directions

With no settings file present:

```
powerplay_dump_target_selection: nothing programmed - 'raise_clocks' is not
    set in ~/config/settings/kernel/drivers/radeon_hd
```

With `raise_clocks true` in that file, the raise applies. Reverting is
deleting the file or setting it false — no reinstall, no rebuild.

### On the settings mechanism

`load_driver_settings()` / `get_driver_boolean_parameter()` is the standard
Haiku approach and is what essentially every graphics *kernel* driver uses
(vesa, intel_extreme, via, neomagic, radeon, nvidia, matrox). Two things
made it the right choice here specifically:

- **It works from userland**, via libroot's implementation, which matters
  because this code deliberately lives in the accelerant. Note that the
  userland path resolves only `B_USER_SETTINGS_DIRECTORY` (libroot carries
  a TODO about also checking the system directory), so the file is
  `~/config/settings/kernel/drivers/radeon_hd` and not the system one.
- **It keeps the feature on one side of the ABI boundary.** Reading the
  setting in the kernel driver instead would have meant passing the flag
  through `radeon_shared_info` — the one genuine cross-binary struct — and
  that would have put the whole feature in lockstep with the kernel binary
  for no benefit.

No in-tree *accelerant* uses `load_driver_settings`; the closest precedent
is the old `radeon` accelerant's `settings.cpp`, which rolls its own
`BFile` reader and carries a comment warning that app_server may start
before a user context exists. That concern does not bite here — the
accelerant is already being loaded from
`~/config/non-packaged/add-ons/accelerants/`, which proves the user config
directory is resolvable at accelerant init.

### What Phase B does not tell us

Nothing about memory clock, which is the change that actually matters for
bandwidth on a board where DRAM binds, and nothing about voltage. Phase C
remains the first genuine voltage write, and the open question about
`SetMemoryClock` on a live display is untouched.

## 10. Phase C result — Barts, 2026-07-31: memory reclocking does not work

Attempted twice, with different preconditions. **The engine raise and the
voltage write both work; the memory clock does not move.**

```
raising VDDCI 950 -> 1100 mV before any clock
setting voltage type 4 to 1100 mV via crev 2          <- accepted
raising engine clock 99990 -> 300000 kHz at unchanged VDDC 950 mV
engine clock now 299980 kHz (verified)                <- works
raising memory clock 150000 -> 1000000 kHz
memory read-back says 150000 kHz, wanted 1000000 kHz - the memory clock did not take
restoring VDDCI to 950 mV                             <- rolled back
```

`SetMemoryClock` **returns success and the clock never moves.** Not a hang,
not corruption — a silent no-op. That answers the question §8 left open:
calling it on a live display is not dangerous here, it is merely futile.

### Attempt 2 added the precondition the reference driver uses, and it did not help

`evergreen_pm_prepare()` sets `EVERGREEN_CRTC_DISP_READ_REQUEST_DISABLE` on
every enabled CRTC before any clock change, and `evergreen_pm_finish()`
clears it after — scanout must not be fetching while the memory controller
is retimed. That looked like exactly the missing step, so
`powerplay_scanout_requests_disable()` / `_restore()` now bracket the write
(saving and restoring the whole `CRTC_CONTROL` so no other field is
disturbed).

**Same result.** So the missing ingredient is not scanout parking.

### Why, most likely

Northern Islands boards carry an SMC, and Linux drives memory reclocking on
Barts/Turks/Caicos through DPM (`btc_dpm.c`) — which uploads MC microcode
and reprograms `MC_ARB` timings around the change. `btc_asic` *does* still
wire `radeon_atom_set_memory_clock`, but that entry belongs to the older
profile-based PM path, which is not what actually reclocks memory on these
parts in practice. GDDR5 also needs a retraining sequence on reclock that
the legacy table has no way to perform.

In short: **memory reclocking on NI is a DPM/SMC-sized project**, not a
legacy-AtomBIOS one, and it is out of scope for this investigation as
framed in §1 ("no SMC firmware").

### What this leaves, and it is still worth having

- **The engine-clock raise works and is the deliverable**: Barts
  2559 -> 3360 MB/s (+31%), no voltage write, verified by read-back,
  stable.
- **Whether that is enough to lift any cap is now the open question.** At
  3360 MB/s available, a 4K@60 mode's 1861 MB/s average fits — it exceeds
  the conservative 30% display allocation (1440 MB/s), but that is exactly
  what forces `PRIORITY_ALWAYS_ON`, which is the mechanism for letting a
  demanding mode win. So a cap may well be liftable on engine clock plus
  correct watermarks alone.
- Testing that on Barts needs a mode above 340 MHz, which means DisplayPort
  and remains blocked on a certified cable. **Caicos is the better test
  vehicle**: its cap is 165 MHz, so 2560x1440@60 at 241.5 MHz is above the
  cap and still inside the 340 MHz HDMI limit.

### Robustness added by the failure

Two things the failed attempt earned:

- **VDDCI is now rolled back when the clock does not take.** The first
  attempt left the interface voltage at 1100 mV for no benefit; since the
  clock never moved, dropping it again is safe and leaves the board as
  found. Confirmed working in the log above.
- The read-back check did its job: it refused to publish a memory clock
  that had not changed, so `bandwidth.cpp` kept computing from the true
  150 MHz and `available` stayed at an honest 3360 MB/s rather than a
  fictional 7680.

## 11. Caicos, 2026-07-31 — sweep 4/4, and the caps are genuine

Caicos XT (`1002:6778`, HD 7470), 2 DRAM channels, **DisplayPort** at 1080p
(the card has no HDMI port — only DVI and DP, and the driver hard-rejects
anything over 165 MHz on DVI because it cannot distinguish dual-link, so DP
is the only usable path for over-cap work on this board).

```
clocks (in use): engine 99990 kHz, memory 154820 kHz, display 540000 kHz
PowerPlay table 5.1, 235 bytes
  level 0: engine 775000 kHz, memory 900000 kHz, VDDC 1075 mV
  level 1: engine 125000 kHz, memory 200000 kHz, VDDC  900 mV
  level 2: engine 400000 kHz, memory 800000 kHz, VDDC  900 mV
running at the LOWEST advertised memory clock (154820 of up to 900000 kHz)
current clocks (99990 / 154820 kHz) match no advertised level
powerplay_apply_target: refusing - current clocks match no advertised level
bandwidth: available 866 MB/s, display share 371 MB/s, mode average 518 MB/s
watermark 16930 ns, line time 14814 ns, priority mark 157 (ALWAYS_ON)
```

### The sweep is 4 for 4

| Card | Cap | Sitting at |
|---|---|---|
| Turks | 250 MHz | lowest |
| Barts | 340 MHz | lowest |
| **Caicos** | **165 MHz** | **below its lowest** |
| Cedar | none | highest |

Every capped card is parked at (or under) its lowest memory clock; the only
uncapped card runs flat out.

### A third pattern: Caicos runs *below* its own lowest advertised level

99990 / 154820 kHz against level 1's 125000 / 200000. The VBIOS has posted a
state that is not in the PowerPlay list at all, so no level matches and
`powerplay_apply_target()` **refuses** — correctly, because the applied
voltage is then unknown. It may well be *below* level 1's 900 mV, in which
case raising the engine clock on the assumption of 900 mV would be
overclocking at undervoltage.

Establishing a known baseline would mean writing VDDC up to level 1's value
first, which is a voltage write and belongs behind the tier-2 gate. Noted,
not implemented.

### The engine raise would gain Caicos nothing anyway

Unlike Barts, **Caicos is DRAM-bound from the start**:

```
dram        = 2 x 4 x 154820 x 0.7 / 10000 =   866 MB/s   <- binds
data_return = 32 x 99990 x 0.8 / 10000     =  2559 MB/s
```

Raising the engine clock to level 2's 400 MHz would take `data_return` to
10240 MB/s and leave `available` at 866. Barts benefited only because it was
*engine*-bound (2559 against a 3360 MB/s DRAM ceiling). So the tier-1 raise
is useful on some boards and worthless on others, and which is which follows
from the two ceilings.

### First `PRIORITY_ALWAYS_ON` seen on real hardware — at plain 1080p

`mode average 518 > display share 371`, and the latency watermark
(16930 ns) actually **exceeds a whole line time** (14814 ns). Caicos is
bandwidth-marginal at 1080p60, which is consistent with it carrying the
lowest cap of the three. The forced-priority path modelled back in Phase B
finally fired, and the desktop is clean with it.

### The over-cap test is not reachable on this board, and would fail anyway

- **Not reachable:** the attached monitor's EDID tops out at 1920×1080@60
  (148.5 MHz), *under* the 165 MHz cap. `screenmode --list` offers 40 modes
  and none exceed the cap, so `ignore_pixel_clock_cap` has nothing to let
  through. Haiku has no custom-modeline facility to synthesise one.
- **Would fail anyway, by arithmetic:** 2560×1440@60 needs
  `2560 x 4 x 1000 / 11262 ns` ≈ **909 MB/s against 866 MB/s available**. No
  watermark or priority tuning fixes a mode that exceeds the DRAM ceiling.

### Conclusion for the whole investigation

**The per-chip caps are genuine bandwidth limits at the clocks these boards
are parked at, and the engine-clock raise is not enough to lift them.**

- Caicos: DRAM-bound, so tier 1 buys nothing; 1440p is arithmetically out of
  reach at 155 MHz memory.
- Barts: tier 1 gives +31%, but testing above its 340 MHz cap needs DP over
  340 MHz and remains blocked on a certified cable.
- Lifting a cap needs **memory reclocking**, which §10 established does not
  work on Northern Islands through the legacy AtomBIOS path.

So the honest end state: the watermark programming (shipped) and the engine
raise (opt-in) are both real improvements to how the driver treats these
boards, and neither lifts the caps. **The caps stay until someone does the
DPM/SMC work.**

## 12. Open questions

- ~~**Which `SetVoltage` crev do our boards report?**~~ **Answered for
  Barts: crev 2** (millivolt/level form, not the index form) — §8. Still to
  be checked on Turks, Cedar and Caicos; the write path must dispatch per
  board rather than assume crev 2 everywhere.
- **Does `SetMemoryClock` succeed on a live display?** Changing memory clock
  usually requires the display engine to be quiesced; AtomBIOS may handle
  this internally via `MC_Synchronization`, or it may produce visible
  corruption or a hang if called mid-scanout. The safe assumption is that it
  must be done with the CRTC blanked, or before the first mode set. Phase C
  must establish this before it is trusted.
- **Is a one-shot raise stable without the SMC?** Northern Islands boards
  have an SMC that Linux uses for DPM. We are not using it; the assumption
  is that a static legacy-path clock/voltage set is exactly what pre-DPM
  drivers did and remains valid. Unverified.
- **Does the level need re-applying after DPMS off/on or a mode change?**
  Unknown. Phase B should check by reading the clocks back after a DPMS
  cycle.
- ~~**Caicos** is still unmeasured~~ **Measured (§11): parked *below* its
  lowest advertised level, DRAM-bound, and 1440p is arithmetically out of
  reach at its parked memory clock.**
