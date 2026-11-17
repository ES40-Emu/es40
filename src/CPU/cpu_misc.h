/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2025-2026 by gdwnldsKSC
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

 /**
  * \file
  * Contains code macros for miscellaneous processor instructions.
  * Based on ARM chapter 4.11.
  *
  * $Id$
  *
  * X-1.12       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.11       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.10       Camiel Vanderhoeven                             30-JAN-2008
  *      Always use set_pc or add_pc to change the program counter.
  *  
  * X-1.9        Camiel Vanderhoeven                             30-JAN-2008
  *      Remember number of instructions left in current memory page, so
  *      that the translation-buffer doens't need to be consulted on every
  *      instruction fetch when the Icache is disabled.
  *
  * X-1.8        Camiel Vanderhoeven                             2-DEC-2007
  *      Use vmspal_call functions.
  *
  * X-1.7        Camiel Vanderhoeven                             16-NOV-2007
  *      Avoid more compiler warnings.
  *
  * X-1.6        Camiel Vanderhoeven                             14-NOV-2007
  *      Bug-fix in CALL_PAL RSCC.
  *
  * X-1.5        Camiel Vanderhoeven                             14-NOV-2007
  *      Implemented most simple PALcode routines in C++.
  *
  * X-1.4        Camiel Vanderhoeven                             12-NOV-2007
  *      Made a start with implementing PALcode routines in C++.
  *
  * X-1.3        Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.2        Camiel Vanderhoeven                             30-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.1        Camiel Vanderhoeven                             18-FEB-2007
  *      File created. Contains code previously found in AlphaCPU.h
  **/
#include <atomic>

#define DO_AMASK                                  \
  do {                                            \
    /* RA must be R31 per AHB / QEMU */           \
    if (REG_1 != 31) {                            \
      GO_PAL(AlphaPALException::OpcodeDecode);                                \
    } else {                                      \
      state.r[REG_3] = V_2 & ~get_profile().amask; \
    }                                             \
  } while (0)

#define ENTER_NATIVE_CALL_PAL()                                       \
  do {                                                                \
    enter_native_call_pal(function);                                  \
    TRC(true, false)                                                  \
  } while (0)


#define DO_CALL_PAL if(!m_profile.pal.call_pal_valid(function, pal_access_context().mode)) \
  {                                                                    \
    UNKNOWN2                                                           \
  }                                                                    \
  else                                                                 \
  {                                                                    \
    if (function == 0x00 && exit_on_pal_halt)                          \
      {FAILURE(Graceful, "HALT invoked, exit_on_pal_halt configured");}\
    else if (handle_call_pal_override(function)) {}                  \
    else if (handle_call_pal_compatibility(function)) {}             \
    else                                                               \
    {                                                                  \
      /* all other native PAL calls */                                 \
      ENTER_NATIVE_CALL_PAL();                                         \
    }                                                                  \
  }

#define DO_IMPLVER  state.r[REG_3] = get_profile().implver;

// state.cc is wall-clock in both engines; each RPCC read syncs it to now so the
// guest never samples a stale batch-start value, with forward progress on host-clock collisions
// (see rpcc_read). _cc_accum only feeds cc_large for the legacy check_state
// speed-factor feedback.
#define DO_RPCC     { state.r[REG_1] = rpcc_read(); }

  // The following ops have no function right now (at least, not until multiple CPU's are supported).
  // well, let's set up to have that happen soon.... 
  // Alpha ordering approximations (conservative):
  // TRAPB: serialize exceptions + memory; use full fence.
#define DO_TRAPB    std::atomic_thread_fence(std::memory_order_seq_cst);
// EXCB/MB: full fence (data+io ordering).
#define DO_EXCB     std::atomic_thread_fence(std::memory_order_seq_cst);
#define DO_MB       std::atomic_thread_fence(std::memory_order_seq_cst);
// WMB: HRM 4.11.1 requires WMB to order all prior stores from this CPU
// before all subsequent stores, as observed by every other CPU/DMA agent.
// A standalone atomic_thread_fence(release) only synchronizes with a matching
// acquire fence/load on another thread; it does NOT impose store-store
// ordering visible to unsynchronized observers. 
#define DO_WMB      std::atomic_thread_fence(std::memory_order_seq_cst);
// FETCH/FETCH_M/ECB/WH64/WH64EN are hints — safe as no-ops.
#define DO_FETCH    ;    /* hint: no effect */
#define DO_FETCH_M  ;    /* hint: no effect */
#define DO_ECB      ;    /* cache hint: no effect */
#define DO_WH64     ;    /* write hint: no effect */
#define DO_WH64EN   ;    /* write hint enable: no effect */

// IMB (Instruction Memory Barrier): ensure instruction fetch observes prior stores.
// Architecturally this invalidates any stale I-cache state visible to this CPU.
#define DO_IMB      do { flush_icache(); tbia(ACCESS_EXEC); } while (0)
