/* ES40 emulator -- JIT engine: the x86-64 codegen backend.
 * Copyright (C) 2026 by gdwnldsKSC
 * All rights reserved.
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef ES40_JIT

#include "jitengine.h"        // defines ES40_JIT_X64 / ES40_JIT_A64 from the host arch
#include "jitengine_internal.h"
#ifdef JIT_STATS
#define JIT_STAT_EXIT(k) do { a.mov(x86::rax, imm((uint64_t) exit_kind_counter(k))); a.inc(x86::qword_ptr(x86::rax)); } while (0)
#else
#define JIT_STAT_EXIT(k) ((void) 0)
#endif

#ifdef ES40_JIT_X64

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstddef>  // offsetof (chain guard)
#include <initializer_list>
#include <vector>   // deferred slow-path stubs (memop helper calls)
#ifndef ASMJIT_STATIC
#define ASMJIT_STATIC
#endif
// asmjit's x64 backend emits x86-64; asmjit's CallConv maps the host C ABI
// (Microsoft x64 or System V) from the build env.
#include <asmjit/x86.h>

// ---- guest-ISA classification + emit support (x86 backend only) ----
namespace {

// Log/error any asmjit emit failure (badly formed instruction / bad operand) that the Assembler
// would otherwise accept and then ship as a silently truncated block/trace. Strict instruction
// validation and formatted disassembly remain optional; rejecting a failed image is not.
class JitErrorHandler : public asmjit::ErrorHandler {
public:
  FILE* fp = nullptr;   // disassembly trace file (falls back to stderr if unopened)
  int   cpu_id = -1;
  bool  failed = false;
  void handle_error(asmjit::Error err, const char* message, asmjit::BaseEmitter*) override {
    (void) err;
    failed = true;
    fprintf(fp ? fp : stderr, "[JIT][CPU%d][EMIT-ERROR] %s\n", cpu_id, message);
  }
};

enum SafeOp {
  OP_NONE,
  OP_ADDQ, OP_SUBQ, OP_ADDL, OP_SUBL,
  OP_S4ADDQ, OP_S8ADDQ, OP_S4SUBQ, OP_S8SUBQ,
  OP_S4ADDL, OP_S8ADDL, OP_S4SUBL, OP_S8SUBL,   // INTA (0x10) scaled longword: sext32((Ra*scale) +/- Rb)
  OP_CMPBGE,                                    // INTA (0x10) per-byte unsigned compare -> 8-bit mask
  OP_SEXTB, OP_SEXTW,                            // FPTI (0x1c) CIX/BWX: Rc = sign-extend byte/word of op2
  OP_CTPOP, OP_CTLZ, OP_CTTZ,                    // FPTI (0x1c) CIX bit-count of op2: popcount / leading / trailing zeros
  OP_ITOFS, OP_ITOFF, OP_ITOFT,                  // ITFP (0x14) int->FP reg moves: f[Fc] = fmt(Ra)
  OP_FTOIS, OP_FTOIT,                            // FPTI (0x1c) FP->int reg moves: Rc = fmt(f[Fa])
  OP_FLTL,                                       // FLTL (0x17) non-arithmetic: FPCR moves, CPYSx, FCMOVx, CVTLQ/QL
  OP_CVTQT, OP_CVTQS,                            // FLTI (0x16) int->IEEE convert: f[Fc] = (T/S)(s64)f[Fb] via SSE
  OP_ADDT, OP_SUBT, OP_MULT, OP_DIVT,            // FLTI (0x16) IEEE T-float arith: f[Fc] = f[Fa] op f[Fb] via SSE
  OP_CMPTUN, OP_CMPTEQ, OP_CMPTLT, OP_CMPTLE,    // FLTI (0x16) IEEE T-float compares: f[Fc] = (cmp) ? 2.0 : 0.0
  OP_ADDS, OP_SUBS, OP_MULS, OP_DIVS,            // FLTI (0x16) IEEE S-float arith: f[Fc] = round_single(f[Fa] op f[Fb])
  OP_CVTST, OP_CVTTS, OP_CVTTQ,                  // FLTI (0x16) IEEE converts: S->T widen, T->S narrow, T->Q to int
  OP_SQRTS, OP_SQRTT,                            // ITFP (0x14) IEEE sqrt: f[Fc] = sqrt(f[Fb]) via sqrtss/sqrtsd
  OP_FLTV,                                       // FLTV (0x15) VAX arith/convert/compare: f[Fc] via jit_fltv helper
  OP_AND, OP_BIS, OP_XOR, OP_BIC, OP_ORNOT, OP_EQV,
  OP_CMOV,                       // INTL (0x11) conditional moves CMOVxx: Rc = cond(Ra) ? op2 : Rc
  OP_AMASK, OP_IMPLVER,          // INTL (0x11) CPU feature probes from the selected profile
  OP_CMPEQ, OP_CMPLT, OP_CMPLE, OP_CMPULT, OP_CMPULE,
  OP_SLL, OP_SRL, OP_SRA, OP_MULQ,
  OP_MULL, OP_UMULH,             // INTM (0x13): MULL = sext32(Ra*op2); UMULH = hi64 of unsigned Ra*op2
  OP_EXTL, OP_EXTH, OP_INSL, OP_INSH, OP_MSKL, OP_MSKH, OP_ZAP,   // INTS (0x12) byte-manip (Rb&7 keyed)
  OP_NOP, OP_MFENCE,             // MISC (0x18): prefetch/cache hints (no-op), barriers (mfence)
  OP_RPCC, OP_RC, OP_RS,         // MISC (0x18) state reads (Ra dest): cycle counter / read-and-clear,set intr flag
  OP_LDQ, OP_LDL,                // memory-format loads: Ra = MEM[Rb + disp16]
  OP_LDBU, OP_LDWU,              // BWX byte/word loads (0x0a/0x0c): Ra = zero-extend MEM[Rb+disp16].{b,w}
  OP_LDL_L, OP_LDQ_L,            // load-locked (0x2a/0x2b): Ra = MEM[Rb+disp16] + establish LL/SC monitor
  OP_STL_C, OP_STQ_C,            // store-conditional (0x2e/0x2f): cond store Ra -> MEM; Ra = success(1)/fail(0)
  OP_STQ, OP_STL,                // memory-format stores: MEM[Rb + disp16] = Ra
  OP_STB, OP_STW,                // BWX byte/word stores (0x0e/0x0d): MEM[Rb + disp16].{b,w} = Ra
  OP_LDQ_U, OP_STQ_U,            // unaligned quad (0x0b/0x0f): MEM[(Rb+disp16) & ~7] load/store
  OP_LDT, OP_LDS, OP_STT, OP_STS, // FP memory (0x23/0x22/0x27/0x26): f[Fa] <-> MEM, LDT/STT raw, LDS/STS ieee conv
  OP_LDF, OP_LDG, OP_STF, OP_STG, // VAX FP memory (0x20/0x21/0x24/0x25): f[Fa] <-> MEM, F/G format conversion (helper)
  OP_LDA, OP_LDAH,               // load-address: Ra = Rb + disp16 (<<16 for LDAH); pure ALU
  OP_HW_MFPR,                    // HW_MFPR (0x19), PALmode only: family-selected IPR read into Ra
  OP_HW_LDL, OP_HW_LDQ,          // HW_LD (0x1b): normalized physical longword/quadword loads
  OP_HW_LD_VIRT,                 // HW_LD: normalized virtual loads with family-selected access flags
  OP_HW_MTPR,                    // HW_MTPR (0x1d): family-approved helper writes, PALmode only
  OP_HW_MTPR_TERM,               // HW_MTPR: a family write changes compiled assumptions -> redispatch
  OP_HW_STL, OP_HW_STQ,          // HW_ST (0x1f): normalized physical longword/quadword stores
  OP_JMP,                        // JMP/JSR/RET (0x1a): Ra = PC+4; PC = Rb & ~3 (computed target)
  OP_HW_RET,                     // HW_RET (0x1e), PALmode: family-selected pure return-target transform
  OP_CALL_PAL,                   // CALL_PAL (0x00): save exc_addr/optional link; PC = pal_base | entry offset
  // Branch-format terminators (contiguous; see is_branch). Conditional on Ra, plus BR/BSR.
  OP_BEQ, OP_BNE, OP_BLT, OP_BLE, OP_BGT, OP_BGE, OP_BLBC, OP_BLBS, OP_BR, OP_BSR,
  OP_FBEQ, OP_FBNE, OP_FBLT, OP_FBLE, OP_FBGT, OP_FBGE   // FP branches (0x31-0x37): branch on f[Fa] vs 0.0
};

static inline bool is_branch(SafeOp op) { return op >= OP_BEQ && op <= OP_FBGE; }
static inline bool is_fp_branch(SafeOp op) { return op >= OP_FBEQ && op <= OP_FBGE; }
static inline bool is_store(SafeOp op)  { return op == OP_STQ || op == OP_STL; }
// A terminator ends the block and writes its own next PC (branches + the computed jump).
static inline bool is_terminator(SafeOp op) { return op == OP_JMP || op == OP_HW_RET || op == OP_CALL_PAL || op == OP_HW_MTPR_TERM || is_branch(op); }

// POPCNT isn't baseline x86-64 (pre-2008 CPUs lack it); query the host once. CTLZ/CTTZ use
// baseline BSR/BSF, so only CTPOP is gated -- it stays interpreted when the host lacks POPCNT.
static bool host_has_popcnt() {
  static const bool ok = asmjit::CpuInfo::host().features().x86().has_popcnt();
  return ok;
}

static bool host_has_bmi2() {
  static const bool ok = asmjit::CpuInfo::host().features().x86().has_bmi2();
  return ok;
}

// Safe = goto-free, register-only operate-format ops (no trap, memory, or branch).
// pal_block enables PALmode-only ops (HW_MFPR): outside PALmode they'd OPCDEC, so only
// compile them when the block is PALmode (the dispatcher keys blocks by PC bit 0).
SafeOp classify(uint32_t ins, bool pal_block, const AlphaIPRReadPolicy& ipr_read,
    const AlphaIPRWritePolicy& ipr_write, const AlphaPALPolicy& pal,
    const AlphaHWLoadPolicy& hw_load, const AlphaHWStorePolicy& hw_store)
{
  uint32_t opcode = ins >> 26;
  uint32_t func = (ins >> 5) & 0x7F;
  switch (opcode) {
    case 0x10: // INTA
      switch (func) {
        case 0x20: return OP_ADDQ;   case 0x29: return OP_SUBQ;
        case 0x00: return OP_ADDL;   case 0x09: return OP_SUBL;
        case 0x22: return OP_S4ADDQ; case 0x32: return OP_S8ADDQ;
        case 0x2b: return OP_S4SUBQ; case 0x3b: return OP_S8SUBQ;
        case 0x02: return OP_S4ADDL; case 0x12: return OP_S8ADDL;
        case 0x0b: return OP_S4SUBL; case 0x1b: return OP_S8SUBL;
        case 0x0f: return OP_CMPBGE;
        case 0x2d: return OP_CMPEQ;  case 0x4d: return OP_CMPLT;
        case 0x6d: return OP_CMPLE;  case 0x1d: return OP_CMPULT;
        case 0x3d: return OP_CMPULE;
        // ADDL/V (0x40), SUBL/V (0x49), ADDQ/V (0x60), SUBQ/V (0x69): overflow-trapping -> interpret
      }
      break;
    case 0x11: // INTL
      switch (func) {
        case 0x00: return OP_AND;   case 0x20: return OP_BIS;
        case 0x40: return OP_XOR;   case 0x08: return OP_BIC;
        case 0x28: return OP_ORNOT; case 0x48: return OP_EQV;
        case 0x14: case 0x16: case 0x24: case 0x26:   // CMOVLBS/LBC/EQ/NE
        case 0x44: case 0x46: case 0x64: case 0x66:   // CMOVLT/GE/LE/GT
          return OP_CMOV;
        case 0x61:                                    // AMASK: Ra must be R31 (else the interp's
          // DO_AMASK traps OPCDEC) -- compile only the architecturally valid form.
          if (((ins >> 21) & 0x1f) != 31) return OP_NONE;
          return OP_AMASK;
        case 0x6c: return OP_IMPLVER;                 // IMPLVER: Rc = implementation version constant
      }
      break;
    case 0x12: // INTS: shifts + byte-manipulation (extract / insert / mask / zap), Rb&7 keyed
      switch (func) {
        case 0x39: return OP_SLL; case 0x34: return OP_SRL; case 0x3c: return OP_SRA;
        case 0x06: case 0x16: case 0x26: case 0x36: return OP_EXTL;   // EXTBL/WL/LL/QL
        case 0x5a: case 0x6a: case 0x7a:            return OP_EXTH;   // EXTWH/LH/QH
        case 0x0b: case 0x1b: case 0x2b: case 0x3b: return OP_INSL;   // INSBL/WL/LL/QL
        case 0x57: case 0x67: case 0x77:            return OP_INSH;   // INSWH/LH/QH
        case 0x02: case 0x12: case 0x22: case 0x32: return OP_MSKL;   // MSKBL/WL/LL/QL
        case 0x52: case 0x62: case 0x72:            return OP_MSKH;   // MSKWH/LH/QH
        case 0x30: case 0x31:                       return OP_ZAP;    // ZAP / ZAPNOT
      }
      break;
    case 0x13: // INTM (integer multiply). MULL/V (0x40) + MULQ/V (0x60) overflow-trap -> interpret.
      if (func == 0x20) return OP_MULQ;
      if (func == 0x00) return OP_MULL;    // 32-bit multiply, low 32 sign-extended
      if (func == 0x30) return OP_UMULH;   // high 64 bits of the unsigned 128-bit product
      break;
    case 0x14: {  // ITFP: compile the int->FP register moves + IEEE SQRT*. Fc==31 is interpreted
      // (the interp zeroes f[31] per instruction -- a compiled write would desync).
      const uint32_t f14 = (ins >> 5) & 0x7ff;
      if ((ins & 0x1f) == 31) return OP_NONE;             // Fc==31 -> interp
      const uint32_t sb = f14 & 0x3f;                     // IEEE sqrt (source Fb): SQRTS 0x0b / SQRTT 0x2b
      if (sb == 0x0b || sb == 0x2b) {
        if (((ins >> 21) & 0x1f) != 31) return OP_NONE;   // Ra must be R31 (else DO_SQRT* traps OPCDEC)
        if (((f14 & 0x600) == 0x200) || ((f14 & 0x500) == 0x400)) return OP_NONE;  // invalid qualifier -> OPCDEC
        const uint32_t r = (f14 >> 6) & 3;
        if (r == 0 || r == 1) return OP_NONE;             // /C, /M: SSE rounds nearest -> interpret
        if (((f14 >> 8) & 7) == 7) return OP_NONE;        // /SUI -> interpret
        return (sb == 0x2b) ? OP_SQRTT : OP_SQRTS;
      }
      if (((ins >> 16) & 0x1f) != 31) return OP_NONE;     // ITOFx: Rb must be 31 (Ra is the int source)
      if (f14 == 0x004) return OP_ITOFS;
      if (f14 == 0x014) return OP_ITOFF;
      if (f14 == 0x024) return OP_ITOFT;
      return OP_NONE;
    }
    case 0x17: {  // FLTL: all non-arithmetic (FPCR moves, sign-copies, FCMOVs, CVTLQ/QL) via
      // jit_fltl; only the /V trap variants of CVTQL stay interpreted.
      const uint32_t f17 = (ins >> 5) & 0x7ff;
      const bool ok = f17 == 0x010 || (f17 >= 0x020 && f17 <= 0x022) || f17 == 0x024
                   || f17 == 0x025 || (f17 >= 0x02a && f17 <= 0x02f) || f17 == 0x030;
      if (!ok) return OP_NONE;
      // f31-dest gate (interp zeroes f[31] per instr): MF_FPCR writes f[Fa]; MT_FPCR has no FP dest
      if (f17 == 0x025)      { if (((ins >> 21) & 0x1f) == 31) return OP_NONE; }
      else if (f17 != 0x024) { if ((ins & 0x1f) == 31) return OP_NONE; }
      return OP_FLTL;
    }
    case 0x15: { // FLTV (VAX): helper-dispatched arith/convert/compare; bail to the interp on trap.
      if ((ins & 0x1f) == 31) return OP_NONE;       // Fc==31: interp zeroes f[31] per instr
      const uint32_t f = (ins >> 5) & 0x7ff;
      if (f == 0x0a5 || f == 0x4a5 || f == 0x0a6 || f == 0x4a6 || f == 0x0a7 || f == 0x4a7   // CMPGEQ/LT/LE
       || f == 0x03c || f == 0x0bc || f == 0x03e || f == 0x0be) return OP_FLTV;              // CVTQF/CVTQG
      if (f & 0x200) return OP_NONE;                 // invalid qualifier -> interp OPCDECs (UNKNOWN2)
      switch (f & 0x7f) {                            // base-op arith / G<->F<->D / G->Q convert
        case 0x000: case 0x001: case 0x002: case 0x003:   // ADDF/SUBF/MULF/DIVF
        case 0x01e:                                       // CVTDG
        case 0x020: case 0x021: case 0x022: case 0x023:   // ADDG/SUBG/MULG/DIVG
        case 0x02c: case 0x02d: case 0x02f:               // CVTGF/CVTGD/CVTGQ
          return OP_FLTV;
      }
      return OP_NONE;                                // other 0x15 funcs: interp OPCDECs
    }
    case 0x16: { // FLTI (IEEE): SSE-inline the int->float converts; bail to the interp on traps/edges.
      const uint32_t f = (ins >> 5) & 0x7ff;
      if ((ins & 0x1f) == 31) return OP_NONE;       // Fc==31: interp zeroes f[31] per instr (all 0x16)
      if (f == 0x2ac || f == 0x6ac) return OP_CVTST;  // CVTST (S->T): before the invalid gate (SRC bit set);
                                                      // S-denormal renormalizes / NaN quiets -> bail, else valid-T copy
      if ((f & 0x3f) == 0x2f) {                       // CVTTQ (T->Q int): /C chop is valid -> handle before round gate
        if (((f >> 6) & 3) == 1) return OP_NONE;      // /M -> interp
        if (((f >> 8) & 7) == 7) return OP_NONE;      // /SUI -> interp
        if (((f & 0x600) == 0x200) || ((f & 0x500) == 0x400)) return OP_NONE;  // invalid qualifier
        return OP_CVTTQ;
      }
      if (((f & 0x600) == 0x200) || ((f & 0x500) == 0x400)) return OP_NONE;  // invalid qualifier -> OPCDEC
      const uint32_t rnd = (f >> 6) & 3;            // 0=/C 1=/M 2=/N 3=/D (dynamic, runtime-checked)
      if (rnd == 0 || rnd == 1) return OP_NONE;     // chopped / minus-inf: SSE rounds to nearest -> interpret
      if (((f >> 8) & 7) == 7) return OP_NONE;      // /SUI: traps on every inexact -> interpret
      if (f == 0x0a4 || f == 0x5a4) return OP_CMPTUN;   // IEEE compares (None + /SU): f[Fc] = (cmp) ? 2.0 : 0.0
      if (f == 0x0a5 || f == 0x5a5) return OP_CMPTEQ;   // full-function; a NaN operand bails to the interp
      if (f == 0x0a6 || f == 0x5a6) return OP_CMPTLT;
      if (f == 0x0a7 || f == 0x5a7) return OP_CMPTLE;
      const uint32_t baseop = f & 0x3f;
      if (baseop == 0x00) return OP_ADDS;          // ADDS \  IEEE S-float (single) arith via SSE
      if (baseop == 0x01) return OP_SUBS;          // SUBS  >
      if (baseop == 0x02) return OP_MULS;          // MULS  |
      if (baseop == 0x03) return OP_DIVS;          // DIVS /
      if (baseop == 0x20) return OP_ADDT;          // ADDT \  IEEE T-float (double) arith via SSE:
      if (baseop == 0x21) return OP_SUBT;          // SUBT  > f[Fc] = f[Fa] op f[Fb]
      if (baseop == 0x22) return OP_MULT;          // MULT  |
      if (baseop == 0x23) return OP_DIVT;          // DIVT /
      if (baseop == 0x2c) return OP_CVTTS;         // CVTTS (T->S narrow): f[Fc] = round_single(f[Fb])
      if (baseop == 0x3e && (f & 0x300) != 0x100) return OP_CVTQT;  // CVTQT: f[Fc] = (double)(s64) f[Fb]
      if (baseop == 0x3c && (f & 0x300) != 0x100) return OP_CVTQS;  // CVTQS: f[Fc] = (single)(s64) f[Fb]
      return OP_NONE;                               // all other 0x16 funcs interpreted
    }
    case 0x1c: // FPTI (CIX/BWX/MVI/FP-moves): the sign-extends, bit-counts, and FP->int register
      // moves compile here (CTPOP needs host POPCNT). MVI packed media (PERR/MIN/MAX/PK/UNPK)
      // stays interpreted for now.
      if (func == 0x00) return OP_SEXTB;   // sign-extend byte of op2
      if (func == 0x01) return OP_SEXTW;   // sign-extend word of op2
      if (func == 0x32) return OP_CTLZ;    // count leading zeros of op2  (BSR; op2==0 -> 64)
      if (func == 0x33) return OP_CTTZ;    // count trailing zeros of op2 (BSF; op2==0 -> 64)
      if (func == 0x30 && host_has_popcnt()) return OP_CTPOP;   // popcount; interpret when host lacks POPCNT
      if (func == 0x70 || func == 0x78) {  // FTOIT / FTOIS: Rb must be 31; Rc==31 -> interpret
        if (((ins >> 16) & 0x1f) != 31 || (ins & 0x1f) == 31) return OP_NONE;
        return (func == 0x70) ? OP_FTOIT : OP_FTOIS;
      }
      break;
    case 0x18: // MISC: MB -> full fence; TRAPB/EXCB/WMB + prefetch/cache hints -> no code
      switch (ins & 0xFFFF) {
        case 0x4000: return OP_MFENCE;               // MB: StoreLoad fence
        case 0x0000: case 0x0400: case 0x4400:       // TRAPB, EXCB, WMB: no code under precise traps and TSO
        case 0x8000: case 0xA000: case 0xE800:       // FETCH, FETCH_M, ECB
        case 0xF800: case 0xFC00: return OP_NOP;     // WH64, WH64EN
        // RPCC/RC/RS read time-varying / consumed state into Ra; the verify log+replays the read so the
        // two passes agree. RC/RS also side-effect the soft-interrupt flag, so the Ra==31 (no GPR dest,
        // flag-only) forms are compiled too -- the helper still runs, the store is skipped. RPCC Ra==31
        // is a pure no-op read (nothing to replay), so it stays interpreted.
        case 0xC000: if (((ins >> 21) & 0x1f) != 31) return OP_RPCC; break;  // RPCC (cycle counter)
        case 0xE000: return OP_RC;   // RC (read & clear soft-intr flag); Ra==31 -> clear-only
        case 0xF000: return OP_RS;   // RS (read & set soft-intr flag); Ra==31 -> set-only
      }
      break;
    case 0x00: {                // CALL_PAL: compile valid standard funcs (priv 0x00-0x3f, unpriv 0x80-0xbf)
      const uint32_t fn = ins & 0x1FFFFFFF;
      if (fn == 0x3E) break;    // WTINT: interpret -> DO_CALL_PAL (native idle completion when enabled)
      if (fn <= 0x3F || (fn >= 0x80 && fn <= 0xBF)) return OP_CALL_PAL;
      break;                    // SRM specials (0x1234xx) / invalid ranges -> interpret
    }
    case 0x08: return OP_LDA;   // load address (Ra = Rb + disp16) -- pure ALU, no memory
    case 0x09: return OP_LDAH;  // load address high (Ra = Rb + (disp16 << 16))
    case 0x19: {                // HW_MFPR: read IPR -> Ra. PALmode-only (else OPCDEC).
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        return OP_NONE;
      const auto descriptor = ipr_read.describe(ins);
      if (descriptor.kind == AlphaIPRReadKind::Unsupported) return OP_NONE;
      if (((ins >> 21) & 0x1f) == 31 && descriptor.discard_safe) return OP_NOP;
      return OP_HW_MFPR;
    }
    case 0x1b: {                // HW_LD: the family declares the normalized access and eligibility.
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        return OP_NONE;
      const auto descriptor = alpha_decode_hw_load(hw_load, ins);
      if (!alpha_hw_load_compilable(descriptor)) return OP_NONE;
      if (descriptor.address == AlphaHWLoadAddress::Physical) {
        if (((ins >> 21) & 0x1f) == 31 && !descriptor.discard_safe) return OP_NONE;
        return descriptor.size_bits == 32 ? OP_HW_LDL : OP_HW_LDQ;
      }
      if (descriptor.address == AlphaHWLoadAddress::Virtual) return OP_HW_LD_VIRT;
      return OP_NONE;
    }
    case 0x1d: {                // HW_MTPR (PALmode): the family declares compiled-safe writes.
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        return OP_NONE;
      switch (ipr_write.describe(ins)) {
        case AlphaIPRWriteKind::Helper: return OP_HW_MTPR;
        case AlphaIPRWriteKind::HelperTerminator: return OP_HW_MTPR_TERM;
        case AlphaIPRWriteKind::Noop: return OP_NOP;
        case AlphaIPRWriteKind::Interpreter: return OP_NONE;
      }
      return OP_NONE;
    }
    case 0x1f: {                // HW_ST: the family declares normalized compiled-safe physical writes.
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        return OP_NONE;
      const auto descriptor = alpha_decode_hw_store(hw_store, ins);
      if (!alpha_hw_store_compilable(descriptor)) return OP_NONE;
      return descriptor.size_bits == 32 ? OP_HW_STL : OP_HW_STQ;
    }
    case 0x1a: return OP_JMP;   // JMP/JSR/RET: computed jump (target = Rb & ~3). Now compiled --
                                // chained in-frame via a block-cache lookup (jit_indirect), which
                                // handles varying targets without the old single-slot thrash.
    case 0x1e:                  // HW_RET (HWREI): the family selects a compilable pure target transform.
      return pal_block && pal.hardware_instruction_access
          && alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins))
          && alpha_pal_return_compilable(alpha_pal_return_descriptor(pal, ins))
          ? OP_HW_RET : OP_NONE;
    case 0x28: return OP_LDL;   // memory-format loads (Ra = MEM[Rb+disp16])
    case 0x29: return OP_LDQ;
    case 0x0a: return OP_LDBU;  // BWX byte/word loads (Ra = zero-extend MEM[Rb+disp16].{b,w})
    case 0x0c: return OP_LDWU;
    case 0x0b: return OP_LDQ_U;  // unaligned quad load: Ra = MEM[(Rb+disp16) & ~7]
    case 0x2a:                  // LDL_L / LDQ_L: the load-locked half of LL/SC. Compile the value-
    case 0x2b:                  // returning forms only -- Ra==31 (lock without consuming the value)
      // leaves the loaded value out of the GPRs, so the verify can't replay it; interpret those.
      if (((ins >> 21) & 0x1f) == 31) return OP_NONE;
      return (opcode == 0x2a) ? OP_LDL_L : OP_LDQ_L;
    case 0x2c: return OP_STL;   // memory-format stores (MEM[Rb+disp16] = Ra)
    case 0x2d: return OP_STQ;
    case 0x0e: return OP_STB;   // BWX byte/word stores (MEM[Rb+disp16].{b,w} = Ra low bits)
    case 0x0d: return OP_STW;
    case 0x0f: return OP_STQ_U;  // unaligned quad store: MEM[(Rb+disp16) & ~7] = Ra
    case 0x23: return OP_LDT;   // FP loads (f[Fa] = MEM[Rb+disp16]): LDT raw 8B
    case 0x22: return OP_LDS;   //                                    LDS ieee_lds(4B)
    case 0x27: return OP_STT;   // FP stores (MEM[Rb+disp16] = f[Fa]): STT raw 8B
    case 0x26: return OP_STS;   //                                     STS ieee_sts(4B)
    case 0x20: return OP_LDF;   // VAX FP loads:  LDF vax_ldf(4B)
    case 0x21: return OP_LDG;   //                LDG vax_ldg(8B)
    case 0x24: return OP_STF;   // VAX FP stores: STF vax_stf(4B)
    case 0x25: return OP_STG;   //                STG vax_stg(8B)
    case 0x2e:                  // STL_C / STQ_C: the store-conditional half of LL/SC. Ra is both the
    case 0x2f:                  // value AND the success/fail dest. Compile Ra!=31 (Ra==31 discards the
      // result into R31, so the verify can't capture it from a GPR); interpret those.
      if (((ins >> 21) & 0x1f) == 31) return OP_NONE;
      return (opcode == 0x2e) ? OP_STL_C : OP_STQ_C;
    // Branch format: opcode | Ra | disp21. Conditional on Ra, plus BR/BSR.
    case 0x30: return OP_BR;    case 0x34: return OP_BSR;
    case 0x38: return OP_BLBC;  case 0x39: return OP_BEQ;
    case 0x3a: return OP_BLT;   case 0x3b: return OP_BLE;
    case 0x3c: return OP_BLBS;  case 0x3d: return OP_BNE;
    case 0x3e: return OP_BGE;   case 0x3f: return OP_BGT;
    case 0x31: return OP_FBEQ;  case 0x32: return OP_FBLT;   // FP branches: FPSTART, then f[Fa] vs 0.0
    case 0x33: return OP_FBLE;  case 0x35: return OP_FBNE;
    case 0x36: return OP_FBGE;  case 0x37: return OP_FBGT;
  }
  return OP_NONE;
}

} // namespace

// Block register allocator: maps each guest GPR to a host x86 register id, or -1 = the
// state.r[] memory slot. The callee-saved guest pins (R1/R16 -> r12/r15) and volatile
// pins (R22/R23 -> r8/r9) are live across the chain; R13 carries the chain count.
// host_of(r) drives emit_op's operand routing either way.
struct CJitEngine::RegAlloc {
  int host[32];                                  // host x86 reg id for guest GPR r, or -1 (memory)
  int rax_holds;                                 // guest GPR whose value currently lives in rax (value-forward), or -1
  int vol_bind;                                  // guest GPRs held in caller-saved R8/R9 (or -1);
  int vol_bind2;                                 // call sites spill/reload them around helpers
  bool dpc_live;                                 // previous guest op left RDX/R10/R11 = va/bias/slot
  int dpc_base, dpc_disp;
  bool dpc_write, dpc_force_align;
  // Direct exits: the terminator leaves its condition in FLAGS; exit_kind 1 = conditional, 2 = BR/BSR.
  bool     direct_exits;
  int      exit_kind;
  uint32_t exit_cc;
  uint64_t exit_tgt, exit_fall;
#ifdef JIT_STATS
  uint64_t* licm_slots;   // per-memop "page last seen" slots (null = don't probe)
  uint32_t  licm_n, licm_max;
#endif
  int host_of(int r) const { return host[r]; }
};

// ZAP/ZAPNOT byte-expand: g_zapnot_mask[b] has byte i = 0xFF where bit i of b is set (ZAPNOT keeps
// those bytes; ZAP keeps the complement). Compiled ZAP indexes this instead of an 8-way bit test.
static uint64_t g_zapnot_mask[256];
static const bool g_zapnot_init = [] {
  for (int b = 0; b < 256; ++b) {
    uint64_t m = 0;
    for (int i = 0; i < 8; ++i) if (b & (1 << i)) m |= (uint64_t) 0xff << (i * 8);
    g_zapnot_mask[b] = m;
  }
  return true;
}();

#ifdef JIT_REGPROF
// Bitmask of the Alpha integer GPRs a block's prefix touches (read or written), for pin selection.
// Format-aware over the ops that drive the store-forward chains -- integer operate, memory, branch,
// JMP, and the MISC state reads; FP / HW / CALL_PAL are skipped (not the GPR forwarding path).
static uint32_t regprof_mask(const uint32_t* w, uint32_t n)
{
  uint32_t mask = 0;
  auto touch = [&](int r) { if (r != 31) mask |= (1u << r); };   // R31 is hardwired 0, never a pin
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t ins = w[i];
    const uint32_t op  = ins >> 26;
    const int ra = (ins >> 21) & 0x1f, rb = (ins >> 16) & 0x1f, rc = ins & 0x1f;
    const bool islit = ((ins >> 12) & 1) != 0;
    switch (op) {
      case 0x10: case 0x11: case 0x12: case 0x13: case 0x1c:     // integer operate: Ra, Rc, Rb(if reg)
        touch(ra); touch(rc); if (!islit) touch(rb); break;
      case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c:     // LDA/LDAH + BWX ld/st: Ra, Rb(base)
      case 0x0d: case 0x0e: case 0x0f:
      case 0x28: case 0x29: case 0x2a: case 0x2b:                // int LDL/LDQ/LDx_L
      case 0x2c: case 0x2d: case 0x2e: case 0x2f:                // int STL/STQ/STx_C
      case 0x1a:                                                 // JMP/JSR/RET: Ra(link), Rb(target)
        touch(ra); touch(rb); break;
      case 0x30: case 0x34: case 0x38: case 0x39: case 0x3a:     // integer branches (incl BR/BSR link): Ra
      case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
        touch(ra); break;
      case 0x18: {                                               // MISC: RPCC/RC/RS write Ra
        const uint32_t f = ins & 0xffff;
        if (f == 0xc000 || f == 0xe000 || f == 0xf000) touch(ra);
        break;
      }
      default: break;   // FP / HW / CALL_PAL: not the GPR forwarding path
    }
  }
  return mask;
}
#endif

// Callee-saved guest pins; R13 carries the chain instruction count. 
// Blocks also keep R22/R23 in volatile R8/R9..
static const int kGlobalPins[2] = { 1, 16 };
static const int kVolPins[2]    = { 22, 23 };   // caller-saved pins (r8/r9)

// regalloc: compile_trace binds the TRACE'S hottest guest GPRs to the pin registers instead
// of the fixed global set, deleting their state.r[] traffic across the whole fused span.
static constexpr bool TracePinSpike = false;   // measured net-negative on the chained vehicle
                                               // (boundary adapters outweigh in-loop binding)

// chain-out: trace exits chain into block bodies via per-exit LinkSlot arrays (block
// poly-link's guard + patch protocol) instead of returning to the dispatcher.
static constexpr bool TraceChainOut = true;

// chain-in: block exits chain into traces via the trace's chained entry (the hook patches
// link slots with it), skipping the dispatcher entry cost.
static constexpr bool TraceChainIn = true;

// Per-trace GPR access counts for the pin selection. Same format decode as regprof_mask 
// plus the FP loads'/stores' integer base register;.
// 
// FP / HW / CALL_PAL data paths don't touch the GPR file.
static void count_gpr_access(const uint32_t* w, uint32_t n, uint32_t* counts)
{
  auto touch = [&](int r) { if (r != 31) counts[r]++; };
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t ins = w[i];
    const uint32_t op  = ins >> 26;
    const int ra = (ins >> 21) & 0x1f, rb = (ins >> 16) & 0x1f, rc = ins & 0x1f;
    const bool islit = ((ins >> 12) & 1) != 0;
    switch (op) {
      case 0x10: case 0x11: case 0x12: case 0x13: case 0x1c:     // integer operate: Ra, Rc, Rb(if reg)
        touch(ra); touch(rc); if (!islit) touch(rb); break;
      case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c:     // LDA/LDAH + BWX ld/st: Ra, Rb(base)
      case 0x0d: case 0x0e: case 0x0f:
      case 0x28: case 0x29: case 0x2a: case 0x2b:                // int LDL/LDQ/LDx_L
      case 0x2c: case 0x2d: case 0x2e: case 0x2f:                // int STL/STQ/STx_C
      case 0x1a:                                                 // JMP/JSR/RET: Ra(link), Rb(target)
        touch(ra); touch(rb); break;
      case 0x30: case 0x34: case 0x38: case 0x39: case 0x3a:     // integer branches (incl BR/BSR link): Ra
      case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
        touch(ra); break;
      case 0x20: case 0x21: case 0x22: case 0x23:                // FP loads/stores: Rb is an integer base
      case 0x24: case 0x25: case 0x26: case 0x27:
        touch(rb); break;
      case 0x18: {                                               // MISC: RPCC/RC/RS write Ra
        const uint32_t f = ins & 0xffff;
        if (f == 0xc000 || f == 0xe000 || f == 0xf000) touch(ra);
        break;
      }
      default: break;
    }
  }
}

// Map a helper fn pointer to its HelperSet slot index.
static int helper_index(const CJitEngine::HelperSet& hs, const void* fn)
{
  static_assert(sizeof(CJitEngine::HelperSet) % sizeof(void*) == 0,
                "HelperSet must be a pure pointer table (indexed as void*[])");
  const void* const* p = reinterpret_cast<const void* const*>(&hs);
  const int n = (int) (sizeof(hs) / sizeof(void*));
  for (int i = 0; i < n; ++i) if (p[i] == fn) return i;
  return -1;
}

// memop slow path: the inline fast path's guards jump to `slow`, which is emitted in a
// cold tail after the block epilogue. The stub re-creates emit_call's marshalling + fault
// bail from captured PODs, then jumps back to `join`.
// Region IR: the fused span as a linear op array, built by decode, walked by emit. 
// Currently reproduces the direct emitter exactly, INSTR delegates to emit_op.
static constexpr bool RegionIR = true;

// cache the region's hottest unpinned guest GPR in R8 (caller-saved; spilled/reloaded only
// around helper calls, stored back when leaving the region).
static constexpr bool RegionCacheReg = false;   // measured flat: the mandatory spill/unbind at every
                                                // helper call cancels the load saved per use

// Non-PAL branch and fall-through exits as patched `jmp rel32`; false = poly-link slot walk.
static constexpr bool DirectPatch = true;

struct RegionOp {
  enum Kind : uint8_t { BLOCK_ENTER, INSTR, BLOCK_END, GUARD };
  Kind     kind;
  uint8_t  blk;     // fused block index (INSTR: emit_op's b/pal_block; GUARD: exit slot owner)
  uint32_t ins;     // INSTR: guest instruction word
  uint32_t idx;     // INSTR: index in block (fault bail); BLOCK_END: block length
  uint64_t pc;      // BLOCK_ENTER: fall-through PC default; GUARD: next fused block's tag
};

struct ColdMemStub {
  enum Kind { LOAD, STORE, FPMEM };
  Kind          kind;
  asmjit::Label slow;       // stub entry
  asmjit::Label watched_probe; // optional monitored write-page probe
  asmjit::Label watched_reuse; // marked tag matched with the previous slot/bias
  asmjit::Label raw_store;  // ordinary store, after address/translation checks
  bool          watched_store = false;
  asmjit::Label join;       // fast-path merge point
  asmjit::Label done;       // the block's shared bail exit
  void*         helper;     // jit_read / jit_write / jit_fp_read / jit_fp_write
  int           hidx = -1;  // helper's HelperSet index for the table call; -1 = imm64 fallback
  int           vol_bind = -1;  // caller-saved guest GPR pins to spill/reload around the helper
  int           vol_host = -1;
  int           vol_bind2 = -1;
  int           vol_host2 = -1;
  int           size_bits;  // LOAD/STORE operand size
  uint32_t      ins;        // instruction word (fault metadata)
  uint32_t      descr;      // FPMEM only: (fmt<<16)|size
  int           ra;         // LOAD dest / STORE value guest reg
  int           pin;        // host reg id bound to `ra`, or -1 = the regs[] memory slot
  int           slot;       // PALshadow-adjusted regs[] index for `ra`
  uint32_t      i;          // op index in the block
  uint64_t      fault_pc;   // resume PC on fault
};

// Lock the LL/SC bucket for an aligned RAM store. 
// Preserve RDX (address), R10 (bias/base), R11 (DPC slot) and guest pins. 
// Wached stores leave RCX = bucket.
// Unwatched stores jump to raw_store if supplied, else leave RCX = 0.
// First LL sets the watch byte after draining active CPUs.
static void emit_x64_ram_store_lock(asmjit::x86::Assembler& a,
    const CJitEngine::JitOffsets& off, uintptr_t ll_watch, uintptr_t ll_buckets,
    const asmjit::Label& slow, bool physical,
    const asmjit::Label* raw_store = nullptr)
{
  using namespace asmjit;
  const Label watched = a.new_label(), ready = a.new_label();
  if (physical) a.mov(x86::rax, x86::rdx);
  else {
    a.lea(x86::rax, x86::ptr(x86::r10, x86::rdx));
    a.sub(x86::rax, x86::qword_ptr(x86::rbp, off.dram_ptr));
  }
  a.shr(x86::rax, imm(6));              // per-line watch index
  a.mov(x86::rcx, imm(ll_watch));
  a.cmp(x86::byte_ptr(x86::rcx, x86::rax), imm(0)); // acquire under x86 TSO
  if (raw_store) {
    a.je(*raw_store);                 // unmonitored line on a monitored page
  } else {
    a.jne(watched);
    a.xor_(x86::ecx, x86::ecx);        // no bucket held
    a.jmp(ready);
  }
  a.bind(watched);
  // CSystem::LLBucket: 64-byte stride, sequence at 0, byte lock at 8.
  a.and_(x86::eax, imm(65535));
  a.shl(x86::rax, imm(6));              // bucket stride
  a.mov(x86::rcx, imm(ll_buckets));
  a.add(x86::rcx, x86::rax);
  a.mov(x86::eax, imm(1));
  a.xchg(x86::byte_ptr(x86::rcx, 8), x86::al); // acquire; XCHG is implicitly locked
  a.test(x86::al, x86::al);
  a.jnz(slow);
  a.bind(ready);
}

static void emit_x64_ram_store_unlock(asmjit::x86::Assembler& a,
    bool bucket_held = false)
{
  using namespace asmjit;
  const Label ready = a.new_label();
  if (!bucket_held) {
    a.test(x86::rcx, x86::rcx);
    a.jz(ready);
  }
  // Update the sequence while locked, then release after the RAM write.
  a.inc(x86::qword_ptr(x86::rcx));
  a.mov(x86::byte_ptr(x86::rcx, 8), imm(0)); // release under x86 TSO
  a.bind(ready);
}

static void emit_x64_integer_store(asmjit::x86::Assembler& a,
    int size_bits, int ra, int pin, int slot)
{
  using namespace asmjit;
  const x86::Gp value = pin >= 0 ? x86::gpq((uint32_t)pin) : x86::rax;
  if (pin < 0) {
    if (ra == 31) a.xor_(x86::eax, x86::eax);
    else          a.mov(x86::rax, x86::qword_ptr(x86::rbx, slot * 8));
  }
  if (size_bits == 64)      a.mov(x86::qword_ptr(x86::r10, x86::rdx), value);
  else if (size_bits == 32) a.mov(x86::dword_ptr(x86::r10, x86::rdx), value.r32());
  else if (size_bits == 16) a.mov(x86::word_ptr(x86::r10, x86::rdx), value.r16());
  else                     a.mov(x86::byte_ptr(x86::r10, x86::rdx), value.r8());
}

// Full-probe miss: R10 = expected tag (with alignment bits), R11 = DPC slot,
// RDX = VA. A watched hit preserves the slot/bias for the next memory op.
static void emit_watched_store_stub(asmjit::x86::Assembler& a,
    const CJitEngine::JitOffsets& off, uintptr_t ll_watch, uintptr_t ll_buckets,
    const ColdMemStub& s)
{
  if (!s.watched_store) return;
  using namespace asmjit;
  a.bind(s.watched_probe);
  a.or_(x86::r10, imm(off.dpc_ll_watch_tag));
  a.cmp(x86::qword_ptr(x86::rbp, x86::r11, 0,
      off.dpc_write_row + off.dpc_virt_page), x86::r10);
  a.jne(s.slow); // miss or unaligned store; RAM untouched
  a.mov(x86::r10, x86::qword_ptr(x86::rbp, x86::r11, 0,
      off.dpc_write_row + off.dpc_host_bias));
  if (s.watched_reuse.is_valid()) a.bind(s.watched_reuse);
  emit_x64_ram_store_lock(a, off, ll_watch, ll_buckets, s.slow, false,
      &s.raw_store);
  if (s.kind == ColdMemStub::STORE) {
    emit_x64_integer_store(a, s.size_bits, s.ra, s.pin, s.slot);
  } else { // FPMEM is raw STT; FPSTART/alignment checks already passed
    a.mov(x86::rax, x86::qword_ptr(x86::rbp, off.f_base + s.ra * 8));
    a.mov(x86::qword_ptr(x86::r10, x86::rdx), x86::rax);
  }
  emit_x64_ram_store_unlock(a, true);
  a.jmp(s.join);
}

// Emit one cold stub. Arg marshalling mirrors emit_call: non-immediate sources are placed before
// immediates so a size/selector immediate can't overwrite RDX 
#ifdef JIT_STATS
static uint64_t* g_cold_stub_counters = nullptr;   // [0] = helper bail, [1] = fault delivered (set per compile_block)
#endif
static void emit_cold_mem_stub(asmjit::x86::Assembler& a, const uint8_t* gpa,
                               const CJitEngine::JitOffsets& off, const ColdMemStub& s)
{
  using namespace asmjit;
  auto aq = [&](int k) { return x86::gpq(gpa[k]); };
  auto ad = [&](int k) { return x86::gpd(gpa[k]); };
  a.bind(s.slow);
  // the region-cached GPR's host reg is an ABI arg reg so we spill before using, and read
  // the store value from regs[] when it IS that reg (placement overwrites it). restore after.
  if (s.vol_bind >= 0)  a.mov(x86::qword_ptr(x86::rbx, s.vol_bind * 8),  x86::gpq((uint32_t) s.vol_host));
  if (s.vol_bind2 >= 0) a.mov(x86::qword_ptr(x86::rbx, s.vol_bind2 * 8), x86::gpq((uint32_t) s.vol_host2));
  if (s.kind != ColdMemStub::FPMEM)
    a.mov(x86::qword_ptr(x86::rsp, 48), x86::rdx); // preserve VA for DPC reuse
  a.mov(x86::r10, imm(s.fault_pc));
  a.mov(x86::qword_ptr(x86::rbp, off.state_current_pc), x86::r10);
  const bool val_in_vol = (s.vol_bind >= 0 && s.pin == s.vol_host)
                       || (s.vol_bind2 >= 0 && s.pin == s.vol_host2);
  a.mov(aq(0), x86::rbp);                                       // cpu
  if (aq(1).id() != x86::rdx.id()) a.mov(aq(1), x86::rdx);      // va (precomputed in RDX)
  switch (s.kind) {
    case ColdMemStub::LOAD:                                     // jit_read(cpu, va, descr, &out)
      a.lea(aq(3), x86::qword_ptr(x86::rsp, 32));               // &out slot
      a.mov(aq(2), imm(((uint64_t)s.ins << 32) | (uint32_t)s.size_bits));
      break;
    case ColdMemStub::STORE:                                    // jit_write(cpu, va, descr, value)
      if (s.ra == 31)                    a.xor_(ad(3), ad(3));  // value (R31 == 0)
      else if (s.pin >= 0 && !val_in_vol) a.mov(aq(3), x86::gpq((uint32_t) s.pin));
      else                                a.mov(aq(3), x86::qword_ptr(x86::rbx, s.slot * 8));
      a.mov(aq(2), imm(((uint64_t)s.ins << 32) | (uint32_t)s.size_bits));
      break;
    case ColdMemStub::FPMEM:                                    // jit_fp_read/write(cpu, va, fa, descr)
      a.mov(ad(2), imm((uint32_t) s.ra));
      a.mov(aq(3), imm(((uint64_t)s.ins << 32) | s.descr));
      break;
  }
  if (s.hidx >= 0 && off.helpers)
    a.call(x86::qword_ptr(x86::rbp, (int32_t) (off.helpers + s.hidx * 8)));   // via the CPU-resident table
  else { a.mov(x86::rax, imm((uint64_t) s.helper)); a.call(x86::rax); }
  if (s.vol_bind >= 0)  a.mov(x86::gpq((uint32_t) s.vol_host),  x86::qword_ptr(x86::rbx, s.vol_bind * 8));
  if (s.vol_bind2 >= 0) a.mov(x86::gpq((uint32_t) s.vol_host2), x86::qword_ptr(x86::rbx, s.vol_bind2 * 8));
  Label ok = a.new_label(), trapped = a.new_label();
  a.test(x86::eax, x86::eax);
  a.jz(ok);
  a.cmp(x86::eax, imm(2));
  a.je(trapped);
#ifdef JIT_STATS
  if (g_cold_stub_counters) { a.mov(x86::rax, imm((uint64_t) &g_cold_stub_counters[0])); a.inc(x86::qword_ptr(x86::rax)); }
#endif
  a.mov(x86::r10, imm(s.fault_pc));                             // fault: resume at this op
  a.mov(x86::qword_ptr(x86::rbp, off.state_pc), x86::r10);
  a.mov(x86::eax, imm(s.i));                                    // this iteration: i instrs done
  a.add(x86::eax, x86::r13d);                                   // + earlier chained iterations
  a.jmp(s.done);
  a.bind(trapped);                                             // fault delivered: keep PAL entry PC
#ifdef JIT_STATS
  if (g_cold_stub_counters) { a.mov(x86::rax, imm((uint64_t) &g_cold_stub_counters[1])); a.inc(x86::qword_ptr(x86::rax)); }
#endif
  a.mov(x86::eax, imm(s.i + 1));                               // count the faulting instruction once
  a.add(x86::eax, x86::r13d);
  a.jmp(s.done);
  a.bind(ok);
  if (s.kind != ColdMemStub::FPMEM) {
    // Recreate the hot path's {VA, slot, host bias} contract before joining the next
    // instruction. jit_read/jit_write filled the matching DRAM DPC entry on success.
    a.mov(x86::rdx, x86::qword_ptr(x86::rsp, 48));
    uint32_t shift = 0;
    while ((1u << shift) < off.dpc_stride) ++shift;
    if ((1u << shift) == off.dpc_stride && shift <= 13) {
      if (host_has_bmi2())
        a.rorx(x86::r11, x86::rdx, imm(13 - shift));
      else {
        a.mov(x86::r11, x86::rdx);
        if (shift != 13) a.shr(x86::r11, imm(13 - shift));
      }
      a.and_(x86::r11, imm((uint64_t)off.dpc_mask << shift));
    } else {
      a.mov(x86::r11, x86::rdx);
      a.shr(x86::r11, imm(13)); a.and_(x86::r11, imm(off.dpc_mask));
      a.imul(x86::r11, x86::r11, imm(off.dpc_stride));
    }
    a.add(x86::r11d, x86::dword_ptr(x86::rbp, (int32_t) off.dpc_bank));   // current mode's bank
    const int row = (s.kind == ColdMemStub::STORE) ? (int)off.dpc_write_row : 0;
    a.mov(x86::r10, x86::qword_ptr(x86::rbp, x86::r11, 0, row + (int)off.dpc_host_bias));

  }
  if (s.kind == ColdMemStub::LOAD) {
    // Result extraction + dest write must leave RAX = r[ra] to uphold the fast path's
    // value-forward contract at `join` (the next op may reuse RAX as Ra).
    if      (s.size_bits == 64) a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32));
    else if (s.size_bits == 32) a.movsxd(x86::rax, x86::dword_ptr(x86::rsp, 32));
    else if (s.size_bits == 16) a.movzx(x86::eax, x86::word_ptr(x86::rsp, 32));
    else                        a.movzx(x86::eax, x86::byte_ptr(x86::rsp, 32));
    if (s.pin >= 0) a.mov(x86::gpq((uint32_t) s.pin), x86::rax);
    else            a.mov(x86::qword_ptr(x86::rbx, s.slot * 8), x86::rax);
  }
  a.jmp(s.join);
}

// check_int and check_timers are adjacent bytes: one word compare covers both.
static void emit_pending_check(asmjit::x86::Assembler& a, const CJitEngine::JitOffsets& off,
                               const asmjit::Label& lbl)
{
  using namespace asmjit;
  if (off.check_timers == off.check_int + 1) {
    a.cmp(x86::word_ptr(x86::rbp, off.check_int), imm(0)); a.jne(lbl);
  } else {
    a.cmp(x86::byte_ptr(x86::rbp, off.check_int), imm(0)); a.jne(lbl);
    a.cmp(x86::byte_ptr(x86::rbp, off.check_timers), imm(0)); a.jne(lbl);
  }
}

void CJitEngine::emit_op(void* a_ptr, const uint8_t* gpa, void* done_ptr, const HelperSet& hs,
    bool pal_block, JitBlock* b, uint32_t ins, uint32_t i,
    RegAlloc& regalloc, void* cold_ptr, bool defer_pc)
{
    using namespace asmjit;
    x86::Assembler& a = *(x86::Assembler*)a_ptr;
    Label& done = *(Label*)done_ptr;
    std::vector<ColdMemStub>& cold = *(std::vector<ColdMemStub>*) cold_ptr;
    (void) cold;   // JIT_VERIFY builds keep every helper call inline and never record a stub
    // aliases so the moved if-chain references the helper names verbatim:
    void* read_helper = hs.read_helper;               void* write_helper = hs.write_helper;
    void* opcdec_helper = hs.opcdec_helper;           void* hw_mfpr_helper = hs.hw_mfpr_helper;
    void* hw_ld_helper = hs.hw_ld_helper;             void* hw_mtpr_helper = hs.hw_mtpr_helper;
    void* hw_st_helper = hs.hw_st_helper;             void* indirect_helper = hs.indirect_helper;
    void* read_locked_helper = hs.read_locked_helper; void* stc_helper = hs.stc_helper;
    void* misc_helper = hs.misc_helper;               void* read_vpte_helper = hs.read_vpte_helper;
    void* read_wchk_helper = hs.read_wchk_helper;     void* itof_helper = hs.itof_helper;
    void* ftoi_helper = hs.ftoi_helper;               void* fltl_helper = hs.fltl_helper;
    void* fp_read_helper = hs.fp_read_helper;         void* fp_write_helper = hs.fp_write_helper;
    void* fltv_helper = hs.fltv_helper;

    auto aq = [&](int k) { return x86::gpq(gpa[k]); };
    auto ad = [&](int k) { return x86::gpd(gpa[k]); };
    auto set_pc = [&](uint64_t pc_val) {
        a.mov(x86::r10, imm(pc_val));
        a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
        };
    auto pin_id = [&](int r) -> int {        // r -> its bound host reg id, or -1 = the state.r[] memory slot
        if (b->pal_shadow && ((r & 0xc) == 0x4)) return -1;
        return regalloc.host_of(r);
        };

    int ra = (ins >> 21) & 0x1F;
    int rb = (ins >> 16) & 0x1F;
    int rc = ins & 0x1F;
    bool islit = ((ins >> 12) & 1) != 0;
    uint32_t lit = (ins >> 13) & 0xFF;
    SafeOp op = classify(ins, pal_block, m_profile.ipr_read, m_profile.ipr_write,
        m_profile.pal, m_profile.hw_load, m_profile.hw_store);

    // A nearby ordinary memory op can reuse the previous op's translated page.
    const bool prev_dpc_live = regalloc.dpc_live;
    const int  prev_dpc_base = regalloc.dpc_base;
    const int  prev_dpc_disp = regalloc.dpc_disp;
    const bool prev_dpc_write = regalloc.dpc_write;
    const bool prev_dpc_force_align = regalloc.dpc_force_align;
    regalloc.dpc_live = false;

    // Preserve the address-cache working for operations that leave
    // RDX (VA), R11 (slot), and R10 (bias) untouched. 
    auto preserve_dpc = [&]() {
        regalloc.dpc_live = prev_dpc_live;
        regalloc.dpc_base = prev_dpc_base;
        regalloc.dpc_disp = prev_dpc_disp;
        regalloc.dpc_write = prev_dpc_write;
        regalloc.dpc_force_align = prev_dpc_force_align;
        };

    do {
        // MB: a locked RMW on our frame is a full barrier, cheaper than mfence.
        if (op == OP_NOP)    { preserve_dpc(); continue; }
        if (op == OP_MFENCE) {
#ifdef JIT_STATS
            a.mov(x86::rax, imm((uint64_t) &m_mb_exec)); a.inc(x86::qword_ptr(x86::rax));
            regalloc.rax_holds = -1;
#endif
            a.lock().or_(x86::dword_ptr(x86::rsp), imm(0));
            preserve_dpc(); continue;
        }

        // A result aimed at R31 is an architectural no-op, so emit nothing for it.
        const uint32_t raw_opcode = ins >> 26;
        if (rc == 31 && ((raw_opcode >= 0x10 && raw_opcode <= 0x13) || raw_opcode == 0x1c)) {
            preserve_dpc();
            continue;
        }

        // LDA/LDAH also cannot trap and write through Ra rather than Rc.
        if (ra == 31 && (op == OP_LDA || op == OP_LDAH)) {
            preserve_dpc();
            continue;
        }

        // Ordinary LD stuff to R31 interpreter just discards in the interp: 
        // no translation, fault, or MMIO read occurs. 
        // Locked loads are excluded because they must still establish load-lock state.
        if (ra == 31 && (op == OP_LDBU || op == OP_LDWU || op == OP_LDL
                     || op == OP_LDQ || op == OP_LDQ_U)) {
            preserve_dpc();
            continue;
        }

        // Value-forwarding: rax may still hold the guest reg the previous op computed. Capture that for
        // op1_rax's reuse, then default-invalidate; only mov_to_reg(_, rax) below re-marks what rax holds.
        const int prev_rax = regalloc.rax_holds;
        regalloc.rax_holds = -1;

        auto reg = [&](int r) {
            // PALshadow (RREG, AlphaCPU.h): in a PALmode block with SDE set, R4-7 and R20-23 map to
            // the shadow bank r[r+32]. 
            int idx = (b->pal_shadow && ((r & 0xc) == 0x4)) ? r + 32 : r;
            return x86::qword_ptr(x86::rbx, idx * 8);
            };
        // 32-bit (low-dword) view with the SAME shadow remap, for the 32-bit ALU ops (ADDL/SUBL).
        auto reg32 = [&](int r) {
            int idx = (b->pal_shadow && ((r & 0xc) == 0x4)) ? r + 32 : r;
            return x86::dword_ptr(x86::rbx, idx * 8);
            };
        // Pin-aware reg access: route through the pinned x86 reg when r is pinned, else the regs[]
        // memory slot via reg()/reg32(). Callers handle r==31 (it varies per site: 0, a displacement,
        // or skip). mov_to_reg writes the slot for any non-pinned r (R31 included, matching the old
        // unconditional stores -- nothing reads r[31] back).
        auto mov_from_reg = [&](const x86::Gp& dst, int r) {        // dst is 64-bit
            int p = pin_id(r);
            if (p >= 0) a.mov(dst, x86::gpq((uint32_t)p));
            else        a.mov(dst, reg(r));
            };
        auto mov_from_reg32 = [&](const x86::Gp& dst, int r) {      // dst is 32-bit
            int p = pin_id(r);
            if (p >= 0) a.mov(dst, x86::gpd((uint32_t)p));
            else        a.mov(dst, reg32(r));
            };
        auto mov_to_reg = [&](int r, const x86::Gp& src) {          // src is 64-bit
            int p = pin_id(r);
            if (p >= 0) a.mov(x86::gpq((uint32_t)p), src);
            else        a.mov(reg(r), src);
            if (src.id() == x86::rax.id() && r != 31) regalloc.rax_holds = r;   // rax now mirrors r[r]; forward it
            };
        auto op1_rax = [&]() {
            if (ra != 31 && prev_rax == ra) return;   // value-forward: rax already holds Ra (prev op's result)
            if (ra == 31) a.xor_(x86::eax, x86::eax);
            else          mov_from_reg(x86::rax, ra);
            };
        auto op2_rcx = [&]() {                  // operand2 (literal, or r[Rb] with r31=0)
            if (islit)         a.mov(x86::rcx, imm(lit));
            else if (rb == 31) a.xor_(x86::ecx, x86::ecx);
            else               mov_from_reg(x86::rcx, rb);
            };
        auto test_ra = [&](bool low_bit) {
            if (ra == 31) { a.xor_(x86::eax, x86::eax); return; } // ZF=1, SF=OF=0
            if (prev_rax == ra) {                                  // value-forward: rax holds Ra
                if (low_bit) a.test(x86::al, imm(1)); else a.test(x86::rax, x86::rax);
                return;
            }
            const int p = pin_id(ra);
            if (p >= 0) {
                const x86::Gp src = x86::gpq((uint32_t)p);
                if (low_bit) a.test(src, imm(1)); else a.test(src, src);
            } else {
                if (low_bit) a.test(reg(ra), imm(1)); else a.cmp(reg(ra), imm(0));
            }
        };
        // op2 as a DIRECT ALU source, folding away the mov-to-rcx: literal imm, R31 -> 0, a pinned host
        // reg, or the regs[] memory slot -- all valid two-operand ALU sources.
        auto op2op = [&]() -> Operand {
            if (islit)    return imm(lit);
            if (rb == 31) return imm(0);
            int p = pin_id(rb);
            if (p >= 0)   return x86::gpq((uint32_t) p);
            return reg(rb);
            };
        bool result_in_dest = false;
        auto emit_alu2 = [&](uint32_t instId) {
            const int dp = pin_id(rc);
            // Keep the result in its long-lived host pin when possible. 
            const bool clobbers_op2 = !islit && rb != 31 && rc == rb && rc != ra;
            if (rc != 31 && dp >= 0 && !clobbers_op2) {
                const x86::Gp dst = x86::gpq((uint32_t)dp);
                const int ap = (ra == 31) ? -1 : pin_id(ra);
                if (ra == 31) a.xor_(x86::gpd((uint32_t)dp), x86::gpd((uint32_t)dp));
                else if (ap != dp) mov_from_reg(dst, ra);
                a.emit(instId, dst, op2op());
                result_in_dest = true;
            } else {
                op1_rax();
                a.emit(instId, x86::rax, op2op());
            }
        };
        auto emit_compare = [&](uint32_t setId) {
            op1_rax();
            a.emit(x86::Inst::kIdCmp, x86::rax, op2op());
            const int dp = pin_id(rc);
            if (rc != 31 && dp >= 0) {
                const x86::Gp db = x86::gpb((uint32_t)dp);
                const x86::Gp dd = x86::gpd((uint32_t)dp);
                a.emit(setId, db);
                a.movzx(dd, db);
                result_in_dest = true;
            } else {
                a.emit(setId, x86::al);
                a.movzx(x86::eax, x86::al);
            }
        };

        // ABI-native helper call.
        // Each JitArg names an argument source; the k # source is placed in arg register k (aq/ad).
        // Non-immediate sources are emitted before immediates so a size/selector immediate can't
        // overwrite RDX while JA_VA (the only register-sourced argument) still needs it.
        enum JitArgKind { JA_CPU, JA_GP, JA_GPZ, JA_VA, JA_OUT, JA_R10, JA_I32, JA_I64 };
        struct JitArg { JitArgKind k; uint64_t v; };
#ifdef JIT_STATS
        // does this memop hit the same page as its last execution? how much loop-invariant 
        // page validation LICM could hoist? 
        // Stats-only; RDX = va, RAX/RCX dead here.
        auto emit_licm_probe = [&]() {
            if (!regalloc.licm_slots || regalloc.licm_n >= regalloc.licm_max) return;
            uint64_t* slot = &regalloc.licm_slots[regalloc.licm_n++];
            Label hit = a.new_label(), end = a.new_label();
            a.mov(x86::rax, x86::rdx);
            a.and_(x86::rax, imm(-0x2000));
            a.mov(x86::rcx, imm((uint64_t) slot));
            a.cmp(x86::qword_ptr(x86::rcx), x86::rax);
            a.je(hit);
            a.mov(x86::qword_ptr(x86::rcx), x86::rax);
            a.mov(x86::rax, imm((uint64_t) &m_licm_diff));
            a.inc(x86::qword_ptr(x86::rax));
            a.jmp(end);
            a.bind(hit);
            a.mov(x86::rax, imm((uint64_t) &m_licm_same));
            a.inc(x86::qword_ptr(x86::rax));
            a.bind(end);
            };
#endif
        auto emit_call = [&](void* fn, std::initializer_list<JitArg> as) {
            // R8 (the region-cached GPR) is an ABI ARG register on both platforms, so spill it
            // and unbind before using so arg reads must come from regs[], not a reg that
            // placement is about to overwrite. Rebound & restore after the call.
            const int vb = regalloc.vol_bind;
            const int vb2 = regalloc.vol_bind2;
            const int vh = (vb >= 0) ? regalloc.host[vb] : -1;
            const int vh2 = (vb2 >= 0) ? regalloc.host[vb2] : -1;
            if (vb >= 0) {
                a.mov(x86::qword_ptr(x86::rbx, vb * 8), x86::gpq((uint32_t) vh));
                regalloc.host[vb] = -1;
            }
            if (vb2 >= 0) {
                a.mov(x86::qword_ptr(x86::rbx, vb2 * 8), x86::gpq((uint32_t) vh2));
                regalloc.host[vb2] = -1;
            }
            auto place = [&](int k, const JitArg& s) {
                switch (s.k) {
                case JA_CPU: a.mov(aq(k), x86::rbp);                              break;  // cpu
                case JA_GP:  mov_from_reg(aq(k), (int)s.v);                      break;  // r[v]
                case JA_GPZ: if (s.v == 31) a.xor_(ad(k), ad(k));                         // r[v], or 0 if R31
                           else           mov_from_reg(aq(k), (int)s.v);       break;
                case JA_VA:  if (aq(k).id() != x86::rdx.id()) a.mov(aq(k), x86::rdx); break;  // va: precomputed in RDX
                case JA_OUT: a.lea(aq(k), x86::qword_ptr(x86::rsp, 32));          break;  // &out slot
                case JA_R10: a.mov(aq(k), x86::r10);                             break;  // value already in R10
                case JA_I32: a.mov(ad(k), imm((uint32_t)s.v));                  break;
                case JA_I64: a.mov(aq(k), imm((uint64_t)s.v));                  break;
                }
                };
            int k = 0; for (const JitArg& s : as) { if (s.k != JA_I32 && s.k != JA_I64) place(k, s); ++k; }
            k = 0;     for (const JitArg& s : as) { if (s.k == JA_I32 || s.k == JA_I64) place(k, s); ++k; }
            const int hi = helper_index(hs, fn);
            if (hi >= 0 && m_off.helpers)
                a.call(x86::qword_ptr(x86::rbp, (int32_t)(m_off.helpers + hi * 8)));   // via the CPU-resident table
            else { a.mov(x86::rax, imm((uint64_t)fn)); a.call(x86::rax); }
            if (vb >= 0) { regalloc.host[vb] = vh; a.mov(x86::gpq((uint32_t) vh), x86::qword_ptr(x86::rbx, vb * 8)); }
            if (vb2 >= 0) { regalloc.host[vb2] = vh2; a.mov(x86::gpq((uint32_t) vh2), x86::qword_ptr(x86::rbx, vb2 * 8)); }
            };

#ifndef JIT_VERIFY
        // Physical stores lock inline; watched virtual stores lock in the cold stub.
        auto emit_ram_store_lock = [&](const Label& slow, bool physical) {
            if (m_smp_memory)
                emit_x64_ram_store_lock(a, m_off, m_ll_watch, m_ll_buckets, slow, physical);
        };
        auto emit_ram_store_unlock = [&]() {
            if (m_smp_memory) emit_x64_ram_store_unlock(a);
        };
#endif

        auto emit_dpc_offset = [&]() {
            uint32_t shift = 0;
            while ((1u << shift) < m_off.dpc_stride) ++shift;
            if ((1u << shift) == m_off.dpc_stride && shift <= 13) {
                // ((va >> 13) & mask) << shift == (va >> (13-shift)) & (mask << shift).
                // Doing this with the extraction saves two instructions on every
                // inline memory access.
                if (host_has_bmi2())
                    a.rorx(x86::r11, x86::rdx, imm(13 - shift));
                else {
                    a.mov(x86::r11, x86::rdx);
                    if (shift != 13) a.shr(x86::r11, imm(13 - shift));
                }
                a.and_(x86::r11, imm((uint64_t)m_off.dpc_mask << shift));
            }
            else {
                a.mov(x86::r11, x86::rdx);
                a.shr(x86::r11, imm(13));
                a.and_(x86::r11, imm(m_off.dpc_mask));
                a.imul(x86::r11, x86::r11, imm(m_off.dpc_stride));
            }
            a.add(x86::r11d, x86::dword_ptr(x86::rbp, (int32_t) m_off.dpc_bank));   // current mode's bank
            };
        auto emit_address = [&](int disp) {
            if (rb == 31) {
                a.mov(x86::rdx, imm(disp));
                return;
            }
            const int bp = pin_id(rb);
            if (bp >= 0)
                a.lea(x86::rdx, x86::ptr(x86::gpq((uint32_t)bp), disp));
            else {
                mov_from_reg(x86::rdx, rb);
                if (disp) a.add(x86::rdx, imm(disp));
            }
        };

        // Memory-format loads: va = regs[Rb] + disp16.
        if (op == OP_LDQ || op == OP_LDL || op == OP_LDBU || op == OP_LDWU || op == OP_LDQ_U) {
            if (ra == 31) continue;            // LDx R31 is a NOP (interpreter skips the read)
            const int disp = (int)(int16_t)(ins & 0xFFFF);
            const int size_bits = (op == OP_LDQ || op == OP_LDQ_U) ? 64 : (op == OP_LDL) ? 32 : (op == OP_LDWU) ? 16 : 8;
            const int amask = (size_bits / 8) - 1;
            const bool force_align = (op == OP_LDQ_U);
            const uint64_t tag_mask = ~(uint64_t)0x1fff | (force_align ? 0 : (uint64_t)amask);
            const int dpc_delta = disp - prev_dpc_disp;
            const bool reuse_dpc = prev_dpc_live && !prev_dpc_write && prev_dpc_base == rb
                && prev_dpc_force_align == force_align && (!force_align || (dpc_delta & 7) == 0);
            if (reuse_dpc) {
                if (dpc_delta) a.add(x86::rdx, imm(dpc_delta));  // previous effective VA + displacement delta
            } else {
                emit_address(disp);                             // va -> RDX
                if (force_align) a.and_(x86::rdx, imm(~(uint64_t)7));
            }
#ifdef JIT_STATS
            emit_licm_probe();
#endif

#ifdef JIT_VERIFY
            // Slow path: jit_read(cpu, va, size, &out); on fault bail to `done` returning i
            // (0..i-1 committed). In JIT_VERIFY builds this is the ONLY path, so the helper's
            // replay keeps the differential check race-free.
            {
                emit_call(read_helper, { {JA_CPU, 0}, {JA_VA, 0}, {JA_I32, (uint64_t)size_bits}, {JA_OUT, 0} });
                Label ok = a.new_label();
                a.test(x86::eax, x86::eax);
                a.jz(ok);
                set_pc(b->tag + 4 * (uint64_t)i);               // resume at the faulting load
                a.mov(x86::eax, imm(i));                          // this iteration: i instrs done
                a.add(x86::eax, x86::r13d);                       // + earlier chained iterations
                a.jmp(done);
                a.bind(ok);
                if (op == OP_LDQ || op == OP_LDQ_U) a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32));  // LDQ/LDQ_U: full quad
                else if (op == OP_LDL)  a.movsxd(x86::rax, x86::dword_ptr(x86::rsp, 32));
                else if (op == OP_LDWU) a.movzx(x86::eax, x86::word_ptr(x86::rsp, 32));   // BWX: zero-extend
                else                    a.movzx(x86::eax, x86::byte_ptr(x86::rsp, 32));   // LDBU
                mov_to_reg(ra, x86::rax);
            }
#else
            // Inline fast path: aligned + data_page_cache[0][dpc_index(va)] hit + DRAM. Falls to the
            // helper on misalign / cache miss / MMIO. Mirrors jit_read's data-cache path. RDX = va
            // (preserved); R11 = slot byte offset = dpc_index(va)*stride; RAX/R10 scratch.
            Label slow = a.new_label();
            Label ldone = a.new_label();
            Label dpc_ready = a.new_label();
            if (reuse_dpc) {
                // R10/R11 still name the preceding site's host bias/page. If this VA remains on
                // that page and is naturally aligned, retain its host bias; otherwise perform
                // the fixed-site lookup.
                a.mov(x86::rax, x86::rdx);
                a.and_(x86::rax, imm(tag_mask));
                a.cmp(x86::qword_ptr(x86::rbp, x86::r11, 0, m_off.dpc_virt_page), x86::rax);
                a.je(dpc_ready);
            }
            emit_dpc_offset();
            a.mov(x86::r10, x86::rdx);
            a.and_(x86::r10, imm(tag_mask));
            a.cmp(x86::qword_ptr(x86::rbp, x86::r11, 0, m_off.dpc_virt_page), x86::r10); a.jne(slow);
            a.mov(x86::r10, x86::qword_ptr(x86::rbp, x86::r11, 0, m_off.dpc_host_bias));
            a.bind(dpc_ready);
            const int dp = pin_id(ra);
            if (dp >= 0) {
                const x86::Gp dq = x86::gpq((uint32_t)dp), dd = x86::gpd((uint32_t)dp);
                if (op == OP_LDQ || op == OP_LDQ_U) a.mov(dq, x86::qword_ptr(x86::r10, x86::rdx));
                else if (op == OP_LDL)  a.movsxd(dq, x86::dword_ptr(x86::r10, x86::rdx));
                else if (op == OP_LDWU) a.movzx(dd, x86::word_ptr(x86::r10, x86::rdx));
                else                    a.movzx(dd, x86::byte_ptr(x86::r10, x86::rdx));
            } else {
                if (op == OP_LDQ || op == OP_LDQ_U) a.mov(x86::rax, x86::qword_ptr(x86::r10, x86::rdx));  // LDQ/LDQ_U: full quad
                else if (op == OP_LDL)  a.movsxd(x86::rax, x86::dword_ptr(x86::r10, x86::rdx));
                else if (op == OP_LDWU) a.movzx(x86::eax, x86::word_ptr(x86::r10, x86::rdx));   // BWX: zero-extend
                else                    a.movzx(x86::eax, x86::byte_ptr(x86::r10, x86::rdx));   // LDBU
                mov_to_reg(ra, x86::rax);
            }
            a.bind(ldone);   // fast path falls through; the slow path is a cold-tail stub jumping back here
            {
                ColdMemStub s{};
                s.kind = ColdMemStub::LOAD;  s.slow = slow;  s.join = ldone;  s.done = done;
                s.helper = read_helper;      s.hidx = helper_index(hs, read_helper);
                s.size_bits = size_bits;
                s.ins = ins;
                s.ra = ra;  s.pin = pin_id(ra);
                s.slot = (b->pal_shadow && ((ra & 0xc) == 0x4)) ? ra + 32 : ra;   // PALshadow remap (see reg())
                s.i = i;    s.fault_pc = b->tag + 4 * (uint64_t) i;
                s.vol_bind = regalloc.vol_bind; s.vol_host = (s.vol_bind >= 0) ? regalloc.host[s.vol_bind] : -1;
                s.vol_bind2 = regalloc.vol_bind2; s.vol_host2 = (s.vol_bind2 >= 0) ? regalloc.host[s.vol_bind2] : -1;
                cold.push_back(s);
            }
            // A load that replaces its own address base cannot feed the next address.
            regalloc.dpc_live = (ra != rb);
            regalloc.dpc_base = rb;
            regalloc.dpc_disp = disp;
            regalloc.dpc_write = false;
            regalloc.dpc_force_align = force_align;
#endif
            continue;
        }

        // Memory-format stores: MEM[regs[Rb] + disp16] = regs[Ra]. Inline fast path mirrors the load
        // path against the WRITE cache [1] (a hit already passed the write-permission + FOW check, so
        // the page is writable); falls to jit_write on misalign / cache miss / MMIO. In verify the
        // helper is the only path -- it compares the store against the interpreter's recorded one.
        if (op == OP_STL || op == OP_STQ || op == OP_STB || op == OP_STW || op == OP_STQ_U) {
            const int disp = (int)(int16_t)(ins & 0xFFFF);
            const int size_bits = (op == OP_STQ || op == OP_STQ_U) ? 64 : (op == OP_STL) ? 32 : (op == OP_STW) ? 16 : 8;
            const int amask = (size_bits / 8) - 1;
            const bool force_align = (op == OP_STQ_U);
            const uint64_t tag_mask = ~(uint64_t)0x1fff | (force_align ? 0 : (uint64_t)amask);
            const int dpc_delta = disp - prev_dpc_disp;
            const bool reuse_dpc = prev_dpc_live && prev_dpc_write && prev_dpc_base == rb
                && prev_dpc_force_align == force_align && (!force_align || (dpc_delta & 7) == 0);
            if (reuse_dpc) {
                if (dpc_delta) a.add(x86::rdx, imm(dpc_delta));              // previous effective VA + displacement delta
            } else {
                emit_address(disp);                                          // va -> RDX (preserved for helper)
                if (force_align) a.and_(x86::rdx, imm(~(uint64_t)7));
            }
#ifdef JIT_STATS
            emit_licm_probe();
#endif

#ifdef JIT_VERIFY
            {
                emit_call(write_helper, { {JA_CPU, 0}, {JA_VA, 0}, {JA_I32, (uint64_t)size_bits}, {JA_GPZ, (uint64_t)ra} });  // jit_write(cpu, va, size, value)
                Label ok = a.new_label();
                a.test(x86::eax, x86::eax);
                a.jz(ok);
                set_pc(b->tag + 4 * (uint64_t)i);                            // resume at the faulting store
                a.mov(x86::eax, imm(i));                                       // this iteration: i instrs done
                a.add(x86::eax, x86::r13d);                                    // + earlier chained iterations
                a.jmp(done);
                a.bind(ok);
            }
#else
            // Inline fast path: aligned + data_page_cache[1][dpc_index(va)] hit + DRAM. RDX = va
            // (preserved for the helper); R11 = write-cache slot byte offset; RAX/R10/R9 scratch.
            Label slow = a.new_label();
            Label sdone = a.new_label();
            Label dpc_ready = a.new_label();
            const Label watched_page = a.new_label();
            const Label watched_reuse = reuse_dpc && m_smp_memory ? a.new_label() : Label();
            if (reuse_dpc) {
                // The preceding store's write-cache translation remains valid on the same page.
                a.mov(x86::rax, x86::rdx);
                a.and_(x86::rax, imm(tag_mask));
                a.cmp(x86::qword_ptr(x86::rbp, x86::r11, 0,
                    m_off.dpc_write_row + m_off.dpc_virt_page), x86::rax);
                a.je(dpc_ready);
                if (watched_reuse.is_valid()) {
                    a.or_(x86::rax, imm(m_off.dpc_ll_watch_tag));
                    a.cmp(x86::qword_ptr(x86::rbp, x86::r11, 0,
                        m_off.dpc_write_row + m_off.dpc_virt_page), x86::rax);
                    a.je(watched_reuse); // same page + aligned: keep slot/bias
                }
            }
            emit_dpc_offset();
            a.mov(x86::r10, x86::rdx);
            a.and_(x86::r10, imm(tag_mask));
            a.cmp(x86::qword_ptr(x86::rbp, x86::r11, 0,
                m_off.dpc_write_row + m_off.dpc_virt_page), x86::r10);
            a.jne(m_smp_memory ? watched_page : slow);
            a.mov(x86::r10, x86::qword_ptr(x86::rbp, x86::r11, 0,
                m_off.dpc_write_row + m_off.dpc_host_bias));
            a.bind(dpc_ready);
            const int sp = (ra == 31) ? -1 : pin_id(ra);
            const int slot = (b->pal_shadow && ((ra & 0xc) == 0x4)) ? ra + 32 : ra;
            // Plain tags need no LL/SC lock. The cold path also lands here for
            // unwatched lines on watched pages. First LL updates all page aliases
            // after draining active CPUs.
            emit_x64_integer_store(a, size_bits, ra, sp, slot);
            a.bind(sdone);   // fast path falls through; the slow path is a cold-tail stub jumping back here
            {
                ColdMemStub s{};
                s.kind = ColdMemStub::STORE;  s.slow = slow;  s.join = sdone;  s.done = done;
                s.watched_probe = watched_page; s.watched_store = m_smp_memory;
                s.watched_reuse = watched_reuse;
                s.raw_store = dpc_ready;
                s.helper = write_helper;      s.hidx = helper_index(hs, write_helper);
                s.size_bits = size_bits;
                s.ins = ins;
                s.ra = ra;  s.pin = sp; s.slot = slot;
                s.i = i;    s.fault_pc = b->tag + 4 * (uint64_t) i;
                s.vol_bind = regalloc.vol_bind; s.vol_host = (s.vol_bind >= 0) ? regalloc.host[s.vol_bind] : -1;
                s.vol_bind2 = regalloc.vol_bind2; s.vol_host2 = (s.vol_bind2 >= 0) ? regalloc.host[s.vol_bind2] : -1;
                cold.push_back(s);
            }
            regalloc.dpc_live = true;
            regalloc.dpc_base = rb;
            regalloc.dpc_disp = disp;
            regalloc.dpc_write = true;
            regalloc.dpc_force_align = force_align;
#endif
            continue;
        }

        // FP memory (LDS/LDT/STS/STT + VAX LDF/LDG/STF/STG): f[Fa] <-> MEM[Rb+disp16] with FPSTART.
        // LDT/STT are raw 8B -> inline fast path; the converting forms (S/F/G) go through the helper. Verify uses the
        // helper only (FP loads replay the logged f-value, FP stores compare via jit_write's store-log).
        if (op == OP_LDT || op == OP_LDS || op == OP_STT || op == OP_STS ||
            op == OP_LDF || op == OP_LDG || op == OP_STF || op == OP_STG) {
            const bool isload = (op == OP_LDT || op == OP_LDS || op == OP_LDF || op == OP_LDG);
            const bool israw = (op == OP_LDT || op == OP_STT);   // T-format: no conversion -> inline-able
            const int  fa = ra;                               // Fa = (ins>>21)&0x1f (FP regs: no shadow remap)
            // fmt: 0=T raw, 1=S ieee, 2=F vax, 3=G vax. S/F are 32-bit in memory; T/G are 64-bit.
            const uint32_t fmt = (op == OP_LDS || op == OP_STS) ? 1u : (op == OP_LDF || op == OP_STF) ? 2u
                : (op == OP_LDG || op == OP_STG) ? 3u : 0u;
            const int  size_bits = (fmt == 1 || fmt == 2) ? 32 : 64;
            const int  disp = (int)(int16_t)(ins & 0xFFFF);
            const uint32_t descr = (fmt << 16) | (uint32_t)size_bits;   // fmt<<16 | size

            if (isload && fa == 31) continue;   // LDT/LDS f31: interp skips the read (NOP)

            emit_address(disp);                                              // va -> RDX (preserved for helper)
#ifdef JIT_STATS
            emit_licm_probe();
#endif

            auto emit_helper = [&]() {
#ifndef JIT_VERIFY
                a.mov(x86::r10, imm(b->tag + 4 * (uint64_t)i));
                a.mov(x86::qword_ptr(x86::rbp, m_off.state_current_pc), x86::r10);
                emit_call(isload ? fp_read_helper : fp_write_helper,
                    { {JA_CPU, 0}, {JA_VA, 0}, {JA_I32, (uint64_t)fa},
                      {JA_I64, ((uint64_t)ins << 32) | descr} });
#else
                emit_call(isload ? fp_read_helper : fp_write_helper,
                    { {JA_CPU, 0}, {JA_VA, 0}, {JA_I32, (uint64_t)fa}, {JA_I32, (uint64_t)descr} });
#endif
                Label ok = a.new_label();
                a.test(x86::eax, x86::eax);
                a.jz(ok);
#ifndef JIT_VERIFY
                Label trapped = a.new_label();
                a.cmp(x86::eax, imm(2));
                a.je(trapped);
#endif
                set_pc(b->tag + 4 * (uint64_t)i);                            // resume at the faulting FP mem op
                a.mov(x86::eax, imm(i));
                a.add(x86::eax, x86::r13d);
                a.jmp(done);
#ifndef JIT_VERIFY
                a.bind(trapped);                                          // keep PAL PC and leave Fa unchanged
                a.mov(x86::eax, imm(i + 1));
                a.add(x86::eax, x86::r13d);
                a.jmp(done);
#endif
                a.bind(ok);
                };

#ifdef JIT_VERIFY
            emit_helper();
#else
            if (!israw) { emit_helper(); continue; }

            // Inline fast path for LDT/STT: FPSTART (fpen gate + exc_sum=0), then the data-cache hit.
            // misalign / cache miss / MMIO / fpen==0 fall to the helper. R11 = slot byte offset; RAX/R10/R9 scratch.
            Label slow = a.new_label();
            Label fdone = a.new_label();
            const Label watched_page = a.new_label();
            const Label raw_store = a.new_label();
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0));                          a.je(slow);   // fpen==0 -> FEN trap
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));                                    // exc_sum = 0
            a.test(x86::dl, imm(7));                                                     a.jnz(slow);  // 8-byte aligned
            emit_dpc_offset();
            const int dpc_row = isload ? 0 : (int)m_off.dpc_write_row;
            a.mov(x86::r10, x86::rdx);
            a.and_(x86::r10, imm(-0x2000));
            a.cmp(x86::qword_ptr(x86::rbp, x86::r11, 0, dpc_row + (int)m_off.dpc_virt_page), x86::r10);
            a.jne(!isload && m_smp_memory ? watched_page : slow);
            a.mov(x86::r10, x86::qword_ptr(x86::rbp, x86::r11, 0, dpc_row + (int)m_off.dpc_host_bias));
            a.bind(raw_store);
            if (isload) {                                                               // LDT: f[fa] = MEM[phys]
                a.mov(x86::rax, x86::qword_ptr(x86::r10, x86::rdx));
                a.mov(x86::qword_ptr(x86::rbp, m_off.f_base + fa * 8), x86::rax);
            }
            else {                                                                    // STT: MEM[phys] = f[fa]
                a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + fa * 8));
                a.mov(x86::qword_ptr(x86::r10, x86::rdx), x86::rax);
            }
            a.bind(fdone);   // fast path falls through; the slow path is a cold-tail stub jumping back here
            {
                ColdMemStub s{};
                s.kind = ColdMemStub::FPMEM;  s.slow = slow;  s.join = fdone;  s.done = done;
                s.watched_probe = watched_page; s.watched_store = !isload && m_smp_memory;
                s.raw_store = raw_store;
                s.helper = isload ? fp_read_helper : fp_write_helper;
                s.hidx = helper_index(hs, s.helper);
                s.ins = ins;  s.descr = descr;  s.ra = fa;  s.pin = -1;  s.slot = 0;   // helper owns the f[] access
                s.i = i;          s.fault_pc = b->tag + 4 * (uint64_t) i;
                s.vol_bind = regalloc.vol_bind; s.vol_host = (s.vol_bind >= 0) ? regalloc.host[s.vol_bind] : -1;
                s.vol_bind2 = regalloc.vol_bind2; s.vol_host2 = (s.vol_bind2 >= 0) ? regalloc.host[s.vol_bind2] : -1;
                cold.push_back(s);
            }
#endif
            continue;
        }

        // Store-conditional STL_C/STQ_C (0x2e/0x2f): conditionally store Ra; Ra = 1 (success) / 0 (fail).
        // jit_stc consumes the LL monitor + does the host CAS (production), or compares against the
        // interpreter's logged outcome (verify). 
        if (op == OP_STL_C || op == OP_STQ_C) {
            const int disp = (int)(int16_t)(ins & 0xFFFF);
            const int size_bits = (op == OP_STQ_C) ? 64 : 32;
            emit_address(disp);                                              // va -> RDX
#ifndef JIT_VERIFY
            a.mov(x86::r10, imm(b->tag + 4 * (uint64_t)i));
            a.mov(x86::qword_ptr(x86::rbp, m_off.state_current_pc), x86::r10);
            emit_call(stc_helper, { {JA_CPU, 0}, {JA_VA, 0},
                {JA_I64, ((uint64_t)ins << 32) | (uint32_t)size_bits}, {JA_GP, (uint64_t)ra} });
#else
            emit_call(stc_helper, { {JA_CPU, 0}, {JA_VA, 0}, {JA_I32, (uint64_t)size_bits}, {JA_GP, (uint64_t)ra} });
#endif
            Label nobail = a.new_label();
            a.test(x86::eax, imm(0x300));                                   // 0x100 = retry; 0x200 = PAL entered
            a.jz(nobail);
#ifndef JIT_VERIFY
            Label trapped = a.new_label();
            a.cmp(x86::eax, imm(0x200));
            a.je(trapped);
#endif
            set_pc(b->tag + 4 * (uint64_t)i);                              // resume at the faulting STx_C
            a.mov(x86::eax, imm(i));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
#ifndef JIT_VERIFY
            a.bind(trapped);                                              // keep PAL PC; Ra remains the store value
            a.mov(x86::eax, imm(i + 1));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
#endif
            a.bind(nobail);
            mov_to_reg(ra, x86::rax);                                      // Ra = success(1) / fail(0)
            continue;
        }

        // HW_LD: normalized physical loads read DRAM inline; virtual loads use the
        // translation helper with the family-selected access flags and displacement.
        if (op == OP_HW_LDL || op == OP_HW_LDQ || op == OP_HW_LD_VIRT) {
            const auto load = alpha_decode_hw_load(m_profile.hw_load, ins);
            const bool virt = load.address == AlphaHWLoadAddress::Virtual;
            if (ra == 31 && !virt && load.discard_safe) continue;   // virtual R31 forms are fault probes
            const int size_bits = static_cast<int>(load.size_bits);
            const uint32_t descr = virt ? alpha_hw_load_helper_descriptor(load) : load.size_bits;
            emit_address(load.displacement);                // physical or virtual address -> RDX
#ifndef JIT_VERIFY
            Label physical_done;
            if (!virt) {
                const Label physical_slow = a.new_label();
                physical_done = a.new_label();
                // DRAM is page-aligned. Test the original address so MMIO reaches
                // the helper unchanged; HW_LD rounds down without alignment faults.
                a.cmp(x86::rdx, x86::qword_ptr(x86::rbp, m_off.dram_size));
                a.jae(physical_slow);
                a.and_(x86::rdx, imm(-(size_bits / 8)));
                a.mov(x86::r10, x86::qword_ptr(x86::rbp, m_off.dram_ptr));
                if (size_bits == 32) a.movsxd(x86::rax, x86::dword_ptr(x86::r10, x86::rdx));
                else                 a.mov(x86::rax, x86::qword_ptr(x86::r10, x86::rdx));
                mov_to_reg(ra, x86::rax);
                a.jmp(physical_done);
                a.bind(physical_slow);
            }
#endif
            if (virt) {
                a.mov(x86::r10, imm(b->tag + 4 * (uint64_t)i));
                a.mov(x86::qword_ptr(x86::rbp, m_off.state_current_pc), x86::r10);
            }
            emit_call(virt ? read_vpte_helper : hw_ld_helper,
                { {JA_CPU, 0}, {JA_VA, 0},
                  {virt ? JA_I64 : JA_I32,
                   virt ? ((uint64_t)ins << 32) | descr : descr}, {JA_OUT, 0} });
            Label ok = a.new_label(), trapped = a.new_label();
            a.test(x86::eax, x86::eax);
            a.jz(ok);
            if (virt) {
                a.cmp(x86::eax, imm(2));
                a.je(trapped);
            }
            set_pc(b->tag + 4 * (uint64_t)i);                       // resume at the faulting HW_LD
            a.mov(x86::eax, imm(i));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            if (virt) {
                a.bind(trapped);                                      // fault delivered: keep PAL entry PC
                a.mov(x86::eax, imm(i + 1));                          // count the HW_LD exactly once
                a.add(x86::eax, x86::r13d);
                a.jmp(done);
            }
            a.bind(ok);
            // HW_LDL SIGN-extends the longword to canonical form (QEMU gen_hw_ld uses MO_LESL; the EV68CB
            // HRM is silent but the Alpha longword-canonical rule applies, same as LDL). NOTE: the interp's
            // DO_HW_LDL zero-extends -- that is the bug, fixed in cpu_pal.h to match this.
            if (ra != 31) {
                if (size_bits == 32) a.movsxd(x86::rax, x86::dword_ptr(x86::rsp, 32));
                else                 a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32));
                mov_to_reg(ra, x86::rax);
            }
#ifndef JIT_VERIFY
            if (!virt) a.bind(physical_done);
#endif
            continue;
        }

        // Load-locked LDL_L/LDQ_L (0x2a/0x2b): Ra = MEM[Rb + disp16] AND establish the LL/SC exclusive
        // monitor. 
        if (op == OP_LDL_L || op == OP_LDQ_L) {
            const int disp = (int)(int16_t)(ins & 0xFFFF);
            const int size_bits = (op == OP_LDQ_L) ? 64 : 32;
            emit_address(disp);                                      // va -> RDX
            a.mov(x86::r10, imm(b->tag + 4 * (uint64_t)i));
            a.mov(x86::qword_ptr(x86::rbp, m_off.state_current_pc), x86::r10);
            emit_call(read_locked_helper, { {JA_CPU, 0}, {JA_VA, 0},
                {JA_I64, ((uint64_t)ins << 32) | (uint32_t)size_bits}, {JA_OUT, 0} });  // jit_read_locked(cpu, va, descr, &out)
            Label ok = a.new_label(), trapped = a.new_label();
            a.test(x86::eax, x86::eax);
            a.jz(ok);
            a.cmp(x86::eax, imm(2));
            a.je(trapped);
            set_pc(b->tag + 4 * (uint64_t)i);                       // resume at the faulting LDx_L
            a.mov(x86::eax, imm(i));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            a.bind(trapped);                                        // fault delivered: keep native-PAL entry PC
            a.mov(x86::eax, imm(i + 1));                            // count the LDx_L exactly once
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            a.bind(ok);
            a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32));           // *out already sign-extended by the helper
            mov_to_reg(ra, x86::rax);
            continue;
        }

        // HW_MTPR: the family has approved this write for a compiled helper call.
        if (op == OP_HW_MTPR) {
            emit_call(hw_mtpr_helper, { {JA_CPU, 0}, {JA_I32, (uint64_t)ins}, {JA_GPZ, (uint64_t)rb} });  // jit_hw_mtpr(cpu, instruction, value)
            continue;
        }

        // The family marks writes that change compiled assumptions as terminators.
        // Run the helper, publish the next PC and redispatch under the new state.
        if (op == OP_HW_MTPR_TERM) {
            emit_call(hw_mtpr_helper, { {JA_CPU, 0}, {JA_I32, (uint64_t)ins}, {JA_GPZ, (uint64_t)rb} });
            set_pc(b->tag + 4 * (uint64_t)(i + 1));                // next PC -> R10 + state.pc (PALmode bit preserved)
            continue;
        }

        // HW_ST physical: the family selects the width and displacement; the source is Ra.
        // DRAM writes inline; the helper retains verification replay and MMIO retry.
        if (op == OP_HW_STL || op == OP_HW_STQ) {
            const auto store = alpha_decode_hw_store(m_profile.hw_store, ins);
            const int size_bits = static_cast<int>(store.size_bits);
            emit_address(store.displacement);                         // phys addr -> RDX
#ifndef JIT_VERIFY
            const Label physical_slow = a.new_label(), physical_done = a.new_label();
            a.cmp(x86::rdx, x86::qword_ptr(x86::rbp, m_off.dram_size));
            a.jae(physical_slow);
            a.and_(x86::rdx, imm(-(size_bits / 8)));
            a.mov(x86::r10, x86::qword_ptr(x86::rbp, m_off.dram_ptr));
            emit_ram_store_lock(physical_slow, true);
            const int sp = (ra == 31) ? -1 : pin_id(ra);
            if (sp >= 0) {
                if (size_bits == 32) a.mov(x86::dword_ptr(x86::r10, x86::rdx), x86::gpd((uint32_t)sp));
                else                 a.mov(x86::qword_ptr(x86::r10, x86::rdx), x86::gpq((uint32_t)sp));
            } else {
                if (ra == 31) a.xor_(x86::eax, x86::eax); else mov_from_reg(x86::rax, ra);
                if (size_bits == 32) a.mov(x86::dword_ptr(x86::r10, x86::rdx), x86::eax);
                else                 a.mov(x86::qword_ptr(x86::r10, x86::rdx), x86::rax);
            }
            emit_ram_store_unlock();
            a.jmp(physical_done);
            a.bind(physical_slow);
#endif
            emit_call(hw_st_helper, { {JA_CPU, 0}, {JA_VA, 0}, {JA_I32, (uint64_t)size_bits}, {JA_GPZ, (uint64_t)ra} });  // jit_write_phys(cpu, phys, size, value)
            Label ok = a.new_label();
            a.test(x86::eax, x86::eax);
            a.jz(ok);
            set_pc(b->tag + 4 * (uint64_t)i);                       // resume at the faulting HW_ST
            a.mov(x86::eax, imm(i));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            a.bind(ok);
#ifndef JIT_VERIFY
            a.bind(physical_done);
#endif
            continue;
        }

        // Load-address: Ra = Rb + disp16 (LDA) or Rb + (disp16 << 16) (LDAH). Pure register
        // arithmetic. R31 dest discards the result (a NOP).
        if (op == OP_LDA || op == OP_LDAH) {
            if (ra == 31) continue;
            int64_t d = (int64_t)(int16_t)(ins & 0xFFFF);
            if (op == OP_LDAH) d *= 65536;
            const int dp = pin_id(ra), bp = (rb == 31) ? -1 : pin_id(rb);
            if (dp >= 0) {
                const x86::Gp dst = x86::gpq((uint32_t)dp);
                if (rb == 31) a.mov(dst, imm(d));
                else {
                    if (dp != bp) mov_from_reg(dst, rb);
                    if (d) a.add(dst, imm(d));
                }
            } else if (ra == rb) {
                // The unpinned in-place form can update the canonical regs[] slot directly.
                if (d) a.add(reg(ra), imm(d));
            } else {
                if (rb == 31) a.mov(x86::rax, imm(d));
                else if (bp >= 0 && d) a.lea(x86::rax, x86::ptr(x86::gpq((uint32_t)bp), (int32_t)d));
                else { mov_from_reg(x86::rax, rb); if (d) a.add(x86::rax, imm(d)); }
                mov_to_reg(ra, x86::rax);
            }
            continue;
        }

        // HW_MFPR (0x19, PALmode): the family selects the IPR from the full instruction.
        // Plain fault-reporting fields read inline; derived/asynchronous registers
        // and verification retain the helper. Destination routing honors PALshadow.
        if (op == OP_HW_MFPR) {
#ifndef JIT_VERIFY
            const auto descriptor = m_profile.ipr_read.describe(ins);
            uint32_t field_offset = 0;
            bool direct_field = ra != 31 && descriptor.discard_safe;
            switch (descriptor.kind) {
            case AlphaIPRReadKind::ExceptionAddress: field_offset = m_off.exc_addr; break;
            case AlphaIPRReadKind::ExceptionSummary: field_offset = m_off.exc_sum; break;
            case AlphaIPRReadKind::PalBase: field_offset = m_off.pal_base; break;
            case AlphaIPRReadKind::MemoryFaultStatus: field_offset = m_off.mm_stat; break;
            case AlphaIPRReadKind::FaultAddress: field_offset = m_off.fault_va; break;
            default: direct_field = false; break;
            }
            if (direct_field)
                a.mov(x86::rax, x86::qword_ptr(x86::rbp, field_offset));
            else
#endif
                emit_call(hw_mfpr_helper, { {JA_CPU, 0}, {JA_I32, (uint64_t)ins}, {JA_GPZ, (uint64_t)ra} });  // -> RAX = IPR value
            if (ra != 31)
                mov_to_reg(ra, x86::rax);                      // Ra = value (reg() applies the PALshadow remap)
            // A family may require the read's side effect even when Ra discards it.
            continue;
        }

        // MISC state reads RPCC/RC/RS (0x18): Ra = jit_misc(cpu, sel). The helper reads the cycle
        // counter / interrupt flag (clearing or setting it) in production, and replays the interp
        // pass's value in verify. Dest is Ra (not Rc); classify gated Ra!=31, so a store always happens.
        if (op == OP_RPCC || op == OP_RC || op == OP_RS) {
            const int sel = (op == OP_RPCC) ? 0 : (op == OP_RC) ? 1 : 2;
            emit_call(misc_helper, { {JA_CPU, 0}, {JA_I32, (uint64_t)sel} });  // -> RAX = value (replayed in verify); RC/RS also side-effect the flag
            if (ra != 31) mov_to_reg(ra, x86::rax);          // Ra = value (reg() remap); Ra==31 RC/RS = flag side-effect only, discard
            continue;
        }

        // ITOFx (0x14): f[Fc] = fmt(Ra). jit_itof mirrors FPSTART (fpen -> FEN trap = bail, exc_sum=0);
        // the verify compares the FP file via its snapshot.
        if (op == OP_ITOFS || op == OP_ITOFF || op == OP_ITOFT) {
            emit_call(itof_helper, { {JA_CPU, 0}, {JA_I32, (uint64_t)rc}, {JA_GPZ, (uint64_t)ra},
                      {JA_I32, (uint64_t)(op == OP_ITOFS ? 1 : (op == OP_ITOFF) ? 2 : 0)} });  // jit_itof(cpu, fc, value, fmt)
            Label ok = a.new_label();
            a.test(x86::eax, x86::eax);
            a.jz(ok);
            set_pc(b->tag + 4 * (uint64_t)i);               // FEN trap: resume here in the interpreter
            a.mov(x86::eax, imm(i));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            a.bind(ok);
            continue;
        }

        // FTOIx (0x1c): Rc = fmt(f[Fa]). Same FPSTART bail shape; dest is a GPR (verify-compared).
        if (op == OP_FTOIS || op == OP_FTOIT) {
            emit_call(ftoi_helper, { {JA_CPU, 0}, {JA_I32, (uint64_t)ra},
                      {JA_I32, (uint64_t)(op == OP_FTOIS ? 1 : 0)}, {JA_OUT, 0} });  // jit_ftoi(cpu, fa, fmt, &out)
            Label ok = a.new_label();
            a.test(x86::eax, x86::eax);
            a.jz(ok);
            set_pc(b->tag + 4 * (uint64_t)i);               // FEN trap: resume here in the interpreter
            a.mov(x86::eax, imm(i));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            a.bind(ok);
            a.mov(x86::rax, x86::qword_ptr(x86::rsp, 32));
            mov_to_reg(rc, x86::rax);                        // Rc (reg() applies the PALshadow remap)
            continue;
        }

        // CVTQT/CVTQS (0x16): f[Fc] = (double|single)(s64) f[Fb] via SSE. Inline the steady-state
        // common case; bail to the interpreter (it owns rounding/traps/FPCR) on any edge that would
        // trap or need non-nearest rounding. The verify carries fpcr/exc_sum across its two passes, so
        // the inline path leaves them untouched -- correctness rests on the bails, not on FPCR upkeep.
        if (op == OP_CVTQT || op == OP_CVTQS) {
            const bool dyn = (((ins >> 11) & 3) == 3);      // /D: rounding is FPCR<59:58>, checked at runtime
            Label bail = a.new_label(), fdone = a.new_label(), cont = a.new_label();
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0));   // FPSTART: FP disabled -> FEN trap (interp)
            a.je(bail);
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            if (dyn) {                                      // dynamic rounding: only nearest is SSE's default
                a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.fpcr));
                a.shr(x86::rax, imm(58)); a.and_(x86::eax, imm(3));
                a.cmp(x86::eax, imm(2)); a.jne(bail);         // FPCR<59:58> != round-to-nearest -> bail
            }
            if (rb == 31) a.xor_(x86::eax, x86::eax);        // val = (s64) f[Fb]  (Fb==31 -> 0)
            else          a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * rb));
            if (op == OP_CVTQT) { a.cvtsi2sd(x86::xmm0, x86::rax); a.cvttsd2si(x86::rcx, x86::xmm0); }
            else { a.cvtsi2ss(x86::xmm0, x86::rax); a.cvttss2si(x86::rcx, x86::xmm0); }
            a.cmp(x86::rcx, x86::rax);                       // round-trip equal -> exact (no inexact, no trap)
            a.je(fdone);
            a.bt(x86::qword_ptr(x86::rbp, m_off.fpcr), imm(56));   // inexact: FPCR.INE clear -> first one traps
            a.jnc(bail);
            a.bind(fdone);
            if (op == OP_CVTQS) a.cvtss2sd(x86::xmm0, x86::xmm0);  // widen single -> register (T-format) bits
            a.movq(x86::qword_ptr(x86::rbp, m_off.f_base + 8u * rc), x86::xmm0);
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);              // resume this instruction in the interpreter
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        // IEEE T-float arithmetic (0x16 ADDT/SUBT/MULT/DIVT): inline SSE on the double f-registers. 
        // FPCR.INE already sticky-set (so an inexact result won't trap). Every edge bails to the interpreter:
        // FP-off, /D-not-nearest, denormal operand, INE-clear (a first inexact would trap), or an Inf/NaN/
        // denormal result (overflow/invalid/div-zero/underflow). 
        if (op == OP_ADDT || op == OP_SUBT || op == OP_MULT || op == OP_DIVT) {
            const bool dyn = (((ins >> 11) & 3) == 3);      // /D: rounding is FPCR<59:58>, checked at runtime
            Label bail = a.new_label(), cont = a.new_label();
            auto class_bail = [&](const x86::Vec& x, bool chk_inf_nan) {   // bail if denormal (always) / Inf|NaN (if asked)
                Label ok = a.new_label();
                a.movq(x86::rax, x);
                a.mov(x86::rcx, x86::rax);
                a.shr(x86::rcx, imm(52)); a.and_(x86::ecx, imm(0x7ff));      // biased exponent
                if (chk_inf_nan) { a.cmp(x86::ecx, imm(0x7ff)); a.je(bail); }
                a.test(x86::ecx, x86::ecx); a.jnz(ok);                       // exp != 0 -> normal (or Inf/NaN)
                a.shl(x86::rax, imm(12)); a.jnz(bail);                       // exp == 0, mantissa != 0 -> denormal
                a.bind(ok);
                };
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0)); a.je(bail);          // FPSTART: FP disabled -> FEN trap
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            a.bt(x86::qword_ptr(x86::rbp, m_off.fpcr), imm(56)); a.jnc(bail);        // INE clear -> first inexact traps
            if (dyn) {                                                              // dynamic rounding: SSE gives nearest
                a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.fpcr));
                a.shr(x86::rax, imm(58)); a.and_(x86::eax, imm(3));
                a.cmp(x86::eax, imm(2)); a.jne(bail);
            }
            a.movq(x86::xmm0, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)ra));   // f[Fa]
            a.movq(x86::xmm1, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));   // f[Fb]
            class_bail(x86::xmm0, false);                                           // denormal operand -> interp (DNZ/trap)
            class_bail(x86::xmm1, false);
            switch (op) {
            case OP_ADDT: a.addsd(x86::xmm0, x86::xmm1); break;
            case OP_SUBT: a.subsd(x86::xmm0, x86::xmm1); break;
            case OP_MULT: a.mulsd(x86::xmm0, x86::xmm1); break;
            default:      a.divsd(x86::xmm0, x86::xmm1); break;                   // OP_DIVT
            }
            class_bail(x86::xmm0, true);                                           // Inf/NaN/denormal result -> interp
            if (op == OP_MULT || op == OP_DIVT) {
                // Complete underflow rounds to +-0 with no denormal to catch
                // ieee_rpack still sets FPCR.UNF (+ /U //SU trap) so a zero result bails to the 
                // interp unless it is provably an operand of +-0, or (DIV) a +-Inf divisor  
                // finite/Inf is an exact signed zero with no exception (ieee_fdiv), committed inline. 
                // ADD/SUB are exempt entirely
                Label nz = a.new_label();
                a.movq(x86::rax, x86::xmm0);
                a.add(x86::rax, x86::rax);                                        // drop the sign bit
                a.jnz(nz);
                a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)ra));
                a.add(x86::rax, x86::rax);
                a.jz(nz);                                                         // Fa == +-0 -> exact zero
                if (op == OP_MULT) {
                    a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));
                    a.add(x86::rax, x86::rax);
                    a.jz(nz);                                                     // Fb == +-0 -> exact zero
                } else {
                    a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));
                    a.add(x86::rax, x86::rax);                                    // +-Inf -> 0xFFE0...0
                    a.mov(x86::rcx, imm(0xFFE0000000000000ull));
                    a.cmp(x86::rax, x86::rcx);
                    a.je(nz);                                                     // finite/+-Inf -> exact zero
                }
                a.jmp(bail);                                                      // underflowed to zero
                a.bind(nz);
            }
            a.movq(x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rc), x86::xmm0);   // f[Fc]
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        // IEEE T-float compares (0x16 CMPTUN/EQ/LT/LE): f[Fc] = (Fa cmp Fb) ? 2.0 : 0.0. No rounding or
        // inexact. A NaN operand (INV / quiet-NaN / SWC rules) or a denormal operand (unmaskable denormal
        // trap -- the interp's ieee_fcmp traps via ieee_unpack) goes to the interp; +/-Inf, zero and
        // normals compare inline via ucomisd. Two ordered operands make CMPTUN always false.
        if (op == OP_CMPTUN || op == OP_CMPTEQ || op == OP_CMPTLT || op == OP_CMPTLE) {
            Label bail = a.new_label(), cont = a.new_label();
            auto cmp_bail = [&](const x86::Vec& x) {                          // bail on NaN or denormal; Inf/zero/normal -> ok
                Label ok = a.new_label(), special = a.new_label();
                a.movq(x86::rax, x);
                a.mov(x86::rcx, x86::rax);
                a.shr(x86::rcx, imm(52)); a.and_(x86::ecx, imm(0x7ff));         // biased exponent
                a.cmp(x86::ecx, imm(0x7ff)); a.je(special);                     // exp all-ones -> Inf or NaN
                a.test(x86::ecx, x86::ecx); a.jnz(ok);                          // exp != 0 -> normal
                a.bind(special);                                                // exp == 0 (zero/denormal) or all-ones (Inf/NaN)
                a.shl(x86::rax, imm(12)); a.jnz(bail);                          // mantissa != 0 -> denormal or NaN -> bail
                a.bind(ok);                                                     // mantissa == 0 -> zero or Inf -> compare inline
                };
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0)); a.je(bail);   // FPSTART
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            a.movq(x86::xmm0, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)ra));   // f[Fa]
            a.movq(x86::xmm1, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));   // f[Fb]
            cmp_bail(x86::xmm0); cmp_bail(x86::xmm1);
            if (op == OP_CMPTUN) {
                a.xor_(x86::eax, x86::eax);                                     // both ordered -> unordered is false
            }
            else {
                a.ucomisd(x86::xmm0, x86::xmm1);
                if (op == OP_CMPTEQ) a.sete(x86::al);                      // ZF    -> Fa == Fb
                else if (op == OP_CMPTLT) a.setb(x86::al);                      // CF    -> Fa <  Fb
                else                      a.setbe(x86::al);                     // CF|ZF -> Fa <= Fb  (CMPTLE)
                a.movzx(x86::eax, x86::al);
                a.shl(x86::rax, imm(62));                                       // true -> 0x4000000000000000 (2.0)
            }
            a.mov(x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rc), x86::rax);     // f[Fc]
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        // IEEE convert / sqrt / S-float class-check bails (used by the blocks below). <bl> is each
        // block's bail label; bail if denormal (always) or Inf|NaN (when chk). dbl = double, sgl = single.
        auto dbl_bail = [&](const x86::Vec& x, bool chk, const Label& bl) {
            Label ok = a.new_label();
            a.movq(x86::rax, x); a.mov(x86::rcx, x86::rax);
            a.shr(x86::rcx, imm(52)); a.and_(x86::ecx, imm(0x7ff));
            if (chk) { a.cmp(x86::ecx, imm(0x7ff)); a.je(bl); }
            a.test(x86::ecx, x86::ecx); a.jnz(ok);
            a.shl(x86::rax, imm(12)); a.jnz(bl);
            a.bind(ok);
            };
        auto sgl_bail = [&](const x86::Vec& x, bool chk, const Label& bl) {
            Label ok = a.new_label();
            a.movd(x86::eax, x); a.mov(x86::ecx, x86::eax);
            a.shr(x86::ecx, imm(23)); a.and_(x86::ecx, imm(0xff));
            if (chk) { a.cmp(x86::ecx, imm(0xff)); a.je(bl); }
            a.test(x86::ecx, x86::ecx); a.jnz(ok);
            a.shl(x86::eax, imm(9)); a.jnz(bl);
            a.bind(ok);
            };
        auto fp_dyn_bail = [&](const Label& bl) {     // /D: SSE rounds to nearest, so bail unless FPCR does too
            a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.fpcr));
            a.shr(x86::rax, imm(58)); a.and_(x86::eax, imm(3));
            a.cmp(x86::eax, imm(2)); a.jne(bl);
            };

        // CVTST (S->T widen): zero/normal/Inf are already valid T bit patterns -> copy. S-denormal
        // (renormalizes) and NaN (quiets / may INV) go to the interpreter. No rounding or inexact.
        if (op == OP_CVTST) {
            Label bail = a.new_label(), cont = a.new_label();
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0)); a.je(bail);          // FPSTART
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            a.movq(x86::xmm0, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));   // f[Fb]
            dbl_bail(x86::xmm0, true, bail);                                        // denormal/Inf/NaN -> interp
            a.movq(x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rc), x86::xmm0);   // S bits are valid T
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        // CVTTS (T->S narrow): round the double to single, re-widen for the S register format. Denormal
        // operand, an Inf/NaN/denormal single result (overflow/invalid/underflow), or a first inexact
        // (INE clear) bail.
        if (op == OP_CVTTS) {
            const bool dyn = (((ins >> 11) & 3) == 3);
            Label bail = a.new_label(), cont = a.new_label();
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0)); a.je(bail);          // FPSTART
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            a.bt(x86::qword_ptr(x86::rbp, m_off.fpcr), imm(56)); a.jnc(bail);        // INE clear -> first inexact traps
            if (dyn) fp_dyn_bail(bail);
            a.movq(x86::xmm0, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));   // f[Fb] (double)
            dbl_bail(x86::xmm0, false, bail);                                       // denormal operand -> interp
            a.cvtsd2ss(x86::xmm0, x86::xmm0);                                       // round to single
            sgl_bail(x86::xmm0, true, bail);                                        // Inf/NaN/denormal single -> interp
            {   // complete underflow to +-0: nonzero double below the single range; interp owns UNF/trap
                Label nz = a.new_label();
                a.movd(x86::eax, x86::xmm0);
                a.add(x86::eax, x86::eax);                                          // drop the sign bit
                a.jnz(nz);
                a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));
                a.add(x86::rax, x86::rax);
                a.jz(nz);                                                           // Fb == +-0 -> exact zero
                a.jmp(bail);
                a.bind(nz);
            }
            a.cvtss2sd(x86::xmm0, x86::xmm0);                                       // -> double (register format)
            a.movq(x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rc), x86::xmm0);
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        // CVTTQ (T->Q): double -> signed 64-bit integer (raw bits into f[Fc]). /C chops (cvttsd2si), else
        // round-to-nearest (cvtsd2si). Overflow / Inf / NaN -> the integer indefinite -> interp (covers
        // IOV). A non-integer source (inexact) with INE clear traps -> interp; exact converts run inline.
        if (op == OP_CVTTQ) {
            const bool chop = (((ins >> 11) & 3) == 0);
            const bool dyn = (((ins >> 11) & 3) == 3);
            Label bail = a.new_label(), cont = a.new_label(), exact = a.new_label();
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0)); a.je(bail);          // FPSTART
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            if (dyn) fp_dyn_bail(bail);
            a.movq(x86::xmm0, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));   // f[Fb] (double)
            dbl_bail(x86::xmm0, false, bail);                                       // denormal operand -> interp
            if (chop) a.cvttsd2si(x86::rax, x86::xmm0); else a.cvtsd2si(x86::rax, x86::xmm0);
            a.mov(x86::ecx, imm(1)); a.shl(x86::rcx, imm(63));                      // rcx = INT64_MIN (indefinite)
            a.cmp(x86::rax, x86::rcx); a.je(bail);                                  // overflow / Inf / NaN -> interp
            a.cvtsi2sd(x86::xmm1, x86::rax);                                        // round-trip to test exactness
            a.ucomisd(x86::xmm1, x86::xmm0); a.je(exact);                           // round-trip == source -> exact
            a.bt(x86::qword_ptr(x86::rbp, m_off.fpcr), imm(56)); a.jnc(bail);       // inexact + INE clear -> trap
            a.bind(exact);
            a.mov(x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rc), x86::rax);     // store s64 bits
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        // IEEE SQRT (T/S): sqrtsd / sqrtss. Denormal operand, an Inf/NaN/denormal result (a negative
        // operand yields NaN), or a first inexact (INE clear) bail to the interpreter.
        if (op == OP_SQRTT || op == OP_SQRTS) {
            const bool dyn = (((ins >> 11) & 3) == 3);
            Label bail = a.new_label(), cont = a.new_label();
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0)); a.je(bail);          // FPSTART
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            a.bt(x86::qword_ptr(x86::rbp, m_off.fpcr), imm(56)); a.jnc(bail);        // INE clear -> first inexact traps
            if (dyn) fp_dyn_bail(bail);
            a.movq(x86::xmm0, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));   // f[Fb]
            dbl_bail(x86::xmm0, false, bail);                                       // denormal operand -> interp
            if (op == OP_SQRTT) {
                a.sqrtsd(x86::xmm0, x86::xmm0);
                dbl_bail(x86::xmm0, true, bail);                                      // Inf/NaN(neg)/denormal result -> interp
            }
            else {
                a.cvtsd2ss(x86::xmm0, x86::xmm0);
                a.sqrtss(x86::xmm0, x86::xmm0);
                sgl_bail(x86::xmm0, true, bail);                                      // Inf/NaN(neg)/denormal result -> interp
                a.cvtss2sd(x86::xmm0, x86::xmm0);
            }
            a.movq(x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rc), x86::xmm0);
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        // IEEE S-float arith (ADDS/SUBS/MULS/DIVS): compute in single precision (narrow the operands,
        // op, re-widen). Same bail policy as the T-float arith -- FP-off, /D-not-nearest, denormal
        // operand, INE clear, or an Inf/NaN/denormal single result.
        if (op == OP_ADDS || op == OP_SUBS || op == OP_MULS || op == OP_DIVS) {
            const bool dyn = (((ins >> 11) & 3) == 3);
            Label bail = a.new_label(), cont = a.new_label();
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0)); a.je(bail);          // FPSTART
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            a.bt(x86::qword_ptr(x86::rbp, m_off.fpcr), imm(56)); a.jnc(bail);        // INE clear -> first inexact traps
            if (dyn) fp_dyn_bail(bail);
            a.movq(x86::xmm0, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)ra));   // f[Fa]
            a.movq(x86::xmm1, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));   // f[Fb]
            dbl_bail(x86::xmm0, false, bail);                                       // denormal operands -> interp
            dbl_bail(x86::xmm1, false, bail);
            a.cvtsd2ss(x86::xmm0, x86::xmm0);                                       // operands -> single (exact)
            a.cvtsd2ss(x86::xmm1, x86::xmm1);
            switch (op) {
            case OP_ADDS: a.addss(x86::xmm0, x86::xmm1); break;
            case OP_SUBS: a.subss(x86::xmm0, x86::xmm1); break;
            case OP_MULS: a.mulss(x86::xmm0, x86::xmm1); break;
            default:      a.divss(x86::xmm0, x86::xmm1); break;                   // OP_DIVS
            }
            sgl_bail(x86::xmm0, true, bail);                                        // Inf/NaN/denormal single result -> interp
            if (op == OP_MULS || op == OP_DIVS) {
                // Complete underflow to +-0 (no denormal to catch). 
                // ieee_rpack sets FPCR.UNF (+ /U // //SU trap), so a zero result bails unless exactly
                // an operand of +-0, or (DIV) a +-Inf divisor (finite/Inf = exact signed zero, no exception,
                // committed inline; a not-canonical S divisor fails the Inf compare and bails to the
                // interp. ADD/SUB exempt entirely.
                Label nz = a.new_label();
                a.movd(x86::eax, x86::xmm0);
                a.add(x86::eax, x86::eax);                                        // drop the sign bit
                a.jnz(nz);
                a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)ra));
                a.add(x86::rax, x86::rax);
                a.jz(nz);                                                         // Fa == +-0 -> exact zero
                if (op == OP_MULS) {
                    a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));
                    a.add(x86::rax, x86::rax);
                    a.jz(nz);                                                     // Fb == +-0 -> exact zero
                } else {
                    a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rb));
                    a.add(x86::rax, x86::rax);                                    // +-Inf (reg format) -> 0xFFE0...0
                    a.mov(x86::rcx, imm(0xFFE0000000000000ull));
                    a.cmp(x86::rax, x86::rcx);
                    a.je(nz);                                                     // finite/+-Inf -> exact zero
                }
                a.jmp(bail);                                                      // underflowed to zero
                a.bind(nz);
            }
            a.cvtss2sd(x86::xmm0, x86::xmm0);                                       // -> double (register format)
            a.movq(x86::qword_ptr(x86::rbp, m_off.f_base + 8u * (uint32_t)rc), x86::xmm0);
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        // FLTL non-arithmetic (0x17): all effects in state.f / fpcr via jit_fltl(cpu, ins).
        if (op == OP_FLTL) {
            emit_call(fltl_helper, { {JA_CPU, 0}, {JA_I32, (uint64_t)ins} });  // jit_fltl(cpu, ins)
            Label ok = a.new_label();
            a.test(x86::eax, x86::eax);
            a.jz(ok);
            set_pc(b->tag + 4 * (uint64_t)i);               // FEN trap: resume here in the interpreter
            a.mov(x86::eax, imm(i));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            a.bind(ok);
            continue;
        }

        // FLTV VAX arith (0x15): jit_fltv runs the op into f[Fc]. Return 0 ok / 1 FPSTART bail (op not run
        // -> set_pc, interp re-runs) / 2 arith trap (op ran + GO_PAL already set state.pc -> return as-is).
        if (op == OP_FLTV) {
            a.mov(x86::r10, imm(b->tag + 4 * (uint64_t)i));   // GO_PAL inside the helper stamps
            a.mov(x86::qword_ptr(x86::rbp, m_off.state_current_pc), x86::r10);   // EXC_ADDR from current_pc
            emit_call(fltv_helper, { {JA_CPU, 0}, {JA_I32, (uint64_t)ins} });  // jit_fltv(cpu, ins)
            Label ok = a.new_label(), trapped = a.new_label();
            a.test(x86::eax, x86::eax);
            a.jz(ok);                                        // 0: no trap -> continue the block
            a.cmp(x86::eax, imm(2));
            a.je(trapped);                                   // 2: arith trap -- GO_PAL already set state.pc
            set_pc(b->tag + 4 * (uint64_t)i);               // 1: FEN trap (op not run) -> resume this instr
            a.mov(x86::eax, imm(i));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            a.bind(trapped);                                 // op ran then diverted: count it, keep state.pc
            a.mov(x86::eax, imm(i + 1));
            a.add(x86::eax, x86::r13d);
            a.jmp(done);
            a.bind(ok);
            continue;
        }

        // Computed jump JMP/JSR/RET (0x1a): Ra = PC+4 (return address); PC = Rb & ~3. A
        // terminator like a branch, but the target is a register -- left in R10 so the epilogue's
        // cached link validates it (succ->tag == target), chaining calls/returns/dispatch.
        if (op == OP_JMP) {
            const uint64_t ret = b->tag + 4 * (uint64_t)(i + 1);
            if (rb == 31) a.xor_(x86::r10d, x86::r10d);
            else          mov_from_reg(x86::r10, rb);
            a.and_(x86::r10, imm(~(uint64_t)3));                       // target = Rb & ~3 (clear low 2)
            if (b->tag & 3) a.or_(x86::r10, imm(b->tag & 3));           // DO_JMP: mode bits come from the current pc
            if (ra != 31) { a.mov(x86::rax, imm(ret & ~(uint64_t)3)); mov_to_reg(ra, x86::rax); }  // return addr = PC & ~3 (DO_JMP)
#ifdef JIT_VERIFY
            a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
#else
            if (!defer_pc || pal_block)
                a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
#endif
            continue;
        }

        // HW_RET (HWREI, 0x1e): transform Rb with the family mask/bits, without a link write.
        // Like OP_JMP, leave R10 = target for the epilogue's in-frame chain.
        if (op == OP_HW_RET) {
            const auto descriptor = alpha_pal_return_descriptor(m_profile.pal, ins);
            if (rb == 31 || descriptor.target_mask == 0
                || descriptor.target_bits == ~uint64_t(0)) {
                a.mov(x86::r10, imm(descriptor.target_bits));
            } else {
                mov_from_reg(x86::r10, rb);
                // 64-bit AND/OR immediates sign-extend an encoded 32-bit value.
                auto immediate_fits = [](uint64_t value) {
                    return value <= 0x7fffffffu || value >= 0xffffffff80000000ull;
                };
                if (descriptor.target_mask != ~uint64_t(0)) {
                    if (immediate_fits(descriptor.target_mask))
                        a.and_(x86::r10, imm(descriptor.target_mask));
                    else {
                        a.mov(x86::rax, imm(descriptor.target_mask));
                        a.and_(x86::r10, x86::rax);
                    }
                }
                if (descriptor.target_bits != 0) {
                    if (immediate_fits(descriptor.target_bits))
                        a.or_(x86::r10, imm(descriptor.target_bits));
                    else {
                        a.mov(x86::rax, imm(descriptor.target_bits));
                        a.or_(x86::r10, x86::rax);
                    }
                }
            }
#ifdef JIT_VERIFY
            a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
#else
            if (!defer_pc || pal_block)
                a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
#endif
            continue;
        }

        // CALL_PAL (0x00): the family policy selects the vector, saved PC and optional link.
        if (op == OP_CALL_PAL) {
            const uint32_t func = ins & 0x1FFFFFFF;
            const uint64_t cpc = b->tag + 4 * (uint64_t)i;                          // CALL_PAL address
            const AlphaPALCallEntry entry = m_profile.pal.call_pal_entry(
                func, cpc, b->tag + 4 * (uint64_t)(i + 1));
            uint32_t allowed_modes = 0;
            for (uint32_t mode = 0; mode < 4; ++mode)
                if (m_profile.pal.call_pal_valid(func, mode)) allowed_modes |= 1u << mode;
            Label do_vector = a.new_label();
            if (allowed_modes != 0xfu) {   // Only modes admitted by the family may vector.
                for (uint32_t mode = 0; mode < 4; ++mode) {
                    if (!(allowed_modes & (1u << mode))) continue;
                    a.cmp(x86::dword_ptr(x86::rbp, m_off.state_cm), imm(mode));
                    a.je(do_vector);
                }
                emit_call(opcdec_helper, { {JA_CPU, 0}, {JA_I64, cpc} });  // jit_opcdec: sets state.pc/exc_addr, clears lock
                a.add(x86::r13, imm(i + 1));                 // count the block; helper already wrote state.pc
                a.mov(x86::eax, x86::r13d);
                a.jmp(done);                               // trap path exits (does not chain)
            }
            a.bind(do_vector);
            a.mov(x86::rax, imm(entry.saved_pc));
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_addr), x86::rax);
            if (entry.write_link) {
                a.movzx(x86::eax, x86::byte_ptr(x86::rbp, m_off.sde));      // SDE (0/1)
                a.mov(x86::rcx, imm(entry.return_pc));
                if (const int link_pin = pin_id(entry.link_register); link_pin >= 0) {
                    if (entry.link_register == entry.shadow_link_register)
                        a.mov(x86::gpq((uint32_t)link_pin), x86::rcx);
                    else {
                        a.test(x86::eax, x86::eax);
                        a.cmovz(x86::gpq((uint32_t)link_pin), x86::rcx);
                    }
                }
                if (entry.shadow_link_register < 32
                    && entry.shadow_link_register != entry.link_register) {
                    if (const int shadow_pin = pin_id(entry.shadow_link_register); shadow_pin >= 0) {
                        a.test(x86::eax, x86::eax);
                        a.cmovnz(x86::gpq((uint32_t)shadow_pin), x86::rcx);
                    }
                }
                const int link_stride = static_cast<int>(entry.shadow_link_register)
                                      - static_cast<int>(entry.link_register);
                if (link_stride == 32) a.shl(x86::eax, imm(5));
                else a.imul(x86::eax, x86::eax, imm(link_stride));
                a.add(x86::eax, imm(entry.link_register));
                a.mov(x86::qword_ptr(x86::rbx, x86::rax, 3), x86::rcx);     // r[idx] = return address
            }
            a.mov(x86::r10, x86::qword_ptr(x86::rbp, m_off.pal_base));
            a.or_(x86::r10, imm(entry.offset));                         // r10 = pal_base | entry offset
            a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);  // state.pc = PAL entry
            continue;                                                  // -> terminator epilogue chains via r10
        }

        // Branch terminators: compute the target into state.pc, then end the block. The
        // branch is at index i, so the PC of the next instruction is b->tag + 4*(i+1).
        // FP conditional branches (0x31-0x37): FPSTART, then branch on f[Fa] vs 0.0 (sign-magnitude).
        // Map the bits to a monotonic signed s = sign ? -magnitude : magnitude so the integer signed
        // cmov conditions apply directly; -0 -> s=0 (matches the interp's zero handling), NaN by bits.
        if (is_fp_branch(op)) {
            const int64_t  bdisp = (int64_t)((uint64_t)(ins & 0x1FFFFF) << 43) >> 43;  // sext disp21
            const uint64_t fall = b->tag + 4 * (uint64_t)(i + 1);
            const uint64_t tgt = fall + (uint64_t)(bdisp * 4);
            Label bail = a.new_label(), cont = a.new_label();
            a.cmp(x86::byte_ptr(x86::rbp, m_off.fpen), imm(0));   // FPSTART: FP disabled -> FEN trap (interp)
            a.je(bail);
            a.mov(x86::qword_ptr(x86::rbp, m_off.exc_sum), imm(0));
            if (ra == 31) a.xor_(x86::eax, x86::eax);             // f[31] = 0 -> s = 0
            else {
                a.mov(x86::rax, x86::qword_ptr(x86::rbp, m_off.f_base + 8u * ra));
                a.mov(x86::rcx, x86::rax);
                a.sar(x86::rcx, imm(63));                           // rcx = sign mask (0 or -1)
                a.btr(x86::rax, imm(63));                           // rax = magnitude (sign bit cleared)
                a.xor_(x86::rax, x86::rcx); a.sub(x86::rax, x86::rcx);   // rax = s = sign ? -magnitude : magnitude
            }
            a.mov(x86::r10, imm(fall));
            a.mov(x86::r11, imm(tgt));
            a.test(x86::rax, x86::rax);
            switch (op) {
            case OP_FBEQ: a.cmovz(x86::r10, x86::r11);  break;
            case OP_FBNE: a.cmovnz(x86::r10, x86::r11); break;
            case OP_FBLT: a.cmovs(x86::r10, x86::r11);  break;
            case OP_FBGE: a.cmovns(x86::r10, x86::r11); break;
            case OP_FBLE: a.cmovle(x86::r10, x86::r11); break;
            case OP_FBGT: a.cmovg(x86::r10, x86::r11);  break;
            default: break;
            }
            if (!defer_pc || pal_block)
                a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
            a.jmp(cont);
            a.bind(bail);
            set_pc(b->tag + 4 * (uint64_t)i);                    // resume this instruction in the interpreter
            JIT_STAT_EXIT(5); a.mov(x86::eax, imm(i)); a.add(x86::eax, x86::r13d); a.jmp(done);
            a.bind(cont);
            continue;
        }

        if (is_branch(op)) {
            const int64_t  bdisp = (int64_t)((uint64_t)(ins & 0x1FFFFF) << 43) >> 43;  // sext disp21
            const uint64_t fall = b->tag + 4 * (uint64_t)(i + 1);
            const uint64_t tgt = fall + (uint64_t)(bdisp * 4);
            if (regalloc.direct_exits) {
                // Direct exit: condition stays in FLAGS; compile_block emits the jumps.
                if (op == OP_BR || op == OP_BSR) {
                    if (ra != 31) { a.mov(x86::r10, imm(fall & ~(uint64_t)3)); mov_to_reg(ra, x86::r10); }
                    regalloc.exit_kind = 2;
                } else {
                    test_ra(op == OP_BLBC || op == OP_BLBS);
                    uint32_t cc = 0;
                    switch (op) {
                    case OP_BEQ: case OP_BLBC: cc = (uint32_t) x86::CondCode::kZ;  break;
                    case OP_BNE: case OP_BLBS: cc = (uint32_t) x86::CondCode::kNZ; break;
                    case OP_BLT: cc = (uint32_t) x86::CondCode::kS;  break;
                    case OP_BGE: cc = (uint32_t) x86::CondCode::kNS; break;
                    case OP_BLE: cc = (uint32_t) x86::CondCode::kLE; break;
                    case OP_BGT: cc = (uint32_t) x86::CondCode::kG;  break;
                    default: break;
                    }
                    regalloc.exit_kind = 1;
                    regalloc.exit_cc = cc;
                }
                regalloc.exit_tgt = tgt;
                regalloc.exit_fall = fall;
                continue;
            }
            if (op == OP_BR || op == OP_BSR) {                 // Ra = return address; PC = target
                if (ra != 31) { a.mov(x86::r10, imm(fall & ~(uint64_t)3)); mov_to_reg(ra, x86::r10); }  // link = PC & ~3 (DO_BR)
                a.mov(x86::r10, imm(tgt));
            }
            else {                                           // conditional: PC = cond ? target : fall
                a.mov(x86::r10, imm(fall));
                a.mov(x86::r11, imm(tgt));
                test_ra(op == OP_BLBC || op == OP_BLBS);
                switch (op) {
                case OP_BEQ:  a.cmovz(x86::r10, x86::r11);  break;
                case OP_BNE:  a.cmovnz(x86::r10, x86::r11); break;
                case OP_BLT:  a.cmovs(x86::r10, x86::r11);  break;
                case OP_BGE:  a.cmovns(x86::r10, x86::r11); break;
                case OP_BLE:  a.cmovle(x86::r10, x86::r11); break;
                case OP_BGT:  a.cmovg(x86::r10, x86::r11);  break;
                case OP_BLBC: a.cmovz(x86::r10, x86::r11);  break;
                case OP_BLBS: a.cmovnz(x86::r10, x86::r11); break;
                default: break;
                }
            }
            if (!defer_pc || pal_block)
                a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
            continue;
        }

        // INTL conditional moves (CMOVxx): Rc = cond(Ra) ? op2 : Rc. Same condition tests as the
        // matching branches; op2 (Rb or literal) moves into Rc only when the condition holds, so the
        // current Rc is read and kept otherwise. R31 dest discards the result.
        if (op == OP_CMOV) {
            if (rc == 31) continue;
            const uint32_t f = (ins >> 5) & 0x7f;
            op2_rcx();                                  // rcx = op2 (Rb or literal -- the moved value)
            test_ra(f == 0x14 || f == 0x16);
            const int dp = pin_id(rc);
            const x86::Gp dst = (dp >= 0) ? x86::gpq((uint32_t)dp) : x86::r10;
            if (dp < 0) mov_from_reg(dst, rc);           // current Rc (kept when the condition fails)
            switch (f) {
            case 0x24: a.cmovz(dst, x86::rcx);  break;   // CMOVEQ  (Ra == 0)
            case 0x26: a.cmovnz(dst, x86::rcx); break;   // CMOVNE  (Ra != 0)
            case 0x44: a.cmovs(dst, x86::rcx);  break;   // CMOVLT  (Ra <  0)
            case 0x46: a.cmovns(dst, x86::rcx); break;   // CMOVGE  (Ra >= 0)
            case 0x64: a.cmovle(dst, x86::rcx); break;   // CMOVLE  (Ra <= 0)
            case 0x66: a.cmovg(dst, x86::rcx);  break;   // CMOVGT  (Ra >  0)
            case 0x14: a.cmovnz(dst, x86::rcx); break;   // CMOVLBS (Ra & 1)
            case 0x16: a.cmovz(dst, x86::rcx);  break;   // CMOVLBC (!(Ra & 1))
            }
            if (dp < 0) mov_to_reg(rc, dst);
            continue;
        }

        // INTS (0x12) byte-manipulation: extract/insert/mask/zap. Rc = a shift+mask of Ra keyed on the
        // byte position pos = Rb&7 (the selector V_2). Pure ALU -> verify-checked by the GPR compare.
        // RAX = Ra/result, CL = the variable shift, R10/R11 scratch, EDX preserves pos for the H-form
        // pos==0 edge cases. Mirrors cpu_bwx.h; size (0=B 1=W 2=L 3=Q) and mask come from the function.
        if (op == OP_EXTL || op == OP_EXTH || op == OP_INSL || op == OP_INSH
            || op == OP_MSKL || op == OP_MSKH || op == OP_ZAP) {
            if (rc == 31) continue;
            const uint32_t f = (ins >> 5) & 0x7f;
            const int size = (f >> 4) & 3;
            const uint64_t mask = (size == 0) ? (uint64_t)0xff : (size == 1) ? (uint64_t)0xffff
                : (size == 2) ? (uint64_t)0xffffffff : ~(uint64_t)0;
            op1_rax();                                                  // rax = Ra (data)
            if (islit) {
                const uint32_t shift = (lit & 7u) * 8u;
                const uint32_t high_shift = (64u - shift) & 63u;
                // Literal masks need no table read or DPC scratch registers.
                auto and_mask = [&](uint64_t keep) {
                    if (keep == ~(uint64_t)0) return;
                    if (keep == 0) a.xor_(x86::eax, x86::eax);
                    else if (keep == 0xffffffffu) a.mov(x86::eax, x86::eax);
                    else if (keep <= 0xffffffffu) a.and_(x86::eax, imm((uint32_t)keep));
                    else if (keep >= 0xffffffff80000000ull) a.and_(x86::rax, imm(keep));
                    else { a.mov(x86::rcx, imm(keep)); a.and_(x86::rax, x86::rcx); }
                };
                switch (op) {
                case OP_EXTL:
                    if (shift) a.shr(x86::rax, imm(shift));
                    and_mask(mask);
                    break;
                case OP_EXTH:
                    if (high_shift) a.shl(x86::rax, imm(high_shift));
                    and_mask(mask);
                    break;
                case OP_INSL:
                    and_mask(mask);
                    if (shift) a.shl(x86::rax, imm(shift));
                    break;
                case OP_INSH:
                    if (!shift) a.xor_(x86::eax, x86::eax);
                    else { and_mask(mask); a.shr(x86::rax, imm(high_shift)); }
                    break;
                case OP_MSKL:
                    and_mask(~(mask << shift));
                    break;
                case OP_MSKH:
                    if (shift) and_mask(~(mask >> high_shift));
                    break;
                case OP_ZAP:
                    and_mask(f == 0x30 ? ~g_zapnot_mask[lit] : g_zapnot_mask[lit]);
                    break;
                default: break;
                }
                mov_to_reg(rc, x86::rax);
                if (rc != prev_dpc_base) preserve_dpc();
                continue;
            }
            op2_rcx();                                                  // rcx = Rb (selector)
            switch (op) {
            case OP_EXTL:                                             // (Ra >> pos*8) & mask
                a.and_(x86::ecx, imm(7)); a.shl(x86::ecx, imm(3));
                a.shr(x86::rax, x86::cl);
                if (size != 3) { a.mov(x86::r10, imm(mask)); a.and_(x86::rax, x86::r10); }
                break;
            case OP_EXTH:                                             // (Ra << ((64-pos*8)&63)) & mask
                a.and_(x86::ecx, imm(7)); a.shl(x86::ecx, imm(3));
                a.neg(x86::ecx); a.and_(x86::ecx, imm(63));
                a.shl(x86::rax, x86::cl);
                if (size != 3) { a.mov(x86::r10, imm(mask)); a.and_(x86::rax, x86::r10); }
                break;
            case OP_INSL:                                             // (Ra & mask) << pos*8
                if (size != 3) { a.mov(x86::r10, imm(mask)); a.and_(x86::rax, x86::r10); }
                a.and_(x86::ecx, imm(7)); a.shl(x86::ecx, imm(3));
                a.shl(x86::rax, x86::cl);
                break;
            case OP_INSH:                                             // pos ? ((Ra&mask) >> ((64-pos*8)&63)) : 0
                if (size != 3) { a.mov(x86::r10, imm(mask)); a.and_(x86::rax, x86::r10); }
                a.and_(x86::ecx, imm(7)); a.mov(x86::edx, x86::ecx);
                a.shl(x86::ecx, imm(3)); a.neg(x86::ecx); a.and_(x86::ecx, imm(63));
                a.shr(x86::rax, x86::cl);
                a.xor_(x86::r11d, x86::r11d); a.test(x86::edx, x86::edx); a.cmovz(x86::rax, x86::r11);
                break;
            case OP_MSKL:                                             // Ra & ~(mask << pos*8)
                a.and_(x86::ecx, imm(7)); a.shl(x86::ecx, imm(3));
                a.mov(x86::r10, imm(mask)); a.shl(x86::r10, x86::cl);
                a.not_(x86::r10); a.and_(x86::rax, x86::r10);
                break;
            case OP_MSKH:                                             // pos ? (Ra & ~(mask >> ((64-pos*8)&63))) : Ra
                a.and_(x86::ecx, imm(7)); a.mov(x86::edx, x86::ecx);
                a.shl(x86::ecx, imm(3)); a.neg(x86::ecx); a.and_(x86::ecx, imm(63));
                a.mov(x86::r10, imm(mask)); a.shr(x86::r10, x86::cl); a.not_(x86::r10);
                a.mov(x86::r11, x86::rax); a.and_(x86::r11, x86::r10);
                a.test(x86::edx, x86::edx); a.cmovnz(x86::rax, x86::r11);
                break;
            case OP_ZAP:                                              // Ra & byte_expand(selector); ZAP inverts
                a.movzx(x86::ecx, x86::cl);
                a.mov(x86::r11, imm((uint64_t)&g_zapnot_mask[0]));
                a.mov(x86::r10, x86::qword_ptr(x86::r11, x86::rcx, 3));   // g_zapnot_mask[selector & 0xff]
                if (f == 0x30) a.not_(x86::r10);                        // ZAP keeps bytes whose bit is CLEAR
                a.and_(x86::rax, x86::r10);
                break;
            default: break;
            }
            mov_to_reg(rc, x86::rax);
            continue;
        }

        switch (op) {
        case OP_ADDQ:  emit_alu2(x86::Inst::kIdAdd); break;
        case OP_SUBQ:  emit_alu2(x86::Inst::kIdSub); break;
        case OP_AND:   emit_alu2(x86::Inst::kIdAnd); break;
        case OP_BIS:
            if (rc != 31 && (ra == 31 || (!islit && rb == 31))) {   // MOV: Rc = the other source
                const int dp = pin_id(rc);
                const int src = (ra == 31) ? rb : ra;                  // rb may be a literal (ra == 31)
                const bool lit_src = (ra == 31) && islit;
                if (dp >= 0) {
                    const x86::Gp dst = x86::gpq((uint32_t) dp);
                    if (lit_src)        a.mov(dst, imm(lit));
                    else if (src == 31) a.xor_(x86::gpd((uint32_t) dp), x86::gpd((uint32_t) dp));
                    else if (pin_id(src) != dp) mov_from_reg(dst, src);
                } else if (lit_src || src == 31) {
                    a.mov(reg(rc), imm(lit_src ? (uint64_t) lit : 0));
                } else if (pin_id(src) >= 0) {
                    a.mov(reg(rc), x86::gpq((uint32_t) pin_id(src)));
                } else {
                    mov_from_reg(x86::rax, src);
                    mov_to_reg(rc, x86::rax);
                }
                result_in_dest = true;
                break;
            }
            emit_alu2(x86::Inst::kIdOr);
            break;
        case OP_XOR:   emit_alu2(x86::Inst::kIdXor); break;
        case OP_BIC:   op1_rax(); op2_rcx(); a.not_(x86::rcx); a.and_(x86::rax, x86::rcx); break;
        case OP_ORNOT: op1_rax(); op2_rcx(); a.not_(x86::rcx); a.or_(x86::rax, x86::rcx); break;
        case OP_EQV:   op1_rax(); op2_rcx(); a.not_(x86::rcx); a.xor_(x86::rax, x86::rcx); break;
        case OP_MULQ:  op1_rax(); op2_rcx(); a.imul(x86::rax, x86::rcx); break;
        case OP_UMULH: op1_rax(); op2_rcx(); a.mul(x86::rcx); a.mov(x86::rax, x86::rdx); break;  // RDX:RAX = Ra*op2; hi64 = RDX
        case OP_MULL:                                          // 32-bit multiply, low 32 sign-extended
        {
            if (ra == 31) a.xor_(x86::eax, x86::eax);
            else          mov_from_reg32(x86::eax, ra);
            if (islit)         a.mov(x86::ecx, imm(lit));
            else if (rb == 31) a.xor_(x86::ecx, x86::ecx);
            else               mov_from_reg32(x86::ecx, rb);
            a.imul(x86::eax, x86::ecx);                          // eax = (Ra*op2)[31:0]
            a.movsxd(x86::rax, x86::eax);
            break;
        }

        case OP_S4ADDQ: op1_rax(); a.shl(x86::rax, imm(2)); op2_rcx(); a.add(x86::rax, x86::rcx); break;
        case OP_S8ADDQ: op1_rax(); a.shl(x86::rax, imm(3)); op2_rcx(); a.add(x86::rax, x86::rcx); break;
        case OP_S4SUBQ: op1_rax(); a.shl(x86::rax, imm(2)); op2_rcx(); a.sub(x86::rax, x86::rcx); break;
        case OP_S8SUBQ: op1_rax(); a.shl(x86::rax, imm(3)); op2_rcx(); a.sub(x86::rax, x86::rcx); break;

        case OP_SLL: op1_rax(); if (islit) a.shl(x86::rax, imm(lit & 63)); else { op2_rcx(); a.shl(x86::rax, x86::cl); } break;
        case OP_SRL: op1_rax(); if (islit) a.shr(x86::rax, imm(lit & 63)); else { op2_rcx(); a.shr(x86::rax, x86::cl); } break;
        case OP_SRA: op1_rax(); if (islit) a.sar(x86::rax, imm(lit & 63)); else { op2_rcx(); a.sar(x86::rax, x86::cl); } break;

        case OP_SEXTB: op2_rcx(); a.movsx(x86::rax, x86::cl); break;   // Rc = sign-extend low byte of op2 (V_2)
        case OP_SEXTW: op2_rcx(); a.movsx(x86::rax, x86::cx); break;   // Rc = sign-extend low word of op2 (V_2)

        case OP_CTPOP: op2_rcx(); a.popcnt(x86::rax, x86::rcx); break; // Rc = popcount(op2); POPCNT(0)==0 == CTPOP(0)
        case OP_CTLZ: {                                                // Rc = (op2==0) ? 64 : 63 - BSR(op2)
            op2_rcx();
            Label zero = a.new_label(), done = a.new_label();
            a.bsr(x86::rax, x86::rcx);            // ZF=1 if op2==0; else rax = index of MSB (0..63)
            a.jz(zero);
            a.xor_(x86::rax, imm(63));            // 63 - bsr  (bsr in [0,63], so 63-n == n^63)
            a.jmp(done);
            a.bind(zero); a.mov(x86::eax, imm(64));
            a.bind(done);
            break;
        }
        case OP_CTTZ:                                                  // Rc = (op2==0) ? 64 : BSF(op2)
            op2_rcx();
            a.bsf(x86::rax, x86::rcx);            // ZF=1 if op2==0; else rax = index of LSB
            a.mov(x86::r10d, imm(64));            // MOV preserves ZF
            a.cmovz(x86::rax, x86::r10);          // op2==0 -> 64 (BSF leaves rax undefined)
            break;

        case OP_AMASK:    // Rc = op2 & ~selected feature mask
            op2_rcx();      // classify enforced Ra==31 (the Ra!=31 form traps OPCDEC in the interpreter)
            a.mov(x86::rax, imm(~m_profile.amask));
            a.and_(x86::rax, x86::rcx);
            break;
        case OP_IMPLVER:  // Rc = selected implementation version
            a.mov(x86::eax, imm(m_profile.implver));
            break;

        case OP_CMPEQ:  emit_compare(x86::Inst::kIdSete);  break;
        case OP_CMPLT:  emit_compare(x86::Inst::kIdSetl);  break;
        case OP_CMPLE:  emit_compare(x86::Inst::kIdSetle); break;
        case OP_CMPULT: emit_compare(x86::Inst::kIdSetb);  break;
        case OP_CMPULE: emit_compare(x86::Inst::kIdSetbe); break;

        case OP_CMPBGE:   // 8 parallel unsigned byte compares (Ra.byte[i] >= op2.byte[i]) -> bit i; bits 63:8 = 0
            op1_rax(); op2_rcx();
            a.movq(x86::xmm0, x86::rax);             // Ra (8 bytes)
            a.movq(x86::xmm1, x86::rcx);             // op2 (8 bytes; literal in byte 0, 0 elsewhere)
            a.movdqa(x86::xmm2, x86::xmm0);          // copy of Ra
            a.pmaxub(x86::xmm2, x86::xmm1);          // per-byte unsigned max(Ra, op2)
            a.pcmpeqb(x86::xmm2, x86::xmm0);         // 0xff where max == Ra, i.e. Ra >= op2
            a.pmovmskb(x86::eax, x86::xmm2);         // gather byte-sign bits -> low 8 bits = the mask
            a.movzx(x86::eax, x86::al);              // discard the high (zero-padded) lane bits
            break;

        case OP_ADDL: case OP_SUBL:                                    // 32-bit, result sign-extended to 64
        case OP_S4ADDL: case OP_S8ADDL: case OP_S4SUBL: case OP_S8SUBL: // scaled longword: sext32((Ra*scale) +/- Rb)
        {
            const bool issub = (op == OP_SUBL || op == OP_S4SUBL || op == OP_S8SUBL);
            const int  sh = (op == OP_S4ADDL || op == OP_S4SUBL) ? 2     // Ra*4
                : (op == OP_S8ADDL || op == OP_S8SUBL) ? 3     // Ra*8
                : 0;
            {
                const int dp = pin_id(rc);
                const bool op2_is_rc = !islit && rb != 31 && rb == rc;
                if (rc != 31 && dp >= 0 && sh == 0 && !op2_is_rc) {
                    const x86::Gp dd = x86::gpd((uint32_t) dp);
                    if (ra == 31) a.xor_(dd, dd);
                    else if (pin_id(ra) != dp) mov_from_reg32(dd, ra);
                    if (islit) { if (issub) a.sub(dd, imm(lit)); else a.add(dd, imm(lit)); }
                    else if (rb != 31) {
                        const int bp = pin_id(rb);
                        const Operand o2 = (bp >= 0) ? Operand(x86::gpd((uint32_t) bp)) : Operand(reg32(rb));
                        a.emit(issub ? x86::Inst::kIdSub : x86::Inst::kIdAdd, dd, o2);
                    }
                    a.movsxd(x86::gpq((uint32_t) dp), dd);
                    result_in_dest = true;
                    break;
                }
            }
            if (ra == 31) a.xor_(x86::eax, x86::eax);
            else          mov_from_reg32(x86::eax, ra);   // shadow-remapped (was a raw rbx read)
            if (sh) a.shl(x86::eax, imm(sh));            // scale in 32-bit: (Ra<<sh)[31:0] == ((RAV<<sh)+..)[31:0]
            if (islit) {
                if (issub) a.sub(x86::eax, imm(lit));
                else       a.add(x86::eax, imm(lit));
            } else if (rb != 31) {
                mov_from_reg32(x86::ecx, rb);
                if (issub) a.sub(x86::eax, x86::ecx);
                else       a.add(x86::eax, x86::ecx);
            }
            a.movsxd(x86::rax, x86::eax);
            break;
        }
        default: break;
        }

        if (rc != 31 && !result_in_dest) mov_to_reg(rc, x86::rax);
        if ((op == OP_ADDL || op == OP_SUBL || op == OP_ADDQ || op == OP_SUBQ
             || op == OP_AND || op == OP_BIS || op == OP_XOR)
            && rc != prev_dpc_base)
            preserve_dpc();
    } while (0);
}

// Shared entry: set up the frame, load the pins, jump to the block body. Shared exit: sync the pins
// back to regs[], tear the frame down, return EAX (instructions completed). Rebuilt with the runtime.
void CJitEngine::build_trampolines()
{
  using namespace asmjit;
  JitRuntime* rt = (JitRuntime*) m_rt;
  CallConv cc;
  if (cc.init(CallConvId::kCDecl, rt->environment()) != Error::kOk) return;
  const uint8_t* gpa = cc.passed_order(RegGroup::kGp);
  auto aq = [&](int i) { return x86::gpq(gpa[i]); };
  {
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbx);
    a.push(x86::rbp);
    a.push(x86::r14);            // callee-saved pin for Alpha R30 (SP)
    a.push(x86::r12);            // callee-saved guest-register pins
    a.push(x86::r13);            // chain instruction count
    a.push(x86::r15);
#ifdef _WIN32
    a.push(x86::rsi);            // callee-saved on Win64: pin for Alpha R27 (PV)
    a.push(x86::rdi);            // callee-saved on Win64: pin for Alpha R0 (v0)
#endif
    a.sub(x86::rsp, imm(56));    // 32 shadow + helper scratch/out slots; 6/8 pushes -> 16-byte alignment
    a.mov(x86::rax, aq(2));      // body (arg 2), before the pins claim r8/r9
    a.mov(x86::rbp, aq(0));      // cpu  (arg 0)
    a.mov(x86::rbx, aq(1));      // regs (arg 1)
    a.xor_(x86::r13d, x86::r13d);                 // chain instruction count := 0
    a.mov(x86::r12, x86::qword_ptr(x86::rbx,  1 * 8));   // R1
    a.mov(x86::r15, x86::qword_ptr(x86::rbx, 16 * 8));   // R16 (a0)
    a.mov(x86::r14, x86::qword_ptr(x86::rbx, 30 * 8));   // R30 (SP)
    a.mov(x86::r8,  x86::qword_ptr(x86::rbx, kVolPins[0] * 8));   // R22, caller-saved global pin
    a.mov(x86::r9,  x86::qword_ptr(x86::rbx, kVolPins[1] * 8));   // R23, caller-saved global pin
#ifdef _WIN32
    a.mov(x86::rsi, x86::qword_ptr(x86::rbx, 27 * 8));   // R27 (PV)
    a.mov(x86::rdi, x86::qword_ptr(x86::rbx,  0 * 8));   // R0 (v0)
#endif
    a.jmp(x86::rax);
    JitEnterFn fn = nullptr;
    if (rt->add(&fn, &code) != Error::kOk) { fprintf(stderr, "[JIT][CPU%d] FATAL: entry trampoline\n", m_cpu_id); abort(); }
    m_enter = fn;
  }
  {
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.mov(x86::qword_ptr(x86::rbx,  1 * 8), x86::r12);   // R1
    a.mov(x86::qword_ptr(x86::rbx, 16 * 8), x86::r15);   // R16 (a0)
    a.mov(x86::qword_ptr(x86::rbx, 30 * 8), x86::r14);   // R30 (SP)
    a.mov(x86::qword_ptr(x86::rbx, kVolPins[0] * 8), x86::r8);    // caller-saved global pins
    a.mov(x86::qword_ptr(x86::rbx, kVolPins[1] * 8), x86::r9);
#ifdef _WIN32
    a.mov(x86::qword_ptr(x86::rbx, 27 * 8), x86::rsi);   // R27 (PV)
    a.mov(x86::qword_ptr(x86::rbx,  0 * 8), x86::rdi);   // R0 (v0)
#endif
    a.add(x86::rsp, imm(56));
#ifdef _WIN32
    a.pop(x86::rdi);
    a.pop(x86::rsi);
#endif
    a.pop(x86::r15);
    a.pop(x86::r13);
    a.pop(x86::r12);
    a.pop(x86::r14);
    a.pop(x86::rbp);
    a.pop(x86::rbx);
    a.ret();
    void* fn = nullptr;
    if (rt->add(&fn, &code) != Error::kOk) { fprintf(stderr, "[JIT][CPU%d] FATAL: exit trampoline\n", m_cpu_id); abort(); }
    m_exit = fn;
    if (m_exit_slot) *m_exit_slot = m_exit;
  }
}

void CJitEngine::compile_block(JitBlock* b, const AlphaInstructionSource& sources, void* read_helper, void* write_helper, void* opcdec_helper, void* hw_mfpr_helper, void* hw_ld_helper, void* hw_mtpr_helper, void* hw_st_helper, void* indirect_helper, void* read_locked_helper, void* stc_helper, void* misc_helper, void* read_vpte_helper, void* read_wchk_helper, void* itof_helper, void* ftoi_helper, void* fltl_helper, void* fp_read_helper, void* fp_write_helper, void* fltv_helper)
{
  // Only the interpreted miss path calls this for an uncompiled block. Keep the
  // first encounter interpreted; compiled dispatch and chaining pay no counter cost.
  if (b->compile_encounters < 2) ++b->compile_encounters;
  if (b->compile_encounters < 2) return;

  using namespace asmjit;
  // Reclaim must self-trigger here, NOT only in flush(): flush() runs when the guest executes
  // IMB/IC_FLUSH, and a compute-heavy phase can go minutes without one while recompiles keep
  // allocating -- code memory grew unbounded (multi-GB). Safe: we're in this CPU's cold path.
  if (m_rt && m_code_bytes >= kReclaimBytes) {
    reclaim_code();
    b->valid = true;   // b was just (re)validated by record(); restore it after the wipe
  }
  b->compiled = true;
  b->jit_body_same_page = nullptr;

  if (b->n_instr == 0) return;
  const auto source = sources.view(b->tag);
  if (!source.contains(1) || source.identity != b->phys
      || source.asm_global != b->asm_global) {
    b->compiled = false;
    return;
  }

  // PALmode blocks (PC bit 0) use the register-bank mapping captured in b->pal_shadow
  const bool pal_block = (b->tag & 1) != 0;

  // The family bounds a contiguous instruction source. Copy it before another
  // query can invalidate the borrowed view, including optional fusion lookahead.
  const uint32_t scan_len = (std::min)((std::min)(b->n_instr, uint32_t{64}),
                                      source.word_count);
  const uint32_t snapshot_len = (std::min)(source.word_count,
      (std::max)(scan_len, uint32_t{39}));
  std::vector<uint32_t> source_words(snapshot_len);
  if (!source.copy_words(source_words.data(), snapshot_len)) {
    b->compiled = false;
    return;
  }
  const uint32_t* const words = source_words.data();
  uint32_t plen = 0;
  bool terminator_branch = false;       // last instruction is a compiled terminator (sets its own PC)
  bool terminator_jmp = false;          // ...and it's a computed jump (don't chain: targets vary)
  while (plen < scan_len)
  {
    SafeOp sop = classify(words[plen], pal_block, m_profile.ipr_read, m_profile.ipr_write,
        m_profile.pal, m_profile.hw_load, m_profile.hw_store);
    if (sop == OP_NONE) {               // uncompilable op ends the straight-line prefix
#ifdef JIT_STATS
      const uint32_t bop = words[plen] >> 26;
      m_term_op[bop]++;                 // tally what cut this block (the coverage gap to chase)
      if (bop == 0x00)                  // CALL_PAL: also tally the function code (low 8 bits)
        m_pal_func[words[plen] & 0xFF]++;
      else if (bop == 0x1d)             // HW_MTPR: diagnostic raw instruction[15:8], not a family selector.
        m_mtpr_encoding_hi[(words[plen] >> 8) & 0xFF]++;
      else if (bop == 0x1b)             // HW_LD: tally the form (phys/virt/lock/vpte/chk, ins[15:12])
        m_hwld_func[(words[plen] >> 12) & 0xF]++;
      else if (bop == 0x18)             // MISC: tally the Ra==31 form (ins[15:12]: 0xc RPCC / 0xe RC / 0xf RS)
        m_misc_func[(words[plen] >> 12) & 0xF]++;
      // Punch list: one-shot print of the first ACTIONABLE breaker -- skip the opcodes whose
      // compilable subset is already settled, so it points at the next NEW target rather than a
      // decided one: 0x00 CALL_PAL (terminator), 0x1b HW_LD / 0x1f HW_ST (physical done;
      // conditional/virtual forms side-effecting), 0x1d HW_MTPR (pure-store IPRs done; rest
      // side-effecting), 0x10 INTA + 0x13 INTM (non-trapping ops done; only /V overflow-trap variants left),
      // 0x18 MISC (barriers/hints + RPCC/RC/RS via log/replay done; only the rare Ra==31 RC/RS forms interpret),
      // 0x14 ITFP (ITOF* moves done; SQRT* = the deferred FP-math class: FPCR/rounding/traps),
      // 0x17 FLTL (non-arithmetic done; only CVTQL/V trap variants left).
      // JMP (0x1a) + HW_RET (0x1e) are now compiled+chained. Stats count all.
      if (!m_first_breaker_logged && bop != 0x00 && bop != 0x1b && bop != 0x1d && bop != 0x1f && bop != 0x10 && bop != 0x18 && bop != 0x13 && bop != 0x14 && bop != 0x17) {
        m_first_breaker_logged = true;
        printf("[JIT][PUNCH][CPU%d] first unhandled breaker: %s(0x%02x) ins=%08x at pc=%016llx%s\n",
               m_cpu_id, jit_opcode_name(bop), bop, words[plen],
               (unsigned long long) ((b->tag & ~(uint64_t) 1) + (uint64_t) plen * 4),
               pal_block ? "  [PALmode]" : "");
      }
#endif
      break;
    }
    plen++;
    if (is_terminator(sop)) {           // branch or computed jump ends the block
      terminator_branch = true;
      if (sop == OP_JMP || sop == OP_HW_RET) terminator_jmp = true;
      break;
    }
  }

  if (plen == 0) return;

  // counted-substring outer loop. 
  // Most candidates fail on byte zero. If we're lucky. That's a good outcome.
  // Recognize the instruction sequence and scan those first bytes 16 at a time. 
  // Potential 22 guest instruction shortcut. 
  bool counted_scan_fused = false;
  // Full-word portion of OpenVMS byte-search routine.
  bool word_scan_fused = false;
  bool byte_scan_fused = false;
  bool tail_scan_fused = false;
#if defined(_WIN32) && !defined(JIT_VERIFY)
  if (!pal_block && plen == 6 && snapshot_len >= 39) {
    static const uint32_t head[15] = {
      0x40020536, 0x47e0341c, 0x400203b7, 0x46df04dc, 0x4797041c, 0xe3800022,
      0x2ffe0000, 0x2ffe0000, 0x2ae10000, 0x2b830000, 0x40003120, 0x20630001,
      0x20210001, 0x42fc0536, 0xf6c00011
    };
    static const uint32_t mismatch[7] = {
      0x40c03126, 0x47e40400, 0x47e50401, 0x20e70001,
      0x47e60402, 0x47e70403, 0xc3ffffd9
    };
    counted_scan_fused = memcmp(words, head, sizeof(head)) == 0
                      && memcmp(words + 32, mismatch, sizeof(mismatch)) == 0;
  }
  if (!pal_block && plen == 5 && snapshot_len >= 10) {
    static const uint32_t word_scan[10] = {
      0xa6e10000, 0x42fc01f6, 0x439701f8, 0x46d80016, 0xf6c00028,
      0x40011120, 0x40211401, 0xf81ffff8, 0x40011400, 0xe4000007
    };
    word_scan_fused = memcmp(words, word_scan, sizeof(word_scan)) == 0;
  }
  // Inner byte-search loop.
  // R23 contains the aligned source word
  // Compare from R1&7 until R27 matches, R0 reaches zero, or R1 reaches the next word.
  if (!pal_block && plen == 3 && snapshot_len >= 8) {
    static const uint32_t byte_scan[8] = {
      0x4ae100d8, 0x43780536, 0xe6c0001e,
      0x40003520, 0x40203401, 0xe400001b,
      0x4420f016, 0xf6dffff8
    };
    byte_scan_fused = memcmp(words, byte_scan, sizeof(byte_scan)) == 0;
  }
  // End of the inner loop search.
  // R23 remains the loaded word and R0 is the remaining byte count.
  // Each mismatch executes the 3 op update block.
  if (!pal_block && plen == 3 && snapshot_len >= 6) {
    static const uint32_t tail_scan[6] = {
      0x4ae100d8, 0x431b0538, 0xe7000003,
      0x40003120, 0x40203401, 0xf41ffffa
    };
    tail_scan_fused = memcmp(words, tail_scan, sizeof(tail_scan)) == 0;
  }
#endif
  const uint32_t hash_len = counted_scan_fused ? 39u : byte_scan_fused ? 8u
                          : tail_scan_fused ? 6u : word_scan_fused ? 10u : plen;
  assert(hash_len <= source_words.size());
  const uint64_t source_hash = src_hash(
      reinterpret_cast<const uint8_t*>(source_words.data()), hash_len);
#ifdef JIT_REGPROF
  b->rp_mask = regprof_mask(words, plen);   // GPR-access fingerprint; exec-weighted at report time
#endif

  // Emit  uint32_t fn(CAlphaCPU* cpu, uint64_t* regs)  (Win64: cpu=RCX, regs=RDX).
  // Keep cpu in RBP and regs in RBX (callee-saved, so they survive helper calls);
  // reserve a 40-byte frame (32 shadow + 8 load-out slot) that keeps RSP 16-aligned
  // for calls. RAX = op1/result, RCX = operand2 (CL for variable shifts).
  JitErrorHandler eh;
  eh.cpu_id = m_cpu_id;
#ifdef JIT_DISASM
  eh.fp = m_disasm_fp;
  StringLogger logger;
#endif
  CodeHolder code;
  if (code.init(((JitRuntime*) m_rt)->environment()) != Error::kOk) {
    m_reclaim_pending = true;   // init only fails on OOM here: free code at the next
    b->compiled = false;        // safe point and retry, don't latch the block interpreted
    return;
  }
  code.set_error_handler(&eh);
#ifdef JIT_DISASM
  // Dev: capture this block's disassembly, validate each emitted instruction, and trap any
  // emit failure (dumped + bailed near rt->add() below). Logging formats every instruction.
  code.set_logger(&logger);
#endif
  x86::Assembler a(&code);
#ifdef JIT_DISASM
  a.add_diagnostic_options(DiagnosticOptions::kValidateAssembler);
#endif

  // Host integer-argument registers:
  // Win64 {rcx,rdx,r8,r9}; System V {rdi,rsi,rdx,rcx}. aq(i)/ad(i) = the i argument as a
  // 64-/32-bit register; emit_call (below) marshals into them, replacing out usage of 
  // #ifdef _WIN32. Helpers limited to <= 4 integer arguments.
  CallConv cc;
  if (cc.init(CallConvId::kCDecl, ((JitRuntime*) m_rt)->environment()) != Error::kOk) return;
  const uint8_t* gpa = cc.passed_order(RegGroup::kGp);
  auto aq = [&](int i) { return x86::gpq(gpa[i]); };
  auto ad = [&](int i) { return x86::gpd(gpa[i]); };
  // cpu (RBP), regs (RBX), chain counter (R13), and callee-saved guest pins
  // all hold live values across helper calls, so they must be callee-saved under the host ABI.
  // Verified at dev time (compiled out in NDEBUG builds).
  [[maybe_unused]] const uint32_t kPinnedGp =
      (1u << x86::rbp.id()) | (1u << x86::rbx.id()) | (1u << x86::r14.id())
    | (1u << x86::r12.id()) | (1u << x86::r13.id()) | (1u << x86::r15.id())
#ifdef _WIN32
    | (1u << x86::rsi.id()) | (1u << x86::rdi.id())
#endif
    ;
  assert((((uint32_t) cc.preserved_regs(RegGroup::kGp)) & kPinnedGp) == kPinnedGp);

  // The frame (pushes, 56-byte scratch, pins in r12/r15/r14/r8/r9[/rsi/rdi]) is set up by the
  // engine's shared entry trampoline (build_trampolines); every exit jumps to the shared epilogue.
  Label done = a.new_label();  // shared exit: `jmp [cpu + jit_exit]` (EAX preset by caller)
  Label body = a.new_label();  // chained re-entry
  const Label body_same_page = a.new_label();
  const Label exec_proof = a.new_label(), exec_denied = a.new_label();
  size_t exec_mask_imm_off = 0;
  a.bind(body);
  const size_t body_off = code.code_size();   // byte offset of the chained entry from fn
  if (!pal_block) {
    // Check instruction access for CM. Static same-page branches use body_same_page;
    // their source checked the same 8 KiB page. Authorization patches the imm8.
    // jit_mode_bit is refreshed on C entry and compiled CM writes.
    if (a.test(x86::byte_ptr(x86::rbp, m_off.jit_mode_bit), imm(0)) != Error::kOk)
      return;
    exec_mask_imm_off = a.offset() - 1; // TEST r/m8, imm8 ends with its one-byte immediate
    a.jz(exec_denied);
    const bool had_optimized_align = a.has_encoding_option(EncodingOptions::kOptimizedAlign);
    a.add_encoding_options(EncodingOptions::kOptimizedAlign);
    const Error align_err = a.align(AlignMode::kCode, 16);
    if (!had_optimized_align)
      a.clear_encoding_options(EncodingOptions::kOptimizedAlign);
    if (align_err != Error::kOk)
      return;
  }
  a.bind(body_same_page);
  const size_t body_same_page_off = code.code_size();
#ifdef JIT_REGPROF
  a.mov(x86::rax, imm((uint64_t) &b->rp_hits));   // REGPROF: count every execution (cold entry + chained re-entry)
  a.inc(x86::qword_ptr(x86::rax));                // RAX is dead at body entry -- the first op reloads it
#endif

  // Successful non-PAL exits keep their next PC in R10 while chaining. 
  // PAL code publishes immediately.
  auto set_pc = [&](uint64_t pc_val) {
    a.mov(x86::r10, imm(pc_val));
    if (pal_block)
      a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
  };

  // Block register allocator: callee-saved and volatile guest pins remain live across chains.
  RegAlloc ra;
  for (int r = 0; r < 32; ++r) ra.host[r] = -1;
  ra.rax_holds = -1;
  ra.vol_bind = kVolPins[0];
  ra.vol_bind2 = kVolPins[1];
  ra.dpc_live = false;
  ra.dpc_base = ra.dpc_disp = 0;
  ra.dpc_write = ra.dpc_force_align = false;
  ra.exit_kind = 0; ra.exit_cc = 0; ra.exit_tgt = ra.exit_fall = 0;
  // Non-PAL only; fused scans pick their next PC in R10 at runtime.
#ifndef JIT_VERIFY
  ra.direct_exits = DirectPatch && !pal_block
      && !(counted_scan_fused || word_scan_fused || byte_scan_fused || tail_scan_fused);
#else
  ra.direct_exits = false;
#endif
#ifdef JIT_STATS
  ra.licm_slots = nullptr; ra.licm_n = ra.licm_max = 0;   // probe regions only, not blocks
#endif
  ra.host[kGlobalPins[0]] = (int) x86::r12.id();
  ra.host[kGlobalPins[1]] = (int) x86::r15.id();
  ra.host[30] = (int) x86::r14.id();                  // SP (reclaimed r14), all platforms
  ra.host[kVolPins[0]] = (int) x86::r8.id();                   // volatile pins; helpers spill/reload them
  ra.host[kVolPins[1]] = (int) x86::r9.id();
#ifdef _WIN32
  ra.host[27] = (int) x86::rsi.id();                  // PV (Win64)
  ra.host[0]  = (int) x86::rdi.id();                  // v0 (Win64)
#endif

  const HelperSet hs = { read_helper, write_helper, opcdec_helper, hw_mfpr_helper, hw_ld_helper,
                       hw_mtpr_helper, hw_st_helper, indirect_helper, read_locked_helper, stc_helper,
                       misc_helper, read_vpte_helper, read_wchk_helper, itof_helper, ftoi_helper,
                       fltl_helper, fp_read_helper, fp_write_helper, fltv_helper };

  std::vector<ColdMemStub> cold;   // outlined memop slow paths, emitted after the epilogue
  Label fused_done = a.new_label();
  if (word_scan_fused) {
#ifdef _WIN32
    Label normal = a.new_label(), scan = a.new_label(), found = a.new_label();
    Label exhausted = a.new_label(), capped = a.new_label(), masks = a.new_label();
    Label remainder = a.new_label();

    // Preserve the normal backward-edge delivery point if it would be cut short.
    a.mov(x86::eax, imm(kChainCeiling));
    a.sub(x86::rax, x86::r13);
    a.cmp(x86::rax, imm(66)); a.jb(normal);
    emit_pending_check(a, m_off, normal);
    a.test(x86::r12, imm(7)); a.jnz(normal);              // R1 must be word-aligned
    a.test(x86::rdi, x86::rdi); a.js(normal);             // loop entry requires R0 >= 0

    // Validate the read DPC entry once. 
    // every batched word remains on this 8 KB page. 
    // R10 becomes the host bias used with the live virtual R1.
    a.mov(x86::r10, x86::r12); a.and_(x86::r10, imm(~(uint64_t)0x1fff));
    if (host_has_bmi2()) a.rorx(x86::rax, x86::r12, imm(8));
    else { a.mov(x86::rax, x86::r12); a.shr(x86::rax, imm(8)); }
    a.and_(x86::rax, imm(0x7e0));
    a.cmp(x86::qword_ptr(x86::rbp, x86::rax, 0, m_off.dpc_virt_page), x86::r10);
    a.jne(normal);
    a.mov(x86::r10, x86::qword_ptr(x86::rbp, x86::rax, 0, m_off.dpc_host_bias));
    a.test(x86::r10, x86::r10); a.jz(normal);

    // Batch cap = min(8, words remaining in this page).
    a.mov(x86::rax, x86::r12); a.and_(x86::eax, imm(0x1fff));
    a.mov(x86::r11d, imm(0x2000)); a.sub(x86::r11, x86::rax); a.shr(x86::r11, imm(3));
    a.cmp(x86::r11, imm(8));
    { Label cap_ok = a.new_label(); a.jbe(cap_ok); a.mov(x86::r11d, imm(8)); a.bind(cap_ok); }
    a.movq(x86::xmm1, x86::qword_ptr(x86::rbx, 28 * 8));    // repeated search byte

    a.bind(scan);
    a.mov(x86::r9, x86::qword_ptr(x86::r10, x86::r12));     // architectural R23
    a.movq(x86::xmm0, x86::r9); a.pcmpeqb(x86::xmm0, x86::xmm1);
    a.pmovmskb(x86::eax, x86::xmm0); a.and_(x86::eax, imm(255));
    a.test(x86::eax, x86::eax); a.jnz(found);
    a.add(x86::r12, imm(8));
    a.sub(x86::edi, imm(8)); a.movsxd(x86::rdi, x86::edi);
    a.add(x86::r13, imm(8));
    a.test(x86::rdi, x86::rdi); a.js(exhausted);
    a.dec(x86::r11); a.jnz(scan);
    a.jmp(capped);

    a.bind(found);
    a.mov(x86::r8d, x86::eax);                              // R22 equality mask
    a.jmp(masks);

    a.bind(exhausted);
    a.xor_(x86::r8d, x86::r8d);
    a.add(x86::rdi, imm(8));                                // undo guest ADDQ at +0x1c
    a.add(x86::r13, imm(2));                                // ADDQ + BEQ
    a.test(x86::rdi, x86::rdi); a.jnz(remainder);
    set_pc(b->tag + 0x44);                                  // exact multiple: common return
    a.jmp(masks);

    a.bind(remainder);
    set_pc(b->tag + 0x28);                                  // 1..7 tail bytes
    a.jmp(masks);

    a.bind(capped);
    a.xor_(x86::r8d, x86::r8d);
    set_pc(b->tag);                                         // next translated page/batch

    a.bind(masks);
    // R24 = CMPBGE(R28,R23), required at every architectural exit.
    a.movq(x86::xmm0, x86::r9); a.movdqa(x86::xmm2, x86::xmm1);
    a.pmaxub(x86::xmm2, x86::xmm0); a.pcmpeqb(x86::xmm2, x86::xmm1);
    a.pmovmskb(x86::eax, x86::xmm2); a.and_(x86::eax, imm(255));
    a.mov(x86::qword_ptr(x86::rbx, 24 * 8), x86::rax);
    // Prior empty words contributed eight ops each. 
    // The shared epilogue adds the current block's five
    // so remove those five on paths that already counted it.
    { Label count_done = a.new_label();
      a.test(x86::r8, x86::r8); a.jnz(count_done);
      a.sub(x86::r13, imm(5));
      a.bind(count_done); }
    { Label pc_done = a.new_label();
      a.test(x86::r8, x86::r8); a.jz(pc_done);
      set_pc(b->tag + 0xb4);                                // found target
      a.bind(pc_done); }
    a.jmp(fused_done);
    a.bind(normal);
#endif
  }
  if (tail_scan_fused) {
#ifdef _WIN32
    Label normal = a.new_label(), no_match = a.new_label(), commit = a.new_label();

    a.mov(x86::eax, imm(kChainCeiling));
    a.sub(x86::rax, x86::r13);
    a.cmp(x86::rax, imm(48)); a.jb(normal);
    emit_pending_check(a, m_off, normal);
    a.cmp(x86::rdi, imm(1)); a.jb(normal);
    a.cmp(x86::rdi, imm(8)); a.ja(normal);
    a.mov(x86::rax, x86::rsi); a.cmp(x86::rax, imm(255)); a.ja(normal);

    // Compare the R0 live lanes beginning at R1&7.
    a.mov(x86::rdx, x86::r12); a.and_(x86::edx, imm(7));
    a.mov(x86::r10d, imm(8)); a.sub(x86::r10, x86::rdx);
    a.cmp(x86::rdi, x86::r10); a.ja(normal);             // never read beyond the loaded word
    a.movq(x86::xmm0, x86::r9);
    a.mov(x86::eax, x86::esi); a.imul(x86::eax, x86::eax, imm(0x01010101));
    a.movd(x86::xmm1, x86::eax); a.pshufd(x86::xmm1, x86::xmm1, imm(0));
    a.pcmpeqb(x86::xmm0, x86::xmm1); a.pmovmskb(x86::eax, x86::xmm0);
    a.mov(x86::ecx, x86::edx); a.shr(x86::eax, x86::cl);
    a.mov(x86::ecx, x86::edi); a.mov(x86::edx, imm(1));
    a.shl(x86::edx, x86::cl); a.dec(x86::edx); a.and_(x86::eax, x86::edx);
    a.test(x86::eax, x86::eax); a.jz(no_match);

    // k mismatches followed by a match, each mismatch consumes six guest ops;
    // the comparison block's three are added by the shared epilogue.
    a.bsf(x86::eax, x86::eax);
    a.sub(x86::rdi, x86::rax); a.add(x86::r12, x86::rax);
    a.mov(x86::qword_ptr(x86::rbx, 24 * 8), imm(0));
    a.imul(x86::rax, x86::rax, imm(6)); a.add(x86::r13, x86::rax);
    set_pc(b->tag + 0x18);
    a.jmp(commit);

    a.bind(no_match);
    // Count end: preserve the final byte except search result in R24.
    a.mov(x86::rax, x86::r12); a.and_(x86::eax, imm(7));
    a.add(x86::rax, x86::rdi); a.dec(x86::rax); a.shl(x86::eax, imm(3));
    a.mov(x86::ecx, x86::eax); a.mov(x86::rax, x86::r9); a.shr(x86::rax, x86::cl);
    a.and_(x86::eax, imm(255)); a.sub(x86::rax, x86::rsi);
    a.mov(x86::qword_ptr(x86::rbx, 24 * 8), x86::rax);
    a.mov(x86::rax, x86::rdi); a.imul(x86::rax, x86::rax, imm(6));
    a.sub(x86::rax, imm(3)); a.add(x86::r13, x86::rax);
    a.add(x86::r12, x86::rdi); a.xor_(x86::edi, x86::edi);
    set_pc(b->tag + 0x18);

    a.bind(commit);
    a.jmp(fused_done);
    a.bind(normal);
#endif
  }
  if (byte_scan_fused) {
#ifdef _WIN32
    Label normal = a.new_label(), no_match = a.new_label(), exhausted = a.new_label();
    Label commit = a.new_label();

    // The collapsed path accounts for at most 64 guest instructions. 
    // Keep it in sync with expected length. 
    a.mov(x86::eax, imm(kChainCeiling));
    a.sub(x86::rax, x86::r13);
    a.cmp(x86::rax, imm(64)); a.jb(normal);
    emit_pending_check(a, m_off, normal);
    a.test(x86::rdi, x86::rdi); a.jz(normal);               // R0==0 would wrap on mismatch
    a.mov(x86::rax, x86::rsi); a.cmp(x86::rax, imm(255)); a.ja(normal);

    // remaining = 8-(R1&7); count = min(R0, remaining).
    a.mov(x86::rdx, x86::r12); a.and_(x86::edx, imm(7));
    a.mov(x86::r10d, imm(8)); a.sub(x86::r10, x86::rdx);
    a.mov(x86::r11, x86::rdi);
    a.cmp(x86::r11, x86::r10); a.cmova(x86::r11, x86::r10);

    // Compare the loaded word with the search byte, then discard before
    // R1 and beyond the count.
    a.movq(x86::xmm0, x86::r9);
    a.mov(x86::eax, x86::esi); a.imul(x86::eax, x86::eax, imm(0x01010101));
    a.movd(x86::xmm1, x86::eax); a.pshufd(x86::xmm1, x86::xmm1, imm(0));
    a.pcmpeqb(x86::xmm0, x86::xmm1); a.pmovmskb(x86::eax, x86::xmm0);
    a.mov(x86::ecx, x86::edx); a.shr(x86::eax, x86::cl);
    a.mov(x86::ecx, x86::r11d); a.mov(x86::edx, imm(1));
    a.shl(x86::edx, x86::cl); a.dec(x86::edx); a.and_(x86::eax, x86::edx);
    a.test(x86::eax, x86::eax); a.jz(no_match);

    a.bsf(x86::eax, x86::eax);                              // preceding mismatches, 0..7
    a.sub(x86::rdi, x86::rax); a.add(x86::r12, x86::rax);
    a.mov(x86::qword_ptr(x86::rbx, 24 * 8), x86::rsi);      // matching extracted byte
    a.xor_(x86::r8d, x86::r8d);                             // R22 = R27-R24 = 0
    a.shl(x86::rax, imm(3)); a.add(x86::r13, x86::rax);     // eight instructions/mismatch
    set_pc(b->tag + 0x84);                                  // loop's common return
    a.jmp(commit);

    a.bind(no_match);
    // Recreate the final tested byte in R24 before updating R1.
    a.mov(x86::rax, x86::r12); a.and_(x86::eax, imm(7));
    a.add(x86::rax, x86::r11); a.dec(x86::rax); a.shl(x86::eax, imm(3));
    a.mov(x86::ecx, x86::eax); a.mov(x86::rax, x86::r9); a.shr(x86::rax, x86::cl);
    a.and_(x86::eax, imm(255));
    a.mov(x86::qword_ptr(x86::rbx, 24 * 8), x86::rax);
    a.sub(x86::rdi, x86::r11); a.add(x86::r12, x86::r11);
    a.cmp(x86::r11, x86::r10); a.jb(exhausted);
    a.cmp(x86::rdi, imm(0)); a.je(exhausted);                // BEQ wins if count and boundary coincide

    // Word boundary after N mismatches: 8*N guest instructions. 
    // Epilogue is first 3. 
    a.xor_(x86::r8d, x86::r8d);
    a.mov(x86::rax, x86::r11); a.shl(x86::rax, imm(3)); a.sub(x86::rax, imm(3));
    a.add(x86::r13, x86::rax);
    set_pc(b->tag + 0x20);
    a.jmp(commit);

    a.bind(exhausted);
    // R0 became zero before the boundary test: 8*N-2 instructions. 
    // R22 keeps the subtraction from the final byte comparison.
    a.mov(x86::r8, x86::rsi); a.sub(x86::r8, x86::rax);
    a.mov(x86::rax, x86::r11); a.shl(x86::rax, imm(3)); a.sub(x86::rax, imm(5));
    a.add(x86::r13, x86::rax);
    set_pc(b->tag + 0x84);

    a.bind(commit);
    a.jmp(fused_done);
    a.bind(normal);
#endif
  }
  if (counted_scan_fused) {
#ifdef _WIN32
    Label normal = a.new_label(), scan = a.new_label(), scalar = a.new_label();
    Label scalar_loop = a.new_label(), found = a.new_label(), commit = a.new_label();

    // Require room for the maximum 16-candidate batch. 
    a.mov(x86::eax, imm(kChainCeiling));
    a.sub(x86::rax, x86::r13);
    a.cmp(x86::rax, imm(16 * 22)); a.jb(normal);
    emit_pending_check(a, m_off, normal);

    // candidates = R6 - R4 + 1, capped at 16 and at the current 8 KB haystack page.
    a.mov(x86::rdx, x86::qword_ptr(x86::rbx, 6 * 8));
    a.sub(x86::rdx, x86::qword_ptr(x86::rbx, 4 * 8)); a.jb(normal);
    a.inc(x86::rdx);
    a.mov(x86::rax, imm(16));
    a.cmp(x86::rdx, x86::rax); a.cmova(x86::rdx, x86::rax);
    a.mov(x86::rcx, x86::qword_ptr(x86::rbx, 7 * 8));
    a.mov(x86::rax, x86::rcx); a.and_(x86::eax, imm(0x1fff));
    a.mov(x86::r10d, imm(0x2000)); a.sub(x86::r10, x86::rax);
    a.cmp(x86::rdx, x86::r10); a.cmova(x86::rdx, x86::r10);

    // Resolve the needle (R5) and haystack (R7/RCX) read-cache slots. 
    // The usual DPC tag check suffices. Miss? Go back to normal block.
    auto fused_bias = [&](const x86::Gp& va, const x86::Gp& bias) {
      a.mov(x86::r10, va); a.and_(x86::r10, imm(~(uint64_t)0x1fff));
      if (host_has_bmi2()) a.rorx(x86::rax, va, imm(8));  // stride=32: ((va>>13)&63)<<5
      else { a.mov(x86::rax, va); a.shr(x86::rax, imm(8)); }
      a.and_(x86::rax, imm(0x7e0));
      a.cmp(x86::qword_ptr(x86::rbp, x86::rax, 0, m_off.dpc_virt_page), x86::r10);
      a.jne(normal);
      a.mov(bias, x86::qword_ptr(x86::rbp, x86::rax, 0, m_off.dpc_host_bias));
    };
    a.mov(x86::rax, x86::qword_ptr(x86::rbx, 5 * 8));
    fused_bias(x86::rax, x86::r10);
    a.mov(x86::qword_ptr(x86::rsp, 48), x86::r10);
    fused_bias(x86::rcx, x86::r11);
    a.mov(x86::r10, x86::qword_ptr(x86::rsp, 48));

    // Broadcast needle[0], then locate the next equal byte. 
    // If no vector matches, send the whole 16-byte groups and resume outer-loop head.
    a.mov(x86::rax, x86::qword_ptr(x86::rbx, 5 * 8));
    a.movzx(x86::eax, x86::byte_ptr(x86::r10, x86::rax));
    a.imul(x86::eax, x86::eax, imm(0x01010101));
    a.movd(x86::xmm0, x86::eax); a.pshufd(x86::xmm0, x86::xmm0, imm(0));
    a.cmp(x86::rdx, imm(16)); a.jb(scalar);
    a.bind(scan);
    a.movdqu(x86::xmm1, x86::oword_ptr(x86::r11, x86::rcx));
    a.pcmpeqb(x86::xmm1, x86::xmm0); a.pmovmskb(x86::eax, x86::xmm1);
    a.test(x86::eax, x86::eax); a.jnz(found);
    a.add(x86::rcx, imm(16)); a.sub(x86::rdx, imm(16));
    a.cmp(x86::rdx, imm(16)); a.jae(scan);
    a.bind(scalar);
    a.movd(x86::eax, x86::xmm0);              // AL = needle[0]
    a.test(x86::rdx, x86::rdx); a.jz(commit);
    a.bind(scalar_loop);
    a.cmp(x86::byte_ptr(x86::r11, x86::rcx), x86::al); a.je(found);
    a.inc(x86::rcx); a.dec(x86::rdx); a.jnz(scalar_loop);
    a.jmp(commit);
    a.bind(found);
    // Vector hits carry a nonzero mask. scalar hits arrive with EAX holding only needle[0].
    // Distinguish by remaining>=16 (outer hasn't subbed yet).
    { Label scalar_found = a.new_label();
      a.cmp(x86::rdx, imm(16)); a.jb(scalar_found);
      a.bsf(x86::eax, x86::eax); a.add(x86::rcx, x86::rax);
      a.bind(scalar_found); }
    a.cmp(x86::rcx, x86::qword_ptr(x86::rbx, 7 * 8)); a.je(normal);

    a.bind(commit);
    // Reproduce N mismatch continuations: R6-=N R7+=N R0/R1/R2/R3=R4/R5/R6/R7.
    a.mov(x86::rax, x86::qword_ptr(x86::rbx, 5 * 8));
    a.movzx(x86::r9d, x86::byte_ptr(x86::r10, x86::rax));         // R23 = needle byte
    a.movzx(x86::eax, x86::byte_ptr(x86::r11, x86::rcx, 0, -1)); // R28 = last mismatch
    a.mov(x86::qword_ptr(x86::rbx, 28 * 8), x86::rax);
    a.mov(x86::r8, x86::r9); a.sub(x86::r8, x86::rax);           // R22 = R23 - R28
#ifdef JIT_STATS
    a.mov(x86::r10, imm((uint64_t)&m_licm_same)); a.inc(x86::qword_ptr(x86::r10));
#endif
    a.mov(x86::rax, x86::rcx); a.sub(x86::rax, x86::qword_ptr(x86::rbx, 7 * 8));
    a.mov(x86::r10, x86::qword_ptr(x86::rbx, 6 * 8)); a.sub(x86::r10, x86::rax);
    a.mov(x86::qword_ptr(x86::rbx, 6 * 8), x86::r10);
    a.mov(x86::qword_ptr(x86::rbx, 2 * 8), x86::r10);
    a.mov(x86::qword_ptr(x86::rbx, 7 * 8), x86::rcx);
    a.mov(x86::qword_ptr(x86::rbx, 3 * 8), x86::rcx);
    a.mov(x86::rdi, x86::qword_ptr(x86::rbx, 4 * 8));
    a.mov(x86::r12, x86::qword_ptr(x86::rbx, 5 * 8));
    a.imul(x86::rax, x86::rax, imm(22)); a.sub(x86::rax, imm(6));
    a.add(x86::r13, x86::rax);
    set_pc(b->tag);
    a.jmp(fused_done);

    a.bind(normal);
#endif
  }
  for (uint32_t i = 0; i < plen; ++i) {
      emit_op(&a, gpa, &done, hs, pal_block, b, words[i], i, ra, &cold, true);
  }
  a.bind(fused_done);

  // Epilogue. Count this block's instructions, then chain into the next block (staying
  // in native code) or return to the dispatcher.
#ifndef JIT_VERIFY
  // Gate the chain: stop if we've hit the budget ceiling or an interrupt/timer is pending
  // Interrupts are architecturally deferred while the target remains in PALmode.
  auto stat_exit = [&](int k) {
#ifdef JIT_STATS
    a.mov(x86::rax, imm((uint64_t) exit_kind_counter(k))); a.inc(x86::qword_ptr(x86::rax));
#else
    (void) k;
#endif
  };
  auto stat_gate_exit = [&]() {   // at an exit_chain label: split ceiling from pending/other
#ifdef JIT_STATS
    Label nc = a.new_label(), e = a.new_label();
    a.cmp(x86::r13, imm(kChainCeiling)); a.jl(nc);
    stat_exit(0); a.jmp(e);
    a.bind(nc); stat_exit(1);
    a.mov(x86::rax, imm((uint64_t) &b->st_exit1)); a.inc(x86::qword_ptr(x86::rax));
    { Label t1 = a.new_label(), t2 = a.new_label();
      a.cmp(x86::byte_ptr(x86::rbp, m_off.check_timers), imm(0)); a.je(t1); stat_exit(8); a.bind(t1);
      a.cmp(x86::byte_ptr(x86::rbp, m_off.check_int), imm(0)); a.je(t2); stat_exit(9); a.bind(t2); }
    a.bind(e);
#endif
  };
  auto emit_gate = [&](Label& lbl) {
    a.cmp(x86::r13, imm(kChainCeiling)); a.jge(lbl);   // constant ceiling: no budget load
    if (!pal_block) {
      // Direct branches and JMP preserve the source mode. CALL_PAL may (always?) enter
      // PALmode. taking one pass on that cold race stays simple.
      emit_pending_check(a, m_off, lbl);
      return;
    }
    if (terminator_jmp && (words[plen - 1] >> 26) == 0x1e) {
      // HW_RET can leave PALmode. If an interrupt became pending in the handler,
      // return to the dispatcher.
      Label deferred = a.new_label();
      a.cmp(x86::byte_ptr(x86::rbp, m_off.check_int), imm(0)); a.je(deferred);
      a.test(x86::r10, imm(1)); a.jz(lbl);
      a.bind(deferred);
    }
    a.cmp(x86::byte_ptr(x86::rbp, m_off.check_timers), imm(0));  a.jne(lbl);
  };
  // Cached direct link: tail straight into our cached successor's body via its SNAPSHOT
  // {tag, vgen, body} in OUR link slots so one cache line, no dereference into the successor's
  // JitBlock. A slot hits when it maps this exit's PC (tag == R10) and was patched under the
  // current epoch. Otherwise record a patch request (m_link_from = this block) and fall back.
  auto emit_source_pal_guard = [&](Label& lbl) {
    if (pal_block) {
      // PAL-source slots are tag-keyed and rely on this guard to imply the target
      // register-bank variant. A mismatch must bypass the link-patch request.
      a.cmp(x86::byte_ptr(x86::rbp, m_off.sde), imm(b->pal_shadow ? 1 : 0)); a.jne(lbl);
    }
  };
  auto emit_chain = [&](Label& lbl) {
    Label miss = a.new_label();
    // Mapping/code invalidation clears links. Mode changes keep them; native
    // entries check CM and PAL entries check SDE.
    // Poly-link: walk the cached successor snapshots, 2-successor cache both.
    for (int sl = 0; sl < kLinkSlots; ++sl) {
      Label nxt = (sl + 1 < kLinkSlots) ? a.new_label() : miss;
      if (sl == 0) a.mov(x86::rax, imm((uint64_t) &b->link[0]));
      else         a.add(x86::rax, imm((uint32_t) sizeof(LinkSlot)));
      a.cmp(x86::qword_ptr(x86::rax, (int32_t) offsetof(LinkSlot, tag)), x86::r10); a.jne(nxt);   // not this exit's target
      if (!pal_block && terminator_branch && (words[plen - 1] >> 26) == 0x00) {
        // CALL_PAL may enter either register-bank variant.
        // Match the variant recorded in this link against the live SDE state.
        a.mov(x86::rcx, x86::qword_ptr(x86::rax, (int32_t) offsetof(LinkSlot, vgen)));
        a.shr(x86::rcx, imm(63));
        a.cmp(x86::cl, x86::byte_ptr(x86::rbp, m_off.sde)); a.jne(nxt);
      }
      // Invalid slots carry the impossible tag ~0. A matching tag therefore proves body
      // was published. Removed redundant test. 
      a.mov(x86::rcx, x86::qword_ptr(x86::rax, (int32_t) offsetof(LinkSlot, body)));
      a.jmp(x86::rcx);                                                 // HIT: tail in (shared frame)
      if (sl + 1 < kLinkSlots) a.bind(nxt);
    }
    a.bind(miss);
    stat_exit(7);                                                    // PIC-slot link miss
    // Bit 0: non-global source.
    a.mov(x86::rax, imm((uint64_t) &b->link[0] | (b->asm_global ? 0u : 1u)));
    a.mov(x86::qword_ptr(x86::rbp, m_off.link_from), x86::rax);
    // fall through to lbl (return to dispatcher)
  };

  // One `jmp rel32` per static successor, aimed at its stub until the dispatcher patches it.
  struct DirectExit { Label site, stub, desc, gate_fail; uint64_t pc; bool gate; };
  std::vector<DirectExit> dexits;
  Label exit_common;   // cold: state.pc = r10, eax = count, -> done
  auto emit_direct_site = [&](uint64_t pc, bool gate) {
    DirectExit x;
    x.site = a.new_label(); x.stub = a.new_label(); x.desc = a.new_label(); x.gate_fail = a.new_label();
    x.pc = pc; x.gate = gate;
    if (gate) emit_gate(x.gate_fail);                 // backward edge: ceiling + pending poll
    a.bind(x.site);
    a.long_().jmp(x.stub);                            // the patch site
    dexits.push_back(x);
  };
  const bool direct_fallthrough = ra.direct_exits && !terminator_branch && !terminator_jmp;
  if (ra.exit_kind != 0 || direct_fallthrough) {
    exit_common = a.new_label();
    a.lea(x86::r13, x86::ptr(x86::r13, (int32_t) plen));   // count; flags untouched for the jcc
    if (ra.exit_kind == 1) {
      Label taken = a.new_label();
      a.j((x86::CondCode) ra.exit_cc, taken);
      emit_direct_site(ra.exit_fall, false);              // fall-through is forward: no gate
      a.bind(taken);
      emit_direct_site(ra.exit_tgt, ((words[plen - 1] >> 20) & 1) != 0);   // gate backward targets
    } else if (ra.exit_kind == 2) {
      emit_direct_site(ra.exit_tgt, ((words[plen - 1] >> 20) & 1) != 0);
    } else {
      emit_direct_site(b->tag + 4 * (uint64_t) plen, false);   // straight-line fall-through
    }
  } else
#endif
  if (terminator_jmp) {
    // Computed jump (JMP / HW_RET): R10 holds the register target. Publish it only on return to 
    // dispatch OR calling cold resolution. 
    // Chain in-frame via jit_indirect -- the dispatcher's own block-cache lookup -- tailing into
    // the target's compiled body when it's live + runnable here. Unlike the old single-slot link
    // this keys on the ACTUAL target, so it handles all targets with no thrash on varying jumps.
    a.add(x86::r13, imm(plen));
#ifndef JIT_VERIFY
    Label exit_chain = a.new_label(), exit_pc_ready = a.new_label();
    emit_gate(exit_chain);                                        // budget/interrupt: bail to dispatcher
#ifdef JIT_STATS
    a.mov(x86::rax, imm((uint64_t) &m_jmp_exits)); a.inc(x86::qword_ptr(x86::rax));   // PIC hit rate = exits - resolver calls
#endif
    // Per-site PIC: returns and most computed calls are stable at a given instruction.
    { Label pic_miss = a.new_label();
      for (int sl = 0; sl < kLinkSlots; ++sl) {
        Label next = (sl + 1 < kLinkSlots) ? a.new_label() : pic_miss;
        const int off = sl * (int) sizeof(LinkSlot);
        a.mov(x86::rax, imm((uint64_t) &b->link[0]));
        a.cmp(x86::qword_ptr(x86::rax, off + (int) offsetof(LinkSlot, tag)), x86::r10); a.jne(next);
        if ((words[plen - 1] >> 26) == 0x1e) {
          Label mode_ok = a.new_label();
          a.test(x86::r10, imm(1)); a.jz(mode_ok);
          a.mov(x86::rcx, x86::qword_ptr(x86::rax, off + (int) offsetof(LinkSlot, vgen)));
          a.shr(x86::rcx, imm(63));
          a.cmp(x86::cl, x86::byte_ptr(x86::rbp, m_off.sde)); a.jne(next);
          a.bind(mode_ok);
        }
        a.mov(x86::rcx, x86::qword_ptr(x86::rax, off + (int) offsetof(LinkSlot, body)));
        if ((words[plen - 1] >> 26) == 0x1e) {
          // A family return transform can produce the empty slot's ~0 tag.
          a.test(x86::rcx, x86::rcx); a.jz(next);
        }
        a.jmp(x86::rcx);
        if (sl + 1 < kLinkSlots) a.bind(next);
      }
      a.bind(pic_miss);
    }
    // Target-keyed jump cache: tag, generation, ASN and (PALmode) register-bank variant must match.
    // Probed only once this site's PIC is full, so the resolver still fills the PIC first.
    if (kJumpCache) { Label jc_miss = a.new_label(), jc_mode_ok = a.new_label();
      a.mov(x86::rax, imm((uint64_t) &b->link[kLinkSlots - 1]));
      a.cmp(x86::qword_ptr(x86::rax, (int32_t) offsetof(LinkSlot, body)), imm(0)); a.je(jc_miss);
      a.mov(x86::rax, x86::r10); a.shr(x86::rax, imm(2));
      a.and_(x86::eax, imm(kJumpCacheEntries - 1)); a.shl(x86::rax, imm(5));
      a.mov(x86::rcx, imm((uint64_t) &m_jcache[0])); a.add(x86::rax, x86::rcx);
      a.cmp(x86::qword_ptr(x86::rax, (int32_t) offsetof(JumpCacheEntry, tag)), x86::r10); a.jne(jc_miss);
      a.mov(x86::rdx, imm((uint64_t) &m_jc_gen)); a.mov(x86::rdx, x86::qword_ptr(x86::rdx));
      a.mov(x86::rcx, x86::qword_ptr(x86::rax, (int32_t) offsetof(JumpCacheEntry, gen)));
      a.btr(x86::rcx, imm(63)); a.cmp(x86::rcx, x86::rdx); a.jne(jc_miss);
      a.mov(x86::ecx, x86::dword_ptr(x86::rbp, m_off.state_asn));
      a.cmp(x86::qword_ptr(x86::rax, (int32_t) offsetof(JumpCacheEntry, asn)), x86::rcx); a.jne(jc_miss);
      a.test(x86::r10, imm(1)); a.jz(jc_mode_ok);
      a.mov(x86::rcx, x86::qword_ptr(x86::rax, (int32_t) offsetof(JumpCacheEntry, gen))); a.shr(x86::rcx, imm(63));
      a.cmp(x86::cl, x86::byte_ptr(x86::rbp, m_off.sde)); a.jne(jc_miss);
      a.bind(jc_mode_ok);
      if ((words[plen - 1] >> 26) == 0x1e) {
        // Targeted invalidation clears body/tag but retains the matching generation.
        a.mov(x86::rdx, x86::qword_ptr(x86::rax, (int32_t) offsetof(JumpCacheEntry, body)));
        a.test(x86::rdx, x86::rdx); a.jz(jc_miss);
      }
#ifdef JIT_STATS
      a.mov(x86::rcx, imm((uint64_t) &m_jc_hits)); a.inc(x86::qword_ptr(x86::rcx));
#endif
      if ((words[plen - 1] >> 26) == 0x1e) a.jmp(x86::rdx);
      else a.jmp(x86::qword_ptr(x86::rax, (int32_t) offsetof(JumpCacheEntry, body)));
      a.bind(jc_miss); }
    // The resolver is a helper boundary and may clobber R10. A hot PIC hit above
    // chains without touching architectural state.pc.
    a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
    a.mov(x86::qword_ptr(x86::rbx, kVolPins[0] * 8), x86::r8);
    a.mov(x86::qword_ptr(x86::rbx, kVolPins[1] * 8), x86::r9);
    a.mov(aq(0), x86::rbp);                                       // cpu    (arg 0)
    a.mov(aq(1), x86::r10);                                       // target (arg 1) == state.pc
    a.mov(aq(2), imm((uint64_t) &b->link[0] | (b->asm_global ? 0u : 1u)));   // per-site target cache (bit 0: non-global source)
    { const int hi = helper_index(hs, indirect_helper);           // jit_indirect(cpu, target) -> body | 0
      if (hi >= 0 && m_off.helpers)
        a.call(x86::qword_ptr(x86::rbp, (int32_t) (m_off.helpers + hi * 8)));
      else { a.mov(x86::rax, imm((uint64_t) indirect_helper)); a.call(x86::rax); } }
    a.mov(x86::r8, x86::qword_ptr(x86::rbx, kVolPins[0] * 8));
    a.mov(x86::r9, x86::qword_ptr(x86::rbx, kVolPins[1] * 8));
    a.test(x86::rax, x86::rax);                              a.jz(exit_pc_ready);
    a.jmp(x86::rax);                                              // HIT: tail into the target's body
    a.bind(exit_chain);
    stat_gate_exit();
    a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
#ifdef JIT_STATS
    { Label pr = a.new_label(); a.jmp(pr); a.bind(exit_pc_ready); stat_exit(2); a.bind(pr); }
#else
    a.bind(exit_pc_ready);
#endif
#endif
  } else if (terminator_branch) {
    a.add(x86::r13, imm(plen));   // R10 still holds the branch's next PC
#ifndef JIT_VERIFY
    // Gate thinning: the budget/interrupt gate is needed only where a chain can REVISIT code.
    // PC strictly increases through fall-throughs and forward branches, so every guest cycle
    // contains a backward branch or a computed jump.
    const uint32_t lw = words[plen - 1];
    const uint32_t lop = lw >> 26;
    const bool gate_exit = counted_scan_fused || word_scan_fused || byte_scan_fused || tail_scan_fused
                        || !(lop >= 0x30 && lop <= 0x3f) || (((lw >> 20) & 1) != 0);
    Label exit_chain = a.new_label();
    if (gate_exit) emit_gate(exit_chain);
    emit_source_pal_guard(exit_chain);
    if (gate_exit) {
      // Self-loop fast path: a taken branch back to our own start (r10 == b->tag) jumps
      // straight into the body, skipping the resolver call entirely.
      Label not_self = a.new_label();
      a.mov(x86::rax, imm(b->tag));                             // tag may exceed imm32
      a.cmp(x86::r10, x86::rax);                                a.jne(not_self);
      a.jmp(pal_block ? body : body_same_page);
      a.bind(not_self);
    }
    emit_chain(exit_chain);
    a.bind(exit_chain);
    stat_gate_exit();
#endif
    a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
  } else {
    set_pc(b->tag + 4 * (uint64_t) plen);   // straight-line fall-through to the next block
    a.add(x86::r13, imm(plen));
#ifndef JIT_VERIFY
    // No gate: fall-through PC is strictly forward.
    Label exit_chain = a.new_label();
    emit_source_pal_guard(exit_chain);
    emit_chain(exit_chain);
    a.bind(exit_chain);
    stat_exit(1);
#ifdef JIT_STATS
    a.mov(x86::rax, imm((uint64_t) &b->st_exit1)); a.inc(x86::qword_ptr(x86::rax));
#endif
#endif
    a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
  }
  a.mov(x86::eax, x86::r13d);                      // total instructions completed across the chain
  a.bind(done);                 // bail jumps here with EAX already set
  a.jmp(x86::qword_ptr(x86::rbp, (int32_t) m_off.jit_exit));   // shared epilogue: sync pins, restore frame, ret

  if (!pal_block) {
    a.bind(exec_denied);
    a.mov(x86::rax, imm(b->tag));
    a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::rax);
    a.mov(x86::eax, x86::r13d);   // no instructions executed in this block
    a.jmp(done);
  }

  // Cold tail: the outlined memop slow paths (dead 99.8% of the time -- dpc hit rate).
#ifdef JIT_STATS
  g_cold_stub_counters = exit_kind_counter(3);   // [3] bail, [4] trapped
#endif
  for (const ColdMemStub& s : cold)
    emit_watched_store_stub(a, m_off, m_ll_watch, m_ll_buckets, s);
  for (const ColdMemStub& s : cold)
    emit_cold_mem_stub(a, gpa, m_off, s);

#ifndef JIT_VERIFY
  // Direct-exit stubs and PatchSite descriptors.
  if (!dexits.empty()) {
    a.bind(exit_common);
    a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
    a.mov(x86::eax, x86::r13d);
    a.jmp(done);
    for (const DirectExit& x : dexits) {
      if (x.gate) {                                     // gate failed: plain exit, no patch request
        a.bind(x.gate_fail);
        stat_gate_exit();
        a.mov(x86::r10, imm(x.pc));
        a.jmp(exit_common);
      }
      a.bind(x.stub);
      stat_exit(6);
      a.mov(x86::r10, imm(x.pc));
      a.lea(x86::rax, x86::ptr(x.desc));
      a.or_(x86::rax, imm(2 | (b->asm_global ? 0 : 1)));   // bit 1: PatchSite; bit 0: non-global source
      a.mov(x86::qword_ptr(x86::rbp, m_off.link_from), x86::rax);
      a.jmp(exit_common);
    }
    a.align(AlignMode::kData, 8);
    for (const DirectExit& x : dexits) {                // PatchSite { site, stub, tag, source_tag }
      a.bind(x.desc);
      a.embed_label(x.site);
      a.embed_label(x.stub);
      a.embed_uint64(x.pc);
      a.embed_uint64(b->tag);
    }
  }
#endif

  size_t exec_proof_off = 0;
  if (!pal_block) {
    a.align(AlignMode::kData, alignof(NativeExecProof));
    a.bind(exec_proof);
    exec_proof_off = code.code_size();
    if (exec_proof_off <= exec_mask_imm_off
        || exec_proof_off - exec_mask_imm_off > UINT32_MAX)
      return;
    a.embed_uint64(0); // epoch
    a.embed_uint32(0); // mode mask; both copies initially deny every mode
    a.embed_uint32(static_cast<uint32_t>(exec_proof_off - exec_mask_imm_off));
  }
  const size_t csz = code.code_size();
  JitFn fn = nullptr;
#ifdef JIT_DISASM
  {
    FILE* out = m_disasm_fp ? m_disasm_fp : stderr;
    fprintf(out, "[JIT][CPU%d] block @ %016llx%s  (%u instr, %llu bytes)\n%s\n",
            m_cpu_id, (unsigned long long) (b->tag & ~(uint64_t) 1),
            (b->tag & 1) ? " PAL" : "", plen, (unsigned long long) csz, logger.data());
    fflush(out);   // per-block flush: preserve the trace if JIT'd code later crashes
  }
#endif
  if (eh.failed) return;   // emit error already reported -- don't ship a broken block
                           // (deterministic for these source bytes: stays compiled=true, no retry loop)

  // A source RACE is transient (another CPU modified the guest bytes mid-compile): clear the
  // attempt flag so the dispatcher retries, instead of interpreting this block until the next flush.
  if (!sources.matches(b->tag, b->phys, b->asm_global, source_words.data(), hash_len)) {
    b->compiled = false;
    return;
  }

  auto* const rt = static_cast<JitRuntime*>(m_rt);
  const Error add_err = rt->add(&fn, &code);
  if (add_err != Error::kOk) {
    if (add_err == Error::kOutOfMemory) {   // transient: reclaim at the next safe point, then retry
      m_reclaim_pending = true;
      b->compiled = false;
    }                                        // any other add() error is deterministic -> stays terminal
    printf("[JIT][CPU%d] JitRuntime::add failed (err=%u%s) for block @ %016llx\n", m_cpu_id,
           (unsigned) add_err, add_err == Error::kOutOfMemory ? ", OOM: will reclaim+retry" : ", terminal",
           (unsigned long long) (b->tag & ~(uint64_t) 1));
    return;
  }
  // add() can allocate/copy for long enough that another CPU modifies the guest source.
  // Never publish old code with a hash of new bytes; abandon the unreferenced image on a mismatch.
  if (eh.failed) { (void)rt->release(fn); return; }   // late deterministic emit error -> terminal
  if (!sources.matches(b->tag, b->phys, b->asm_global, source_words.data(), hash_len)) {
    (void)rt->release(fn);
    b->compiled = false;   // post-add source race -> retry
    return;
  }

  void* const jit_body = (void*) ((uint8_t*) (void*) fn + body_off);   // chained re-entry (past prologue)
  b->body_off = (uint32_t) body_off;                          // to restore jit_body on revalidate
  b->src_sum  = source_hash;                                  // include every word the fusion depends on
  b->hash_len = hash_len;                                     // freeze the hash extent (n_instr drifts)
  b->prefix_len = plen;
  set_exec_proof(*b, pal_block ? nullptr
      : reinterpret_cast<NativeExecProof*>((uint8_t*)(void*)fn + exec_proof_off));
  b->jit_body_same_page = pal_block ? nullptr
      : static_cast<void*>((uint8_t*)(void*)fn + body_same_page_off);
  m_code_bytes += csz;   // track for the reclaim threshold (see flush())
#ifdef JIT_STATS
  m_stat_compiled++;
  m_stat_plen_sum += plen;
  m_stat_code_bytes += csz;
#endif
#ifdef JIT_REGPROF
  b->rp_csz = (uint32_t) csz;   // exec-weighted expansion: sum(rp_hits*rp_csz) / sum(rp_hits*prefix_len)
#endif
  b->code = fn;
  b->jit_body = jit_body;                                     // runnable chain entry publishes last
#ifdef JIT_REGPROF
  m_dbg_source = sources;
#endif
}

// Compile an N-block trace. Reuses the shared emit_op for each block's per-op codegen (so the body
// is the exact one the block path already verifies). Blocks are fused with a GUARD between them: after a
// block's terminator (which left R10 = its next PC), check R10 == the next fused block's tag; on a hit fall
// through in-trace, on a miss SIDE-EXIT to the dispatcher at the real next PC. n_blocks==1 = the single-
// block trace. Fills the caller-provided slot t (code + per-segment source descriptors + coherence epoch).
void CJitEngine::compile_trace(TraceFragment* t, JitBlock** blocks, uint32_t n_blocks,
                               const AlphaInstructionSource& sources, const HelperSet& hs)
{
  using namespace asmjit;
  if (m_rt == nullptr || n_blocks == 0 || n_blocks > kMaxTraceSegs) return;
  // Mirror compile_block's reclaim threshold.
  if (m_code_bytes >= kReclaimBytes) { m_reclaim_pending = true; return; }
  std::array<SourceSeg, kMaxTraceSegs> source_desc{};
  std::array<std::vector<uint32_t>, kMaxTraceSegs> source_words{};
  // Validate every segment up front: a compiled prefix within its source. (prefix_len, NOT n_instr --
  // ops past the prefix never passed the safe-to-compile scan; emitting them runs code PAST the terminator.)
  for (uint32_t bi = 0; bi < n_blocks; ++bi) {
    const JitBlock* b = blocks[bi];
    if (b == nullptr || b->prefix_len == 0)
      return;
    const auto source = sources.view(b->tag);
    if (!source.contains(b->prefix_len) || source.identity != b->phys
        || source.asm_global != b->asm_global)
      return;
    source_desc[bi] = {b->tag, b->phys, b->prefix_len, b->asm_global, b->asn, 0};
    source_words[bi].resize(source_desc[bi].n_instr);
    if (!source.copy_words(source_words[bi].data(), source_desc[bi].n_instr))
      return;
  }

  // Regalloc: pick the trace's hottest guest GPRs for its callee-saved pins. A fresh
  // call loads them here; the chained entry below first converts the global block convention.
#ifdef _WIN32
  const int pin_hosts[5] = { (int) x86::r12.id(), (int) x86::r15.id(),
                             (int) x86::r14.id(), (int) x86::rsi.id(), (int) x86::rdi.id() };
  const int n_pin_hosts = 5;
#else
  const int pin_hosts[3] = { (int) x86::r12.id(), (int) x86::r15.id(), (int) x86::r14.id() };
  const int n_pin_hosts = 3;
#endif
  int pin_guest[5];
  int n_pins = 0;
  if (TracePinSpike) {
    uint32_t acc[32] = {};
    for (uint32_t bi = 0; bi < n_blocks; ++bi)
      count_gpr_access(source_words[bi].data(), source_desc[bi].n_instr, acc);
    acc[31] = 0;
    for (int r = 0; r < 32; ++r) if ((r & 0xc) == 0x4) acc[r] = 0;   // r4-7, r20-23: PALshadow-remappable
    for (int k = 0; k < n_pin_hosts; ++k) {
      int best = -1; uint32_t bestc = 2;              // strictly > 2 accesses to justify load+sync
      for (int r = 0; r < 32; ++r) if (acc[r] > bestc) { bestc = acc[r]; best = r; }
      if (best < 0) break;
      acc[best] = 0;
      pin_guest[n_pins++] = best;
    }
  } else {
    // A/B fallback: the fixed global set (block-JIT behavior)
    pin_guest[n_pins++] = kGlobalPins[0]; pin_guest[n_pins++] = kGlobalPins[1];
    pin_guest[n_pins++] = 30;
#ifdef _WIN32
    pin_guest[n_pins++] = 27; pin_guest[n_pins++] = 0;
#endif
  }

  // RLE: keep the region's hottest unpinned guest GPR live in R8 (caller-saved) 
  // deletes one regs[] load per use. convention is unchanged: one store when leaving. 
  // Excludes pinned regs and r4-7/r20-23 (pin routing skips PALshadow).
  int vol_reg = -1;
  if (RegionCacheReg) {
    uint32_t acc2[32] = {};
    for (uint32_t bi = 0; bi < n_blocks; ++bi)
      count_gpr_access(source_words[bi].data(), source_desc[bi].n_instr, acc2);
    acc2[31] = 0;
    for (int r = 0; r < 32; ++r) if ((r & 0xc) == 0x4) acc2[r] = 0;
    for (int k = 0; k < n_pins; ++k) acc2[pin_guest[k]] = 0;
    uint32_t bestc = 3;                                  // > 3 uses to pay its load + exit store
    for (int r = 0; r < 32; ++r) if (acc2[r] > bestc) { bestc = acc2[r]; vol_reg = r; }
  }
  const bool vol_sync = (vol_reg >= 0);

  JitErrorHandler eh;
  eh.cpu_id = m_cpu_id;
#ifdef JIT_DISASM
  eh.fp = m_disasm_fp;
#endif
  CodeHolder code;
  if (code.init(((JitRuntime*) m_rt)->environment()) != Error::kOk) {
    m_reclaim_pending = true;   // OOM: free code at the next safe point (the head re-promotes)
    return;
  }
  code.set_error_handler(&eh);
  x86::Assembler a(&code);
#ifdef JIT_DISASM
  a.add_diagnostic_options(DiagnosticOptions::kValidateAssembler);
#endif
  CallConv cc;
  if (cc.init(CallConvId::kCDecl, ((JitRuntime*) m_rt)->environment()) != Error::kOk) return;
  const uint8_t* gpa = cc.passed_order(RegGroup::kGp);
  [[maybe_unused]] const uint32_t kPinnedGp =
      (1u << x86::rbp.id()) | (1u << x86::rbx.id()) | (1u << x86::r14.id())
    | (1u << x86::r12.id()) | (1u << x86::r13.id()) | (1u << x86::r15.id())
#ifdef _WIN32
    | (1u << x86::rsi.id()) | (1u << x86::rdi.id())
#endif
    ;
  assert((((uint32_t) cc.preserved_regs(RegGroup::kGp)) & kPinnedGp) == kPinnedGp);

  a.push(x86::rbx); a.push(x86::rbp); a.push(x86::r14);
  a.push(x86::r12); a.push(x86::r13); a.push(x86::r15);
#ifdef _WIN32
  a.push(x86::rsi); a.push(x86::rdi);
#endif
  a.sub(x86::rsp, imm(56));
  a.mov(x86::rbp, x86::gpq(gpa[0]));                   // cpu
  a.mov(x86::rbx, x86::gpq(gpa[1]));                   // regs
  a.xor_(x86::r13d, x86::r13d);                       // chain count := 0
  for (int k = 0; k < n_pins; ++k)                     // load the trace's pin set from regs[]
    a.mov(x86::gpq((uint32_t) pin_hosts[k]), x86::qword_ptr(x86::rbx, pin_guest[k] * 8));
  a.mov(x86::r8, x86::qword_ptr(x86::rbx, kVolPins[0] * 8));
  a.mov(x86::r9, x86::qword_ptr(x86::rbx, kVolPins[1] * 8));
  if (vol_sync) a.mov(x86::r8, x86::qword_ptr(x86::rbx, vol_reg * 8));   // region-cached GPR

  Label done = a.new_label();   // shared side-exit/return: EAX preset to the instr count, state.pc live
  Label body = a.new_label();   // loop re-entry (after the prologue; pins + count stay live across iterations)
#ifndef JIT_VERIFY
  // Chain bookkeeping: exit trampolines pass their LinkSlot array in R11 to one shared stub; 
  // translate between the trace's pin set and the global block, skipped when the sets coincide.
  Label chain_stub = a.new_label();
  Label done_nosync = a.new_label();   // post-sync teardown entry (adapter already synced pins)
  Label exit_tramp[kMaxTraceExits];
  uint32_t tramp_slot[kMaxTraceExits];
  uint32_t n_tramp = 0, n_x = 0;
  bool pins_differ = (n_pins != n_pin_hosts);
  { const int glob[5] = { kGlobalPins[0], kGlobalPins[1], 30, 27, 0 };
    for (int k = 0; k < n_pins && !pins_differ; ++k) if (pin_guest[k] != glob[k]) pins_differ = true; }
  // chained entry: tail-jmp target with the frame live and GLOBAL pins in registers; the
  // adapter syncs them to regs[] and loads the trace set, then falls into body.
  size_t chain_off = 0;
  if (TraceChainIn) {
    if (pins_differ || vol_sync) a.jmp(body);   // the C entry (already loaded) skips the adapter
    Label chain_in = a.new_label();
    a.bind(chain_in);
    chain_off = code.code_size();
    if (pins_differ) {
      a.mov(x86::qword_ptr(x86::rbx, 1 * 8), x86::r12);
      a.mov(x86::qword_ptr(x86::rbx, 16 * 8), x86::r15); a.mov(x86::qword_ptr(x86::rbx, 30 * 8), x86::r14);
#ifdef _WIN32
      a.mov(x86::qword_ptr(x86::rbx, 27 * 8), x86::rsi); a.mov(x86::qword_ptr(x86::rbx, 0 * 8), x86::rdi);
#endif
      for (int k = 0; k < n_pins; ++k)
        a.mov(x86::gpq((uint32_t) pin_hosts[k]), x86::qword_ptr(x86::rbx, pin_guest[k] * 8));
    }
    if (vol_sync) a.mov(x86::r8, x86::qword_ptr(x86::rbx, vol_reg * 8));   // M2 region-cached GPR
  }
#endif
  a.bind(body);

  // Trace register binding: the spike's per-trace pin set (or the global set when disabled)
  RegAlloc ra;
  for (int r = 0; r < 32; ++r) ra.host[r] = -1;
  ra.rax_holds = -1;
  ra.vol_bind = kVolPins[0];
  ra.vol_bind2 = kVolPins[1];
  ra.dpc_live = false;
  ra.dpc_base = ra.dpc_disp = 0;
  ra.dpc_write = ra.dpc_force_align = false;
  ra.direct_exits = false;   // traces keep the R10 convention (guards read it)
  ra.exit_kind = 0; ra.exit_cc = 0; ra.exit_tgt = ra.exit_fall = 0;
#ifdef JIT_STATS
  { const uint32_t left = (uint32_t) (sizeof(m_licm_pool) / sizeof(m_licm_pool[0])) - m_licm_next;
    ra.licm_slots = m_licm_pool + m_licm_next; ra.licm_n = 0; ra.licm_max = left > 64 ? 64 : left; }
#endif
  for (int k = 0; k < n_pins; ++k) ra.host[pin_guest[k]] = pin_hosts[k];
  ra.host[kVolPins[0]] = (int)x86::r8.id();
  ra.host[kVolPins[1]] = (int)x86::r9.id();
  if (vol_sync) { ra.host[vol_reg] = (int) x86::r8.id(); ra.vol_bind = vol_reg; }

  std::vector<ColdMemStub> cold;   // outlined memop slow paths, emitted after the epilogue

  // DECODE: fused span -> region IR. Structure ops carry what the emit walk needs; passes
  // rewrite this array before emission.
  std::vector<RegionOp> ir;
  if (RegionIR) {
    for (uint32_t bi = 0; bi < n_blocks; ++bi) {
      const uint32_t plen = source_desc[bi].n_instr;
      const uint32_t* words = source_words[bi].data();
      RegionOp o{}; o.blk = (uint8_t) bi;
      o.kind = RegionOp::BLOCK_ENTER;
      o.pc = source_desc[bi].guest_pc + 4 * (uint64_t) plen; ir.push_back(o);
      o.kind = RegionOp::INSTR;
      for (uint32_t i = 0; i < plen; ++i) { o.ins = words[i]; o.idx = i; ir.push_back(o); }
      o.kind = RegionOp::BLOCK_END; o.idx = plen; ir.push_back(o);
      if (bi + 1 < n_blocks) {
        o.kind = RegionOp::GUARD; o.pc = source_desc[bi + 1].guest_pc; ir.push_back(o);
      }
    }
  }

  // EMIT: walk the IR (or the blocks directly when RegionIR is off -- the A/B baseline).
  for (size_t k = 0; k < ir.size(); ++k) {
    const RegionOp& o = ir[k];
    JitBlock* b = blocks[o.blk];
    switch (o.kind) {
    case RegionOp::BLOCK_ENTER:
      // Default R10 + state.pc = this block's sequential next (the fall-through exit). emit_op's branch/jump
      // terminator overwrites both with its target; a fault bail writes the fault PC. For an intermediate
      // block this also makes the guard below see R10 == the sequential successor when it falls through.
      a.mov(x86::r10, imm(o.pc));
      a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
      break;
    case RegionOp::INSTR:
      emit_op(&a, gpa, &done, hs, (source_desc[o.blk].guest_pc & 1) != 0,
              b, o.ins, o.idx, ra, &cold);
      break;
    case RegionOp::BLOCK_END:
      a.add(x86::r13, imm(o.idx));                       // count this block
      a.mov(x86::eax, x86::r13d);                        // EAX = instrs completed so far (preset for `done`)
      break;
    case RegionOp::GUARD:
      // Did this block actually flow to the next fused block? R10 = its next PC; a mismatch means
      // the path diverged from what we fused -> side-exit (chained out when enabled) at the real next PC.
      a.mov(x86::rcx, imm(o.pc));   // 64-bit tag may exceed imm32; rcx scratch (not EAX)
      a.cmp(x86::r10, x86::rcx);
#ifndef JIT_VERIFY
      if (TraceChainOut && n_x < kMaxTraceExits) {
        exit_tramp[n_tramp] = a.new_label(); tramp_slot[n_tramp] = n_x;
        a.jne(exit_tramp[n_tramp]); n_tramp++; n_x++;
      } else
        a.jne(done);
#else
      a.jne(done);
#endif
      break;
    }
  }
  if (!RegionIR) {
    for (uint32_t bi = 0; bi < n_blocks; ++bi) {
      JitBlock* b = blocks[bi];
      const uint32_t plen = source_desc[bi].n_instr;
      const uint32_t* words = source_words[bi].data();
      const bool pal_block = (source_desc[bi].guest_pc & 1) != 0;
      a.mov(x86::r10, imm(source_desc[bi].guest_pc + 4 * (uint64_t) plen));
      a.mov(x86::qword_ptr(x86::rbp, m_off.state_pc), x86::r10);
      for (uint32_t i = 0; i < plen; ++i)
        emit_op(&a, gpa, &done, hs, pal_block, b, words[i], i, ra, &cold);
      a.add(x86::r13, imm(plen));
      a.mov(x86::eax, x86::r13d);
      if (bi + 1 < n_blocks) {
        a.mov(x86::rcx, imm(source_desc[bi + 1].guest_pc));
        a.cmp(x86::r10, x86::rcx);
#ifndef JIT_VERIFY
        if (TraceChainOut && n_x < kMaxTraceExits) {
          exit_tramp[n_tramp] = a.new_label(); tramp_slot[n_tramp] = n_x;
          a.jne(exit_tramp[n_tramp]); n_tramp++; n_x++;
        } else
          a.jne(done);
#else
        a.jne(done);
#endif
      }
    }
  }
#ifndef JIT_VERIFY
  // Loop closure, generalized: ANY final terminator left R10 = the next PC, so close the
  // loop dynamically whenever it returns to the head, covers static back-edges AND computed
  // ones (RET-closed call-containing loops from link-guided fusion). Budget/interrupt gate ON
  // the back-edge. Verify builds omit this.
  bool final_is_jmp = false;
  {
    const SourceSeg& last = source_desc[n_blocks - 1];
    const uint32_t* lw = source_words[n_blocks - 1].data();
    const uint32_t lop = lw[last.n_instr - 1], lopc = lop >> 26;
    final_is_jmp = (lopc == 0x1a || lopc == 0x1e);   // JMP/HW_RET: varying target -> jit_indirect exit
  }
  {
    Label not_head = a.new_label();
    a.mov(x86::rcx, imm(source_desc[0].guest_pc));
    a.cmp(x86::r10, x86::rcx); a.jne(not_head);                                  // not the head -> final exit
    a.cmp(x86::r13, imm(kChainCeiling)); a.jge(done);                          // chain ceiling
    emit_pending_check(a, m_off, done);                                        // interrupt / timer pending
    a.mov(x86::rcx, imm((uint64_t) &t->underrun));                             // R3b: looping -> healthy
    a.mov(x86::dword_ptr(x86::rcx), imm(0));
    a.jmp(body);                                                               // loop in compiled code
    a.bind(not_head);
  }
  if (TraceChainOut) {
    // Final exit: R10 = next PC (!= head), EAX/count current.
    if (final_is_jmp) {
      // Computed target: chain via jit_indirect, gated + pin-adapted like the stub.
      Label jref = a.new_label(), jmiss = a.new_label();
      a.cmp(x86::r13, imm(kChainCeiling)); a.jge(jref);
      emit_pending_check(a, m_off, jref);
      if (pins_differ) {
        for (int k = 0; k < n_pins; ++k) a.mov(x86::qword_ptr(x86::rbx, pin_guest[k] * 8), x86::gpq((uint32_t) pin_hosts[k]));
        a.mov(x86::r12, x86::qword_ptr(x86::rbx, 1 * 8));
        a.mov(x86::r15, x86::qword_ptr(x86::rbx, 16 * 8)); a.mov(x86::r14, x86::qword_ptr(x86::rbx, 30 * 8));
#ifdef _WIN32
        a.mov(x86::rsi, x86::qword_ptr(x86::rbx, 27 * 8)); a.mov(x86::rdi, x86::qword_ptr(x86::rbx, 0 * 8));
#endif
      }
      // R8/R9 are caller-saved on both host ABIs. Publish the volatile global pins
      // across jit_indirect just as emit_call does for ordinary helpers.
      a.mov(x86::qword_ptr(x86::rbx, kVolPins[0] * 8), x86::r8);
      a.mov(x86::qword_ptr(x86::rbx, kVolPins[1] * 8), x86::r9);
      a.mov(x86::gpq(gpa[0]), x86::rbp);
      a.mov(x86::gpq(gpa[1]), x86::r10);
      a.xor_(x86::gpd(gpa[2]), x86::gpd(gpa[2]));                 // no per-site cache for trace final exits yet
      { const int hi = helper_index(hs, hs.indirect_helper);
        if (hi >= 0 && m_off.helpers) a.call(x86::qword_ptr(x86::rbp, (int32_t) (m_off.helpers + hi * 8)));
        else { a.mov(x86::rax, imm((uint64_t) hs.indirect_helper)); a.call(x86::rax); } }
      a.mov(x86::r8, x86::qword_ptr(x86::rbx, kVolPins[0] * 8));
      a.mov(x86::r9, x86::qword_ptr(x86::rbx, kVolPins[1] * 8));
      a.test(x86::rax, x86::rax); a.jz(jmiss);
      a.jmp(x86::rax);                                    // HIT: tail into the target block's body
      a.bind(jmiss);
      a.mov(x86::eax, x86::r13d);
      a.jmp(pins_differ ? done_nosync : done);
      a.bind(jref);
      a.mov(x86::eax, x86::r13d);
      a.jmp(done);
    } else if (n_x < kMaxTraceExits) {
      a.mov(x86::r11, imm((uint64_t) &t->exits[n_x].link[0]));
      a.jmp(chain_stub);
      n_x++;
    } else
      a.jmp(done);
  }
  // Side-exit trampolines: R11 = the exit's slot array, then the shared stub.
  for (uint32_t x = 0; x < n_tramp; ++x) {
    a.bind(exit_tramp[x]);
    a.mov(x86::r11, imm((uint64_t) &t->exits[tramp_slot[x]].link[0]));
    a.jmp(chain_stub);
  }
  if (TraceChainOut) {
    // Shared chain-out stub: r10 = target, r11 = this exit's LinkSlot array, count in R13.
    // Gate + PAL/SDE run BEFORE the adapter, so bail paths return with trace pins live (-> done).
    a.bind(chain_stub);
    Label stub_ret = a.new_label(), miss = a.new_label();
    a.mov(x86::rcx, imm((uint64_t) &t->underrun));   // count the side-exit (closure resets)
    a.inc(x86::dword_ptr(x86::rcx));
    a.cmp(x86::r13, imm(kChainCeiling)); a.jge(stub_ret);
    emit_pending_check(a, m_off, stub_ret);
    if (vol_sync) a.mov(x86::qword_ptr(x86::rbx, vol_reg * 8), x86::r8);   // commit before leaving
    if (pins_differ) {   // adapter: trace pins -> regs[], then the global block convention
      for (int k = 0; k < n_pins; ++k) a.mov(x86::qword_ptr(x86::rbx, pin_guest[k] * 8), x86::gpq((uint32_t) pin_hosts[k]));
      // A miss returns through done_nosync, so publish the volatile pins here too.
      a.mov(x86::qword_ptr(x86::rbx, kVolPins[0] * 8), x86::r8);
      a.mov(x86::qword_ptr(x86::rbx, kVolPins[1] * 8), x86::r9);
      a.mov(x86::r12, x86::qword_ptr(x86::rbx, 1 * 8));
      a.mov(x86::r15, x86::qword_ptr(x86::rbx, 16 * 8)); a.mov(x86::r14, x86::qword_ptr(x86::rbx, 30 * 8));
#ifdef _WIN32
      a.mov(x86::rsi, x86::qword_ptr(x86::rbx, 27 * 8)); a.mov(x86::rdi, x86::qword_ptr(x86::rbx, 0 * 8));
#endif
    }
    a.mov(x86::rdx, imm((uint64_t) &m_vgen_cur));
    a.mov(x86::rdx, x86::qword_ptr(x86::rdx));            // current epoch
    for (int sl = 0; sl < kLinkSlots; ++sl) {
      Label nxt = (sl + 1 < kLinkSlots) ? a.new_label() : miss;
      const int off = sl * (int) sizeof(LinkSlot);
      a.mov(x86::rcx, x86::qword_ptr(x86::r11, off + (int) offsetof(LinkSlot, body)));
      a.test(x86::rcx, x86::rcx);                                                  a.jz(nxt);
      a.cmp(x86::qword_ptr(x86::r11, off + (int) offsetof(LinkSlot, tag)), x86::r10);  a.jne(nxt);
      a.mov(x86::rax, x86::qword_ptr(x86::r11, off + (int) offsetof(LinkSlot, vgen)));
      a.mov(x86::rcx, x86::rax); a.btr(x86::rcx, imm(63));
      a.cmp(x86::rcx, x86::rdx); a.jne(nxt);
      { Label mode_ok = a.new_label();
        a.test(x86::r10, imm(1)); a.jz(mode_ok);
        a.shr(x86::rax, imm(63));
        a.cmp(x86::al, x86::byte_ptr(x86::rbp, m_off.sde)); a.jne(nxt);
        a.bind(mode_ok); }
      a.mov(x86::rcx, x86::qword_ptr(x86::r11, off + (int) offsetof(LinkSlot, body)));
      a.jmp(x86::rcx);                                    // HIT: tail into the block body
      if (sl + 1 < kLinkSlots) a.bind(nxt);
    }
    a.bind(miss);
    a.mov(x86::rax, x86::r11);                                     // request a patch of this exit's slots
    if (!source_desc[0].asm_global) a.or_(x86::rax, imm(1));       // bit 0: non-global source (see emit_chain)
    a.mov(x86::qword_ptr(x86::rbp, m_off.link_from), x86::rax);
    a.mov(x86::eax, x86::r13d);
    if (pins_differ) a.jmp(done_nosync); else a.jmp(done);
    a.bind(stub_ret);
    a.mov(x86::eax, x86::r13d);
    a.jmp(done);
  }
#endif
  a.bind(done);
  for (int k = 0; k < n_pins; ++k)                     // sync the trace's pin set back to regs[]
    a.mov(x86::qword_ptr(x86::rbx, pin_guest[k] * 8), x86::gpq((uint32_t) pin_hosts[k]));
  a.mov(x86::qword_ptr(x86::rbx, kVolPins[0] * 8), x86::r8);
  a.mov(x86::qword_ptr(x86::rbx, kVolPins[1] * 8), x86::r9);
  if (vol_sync) a.mov(x86::qword_ptr(x86::rbx, vol_reg * 8), x86::r8);   // region-cached GPR
#ifndef JIT_VERIFY
  a.bind(done_nosync);   // chain paths whose adapter already synced land here
#endif
  a.add(x86::rsp, imm(56));
#ifdef _WIN32
  a.pop(x86::rdi); a.pop(x86::rsi);
#endif
  a.pop(x86::r15); a.pop(x86::r13); a.pop(x86::r12);
  a.pop(x86::r14); a.pop(x86::rbp); a.pop(x86::rbx);
  a.ret();

  // Cold tail: the outlined memop slow paths
  for (const ColdMemStub& s : cold)
    emit_watched_store_stub(a, m_off, m_ll_watch, m_ll_buckets, s);
  for (const ColdMemStub& s : cold)
    emit_cold_mem_stub(a, gpa, m_off, s);

#ifdef JIT_STATS
  m_licm_next += ra.licm_n;
  if (m_licm_next + 64 > (uint32_t) (sizeof(m_licm_pool) / sizeof(m_licm_pool[0]))) m_licm_next = 0;   // wrap
#endif
  const size_t csz = code.code_size();
  JitFn fn = nullptr;
  if (eh.failed) return;
  auto sources_current = [&]() noexcept {
    for (uint32_t bi = 0; bi < n_blocks; ++bi) {
      const SourceSeg& source = source_desc[bi];
      if (!sources.matches(source.guest_pc, source.phys_pc, source.asm_global,
                           source_words[bi].data(), source.n_instr))
        return false;
    }
    return true;
  };
  if (!sources_current()) return;

  auto* const rt = static_cast<JitRuntime*>(m_rt);
  if (rt->add(&fn, &code) != Error::kOk) return;
  if (eh.failed || !sources_current()) {
    (void)rt->release(fn);
    return;
  }
  // Old trace code still refers to t->exits[]. Clear incoming links before
  // reusing those slots; compilation and source validation have succeeded.
  if (t->code) invalidate_links(kBumpEvict);
  t->valid = false;
  uint32_t total = 0;
  for (uint32_t bi = 0; bi < n_blocks; ++bi) {
    t->segs[bi] = source_desc[bi];
    t->segs[bi].src_sum = src_hash(
        reinterpret_cast<const uint8_t*>(source_words[bi].data()),
        source_desc[bi].n_instr);
    total += source_desc[bi].n_instr;
  }
  for (uint32_t e = 0; e < kMaxTraceExits; ++e) t->exits[e] = TraceExit{};   // fresh slot: empty links
  t->code       = fn;
#ifndef JIT_VERIFY
  t->chain_entry = TraceChainIn ? (void*) ((uint8_t*) (void*) fn + chain_off) : nullptr;
#else
  t->chain_entry = nullptr;
#endif
  t->underrun   = 0;
  t->head_tag   = source_desc[0].guest_pc;
  t->asn        = source_desc[0].asn;
  t->asm_global = source_desc[0].asm_global;
  t->vgen       = m_vgen_cur;
  t->flush_gen  = m_flush_gen;
  t->n_blocks   = n_blocks;
  t->n_instr    = total;
  t->n_segs     = n_blocks;
#ifndef JIT_VERIFY
  t->n_exits    = n_x;
#else
  t->n_exits    = 0;
#endif
  m_code_bytes += csz;
#ifdef JIT_STATS
  m_trace_formed++;
#endif
  t->valid = true;   // commit the fully populated trace
}

#endif // ES40_JIT_X64

#endif // ES40_JIT

// Keeps this translation unit non-empty when x86-64 is not the build host (MSVC LNK4221).
extern const char jit_x64_backend_tu;
const char jit_x64_backend_tu = 0;
