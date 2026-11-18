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
  * Contains code macros for the processor PALmode instructions.
  * Based on HRM.
  *
  * $Id$
  *
  * X-1.15       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.14       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.13       Camiel Vanderhoeven                             30-JAN-2008
  *      Always use set_pc or add_pc to change the program counter.
  *
  * X-1.12       Camiel Vanderhoeven                             30-JAN-2008
  *      Remember number of instructions left in current memory page, so
  *      that the translation-buffer doens't need to be consulted on every
  *      instruction fetch when the Icache is disabled.
  *
  * X-1.11       Camiel Vanderhoeven                             25-JAN-2008
  *      Trap on unalogned memory access. The previous implementation where
  *      unaligned accesses were silently allowed could go wrong when page
  *      boundaries are crossed.
  *
  * X-1.10       Camiel Vanderhoeven                             18-JAN-2008
  *      Replaced sext_64 inlines with sext_u64_<bits> inlines for
  *      performance reasons (thanks to David Hittner for spotting this!);
  *
  * X-1.9        Camiel Vanderhoeven                             2-DEC-2007
  *      Changed the way translation buffers work, the way interrupts work.
  *
  * X-1.8        Camiel Vanderhoeven                             12-NOV-2007
  *      Fix Software Interrupt behaviour.
  *
  * X-1.7        Camiel Vanderhoeven                             30-OCT-2007
  *      IPR 0x2d identified as M_FIX (Mbox fixed behaviour)
  *
  * X-1.6        Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.5        Camiel Vanderhoeven                             30-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.4        Camiel Vanderhoeven                             8-MAR-2007
  *      Different argument list for CTranslationBuffer methods, and for
  *      va_form function.
  *
  * X-1.3        Camiel Vanderhoeven                             3-MAR-2007
  *      Put 0x2d in HW_MTPR to get rid of error messages about this unknown
  *      IPR. WE STILL NEED TO FIND OUT WHAT THIS REGISTER DOES.
  *
  * X-1.2        Camiel Vanderhoeven                             22-FEB-2007
  *      Got rid of calls to get_asn.
  *
  * X-1.1        Camiel Vanderhoeven                             18-FEB-2007
  *      File created. Contains code previously found in AlphaCPU.h
  **/

#define DO_HW_MFPR do {                                                       \
  const auto describe = m_profile.ipr_read.describe;                          \
  if (!describe || describe(ins).kind != AlphaIPRReadKind::Noop)              \
    state.r[REG_1] = read_ipr(ins);                                           \
} while (0)

#define DO_HW_MTPR write_ipr(ins, state.r[REG_2], AlphaIPRWriteContext::Interpreter);

// Family return rules feed the shared executor after the hardware-access check.
#define DO_HW_RET do {                                                         \
  if (!alpha_pal_return_valid(hw_return)) { UNKNOWN1; }                        \
  const u64 target = alpha_pal_return_target(                                 \
    hw_return, state.r[REG_2], state.exc_addr);                                \
  set_pc(target);                                                              \
  if (hw_return.clear_lock)                                                    \
    cSystem->cpu_clear_lock(state.iProcNum);                                   \
} while (0)

// Family decoding feeds one shared load executor. The caller handles faults
// and unknown forms in the interpreter instruction-exit context.
#define DO_HW_LD do {                                                          \
  if (!alpha_hw_load_valid(hw_load)) { UNKNOWN2; }                             \
  const u64 hw_address = state.r[REG_2] + (u64)hw_load.displacement;           \
  if (hw_load.address == AlphaHWLoadAddress::Virtual) {                        \
    const int hw_flags = ACCESS_READ | (hw_load.vpte ? VPTE : 0)               \
      | (hw_load.alternate ? ALT : 0) | (hw_load.write_check ? WRCHK : 0);     \
    if (hw_flags == ACCESS_READ) {                                             \
      DATA_PHYS_NT(hw_address, ACCESS_READ);                                   \
    } else if (virt2phys(hw_address, &phys_address, hw_flags, nullptr, ins)) { \
      ES40_EXECUTE_END();                                                      \
    }                                                                          \
  } else {                                                                     \
    phys_address = hw_address;                                                 \
  }                                                                            \
  /* Translate before alignment; reserve the address actually read. */         \
  if (hw_load.locked) {                                                        \
    state.r[REG_1] = cSystem->cpu_read_locked(state.iProcNum,                  \
      ALIGN_PHYS(hw_load.size_bits / 8), hw_load.size_bits, this);            \
    LLR;                                                                       \
  } else {                                                                     \
    state.r[REG_1] = READ_PHYS_NT(hw_load.size_bits);                          \
  }                                                                            \
  if (hw_load.size_bits == 32)                                                 \
    state.r[REG_1] = sext_u64_32(state.r[REG_1]);                              \
} while (0)

#define DO_HW_LDL DO_HW_LD
#define DO_HW_LDQ DO_HW_LD

// Family decoding feeds the shared store executor. Faults exit in the caller.
#define DO_HW_ST do {                                                          \
  if (!alpha_hw_store_valid(hw_store)) { UNKNOWN2; }                           \
  const u64 hw_address = state.r[REG_2] + (u64)hw_store.displacement;          \
  if (hw_store.address == AlphaHWStoreAddress::Virtual) {                      \
    if (hw_store.alternate) {                                                  \
      DATA_PHYS_NT(hw_address, ACCESS_WRITE | ALT);                            \
    } else {                                                                   \
      DATA_PHYS_NT(hw_address, ACCESS_WRITE);                                  \
    }                                                                          \
  } else {                                                                     \
    phys_address = hw_address;                                                 \
  }                                                                            \
  /* Evaluate the source after translation, before any conditional result. */  \
  if (hw_store.conditional) {                                                  \
    state.r[REG_1] = cSystem->cpu_stx_c(state.iProcNum,                       \
      ALIGN_PHYS(hw_store.size_bits / 8),                                    \
      hw_store.size_bits, state.r[REG_1], dram_ptr, dram_size, this);          \
  } else {                                                                     \
    WRITE_PHYS_NT(state.r[REG_1], hw_store.size_bits);                         \
  }                                                                            \
} while (0)

#define DO_HW_STL DO_HW_ST
#define DO_HW_STQ DO_HW_ST
