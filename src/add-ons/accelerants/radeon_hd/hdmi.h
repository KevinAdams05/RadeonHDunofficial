/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 *
 * HDMI infoframe support for Evergreen / NI / SI / CIK.
 *
 * The driver previously forced HDMI connectors into ATOM_ENCODER_MODE_DVI
 * (Phase 1.5 workaround for the magenta-stripe bug) because we didn't
 * program a valid AVI infoframe. With an infoframe + HDMI_KEEPOUT_MODE
 * configured per CTA-861, sinks correctly decode the HBLANK data island
 * as packet data instead of as visible pixels, and the workaround can
 * be retired.
 *
 * Audio is explicitly out of scope here — that needs codec/driver
 * integration (Azalia codec endpoints, ACR tables, IEC 60958 channel
 * status). Audio infoframes live in the same AFMT block and can be
 * added later without touching this file's API.
 */
#ifndef RADEON_HD_HDMI_H
#define RADEON_HD_HDMI_H


#include <SupportDefs.h>


/*! Program the HDMI AVI infoframe + KEEPOUT for one CRTC.

	Builds a CTA-861 AVI infoframe describing the current mode, packs it
	into AFMT_AVI_INFO0..3, enables continuous transmission on VBI line 2,
	and sets HDMI_KEEPOUT_MODE so the encoder suppresses active pixels
	during the HBLANK data-island window.

	Caller must check the connector is HDMI-class before calling; this
	function does not gate on connector type itself. Safe to call on
	any DCE 4+ chipset (Evergreen / NI / SI / CIK share the register
	layout).

	Called from display_crtc_fb_set() after the surface and CRTC are
	programmed. Must also be re-called from the DPMS-on path because
	AFMT block state is lost across DPMS off → on. */
void hdmi_avi_infoframe_program(uint8 crtcID);


/*! Tear-down — disable the AVI infoframe transmission and clear
	KEEPOUT for one CRTC. Used in DPMS-off / encoder-disable paths. */
void hdmi_avi_infoframe_disable(uint8 crtcID);


/*! Phase A instrumentation for the magenta-stripe investigation:
	TRACE a read-back of every register hdmi_avi_infoframe_program()
	touches, tagged with a caller-supplied stage label. Read-only —
	safe to call at any point in the mode-set sequence. */
void hdmi_registers_dump(uint8 crtcID, const char* stage);


/*! Wide-block instrumentation: TRACE the raw 0x7000–0x70FF HDMI/AFMT
	register window for one CRTC. Diff between encoder-mode boots to
	isolate the AtomBIOS delta. Read-only. */
void hdmi_block_dump(uint8 crtcID);


#endif	// RADEON_HD_HDMI_H
