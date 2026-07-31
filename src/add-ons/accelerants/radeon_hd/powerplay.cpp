/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 */


#include "powerplay.h"

#include <string.h>

#include <driver_settings.h>

#include "accelerant.h"
#include "atom.h"
#include "pptable.h"


// Declared outside the TRACE gate: ERROR() is always on, so it needs
// _sPrintf even in a build with tracing compiled out.
extern "C" void _sPrintf(const char* format, ...);

#undef TRACE
#define TRACE_POWERPLAY
#ifdef TRACE_POWERPLAY
#	define TRACE(x...) _sPrintf("radeon_hd: " x)
#else
#	define TRACE(x...) ;
#endif

#define ERROR(x...) _sPrintf("radeon_hd: " x)


uint32
powerplay_engine_clock_current()
{
	GET_ENGINE_CLOCK_PS_ALLOCATION args;
	args.ulReturnEngineClock = 0;

	int index = GetIndexIntoMasterTable(COMMAND, GetEngineClock);
	if (atom_execute_table(gAtomContext, index, (uint32*)&args) != B_OK) {
		TRACE("%s: GetEngineClock command table failed\n", __func__);
		return 0;
	}

	// Returned in 10 kHz units.
	return B_LENDIAN_TO_HOST_INT32(args.ulReturnEngineClock) * 10;
}


uint32
powerplay_memory_clock_current()
{
	GET_MEMORY_CLOCK_PS_ALLOCATION args;
	args.ulReturnMemoryClock = 0;

	int index = GetIndexIntoMasterTable(COMMAND, GetMemoryClock);
	if (atom_execute_table(gAtomContext, index, (uint32*)&args) != B_OK) {
		TRACE("%s: GetMemoryClock command table failed\n", __func__);
		return 0;
	}

	return B_LENDIAN_TO_HOST_INT32(args.ulReturnMemoryClock) * 10;
}


/*!	Read one boolean out of the driver settings, defaulting to off.

	See the declaration in powerplay.h for why every gate in this fork is
	opt-in and how the settings file is resolved. */
bool
radeon_setting_enabled(const char* key)
{
	bool enabled = false;

	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		enabled = get_driver_boolean_parameter(settings, key, false, false);
		unload_driver_settings(settings);
	}

	return enabled;
}


static bool
powerplay_raise_enabled()
{
	return radeon_setting_enabled("raise_clocks");
}


/*!	Is the riskier memory-clock-plus-voltage tier turned on?

	Deliberately a second switch rather than an extension of the first: that
	tier writes a voltage and the engine-only tier does not, so enabling the
	safe one must never silently opt anyone into the risky one. Both are
	required because on a board whose data-return path binds the available
	bandwidth, raising memory without engine achieves nothing. */
static bool
powerplay_memory_raise_enabled()
{
	return powerplay_raise_enabled()
		&& radeon_setting_enabled("raise_memory_clock");
}


/*!	Program one voltage rail, dispatching on the SetVoltage table revision.

	There is no counterpart read-back for voltage through this interface, so
	success here means "AtomBIOS accepted the call", not "the rail moved". */
static status_t
powerplay_set_voltage(uint8 voltageType, uint16 millivolts)
{
	// Never program a leakage-binned placeholder as though it were a level.
	if (powerplay_voltage_is_virtual(millivolts)) {
		ERROR("%s: refusing to program virtual voltage ID 0x%04" B_PRIx16
			"\n", __func__, millivolts);
		return B_BAD_VALUE;
	}

	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	uint8 tableMajor;
	uint8 tableMinor;
	if (atom_parse_cmd_header(gAtomContext, index, &tableMajor, &tableMinor)
			!= B_OK) {
		ERROR("%s: no SetVoltage command table\n", __func__);
		return B_ERROR;
	}

	union {
		SET_VOLTAGE_PARAMETERS v1;
		SET_VOLTAGE_PARAMETERS_V2 v2;
		SET_VOLTAGE_PARAMETERS_V1_3 v3;
	} args;
	memset(&args, 0, sizeof(args));

	switch (tableMinor) {
		case 1:
			// Revision 1 takes an index into a board-specific voltage list,
			// but PowerPlay gives us millivolts. There is no safe conversion
			// without the list, and a wrong index is a wrong voltage.
			ERROR("%s: SetVoltage crev 1 wants a voltage index, not the %"
				B_PRIu16 " mV PowerPlay reports - refusing\n", __func__,
				millivolts);
			return B_NOT_SUPPORTED;

		case 2:
			args.v2.ucVoltageType = voltageType;
			args.v2.ucVoltageMode = SET_ASIC_VOLTAGE_MODE_SET_VOLTAGE;
			args.v2.usVoltageLevel = B_HOST_TO_LENDIAN_INT16(millivolts);
			break;

		case 3:
		case 4:
			args.v3.ucVoltageType = voltageType;
			args.v3.ucVoltageMode = ATOM_SET_VOLTAGE;
			args.v3.usVoltageLevel = B_HOST_TO_LENDIAN_INT16(millivolts);
			break;

		default:
			ERROR("%s: unknown SetVoltage crev %" B_PRIu8 " - refusing to "
				"guess what its argument means\n", __func__, tableMinor);
			return B_NOT_SUPPORTED;
	}

	TRACE("%s: setting voltage type %" B_PRIu8 " to %" B_PRIu16 " mV via "
		"crev %" B_PRIu8 "\n", __func__, voltageType, millivolts, tableMinor);

	if (atom_execute_table(gAtomContext, index, (uint32*)&args) != B_OK) {
		ERROR("%s: SetVoltage command table failed\n", __func__);
		return B_ERROR;
	}

	return B_OK;
}


/*!	Does a clock read back from the hardware correspond to a table value?

	The AtomBIOS query returns a frequency computed from the PLL dividers,
	so it lands a hair off the round number the table states — 99990 kHz
	against 100000, 649890 against 650000. Every case measured so far is
	within 0.02%; allow 0.5% so the match is robust without being loose
	enough to confuse adjacent levels. */
static bool
powerplay_clock_matches(uint32 tableClock, uint32 actualClock)
{
	uint32 delta = tableClock > actualClock
		? tableClock - actualClock : actualClock - tableClock;

	return (uint64)delta * 200 <= tableClock;
}


status_t
powerplay_read_levels(powerplay_level* levels, uint32 maxLevels,
	uint32* _count)
{
	radeon_shared_info& info = *gInfo->shared_info;

	// The clock-info entry layout is per-family. Only the Evergreen / NI
	// form (ATOM_PPLIB_EVERGREEN_CLOCK_INFO) is decoded here; DCE 6 and
	// later use ATOM_PPLIB_SI_CLOCK_INFO and friends.
	//
	// Say so rather than returning silently: an unexplained absence of all
	// PowerPlay output is far harder to diagnose than one line stating why,
	// as this function's own bring-up demonstrated.
	if (info.dceMajor != 4 && info.dceMajor != 5) {
		TRACE("%s: skipped, only Evergreen / NI clock info is decoded (this "
			"is DCE %" B_PRIu8 ")\n", __func__, info.dceMajor);
		return B_NOT_SUPPORTED;
	}

	if (levels == NULL || maxLevels == 0 || _count == NULL) {
		ERROR("%s: bad arguments\n", __func__);
		return B_BAD_VALUE;
	}

	uint16 tableSize;
	uint8 tableMajor;
	uint8 tableMinor;
	uint16 tableOffset;

	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
	if (atom_parse_data_header(gAtomContext, index, &tableSize, &tableMajor,
		&tableMinor, &tableOffset) != B_OK) {
		ERROR("%s: no PowerPlay table in AtomBIOS\n", __func__);
		return B_ERROR;
	}

	TRACE("%s: PowerPlay table %" B_PRIu8 ".%" B_PRIu8 ", %" B_PRIu16
		" bytes\n", __func__, tableMajor, tableMinor, tableSize);

	if (tableSize == 0) {
		ERROR("%s: table reports zero size, refusing to walk it\n", __func__);
		return B_ERROR;
	}

	uint8* table = gAtomContext->bios + tableOffset;
	ATOM_PPLIB_POWERPLAYTABLE* powerPlay = (ATOM_PPLIB_POWERPLAYTABLE*)table;

	uint16 clockOffset
		= B_LENDIAN_TO_HOST_INT16(powerPlay->usClockInfoArrayOffset);
	uint16 nonClockOffset
		= B_LENDIAN_TO_HOST_INT16(powerPlay->usNonClockInfoArrayOffset);
	uint8 entrySize = powerPlay->ucClockInfoSize;
	uint32 wanted = (uint32)sizeof(ATOM_PPLIB_EVERGREEN_CLOCK_INFO);

	// The clock-info array is a *bare* array of ucClockInfoSize entries; the
	// entry count is not stored alongside it. It runs from
	// usClockInfoArrayOffset up to the non-clock array that follows, and
	// that span is where the count comes from.
	//
	// pptable.h also declares a counted ClockInfoArray { ucNumEntries,
	// ucEntrySize, ... } form. DCE 4/5 boards do NOT use it: assuming it on
	// a Turks HD 6570 read the first entry's clock bytes as a count of 232
	// entries of 253 bytes.
	if (clockOffset == 0 || nonClockOffset <= clockOffset
		|| nonClockOffset > tableSize) {
		ERROR("%s: clock info at +%" B_PRIu16 ", non-clock at +%" B_PRIu16
			" in a %" B_PRIu16 " byte table make no sense\n", __func__,
			clockOffset, nonClockOffset, tableSize);
		return B_ERROR;
	}

	if (entrySize != wanted) {
		ERROR("%s: clock info entry is %" B_PRIu8 " bytes, expected %"
			B_PRIu32 " for Evergreen / NI\n", __func__, entrySize, wanted);
		return B_ERROR;
	}

	uint32 count = (uint32)(nonClockOffset - clockOffset) / entrySize;
	if (count == 0) {
		ERROR("%s: clock info array holds no whole entries\n", __func__);
		return B_ERROR;
	}

	// Informational: states index into this array, so the two counts are
	// normally equal but are not required to be.
	if (count != powerPlay->ucNumStates) {
		TRACE("%s: %" B_PRIu32 " clock level(s) but %" B_PRIu8 " state(s)\n",
			__func__, count, powerPlay->ucNumStates);
	}

	if (count > maxLevels) {
		TRACE("%s: %" B_PRIu32 " levels advertised, reading the first %"
			B_PRIu32 "\n", __func__, count, maxLevels);
		count = maxLevels;
	}

	for (uint32 level = 0; level < count; level++) {
		ATOM_PPLIB_EVERGREEN_CLOCK_INFO* entry
			= (ATOM_PPLIB_EVERGREEN_CLOCK_INFO*)(table + clockOffset
				+ level * entrySize);

		// Clocks are split into a 16-bit low and an 8-bit high half, in
		// 10 kHz units like the rest of AtomBIOS.
		uint32 engine = B_LENDIAN_TO_HOST_INT16(entry->usEngineClockLow)
			| ((uint32)entry->ucEngineClockHigh << 16);
		uint32 memory = B_LENDIAN_TO_HOST_INT16(entry->usMemoryClockLow)
			| ((uint32)entry->ucMemoryClockHigh << 16);

		levels[level].engineClock = engine * 10;
		levels[level].memoryClock = memory * 10;
		levels[level].vddc = B_LENDIAN_TO_HOST_INT16(entry->usVDDC);
		levels[level].vddci = B_LENDIAN_TO_HOST_INT16(entry->usVDDCI);
	}

	*_count = count;
	return B_OK;
}


void
powerplay_dump_performance_levels()
{
	powerplay_level levels[MAX_POWERPLAY_LEVELS];
	uint32 count;

	if (powerplay_read_levels(levels, MAX_POWERPLAY_LEVELS, &count) != B_OK)
		return;

	uint32 engineMin = 0xffffffff;
	uint32 engineMax = 0;
	uint32 memoryMin = 0xffffffff;
	uint32 memoryMax = 0;

	for (uint32 level = 0; level < count; level++) {
		// A virtual voltage ID is not a millivolt figure, so do not print
		// it as one — an unannotated "65281 mV" is how this was originally
		// missed.
		if (powerplay_voltage_is_virtual(levels[level].vddc)) {
			TRACE("  level %" B_PRIu32 ": engine %" B_PRIu32 " kHz, memory %"
				B_PRIu32 " kHz, VDDC = virtual ID 0x%04" B_PRIx16
				" (leakage-binned, not programmable), VDDCI %" B_PRIu16
				" mV\n", level, levels[level].engineClock,
				levels[level].memoryClock, levels[level].vddc,
				levels[level].vddci);
		} else {
			TRACE("  level %" B_PRIu32 ": engine %" B_PRIu32 " kHz, memory %"
				B_PRIu32 " kHz, VDDC %" B_PRIu16 " mV, VDDCI %" B_PRIu16
				" mV\n", level, levels[level].engineClock,
				levels[level].memoryClock, levels[level].vddc,
				levels[level].vddci);
		}

		if (levels[level].engineClock < engineMin)
			engineMin = levels[level].engineClock;
		if (levels[level].engineClock > engineMax)
			engineMax = levels[level].engineClock;
		if (levels[level].memoryClock < memoryMin)
			memoryMin = levels[level].memoryClock;
		if (levels[level].memoryClock > memoryMax)
			memoryMax = levels[level].memoryClock;
	}

	TRACE("%s: %" B_PRIu32 " level(s): engine %" B_PRIu32 " - %" B_PRIu32
		" kHz, memory %" B_PRIu32 " - %" B_PRIu32 " kHz\n", __func__, count,
		engineMin, engineMax, memoryMin, memoryMax);

	// The whole point of the dump: say plainly whether the clocks we are
	// actually running at sit at the bottom of what the board advertises,
	// because that is what caps the DRAM bandwidth available to scanout.
	if (gInfo->memoryClockFrequency <= memoryMin && memoryMax > memoryMin) {
		TRACE("%s: running at the LOWEST advertised memory clock (%" B_PRIu32
			" of up to %" B_PRIu32 " kHz) - scanout bandwidth is at its "
			"floor\n", __func__, gInfo->memoryClockFrequency, memoryMax);
	}
}


/*!	TRACE one command table's revision, or say plainly that it is absent. */
static void
powerplay_dump_one_table(const char* name, int index)
{
	uint8 tableMajor;
	uint8 tableMinor;

	if (atom_parse_cmd_header(gAtomContext, index, &tableMajor, &tableMinor)
		!= B_OK) {
		TRACE("  %-16s ABSENT\n", name);
		return;
	}

	TRACE("  %-16s frev %" B_PRIu8 ", crev %" B_PRIu8 "\n", name, tableMajor,
		tableMinor);
}


void
powerplay_dump_control_tables()
{
	radeon_shared_info& info = *gInfo->shared_info;

	if (info.dceMajor != 4 && info.dceMajor != 5) {
		TRACE("%s: skipped, DCE %" B_PRIu8 " not in scope\n", __func__,
			info.dceMajor);
		return;
	}

	TRACE("%s: AtomBIOS clock/voltage control tables:\n", __func__);

	powerplay_dump_one_table("SetEngineClock",
		GetIndexIntoMasterTable(COMMAND, SetEngineClock));
	powerplay_dump_one_table("SetMemoryClock",
		GetIndexIntoMasterTable(COMMAND, SetMemoryClock));
	powerplay_dump_one_table("SetVoltage",
		GetIndexIntoMasterTable(COMMAND, SetVoltage));

	// SetVoltage's argument means different things per revision: crev 1
	// takes a voltage *index*, crev 2 and above a voltage *level* that is
	// itself either millivolts or a phase number. Nothing may be programmed
	// until this is known, so state the consequence next to the fact.
	uint8 tableMajor;
	uint8 tableMinor;
	int voltageIndex = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	if (atom_parse_cmd_header(gAtomContext, voltageIndex, &tableMajor,
			&tableMinor) == B_OK) {
		switch (tableMinor) {
			case 1:
				TRACE("  SetVoltage crev 1 takes ucVoltageIndex - an INDEX, "
					"not millivolts\n");
				break;

			case 2:
			case 3:
			case 4:
				TRACE("  SetVoltage crev %" B_PRIu8 " takes usVoltageLevel - "
					"millivolts or a phase number\n", tableMinor);
				break;

			default:
				TRACE("  SetVoltage crev %" B_PRIu8 " is UNKNOWN - a write "
					"path must refuse this board\n", tableMinor);
				break;
		}
	}

	// Presence only; resolving a virtual voltage ID would mean parsing this.
	uint16 dataSize;
	uint16 dataOffset;
	if (atom_parse_data_header(gAtomContext,
			GetIndexIntoMasterTable(DATA, ASIC_ProfilingInfo), &dataSize,
			&tableMajor, &tableMinor, &dataOffset) == B_OK) {
		TRACE("  %-16s present, %" B_PRIu16 " bytes (frev %" B_PRIu8
			", crev %" B_PRIu8 ")\n", "ASIC_Profiling", dataSize, tableMajor,
			tableMinor);
	} else
		TRACE("  %-16s ABSENT - virtual voltage IDs cannot be resolved\n",
			"ASIC_Profiling");
}


status_t
powerplay_select_target(const powerplay_level* levels, uint32 count,
	int32* _currentLevel, int32* _target)
{
	if (levels == NULL || count == 0 || _currentLevel == NULL
		|| _target == NULL) {
		return B_BAD_VALUE;
	}

	uint32 currentEngine = gInfo->engineClockFrequency;
	uint32 currentMemory = gInfo->memoryClockFrequency;

	// Which advertised level are we sitting on?
	*_currentLevel = -1;
	for (uint32 i = 0; i < count; i++) {
		if (powerplay_clock_matches(levels[i].memoryClock, currentMemory)
			&& powerplay_clock_matches(levels[i].engineClock,
				currentEngine)) {
			*_currentLevel = (int32)i;
			break;
		}
	}

	*_target = -1;

	uint32 memoryMax = 0;
	for (uint32 i = 0; i < count; i++) {
		if (levels[i].memoryClock > memoryMax)
			memoryMax = levels[i].memoryClock;
	}

	// Nothing to raise to — the common and correct outcome on a board whose
	// VBIOS already posted it at full speed.
	if (currentMemory >= memoryMax)
		return B_ENTRY_NOT_FOUND;

	// Policy: the lowest level that still reaches maximum memory clock.
	// Display bandwidth is bound by DRAM or the DMIF once memory is at full
	// speed, so a higher engine clock buys nothing and costs voltage and
	// heat. Skip levels whose VDDC is a virtual ID (unresolvable, and on
	// some boards the leakage table is not even present), and never pick
	// one that would lower the engine clock.
	for (uint32 i = 0; i < count; i++) {
		if (levels[i].memoryClock != memoryMax)
			continue;
		if (powerplay_voltage_is_virtual(levels[i].vddc))
			continue;
		if (levels[i].engineClock < currentEngine)
			continue;
		if (*_target < 0
			|| levels[i].engineClock < levels[*_target].engineClock) {
			*_target = (int32)i;
		}
	}

	return *_target >= 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


void
powerplay_dump_target_selection()
{
	// The whole body is diagnostics, so gate it: with tracing compiled out
	// its locals would otherwise be unused (STYLE_GUIDE §18).
#ifdef TRACE_POWERPLAY
	powerplay_level levels[MAX_POWERPLAY_LEVELS];
	uint32 count;

	if (powerplay_read_levels(levels, MAX_POWERPLAY_LEVELS, &count) != B_OK)
		return;

	uint32 currentEngine = gInfo->engineClockFrequency;
	uint32 currentMemory = gInfo->memoryClockFrequency;

	int32 currentLevel;
	int32 target;
	status_t status = powerplay_select_target(levels, count, &currentLevel,
		&target);

	if (currentLevel < 0) {
		TRACE("%s: current clocks (%" B_PRIu32 " / %" B_PRIu32 " kHz) match "
			"no advertised level\n", __func__, currentEngine, currentMemory);
	} else {
		TRACE("%s: currently on level %" B_PRId32 " (%" B_PRIu32 " / %"
			B_PRIu32 " kHz)\n", __func__, currentLevel, currentEngine,
			currentMemory);
	}

	// Name the skipped levels here rather than inside the decision function,
	// so the decision stays free of output while the reason stays visible.
	for (uint32 i = 0; i < count; i++) {
		if (powerplay_voltage_is_virtual(levels[i].vddc)) {
			TRACE("  level %" B_PRIu32 " has a virtual-ID VDDC - not a "
				"candidate\n", i);
		}
	}

	if (status != B_OK) {
		TRACE("%s: no raise available - either already at the highest "
			"advertised memory clock, or no usable level reaches it\n",
			__func__);
		return;
	}

	TRACE("%s: WOULD target level %" B_PRId32 " (%" B_PRIu32 " / %" B_PRIu32
		" kHz, VDDC %" B_PRIu16 " mV, VDDCI %" B_PRIu16 " mV)\n", __func__,
		target, levels[target].engineClock, levels[target].memoryClock,
		levels[target].vddc, levels[target].vddci);

	if (currentLevel < 0) {
		TRACE("%s: current level unknown, so the required voltage change "
			"cannot be stated\n", __func__);
		return;
	}

	bool vddcChanges = levels[target].vddc != levels[currentLevel].vddc;
	bool vddciChanges = levels[target].vddci != levels[currentLevel].vddci;

	TRACE("%s: voltage delta: VDDC %" B_PRIu16 " -> %" B_PRIu16 " (%s), "
		"VDDCI %" B_PRIu16 " -> %" B_PRIu16 " (%s)\n", __func__,
		levels[currentLevel].vddc, levels[target].vddc,
		vddcChanges ? "CHANGE" : "unchanged",
		levels[currentLevel].vddci, levels[target].vddci,
		vddciChanges ? "CHANGE" : "unchanged");

	if (!vddcChanges && !vddciChanges) {
		TRACE("%s: target needs NO voltage write at all\n", __func__);
	} else if (!vddcChanges) {
		TRACE("%s: target needs only a VDDCI write; VDDC stays put\n",
			__func__);
	}

	// An engine-only raise to the target's engine clock is sanctioned by
	// the table at the *current* VDDC whenever the target shares it, which
	// makes it the safest possible first write (Phase B).
	if (!vddcChanges && levels[target].engineClock > currentEngine) {
		TRACE("%s: engine-only raise to %" B_PRIu32 " kHz is valid at the "
			"current VDDC - safe Phase B candidate\n", __func__,
			levels[target].engineClock);
	}

	if (!powerplay_raise_enabled()) {
		TRACE("%s: nothing programmed - 'raise_clocks' is not set in "
			"~/config/settings/kernel/drivers/radeon_hd\n", __func__);
	}
#endif	// TRACE_POWERPLAY
}


// Both CRTCs of the first pair — the only ones this accelerant drives.
static const uint32 kPowerplayCrtcOffsets[2] = {
	EVERGREEN_CRTC0_REGISTER_OFFSET,
	EVERGREEN_CRTC1_REGISTER_OFFSET
};


/*!	Park the display controllers' memory fetches, saving CRTC_CONTROL so the
	caller can put back exactly what was there.

	A memory clock change disrupts the memory controller, so active scanout
	requests have to be stopped first. Skipping this does not fail loudly —
	it fails silently: a first attempt on a Barts HD 6850 had AtomBIOS accept
	SetMemoryClock and return success while the clock never moved at all.
	The reference driver brackets every clock change the same way
	(evergreen_pm_prepare / evergreen_pm_finish).

	The whole register is saved and restored rather than just toggling the
	one bit back, so this cannot leave any other field disturbed. */
static void
powerplay_scanout_requests_disable(uint32* saved)
{
	for (uint32 i = 0; i < B_COUNT_OF(kPowerplayCrtcOffsets); i++) {
		uint32 reg = EVERGREEN_CRTC_CONTROL + kPowerplayCrtcOffsets[i];

		saved[i] = Read32(OUT, reg);
		Write32(OUT, reg,
			saved[i] | EVERGREEN_CRTC_DISP_READ_REQUEST_DISABLE);
	}
}


static void
powerplay_scanout_requests_restore(const uint32* saved)
{
	for (uint32 i = 0; i < B_COUNT_OF(kPowerplayCrtcOffsets); i++) {
		Write32(OUT, EVERGREEN_CRTC_CONTROL + kPowerplayCrtcOffsets[i],
			saved[i]);
	}
}


status_t
powerplay_apply_target()
{
	if (!powerplay_raise_enabled())
		return B_NOT_ALLOWED;

	powerplay_level levels[MAX_POWERPLAY_LEVELS];
	uint32 count;

	if (powerplay_read_levels(levels, MAX_POWERPLAY_LEVELS, &count) != B_OK)
		return B_ERROR;

	// One decision up front, then the ordered writes. Deciding per-write
	// would not compose: after the engine clock alone has been raised the
	// card sits at a clock pair no level advertises, and a second lookup
	// would refuse.
	int32 currentLevel;
	int32 target;
	if (powerplay_select_target(levels, count, &currentLevel, &target)
			!= B_OK) {
		TRACE("%s: no raise available on this board\n", __func__);
		return B_ENTRY_NOT_FOUND;
	}

	if (currentLevel < 0) {
		ERROR("%s: refusing - current clocks match no advertised level, so "
			"the applied voltage is unknown\n", __func__);
		return B_ERROR;
	}

	// A core-voltage raise is a larger, separate step (Turks needs one).
	if (levels[target].vddc != levels[currentLevel].vddc) {
		TRACE("%s: refusing - target level %" B_PRId32 " needs VDDC %" B_PRIu16
			" mV against %" B_PRIu16 " mV applied; core-voltage raises are "
			"out of scope\n", __func__, target, levels[target].vddc,
			levels[currentLevel].vddc);
		return B_NOT_ALLOWED;
	}

	bool wantMemory = powerplay_memory_raise_enabled();
	bool raised = false;
	bool vddciRaised = false;

	// Voltage leads the clocks going up, so VDDCI is written before either
	// clock moves. Only needed for the memory tier: the engine clock is only
	// ever raised to a value the target level states at the present VDDC.
	if (wantMemory && levels[target].vddci != levels[currentLevel].vddci) {
		TRACE("%s: raising VDDCI %" B_PRIu16 " -> %" B_PRIu16 " mV before any "
			"clock\n", __func__, levels[currentLevel].vddci,
			levels[target].vddci);

		if (powerplay_set_voltage(SET_VOLTAGE_TYPE_ASIC_VDDCI,
				levels[target].vddci) != B_OK) {
			ERROR("%s: VDDCI write failed, leaving all clocks alone\n",
				__func__);
			return B_ERROR;
		}

		vddciRaised = true;
	}

	// Engine clock. Sanctioned by the target level at the VDDC already
	// applied, which the check above guaranteed is unchanged.
	uint32 engineBefore = powerplay_engine_clock_current();
	if (levels[target].engineClock > engineBefore) {
		TRACE("%s: raising engine clock %" B_PRIu32 " -> %" B_PRIu32 " kHz at "
			"unchanged VDDC %" B_PRIu16 " mV\n", __func__, engineBefore,
			levels[target].engineClock, levels[currentLevel].vddc);

		SET_ENGINE_CLOCK_PS_ALLOCATION args;
		memset(&args, 0, sizeof(args));
		// AtomBIOS wants 10 kHz units.
		args.ulTargetEngineClock
			= B_HOST_TO_LENDIAN_INT32(levels[target].engineClock / 10);

		int index = GetIndexIntoMasterTable(COMMAND, SetEngineClock);
		if (atom_execute_table(gAtomContext, index, (uint32*)&args) != B_OK) {
			ERROR("%s: SetEngineClock command table failed\n", __func__);
			return B_ERROR;
		}

		// Never trust the write; ask the hardware.
		uint32 after = powerplay_engine_clock_current();
		if (!powerplay_clock_matches(levels[target].engineClock, after)) {
			ERROR("%s: engine read-back says %" B_PRIu32 " kHz, wanted %"
				B_PRIu32 " kHz - stopping here\n", __func__, after,
				levels[target].engineClock);
			return B_ERROR;
		}

		// Publish the real clock so the display bandwidth watermarks are
		// computed from what the card is actually running.
		gInfo->engineClockFrequency = after;
		raised = true;

		TRACE("%s: engine clock now %" B_PRIu32 " kHz (verified)\n", __func__,
			after);
	}

	if (!wantMemory) {
		TRACE("%s: memory clock left alone - 'raise_memory_clock' is not "
			"set\n", __func__);
		return raised ? B_OK : B_ENTRY_NOT_FOUND;
	}

	uint32 memoryBefore = powerplay_memory_clock_current();
	if (levels[target].memoryClock <= memoryBefore)
		return raised ? B_OK : B_ENTRY_NOT_FOUND;

	TRACE("%s: raising memory clock %" B_PRIu32 " -> %" B_PRIu32 " kHz\n",
		__func__, memoryBefore, levels[target].memoryClock);

	SET_MEMORY_CLOCK_PS_ALLOCATION memoryArgs;
	memset(&memoryArgs, 0, sizeof(memoryArgs));
	memoryArgs.ulTargetMemoryClock
		= B_HOST_TO_LENDIAN_INT32(levels[target].memoryClock / 10);

	// Scanout must not be fetching while the memory controller is retimed.
	uint32 savedCrtcControl[B_COUNT_OF(kPowerplayCrtcOffsets)];
	powerplay_scanout_requests_disable(savedCrtcControl);

	int memoryIndex = GetIndexIntoMasterTable(COMMAND, SetMemoryClock);
	status_t status = atom_execute_table(gAtomContext, memoryIndex,
		(uint32*)&memoryArgs);

	powerplay_scanout_requests_restore(savedCrtcControl);

	if (status != B_OK) {
		ERROR("%s: SetMemoryClock command table failed\n", __func__);
		return B_ERROR;
	}

	uint32 memoryAfter = powerplay_memory_clock_current();
	if (!powerplay_clock_matches(levels[target].memoryClock, memoryAfter)) {
		ERROR("%s: memory read-back says %" B_PRIu32 " kHz, wanted %" B_PRIu32
			" kHz - the memory clock did not take\n", __func__, memoryAfter,
			levels[target].memoryClock);

		// The clock never moved, so dropping VDDCI back is safe and leaves
		// the board as it was found rather than sitting at a raised
		// interface voltage for no benefit.
		if (vddciRaised) {
			TRACE("%s: restoring VDDCI to %" B_PRIu16 " mV\n", __func__,
				levels[currentLevel].vddci);
			powerplay_set_voltage(SET_VOLTAGE_TYPE_ASIC_VDDCI,
				levels[currentLevel].vddci);
		}

		return B_ERROR;
	}

	gInfo->memoryClockFrequency = memoryAfter;

	TRACE("%s: memory clock now %" B_PRIu32 " kHz (verified)\n", __func__,
		memoryAfter);

	return B_OK;
}


bool
powerplay_pixel_clock_cap_ignored()
{
	return radeon_setting_enabled("ignore_pixel_clock_cap");
}
