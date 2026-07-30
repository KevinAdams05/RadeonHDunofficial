/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 */


#include "bandwidth.h"

#include <string.h>

#include <OS.h>

#include "accelerant.h"
#include "accelerant_protos.h"


// Declared outside the TRACE gate: ERROR() is always on, so it needs
// _sPrintf even in a build with tracing compiled out.
extern "C" void _sPrintf(const char* format, ...);

#undef TRACE
#define TRACE_BANDWIDTH
#ifdef TRACE_BANDWIDTH
#	define TRACE(x...) _sPrintf("radeon_hd: " x)
#else
#	define TRACE(x...) ;
#endif

#define ERROR(x...) _sPrintf("radeon_hd: " x)


/*	Unit conventions, pinned explicitly because the reference material
	mixes fixed-point scales and getting this wrong produces plausible
	but useless watermarks:

		bandwidths		MB/s (10^6 bytes per second)
		times, latency	ns
		clocks			kHz (as stored in accelerant_info)
		pixel clocks	kHz (as carried in display_timing)

	Every intermediate product runs through uint64 so a 4K mode on an
	eight-channel board cannot overflow ahead of the divide.
*/

// Fixed memory-controller latency the display engine always has to absorb.
static const uint32 kMemoryControllerLatency = 2000;
	// ns

// Worst-case round trips the DMIF has to price in: a full 512-byte chunk
// eight deep, and a 128-byte cursor line pair four deep. Used to charge
// this head for the time the other heads spend on the bus.
static const uint32 kChunkBytes = 512 * 8;
static const uint32 kCursorPairBytes = 128 * 4;

// We never enable the scaler, so a destination line never draws on more
// than two source lines. (Scaled or interlaced output would allow four.)
static const uint32 kMaxSourceLinesPerDestLine = 2;

// We always scan out 32-bit. Deliberately left at 4 even for 15/16-bit
// modes: overstating the pixel size only makes the watermarks more
// conservative, and low-depth modes are not where starvation happens.
static const uint32 kBytesPerPixel = 4;

// Both halves of LATENCY_CONTROL are 16-bit fields.
static const uint32 kWatermarkMax = 65535;

// Number of CRTCs sharing one line buffer.
static const uint8 kCrtcsPerLineBuffer = 2;

// DMIF allocation handshake. Bounded because a board that never sets
// ALLOCATED_COMPLETED must not wedge the mode set.
static const uint32 kDmifPollAttempts = 100;
static const bigtime_t kDmifPollInterval = 10;

// The accelerant only ever programs the first CRTC pair, and gDisplay[]
// register maps are only initialized for attached displays — so the
// offsets come from here rather than from gDisplay[id]->regs, which is
// all zeroes for a CRTC that was never brought up.
static const uint32 kCrtcOffsets[kCrtcsPerLineBuffer] = {
	EVERGREEN_CRTC0_REGISTER_OFFSET,
	EVERGREEN_CRTC1_REGISTER_OFFSET
};


// Everything the watermark math needs about one head, in the units
// documented above.
struct bandwidth_params {
	uint32	memoryClock;
	uint32	engineClock;
	uint32	displayClock;
	uint32	pixelClock;
	uint32	dramChannels;
	uint32	activeHeads;
	uint32	lineBufferSize;		// pixels of latency hiding
	uint32	sourceWidth;		// pixels
	uint32	activeTime;
	uint32	blankTime;
};


/*!	DRAM channel count from the memory-controller channel map.

	Read here rather than cached at probe time because it is a single MMIO
	read and this is the only caller — which also keeps the read inside
	the DCE 4 / 5 gate, where the register is known to have this layout. */
static uint32
bandwidth_dram_channels()
{
	uint32 channelMap = Read32(MC, EVERGREEN_MC_SHARED_CHMAP);
	uint32 encoded = (channelMap & EVERGREEN_NOOFCHAN_MASK)
		>> EVERGREEN_NOOFCHAN_SHIFT;

	switch (encoded) {
		case 1:
			return 2;

		case 2:
			return 4;

		case 3:
			return 8;

		default:
			return 1;
	}
}


/*!	Has this CRTC actually been programmed with a mode?

	Deliberately not gDisplay[id]->powered: detect_displays() marks every
	attached display powered before any mode has been set, so a second
	monitor that is merely plugged in would otherwise inflate the head
	count and halve the first head's line buffer for nothing. */
static bool
bandwidth_crtc_is_active(uint8 crtcID)
{
	if (crtcID >= MAX_DISPLAY)
		return false;

	return gDisplay[crtcID]->attached
		&& gDisplay[crtcID]->currentMode.timing.pixel_clock != 0;
}


/*!	DRAM bandwidth, derated to the given efficiency in tenths.

	Evergreen and NI move four bytes per memory clock per channel. Linux
	prices the whole-chip ceiling at 70% efficiency and the display's own
	conservative share of it at 30%. */
static uint32
bandwidth_dram(const bandwidth_params& params, uint32 efficiencyTenths)
{
	return (uint32)((uint64)params.dramChannels * 4 * params.memoryClock
		* efficiencyTenths / 10000);
}


/*!	Bandwidth of a 32-byte-per-clock path, derated to efficiencyTenths.
	Serves both the data-return path (engine clock) and the DMIF
	(display clock). */
static uint32
bandwidth_from_clock(uint32 clock, uint32 efficiencyTenths)
{
	return (uint32)((uint64)32 * clock * efficiencyTenths / 10000);
}


/*!	The tightest of the three ceilings scanout has to live within. */
static uint32
bandwidth_available(const bandwidth_params& params)
{
	uint32 available = bandwidth_dram(params, 7);

	uint32 dataReturn = bandwidth_from_clock(params.engineClock, 8);
	if (dataReturn < available)
		available = dataReturn;

	uint32 dmif = bandwidth_from_clock(params.displayClock, 8);
	if (dmif < available)
		available = dmif;

	return available;
}


/*!	Average bandwidth this mode demands: one line's worth of pixels
	divided by the time a line takes. */
static uint32
bandwidth_mode_average(const bandwidth_params& params)
{
	uint32 lineTime = params.activeTime + params.blankTime;
	if (lineTime == 0)
		return 0;

	// Bytes per line over the line time in microseconds is bytes per
	// microsecond, which is already MB/s.
	return (uint32)((uint64)params.sourceWidth * kBytesPerPixel * 1000
		/ lineTime);
}


/*!	How far ahead of the scanout the line buffer has to run, in ns.

	The memory controller's own latency, plus the time the other heads
	(and their cursors) can hold the bus, plus the display pipe's own
	latency. If refilling the line buffer takes longer than the active
	part of a line, that overflow lands on top. */
static uint32
bandwidth_latency_watermark(const bandwidth_params& params)
{
	if (params.activeHeads == 0)
		return 0;

	uint32 available = bandwidth_available(params);
	if (available == 0 || params.displayClock == 0)
		return 0;

	uint32 chunkReturnTime = (uint32)((uint64)kChunkBytes * 1000 / available);
	uint32 cursorReturnTime
		= (uint32)((uint64)kCursorPairBytes * 1000 / available);
	uint32 displayPipeLatency = 40000000 / params.displayClock;

	uint32 latency = kMemoryControllerLatency
		+ (params.activeHeads + 1) * chunkReturnTime
		+ params.activeHeads * cursorReturnTime
		+ displayPipeLatency;

	// Rate the line buffer can actually be filled at: our share of the
	// bus, capped by what the display pipe itself can consume.
	uint32 fillBandwidth = available / params.activeHeads;
	uint32 pipeBandwidth = params.displayClock * kBytesPerPixel / 1000;
	if (pipeBandwidth < fillBandwidth)
		fillBandwidth = pipeBandwidth;

	if (fillBandwidth == 0)
		return latency;

	uint32 lineFillTime = (uint32)((uint64)kMaxSourceLinesPerDestLine
		* params.sourceWidth * kBytesPerPixel * 1000 / fillBandwidth);

	if (lineFillTime > params.activeTime)
		latency += lineFillTime - params.activeTime;

	return latency;
}


/*!	Can the line buffer absorb the computed latency?

	With no scaler and single-tap filtering, the buffer tolerates two
	lines of latency as long as it holds more than two source lines'
	worth of pixels; otherwise only one. */
static bool
bandwidth_latency_is_hidden(const bandwidth_params& params, uint32 latency)
{
	uint32 tolerantLines = 1;
	if (params.lineBufferSize > 2 * params.sourceWidth)
		tolerantLines = 2;

	uint32 lineTime = params.activeTime + params.blankTime;

	return latency <= tolerantLines * lineTime + params.blankTime;
}


/*!	Program the line-buffer split for one CRTC and return the resulting
	latency-hiding depth in pixels (0 if the CRTC is off).

	Each line buffer is shared by a CRTC pair. Bits 23:20 of
	DC_LB_MEMORY_SPLIT pick one of four partitions for the first CRTC of
	the pair; the second selects the mirrored partition by adding
	EVERGREEN_DC_LB_MEMORY_SPLIT_SECOND. A CRTC can only take the whole
	buffer while its partner is off.

	On DCE 4.1 and DCE 5 the DMIF buffer allocation has to be handed over
	explicitly, and the hardware acknowledges it in
	DMIF_BUFFERS_ALLOCATED_COMPLETED. Phase A found the VBIOS-left value
	varies by board — 0x11 on one Caicos, 0x00 on a Turks — so we always
	drive the handshake rather than trusting what we find. */
static uint32
bandwidth_line_buffer_adjust(uint8 crtcID, bool active, bool partnerActive)
{
	radeon_shared_info& info = *gInfo->shared_info;

	uint32 partition;
	uint32 buffers;
	if (!active) {
		partition = EVERGREEN_DC_LB_MEMORY_SPLIT_D1HALF_D2HALF;
		buffers = 0;
	} else if (partnerActive) {
		partition = EVERGREEN_DC_LB_MEMORY_SPLIT_D1HALF_D2HALF;
		buffers = 1;
	} else {
		partition = EVERGREEN_DC_LB_MEMORY_SPLIT_D1_ONLY;
		buffers = 2;
	}

	uint32 split = partition;
	if ((crtcID % kCrtcsPerLineBuffer) != 0)
		split += EVERGREEN_DC_LB_MEMORY_SPLIT_SECOND;

	// Bare value into bits 2:0 — NOT the DCE 6 shifted form. Other bits in
	// this register read back as status on some boards and are not ours to
	// preserve, which is why this is a plain write.
	Write32(OUT, EVERGREEN_DC_LB_MEMORY_SPLIT + kCrtcOffsets[crtcID],
		split & EVERGREEN_DC_LB_MEMORY_SPLIT_MASK);

	if (info.dceMajor >= 5 || (info.dceMajor == 4 && info.dceMinor >= 1)) {
		uint32 pipeOffset = crtcID * EVERGREEN_PIPE_REGISTER_STRIDE;
		uint32 control = EVERGREEN_PIPE0_DMIF_BUFFER_CONTROL + pipeOffset;

		Write32(OUT, control, EVERGREEN_DMIF_BUFFERS_ALLOCATED(buffers));

		bool completed = false;
		for (uint32 attempt = 0; attempt < kDmifPollAttempts; attempt++) {
			if ((Read32(OUT, control)
				& EVERGREEN_DMIF_BUFFERS_ALLOCATED_COMPLETED) != 0) {
				completed = true;
				break;
			}
			snooze(kDmifPollInterval);
		}

		if (!completed) {
			ERROR("%s: CRTC %" B_PRIu8 ": DMIF allocation of %" B_PRIu32
				" buffers never completed\n", __func__, crtcID, buffers);
		}
	}

	if (!active)
		return 0;

	// Latency-hiding depth of the partition we just selected. Only the
	// half and whole partitions are ever chosen above; the 3/4 and 1/4
	// partitions exist in hardware but need a scaler-aware policy to be
	// worth using.
	bool isDce5 = info.dceMajor >= 5;
	if (partition == EVERGREEN_DC_LB_MEMORY_SPLIT_D1_ONLY)
		return isDce5 ? 8192 * 2 : 7680 * 2;

	return isDce5 ? 4096 * 2 : 3840 * 2;
}


/*!	Program both latency-watermark slots and the priority counters for one
	CRTC. An inactive CRTC is programmed with zeroes so a previous mode's
	values cannot linger. */
static void
bandwidth_program_watermarks(uint8 crtcID, bool active,
	const bandwidth_params& params)
{
	uint32 latency = 0;
	uint32 lineTime = 0;
	uint32 priorityMark = 0;
	bool forceAlwaysOn = false;

	if (active) {
		latency = bandwidth_latency_watermark(params);
		lineTime = params.activeTime + params.blankTime;

		uint32 average = bandwidth_mode_average(params);
		uint32 available = bandwidth_available(params);
		uint32 displayShare = bandwidth_dram(params, 3);
		bool hidden = bandwidth_latency_is_hidden(params, latency);

		// Scanout has to outrank other clients outright when the mode
		// needs more than its share of DRAM, more than its share of the
		// tightest ceiling, or more latency than the line buffer can
		// cover. This is the path that matters on bandwidth-tight
		// boards.
		if (params.activeHeads != 0) {
			forceAlwaysOn = average > displayShare / params.activeHeads
				|| average > available / params.activeHeads
				|| !hidden;
		}

		TRACE("%s: CRTC %" B_PRIu8 ": %" B_PRIu32 " px wide @ %" B_PRIu32
			" kHz, %" B_PRIu32 " head(s), line buffer %" B_PRIu32 " px\n",
			__func__, crtcID, params.sourceWidth, params.pixelClock,
			params.activeHeads, params.lineBufferSize);
		TRACE("  bandwidth: available %" B_PRIu32 " MB/s, display share %"
			B_PRIu32 " MB/s, mode average %" B_PRIu32 " MB/s\n", available,
			displayShare, average);
		TRACE("  timing: active %" B_PRIu32 " ns, blank %" B_PRIu32 " ns\n",
			params.activeTime, params.blankTime);
		TRACE("  latency %" B_PRIu32 " ns (%s by line buffer)\n", latency,
			hidden ? "hidden" : "NOT hidden");

		// The priority counter is expressed in 16-pixel groups the line
		// buffer must lead the scanout by. latency(ns) * clock(kHz) /
		// 10^6 is that lead in pixels.
		priorityMark = (uint32)((uint64)latency * params.pixelClock
			/ 16000000);

		// Clamped rather than masked: a mark that overflowed the field
		// would otherwise wrap to a near-zero lead, which is the exact
		// opposite of what a demanding mode needs.
		if (priorityMark > EVERGREEN_PRIORITY_MARK_MASK)
			priorityMark = EVERGREEN_PRIORITY_MARK_MASK;
	}

	if (latency > kWatermarkMax)
		latency = kWatermarkMax;
	if (lineTime > kWatermarkMax)
		lineTime = kWatermarkMax;

	uint32 pipeOffset = crtcID * EVERGREEN_PIPE_ARBITRATION_STRIDE;
	uint32 arbitration = EVERGREEN_PIPE0_ARBITRATION_CONTROL3 + pipeOffset;
	uint32 selection = Read32(OUT, arbitration);

	// Slot A is for the high clock state and slot B for the low one. With
	// no power management there is only one clock state, so both slots get
	// the same values.
	for (uint32 slot = 1; slot <= 2; slot++) {
		uint32 select = selection
			& ~EVERGREEN_PIPE_LATENCY_WATERMARK_MASK(3);
		select |= EVERGREEN_PIPE_LATENCY_WATERMARK_MASK(slot);

		Write32(OUT, arbitration, select);
		Write32(OUT, EVERGREEN_PIPE0_LATENCY_CONTROL + pipeOffset,
			EVERGREEN_PIPE_LATENCY_LOW_WATERMARK(latency)
				| EVERGREEN_PIPE_LATENCY_HIGH_WATERMARK(lineTime));
	}

	// Put the slot selection back the way the hardware had it.
	Write32(OUT, arbitration, selection);

	uint32 priority = priorityMark;
	if (forceAlwaysOn)
		priority |= EVERGREEN_PRIORITY_ALWAYS_ON;

	Write32(OUT, EVERGREEN_PRIORITY_A_CNT + kCrtcOffsets[crtcID], priority);
	Write32(OUT, EVERGREEN_PRIORITY_B_CNT + kCrtcOffsets[crtcID], priority);

	TRACE("%s: CRTC %" B_PRIu8 ": watermark %" B_PRIu32 " ns, line time %"
		B_PRIu32 " ns, priority mark %" B_PRIu32 "%s\n", __func__, crtcID,
		latency, lineTime, priorityMark,
		forceAlwaysOn ? " (ALWAYS_ON)" : "");
}


void
bandwidth_update()
{
	radeon_shared_info& info = *gInfo->shared_info;

	// DCE 4 / 4.1 (Evergreen, Palm, Sumo) and DCE 5 (Northern Islands,
	// Cayman included) share this register layout. DCE 6 moved the
	// arbitration pair into the DPG block, and pre-DCE-4 parts have a
	// different arrangement again.
	if (info.dceMajor != 4 && info.dceMajor != 5)
		return;

	bandwidth_params params;
	memset(&params, 0, sizeof(params));

	params.memoryClock = gInfo->memoryClockFrequency;
	params.engineClock = gInfo->engineClockFrequency;
	params.displayClock = gInfo->displayClockFrequency;

	if (params.memoryClock == 0 || params.engineClock == 0
		|| params.displayClock == 0) {
		ERROR("%s: missing clock inputs (engine %" B_PRIu32 ", memory %"
			B_PRIu32 ", display %" B_PRIu32 " kHz), leaving arbitration "
			"alone\n", __func__, params.engineClock, params.memoryClock,
			params.displayClock);
		return;
	}

	params.dramChannels = bandwidth_dram_channels();

	params.activeHeads = 0;
	for (uint8 id = 0; id < MAX_DISPLAY; id++) {
		if (bandwidth_crtc_is_active(id))
			params.activeHeads++;
	}

	TRACE("%s: %" B_PRIu32 " active head(s), %" B_PRIu32 " DRAM channel(s)\n",
		__func__, params.activeHeads, params.dramChannels);

	for (uint8 id = 0; id < kCrtcsPerLineBuffer; id++) {
		uint8 partner = id ^ 1;
		bool active = bandwidth_crtc_is_active(id);
		bool partnerActive = bandwidth_crtc_is_active(partner);

		// The split has to be programmed before the watermarks — the
		// resulting line-buffer depth decides how much latency the
		// watermark is allowed to assume can be hidden.
		params.lineBufferSize
			= bandwidth_line_buffer_adjust(id, active, partnerActive);

		if (active) {
			display_mode& mode = gDisplay[id]->currentMode;

			params.pixelClock = mode.timing.pixel_clock;
			params.sourceWidth = mode.timing.h_display;
			params.activeTime = (uint32)((uint64)mode.timing.h_display
				* 1000000 / params.pixelClock);

			uint32 lineTime = (uint32)((uint64)mode.timing.h_total * 1000000
				/ params.pixelClock);
			params.blankTime = lineTime > params.activeTime
				? lineTime - params.activeTime : 0;
		} else {
			// Don't leave the previous head's mode behind for the next
			// iteration to trip over.
			params.pixelClock = 0;
			params.sourceWidth = 0;
			params.activeTime = 0;
			params.blankTime = 0;
		}

		bandwidth_program_watermarks(id, active, params);
	}
}


void
bandwidth_registers_dump(const char* stage)
{
	// The whole body is diagnostics: gating it here keeps the locals from
	// going unused in a build with tracing compiled out (STYLE_GUIDE §18).
#ifdef TRACE_BANDWIDTH
	radeon_shared_info& info = *gInfo->shared_info;

	if (info.dceMajor != 4 && info.dceMajor != 5)
		return;

	for (uint8 id = 0; id < kCrtcsPerLineBuffer; id++) {
		uint32 crtcOffset = kCrtcOffsets[id];
		uint32 pipeOffset = id * EVERGREEN_PIPE_ARBITRATION_STRIDE;

		TRACE("%s: CRTC %" B_PRIu8 " arbitration state (%s):\n", __func__,
			id, stage);
		TRACE("  DC_LB_MEMORY_SPLIT        0x%08" B_PRIx32 "\n",
			Read32(OUT, EVERGREEN_DC_LB_MEMORY_SPLIT + crtcOffset));
		TRACE("  PRIORITY_A_CNT            0x%08" B_PRIx32 "\n",
			Read32(OUT, EVERGREEN_PRIORITY_A_CNT + crtcOffset));
		TRACE("  PRIORITY_B_CNT            0x%08" B_PRIx32 "\n",
			Read32(OUT, EVERGREEN_PRIORITY_B_CNT + crtcOffset));
		TRACE("  ARBITRATION_CONTROL3      0x%08" B_PRIx32 "\n",
			Read32(OUT, EVERGREEN_PIPE0_ARBITRATION_CONTROL3 + pipeOffset));
		TRACE("  LATENCY_CONTROL           0x%08" B_PRIx32 "\n",
			Read32(OUT, EVERGREEN_PIPE0_LATENCY_CONTROL + pipeOffset));

		if (id > 0) {
			// Same registers at the DMIF stride, to catch a stride mix-up
			// (see the note on EVERGREEN_PIPE_ARBITRATION_STRIDE).
			uint32 wideOffset = id * EVERGREEN_PIPE_REGISTER_STRIDE;

			TRACE("  ARBITRATION_CONTROL3/x20  0x%08" B_PRIx32 "\n",
				Read32(OUT, EVERGREEN_PIPE0_ARBITRATION_CONTROL3
					+ wideOffset));
			TRACE("  LATENCY_CONTROL/x20       0x%08" B_PRIx32 "\n",
				Read32(OUT, EVERGREEN_PIPE0_LATENCY_CONTROL + wideOffset));
		}

		TRACE("  DMIF_BUFFER_CONTROL       0x%08" B_PRIx32 "\n",
			Read32(OUT, EVERGREEN_PIPE0_DMIF_BUFFER_CONTROL
				+ id * EVERGREEN_PIPE_REGISTER_STRIDE));
	}
#endif	// TRACE_BANDWIDTH
}
