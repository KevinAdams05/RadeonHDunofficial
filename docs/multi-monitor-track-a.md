# Track A (span / clone) — implementation log

Working log for the driver-only multi-monitor work. The design and the
scope argument live in
[`multi-monitor-analysis.md`](multi-monitor-analysis.md); this file records
what has actually been built, what it needs from the hardware, and the
results as they come in.

Branch: `radeonhd/multi-monitor/v0.7.0`. Target release 0.7.0.

| Milestone | State |
|---|---|
| A1 — clone / mirror | **implemented and hardware-verified** (Caicos, DCE 5, 2026-07-31) |
| A2 — horizontal span | not started |
| A3 — vertical span, mismatched heads | not started (also gated on power management) |

---

## 1. Milestone A1 — clone / mirror

### What it does

`radeon_set_display_mode()` no longer hardcodes `crtcID = 0`. It now picks a
set of heads, programs each one with the same mode, and blanks any attached
head it did not pick.

Off by default. Set `clone_displays true` in
`~/config/settings/kernel/drivers/radeon_hd` to turn it on — the same
mechanism and the same reasoning as the power-management gates: a second
head that mode-sets badly costs the user the working display they already
had, so bring-up stays opt-in until the hardware matrix is covered. There is
no reinstall and no rebuild in the loop; edit the file and restart
app_server.

### Head selection

`select_clone_heads()` decides. Head 0 is always driven. A second head joins
only if **all** of these hold, and is silently left dark otherwise:

| Condition | Why |
|---|---|
| `clone_displays` is set | Opt-in during bring-up (above) |
| `dceMajor >= 4` | `pll_pick()`'s pre-DCE-4 path ends in an unconditional `pll->id = ATOM_PPLL1` with no allocator, so two heads would both claim PPLL1. Real allocator = §6.4 of the analysis |
| Head is `attached` | `detect_displays()` already probes and populates both `gDisplay[]` slots |
| `is_mode_supported_on_display(mode, id)` | Clone drives every head from one `display_mode`, so the second monitor's EDID ranges and its connector's pixel-clock ceiling both have to accept it |

Never failing a mode set because a secondary head could not join is the
degradation rule the BeOS-era `radeon` driver applied in
`Radeon_VerifyMultiMode()`, and it is deliberate here too.

### Two prerequisite bugs found while implementing it

Both were invisible with one head, and both would have looked like span bugs
later. This is the same pattern the watermark work hit with the line-buffer
split field position, and the third time §7 of the analysis has been right
about that.

**1. The PLL allocator never released assignments.** `pll_pick()` allocates
out of `pll_usage_mask()`, which is built from the PLL ids still recorded on
`gConnector[]` — and nothing ever cleared them, so the mask only ever grew.
With one head that was harmless: the allocator alternated between `PPLL1`
and `PPLL2` across successive mode sets, because each pass saw only the
other one as taken. With two heads it is fatal — the first mode set takes
`PPLL1` and `PPLL2`, and the second finds both "in use", gets
`ATOM_PPLL_INVALID` back for head 0, and programs a head with no PLL.

Fixed by `release_pll_assignments()`, called once at the top of each mode
set. A mode set reprograms every head it drives, so no PLL is genuinely in
use at that point. Side effects worth knowing: single-head mode sets now
land on `PPLL1` every time instead of alternating (first boot is unchanged,
since the mask started empty either way), and the DCE 6.1+ DisplayPort
sharing path in `pll_pick()` now only ever sees assignments made during the
current pass, which is what it wanted in the first place.

**2. `is_mode_supported()` answered for display 0 only.** The connector-type
pixel-clock ceiling and the EDID frequency ranges are properties of the
monitor that is plugged in, not of the card, so the answer differs per head
as soon as two are driven. Split into
`is_mode_supported_on_display(mode, crtid)` with the public
`is_mode_supported()` as the head-0 wrapper that `create_display_modes()`
keeps calling — the mode *list* is still built from head 0's EDID, which is
correct for Track A (one `display_mode` for the whole desktop).

### Other behavior changes

- **`radeon_dpms_set_hook()` iterates.** It drove CRTC 0 only. It now
  applies DPMS to every head the last mode set actually lit, tested via
  `display_is_lit()` — `attached && currentMode.timing.pixel_clock != 0`,
  the same test `bandwidth_crtc_is_active()` uses, and for the same reason:
  `powered` is set for every attached display before any mode is set, so it
  cannot tell "driving a monitor" from "a monitor is plugged in". A
  pre-mode-set DPMS call still falls back to head 0 so it is not dropped.
- **An attached head that is not driven is now explicitly blanked.**
  Previously a second monitor was left in whatever state the VBIOS or GOP
  left it. This is what makes turning clone back off — or landing on a mode
  the second monitor cannot take — leave that monitor dark rather than
  scanning out a stale timing. Visible consequence: on a machine where
  firmware lit both panels, the second one now goes dark at the first mode
  set unless `clone_displays` is on.
- **Undriven heads get `currentMode` cleared**, so `bandwidth_update()`
  cannot keep giving a dark head half the line buffer.
- **HDMI infoframe programming and both register dumps now run per lit
  head**, after the encoder output lock is released — the same order the
  single-head path always used.

### What did *not* change

No mode-list changes, no `B_SCROLL`, no `virtual_width` change, no pitch
change, no new hooks. Both heads get the same mode and the same
`grphPrimarySurfaceAddr`, both viewports at (0,0). app_server still sees one
screen of one size, exactly as before. That isolation is the point of doing
clone first: it tests "can we light two CRTCs at once" without touching a
single framebuffer question.

---

## 2. How to test A1

Needs a card with two usable outputs and two monitors. Per
`TestHardware/` and the Bonaire notes, the Turks HD 6570 (DCE 5) and the
Bonaire R7 260X (DCE 8) both qualify; Cedar HD 5450 is the DCE 4 datapoint.

1. Install the 0.7.0 test `.hpkg` (kernel driver **and** accelerant — Barts
   and any other card whose upstream table entry is gated needs the fork's
   kernel driver, not just an accelerant override).
2. Boot with both monitors connected. Confirm the baseline first: **head 0
   works, head 1 is dark.** Grab `/var/log/syslog`.
3. Create `~/config/settings/kernel/drivers/radeon_hd` containing
   `clone_displays true`, then restart app_server (or reboot).
4. Confirm both monitors show the same image.
5. Pull the syslog and check, in order:
   - `select_clone_heads: cloning across 2 head(s)`
   - a `set_mode_on_head` line per head, each with a **different** PLL id
     (this is the regression the release fix exists for)
   - `bandwidth_update` reporting **2** active heads, and
     `DC_LB_MEMORY_SPLIT` programmed as a half-buffer split per CRTC rather
     than the whole buffer for CRTC 0
   - `mode set complete on 2 head(s)`
6. Change resolution in Screen preferences a few times, then check the
   syslog again for `Unable to find a PLL!` — absence of that line across
   repeated mode sets is the PLL-release fix working.
7. Turn `clone_displays` back off, restart app_server, and confirm head 1
   goes **dark** rather than keeping a stale image.
8. DPMS: let the screen blank, wake it, and confirm both heads come back.

Mismatched monitors are a useful negative test: a mode only one of them
accepts should produce `display 1 cannot take …  - leaving it dark` and a
working head 0, never a failed mode set.

### Things most likely to go wrong

- **DIG/UNIPHY collision.** `encoder_pick_dig()` returns a link-based index;
  two connectors that resolve to the same DIG would fight. Expect this on
  cards where DVI-I and HDMI hang off one UNIPHY. Symptom: second head dark
  or corrupt, with both heads reporting the same DIG in the syslog.
- **Bandwidth.** Two heads roughly double average scanout demand against
  boards that are parked in their lowest power state. Dual 1080p fits
  (~1036 MB/s); see §7.1 of the analysis for the table. A head that starves
  looks like tearing or a garbage band, not a mode-set failure.
- **PLL exhaustion on a 2-PLL part with a third clock needed.** The TODO in
  `pll_next_available()` about sharing PLLs at identical clock rates becomes
  reachable here — and clone is its worst case, because both heads want the
  *same* clock. If `Unable to find a PLL!` shows up on a fresh boot rather
  than on a re-set, that is this, not the release bug.

---

## 2a. Results — Caicos XT, DCE 5, 2026-07-31

**A1 clone works.** Both monitors showed the same desktop on the first boot
that had `clone_displays` set. Every step of §2 passed.

Test rig: Radeon HD 7470/8470 OEM (**Caicos XT, `1002:6778`**, DCE 5, 2 DRAM
channels) on the Supermicro X11SSH-LN4F, hrev59697. Two 1080p monitors, one
on DisplayPort and one on DVI-I. `raise_clocks true` was already set from the
power-management work; `clone_displays true` added for this run.

This is the fork's **first Caicos datapoint** and the first two-head one. Note
it is a *different* card from the Turks/Bonaire the plan expected — Caicos was
listed as untested in the watermark investigation.

### Connector topology — only two physical ports

`connector_probe` reports three paths for two connectors:

| Connector | Port | Encoder | i2c / HPD gpio | DIG |
|---|---|---|---|---|
| #0 | DisplayPort | UNIPHY1, enum 2 (link B) | 0x93 / 0x2 | 3 |
| #1 | DVI-I digital | UNIPHY, enum 1 (link A) | 0x92 / 0x4 | 0 |
| #2 | DVI-I **analog** | DAC1 (TV DAC), CRT1 | 0x92 / 0x4 | — |

Connectors #1 and #2 share both GPIO pins because they are the digital and
analog halves of one physical DVI-I connector. #2 reads the *same* monitor's
EDID over the shared DDC line and is then correctly rejected by
`encoder_analog_load_detect()`. It is not a second monitor, so **dual-head on
this card means DP + DVI, with no third option.**

The two heads land on **different UNIPHYs**, so the DIG/UNIPHY collision
predicted first in §2 did not occur. Worth keeping in mind that this card
cannot demonstrate that failure mode either way.

### What the syslog confirmed

- `select_clone_heads: cloning across 2 head(s)`
- `set_mode_on_head: display 0 → pll 2`, `display 1 → pll 0` — **different
  PLLs**, the regression `release_pll_assignments()` exists for
- `bandwidth_update: 2 active head(s)`, and the line buffer halved correctly:
  **`line buffer 8192 px` with two heads, `16384 px` with one** (the
  `D1HALF_D2HALF` vs `D1_ONLY` partitions)
- `mode set complete on 2 head(s)`
- **Zero `Unable to find a PLL!` across five consecutive mode sets**, with the
  assignment stable at pll 2 / pll 0 every time. The release bug would have
  surfaced here.
- DPMS iterates: powerdown blanks CRTC 0 *and* CRTC 1, powerup restores both.
- `clone_displays false` + app_server restart → `blanking undriven head 1`,
  `mode set complete on 1 head(s)`, and the second monitor goes dark rather
  than holding a stale image.

### The one visible defect is bandwidth, and it is the known PM blocker

At **2 × 1920×1080 the DVI head is garbled** while the DP head is clean. The
driver diagnoses it itself:

```
powerplay: running at the LOWEST advertised memory clock
           (154820 of up to 900000 kHz) - scanout bandwidth is at its floor
bandwidth: available 866 MB/s, display share 371 MB/s, mode average 518 MB/s
latency 39987 ns (NOT hidden by line buffer)
```

Two heads at 518 MB/s each is 1036 MB/s against 866 MB/s available. Confirmed
causally by walking modes — the driver's own "hidden by line buffer" flag
tracks the visible garbling exactly:

| Mode | pixel clock | mode average | 2-head total | latency hidden? | visually |
|---|---|---|---|---|---|
| 1024×768 | 64996 kHz | 198 MB/s | 396 | hidden | clean |
| 1280×1024 | 107964 kHz | 327 MB/s | 654 | hidden | clean |
| 1600×900 | 120331 kHz | 363 MB/s | 726 | hidden | clean |
| 1920×1080 | 148500 kHz | **518 MB/s** | **1036** | **NOT hidden** | **DVI garbled** |

So the **dual-head ceiling on this board is 1600×900**, and it is set by the
memory clock, not by the clone code: 154.8 MHz of an advertised 900 MHz. That
is [`power-management-investigation.md`](power-management-investigation.md)'s
finding — `raise_clocks` lifts only
the engine clock, and memory reclocking silently no-ops on Northern Islands
because it needs DPM/SMC. Nothing in A1 can work around it.

This also **revises §7.1 of the analysis downward.** That table assumed
~1680 MB/s at PowerPlay level 1 and concluded 2×1080p (≈1036 MB/s) fits. On a
board parked at its memory floor there is only 866 MB/s, so 2×1080p does *not*
fit. The shape of the argument holds; the specific budget is per-board and has
to be read from `bandwidth_program_watermarks` rather than assumed.

### One gotcha that cost time: a sleeping DP sink

The first boot with both monitors attached failed to detect the DP monitor at
all — `dp_aux_speak: dp_aux channel flags not zero!` twelve times, then
`ddc2_dp_read_edid1: error reading EDID data at index 0`. The DVI monitor came
up fine, so it looked like DP-vs-DVI interference.

It was not. The DP monitor had gone to sleep, and a sleeping sink makes the
AUX engine report flag errors rather than a clean timeout. **A reboot that
woke the monitor fixed it** and DP EDID then read on the first try. Worth
knowing because the symptom points at the driver: the failure was 100%
reproducible within that boot, appeared only in the two-monitor case, and
never once reported the timeout you would expect from an absent sink.

`dp_aux_dump_state()` was added while chasing this — it decodes AUX_SW_STATUS
after AtomBIOS reports a failure, because AtomBIOS collapses every AUX error
into one status byte and cannot distinguish "sink never answered" from "engine
was never brought up". **It has not yet fired on real hardware**, since AUX
started working before it was deployed; the register offsets are from the
DCE 5–8 AUX block layout and are still unverified in practice.

---

## 3. Not done yet

- Identical-clock PLL sharing (`pll_next_available()` TODO) — clone is
  exactly the case that wants it.
- PLL allocator for DCE ≤ 3, or a decision to leave those parts single-head
  permanently. The device table does claim DCE 2/3 cards (R600/RV610/RV630
  and friends), so this is a real gap rather than a theoretical one; they
  are currently untested for anything else either.
- `MAX_DISPLAY` is still 2. `init_registers()` already handles CRTC 0–5 and
  all six offsets are defined, so raising it is display/connector matching
  work rather than register plumbing — but nothing above two heads is in
  scope for A1.
- Per-CRTC LUT/gamma in `B_CMAP8` is untested with two heads. Both loaders
  are already per-CRTC and both get called, so it should be consistent by
  construction. (`display_dce45_crtc_load_lut` was observed running for
  crtcID 0 and 1 on the Caicos run, so both do get invoked.)

### Opened by the Caicos run

- **Identical-clock PLL sharing is now the top A1 follow-up, and it is a
  bandwidth question rather than a correctness one.** Both heads want the same
  clock in clone, and the allocator spends two PLLs on it (pll 2 + pll 0).
  That works on a 2-PLL part with 2 heads but leaves nothing for a third.
- **`dp_aux_dump_state()` has never fired.** Validate it by detaching the DP
  monitor and restarting app_server — AUX should then fail and the dump should
  print a plausible `AUX_SW_STATUS` with `RX_HPD_DISCON` set. Until that runs,
  treat its register offsets as unverified.
- **A sleeping DP sink is indistinguishable from a driver fault** in the
  current logging (§2a). Reading HPD sense before attempting AUX, and saying
  so in the log, would have saved the whole detour. Cheap and worth doing.
- **The 2×1080p garbling is the first visible symptom the power-management
  work would fix.** Previously the memory-clock floor only showed up as
  pixel-clock caps on single-head modes; it now costs a working dual-1080p
  desktop, which makes DPM/SMC support materially more valuable.
- Two-head testing on a card whose DVI-I and HDMI share a UNIPHY is still
  outstanding — Caicos cannot exercise the DIG collision path, since its two
  heads sit on different UNIPHYs.
