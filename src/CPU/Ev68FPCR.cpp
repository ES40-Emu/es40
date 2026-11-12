/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023-2026 by gdwnldsKSC
 * Copyright (C) 2026 by Scott Klein
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 *
 */

// EV68 FPCR access rules extracted from AlphaCPU.cpp.
#include "StdAfx.h"
#include "cpu_defs.h"
#include "Ev68FPCR.h"

// ============================================================================
//
// EV68 FPCR layout (all meaningful bits are in the upper 32 bits):
//
//   63    SUM      - Summary Bit (SUM).
//   62    INED     - Inexact Disable (INED).
//   61    UNFD     - Underflow Disable (UNFD)
//   60    UNDZ     - Underflow to Zero (UNDZ)
//   59:58 DYN      -  Dynamic Rounding Mode (DYN)
//   57    IOV      - Integer Overflow (IOV)
//   56    INE      - Inexact Result (INE)
//   55    UNF      - Underflow (UNF).
//   54    OVF      - Overflow (OVF)
//   53    DZE      - Division by Zero (DZE)
//   52    INV      - Invalid Operation (INV)
//   51    OVFD     - Overflow Disable (OVFD)
//   50    DZED     - Division by Zero Disable (DZED)
//   49    INVD     - Invalid Operation Disable (INVD)
//   48    DNZ      - Denormal Operands to Zero (DNZ)
//   47    DNOD     - Denormal Operand Exception Disable (DNOD)
//   46:0  Reserved, must be read as zero
//
//   Alpha Architecture Reference Manual, 4th ed. [ARM]:
//     Section 4.7.8   - Floating-Point Control Register (FPCR)
//     Section 4.7.8.1 - Accessing the FPCR
//     Section 4.7.8.2 - Default Values of the FPCR
//     Section 4.10.4  - Move from/to Floating-Point Control Register
//   at least according to this one https://download.majix.org/dec/alpha_arch_ref.pdf
// ============================================================================

std::uint64_t Ev68FPCR::normalize(std::uint64_t value)
{
  u64 val = value & ~(FPCR_RAZ | U64(0x00000000FFFFFFFF));

  if (val & FPCR_ERR)
    val |= FPCR_SUM;
  else
    val &= ~FPCR_SUM;

  return val;
}
