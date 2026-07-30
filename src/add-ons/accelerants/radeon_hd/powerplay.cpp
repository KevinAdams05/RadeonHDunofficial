/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 */


#include "powerplay.h"

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


void
powerplay_dump_performance_levels()
{
	radeon_shared_info& info = *gInfo->shared_info;

	// The clock-info entry layout is per-family. Only the Evergreen / NI
	// form (ATOM_PPLIB_EVERGREEN_CLOCK_INFO) is decoded here; DCE 6 and
	// later use ATOM_PPLIB_SI_CLOCK_INFO and friends.
	if (info.dceMajor != 4 && info.dceMajor != 5) {
		TRACE("%s: skipped, only Evergreen / NI clock info is decoded\n",
			__func__);
		return;
	}

	uint16 tableSize;
	uint8 tableMajor;
	uint8 tableMinor;
	uint16 tableOffset;

	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
	if (atom_parse_data_header(gAtomContext, index, &tableSize, &tableMajor,
		&tableMinor, &tableOffset) != B_OK) {
		ERROR("%s: no PowerPlay table in AtomBIOS\n", __func__);
		return;
	}

	TRACE("%s: PowerPlay table %" B_PRIu8 ".%" B_PRIu8 ", %" B_PRIu16
		" bytes\n", __func__, tableMajor, tableMinor, tableSize);

	if (tableSize == 0) {
		ERROR("%s: table reports zero size, refusing to walk it\n", __func__);
		return;
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
		return;
	}

	if (entrySize != wanted) {
		ERROR("%s: clock info entry is %" B_PRIu8 " bytes, expected %"
			B_PRIu32 " for Evergreen / NI\n", __func__, entrySize, wanted);
		return;
	}

	uint32 levels = (uint32)(nonClockOffset - clockOffset) / entrySize;
	if (levels == 0) {
		ERROR("%s: clock info array holds no whole entries\n", __func__);
		return;
	}

	// Informational: states index into this array, so the two counts are
	// normally equal but are not required to be.
	if (levels != powerPlay->ucNumStates) {
		TRACE("%s: %" B_PRIu32 " clock level(s) but %" B_PRIu8 " state(s)\n",
			__func__, levels, powerPlay->ucNumStates);
	}

	uint32 engineMin = 0xffffffff;
	uint32 engineMax = 0;
	uint32 memoryMin = 0xffffffff;
	uint32 memoryMax = 0;

	for (uint32 level = 0; level < levels; level++) {
		ATOM_PPLIB_EVERGREEN_CLOCK_INFO* entry
			= (ATOM_PPLIB_EVERGREEN_CLOCK_INFO*)(table + clockOffset
				+ level * entrySize);

		// Clocks are split into a 16-bit low and an 8-bit high half, in
		// 10 kHz units like the rest of AtomBIOS.
		uint32 engine = B_LENDIAN_TO_HOST_INT16(entry->usEngineClockLow)
			| ((uint32)entry->ucEngineClockHigh << 16);
		uint32 memory = B_LENDIAN_TO_HOST_INT16(entry->usMemoryClockLow)
			| ((uint32)entry->ucMemoryClockHigh << 16);
		engine *= 10;
		memory *= 10;

		TRACE("  level %" B_PRIu32 ": engine %" B_PRIu32 " kHz, memory %"
			B_PRIu32 " kHz, VDDC %" B_PRIu16 " mV, VDDCI %" B_PRIu16 " mV\n",
			level, engine, memory,
			B_LENDIAN_TO_HOST_INT16(entry->usVDDC),
			B_LENDIAN_TO_HOST_INT16(entry->usVDDCI));

		if (engine < engineMin)
			engineMin = engine;
		if (engine > engineMax)
			engineMax = engine;
		if (memory < memoryMin)
			memoryMin = memory;
		if (memory > memoryMax)
			memoryMax = memory;
	}

	TRACE("%s: %" B_PRIu32 " level(s): engine %" B_PRIu32 " - %" B_PRIu32
		" kHz, memory %" B_PRIu32 " - %" B_PRIu32 " kHz\n", __func__, levels,
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
