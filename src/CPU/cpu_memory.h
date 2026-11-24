/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
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

 /**
  * \file
  * Contains code macros for the processor memory load/store instructions.
  * Based on ARM chapter 4.2.
  *
  * $Id$
  *
  * X-1.12       Camiel Vanderhoeven                             12-JUN-2008
  *      Support for last written and last read memory locations.
  *
  * X-1.10       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.9        Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.8        Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.7        Camiel Vanderhoeven                             25-JAN-2008
  *      Trap on unalogned memory access. The previous implementation where
  *      unaligned accesses were silently allowed could go wrong when page
  *      boundaries are crossed.
  *
  * X-1.6        Camiel Vanderhoeven                             18-JAN-2008
  *      Replaced sext_64 inlines with sext_u64_<bits> inlines for
  *      performance reasons (thanks to David Hittner for spotting this!);
  *
  * X-1.5        Camiel Vanderhoeven                             2-DEC-2007
  *      Changed the way translation buffers work.
  *
  * X-1.4        Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.3        Camiel Vanderhoeven                             30-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.2        Camiel Vanderhoeven                             8-MAR-2007
  *      LDL and LDQ where the destination is R31 do not cause exceptions.
  *
  * X-1.1        Camiel Vanderhoeven                             18-FEB-2007
  *      File created. Contains code previously found in AlphaCPU.h
  **/
#define DO_LDA    state.r[REG_1] = state.r[REG_2] + DISP_16;

#define DO_LDAH   state.r[REG_1] = state.r[REG_2] + (DISP_16 << 16);

#define DO_LDBU   if ((REG_1 & 0x1f) != 31) { READ_VIRT_F(state.r[REG_2] + DISP_16, 8, state.r[REG_1], zext_u64_8); }

#define DO_LDL    if ((REG_1 & 0x1f) != 31) { READ_VIRT_F(state.r[REG_2] + DISP_16, 32, state.r[REG_1], sext_u64_32); }

/* LDx_L: always perform the load and establish the lock, regardless of Ra.
   The state.r[31] auto-clear after each instruction makes writing to R31
   harmless, but the cpu_lock side-effect must occur for correct LL/SC pairing.
*/
#define DO_LDL_L  READ_VIRT_LOCK_F(state.r[REG_2] + DISP_16, 32, state.r[REG_1], sext_u64_32);

#define DO_LDQ    if ((REG_1 & 0x1f) != 31) { READ_VIRT(state.r[REG_2] + DISP_16, 64, state.r[REG_1]); }

#define DO_LDQ_L  READ_VIRT_LOCK(state.r[REG_2] + DISP_16, 64, state.r[REG_1]);

// EV4 translates the effective VA; LDQ_U/STQ_U transfer the containing quadword (21064 HRM §§4.7.1.2, 5.3.5).
#define DATA_PHYS_QUAD_U(flags) {                                                  \
  const u64 va = state.r[REG_2] + DISP_16;                                        \
  DATA_PHYS_NT(m_profile.data_fault.unmasked_quad_va ? va : (va & ~U64(7)), flags); \
  phys_address = ALIGN_PHYS(8);                                                   \
}

#define DO_LDQ_U  if ((REG_1 & 0x1f) != 31) { DATA_PHYS_QUAD_U(ACCESS_READ); state.r[REG_1] = READ_PHYS(64); }

#define DO_LDWU   if ((REG_1 & 0x1f) != 31) { READ_VIRT_F(state.r[REG_2] + DISP_16, 16, state.r[REG_1], zext_u64_16); }

#define DO_STB    WRITE_VIRT(state.r[REG_2] + DISP_16, 8, state.r[REG_1]);

#define DO_STL    WRITE_VIRT(state.r[REG_2] + DISP_16, 32, state.r[REG_1]);

#define DO_STL_C  WRITE_VIRT_COND(state.r[REG_2] + DISP_16, 32, state.r[REG_1], state.r[REG_1]);

#define DO_STQ    WRITE_VIRT(state.r[REG_2] + DISP_16, 64, state.r[REG_1]);

#define DO_STQ_C  WRITE_VIRT_COND(state.r[REG_2] + DISP_16, 64, state.r[REG_1], state.r[REG_1]);

#define DO_STQ_U  { DATA_PHYS_QUAD_U(ACCESS_WRITE); WRITE_PHYS(state.r[REG_1], 64); }

#define DO_STW    WRITE_VIRT(state.r[REG_2] + DISP_16, 16, state.r[REG_1]);
