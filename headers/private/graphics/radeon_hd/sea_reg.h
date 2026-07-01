/*
 * DCE_8_0 Register documentation
 *
 * Copyright (C) 2014  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* This header has been modified from its original version:
 * it has been formatted to contain only what Haiku needs. */

#ifndef DCE_8_0_D_H
#define DCE_8_0_D_H

/* NOTE: AMD's dce_8_0_d.h lists mmDC_GPIO_HPD_A as the dword register index
 * 0x196d. connector_pick_atom_hpdid() compares this against gGPIOInfo->hwReg,
 * which is populated as (AtomBIOS dword index * 4) -- i.e. a BYTE offset. The
 * sibling targets it is compared against are likewise byte offsets
 * (EVERGREEN_DC_GPIO_HPD_A 0x64b4, SI_DC_GPIO_HPD_A 0x65b4). The original
 * 0x196d dword value therefore never matched on Sea Islands (DCE8), so the HPD
 * id resolved to 0xff (HPD_NONE) and DisplayPort AUX transactions failed with
 * "flags not zero". Express it as the byte offset like its siblings: it is the
 * same physical register as SI (0x196d * 4 == 0x65b4). */
#define SEA_mmDC_GPIO_HPD_A                                                         0x65b4

#endif /* DCE_8_0_D_H */
