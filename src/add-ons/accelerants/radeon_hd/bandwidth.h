/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 *
 * Display bandwidth arbitration for Evergreen (DCE 4), Palm / Sumo
 * (DCE 4.1) and Northern Islands (DCE 5).
 *
 * Scanout DMA competes with every other memory client on the card. Three
 * register groups decide whether it wins that fight: the line-buffer
 * split (how much latency each CRTC can hide), the latency watermarks
 * (how early the DMIF escalates to urgent requests), and the priority
 * counters (when the memory controller elevates scanout above other
 * clients). Left at reset, scanout has no starvation protection at all,
 * and the display FIFO underruns at high pixel clocks — stride-aliased
 * garbage at modes the chip can otherwise drive cleanly.
 *
 * Phase A of docs/scanout-watermark-investigation.md confirmed on Cedar,
 * Caicos and Turks hardware that the VBIOS/GOP leaves the whole block at
 * reset: zero watermarks, zero priority marks, and a half/half line
 * buffer split even with a single display attached. This file is Phase B
 * — it programs all three groups from the mode being set.
 *
 * DCE 6 and later are deliberately out of scope: Southern Islands moved
 * the arbitration pair into the DPG block (SI_DPG_PIPE_* in si_reg.h)
 * with a different layout, and no DCE 6+ board has been through the
 * investigation yet.
 */
#ifndef RADEON_HD_BANDWIDTH_H
#define RADEON_HD_BANDWIDTH_H


#include <SupportDefs.h>


/*! Recompute and program line-buffer split, latency watermarks and
	priority counters for every CRTC.

	Call at the end of a mode set, after the CRTC, PLL and encoder are
	programmed — the computation needs the mode that was actually applied.

	All CRTCs are reprogrammed on every call, not just the one whose mode
	changed: the line buffer is shared within a CRTC pair and the latency
	watermark scales with the number of active heads, so enabling or
	disabling one head changes the correct answer for the other.

	A no-op on anything outside DCE 4 / 4.1 / 5. Safe to call with no
	display attached. */
void bandwidth_update();


/*! Instrumentation: TRACE a read-back of every register
	bandwidth_update() touches, for both CRTCs of the first pair, tagged
	with a caller-supplied stage label.

	Both CRTCs are dumped regardless of which is active because they share
	a line buffer — the idle CRTC's split setting still constrains its
	partner. The arbitration/latency pair is read at both candidate pipe
	strides (0x10 and 0x20) so that a "the watermarks didn't take effect"
	report from the field can be told apart from "we wrote to the wrong
	address"; the two are identical for pipe 0, so the second read only
	happens for CRTC 1.

	Read-only — safe at any point in the mode-set sequence. */
void bandwidth_registers_dump(const char* stage);


#endif	// RADEON_HD_BANDWIDTH_H
