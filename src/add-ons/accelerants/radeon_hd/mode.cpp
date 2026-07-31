/*
 * Copyright 2006-2013, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Support for i915 chipset and up based on the X driver,
 * Copyright 2006-2007 Intel Corporation.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexander von Gluck, kallisti5@unixzen.com
 *		Kevin Adams <kevinadams05@gmail.com>
 */


#include "mode.h"

#include <create_display_modes.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "accelerant.h"
#include "accelerant_protos.h"
#include "bandwidth.h"
#include "bios.h"
#include "connector.h"
#include "display.h"
#include "displayport.h"
#include "encoder.h"
#include "hdmi.h"
#include "multimon_tunnel.h"
#include "pll.h"
#include "powerplay.h"
#include "utility.h"


extern "C" void _sPrintf(const char* format, ...);

#define TRACE_MODE
#ifdef TRACE_MODE
#	define TRACE(x...) _sPrintf("radeon_hd: " x)
#else
#	define TRACE(x...) ;
#endif

#define ERROR(x...) _sPrintf("radeon_hd: " x)


// Defined further down, alongside the public is_mode_supported() it backs.
static bool is_mode_supported_on_display(display_mode* mode, uint32 crtid);


/*!	Is this a horizontal-span mode?

	The test is Screen preferences' own, from get_combine_mode() in
	ScreenMode.cpp: B_SCROLL set, and a virtual_width of exactly twice the
	per-head h_display. Matching it exactly matters — app_server and Screen
	preferences decide a mode is "combined" by this rule, so if we programmed
	span on any other basis the two would disagree about the desktop size. */
static bool
is_horizontal_span_mode(const display_mode* mode)
{
	if ((mode->flags & B_SCROLL) == 0)
		return false;

	return mode->virtual_width == mode->timing.h_display * 2
		&& mode->virtual_height == mode->timing.v_display;
}


/*!	Add horizontal-span variants of the offered modes.

	A span mode is an ordinary mode — same timing, same per-head pixel clock —
	carrying a framebuffer twice as wide and the B_SCROLL flag. app_server
	derives the desktop size from virtual_width all by itself
	(Screen::Frame()), so advertising these is all it takes for the desktop to
	become 2 x wide; the driver's part is pointing the second CRTC at the
	right-hand half, which display_crtc_fb_set() does from
	display_info::viewportOriginX.

	Gated on span_displays, off by default, for the same reason clone is: a
	second head that mode-sets badly costs the user the working display they
	already had.

	create_display_modes() owns the area it returns, so growing the list means
	building a new area and handing the old one back. */
static status_t
add_span_modes(void)
{
	radeon_shared_info& info = *gInfo->shared_info;

	if (!radeon_setting_enabled("span_displays"))
		return B_OK;

	// Span needs a real second head. Without one the extra modes would be
	// offered but only ever show their left half.
	if (!gDisplay[1]->attached) {
		TRACE("%s: span_displays set but only one display attached - not "
			"offering span modes\n", __func__);
		return B_OK;
	}

	if (info.dceMajor < 4) {
		TRACE("%s: span_displays set, but DCE %" B_PRIu8 " has no PLL "
			"allocator - not offering span modes\n", __func__, info.dceMajor);
		return B_OK;
	}

	uint32 baseCount = info.mode_count;
	display_mode* baseList = gInfo->mode_list;

	// Which of the offered modes can actually be spanned? Both heads have to
	// accept the timing, and the double-width surface has to fit the mapped
	// framebuffer.
	display_mode* spanList = (display_mode*)malloc(
		baseCount * sizeof(display_mode));
	if (spanList == NULL)
		return B_NO_MEMORY;

	uint32 spanCount = 0;
	for (uint32 i = 0; i < baseCount; i++) {
		display_mode candidate = baseList[i];

		// Already a span mode, or too wide to double without overflowing the
		// 16-bit virtual_width.
		if (is_horizontal_span_mode(&candidate)
			|| candidate.timing.h_display > 0x7fff)
			continue;

		candidate.virtual_width = candidate.timing.h_display * 2;
		candidate.virtual_height = candidate.timing.v_display;
		candidate.flags |= B_SCROLL;

		if (!is_mode_supported_on_display(&candidate, 0)
			|| !is_mode_supported_on_display(&candidate, 1))
			continue;

		// Bytes per row is derived from virtual_width, so a span surface costs
		// twice the memory of the same mode unspanned.
		//
		// shared_info.frame_buffer_size is in KILOBYTES, not bytes — the
		// kernel driver stores the BAR size that way and multiplies by 1024
		// wherever it needs an address range. Comparing bytes against it
		// directly makes every span mode look 1024x too expensive.
		uint32 bytesPerPixel = (candidate.space == B_CMAP8) ? 1
			: ((candidate.space == B_RGB15_LITTLE
				|| candidate.space == B_RGB16_LITTLE) ? 2 : 4);
		uint64 needed = (uint64)candidate.virtual_width
			* candidate.virtual_height * bytesPerPixel;
		uint64 available = (uint64)info.frame_buffer_size * 1024;
		if (needed > available) {
			TRACE("%s: %" B_PRIu16 "x%" B_PRIu16 " span needs %" B_PRIu64
				" bytes, framebuffer is %" B_PRIu64 " - skipping\n", __func__,
				candidate.virtual_width, candidate.virtual_height, needed,
				available);
			continue;
		}

		spanList[spanCount++] = candidate;
	}

	if (spanCount == 0) {
		TRACE("%s: no offered mode can be spanned\n", __func__);
		free(spanList);
		return B_OK;
	}

	uint32 totalCount = baseCount + spanCount;
	size_t size = (sizeof(display_mode) * totalCount + B_PAGE_SIZE - 1)
		& ~(B_PAGE_SIZE - 1);

	display_mode* combined = NULL;
	area_id area = create_area("radeon HD modes", (void**)&combined,
		B_ANY_ADDRESS, size, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (area < B_OK) {
		free(spanList);
		return area;
	}

	memcpy(combined, baseList, baseCount * sizeof(display_mode));
	memcpy(combined + baseCount, spanList, spanCount * sizeof(display_mode));
	free(spanList);

	// Publish the new list before dropping the old area, so nothing can
	// observe a mode_list pointing into freed space.
	area_id oldArea = gInfo->mode_list_area;
	gInfo->mode_list_area = area;
	gInfo->mode_list = combined;
	info.mode_list_area = area;
	info.mode_count = totalCount;
	delete_area(oldArea);

	TRACE("%s: added %" B_PRIu32 " span mode(s) to %" B_PRIu32
		" base mode(s)\n", __func__, spanCount, baseCount);

	return B_OK;
}


status_t
create_mode_list(void)
{
	// TODO: multi-monitor?  for now we use VESA and not gDisplay edid
	uint8 crtcID = 0;

	const color_space kRadeonHDSpaces[] = {B_RGB32_LITTLE, B_RGB24_LITTLE,
		B_RGB16_LITTLE, B_RGB15_LITTLE, B_CMAP8};

	gInfo->mode_list_area = create_display_modes("radeon HD modes",
		&gDisplay[crtcID]->edidData, NULL, 0, kRadeonHDSpaces,
		B_COUNT_OF(kRadeonHDSpaces), is_mode_supported, &gInfo->mode_list,
		&gInfo->shared_info->mode_count);
	if (gInfo->mode_list_area < B_OK)
		return gInfo->mode_list_area;

	gInfo->shared_info->mode_list_area = gInfo->mode_list_area;

	// Widen the list with span variants. A failure here is not fatal — the
	// base list is already usable, so fall through with what we have.
	if (add_span_modes() != B_OK)
		ERROR("%s: could not add span modes, continuing without them\n",
			__func__);

	return B_OK;
}


//	#pragma mark -


uint32
radeon_accelerant_mode_count(void)
{
	TRACE("%s\n", __func__);
	// TODO: multi-monitor?  we need crtcid here

	return gInfo->shared_info->mode_count;
}


status_t
radeon_get_mode_list(display_mode* modeList)
{
	TRACE("%s\n", __func__);
	// TODO: multi-monitor?  we need crtcid here
	memcpy(modeList, gInfo->mode_list,
		gInfo->shared_info->mode_count * sizeof(display_mode));
	return B_OK;
}


status_t
radeon_get_preferred_mode(display_mode* preferredMode)
{
	TRACE("%s\n", __func__);
	// TODO: multi-monitor?  we need crtcid here

	uint8_t crtc = 0;

	if (gDisplay[crtc]->preferredMode.virtual_width > 0
		&& gDisplay[crtc]->preferredMode.virtual_height > 0) {
		TRACE("%s: preferred mode was found for display %" B_PRIu8 "\n",
			__func__, crtc);
		memcpy(preferredMode, &gDisplay[crtc]->preferredMode,
			sizeof(gDisplay[crtc]->preferredMode));
		return B_OK;
	}

	return B_ERROR;
}


status_t
radeon_get_edid_info(void* info, size_t size, uint32* edid_version)
{
	// TODO: multi-monitor?  for now we use display 0
	uint8 crtcID = 0;

	TRACE("%s\n", __func__);
	if (!gInfo->shared_info->has_edid)
		return B_ERROR;
	if (size < sizeof(struct edid1_info))
		return B_BUFFER_OVERFLOW;

	//memcpy(info, &gInfo->shared_info->edid_info, sizeof(struct edid1_info));
		// VESA
	memcpy(info, &gDisplay[crtcID]->edidData, sizeof(struct edid1_info));
		// Display 0

	*edid_version = EDID_VERSION_1;

	return B_OK;
}


uint32
radeon_dpms_capabilities(void)
{
	// These should be pretty universally supported on Radeon HD cards
	return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}


uint32
radeon_dpms_mode(void)
{
	// TODO: this really isn't a good long-term solution
	// we may need to look at the encoder dpms scratch registers
	return gInfo->dpms_mode;
}


void
radeon_dpms_set(uint8 id, int mode)
{
	if (mode == B_DPMS_ON) {
		display_crtc_dpms(id, mode);
		encoder_dpms_set(id, mode);
	} else {
		encoder_dpms_set(id, mode);
		display_crtc_dpms(id, mode);
	}
	gInfo->dpms_mode = mode;
}


/*!	Has this head actually been programmed with a mode?

	The same test bandwidth_crtc_is_active() uses, and for the same reason:
	gDisplay[]->powered is set for every attached display before any mode has
	been set, so it cannot distinguish "driving a monitor" from "a monitor is
	plugged in". A head that is attached but not driven — clone turned off, or
	a second monitor that could not take the mode — must read as dark. */
static bool
display_is_lit(uint32 crtcID)
{
	if (crtcID >= MAX_DISPLAY)
		return false;

	return gDisplay[crtcID]->attached
		&& gDisplay[crtcID]->currentMode.timing.pixel_clock != 0;
}


void
radeon_dpms_set_hook(int mode)
{
	// Every head this configuration lit, not every attached head: powering an
	// attached-but-undriven head here would scan out whatever stale timing
	// its CRTC happens to hold.
	bool anyHead = false;
	for (uint32 crtcID = 0; crtcID < MAX_DISPLAY; crtcID++) {
		if (!display_is_lit(crtcID))
			continue;

		radeon_dpms_set(crtcID, mode);
		anyHead = true;
	}

	if (!anyHead) {
		// Before the first mode set there is no lit head, and app_server does
		// call this on the way in; fall back to the historical behavior so a
		// pre-mode-set DPMS request is not silently dropped.
		if (gDisplay[0]->attached)
			radeon_dpms_set(0, mode);
	}
}


/*!	Release every PLL assignment ahead of a mode set.

	pll_pick() allocates out of pll_usage_mask(), which is built from the PLL
	ids still recorded on gConnector[] — and nothing ever clears them, so the
	mask only ever grows. With one head that was harmless: the allocator just
	alternated between PPLL1 and PPLL2 across successive mode sets. With two
	heads it is fatal. The first mode set takes PPLL1 and PPLL2; the second
	finds both "in use", gets ATOM_PPLL_INVALID back for head 0, and programs
	a head with no PLL.

	A mode set reprograms every head it is going to drive, so no PLL is
	genuinely in use at this point. Clearing first also means the DCE 6.1+
	DisplayPort sharing path in pll_pick() only ever sees assignments made
	during this pass. */
static void
release_pll_assignments()
{
	for (uint32 id = 0; id < ATOM_MAX_SUPPORTED_DEVICE; id++) {
		if (gConnector[id]->valid)
			gConnector[id]->encoder.pll.id = ATOM_PPLL_INVALID;
	}
}


/*!	Choose which heads this mode set drives — Track A milestone A1, clone.

	Head 0 is always driven; that is what this driver has always done. A
	second head joins it in clone (mirror) only when all of the following
	hold:

	- **`clone_displays` is set** in the driver settings. Off by default:
	  until the hardware matrix is covered, a second head that mode-sets
	  badly would cost the user the working display they already had.
	- **The card is DCE 4 or later.** pll_pick()'s pre-DCE-4 path ends in an
	  unconditional `pll->id = ATOM_PPLL1` with no allocator behind it, so two
	  heads would both claim PPLL1. Teaching that path to allocate is separate
	  work — see docs/multi-monitor-analysis.md §6.4.
	- **The monitor on it accepts this exact mode.** Clone drives every head
	  from one display_mode, so a mode outside the second monitor's EDID
	  ranges, or over its connector's pixel-clock ceiling, cannot be sent to
	  it.

	A head that fails any of these is left dark rather than failing the mode
	set — the degradation rule the BeOS-era radeon driver applied in
	Radeon_VerifyMultiMode(): never fail a mode set because a secondary head
	could not join it.

	Returns the number of heads selected, always at least one. */
static uint32
select_clone_heads(display_mode* mode, bool* heads, bool span)
{
	radeon_shared_info &info = *gInfo->shared_info;

	for (uint32 id = 0; id < MAX_DISPLAY; id++)
		heads[id] = false;

	heads[0] = true;
	uint32 headCount = 1;

	// A span mode is itself the request for two heads — app_server has already
	// sized the desktop to virtual_width — so it does not also need the clone
	// gate. Clone, which is indistinguishable from a plain single-head mode
	// from the mode alone, does.
	if (!span && !radeon_setting_enabled("clone_displays"))
		return headCount;

	if (info.dceMajor < 4) {
		TRACE("%s: %s set, but DCE %" B_PRIu8 " has no PLL allocator - "
			"driving head 0 only\n", __func__,
			span ? "span mode requested" : "clone_displays", info.dceMajor);
		return headCount;
	}

	for (uint32 id = 1; id < MAX_DISPLAY; id++) {
		if (!gDisplay[id]->attached)
			continue;

		if (!is_mode_supported_on_display(mode, id)) {
			TRACE("%s: display %" B_PRIu32 " cannot take %" B_PRIu16 "x%"
				B_PRIu16 " at %" B_PRIu32 " kHz - leaving it dark\n", __func__,
				id, mode->virtual_width, mode->virtual_height,
				mode->timing.pixel_clock);
			continue;
		}

		heads[id] = true;
		headCount++;
	}

	TRACE("%s: %s across %" B_PRIu32 " head(s)\n", __func__,
		span ? "spanning" : "cloning", headCount);

	return headCount;
}


/*!	Give each driven head its origin in the shared surface.

	Clone puts every head at (0,0). Horizontal span leaves the left head at 0
	and starts the right head one screen-width in — that offset, written to
	VIEWPORT_START by display_crtc_fb_set(), is the entire span mechanism.

	Heads that are not driven are reset to 0 too, so a later single-head mode
	set cannot inherit a stale offset and scan out from the middle of the
	surface. */
static void
assign_viewport_origins(display_mode* mode, bool* heads, bool span)
{
	for (uint32 id = 0; id < MAX_DISPLAY; id++) {
		gDisplay[id]->viewportOriginX = 0;
		gDisplay[id]->viewportOriginY = 0;
	}

	if (!span)
		return;

	// Collect the driven heads in head order.
	uint32 driven[MAX_DISPLAY];
	uint32 drivenCount = 0;
	for (uint32 id = 0; id < MAX_DISPLAY; id++) {
		if (heads[id])
			driven[drivenCount++] = id;
	}

	if (drivenCount < 2) {
		// Degraded rather than failed, the same rule clone follows: the mode
		// still sets, the user just sees the left half of a desktop that
		// app_server already believes is twice as wide.
		TRACE("%s: span mode with only %" B_PRIu32 " head(s) - showing the "
			"left half only\n", __func__, drivenCount);
		return;
	}

	uint32 leftHead = driven[0];
	uint32 rightHead = driven[1];

	// "Swap displays" in Screen preferences, arriving through the settings
	// tunnel, decides which physical monitor is the left half.
	if (gInfo->swapDisplays) {
		uint32 swap = leftHead;
		leftHead = rightHead;
		rightHead = swap;
	}

	gDisplay[leftHead]->viewportOriginX = 0;
	gDisplay[rightHead]->viewportOriginX = mode->timing.h_display;

	TRACE("%s: span: head %" B_PRIu32 " shows x=0, head %" B_PRIu32 " shows x=%"
		B_PRIu16 "%s\n", __func__, leftHead, rightHead, mode->timing.h_display,
		gInfo->swapDisplays ? " (swapped)" : "");
}


/*!	Run the mode-set sequence on one head.

	Lifted unchanged out of radeon_set_display_mode() so clone mode can run it
	per head. The caller owns everything global: the encoder output lock, the
	PLL release, clearing the recorded mode of heads that drop out, the HDMI
	infoframe pass and the bandwidth update. (This head's own currentMode is
	published from inside, by display_crtc_fb_set().) */
static void
set_mode_on_head(uint8 crtcID, display_mode* mode)
{
	uint32 connectorIndex = gDisplay[crtcID]->connectorIndex;

	TRACE("%s: display %" B_PRIu8 ", connector %" B_PRIu32 "\n", __func__,
		crtcID, connectorIndex);

	// Determine DP lanes if DP
	if (connector_is_dp(connectorIndex)) {
		dp_info *dpInfo = &gConnector[connectorIndex]->dpInfo;
		dpInfo->laneCount = dp_get_lane_count(connectorIndex, mode);
		dpInfo->linkRate = dp_get_link_rate(connectorIndex, mode);
	}

	// *** crtc and encoder prep
	display_crtc_lock(crtcID, ATOM_ENABLE);
	radeon_dpms_set(crtcID, B_DPMS_OFF);

	// *** Set up encoder -> crtc routing
	encoder_assign_crtc(crtcID);

	// *** CRT controler mode set
	// Set up PLL for connector
	pll_pick(connectorIndex);
#ifdef TRACE_MODE
	pll_info* pll = &gConnector[connectorIndex]->encoder.pll;
	TRACE("%s: pll %d selected for connector %" B_PRIu32 "\n", __func__,
		pll->id, connectorIndex);
#endif
	pll_set(mode, crtcID);

	display_crtc_set_dtd(crtcID, mode);

	display_crtc_fb_set(crtcID, mode);
	// atombios_overscan_setup
	display_crtc_scale(crtcID, mode);

	// *** encoder mode set
	encoder_mode_set(crtcID);

	// *** encoder and CRT controller commit
	radeon_dpms_set(crtcID, B_DPMS_ON);
	display_crtc_lock(crtcID, ATOM_DISABLE);
}


status_t
radeon_set_display_mode(display_mode* mode)
{
	TRACE("%s\n", __func__);
	TRACE("  mode->space: %#" B_PRIx32 "\n", mode->space);
	TRACE("  mode->virtual_width: %" B_PRIu16 "\n", mode->virtual_width);
	TRACE("  mode->virtual_height: %" B_PRIu16 "\n", mode->virtual_height);
	TRACE("  mode->h_display_start: %" B_PRIu16 "\n", mode->h_display_start);
	TRACE("  mode->v_display_start: %" B_PRIu16 "\n", mode->v_display_start);
	TRACE("  mode->flags: %#" B_PRIx32 "\n", mode->flags);

	if (gDisplay[0]->attached == false)
		return B_ERROR;

	// Re-validate the requested mode. is_mode_supported() filters the
	// offered mode list, but app_server can also restore a previously-
	// saved mode that's no longer in the list (e.g. after a driver
	// update tightens the caps). Reject those here so persisted user
	// preferences can't bypass the per-chip pixel-clock caps.
	if (!is_mode_supported(mode))
		return B_BAD_VALUE;

	bool span = is_horizontal_span_mode(mode);
	if (span) {
		TRACE("%s: horizontal span requested: %" B_PRIu16 " wide surface, %"
			B_PRIu16 " per head\n", __func__, mode->virtual_width,
			mode->timing.h_display);
	}

	bool heads[MAX_DISPLAY];
	uint32 headCount = select_clone_heads(mode, heads, span);

	assign_viewport_origins(mode, heads, span);

	// Clear the recorded mode of every head this configuration will not drive.
	// display_crtc_fb_set() publishes currentMode for the heads that are
	// programmed, but nothing clears it for one that drops out — and
	// bandwidth_update() counts exactly that field to decide the line-buffer
	// split, so a stale entry would let a head that is now dark keep claiming
	// half the line buffer.
	for (uint32 id = 0; id < MAX_DISPLAY; id++) {
		if (!heads[id])
			memset(&gDisplay[id]->currentMode, 0, sizeof(display_mode));
	}

	release_pll_assignments();

	encoder_output_lock(true);

	for (uint32 id = 0; id < MAX_DISPLAY; id++) {
		if (heads[id])
			set_mode_on_head(id, mode);
	}

	// Blank any attached head this configuration does not drive, so turning
	// clone back off — or landing on a mode the second monitor cannot take —
	// leaves that monitor dark instead of scanning out a stale timing.
	for (uint32 id = 0; id < MAX_DISPLAY; id++) {
		if (!heads[id] && gDisplay[id]->attached
			&& gDisplay[id]->powered) {
			TRACE("%s: blanking undriven head %" B_PRIu32 "\n", __func__, id);
			radeon_dpms_set(id, B_DPMS_OFF);
		}
	}

	encoder_output_lock(false);

	// Always-on bug-report instrumentation, per lit HDMI head: program the
	// AVI infoframe and dump the HDMI/AFMT block, then dump it again at the
	// end of the mode set. Two samples rather than one because the 0.6.3
	// investigation turned on whether a later AtomBIOS call clobbers our
	// writes; the pair makes that visible in any user's syslog. Runs after
	// the output lock is released, matching the order the single-head path
	// has always used.
	for (uint32 id = 0; id < MAX_DISPLAY; id++) {
		if (!heads[id])
			continue;

		uint32 connectorIndex = gDisplay[id]->connectorIndex;
		if (gConnector[connectorIndex]->type == VIDEO_CONNECTOR_HDMIA) {
			hdmi_avi_infoframe_program(id);
			hdmi_registers_dump(id, "right after infoframe program");
		}
	}

	#ifdef TRACE_MODE
	// for debugging
	debug_dp_info();

	TRACE("D1CRTC_STATUS        Value: 0x%X\n",
		Read32(CRT, AVIVO_D1CRTC_STATUS));
	TRACE("D2CRTC_STATUS        Value: 0x%X\n",
		Read32(CRT, AVIVO_D2CRTC_STATUS));
	TRACE("D1CRTC_CONTROL       Value: 0x%X\n",
		Read32(CRT, AVIVO_D1CRTC_CONTROL));
	TRACE("D2CRTC_CONTROL       Value: 0x%X\n",
		Read32(CRT, AVIVO_D2CRTC_CONTROL));
	TRACE("D1GRPH_ENABLE        Value: 0x%X\n",
		Read32(CRT, AVIVO_D1GRPH_ENABLE));
	TRACE("D2GRPH_ENABLE        Value: 0x%X\n",
		Read32(CRT, AVIVO_D2GRPH_ENABLE));
	TRACE("D1SCL_ENABLE         Value: 0x%X\n",
		Read32(CRT, AVIVO_D1SCL_SCALER_ENABLE));
	TRACE("D2SCL_ENABLE         Value: 0x%X\n",
		Read32(CRT, AVIVO_D2SCL_SCALER_ENABLE));
	TRACE("D1CRTC_BLANK_CONTROL Value: 0x%X\n",
		Read32(CRT, AVIVO_D1CRTC_BLANK_CONTROL));
	TRACE("D2CRTC_BLANK_CONTROL Value: 0x%X\n",
		Read32(CRT, AVIVO_D1CRTC_BLANK_CONTROL));
	#endif

	// Second sample: values differing from the "right after infoframe
	// program" dump above mean something later in the mode-set path
	// (AtomBIOS DPMS / lock release) reprogrammed the HDMI block behind our
	// back — the failure mode the 0.6.3 stripe fix was chasing.
	for (uint32 id = 0; id < MAX_DISPLAY; id++) {
		if (!heads[id])
			continue;

		uint32 connectorIndex = gDisplay[id]->connectorIndex;
		if (gConnector[connectorIndex]->type == VIDEO_CONNECTOR_HDMIA) {
			hdmi_registers_dump(id, "end of mode set");
			hdmi_block_dump(id);
		}
	}

	// Scanout bandwidth arbitration. Must run after the CRTC and encoder
	// are programmed, because it derives the watermarks from the mode that
	// was actually applied — and after every head is programmed, because the
	// line-buffer split depends on how many heads ended up active. The
	// before/after dumps bracket it so a syslog alone shows both what the
	// VBIOS left and what we wrote.
	bandwidth_registers_dump("before bandwidth update");
	bandwidth_update();
	bandwidth_registers_dump("after bandwidth update");

	TRACE("%s: mode set complete on %" B_PRIu32 " head(s)\n", __func__,
		headCount);

	return B_OK;
}


status_t
radeon_get_display_mode(display_mode* _currentMode)
{
	TRACE("%s\n", __func__);

	*_currentMode = gInfo->shared_info->current_mode;
	//*_currentMode = gDisplay[X]->currentMode;
	return B_OK;
}


/*!	Is this ProposeMode call actually a multi-monitor settings request?

	Screen preferences smuggles settings through B_PROPOSE_DISPLAY_MODE by
	handing us a display_mode that could never be a real mode. Recognising it
	has to be exact — a false positive would silently eat a legitimate mode
	proposal. See multimon_tunnel.h for the protocol. */
static bool
is_multimon_tunnel(const display_mode* target, const display_mode* low,
	const display_mode* high)
{
	return target->space == 0 && low->space == 0 && high->space == 0
		&& low->virtual_width == 0xffff && low->virtual_height == 0xffff
		&& high->virtual_width == 0 && high->virtual_height == 0
		&& target->timing.pixel_clock == 0
		&& low->timing.pixel_clock == RADEON_TUNNEL_SENTINEL_LOW
		&& high->timing.pixel_clock == RADEON_TUNNEL_SENTINEL_HIGH;
}


/*!	Service one tunneled multi-monitor setting.

	Only swap-displays is answered. Use-laptop-panel and TV-standard are
	refused rather than faked: refusing makes Screen preferences hide those
	controls, which is honest, whereas answering would advertise controls that
	do nothing. */
static status_t
handle_multimon_tunnel(display_mode* target)
{
	uint16 setting = target->h_display_start;
	uint16 operation = target->v_display_start;

	switch (setting) {
		case RADEON_TUNNEL_SETTING_SWAP:
			switch (operation) {
				case RADEON_TUNNEL_OP_GET:
					target->timing.flags = gInfo->swapDisplays ? 1 : 0;
					TRACE("%s: tunnel get swap -> %" B_PRIu32 "\n", __func__,
						target->timing.flags);
					return B_OK;
				case RADEON_TUNNEL_OP_SET:
				{
					gInfo->swapDisplays = target->timing.flags != 0;
					TRACE("%s: tunnel set swap -> %s\n", __func__,
						gInfo->swapDisplays ? "true" : "false");
					// Screen preferences does not follow a settings change
					// with its own SetMode, so re-apply the current mode here
					// or the swap would not take effect until something else
					// triggers a mode set. Copy it first: the mode set
					// rewrites shared_info->current_mode as it goes, so
					// handing it a pointer into that field would have it
					// reading from something it is concurrently overwriting.
					display_mode current = gInfo->shared_info->current_mode;
					return radeon_set_display_mode(&current);
				}
			}
			break;

		case RADEON_TUNNEL_SETTING_USE_LAPTOP_PANEL:
			// Panels are ordinary displays here; there is nothing to toggle.
			return B_ERROR;

		case RADEON_TUNNEL_SETTING_TV_STANDARD:
			// No TV-out support in this driver.
			return B_ERROR;
	}

	TRACE("%s: unhandled tunnel setting %#" B_PRIx16 ", operation %" B_PRIu16
		"\n", __func__, setting, operation);
	return B_ERROR;
}


/*!	B_PROPOSE_DISPLAY_MODE — adjust a requested mode to something this card can
	actually drive, or refuse it.

	Two unrelated jobs, because Screen preferences overloads this one hook.
	First the multi-monitor settings tunnel, including the support handshake
	that decides whether the "Combine displays" menu is visible at all
	(multimon_tunnel.h). Then ordinary mode proposal.

	The mode-proposal half deliberately does not invent timings. The offered
	mode list is already built and filtered from EDID, so the useful answer is
	"the closest listed mode that fits between low and high", and refusing
	anything else keeps this hook honest about the per-chip pixel-clock caps
	that is_mode_supported() enforces. */
status_t
radeon_propose_display_mode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	TRACE("%s\n", __func__);

	if (target == NULL || low == NULL || high == NULL)
		return B_BAD_VALUE;

	// The support handshake. Screen preferences sets REQUEST on a real mode
	// and reads back REPLY; answering it is what makes the combine UI appear.
	// Checked before the sentinel test because the probe rides on a genuine
	// mode rather than the magic one.
	if ((target->timing.flags & RADEON_MODE_MULTIMON_REQUEST) != 0
		&& (target->timing.flags & RADEON_MODE_MULTIMON_REPLY) == 0) {
		target->timing.flags &= ~RADEON_MODE_MULTIMON_REQUEST;
		target->timing.flags |= RADEON_MODE_MULTIMON_REPLY;
		TRACE("%s: answered multi-monitor support handshake\n", __func__);
		return B_OK;
	}

	if (is_multimon_tunnel(target, low, high))
		return handle_multimon_tunnel(target);

	// Ordinary mode proposal. Find the listed mode that matches the requested
	// virtual size and color space; fall back to the closest refresh rate
	// among modes of that size.
	const display_mode* best = NULL;
	for (uint32 i = 0; i < gInfo->shared_info->mode_count; i++) {
		const display_mode* candidate = &gInfo->mode_list[i];

		if (candidate->virtual_width != target->virtual_width
			|| candidate->virtual_height != target->virtual_height
			|| candidate->space != target->space)
			continue;

		if (candidate->timing.pixel_clock < low->timing.pixel_clock
			|| candidate->timing.pixel_clock > high->timing.pixel_clock)
			continue;

		if (best == NULL) {
			best = candidate;
			continue;
		}

		// Prefer the candidate whose pixel clock is closest to what was asked
		// for, so a request carrying a refresh rate is honored where possible.
		uint32 bestDelta = best->timing.pixel_clock > target->timing.pixel_clock
			? best->timing.pixel_clock - target->timing.pixel_clock
			: target->timing.pixel_clock - best->timing.pixel_clock;
		uint32 candidateDelta
			= candidate->timing.pixel_clock > target->timing.pixel_clock
				? candidate->timing.pixel_clock - target->timing.pixel_clock
				: target->timing.pixel_clock - candidate->timing.pixel_clock;

		if (candidateDelta < bestDelta)
			best = candidate;
	}

	if (best == NULL) {
		TRACE("%s: no supported mode matches %" B_PRIu16 "x%" B_PRIu16
			" space %#" B_PRIx32 "\n", __func__, target->virtual_width,
			target->virtual_height, target->space);
		return B_ERROR;
	}

	*target = *best;

	return B_OK;
}


status_t
radeon_get_frame_buffer_config(frame_buffer_config* config)
{
	TRACE("%s\n", __func__);

	config->frame_buffer = gInfo->shared_info->frame_buffer;
	config->frame_buffer_dma = (uint8*)gInfo->shared_info->frame_buffer_phys;

	config->bytes_per_row = gInfo->shared_info->bytes_per_row;

	TRACE("  config->frame_buffer: %#" B_PRIxADDR "\n",
		(phys_addr_t)config->frame_buffer);
	TRACE("  config->frame_buffer_dma: %#" B_PRIxADDR "\n",
		(phys_addr_t)config->frame_buffer_dma);
	TRACE("  config->bytes_per_row: %" B_PRIu32 "\n", config->bytes_per_row);

	return B_OK;
}


status_t
radeon_get_pixel_clock_limits(display_mode* mode, uint32* _low, uint32* _high)
{
	TRACE("%s\n", __func__);

	if (_low != NULL) {
		// lower limit of about 48Hz vertical refresh
		uint32 totalClocks = (uint32)mode->timing.h_total
			* (uint32)mode->timing.v_total;
		uint32 low = (totalClocks * 48L) / 1000L;

		if (low < PLL_MIN_DEFAULT)
			low = PLL_MIN_DEFAULT;
		else if (low > PLL_MAX_DEFAULT)
			return B_ERROR;

		*_low = low;
	}

	if (_high != NULL)
		*_high = PLL_MAX_DEFAULT;

	//*_low = 48L;
	//*_high = 100 * 1000000L;
	return B_OK;
}


/*!	Would this mode work on one specific head?

	The connector-type pixel-clock ceiling and the EDID frequency ranges are
	both properties of the display that is plugged in, not of the card, so
	the answer differs per head as soon as more than one is driven. Clone
	mode asks this of every candidate head before offering it the mode; the
	public is_mode_supported() below asks it of head 0 only, because that is
	the head whose EDID built the mode list. */
static bool
is_mode_supported_on_display(display_mode* mode, uint32 crtid)
{
	bool sane = true;

	// Validate modeline is within a sane range
	if (is_mode_sane(mode) != B_OK)
		sane = false;

	// Reject 1:1 (square) synthesized modes. These come out of Haiku's
	// shared accelerant helper create_display_modes() when an EDID
	// Standard Timing Identifier's aspect-ratio bits decode to "unknown"
	// and the parser falls through to width-equals-height. No real
	// consumer monitor advertises native 1280x1280 / 1440x1440 /
	// 1680x1680 timings; offering them in the candidate list just lets
	// the default-mode picker pick a square mode on a 16:9 panel by
	// pure pixel-count tiebreak.
	if (mode->virtual_width == mode->virtual_height
		&& mode->virtual_width > 1024) {
		TRACE("%s: rejecting square synthesized mode %" B_PRIu32
			"x%" B_PRIu32 " (likely create_display_modes aspect-ratio "
			"fallthrough)\n", __func__, mode->virtual_width,
			mode->virtual_height);
		sane = false;
	}

	// Validate pixel clock against connector type limits.
	// HDMI single-link TMDS maxes out at 165 MHz (pre-DCE6) or
	// 340 MHz (DCE6+ with HDMI 1.3+). DVI single-link has the same
	// 165 MHz limit; dual-link DVI supports up to 330 MHz.
	// Without this check, the driver may attempt pixel clocks the
	// hardware physically cannot produce (e.g. 533 MHz for 4K@60Hz
	// on a Cedar GPU over HDMI), causing a garbled display.
	uint32 connectorIndex = gDisplay[crtid]->connectorIndex;
	uint32 connectorType = gConnector[connectorIndex]->type;
	radeon_shared_info &info = *gInfo->shared_info;

	if (mode->timing.pixel_clock > 165000) {
		switch (connectorType) {
			case VIDEO_CONNECTOR_HDMIA:
				if (info.chipsetID >= RADEON_CAPEVERDE) {
					// DCE6+: HDMI 1.3+ supports up to 340 MHz single-link
					if (mode->timing.pixel_clock > 340000)
						sane = false;
				} else {
					// Pre-DCE6: HDMI limited to 165 MHz single-link TMDS
					sane = false;
				}
				break;
			case VIDEO_CONNECTOR_DVID:
			case VIDEO_CONNECTOR_DVII:
				// Single-link DVI: 165 MHz max. Only dual-link DVI
				// connectors can exceed this, and Haiku doesn't currently
				// distinguish single vs dual-link in connector enumeration.
				sane = false;
				break;
			case VIDEO_CONNECTOR_HDMIB:
				// HDMI-B is electrically dual-link DVI, up to 340 MHz
				if (mode->timing.pixel_clock > 340000)
					sane = false;
				break;
			default:
				break;
		}
	}

	// Per-chip memory-bandwidth caps for linear scanout.
	//
	// Haiku writes pixels into the framebuffer through the PCI BAR as a
	// linear surface and programs the chip with ARRAY_MODE_LINEAR_ALIGNED
	// (see Phase 3.2 / display_crtc_fb_set). On lower-end Northern Islands
	// silicon the memory bus is too narrow to sustain the read bandwidth
	// linear scanout needs at high pixel clocks. Symptoms above the cap
	// range from minor edge jitter (4K@30Hz) to severe stride-aliased
	// corruption (4K@60Hz).
	//
	// Linux's radeon driver works around this by using tiled scanout
	// (ARRAY_MODE = 2D_TILED_THIN1), which has dramatically better
	// memory-access locality. Tiled scanout requires app_server to write
	// pixels in tile order or use a translation layer — that change lives
	// outside the radeon_hd driver and is therefore out of scope for this
	// fork (which is distributed as a standalone .hpkg overlay).
	//
	// Caps are per-chip and empirical — picked as the highest pixel clock
	// validated as clean on real hardware:
	//
	//   Caicos (64-bit bus)   165 MHz   tops out around 1080p@75Hz
	//   Turks  (128-bit bus)  250 MHz   tops out around 1680x1680@60Hz
	//                                   (verified clean at 240261 kHz;
	//                                   cap set with 5 MHz headroom)
	//   Barts  (256-bit bus)  340 MHz   matches HDMI 1.4a native ceiling;
	//                                   4K@60Hz (533 MHz on DP) confirmed
	//                                   stride-aliased on HD 6850. Cap can
	//                                   move up once intermediate modes
	//                                   (4K@30, 1440p@60, 1080p@144) are
	//                                   tested empirically.
	//
	// Cayman (256-bit bus, same DCE 5 generation as Barts) shares this
	// scanout architecture but is left uncapped pending HW testing.
	//
	// Anything above these is rejected before it reaches the PLL/encoder
	// programming path. See Phase 4 in the RadeonHD technical
	// documentation for the full investigation.
	uint32 capKHz = 0;
	const char* capChipName = NULL;
	if (info.chipsetID == RADEON_CAICOS) {
		capKHz = 165000;
		capChipName = "Caicos";
	} else if (info.chipsetID == RADEON_TURKS) {
		capKHz = 250000;
		capChipName = "Turks";
	} else if (info.chipsetID == RADEON_BARTS) {
		capKHz = 340000;
		capChipName = "Barts";
	}
	if (capKHz != 0 && mode->timing.pixel_clock > capKHz) {
		// The caps are empirical, and re-deriving them needs over-cap modes
		// to be reachable. `ignore_pixel_clock_cap` in the driver settings
		// makes them advisory for that purpose only: it is off by default,
		// it is not a supported configuration, and the expected outcome of
		// enabling it is the stride-aliased corruption the caps exist to
		// avoid. See docs/power-management-investigation.md.
		if (powerplay_pixel_clock_cap_ignored()) {
			ERROR("%s: ALLOWING %" B_PRIu32 "x%" B_PRIu32 " on %s at %"
				B_PRIu32 " kHz, over the %" B_PRIu32 " MHz cap - "
				"'ignore_pixel_clock_cap' is set. Corruption is expected.\n",
				__func__, mode->virtual_width, mode->virtual_height,
				capChipName, mode->timing.pixel_clock, capKHz / 1000);
		} else {
			TRACE("%s: rejecting %" B_PRIu32 "x%" B_PRIu32 " on %s "
				"(pixel clock %" B_PRIu32 " kHz exceeds %" B_PRIu32
				" MHz linear-scanout cap)\n", __func__,
				mode->virtual_width, mode->virtual_height, capChipName,
				mode->timing.pixel_clock, capKHz / 1000);
			sane = false;
		}
	}

	// if we have edid info, check frequency adginst crt reported valid ranges
	if (gInfo->shared_info->has_edid
		&& gDisplay[crtid]->foundRanges) {

		// All four range fields are uint32. Phrasing the lower bound as
		// `freq < min - 1` underflows to 0xFFFFFFFF when min == 0 and
		// rejects every mode. Use `freq + 1 < min` to keep the same
		// ±1 unit tolerance without underflowing.

		// validate horizontal frequency range
		uint32 hfreq = mode->timing.pixel_clock / mode->timing.h_total;
		if (hfreq > gDisplay[crtid]->hfreqMax + 1
			|| hfreq + 1 < gDisplay[crtid]->hfreqMin) {
			TRACE("%s: hfreq %" B_PRIu32 " outside EDID range [%" B_PRIu32
				", %" B_PRIu32 "]\n", __func__, hfreq,
				gDisplay[crtid]->hfreqMin, gDisplay[crtid]->hfreqMax);
			sane = false;
		}

		// validate vertical frequency range
		uint32 vfreq = mode->timing.pixel_clock / ((mode->timing.v_total
			* mode->timing.h_total) / 1000);
		if (vfreq > gDisplay[crtid]->vfreqMax + 1
			|| vfreq + 1 < gDisplay[crtid]->vfreqMin) {
			TRACE("%s: vfreq %" B_PRIu32 " outside EDID range [%" B_PRIu32
				", %" B_PRIu32 "]\n", __func__, vfreq,
				gDisplay[crtid]->vfreqMin, gDisplay[crtid]->vfreqMax);
			sane = false;
		}
	}

	TRACE("MODE (display %" B_PRIu32 "): %" B_PRIu32
		" ; %d %d %d %d ; %d %d %d %d is %s\n", crtid,
		mode->timing.pixel_clock, mode->timing.h_display,
		mode->timing.h_sync_start, mode->timing.h_sync_end,
		mode->timing.h_total, mode->timing.v_display,
		mode->timing.v_sync_start, mode->timing.v_sync_end,
		mode->timing.v_total,
		sane ? "OK." : "BAD, out of range!");

	return sane;
}


bool
is_mode_supported(display_mode* mode)
{
	// Head 0 is the reference: its EDID is what create_mode_list() built the
	// offered list from, and this is the filter create_display_modes() calls.
	return is_mode_supported_on_display(mode, 0);
}


/*
 * A quick sanity check of the provided display_mode
 */
status_t
is_mode_sane(display_mode* mode)
{
	// horizontal timing
	// validate h_sync_start is less then h_sync_end
	if (mode->timing.h_sync_start > mode->timing.h_sync_end) {
		TRACE("%s: ERROR: (%dx%d) "
			"received h_sync_start greater then h_sync_end!\n",
			__func__, mode->timing.h_display, mode->timing.v_display);
		return B_ERROR;
	}
	// validate h_total is greater then h_display
	if (mode->timing.h_total < mode->timing.h_display) {
		TRACE("%s: ERROR: (%dx%d) "
			"received h_total greater then h_display!\n",
			__func__, mode->timing.h_display, mode->timing.v_display);
		return B_ERROR;
	}

	// vertical timing
	// validate v_start is less then v_end
	if (mode->timing.v_sync_start > mode->timing.v_sync_end) {
		TRACE("%s: ERROR: (%dx%d) "
			"received v_sync_start greater then v_sync_end!\n",
			__func__, mode->timing.h_display, mode->timing.v_display);
		return B_ERROR;
	}
	// validate v_total is greater then v_display
	if (mode->timing.v_total < mode->timing.v_display) {
		TRACE("%s: ERROR: (%dx%d) "
			"received v_total greater then v_display!\n",
			__func__, mode->timing.h_display, mode->timing.v_display);
		return B_ERROR;
	}

	// calculate refresh rate for given timings to whole int (in Hz)
	int refresh = mode->timing.pixel_clock * 1000
		/ (mode->timing.h_total * mode->timing.v_total);

	if (refresh < 30 || refresh > 250) {
		TRACE("%s: ERROR: (%dx%d) "
			"refresh rate of %dHz is unlikely for any kind of monitor!\n",
			__func__, mode->timing.h_display, mode->timing.v_display, refresh);
		return B_ERROR;
	}

	return B_OK;
}


uint32
get_mode_bpp(display_mode* mode)
{
	// Get bitsPerPixel for given mode

	switch (mode->space) {
		case B_CMAP8:
			return 8;
		case B_RGB15_LITTLE:
			return 15;
		case B_RGB16_LITTLE:
			return 16;
		case B_RGB24_LITTLE:
		case B_RGB32_LITTLE:
			return 32;
	}
	ERROR("%s: Unknown colorspace for mode, guessing 32 bits per pixel\n",
		__func__);
	return 32;
}


static uint32_t
radeon_get_backlight_register()
{
	// R600 and up is 0x172c else its 0x0018
	if (gInfo->shared_info->chipsetID >= RADEON_R600)
		return 0x172c;
	return 0x0018;
}


status_t
radeon_set_brightness(float brightness)
{
	TRACE("%s (%f)\n", __func__, brightness);

	if (brightness < 0 || brightness > 1)
		return B_BAD_VALUE;

	uint32_t backlightReg = radeon_get_backlight_register();
	uint8_t brightnessRaw = (uint8_t)ceilf(brightness * 255);
	uint32_t level = Read32(OUT, backlightReg);
	TRACE("brightness level = %lx\n", level);
	level &= ~ATOM_S2_CURRENT_BL_LEVEL_MASK;
	level |= (( brightnessRaw << ATOM_S2_CURRENT_BL_LEVEL_SHIFT )
					& ATOM_S2_CURRENT_BL_LEVEL_MASK);
	TRACE("new brightness level = %lx\n", level);

	Write32(OUT, backlightReg, level);

	//TODO crtcID = 0: see create_mode
	// TODO: multi-monitor?  for now we use VESA and not gDisplay edid
	uint8 crtcID = 0;
	//TODO : test if it is a LCD ?
	uint32 connectorIndex = gDisplay[crtcID]->connectorIndex;
	connector_info* connector = gConnector[connectorIndex];
	pll_info* pll = &connector->encoder.pll;

	transmitter_dig_setup(connectorIndex, pll->pixelClock,
		0, 0, ATOM_TRANSMITTER_ACTION_BL_BRIGHTNESS_CONTROL);
	transmitter_dig_setup(connectorIndex, pll->pixelClock,
		0, 0, ATOM_TRANSMITTER_ACTION_LCD_BLON);

	return B_OK;
}


status_t
radeon_get_brightness(float* brightness)
{
	TRACE("%s\n", __func__);

	if (brightness == NULL)
		return B_BAD_VALUE;

	uint32_t backlightReg = Read32(OUT, radeon_get_backlight_register());
	uint8_t brightnessRaw = ((backlightReg & ATOM_S2_CURRENT_BL_LEVEL_MASK) >>
			ATOM_S2_CURRENT_BL_LEVEL_SHIFT);
	*brightness = (float)brightnessRaw / 255;

	return B_OK;
}
