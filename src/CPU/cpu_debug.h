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
  * Contains debugging macros used by AlphaCPU.cpp
  *
  * $Id$
  *
  * X-1.29       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.28       Camiel Vanderhoeven                             19-MAR-2008
  *      IDB versions compileable again.
  *
  * X-1.27       Camiel Vanderhoeven                             13-MAR-2008
  *      Formatting.
  *
  * X-1.26       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.25       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.24       Camiel Vanderhoeven                             29-FEB-2008
  *      Comments.
  *
  * X-1.23       Brian Wheeler                                   29-FEB-2008
  *      Fix IDB compilation.
  *
  * X-1.22       Camiel Vanderhoeven                             30-JAN-2008
  *      Always use set_pc or add_pc to change the program counter.
  *
  * X-1.21       Camiel Vanderhoeven                             30-JAN-2008
  *      Remember number of instructions left in current memory page, so
  *      that the translation-buffer doens't need to be consulted on every
  *      instruction fetch when the Icache is disabled.
  *
  * X-1.20       Camiel Vanderhoeven                             28-JAN-2008
  *      Better floating-point exception handling.
  *
  * X-1.19       Camiel Vanderhoeven                             27-JAN-2008
  *      Have GO_PAL throw an exception, so we don't continue doing what we
  *      were doing before the exception was taken.
  *
  * X-1.18       Camiel Vanderhoeven                             21-JAN-2008
  *      Moved some macro's to cpu_defs.h.
  *
  * X-1.17       Camiel Vanderhoeven                             16-JAN-2008
  *      Added ARITH_TRAP macro.
  *
  * X-1.16       Camiel Vanderhoeven                             2-DEC-2007
  *      Moved PAL_NAME and IPR_NAME to AlphaCPU.cpp.
  *
  * X-1.15       Camiel Vanderhoeven                             10-NOV-2007
  *      f12_f3 operations show fxx as registers instead of rxx.
  *
  * X-1.14       Camiel Vanderhoeven                             30-OCT-2007
  *      IPR 0x2d identified as M_FIX (Mbox fixed behaviour)
  *
  * X-1.13       Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.12       Camiel Vanderhoeven                             30-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.11       Camiel Vanderhoeven                             14-MAR-2007
  *      Small bugfix in DEBUG_XX macro.
  *
  * X-1.10       Camiel Vanderhoeven                             14-MAR-2007
  *      Small bugfix in FP debugging macro's.
  *
  * X-1.9        Camiel Vanderhoeven                             13-MAR-2007
  *      Added FP debugging macro's.
  *
  * X-1.8        Camiel Vanderhoeven                             12-MAR-2007
  *   a) When an exception occurs, debugging information for the previous
  *      instruction is no longer lost.
  *   b) Made various printf-statements 64-bit safe in a neater way. (Use
  *      printf("%" LL "x",value) instead of printf("%x",(u64)value)).
  *
  * X-1.7        Camiel Vanderhoeven                             8-MAR-2007
  *      Pass "this" as an argument to CTraceEngine::get_fnc_name.
  *
  * X-1.6        Camiel Vanderhoeven                             28-FEB-2007
  *   a) Removed MINI_DEBUG_XX
  *   b) Debugging now writes to the char* debug_string, rather than using
  *      printf directly. This allows us to do other things with debugger
  *      output.
  *
  * X-1.5        Camiel Vanderhoeven                             22-FEB-2007
  *   a) Replaced IPR macro's with separate MTPR and MFPR macro's (because
  *      these instructions use different registers).
  *   b) Made a couple of printf's multi-platform-safe.
  *
  * X-1.4        Camiel Vanderhoeven                             18-FEB-2007
  *      Introduced POST_xxx and PRE_xxx macro's. When debugging, these are
  *      included from the OP macro in AlphaCPU.cpp.
  *
  * X-1.3        Camiel Vanderhoeven                             16-FEB-2007
  *   a) Macro's that end CAlphaCPU::DoClock() now return 0.
  *   b) Made a couple of printf's multi-platform-safe.
  *
  * X-1.2        Brian Wheeler                                   13-FEB-2007
  *      Added mini-version of DEBUG_XX, that dumps all registers in a
  *      concise format.
  *
  * X-1.1        Camiel Vanderhoeven                             12-FEB-2007
  *      File created. Contains code previously found in AlphaCPU.h
  **/

#ifdef ES40_JIT
#define ES40_EXECUTE_END() return
#else
#define ES40_EXECUTE_END() goto _next_instruction
#endif

#if defined(DEBUG_INSTALL_VMFAULT)
#define DEBUG_INSTALL_PAL_TRAP(offset) debug_trace_user_pal_trap(offset)
#else
#define DEBUG_INSTALL_PAL_TRAP(offset) ((void) 0)
#endif

#if defined(IDB)
extern const char* PAL_NAME[];

extern char   dbg_string[1000];
#if !defined(LS_MASTER) && !defined(LS_SLAVE)
extern char* dbg_strptr;
#endif
void          handle_debug_string(char* s);

#define TRC_(down, up, x, y) \
  if(bTrace)                 \
    trc->trace(this, state.current_pc, state.pc, down, up, x, y);

#define TRC(down, up) \
  if(bTrace)          \
    trc->trace(this, state.current_pc, state.pc, down, up, (char*) 0, 0);

#define TRC_BR  if(bTrace) \
    trc->trace_br(this, state.current_pc, state.pc);

#define GO_PAL(reason)                                                        \
  {                                                                           \
    const auto pal_entry = pal_exception_entry((reason), state.current_pc);    \
    const u64 pal_offset = pal_entry.offset;                                  \
    if(bDisassemble)                                                         \
    {                                                                         \
      sprintf(dbg_strptr, " ==> PAL %" PRIx64 "!\n", pal_offset);            \
      dbg_strptr += strlen(dbg_strptr);                                      \
    }                                                                         \
    handle_debug_string(dbg_string);                                         \
    DEBUG_INSTALL_PAL_TRAP(pal_offset);                                      \
    enter_pal_exception(pal_entry);                                          \
    if constexpr ((reason) == AlphaPALException::DataTlbMiss ||               \
                  (reason) == AlphaPALException::InstructionTlbMiss) {       \
      if(bTrace)                                                             \
        trc->set_waitfor(this, state.exc_addr & ~U64(0x3));                   \
      else                                                                    \
        TRC_(true, false, "GO_PAL %" PRIx64, pal_offset);                     \
    } else                                                                    \
      TRC_(true, false, "GO_PAL %" PRIx64, pal_offset);                       \
  }

#else
#define TRC_(down, up, x, y)  ;
#define TRC(down, up)         ;
#define TRC_BR                ;

#define GO_PAL(reason)                                                        \
  {                                                                           \
    const auto pal_entry = pal_exception_entry((reason), state.current_pc);    \
    DEBUG_INSTALL_PAL_TRAP(pal_entry.offset);                                 \
    enter_pal_exception(pal_entry);                                          \
  }
#endif
#if defined(IDB)
#define DEBUG_XX  if(trc->get_fnc_name(this, state.current_pc &~U64(0x3), &funcname)) \
  {                                                                                   \
    if(bListing && !strcmp(funcname, ""))                                             \
    {                                                                                 \
      printf("%08" PRIx64 ": \"%s\"\n", state.current_pc,                                 \
             cSystem->PtrToMem(state.current_pc));                                    \
      state.pc =                                                                      \
        (                                                                             \
          state.current_pc +                                                          \
          strlen(cSystem->PtrToMem(state.current_pc)) +                               \
          4                                                                           \
        ) &~U64(0x3);                                                                 \
      while(state.pc < 0x600000 && cSystem->ReadMem(state.pc, 32, this) == 0)         \
        state.pc += 4;                                                                \
      return;                                                                       \
    }                                                                                 \
    else if(bListing && !strcmp(funcname, "!SKIP"))                                   \
    {                                                                                 \
      while(state.pc < 0x600000 && cSystem->ReadMem(state.pc, 32, this) == 0)         \
        state.pc += 4;                                                                \
      return;                                                                       \
    }                                                                                 \
    else if(bListing && !strncmp(funcname, "!CHAR-", 6))                              \
    {                                                                                 \
      u64 xx_upto;                                                                    \
      int xx_result;                                                                  \
      xx_result = sscanf(&(funcname[6]), "%" SCNx64, &xx_upto);                       \
      if(xx_result == 1)                                                              \
      {                                                                               \
        state.pc = state.current_pc;                                                  \
        while(state.pc < xx_upto)                                                     \
        {                                                                             \
          printf("%08" PRIx64 ": \"%s\"\n", state.pc, cSystem->PtrToMem(state.pc));       \
          state.pc += strlen(cSystem->PtrToMem(state.pc));                            \
          while(state.pc < xx_upto && cSystem->ReadMem(state.pc, 8, this) == 0)       \
            state.pc++;                                                               \
        }                                                                             \
        return;                                                                     \
      }                                                                               \
    }                                                                                 \
    else if(bListing && !strncmp(funcname, "!LCHAR-", 7))                             \
    {                                                                                 \
      char  stringval[300];                                                           \
      int   stringlen;                                                                \
      u64   xx_upto;                                                                  \
      int   xx_result;                                                                \
      xx_result = sscanf(&(funcname[7]), "%" SCNx64, &xx_upto);                       \
      if(xx_result == 1)                                                              \
      {                                                                               \
        state.pc = state.current_pc;                                                  \
        while(state.pc < xx_upto)                                                     \
        {                                                                             \
          stringlen = (int) cSystem->ReadMem(state.pc++, 8, this);                    \
          memset(stringval, 0, 300);                                                  \
          strncpy(stringval, cSystem->PtrToMem(state.pc), stringlen);                 \
          printf("%08" PRIx64 ": \"%s\"\n", state.pc - 1, stringval);                     \
          state.pc += stringlen;                                                      \
          while(state.pc < xx_upto && cSystem->ReadMem(state.pc, 8, this) == 0)       \
            state.pc++;                                                               \
        }                                                                             \
        return;                                                                     \
      }                                                                               \
    }                                                                                 \
    else if(bListing && !strncmp(funcname, "!X64-", 5))                               \
    {                                                                                 \
      printf("\n%s:\n", &(funcname[5]));                                              \
      state.pc = state.current_pc;                                                    \
      while                                                                           \
      (                                                                               \
        (state.pc == state.current_pc)                                                \
      || !trc->get_fnc_name(this, state.pc, &funcname)                                \
      )                                                                               \
      {                                                                               \
        printf("%08" PRIx64 ": %016" PRIx64 "\n", state.pc,                                   \
               cSystem->ReadMem(state.pc, 64, this));                                 \
        state.pc += 8;                                                                \
      }                                                                               \
      return;                                                                       \
    }                                                                                 \
    else if(bListing && !strncmp(funcname, "!X32-", 5))                               \
    {                                                                                 \
      printf("\n%s:\n", &(funcname[5]));                                              \
      state.pc = state.current_pc;                                                    \
      while                                                                           \
      (                                                                               \
        (state.pc == state.current_pc)                                                \
      || !trc->get_fnc_name(this, state.pc, &funcname)                                \
      )                                                                               \
      {                                                                               \
        printf("%08" PRIx64 ": %08" PRIx64 "\n", state.pc,                                    \
               cSystem->ReadMem(state.pc, 32, this));                                 \
        state.pc += 4;                                                                \
      }                                                                               \
      return;                                                                       \
    }                                                                                 \
    else if(!strncmp(funcname, ":", 1))                                               \
    {                                                                                 \
      sprintf(dbg_strptr, "%s:\n", funcname);                                         \
      dbg_strptr += strlen(dbg_strptr);                                               \
    }                                                                                 \
    else                                                                              \
    {                                                                                 \
      sprintf(dbg_strptr, "\n%s:\n", funcname);                                       \
      dbg_strptr += strlen(dbg_strptr);                                               \
    }                                                                                 \
  }                                                                                   \
  sprintf(dbg_strptr, bListing ? "%08" PRIx64 ": " : "%016" PRIx64, state.current_pc);     \
  dbg_strptr += strlen(dbg_strptr);                                                   \
  if(!bListing)                                                                       \
    sprintf(dbg_strptr, "(%08" PRIx64 "): ", current_pc_physical);                        \
  else                                                                                \
  {                                                                                   \
    sprintf(dbg_strptr, "%08x %c%c%c%c: ", ins, printable((char) (ins)),              \
            printable((char) (ins >> 8)), printable((char) (ins >> 16)),              \
            printable((char) (ins >> 24)));                                           \
  }                                                                                   \
  dbg_strptr += strlen(dbg_strptr);

/* IDB-disassembly variants -- log then raise OPCDEC, matching the non-IDB
 * macros below. HRM 6.8.2 Table 6-8 mandates OPCDEC for unallocated
 * encodings; brokenpipe routes them through the same path. */
#define UNKNOWN1  if(bDisassemble)                        \
  {                                                       \
    DEBUG_XX                                              \
  }                                                       \
  sprintf(dbg_strptr, "Unknown opcode: %02x   ", opcode); \
  dbg_strptr += strlen(dbg_strptr);                       \
  handle_debug_string(dbg_string);                        \
  GO_PAL(AlphaPALException::OpcodeDecode);                                    \
  ES40_EXECUTE_END();

#define UNKNOWN2  if(bDisassemble)                                       \
  {                                                                      \
    DEBUG_XX                                                             \
  }                                                                      \
  sprintf(dbg_strptr, "Unknown opcode: %02x.%02x   ", opcode, function); \
  dbg_strptr += strlen(dbg_strptr);                                      \
  handle_debug_string(dbg_string);                                       \
  GO_PAL(AlphaPALException::OpcodeDecode);                                    \
  ES40_EXECUTE_END();

#define POST_X64(a)                           \
  if(bDisassemble)                            \
  {                                           \
    if(!bListing)                             \
    {                                         \
      sprintf(dbg_strptr, " ==> %" PRIx64, a); \
      dbg_strptr += strlen(dbg_strptr);       \
    }                                         \
  }

#define PRE_PAL(mnemonic)                                       \
  if(bDisassemble)                                              \
  {                                                             \
    DEBUG_XX;                                                   \
    if(function < 0x40 || (function > 0x7f && function < 0xc0)) \
      sprintf(dbg_strptr, #mnemonic " %s", PAL_NAME[function]); \
    else                                                        \
      sprintf(dbg_strptr, #mnemonic " ?%x?", function);         \
    dbg_strptr += strlen(dbg_strptr);                           \
  }

#define POST_PAL  TRC(1, 0);

#define PRE_BR(mnemonic)                                                 \
  if(bDisassemble)                                                       \
  {                                                                      \
    u64       dbg_x = (state.current_pc + 4 + (DISP_21 * 4)) &~U64(0x3); \
    DEBUG_XX  sprintf(dbg_strptr, #mnemonic " r%d, ", REG_1 & 31);       \
    dbg_strptr += strlen(dbg_strptr);                                    \
    if(trc->get_fnc_name(this, dbg_x, &funcname))                        \
      sprintf(dbg_strptr, "%s", funcname);                               \
    else                                                                 \
      sprintf(dbg_strptr, "%" PRIx64, dbg_x);                             \
    dbg_strptr += strlen(dbg_strptr);                                    \
  }

#define POST_BR TRC_BR;

#define PRE_COND(mnemonic)                                               \
  if(bDisassemble)                                                       \
  {                                                                      \
    u64       dbg_x = (state.current_pc + 4 + (DISP_21 * 4)) &~U64(0x3); \
    DEBUG_XX  sprintf(dbg_strptr, #mnemonic " r%d, ", REG_1 & 31);       \
    dbg_strptr += strlen(dbg_strptr);                                    \
    if(trc->get_fnc_name(this, dbg_x, &funcname))                        \
      sprintf(dbg_strptr, "%s", funcname);                               \
    else                                                                 \
      sprintf(dbg_strptr, "%" PRIx64, dbg_x);                             \
    dbg_strptr += strlen(dbg_strptr);                                    \
    if(!bListing)                                                        \
    {                                                                    \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", state.r[REG_1]);            \
      dbg_strptr += strlen(dbg_strptr);                                  \
    }                                                                    \
  }

#define POST_COND TRC_BR;

#define PRE_FCOND(mnemonic)                                              \
  if(bDisassemble)                                                       \
  {                                                                      \
    u64       dbg_x = (state.current_pc + 4 + (DISP_21 * 4)) &~U64(0x3); \
    DEBUG_XX  sprintf(dbg_strptr, #mnemonic " f%d, ", FREG_1);           \
    dbg_strptr += strlen(dbg_strptr);                                    \
    if(trc->get_fnc_name(this, dbg_x, &funcname))                        \
      sprintf(dbg_strptr, "%s", funcname);                               \
    else                                                                 \
      sprintf(dbg_strptr, "%" PRIx64, dbg_x);                            \
    dbg_strptr += strlen(dbg_strptr);                                    \
    if(!bListing)                                                        \
    {                                                                    \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", state.f[FREG_1]);               \
      dbg_strptr += strlen(dbg_strptr);                                  \
    }                                                                    \
  }

#define POST_FCOND  TRC_BR;

#define PRE_BSR(mnemonic)                                                \
  if(bDisassemble)                                                       \
  {                                                                      \
    u64       dbg_x = (state.current_pc + 4 + (DISP_21 * 4)) &~U64(0x3); \
    DEBUG_XX  sprintf(dbg_strptr, #mnemonic " r%d, ", REG_1 & 31);       \
    dbg_strptr += strlen(dbg_strptr);                                    \
    if(trc->get_fnc_name(this, dbg_x, &funcname))                        \
      sprintf(dbg_strptr, "%s", funcname);                               \
    else                                                                 \
      sprintf(dbg_strptr, "%" PRIx64, dbg_x);                            \
    dbg_strptr += strlen(dbg_strptr);                                    \
  }

#define POST_BSR  if(REG_1 == 31) \
  {                               \
    TRC(0, 1);                    \
  }                               \
  else                            \
  {                               \
    TRC(1, 1);                    \
  }

#define PRE_JMP(mnemonic)                                                         \
  if(bDisassemble)                                                                \
  {                                                                               \
    DEBUG_XX  sprintf(dbg_strptr, #mnemonic " r%d, r%d", REG_1 & 31, REG_2 & 31); \
    dbg_strptr += strlen(dbg_strptr);                                             \
    if(!bListing)                                                                 \
    {                                                                             \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", state.r[REG_2]);                         \
      dbg_strptr += strlen(dbg_strptr);                                           \
    }                                                                             \
  }

#define POST_JMP  if(REG_1 == 31) \
  {                               \
    TRC(0, 1);                    \
  }                               \
  else                            \
  {                               \
    TRC(1, 1);                    \
  }

#define PRE_RET(mnemonic)                                        \
  if(bDisassemble)                                               \
  {                                                              \
    DEBUG_XX;                                                   \
    if(hw_return.source == AlphaPALReturnSource::ExceptionAddress) \
      sprintf(dbg_strptr, #mnemonic " EXC_ADDR");                \
    else                                                        \
      sprintf(dbg_strptr, #mnemonic " r%d", REG_2 & 31);         \
    dbg_strptr += strlen(dbg_strptr);                            \
    if(!bListing)                                                \
    {                                                            \
      sprintf(dbg_strptr, ": (%" PRIx64 ")",                     \
        hw_return.source == AlphaPALReturnSource::ExceptionAddress \
          ? state.exc_addr : state.r[REG_2]);                    \
      dbg_strptr += strlen(dbg_strptr);                          \
    }                                                            \
  }

#define POST_RET  TRC(0, 1);

#define PRE_IPR(mnemonic, reg)                                                 \
  if(bDisassemble)                                                             \
  {                                                                            \
    DEBUG_XX;                                                                  \
    const char* name = m_profile.ipr_name ? m_profile.ipr_name(ins) : nullptr; \
    dbg_strptr += sprintf(dbg_strptr, #mnemonic " r%d, ", (reg) & 31);         \
    if(name)                                                                   \
      dbg_strptr += sprintf(dbg_strptr, "%s", name);                           \
    else                                                                       \
      dbg_strptr += sprintf(dbg_strptr, "0x%04x", ins & 0xffffu);              \
  }

#define PRE_MFPR(mnemonic) PRE_IPR(mnemonic, REG_1)
#define POST_MFPR POST_X64(state.r[REG_1]);

#define PRE_MTPR(mnemonic) PRE_IPR(mnemonic, REG_2)
#define POST_MTPR POST_X64(state.r[REG_2]);

#define PRE_NOP(mnemonic)              \
  if(bDisassemble)                     \
  {                                    \
    DEBUG_XX;                          \
    sprintf(dbg_strptr, #mnemonic ""); \
    dbg_strptr += strlen(dbg_strptr);  \
  }

#define POST_NOP  ;

#define PRE_MEM(mnemonic)                                                        \
  if(bDisassemble)                                                               \
  {                                                                              \
    DEBUG_XX;                                                                    \
    sprintf(dbg_strptr, #mnemonic " r%d, %04xH(r%d)", REG_1 & 31, (u32) DISP_16, \
            REG_2 & 31);                                                         \
    dbg_strptr += strlen(dbg_strptr);                                            \
    if(!bListing)                                                                \
    {                                                                            \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", state.r[REG_2]);                        \
      dbg_strptr += strlen(dbg_strptr);                                          \
    }                                                                            \
  }

#define POST_MEM  POST_X64(state.r[REG_1]);

#define PRE_R12_R3(mnemonic)                                           \
  if(bDisassemble)                                                     \
  {                                                                    \
    DEBUG_XX;                                                          \
    sprintf(dbg_strptr, #mnemonic " r%d, ", REG_1 & 31);               \
    dbg_strptr += strlen(dbg_strptr);                                  \
    if(ins & 0x1000)                                                   \
      sprintf(dbg_strptr, "%" PRIx64 "H", V_2);                               \
    else                                                               \
      sprintf(dbg_strptr, "r%d", REG_2 & 31);                          \
    dbg_strptr += strlen(dbg_strptr);                                  \
    sprintf(dbg_strptr, ", r%d", REG_3 & 31);                          \
    dbg_strptr += strlen(dbg_strptr);                                  \
    if(!bListing)                                                      \
    {                                                                  \
      sprintf(dbg_strptr, ": (%" PRIx64 ",%" PRIx64 ")", state.r[REG_1], V_2); \
      dbg_strptr += strlen(dbg_strptr);                                \
    }                                                                  \
  }

#define POST_R12_R3 POST_X64(state.r[REG_3]);

  // Pre-debugging macro for floating-point instructions that have Fa and Fb

  // as input and Fc as output.
#define PRE_F12_F3(mnemonic)                                                 \
  if(bDisassemble)                                                           \
  {                                                                          \
    DEBUG_XX;                                                                \
    sprintf(dbg_strptr, #mnemonic " f%d, f%d, f%d", FREG_1, FREG_2, FREG_3); \
    dbg_strptr += strlen(dbg_strptr);                                        \
    if(!bListing)                                                            \
    {                                                                        \
      sprintf(dbg_strptr, ": (%" PRIx64 ",%" PRIx64 ")", state.f[FREG_1],            \
              state.f[FREG_2]);                                              \
      dbg_strptr += strlen(dbg_strptr);                                      \
    }                                                                        \
  }

// Post-debugging macro for floating-point instructions that have Fa and Fb
// as input and Fc as output.
#define POST_F12_F3 POST_X64(state.f[FREG_3]);

#define PRE_R1_F3(mnemonic)                                          \
  if(bDisassemble)                                                   \
  {                                                                  \
    DEBUG_XX;                                                        \
    sprintf(dbg_strptr, #mnemonic " r%d, f%d ", REG_1 & 31, FREG_3); \
    dbg_strptr += strlen(dbg_strptr);                                \
    if(!bListing)                                                    \
    {                                                                \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", state.r[REG_1]);            \
      dbg_strptr += strlen(dbg_strptr);                              \
    }                                                                \
  }

#define POST_R1_F3  POST_X64(state.f[FREG_3]);

#define PRE_F1_R3(mnemonic)                                          \
  if(bDisassemble)                                                   \
  {                                                                  \
    DEBUG_XX;                                                        \
    sprintf(dbg_strptr, #mnemonic " f%d, r%d ", FREG_1, REG_3 & 31); \
    dbg_strptr += strlen(dbg_strptr);                                \
    if(!bListing)                                                    \
    {                                                                \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", state.f[FREG_1]);           \
      dbg_strptr += strlen(dbg_strptr);                              \
    }                                                                \
  }

#define POST_F1_R3  POST_X64(state.r[REG_3]);

#define PRE_X_F1(mnemonic)                               \
  if(bDisassemble)                                       \
  {                                                      \
    DEBUG_XX;                                            \
    sprintf(dbg_strptr, #mnemonic " f%d ", FREG_1 & 31); \
    dbg_strptr += strlen(dbg_strptr);                    \
  }

#define POST_X_F1 POST_X64(state.f[FREG_1]);

#define PRE_R2_R3(mnemonic)                    \
  if(bDisassemble)                             \
  {                                            \
    DEBUG_XX;                                  \
    sprintf(dbg_strptr, #mnemonic " ");        \
    dbg_strptr += strlen(dbg_strptr);          \
    if(ins & 0x1000)                           \
      sprintf(dbg_strptr, "%" PRIx64 "H", V_2);       \
    else                                       \
      sprintf(dbg_strptr, "r%d", REG_2 & 31);  \
    dbg_strptr += strlen(dbg_strptr);          \
    sprintf(dbg_strptr, ", r%d", REG_3 & 31);  \
    dbg_strptr += strlen(dbg_strptr);          \
    if(!bListing)                              \
    {                                          \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", V_2); \
      dbg_strptr += strlen(dbg_strptr);        \
    }                                          \
  }

#define POST_R2_R3  POST_X64(state.r[REG_3]);

#define PRE_X_R1(mnemonic)                             \
  if(bDisassemble)                                     \
  {                                                    \
    DEBUG_XX;                                          \
    sprintf(dbg_strptr, #mnemonic " r%d", REG_1 & 31); \
    dbg_strptr += strlen(dbg_strptr);                  \
  }

#define POST_X_R1 POST_X64(state.r[REG_1]);

#define PRE_X_R3(mnemonic)                             \
  if(bDisassemble)                                     \
  {                                                    \
    DEBUG_XX;                                          \
    sprintf(dbg_strptr, #mnemonic " r%d", REG_3 & 31); \
    dbg_strptr += strlen(dbg_strptr);                  \
  }

#define POST_X_R3 POST_X64(state.r[REG_3]);

#define PRE_HW_LD(mnemonic)                                                     \
  if(bDisassemble)                                                              \
  {                                                                             \
    DEBUG_XX;                                                                   \
    dbg_strptr += sprintf(dbg_strptr, #mnemonic "%s%s%s%s%s r%d, %04xH(r%d)",   \
      hw_load.address == AlphaHWLoadAddress::Physical ? "/Phys" : "",           \
      hw_load.locked ? "/Lock" : "", hw_load.vpte ? "/Vpte" : "",               \
      hw_load.alternate ? "/Alt" : "", hw_load.write_check ? "/Chk" : "",       \
      REG_1 & 31, (u32) hw_load.displacement, REG_2 & 31);                      \
    if(!bListing)                                                               \
      dbg_strptr += sprintf(dbg_strptr, ": (%" PRIx64 ")", state.r[REG_2]);     \
  }

#define POST_HW_LD  POST_X64(state.r[REG_1]);

#define PRE_HW_ST(mnemonic)                                                     \
  if(bDisassemble)                                                              \
  {                                                                             \
    DEBUG_XX;                                                                   \
    dbg_strptr += sprintf(dbg_strptr, #mnemonic "%s%s%s r%d, %04xH(r%d)",       \
      hw_store.address == AlphaHWStoreAddress::Physical ? "/Phys" : "",         \
      hw_store.conditional ? "/Cond" : "", hw_store.alternate ? "/Alt" : "",    \
      REG_1 & 31, (u32) hw_store.displacement, REG_2 & 31);                     \
    if(!bListing)                                                               \
      dbg_strptr += sprintf(dbg_strptr, ": (%" PRIx64 ")", state.r[REG_2]);     \
  }

#define POST_HW_ST  POST_X64(state.r[REG_1]);

// Pre-debugging macro for floating-point load/store instructions.
#define PRE_FMEM(mnemonic)                                                   \
  if(bDisassemble)                                                           \
  {                                                                          \
    DEBUG_XX;                                                                \
    sprintf(dbg_strptr, #mnemonic " f%d, %04xH(r%d)", FREG_1, (u32) DISP_16, \
            REG_2 & 31);                                                     \
    dbg_strptr += strlen(dbg_strptr);                                        \
    if(!bListing)                                                            \
    {                                                                        \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", state.r[REG_2]);                    \
      dbg_strptr += strlen(dbg_strptr);                                      \
    }                                                                        \
  }

// Post-debugging macro for floating-point load/store instructions.
#define POST_FMEM POST_X64(state.f[FREG_1]);

// Pre-debugging macro for floating-point instructions that have Fb as input

// And Fc as output.
#define PRE_F2_F3(mnemonic)                                     \
  if(bDisassemble)                                              \
  {                                                             \
    DEBUG_XX;                                                   \
    sprintf(dbg_strptr, #mnemonic " f%d, f%d", FREG_2, FREG_3); \
    dbg_strptr += strlen(dbg_strptr);                           \
    if(!bListing)                                               \
    {                                                           \
      sprintf(dbg_strptr, ": (%" PRIx64 ")", state.f[FREG_2]);      \
      dbg_strptr += strlen(dbg_strptr);                         \
    }                                                           \
  }

// Post-debugging macro for floating-point instructions that have Fb as input
// And Fc as output.
#define POST_F2_F3  POST_X64(state.f[FREG_3]);

#else
/* HRM 6.8.2 Table 6-8: OPCDEC fault is raised for opcodes 1-7, the PALRES
 * opcodes (0x19/0x1B/0x1D/0x1E/0x1F) when not in PALmode and HWE is clear,
 * for unimplemented function-field encodings of opcodes 0x14 and 0x1C, and
 * for invalid CALL_PAL function values. brokenpipe (alphafix) routes all of
 * these through the same `unallocated_encoding()` -> EXCP_OPCDEC path.
 *
 * The printfs are intentional diagnostic noise: if a truly missing opcode case
 * exists in es40's dispatch, the warning will surface at the host stderr even
 * though the architectural OPCDEC trap also fires. The hot path under a
 * working guest workload should never see these. */
#define UNKNOWN1                                                             \
  printf("%%CPU-W-OPCDEC: unknown opcode %02x at pc=%016" PRIx64 "\n",       \
         opcode, state.current_pc);                                          \
  GO_PAL(AlphaPALException::OpcodeDecode);                                    \
  ES40_EXECUTE_END();

#define UNKNOWN2                                                             \
  printf("%%CPU-W-OPCDEC: unknown opcode %02x.%02x at pc=%016" PRIx64 "\n",  \
         opcode, function, state.current_pc);                                \
  GO_PAL(AlphaPALException::OpcodeDecode);                                    \
  ES40_EXECUTE_END();
#endif
#if defined(IDB)

  // Debugging version of the OP macro:
  // Execute the DO_<mnemonic> macro for an instruction, along with disassembling

  // the instruction if needed.
#define OP(mnemonic, format)       \
  PRE_##format(mnemonic);          \
  if(!bListing)                    \
  {                                \
    DO_##mnemonic;                 \
  } POST_##format;                 \
  handle_debug_string(dbg_string); \
  ES40_EXECUTE_END();

// Execute a function rather than a DO_<mnemonic> macro for an instruction
#define OP_FNC(mnemonic, format)   \
  PRE_##format(mnemonic);          \
  if(!bListing)                    \
  {                                \
    mnemonic();                    \
  } POST_##format;                 \
  handle_debug_string(dbg_string); \
  ES40_EXECUTE_END();

#else //defined(IDB)

  // Non-debugging version of the OP macro:

  // Execute the DO_<mnemonic> macro for an instruction.
#define OP(mnemonic, format) \
  DO_##mnemonic;             \
  ES40_EXECUTE_END();

// Execute a function rather than a DO_<mnemonic> macro for an instruction
#define OP_FNC(mnemonic, format) \
  mnemonic();                    \
  ES40_EXECUTE_END();
#endif //defined(IDB)
