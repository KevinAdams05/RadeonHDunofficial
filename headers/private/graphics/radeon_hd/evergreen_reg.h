/*
 * Copyright 2010 Advanced Micro Devices, Inc.
 * Copyright 2000 ATI Technologies Inc., Markham, Ontario, and
 *                VA Linux Systems Inc., Fremont, California.
 * Copyright 2026 Kevin Adams <kevinadams05@gmail.com>.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Alex Deucher
 *          Kevin E. Martin <martin@xfree86.org>
 *          Rickard E. Faith <faith@valinux.com>
 *          Alan Hourihane <alanh@fairlite.demon.co.uk>
 *          Alexander von Gluck <kallisti5@unixzen.com>
 */
#ifndef __EVERGREEN_REG_H__
#define __EVERGREEN_REG_H__


/* Scratch Registers */
#define	EVERGREEN_SCRATCH_REG0				0x8500
#define	EVERGREEN_SCRATCH_REG1				0x8504
#define	EVERGREEN_SCRATCH_REG2				0x8508
#define	EVERGREEN_SCRATCH_REG3				0x850C
#define	EVERGREEN_SCRATCH_REG4				0x8510
#define	EVERGREEN_SCRATCH_REG5				0x8514
#define	EVERGREEN_SCRATCH_REG6				0x8518
#define	EVERGREEN_SCRATCH_REG7				0x851C
#define	EVERGREEN_SCRATCH_UMSK				0x8540
#define	EVERGREEN_SCRATCH_ADDR				0x8544

/* CRT controler register offset */
#define EVERGREEN_CRTC0_REGISTER_OFFSET		(0x6df0 - 0x6df0)
#define EVERGREEN_CRTC1_REGISTER_OFFSET		(0x79f0 - 0x6df0)
#define EVERGREEN_CRTC2_REGISTER_OFFSET		(0x105f0 - 0x6df0)
#define EVERGREEN_CRTC3_REGISTER_OFFSET		(0x111f0 - 0x6df0)
#define EVERGREEN_CRTC4_REGISTER_OFFSET		(0x11df0 - 0x6df0)
#define EVERGREEN_CRTC5_REGISTER_OFFSET		(0x129f0 - 0x6df0)

/* Memory Controler */
#define	EVERGREEN_MC_ARB_RAMCFG						0x2760
#define		EVERGREEN_NOOFBANK_SHIFT				0
#define		EVERGREEN_NOOFBANK_MASK					0x00000003
#define		EVERGREEN_NOOFRANK_SHIFT				2
#define		EVERGREEN_NOOFRANK_MASK					0x00000004
#define		EVERGREEN_NOOFROWS_SHIFT				3
#define		EVERGREEN_NOOFROWS_MASK					0x00000038
#define		EVERGREEN_NOOFCOLS_SHIFT				6
#define		EVERGREEN_NOOFCOLS_MASK					0x000000C0
#define		EVERGREEN_CHANSIZE_SHIFT				8
#define		EVERGREEN_CHANSIZE_MASK					0x00000100
#define		EVERGREEN_BURSTLENGTH_SHIFT				9
#define		EVERGREEN_BURSTLENGTH_MASK				0x00000200
#define		EVERGREEN_CHANSIZE_OVERRIDE				(1 << 11)
#define	EVERGREEN_FUS_MC_ARB_RAMCFG					0x2768
#define	EVERGREEN_MC_VM_AGP_TOP						0x2028
#define	EVERGREEN_MC_VM_AGP_BOT						0x202C
#define	EVERGREEN_MC_VM_AGP_BASE					0x2030
#define	EVERGREEN_MC_VM_FB_LOCATION					0x2024
#define	EVERGREEN_MC_FUS_VM_FB_OFFSET				0x2898
#define	EVERGREEN_MC_VM_MB_L1_TLB0_CNTL				0x2234
#define	EVERGREEN_MC_VM_MB_L1_TLB1_CNTL				0x2238
#define	EVERGREEN_MC_VM_MB_L1_TLB2_CNTL				0x223C
#define	EVERGREEN_MC_VM_MB_L1_TLB3_CNTL				0x2240
#define		EVERGREEN_ENABLE_L1_TLB						(1 << 0)
#define		EVERGREEN_ENABLE_L1_FRAGMENT_PROCESSING		(1 << 1)
#define		EVERGREEN_SYSTEM_ACCESS_MODE_PA_ONLY		(0 << 3)
#define		EVERGREEN_SYSTEM_ACCESS_MODE_USE_SYS_MAP	(1 << 3)
#define		EVERGREEN_SYSTEM_ACCESS_MODE_IN_SYS			(2 << 3)
#define		EVERGREEN_SYSTEM_ACCESS_MODE_NOT_IN_SYS		(3 << 3)
#define		EVERGREEN_SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU (0 << 5)
#define		EVERGREEN_EFFECTIVE_L1_TLB_SIZE(x)			((x)<<15)
#define		EVERGREEN_EFFECTIVE_L1_QUEUE_SIZE(x)		((x)<<18)
#define	EVERGREEN_MC_VM_MD_L1_TLB0_CNTL				0x2654
#define	EVERGREEN_MC_VM_MD_L1_TLB1_CNTL				0x2658
#define	EVERGREEN_MC_VM_MD_L1_TLB2_CNTL				0x265C

#define	EVERGREEN_FUS_MC_VM_MD_L1_TLB0_CNTL			0x265C
#define	EVERGREEN_FUS_MC_VM_MD_L1_TLB1_CNTL			0x2660
#define	EVERGREEN_FUS_MC_VM_MD_L1_TLB2_CNTL			0x2664

#define	EVERGREEN_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR 0x203C
#define	EVERGREEN_MC_VM_SYSTEM_APERTURE_HIGH_ADDR	0x2038
#define	EVERGREEN_MC_VM_SYSTEM_APERTURE_LOW_ADDR	0x2034

/* Hot Plug Detection */
#define EVERGREEN_HDP_HOST_PATH_CNTL				0x2C00
#define EVERGREEN_HDP_NONSURFACE_BASE				0x2C04
#define EVERGREEN_HDP_NONSURFACE_INFO				0x2C08
#define EVERGREEN_HDP_NONSURFACE_SIZE				0x2C0C
#define EVERGREEN_HDP_TILING_CONFIG					0x2F3C
#define EVERGREEN_HDP_MEM_COHERENCY_FLUSH_CNTL		0x5480
#define EVERGREEN_HDP_REG_COHERENCY_FLUSH_CNTL		0x54A0

/* Sensors */
#define EVERGREEN_CG_THERMAL_CTRL					0x72c
#define		EVERGREEN_TOFFSET_MASK					0x00003FE0
#define		EVERGREEN_TOFFSET_SHIFT					5
#define EVERGREEN_CG_MULT_THERMAL_STATUS			0x740
#define		EVERGREEN_ASIC_T(x)						((x) << 16)
#define		EVERGREEN_ASIC_T_MASK					0x07FF0000
#define     EVERGREEN_ASIC_T_SHIFT					16
#define EVERGREEN_CG_TS0_STATUS						0x760
#define		EVERGREEN_TS0_ADC_DOUT_MASK				0x000003FF
#define		EVERGREEN_TS0_ADC_DOUT_SHIFT			0
/* APU */
#define EVERGREEN_CG_THERMAL_STATUS					0x678

#define EVERGREEN_VGA_MEMORY_BASE_ADDRESS			0x310
#define EVERGREEN_VGA_MEMORY_BASE_ADDRESS_HIGH		0x324
#define EVERGREEN_D3VGA_CONTROL						0x3e0
#define EVERGREEN_D4VGA_CONTROL						0x3e4
#define EVERGREEN_D5VGA_CONTROL						0x3e8
#define EVERGREEN_D6VGA_CONTROL						0x3ec
#define EVERGREEN_P1PLL_SS_CNTL						0x414
#define EVERGREEN_P2PLL_SS_CNTL						0x454
#define 	EVERGREEN_PxPLL_SS_EN					(1 << 12)
#define EVERGREEN_GRPH_ENABLE						0x6800
#define EVERGREEN_GRPH_CONTROL						0x6804
#define 	EVERGREEN_GRPH_DEPTH(x)					(((x) & 0x3) << 0)
#define 	EVERGREEN_GRPH_DEPTH_8BPP				0
#define 	EVERGREEN_GRPH_DEPTH_16BPP				1
#define 	EVERGREEN_GRPH_DEPTH_32BPP				2
#define 	EVERGREEN_GRPH_FORMAT(x)				(((x) & 0x7) << 8)
#define 	EVERGREEN_GRPH_ARRAY_MODE(x)			(((x) & 0xf) << 20)
#define 	EVERGREEN_GRPH_ARRAY_LINEAR_GENERAL		0
#define 	EVERGREEN_GRPH_ARRAY_LINEAR_ALIGNED		1
#define 	EVERGREEN_GRPH_ARRAY_1D_TILED_THIN1		2
#define 	EVERGREEN_GRPH_ARRAY_2D_TILED_THIN1		4
#define 	EVERGREEN_GRPH_FORMAT_INDEXED			0
#define 	EVERGREEN_GRPH_FORMAT_ARGB1555			0
#define 	EVERGREEN_GRPH_FORMAT_ARGB565			1
#define 	EVERGREEN_GRPH_FORMAT_ARGB4444			2
#define 	EVERGREEN_GRPH_FORMAT_AI88				3
#define 	EVERGREEN_GRPH_FORMAT_MONO16			4
#define 	EVERGREEN_GRPH_FORMAT_BGRA5551			5
#define 	EVERGREEN_GRPH_FORMAT_ARGB8888			0
#define 	EVERGREEN_GRPH_FORMAT_ARGB2101010		1
#define 	EVERGREEN_GRPH_FORMAT_32BPP_DIG			2
#define 	EVERGREEN_GRPH_FORMAT_8B_ARGB2101010	3
#define 	EVERGREEN_GRPH_FORMAT_BGRA1010102		4
#define 	EVERGREEN_GRPH_FORMAT_8B_BGRA1010102	5
#define 	EVERGREEN_GRPH_FORMAT_RGB111110			6
#define 	EVERGREEN_GRPH_FORMAT_BGR101111			7
#define EVERGREEN_GRPH_SWAP_CONTROL					0x680c
#define 	EVERGREEN_GRPH_ENDIAN_SWAP(x)			(((x) & 0x3) << 0)
#define 	EVERGREEN_GRPH_ENDIAN_NONE				0
#define 	EVERGREEN_GRPH_ENDIAN_8IN16				1
#define 	EVERGREEN_GRPH_ENDIAN_8IN32				2
#define 	EVERGREEN_GRPH_ENDIAN_8IN64				3
#define 	EVERGREEN_GRPH_RED_CROSSBAR(x)			(((x) & 0x3) << 4)
#define 	EVERGREEN_GRPH_RED_SEL_R				0
#define 	EVERGREEN_GRPH_RED_SEL_G				1
#define 	EVERGREEN_GRPH_RED_SEL_B				2
#define 	EVERGREEN_GRPH_RED_SEL_A				3
#define 	EVERGREEN_GRPH_GREEN_CROSSBAR(x)		(((x) & 0x3) << 6)
#define 	EVERGREEN_GRPH_GREEN_SEL_G				0
#define 	EVERGREEN_GRPH_GREEN_SEL_B				1
#define 	EVERGREEN_GRPH_GREEN_SEL_A				2
#define 	EVERGREEN_GRPH_GREEN_SEL_R				3
#define 	EVERGREEN_GRPH_BLUE_CROSSBAR(x)			(((x) & 0x3) << 8)
#define 	EVERGREEN_GRPH_BLUE_SEL_B				0
#define 	EVERGREEN_GRPH_BLUE_SEL_A				1
#define 	EVERGREEN_GRPH_BLUE_SEL_R				2
#define 	EVERGREEN_GRPH_BLUE_SEL_G				3
#define 	EVERGREEN_GRPH_ALPHA_CROSSBAR(x)		(((x) & 0x3) << 10)
#define 	EVERGREEN_GRPH_ALPHA_SEL_A				0
#define 	EVERGREEN_GRPH_ALPHA_SEL_R				1
#define 	EVERGREEN_GRPH_ALPHA_SEL_G				2
#define 	EVERGREEN_GRPH_ALPHA_SEL_B				3
#define EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS		0x6810
#define EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS	0x6814
#define 	EVERGREEN_GRPH_DFQ_ENABLE				(1 << 0)
#define 	EVERGREEN_GRPH_SURFACE_ADDRESS_MASK		0xffffff00
#define EVERGREEN_GRPH_PITCH						0x6818
#define EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH	0x681c
#define EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH 0x6820
#define EVERGREEN_GRPH_SURFACE_OFFSET_X				0x6824
#define EVERGREEN_GRPH_SURFACE_OFFSET_Y				0x6828
#define EVERGREEN_GRPH_X_START						0x682c
#define EVERGREEN_GRPH_Y_START						0x6830
#define EVERGREEN_GRPH_X_END						0x6834
#define EVERGREEN_GRPH_Y_END						0x6838
#define EVERGREEN_GRPH_FLIP_CONTROL					0x6848
#       define EVERGREEN_GRPH_SURFACE_UPDATE_H_RETRACE_EN (1 << 0)
#define EVERGREEN_CUR_CONTROL						0x6998
#define 	EVERGREEN_CURSOR_EN						(1 << 0)
#define 	EVERGREEN_CURSOR_MODE(x)				(((x) & 0x3) << 8)
#define 	EVERGREEN_CURSOR_MONO					0
#define 	EVERGREEN_CURSOR_24_1					1
#define 	EVERGREEN_CURSOR_24_8_PRE_MULT			2
#define 	EVERGREEN_CURSOR_24_8_UNPRE_MULT		3
#define 	EVERGREEN_CURSOR_2X_MAGNIFY				(1 << 16)
#define 	EVERGREEN_CURSOR_FORCE_MC_ON			(1 << 20)
#define 	EVERGREEN_CURSOR_URGENT_CONTROL(x)		(((x) & 0x7) << 24)
#define 	EVERGREEN_CURSOR_URGENT_ALWAYS			0
#define 	EVERGREEN_CURSOR_URGENT_1_8				1
#define 	EVERGREEN_CURSOR_URGENT_1_4				2
#define 	EVERGREEN_CURSOR_URGENT_3_8				3
#define 	EVERGREEN_CURSOR_URGENT_1_2				4
#define EVERGREEN_CUR_SURFACE_ADDRESS				0x699c
#define 	EVERGREEN_CUR_SURFACE_ADDRESS_MASK		0xfffff000
#define EVERGREEN_CUR_SIZE							0x69a0
#define EVERGREEN_CUR_SURFACE_ADDRESS_HIGH			0x69a4
#define EVERGREEN_CUR_POSITION						0x69a8
#define EVERGREEN_CUR_HOT_SPOT						0x69ac
#define EVERGREEN_CUR_COLOR1						0x69b0
#define EVERGREEN_CUR_COLOR2						0x69b4
#define EVERGREEN_CUR_UPDATE						0x69b8
#define 	EVERGREEN_CURSOR_UPDATE_PENDING			(1 << 0)
#define 	EVERGREEN_CURSOR_UPDATE_TAKEN			(1 << 1)
#define 	EVERGREEN_CURSOR_UPDATE_LOCK			(1 << 16)
#define 	EVERGREEN_CURSOR_DISABLE_MULTIPLE_UPDATE (1 << 24)
#define EVERGREEN_DC_LUT_RW_MODE					0x69e0
#define EVERGREEN_DC_LUT_RW_INDEX					0x69e4
#define EVERGREEN_DC_LUT_SEQ_COLOR					0x69e8
#define EVERGREEN_DC_LUT_PWL_DATA					0x69ec
#define EVERGREEN_DC_LUT_30_COLOR					0x69f0
#define EVERGREEN_DC_LUT_VGA_ACCESS_ENABLE			0x69f4
#define EVERGREEN_DC_LUT_WRITE_EN_MASK				0x69f8
#define EVERGREEN_DC_LUT_AUTOFILL					0x69fc
#define EVERGREEN_DC_LUT_CONTROL					0x6a00
#define EVERGREEN_DC_LUT_BLACK_OFFSET_BLUE			0x6a04
#define EVERGREEN_DC_LUT_BLACK_OFFSET_GREEN			0x6a08
#define EVERGREEN_DC_LUT_BLACK_OFFSET_RED			0x6a0c
#define EVERGREEN_DC_LUT_WHITE_OFFSET_BLUE			0x6a10
#define EVERGREEN_DC_LUT_WHITE_OFFSET_GREEN			0x6a14
#define EVERGREEN_DC_LUT_WHITE_OFFSET_RED			0x6a18
#define EVERGREEN_DATA_FORMAT						0x6b00
#define 	EVERGREEN_INTERLEAVE_EN					(1 << 0)
#define EVERGREEN_DESKTOP_HEIGHT					0x6b04
#define EVERGREEN_VLINE_START_END					0x6b08
#define 	EVERGREEN_VLINE_START_SHIFT				0
#define 	EVERGREEN_VLINE_END_SHIFT				16
#define 	EVERGREEN_VLINE_INV						(1 << 31)
#define EVERGREEN_VLINE_STATUS						0x6bb8
#define 	EVERGREEN_VLINE_STAT					(1 << 12)
#define EVERGREEN_VIEWPORT_START					0x6d70
#define EVERGREEN_VIEWPORT_SIZE						0x6d74
#define EVERGREEN_CRTC_CONTROL						0x6e70
#define 	EVERGREEN_CRTC_MASTER_EN				(1 << 0)
#define		EVERGREEN_CRTC_DISP_READ_REQUEST_DISABLE (1 << 24)
#define EVERGREEN_CRTC_STATUS						0x6e8c
#define EVERGREEN_CRTC_UPDATE_LOCK					0x6ed4
#define EVERGREEN_GRPH_UPDATE						0x6844
#define		EVERGREEN_GRPH_UPDATE_LOCK				(1 << 16)
#define		EVERGREEN_GRPH_SURFACE_UPDATE_PENDING	(1 << 2)
#define EVERGREEN_MASTER_UPDATE_LOCK				0x6ef4
#define EVERGREEN_MASTER_UPDATE_MODE				0x6ef8
#define EVERGREEN_DC_GPIO_HPD_MASK					0x64b0
#define EVERGREEN_DC_GPIO_HPD_A						0x64b4
#define EVERGREEN_DC_GPIO_HPD_EN					0x64b8
#define EVERGREEN_DC_GPIO_HPD_Y						0x64bc


/* Display Bandwidth / Watermark / Priority registers
 *
 * The display engine arbitrates with other memory clients via two
 * watermark slots (A = high-clock, B = low-clock for DPM transitions)
 * and a priority counter per CRTC. Without these programmed, scanout
 * DMA can starve at high resolutions / refresh rates and the display
 * FIFO underruns — symptom is stride-aliased garbage at modes the chip
 * can otherwise drive cleanly. The forced-PRIORITY_ALWAYS_ON path is
 * what fixes 4K scanout on bandwidth-tight cards (e.g. Caicos).
 *
 * The LB-split and PRIORITY counters live at the same absolute offsets
 * across DCE 4 (Evergreen), DCE 5 (Northern Islands), DCE 6 (Southern
 * Islands), and DCE 8 (Sea Islands) — each CRTC adds an offset from
 * EVERGREEN_CRTCn_REGISTER_OFFSET above.
 *
 * The PIPE_* arbitration pair below covers every chipset this driver
 * programs watermarks on: DCE 4 (Evergreen), DCE 4.1 (Palm / Sumo) and
 * DCE 5 (Northern Islands, including Cayman). DCE 6 / Southern Islands
 * moved the pair into the DPG block at a different offset and layout —
 * see the NI_DPG_* note in ni_reg.h.
 *
 * NOTE: the two register groups here use *different* per-pipe strides.
 * See EVERGREEN_PIPE_ARBITRATION_STRIDE below.
 */
/* On DCE 4 and DCE 5 the partition number is written as a bare value into
 * bits 2:0 of this register. DCE 6 / Southern Islands keeps the same
 * register address but moved the field to bits 23:20 — that is what
 * SI_DC_LB_MEMORY_CONFIG() in si_reg.h is for. Do not use the shifted
 * form here: an earlier revision of this header carried a
 * EVERGREEN_DC_LB_MEMORY_CONFIG(x) << 20 macro copied from the SI
 * layout, and on a Turks board it wrote to read-only bits while silently
 * clearing the partner CRTC's real setting in bits 2:0. */
#define EVERGREEN_DC_LB_MEMORY_SPLIT				0x6b0c
#define		EVERGREEN_DC_LB_MEMORY_SPLIT_MASK		0x00000007
#define		EVERGREEN_DC_LB_DISP1_END_ADR_SHIFT		4
#define		EVERGREEN_DC_LB_MEMORY_SPLIT_D1HALF_D2HALF	0
#define		EVERGREEN_DC_LB_MEMORY_SPLIT_D1_3Q_D2_1Q	1
#define		EVERGREEN_DC_LB_MEMORY_SPLIT_D1_ONLY		2
#define		EVERGREEN_DC_LB_MEMORY_SPLIT_D1_1Q_D2_3Q	3
// Each line buffer is shared by a CRTC pair. The four partitions above
// describe the first CRTC's share; the second CRTC of the pair selects
// the mirrored partition by adding this to the partition number.
#define		EVERGREEN_DC_LB_MEMORY_SPLIT_SECOND		4

#define EVERGREEN_PRIORITY_A_CNT					0x6b18
#define EVERGREEN_PRIORITY_B_CNT					0x6b1c
#define		EVERGREEN_PRIORITY_MARK_MASK			0x7fff
#define		EVERGREEN_PRIORITY_OFF					(1 << 16)
#define		EVERGREEN_PRIORITY_ALWAYS_ON			(1 << 20)

/* Per-pipe display arbitration — DCE 4, 4.1 and 5.
 *
 * ARBITRATION_CONTROL3 selects which of the two latency-watermark slots
 * LATENCY_CONTROL reads and writes; slot A is for the high clock state
 * and slot B for the low one (DPM). Both slots must be programmed.
 */
#define EVERGREEN_PIPE0_ARBITRATION_CONTROL3		0x0bf0
#define		EVERGREEN_PIPE_LATENCY_WATERMARK_MASK(x)	(((x) & 0x3) << 16)
#define EVERGREEN_PIPE0_LATENCY_CONTROL				0x0bf4
#define		EVERGREEN_PIPE_LATENCY_LOW_WATERMARK(x)		(((x) & 0xffff) << 0)
#define		EVERGREEN_PIPE_LATENCY_HIGH_WATERMARK(x)	(((x) & 0xffff) << 16)
#define EVERGREEN_PIPE0_DMIF_BUFFER_CONTROL			0x0ca0
#define		EVERGREEN_DMIF_BUFFERS_ALLOCATED(x)		(((x) & 0xf) << 0)
#define		EVERGREEN_DMIF_BUFFERS_ALLOCATED_COMPLETED	(1 << 4)

// Two different per-pipe strides, which is easy to get wrong: the
// arbitration/latency pair steps 0x10 per pipe, while the DMIF buffer
// control steps 0x20. They are identical for pipe 0, so a mistake here
// only shows up once a second head is programmed.
#define EVERGREEN_PIPE_ARBITRATION_STRIDE			0x10
#define EVERGREEN_PIPE_REGISTER_STRIDE				0x20
	// DMIF buffer control only

/* MC channel-count map. Address shared with R700; mask differs across
 * generations (Evergreen / NI = 2 bits → max 8 channels;
 * SI / CIK widen to 4 bits via SI_NOOFCHAN_MASK). */
#define EVERGREEN_MC_SHARED_CHMAP					0x2004
#define		EVERGREEN_NOOFCHAN_SHIFT				12
#define		EVERGREEN_NOOFCHAN_MASK					0x00003000


/* HDMI / AFMT infoframe registers.
 *
 * The display block has six AFMT instances on DCE 4 / 5 / 6 / 8, one
 * per DIG (digital) encoder. Linux radeon hardcodes per-block offsets
 * in eg_offsets[]; we expose the same six values via
 * EVERGREEN_AFMTn_OFFSET so callers can index by AFMT/DIG instance.
 *
 * NOTE: these offsets are NOT the same as EVERGREEN_CRTCn_REGISTER_OFFSET
 * above. AFMT blocks live at their own MMIO stride. For a single-display
 * Cedar/HDMI test the AFMT instance happens to match the CRTC index, but
 * the broader mapping is connector → DIG encoder → AFMT block, not
 * connector → CRTC → AFMT block.
 *
 * Without these programmed, ATOM_ENCODER_MODE_HDMI causes the encoder
 * to emit data-island guard bands during HBLANK that receivers decode
 * as visible pixels — the magenta-stripe bug worked around in Phase 1.5
 * by forcing HDMIA connectors to ATOM_ENCODER_MODE_DVI. With a proper
 * AVI infoframe + HDMI_KEEPOUT_MODE, the data island is correctly
 * interpreted as packet data and the workaround can be retired.
 */
/* One AFMT block per DIG encoder; the blocks sit inside the DIG
 * register ranges (DIG0 0x7000, DIG1 0x7C00, DIG2 0x10800, DIG3
 * 0x11400, DIG4 0x12000, DIG5 0x12C00), so the per-instance offsets
 * relative to the DIG0-based register defines above are the same
 * values Linux reuses from EVERGREEN_CRTCn_REGISTER_OFFSET for its
 * afmt[] table (radeon_display.c eg_offsets[]). Index by DIG id from
 * encoder_pick_dig(), NOT by CRTC id. */
#define EVERGREEN_AFMT0_OFFSET						(0x7000 - 0x7000)
#define EVERGREEN_AFMT1_OFFSET						(0x7C00 - 0x7000)
#define EVERGREEN_AFMT2_OFFSET						(0x10800 - 0x7000)
#define EVERGREEN_AFMT3_OFFSET						(0x11400 - 0x7000)
#define EVERGREEN_AFMT4_OFFSET						(0x12000 - 0x7000)
#define EVERGREEN_AFMT5_OFFSET						(0x12C00 - 0x7000)

/* Per-block AFMT / HDMI register base addresses. Add the AFMTn offset
 * above to address a specific block's copy. */
#define EVERGREEN_HDMI_CONTROL						0x7030
#define		EVERGREEN_HDMI_KEEPOUT_MODE				(1 << 0)
#define		EVERGREEN_HDMI_PACKET_GEN_VERSION		(1 << 4)
#define		EVERGREEN_HDMI_DEEP_COLOR_ENABLE		(1 << 24)

#define EVERGREEN_HDMI_AUDIO_PACKET_CONTROL			0x7038
#define EVERGREEN_HDMI_ACR_PACKET_CONTROL			0x703C
#define EVERGREEN_HDMI_VBI_PACKET_CONTROL			0x7040
#define		EVERGREEN_HDMI_NULL_SEND				(1 << 0)
#define		EVERGREEN_HDMI_GC_SEND					(1 << 4)
#define		EVERGREEN_HDMI_GC_CONT					(1 << 5)
	/* GC_CONT: 0 = send GC packet once, 1 = every frame */

#define EVERGREEN_HDMI_INFOFRAME_CONTROL0			0x7044
#define		EVERGREEN_HDMI_AVI_INFO_SEND			(1 << 0)
#define		EVERGREEN_HDMI_AVI_INFO_CONT			(1 << 1)
#define		EVERGREEN_HDMI_AUDIO_INFO_SEND			(1 << 4)
#define		EVERGREEN_HDMI_AUDIO_INFO_CONT			(1 << 5)
#define		EVERGREEN_HDMI_MPEG_INFO_SEND			(1 << 8)
#define		EVERGREEN_HDMI_MPEG_INFO_CONT			(1 << 9)

#define EVERGREEN_HDMI_INFOFRAME_CONTROL1			0x7048
#define		EVERGREEN_HDMI_AVI_INFO_LINE_MASK		0x0000003F
#define		EVERGREEN_HDMI_AVI_INFO_LINE(x)			((x) & 0x3F)

#define EVERGREEN_HDMI_GENERIC_PACKET_CONTROL		0x704C

#define EVERGREEN_HDMI_GC							0x7058
#define		EVERGREEN_HDMI_GC_AVMUTE				(1 << 0)
#define		EVERGREEN_HDMI_GC_AVMUTE_CONT			(1 << 2)

/* AVI infoframe registers. Pack 4 × 32-bit words:
 *   AFMT_AVI_INFO0 = PB1 | (PB2 << 8) | (PB3 << 16) | (PB4 << 24)
 *   AFMT_AVI_INFO1 = PB5 | (PB6 << 8) | (PB7 << 16) | (PB8 << 24)
 *   AFMT_AVI_INFO2 = PB9 | (PB10 << 8) | (PB11 << 16) | (PB12 << 24)
 *   AFMT_AVI_INFO3 = PB13 | (PB0 << 24)   PB0 = checksum, high byte
 * The 3-byte header (HB0=0x82, HB1=0x02, HB2=0x0D) is emitted by hw
 * and is NOT written to registers — only the 14-byte payload + checksum.
 * Write order matters: INFO3 last, because some HW latches infoframe
 * state on the write to INFO3. */
#define EVERGREEN_AFMT_AVI_INFO0					0x7084
#define EVERGREEN_AFMT_AVI_INFO1					0x7088
#define EVERGREEN_AFMT_AVI_INFO2					0x708C
#define EVERGREEN_AFMT_AVI_INFO3					0x7090

#define EVERGREEN_AFMT_INFOFRAME_CONTROL0			0x7134
#define EVERGREEN_AFMT_AUDIO_PACKET_CONTROL			0x712C
#define		EVERGREEN_AFMT_AUDIO_SAMPLE_SEND		(1 << 0)


#endif /* __EVERGREEN_REG_H__ */
