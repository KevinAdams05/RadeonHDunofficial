/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 */


#include "hdmi.h"

#include <string.h>

#include "accelerant.h"
#include "accelerant_protos.h"


extern "C" void _sPrintf(const char* format, ...);

#undef TRACE
#define TRACE_HDMI
#ifdef TRACE_HDMI
#	define TRACE(x...) _sPrintf("radeon_hd: " x)
#else
#	define TRACE(x...) ;
#endif

#define ERROR(x...) _sPrintf("radeon_hd: " x)


/*! Per-AFMT-block offset table — same six values Linux radeon's
	eg_offsets[] uses (radeon_display.c). MUST be indexed by the DIG
	encoder id from encoder_pick_dig(connectorIndex) — the DIG a
	connector uses is determined by its UNIPHY object + link
	enumeration in the AtomBIOS object table, and has nothing to do
	with which CRTC scans out to it. (Found the hard way on the
	AX5450: its HDMI port is UNIPHY1 link B = DIG3, while CRTC 0 does
	the scanout — indexing by CRTC programmed dormant DIG0 and left
	the live DIG3 AFMT unconfigured.) */
static const uint32 kAfmtOffsets[] = {
	EVERGREEN_AFMT0_OFFSET,
	EVERGREEN_AFMT1_OFFSET,
	EVERGREEN_AFMT2_OFFSET,
	EVERGREEN_AFMT3_OFFSET,
	EVERGREEN_AFMT4_OFFSET,
	EVERGREEN_AFMT5_OFFSET,
};


/*! CTA-861 picture-aspect codes for the AVI infoframe M field.
	The infoframe only defines two values in version 2: 4:3 and 16:9.
	Anything else writes 0 ("no data") and lets the sink fall back to
	the detailed timing. */
static uint8
_PictureAspect(uint16 width, uint16 height)
{
	if (width == 0 || height == 0)
		return 0;
	// 16:9 ratio: width * 9 == height * 16  (within rounding)
	if ((uint32)width * 9 == (uint32)height * 16)
		return 2;
	// 4:3 ratio: width * 3 == height * 4
	if ((uint32)width * 3 == (uint32)height * 4)
		return 1;
	return 0;
}


/*! Map a few common modes to their CTA-861 VIC codes. Non-matching
	modes return 0 — that's CTA-compliant and tells the sink to fall
	back to the detailed timing for cadence info while still using the
	AVI infoframe for colorspace / aspect signaling. */
static uint8
_MatchCtaVic(uint16 width, uint16 height, uint32 pixelClock)
{
	// VIC 1: 640x480p @ 60Hz
	if (width == 640 && height == 480 && pixelClock > 24000
		&& pixelClock < 27000) {
		return 1;
	}
	// VIC 4: 1280x720p @ 60Hz
	if (width == 1280 && height == 720 && pixelClock > 73000
		&& pixelClock < 76000) {
		return 4;
	}
	// VIC 16: 1920x1080p @ 60Hz
	if (width == 1920 && height == 1080 && pixelClock > 146000
		&& pixelClock < 150000) {
		return 16;
	}
	// VIC 34: 1920x1080p @ 30Hz
	if (width == 1920 && height == 1080 && pixelClock > 73000
		&& pixelClock < 76000) {
		return 34;
	}
	// VIC 31: 1920x1080p @ 50Hz
	if (width == 1920 && height == 1080 && pixelClock > 147000
		&& pixelClock < 149000) {
		return 31;
	}
	// VIC 95: 3840x2160p @ 30Hz
	if (width == 3840 && height == 2160 && pixelClock > 295000
		&& pixelClock < 299000) {
		return 95;
	}
	// VIC 97: 3840x2160p @ 60Hz
	if (width == 3840 && height == 2160 && pixelClock > 590000
		&& pixelClock < 596000) {
		return 97;
	}
	return 0;
}


/*! Build a CTA-861 AVI infoframe payload (14 bytes: PB0 checksum +
	PB1..PB13).

	Minimal "RGB Full default range" config that matches the Phase 1.5
	use case — most desktop modes. Doesn't try to advertise colorimetry,
	deep color, or content type. Quantization range left at Q=0
	(default) because Q=2 (full) on a sink without RGB-Q advertisement
	produces washed-out blacks. */
static void
_BuildAviInfoframe(const display_mode& mode, uint8 buffer[14])
{
	memset(buffer, 0, 14);

	// PB1: Y=RGB(0), A=1 (active-aspect-info valid), B=0, S=0
	buffer[1] = (0 << 5) | (1 << 4) | (0 << 2) | 0;

	// PB2: C=0 (no colorimetry — sink uses VIC default),
	//      M=aspect ratio (1=4:3, 2=16:9),
	//      R=8 (same-as-coded — the safe default)
	uint8 aspectM = _PictureAspect(mode.virtual_width, mode.virtual_height);
	buffer[2] = (0 << 6) | (aspectM << 4) | 0x8;

	// PB3: ITC=0, EC=0, Q=0 (default RGB range), SC=0
	buffer[3] = 0;

	// PB4: VIC — set if the mode matches a known CTA timing, else 0
	buffer[4] = _MatchCtaVic(mode.virtual_width, mode.virtual_height,
		mode.timing.pixel_clock);

	// PB5: YQ=0, CN=0, PR=0 (no pixel repetition for 24-bit modes)
	buffer[5] = 0;

	// PB6..PB13: bar info stays zero (B bits in PB1 are 0)

	// PB0: checksum so header+payload sums to 0 modulo 256.
	// Header constants: HB0=0x82 + HB1=0x02 + HB2=0x0D = 0x91.
	uint16 sum = 0x91;
	for (int i = 1; i <= 13; i++)
		sum += buffer[i];
	buffer[0] = (uint8)(0x100 - (sum & 0xFF));
}


/*! Pack the 14-byte AVI payload into the four AFMT_AVI_INFO0..3
	registers, using the byte layout the packet generator actually
	transmits (matches Linux evergreen_set_avi_packet(), which writes
	from `buffer + 3` of the drm-packed frame):

	  INFO0 = checksum | PB1 << 8 | PB2 << 16 | PB3 << 24
	  INFO1 = PB4..PB7,   INFO2 = PB8..PB11
	  INFO3 = PB12 | PB13 << 8 | version(2) << 24

	The original 0.6.0 packing started INFO0 at PB1, shifting every
	payload byte down by one: PB2 (aspect, 0x28) landed in the PB1
	position, whose bits [6:5] = 01 then told the sink the stream was
	YCbCr 4:2:2 — instantly wrong colors on sinks that honor the
	infoframe. */
static void
_PackAviInfoframe(const uint8 buffer[14], uint32 afmtOffset)
{
	uint32 w0 = (uint32)buffer[0]
		| ((uint32)buffer[1] << 8)
		| ((uint32)buffer[2] << 16)
		| ((uint32)buffer[3] << 24);
	uint32 w1 = (uint32)buffer[4]
		| ((uint32)buffer[5] << 8)
		| ((uint32)buffer[6] << 16)
		| ((uint32)buffer[7] << 24);
	uint32 w2 = (uint32)buffer[8]
		| ((uint32)buffer[9] << 8)
		| ((uint32)buffer[10] << 16)
		| ((uint32)buffer[11] << 24);
	uint32 w3 = (uint32)buffer[12]
		| ((uint32)buffer[13] << 8)
		| ((uint32)2 << 24);
		// infoframe version 2 in the top byte

	Write32(OUT, EVERGREEN_AFMT_AVI_INFO0 + afmtOffset, w0);
	Write32(OUT, EVERGREEN_AFMT_AVI_INFO1 + afmtOffset, w1);
	Write32(OUT, EVERGREEN_AFMT_AVI_INFO2 + afmtOffset, w2);
	// INFO3 last — some HW latches infoframe state on this write.
	Write32(OUT, EVERGREEN_AFMT_AVI_INFO3 + afmtOffset, w3);
}


void
hdmi_avi_infoframe_program(uint8 crtcID)
{
	radeon_shared_info& info = *gInfo->shared_info;

	// Only Evergreen and newer have the AFMT block layout we know about.
	// Pre-Evergreen (R600/R700) had a different HDMI path that this
	// driver doesn't currently program; falling silent here matches
	// the existing behavior of not touching HDMI on those generations.
	if (info.chipsetID < RADEON_CEDAR)
		return;

	uint32 connectorIndex = gDisplay[crtcID]->connectorIndex;
	uint32 digID = encoder_pick_dig(connectorIndex);
	if (digID >= (sizeof(kAfmtOffsets) / sizeof(kAfmtOffsets[0])))
		return;

	uint32 afmtOffset = kAfmtOffsets[digID];
	const display_mode& mode = gInfo->shared_info->current_mode;

	uint8 buffer[14];
	_BuildAviInfoframe(mode, buffer);

	// Disable the packet generators we don't intend to use, but keep
	// the VBI block sending NULL and General Control packets. HDMI
	// data-island periods must always carry validly-coded packets —
	// with NULL_SEND off, the encoder transmits garbage during island
	// windows where nothing is queued, which the sink decodes as
	// visible pixels: the magenta stripe along the left edge of the
	// active region. (Confirmed 2026-06-04: with NULL_SEND off and
	// every value below read back as correctly programmed, the stripe
	// still appeared. Linux evergreen_hdmi.c always enables
	// NULL_SEND | GC_SEND | GC_CONT, and the GC packet carries the
	// AVMUTE flag, cleared below.)
	Write32(OUT, EVERGREEN_HDMI_VBI_PACKET_CONTROL + afmtOffset,
		EVERGREEN_HDMI_NULL_SEND | EVERGREEN_HDMI_GC_SEND
		| EVERGREEN_HDMI_GC_CONT);
	Write32(OUT, EVERGREEN_HDMI_ACR_PACKET_CONTROL + afmtOffset, 0);
	Write32(OUT, EVERGREEN_HDMI_GENERIC_PACKET_CONTROL + afmtOffset, 0);
	Write32(OUT, EVERGREEN_HDMI_AUDIO_PACKET_CONTROL + afmtOffset, 0);
	Write32(OUT, EVERGREEN_AFMT_AUDIO_PACKET_CONTROL + afmtOffset, 0);

	// The General Control packet we just enabled carries the AVMUTE
	// flag — make sure it's cleared, or compliant sinks blank the video.
	uint32 gcControl = Read32(OUT, EVERGREEN_HDMI_GC + afmtOffset);
	gcControl &= ~EVERGREEN_HDMI_GC_AVMUTE;
	Write32(OUT, EVERGREEN_HDMI_GC + afmtOffset, gcControl);

	// HDMI_CONTROL: clear the deep-color bits (24-bit RGB only) and
	// leave everything else at its hardware/AtomBIOS default — exactly
	// what Linux's dce4_hdmi_set_color_depth() does. Notably, do NOT
	// set KEEPOUT_MODE or PACKET_GEN_VERSION: nothing in Linux's
	// DCE 4/5 path touches either bit (PACKET_GEN_VERSION is an r6xx
	// compatibility control), and KEEPOUT defines an active-pixel
	// suppression window adjacent to the data island — i.e. at the
	// left edge of the visible region, precisely where the magenta
	// stripe appears. Earlier experiments (0.6.0 and 0.6.3~pre3/4) all
	// ran with KEEPOUT set and all showed the stripe; this build tests
	// the Linux-parity configuration with it off.
	uint32 hdmiControl = Read32(OUT, EVERGREEN_HDMI_CONTROL + afmtOffset);
	hdmiControl &= ~(EVERGREEN_HDMI_KEEPOUT_MODE
		| EVERGREEN_HDMI_PACKET_GEN_VERSION
		| EVERGREEN_HDMI_DEEP_COLOR_ENABLE);
	Write32(OUT, EVERGREEN_HDMI_CONTROL + afmtOffset, hdmiControl);

	_PackAviInfoframe(buffer, afmtOffset);

	// Transmit the infoframe on VBI line 2 — empirically the line most
	// sinks accept. Some sinks reject lines 0 / 1.
	uint32 infoControl1 = Read32(OUT,
		EVERGREEN_HDMI_INFOFRAME_CONTROL1 + afmtOffset);
	infoControl1 &= ~EVERGREEN_HDMI_AVI_INFO_LINE_MASK;
	infoControl1 |= EVERGREEN_HDMI_AVI_INFO_LINE(2);
	Write32(OUT, EVERGREEN_HDMI_INFOFRAME_CONTROL1 + afmtOffset,
		infoControl1);

	// Enable AVI infoframe transmission. CONT is mandatory — SEND alone
	// fires once and the sink reverts to legacy/default after ~1 frame.
	Write32(OUT, EVERGREEN_HDMI_INFOFRAME_CONTROL0 + afmtOffset,
		EVERGREEN_HDMI_AVI_INFO_SEND | EVERGREEN_HDMI_AVI_INFO_CONT);

	TRACE("%s: CRTC %u: AVI infoframe programmed (offset 0x%" B_PRIx32
		", VIC=%u, aspect_M=%u, mode=%ux%u@%u kHz)\n",
		__func__, crtcID, afmtOffset, buffer[4],
		(buffer[2] >> 4) & 0x3,
		mode.virtual_width, mode.virtual_height,
		mode.timing.pixel_clock);
}


void
hdmi_avi_infoframe_disable(uint8 crtcID)
{
	radeon_shared_info& info = *gInfo->shared_info;

	if (info.chipsetID < RADEON_CEDAR)
		return;

	uint32 connectorIndex = gDisplay[crtcID]->connectorIndex;
	uint32 digID = encoder_pick_dig(connectorIndex);
	if (digID >= (sizeof(kAfmtOffsets) / sizeof(kAfmtOffsets[0])))
		return;

	uint32 afmtOffset = kAfmtOffsets[digID];

	// Stop transmitting the AVI infoframe.
	Write32(OUT, EVERGREEN_HDMI_INFOFRAME_CONTROL0 + afmtOffset, 0);

	// Drop HDMI keepout — the encoder is going away, no need to
	// suppress pixels in its data-island window.
	uint32 hdmiControl = Read32(OUT, EVERGREEN_HDMI_CONTROL + afmtOffset);
	hdmiControl &= ~EVERGREEN_HDMI_KEEPOUT_MODE;
	Write32(OUT, EVERGREEN_HDMI_CONTROL + afmtOffset, hdmiControl);

	TRACE("%s: CRTC %u: AVI infoframe disabled (offset 0x%" B_PRIx32 ")\n",
		__func__, crtcID, afmtOffset);
}


/*! Phase A instrumentation for the Cedar magenta-stripe investigation:
	read back every register hdmi_avi_infoframe_program() writes and
	TRACE the values, so syslog shows whether our writes stick or get
	clobbered by a later AtomBIOS call (DPMS / encoder setup) — suspect
	number one from the 0.6.0 investigation. Read-only by design; safe
	to call at any point in the mode-set sequence. */
void
hdmi_registers_dump(uint8 crtcID, const char* stage)
{
#ifndef TRACE_HDMI
	// The whole body is diagnostics, so with tracing compiled out there
	// is nothing to do and the locals would go unused (STYLE_GUIDE §18).
	(void)crtcID;
	(void)stage;
#else
	radeon_shared_info& info = *gInfo->shared_info;

	if (info.chipsetID < RADEON_CEDAR)
		return;

	uint32 connectorIndex = gDisplay[crtcID]->connectorIndex;
	uint32 digID = encoder_pick_dig(connectorIndex);
	if (digID >= (sizeof(kAfmtOffsets) / sizeof(kAfmtOffsets[0])))
		return;

	uint32 afmtOffset = kAfmtOffsets[digID];

	TRACE("%s: CRTC %u, DIG %" B_PRIu32 ", AFMT offset 0x%" B_PRIx32
		" (%s):\n", __func__, crtcID, digID, afmtOffset, stage);
	TRACE("  HDMI_CONTROL             0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_HDMI_CONTROL + afmtOffset));
	TRACE("  HDMI_INFOFRAME_CONTROL0  0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_HDMI_INFOFRAME_CONTROL0 + afmtOffset));
	TRACE("  HDMI_INFOFRAME_CONTROL1  0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_HDMI_INFOFRAME_CONTROL1 + afmtOffset));
	TRACE("  HDMI_VBI_PACKET_CONTROL  0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_HDMI_VBI_PACKET_CONTROL + afmtOffset));
	TRACE("  HDMI_GC                  0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_HDMI_GC + afmtOffset));
	TRACE("  HDMI_ACR_PACKET_CONTROL  0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_HDMI_ACR_PACKET_CONTROL + afmtOffset));
	TRACE("  HDMI_GENERIC_PACKET_CTRL 0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_HDMI_GENERIC_PACKET_CONTROL + afmtOffset));
	TRACE("  HDMI_AUDIO_PACKET_CTRL   0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_HDMI_AUDIO_PACKET_CONTROL + afmtOffset));
	TRACE("  AFMT_AUDIO_PACKET_CTRL   0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_AFMT_AUDIO_PACKET_CONTROL + afmtOffset));
	TRACE("  AFMT_AVI_INFO0..3        0x%08" B_PRIx32 " 0x%08" B_PRIx32
		" 0x%08" B_PRIx32 " 0x%08" B_PRIx32 "\n",
		Read32(OUT, EVERGREEN_AFMT_AVI_INFO0 + afmtOffset),
		Read32(OUT, EVERGREEN_AFMT_AVI_INFO1 + afmtOffset),
		Read32(OUT, EVERGREEN_AFMT_AVI_INFO2 + afmtOffset),
		Read32(OUT, EVERGREEN_AFMT_AVI_INFO3 + afmtOffset));
#endif
}


/*! Wide-block instrumentation: TRACE the raw 0x7000–0x73FF window of
	the connector's DIG block, 4 dwords per line (~65 syslog lines per
	mode set). This was the A/B-diff tool that localized the
	magenta-stripe root cause; its job is done, so it's compiled out by
	default — define TRACE_HDMI_BLOCK_DUMP to re-enable when a bug
	report needs a full DIG-block snapshot. The lighter
	hdmi_registers_dump() and bandwidth_registers_dump() stay always-on
	for bug-report syslogs. */
//#define TRACE_HDMI_BLOCK_DUMP
void
hdmi_block_dump(uint8 crtcID)
{
#ifndef TRACE_HDMI_BLOCK_DUMP
	(void)crtcID;
	return;
#else
	radeon_shared_info& info = *gInfo->shared_info;

	if (info.chipsetID < RADEON_CEDAR)
		return;

	uint32 connectorIndex = gDisplay[crtcID]->connectorIndex;
	uint32 digID = encoder_pick_dig(connectorIndex);
	if (digID >= (sizeof(kAfmtOffsets) / sizeof(kAfmtOffsets[0])))
		return;

	uint32 afmtOffset = kAfmtOffsets[digID];

	// 0x7000–0x73FF within the connector's DIG block: the DIG front
	// end, HDMI/AFMT packet block, the DIG back end (DIG_BE_CNTL
	// 0x7140-equivalent holds the encoder MODE field, bits 18:16), and
	// the DP/SEC packet block.
	TRACE("%s: CRTC %u raw DIG %" B_PRIu32 " block (base 0x7000 + 0x%"
		B_PRIx32 "):\n", __func__, crtcID, digID, afmtOffset);
	for (uint32 reg = 0x7000; reg < 0x7400; reg += 16) {
		TRACE("  0x%04" B_PRIx32 ": %08" B_PRIx32 " %08" B_PRIx32
			" %08" B_PRIx32 " %08" B_PRIx32 "\n", reg,
			Read32(OUT, reg + afmtOffset),
			Read32(OUT, reg + 4 + afmtOffset),
			Read32(OUT, reg + 8 + afmtOffset),
			Read32(OUT, reg + 12 + afmtOffset));
	}
#endif	// TRACE_HDMI_BLOCK_DUMP
}
