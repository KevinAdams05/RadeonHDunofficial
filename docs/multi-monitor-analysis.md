# Multi-monitor support for RadeonHD — feasibility analysis

Compiled 2026-07-30. Covers (1) what Haiku's app_server, accelerant API
and Screen preferences can do today, (2) what the BeOS-era Matrox /
Radeon "dual head" support actually was and how much of it survives,
(3) the state of the six-year-old upstream design discussion, and (4) a
concrete work breakdown for `radeon_hd`, split by whether the work lands
inside this fork's driver-only scope or requires Haiku OS changes.

Line references are against the Haiku tree at `a0bfeae472` and the
RadeonHD working tree as of the compile date.

**Diagrams**

- [`diagrams/multi-monitor-architecture-tracks.svg`](../diagrams/multi-monitor-architecture-tracks.svg)
  — the three candidate architectures side by side through the whole
  stack, colour-coded by who has to do the work.
- [`diagrams/multi-monitor-span-framebuffer.svg`](../diagrams/multi-monitor-span-framebuffer.svg)
  — span-mode framebuffer/viewport mechanics with two mismatched
  monitors, the dead-zone problem, and the bandwidth consequence.

---

## 1. Executive summary

There are two genuinely different features hiding behind "multi-monitor
support", and they have completely different cost profiles:

| | Track A — span / clone | Track B — true multi-screen |
|---|---|---|
| Monitors light up | yes | yes |
| One desktop across both | yes | yes |
| Per-monitor resolution / refresh | **no** (see §6.3 for a partial escape) | yes |
| Per-monitor colour space / gamma | no | yes |
| `BScreen` enumeration (`SetToNext()`) | no — one screen | yes |
| Hot-plug | no | yes |
| Per-monitor DPI / UI scale | no | needs **new Haiku API** either way |
| Haiku changes required | **none** | app_server + accelerant API + Screen prefs |
| Fits this fork's driver-only scope | **yes** | no |
| Rough size | ~1–2 weeks of driver work | multi-month, multi-developer, cross-team |

**Track A is buildable today, entirely inside `radeon_hd`, with zero
patches to Haiku.** That is not a workaround — it is the mechanism BeOS
and the old Haiku Matrox/Radeon drivers used, and the entire OS-side
half of it (Screen preferences UI, `B_SCROLL` span-mode recognition,
`Screen::Frame()` honouring `virtual_width`) is still present and
functional in Haiku today. Nobody removed it; there is simply no modern
driver that answers it.

**Track B is blocked on Haiku, not on the driver.** The accelerant API
has no concept of a display id; the proposed fix (Gerrit change 329) has
been open and unimplemented since July 2018. app_server's multi-screen
support is a data-structure skeleton with the load-bearing parts
stubbed. PulkoMandy said as much in the forum thread (post #149): *"the
blocking point is mainly the drivers side"* — but that is only true for
Track A. For Track B the driver is the easy half.

**Recommendation.** Ship Track A in the fork, in three milestones
(clone → horizontal span → vertical span), gated behind the *existing*
Screen preferences "Combine displays" menu. Do the watermark Phase B
work **first** (§7). Treat Track B as a separate upstream conversation,
and if you want to move it, the highest-leverage single contribution is
not driver code — it is finishing and landing Gerrit 329 with one real
driver behind it.

---

## 2. What Haiku can do today

### 2.1 app_server: the skeleton is real, the joints are welded shut

app_server genuinely has multi-screen *types*. `VirtualScreen` holds a
list of screens each with its own frame, and exposes `ScreenAt()`,
`ScreenByID()`, `ScreenFrameAt()`, `CountScreens()` and
`SetScreenFrame()`. Axel Dörfler implemented this in 2005 and blogged
about it; in the thread (post #11) he correctly says *"app_server itself
already supports multiple displays"*.

But every path that would make a second screen *do* anything is stubbed:

- **Only one accelerant is ever loaded.**
  `ScreenManager::_ScanDrivers()` (`ScreenManager.cpp:193`) contains a
  `while (initDrivers)` loop that executes exactly once, with the
  comment *"Eventually we will loop through drivers until one can't
  initialize in order to support multiple monitors. For now, we'll just
  load one and be done with it."* One `AccelerantHWInterface`, therefore
  one `Screen`, therefore one entry in `VirtualScreen`.

- **`VirtualScreen::AddScreen()` overwrites instead of aggregating.**
  At `VirtualScreen.cpp:153`: *"TODO: this works only for single screen
  configurations"* — `fDrawingEngine`, `fHWInterface` and `fFrame` are
  each assigned from the screen being added, so with N screens the last
  one wins and the rest are inert.

- **`VirtualScreen::RemoveScreen()` returns `B_ERROR`**
  (`VirtualScreen.cpp:168`, *"not implemented yet (config changes when
  running)"*). No hot-plug, no mode-change-at-runtime teardown.

- **`UpdateFrame()` assumes a horizontal strip.** It sums widths and
  takes the max height, with *"TODO: compute virtual size depending on
  the actual screen position!"* — no arbitrary layout.

- **Userland can never see screen 1.** `BScreen::SetToNext()` calls
  `BPrivateScreen::GetNextID()`, which sends `AS_GET_NEXT_SCREEN_ID`.
  app_server's handler (`ServerApp.cpp:2960`) reads the id and
  unconditionally replies `B_ENTRY_NOT_FOUND` — *"TODO: for now, just
  say we're the last one."* This is exactly what Rockford observed in
  post #148 ("Haiku currently supports only `B_MAIN_SCREEN`"). It is not
  that the enumeration is buggy; it was never wired up.

- **`Desktop` is single-interface throughout.** Cursor placement, the
  input event stream (`HWInterface()->CreateEventStream()`), DPMS and
  brightness all go through the one `fVirtualScreen.HWInterface()`, and
  `Desktop::ScreenAt()` (`Desktop.cpp:3481`) hard-returns
  `fVirtualScreen.ScreenAt(0)`.

- **`Window` binds its `DrawingEngine` in its constructor**
  (`Window.cpp:95`) with no reassignment path — this is X512's point in
  post #15: you cannot move a window to another screen because there is
  no way to change which engine draws it.

### 2.2 The one thing that *does* work: `virtual_width`

This is the crux of the whole analysis:

```cpp
// src/servers/app/Screen.cpp:228
BRect
Screen::Frame() const
{
    display_mode mode;
    fHWInterface->GetMode(&mode);

    return BRect(0, 0, mode.virtual_width - 1, mode.virtual_height - 1);
}
```

The desktop's size comes from the mode's **virtual** dimensions, not
from `timing.h_display`. If the accelerant advertises and accepts a mode
whose `virtual_width` is twice `h_display`, app_server's desktop becomes
twice as wide — no app_server changes at all. Whether the extra width is
*visible* is entirely the driver's business: it can point a second CRTC
at the right-hand half (span), or leave it invisible.

One important caveat: **app_server never pans.** Nothing in
`src/servers/app/` calls `B_MOVE_DISPLAY` / `move_display_area` (only
the `vesa`, `intel_extreme`, `s3`, `intel_810`, `ati` and `3dfx`
accelerants even export the hook, and nothing drives it). So a
double-width mode with no driver-side span support does not give you a
scrolling virtual desktop — it gives you a desktop with half of itself
permanently off-screen. The driver must make the second half visible or
not offer the mode.

### 2.3 The Screen preferences UI is already built

`src/preferences/screen/` still ships Thomas Kurschel's 2002
multi-monitor plumbing:

- `ScreenMode.cpp:64` — `get_combine_mode()` classifies a mode as
  `kCombineHorizontally` / `kCombineVertically` purely from
  `flags & B_SCROLL` plus `virtual_width == timing.h_display * 2` (or
  the vertical equivalent). Mode-list sorting already de-doubles those
  widths so the list stays sane.
- `multimon.cpp` — a settings **tunnel** smuggled through
  `BScreen::ProposeMode()`. `PrepareTunnel()` marks a `display_mode`
  triple with sentinels (`low->timing.pixel_clock = 'TKTK'`,
  `high->timing.pixel_clock = 'KTKT'`, `space = 0`, inverted
  virtual sizes), puts a setting code in `mode.h_display_start` and a
  verb in `mode.v_display_start` (0 = get, 1 = set, 2 = enumerate), and
  reads the answer back out of `mode.timing.flags`.
- `ScreenWindow.cpp:480` — builds the **"Combine displays:
  disable / horizontally / vertically"** menu, plus **"Swap displays"**,
  **"Use laptop panel"** and **"Video format"**. Each field is
  `Hide()`n unless the driver answers the corresponding tunnel probe.
  The header even carries `#include "multimon.h" // the usual: DANGER
  WILL, ROBINSON!`.

So the user-facing controls for Track A exist, are localised, and are
sitting dark waiting for a driver to answer. That is a meaningful amount
of free work.

### 2.4 DPI and UI scaling: nothing exists, for anyone

You raised this, and it is worth being blunt about: **Haiku has no
display-scaling API at all.** There is no `ui_scale`, no DPI field, no
per-screen scale factor anywhere in `headers/os/interface/`,
`headers/private/interface/`, or app_server. UI size is derived from the
global plain-font size (Appearance preferences) and `BControlLook`
metrics computed from it — one value for the whole system.

Consequences for this analysis:

- Per-monitor DPI is **not** a Track A limitation that Track B fixes. It
  is a missing Haiku feature that blocks both. Track B is a
  *prerequisite* for it (you need per-screen state to hang a scale
  factor off), not a delivery of it.
- The thread burned roughly 80 posts on the DPI/scaling question
  (posts #30–#37, #56–#60, #90–#100, #111–#114, #152–#161) without
  reaching a decision, precisely because it is a separate feature.
  Do not let it gate driver work.
- Gerrit 329 does plant the seed: it adds `dpcm` (dots per cm) to
  `monitor_info.hardware`, so the API-v2 proposal at least carries
  physical density per display. EDID already contains physical size and
  `radeon_hd` already parses EDID per display, so the driver side of
  that is nearly free whenever the OS wants it.
- Practical note from the thread worth keeping: nephele (#154) and
  Rockford (#153, #155) argue EDID physical size is often nonsense and
  that projectors/splitters cannot know it, so any future DPI feature
  needs a user override. X512 (#157) and ubu (#161) counter that modern
  monitors report it correctly. The honest design is *trust EDID,
  always allow override* — but that is an Appearance-prefs
  conversation, not a driver one.

---

## 3. The BeOS-era "dual head" support — what it was, and what's left

PulkoMandy's post #10 refers to *"the hacked multi-display support in
the old Radeon and Matrox drivers"*, and #149 notes he still runs it on
a 2003 Athlon XP with a Radeon 9250. Here is what that actually is.

### 3.1 Which drivers have it

Grepping `src/add-ons/accelerants/` for dual-head code:

| Driver | Hits | Notes |
|---|---|---|
| `nvidia` | 87 | full dual-head + TV-out (Rudolf Cornelissen) |
| `matrox` | 81 | full dual-head, CRTC2 + MAVEN (Rudolf Cornelissen) |
| `via` | 71 | same lineage |
| `radeon` (R100–R400) | via `multimon.c` | Thomas Kurschel's combine/clone |
| `neomagic` | 5 | vestigial |
| **`radeon_hd`** | **0** | — |
| **`intel_extreme`** | **0** | — |
| `ati`, `s3`, `3dfx` | 0 | — |

So: every driver that supports two monitors on Haiku is a pre-2005
driver for pre-2005 hardware. Everything written since — `radeon_hd`,
`intel_extreme` — is single-head. That is the real reason this feature
has never moved.

### 3.2 The mechanism (old `radeon`, which is the closest analogue)

`src/add-ons/accelerants/radeon/ProposeDisplayMode.c:515`:

```c
static void checkAndAddMultiMode( accelerator_info *ai,
    const display_mode *mode, bool ignore_timing )
{
    display_mode wide_mode;

    checkAndAddMode( ai, mode, ignore_timing );      // plain mode

    wide_mode = *mode;                                // double width
    wide_mode.virtual_width *= 2;
    wide_mode.flags |= B_SCROLL;
    checkAndAddMode( ai, &wide_mode, ignore_timing );

    wide_mode = *mode;                                // double height
    wide_mode.virtual_height *= 2;
    wide_mode.flags |= B_SCROLL;
    checkAndAddMode( ai, &wide_mode, ignore_timing );
}
```

Then `multimon.c` translates that back into an internal mode class:

- `Radeon_DetectMultiMode()` — if `B_SCROLL` is set and
  `virtual_width == 2 * h_display`, strip `B_SCROLL`, set
  `RADEON_MODE_COMBINE | RADEON_MODE_POSITION_HORIZONTAL`. Same for
  vertical. If neither matches, it wasn't really a combine mode —
  restore `B_SCROLL`.
- `Radeon_VerifyMultiMode()` — if fewer than two CRTCs are usable
  (no second port, or no second monitor connected), silently fall back
  to `RADEON_MODE_STANDARD`. **Graceful degradation is built in.**
- `Radeon_HideMultiMode()` — reverse the translation on the way back out
  to app_server, so the OS only ever sees the public `B_SCROLL` form.
- `Radeon_InitMultiModeVars()` — compute `eff_width`/`eff_height` and
  each CRTC's `rel_x`/`rel_y` offset into the shared framebuffer.
- Clone mode is the fallback when combining is impossible.

Plus the settings tunnel (`ms_swap`, `ms_use_laptop_panel`,
`ms_tv_standard` in `headers/private/graphics/radeon/accelerant_ext.h`)
answering the Screen-prefs probes described in §2.3.

### 3.3 How much is usable?

**The design is entirely usable; the code is not portable.** The old
`radeon` accelerant is C, pre-AtomBIOS, and register-level incompatible
with R600+ — nothing can be lifted verbatim. What transfers is:

1. **The public contract** — double-width/`B_SCROLL` mode advertisement.
   Unchanged in Haiku, understood by Screen prefs, honoured by
   `Screen::Frame()`. This is the whole reason Track A is cheap.
2. **The internal mode-class pattern** — a private mode taxonomy
   (`STANDARD` / `COMBINE` / `CLONE`) hidden behind translate-in /
   translate-out at the hook boundary. Directly reusable as a design.
3. **The degradation rule** — verify usable CRTC count at set-mode time
   and fall back, never fail. Directly reusable.
4. **The settings tunnel** — `PrepareTunnel()`'s sentinel protocol is
   fixed by the *preferences app*, so a new driver must implement the
   driver half exactly as specified to light up the existing UI. That
   means exporting `B_PROPOSE_DISPLAY_MODE`, which `radeon_hd` currently
   does not (§4.2).

---

## 4. Current state of `radeon_hd`

### 4.1 What is already multi-head shaped

More than you would expect. The driver was written by Alexander von
Gluck IV, who is also the author of Gerrit 329, and it shows:

- **`MAX_DISPLAY 2`** (`accelerant.h:25`) with `gDisplay[MAX_DISPLAY]`,
  and `detect_displays()` (`display.cpp:245`) already probes every
  connector, reads EDID per display, records `preferredMode`, and
  populates **both** slots. Two monitors are already detected, ranged
  and register-mapped at init. `debug_displays()` prints both.
- **`init_registers(regs, crtcID)`** (`display.cpp:42`) already computes
  a full per-CRTC register map, and on Evergreen+ handles **CRTC 0
  through 5** — all six `EVERGREEN_CRTCn_REGISTER_OFFSET` values are
  defined in `evergreen_reg.h:49-54`. Raising `MAX_DISPLAY` past 2 is
  mostly a matter of the display/connector matching logic, not the
  register plumbing.
- **Every `display_crtc_*` function is already parameterised by
  `crtcID`** — `display_crtc_set`, `_set_dtd`, `_fb_set`, `_scale`,
  `_lock`, `_blank`, `_dpms`, `_power`, `_memreq`, plus both LUT
  loaders.
- **DPMS already iterates all displays.** `radeon_dpms_set_hook()`
  (`mode.cpp:168`) loops `crtcID` over attached displays and calls
  `radeon_dpms_set(crtcID, mode)`.
- **The PLL allocator is already multi-head aware.** `pll_pick()` →
  `pll_next_available()` / `pll_shared_dp()` / `pll_usage_mask()` track
  which PLLs are in use across connectors, with DCE-6.1 DP sharing
  handled. (There is a `TODO` for sharing PLLs at identical clock
  rates — that becomes reachable with two heads; see §6.4.)
- **Encoder routing is already per-CRTC.** `encoder_assign_crtc(crtcID)`
  issues `SelectCRTC_Source` for the given CRTC, and
  `encoder_pick_dig(connectorIndex)` already resolves a `crtcID` by
  scanning `gDisplay[]` for the matching connector.
- **The kernel driver maps the entire VRAM BAR** as the framebuffer area
  (`radeon_hd.cpp:795`), so a wider surface needs no new allocation —
  just a larger pitch. 4480 × 1440 × 4 B ≈ 24.6 MiB, trivial.

### 4.2 What is hardwired to display 0

- **`radeon_set_display_mode()`** (`mode.cpp:177`) — *"TODO:
  multi-monitor? For now we set the mode on the first display found"*,
  then `uint8 crtcID = 0;`. This is the single biggest change.
- **`create_mode_list()`** (`mode.cpp:43`) — builds the mode list from
  `gDisplay[0]->edidData` only.
- **`radeon_accelerant_mode_count()`, `radeon_get_mode_list()`,
  `radeon_get_preferred_mode()`, `radeon_get_edid_info()`** — all
  display 0, each carrying a *"we need crtcid here"* TODO.
- **`display_crtc_fb_set()`** (`display.cpp:855-872`) — writes
  `grphSurfaceOffsetX/Y = 0`, `grphXStart/YStart = 0`,
  `viewportStart = 0`. **These are exactly the registers that need a
  per-head offset for span mode**, and they are already in the
  per-CRTC `register_info` map.
- **No `B_PROPOSE_DISPLAY_MODE` hook** and no `B_MOVE_DISPLAY` hook
  exported (`hooks.cpp`). `B_PROPOSE_DISPLAY_MODE` is required for the
  Screen-prefs settings tunnel.
- **`radeon_get_frame_buffer_config()`** returns one surface — correct
  for Track A, needs a display id for Track B.
- **HPD is detect-only.** `connector.cpp:125` resolves an
  `atomHPDID` for AtomBIOS calls, but there is no interrupt handler and
  no hot-plug notification path. Hot-plug is Track B territory anyway
  (app_server can't tear a screen down — §2.1).

Item 14 of `docs/TODO.md` already tracks these scattered TODOs; this
document is the analysis that item was waiting for.

---

## 5. The upstream design discussion (forum thread 10126)

161 posts, 2020-11-21 → 2026-05-21, started by X512 asking two
questions. It is worth reading the summary because **it determines what
would be accepted upstream**, and because it explains why nothing has
shipped.

### 5.1 The two questions and where they landed

**Q1: does a workspace span all displays, or does each display get its
own workspaces?** Effectively settled: a workspace spans all displays.
PulkoMandy (#10) reports that as the long-standing devteam consensus;
axeld (#42, #112) confirms `Desktop` is per-user-session and holds a
`VirtualScreen` that can contain multiple screens; PulkoMandy (#128)
confirms the current behaviour (switching workspaces switches all
displays at once). Per-display workspaces are acknowledged as
attractive (waddlesplash #7, several users) but axeld (#21) is explicit
that showing more than one workspace simultaneously *"would require
major rework"*. **Not in scope for a first cut.**

**Q2: may a window straddle two displays?** Never settled. This is the
thread's fault line and it ran for six years:

- **Pro (axeld, waddlesplash, bitigchi, win8linux, humdinger,
  Rockford):** screens are views onto one larger canvas; forbidding
  spanning is an arbitrary API restriction; real use cases exist (video
  walls, DAW/NLE timelines, flight sims, presentations). axeld (#66,
  #76): tearing, gamma, hinting are app_server's problem, not the
  app's; *"having the ability to have windows span over multiple screens
  DOES NOT SLOW DOWN ANYTHING."*
- **Con (nephele, X512, SamuraiCrow, SCollins, jscipione):** the shared
  coordinate system leaks — you cannot then do per-display refresh rate,
  colour space, gamma, subpixel order, or scaling; the rectangular
  virtual canvas creates unreachable regions where windows get lost;
  macOS/iPadOS deliberately don't do it (nephele #101, from experience);
  apps that need two screens should open two windows (X512 #68, with
  Blender as the example).
- PulkoMandy (#95) enumerated five reasonable options for mismatched
  pixel densities and asked the thread to pick one. Nobody did.
- PulkoMandy (#115) is the pragmatic closer: *"by trying to have the
  perfect solution, we have managed to have nothing at all for the last
  20 years. Maybe we should first make something simple that just gets
  multiple monitors going, then we can iterate."*

### 5.2 Architecture proposals on the table

1. **Multiplexing `HWInterface`/`DrawingEngine`** (axeld #17, #19, #21,
   #23, #56; PulkoMandy #19). One virtual interface per `Desktop`
   forwarding draw calls to per-screen children with per-screen offset
   and clipping; windows entirely on one screen keep a direct interface,
   so no overhead in the common case (axeld #45, #98; waddlesplash #24
   suggests the window route its own draws to skip intersection tests).
   Allows a `RemoteHWInterface` as an extra screen (#12) — axeld's
   suggested cheap starting point.
2. **Per-screen `Desktop` objects** (X512 #15, #41, #106, #109, #118).
   Split `Desktop` into session state vs. per-screen window/region
   management; each screen gets its own visible-region lock and its own
   thread, so differing refresh rates work naturally and no draw
   commands are broadcast. Window moves between screens the way it moves
   between workspaces. axeld (#48, #108, #112, #119) pushes back: the
   `Desktop` "god object" objection is misplaced, a per-session object is
   necessary, and each window need only know its drawing context.
3. **Multiple `app_server` instances, one per monitor** (nephele #101).
   Rejected immediately — axeld #102 *"definitely not the direction"*,
   X512 #103 (`BApplication` can't talk to two app_servers), PulkoMandy
   #104 (moves complexity up into the Interface Kit). nephele withdrew
   it (#107).
4. **Accelerant-side options** (axeld #11, PulkoMandy #13):
   - instantiate one accelerant *per display* — minimal API change,
     app_server just names the display when instantiating;
   - **add a display id to the accelerant hooks** — one accelerant
     drives several displays. This is Gerrit 329.
   - PulkoMandy #13: *"Possibly we need both"* — several displays per
     card **and** several cards.

### 5.3 What everyone agrees on

- **The accelerant API is the blocker for the first step.** axeld #11:
  app_server support exists, *"what is still missing… is support for
  this in the accelerants, including their current API."* axeld #105:
  *"The first step would either be to implement a multi monitor
  accelerant API, or use the test environment for this."*
- **`test_app_server` already supports multiple displays** (axeld #11,
  X512 #16) — a way to develop app_server-side multi-screen with no
  hardware at all. Worth knowing about; it decouples the two halves of
  Track B.
- Design decisions keep getting lost. X512 #120: *"It is better to put
  design proposal on some more persistent place such as
  dev.haiku-os.org Wiki or code tree documentation so it can be easily
  found and be not forgotten."* Nobody did that either — which is part
  of why this document exists.
- PulkoMandy #140: GSoC is only viable *"if there is a clear plan and
  architecture decided, and someone willing to mentor it"*, and he
  reports having *"spent quite a few hours trying to bring up a second
  display in the intel_extreme driver without success."*
- X512 #145: NVIDIA's open kernel modules expose a ready-made modeset
  API (`nvkms-api.h`) already ported to Haiku, making NVIDIA *"currently
  best option for experimenting with advanced display support"*. Worth
  knowing if you ever want to prototype Track B against something other
  than AtomBIOS.

### 5.4 What this means for a driver-only fork

Track A does not need a single one of these decisions to be made. It
predates the argument, it uses the API contract that already shipped,
and it survives whatever app_server eventually becomes: if Haiku later
grows real per-screen support, a span-capable driver either keeps
offering combine modes as a legacy option, or drops them in favour of
API-v2 hooks. Nothing built for Track A is wasted except the
`B_SCROLL` mode-list glue, which is small.

> **Confirmed on hardware, 2026-07-31.** A1 and A2 both shipped with **zero
> changes outside `src/add-ons/accelerants/radeon_hd/`** — no app_server
> patch, no Haiku header change, no Screen preferences change. An unmodified
> hrev59697 desktop spans two monitors as soon as the driver offers the modes
> and answers the handshake. The central claim of this document held.

---

## 6. Track A: implementation plan (fork scope)

### 6.1 Milestone A1 — clone / mirror

Simplest possible second head, and the right first step because it
isolates "can we light two CRTCs at once" from every framebuffer
question.

1. In `radeon_set_display_mode()`, replace the hardcoded `crtcID = 0`
   with a loop over `gDisplay[id]` where `attached` is true, up to the
   number of usable CRTCs.
2. Per head, run the existing sequence: `encoder_output_lock`,
   `display_crtc_lock`, `radeon_dpms_set(id, B_DPMS_OFF)`,
   `encoder_assign_crtc(id)`, `pll_pick(connectorIndex)` + `pll_set`,
   `display_crtc_set_dtd(id, mode)`, `display_crtc_fb_set(id, mode)`,
   `display_crtc_scale(id, mode)`, `encoder_mode_set(id)`, then
   `radeon_dpms_set(id, B_DPMS_ON)` and unlock.
3. Both heads get the same `mode` and the same
   `grphPrimarySurfaceAddr`, both viewports at (0,0). No pitch change,
   no `virtual_width` change, no OS-visible change at all.
4. Only offer a mode to both heads if both monitors' EDID ranges accept
   it — otherwise fall back to head 0 alone (the
   `Radeon_VerifyMultiMode()` rule from §3.2).

**Risks:** PLL exhaustion on pre-DCE4 parts (`pll_pick()` falls through
to `ATOM_PPLL1` unconditionally for `dceMajor < 4` — that will collide
with two heads and needs the DCE-3 path taught to allocate); DIG/UNIPHY
sharing on DCE4+ (`encoder_pick_dig()` returns a link-based index that
two connectors can collide on); and scanout bandwidth (§7).

**Validation:** the Bonaire R7 260X (DCE8) and Turks (DCE5) cards in
`project_radeon_bonaire_testing` both have ≥2 outputs; Cedar is the
DCE4 datapoint. A clone test needs no new hardware.

### 6.2 Milestone A2 — horizontal span

1. **Advertise the modes.** In `create_mode_list()`, after the normal
   list is built, add double-width variants with `B_SCROLL` set —
   `checkAndAddMultiMode()` from §3.2 is the template. Only add them
   when two displays are attached and both accept the base timing.
2. **Recognise them.** A private translate-in step at the top of
   `radeon_set_display_mode()`: `B_SCROLL` + `virtual_width == 2 *
   timing.h_display` → internal `COMBINE_HORIZONTAL`; strip `B_SCROLL`.
   Mirror it in `radeon_get_display_mode()` on the way out so app_server
   and Screen prefs only ever see the public form.
3. **Widen the surface.** `display_crtc_fb_set()` already computes
   `widthAligned` from `mode->virtual_width` and programs `grphPitch`
   plus `shared_info->bytes_per_row` from it — so the wide pitch mostly
   falls out for free. Confirm the `GRPH_PITCH` field width per DCE
   generation before trusting large values (the old `radeon` driver
   clamped `virtual_width` to 8192).
4. **Offset the second viewport.** This is the actual span mechanism,
   and it is two register writes that are currently hardcoded to zero:
   - `regs->viewportStart` ← `(x << 16) | y` — for head 1,
     `x = h_display`;
   - `regs->grphSurfaceOffsetX` / `grphXStart` ← the same x.

   Both CRTCs keep the same `grphPrimarySurfaceAddr`. Verify against
   Linux's `radeon`/`amdgpu` DCE code which of the offset registers is
   authoritative per generation — `GRPH_SURFACE_OFFSET_X`,
   `GRPH_X_START` and `VIEWPORT_START` are three different knobs and the
   correct one differs between AVIVO, DCE4+ and DCE6+.
   (Per `feedback_linux_reference`: read the logic, do not copy code.)
5. **Answer the settings tunnel.** Export `B_PROPOSE_DISPLAY_MODE` and
   implement the `PrepareTunnel()` sentinel protocol from
   `src/preferences/screen/multimon.cpp` — get/set/enumerate for at
   minimum a combine setting and `ms_swap` (swap displays). That lights
   up the existing "Combine displays" and "Swap displays" menus.

   > **Correction, 2026-07-31 (implementation).** There is **no combine
   > setting in the tunnel** — the protocol only carries `ms_swap`,
   > `ms_use_laptop_panel` and `ms_tv_standard`. Combine is selected purely
   > from the **mode list**: `get_combine_mode()` in `ScreenMode.cpp` looks for
   > `B_SCROLL` plus `virtual_width == timing.h_display * 2`, and
   > `_GetDisplayMode()` searches the offered modes for that doubled width.
   >
   > The hook is still required, but for a different reason than assumed:
   > `ScreenWindow.cpp:496` does `if (!multiMonSupport) fCombineField->Hide()`,
   > so the **support handshake gates whether the combine menu is visible at
   > all**. Answer `MULTIMON_REQUEST`/`MULTIMON_REPLY` and the menu appears;
   > ignore it and horizontal span is unreachable from the GUI no matter how
   > good the mode list is.
   >
   > Also worth knowing: Screen preferences does **not** issue its own SetMode
   > after a tunneled settings change, so the swap handler has to re-apply the
   > current mode itself. Both behaviours confirmed working on hardware.
   >
   > Step 4's open question is settled too: on DCE 5 and DCE 8,
   > **`VIEWPORT_START` is the authoritative knob** —
   > `GRPH_SURFACE_OFFSET_X` and `GRPH_X_START` stay at zero and span works.

### 6.3 Milestone A3 — vertical span, and the mismatched-monitor question

Vertical span is the same work with `virtual_height` and a y offset;
`get_combine_mode()` already recognises it.

**Mismatched resolutions.** Track A's headline limitation is one
`display_mode` for two heads — but the *hardware* does not require
identical timings. Each CRTC has its own DTD registers, its own PLL and
its own viewport origin. So the driver can, internally:

- treat the incoming mode's `virtual_width`/`virtual_height` as the
  **framebuffer bounding box only**, and
- give each head its own timing (its EDID `preferredMode`) and its own
  viewport origin into that box.

That gets you 1920×1200 + 2560×1440 side by side on one desktop of
4480×1440 — see
[`multi-monitor-span-framebuffer.svg`](../diagrams/multi-monitor-span-framebuffer.svg).

The cost is a **non-rectangular union inside a rectangular desktop**:
1920×240 of dead zone that app_server will happily place windows and the
cursor into, because it has no idea it exists. This is precisely
nephele's objection in the thread (#64: *"the rectangular nature of the
'virtual' canvas… makes it easy to accidentally 'loose' windows"*), and
it is a real, user-visible defect, not a theoretical one. Also: dialogs
centre on the bounding box, so `BAlert`s land on the bezel.

**Recommendation:** implement equal-size span as the default and shipped
path; implement unequal-size span behind an explicit opt-in setting with
the dead-zone caveat documented, or defer it entirely. Do not make it
the default. A user with mismatched monitors is better served by both
heads running the smaller monitor's mode (letterboxed by the monitor's
own scaler) than by a desktop with holes in it.

### 6.4 Cross-cutting driver work

- **PLL allocation for DCE ≤ 3.** `pll_pick()`'s fallthrough
  (`pll->id = ATOM_PPLL1` with *"TODO: Should return the CRTCID here"*)
  is single-head-only. Needs a real allocator, or gate Track A to
  DCE ≥ 4 initially.
- **Identical-clock PLL sharing.** `pll_next_available()`'s TODO
  (*"we likely need to add the sharing of PLL's with identical clock
  rates, see radeon_atom_pick_pll in drm"*) becomes reachable as soon as
  two heads run, especially in clone mode where both want the same
  clock. Two heads on a 2-PLL part with a third clock source needed is
  exactly the failure this predicts.
- **`display.cpp:1069` "TODO: shared PLL detected!"** — same family of
  problem, already flagged in `docs/TODO.md` item 14.
- **Per-CRTC LUT/gamma.** `display_avivo_crtc_load_lut()` and
  `display_dce45_crtc_load_lut()` are already per-CRTC and both get
  called; with a single `BScreen` there is only one system palette to
  push, so this should be consistent by construction. Worth verifying
  in `B_CMAP8`.
- **HDMI/AFMT routing — already done, and it was a prerequisite.** The
  0.6.3 magenta-stripe fix (AFMT indexed by CRTC rather than DIG) is
  latent with one head because CRTC 0 and DIG 0 coincide. With two heads
  it becomes live on every mismatched pairing. That fix is already in
  and validated on Cedar and Barts, which removes a class of bug that
  would otherwise have surfaced as "span mode corrupts the second
  monitor".

---

## 6.5 Is Track A throwaway if Track B ever happens?

Mostly no, and the parts that are throwaway are the shallowest code in
the plan. Roughly 80% carries over; what gets discarded is mode-list glue
and a sentinel protocol, not hardware bring-up.

| Track A work | Fate under Track B |
|---|---|
| Watermark / arbitration work (§7) | **Keeps entirely** — and matters more: two heads at different resolutions and refresh rates is a harder arbitration problem than two heads at one mode |
| Per-head mode-set sequence parameterised by `crtcID` | **Keeps entirely** — becomes the body of `set_display_mode(displayID, …)` |
| PLL allocator for DCE ≤ 3; identical-clock sharing | **Keeps entirely** — a hardware constraint, independent of the API |
| DIG/UNIPHY collision resolution for two live encoders | **Keeps entirely** — same |
| Per-display EDID / mode list / preferred mode plumbing | **Keeps**, if built as per-display lists with a combine synthesiser layered on top rather than one pre-merged list |
| Surface-origin register knowledge (which of `GRPH_SURFACE_OFFSET_X` / `GRPH_X_START` / `VIEWPORT_START` is authoritative per DCE generation, pitch field widths, alignment) | **Keeps** — see below |
| `B_SCROLL` double-width mode advertisement + combine/clone taxonomy | **Throwaway**, or degrades to a legacy path |
| Screen-prefs `'TKTK'` settings tunnel | **Throwaway** — though the `B_PROPOSE_DISPLAY_MODE` hook itself is worth having regardless, for the mode validation the driver already does inline |

Two points deserve emphasis.

**The viewport work may not be throwaway even at the register level.**
Gerrit 329 adds `x_display_position` / `y_display_position` to
`frame_buffer_config` and makes `get_frame_buffer_config` per-display.
That shape accommodates *both* models — separate surfaces per head with
positions describing desktop layout, or one shared surface with per-head
origins. Which one app_server ends up wanting is downstream of the
unresolved multiplexing-`HWInterface` vs. per-screen-`Desktop` argument
(§5.2). If it lands anywhere near the shared-surface reading, the span
offset code *is* the implementation. Either way, per-head surface
addressing is required, so the per-DCE-generation register knowledge is
durable. What Track A does **not** solve is where head 1's framebuffer
lives if Track B goes with independent surfaces — that is new VRAM
allocation work.

**Track A is the validation infrastructure for Track B's driver half.**
The genuinely hard, undone thing is proving on real silicon that two
CRTCs, two PLLs, two DIGs and two encoders come up simultaneously on
Cedar, Turks and Bonaire without underflow. That is what PulkoMandy burned
hours failing at on `intel_extreme` (thread #140), and it is entirely
API-independent. Every bug found doing Track A is a bug Track B would
have hit — and §7 is a worked example: the line-buffer split field
position was wrong in this driver's headers, invisible with one head, and
would have surfaced under Track A as the second monitor starving.

**The one real lock-in risk** is building Track A badly — threading
"combine mode" as a concept down into the encoder/PLL/CRTC layers instead
of translating at the hook boundary. The old `radeon` driver got this
right (§3.2): `Radeon_DetectMultiMode()` / `Radeon_HideMultiMode()`
translate in and out at the edge, and everything below speaks only in
heads, timings and offsets. Hold that discipline — one narrow layer owns
the `B_SCROLL` translation, everything below takes
`(head, timing, surface origin)` — and the throwaway stays isolated in
one file.

Finally, the scheduling argument: Track B's arrival date is unknowable.
Six years of thread, Gerrit 329 stalled for eight, no app_server code
written. Track A ships a working second monitor on real Radeon hardware
in the meantime, and the two can coexist — a span-capable driver can keep
offering combine modes as a legacy option indefinitely.

---

## 7. Do the watermark work first

This is the strongest sequencing recommendation in this document.

`docs/scanout-watermark-investigation.md` Phase A established, with
register read-back on real hardware, that the VBIOS/GOP leaves display
arbitration at reset: zero watermarks, zero priority counters, and a
half line-buffer split. The Phase A instrumentation in the fork's
`mode.cpp` (`bandwidth_registers_dump()`) already dumps, per CRTC:

```
DC_LB_MEMORY_SPLIT, PRIORITY_A_CNT, PRIORITY_B_CNT,
PIPEn_ARBITRATION_CONTROL3, PIPEn_LATENCY_CONTROL, DMIF_BUFFER_CONTROL
```

with an explicit comment that *"the line buffer is shared between
[both CRTCs of a pair], so the idle CRTC's split setting matters too."*

That comment is the whole point. With one active head, a wrong split and
absent watermarks are survivable — the single head gets enough of the
buffer by luck. **With two active heads it is not luck any more:**

- the line buffer is divided between the CRTC pair, and the reset value
  assumes the idle head needs nothing;
- total scanout bandwidth roughly doubles, against unprogrammed latency
  watermarks and priority counters;
- a wide framebuffer increases the per-line fetch burst.

The failure mode is intermittent scanout underflow — tearing, a
horizontal band of garbage, or a flickering line on whichever head loses
the split. That is **indistinguishable from a mode-set or viewport bug**
during bring-up, and you will burn days chasing it in the wrong file.
Phase B (implementing the arbitration/watermark programming) should land
before Milestone A1, or at minimum before A2.

### 7.1 Update, 2026-07-30 — Phase B is done, and it surfaced a second prerequisite

Phase B is implemented (`bandwidth.cpp`) and verified on Turks hardware,
so the sequencing recommendation above is satisfied. Two results from that
work change this document.

**First, it already paid for itself against Track A.** The line-buffer
split field position was wrong in this driver's headers — the DCE 6 form
(`<< 20`) had been copied into the Evergreen defines, so the split write
hit read-only bits *and* cleared the partner CRTC's setting. Completely
invisible with one head. Under Track A it would have shown up as the
second monitor starving, and it would have been blamed on the span code.
That is exactly the class of bug §7 predicted.

**Second, span modes have a bandwidth ceiling that is not about
watermarks.** A clock probe added in the same pass found the test card
running at PowerPlay level 1 — 100 MHz engine / 150 MHz memory — for about
1680 MB/s of DRAM bandwidth, because nothing raises it off the
VBIOS-posted state. Span modes are the worst case for this, since a
double-width surface roughly doubles average scanout demand:

| Span mode | Average scanout | vs 1680 MB/s at level 1 |
|---|---|---|
| 2 × 1920×1080 → 3840×1080 | ~1036 MB/s | fits |
| 2 × 1920×1200 → 3840×1200 | ~1136 MB/s | fits |
| 2 × 2560×1440 → 5120×1440 | ~1818 MB/s | **over budget** |

So dual 1080p or 1200p span is viable on a level-1 card, but anything
larger is not — and the same board advertises 900 MHz memory (≈10080
MB/s) at levels 0 and 2. **Power management is therefore a prerequisite
for span modes above 1200p, not just for lifting the pixel-clock caps.**
See `docs/scanout-watermark-investigation.md` §9 for the target level and
why voltage has to move first.

The practical effect on the plan: Milestones A1 and A2 are unaffected —
clone and dual-1080p span fit inside the current bandwidth. A3 and any
large-monitor span should wait for the clock work.

### 7.1 Correction, 2026-07-31 — the budget is per-board, and 1680 MB/s was optimistic

Hardware testing revised the table above **downward**. The 1680 MB/s figure
came from the Turks at PowerPlay level 1; it is not a floor that every board
shares. On the **Caicos** test card `bandwidth_program_watermarks` reported
only **866 MB/s available**, because that board is parked at a memory clock of
154.8 MHz out of an advertised 900:

| Mode, 2 heads | Per-head average | Total | vs 866 MB/s |
|---|---|---|---|
| 2 × 1024×768 | 198 MB/s | 396 | fits |
| 2 × 1280×1024 | 327 MB/s | 654 | fits |
| 2 × 1600×900 | 363 MB/s | 726 | fits |
| 2 × 1920×1080 | **518 MB/s** | **1036** | **over budget** |

So **2×1080p does not fit on a board at its memory floor** — the row marked
"fits" above is wrong for Caicos. Confirmed visually: at 2×1080p the second
head is garbled and the driver itself reports `latency … (NOT hidden by line
buffer)`; at 1600×900 and below both heads are clean and the latency is
hidden. Dual-head on that card tops out at **1600×900**.

Bonaire, by contrast, drove 2×1080p clone *and* 3840×1080 span cleanly.

Two lessons: the shape of the argument holds (span roughly doubles average
scanout demand, and power management is the real unlock), but **the specific
budget has to be read per board out of `bandwidth_program_watermarks` rather
than assumed** — and the driver's own "hidden by line buffer" flag turned out
to predict the visible corruption exactly, which makes it a usable
go/no-go signal.

---

## 8. Track B: what it would take, and where to push

Listed for completeness and because it defines what "real" support
means. All of this is **outside** this fork's driver-only scope
(`feedback_radeon_driver_only_scope`) except item 3.

1. **Land the accelerant API v2.** Gerrit change
   [329](https://review.haiku-os.org/c/haiku/+/329) — "Add a 'display
   ID' to most accelerant hooks", Alexander von Gluck IV, uploaded
   2018-07-12, last touched 2021-07-19, **status NEW (open, unmerged),
   header-only, no implementation**. It:
   - bumps `B_ACCELERANT_VERSION` 1 → 2;
   - adds `B_ACCELERANT_GET_HEAD_COUNT` / `get_head_count()`;
   - prefixes `uint8 displayID` onto ~18 hook typedefs
     (`accelerant_mode_count`, `get_mode_list`, `propose_display_mode`,
     `set_display_mode`, `get_display_mode`, `get_frame_buffer_config`,
     `get_pixel_clock_limits`, `move_display_area`,
     `get_timing_constraints`, `set_indexed_colors`, all three DPMS
     hooks, `get_preferred_display_mode`, `get_monitor_info`,
     `get_edid_info`, `set_brightness`, `get_brightness`);
   - adds `x_display_position` / `y_display_position` to
     `frame_buffer_config` — i.e. each head declares where it sits;
   - restructures `monitor_info` into `.constraints` and a new
     `.hardware { crtc, encoder_type, connector_type, dpcm }`.

   Note that it does **not** version-negotiate: bumping
   `B_ACCELERANT_VERSION` and changing every signature breaks all 15
   in-tree accelerants plus every out-of-tree one simultaneously. A
   landable version needs either a v1 shim in `AccelerantHWInterface` or
   a parallel `B_ACCELERANT2_*` hook space (X512's "accelerant2" plan,
   thread #16). That design question is probably why it never merged,
   and resolving it is the single highest-leverage contribution
   available here.

2. **app_server.** Load N accelerants in `_ScanDrivers()`; make
   `VirtualScreen::AddScreen()` aggregate rather than overwrite;
   implement `RemoveScreen()`; make `UpdateFrame()` honour real screen
   positions; implement `AS_GET_NEXT_SCREEN_ID`; give `Window` a
   reassignable `DrawingEngine`; decide multiplexing-`HWInterface`
   (axeld) vs. per-screen `Desktop` (X512); route input/cursor across
   screen boundaries. `test_app_server` already supports multiple
   displays, so all of this is developable without hardware.

3. **`radeon_hd` v2 hooks** *(the only fork-scope item)*: thread
   `displayID` through the hooks listed in §4.2 — per-display mode list
   from that display's EDID, per-display preferred mode, per-display
   `get_frame_buffer_config` with `x/y_display_position`, per-display
   DPMS and brightness, and populate `monitor_info.hardware`
   (`crtc`, `encoder_type`, `connector_type`, and `dpcm` straight from
   the EDID physical size the driver already parses). Given §4.1, this
   is the *cheapest* part of Track B.

4. **Screen preferences.** Per-screen mode selection, a drag-to-arrange
   layout view, a primary-screen choice. The existing `MonitorView` is
   hardcoded to `B_MAIN_SCREEN_ID`.

5. **Interface Kit / Deskbar.** `BAlert` and other centred windows must
   target the screen that owns the parent window, not the bounding box;
   Deskbar's Twitcher already assumes a "main screen" (jscipione #125);
   `BWindow::ScreenChanged()` needs to fire on screen moves;
   `BDirectWindow` needs thought (Rockford #148).

6. **Hot-plug.** HPD interrupt in the kernel driver → notification to
   app_server → `RemoveScreen()`/`AddScreen()` at runtime, plus window
   rescue from a disappearing screen (Polli's complaint in #3).

7. **Per-display DPI/scaling** — a new Haiku feature, gated on 2 and 5
   above, with an unresolved design (§2.4).

---

## 9. Recommended sequence

| # | Step | Scope | Blocked on |
|---|---|---|---|
| 1 | ✅ Watermark/arbitration Phase B | fork | done 2026-07-30 |
| 2 | 🔨 A1 — clone/mirror on two heads | fork | 1 — **implemented 2026-07-31, awaiting hardware test** |
| 3 | PLL allocator for DCE ≤ 3 (gated to DCE ≥ 4 for now) | fork | 2 |
| 4 | A2 — horizontal span (≤ 1200p heads) + `B_PROPOSE_DISPLAY_MODE` tunnel | fork | 2, 3 |
| 5 | ✅ Clock/power management — raise off PowerPlay level 1 | fork | investigated 2026-07-31; engine raise opt-in, memory reclock needs DPM/SMC |
| 6 | A3 — vertical span, larger heads, unequal-size opt-in | fork | 4, 5 |
| 7 | Write up the mode taxonomy + register findings in `docs/` | fork | 4 |
| 8 | Resolve API-v2 versioning and revive Gerrit 329 | upstream | discussion |
| 9 | `radeon_hd` v2 hooks behind the new API | fork | 8 |
| 10 | app_server multi-screen (develop in `test_app_server`) | upstream | 8 |

Note step 5's new position: it is not needed for clone or dual-1080p span,
but it gates anything larger (§7.1).

**Update 2026-07-31 — step 5 is answered, and not in this plan's favour.**
The engine-clock raise works and is shipped opt-in, but memory reclocking
silently no-ops on Northern Islands through the legacy AtomBIOS path, and
lifting a board off its parked memory clock needs DPM/SMC work
(`power-management-investigation.md` §10–§11). Clone and dual-1080p/1200p
span are unaffected — they fit inside the current bandwidth. **Span above
1200p per head is now blocked on a DPM-sized project, not on a clock
write.** That is an argument for shipping A1 and A2 at the sizes that fit
and saying so plainly, rather than holding them for A3.

Implementation progress for steps 2–4 is logged in
[`multi-monitor-track-a.md`](multi-monitor-track-a.md).

Steps 1–7 are self-contained, testable on the hardware in
`TestHardware/`, and produce a shippable `.hpkg` that lights up a second
monitor on real Radeon cards for the first time. That is also the
strongest possible argument to put behind step 8 — the thread has had
six years of architecture debate and zero working modern hardware, and
PulkoMandy (#115, #140) has been explicit that a working simple thing
beats a perfect plan.

---

## 10. Source index

**Haiku app_server**
`src/servers/app/ScreenManager.cpp:193` (`_ScanDrivers`),
`ScreenManager.cpp:217` (`_AddHWInterface`),
`VirtualScreen.cpp:113-160` (`AddScreen`, TODO at `:153`),
`VirtualScreen.cpp:168` (`RemoveScreen`),
`VirtualScreen.cpp:176-193` (`UpdateFrame`),
`Screen.cpp:228` (`Frame()`),
`ServerApp.cpp:2960` (`AS_GET_NEXT_SCREEN_ID`),
`Desktop.cpp:3481` (`ScreenAt`),
`Window.cpp:95` (`fDrawingEngine`),
`drawing/interface/local/AccelerantHWInterface.cpp:548` (`SetMode`).

**Haiku accelerant API**
`headers/os/add-ons/graphics/Accelerant.h`;
Gerrit change 329 (open, header-only).

**Haiku Screen preferences**
`src/preferences/screen/ScreenMode.cpp:64` (`get_combine_mode`),
`src/preferences/screen/multimon.cpp` (settings tunnel),
`src/preferences/screen/ScreenWindow.cpp:480` (combine/swap/panel/TV UI).

**BeOS-era dual head**
`src/add-ons/accelerants/radeon/multimon.c`,
`src/add-ons/accelerants/radeon/ProposeDisplayMode.c:515`,
`headers/private/graphics/radeon/accelerant_ext.h:33`;
`src/add-ons/accelerants/{matrox,nvidia,via}/` (Rudolf Cornelissen's
dual-head drivers).

**radeon_hd**
`accelerant.h:25` (`MAX_DISPLAY`),
`display.cpp:42` (`init_registers`, CRTC 0-5),
`display.cpp:245` (`detect_displays`),
`display.cpp:855-872` (viewport/offset writes),
`mode.cpp:43` (`create_mode_list`),
`mode.cpp:168` (`radeon_dpms_set_hook`),
`mode.cpp:177` (`radeon_set_display_mode`, `crtcID = 0`),
`pll.cpp` (`pll_pick`, `pll_next_available`, `pll_usage_mask`),
`encoder.cpp` (`encoder_assign_crtc`, `encoder_pick_dig`),
`headers/private/graphics/radeon_hd/evergreen_reg.h:49-54` (six CRTC
offsets),
kernel `radeon_hd.cpp:795` (VRAM BAR mapping).

**Fork docs**
`docs/TODO.md` item 14 (the multi-monitor TODO cluster),
`docs/scanout-watermark-investigation.md` (Phase A findings),
`docs/technical-documentation.md:1281` ("the fork doesn't have a
multi-display setup to validate"), and tickets
[#12970](https://dev.haiku-os.org/ticket/12970) (HD 2600 Pro dual head),
[#11242](https://dev.haiku-os.org/ticket/11242),
[#8485](https://dev.haiku-os.org/ticket/8485).

**Discussion**
[How multiple displays should work?](https://discuss.haiku-os.org/t/how-multiple-displays-should-work/10126)
— 161 posts, 2020-11-21 → 2026-05-21.
