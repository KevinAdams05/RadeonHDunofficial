>[!NOTE]
>An LLM was used to aid in development of this code.

# Test Plan — Bug Verification Pass

This document captures the bugs from Haiku's tracker that we have
matching test hardware for and have not yet exercised directly with
the fork's shipped fixes. The intent is a targeted verification
session, not full coverage — see
[`technical-documentation.md`](technical-documentation.md#bug-tracker-cross-reference)
for the full bug-tracker cross-reference, including bugs we cannot
reach with current hardware.

## Test hardware on hand

| Card | Chip | DCE | Bus | Memory bus | Boot-validated against |
|---|---|---|---|---|---|
| PowerColor AX5450 | Cedar (Evergreen) | 4.0 | PCIe | 64-bit DDR3 | 0.1.0 (HDMI), 0.2.0 (DVI fallback) |
| AMD OEM HD 7470 / 8470 | Caicos XT (NI) | 5.0 | PCIe | 64-bit DDR3 | 0.4.0 (EDID range, linear scanout, 165 MHz cap) |
| AMD OEM HD 6570 / 7570 / 8550 / R5 230 | Turks PRO (NI) | 5.0 | PCIe | 128-bit DDR3 | 0.5.0 (250 MHz cap, square-mode filter) |

## In-scope bugs

Three bugs have a chip match in our test rotation and merit a direct
test pass on the 0.6.0 driver. Two are regression checks against
shipped fixes; one is a reproduction attempt of a known unfixed bug to
either confirm or rebut the scope of a proposed work item.

### Bug 1 — [#8339] HD 6450 hash in image

- **Reported hardware:** HD 6450 (Caicos in some skews; the original
  ticket text describes display "hash" — interference-style noise
  artifacts on a static image).
- **Shipped fix:** 0.3.0 spread-spectrum V2 constant correction in
  `display_crtc_ss()`. The V2 PLL path on DCE 4.x (Evergreen) was
  incorrectly using V3-typed constants for PPLL2 and DCPLL.
- **Best-matching card:** **AX5450 (Cedar, DCE 4.0)**. The
  V2 spread-spectrum path is specifically the Evergreen / DCE 4.x
  branch, so Cedar is the right chip to verify it on. The HD 7470
  (Caicos) takes the V3 path and would not exercise the change.
- **Test procedure:**
  1. Boot AX5450 to desktop at 1920×1080@60Hz over HDMI.
  2. Open a solid-color background (the default desktop is fine, or
     set wallpaper to a uniform color via Preferences → Backgrounds).
  3. With the screen still, look for any horizontal banding, dithering
     noise, or shimmer ("hash"). Hold for 30s — spread-spectrum
     artifacts are usually visible immediately if present.
  4. Repeat at 1024×768 and 1600×1200.
  5. Take a phone photo of any visible artifact with shutter ≤ 1/60s
     so the camera doesn't average the wobble out.
- **Pass criteria:** no visible hash / banding / shimmer at any of the
  three resolutions.
- **Fail action:** capture the phone photo plus a syslog snippet of
  `display_crtc_ss` TRACE lines (enable in `display.cpp` if not on)
  and attach to the bug or a new investigation note.
- **Effort:** ~15 minutes (one card, three resolutions).

### Bug 2 — [#9964] HD 5470 unsupported laptop native mode

- **Reported hardware:** HD 5470 (Cedar Mobility, Evergreen DCE 4.0).
  Laptop EDID advertised a native mode the driver rejected, falling
  the system back to VESA.
- **Shipped fixes:** 0.1.0 pixel-clock validation per connector type;
  0.4.0 EDID range parser hardening (rejects bogus range descriptors
  + underflow-safe lower-bound test).
- **Best-matching card:** **AX5450 (Cedar, DCE 4.0)** — same chip as
  HD 5470, just desktop bin.
- **Test procedure:**
  1. Boot AX5450 over HDMI to the 4K test monitor.
  2. From a Terminal, run `listdev | grep -i radeon` and confirm
     `0x1002:0x68f9 Cedar` is detected.
  3. Open Preferences → Screen and confirm the full mode list contains
     the monitor's native and a sampling of non-standard timings
     (1024×768, 1280×720, 1366×768 if the monitor advertises any of
     those).
  4. If the monitor exposes a non-standard or laptop-class timing
     (1366×768, 1600×900), select it and confirm the modeset succeeds
     and the picture is correct.
  5. Pull syslog (`/var/log/syslog`) and grep for
     `is_mode_supported.*BAD` — any rejections should be for modes
     that genuinely exceed pixel-clock or connector limits, not for
     ordinary candidate modes.
- **Pass criteria:** no `BAD, out of range` lines for ordinary
  monitor-advertised modes. Native mode selectable and correct.
- **Limitation:** without the exact problematic HD 5470 laptop EDID
  on hand, we are testing the *general* path the fix opens up, not
  the original ticket's specific EDID. This narrows the verification
  to "regression-free" rather than "specifically resolves #9964."
  Worth a note on the ticket either way.
- **Effort:** ~20 minutes including syslog grep.

### Bug 3 — [#17279] Caicos 32bpp screen tearing (reproduction attempt)

- **Reported hardware:** Caicos NI DCE 5.0 — at 32bpp the user saw
  visible horizontal tearing on motion; 16bpp did not exhibit it.
- **Status:** **not fixed in any shipped version**. Listed under
  proposed *NI/Polaris extensions* work — needs a vblank-synchronized
  page-flip path which the driver doesn't currently implement.
- **Best-matching card:** **HD 7470 (Caicos)** — exact match.
- **Goal:** confirm the bug reproduces on our hardware so the
  proposed-work scope is correct. If it *doesn't* reproduce on our
  setup, that downgrades the priority and changes the proposed
  fix's framing.
- **Test procedure:**
  1. Boot HD 7470 to 1920×1080@60Hz × 32bpp over DisplayPort.
  2. Drag a large window (Tracker file list, full Terminal) back and
     forth across the screen at a steady pace. Look for a horizontal
     line where the top half of the moving content is offset from
     the bottom half.
  3. Play a video at full screen (any 1080p clip) and watch for the
     same horizontal tearing on panning shots.
  4. If 16bpp is still a valid Haiku mode, try
     `screenmode 1920 1080 16 60` and repeat steps 2–3. The bug
     report says 16bpp is clean.
- **Pass criteria (for the test, not the bug):** bug reproduces at
  32bpp and is absent / reduced at 16bpp. That validates the proposed
  vblank-sync work.
- **Alternate outcome:** if 32bpp is also clean on our card and
  monitor combo, the bug may have been display-specific or
  environment-specific. Note it on the ticket and rescope the
  proposed work.
- **Effort:** ~20 minutes.

## Out of scope for this pass

Bugs whose reported hardware does not match anything in our rotation
are deferred until the relevant card is acquired. See the test-hardware
acquisition notes in
[`technical-documentation.md`](technical-documentation.md#hardware-generations-affected)
for the cards that would unlock each.

| Generation | Bugs gated on acquisition | Suggested cheap test buy |
|---|---|---|
| R600 (DCE 1.0 / 2.0) | [#12642], [#12970], [#17614] | HD 2400 PRO or HD 2600 ($5–10) |
| R700 (DCE 3.0 / 3.2) | [#8457], [#11242], [#11907], [#15125], [#19166] | HD 4670 or HD 3470 ($5–10) |
| Evergreen non-Cedar | [#19934] (Redwood) | HD 5670 |
| Northern Islands non-Caicos/Turks | [#10327] (Barts), hybrid bugs | HD 6850 |
| Southern Islands discrete | [#13700] | HD 7770 / HD 7870 |
| Southern Islands APU (Aruba) | [#10606], [#17582] | A10-6800K + FM2 board |
| Sea Islands discrete | (none triaged) | R9 290 |
| Sea Islands APU | [#10939], [#12968], [#13234], [#13864], [#16805], [#19281] | A10-7860K + FM2+ board |
| Volcanic Islands APU | [#16560] (Stoney) | Stoney laptop |
| Polaris | [#14918], [#15385], [#16482], [#16818], [#16960], [#17342], [#17416], [#18530] | RX 580 + AM4 board |

## Reporting results

After the verification pass:

1. For each bug ticket with a clean pass, post a short comment on the
   Haiku Trac ticket noting `radeon_hd_unofficial v0.6.0` plus
   hardware and result. Include the relevant chip+DCE pair so future
   readers can tell what was actually tested.
2. For a fail, capture syslog plus phone photos and either reopen
   investigation locally or add a note to the project memory
   (`project_radeon_*` slugs).
3. For #17279 specifically, the outcome shapes the proposed-work
   priority — record either "reproduced on HD 7470 + monitor X, work
   needed" or "did not reproduce, may be environment-specific" in
   the bug tracker cross-reference notes in
   [`technical-documentation.md`](technical-documentation.md#bug-tracker-cross-reference).
