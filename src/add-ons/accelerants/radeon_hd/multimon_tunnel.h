/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 */
#ifndef RADEON_HD_MULTIMON_TUNNEL_H
#define RADEON_HD_MULTIMON_TUNNEL_H


/*!	Multi-monitor settings tunnel — the wire protocol Screen preferences uses
	to talk to a multi-head accelerant through B_PROPOSE_DISPLAY_MODE.

	Thomas Kurschel defined this for the BeOS-era `radeon` driver in 2002 and
	Screen preferences still speaks it verbatim
	(`src/preferences/screen/multimon.cpp`). There is no public header for it:
	the canonical definitions live in that other driver's private
	`headers/private/graphics/radeon/accelerant_ext.h`, which is not ours to
	include. They are duplicated here instead, and **the values are fixed by
	the protocol — do not renumber them.**

	Two independent things travel over this one hook, and it matters which is
	which:

	1. A **support handshake**. Screen preferences calls
	   `TestMultiMonSupport()` before it will show the "Combine displays:"
	   menu at all — `ScreenWindow.cpp` does
	   `if (!multiMonSupport) fCombineField->Hide()`. The probe sets
	   MULTIMON_REQUEST on a real mode and expects the accelerant to clear it
	   and set MULTIMON_REPLY. Answer this and the combine UI appears; ignore
	   it and horizontal span is unreachable from the GUI no matter how good
	   the mode list is.

	2. A **settings channel** for swap-displays / use-laptop-panel /
	   TV-standard. Recognised by a deliberately impossible display_mode:
	   all three `space` fields zero, `low` sized 0xffff x 0xffff, `high`
	   sized 0x0, and the two pixel-clock sentinels below. The setting code
	   arrives in `mode.h_display_start`, the operation in
	   `mode.v_display_start` (0 = get, 1 = set, 2 = get n-th supported), and
	   the value in `mode.timing.flags` both ways.

	Note that the combine mode itself does NOT come through this channel — it
	is chosen purely from the mode list, by `get_combine_mode()` in
	`ScreenMode.cpp`, which looks for B_SCROLL plus a virtual_width that is
	exactly twice timing.h_display.
*/

// Sentinel pixel clocks marking a tunneled settings request. 'TKTK'/'KTKT'
// are Kurschel's initials; they are magic numbers, not meaningful clocks.
#define RADEON_TUNNEL_SENTINEL_LOW			'TKTK'
#define RADEON_TUNNEL_SENTINEL_HIGH			'KTKT'

// Additional display_mode timing flags. Only the two handshake bits matter to
// us; the mode-type bits are kept for documentation of the protocol's shape.
enum {
	RADEON_MODE_STANDARD				= 0 << 16,
	RADEON_MODE_COMBINE					= 3 << 16,
	RADEON_MODE_MASK					= 7 << 16,

	RADEON_MODE_POSITION_HORIZONTAL		= 0 << 21,
	RADEON_MODE_POSITION_VERTICAL		= 1 << 21,
	RADEON_MODE_POSITION_MASK			= 1 << 21,

	RADEON_MODE_MULTIMON_REQUEST		= 1 << 25,
	RADEON_MODE_MULTIMON_REPLY			= 1 << 26
};

// Setting codes, arriving in mode.h_display_start.
enum {
	RADEON_TUNNEL_SETTING_SWAP				= 'sw',
	RADEON_TUNNEL_SETTING_USE_LAPTOP_PANEL	= 'up',
	RADEON_TUNNEL_SETTING_TV_STANDARD		= 'tv'
};

// Operations, arriving in mode.v_display_start.
enum {
	RADEON_TUNNEL_OP_GET				= 0,
	RADEON_TUNNEL_OP_SET				= 1,
	RADEON_TUNNEL_OP_GET_NTH_SUPPORTED	= 2
};


#endif /* RADEON_HD_MULTIMON_TUNNEL_H */
