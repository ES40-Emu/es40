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
    else if(state.pal_vms)                                             \
    {                                                                  \
      switch(function)                                                 \
      {                                                                \
      case 0x01:  /* CFLUSH */                                         \
        vmspal_call_cflush();                                          \
        break;                                                         \
                                                                \
      case 0x02:  /* DRAINA */                                         \
        vmspal_call_draina();                                          \
        break;                                                         \
                                                                \
      case 0x03:  /* LDQP */                                           \
        vmspal_call_ldqp();                                            \
        break;                                                         \
                                                                \
      case 0x04:  /* STQP */                                           \
        vmspal_call_stqp();                                            \
        break;                                                         \
                                                                \
      case 0x05:  /* SWPCTX */                                         \
        vmspal_call_swpctx();                                          \
        break;                                                         \
                                                                \
      case 0x06:  /* MFPR_ASN */                                       \
        vmspal_call_mfpr_asn();                                        \
        break;                                                         \
                                                                \
      case 0x07:  /* MTPR_ASTEN */                                     \
        vmspal_call_mtpr_asten();                                      \
        break;                                                         \
                                                                \
      case 0x08:  /* MTPR_ASTSR */                                     \
        vmspal_call_mtpr_astsr();                                      \
        break;                                                         \
                                                                \
      case 0x09:  /* CSERVE */                                         \
        ENTER_NATIVE_CALL_PAL();                                       \
        break;                                                         \
                                                                \
      case 0x0b:  /* MFPR_FEN */                                       \
        vmspal_call_mfpr_fen();                                        \
        break;                                                         \
                                                                \
      case 0x0c:  /* MTPR_FEN */                                       \
        vmspal_call_mtpr_fen();                                        \
        break;                                                         \
                                                                \
      case 0x0e:  /* MFPR_IPL */                                       \
        vmspal_call_mfpr_ipl();                                        \
        break;                                                         \
                                                                \
      case 0x0f:  /* MTPR_IPL */                                       \
        vmspal_call_mtpr_ipl();                                        \
        break;                                                         \
                                                                \
      case 0x10:  /* MFPR_MCES */                                      \
        vmspal_call_mfpr_mces();                                       \
        break;                                                         \
                                                                \
      case 0x11:  /* MTPR_MCES */                                      \
        vmspal_call_mtpr_mces();                                       \
        break;                                                         \
                                                                \
      case 0x12:  /* MFPR_PCBB */                                      \
        vmspal_call_mfpr_pcbb();                                       \
        break;                                                         \
                                                                \
      case 0x13:  /* MFPR_PRBR */                                      \
        vmspal_call_mfpr_prbr();                                       \
        break;                                                         \
                                                                \
      case 0x14:  /* MTPR_PRBR */                                      \
        vmspal_call_mtpr_prbr();                                       \
        break;                                                         \
                                                                \
      case 0x15:  /* MFPR_PTBR */                                      \
        vmspal_call_mfpr_ptbr();                                       \
        break;                                                         \
                                                                \
      case 0x16:  /* MFPR_SCBB */                                      \
        vmspal_call_mfpr_scbb();                                       \
        break;                                                         \
                                                                \
      case 0x17:  /* MTPR_SCBB */                                      \
        vmspal_call_mtpr_scbb();                                       \
        break;                                                         \
                                                                \
      case 0x18:  /* MTPR_SIRR */                                      \
        vmspal_call_mtpr_sirr();                                       \
        break;                                                         \
                                                                \
      case 0x19:  /* MFPR_SISR */                                      \
        vmspal_call_mfpr_sisr();                                       \
        break;                                                         \
                                                                \
      case 0x1a:  /* MFPR_TBCHK */                                     \
        vmspal_call_mfpr_tbchk();                                      \
        break;                                                         \
                                                                \
      case 0x1b:  /* MTPR_TBIA */                                      \
        vmspal_call_mtpr_tbia();                                       \
        break;                                                         \
                                                                \
      case 0x1c:  /* MTPR_TBIAP */                                     \
        vmspal_call_mtpr_tbiap();                                      \
        break;                                                         \
                                                                \
      case 0x1d:  /* MTPR_TBIS */                                      \
        vmspal_call_mtpr_tbis();                                       \
        break;                                                         \
                                                                \
      case 0x1e:  /* MFPR_ESP */                                       \
        vmspal_call_mfpr_esp();                                        \
        break;                                                         \
                                                                \
      case 0x1f:  /* MTPR_ESP */                                       \
        vmspal_call_mtpr_esp();                                        \
        break;                                                         \
                                                                \
      case 0x20:  /* MFPR_SSP */                                       \
        vmspal_call_mfpr_ssp();                                        \
        break;                                                         \
                                                                \
      case 0x21:  /* MTPR_SSP */                                       \
        vmspal_call_mtpr_ssp();                                        \
        break;                                                         \
                                                                \
      case 0x22:  /* MFPR_USP */                                       \
        vmspal_call_mfpr_usp();                                        \
        break;                                                         \
                                                                \
      case 0x23:  /* MTPR_USP */                                       \
        vmspal_call_mtpr_usp();                                        \
        break;                                                         \
                                                                \
      case 0x24:  /* MTPR_TBISD */                                     \
        vmspal_call_mtpr_tbisd();                                      \
        break;                                                         \
                                                                \
      case 0x25:  /* MTPR_TBISI */                                     \
        vmspal_call_mtpr_tbisi();                                      \
        break;                                                         \
                                                                \
      case 0x26:  /* MFPR_ASTEN */                                     \
        vmspal_call_mfpr_asten();                                      \
        break;                                                         \
                                                                \
      case 0x27:  /* MFPR_ASTSR */                                     \
        vmspal_call_mfpr_astsr();                                      \
        break;                                                         \
                                                                \
      case 0x29:  /* MFPR_VPTB */                                      \
        vmspal_call_mfpr_vptb();                                       \
        break;                                                         \
                                                                \
      case 0x2e:  /* MTPR_DATFX */                                     \
        vmspal_call_mtpr_datfx();                                      \
        break;                                                         \
                                                                \
      case 0x3f:  /* MFPR_WHAMI */                                     \
        vmspal_call_mfpr_whami();                                      \
        break;                                                         \
                                                                \
      case 0x86:  /* IMB */                                            \
        vmspal_call_imb();                                             \
        break;                                                         \
                                                                \
      case 0x8f:  /* PROBER */                                         \
        vmspal_call_prober();                                          \
        break;                                                         \
                                                                \
      case 0x90:  /* PROBEW */                                         \
        vmspal_call_probew();                                          \
        break;                                                         \
                                                                \
      case 0x91:  /* RD_PS */                                          \
        vmspal_call_rd_ps();                                           \
        break;                                                         \
                                                                \
      case 0x92:  /* REI */                                            \
        vmspal_call_rei();                                             \
        break;                                                         \
                                                                \
      case 0x9b:  /* SWASTEN */                                        \
        vmspal_call_swasten();                                         \
        break;                                                         \
                                                                \
      case 0x9c:  /* WR_PS_SW */                                       \
        vmspal_call_wr_ps_sw();                                        \
        break;                                                         \
                                                                \
      case 0x9d:  /* RSCC */                                           \
        vmspal_call_rscc();                                            \
        break;                                                         \
                                                                \
      case 0x9e:  /* READ_UNQ */                                       \
        vmspal_call_read_unq();                                        \
        break;                                                         \
                                                                \
      case 0x9f:  /* WRITE_UNQ */                                      \
        vmspal_call_write_unq();                                       \
        break;                                                         \
                                                                \
      default:                                                         \
        ENTER_NATIVE_CALL_PAL();                                       \
      }                                                                \
    }                                                                  \
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
