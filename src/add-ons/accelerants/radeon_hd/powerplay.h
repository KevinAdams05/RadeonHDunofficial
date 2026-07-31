/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 *
 * PowerPlay table reader for Evergreen / Northern Islands.
 *
 * The driver does no power management: the card runs at whatever clocks
 * the VBIOS posted, which on the boards tested so far is the lowest DPM
 * state. On a Turks HD 6570 that measured 99990 kHz engine / 149970 kHz
 * memory against a 650 / 900 MHz operating spec, which caps the DRAM
 * bandwidth available to scanout at roughly 1.7 GB/s and is the real
 * reason behind the per-chip pixel-clock caps in mode.cpp — see
 * docs/scanout-watermark-investigation.md.
 *
 * This file reads the PowerPlay table's performance levels, reports what a
 * raise would target, and — only when explicitly enabled in the driver
 * settings — applies one.
 *
 * Everything that writes to the hardware is opt-in and gated, in two tiers
 * (`raise_clocks`, then `raise_memory_clock`), because the second tier
 * programs a voltage. See powerplay_apply_target() and
 * docs/power-management-investigation.md. Scope is deliberately one static
 * level applied at init: no dynamic power management, no thermal
 * management, no SMC firmware.
 */
#ifndef RADEON_HD_POWERPLAY_H
#define RADEON_HD_POWERPLAY_H


#include <SupportDefs.h>


// More than any Evergreen/NI board is expected to advertise; the reader
// bounds-checks against this and against the table's own extent.
#define MAX_POWERPLAY_LEVELS 8


/*! One PowerPlay performance level, decoded.

	Clocks are in kHz. Voltages are as the table states them, which is
	*not* always millivolts — a vddc in the virtual-voltage-ID range is a
	leakage-binned placeholder, not a level. Test with
	powerplay_voltage_is_virtual() before believing or programming it. */
struct powerplay_level {
	uint32	engineClock;
	uint32	memoryClock;
	uint16	vddc;
	uint16	vddci;
};


/*! Is this voltage field a virtual voltage ID rather than a real level?

	ATOM_VIRTUAL_VOLTAGE_ID0..7 (0xff01-0xff08) mean "the real voltage for
	this level is binned per die and must be resolved from
	ASIC_ProfilingInfo". Such a level cannot be programmed as-is, and a
	value like 65281 shows up in a naive millivolt dump. */
static inline bool
powerplay_voltage_is_virtual(uint16 voltage)
{
	return voltage >= 0xff01 && voltage <= 0xff08;
}


/*! Read one boolean out of the driver settings, defaulting to off.

	The fork's generic gate reader, not specific to power management — it
	lives here because this file is where the settings mechanism is
	documented, and `mode.cpp` already reaches in for
	`powerplay_pixel_clock_cap_ignored()`.

	Every gate in this fork is opt-in and defaults off, because recovering
	from a board that does not like what a gate turned on should be editing
	one line in ~/config/settings/kernel/drivers/radeon_hd — not
	reinstalling anything.

	load_driver_settings() is the standard Haiku mechanism and is available
	to userland through libroot, which matters because this runs in the
	accelerant rather than the kernel driver. Note the userland
	implementation resolves only B_USER_SETTINGS_DIRECTORY, so this is the
	per-user file and not a system-wide one. */
bool radeon_setting_enabled(const char* key);


/*! Current engine / memory clock in kHz via the AtomBIOS GetEngineClock /
	GetMemoryClock command tables, or 0 if unavailable.

	FirmwareInfo's ulDefault*Clock are table *defaults*, which on a board
	whose VBIOS posts a low power state are not what the card is running.
	These two ask the hardware. They are also the read-back channel used to
	confirm a clock change actually took effect. */
uint32 powerplay_engine_clock_current();
uint32 powerplay_memory_clock_current();


/*! Read the PowerPlay performance levels into caller-provided storage.

	Returns B_OK and sets *_count on success. Best-effort and defensive:
	unsupported table shapes and out-of-range offsets are reported and
	rejected rather than guessed at. Evergreen / NI only. */
status_t powerplay_read_levels(powerplay_level* levels, uint32 maxLevels,
	uint32* _count);


/*! TRACE the engine/memory/voltage of every performance level the
	PowerPlay table advertises, plus the resulting min and max.

	Read-only and best-effort: unsupported table revisions and malformed
	offsets are reported and skipped rather than guessed at. Evergreen and
	NI only — the clock-info entry layout differs on DCE 6 and later.

	Called from radeon_gpu_probe() after the current clocks are read, so a
	syslog shows the clocks in use immediately followed by the levels the
	board could reach. */
void powerplay_dump_performance_levels();


/*! Phase A probe: TRACE the frev/crev of the AtomBIOS command tables a
	future clock/voltage write path would have to call, plus whether the
	leakage-voltage data table is present.

	SetVoltage in particular cannot be called safely without this: at
	table revision 1 it takes a voltage *index*, and at 2 and above a
	voltage *level* that may itself be millivolts or a phase number. The
	revision is the only thing that says which.

	Read-only — parses headers, executes nothing. */
void powerplay_dump_control_tables();


/*! Phase A probe: work out which advertised level the card is currently
	running, and which level a clock raise would target, then TRACE both
	along with the voltage changes that target would require.

	**Decides nothing and programs nothing** — this exists so the target
	policy can be reviewed against real boards before any write path is
	built. See docs/power-management-investigation.md §5. */
void powerplay_dump_target_selection();


/*! Work out which level the card is on and which one a raise should aim
	for, per the policy in docs/power-management-investigation.md §5:
	the lowest level that still reaches the maximum advertised memory
	clock, skipping levels whose VDDC is a virtual ID and never choosing
	one that would lower the engine clock.

	Sets *_currentLevel to -1 if the current clocks match no advertised
	level. Returns B_OK only when a target was found; B_ENTRY_NOT_FOUND
	when there is nothing worth raising to (including the common case of a
	board already at its maximum memory clock).

	Pure decision function — reads nothing from the hardware beyond the
	cached current clocks, and programs nothing. Shared by the Phase A dump
	and the apply path so the two cannot disagree. */
status_t powerplay_select_target(const powerplay_level* levels, uint32 count,
	int32* _currentLevel, int32* _target);


/*! Raise the card towards the selected target level.

	Two tiers, each separately gated in
	`~/config/settings/kernel/drivers/radeon_hd`, both default off:

	- `raise_clocks` — engine clock only. Safe by construction: it only ever
	  moves to a clock the target level states at the VDDC already applied,
	  so no voltage is written.
	- `raise_memory_clock` — additionally raises VDDCI and the memory clock.
	  This is the tier that writes a voltage. It also requires
	  `raise_clocks`, because on a board where the data-return path binds
	  the available bandwidth, raising memory alone achieves nothing.

	Order is voltage first, then engine, then memory — a clock must never
	run ahead of the voltage supporting it.

	The target is decided **once**, before any write. Deciding per-write
	would not compose: with the engine clock alone raised, the card sits at
	a clock pair no level advertises and a second lookup would refuse.

	Refuses rather than guessing when the current clocks match no advertised
	level (the applied voltage would be unknown), when the target needs a
	different VDDC (a core-voltage raise is a larger step, which Turks
	needs), when the SetVoltage table revision takes an index rather than a
	level, or when a voltage field holds a virtual ID.

	Each clock is confirmed by read-back, and gInfo->engineClockFrequency /
	gInfo->memoryClockFrequency are updated so bandwidth.cpp computes its
	watermarks from the real clocks. The voltage itself cannot be read back
	through this interface, so a successful clock is the only confirmation
	available.

	Returns B_OK if anything was raised and verified. */
status_t powerplay_apply_target();


/*! Is `ignore_pixel_clock_cap` set in the driver settings?

	Makes the empirical per-chip pixel-clock caps in mode.cpp advisory so
	that over-cap modes can be reached while re-deriving them. Off by
	default, **not** a supported configuration: the expected result of
	enabling it is exactly the stride-aliased scanout corruption the caps
	were added to avoid. It exists because re-deriving a cap requires being
	able to exceed it. */
bool powerplay_pixel_clock_cap_ignored();


#endif	// RADEON_HD_POWERPLAY_H
