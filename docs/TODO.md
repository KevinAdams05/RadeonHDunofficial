# RadeonHD (unofficial) — Driver TODO / Latent-Bug List

**Actionable work only.** Completed items are not kept here — they are
written up in [`technical-documentation.md`](technical-documentation.md)
under the version that shipped them. Work that is finished but not yet
released is under
[0.6.6 — Pending Release](technical-documentation.md#066--pending-release)
there, so it is not lost between releases.

The list is maintained by scanning the *effective* RadeonHD codebase — the
fork-carried files in `RadeonHD/src/.../radeon_hd/` and
`RadeonHD/headers/.../radeon_hd/` that override upstream, plus the
upstream-only `radeon_hd` files that are still pulled into the build from
the Haiku source tree — for explicit source markers (`TODO` / `FIXME` /
`XXX` / `HACK` / `WORKAROUND` / `#if 0`) and for latent bugs (unchecked
allocations/mappings, error-path leaks, format-specifier mismatches,
unbalanced module refcounts, register-write/read-back hazards), together
with the open issues recorded in `CHANGELOG.md` and `README.md`.
Style/formatting findings are excluded — `scripts/style-check.py` covers
those and the release gate in `scripts/package.sh` enforces them.

Line numbers are against the working tree as last verified 2026-07-31 and
may drift as the code changes.

---

## High priority (likely user-visible)

### 1. Per-chip pixel-clock caps need memory reclocking (DPM/SMC)
`src/.../accelerants/radeon_hd/mode.cpp:481-489` — Caicos 165 MHz, Turks
250 MHz, Barts 340 MHz. The caps are genuine memory-bandwidth limits, and
the investigation into lifting them is complete: they are caused by the
boards sitting in the lowest power state their VBIOS posted, not by
unprogrammed display arbitration.

Both in-scope mitigations are done and neither lifts a cap. Display
bandwidth arbitration is implemented for DCE 4/5 (`bandwidth.cpp`) and
verified on Turks and Cedar. The opt-in engine-clock raise
(`raise_clocks`) works and gives Barts +31%, but only helps boards whose
bottleneck is the engine — the capped boards are DRAM-bound. Memory
reclocking through the legacy AtomBIOS `SetMemoryClock` path **silently
no-ops on Northern Islands**: the call succeeds and the read-back never
moves. See
[`power-management-investigation.md`](power-management-investigation.md)
§10 and §11.

**The caps stay until someone does the DPM/SMC work** — driving the
board's SMC firmware the way Linux's `radeon` DPM does, rather than the
one-shot legacy command tables. That is a substantially larger project
than anything the fork has taken on so far. Prerequisites that must be
settled before it starts:

- **`SetVoltage` table revision per board.** Barts reports crev 2 (a
  millivolt/level form). Turks, Cedar and Caicos are unchecked; crev 1
  takes a voltage *index* instead, so the write path must dispatch per
  board rather than assume crev 2. See
  [`power-management-investigation.md`](power-management-investigation.md)
  §4.1.
- **`usVDDC` is sometimes not a voltage.** Values in the
  `ATOM_VIRTUAL_VOLTAGE_ID` range (`0xff01`–`0xff08`) are leakage-binned
  placeholders that must be resolved from `ASIC_ProfilingInfo`; they must
  never be programmed as millivolts (`powerplay_voltage_is_virtual()`
  guards this today). §4.2.
- **Whether a memory-clock change is safe on a live display**, or requires
  the CRTC blanked / must happen before the first mode set. §12.
- **Whether a raise survives a DPMS off/on cycle or a mode change**, i.e.
  whether it needs re-applying. §12.

Separately, re-deriving the Barts cap empirically is **blocked on a
certified DP cable** — the over-cap modes need more than 340 MHz over
DisplayPort, and the cable on hand fails on Turks too, so it is a cable
fault rather than a driver limit. `ignore_pixel_clock_cap` in the driver
settings exists to make the caps advisory for exactly that test; it is off
by default and is not a supported configuration.

### 2. DCE 10–12 bring-up and testing (0.6.5 enabled them untested)
0.6.5 enabled the GCN3+ block of the device table in
`src/.../kernel/drivers/graphics/radeon_hd/driver.cpp`, so the driver now
claims Tonga, Fiji, Carrizo/Stoney, Polaris 10/11/12 (incl. RX 470–590)
and Vega/Raven. **None of these families has been verified on real
hardware** — the driver attaching is not a working display. Work needed:

- Test at least one board per DCE generation (10.0/10.1, 11.0/11.1/11.2,
  12.0/12.2) and record results in the README's tested-hardware table.
- The Polaris (RX 580) test is the first one queued; the capture helpers
  `scripts/rx580-capture.sh` (read-only Haiku-side capture) and
  `scripts/rx580-linux-atombios.sh` (Linux-side AtomBIOS reference dump)
  are written and waiting on the hardware.
- Expect per-generation register/offset divergence of the kind DCE8
  needed in 0.6.4 (HPD id lookup) — the AFMT/bandwidth/watermark paths in
  particular are only implemented for DCE 4/5.
- Raven is deliberately refused at runtime despite being in the table (its
  display engine is DCN 1.0, not DCE 12); confirm the guard still fires
  before blaming the table.

---

## Medium (correctness issues unlikely to bite often)

### 3. DP aux probe noise on unconnected / asleep ports
`src/.../accelerants/radeon_hd/displayport.cpp:84`. Every DPCD read on a
connector with no awake sink logs `dp_aux channel flags not zero!` and
then burns a 7-attempt timeout, roughly 34 wedged transactions per boot on
a card with two empty DP connectors.

This is understood and is quality-of-life only: "flags not zero" is the
driver's label for AtomBIOS `ProcessAuxChannel` **reply status 2** — no or
deferred response from the sink — not a stuck driver state machine. It
reproduces on a *connected* DP port whose monitor has its input switched
elsewhere, because DP EDID rides the aux channel and needs the sink awake
(unlike HDMI/DVI's passive EEPROM). Work: log one line per connector per
detect pass instead of 7-attempt spam per byte, and early-out after the
first no-response on a connector within a pass.

### 4. `validate_bars()` stores only the low dword of a 64-bit BAR0
`src/.../kernel/drivers/graphics/radeon_hd/driver.cpp:894`
(`gDeviceInfo[found]->registers = info->u.h0.base_registers[0];`). The
high dword is dropped. `radeon_hd_init()` recomputes and remaps the
register window correctly, so the stale value is currently unused — but it
is a latent trap for any future code that reads `info.registers` before
the remap. Fix: compose the 64-bit address here too (as `validate_bars()`
already does for its own checks), or drop the assignment and document that
`info.registers` is only valid after `radeon_hd_init()`.

### 5. Unbalanced `put_module(B_AGP_GART_MODULE_NAME)` on the no-devices path
`src/.../kernel/drivers/graphics/radeon_hd/driver.cpp:912`. `init_driver()`
puts the AGP GART module in its `found == 0` cleanup, but nothing in
driver.cpp ever `get_module`s it — a put without a matching get. A
pre-existing upstream artifact; harmless on current Haiku but technically
corrupts the module refcount. Fix: remove the stray `put_module` (or add
the corresponding get if AGP is actually meant to be acquired).

### 6. `new RingQueue` allocation not null-checked; return value ignored
`src/.../accelerants/radeon_hd/gpu.cpp:846-852` (`radeon_gpu_ring_setup`).
The `new RingQueue(1024 * 1024, ...)` result is stored without a NULL
check and would later be dereferenced; the function's status is discarded
at its `accelerant.cpp` (upstream-only) call site. Low real-world impact
because the GFX ring is an unused stub (item 10), but it is an unchecked
allocation on the init path. Fix: return `B_NO_MEMORY` on NULL and check
at the call site, or guard the whole ring path out until it does
something.

---

## Low / cleanup (stale TODOs, dead code, polish)

### 7. Cayman pixel-clock cap pending hardware
`src/.../accelerants/radeon_hd/mode.cpp:474-475` (comment), cap table at
`:481-489`. Cayman shares Barts' 256-bit-bus / DCE-5 linear-scanout
architecture but is intentionally left uncapped because no Cayman card has
been tested. Once hardware is available, add an `else if (info.chipsetID
== RADEON_CAYMAN)` clause — or validate that it genuinely needs no cap.

### 8. Debug-only format-specifier nits in TRACE strings
`src/.../accelerants/radeon_hd/mode.cpp:653`, `:657`
(`"brightness level = %lx"` for a `uint32`), `mode.cpp:545`
(`"MODE: %d ..."` for `mode->timing.pixel_clock`, a `uint32`), and
`driver.cpp:768` uses `%d` for `PCI_BAR_FB` while `:777` uses `B_PRIu32`
for the equivalent `mmioBar`. Diagnostics only, no functional effect, but
`%lx`/`%d` for `uint32` mismatches the Haiku `B_PRIx32`/`B_PRIu32`
convention and can mis-print on some builds.

### 9. Large `#if 0` Linux-derived dead blocks in `gpu.cpp` ring/MC code
`src/.../accelerants/radeon_hd/gpu.cpp:854`, `:911`, `:933`, `:950`,
`:956`, `:972`, `:987` — commented-out reference code from the Linux
radeon ring/command-processor bring-up, carrying `dev_priv` / `dev->sg`
references and AGP TODOs the driver does not use. Either finish the
ring/CP work or strip the scaffolding to reduce noise.

### 10. `radeon_gpu_ring_boot()` is a stub that returns before its body
`src/.../accelerants/radeon_hd/gpu.cpp:866`. The function logs `"%s: TODO"`
and returns `B_OK` immediately, leaving ~120 lines of unreachable ring-init
code below it. Document it as a deliberate no-op or remove the unreachable
tail.

### 11. Driver-table NAVI granularity TODO
`src/.../kernel/drivers/graphics/radeon_hd/driver.cpp:622` ("We might need
to split NAVI into NAVI10, NAVI12, etc"). Many entries share
`RADEON_NAVI`; per-variant behavior would need a finer chipset enum. No
current functional impact — Navi is DCN, out of scope for this AtomBIOS/DCE
driver, and its table block stays behind `#if 0`.

### 12. Multi-monitor: remaining single-CRTC TODOs
**In progress on `radeonhd/multi-monitor/v0.7.0`** — Track A milestone A1
(clone/mirror) is implemented and awaiting a hardware test; see
[`multi-monitor-track-a.md`](multi-monitor-track-a.md) for what landed and
[`multi-monitor-analysis.md`](multi-monitor-analysis.md) for the design.
Track B needs Haiku-side work and stays blocked on unmerged Gerrit 329.

Still display-0-only after A1, all of it mode-list and query surface rather
than hardware bring-up: `src/.../accelerants/radeon_hd/mode.cpp:56`
(`create_mode_list()` builds from head 0's EDID — correct for Track A,
wrong for Track B), `:82`, `:92`, `:103` (mode count / mode list /
preferred mode), `:123` (`radeon_get_edid_info()`), `:856-859`
(`radeon_set_brightness()` — backlight assumes head 0 is the panel), and
`display.cpp:1075` ("TODO: shared PLL detected!"). The identical-clock PLL sharing TODO in
`pll_next_available()` becomes reachable with two heads and clone is its
worst case, since both heads want the same clock.

### 13. Stale per-encoder TODO stubs (mostly diagnostic)
`src/.../accelerants/radeon_hd/encoder.cpp:465`, `:1114`, `:2124`
(DVO setup / hardcoded NTSC TV / DVO DPMS stubs that only log) and
`displayport.cpp:503`, `:581`, `:1031` ("this surely can be cleaned up").
Unsupported-path stubs that emit a TRACE and continue. Revisit only if
those encoder types need to be supported.

### 14. Documentation gaps
- `technical-documentation.md` has no sections for **0.6.4** (DisplayPort
  on DCE8, Cape Verde) or **0.6.5** (DCE 10–12 device IDs). Both are
  covered in `CHANGELOG.md` and `fixes-by-version.md`, so the deep-dive
  document is the one that is behind.
- Diagrams still owed for the watermark and power-management write-ups
  beyond the two already committed
  (`scanout-watermark-arbitration.svg`, `powerplay-level-targets.svg`).

---

## Out of scope (needs changes outside the driver tree)

- **Tiled scanout to lift the per-chip pixel-clock caps.** The caps in
  `mode.cpp:481-489` exist because Haiku writes a *linear* PCI-BAR-mapped
  framebuffer. The classic fix — 2D-tiled scanout — requires app_server to
  write pixels in tile order or add a translation layer, which lives in
  app_server, not the driver. Note that the in-driver alternative
  (display-watermark/bandwidth arbitration) has since been implemented and
  did **not** lift the caps; see item 1 for where that leaves them.

- **Haiku PCI ticket #3 — BAR resource assignment.** `validate_bars()`
  (`driver.cpp:745-800`) only *detects and refuses* boards whose BARs the
  firmware left unprogrammed; it cannot assign them. The actual fix is in
  the Haiku PCI bus manager (kernel), out of this fork's scope. The guard
  itself is the correct driver-side mitigation.

- **AtomBIOS area kept writable (`#19348`).** In `radeon_hd.cpp`, the rom
  area "should" be `B_KERNEL_READ_AREA | B_CLONEABLE_AREA` but AtomBIOS
  calls fail when it is read-only, so the `set_area_protection()` is
  commented out (XXX). The root cause is Haiku ticket #19348 (the AtomBIOS
  parser writing into nominally-RO memory), a kernel/shared-parser issue
  rather than a driver-local fix.
