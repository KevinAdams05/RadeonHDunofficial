>[!NOTE]
>An LLM was used to aid in development of this code.

# RadeonHD (unofficial) — Driver TODO / Latent-Bug List

Compiled 2026-06-04 by scanning the *effective* RadeonHD codebase — the
fork-carried files in `RadeonHD/src/.../radeon_hd/` and
`RadeonHD/headers/.../radeon_hd/` that override upstream, plus the
upstream-only `radeon_hd` files that are still pulled into the build from
the Haiku source tree. The list was built from (1) explicit source
markers (`TODO` / `FIXME` / `XXX` / `HACK` / `WORKAROUND` / `#if 0`),
(2) a manual read for latent bugs (unchecked allocations/mappings, error-
path leaks, format-specifier mismatches, unbalanced module refcounts,
register-write/read-back hazards), and (3) the known open issues recorded
in `CHANGELOG.md` and `README.md`. Style/formatting findings are
deliberately excluded — a separate style audit covers those. Items in
files the fork does **not** carry are flagged "(upstream-only file)".

Line numbers are against the current working tree as of the compile date
and may drift as the code changes.

---

## High priority (likely user-visible)

### 1.✅ Area leak on `radeon_hd_init()` error paths — FIXED (unreleased, 2026-06-04)

**Fixed** by keeping the `AreaKeeper`s armed until a single detach point
just before the success return; every error path now cleans up via RAII.
The same pass also fixed two `info.rom_area` leaks inside
`mapAtomBIOS()` (rom-size-zero and copied-rom-invalid paths). Original
finding kept below for reference.
`src/.../kernel/drivers/graphics/radeon_hd/radeon_hd.cpp:808`,
`:904` (and any later error return). The shared-info, MMIO-register, and
framebuffer areas are created via `AreaKeeper` and then `Detach()`'d
(lines 688, 709, 837), transferring ownership to `info`. But the later
`return B_ERROR` (frame buffer < 8 MiB) and `return B_ERROR`
(no AtomBIOS found) bail out *without* releasing those areas, and the
caller `device_open()` (device.cpp, upstream-only) only calls
`radeon_hd_uninit()` on the *success* path. Result: every failed init
leaks up to three kernel areas (shared, registers, framebuffer mapping).
Fix: on each error return after the areas are detached, delete the areas
already created (or restructure so the AreaKeepers are only detached at
the single success point).

### 2. Cedar HDMI magenta-stripe data-island bleed (open issue 3a)
`src/.../accelerants/radeon_hd/display.cpp:495-507`
(`display_get_encoder_mode` forces `ATOM_ENCODER_MODE_DVI` for
`VIDEO_CONNECTOR_HDMIA`), with the dormant infoframe machinery in
`hdmi.cpp` / `hdmi.h` and its call site commented out in
`mode.cpp:248-254`. HDMI-A is driven as DVI as the 0.2.0/Phase-1.5
workaround; full HDMI (audio + infoframes) does not work on Cedar because
programming the AVI infoframe + KEEPOUT + packet-generator disables did
**not** suppress the bleed — a Cedar-specific register or sequence is
still missing. Fix direction: identify the missing Cedar register/sequence
(compare against Linux `evergreen_hdmi.c` `dce4`/`dce6` setup ordering),
then re-enable the `hdmi_avi_infoframe_program()` call in `mode.cpp` and
let `display_get_encoder_mode` return `ATOM_ENCODER_MODE_HDMI`.

### 3. Barts 4K@60 DP scanout corruption / provisional 340 MHz cap (open issue 3b)
`src/.../accelerants/radeon_hd/mode.cpp:459-462` (Barts cap at 340 MHz in
`is_mode_supported`). The cap is empirical — 4K@60Hz over DP (533 MHz
pixel clock) is stride-aliased on the HD 6850, but the true ceiling
between 340 MHz and 533 MHz is undetermined, so intermediate modes
(4K@30, 1440p@60, 1080p@144, 3440×1440@60) are being rejected or allowed
only by approximation.

**Updated 2026-07-30 — the watermark theory is done and it was not the
answer.** The display-watermark / line-buffer programming named here as
"the real fix" is now implemented for DCE 4/5 (`bandwidth.cpp`) and
verified on Turks hardware, but it does **not** lift the caps. A clock
probe added in the same pass showed why: the card runs at PowerPlay
**level 1** (100 MHz engine / 150 MHz memory), which allows about
1680 MB/s of DRAM bandwidth, and 4K@60 needs 1861 MB/s of average
scanout. The caps are a genuine bandwidth limit — just one caused by the
card sitting in its lowest power state, not by unprogrammed arbitration.

The board advertises 900 MHz memory at levels 0 and 2, worth roughly
10080 MB/s, so the caps *are* liftable — via **power management**, not
watermarks. Fix direction is now: implement the clock/voltage write path
(target level 2; voltage first, see
`docs/scanout-watermark-investigation.md` §9), then recompute the caps.
`bandwidth.cpp` needs no change for that — its watermarks derive from
`gInfo->memoryClockFrequency` and follow raised clocks automatically.

---

## Medium (correctness issues unlikely to bite often)

### 0. DP aux channel wedge on Turks ("flags not zero") + one-off UI freeze after card swap
Observed 2026-06-04 on the HD 6570 (Turks) immediately after a physical
card swap: one boot reached the desktop with a working cursor but an
otherwise unresponsive UI; the next boot was clean. Syslog shows the
mode set completed normally, no panic/KDL — inconclusive. What IS
reproducible (both the frozen and the clean boot): every DPCD read on
the card's two unconnected DP connectors fails with
`dp_aux_speak: dp_aux channel flags not zero!` followed by a 7-attempt
timeout (`dp_aux_transaction: IO Error`), ~34 wedged transactions per
boot in `displayport.cpp`. Two angles: (a) the aux channel state
machine is never reset/cleared when a transaction aborts — investigate
clearing the channel flags before retrying; (b) repeated polling of
dead aux channels costs 7 timeout cycles per read — cache the failure
per connector or rate-limit so an app_server thread can't stall on it
(candidate explanation for the cursor-alive/UI-frozen symptom). If the
freeze recurs: check whether SSH still works (localizes app_server vs
system-wide) and pull syslog before rebooting.
**Update (same day):** DP validated working on the Turks' other
connector — EDID, 270 MHz × 2-lane link training, all clean, zero aux
errors on the *connected* port. The wedge is confined to probing the
*empty* DP port (aux engine flags apparently never cleared after a
failed transaction). Severity accordingly low: boot-time noise + 7
wasted timeout cycles per probe of an unconnected port. Fix direction
unchanged: clear/reset the aux channel flags before (or after) a
failed transaction in `dp_aux_speak`, and consider caching
"nothing there" per connector per detect pass.
**Update 2 (Barts DP testing):** "flags not zero" is the driver's
label for AtomBIOS ProcessAuxChannel **reply status 2** — i.e. "no /
deferred response from the sink", not a stuck driver state machine.
Reproduced on a *connected* DP port whose monitor had its active
input switched to another machine (DP EDID rides the aux channel and
needs the sink awake, unlike HDMI/DVI's passive EEPROM): zero
monitors found, no display until the monitor's input was switched
back and the box rebooted — then EDID + 270 MHz × 2-lane training
came up instantly. Remaining work is therefore quality-of-life only:
quieter logging (one line per connector per detect pass instead of
7-attempt spam per byte), and possibly an early-out after the first
no-response on a connector within one pass. The freeze correlation
from the original report is considered coincidental.

### 4. ✅ `mapAtomBIOSACPI()` trusts the VFCT table without bounds checks — FIXED (unreleased, 2026-06-04)

**Fixed**: table size (per its own `TableLength`), image-header offset,
image length, the 0x48 header pointer, and the signature read are all
validated before use; failures return `B_BAD_DATA` with specific syslog
diagnostics and fall through to the other bios-read methods. The
copied-rom-invalid leak of `info.rom_area` in the same function was
fixed too. Original finding kept below for reference.
`src/.../kernel/drivers/graphics/radeon_hd/radeon_hd.cpp:69-88`.
`vfct->VBIOSImageOffset` is added to the table pointer and `vbios`,
`vhdr`, then `rom`/`romSize` (`vhdr->ImageLength`) are dereferenced with
no validation that the offset/length stay within the ACPI table the
firmware handed us, and `RADEON_BIOS16(rom, 0x48)` plus the
`rom[romHeader + 4]` signature read can index past the mapped region.
A malformed/hostile VFCT could read out of bounds in kernel space. Fix:
validate `VBIOSImageOffset`, `ImageLength`, and `romHeader + 8` against
the table size before dereferencing.

### 5. `validate_bars()` 64-bit register only takes the low dword for `info.registers`
`src/.../kernel/drivers/graphics/radeon_hd/driver.cpp:867`
(`gDeviceInfo[found]->registers = info->u.h0.base_registers[0];`). This
stores only the low 32 bits of BAR0 and ignores the high dword on 64-bit
BARs. In practice `radeon_hd_init()` recomputes and remaps the register
window correctly, so this stale value is currently unused, but it is a
latent trap if any future code reads `info.registers` before the remap.
Fix: either compose the 64-bit address here too, or drop the assignment
and document that `info.registers` is only valid after `radeon_hd_init`.

### 6. ✅ `init_driver()` leaks the `pci_info` allocation on `strdup`/`malloc` failure — FIXED (unreleased, 2026-06-04)

**Fixed**: both failure branches now `free(info)` before breaking, and
the second branch also nulls `gDeviceNames[found]` after freeing it.
Original finding kept below for reference.
`src/.../kernel/drivers/graphics/radeon_hd/driver.cpp:851-859`.
If `strdup(name)` (851) or the `gDeviceInfo[found]` `malloc` (855) fails,
the loop `break`s without `free(info)` — the `pci_info` allocated at
line 826 leaks. (The second case also leaves `gDeviceInfo[found]`
unassigned while `gDeviceNames[found]` was freed, but `info` is the
clear leak.) Fix: `free(info)` before breaking in both failure branches.

### 7. Unbalanced `put_module(B_AGP_GART_MODULE_NAME)` on the no-devices path
`src/.../kernel/drivers/graphics/radeon_hd/driver.cpp:885`. `init_driver`
calls `put_module(B_AGP_GART_MODULE_NAME)` in the `found == 0` cleanup,
but the AGP GART module is never `get_module`'d anywhere in driver.cpp —
a put without a matching get. Likely a pre-existing upstream artifact;
harmless on current Haiku but technically corrupts the module refcount.
Fix: remove the stray `put_module` (or add the corresponding get if AGP
is actually intended to be acquired).

### 8. `new RingQueue` allocation not null-checked; return value ignored
`src/.../accelerants/radeon_hd/gpu.cpp:797-798` (`radeon_gpu_ring_setup`).
The `new RingQueue(1024*1024, ...)` result is stored without a NULL check;
a failed allocation would later be dereferenced. The function is invoked
from `accelerant.cpp` (upstream-only) and its return value is discarded.
Low real-world impact because the GFX ring is otherwise an unused stub
(see item 12), but it is an unchecked allocation on the init path. Fix:
check for NULL and return `B_NO_MEMORY`; check the result at the call
site, or guard the whole ring path out until it does something.

---

## Low / cleanup (stale TODOs, dead code, polish)

### 9. Cayman pixel-clock cap pending hardware (open issue 3c)
`src/.../accelerants/radeon_hd/mode.cpp:445-446` (comment), cap table at
`:453-462`. Cayman shares Barts' 256-bit-bus / DCE-5 linear-scanout
architecture but is intentionally left uncapped because no Cayman card
has been tested. Once hardware is available, add an `else if
(info.chipsetID == RADEON_CAYMAN)` clause (or validate that it genuinely
needs no cap).

### 10. Debug-only format-specifier nits in TRACE strings
`src/.../accelerants/radeon_hd/mode.cpp:611`, `:615`
(`"brightness level = %lx"` for a `uint32`), `mode.cpp:503`
(`"MODE: %d ..."` for `mode->timing.pixel_clock`, a `uint32`), and
`driver.cpp:745` uses `%d` for `PCI_BAR_FB` while `:754` uses
`B_PRIu32` for the equivalent `mmioBar`. All are inside TRACE/ERROR
diagnostics only (no functional effect), but `%lx`/`%d` for `uint32`
mismatches the Haiku `B_PRIx32`/`B_PRIu32` convention and can mis-print on
some builds. Fix: switch to the `B_PRI*32` macros for consistency.

### 11. Large `#if 0` Linux-derived dead blocks in `gpu.cpp` ring/MC code
`src/.../accelerants/radeon_hd/gpu.cpp:800-804`, `:857-940` (multiple
`#if 0` blocks carrying `dev_priv` / `dev->sg` Linux DRM references and
AGP TODOs). This is commented-out reference code from the Linux radeon
ring/command-processor bring-up that the driver does not use. Either
finish the ring/CP work or strip the dead Linux scaffolding to reduce
noise.

### 12. `radeon_gpu_ring_boot()` is a stub that returns before its body
`src/.../accelerants/radeon_hd/gpu.cpp:822-824`. The function logs
`"%s: TODO"` and `return B_OK` immediately, leaving ~120 lines of
unreachable ring-init code below it. Document it as a deliberate no-op or
remove the unreachable tail.

### 13. Driver-table NAVI granularity TODO
`src/.../kernel/drivers/graphics/radeon_hd/driver.cpp:621`
("We might need to split NAVI into NAVI10, NAVI12, etc"). Many entries
share `RADEON_NAVI`; if per-variant behavior is ever needed this needs a
finer chipset enum. No current functional impact (most NAVI entries are
inside `#if 0` and unbuilt).

### 14. Assorted "multi-monitor?" / single-CRTC TODOs
`src/.../accelerants/radeon_hd/mode.cpp` (lines 49, 75, 85, 96, 116, 170,
182, 371, 619-622) and `display.cpp:1069-1071` ("TODO: shared PLL
detected!"). The accelerant hardwires `crtcID = 0` / display 0 throughout;
true multi-head support is unimplemented. Large feature, not a bug —
tracked here so the scattered TODOs are visible in one place.

### 15. Stale per-encoder TODO stubs (mostly diagnostic)
`src/.../accelerants/radeon_hd/encoder.cpp` (e.g. `:464`, `:1113`,
`:2123` DVO/TV-NTSC "TODO" stubs that only log) and
`displayport.cpp:503`, `:581`, `:1028` ("this surely can be cleaned up").
These are unsupported-path stubs (DVO encoder setup, hardcoded NTSC TV)
that emit a TRACE and continue. Low priority; revisit only if those
encoder types need to be supported.

### 16. Style-guide linter + pre-release gate (tooling)
Build a script/tool that mechanically enforces `docs/STYLE_GUIDE.md` and run
it (requiring a pass) before every release — i.e. automate the "separate style
audit" this list defers to in its preamble, and wire it into the release
checklist. Scope-aware: this fork is driver-only, so the linter runs against
the fork-carried `radeon_hd` tree, not upstream-only files. Cross-project
effort shared with the AST2400, NFSMount, and UEFI Wizard tools (all
Haiku-style guides); a single shared linter is the goal rather than four
one-offs. Likely path: clang-format + clang-tidy with a project config in a
release-gate wrapper.

---

## Out of scope (needs changes outside the driver tree)

- **Tiled scanout to lift the per-chip pixel-clock caps.** The Caicos
  (165 MHz), Turks (250 MHz), and Barts (340 MHz) caps in
  `mode.cpp:453-462` exist because Haiku writes a *linear* PCI-BAR-mapped
  framebuffer (`mode.cpp:414-450` comment). The classic fix — 2D-tiled
  scanout — requires app_server to write pixels in tile order or add a
  translation layer, which lives in app_server, not the driver. (Note:
  the in-driver display-watermark/line-buffer port in item 3 is the
  *in-scope* alternative and is the preferred path.)

- **Haiku PCI ticket #3 — BAR resource assignment.** `validate_bars()`
  (`driver.cpp:720-776`) only *detects and refuses* boards whose BARs the
  firmware left unprogrammed; it cannot assign them. The actual fix is in
  the Haiku PCI bus manager (kernel), out of this fork's scope. The guard
  itself is the correct driver-side mitigation.

- **AtomBIOS area kept writable (`#19348`).** `radeon_hd.cpp:120-122` and
  `:233-235` (XXX): the rom area "should" be `B_KERNEL_READ_AREA |
  B_CLONEABLE_AREA` but AtomBIOS calls fail when it is read-only, so the
  `set_area_protection()` is commented out. The root cause is Haiku ticket
  #19348 (AtomBIOS parser writing into nominally-RO memory), which is a
  kernel/shared-AtomBIOS-parser issue, not a driver-local fix.
