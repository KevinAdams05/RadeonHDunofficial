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
 * This file reads the PowerPlay table's performance levels so that
 * (a) the low clocks can be corroborated against the table's own lowest
 * level rather than trusted from a single query, and (b) the clocks a
 * future power-management implementation would need to target are on the
 * record. It is read-only: nothing here programs a clock or a voltage.
 */
#ifndef RADEON_HD_POWERPLAY_H
#define RADEON_HD_POWERPLAY_H


#include <SupportDefs.h>


/*! TRACE the engine/memory/voltage of every performance level the
	PowerPlay table advertises, plus the resulting min and max.

	Read-only and best-effort: unsupported table revisions and malformed
	offsets are reported and skipped rather than guessed at. Evergreen and
	NI only — the clock-info entry layout differs on DCE 6 and later.

	Called from radeon_gpu_probe() after the current clocks are read, so a
	syslog shows the clocks in use immediately followed by the levels the
	board could reach. */
void powerplay_dump_performance_levels();


#endif	// RADEON_HD_POWERPLAY_H
