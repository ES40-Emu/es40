/* ES40 emulator -- JIT engine: the ARM64 (AArch64) codegen backend.
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

#ifdef ES40_JIT_A64

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <vector>
#ifndef ASMJIT_STATIC
#define ASMJIT_STATIC
#endif
#include <asmjit/a64.h>

// Cross-op DPC reuse state (x64's dpc_live/base/disp)
struct A64DpcState {
  bool     live = false;
  bool     write = false;
  bool     force_align = false;
  uint8_t  base = 31;
  int32_t  disp = 0;
};

// register convention for both block and trace codegen:
//   x9-x15 = transient expansion/tail scratch; x19 = CAlphaCPU*, x20 = state.r[] base,
//   x21 = completed-instruction count, x22 = next guest PC / chain target,
//   x23-x28 = guest-GPR pin bank.
struct CJitEngine::RegAlloc {
  static constexpr uint32_t kGuestPinCount = 6;
  A64DpcState dpc{};
  int8_t fwd_reg = -1;   // guest GPR whose value kScratch2 still mirrors (x64 rax_holds)

  static constexpr asmjit::a64::Gp kArgCpu = asmjit::a64::x0;
  static constexpr asmjit::a64::Gp kArgRegs = asmjit::a64::x1;
  static constexpr asmjit::a64::Gp kResultCount = asmjit::a64::w0;
  static constexpr asmjit::a64::Gp kScratch0 = asmjit::a64::x9;
  static constexpr asmjit::a64::Gp kScratch1 = asmjit::a64::x10;
  static constexpr asmjit::a64::Gp kScratch2 = asmjit::a64::x11;
  static constexpr asmjit::a64::Gp kScratch3 = asmjit::a64::x12;
  static constexpr asmjit::a64::Gp kScratch4 = asmjit::a64::x13;
  static constexpr asmjit::a64::Gp kScratch5 = asmjit::a64::x14;
  static constexpr asmjit::a64::Gp kScratch6 = asmjit::a64::x15;
  static constexpr asmjit::a64::Gp kCallTarget = asmjit::a64::x16;

  static constexpr asmjit::a64::Gp kCpu = asmjit::a64::x19;
  static constexpr asmjit::a64::Gp kRegs = asmjit::a64::x20;
  static constexpr asmjit::a64::Gp kChainCount = asmjit::a64::x21;
  static constexpr asmjit::a64::Gp kNextPc = asmjit::a64::x22;
  static constexpr asmjit::a64::Gp kGuestPin0 = asmjit::a64::x23;
  static constexpr asmjit::a64::Gp kGuestPin1 = asmjit::a64::x24;
  static constexpr asmjit::a64::Gp kGuestPin2 = asmjit::a64::x25;
  static constexpr asmjit::a64::Gp kGuestPin3 = asmjit::a64::x26;
  static constexpr asmjit::a64::Gp kGuestPin4 = asmjit::a64::x27;
  static constexpr asmjit::a64::Gp kGuestPin5 = asmjit::a64::x28;

  static constexpr asmjit::RegMask kPersistentGpMask =
      asmjit::axl::bit_mask<asmjit::RegMask>(
          kCpu.id(), kRegs.id(), kChainCount.id(), kNextPc.id(),
          kGuestPin0.id(), kGuestPin1.id(), kGuestPin2.id(),
          kGuestPin3.id(), kGuestPin4.id(), kGuestPin5.id());

  // Bindings are explicit and ordered: entry i maps to host x23+i. Keeping the
  // policy out of this table lets blocks and traces share the same routing machinery.
  std::array<uint8_t, kGuestPinCount> guest_by_pin{};
  uint8_t pin_count = 0;

  static constexpr bool guest_is_shadowable(uint32_t raw_guest) noexcept {
    return raw_guest < 32 && (raw_guest & 0x0cu) == 0x04u;
  }

  // Dynamic bindings exclude variant-dependent slots. Fixed block bindings may use
  // bind_main_guest() for an explicitly main-bank value that PAL-shadow routing bypasses.
  static constexpr bool guest_pin_candidate(uint32_t raw_guest) noexcept {
    return raw_guest < 31 && !guest_is_shadowable(raw_guest);
  }

  constexpr bool bind_main_guest(uint32_t raw_guest) noexcept {
    if (raw_guest >= 31 || pin_count >= kGuestPinCount
        || pin_index_of(raw_guest) >= 0) return false;
    guest_by_pin[pin_count++] = static_cast<uint8_t>(raw_guest);
    return true;
  }

  constexpr bool bind_guest(uint32_t raw_guest) noexcept {
    return guest_pin_candidate(raw_guest) && bind_main_guest(raw_guest);
  }

  constexpr int pin_index_of(uint32_t raw_guest) const noexcept {
    if (raw_guest >= 32) return -1;
    for (uint32_t i = 0; i < pin_count; ++i)
      if (guest_by_pin[i] == raw_guest) return static_cast<int>(i);
    return -1;
  }

  constexpr int guest_of_pin(uint32_t pin_index) const noexcept {
    return pin_index < pin_count ? guest_by_pin[pin_index] : -1;
  }

  constexpr int host_of(uint32_t raw_guest, bool pal_shadow) const noexcept {
    if (raw_guest >= 31 || (pal_shadow && guest_is_shadowable(raw_guest))) return -1;
    const int pin_index = pin_index_of(raw_guest);
    return pin_index < 0 ? -1 : static_cast<int>(kGuestPin0.id()) + pin_index;
  }
};

static_assert(CJitEngine::RegAlloc::kGuestPin1.id()
                  == CJitEngine::RegAlloc::kGuestPin0.id() + 1
              && CJitEngine::RegAlloc::kGuestPin2.id()
                  == CJitEngine::RegAlloc::kGuestPin0.id() + 2
              && CJitEngine::RegAlloc::kGuestPin3.id()
                  == CJitEngine::RegAlloc::kGuestPin0.id() + 3
              && CJitEngine::RegAlloc::kGuestPin4.id()
                  == CJitEngine::RegAlloc::kGuestPin0.id() + 4
              && CJitEngine::RegAlloc::kGuestPin5.id()
                  == CJitEngine::RegAlloc::kGuestPin0.id() + 5,
              "A64 guest pin indices must map contiguously to x23-x28");

static_assert((CJitEngine::RegAlloc::kPersistentGpMask
               & asmjit::axl::bit_mask<asmjit::RegMask>(8, 16, 17,
                     asmjit::a64::Gp::kIdOs, 29, 30, 31)) == 0,
              "A64 persistent registers must exclude ABI, linker, platform, frame, link, and stack registers");
static_assert((CJitEngine::RegAlloc::kPersistentGpMask
               & asmjit::axl::bit_mask<asmjit::RegMask>(
                     CJitEngine::RegAlloc::kScratch0.id())) == 0,
              "A64 expansion scratch must not overlap persistent registers");
static_assert(CJitEngine::RegAlloc::kScratch6.id()
                  - CJitEngine::RegAlloc::kScratch0.id() == 6
              && CJitEngine::RegAlloc::kCallTarget.id()
                  == CJitEngine::RegAlloc::kScratch6.id() + 1,
              "A64 tail scratch registers must remain the contiguous x9-x16 bank");

namespace {

// Log any asmjit emit failure (stderr, or the disassembly trace file under JIT_DISASM).
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

// dirty kill switches for remote debugging
static constexpr bool kA64DpcReuse = true;        // reuse {va, slot, bias} across memops
static constexpr bool kA64ForwardValues = true;   // reuse x11's guest-register mirror
static constexpr bool kA64DirectPatch = true;     // non-PAL branch/fall-through exits as patched `b`

// Every block body shares this exact mapping so a chained entry can inherit the
// live pin bank without an adapter. R22/R23 name the main-bank slots; PAL-shadow
// accesses to those architectural registers continue to route through memory.
static constexpr std::array<uint8_t, CJitEngine::RegAlloc::kGuestPinCount>
    kA64BlockGuestPins{{1, 16, 30, 22, 23, 27}};

static constexpr CJitEngine::RegAlloc make_a64_block_regalloc() noexcept
{
  CJitEngine::RegAlloc regs{};
  for (const uint8_t guest : kA64BlockGuestPins)
    if (!regs.bind_main_guest(guest)) return {};
  return regs;
}

enum A64GprOperandMask : uint8_t {
  kA64GprRa = 1u << 0,
  kA64GprRb = 1u << 1,
  kA64GprRc = 1u << 2
};

enum class A64OpKind : uint8_t {
  kUnsupported,
  kValidationProbe,
  kIntlLogical,   // INTL (0x11) AND/BIS/XOR/BIC/ORNOT/EQV: 1:1 A64 register ops
  kBranchInt,     // BR/BSR + conditional integer branches (0x30/0x34/0x38-0x3f)
  kHwMfpr,        // HW_MFPR (0x19), PALmode: plain IPR fields inline, others via helper
  kIntsShift,     // INTS (0x12) SLL/SRL/SRA: LSLV/LSRV/ASRV share Alpha's mod-64 count
  kLoadAddress,   // LDA/LDAH (0x08/0x09): Ra = Rb + sext(disp16) (<<16) -- pure ALU
  kIntsZap,       // INTS (0x12) ZAP/ZAPNOT: Rc = Ra & byte-expanded keep-mask
  kMemLoad,       // LDQ/LDL/LDWU/LDBU/LDQ_U via jit_read (helper path; DPC inline later)
  kMemStore,      // STQ/STL/STW/STB/STQ_U via jit_write (helper path; DPC inline later)
  kJmpIndirect,   // JMP/JSR/RET (0x1a) + HW_RET (0x1e): computed-jump terminators
  kIntlCmov,      // INTL CMOVxx: Rc = cond(Ra) ? op2 : Rc (branch-over-store form)
  kIntlProbe,     // INTL AMASK (Ra==31 form) / IMPLVER: CPU feature constants
  kIntsByte,      // INTS EXT/INS/MSK byte-manip, pos = (op2 & 7) * 8 (cpu_bwx.h)
  kFptiInt,       // FPTI (0x1c) SEXTB/SEXTW/CTLZ/CTTZ: sxtb/sxth/clz/rbit+clz
  kFtoi,          // FPTI FTOIT/FTOIS: Rc = int view of f[Fa] via jit_ftoi (FEN bail)
  kLoadLocked,    // LDL_L/LDQ_L via jit_read_locked (lock monitor lives in the helper)
  kStoreCond,     // STL_C/STQ_C via jit_stc: Ra = success; 0x100 = translation bail
  kInta,          // INTA (0x10) add/sub/scaled/compares/CMPBGE (/V forms interpret)
  kMisc,          // MISC (0x18): MB dmb ish, WMB dmb ishst, TRAPB/EXCB/hints nothing, RPCC/RC/RS helper
  kCallPal,       // CALL_PAL (0x00): OPCDEC gate, exc_addr, optional link, vector terminator
  kHwMtpr,        // HW_MTPR (0x1d): family helper / no-op writes; policy selects redispatch
  kHwLd,          // HW_LD (0x1b): physical DRAM inline, virtual via jit_read_vpte
  kHwSt,          // HW_ST (0x1f): physical DRAM inline, MMIO/verify via jit_write_phys
  kIntm,          // INTM (0x13) MULQ/MULL/UMULH -> mul / mul+sxtw / umulh
  kFpMem,         // FP memory (0x20-0x27): f[Fa] <-> MEM via jit_fp_read/jit_fp_write
  kFltl,          // FLTL (0x17) non-arithmetic via jit_fltl (0/1 = FEN retry)
  kFltv,          // FLTV (0x15) VAX arith via jit_fltv (0/1 FEN retry/2 arith trap)
  kItof,          // ITFP ITOFS/ITOFF/ITOFT via jit_itof (0/1 = FEN retry)
  kBranchFp,      // FP branches (0x31-0x37): FPSTART gate + sign-magnitude compare
  kFltiArith,     // FLTI ADDx/SUBx/MULx/DIVx (S+T): inline scalar FP, bail on edges
  kFltiCmp,       // FLTI CMPTUN/EQ/LT/LE: fcmp+cset -> 2.0/0.0, NaN/denorm bails
  kFltiCvt,       // FLTI CVTST/CVTTS/CVTTQ/CVTQT/CVTQS: inline converts w/ bails
  kFsqrt          // ITFP SQRTS/SQRTT: fsqrt, same bail policy as the arith set
};

enum class A64TerminatorKind : uint8_t {
  kNone,
  kDirect,
  kIndirect,
  kCallPal,
  kRedispatch
};

enum class A64PlanStop : uint8_t {
  kInvalidSource,
  kEmptyBlock,
  kUnsupported,
  kBlockEnd,
  kSourceBoundary,
  kInstructionLimit,
  kTerminator
};

struct A64OpClass {
  A64OpKind kind = A64OpKind::kUnsupported;
  A64TerminatorKind terminator = A64TerminatorKind::kNone;
  uint8_t gpr_reads = 0;
  uint8_t gpr_writes = 0;
};

struct A64DecodedOp {
  uint32_t ins = 0;
  A64OpKind kind = A64OpKind::kUnsupported;
  A64TerminatorKind terminator = A64TerminatorKind::kNone;
  uint8_t gpr_reads = 0;
  uint8_t gpr_writes = 0;
  uint8_t opcode = 0;
  uint8_t ra = 0;
  uint8_t rb = 0;
  uint8_t rc = 0;
  uint8_t literal = 0;
  bool is_literal = false;
};

static_assert(sizeof(A64DecodedOp) <= 16,
              "A64 block plans must keep their fixed instruction snapshot compact");

struct A64GprUsage {
  std::array<uint16_t, 32> refs{};
  uint32_t reads = 0;
  uint32_t writes = 0;
};

static constexpr void note_a64_gpr(A64GprUsage& usage, uint32_t raw_guest,
                                   bool write) noexcept
{
  if (raw_guest >= 31) return;
  ++usage.refs[raw_guest];
  if (write) usage.writes |= 1u << raw_guest;
  else       usage.reads  |= 1u << raw_guest;
}

static constexpr void note_a64_op_gprs(A64GprUsage& usage,
                                       const A64DecodedOp& op) noexcept
{
  if (op.gpr_reads & kA64GprRa) note_a64_gpr(usage, op.ra, false);
  if (op.gpr_reads & kA64GprRb) note_a64_gpr(usage, op.rb, false);
  if (op.gpr_reads & kA64GprRc) note_a64_gpr(usage, op.rc, false);
  if (op.gpr_writes & kA64GprRa) note_a64_gpr(usage, op.ra, true);
  if (op.gpr_writes & kA64GprRb) note_a64_gpr(usage, op.rb, true);
  if (op.gpr_writes & kA64GprRc) note_a64_gpr(usage, op.rc, true);
}

static constexpr uint64_t a64_advance_pc(uint64_t pc, uint32_t n_instr) noexcept
{
  // Alpha PC arithmetic wraps modulo 2^64; adding whole words preserves its mode bits.
  return pc + static_cast<uint64_t>(n_instr) * sizeof(uint32_t);
}

static constexpr int64_t a64_branch_displacement(uint32_t ins) noexcept
{
  const uint32_t raw = ins & 0x1fffffu;
  return (raw & 0x100000u) != 0
      ? static_cast<int64_t>(raw) - 0x200000
      : static_cast<int64_t>(raw);
}

static constexpr uint64_t a64_branch_target(uint64_t fallthrough_pc,
                                            uint32_t ins) noexcept
{
  const int64_t byte_disp = a64_branch_displacement(ins) * 4;
  return fallthrough_pc + static_cast<uint64_t>(byte_disp);
}

static constexpr uint64_t a64_jmp_target(uint64_t source_pc, uint64_t rb) noexcept
{
  return (rb & ~uint64_t(3)) | (source_pc & 3u);
}

static constexpr uint64_t a64_hw_ret_target(uint64_t rb) noexcept
{
  return alpha_pal_return_target(
      alpha_pal_return_descriptor(kEv68PALPolicy, 0x1eu << 26), rb, 0);
}

enum class A64ExitKind : uint8_t {
  kNone,
  kFallthrough,
  kDirect,
  kIndirect,
  kCallPal,
  kRedispatch
};

enum class A64PcAuthority : uint8_t {
  kNextPc,
  kCpuState
};

struct A64BlockExit {
  // Deltas are added exactly once to x21, which carries prior chained-block progress.
  uint64_t fallthrough_pc = 0;
  uint32_t completed_delta = 0;
  A64ExitKind kind = A64ExitKind::kNone;

  constexpr bool valid() const noexcept { return kind != A64ExitKind::kNone; }
};

struct A64OpExit {
  uint64_t next_pc = 0;
  uint32_t completed_delta = 0;
  A64PcAuthority pc_authority = A64PcAuthority::kNextPc;
};

static constexpr A64BlockExit plan_a64_exit(uint64_t start_pc, uint32_t count,
                                            A64TerminatorKind terminator) noexcept
{
  if (count == 0) return {start_pc, 0, A64ExitKind::kNone};

  A64ExitKind kind = A64ExitKind::kFallthrough;
  switch (terminator) {
    case A64TerminatorKind::kDirect:     kind = A64ExitKind::kDirect; break;
    case A64TerminatorKind::kIndirect:   kind = A64ExitKind::kIndirect; break;
    case A64TerminatorKind::kCallPal:    kind = A64ExitKind::kCallPal; break;
    case A64TerminatorKind::kRedispatch: kind = A64ExitKind::kRedispatch; break;
    case A64TerminatorKind::kNone:       break;
  }
  return {a64_advance_pc(start_pc, count), count, kind};
}

static constexpr A64OpExit a64_retry_exit(uint64_t start_pc,
                                          uint32_t op_index) noexcept
{
  return {a64_advance_pc(start_pc, op_index), op_index, A64PcAuthority::kNextPc};
}

static constexpr A64OpExit a64_completed_trap_exit(uint32_t op_index) noexcept
{
  return {0, op_index + 1, A64PcAuthority::kCpuState};
}

static constexpr bool a64_publish_pc_before_chain(const A64BlockExit& exit,
                                                  bool pal_block) noexcept
{
  return pal_block || exit.kind == A64ExitKind::kCallPal
      || exit.kind == A64ExitKind::kRedispatch;
}

enum class A64ChainGate : uint8_t {
  kNone,
  kPollAll,
  kDeferInterrupt,
  kPollInterruptOnNativeTarget
};

enum class A64LinkVariantPolicy : uint8_t {
  kNone,
  kAlways,
  kIfTargetPal
};

static constexpr uint32_t kA64MaxBlockOps = 64;

// Local policy for the shared LinkSlot protocol.
struct A64DirectChainContract {
  A64ExitKind kind = A64ExitKind::kNone;
  uint32_t completed_delta = 0;
  A64ChainGate gate = A64ChainGate::kNone;
  A64PcAuthority bailout_pc = A64PcAuthority::kNextPc;
  bool gate_exit = false;
  bool source_pal_guard = false;
  bool target_pal_guard = false;
  bool request_patch_on_slot_miss = false;
  bool self_loop_candidate = false;
  bool publish_pc_before_probe = false;
  bool source_pal_shadow = false;

  constexpr bool eligible() const noexcept {
    return kind == A64ExitKind::kFallthrough
        || kind == A64ExitKind::kDirect
        || kind == A64ExitKind::kCallPal
        || kind == A64ExitKind::kRedispatch;
  }

  constexpr bool valid() const noexcept {
    if (kind == A64ExitKind::kNone || completed_delta == 0
        || completed_delta > kA64MaxBlockOps
        || bailout_pc != A64PcAuthority::kNextPc)
      return false;

    if (!eligible()) {
      if (kind != A64ExitKind::kIndirect)
        return false;
      return gate == A64ChainGate::kNone && !gate_exit
          && !source_pal_guard && !target_pal_guard
          && !request_patch_on_slot_miss && !self_loop_candidate
          && !publish_pc_before_probe && !source_pal_shadow;
    }

    // Gate thinning
    const bool gate_ok =
        kind == A64ExitKind::kFallthrough ? !gate_exit
      : kind == A64ExitKind::kDirect      ? true
      : gate_exit;
    return gate_ok
        && gate == (!gate_exit ? A64ChainGate::kNone
                    : source_pal_guard ? A64ChainGate::kDeferInterrupt
                                       : A64ChainGate::kPollAll)
        && target_pal_guard
            == (kind == A64ExitKind::kCallPal && !source_pal_guard)
        && request_patch_on_slot_miss
        && self_loop_candidate == gate_exit
        && publish_pc_before_probe
            == (source_pal_guard || target_pal_guard
                || kind == A64ExitKind::kRedispatch)
        && (source_pal_guard || !source_pal_shadow);
  }
};

static constexpr A64DirectChainContract plan_a64_direct_chain(
    const A64BlockExit& exit, uint32_t terminator_ins,
    bool pal_block, bool pal_shadow) noexcept
{
  A64DirectChainContract contract{};
  contract.kind = exit.kind;
  contract.completed_delta = exit.completed_delta;
  if (!contract.eligible()) return contract;

  // Gate thinning (x64 parity)
  const uint32_t opcode = terminator_ins >> 26;
  const bool forward_branch = exit.kind == A64ExitKind::kDirect
      && opcode >= 0x30 && opcode <= 0x3f
      && ((terminator_ins >> 20) & 1u) == 0;   // disp21 sign bit clear
  contract.gate_exit = exit.kind != A64ExitKind::kFallthrough && !forward_branch;

  contract.gate = !contract.gate_exit ? A64ChainGate::kNone
      : pal_block ? A64ChainGate::kDeferInterrupt
                  : A64ChainGate::kPollAll;
  contract.source_pal_guard = pal_block;
  contract.target_pal_guard = exit.kind == A64ExitKind::kCallPal && !pal_block;
  contract.request_patch_on_slot_miss = true;
  // x64 emits the self-link check only on gated exits: a fall-through or forward
  // branch can never target its own block start. CALL_PAL and IPR redispatch
  // keep it, matching the shared branch-tail path.
  contract.self_loop_candidate = contract.gate_exit;
  contract.publish_pc_before_probe = a64_publish_pc_before_chain(exit, pal_block);
  contract.source_pal_shadow = pal_block && pal_shadow;
  return contract;
}

enum class A64IndirectKind : uint8_t {
  kNone,
  kJmp,
  kHwRet,
  kInvalid
};

struct A64IndirectChainContract {
  A64IndirectKind kind = A64IndirectKind::kNone;
  uint32_t completed_delta = 0;
  A64ChainGate gate = A64ChainGate::kNone;
  A64LinkVariantPolicy target_variant = A64LinkVariantPolicy::kNone;
  bool pal_block = false;
  bool source_pal_shadow = false;
  bool probe_pic = false;
  bool call_resolver_on_miss = false;
  bool publish_pc_before_probe = false;
  bool publish_pc_on_miss = false;

  constexpr bool eligible() const noexcept {
    return kind == A64IndirectKind::kJmp || kind == A64IndirectKind::kHwRet;
  }

  constexpr bool valid() const noexcept {
    if (kind == A64IndirectKind::kNone)
      return completed_delta == 0 && gate == A64ChainGate::kNone
          && target_variant == A64LinkVariantPolicy::kNone
          && !pal_block && !source_pal_shadow && !probe_pic
          && !call_resolver_on_miss && !publish_pc_before_probe
          && !publish_pc_on_miss;
    if (!eligible() || (kind == A64IndirectKind::kHwRet && !pal_block)
        || completed_delta == 0
        || completed_delta > kA64MaxBlockOps)
      return false;

    const A64ChainGate expected_gate = !pal_block
        ? A64ChainGate::kPollAll
        : kind == A64IndirectKind::kHwRet
            ? A64ChainGate::kPollInterruptOnNativeTarget
            : A64ChainGate::kDeferInterrupt;
    const A64LinkVariantPolicy expected_variant =
        kind == A64IndirectKind::kHwRet
            ? A64LinkVariantPolicy::kIfTargetPal
            : A64LinkVariantPolicy::kNone;
    return gate == expected_gate && target_variant == expected_variant
        && (pal_block || !source_pal_shadow)
        && probe_pic && call_resolver_on_miss
        && publish_pc_before_probe == pal_block
        && publish_pc_on_miss;
  }
};

static constexpr A64IndirectChainContract plan_a64_indirect_chain(
    const A64BlockExit& exit, uint32_t terminator_ins,
    bool pal_block, bool pal_shadow) noexcept
{
  A64IndirectChainContract contract{};
  if (exit.kind != A64ExitKind::kIndirect) return contract;

  contract.completed_delta = exit.completed_delta;
  switch (terminator_ins >> 26) {
    case 0x1a: contract.kind = A64IndirectKind::kJmp; break;
    case 0x1e:
      contract.kind = pal_block ? A64IndirectKind::kHwRet
                                : A64IndirectKind::kInvalid;
      if (!pal_block) return contract;
      break;
    default:   contract.kind = A64IndirectKind::kInvalid; return contract;
  }
  contract.gate = !pal_block
      ? A64ChainGate::kPollAll
      : contract.kind == A64IndirectKind::kHwRet
          ? A64ChainGate::kPollInterruptOnNativeTarget
          : A64ChainGate::kDeferInterrupt;
  contract.target_variant = contract.kind == A64IndirectKind::kHwRet
      ? A64LinkVariantPolicy::kIfTargetPal
      : A64LinkVariantPolicy::kNone;
  contract.pal_block = pal_block;
  contract.source_pal_shadow = pal_block && pal_shadow;
  contract.probe_pic = true;
  contract.call_resolver_on_miss = true;
  contract.publish_pc_before_probe = pal_block;
  contract.publish_pc_on_miss = true;
  return contract;
}

static constexpr uint64_t kA64LinkPalShadowBit = uint64_t(1) << 63;
static constexpr uint64_t kA64LinkEpochMask = ~kA64LinkPalShadowBit;
static constexpr uint64_t kA64InvalidLinkTag = ~uint64_t(0);

static constexpr uint64_t a64_pack_link_vgen(uint64_t epoch,
                                             bool pal_shadow) noexcept
{
  return (epoch & kA64LinkEpochMask)
      | (pal_shadow ? kA64LinkPalShadowBit : 0);
}

static constexpr uint64_t a64_link_epoch(uint64_t packed_vgen) noexcept
{
  return packed_vgen & kA64LinkEpochMask;
}

static constexpr bool a64_link_pal_shadow(uint64_t packed_vgen) noexcept
{
  return (packed_vgen & kA64LinkPalShadowBit) != 0;
}

struct A64LinkProbe {
  uint64_t tag = kA64InvalidLinkTag;
  uint64_t packed_vgen = 0;
  bool body_present = false;

  // Mapping/coherence changes invalidate affected links.
  // Mode changes may keep them.
  // Native target bodies check their per-mode execution proof on entry.
  constexpr bool matches(uint64_t target,
                         A64LinkVariantPolicy variant_policy,
                         bool live_pal_shadow) const noexcept {
    if (!body_present || tag != target)
      return false;
    switch (variant_policy) {
      case A64LinkVariantPolicy::kNone:
        return true;
      case A64LinkVariantPolicy::kAlways:
        return a64_link_pal_shadow(packed_vgen) == live_pal_shadow;
      case A64LinkVariantPolicy::kIfTargetPal:
        return (target & 1u) == 0
            || a64_link_pal_shadow(packed_vgen) == live_pal_shadow;
    }
    return false;
  }
};

static constexpr int a64_find_link_probe(
    const std::array<A64LinkProbe, CJitEngine::kLinkSlots>& slots,
    uint64_t target,
    A64LinkVariantPolicy variant_policy,
    bool live_pal_shadow) noexcept
{
  for (uint32_t i = 0; i < slots.size(); ++i)
    if (slots[i].matches(target, variant_policy, live_pal_shadow))
      return static_cast<int>(i);
  return -1;
}

static_assert(sizeof(void*) == 8
              && std::is_standard_layout<CJitEngine::LinkSlot>::value
              && offsetof(CJitEngine::LinkSlot, tag) == 0
              && offsetof(CJitEngine::LinkSlot, vgen) == 8
              && offsetof(CJitEngine::LinkSlot, body) == 16
              && sizeof(CJitEngine::LinkSlot) == 24
              && alignof(CJitEngine::LinkSlot) >= 8
              && CJitEngine::kLinkSlots == 2,
              "A64 cached links must match the shared two-slot snapshot ABI");

static constexpr bool a64_direct_chain_contract_probe() noexcept
{
  const A64BlockExit none = plan_a64_exit(0x1000, 0, A64TerminatorKind::kNone);
  const A64BlockExit fall = plan_a64_exit(0x1000, 2, A64TerminatorKind::kNone);
  const A64BlockExit direct =
      plan_a64_exit(0x1000, 1, A64TerminatorKind::kDirect);
  const A64BlockExit indirect =
      plan_a64_exit(0x1000, 1, A64TerminatorKind::kIndirect);
  const A64BlockExit call_pal =
      plan_a64_exit(0x1000, 1, A64TerminatorKind::kCallPal);
  const A64BlockExit redispatch =
      plan_a64_exit(0x1000, 1, A64TerminatorKind::kRedispatch);

  // Terminator words for the gate-thinning split: BEQ forward (disp21 sign clear),
  // BEQ backward (sign set), CALL_PAL, I_CTL (the redispatch HW_MTPR), and JMP.
  constexpr uint32_t fwd_br = (0x39u << 26) | 0x000004u;
  constexpr uint32_t back_br = (0x39u << 26) | 0x1fffffu;
  constexpr uint32_t non_br = 0x10u << 26;
  constexpr uint32_t call_ins = 0x86u;
  constexpr uint32_t ictl_ins = (0x1du << 26) | (0x11u << 8);
  constexpr uint32_t jmp_ins = 0x1au << 26;

  const A64DirectChainContract native_fall =
      plan_a64_direct_chain(fall, non_br, false, false);
  const A64DirectChainContract native_fwd =
      plan_a64_direct_chain(direct, fwd_br, false, false);
  const A64DirectChainContract native_back =
      plan_a64_direct_chain(direct, back_br, false, false);
  const A64DirectChainContract native_call =
      plan_a64_direct_chain(call_pal, call_ins, false, false);
  const A64DirectChainContract pal_fall =
      plan_a64_direct_chain(fall, non_br, true, false);
  const A64DirectChainContract pal_fwd =
      plan_a64_direct_chain(direct, fwd_br, true, true);
  const A64DirectChainContract pal_back =
      plan_a64_direct_chain(direct, back_br, true, true);
  const A64DirectChainContract pal_call =
      plan_a64_direct_chain(call_pal, call_ins, true, true);
  const A64DirectChainContract pal_redispatch =
      plan_a64_direct_chain(redispatch, ictl_ins, true, true);
  const A64DirectChainContract dynamic =
      plan_a64_direct_chain(indirect, jmp_ins, false, false);
  const A64DirectChainContract native_redispatch =
      plan_a64_direct_chain(redispatch, ictl_ins, false, false);
  const A64DirectChainContract empty =
      plan_a64_direct_chain(none, non_br, false, false);

  A64DirectChainContract malformed = native_back;
  malformed.request_patch_on_slot_miss = false;
  A64DirectChainContract zero_count = native_back;
  zero_count.completed_delta = 0;
  A64DirectChainContract max_count = native_back;
  max_count.completed_delta = kA64MaxBlockOps;
  A64DirectChainContract unknown_gate = native_back;
  unknown_gate.gate = static_cast<A64ChainGate>(0xff);
  A64DirectChainContract native_shadow = native_back;
  native_shadow.source_pal_shadow = true;
  A64DirectChainContract missing_target_guard = native_call;
  missing_target_guard.target_pal_guard = false;
  A64DirectChainContract inactive_patch = dynamic;
  inactive_patch.request_patch_on_slot_miss = true;
  A64DirectChainContract gated_fall = native_fall;      // a fall-through must thin
  gated_fall.gate_exit = true;
  gated_fall.gate = A64ChainGate::kPollAll;
  gated_fall.self_loop_candidate = true;
  A64DirectChainContract thinned_call = native_call;    // CALL_PAL must gate
  thinned_call.gate_exit = false;
  thinned_call.gate = A64ChainGate::kNone;
  thinned_call.self_loop_candidate = false;
  A64DirectChainContract looped_fwd = native_fwd;       // thinned exits carry no self-link
  looped_fwd.self_loop_candidate = true;
  const A64DirectChainContract oversized =
      plan_a64_direct_chain(
          {0x1104, kA64MaxBlockOps + 1, A64ExitKind::kDirect}, back_br, false, false);
  const A64DirectChainContract unknown =
      plan_a64_direct_chain({0x1104, 1, static_cast<A64ExitKind>(0xff)}, non_br,
                            false, false);

  return native_fall.valid() && native_fall.eligible()
      && !native_fall.gate_exit
      && native_fall.gate == A64ChainGate::kNone
      && native_fall.request_patch_on_slot_miss
      && !native_fall.self_loop_candidate
      && !native_fall.publish_pc_before_probe
      && native_fwd.valid() && !native_fwd.gate_exit
      && native_fwd.gate == A64ChainGate::kNone
      && !native_fwd.self_loop_candidate
      && native_fwd.request_patch_on_slot_miss
      && !native_fwd.publish_pc_before_probe
      && native_back.valid() && native_back.gate_exit
      && native_back.self_loop_candidate
      && native_back.gate == A64ChainGate::kPollAll
      && native_call.valid() && native_call.gate_exit
      && native_call.target_pal_guard
      && native_call.self_loop_candidate && native_call.publish_pc_before_probe
      && !native_call.source_pal_guard
      && pal_fall.valid() && pal_fall.source_pal_guard
      && !pal_fall.source_pal_shadow
      && !pal_fall.gate_exit && pal_fall.gate == A64ChainGate::kNone
      && pal_fall.publish_pc_before_probe
      && pal_fwd.valid() && !pal_fwd.gate_exit
      && pal_fwd.gate == A64ChainGate::kNone
      && pal_fwd.source_pal_guard && pal_fwd.source_pal_shadow
      && !pal_fwd.self_loop_candidate && pal_fwd.publish_pc_before_probe
      && pal_back.valid() && pal_back.source_pal_guard
      && pal_back.gate == A64ChainGate::kDeferInterrupt
      && pal_back.source_pal_shadow && pal_back.self_loop_candidate
      && pal_call.valid() && pal_call.source_pal_guard
      && pal_call.gate == A64ChainGate::kDeferInterrupt
      && !pal_call.target_pal_guard && pal_call.self_loop_candidate
      && pal_call.publish_pc_before_probe
      && pal_redispatch.valid() && pal_redispatch.eligible()
      && pal_redispatch.gate == A64ChainGate::kDeferInterrupt
      && pal_redispatch.source_pal_guard
      && pal_redispatch.source_pal_shadow
      && !pal_redispatch.target_pal_guard
      && pal_redispatch.request_patch_on_slot_miss
      && pal_redispatch.self_loop_candidate
      && pal_redispatch.publish_pc_before_probe
      && dynamic.valid() && !dynamic.eligible()
      && !dynamic.request_patch_on_slot_miss
      && native_redispatch.valid() && native_redispatch.eligible()
      && native_redispatch.gate == A64ChainGate::kPollAll
      && !native_redispatch.source_pal_guard
      && !native_redispatch.target_pal_guard
      && native_redispatch.request_patch_on_slot_miss
      && native_redispatch.self_loop_candidate
      && native_redispatch.publish_pc_before_probe
      && !empty.valid() && !malformed.valid()
      && !zero_count.valid() && max_count.valid()
      && !unknown_gate.valid() && !native_shadow.valid()
      && !missing_target_guard.valid() && !inactive_patch.valid()
      && !gated_fall.valid() && !thinned_call.valid() && !looped_fwd.valid()
      && !oversized.valid()
      && !unknown.valid();
}

static constexpr bool a64_indirect_chain_contract_probe() noexcept
{
  constexpr uint32_t jmp = 0x1au << 26;
  constexpr uint32_t hw_ret = 0x1eu << 26;
  const A64BlockExit indirect =
      plan_a64_exit(0x1001, 1, A64TerminatorKind::kIndirect);
  const A64BlockExit fall =
      plan_a64_exit(0x1001, 1, A64TerminatorKind::kNone);

  const A64IndirectChainContract native_jmp =
      plan_a64_indirect_chain(indirect, jmp, false, false);
  const A64IndirectChainContract native_hw =
      plan_a64_indirect_chain(indirect, hw_ret, false, false);
  const A64IndirectChainContract pal_jmp =
      plan_a64_indirect_chain(indirect, jmp, true, false);
  const A64IndirectChainContract pal_hw =
      plan_a64_indirect_chain(indirect, hw_ret, true, true);
  const A64IndirectChainContract inactive =
      plan_a64_indirect_chain(fall, jmp, false, false);
  const A64IndirectChainContract unknown =
      plan_a64_indirect_chain(indirect, 0x10u << 26, false, false);

  A64IndirectChainContract zero_count = native_jmp;
  zero_count.completed_delta = 0;
  A64IndirectChainContract max_count = native_jmp;
  max_count.completed_delta = kA64MaxBlockOps;
  A64IndirectChainContract oversized = native_jmp;
  oversized.completed_delta = kA64MaxBlockOps + 1;
  A64IndirectChainContract wrong_gate = pal_hw;
  wrong_gate.gate = A64ChainGate::kDeferInterrupt;
  A64IndirectChainContract wrong_variant = pal_hw;
  wrong_variant.target_variant = A64LinkVariantPolicy::kAlways;
  A64IndirectChainContract no_resolver = native_jmp;
  no_resolver.call_resolver_on_miss = false;
  A64IndirectChainContract native_shadow = native_jmp;
  native_shadow.source_pal_shadow = true;
  A64IndirectChainContract bad_kind = native_jmp;
  bad_kind.kind = static_cast<A64IndirectKind>(0xff);

  return native_jmp.valid() && native_jmp.eligible()
      && native_jmp.kind == A64IndirectKind::kJmp
      && native_jmp.gate == A64ChainGate::kPollAll
      && native_jmp.target_variant == A64LinkVariantPolicy::kNone
      && !native_jmp.pal_block
      && !native_jmp.publish_pc_before_probe
      && !native_hw.valid()
      && pal_jmp.valid() && pal_jmp.pal_block
      && !pal_jmp.source_pal_shadow
      && pal_jmp.gate == A64ChainGate::kDeferInterrupt
      && pal_jmp.publish_pc_before_probe
      && pal_hw.valid() && pal_hw.pal_block
      && pal_hw.source_pal_shadow
      && pal_hw.gate == A64ChainGate::kPollInterruptOnNativeTarget
      && pal_hw.target_variant == A64LinkVariantPolicy::kIfTargetPal
      && pal_hw.publish_pc_before_probe
      && inactive.valid() && !inactive.eligible()
      && !unknown.valid() && !zero_count.valid() && max_count.valid()
      && !oversized.valid() && !wrong_gate.valid() && !wrong_variant.valid()
      && !no_resolver.valid() && !native_shadow.valid() && !bad_kind.valid();
}

static constexpr bool a64_link_probe_contract_probe() noexcept
{
  constexpr uint64_t native_target = 0x2000;
  constexpr uint64_t pal_target = 0x2001;
  constexpr uint64_t epoch = 0x12345678;
  const std::array<A64LinkProbe, CJitEngine::kLinkSlots> native_slots{{
      {0x3000, a64_pack_link_vgen(epoch, false), true},
      {native_target, a64_pack_link_vgen(epoch, true), true}}};
  const std::array<A64LinkProbe, CJitEngine::kLinkSlots> pal_slots{{
      {pal_target, a64_pack_link_vgen(epoch, false), true},
      {pal_target, a64_pack_link_vgen(epoch, true), true}}};
  const std::array<A64LinkProbe, CJitEngine::kLinkSlots> empty_slots{{
      {pal_target, a64_pack_link_vgen(epoch, true), false},
      {kA64InvalidLinkTag, 0, false}}};

  return a64_pack_link_vgen(epoch, false) == epoch
      && a64_pack_link_vgen(UINT64_MAX, false) == kA64LinkEpochMask
      && a64_pack_link_vgen(UINT64_MAX, true) == UINT64_MAX
      && a64_link_epoch(a64_pack_link_vgen(epoch, true)) == epoch
      && a64_link_pal_shadow(a64_pack_link_vgen(epoch, true))
      && !a64_link_pal_shadow(a64_pack_link_vgen(epoch, false))
      // A native target ignores the packed PAL variant; slot zero misses by tag.
      && a64_find_link_probe(
          native_slots, native_target,
          A64LinkVariantPolicy::kNone, false) == 1
      && a64_find_link_probe(native_slots, 0x4000,
          A64LinkVariantPolicy::kNone, false) == -1
      // A PAL target skips the wrong variant in slot zero and reaches slot one.
      && a64_find_link_probe(pal_slots, pal_target,
          A64LinkVariantPolicy::kAlways, true) == 1
      && a64_find_link_probe(pal_slots, pal_target,
          A64LinkVariantPolicy::kAlways, false) == 0
      // HW_RET ignores the variant for a native target, but enforces it in PALmode.
      && a64_find_link_probe(native_slots, native_target,
          A64LinkVariantPolicy::kIfTargetPal, false) == 1
      && a64_find_link_probe(pal_slots, pal_target,
          A64LinkVariantPolicy::kIfTargetPal, true) == 1
      && a64_find_link_probe(
          empty_slots, pal_target,
          A64LinkVariantPolicy::kAlways, true) == -1
      && a64_find_link_probe(pal_slots, pal_target,
          static_cast<A64LinkVariantPolicy>(0xff), true) == -1;
}

static_assert(a64_direct_chain_contract_probe(),
              "A64 direct-chain policy must gate, guard, and route every exit kind consistently");
static_assert(a64_indirect_chain_contract_probe(),
              "A64 indirect-chain policy must distinguish JMP and PAL-return gates");
static_assert(a64_link_probe_contract_probe(),
              "A64 link probes must reject absent, mistagged, and wrong-variant snapshots");

struct A64BlockPlan {
  static constexpr uint32_t kMaxOps = kA64MaxBlockOps;

  std::array<A64DecodedOp, kMaxOps> ops{};
  A64GprUsage gpr_usage{};
  uint32_t count = 0;
  uint32_t breaker_word = 0;
  A64PlanStop stop = A64PlanStop::kInvalidSource;
  A64TerminatorKind terminator = A64TerminatorKind::kNone;
};

enum class A64EmitState : uint8_t {
  kUnhandled,
  kComplete
};

enum class A64Mainline : uint8_t {
  kNone,
  kContinues,
  kExitReady
};

static constexpr bool a64_known_terminator(A64TerminatorKind kind) noexcept
{
  switch (kind) {
    case A64TerminatorKind::kNone:
    case A64TerminatorKind::kDirect:
    case A64TerminatorKind::kIndirect:
    case A64TerminatorKind::kCallPal:
    case A64TerminatorKind::kRedispatch:
      return true;
  }
  return false;
}

static constexpr bool a64_supported_op_kind(A64OpKind kind) noexcept
{
  switch (kind) {
    case A64OpKind::kUnsupported:
    case A64OpKind::kValidationProbe:
      return false;
    case A64OpKind::kIntlLogical:
    case A64OpKind::kBranchInt:
    case A64OpKind::kHwMfpr:
    case A64OpKind::kIntsShift:
    case A64OpKind::kLoadAddress:
    case A64OpKind::kIntsZap:
    case A64OpKind::kMemLoad:
    case A64OpKind::kMemStore:
    case A64OpKind::kJmpIndirect:
    case A64OpKind::kIntlCmov:
    case A64OpKind::kIntlProbe:
    case A64OpKind::kIntsByte:
    case A64OpKind::kFptiInt:
    case A64OpKind::kFtoi:
    case A64OpKind::kLoadLocked:
    case A64OpKind::kStoreCond:
    case A64OpKind::kInta:
    case A64OpKind::kMisc:
    case A64OpKind::kCallPal:
    case A64OpKind::kHwMtpr:
    case A64OpKind::kHwLd:
    case A64OpKind::kHwSt:
    case A64OpKind::kIntm:
    case A64OpKind::kFpMem:
    case A64OpKind::kFltl:
    case A64OpKind::kFltv:
    case A64OpKind::kItof:
    case A64OpKind::kBranchFp:
    case A64OpKind::kFltiArith:
    case A64OpKind::kFltiCmp:
    case A64OpKind::kFltiCvt:
    case A64OpKind::kFsqrt:
      return true;
  }
  return false;
}

static constexpr bool a64_validation_op_kind(A64OpKind kind) noexcept
{
  return kind == A64OpKind::kValidationProbe;
}

// A nonempty prefix may legitimately stop before an unsupported instruction.
// Dispatch back at start_pc + count * 4.
static constexpr bool a64_plan_shape_valid(const A64BlockPlan& plan) noexcept
{
  if (plan.count == 0 || plan.count > plan.ops.size()
      || !a64_known_terminator(plan.terminator))
    return false;

  const bool has_terminator = plan.terminator != A64TerminatorKind::kNone;
  if (has_terminator != (plan.stop == A64PlanStop::kTerminator)) return false;
  if (!has_terminator
      && plan.stop != A64PlanStop::kUnsupported
      && plan.stop != A64PlanStop::kBlockEnd
      && plan.stop != A64PlanStop::kSourceBoundary
      && plan.stop != A64PlanStop::kInstructionLimit)
    return false;
  if (plan.stop == A64PlanStop::kInstructionLimit
      && plan.count != A64BlockPlan::kMaxOps)
    return false;

  for (uint32_t i = 0; i < plan.count; ++i) {
    const A64DecodedOp& op = plan.ops[i];
    if (!a64_known_terminator(op.terminator))
      return false;
    const A64TerminatorKind expected = i + 1 == plan.count
        ? plan.terminator : A64TerminatorKind::kNone;
    if (op.terminator != expected) return false;
  }
  return true;
}

static constexpr bool a64_plan_ready_for_emission(
    const A64BlockPlan& plan) noexcept
{
  if (!a64_plan_shape_valid(plan)) return false;
  for (uint32_t i = 0; i < plan.count; ++i)
    if (!a64_supported_op_kind(plan.ops[i].kind)) return false;
  return true;
}

static constexpr bool a64_plan_ready_for_validation(
    const A64BlockPlan& plan) noexcept
{
  if (!a64_plan_shape_valid(plan)) return false;
  for (uint32_t i = 0; i < plan.count; ++i)
    if (!a64_validation_op_kind(plan.ops[i].kind)) return false;
  return true;
}

struct A64OpEmitReceipt {
  asmjit::Error error = asmjit::Error::kInvalidInstruction;
  A64OpKind kind = A64OpKind::kUnsupported;
  A64EmitState state = A64EmitState::kUnhandled;
  A64Mainline mainline = A64Mainline::kNone;
  A64TerminatorKind terminator = A64TerminatorKind::kNone;

  constexpr bool accepted_for(const A64DecodedOp& op,
                              bool validation_probe = false) const noexcept {
    const A64Mainline expected = op.terminator == A64TerminatorKind::kNone
        ? A64Mainline::kContinues : A64Mainline::kExitReady;
    const bool known_kind = a64_supported_op_kind(op.kind)
        || (validation_probe && a64_validation_op_kind(op.kind));
    return known_kind
        && error == asmjit::Error::kOk
        && kind == op.kind
        && state == A64EmitState::kComplete
        && mainline == expected
        && terminator == op.terminator;
  }
};

struct A64BodyEmitReceipt {
  asmjit::Error error = asmjit::Error::kInvalidState;
  uint32_t planned_count = 0;
  uint32_t attempted_count = 0;
  uint32_t emitted_count = 0;
  A64TerminatorKind observed_terminator = A64TerminatorKind::kNone;
  bool validation_probe = false;
  bool complete = false;

  constexpr bool complete_for(const A64BlockPlan& plan) const noexcept {
    return error == asmjit::Error::kOk && complete && !validation_probe
        && planned_count == plan.count
        && attempted_count == plan.count && emitted_count == plan.count
        && observed_terminator == plan.terminator
        && a64_plan_ready_for_emission(plan);
  }

  constexpr bool complete_for_validation(
      const A64BlockPlan& plan) const noexcept {
    return error == asmjit::Error::kOk && complete && validation_probe
        && planned_count == plan.count
        && attempted_count == plan.count && emitted_count == plan.count
        && observed_terminator == plan.terminator
        && a64_plan_ready_for_validation(plan);
  }
};

static constexpr A64BodyEmitReceipt a64_begin_body_emission(
    const A64BlockPlan& plan) noexcept
{
  A64BodyEmitReceipt receipt{};
  receipt.planned_count = plan.count;
  receipt.error = a64_plan_ready_for_emission(plan)
      ? asmjit::Error::kOk : asmjit::Error::kInvalidArgument;
  return receipt;
}

static constexpr A64BodyEmitReceipt a64_begin_body_validation(
    const A64BlockPlan& plan) noexcept
{
  A64BodyEmitReceipt receipt{};
  receipt.planned_count = plan.count;
  receipt.validation_probe = true;
  receipt.error = a64_plan_ready_for_validation(plan)
      ? asmjit::Error::kOk : asmjit::Error::kInvalidArgument;
  return receipt;
}

static constexpr bool a64_begin_op_emission(A64BodyEmitReceipt& body,
    const A64BlockPlan& plan, uint32_t index) noexcept
{
  if (body.error != asmjit::Error::kOk || body.complete
      || body.planned_count != plan.count
      || index != body.attempted_count || index != body.emitted_count
      || index >= plan.count) {
    if (body.error == asmjit::Error::kOk)
      body.error = asmjit::Error::kInvalidState;
    return false;
  }
  ++body.attempted_count;
  return true;
}

static constexpr bool a64_accept_op_emission(A64BodyEmitReceipt& body,
    const A64BlockPlan& plan, uint32_t index,
    const A64OpEmitReceipt& op) noexcept
{
  if (body.error != asmjit::Error::kOk || body.complete
      || body.planned_count != plan.count
      || index != body.emitted_count || body.attempted_count != index + 1
      || index >= plan.count) {
    if (body.error == asmjit::Error::kOk)
      body.error = asmjit::Error::kInvalidState;
    return false;
  }
  if (!op.accepted_for(plan.ops[index], body.validation_probe)) {
    body.error = op.error == asmjit::Error::kOk
        ? asmjit::Error::kInvalidState : op.error;
    return false;
  }
  if (op.terminator != A64TerminatorKind::kNone)
    body.observed_terminator = op.terminator;
  ++body.emitted_count;
  return true;
}

static constexpr bool a64_finish_body_emission(A64BodyEmitReceipt& body,
    const A64BlockPlan& plan) noexcept
{
  if (body.error != asmjit::Error::kOk || body.complete
      || body.planned_count != plan.count
      || body.attempted_count != plan.count
      || body.emitted_count != plan.count
      || body.observed_terminator != plan.terminator) {
    if (body.error == asmjit::Error::kOk)
      body.error = asmjit::Error::kInvalidState;
    return false;
  }
  body.complete = true;
  return true;
}

// Block image description.
struct A64PendingPublication {
  uint64_t code_size = 0;
  uint64_t body_off = 0;
  uint64_t source_hash = 0;
  uint64_t prior_code_bytes = 0;
  uint32_t plan_count = 0;
  uint32_t prefix_len = 0;
  uint32_t hash_len = 0;
  bool source_current = false;
  bool layout_complete = false;
  bool source_raced = false;      // snapshot words no longer match guest memory (transient)

  constexpr bool ready() const noexcept {
    return source_current && layout_complete
        && plan_count != 0 && plan_count <= A64BlockPlan::kMaxOps
        && prefix_len == plan_count
        && hash_len == prefix_len
        && code_size != 0 && (code_size & 3u) == 0
        && code_size <= UINT32_MAX
        && (body_off & 3u) == 0 && body_off < code_size
        && body_off <= UINT32_MAX
        && code_size <= UINT64_MAX - prior_code_bytes;
  }
};

struct A64ScanLimit {
  uint32_t count;
  A64PlanStop stop;
};

// Classification stays fail-closed: each translation adds its classifier and emitter together.
static constexpr A64OpClass classify_a64_op(uint32_t ins, bool pal_block,
    const AlphaIPRReadPolicy& ipr_read = kEv68IPRReadPolicy,
    const AlphaIPRWritePolicy& ipr_write = kEv68IPRWritePolicy,
    const AlphaPALPolicy& pal = kEv68PALPolicy,
    const AlphaHWLoadPolicy& hw_load = kEv68HWLoadPolicy,
    const AlphaHWStorePolicy& hw_store = kEv68HWStorePolicy) noexcept
{
  const uint32_t opcode = ins >> 26;
  const uint32_t func = (ins >> 5) & 0x7fu;
  const bool is_literal = ((ins >> 12) & 1u) != 0;
  const uint8_t operate_reads =
      static_cast<uint8_t>(kA64GprRa | (is_literal ? 0 : kA64GprRb));

  switch (opcode) {
    case 0x00: {   // CALL_PAL, WTINT interprets. HW format: no pin-mask roles.
      const uint32_t fn = ins & 0x1fffffffu;
      if (fn == 0x3E) break;
      if (fn <= 0x3F || (fn >= 0x80 && fn <= 0xBF))
        return {A64OpKind::kCallPal, A64TerminatorKind::kCallPal, 0, 0};
      break;
    }
    case 0x1b: {   // HW_LD (PALmode)
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        break;
      const auto descriptor = alpha_decode_hw_load(hw_load, ins);
      if (!alpha_hw_load_compilable(descriptor)) break;
      if (descriptor.address == AlphaHWLoadAddress::Physical
          && ((ins >> 21) & 0x1fu) == 31 && !descriptor.discard_safe)
        break;
      return {A64OpKind::kHwLd, A64TerminatorKind::kNone, 0, 0};
    }
    case 0x1d: {   // HW_MTPR (PALmode)
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        break;
      switch (ipr_write.describe(ins)) {
        case AlphaIPRWriteKind::Helper:
        case AlphaIPRWriteKind::Noop:
          return {A64OpKind::kHwMtpr, A64TerminatorKind::kNone, 0, 0};
        case AlphaIPRWriteKind::HelperTerminator:
          return {A64OpKind::kHwMtpr, A64TerminatorKind::kRedispatch, 0, 0};
        case AlphaIPRWriteKind::Interpreter: break;
      }
      break;
    }
    case 0x1f: {   // HW_ST (PALmode): the family admits compiled-safe physical stores.
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        break;
      const auto descriptor = alpha_decode_hw_store(hw_store, ins);
      if (alpha_hw_store_compilable(descriptor))
        return {A64OpKind::kHwSt, A64TerminatorKind::kNone, 0, 0};
      break;
    }
    case 0x10:   // INTA: non-trapping add/sub/scaled + compares + CMPBGE
      switch (func) {
        case 0x20: case 0x29: case 0x00: case 0x09:   // ADDQ SUBQ ADDL SUBL
        case 0x22: case 0x32: case 0x2b: case 0x3b:   // S4ADDQ S8ADDQ S4SUBQ S8SUBQ
        case 0x02: case 0x12: case 0x0b: case 0x1b:   // S4ADDL S8ADDL S4SUBL S8SUBL
        case 0x0f:                                    // CMPBGE
        case 0x2d: case 0x4d: case 0x6d:              // CMPEQ CMPLT CMPLE
        case 0x1d: case 0x3d:                         // CMPULT CMPULE
          return {A64OpKind::kInta, A64TerminatorKind::kNone,
                  operate_reads, static_cast<uint8_t>(kA64GprRc)};
      }
      break;
    case 0x17: {   // FLTL non-arithmetic
      const uint32_t f17 = (ins >> 5) & 0x7ffu;
      const bool ok17 = f17 == 0x010 || (f17 >= 0x020 && f17 <= 0x022)
                     || f17 == 0x024 || f17 == 0x025
                     || (f17 >= 0x02a && f17 <= 0x02f) || f17 == 0x030;
      if (!ok17) break;
      // f31-dest gate (the interp zeroes f[31] per instr): MF_FPCR writes f[Fa].
      if (f17 == 0x025)      { if (((ins >> 21) & 0x1fu) == 31) break; }
      else if (f17 != 0x024) { if ((ins & 0x1fu) == 31) break; }
      return {A64OpKind::kFltl, A64TerminatorKind::kNone, 0, 0};
    }
    case 0x15: {   // FLTV VAX arith/convert/compare via jit_fltv (x64 gate list)
      if ((ins & 0x1fu) == 31) break;   // Fc==31: interp zeroes f[31] per instr
      const uint32_t f15 = (ins >> 5) & 0x7ffu;
      if (f15 == 0x0a5 || f15 == 0x4a5 || f15 == 0x0a6 || f15 == 0x4a6
       || f15 == 0x0a7 || f15 == 0x4a7 || f15 == 0x03c || f15 == 0x0bc
       || f15 == 0x03e || f15 == 0x0be)
        return {A64OpKind::kFltv, A64TerminatorKind::kNone, 0, 0};
      if (f15 & 0x200u) break;          // invalid qualifier -> interp OPCDECs
      switch (f15 & 0x7fu) {
        case 0x000: case 0x001: case 0x002: case 0x003:   // ADDF/SUBF/MULF/DIVF
        case 0x01e:                                       // CVTDG
        case 0x020: case 0x021: case 0x022: case 0x023:   // ADDG/SUBG/MULG/DIVG
        case 0x02c: case 0x02d: case 0x02f:               // CVTGF/CVTGD/CVTGQ
          return {A64OpKind::kFltv, A64TerminatorKind::kNone, 0, 0};
      }
      break;
    }
    case 0x14: {   // ITFP: int->FP moves via jit_itof + inline IEEE SQRT 
      const uint32_t f14 = (ins >> 5) & 0x7ffu;
      if ((ins & 0x1fu) == 31) break;
      const uint32_t sb = f14 & 0x3fu;
      if (sb == 0x0b || sb == 0x2b) {              // SQRTS / SQRTT
        if (((ins >> 21) & 0x1fu) != 31) break;    // Ra must be R31 (else DO_SQRT* traps OPCDEC)
        if (((f14 & 0x600u) == 0x200u) || ((f14 & 0x500u) == 0x400u)) break;  // invalid qualifier -> OPCDEC
        const uint32_t r14 = (f14 >> 6) & 3u;
        if (r14 == 0 || r14 == 1) break;
        if (((f14 >> 8) & 7u) == 7) break;
        return {A64OpKind::kFsqrt, A64TerminatorKind::kNone, 0, 0};
      }
      if (((ins >> 16) & 0x1fu) != 31) break;      // ITOFx: Rb must be 31
      if (f14 == 0x004 || f14 == 0x014 || f14 == 0x024)
        return {A64OpKind::kItof, A64TerminatorKind::kNone, 0, 0};
      break;
    }
    case 0x16: {   // FLTI (IEEE): inline the steady-state paths
      const uint32_t f16 = (ins >> 5) & 0x7ffu;
      if ((ins & 0x1fu) == 31) break;
      if (f16 == 0x2ac || f16 == 0x6ac)            // CVTST (before the invalid gate)
        return {A64OpKind::kFltiCvt, A64TerminatorKind::kNone, 0, 0};
      if ((f16 & 0x3fu) == 0x2f) {                 // CVTTQ: /C chop is valid
        if (((f16 >> 6) & 3u) == 1) break;
        if (((f16 >> 8) & 7u) == 7) break;
        if (((f16 & 0x600u) == 0x200u) || ((f16 & 0x500u) == 0x400u)) break;
        return {A64OpKind::kFltiCvt, A64TerminatorKind::kNone, 0, 0};
      }
      if (((f16 & 0x600u) == 0x200u) || ((f16 & 0x500u) == 0x400u)) break;
      const uint32_t rnd16 = (f16 >> 6) & 3u;
      if (rnd16 == 0 || rnd16 == 1) break;
      if (((f16 >> 8) & 7u) == 7) break;
      if (f16 == 0x0a4 || f16 == 0x5a4 || f16 == 0x0a5 || f16 == 0x5a5
       || f16 == 0x0a6 || f16 == 0x5a6 || f16 == 0x0a7 || f16 == 0x5a7)
        return {A64OpKind::kFltiCmp, A64TerminatorKind::kNone, 0, 0};
      const uint32_t base16 = f16 & 0x3fu;
      if (base16 <= 0x03 || (base16 >= 0x20 && base16 <= 0x23))
        return {A64OpKind::kFltiArith, A64TerminatorKind::kNone, 0, 0};
      if (base16 == 0x2c)                          // CVTTS
        return {A64OpKind::kFltiCvt, A64TerminatorKind::kNone, 0, 0};
      if ((base16 == 0x3e || base16 == 0x3c) && (f16 & 0x300u) != 0x100u)
        return {A64OpKind::kFltiCvt, A64TerminatorKind::kNone, 0, 0};   // CVTQT/CVTQS
      break;
    }
    case 0x31: case 0x32: case 0x33:   // FBEQ FBLT FBLE: FPSTART + f[Fa] vs 0.0
    case 0x35: case 0x36: case 0x37:   // FBNE FBGE FBGT
      return {A64OpKind::kBranchFp, A64TerminatorKind::kDirect, 0, 0};
    case 0x23: case 0x22: case 0x27: case 0x26:   // LDT LDS STT STS
    case 0x20: case 0x21: case 0x24: case 0x25:   // LDF LDG STF STG (VAX)
      return {A64OpKind::kFpMem, A64TerminatorKind::kNone, 0, 0};
    case 0x13:   // INTM: MULQ/MULL/UMULH; MULL/V and MULQ/V overflow-trap -> interpret
      if (func == 0x20 || func == 0x00 || func == 0x30)
        return {A64OpKind::kIntm, A64TerminatorKind::kNone,
                operate_reads, static_cast<uint8_t>(kA64GprRc)};
      break;
    case 0x11:   // INTL: fully covered (logicals, CMOVs, AMASK/IMPLVER)
      switch (func) {
        case 0x00: case 0x20: case 0x40:   // AND, BIS, XOR
        case 0x08: case 0x28: case 0x48:   // BIC, ORNOT, EQV
          return {A64OpKind::kIntlLogical, A64TerminatorKind::kNone,
                  operate_reads, static_cast<uint8_t>(kA64GprRc)};
        case 0x14: case 0x16: case 0x24: case 0x26:   // CMOVLBS/LBC/EQ/NE
        case 0x44: case 0x46: case 0x64: case 0x66:   // CMOVLT/GE/LE/GT
          return {A64OpKind::kIntlCmov, A64TerminatorKind::kNone,
                  static_cast<uint8_t>(operate_reads | kA64GprRc),   // keeps current Rc
                  static_cast<uint8_t>(kA64GprRc)};
        case 0x61:   // AMASK: only the architecturally valid Ra==31 form compiles
          if (((ins >> 21) & 0x1fu) != 31) break;
          return {A64OpKind::kIntlProbe, A64TerminatorKind::kNone,
                  static_cast<uint8_t>(is_literal ? 0 : kA64GprRb),
                  static_cast<uint8_t>(kA64GprRc)};
        case 0x6c:   // IMPLVER: Rc = the implementation constant
          return {A64OpKind::kIntlProbe, A64TerminatorKind::kNone,
                  0, static_cast<uint8_t>(kA64GprRc)};
      }
      break;
    case 0x12:   // INTS: (shifts, ZAP, EXT/INS/MSK byte-manip)
      switch (func) {
        case 0x39: case 0x34: case 0x3c:   // SLL, SRL, SRA
          return {A64OpKind::kIntsShift, A64TerminatorKind::kNone,
                  operate_reads, static_cast<uint8_t>(kA64GprRc)};
        case 0x30: case 0x31:              // ZAP, ZAPNOT
          return {A64OpKind::kIntsZap, A64TerminatorKind::kNone,
                  operate_reads, static_cast<uint8_t>(kA64GprRc)};
        case 0x06: case 0x16: case 0x26: case 0x36:   // EXTBL/WL/LL/QL
        case 0x5a: case 0x6a: case 0x7a:              // EXTWH/LH/QH
        case 0x0b: case 0x1b: case 0x2b: case 0x3b:   // INSBL/WL/LL/QL
        case 0x57: case 0x67: case 0x77:              // INSWH/LH/QH
        case 0x02: case 0x12: case 0x22: case 0x32:   // MSKBL/WL/LL/QL
        case 0x52: case 0x62: case 0x72:              // MSKWH/LH/QH
          return {A64OpKind::kIntsByte, A64TerminatorKind::kNone,
                  operate_reads, static_cast<uint8_t>(kA64GprRc)};
      }
      break;
    case 0x28: case 0x29:   // LDL, LDQ: memory-format integer loads
    case 0x0a: case 0x0c:   // LDBU, LDWU (BWX zero-extending)
    case 0x0b:              // LDQ_U (VA forced 8-aligned)
      return {A64OpKind::kMemLoad, A64TerminatorKind::kNone,
              static_cast<uint8_t>(kA64GprRb), static_cast<uint8_t>(kA64GprRa)};
    case 0x2c: case 0x2d:   // STL, STQ: memory-format integer stores
    case 0x0e: case 0x0d:   // STB, STW
    case 0x0f:              // STQ_U
      return {A64OpKind::kMemStore, A64TerminatorKind::kNone,
              static_cast<uint8_t>(kA64GprRa | kA64GprRb), 0};
    case 0x2a: case 0x2b:   // LDL_L / LDQ_L
      if (((ins >> 21) & 0x1fu) == 31) break;
      return {A64OpKind::kLoadLocked, A64TerminatorKind::kNone,
              static_cast<uint8_t>(kA64GprRb), static_cast<uint8_t>(kA64GprRa)};
    case 0x2e: case 0x2f:   // STL_C / STQ_C
      if (((ins >> 21) & 0x1fu) == 31) break;
      return {A64OpKind::kStoreCond, A64TerminatorKind::kNone,
              static_cast<uint8_t>(kA64GprRa | kA64GprRb),
              static_cast<uint8_t>(kA64GprRa)};
    case 0x08:   // LDA:  Ra = Rb + sext(disp16) - all ALU, all the time
    case 0x09:   // LDAH: Ra = Rb + (sext(disp16) << 16)
      return {A64OpKind::kLoadAddress, A64TerminatorKind::kNone,
              static_cast<uint8_t>(kA64GprRb), static_cast<uint8_t>(kA64GprRa)};
    case 0x18:   // MISC
      switch (ins & 0xffffu) {
        case 0x0000: case 0x0400:            // TRAPB, EXCB
        case 0x4000: case 0x4400:            // MB, WMB -> dmb ish
        case 0x8000: case 0xA000: case 0xE800:   // FETCH, FETCH_M, ECB
        case 0xF800: case 0xFC00:            // WH64, WH64EN -> no code
          return {A64OpKind::kMisc, A64TerminatorKind::kNone, 0, 0};
        case 0xC000:                         // RPCC
          if (((ins >> 21) & 0x1fu) == 31) break;
          return {A64OpKind::kMisc, A64TerminatorKind::kNone, 0,
                  static_cast<uint8_t>(kA64GprRa)};
        case 0xE000: case 0xF000:            // RC, RS
          return {A64OpKind::kMisc, A64TerminatorKind::kNone, 0,
                  static_cast<uint8_t>(kA64GprRa)};
      }
      break;
    case 0x1c:   // FPTI.
      if (func == 0x00 || func == 0x01 || func == 0x30
          || func == 0x32 || func == 0x33)
        return {A64OpKind::kFptiInt, A64TerminatorKind::kNone,
                static_cast<uint8_t>(is_literal ? 0 : kA64GprRb),
                static_cast<uint8_t>(kA64GprRc)};
      if (func == 0x70 || func == 0x78) {   // FTOIT / FTOIS: Rb==31, Rc!=31 required
        if (((ins >> 16) & 0x1fu) != 31 || (ins & 0x1fu) == 31) break;
        return {A64OpKind::kFtoi, A64TerminatorKind::kNone, 0,
                static_cast<uint8_t>(kA64GprRc)};
      }
      break;
    case 0x19: {   // HW_MFPR (PALmode only)
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        break;
      if (ipr_read.describe(ins).kind != AlphaIPRReadKind::Unsupported)
        return {A64OpKind::kHwMfpr, A64TerminatorKind::kNone, 0, 0};
      break;
    }
    case 0x1a:   // JMP/JSR/RET: computed jump, Ra = link -- indirect terminator
      return {A64OpKind::kJmpIndirect, A64TerminatorKind::kIndirect,
              static_cast<uint8_t>(kA64GprRb), static_cast<uint8_t>(kA64GprRa)};
    case 0x1e:   // HW_RET/HWREI (PALmode only): family-selected pure target transform, no link.
      // HW format: excluded from the pin-selection mask (regprof_mask parity).
      if (!pal_block || !pal.hardware_instruction_access
          || !alpha_pal_instruction_compilable(pal.hardware_instruction_access(ins)))
        break;
      if (!alpha_pal_return_compilable(alpha_pal_return_descriptor(pal, ins))) break;
      return {A64OpKind::kJmpIndirect, A64TerminatorKind::kIndirect, 0, 0};
    case 0x30: case 0x34:   // BR / BSR: Ra = link, unconditional direct terminator
      return {A64OpKind::kBranchInt, A64TerminatorKind::kDirect,
              0, static_cast<uint8_t>(kA64GprRa)};
    case 0x38: case 0x39: case 0x3a: case 0x3b:   // BLBC/BEQ/BLT/BLE: conditional on Ra
    case 0x3c: case 0x3d: case 0x3e: case 0x3f:   // BLBS/BNE/BGE/BGT
      return {A64OpKind::kBranchInt, A64TerminatorKind::kDirect,
              static_cast<uint8_t>(kA64GprRa), 0};
    // FP branches (0x31-0x33, 0x35-0x37) need the FPSTART gate.
  }
  return {};
}

static constexpr A64DecodedOp decode_a64_op(uint32_t ins, bool pal_block,
    const AlphaIPRReadPolicy& ipr_read = kEv68IPRReadPolicy,
    const AlphaIPRWritePolicy& ipr_write = kEv68IPRWritePolicy,
    const AlphaPALPolicy& pal = kEv68PALPolicy,
    const AlphaHWLoadPolicy& hw_load = kEv68HWLoadPolicy,
    const AlphaHWStorePolicy& hw_store = kEv68HWStorePolicy) noexcept
{
  const A64OpClass classification = classify_a64_op(
      ins, pal_block, ipr_read, ipr_write, pal, hw_load, hw_store);
  A64DecodedOp decoded{};
  decoded.ins = ins;
  decoded.kind = classification.kind;
  decoded.terminator = classification.terminator;
  decoded.gpr_reads = classification.gpr_reads;
  decoded.gpr_writes = classification.gpr_writes;
  decoded.opcode = static_cast<uint8_t>(ins >> 26);
  decoded.ra = static_cast<uint8_t>((ins >> 21) & 31u);
  decoded.rb = static_cast<uint8_t>((ins >> 16) & 31u);
  decoded.rc = static_cast<uint8_t>(ins & 31u);
  decoded.literal = static_cast<uint8_t>((ins >> 13) & 0xffu);
  decoded.is_literal = ((ins >> 12) & 1u) != 0;
  return decoded;
}

static constexpr A64ScanLimit a64_scan_limit(uint32_t n_instr, uint32_t source_words) noexcept
{
  A64ScanLimit limit{n_instr, A64PlanStop::kBlockEnd};
  if (limit.count > A64BlockPlan::kMaxOps) {
    limit.count = A64BlockPlan::kMaxOps;
    limit.stop = A64PlanStop::kInstructionLimit;
  }

  if (limit.count > source_words) {
    limit.count = source_words;
    limit.stop = A64PlanStop::kSourceBoundary;
  }
  return limit;
}

static A64BlockPlan plan_a64_block(const CJitEngine::JitBlock& block,
    const AlphaInstructionSourceView& source, const AlphaIPRReadPolicy& ipr_read,
    const AlphaIPRWritePolicy& ipr_write, const AlphaPALPolicy& pal,
    const AlphaHWLoadPolicy& hw_load, const AlphaHWStorePolicy& hw_store) noexcept
{
  A64BlockPlan plan{};
  if (block.n_instr == 0) {
    plan.stop = A64PlanStop::kEmptyBlock;
    return plan;
  }
  if (!source.contains(1) || source.identity != block.phys
      || source.asm_global != block.asm_global)
    return plan;

  const A64ScanLimit limit = a64_scan_limit(block.n_instr, source.word_count);
  plan.stop = limit.stop;
  std::array<uint32_t, A64BlockPlan::kMaxOps> source_words{};
  if (!source.copy_words(source_words.data(), limit.count)) return plan;
  const bool pal_block = (block.tag & 1u) != 0;
  for (uint32_t i = 0; i < limit.count; ++i) {
    const uint32_t word = source_words[i];
    const A64DecodedOp decoded = decode_a64_op(
        word, pal_block, ipr_read, ipr_write, pal, hw_load, hw_store);
    if (decoded.kind == A64OpKind::kUnsupported) {
      plan.breaker_word = word;
      plan.stop = A64PlanStop::kUnsupported;
      return plan;
    }

    plan.ops[plan.count++] = decoded;
    note_a64_op_gprs(plan.gpr_usage, decoded);
    if (decoded.terminator != A64TerminatorKind::kNone) {
      plan.terminator = decoded.terminator;
      plan.stop = A64PlanStop::kTerminator;
      return plan;
    }
  }
  return plan;
}

static A64PendingPublication prepare_a64_block_publication(
    const CJitEngine::JitBlock& block, const A64BlockPlan& plan,
    const A64BlockExit& exit, const A64BodyEmitReceipt& emission,
    const AlphaInstructionSource& sources, const asmjit::CodeHolder& code,
    const asmjit::Label& body, uint64_t prior_code_bytes) noexcept
{
  A64PendingPublication pending{};
  pending.code_size = static_cast<uint64_t>(code.code_size());
  pending.prior_code_bytes = prior_code_bytes;
  pending.plan_count = plan.count;
  pending.prefix_len = plan.count;
  // Hash extent = every word the compiled code depends on.
  pending.hash_len = plan.count;

  const bool body_bound = code.is_label_bound(body);
  pending.layout_complete = emission.complete_for(plan)
      && exit.valid()
      && exit.completed_delta == plan.count
      && !code.has_unresolved_fixups()
      && code.section_count() == 1
      && code.section_by_id(0)->has_offset()
      && code.section_by_id(0)->offset() == 0
      && body_bound
      && code.label_entry_of(body).section_id() == 0;
  if (body_bound) pending.body_off = code.label_offset(body);

  if (pending.prefix_len > plan.ops.size()
      || pending.hash_len != pending.prefix_len)
    return pending;

  std::array<uint32_t, A64BlockPlan::kMaxOps> current_words{};
  for (uint32_t i = 0; i < pending.prefix_len; ++i)
    current_words[i] = plan.ops[i].ins;
  if (!sources.matches(block.tag, block.phys, block.asm_global,
                       current_words.data(), pending.hash_len)) {
    pending.source_raced = true;
    return pending;
  }

  // Hash reads that prove snapshot current
  pending.source_hash = src_hash(
      reinterpret_cast<const uint8_t*>(current_words.data()), pending.hash_len);
  pending.source_current = true;
  return pending;
}

static constexpr uint32_t kA64DecodeProbe =
    (0x10u << 26) | (17u << 21) | (9u << 16) | 29u;
static constexpr uint32_t kA64LiteralProbe =
    (0x11u << 26) | (4u << 21) | (0xa5u << 13) | (1u << 12) | 7u;
static_assert(decode_a64_op(kA64DecodeProbe, false).opcode == 0x10
              && decode_a64_op(kA64DecodeProbe, false).ra == 17
              && decode_a64_op(kA64DecodeProbe, false).rb == 9
              && decode_a64_op(kA64DecodeProbe, false).rc == 29
              && !decode_a64_op(kA64DecodeProbe, false).is_literal,
              "A64 planning must decode Alpha register-form operands once");
static_assert(decode_a64_op(kA64LiteralProbe, false).is_literal
              && decode_a64_op(kA64LiteralProbe, false).literal == 0xa5,
              "A64 planning must decode Alpha literal operands once");
// BIS R31,R31,R16 
static_assert(decode_a64_op(0x47ff0410u, true).kind == A64OpKind::kIntlLogical
              && decode_a64_op(0x47ff0410u, true).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x47ff0410u, true).ra == 31
              && decode_a64_op(0x47ff0410u, true).rb == 31
              && decode_a64_op(0x47ff0410u, true).rc == 16
              && !decode_a64_op(0x47ff0410u, true).is_literal
              && a64_supported_op_kind(A64OpKind::kIntlLogical),
              "A64 INTL classification must accept the six register-form logicals");
static_assert(classify_a64_op((0x11u << 26) | (0x61u << 5), false).kind
                  == A64OpKind::kUnsupported   // AMASK with Ra!=31 OPCDECs -> interp
              && classify_a64_op((0x11u << 26) | (1u << 12) | (0x20u << 5), true).gpr_reads
                  == kA64GprRa                 // literal form reads Ra only
              && classify_a64_op((0x11u << 26) | (0x20u << 5), false).gpr_reads
                  == (kA64GprRa | kA64GprRb)
              && classify_a64_op((0x11u << 26) | (0x20u << 5), false).gpr_writes
                  == kA64GprRc,
              "A64 INTL classification must reject only the invalid forms");
// CMOVNE R0,#0,R3
static_assert(decode_a64_op(0x440014c3u, false).kind == A64OpKind::kIntlCmov
              && decode_a64_op(0x440014c3u, false).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x440014c3u, false).ra == 0
              && decode_a64_op(0x440014c3u, false).rc == 3
              && decode_a64_op(0x440014c3u, false).is_literal
              && decode_a64_op(0x440014c3u, false).literal == 0
              && decode_a64_op(0x440014c3u, false).gpr_reads == (kA64GprRa | kA64GprRc)
              && decode_a64_op(0x440014c3u, false).gpr_writes == kA64GprRc
              && classify_a64_op((0x11u << 26) | (31u << 21) | (0x61u << 5), false).kind
                  == A64OpKind::kIntlProbe     // AMASK Ra==31 compiles
              && classify_a64_op((0x11u << 26) | (0x6cu << 5), false).kind
                  == A64OpKind::kIntlProbe,    // IMPLVER compiles
              "A64 INTL CMOV/AMASK/IMPLVER classification must complete the opcode");
// EXTBL R1,#1,R1
static_assert(decode_a64_op(0x482030c1u, false).kind == A64OpKind::kIntsByte
              && decode_a64_op(0x482030c1u, false).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x482030c1u, false).ra == 1
              && decode_a64_op(0x482030c1u, false).rc == 1
              && decode_a64_op(0x482030c1u, false).is_literal
              && decode_a64_op(0x482030c1u, false).literal == 1
              && classify_a64_op((0x12u << 26) | (0x7au << 5), false).kind
                  == A64OpKind::kIntsByte      // EXTQH (H-form)
              && classify_a64_op((0x12u << 26) | (0x77u << 5), false).kind
                  == A64OpKind::kIntsByte      // INSQH
              && classify_a64_op((0x12u << 26) | (0x52u << 5), false).kind
                  == A64OpKind::kIntsByte      // MSKWH
              && classify_a64_op((0x12u << 26) | (0x00u << 5), false).kind
                  == A64OpKind::kUnsupported,  // func 0x00 is not an INTS op
              "A64 INTS byte-manip classification must cover EXT/INS/MSK L+H forms");
// SEXTB R19
static_assert(decode_a64_op(0x73f30013u, false).kind == A64OpKind::kFptiInt
              && decode_a64_op(0x73f30013u, false).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x73f30013u, false).rb == 19
              && decode_a64_op(0x73f30013u, false).rc == 19
              && !decode_a64_op(0x73f30013u, false).is_literal
              && decode_a64_op(0x73f30013u, false).gpr_reads == kA64GprRb
              && decode_a64_op(0x73f30013u, false).gpr_writes == kA64GprRc
              && classify_a64_op((0x1cu << 26) | (0x32u << 5), false).kind
                  == A64OpKind::kFptiInt       // CTLZ
              && classify_a64_op((0x1cu << 26) | (0x30u << 5), false).kind
                  == A64OpKind::kFptiInt       // CTPOP via NEON (asimd baseline)
              && classify_a64_op((0x1cu << 26) | (31u << 16) | (0x70u << 5) | 3u,
                                 false).kind == A64OpKind::kFtoi   // FTOIT, Rb==31
              && classify_a64_op((0x1cu << 26) | (0x70u << 5) | 3u, false).kind
                  == A64OpKind::kUnsupported   // FTOIT with Rb!=31 -> interp
              && classify_a64_op((0x1cu << 26) | (31u << 16) | (0x70u << 5) | 31u,
                                 false).kind == A64OpKind::kUnsupported,  // Rc==31 -> interp
              "A64 FPTI classification must cover the integer set and gate the FP moves");
static_assert(decode_a64_op(0xc3402e1eu, true).kind == A64OpKind::kBranchInt
              && decode_a64_op(0xc3402e1eu, true).terminator == A64TerminatorKind::kDirect
              && decode_a64_op(0xc3402e1eu, true).ra == 26
              && decode_a64_op(0xc3402e1eu, true).gpr_writes == kA64GprRa
              && decode_a64_op(0xc3402e1eu, true).gpr_reads == 0
              && plan_a64_direct_chain(
                     plan_a64_exit(0x8001, 2, A64TerminatorKind::kDirect),
                     0xc3402e1eu, true, false).gate == A64ChainGate::kNone
              && classify_a64_op(0x39u << 26, false).gpr_reads == kA64GprRa   // BEQ reads Ra
              && classify_a64_op(0x39u << 26, false).gpr_writes == 0
              && classify_a64_op(0x31u << 26, false).kind
                  == A64OpKind::kBranchFp,      // FP branches now compile (FPSTART gate)
              "A64 branch classification must link, read, and thin per the x64 contract");
// HW_MFPR R4, IPR 0x2b
static_assert(decode_a64_op(0x649f2b40u, true).kind == A64OpKind::kHwMfpr
              && decode_a64_op(0x649f2b40u, true).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x649f2b40u, true).ra == 4
              && decode_a64_op(0x649f2b40u, true).gpr_reads == 0    // HW formats excluded
              && decode_a64_op(0x649f2b40u, true).gpr_writes == 0   // from the pin mask
              && decode_a64_op(0x649f2b40u, false).kind
                  == A64OpKind::kUnsupported    // outside PALmode it OPCDECs -> interp
              && classify_a64_op((0x19u << 26) | (0x0du << 8), true).kind
                  == A64OpKind::kHwMfpr         // ISUM compiles (helper log/replays it)
              && classify_a64_op((0x19u << 26) | (0x00u << 8), true).kind
                  == A64OpKind::kUnsupported,   // unknown IPR index -> interp warn-once
              "A64 HW_MFPR classification must be PALmode-gated to jit_hw_mfpr's IPR set");
// SLL stuff
static_assert(decode_a64_op(0x48e0d727u, true).kind == A64OpKind::kIntsShift
              && decode_a64_op(0x48e0d727u, true).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x48e0d727u, true).ra == 7
              && decode_a64_op(0x48e0d727u, true).rc == 7
              && decode_a64_op(0x48e0d727u, true).is_literal
              && decode_a64_op(0x48e0d727u, true).literal == 6
              && decode_a64_op(0x48e0d727u, true).gpr_reads == kA64GprRa
              && decode_a64_op(0x48e0d727u, true).gpr_writes == kA64GprRc
              && classify_a64_op((0x12u << 26) | (0x34u << 5), false).gpr_reads
                  == (kA64GprRa | kA64GprRb)    // register-form SRL reads Ra and Rb
              && classify_a64_op((0x12u << 26) | (0x06u << 5), false).kind
                  == A64OpKind::kIntsByte,      // EXTBL now compiles (kIntsByte)
              "A64 INTS classification must accept only the three mod-64 shifts");
// LDAH R2,0(R2).
static_assert(decode_a64_op(0x24420000u, true).kind == A64OpKind::kLoadAddress
              && decode_a64_op(0x24420000u, true).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x24420000u, true).opcode == 0x09
              && decode_a64_op(0x24420000u, true).ra == 2
              && decode_a64_op(0x24420000u, true).rb == 2
              && decode_a64_op(0x24420000u, true).gpr_reads == kA64GprRb
              && decode_a64_op(0x24420000u, true).gpr_writes == kA64GprRa
              && classify_a64_op(0x08u << 26, false).kind == A64OpKind::kLoadAddress,
              "A64 LDA/LDAH classification must write Ra from the Rb base");
// ZAPNOT R13,#3,R13
static_assert(decode_a64_op(0x49a0762du, true).kind == A64OpKind::kIntsZap
              && decode_a64_op(0x49a0762du, true).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x49a0762du, true).ra == 13
              && decode_a64_op(0x49a0762du, true).rc == 13
              && decode_a64_op(0x49a0762du, true).is_literal
              && decode_a64_op(0x49a0762du, true).literal == 3
              && classify_a64_op((0x12u << 26) | (0x30u << 5), false).kind
                  == A64OpKind::kIntsZap        // register-form ZAP
              && classify_a64_op((0x12u << 26) | (0x30u << 5), false).gpr_reads
                  == (kA64GprRa | kA64GprRb)
              && classify_a64_op((0x12u << 26) | (0x06u << 5), false).kind
                  == A64OpKind::kIntsByte,      // EXTBL now compiles (kIntsByte)
              "A64 ZAP classification must cover both selector forms");
// STB R4,0(R5)
static_assert(decode_a64_op(0x38850000u, true).kind == A64OpKind::kMemStore
              && decode_a64_op(0x38850000u, true).terminator == A64TerminatorKind::kNone
              && decode_a64_op(0x38850000u, true).ra == 4
              && decode_a64_op(0x38850000u, true).rb == 5
              && decode_a64_op(0x38850000u, true).gpr_reads == (kA64GprRa | kA64GprRb)
              && decode_a64_op(0x38850000u, true).gpr_writes == 0
              && classify_a64_op(0x29u << 26, false).kind == A64OpKind::kMemLoad
              && classify_a64_op(0x29u << 26, false).gpr_reads == kA64GprRb
              && classify_a64_op(0x29u << 26, false).gpr_writes == kA64GprRa
              && classify_a64_op(0x2au << 26, false).kind
                  == A64OpKind::kLoadLocked     // LDL_L now compiles (helper monitor)
              && classify_a64_op(0x2eu << 26, false).kind
                  == A64OpKind::kStoreCond,     // STL_C now compiles
              "A64 memory classification must cover the ten plain int loads/stores");
// STL_C R16,0(R22)
static_assert(decode_a64_op(0xba160000u, false).kind == A64OpKind::kStoreCond
              && decode_a64_op(0xba160000u, false).ra == 16
              && decode_a64_op(0xba160000u, false).rb == 22
              && decode_a64_op(0xba160000u, false).gpr_reads == (kA64GprRa | kA64GprRb)
              && decode_a64_op(0xba160000u, false).gpr_writes == kA64GprRa
              && classify_a64_op(0x2bu << 26, false).kind == A64OpKind::kLoadLocked
              && classify_a64_op((0x2au << 26) | (31u << 21), false).kind
                  == A64OpKind::kUnsupported    // LDx_L Ra==31: lock-only -> interp
              && classify_a64_op((0x2eu << 26) | (31u << 21), false).kind
                  == A64OpKind::kUnsupported,   // STx_C Ra==31: result-discard -> interp
              "A64 LL/SC classification must compile only the value-returning forms");
// INTA
static_assert(classify_a64_op((0x10u << 26) | (0x20u << 5), false).kind
                  == A64OpKind::kInta          // ADDQ
              && classify_a64_op((0x10u << 26) | (0x20u << 5), false).gpr_reads
                  == (kA64GprRa | kA64GprRb)
              && classify_a64_op((0x10u << 26) | (0x20u << 5), false).gpr_writes
                  == kA64GprRc
              && classify_a64_op((0x10u << 26) | (0x1du << 5), false).kind
                  == A64OpKind::kInta          // CMPULT
              && classify_a64_op((0x10u << 26) | (0x0fu << 5), false).kind
                  == A64OpKind::kInta          // CMPBGE
              && classify_a64_op((0x10u << 26) | (0x12u << 5), false).kind
                  == A64OpKind::kInta          // S8ADDL
              && classify_a64_op((0x10u << 26) | (0x60u << 5), false).kind
                  == A64OpKind::kUnsupported   // ADDQ/V overflow-traps -> interp
              && classify_a64_op((0x10u << 26) | (0x49u << 5), false).kind
                  == A64OpKind::kUnsupported,  // SUBL/V -> interp
              "A64 INTA classification must cover the non-trapping set only");
static_assert(classify_a64_op((0x18u << 26) | 0x4000u, false).kind
                  == A64OpKind::kMisc          // MB -> dmb ish
              && classify_a64_op((0x18u << 26) | 0xF800u, false).kind
                  == A64OpKind::kMisc          // WH64 hint -> no code
              && classify_a64_op((0x18u << 26) | (3u << 21) | 0xC000u, false).kind
                  == A64OpKind::kMisc          // RPCC R3
              && classify_a64_op((0x18u << 26) | (31u << 21) | 0xC000u, false).kind
                  == A64OpKind::kUnsupported   // RPCC R31: pure no-op read -> interp
              && classify_a64_op((0x18u << 26) | (31u << 21) | 0xE000u, false).kind
                  == A64OpKind::kMisc          // RC R31 compiles (flag side effect)
              && classify_a64_op((0x18u << 26) | 0x1234u, false).kind
                  == A64OpKind::kUnsupported,  // unknown MISC form stays closed
              "A64 MISC classification must split barriers, hints, and state reads");
//CALL_PAL / HWLD / HWMTPR / HWST).
static_assert(classify_a64_op(0x86u, false).kind == A64OpKind::kCallPal
              && classify_a64_op(0x86u, false).terminator == A64TerminatorKind::kCallPal
              && classify_a64_op(0x3Eu, false).kind
                  == A64OpKind::kUnsupported   // WTINT: native idle -> interp
              && classify_a64_op(0x123456u, false).kind
                  == A64OpKind::kUnsupported   // SRM-special range -> interp
              && classify_a64_op((0x1bu << 26) | (1u << 12), true).kind
                  == A64OpKind::kHwLd          // physical quad (the 1.5M form)
              && classify_a64_op((0x1bu << 26) | (5u << 12), true).kind
                  == A64OpKind::kHwLd          // virtual quad
              && classify_a64_op((0x1bu << 26) | (2u << 12), true).kind
                  == A64OpKind::kUnsupported   // physical locked -> interp
              && classify_a64_op((0x1bu << 26) | (1u << 12), false).kind
                  == A64OpKind::kUnsupported   // HW_LD outside PALmode -> interp
              && classify_a64_op((0x1du << 26) | (0x11u << 8), true).terminator
                  == A64TerminatorKind::kRedispatch   // I_CTL redispatches
              && classify_a64_op((0x1du << 26) | (0x27u << 8), true).kind
                  == A64OpKind::kHwMtpr        // MM_STAT no-op compiles (emits nothing)
              && classify_a64_op((0x1du << 26) | (0x41u << 8), true).kind
                  == A64OpKind::kUnsupported   // 0x40-group bit 0 = ASN write -> interp
              && classify_a64_op((0x1fu << 26) | (1u << 12), true).kind
                  == A64OpKind::kHwSt
              && classify_a64_op((0x1fu << 26) | (4u << 12), true).kind
                  == A64OpKind::kUnsupported,  // HW_ST virtual -> interp
              "A64 PAL-op classification must mirror the x64 gate lists exactly");
// INTM, LDT F0,0x220(R3)
static_assert(classify_a64_op((0x13u << 26) | (0x20u << 5), false).kind
                  == A64OpKind::kIntm          // MULQ
              && classify_a64_op((0x13u << 26) | (0x30u << 5), false).kind
                  == A64OpKind::kIntm          // UMULH
              && classify_a64_op((0x13u << 26) | (0x60u << 5), false).kind
                  == A64OpKind::kUnsupported   // MULQ/V overflow-traps -> interp
              && decode_a64_op(0x8c030220u, false).kind == A64OpKind::kFpMem
              && decode_a64_op(0x8c030220u, false).ra == 0      // F0
              && decode_a64_op(0x8c030220u, false).rb == 3
              && classify_a64_op(0x26u << 26, false).kind == A64OpKind::kFpMem   // STS
              && classify_a64_op(0x21u << 26, false).kind == A64OpKind::kFpMem,  // LDG
              "A64 INTM and FP-memory classification must cover the x64 sets");
// FLTL/FLTV/ITOF/FP-branch helpers
static_assert(classify_a64_op((0x17u << 26) | (0x020u << 5) | 5u, false).kind
                  == A64OpKind::kFltl          // CPYS to f5
              && classify_a64_op((0x17u << 26) | (0x020u << 5) | 31u, false).kind
                  == A64OpKind::kUnsupported   // Fc==31 -> interp (f31 zeroing)
              && classify_a64_op((0x15u << 26) | (0x020u << 5) | 4u, false).kind
                  == A64OpKind::kFltv          // ADDG
              && classify_a64_op((0x15u << 26) | (0x220u << 5) | 4u, false).kind
                  == A64OpKind::kUnsupported   // invalid VAX qualifier -> interp
              && classify_a64_op((0x14u << 26) | (31u << 16) | (0x024u << 5) | 7u,
                                 false).kind == A64OpKind::kItof   // ITOFT
              && classify_a64_op((0x14u << 26) | (0x024u << 5) | 7u, false).kind
                  == A64OpKind::kUnsupported   // ITOFx needs Rb==31
              && classify_a64_op((0x35u << 26) | (0x1fffffu), false).kind
                  == A64OpKind::kBranchFp      // FBNE backward
              && classify_a64_op((0x35u << 26) | (0x1fffffu), false).terminator
                  == A64TerminatorKind::kDirect
              && classify_a64_op((0x16u << 26) | (0x0beu << 5) | 1u, false).kind
                  == A64OpKind::kFltiCvt,      // CVTQT (the punch) now compiles
              "A64 FP helper families must mirror the x64 gates");
// FLTI arithmetic 
static_assert(classify_a64_op((0x16u << 26) | (0x0a0u << 5) | 2u, false).kind
                  == A64OpKind::kFltiArith     // ADDT/N
              && classify_a64_op((0x16u << 26) | (0x080u << 5) | 2u, false).kind
                  == A64OpKind::kFltiArith     // ADDS/N (0x080: base 0, rnd 2)
              && classify_a64_op((0x16u << 26) | (0x020u << 5) | 2u, false).kind
                  == A64OpKind::kUnsupported   // ADDT/C: chopped rounding -> interp
              && classify_a64_op((0x16u << 26) | (0x0a5u << 5) | 2u, false).kind
                  == A64OpKind::kFltiCmp       // CMPTEQ
              && classify_a64_op((0x16u << 26) | (0x7acu << 5) | 2u, false).kind
                  == A64OpKind::kUnsupported   // CVTTS/SUI -> interp
              && classify_a64_op((0x16u << 26) | (0x02fu << 5) | 2u, false).kind
                  == A64OpKind::kFltiCvt       // CVTTQ/C: chop is valid here
              && classify_a64_op((0x14u << 26) | (31u << 21) | (0x0abu << 5) | 3u, false).kind
                  == A64OpKind::kFsqrt         // SQRTT/N
              && classify_a64_op((0x14u << 26) | (31u << 21) | (0x02bu << 5) | 3u, false).kind
                  == A64OpKind::kUnsupported   // SQRTT/C -> interp
              && classify_a64_op((0x14u << 26) | (0x0abu << 5) | 3u, false).kind
                  == A64OpKind::kUnsupported   // SQRTT with Ra != R31 -> OPCDEC (interp)
              && classify_a64_op((0x14u << 26) | (31u << 21) | (0x2abu << 5) | 3u, false).kind
                  == A64OpKind::kUnsupported,  // SQRTT invalid trap qualifier -> OPCDEC (interp)
              "A64 FLTI/SQRT classification must gate every rounding and trap edge");
// HW_RET (R23) 
static_assert(decode_a64_op(0x7bf7a000u, true).kind == A64OpKind::kJmpIndirect
              && decode_a64_op(0x7bf7a000u, true).terminator == A64TerminatorKind::kIndirect
              && decode_a64_op(0x7bf7a000u, true).rb == 23
              && decode_a64_op(0x7bf7a000u, true).gpr_reads == 0    // HW format:
              && decode_a64_op(0x7bf7a000u, true).gpr_writes == 0   // no pin-mask roles
              && decode_a64_op(0x7bf7a000u, false).kind
                  == A64OpKind::kUnsupported    // HW_RET outside PALmode -> interp
              && classify_a64_op(0x1au << 26, false).kind == A64OpKind::kJmpIndirect
              && classify_a64_op(0x1au << 26, false).terminator
                  == A64TerminatorKind::kIndirect
              && classify_a64_op(0x1au << 26, false).gpr_reads == kA64GprRb
              && classify_a64_op(0x1au << 26, false).gpr_writes == kA64GprRa,
              "A64 computed-jump classification must gate HW_RET to PALmode");
static_assert(a64_scan_limit(3, 3).count == 3
              && a64_scan_limit(3, 3).stop == A64PlanStop::kBlockEnd
              && a64_scan_limit(65, 65).count == 64
              && a64_scan_limit(65, 65).stop == A64PlanStop::kInstructionLimit
              && a64_scan_limit(3, 2).count == 2
              && a64_scan_limit(3, 2).stop == A64PlanStop::kSourceBoundary
              && a64_scan_limit(1, 0).count == 0,
              "A64 planning must honor block, instruction, and family source bounds");

static constexpr A64OpEmitReceipt a64_completed_op_receipt(
    const A64DecodedOp& op,
    asmjit::Error error = asmjit::Error::kOk) noexcept
{
  if (error != asmjit::Error::kOk)
    return {error, op.kind, A64EmitState::kUnhandled,
            A64Mainline::kNone, A64TerminatorKind::kNone};
  return {asmjit::Error::kOk, op.kind, A64EmitState::kComplete,
          op.terminator == A64TerminatorKind::kNone
              ? A64Mainline::kContinues : A64Mainline::kExitReady,
          op.terminator};
}

static constexpr bool a64_body_emission_contract_probe() noexcept
{
  A64BlockPlan linear{};
  linear.count = 3;
  linear.stop = A64PlanStop::kBlockEnd;
  for (uint32_t i = 0; i < linear.count; ++i) {
    linear.ops[i].ins = i + 1;
    linear.ops[i].kind = A64OpKind::kValidationProbe;
  }

  A64BlockPlan prefix = linear;
  prefix.count = 2;
  prefix.stop = A64PlanStop::kUnsupported;

  A64BlockPlan terminated = linear;
  terminated.stop = A64PlanStop::kTerminator;
  terminated.terminator = A64TerminatorKind::kDirect;
  terminated.ops[2].terminator = A64TerminatorKind::kDirect;

  A64BlockPlan unsupported = linear;
  unsupported.ops[1].kind = A64OpKind::kUnsupported;
  A64BlockPlan early_terminator = linear;
  early_terminator.ops[1].terminator = A64TerminatorKind::kDirect;
  A64BlockPlan missing_terminator = terminated;
  missing_terminator.ops[2].terminator = A64TerminatorKind::kNone;
  A64BlockPlan wrong_terminator = terminated;
  wrong_terminator.ops[2].terminator = A64TerminatorKind::kIndirect;
  A64BlockPlan wrong_stop = terminated;
  wrong_stop.stop = A64PlanStop::kBlockEnd;
  A64BlockPlan oversized = linear;
  oversized.count = A64BlockPlan::kMaxOps + 1;
  A64BlockPlan unknown_kind = linear;
  unknown_kind.ops[0].kind = static_cast<A64OpKind>(0x7f);

  if (a64_plan_ready_for_emission(linear)
      || !a64_plan_ready_for_validation(linear)
      || !a64_plan_ready_for_validation(prefix)
      || !a64_plan_ready_for_validation(terminated)
      || a64_plan_ready_for_emission(A64BlockPlan{})
      || a64_plan_ready_for_validation(unsupported)
      || a64_plan_ready_for_validation(early_terminator)
      || a64_plan_ready_for_validation(missing_terminator)
      || a64_plan_ready_for_validation(wrong_terminator)
      || a64_plan_ready_for_validation(wrong_stop)
      || a64_plan_ready_for_validation(oversized)
      || a64_plan_ready_for_emission(unknown_kind)
      || a64_plan_ready_for_validation(unknown_kind))
    return false;

  A64BodyEmitReceipt complete = a64_begin_body_validation(linear);
  for (uint32_t i = 0; i < linear.count; ++i) {
    if (!a64_begin_op_emission(complete, linear, i)
        || !a64_accept_op_emission(
            complete, linear, i, a64_completed_op_receipt(linear.ops[i])))
      return false;
  }
  if (!a64_finish_body_emission(complete, linear)
      || !complete.complete_for_validation(linear)
      || complete.complete_for(linear))
    return false;

  A64BodyEmitReceipt terminal = a64_begin_body_validation(terminated);
  for (uint32_t i = 0; i < terminated.count; ++i) {
    if (!a64_begin_op_emission(terminal, terminated, i)
        || !a64_accept_op_emission(
            terminal, terminated, i,
            a64_completed_op_receipt(terminated.ops[i])))
      return false;
  }
  if (!a64_finish_body_emission(terminal, terminated)
      || terminal.observed_terminator != A64TerminatorKind::kDirect
      || !terminal.complete_for_validation(terminated))
    return false;

  A64BodyEmitReceipt duplicate = a64_begin_body_validation(linear);
  if (!a64_begin_op_emission(duplicate, linear, 0)
      || a64_begin_op_emission(duplicate, linear, 0)
      || duplicate.error != asmjit::Error::kInvalidState)
    return false;

  A64BodyEmitReceipt partial = a64_begin_body_validation(linear);
  if (!a64_begin_op_emission(partial, linear, 0)
      || !a64_accept_op_emission(
          partial, linear, 0, a64_completed_op_receipt(linear.ops[0]))
      || a64_finish_body_emission(partial, linear)
      || partial.complete || partial.emitted_count != 1)
    return false;

  A64BodyEmitReceipt rejected = a64_begin_body_validation(linear);
  if (!a64_begin_op_emission(rejected, linear, 0)
      || a64_accept_op_emission(rejected, linear, 0, {})
      || rejected.error != asmjit::Error::kInvalidInstruction
      || rejected.emitted_count != 0)
    return false;

  A64BodyEmitReceipt mismatch = a64_begin_body_validation(terminated);
  A64OpEmitReceipt wrong_mainline =
      a64_completed_op_receipt(terminated.ops[0]);
  wrong_mainline.mainline = A64Mainline::kExitReady;
  if (!a64_begin_op_emission(mismatch, terminated, 0)
      || a64_accept_op_emission(mismatch, terminated, 0, wrong_mainline)
      || mismatch.error != asmjit::Error::kInvalidState)
    return false;

  return true;
}

static_assert(a64_body_emission_contract_probe(),
              "A64 body emission must be exact, ordered, terminator-aware, and fail-closed");
static_assert(A64PendingPublication{64, 16, 1, 0, 3, 3, 3, true, true}.ready()
              && A64PendingPublication{64, 16, 1, UINT64_MAX - 64,
                                       3, 3, 3, true, true}.ready()
              && !A64PendingPublication{}.ready()
              && !A64PendingPublication{64, 16, 1, 0, 3, 0, 3, true, true}.ready()
              && !A64PendingPublication{64, 16, 1, 0, 3, 2, 3, true, true}.ready()
              && !A64PendingPublication{64, 16, 1, 0, 65, 65, 65, true, true}.ready()
              && !A64PendingPublication{64, 16, 1, 0, 3, 3, 2, true, true}.ready()
              && !A64PendingPublication{64, 16, 1, 0, 3, 3, 4, true, true}.ready()
              && !A64PendingPublication{0, 0, 1, 0, 3, 3, 3, true, true}.ready()
              && !A64PendingPublication{62, 16, 1, 0, 3, 3, 3, true, true}.ready()
              && !A64PendingPublication{64, 64, 1, 0, 3, 3, 3, true, true}.ready()
              && !A64PendingPublication{64, 18, 1, 0, 3, 3, 3, true, true}.ready()
              && !A64PendingPublication{uint64_t(UINT32_MAX) + 1, 16, 1, 0,
                                         3, 3, 3, true, true}.ready()
              && !A64PendingPublication{64, 16, 1, UINT64_MAX - 63,
                                         3, 3, 3, true, true}.ready()
              && !A64PendingPublication{64, 16, 1, 0, 3, 3, 3, false, true}.ready()
              && !A64PendingPublication{64, 16, 1, 0, 3, 3, 3, true, false}.ready(),
              "A64 publication must be complete, aligned, bounded, and transactionally safe");

static constexpr bool a64_exit_contract_probe() noexcept
{
  const A64BlockExit none = plan_a64_exit(0x1001, 0, A64TerminatorKind::kNone);
  const A64BlockExit fall = plan_a64_exit(0x1001, 3, A64TerminatorKind::kNone);
  const A64BlockExit direct = plan_a64_exit(0x1001, 2, A64TerminatorKind::kDirect);
  const A64BlockExit indirect = plan_a64_exit(0x1001, 2, A64TerminatorKind::kIndirect);
  const A64BlockExit call_pal = plan_a64_exit(0x1000, 1, A64TerminatorKind::kCallPal);
  const A64BlockExit redispatch =
      plan_a64_exit(0x1001, 1, A64TerminatorKind::kRedispatch);
  const A64OpExit retry = a64_retry_exit(0x1001, 63);
  const A64OpExit trapped = a64_completed_trap_exit(63);

  return !none.valid() && none.completed_delta == 0 && none.fallthrough_pc == 0x1001
      && fall.kind == A64ExitKind::kFallthrough
      && fall.completed_delta == 3 && fall.fallthrough_pc == 0x100d
      && direct.kind == A64ExitKind::kDirect
      && indirect.kind == A64ExitKind::kIndirect
      && call_pal.kind == A64ExitKind::kCallPal
      && redispatch.kind == A64ExitKind::kRedispatch
      && redispatch.completed_delta == 1
      && redispatch.fallthrough_pc == 0x1005
      && retry.completed_delta == 63 && retry.next_pc == 0x10fd
      && retry.pc_authority == A64PcAuthority::kNextPc
      && trapped.completed_delta == 64
      && trapped.pc_authority == A64PcAuthority::kCpuState
      && !a64_publish_pc_before_chain(fall, false)
      && a64_publish_pc_before_chain(fall, true)
      && a64_publish_pc_before_chain(call_pal, false)
      && a64_publish_pc_before_chain(redispatch, false);
}

static_assert(a64_advance_pc(~uint64_t(0) - 2, 1) == 1
              && a64_branch_displacement(0x000fffffu) == 1048575
              && a64_branch_displacement(0x00100000u) == -1048576
              && a64_branch_displacement(0x001fffffu) == -1
              && a64_branch_target(0x1005, 0x001fffffu) == 0x1001
              && a64_jmp_target(0x1001, 0x2007) == 0x2005
              && a64_hw_ret_target(0x2007) == 0x2005,
              "A64 exit planning must preserve Alpha PC arithmetic and target masks");
static_assert(a64_exit_contract_probe(),
              "A64 exits must distinguish normal, retry, trap, and terminator bookkeeping");

static constexpr bool a64_gpr_usage_probe() noexcept
{
  A64GprUsage usage{};
  A64DecodedOp register_form{};
  register_form.ra = 1;
  register_form.rb = 1;
  register_form.rc = 2;
  register_form.gpr_reads = kA64GprRa | kA64GprRb;
  register_form.gpr_writes = kA64GprRc;
  note_a64_op_gprs(usage, register_form);

  // Operand roles, not bit 12 by itself, decide whether Rb is a register.
  A64DecodedOp literal_form{};
  literal_form.ra = 2;
  literal_form.rb = 16;
  literal_form.rc = 31;
  literal_form.is_literal = true;
  literal_form.gpr_reads = kA64GprRa;
  literal_form.gpr_writes = kA64GprRc;
  note_a64_op_gprs(usage, literal_form);

  return usage.refs[1] == 2 && usage.refs[2] == 2
      && usage.refs[16] == 0 && usage.refs[31] == 0
      && usage.reads == ((1u << 1) | (1u << 2))
      && usage.writes == (1u << 2);
}

static_assert(a64_gpr_usage_probe(),
              "A64 GPR analysis must follow semantic roles and exclude architectural R31");

static constexpr uint32_t a64_guest_gpr_slot(uint32_t raw_reg, bool pal_shadow) noexcept
{
  const uint32_t reg = raw_reg & 31u;
  return reg + ((pal_shadow && CJitEngine::RegAlloc::guest_is_shadowable(reg)) ? 32u : 0u);
}

enum class A64GprRouteKind : uint8_t {
  kInvalid,
  kMemory,
  kPinned,
  kZero,
  kDiscard
};

struct A64GprRoute {
  A64GprRouteKind kind;
  uint8_t slot;
  int8_t host;
};

// A route selects operand storage only. In particular, kDiscard suppresses the
// R31 destination write, never the instruction's faults or other side effects.

static constexpr A64GprRoute a64_guest_gpr_read_route(
    const CJitEngine::RegAlloc& regs, uint32_t raw_reg, bool pal_shadow) noexcept
{
  if (raw_reg >= 32) return {A64GprRouteKind::kInvalid, 0, -1};
  const uint32_t reg = raw_reg;
  if (reg == 31) return {A64GprRouteKind::kZero, 31, -1};
  const uint8_t slot = static_cast<uint8_t>(a64_guest_gpr_slot(reg, pal_shadow));
  const int host = regs.host_of(reg, pal_shadow);
  return host < 0 ? A64GprRoute{A64GprRouteKind::kMemory, slot, -1}
                  : A64GprRoute{A64GprRouteKind::kPinned, slot,
                                static_cast<int8_t>(host)};
}

static constexpr A64GprRoute a64_guest_gpr_write_route(
    const CJitEngine::RegAlloc& regs, uint32_t raw_reg, bool pal_shadow) noexcept
{
  if (raw_reg >= 32) return {A64GprRouteKind::kInvalid, 0, -1};
  const uint32_t reg = raw_reg;
  if (reg == 31) return {A64GprRouteKind::kDiscard, 31, -1};
  const uint8_t slot = static_cast<uint8_t>(a64_guest_gpr_slot(reg, pal_shadow));
  const int host = regs.host_of(reg, pal_shadow);
  return host < 0 ? A64GprRoute{A64GprRouteKind::kMemory, slot, -1}
                  : A64GprRoute{A64GprRouteKind::kPinned, slot,
                                static_cast<int8_t>(host)};
}

static constexpr bool a64_gpr_route_probe() noexcept
{
  CJitEngine::RegAlloc regs{};
  if (regs.host_of(1, false) != -1
      || !regs.bind_guest(1)
      || !regs.bind_guest(16)
      || regs.bind_guest(1)
      || regs.bind_guest(22)
      || regs.bind_guest(31)
      || !regs.bind_guest(0)
      || !regs.bind_guest(2)
      || !regs.bind_guest(3)
      || !regs.bind_guest(30)
      || regs.bind_guest(27)) return false;

  const A64GprRoute r1 = a64_guest_gpr_read_route(regs, 1, false);
  const A64GprRoute r16 = a64_guest_gpr_read_route(regs, 16, false);
  const A64GprRoute p22 = a64_guest_gpr_read_route(regs, 22, true);
  const A64GprRoute r24 = a64_guest_gpr_read_route(regs, 24, true);
  const A64GprRoute r31 = a64_guest_gpr_read_route(regs, 31, false);
  const A64GprRoute w31 = a64_guest_gpr_write_route(regs, 31, false);
  const A64GprRoute bad = a64_guest_gpr_read_route(regs, 32, false);
  return regs.guest_of_pin(0) == 1 && regs.guest_of_pin(1) == 16
      && r1.kind == A64GprRouteKind::kPinned
      && r1.host == CJitEngine::RegAlloc::kGuestPin0.id()
      && r16.kind == A64GprRouteKind::kPinned
      && r16.host == CJitEngine::RegAlloc::kGuestPin1.id()
      && p22.kind == A64GprRouteKind::kMemory && p22.slot == 54
      && r24.kind == A64GprRouteKind::kMemory && r24.slot == 24
      && r31.kind == A64GprRouteKind::kZero
      && w31.kind == A64GprRouteKind::kDiscard
      && bad.kind == A64GprRouteKind::kInvalid
      && CJitEngine::RegAlloc::guest_pin_candidate(3)
      && !CJitEngine::RegAlloc::guest_pin_candidate(4)
      && !CJitEngine::RegAlloc::guest_pin_candidate(20)
      && CJitEngine::RegAlloc::guest_pin_candidate(24)
      && !CJitEngine::RegAlloc::guest_pin_candidate(31);
}

static_assert(a64_gpr_route_probe(),
              "A64 GPR routing must preserve pin, PAL-shadow, and R31 semantics");

static constexpr asmjit::a64::Mem a64_guest_gpr_mem(uint32_t raw_reg,
                                                     bool pal_shadow) noexcept
{
  return asmjit::a64::ptr(CJitEngine::RegAlloc::kRegs,
      static_cast<int32_t>(a64_guest_gpr_slot(raw_reg, pal_shadow) * sizeof(uint64_t)));
}

static_assert(a64_guest_gpr_slot(3, true) == 3
              && a64_guest_gpr_slot(4, true) == 36
              && a64_guest_gpr_slot(23, true) == 55
              && a64_guest_gpr_slot(24, true) == 24
              && a64_guest_gpr_slot(23, false) == 23,
              "A64 guest operands must match the PAL-shadow register mapping");
static_assert(a64_guest_gpr_mem(23, true).base_id() == CJitEngine::RegAlloc::kRegs.id()
              && a64_guest_gpr_mem(23, true).offset() == 55 * 8,
              "A64 guest GPR operands must be fixed offsets from the register-bank base");

static constexpr bool a64_block_pin_contract_probe() noexcept
{
  const CJitEngine::RegAlloc regs = make_a64_block_regalloc();
  return regs.pin_count == kA64BlockGuestPins.size()
      && regs.guest_of_pin(0) == 1 && regs.host_of(1, false) == 23
      && regs.guest_of_pin(1) == 16 && regs.host_of(16, false) == 24
      && regs.guest_of_pin(2) == 30 && regs.host_of(30, false) == 25
      && regs.guest_of_pin(3) == 22 && regs.host_of(22, false) == 26
      && regs.guest_of_pin(4) == 23 && regs.host_of(23, false) == 27
      && regs.guest_of_pin(5) == 27 && regs.host_of(27, false) == 28
      && regs.host_of(22, true) == -1 && regs.host_of(23, true) == -1
      && a64_guest_gpr_slot(22, true) == 54
      && a64_guest_gpr_slot(23, true) == 55
      && !CJitEngine::RegAlloc::guest_pin_candidate(22)
      && !CJitEngine::RegAlloc::guest_pin_candidate(23);
}

static_assert(a64_block_pin_contract_probe(),
              "A64 block bodies must share one PAL-safe guest-pin convention");

static asmjit::Error emit_a64_load_guest_pins(asmjit::a64::Assembler& a,
                                               const CJitEngine::RegAlloc& regs)
{
  using namespace asmjit;
  for (uint32_t i = 0; i < regs.pin_count; ++i) {
    const int guest = regs.guest_of_pin(i);
    if (guest < 0) return Error::kInvalidArgument;
    const Error err = a.ldr(a64::x(CJitEngine::RegAlloc::kGuestPin0.id() + i),
                            a64_guest_gpr_mem(static_cast<uint32_t>(guest), false));
    if (err != Error::kOk) return err;
  }
  return Error::kOk;
}

static asmjit::Error emit_a64_sync_guest_pins(asmjit::a64::Assembler& a,
                                               const CJitEngine::RegAlloc& regs)
{
  using namespace asmjit;
  for (uint32_t i = 0; i < regs.pin_count; ++i) {
    const int guest = regs.guest_of_pin(i);
    if (guest < 0) return Error::kInvalidArgument;
    const Error err = a.str(a64::x(CJitEngine::RegAlloc::kGuestPin0.id() + i),
                            a64_guest_gpr_mem(static_cast<uint32_t>(guest), false));
    if (err != Error::kOk) return err;
  }
  return Error::kOk;
}

static bool a64_is_real_x(const asmjit::a64::Gp& reg) noexcept
{
  return reg.is_gp64() && reg.id() < asmjit::a64::Gp::kIdSp;
}

static bool a64_is_address_x(const asmjit::a64::Gp& reg) noexcept
{
  return reg.is_gp64() && reg.id() <= asmjit::a64::Gp::kIdSp;
}

// AsmJit's MOV pseudo-op selects a logical immediate or the shortest MOVZ/MOVN/MOVK sequence.
static asmjit::Error emit_a64_mov_u64(asmjit::a64::Assembler& a,
                                      const asmjit::a64::Gp& dst, uint64_t value)
{
  if (!a64_is_real_x(dst)) return asmjit::Error::kInvalidArgument;
  return a.mov(dst, asmjit::imm(value));
}

static asmjit::Error emit_a64_add_offset(asmjit::a64::Assembler& a,
    const asmjit::a64::Gp& dst, const asmjit::a64::Gp& base, int64_t delta,
    const asmjit::a64::Gp& scratch = CJitEngine::RegAlloc::kScratch0)
{
  using namespace asmjit;
  if (!a64_is_address_x(dst) || !a64_is_address_x(base)) return Error::kInvalidArgument;
  if (delta == 0)
    return dst.id() == base.id() ? Error::kOk : a.mov(dst, base);

  const bool negative = delta < 0;
  const uint64_t magnitude = negative ? uint64_t(0) - uint64_t(delta) : uint64_t(delta);
  auto emit_part = [&](const a64::Gp& part_dst, const a64::Gp& part_base,
                       uint64_t part) -> Error {
    return negative ? a.sub(part_dst, part_base, imm(part))
                    : a.add(part_dst, part_base, imm(part));
  };

  if (arm::Utils::is_add_sub_imm(magnitude))
    return emit_part(dst, base, magnitude);

  // Two immediate instructions cover every 24-bit magnitude without consuming scratch.
  if (magnitude <= 0x00ffffffu) {
    Error err = emit_part(dst, base, magnitude & 0xfffu);
    return err != Error::kOk ? err : emit_part(dst, dst, magnitude & 0xfff000u);
  }

  if (!a64_is_real_x(scratch) || scratch.id() == base.id())
    return Error::kInvalidArgument;
  Error err = emit_a64_mov_u64(a, scratch, magnitude);
  if (err != Error::kOk) return err;
  return negative ? a.sub(dst, base, scratch) : a.add(dst, base, scratch);
}

static bool a64_is_tail_scratch(const asmjit::a64::Gp& reg) noexcept
{
  return reg.is_gp() && reg.id() >= CJitEngine::RegAlloc::kScratch0.id()
      && reg.id() <= CJitEngine::RegAlloc::kScratch6.id();
}

static asmjit::a64::Gp a64_tail_scratch_avoiding(
    uint32_t first, uint32_t second = UINT32_MAX) noexcept
{
  for (uint32_t id = CJitEngine::RegAlloc::kScratch0.id();
       id <= CJitEngine::RegAlloc::kScratch6.id(); ++id)
    if (id != first && id != second) return asmjit::a64::x(id);
  return asmjit::a64::xzr;
}

static asmjit::Error emit_a64_cpu_mem(asmjit::a64::Assembler& a,
    const asmjit::a64::Gp& address, const asmjit::a64::Gp& expansion,
    uint32_t offset, uint32_t access_size, asmjit::a64::Mem& mem)
{
  using namespace asmjit;
  if ((offset & (access_size - 1u)) == 0) {
    if (offset <= 4095u * access_size) {
      mem = a64::ptr(CJitEngine::RegAlloc::kCpu, static_cast<int32_t>(offset));
      return Error::kOk;
    }
    // Leave the low 12 bits in the load/store displacement.
    if (offset <= 0x00ffffffu) {
      mem = a64::ptr(address, static_cast<int32_t>(offset & 0xfffu));
      return a.add(address, CJitEngine::RegAlloc::kCpu, imm(offset & 0xfff000u));
    }
  }
  mem = a64::ptr(address);
  return emit_a64_add_offset(a, address, CJitEngine::RegAlloc::kCpu,
                            static_cast<int64_t>(offset), expansion);
}

static asmjit::Error emit_a64_load_cpu_u64(asmjit::a64::Assembler& a,
    const asmjit::a64::Gp& dst, uint32_t offset)
{
  using namespace asmjit;
  if (!dst.is_gp64() || !a64_is_tail_scratch(dst)) return Error::kInvalidArgument;
  const a64::Gp expansion = a64_tail_scratch_avoiding(dst.id());
  a64::Mem mem;
  Error err = emit_a64_cpu_mem(a, dst, expansion, offset, 8, mem);
  return err != Error::kOk ? err : a.ldr(dst, mem);
}

static asmjit::Error emit_a64_load_cpu_u8(asmjit::a64::Assembler& a,
    const asmjit::a64::Gp& dst, uint32_t offset, bool acquire)
{
  using namespace asmjit;
  if (!dst.is_gp32() || !a64_is_tail_scratch(dst)) return Error::kInvalidArgument;
  const a64::Gp address = a64::x(dst.id());
  const a64::Gp expansion = a64_tail_scratch_avoiding(dst.id());
  if (acquire) {
    Error err = emit_a64_add_offset(a, address, CJitEngine::RegAlloc::kCpu,
                                    static_cast<int64_t>(offset), expansion);
    return err != Error::kOk ? err : a.ldarb(dst, a64::ptr(address));
  }
  a64::Mem mem;
  Error err = emit_a64_cpu_mem(a, address, expansion, offset, 1, mem);
  return err != Error::kOk ? err : a.ldrb(dst, mem);
}

static asmjit::Error emit_a64_load_cpu_u16(asmjit::a64::Assembler& a,
    const asmjit::a64::Gp& dst, uint32_t offset, bool acquire)
{
  using namespace asmjit;
  if (!dst.is_gp32() || !a64_is_tail_scratch(dst)) return Error::kInvalidArgument;
  const a64::Gp address = a64::x(dst.id());
  const a64::Gp expansion = a64_tail_scratch_avoiding(dst.id());
  if (acquire) {
    Error err = emit_a64_add_offset(a, address, CJitEngine::RegAlloc::kCpu,
                                    static_cast<int64_t>(offset), expansion);
    return err != Error::kOk ? err : a.ldarh(dst, a64::ptr(address));
  }
  a64::Mem mem;
  Error err = emit_a64_cpu_mem(a, address, expansion, offset, 2, mem);
  return err != Error::kOk ? err : a.ldrh(dst, mem);
}

static asmjit::Error emit_a64_load_cpu_u32(asmjit::a64::Assembler& a,
    const asmjit::a64::Gp& dst, uint32_t offset)
{
  using namespace asmjit;
  if (!dst.is_gp32() || !a64_is_tail_scratch(dst)) return Error::kInvalidArgument;
  const a64::Gp address = a64::x(dst.id());
  const a64::Gp expansion = a64_tail_scratch_avoiding(dst.id());
  a64::Mem mem;
  Error err = emit_a64_cpu_mem(a, address, expansion, offset, 4, mem);
  return err != Error::kOk ? err : a.ldr(dst, mem);
}

static asmjit::Error emit_a64_store_cpu_u64(asmjit::a64::Assembler& a,
    const asmjit::a64::Gp& src, uint32_t offset)
{
  using namespace asmjit;
  if (!a64_is_real_x(src) || src.id() == CJitEngine::RegAlloc::kCpu.id())
    return Error::kInvalidArgument;
  const a64::Gp address = a64_tail_scratch_avoiding(src.id());
  const a64::Gp expansion =
      a64_tail_scratch_avoiding(src.id(), address.id());
  if (!a64_is_real_x(address) || !a64_is_real_x(expansion))
    return Error::kInvalidArgument;
  a64::Mem mem;
  Error err = emit_a64_cpu_mem(a, address, expansion, offset, 8, mem);
  return err != Error::kOk ? err : a.str(src, mem);
}

struct A64FrameLayout {
  static constexpr int32_t kCpuRegs = 16;
  static constexpr int32_t kCountPc = 32;
  static constexpr int32_t kPin01 = 48;
  static constexpr int32_t kPin23 = 64;
  static constexpr int32_t kPin45 = 80;
  static constexpr int32_t kHelperOut = 96;
  static constexpr int32_t kHelperScratch = 104;
  static constexpr int32_t kSize = 112;
};

static_assert(A64FrameLayout::kSize % 16 == 0,
              "A64 JIT frame must preserve 16-byte stack alignment");
static_assert(A64FrameLayout::kHelperOut % alignof(uint64_t) == 0
              && A64FrameLayout::kHelperOut + 8 <= A64FrameLayout::kHelperScratch,
              "A64 helper out storage must be aligned and disjoint from call scratch");
static_assert(A64FrameLayout::kHelperScratch + 8 == A64FrameLayout::kSize,
              "A64 helper locals must fit inside the fixed frame");

enum class A64CallArgKind : uint8_t {
  kCpu,
  kGuest,
  kGuestOrZero,
  kHost,
  kOut,
  kImm32,
  kImm64
};

struct A64CallArg {
  A64CallArgKind kind = A64CallArgKind::kImm64;
  uint64_t value = 0;
};

static constexpr uint32_t kA64MaxCallArgs = 8;

static constexpr bool a64_valid_helper_host_source(uint32_t host) noexcept
{
  // x16 is the call target, x17 is linker scratch, x18 is platform-reserved,
  // and x29/x30/SP/ZR are fixed framee, not helper
  return host < 16 || (host >= 19 && host <= 28);
}

static constexpr bool a64_valid_call_arg(const A64CallArg& arg) noexcept
{
  switch (arg.kind) {
    case A64CallArgKind::kCpu:
    case A64CallArgKind::kOut:
    case A64CallArgKind::kImm64:
      return true;
    case A64CallArgKind::kGuest:
      return arg.value < 31;
    case A64CallArgKind::kGuestOrZero:
      return arg.value < 32;
    case A64CallArgKind::kHost:
      return arg.value <= UINT32_MAX
          && a64_valid_helper_host_source(static_cast<uint32_t>(arg.value));
    case A64CallArgKind::kImm32:
      return arg.value <= UINT32_MAX;
  }
  return false;
}

static_assert(kA64MaxCallArgs == 8
              && a64_valid_call_arg({A64CallArgKind::kGuest, 30})
              && !a64_valid_call_arg({A64CallArgKind::kGuest, 31})
              && a64_valid_call_arg({A64CallArgKind::kGuestOrZero, 31})
              && a64_valid_helper_host_source(0)
              && a64_valid_helper_host_source(15)
              && !a64_valid_helper_host_source(16)
              && !a64_valid_helper_host_source(17)
              && !a64_valid_helper_host_source(18)
              && a64_valid_helper_host_source(19)
              && a64_valid_helper_host_source(28)
              && !a64_valid_helper_host_source(29),
              "A64 helper arguments must respect AAPCS64 register roles");

static int a64_helper_index(const CJitEngine::HelperSet& helpers,
                            const void* helper) noexcept
{
  constexpr size_t kHelperCount = 19;
  static_assert(sizeof(CJitEngine::HelperSet) == kHelperCount * sizeof(void*),
                "HelperSet must remain the CPU helper table's pure pointer layout");
  if (helper == nullptr) return -1;
  std::array<void*, kHelperCount> table{};
  std::memcpy(table.data(), &helpers, sizeof(helpers));
  for (size_t i = 0; i < table.size(); ++i)
    if (table[i] == helper) return static_cast<int>(i);
  return -1;
}

static constexpr asmjit::a64::Mem a64_helper_out_mem() noexcept
{
  return asmjit::a64::ptr(asmjit::a64::sp, A64FrameLayout::kHelperOut);
}

static constexpr asmjit::a64::Mem a64_helper_scratch_mem() noexcept
{
  return asmjit::a64::ptr(asmjit::a64::sp, A64FrameLayout::kHelperScratch);
}

static_assert(a64_helper_out_mem().base_id() == asmjit::a64::Gp::kIdSp
              && a64_helper_out_mem().offset() == A64FrameLayout::kHelperOut
              && a64_helper_scratch_mem().offset() == A64FrameLayout::kHelperScratch,
              "A64 helper locals must remain fixed SP-relative operands");

// Preserve the incoming values of x0-x7 when helper arguments change them.
static asmjit::Error emit_a64_parallel_arg_moves(asmjit::a64::Assembler& a,
    const std::array<A64CallArg, kA64MaxCallArgs>& args, uint32_t count)
{
  using namespace asmjit;
  constexpr int8_t kNoSource = -1;
  constexpr int8_t kStackSource = -2;
  std::array<int8_t, kA64MaxCallArgs> source{};
  std::array<bool, kA64MaxCallArgs> pending{};
  source.fill(kNoSource);

  uint32_t remaining = 0;
  for (uint32_t dst = 0; dst < count; ++dst) {
    const A64CallArg& arg = args[dst];
    if (arg.kind != A64CallArgKind::kHost || arg.value >= kA64MaxCallArgs
        || arg.value == dst) continue;
    source[dst] = static_cast<int8_t>(arg.value);
    pending[dst] = true;
    ++remaining;
  }

  while (remaining != 0) {
    bool progressed = false;
    for (uint32_t dst = 0; dst < count; ++dst) {
      if (!pending[dst]) continue;
      bool old_dst_needed = false;
      for (uint32_t other = 0; other < count; ++other) {
        if (pending[other] && source[other] == static_cast<int8_t>(dst)) {
          old_dst_needed = true;
          break;
        }
      }
      if (old_dst_needed) continue;

      const Error err = source[dst] == kStackSource
          ? a.ldr(a64::x(dst), a64_helper_scratch_mem())
          : a.mov(a64::x(dst), a64::x(static_cast<uint32_t>(source[dst])));
      if (err != Error::kOk) return err;
      pending[dst] = false;
      --remaining;
      progressed = true;
    }
    if (progressed) continue;

    uint32_t cycle_dst = 0;
    while (cycle_dst < count && !pending[cycle_dst]) ++cycle_dst;
    if (cycle_dst == count) return Error::kInvalidState;
    for (uint32_t i = 0; i < count; ++i)
      if (pending[i] && source[i] == kStackSource) return Error::kInvalidState;

    Error err = a.str(a64::x(cycle_dst), a64_helper_scratch_mem());
    if (err != Error::kOk) return err;
    for (uint32_t i = 0; i < count; ++i)
      if (pending[i] && source[i] == static_cast<int8_t>(cycle_dst))
        source[i] = kStackSource;
  }
  return Error::kOk;
}

static asmjit::Error emit_a64_call_arg(asmjit::a64::Assembler& a,
    const CJitEngine::RegAlloc& regs, bool pal_shadow, uint32_t index,
    const A64CallArg& arg)
{
  using namespace asmjit;
  const a64::Gp dst = a64::x(index);
  switch (arg.kind) {
    case A64CallArgKind::kCpu:
      return a.mov(dst, CJitEngine::RegAlloc::kCpu);
    case A64CallArgKind::kGuest:
    case A64CallArgKind::kGuestOrZero: {
      const A64GprRoute route = a64_guest_gpr_read_route(
          regs, static_cast<uint32_t>(arg.value), pal_shadow);
      switch (route.kind) {
        case A64GprRouteKind::kMemory:
          return a.ldr(dst, a64::ptr(CJitEngine::RegAlloc::kRegs,
              static_cast<int32_t>(route.slot * sizeof(uint64_t))));
        case A64GprRouteKind::kPinned:
          return a.mov(dst, a64::x(static_cast<uint32_t>(route.host)));
        case A64GprRouteKind::kZero:
          return a.mov(dst, a64::xzr);
        default:
          return Error::kInvalidArgument;
      }
    }
    case A64CallArgKind::kHost:
      // Sources in x0-x7 were resolved as a parallel copy above.
      return arg.value < kA64MaxCallArgs
          ? Error::kOk
          : a.mov(dst, a64::x(static_cast<uint32_t>(arg.value)));
    case A64CallArgKind::kOut:
      return emit_a64_add_offset(a, dst, a64::sp, A64FrameLayout::kHelperOut);
    case A64CallArgKind::kImm32:
      return a.mov(dst.w(), imm(static_cast<uint32_t>(arg.value)));
    case A64CallArgKind::kImm64:
      return emit_a64_mov_u64(a, dst, arg.value);
  }
  return Error::kInvalidArgument;
}

static asmjit::Error emit_a64_helper_call(
    asmjit::a64::Assembler& a, const CJitEngine::JitOffsets& offsets,
    const CJitEngine::HelperSet& helpers, const CJitEngine::RegAlloc& regs,
    bool pal_shadow, const void* helper, std::initializer_list<A64CallArg> call_args)
{
  using namespace asmjit;
  if (helper == nullptr || call_args.size() > kA64MaxCallArgs)
    return Error::kInvalidArgument;

  std::array<A64CallArg, kA64MaxCallArgs> args{};
  uint32_t count = 0;
  for (const A64CallArg& arg : call_args) {
    if (!a64_valid_call_arg(arg)) return Error::kInvalidArgument;
    args[count++] = arg;
  }

  Error err = emit_a64_parallel_arg_moves(a, args, count);
  if (err != Error::kOk) return err;
  for (uint32_t i = 0; i < count; ++i) {
    err = emit_a64_call_arg(a, regs, pal_shadow, i, args[i]);
    if (err != Error::kOk) return err;
  }

  const int helper_slot = a64_helper_index(helpers, helper);
  if (helper_slot >= 0 && offsets.helpers != 0) {
    const uint64_t table_offset = static_cast<uint64_t>(offsets.helpers)
        + static_cast<uint64_t>(helper_slot) * sizeof(void*);
    err = emit_a64_add_offset(a, CJitEngine::RegAlloc::kScratch0,
        CJitEngine::RegAlloc::kCpu, static_cast<int64_t>(table_offset));
    if (err != Error::kOk) return err;
    err = a.ldr(CJitEngine::RegAlloc::kCallTarget,
                a64::ptr(CJitEngine::RegAlloc::kScratch0));
  }
  else {
    err = emit_a64_mov_u64(a, CJitEngine::RegAlloc::kCallTarget,
                           reinterpret_cast<uintptr_t>(helper));
  }
  if (err != Error::kOk) return err;
  return a.blr(CJitEngine::RegAlloc::kCallTarget);
}

// Outlined memop slow path.
struct A64ColdStub {
  asmjit::Label slow;
  asmjit::Label join;
  A64DecodedOp op{};
  uint32_t index = 0;
  enum Kind : uint8_t { kLoad, kStore, kFpMem } kind = kLoad;
  bool rederive = false;      // the op left DPC state live: rebuild it after the helper
  int8_t restore_fwd = -1;    // a forward kept across this op: reload kScratch2's mirror
};

struct A64EmitContext {
  asmjit::a64::Assembler& assembler;
  const CJitEngine::JitOffsets& offsets;
  const AlphaCPUProfile& profile;
  const CJitEngine::HelperSet& helpers;
  CJitEngine::RegAlloc& regs;
  const asmjit::Label& done;
  uint64_t start_pc;
  bool pal_block;
  bool pal_shadow;
  bool smp_memory = false;
  uintptr_t ll_buckets = 0;
  uintptr_t ll_watch = 0;
  const A64BlockPlan* plan = nullptr;
  A64OpEmitReceipt receipt{};
  uint32_t receipt_index = UINT32_MAX;
  std::vector<A64ColdStub>* cold = nullptr;   // production memop slow paths
  A64DpcState prev_dpc{};                     // DPC state entering the current op
  int prev_fwd = -1;                          // forwarded guest GPR entering the op
  // Direct exits: the branch terminator records its successors here instead of moving x22.
  bool direct_exits = false;
  uint8_t direct_kind = 0;                    // 0 fall-through, 1 conditional, 2 unconditional
  asmjit::Label direct_taken{};
  uint64_t direct_target = 0;
  uint64_t direct_fall = 0;
};

// Rc = Ra <logical> op2 -- each of the six is one A64 register-form op. The 8-bit
// literal is materialized in scratch: 0-255 is rarely a valid A64 logical immediate.
static A64OpEmitReceipt emit_a64_intl_logical(A64EmitContext& context,
                                              const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)   // Rc==31: architectural no-op
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;

  // Resolve a source operand into a register: xzr for R31, the pin, or a load.
  auto source = [&](uint32_t raw_reg, const a64::Gp& scratch) -> a64::Gp {
    const A64GprRoute route =
        a64_guest_gpr_read_route(context.regs, raw_reg, context.pal_shadow);
    switch (route.kind) {
      case A64GprRouteKind::kZero:   return a64::xzr;
      case A64GprRouteKind::kPinned: return a64::x(static_cast<uint32_t>(route.host));
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(raw_reg))
          return RA::kScratch2;   // forwarded: the previous op left it here
        err = a.ldr(scratch, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(route.slot * sizeof(uint64_t))));
        return scratch;
      default:
        err = Error::kInvalidArgument;
        return scratch;
    }
  };

  const a64::Gp op1 = source(op.ra, RA::kScratch0);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  a64::Gp op2 = RA::kScratch1;
  if (op.is_literal) {
    if (op.literal == 0) op2 = a64::xzr;
    else                 err = a.mov(RA::kScratch1, imm(op.literal));
  } else {
    op2 = source(op.rb, RA::kScratch1);
  }
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;

  switch ((op.ins >> 5) & 0x7fu) {
    case 0x00: err = a.and_(dst, op1, op2); break;   // AND
    case 0x20: err = a.orr(dst, op1, op2);  break;   // BIS
    case 0x40: err = a.eor(dst, op1, op2);  break;   // XOR
    case 0x08: err = a.bic(dst, op1, op2);  break;   // BIC
    case 0x28: err = a.orn(dst, op1, op2);  break;   // ORNOT
    case 0x48: err = a.eon(dst, op1, op2);  break;   // EQV
    default:   err = Error::kInvalidInstruction; break;
  }
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

// Branch format: BR/BSR link, etc
static A64OpEmitReceipt emit_a64_branch_int(A64EmitContext& context,
                                            const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;

  const uint64_t fall = a64_advance_pc(context.start_pc, index + 1);
  const uint64_t target = a64_branch_target(fall, op.ins);
  Error err = Error::kOk;
  auto exit_to = [&](uint64_t pc) {   // direct exit: one static successor
    context.direct_kind = 2;
    context.direct_target = pc;
    context.direct_fall = fall;
  };

  if (op.opcode == 0x30 || op.opcode == 0x34) {   // BR / BSR
    const A64GprRoute wa =
        a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
    switch (wa.kind) {
      case A64GprRouteKind::kPinned:
        err = emit_a64_mov_u64(a, a64::x(static_cast<uint32_t>(wa.host)),
                               fall & ~uint64_t(3));
        break;
      case A64GprRouteKind::kMemory:
        err = emit_a64_mov_u64(a, RA::kScratch0, fall & ~uint64_t(3));
        if (err == Error::kOk)
          err = a.str(RA::kScratch0, a64::ptr(RA::kRegs,
                      static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
        break;
      case A64GprRouteKind::kDiscard:   // Ra==31: no link
        break;
      default:
        return {Error::kInvalidArgument, op.kind};
    }
    if (err == Error::kOk) {
      if (context.direct_exits) exit_to(target);
      else err = emit_a64_mov_u64(a, RA::kNextPc, target);
    }
    return a64_completed_op_receipt(op, err);
  }

  const A64GprRoute rra =
      a64_guest_gpr_read_route(context.regs, op.ra, context.pal_shadow);
  if (rra.kind == A64GprRouteKind::kZero) {
    // Ra==31 reads zero, so the condition folds (and CMP can't encode xzr as its
    // base -- reg 31 is SP there). BEQ/BGE/BLE/BLBC on zero are always taken.
    const bool taken = op.opcode == 0x39 || op.opcode == 0x3e
                    || op.opcode == 0x3b || op.opcode == 0x38;
    if (context.direct_exits) exit_to(taken ? target : fall);
    else if (taken) err = emit_a64_mov_u64(a, RA::kNextPc, target);
    return a64_completed_op_receipt(op, err);
  }
  a64::Gp src = RA::kScratch0;
  switch (rra.kind) {
    case A64GprRouteKind::kPinned: src = a64::x(static_cast<uint32_t>(rra.host)); break;
    case A64GprRouteKind::kMemory:
      if (context.prev_fwd == static_cast<int>(op.ra)) { src = RA::kScratch2; break; }
      err = a.ldr(RA::kScratch0, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(rra.slot * sizeof(uint64_t))));
      break;
    default:
      return {Error::kInvalidArgument, op.kind};
  }
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (context.direct_exits) {
    // Branch to the taken label; the patch tail binds it and emits both sites.
    const Label taken = a.new_label();
    switch (op.opcode) {
      case 0x39: err = a.cbz(src, taken);  break;                 // BEQ
      case 0x3d: err = a.cbnz(src, taken); break;                 // BNE
      case 0x38: err = a.tst(src, imm(1));                        // BLBC
                 if (err == Error::kOk) err = a.b_eq(taken);
                 break;
      case 0x3c: err = a.tst(src, imm(1));                        // BLBS
                 if (err == Error::kOk) err = a.b_ne(taken);
                 break;
      case 0x3a: err = a.cmp(src, imm(0));                        // BLT
                 if (err == Error::kOk) err = a.b_lt(taken);
                 break;
      case 0x3b: err = a.cmp(src, imm(0));                        // BLE
                 if (err == Error::kOk) err = a.b_le(taken);
                 break;
      case 0x3e: err = a.cmp(src, imm(0));                        // BGE
                 if (err == Error::kOk) err = a.b_ge(taken);
                 break;
      case 0x3f: err = a.cmp(src, imm(0));                        // BGT
                 if (err == Error::kOk) err = a.b_gt(taken);
                 break;
      default:   return {Error::kInvalidInstruction, op.kind};
    }
    context.direct_kind = 1;
    context.direct_taken = taken;
    context.direct_target = target;
    context.direct_fall = fall;
    return a64_completed_op_receipt(op, err);
  }

  const Label not_taken = a.new_label();
  switch (op.opcode) {
    case 0x39: err = a.cbnz(src, not_taken); break;               // BEQ
    case 0x3d: err = a.cbz(src, not_taken);  break;               // BNE
    case 0x38: err = a.tst(src, imm(1));                          // BLBC
               if (err == Error::kOk) err = a.b_ne(not_taken);
               break;
    case 0x3c: err = a.tst(src, imm(1));                          // BLBS
               if (err == Error::kOk) err = a.b_eq(not_taken);
               break;
    case 0x3a: err = a.cmp(src, imm(0));                          // BLT
               if (err == Error::kOk) err = a.b_ge(not_taken);
               break;
    case 0x3b: err = a.cmp(src, imm(0));                          // BLE
               if (err == Error::kOk) err = a.b_gt(not_taken);
               break;
    case 0x3e: err = a.cmp(src, imm(0));                          // BGE
               if (err == Error::kOk) err = a.b_lt(not_taken);
               break;
    case 0x3f: err = a.cmp(src, imm(0));                          // BGT
               if (err == Error::kOk) err = a.b_le(not_taken);
               break;
    default:   return {Error::kInvalidInstruction, op.kind};
  }
  if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kNextPc, target);
  if (err == Error::kOk) err = a.bind(not_taken);
  return a64_completed_op_receipt(op, err);
}

// Rc = Ra shift op2: LSLV/LSRV/ASRV take the count mod 64 
static A64OpEmitReceipt emit_a64_ints_shift(A64EmitContext& context,
                                            const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)   // Rc==31: architectural no-op
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;

  auto source = [&](uint32_t raw_reg, const a64::Gp& scratch) -> a64::Gp {
    const A64GprRoute route =
        a64_guest_gpr_read_route(context.regs, raw_reg, context.pal_shadow);
    switch (route.kind) {
      case A64GprRouteKind::kZero:   return a64::xzr;
      case A64GprRouteKind::kPinned: return a64::x(static_cast<uint32_t>(route.host));
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(raw_reg))
          return RA::kScratch2;   // forwarded: the previous op left it here
        err = a.ldr(scratch, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(route.slot * sizeof(uint64_t))));
        return scratch;
      default:
        err = Error::kInvalidArgument;
        return scratch;
    }
  };

  const a64::Gp op1 = source(op.ra, RA::kScratch0);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;
  const uint32_t func = (op.ins >> 5) & 0x7fu;

  if (op.is_literal) {
    const uint32_t count = op.literal & 63u;
    switch (func) {
      case 0x39: err = a.lsl(dst, op1, imm(count)); break;   // SLL
      case 0x34: err = a.lsr(dst, op1, imm(count)); break;   // SRL
      case 0x3c: err = a.asr(dst, op1, imm(count)); break;   // SRA
      default:   err = Error::kInvalidInstruction; break;
    }
  } else {
    const a64::Gp op2 = source(op.rb, RA::kScratch1);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    switch (func) {
      case 0x39: err = a.lsl(dst, op1, op2); break;
      case 0x34: err = a.lsr(dst, op1, op2); break;
      case 0x3c: err = a.asr(dst, op1, op2); break;
      default:   err = Error::kInvalidInstruction; break;
    }
  }
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

// LDA/LDAH
static A64OpEmitReceipt emit_a64_load_address(A64EmitContext& context,
                                              const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wa =
      a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
  if (wa.kind == A64GprRouteKind::kInvalid || wa.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wa.kind == A64GprRouteKind::kDiscard)   // Ra==31: architectural no-op
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  const int64_t disp = static_cast<int16_t>(op.ins & 0xffffu);
  const int64_t delta = op.opcode == 0x09 ? disp * 65536 : disp;

  const a64::Gp dst = wa.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wa.host)) : RA::kScratch2;

  Error err = Error::kOk;
  const A64GprRoute rb =
      a64_guest_gpr_read_route(context.regs, op.rb, context.pal_shadow);
  switch (rb.kind) {
    case A64GprRouteKind::kZero:
      err = emit_a64_mov_u64(a, dst, static_cast<uint64_t>(delta));
      break;
    case A64GprRouteKind::kPinned:
      err = emit_a64_add_offset(a, dst, a64::x(static_cast<uint32_t>(rb.host)),
                                delta, RA::kScratch0);
      break;
    case A64GprRouteKind::kMemory:
      if (context.prev_fwd == static_cast<int>(op.rb)) {   // forwarded base
        err = emit_a64_add_offset(a, dst, RA::kScratch2, delta, RA::kScratch1);
        break;
      }
      err = a.ldr(RA::kScratch0, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(rb.slot * sizeof(uint64_t))));
      if (err == Error::kOk)
        err = emit_a64_add_offset(a, dst, RA::kScratch0, delta, RA::kScratch1);
      break;
    default:
      return {Error::kInvalidArgument, op.kind};
  }
  if (err == Error::kOk && wa.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.ra);
  }
  return a64_completed_op_receipt(op, err);
}

// ZAP/ZAPNOT byte-expand
static constexpr std::array<uint64_t, 256> g_a64_zapnot_mask = [] {
  std::array<uint64_t, 256> t{};
  for (int b = 0; b < 256; ++b) {
    uint64_t m = 0;
    for (int i = 0; i < 8; ++i) if (b & (1 << i)) m |= uint64_t(0xff) << (i * 8);
    t[b] = m;
  }
  return t;
}();
static_assert(g_a64_zapnot_mask[0] == 0 && g_a64_zapnot_mask[0x03] == 0xffffu
              && g_a64_zapnot_mask[0xf0] == 0xffffffff00000000u
              && g_a64_zapnot_mask[0xff] == ~uint64_t(0),
              "ZAP byte expansion must map selector bits to whole bytes");

// Rc = Ra & keep-mask. 
static A64OpEmitReceipt emit_a64_ints_zap(A64EmitContext& context,
                                          const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)   // Rc==31: architectural no-op
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;

  auto source = [&](uint32_t raw_reg, const a64::Gp& scratch) -> a64::Gp {
    const A64GprRoute route =
        a64_guest_gpr_read_route(context.regs, raw_reg, context.pal_shadow);
    switch (route.kind) {
      case A64GprRouteKind::kZero:   return a64::xzr;
      case A64GprRouteKind::kPinned: return a64::x(static_cast<uint32_t>(route.host));
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(raw_reg))
          return RA::kScratch2;   // forwarded: the previous op left it here
        err = a.ldr(scratch, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(route.slot * sizeof(uint64_t))));
        return scratch;
      default:
        err = Error::kInvalidArgument;
        return scratch;
    }
  };

  const a64::Gp op1 = source(op.ra, RA::kScratch0);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;
  const bool is_zap = ((op.ins >> 5) & 0x7fu) == 0x30;

  if (op.is_literal) {
    const uint64_t keep = is_zap ? ~g_a64_zapnot_mask[op.literal]
                                 : g_a64_zapnot_mask[op.literal];
    if (keep == 0) {
      err = a.mov(dst, a64::xzr);
    } else if (keep == ~uint64_t(0)) {
      if (dst.id() != op1.id()) err = a.mov(dst, op1);
    } else {
      if (arm::Utils::is_logical_imm(keep, 64)) {
        err = a.and_(dst, op1, imm(keep));
      } else {
        err = emit_a64_mov_u64(a, RA::kScratch1, keep);
        if (err == Error::kOk) err = a.and_(dst, op1, RA::kScratch1);
      }
    }
  } else {
    const a64::Gp sel = source(op.rb, RA::kScratch1);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    err = a.and_(RA::kScratch1, sel, imm(0xff));            // selector byte (ZR-safe)
    if (err == Error::kOk)
      err = emit_a64_mov_u64(a, RA::kScratch3,
          reinterpret_cast<uintptr_t>(g_a64_zapnot_mask.data()));
    if (err == Error::kOk) err = a.lsl(RA::kScratch1, RA::kScratch1, imm(3));
    if (err == Error::kOk) err = a.add(RA::kScratch3, RA::kScratch3, RA::kScratch1);
    if (err == Error::kOk) err = a.ldr(RA::kScratch1, a64::ptr(RA::kScratch3));
    if (err == Error::kOk && is_zap)
      err = (a.mvn)(RA::kScratch1, RA::kScratch1);          // ZAP keeps the CLEAR bytes; avoid the MSVC mvn macro.
    if (err == Error::kOk) err = a.and_(dst, op1, RA::kScratch1);
  }
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

// Memory ops run through jit_read/jit_write only.

// VA = Rb + normalized displacement. Hardware accesses supply their family descriptor.
static asmjit::Error emit_a64_mem_va_displacement(A64EmitContext& context,
    const A64DecodedOp& op, bool force_align, int64_t disp)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;
  const A64GprRoute rb =
      a64_guest_gpr_read_route(context.regs, op.rb, context.pal_shadow);
  switch (rb.kind) {
    case A64GprRouteKind::kZero:
      err = emit_a64_mov_u64(a, RA::kScratch4, static_cast<uint64_t>(disp));
      break;
    case A64GprRouteKind::kPinned:
      err = emit_a64_add_offset(a, RA::kScratch4,
                                a64::x(static_cast<uint32_t>(rb.host)), disp,
                                RA::kScratch0);
      break;
    case A64GprRouteKind::kMemory:
      if (context.prev_fwd == static_cast<int>(op.rb)) {   // forwarded base
        err = emit_a64_add_offset(a, RA::kScratch4, RA::kScratch2, disp,
                                  RA::kScratch0);
        break;
      }
      err = a.ldr(RA::kScratch4, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(rb.slot * sizeof(uint64_t))));
      if (err == Error::kOk)
        err = emit_a64_add_offset(a, RA::kScratch4, RA::kScratch4, disp,
                                  RA::kScratch0);
      break;
    default:
      return Error::kInvalidArgument;
  }
  if (err == Error::kOk && force_align)
    err = a.and_(RA::kScratch4, RA::kScratch4, imm(~uint64_t(7)));
  return err;
}

// Ordinary memory forms retain their 16-bit displacement.
static asmjit::Error emit_a64_mem_va(A64EmitContext& context,
                                     const A64DecodedOp& op, bool force_align)
{
  const int64_t disp = static_cast<int16_t>(op.ins & 0xffffu);
  return emit_a64_mem_va_displacement(context, op, force_align, disp);
}

// Helper-result dispatch: 0 = ok, 2 (production) = fault delivered (count this op,
// keep the PAL-entry PC), anything else = publish this op's PC and retry in the
// interpreter. Bail paths bump x21 and jump to `done` (which derives w0 from it).
static asmjit::Error emit_a64_mem_result_bail(A64EmitContext& context,
                                              uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const Label ok = a.new_label();
  Error err = a.cbz(a64::w0, ok);
  if (err != Error::kOk) return err;
#ifndef JIT_VERIFY
  const Label retry = a.new_label();
  err = a.cmp(a64::w0, imm(2));
  if (err == Error::kOk) err = a.b_ne(retry);
  if (err == Error::kOk)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index + 1));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(retry);
  if (err != Error::kOk) return err;
#endif
  err = emit_a64_mov_u64(a, RA::kScratch0,
                         a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.state_pc);
  if (err == Error::kOk && index != 0)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(ok);
  return err;
}

static constexpr int a64_int_mem_size_bits(uint8_t opcode) noexcept
{
  switch (opcode) {
    case 0x29: case 0x0b: case 0x2d: case 0x0f: return 64;
    case 0x28: case 0x2c: return 32;
    case 0x0c: case 0x0d: return 16;
    default:              return 8;   // 0x0a LDBU / 0x0e STB
  }
}

// integer memop's full helper sequence 
// VA is expected live in kScratch4. 
// Loads end with the out-slot extraction into Ra's route, so this upholds the fastpath state.
static asmjit::Error emit_a64_int_mem_helper_seq(A64EmitContext& context,
    const A64DecodedOp& op, uint32_t index, bool is_store)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const int size_bits = a64_int_mem_size_bits(op.opcode);
  void* const helper = is_store ? context.helpers.write_helper
                                : context.helpers.read_helper;
  const A64CallArg last = is_store
      ? A64CallArg{A64CallArgKind::kGuestOrZero, op.ra}
      : A64CallArg{A64CallArgKind::kOut, 0};
  Error err = Error::kOk;
#ifndef JIT_VERIFY
  err = emit_a64_mov_u64(a, RA::kScratch0,
                         a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0,
                                 context.offsets.state_current_pc);
  if (err != Error::kOk) return err;
  const uint64_t descr = (static_cast<uint64_t>(op.ins) << 32)
                       | static_cast<uint32_t>(size_bits);
  err = emit_a64_helper_call(a, context.offsets, context.helpers, context.regs,
      context.pal_shadow, helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kHost, RA::kScratch4.id()},
       {A64CallArgKind::kImm64, descr},
       last});
#else
  err = emit_a64_helper_call(a, context.offsets, context.helpers, context.regs,
      context.pal_shadow, helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kHost, RA::kScratch4.id()},
       {A64CallArgKind::kImm32, static_cast<uint32_t>(size_bits)},
       last});
#endif
  if (err == Error::kOk) err = emit_a64_mem_result_bail(context, index);
  if (err != Error::kOk || is_store) return err;

  const A64GprRoute wa =
      a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
  const a64::Gp dst = wa.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wa.host)) : RA::kScratch2;
  switch (size_bits) {   // LDL sign-extends; the BWX forms zero-extend
    case 64: err = a.ldr(dst, a64_helper_out_mem()); break;
    case 32: err = a.ldrsw(dst, a64_helper_out_mem()); break;
    case 16: err = a.ldrh(dst.w(), a64_helper_out_mem()); break;
    default: err = a.ldrb(dst.w(), a64_helper_out_mem()); break;
  }
  if (err == Error::kOk && wa.kind == A64GprRouteKind::kMemory)
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
  return err;
}

#ifndef JIT_VERIFY
// DPC fast path (mirrors jit_read/jit_write's cache path)

// Loads a slot field into `dst`; slot fields may sit beyond the scaled-imm range.
static asmjit::Error emit_a64_dpc_field(A64EmitContext& c, const asmjit::a64::Gp& dst,
                                        uint32_t field_off)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = c.assembler;
  if (field_off <= 32760 && (field_off & 7u) == 0)
    return a.ldr(dst, a64::ptr(RA::kScratch6, static_cast<int32_t>(field_off)));
  Error err = emit_a64_add_offset(a, RA::kScratch0, RA::kScratch6,
                                  static_cast<int64_t>(field_off), RA::kScratch1);
  return err != Error::kOk ? err : a.ldr(dst, a64::ptr(RA::kScratch0));
}

// kScratch6 = cpu + ((va >> 13) & dpc_mask) * dpc_stride.
static asmjit::Error emit_a64_dpc_slot(A64EmitContext& c)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = c.assembler;
  Error err = a.lsr(RA::kScratch5, RA::kScratch4, imm(13));
  if (err != Error::kOk) return err;
  if (arm::Utils::is_logical_imm(c.offsets.dpc_mask, 64)) {
    err = a.and_(RA::kScratch5, RA::kScratch5, imm(c.offsets.dpc_mask));
  } else {
    err = emit_a64_mov_u64(a, RA::kScratch1, c.offsets.dpc_mask);
    if (err == Error::kOk) err = a.and_(RA::kScratch5, RA::kScratch5, RA::kScratch1);
  }
  if (err != Error::kOk) return err;
  const uint32_t stride = c.offsets.dpc_stride;
  if (stride != 0 && (stride & (stride - 1)) == 0) {
    uint32_t sh = 0;
    while ((1u << sh) < stride) ++sh;
    if (sh) err = a.lsl(RA::kScratch5, RA::kScratch5, imm(sh));
  } else {
    err = emit_a64_mov_u64(a, RA::kScratch1, stride);
    if (err == Error::kOk) err = a.mul(RA::kScratch5, RA::kScratch5, RA::kScratch1);
  }
  if (err != Error::kOk) return err;
  err = emit_a64_load_cpu_u32(a, RA::kScratch1.w(), c.offsets.dpc_bank);   // current mode's bank
  if (err != Error::kOk) return err;
  err = a.add(RA::kScratch5, RA::kScratch5, RA::kScratch1);
  return err != Error::kOk ? err : a.add(RA::kScratch6, RA::kCpu, RA::kScratch5);
}

// Sets flags: (va & tag_mask) == slot.virt_page. Caller branches on eq/ne.
static asmjit::Error emit_a64_dpc_tag_cmp(A64EmitContext& c, uint64_t tag_mask,
                                          bool is_write)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = c.assembler;
  const uint32_t row = is_write ? c.offsets.dpc_write_row : 0;
  Error err = a.and_(RA::kScratch5, RA::kScratch4, imm(tag_mask));   // wrapped run: encodable
  if (err == Error::kOk)
    err = emit_a64_dpc_field(c, RA::kScratch0, row + c.offsets.dpc_virt_page);
  return err != Error::kOk ? err : a.cmp(RA::kScratch0, RA::kScratch5);
}

// Recheck the loaded write tag with the LL-watch bit.
//
// Alignment bits in the expected tag remain intact, so a marked page cannot admit a misaligned store.
static asmjit::Error emit_a64_dpc_watch_cmp(A64EmitContext& c)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  Error err = c.assembler.orr(RA::kScratch5, RA::kScratch5,
                               imm(c.offsets.dpc_ll_watch_tag));
  return err != Error::kOk ? err
      : c.assembler.cmp(RA::kScratch0, RA::kScratch5);
}

static asmjit::Error emit_a64_dpc_bias(A64EmitContext& c, bool is_write)
{
  const uint32_t row = is_write ? c.offsets.dpc_write_row : 0;
  return emit_a64_dpc_field(c, CJitEngine::RegAlloc::kScratch3,
                            row + c.offsets.dpc_host_bias);
}

static constexpr uint64_t a64_dpc_tag_mask(int size_bits, bool force_align) noexcept
{
  const uint64_t amask = static_cast<uint64_t>(size_bits / 8) - 1;
  return ~uint64_t(0x1fff) | (force_align ? 0 : amask);
}

// Full probe: VA in kScratch4 -> hit leaves bias in kScratch3, else `slow`.
// Writes to marked pages branch to watched; plain hits fall through.
static asmjit::Error emit_a64_dpc_probe(A64EmitContext& c, int size_bits,
    bool force_align, bool is_write, const asmjit::Label& slow,
    const asmjit::Label* watched = nullptr)
{
  using namespace asmjit;
  asmjit::Error err = emit_a64_dpc_slot(c);
  if (err == asmjit::Error::kOk)
    err = emit_a64_dpc_tag_cmp(c, a64_dpc_tag_mask(size_bits, force_align), is_write);
  if (watched) {
    const Label plain = c.assembler.new_label();
    if (err == Error::kOk) err = c.assembler.b_eq(plain);
    if (err == Error::kOk) err = emit_a64_dpc_watch_cmp(c);
    if (err == Error::kOk) err = c.assembler.b_ne(slow);
    if (err == Error::kOk) err = emit_a64_dpc_bias(c, true);
    if (err == Error::kOk) err = c.assembler.b(*watched);
    if (err == Error::kOk) err = c.assembler.bind(plain);
  } else if (err == Error::kOk) {
    err = c.assembler.b_ne(slow);
  }
  return err != asmjit::Error::kOk ? err : emit_a64_dpc_bias(c, is_write);
}

// Cold-stub epilogue
static asmjit::Error emit_a64_dpc_rederive(A64EmitContext& c, const A64DecodedOp& op,
                                           bool force_align, bool is_write)
{
  // the helper clobbered kScratch2, so the VA must come from the slot not a forward 
  const int saved_fwd = c.prev_fwd;
  c.prev_fwd = -1;
  asmjit::Error err = emit_a64_mem_va(c, op, force_align);
  c.prev_fwd = saved_fwd;
  if (err == asmjit::Error::kOk) err = emit_a64_dpc_slot(c);
  return err != asmjit::Error::kOk ? err : emit_a64_dpc_bias(c, is_write);
}

// Memop entry - (x64's reuse_dpc)
static asmjit::Error emit_a64_dpc_enter(A64EmitContext& c, const A64DecodedOp& op,
    int size_bits, bool force_align, bool is_write, const asmjit::Label& slow,
    const asmjit::Label* watched = nullptr)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = c.assembler;
  const A64DpcState& prev = c.prev_dpc;
  const int32_t disp = static_cast<int16_t>(op.ins & 0xffffu);
  const int32_t delta = disp - prev.disp;
  const bool reuse = prev.live && prev.write == is_write && prev.base == op.rb
                  && prev.force_align == force_align
                  && (!force_align || (delta & 7) == 0);
  const uint64_t tag_mask = a64_dpc_tag_mask(size_bits, force_align);
  const Label ready = a.new_label();
  Error err = Error::kOk;
  if (reuse) {
    if (delta != 0)
      err = emit_a64_add_offset(a, RA::kScratch4, RA::kScratch4, delta, RA::kScratch0);
    if (err == Error::kOk) err = emit_a64_dpc_tag_cmp(c, tag_mask, is_write);
    if (err == Error::kOk) err = a.b_eq(ready);   // same page + aligned: keep slot/bias
    if (watched) {
      if (err == Error::kOk) err = emit_a64_dpc_watch_cmp(c);
      if (err == Error::kOk) err = a.b_eq(*watched); // marked hit keeps the same slot/bias
    }
  } else {
    err = emit_a64_mem_va(c, op, force_align);
  }
  if (err == Error::kOk)
    err = emit_a64_dpc_probe(c, size_bits, force_align, is_write, slow, watched);
  return err != Error::kOk ? err : a.bind(ready);
}

// Aligned RAM hits touch one LL/SC bucket. Preserve the VA, DPC slot/bias and forwarded value;
// x14 holds the lock address until release, or zero when the exact physical line is unmonitored.
// First LL sets the watch byte after draining active CPUs. Contention falls
// back to the helper before the RAM write, with no bucket held.
static asmjit::Error emit_a64_ram_store_lock(A64EmitContext& c,
    const asmjit::Label& slow, bool physical)
{
  if (!c.smp_memory) return asmjit::Error::kOk;
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = c.assembler;
  const Label ready = a.new_label();
  Error err;
  if (physical) {
    err = a.mov(RA::kScratch5, RA::kScratch4);
  } else {
    err = a.add(RA::kScratch5, RA::kScratch3, RA::kScratch4);
    if (err == Error::kOk)
      err = emit_a64_load_cpu_u64(a, RA::kScratch0, c.offsets.dram_ptr);
    if (err == Error::kOk)
      err = a.sub(RA::kScratch5, RA::kScratch5, RA::kScratch0);
  }
  if (c.ll_watch) {
    const Label watched = a.new_label();
    if (err == Error::kOk) err = a.lsr(RA::kScratch0, RA::kScratch5, 6);
    if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch1, c.ll_watch);
    if (err == Error::kOk) err = a.add(RA::kScratch0, RA::kScratch0, RA::kScratch1);
    if (err == Error::kOk) err = a.ldarb(RA::kScratch0.w(), a64::ptr(RA::kScratch0));
    if (err == Error::kOk) err = a.cbnz(RA::kScratch0.w(), watched);
    if (err == Error::kOk) err = a.mov(RA::kScratch5, a64::xzr);
    if (err == Error::kOk) err = a.b(ready);
    if (err == Error::kOk) err = a.bind(watched);
  }
  // CSystem::LLBucket: 64-byte stride, sequence at 0, byte lock at 8.
  if (err == Error::kOk) err = a.and_(RA::kScratch5, RA::kScratch5, imm(0x3fffc0));
  if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch0, c.ll_buckets + 8);
  if (err == Error::kOk) err = a.add(RA::kScratch5, RA::kScratch5, RA::kScratch0);
  const Label busy = a.new_label();
  if (err == Error::kOk) err = a.ldaxrb(RA::kScratch0.w(), a64::ptr(RA::kScratch5));
  if (err == Error::kOk) err = a.cbnz(RA::kScratch0.w(), busy);
  if (err == Error::kOk) err = a.mov(RA::kScratch0.w(), imm(1));
  if (err == Error::kOk)
    err = a.stxrb(RA::kScratch1.w(), RA::kScratch0.w(), a64::ptr(RA::kScratch5));
  if (err == Error::kOk) err = a.cbnz(RA::kScratch1.w(), slow);
  if (err == Error::kOk) err = a.b(ready);
  if (err == Error::kOk) err = a.bind(busy);
  if (err == Error::kOk) err = a.clrex(imm(15));
  if (err == Error::kOk) err = a.b(slow);
  return err != Error::kOk ? err : a.bind(ready);
}

static asmjit::Error emit_a64_ram_store_unlock(A64EmitContext& c)
{
  if (!c.smp_memory) return asmjit::Error::kOk;
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = c.assembler;
  const Label done = a.new_label();
  Error err = a.cbz(RA::kScratch5, done);   // unmonitored store acquired no bucket
  // Bump the sequence while locked. STLRB orders the RAM and sequence writes
  // before releasing the bucket.
  if (err == Error::kOk) err = a.ldur(RA::kScratch0, a64::ptr(RA::kScratch5, -8));
  if (err == Error::kOk) err = a.add(RA::kScratch0, RA::kScratch0, imm(1));
  if (err == Error::kOk) err = a.stur(RA::kScratch0, a64::ptr(RA::kScratch5, -8));
  // The bundled AsmJit's stlrb() alias selects STLLRB; request baseline STLRB
  // explicitly so this release works on ARMv8.0 hosts too.
  if (err == Error::kOk)
    err = a.emit(a64::Inst::kIdStlrb, a64::wzr, a64::ptr(RA::kScratch5));
  return err != Error::kOk ? err : a.bind(done);
}
#endif

static A64OpEmitReceipt emit_a64_mem_load(A64EmitContext& context,
                                          const A64DecodedOp& op, uint32_t index)
{
  [[maybe_unused]] typedef CJitEngine::RegAlloc RA;
  using namespace asmjit;

  const A64GprRoute wa =
      a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
  if (wa.kind == A64GprRouteKind::kInvalid || wa.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wa.kind == A64GprRouteKind::kDiscard)   // LDx to R31 is a NOP (x64 parity)
    return a64_completed_op_receipt(op);

  const bool force_align = op.opcode == 0x0b;   // LDQ_U
  [[maybe_unused]] a64::Assembler& a = context.assembler;
#ifdef JIT_VERIFY
  Error err = emit_a64_mem_va(context, op, force_align);
  if (err == Error::kOk) err = emit_a64_int_mem_helper_seq(context, op, index, false);
#else
  if (context.cold == nullptr) return {Error::kInvalidState, op.kind};
  A64ColdStub stub{};
  stub.slow = a.new_label();
  stub.join = a.new_label();
  stub.op = op;
  stub.index = index;
  stub.kind = A64ColdStub::kLoad;
  stub.rederive = op.ra != op.rb;   // a load replacing its own base can't feed the next VA

  const int size_bits = a64_int_mem_size_bits(op.opcode);
  Error err = emit_a64_dpc_enter(context, op, size_bits, force_align, false, stub.slow);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  const a64::Gp dst = wa.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wa.host)) : RA::kScratch2;
  switch (size_bits) {   // LDL sign-extends; the BWX forms zero-extend
    case 64: err = a.ldr(dst, a64::ptr(RA::kScratch3, RA::kScratch4)); break;
    case 32: err = a.ldrsw(dst, a64::ptr(RA::kScratch3, RA::kScratch4)); break;
    case 16: err = a.ldrh(dst.w(), a64::ptr(RA::kScratch3, RA::kScratch4)); break;
    default: err = a.ldrb(dst.w(), a64::ptr(RA::kScratch3, RA::kScratch4)); break;
  }
  if (err == Error::kOk && wa.kind == A64GprRouteKind::kMemory)
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
  if (err == Error::kOk) err = a.bind(stub.join);
  if (err == Error::kOk) {
    context.cold->push_back(stub);
    context.regs.dpc = {stub.rederive, false, force_align,
                        static_cast<uint8_t>(op.rb),
                        static_cast<int32_t>(static_cast<int16_t>(op.ins & 0xffffu))};
  }
#endif
  // Both shapes leave an unpinned Ra in kScratch2 (fast path and stub alike).
  if (err == Error::kOk && wa.kind == A64GprRouteKind::kMemory)
    context.regs.fwd_reg = static_cast<int8_t>(op.ra);
  return a64_completed_op_receipt(op, err);
}

static A64OpEmitReceipt emit_a64_mem_store(A64EmitContext& context,
                                           const A64DecodedOp& op, uint32_t index)
{
  [[maybe_unused]] typedef CJitEngine::RegAlloc RA;
  using namespace asmjit;

  const bool force_align = op.opcode == 0x0f;   // STQ_U
  [[maybe_unused]] a64::Assembler& a = context.assembler;
#ifdef JIT_VERIFY
  Error err = emit_a64_mem_va(context, op, force_align);
  if (err == Error::kOk) err = emit_a64_int_mem_helper_seq(context, op, index, true);
#else
  if (context.cold == nullptr) return {Error::kInvalidState, op.kind};
  A64ColdStub stub{};
  stub.slow = a.new_label();
  stub.join = a.new_label();
  stub.op = op;
  stub.index = index;
  stub.kind = A64ColdStub::kStore;
  stub.rederive = true;
  {   // a forward survives this store only if the value doesn't pass through x11
    const A64GprRoute v =
        a64_guest_gpr_read_route(context.regs, op.ra, context.pal_shadow);
    if (v.kind != A64GprRouteKind::kMemory || op.ra == context.prev_fwd)
      stub.restore_fwd = static_cast<int8_t>(context.prev_fwd);   // helper clobbers x11
  }

  const int size_bits = a64_int_mem_size_bits(op.opcode);
  const Label watched = a.new_label();
  Error err = emit_a64_dpc_enter(context, op, size_bits, force_align, true,
                                 stub.slow, context.smp_memory ? &watched : nullptr);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  // Store value: pin, xzr for R31, or a reload from the regs[] slot.
  const A64GprRoute ra =
      a64_guest_gpr_read_route(context.regs, op.ra, context.pal_shadow);
  const auto emit_store = [&]() -> Error {
    a64::Gp val = RA::kScratch2;
    switch (ra.kind) {
      case A64GprRouteKind::kZero:   val = a64::xzr; break;
      case A64GprRouteKind::kPinned: val = a64::x(static_cast<uint32_t>(ra.host)); break;
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(op.ra)) break;   // already in kScratch2
        {
          const Error load_err = a.ldr(RA::kScratch2, a64::ptr(RA::kRegs,
              static_cast<int32_t>(ra.slot * sizeof(uint64_t))));
          if (load_err != Error::kOk) return load_err;
        }
        break;
      default: return Error::kInvalidArgument;
    }
    switch (size_bits) {
      case 64: return a.str(val, a64::ptr(RA::kScratch3, RA::kScratch4));
      case 32: return a.str(val.w(), a64::ptr(RA::kScratch3, RA::kScratch4));
      case 16: return a.strh(val.w(), a64::ptr(RA::kScratch3, RA::kScratch4));
      default: return a.strb(val.w(), a64::ptr(RA::kScratch3, RA::kScratch4));
    }
  };
  // Plain page tags skip the watch lookup and lock. First LL marks the page
  // only after draining active CPUs.
  err = emit_store();
  if (context.smp_memory) {
    if (err == Error::kOk) err = a.b(stub.join);
    if (err == Error::kOk) err = a.bind(watched);
    if (err == Error::kOk) err = emit_a64_ram_store_lock(context, stub.slow, false);
    if (err == Error::kOk) err = emit_store();
    if (err == Error::kOk) err = emit_a64_ram_store_unlock(context);
  }
  if (err == Error::kOk) err = a.bind(stub.join);
  if (err == Error::kOk) {
    context.cold->push_back(stub);
    context.regs.dpc = {true, true, force_align, static_cast<uint8_t>(op.rb),
                        static_cast<int32_t>(static_cast<int16_t>(op.ins & 0xffffu))};
  }
#endif
  return a64_completed_op_receipt(op, err);
}

// JMP/JSR/RET + HW_RET terminators.
static A64OpEmitReceipt emit_a64_jmp_indirect(A64EmitContext& context,
                                              const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;

  const A64GprRoute rb =
      a64_guest_gpr_read_route(context.regs, op.rb, context.pal_shadow);
  a64::Gp src = RA::kScratch0;
  switch (rb.kind) {
    case A64GprRouteKind::kZero:   src = a64::xzr; break;
    case A64GprRouteKind::kPinned: src = a64::x(static_cast<uint32_t>(rb.host)); break;
    case A64GprRouteKind::kMemory:
      if (context.prev_fwd == static_cast<int>(op.rb)) { src = RA::kScratch2; break; }
      err = a.ldr(RA::kScratch0, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(rb.slot * sizeof(uint64_t))));
      break;
    default:
      return {Error::kInvalidArgument, op.kind};
  }
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (op.opcode == 0x1e) {   // HW_RET: the family selects the final target, including its mode bit.
    const auto descriptor = alpha_pal_return_descriptor(context.profile.pal, op.ins);
    if (!alpha_pal_return_compilable(descriptor)) return {Error::kInvalidArgument, op.kind};
    if (src.id() == a64::xzr.id() || descriptor.target_mask == 0
        || descriptor.target_bits == ~uint64_t(0)) {
      err = emit_a64_mov_u64(a, RA::kNextPc, descriptor.target_bits);
      return a64_completed_op_receipt(op, err);
    }
    if (descriptor.target_mask == ~uint64_t(0)) {
      err = a.mov(RA::kNextPc, src);
    } else if (arm::Utils::is_logical_imm(descriptor.target_mask, 64)) {
      err = a.and_(RA::kNextPc, src, imm(descriptor.target_mask));
    } else {
      err = emit_a64_mov_u64(a, RA::kScratch1, descriptor.target_mask);
      if (err == Error::kOk) err = a.and_(RA::kNextPc, src, RA::kScratch1);
    }
    if (err == Error::kOk && descriptor.target_bits != 0) {
      if (arm::Utils::is_logical_imm(descriptor.target_bits, 64)) {
        err = a.orr(RA::kNextPc, RA::kNextPc, imm(descriptor.target_bits));
      } else {
        err = emit_a64_mov_u64(a, RA::kScratch1, descriptor.target_bits);
        if (err == Error::kOk) err = a.orr(RA::kNextPc, RA::kNextPc, RA::kScratch1);
      }
    }
    return a64_completed_op_receipt(op, err);
  }

  err = a.and_(RA::kNextPc, src, imm(~uint64_t(3)));
  const uint64_t mode = context.start_pc & 3u;
  if (err == Error::kOk && mode != 0)
    err = a.orr(RA::kNextPc, RA::kNextPc, imm(mode));
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const A64GprRoute wa =
      a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
  if (wa.kind == A64GprRouteKind::kInvalid || wa.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wa.kind != A64GprRouteKind::kDiscard) {
    const uint64_t ret =
        a64_advance_pc(context.start_pc, index + 1) & ~uint64_t(3);
    if (wa.kind == A64GprRouteKind::kPinned) {
      err = emit_a64_mov_u64(a, a64::x(static_cast<uint32_t>(wa.host)), ret);
    } else {
      err = emit_a64_mov_u64(a, RA::kScratch0, ret);
      if (err == Error::kOk)
        err = a.str(RA::kScratch0, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
    }
  }
  return a64_completed_op_receipt(op, err);
}

// CMOVxx
static A64OpEmitReceipt emit_a64_intl_cmov(A64EmitContext& context,
                                           const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)   // Rc==31: architectural no-op
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;
  const uint32_t func = (op.ins >> 5) & 0x7fu;

  auto source = [&](uint32_t raw_reg, const a64::Gp& scratch) -> a64::Gp {
    const A64GprRoute route =
        a64_guest_gpr_read_route(context.regs, raw_reg, context.pal_shadow);
    switch (route.kind) {
      case A64GprRouteKind::kZero:   return a64::xzr;
      case A64GprRouteKind::kPinned: return a64::x(static_cast<uint32_t>(route.host));
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(raw_reg))
          return RA::kScratch2;   // forwarded: the previous op left it here
        err = a.ldr(scratch, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(route.slot * sizeof(uint64_t))));
        return scratch;
      default:
        err = Error::kInvalidArgument;
        return scratch;
    }
  };

  const A64GprRoute rra =
      a64_guest_gpr_read_route(context.regs, op.ra, context.pal_shadow);
  Label skip;
  bool have_skip = false;
  if (rra.kind == A64GprRouteKind::kZero) {
    // Condition on zero folds: CMOVEQ/GE/LE/LBC always move, the rest never.
    const bool taken = func == 0x24 || func == 0x46
                    || func == 0x64 || func == 0x16;
    if (!taken) return a64_completed_op_receipt(op);
  } else {
    const a64::Gp src = source(op.ra, RA::kScratch0);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    skip = a.new_label();
    have_skip = true;
    switch (func) {
      case 0x24: err = a.cbnz(src, skip); break;                  // CMOVEQ
      case 0x26: err = a.cbz(src, skip);  break;                  // CMOVNE
      case 0x14: err = a.tst(src, imm(1));                        // CMOVLBS
                 if (err == Error::kOk) err = a.b_eq(skip);
                 break;
      case 0x16: err = a.tst(src, imm(1));                        // CMOVLBC
                 if (err == Error::kOk) err = a.b_ne(skip);
                 break;
      case 0x44: err = a.cmp(src, imm(0));                        // CMOVLT
                 if (err == Error::kOk) err = a.b_ge(skip);
                 break;
      case 0x46: err = a.cmp(src, imm(0));                        // CMOVGE
                 if (err == Error::kOk) err = a.b_lt(skip);
                 break;
      case 0x64: err = a.cmp(src, imm(0));                        // CMOVLE
                 if (err == Error::kOk) err = a.b_gt(skip);
                 break;
      case 0x66: err = a.cmp(src, imm(0));                        // CMOVGT
                 if (err == Error::kOk) err = a.b_le(skip);
                 break;
      default:   return {Error::kInvalidInstruction, op.kind};
    }
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  }

  // taken: Rc = op2
  if (op.is_literal) {
    if (wc.kind == A64GprRouteKind::kPinned) {
      err = emit_a64_mov_u64(a, a64::x(static_cast<uint32_t>(wc.host)),
                             op.literal);
    } else if (op.literal == 0) {
      err = a.str(a64::xzr, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    } else {
      err = emit_a64_mov_u64(a, RA::kScratch1, op.literal);
      if (err == Error::kOk)
        err = a.str(RA::kScratch1, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    }
  } else {
    const a64::Gp op2 = source(op.rb, RA::kScratch1);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    if (wc.kind == A64GprRouteKind::kPinned) {
      const a64::Gp dst = a64::x(static_cast<uint32_t>(wc.host));
      if (dst.id() != op2.id()) err = a.mov(dst, op2);
    } else {
      err = a.str(op2, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    }
  }
  if (err == Error::kOk && have_skip) err = a.bind(skip);
  return a64_completed_op_receipt(op, err);
}

// AMASK & IMPLVER
static A64OpEmitReceipt emit_a64_intl_probe(A64EmitContext& context,
                                            const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;
  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;
  const uint64_t amask = context.profile.amask;

  if (((op.ins >> 5) & 0x7fu) == 0x6c) {
    err = emit_a64_mov_u64(a, dst, context.profile.implver); // IMPLVER
  } else if (op.is_literal) {
    err = emit_a64_mov_u64(a, dst, op.literal & ~amask);   // AMASK literal folds
  } else {
    const A64GprRoute rb =
        a64_guest_gpr_read_route(context.regs, op.rb, context.pal_shadow);
    if (rb.kind == A64GprRouteKind::kZero) {
      err = a.mov(dst, a64::xzr);
    } else if (rb.kind == A64GprRouteKind::kPinned
               || rb.kind == A64GprRouteKind::kMemory) {
      a64::Gp op2 = RA::kScratch0;
      if (rb.kind == A64GprRouteKind::kPinned)
        op2 = a64::x(static_cast<uint32_t>(rb.host));
      else if (context.prev_fwd == static_cast<int>(op.rb))
        op2 = RA::kScratch2;   // forwarded
      else
        err = a.ldr(RA::kScratch0, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(rb.slot * sizeof(uint64_t))));
      if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch1, ~amask);
      if (err == Error::kOk) err = a.and_(dst, op2, RA::kScratch1);
    } else {
      return {Error::kInvalidArgument, op.kind};
    }
  }
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

// INTS byte-manipulation (mirrors cpu_bwx.h / the x64 sequences), pos = (op2&7)*8:
//   EXTxL: (Ra >> pos) & mask          EXTxH: (Ra << ((64-pos)&63)) & mask
//   INSxL: (Ra & mask) << pos          INSxH: pos ? (Ra&mask) >> ((64-pos)&63) : 0
//   MSKxL: Ra & ~(mask << pos)         MSKxH: pos ? Ra & ~(mask >> ((64-pos)&63)) : Ra
static A64OpEmitReceipt emit_a64_ints_byte(A64EmitContext& context,
                                           const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)   // Rc==31: architectural no-op
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;
  const uint32_t func = (op.ins >> 5) & 0x7fu;
  const uint32_t nib = func & 0xfu;
  const bool is_ext = nib == 0x6 || nib == 0xa;
  const bool is_msk = nib == 0x2;
  const bool is_high = func >= 0x40;   // H funcs are 0x52+; L funcs end at 0x3b
  const int size = (func >> 4) & 3;
  const uint64_t mask = size == 0 ? 0xffull : size == 1 ? 0xffffull
                      : size == 2 ? 0xffffffffull : ~uint64_t(0);

  auto source = [&](uint32_t raw_reg, const a64::Gp& scratch) -> a64::Gp {
    const A64GprRoute route =
        a64_guest_gpr_read_route(context.regs, raw_reg, context.pal_shadow);
    switch (route.kind) {
      case A64GprRouteKind::kZero:   return a64::xzr;
      case A64GprRouteKind::kPinned: return a64::x(static_cast<uint32_t>(route.host));
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(raw_reg))
          return RA::kScratch2;   // forwarded: the previous op left it here
        err = a.ldr(scratch, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(route.slot * sizeof(uint64_t))));
        return scratch;
      default:
        err = Error::kInvalidArgument;
        return scratch;
    }
  };
  const a64::Gp op1 = source(op.ra, RA::kScratch0);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;
  auto ident = [&]() {   // dst = Ra
    return dst.id() == op1.id() ? Error::kOk : a.mov(dst, op1);
  };
  auto and_const = [&](const a64::Gp& src, uint64_t k) {   // dst = src & k
    if (k == 0) return a.mov(dst, a64::xzr);
    if (k == ~uint64_t(0)) return dst.id() == src.id() ? Error::kOk : a.mov(dst, src);
    if (arm::Utils::is_logical_imm(k, 64)) return a.and_(dst, src, imm(k));
    Error e = emit_a64_mov_u64(a, RA::kScratch1, k);
    return e != Error::kOk ? e : a.and_(dst, src, RA::kScratch1);
  };

  if (op.is_literal) {
    const uint32_t pos = (op.literal & 7u) * 8u;
    const uint32_t hsh = (64u - pos) & 63u;   // the H-form shift (pos==0 -> 0)
    if (is_ext) {
      if (is_high) {
        err = hsh ? a.lsl(dst, op1, imm(hsh)) : ident();
      } else {
        err = pos ? a.lsr(dst, op1, imm(pos)) : ident();
      }
      if (err == Error::kOk && size != 3) err = and_const(dst, mask);
    } else if (is_msk) {
      const uint64_t keep = is_high
          ? (pos ? ~(mask >> hsh) : ~uint64_t(0))   // MSKxH pos==0 keeps Ra
          : ~(mask << pos);
      err = and_const(op1, keep);
    } else {   // INS
      if (is_high && pos == 0) {
        err = a.mov(dst, a64::xzr);                 // INSxH pos==0 -> 0
      } else {
        err = size != 3 ? and_const(op1, mask) : ident();
        if (err == Error::kOk) {
          if (is_high)      err = a.lsr(dst, dst, imm(hsh));
          else if (pos)     err = a.lsl(dst, dst, imm(pos));
        }
      }
    }
  } else {
    const a64::Gp sel = source(op.rb, RA::kScratch1);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    err = a.and_(RA::kScratch3, sel, imm(7));           // pos (bytes); ZR-safe
    if (err == Error::kOk) err = a.lsl(RA::kScratch5, RA::kScratch3, imm(3));
    if (err == Error::kOk && (is_high))
      err = a.neg(RA::kScratch5, RA::kScratch5);        // LSxV mods by 64
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);

    if (is_ext) {
      err = is_high ? a.lsl(dst, op1, RA::kScratch5)
                    : a.lsr(dst, op1, RA::kScratch5);
      if (err == Error::kOk && size != 3) err = and_const(dst, mask);
    } else if (is_msk) {
      err = emit_a64_mov_u64(a, RA::kScratch1, mask);   // sel is consumed
      if (err == Error::kOk)
        err = is_high ? a.lsr(RA::kScratch1, RA::kScratch1, RA::kScratch5)
                      : a.lsl(RA::kScratch1, RA::kScratch1, RA::kScratch5);
      if (err == Error::kOk) err = (a.mvn)(RA::kScratch1, RA::kScratch1);
      if (err != Error::kOk) return a64_completed_op_receipt(op, err);
      if (is_high) {
        // MSKxH pos==0 keeps Ra: select via branch (op1 stays intact).
        err = a.and_(RA::kScratch1, op1, RA::kScratch1);
        const Label z = a.new_label(), done_l = a.new_label();
        if (err == Error::kOk) err = a.cbz(RA::kScratch3, z);
        if (err == Error::kOk) err = a.mov(dst, RA::kScratch1);
        if (err == Error::kOk) err = a.b(done_l);
        if (err == Error::kOk) err = a.bind(z);
        if (err == Error::kOk) err = ident();
        if (err == Error::kOk) err = a.bind(done_l);
      } else {
        err = a.and_(dst, op1, RA::kScratch1);
      }
    } else {   // INS
      err = size != 3 ? and_const(op1, mask) : ident();
      if (err == Error::kOk)
        err = is_high ? a.lsr(dst, dst, RA::kScratch5)
                      : a.lsl(dst, dst, RA::kScratch5);
      if (err == Error::kOk && is_high) {               // INSxH pos==0 -> 0
        const Label nz = a.new_label();
        err = a.cbnz(RA::kScratch3, nz);
        if (err == Error::kOk) err = a.mov(dst, a64::xzr);
        if (err == Error::kOk) err = a.bind(nz);
      }
    }
  }
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

// FPTI integer ops on op2: SEXTB/SEXTW = sxtb/sxth; CTLZ = clz (A64's zero-input = 64 matches Alpha, 
// unlike x86 BSR); CTTZ -> rbit+clz. Literals fold.
static A64OpEmitReceipt emit_a64_fpti_int(A64EmitContext& context,
                                          const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;
  const uint32_t func = (op.ins >> 5) & 0x7fu;
  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;

  if (op.is_literal) {
    const uint64_t v = op.literal;
    uint64_t r = 0;
    switch (func) {
      case 0x00: r = static_cast<uint64_t>(static_cast<int64_t>(
                     static_cast<int8_t>(static_cast<uint8_t>(v)))); break;
      case 0x01: r = v; break;   // an 8-bit literal is never word-negative
      case 0x32: { r = 64; for (int b = 63; b >= 0; --b)
                     if ((v >> b) & 1) { r = static_cast<uint64_t>(63 - b); break; }
                   break; }
      case 0x33: { r = 64; for (int b = 0; b < 64; ++b)
                     if ((v >> b) & 1) { r = static_cast<uint64_t>(b); break; }
                   break; }
      case 0x30: { r = 0; for (int b = 0; b < 8; ++b) r += (v >> b) & 1; break; }
      default: return {Error::kInvalidInstruction, op.kind};
    }
    err = emit_a64_mov_u64(a, dst, r);
  } else {
    const A64GprRoute rb =
        a64_guest_gpr_read_route(context.regs, op.rb, context.pal_shadow);
    a64::Gp src = RA::kScratch1;
    switch (rb.kind) {
      case A64GprRouteKind::kZero:   src = a64::xzr; break;
      case A64GprRouteKind::kPinned: src = a64::x(static_cast<uint32_t>(rb.host)); break;
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(op.rb)) { src = RA::kScratch2; break; }
        err = a.ldr(RA::kScratch1, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(rb.slot * sizeof(uint64_t))));
        break;
      default:
        return {Error::kInvalidArgument, op.kind};
    }
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    switch (func) {
      case 0x00: err = a.sxtb(dst, src.w()); break;
      case 0x01: err = a.sxth(dst, src.w()); break;
      case 0x32: err = a.clz(dst, src); break;
      case 0x33: err = a.rbit(dst, src);
                 if (err == Error::kOk) err = a.clz(dst, dst);
                 break;
      case 0x30:   // CTPOP: NEON per-byte count + across-vector add (v16 transient)
        err = a.fmov(a64::d16, src);
        if (err == Error::kOk) err = a.cnt(a64::v16.b8(), a64::v16.b8());
        if (err == Error::kOk) err = a.addv(a64::v16.b(), a64::v16.b8());
        if (err == Error::kOk) err = a.umov(dst.w(), a64::v16.b(0));
        break;
      default:   return {Error::kInvalidInstruction, op.kind};
    }
  }
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

// FTOIT/FTOIS
static A64OpEmitReceipt emit_a64_ftoi(A64EmitContext& context,
                                      const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  const uint32_t fmt = ((op.ins >> 5) & 0x7fu) == 0x78 ? 1u : 0u;
  Error err = emit_a64_helper_call(a, context.offsets, context.helpers,
      context.regs, context.pal_shadow, context.helpers.ftoi_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kImm32, op.ra},
       {A64CallArgKind::kImm32, fmt},
       {A64CallArgKind::kOut, 0}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const Label ok = a.new_label();
  err = a.cbz(a64::w0, ok);
  if (err == Error::kOk)
    err = emit_a64_mov_u64(a, RA::kScratch0,
                           a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.state_pc);
  if (err == Error::kOk && index != 0)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(ok);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;
  err = a.ldr(dst, a64_helper_out_mem());
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

// LDx_L
static A64OpEmitReceipt emit_a64_load_locked(A64EmitContext& context,
                                             const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wa =
      a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
  if (wa.kind != A64GprRouteKind::kPinned && wa.kind != A64GprRouteKind::kMemory)
    return {Error::kInvalidArgument, op.kind};   // classify guarantees Ra != 31

  a64::Assembler& a = context.assembler;
  const int size_bits = op.opcode == 0x2b ? 64 : 32;
  Error err = emit_a64_mem_va(context, op, false);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  err = emit_a64_mov_u64(a, RA::kScratch0,
                         a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0,
                                 context.offsets.state_current_pc);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  err = emit_a64_helper_call(a, context.offsets, context.helpers, context.regs,
      context.pal_shadow, context.helpers.read_locked_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kHost, RA::kScratch4.id()},
       {A64CallArgKind::kImm64, (static_cast<uint64_t>(op.ins) << 32)
                              | static_cast<uint32_t>(size_bits)},
       {A64CallArgKind::kOut, 0}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const Label ok = a.new_label(), retry = a.new_label();
  err = a.cbz(a64::w0, ok);
  if (err == Error::kOk) err = a.cmp(a64::w0, imm(2));
  if (err == Error::kOk) err = a.b_ne(retry);
  if (err == Error::kOk)   // fault delivered: PAL entry PC stands, count this op
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index + 1));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(retry);
  if (err == Error::kOk)
    err = emit_a64_mov_u64(a, RA::kScratch0,
                           a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.state_pc);
  if (err == Error::kOk && index != 0)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(ok);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const a64::Gp dst = wa.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wa.host)) : RA::kScratch2;
  err = a.ldr(dst, a64_helper_out_mem());
  if (err == Error::kOk && wa.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.ra);
  }
  return a64_completed_op_receipt(op, err);
}

// STx_C
static A64OpEmitReceipt emit_a64_store_cond(A64EmitContext& context,
                                            const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wa =
      a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
  if (wa.kind != A64GprRouteKind::kPinned && wa.kind != A64GprRouteKind::kMemory)
    return {Error::kInvalidArgument, op.kind};   // classify guarantees Ra != 31

  a64::Assembler& a = context.assembler;
  const int size_bits = op.opcode == 0x2f ? 64 : 32;
  Error err = emit_a64_mem_va(context, op, false);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

#ifndef JIT_VERIFY
  err = emit_a64_mov_u64(a, RA::kScratch0,
                         a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0,
                                 context.offsets.state_current_pc);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  const uint64_t descr = (static_cast<uint64_t>(op.ins) << 32)
                       | static_cast<uint32_t>(size_bits);
#else
  const uint64_t descr = static_cast<uint32_t>(size_bits);
#endif
  err = emit_a64_helper_call(a, context.offsets, context.helpers, context.regs,
      context.pal_shadow, context.helpers.stc_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kHost, RA::kScratch4.id()},
       {A64CallArgKind::kImm64, descr},
       {A64CallArgKind::kGuest, op.ra}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const Label nobail = a.new_label();
#ifndef JIT_VERIFY
  const Label retry = a.new_label();
  err = a.cmp(a64::x0, imm(0x200));
  if (err == Error::kOk) err = a.b_ne(retry);
  if (err == Error::kOk)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index + 1));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(retry);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
#endif
  err = a.tst(a64::x0, imm(0x100));
  if (err == Error::kOk) err = a.b_eq(nobail);
  if (err == Error::kOk)
    err = emit_a64_mov_u64(a, RA::kScratch0,
                           a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.state_pc);
  if (err == Error::kOk && index != 0)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(nobail);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (wa.kind == A64GprRouteKind::kPinned) {
    err = a.mov(a64::x(static_cast<uint32_t>(wa.host)), a64::x0);
  } else {   // route the result through the mirror so the next op can forward it (x64 parity)
    err = a.mov(RA::kScratch2, a64::x0);
    if (err == Error::kOk)
      err = a.str(RA::kScratch2, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.ra);
  }
  return a64_completed_op_receipt(op, err);
}

// INTA
static A64OpEmitReceipt emit_a64_inta(A64EmitContext& context,
                                      const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)   // Rc==31: architectural no-op
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;
  const uint32_t func = (op.ins >> 5) & 0x7fu;

  auto source = [&](uint32_t raw_reg, const a64::Gp& scratch) -> a64::Gp {
    const A64GprRoute route =
        a64_guest_gpr_read_route(context.regs, raw_reg, context.pal_shadow);
    switch (route.kind) {
      case A64GprRouteKind::kZero:   return a64::xzr;
      case A64GprRouteKind::kPinned: return a64::x(static_cast<uint32_t>(route.host));
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(raw_reg))
          return RA::kScratch2;   // forwarded: the previous op left it here
        err = a.ldr(scratch, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(route.slot * sizeof(uint64_t))));
        return scratch;
      default:
        err = Error::kInvalidArgument;
        return scratch;
    }
  };
  const a64::Gp op1 = source(op.ra, RA::kScratch0);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;

  if (func == 0x0f) {                            // CMPBGE: per-byte unsigned Ra >= op2
    if (op.is_literal) {                         // literal lives in byte 0; 1-7 are 0
      err = emit_a64_mov_u64(a, RA::kScratch1, op.literal);
    } else {
      const a64::Gp op2 = source(op.rb, RA::kScratch1);
      if (err != Error::kOk) return a64_completed_op_receipt(op, err);
      if (op2.id() != RA::kScratch1.id()) err = a.mov(RA::kScratch1, op2);
    }
    if (err == Error::kOk) err = a.fmov(a64::d16, op1);
    if (err == Error::kOk) err = a.fmov(a64::d17, RA::kScratch1);
    if (err == Error::kOk) err = a.cmhs(a64::v16.b8(), a64::v16.b8(), a64::v17.b8());
    if (err == Error::kOk)
      err = emit_a64_mov_u64(a, RA::kScratch1, 0x8040201008040201ull);
    if (err == Error::kOk) err = a.fmov(a64::d17, RA::kScratch1);
    if (err == Error::kOk) err = a.and_(a64::v16.b8(), a64::v16.b8(), a64::v17.b8());
    if (err == Error::kOk) err = a.addv(a64::v16.b(), a64::v16.b8());
    if (err == Error::kOk) err = a.umov(dst.w(), a64::v16.b(0));
  } else if (func == 0x2d || func == 0x4d || func == 0x6d
          || func == 0x1d || func == 0x3d) {     // compares -> cmp + cset
    const arm::CondCode cond =
        func == 0x2d ? arm::CondCode::kEQ
      : func == 0x4d ? arm::CondCode::kLT
      : func == 0x6d ? arm::CondCode::kLE
      : func == 0x1d ? arm::CondCode::kLO : arm::CondCode::kLS;
    bool folded = false;
    if (op.is_literal) {
      if (op1.id() == a64::xzr.id()) {           // 0 cmp lit folds (lit is 0..255)
        const uint64_t r =
            func == 0x2d ? uint64_t(op.literal == 0)      // 0 == lit
          : func == 0x6d || func == 0x3d ? uint64_t(1)    // 0 <= lit always (lit >= 0)
          : uint64_t(op.literal != 0);                    // 0 < lit (signed or unsigned)
        err = emit_a64_mov_u64(a, dst, r);       // fall through: an unpinned Rc still needs the slot store
        folded = true;
      } else {
        err = a.cmp(op1, imm(op.literal));
      }
    } else {
      const a64::Gp op2 = source(op.rb, RA::kScratch1);
      if (err != Error::kOk) return a64_completed_op_receipt(op, err);
      err = a.cmp(op1, op2);                     // register form: ZR-safe both sides
    }
    if (err == Error::kOk && !folded)
      err = a.cset(dst, imm(static_cast<uint32_t>(cond)));
  } else {                                       // add/sub, optionally scaled, Q or L
    const bool is_sub = func == 0x29 || func == 0x09 || func == 0x2b
                     || func == 0x3b || func == 0x0b || func == 0x1b;
    const bool is_long = (func & 0x20u) == 0;
    const uint32_t sh =
        (func == 0x22 || func == 0x2b || func == 0x02 || func == 0x0b) ? 2
      : (func == 0x32 || func == 0x3b || func == 0x12 || func == 0x1b) ? 3 : 0;
    const bool shifted_add = sh != 0 && !is_sub && !op.is_literal;
    a64::Gp val1 = op1;
    if (sh != 0 && !shifted_add) {                // lsl is bitfield-class: ZR-safe
      err = a.lsl(RA::kScratch3, op1, imm(sh));
      val1 = RA::kScratch3;
    }
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    if (op.is_literal) {
      if (val1.id() == a64::xzr.id()) {          // fold: imm forms can't base on xzr
        err = emit_a64_mov_u64(a, dst,
            is_sub ? uint64_t(0) - op.literal : op.literal);
      } else {
        err = is_sub ? a.sub(dst, val1, imm(op.literal))
                     : a.add(dst, val1, imm(op.literal));
      }
    } else {
      const a64::Gp op2 = source(op.rb, RA::kScratch1);
      if (err != Error::kOk) return a64_completed_op_receipt(op, err);
      err = shifted_add ? a.add(dst, op2, op1, a64::lsl(sh))
          : is_sub ? a.sub(dst, val1, op2) : a.add(dst, val1, op2);
    }
    if (err == Error::kOk && is_long) err = a.sxtw(dst, dst.w());
  }
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

// MISC: TRAPB/EXCB/MB/WMB
static A64OpEmitReceipt emit_a64_misc(A64EmitContext& context,
                                      const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const uint32_t fn = op.ins & 0xffffu;

  switch (fn) {
    case 0x4000:
      return a64_completed_op_receipt(op, a.dmb(imm(0xB)));   // MB: ish
    case 0x4400:
      return a64_completed_op_receipt(op, a.dmb(imm(0xA)));   // WMB: ishst
    case 0x0000: case 0x0400:                                 // TRAPB, EXCB: traps are precise
    case 0x8000: case 0xA000: case 0xE800: case 0xF800: case 0xFC00:
      return a64_completed_op_receipt(op);                    // no code
    default:
      break;
  }

  const uint32_t sel = fn == 0xC000 ? 0u : fn == 0xE000 ? 1u : 2u;
  Error err = emit_a64_helper_call(a, context.offsets, context.helpers,
      context.regs, context.pal_shadow, context.helpers.misc_helper,
      {{A64CallArgKind::kCpu, 0}, {A64CallArgKind::kImm32, sel}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const A64GprRoute wa =
      a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
  if (wa.kind == A64GprRouteKind::kPinned)
    err = a.mov(a64::x(static_cast<uint32_t>(wa.host)), a64::x0);
  else if (wa.kind == A64GprRouteKind::kMemory)
    err = a.str(a64::x0, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
  else if (wa.kind != A64GprRouteKind::kDiscard)   // RC/RS Ra==31: flag-only
    return {Error::kInvalidArgument, op.kind};
  return a64_completed_op_receipt(op, err);
}

// CALL_PAL
static A64OpEmitReceipt emit_a64_call_pal(A64EmitContext& context,
                                          const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;

  const uint32_t fn = op.ins & 0x1fffffffu;
  const uint64_t cpc = a64_advance_pc(context.start_pc, index);
  const AlphaPALCallEntry entry = context.profile.pal.call_pal_entry(
      fn, cpc, a64_advance_pc(context.start_pc, index + 1));
  uint32_t allowed_modes = 0;
  for (uint32_t mode = 0; mode < 4; ++mode)
    if (context.profile.pal.call_pal_valid(fn, mode)) allowed_modes |= 1u << mode;
  Error err = Error::kOk;

  const Label do_vector = a.new_label();
  if (allowed_modes != 0xfu) {   // Only modes admitted by the family may vector.
    if (allowed_modes)
      err = emit_a64_load_cpu_u8(a, RA::kScratch3.w(), context.offsets.state_cm, false);
    for (uint32_t mode = 0; mode < 4; ++mode) {
      if (!(allowed_modes & (1u << mode))) continue;
      if (mode == 0) {
        if (err == Error::kOk) err = a.cbz(RA::kScratch3.w(), do_vector);
      } else {
        if (err == Error::kOk) err = a.cmp(RA::kScratch3.w(), imm(mode));
        if (err == Error::kOk) err = a.b_eq(do_vector);
      }
    }
    if (err == Error::kOk)
      err = emit_a64_helper_call(a, context.offsets, context.helpers, context.regs,
          context.pal_shadow, context.helpers.opcdec_helper,
          {{A64CallArgKind::kCpu, 0}, {A64CallArgKind::kImm64, cpc}});
    if (err == Error::kOk)   // helper already wrote state.pc; count the CALL_PAL
      err = a.add(RA::kChainCount, RA::kChainCount, imm(index + 1));
    if (err == Error::kOk) err = a.b(context.done);
  }
  if (err == Error::kOk) err = a.bind(do_vector);
  if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch0, entry.saved_pc);
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.exc_addr);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (entry.write_link) {
    // The family selects the link slots; LIVE SDE chooses the runtime destination.
    err = emit_a64_load_cpu_u8(a, RA::kScratch3.w(), context.offsets.sde, false);
    if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch5, entry.return_pc);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    const Label shadow = a.new_label(), linked = a.new_label();
    err = a.cbnz(RA::kScratch3.w(), shadow);
    const A64GprRoute link =
        a64_guest_gpr_write_route(context.regs, entry.link_register, false);
    if (err == Error::kOk && link.kind == A64GprRouteKind::kPinned)
      err = a.mov(a64::x(static_cast<uint32_t>(link.host)), RA::kScratch5);
    if (err == Error::kOk)
      err = a.str(RA::kScratch5, a64::ptr(RA::kRegs, entry.link_register * 8));
    if (err == Error::kOk) err = a.b(linked);
    if (err == Error::kOk) err = a.bind(shadow);
    if (entry.shadow_link_register < 32) {
      const A64GprRoute shadow_link =
          a64_guest_gpr_write_route(context.regs, entry.shadow_link_register, false);
      if (err == Error::kOk && shadow_link.kind == A64GprRouteKind::kPinned)
        err = a.mov(a64::x(static_cast<uint32_t>(shadow_link.host)), RA::kScratch5);
    }
    if (err == Error::kOk)
      err = a.str(RA::kScratch5, a64::ptr(RA::kRegs, entry.shadow_link_register * 8));
    if (err == Error::kOk) err = a.bind(linked);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  }

  err = emit_a64_load_cpu_u64(a, RA::kScratch1, context.offsets.pal_base);
  if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch2, entry.offset);
  if (err == Error::kOk)
    err = a.orr(RA::kNextPc, RA::kScratch1, RA::kScratch2);
  return a64_completed_op_receipt(op, err);
}

// HW_MTPR: jit_hw_mtpr(cpu, instruction, Rb)
static A64OpEmitReceipt emit_a64_hw_mtpr(A64EmitContext& context,
                                         const A64DecodedOp& op)
{
  using namespace asmjit;
  if (context.profile.ipr_write.describe(op.ins) == AlphaIPRWriteKind::Noop)
    return a64_completed_op_receipt(op);   // no-op IPRs

  const Error err = emit_a64_helper_call(context.assembler, context.offsets,
      context.helpers, context.regs, context.pal_shadow,
      context.helpers.hw_mtpr_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kImm32, op.ins},
       {A64CallArgKind::kGuestOrZero, op.rb}});
  return a64_completed_op_receipt(op, err);
}

// HW_LD
static A64OpEmitReceipt emit_a64_hw_ld(A64EmitContext& context,
                                       const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;

  const auto load = alpha_decode_hw_load(context.profile.hw_load, op.ins);
  if (!alpha_hw_load_compilable(load)) return {Error::kInvalidArgument, op.kind};
  const bool virt = load.address == AlphaHWLoadAddress::Virtual;
  const int size_bits = static_cast<int>(load.size_bits);
  if (!virt && op.ra == 31 && !load.discard_safe)
    return {Error::kInvalidArgument, op.kind};
  if (!virt && op.ra == 31 && load.discard_safe)
    return a64_completed_op_receipt(op);   // physical R31: NOP (virtual forms probe)

  Error err = emit_a64_mem_va_displacement(context, op, false, load.displacement);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

#ifndef JIT_VERIFY
  Label physical_done;
  if (!virt) {
    const A64GprRoute wa =
        a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
    if (wa.kind != A64GprRouteKind::kPinned && wa.kind != A64GprRouteKind::kMemory)
      return {Error::kInvalidArgument, op.kind};
    const a64::Gp dst = wa.kind == A64GprRouteKind::kPinned
        ? a64::x(static_cast<uint32_t>(wa.host)) : RA::kScratch2;
    const Label physical_slow = a.new_label();
    physical_done = a.new_label();
    // Check the original PA unsigned before rounding down. Page-aligned DRAM
    // contains the whole aligned access; MMIO keeps its original helper input.
    err = emit_a64_load_cpu_u64(a, RA::kScratch0, context.offsets.dram_size);
    if (err == Error::kOk) err = a.cmp(RA::kScratch4, RA::kScratch0);
    if (err == Error::kOk) err = a.b_hs(physical_slow);
    if (err == Error::kOk)
      err = a.and_(RA::kScratch4, RA::kScratch4,
                    imm(~(static_cast<uint64_t>(size_bits / 8) - 1)));
    if (err == Error::kOk)
      err = emit_a64_load_cpu_u64(a, RA::kScratch3, context.offsets.dram_ptr);
    if (err == Error::kOk)
      err = size_bits == 32 ? a.ldrsw(dst, a64::ptr(RA::kScratch3, RA::kScratch4))
                            : a.ldr(dst, a64::ptr(RA::kScratch3, RA::kScratch4));
    if (err == Error::kOk && wa.kind == A64GprRouteKind::kMemory)
      err = a.str(dst, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
    if (err == Error::kOk) err = a.b(physical_done);
    if (err == Error::kOk) err = a.bind(physical_slow);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  }
#endif

  if (virt) {
    err = emit_a64_mov_u64(a, RA::kScratch0,
                           a64_advance_pc(context.start_pc, index));
    if (err == Error::kOk)
      err = emit_a64_store_cpu_u64(a, RA::kScratch0,
                                   context.offsets.state_current_pc);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    const uint32_t descr = alpha_hw_load_helper_descriptor(load);
    err = emit_a64_helper_call(a, context.offsets, context.helpers, context.regs,
        context.pal_shadow, context.helpers.read_vpte_helper,
        {{A64CallArgKind::kCpu, 0},
         {A64CallArgKind::kHost, RA::kScratch4.id()},
         {A64CallArgKind::kImm64, (static_cast<uint64_t>(op.ins) << 32) | descr},
         {A64CallArgKind::kOut, 0}});
  } else {
    err = emit_a64_helper_call(a, context.offsets, context.helpers, context.regs,
        context.pal_shadow, context.helpers.hw_ld_helper,
        {{A64CallArgKind::kCpu, 0},
         {A64CallArgKind::kHost, RA::kScratch4.id()},
         {A64CallArgKind::kImm32, static_cast<uint32_t>(size_bits)},
         {A64CallArgKind::kOut, 0}});
  }
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const Label ok = a.new_label(), retry = a.new_label();
  err = a.cbz(a64::w0, ok);
  if (virt) {
    if (err == Error::kOk) err = a.cmp(a64::w0, imm(2));
    if (err == Error::kOk) err = a.b_ne(retry);
    if (err == Error::kOk)   // fault delivered: PAL entry PC stands, count this op
      err = a.add(RA::kChainCount, RA::kChainCount, imm(index + 1));
    if (err == Error::kOk) err = a.b(context.done);
  }
  if (err == Error::kOk) err = a.bind(retry);
  if (err == Error::kOk)
    err = emit_a64_mov_u64(a, RA::kScratch0,
                           a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.state_pc);
  if (err == Error::kOk && index != 0)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(ok);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (op.ra != 31) {
    const A64GprRoute wa =
        a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
    if (wa.kind != A64GprRouteKind::kPinned && wa.kind != A64GprRouteKind::kMemory)
      return {Error::kInvalidArgument, op.kind};
    const a64::Gp dst = wa.kind == A64GprRouteKind::kPinned
        ? a64::x(static_cast<uint32_t>(wa.host)) : RA::kScratch2;
    err = size_bits == 32 ? a.ldrsw(dst, a64_helper_out_mem())
                          : a.ldr(dst, a64_helper_out_mem());
    if (err == Error::kOk && wa.kind == A64GprRouteKind::kMemory)
      err = a.str(dst, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
  }
#ifndef JIT_VERIFY
  if (err == Error::kOk && !virt) err = a.bind(physical_done);
#endif
  return a64_completed_op_receipt(op, err);
}

// HW_ST physical: inline DRAM, helper for MMIO retry and verification replay.
static A64OpEmitReceipt emit_a64_hw_st(A64EmitContext& context,
                                       const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;

  const auto store = alpha_decode_hw_store(context.profile.hw_store, op.ins);
  if (!alpha_hw_store_compilable(store)) return {Error::kInvalidArgument, op.kind};
  const int size_bits = static_cast<int>(store.size_bits);
  Error err = emit_a64_mem_va_displacement(context, op, false, store.displacement);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

#ifndef JIT_VERIFY
  const Label physical_slow = a.new_label(), physical_done = a.new_label();
  if (err == Error::kOk) err = emit_a64_load_cpu_u64(a, RA::kScratch0, context.offsets.dram_size);
  if (err == Error::kOk) err = a.cmp(RA::kScratch4, RA::kScratch0);
  if (err == Error::kOk) err = a.b_hs(physical_slow);
  if (err == Error::kOk)
    err = a.and_(RA::kScratch4, RA::kScratch4,
                  imm(~(static_cast<uint64_t>(size_bits / 8) - 1)));
  if (err == Error::kOk)
    err = emit_a64_load_cpu_u64(a, RA::kScratch3, context.offsets.dram_ptr);
  if (err == Error::kOk) err = emit_a64_ram_store_lock(context, physical_slow, true);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const A64GprRoute ra =
      a64_guest_gpr_read_route(context.regs, op.ra, context.pal_shadow);
  a64::Gp value = RA::kScratch2;
  switch (ra.kind) {
    case A64GprRouteKind::kZero:   value = a64::xzr; break;
    case A64GprRouteKind::kPinned: value = a64::x(static_cast<uint32_t>(ra.host)); break;
    case A64GprRouteKind::kMemory:
      err = a.ldr(value, a64::ptr(RA::kRegs,
                  static_cast<int32_t>(ra.slot * sizeof(uint64_t))));
      break;
    default:
      return {Error::kInvalidArgument, op.kind};
  }
  if (err == Error::kOk)
    err = size_bits == 32 ? a.str(value.w(), a64::ptr(RA::kScratch3, RA::kScratch4))
                          : a.str(value, a64::ptr(RA::kScratch3, RA::kScratch4));
  if (err == Error::kOk) err = emit_a64_ram_store_unlock(context);
  if (err == Error::kOk) err = a.b(physical_done);
  if (err == Error::kOk) err = a.bind(physical_slow);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
#endif

  err = emit_a64_helper_call(a, context.offsets, context.helpers, context.regs,
      context.pal_shadow, context.helpers.hw_st_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kHost, RA::kScratch4.id()},
       {A64CallArgKind::kImm32, static_cast<uint32_t>(size_bits)},
       {A64CallArgKind::kGuestOrZero, op.ra}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const Label ok = a.new_label();
  err = a.cbz(a64::w0, ok);
  if (err == Error::kOk)
    err = emit_a64_mov_u64(a, RA::kScratch0,
                           a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.state_pc);
  if (err == Error::kOk && index != 0)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(ok);
#ifndef JIT_VERIFY
  if (err == Error::kOk) err = a.bind(physical_done);
#endif
  return a64_completed_op_receipt(op, err);
}

// INTM
static A64OpEmitReceipt emit_a64_intm(A64EmitContext& context,
                                      const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wc =
      a64_guest_gpr_write_route(context.regs, op.rc, context.pal_shadow);
  if (wc.kind == A64GprRouteKind::kInvalid || wc.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  if (wc.kind == A64GprRouteKind::kDiscard)   // Rc==31: architectural no-op
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
  Error err = Error::kOk;

  auto source = [&](uint32_t raw_reg, const a64::Gp& scratch) -> a64::Gp {
    const A64GprRoute route =
        a64_guest_gpr_read_route(context.regs, raw_reg, context.pal_shadow);
    switch (route.kind) {
      case A64GprRouteKind::kZero:   return a64::xzr;
      case A64GprRouteKind::kPinned: return a64::x(static_cast<uint32_t>(route.host));
      case A64GprRouteKind::kMemory:
        if (context.prev_fwd == static_cast<int>(raw_reg))
          return RA::kScratch2;   // forwarded: the previous op left it here
        err = a.ldr(scratch, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(route.slot * sizeof(uint64_t))));
        return scratch;
      default:
        err = Error::kInvalidArgument;
        return scratch;
    }
  };
  const a64::Gp op1 = source(op.ra, RA::kScratch0);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  a64::Gp op2 = RA::kScratch1;
  if (op.is_literal) {
    if (op.literal == 0) op2 = a64::xzr;
    else                 err = emit_a64_mov_u64(a, RA::kScratch1, op.literal);
  } else {
    op2 = source(op.rb, RA::kScratch1);
  }
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const a64::Gp dst = wc.kind == A64GprRouteKind::kPinned
      ? a64::x(static_cast<uint32_t>(wc.host)) : RA::kScratch2;
  switch ((op.ins >> 5) & 0x7fu) {
    case 0x20: err = a.mul(dst, op1, op2); break;                  // MULQ
    case 0x00: err = a.mul(dst, op1, op2);                         // MULL
               if (err == Error::kOk) err = a.sxtw(dst, dst.w());
               break;
    case 0x30: err = a.umulh(dst, op1, op2); break;                // UMULH
    default:   err = Error::kInvalidInstruction; break;
  }
  if (err == Error::kOk && wc.kind == A64GprRouteKind::kMemory) {
    err = a.str(dst, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wc.slot * sizeof(uint64_t))));
    if (err == Error::kOk) context.regs.fwd_reg = static_cast<int8_t>(op.rc);
  }
  return a64_completed_op_receipt(op, err);
}

static asmjit::Error emit_a64_load_f_bits(A64EmitContext& c,
                                          const asmjit::a64::Gp& x, uint32_t freg);
static asmjit::Error emit_a64_store_f_bits(A64EmitContext& c,
                                           const asmjit::a64::Gp& x, uint32_t freg);

// FP-memory helper sequence. Production carries the instruction in descr[63:32]
// so a fault can enter PAL without retrying the instruction in the interpreter.
static asmjit::Error emit_a64_fp_mem_helper_seq(A64EmitContext& context,
    const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const bool isload = op.opcode == 0x23 || op.opcode == 0x22
                   || op.opcode == 0x20 || op.opcode == 0x21;
  const uint32_t fmt = (op.opcode == 0x22 || op.opcode == 0x26) ? 1u
                     : (op.opcode == 0x20 || op.opcode == 0x24) ? 2u
                     : (op.opcode == 0x21 || op.opcode == 0x25) ? 3u : 0u;
  uint64_t descr = (fmt << 16)
                 | static_cast<uint32_t>((fmt == 1 || fmt == 2) ? 32 : 64);
  Error err = Error::kOk;
#ifndef JIT_VERIFY
  descr |= static_cast<uint64_t>(op.ins) << 32;
  err = emit_a64_mov_u64(a, RA::kScratch0,
                         a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0,
                                 context.offsets.state_current_pc);
  if (err != Error::kOk) return err;
#endif
  err = emit_a64_helper_call(a, context.offsets, context.helpers,
      context.regs, context.pal_shadow,
      isload ? context.helpers.fp_read_helper : context.helpers.fp_write_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kHost, RA::kScratch4.id()},
       {A64CallArgKind::kImm32, op.ra},
       {A64CallArgKind::kImm64, descr}});
  if (err != Error::kOk) return err;

  return emit_a64_mem_result_bail(context, index);
}

// FP memory. LDT/STT (raw T-format) get the DPC fast path
static A64OpEmitReceipt emit_a64_fp_mem(A64EmitContext& context,
                                        const A64DecodedOp& op, uint32_t index)
{
  [[maybe_unused]] typedef CJitEngine::RegAlloc RA;
  using namespace asmjit;
  [[maybe_unused]] a64::Assembler& a = context.assembler;

  const bool isload = op.opcode == 0x23 || op.opcode == 0x22
                   || op.opcode == 0x20 || op.opcode == 0x21;
  if (isload && op.ra == 31) return a64_completed_op_receipt(op);
  [[maybe_unused]] const bool israw = op.opcode == 0x23 || op.opcode == 0x27;   // LDT / STT

  Error err = emit_a64_mem_va(context, op, false);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
#ifndef JIT_VERIFY
  if (israw) {
    if (context.cold == nullptr) return {Error::kInvalidState, op.kind};
    A64ColdStub stub{};
    stub.slow = a.new_label();
    stub.join = a.new_label();
    stub.op = op;
    stub.index = index;
    stub.kind = A64ColdStub::kFpMem;
    const Label watched = a.new_label();

    err = emit_a64_load_cpu_u8(a, RA::kScratch3.w(), context.offsets.fpen, false);
    if (err == Error::kOk) err = a.cbz(RA::kScratch3.w(), stub.slow);
    if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch0, 0);
    if (err == Error::kOk)
      err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.exc_sum);
    if (err == Error::kOk)
      err = emit_a64_dpc_probe(context, 64, false, !isload, stub.slow,
          !isload && context.smp_memory ? &watched : nullptr);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
    if (isload) {
      err = a.ldr(RA::kScratch2, a64::ptr(RA::kScratch3, RA::kScratch4));
      if (err == Error::kOk)
        err = emit_a64_store_f_bits(context, RA::kScratch2, op.ra);
    } else {
      err = emit_a64_load_f_bits(context, RA::kScratch2, op.ra);
      if (err == Error::kOk)
        err = a.str(RA::kScratch2, a64::ptr(RA::kScratch3, RA::kScratch4));
      if (context.smp_memory) {
        if (err == Error::kOk) err = a.b(stub.join);
        if (err == Error::kOk) err = a.bind(watched);
        // Load before taking the bucket: large CPU-field offsets may use x14.
        if (err == Error::kOk) err = emit_a64_load_f_bits(context, RA::kScratch2, op.ra);
        if (err == Error::kOk) err = emit_a64_ram_store_lock(context, stub.slow, false);
        if (err == Error::kOk)
          err = a.str(RA::kScratch2, a64::ptr(RA::kScratch3, RA::kScratch4));
        if (err == Error::kOk) err = emit_a64_ram_store_unlock(context);
      }
    }
    if (err == Error::kOk) err = a.bind(stub.join);
    if (err == Error::kOk) context.cold->push_back(stub);
    return a64_completed_op_receipt(op, err);
  }
#endif
  err = emit_a64_fp_mem_helper_seq(context, op, index);
  return a64_completed_op_receipt(op, err);
}

// Shared retry bail.
static asmjit::Error emit_a64_retry_bail(A64EmitContext& context, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  Error err = emit_a64_mov_u64(a, RA::kScratch0,
                               a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.state_pc);
  if (err == Error::kOk && index != 0)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index));
  return err != Error::kOk ? err : a.b(context.done);
}

// FLTL non-arithmetic: jit_fltl(cpu, ins) does all effects in f[]/fpcr
static A64OpEmitReceipt emit_a64_fltl(A64EmitContext& context,
                                      const A64DecodedOp& op, uint32_t index)
{
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  Error err = emit_a64_helper_call(a, context.offsets, context.helpers,
      context.regs, context.pal_shadow, context.helpers.fltl_helper,
      {{A64CallArgKind::kCpu, 0}, {A64CallArgKind::kImm32, op.ins}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  const Label ok = a.new_label();
  err = a.cbz(a64::w0, ok);
  if (err == Error::kOk) err = emit_a64_retry_bail(context, index);
  if (err == Error::kOk) err = a.bind(ok);
  return a64_completed_op_receipt(op, err);
}

// FLTV VAX arith
static A64OpEmitReceipt emit_a64_fltv(A64EmitContext& context,
                                      const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  Error err = emit_a64_mov_u64(a, RA::kScratch0,
                               a64_advance_pc(context.start_pc, index));
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0,
                                 context.offsets.state_current_pc);
  if (err == Error::kOk)
    err = emit_a64_helper_call(a, context.offsets, context.helpers,
        context.regs, context.pal_shadow, context.helpers.fltv_helper,
        {{A64CallArgKind::kCpu, 0}, {A64CallArgKind::kImm32, op.ins}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  const Label ok = a.new_label(), retry = a.new_label();
  err = a.cbz(a64::w0, ok);
  if (err == Error::kOk) err = a.cmp(a64::w0, imm(2));
  if (err == Error::kOk) err = a.b_ne(retry);
  if (err == Error::kOk)
    err = a.add(RA::kChainCount, RA::kChainCount, imm(index + 1));
  if (err == Error::kOk) err = a.b(context.done);
  if (err == Error::kOk) err = a.bind(retry);
  if (err == Error::kOk) err = emit_a64_retry_bail(context, index);
  if (err == Error::kOk) err = a.bind(ok);
  return a64_completed_op_receipt(op, err);
}

// ITOFS/ITOFF/ITOFT: jit_itof(cpu, fc, Ra-or-zero, fmt 0=T/1=S/2=F); 1 = FEN retry.
static A64OpEmitReceipt emit_a64_itof(A64EmitContext& context,
                                      const A64DecodedOp& op, uint32_t index)
{
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const uint32_t f14 = (op.ins >> 5) & 0x7ffu;
  const uint32_t fmt = f14 == 0x004 ? 1u : f14 == 0x014 ? 2u : 0u;
  Error err = emit_a64_helper_call(a, context.offsets, context.helpers,
      context.regs, context.pal_shadow, context.helpers.itof_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kImm32, op.rc},
       {A64CallArgKind::kGuestOrZero, op.ra},
       {A64CallArgKind::kImm32, fmt}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  const Label ok = a.new_label();
  err = a.cbz(a64::w0, ok);
  if (err == Error::kOk) err = emit_a64_retry_bail(context, index);
  if (err == Error::kOk) err = a.bind(ok);
  return a64_completed_op_receipt(op, err);
}

// FP branches
static A64OpEmitReceipt emit_a64_branch_fp(A64EmitContext& context,
                                           const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;

  const uint64_t fall = a64_advance_pc(context.start_pc, index + 1);
  const uint64_t target = a64_branch_target(fall, op.ins);

  const Label fen_ok = a.new_label();
  Error err = emit_a64_load_cpu_u8(a, RA::kScratch3.w(),
                                   context.offsets.fpen, false);
  if (err == Error::kOk) err = a.cbnz(RA::kScratch3.w(), fen_ok);
  if (err == Error::kOk) err = emit_a64_retry_bail(context, index);
  if (err == Error::kOk) err = a.bind(fen_ok);
  if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch0, 0);
  if (err == Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.exc_sum);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (op.ra == 31) {   // f[31] = 0 -> s = 0: the condition folds
    const bool taken = op.opcode == 0x31 || op.opcode == 0x33
                    || op.opcode == 0x36;   // FBEQ / FBLE / FBGE on zero
    if (taken) err = emit_a64_mov_u64(a, RA::kNextPc, target);
    return a64_completed_op_receipt(op, err);
  }

  err = emit_a64_load_f_bits(context, RA::kScratch1, op.ra);
  if (err == Error::kOk) err = a.asr(RA::kScratch3, RA::kScratch1, imm(63));
  if (err == Error::kOk)
    err = a.and_(RA::kScratch1, RA::kScratch1, imm(0x7fffffffffffffffull));
  if (err == Error::kOk)
    err = a.eor(RA::kScratch1, RA::kScratch1, RA::kScratch3);
  if (err == Error::kOk)
    err = a.sub(RA::kScratch1, RA::kScratch1, RA::kScratch3);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  const Label skip = a.new_label();
  switch (op.opcode) {
    case 0x31: err = a.cbnz(RA::kScratch1, skip); break;          // FBEQ
    case 0x35: err = a.cbz(RA::kScratch1, skip);  break;          // FBNE
    case 0x32: err = a.cmp(RA::kScratch1, imm(0));                // FBLT
               if (err == Error::kOk) err = a.b_ge(skip);
               break;
    case 0x36: err = a.cmp(RA::kScratch1, imm(0));                // FBGE
               if (err == Error::kOk) err = a.b_lt(skip);
               break;
    case 0x33: err = a.cmp(RA::kScratch1, imm(0));                // FBLE
               if (err == Error::kOk) err = a.b_gt(skip);
               break;
    case 0x37: err = a.cmp(RA::kScratch1, imm(0));                // FBGT
               if (err == Error::kOk) err = a.b_le(skip);
               break;
    default:   return {Error::kInvalidInstruction, op.kind};
  }
  if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kNextPc, target);
  if (err == Error::kOk) err = a.bind(skip);
  return a64_completed_op_receipt(op, err);
}

// FLTI/SQRT inline support. 

static asmjit::Error emit_a64_f_mem(A64EmitContext& c, uint32_t freg,
                                     asmjit::a64::Mem& mem)
{
  using RA = CJitEngine::RegAlloc;
  const uint64_t offset = uint64_t(c.offsets.f_from_r) + uint64_t(freg) * 8;
  if ((offset & 7u) == 0 && offset <= 32760u) {
    mem = asmjit::a64::ptr(RA::kRegs, static_cast<int32_t>(offset));
    return asmjit::Error::kOk;
  }
  mem = asmjit::a64::ptr(RA::kScratch6);
  return emit_a64_add_offset(c.assembler, RA::kScratch6, RA::kCpu,
      static_cast<int64_t>(c.offsets.f_base) + 8 * static_cast<int64_t>(freg),
      RA::kScratch5);
}
static asmjit::Error emit_a64_load_f(A64EmitContext& c, const asmjit::a64::Vec& d,
                                     uint32_t freg)
{
  asmjit::a64::Mem mem;
  asmjit::Error err = emit_a64_f_mem(c, freg, mem);
  return err != asmjit::Error::kOk ? err
      : c.assembler.ldr(d, mem);
}
static asmjit::Error emit_a64_store_f(A64EmitContext& c, const asmjit::a64::Vec& d,
                                      uint32_t freg)
{
  asmjit::a64::Mem mem;
  asmjit::Error err = emit_a64_f_mem(c, freg, mem);
  return err != asmjit::Error::kOk ? err
      : c.assembler.str(d, mem);
}
static asmjit::Error emit_a64_load_f_bits(A64EmitContext& c,
                                          const asmjit::a64::Gp& x, uint32_t freg)
{
  asmjit::a64::Mem mem;
  asmjit::Error err = emit_a64_f_mem(c, freg, mem);
  return err != asmjit::Error::kOk ? err
      : c.assembler.ldr(x, mem);
}
static asmjit::Error emit_a64_store_f_bits(A64EmitContext& c,
                                           const asmjit::a64::Gp& x, uint32_t freg)
{
  asmjit::a64::Mem mem;
  asmjit::Error err = emit_a64_f_mem(c, freg, mem);
  return err != asmjit::Error::kOk ? err
      : c.assembler.str(x, mem);
}

// FPSTART: fpen==0 -> FEN bail; exc_sum = 0.
static asmjit::Error emit_a64_fp_start(A64EmitContext& c, const asmjit::Label& bail)
{
  using RA = CJitEngine::RegAlloc;
  asmjit::a64::Assembler& a = c.assembler;
  asmjit::Error err = emit_a64_load_cpu_u8(a, RA::kScratch3.w(), c.offsets.fpen, false);
  if (err == asmjit::Error::kOk) err = a.cbz(RA::kScratch3.w(), bail);
  if (err == asmjit::Error::kOk) err = emit_a64_mov_u64(a, RA::kScratch0, 0);
  if (err == asmjit::Error::kOk)
    err = emit_a64_store_cpu_u64(a, RA::kScratch0, c.offsets.exc_sum);
  return err;
}
// FPCR.INE (bit 56) clear -> the first inexact would trap -> bail.
static asmjit::Error emit_a64_fp_ine_gate(A64EmitContext& c, const asmjit::Label& bail)
{
  using RA = CJitEngine::RegAlloc;
  asmjit::Error err = emit_a64_load_cpu_u64(c.assembler, RA::kScratch3, c.offsets.fpcr);
  return err != asmjit::Error::kOk ? err
      : c.assembler.tbz(RA::kScratch3, asmjit::imm(56), bail);
}
// /D dynamic rounding: bail unless FPCR<59:58> is round-to-nearest (2).
static asmjit::Error emit_a64_fp_dyn_gate(A64EmitContext& c, const asmjit::Label& bail)
{
  using RA = CJitEngine::RegAlloc;
  asmjit::a64::Assembler& a = c.assembler;
  asmjit::Error err = emit_a64_load_cpu_u64(a, RA::kScratch3, c.offsets.fpcr);
  if (err == asmjit::Error::kOk) err = a.lsr(RA::kScratch3, RA::kScratch3, asmjit::imm(58));
  if (err == asmjit::Error::kOk)
    err = a.and_(RA::kScratch3, RA::kScratch3, asmjit::imm(3));
  if (err == asmjit::Error::kOk) err = a.cmp(RA::kScratch3, asmjit::imm(2));
  return err != asmjit::Error::kOk ? err : a.b_ne(bail);
}
// Double class check: bail if denormal (always) or Inf/NaN (chk).
static asmjit::Error emit_a64_dbl_class_bail(A64EmitContext& c,
    const asmjit::a64::Vec& d, bool chk, const asmjit::Label& bail)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = c.assembler;
  Error err = a.fmov(RA::kScratch3, d);
  if (err == Error::kOk) err = a.lsr(RA::kScratch5, RA::kScratch3, imm(52));
  if (err == Error::kOk) err = a.and_(RA::kScratch5, RA::kScratch5, imm(0x7ff));
  if (err != Error::kOk) return err;
  if (chk) {
    err = a.cmp(RA::kScratch5, imm(0x7ff));
    if (err == Error::kOk) err = a.b_eq(bail);
    if (err != Error::kOk) return err;
  }
  const Label ok = a.new_label();
  err = a.cbnz(RA::kScratch5, ok);
  if (err == Error::kOk) err = a.lsl(RA::kScratch3, RA::kScratch3, imm(12));
  if (err == Error::kOk) err = a.cbnz(RA::kScratch3, bail);
  if (err == Error::kOk) err = a.bind(ok);
  return err;
}
// Single class check (on an s-view value): same policy at single-precision fields.
static asmjit::Error emit_a64_sgl_class_bail(A64EmitContext& c,
    const asmjit::a64::Vec& s, bool chk, const asmjit::Label& bail)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = c.assembler;
  Error err = a.fmov(RA::kScratch3.w(), s);
  if (err == Error::kOk) err = a.lsr(RA::kScratch5.w(), RA::kScratch3.w(), imm(23));
  if (err == Error::kOk) err = a.and_(RA::kScratch5.w(), RA::kScratch5.w(), imm(0xff));
  if (err != Error::kOk) return err;
  if (chk) {
    err = a.cmp(RA::kScratch5.w(), imm(0xff));
    if (err == Error::kOk) err = a.b_eq(bail);
    if (err != Error::kOk) return err;
  }
  const Label ok = a.new_label();
  err = a.cbnz(RA::kScratch5.w(), ok);
  if (err == Error::kOk) err = a.lsl(RA::kScratch3.w(), RA::kScratch3.w(), imm(9));
  if (err == Error::kOk) err = a.cbnz(RA::kScratch3.w(), bail);
  if (err == Error::kOk) err = a.bind(ok);
  return err;
}

// FLTI ADDx/SUBx/MULx/DIVx (S and T).
static A64OpEmitReceipt emit_a64_flti_arith(A64EmitContext& context,
                                            const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const uint32_t f = (op.ins >> 5) & 0x7ffu;
  const bool dyn = ((op.ins >> 11) & 3u) == 3u;
  const uint32_t baseop = f & 0x3fu;
  const bool sgl = baseop < 0x20;
  const uint32_t k = baseop & 3u;   // 0=add 1=sub 2=mul 3=div

  const Label bail = a.new_label(), cont = a.new_label();
  Error err = emit_a64_fp_start(context, bail);
  if (err == Error::kOk) err = emit_a64_fp_ine_gate(context, bail);
  if (err == Error::kOk && dyn) err = emit_a64_fp_dyn_gate(context, bail);
  if (err == Error::kOk) err = emit_a64_load_f(context, a64::d16, op.ra);
  if (err == Error::kOk) err = emit_a64_load_f(context, a64::d17, op.rb);
  if (err == Error::kOk) err = emit_a64_dbl_class_bail(context, a64::d16, false, bail);
  if (err == Error::kOk) err = emit_a64_dbl_class_bail(context, a64::d17, false, bail);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (sgl) {
    err = a.fcvt(a64::s16, a64::d16);
    if (err == Error::kOk) err = a.fcvt(a64::s17, a64::d17);
    if (err == Error::kOk)
      err = k == 0 ? a.fadd(a64::s16, a64::s16, a64::s17)
          : k == 1 ? a.fsub(a64::s16, a64::s16, a64::s17)
          : k == 2 ? a.fmul(a64::s16, a64::s16, a64::s17)
                   : a.fdiv(a64::s16, a64::s16, a64::s17);
    if (err == Error::kOk)
      err = emit_a64_sgl_class_bail(context, a64::s16, true, bail);
  } else {
    err = k == 0 ? a.fadd(a64::d16, a64::d16, a64::d17)
        : k == 1 ? a.fsub(a64::d16, a64::d16, a64::d17)
        : k == 2 ? a.fmul(a64::d16, a64::d16, a64::d17)
                 : a.fdiv(a64::d16, a64::d16, a64::d17);
    if (err == Error::kOk)
      err = emit_a64_dbl_class_bail(context, a64::d16, true, bail);
  }
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (k == 2 || k == 3) {
    const Label nz = a.new_label();
    if (sgl) { err = a.fmov(RA::kScratch3.w(), a64::s16);
               if (err == Error::kOk)
                 err = a.lsl(RA::kScratch3.w(), RA::kScratch3.w(), imm(1));
               if (err == Error::kOk) err = a.cbnz(RA::kScratch3.w(), nz); }
    else     { err = a.fmov(RA::kScratch3, a64::d16);
               if (err == Error::kOk)
                 err = a.lsl(RA::kScratch3, RA::kScratch3, imm(1));
               if (err == Error::kOk) err = a.cbnz(RA::kScratch3, nz); }
    if (err == Error::kOk) err = emit_a64_load_f_bits(context, RA::kScratch3, op.ra);
    if (err == Error::kOk) err = a.lsl(RA::kScratch3, RA::kScratch3, imm(1));
    if (err == Error::kOk) err = a.cbz(RA::kScratch3, nz);      // Fa == +-0: exact zero
    if (err == Error::kOk) err = emit_a64_load_f_bits(context, RA::kScratch3, op.rb);
    if (err == Error::kOk) err = a.lsl(RA::kScratch3, RA::kScratch3, imm(1));
    if (err == Error::kOk) {
      if (k == 2) err = a.cbz(RA::kScratch3, nz);               // MUL: Fb == +-0
      else {                                                    // DIV: Fb == +-Inf
        err = emit_a64_mov_u64(a, RA::kScratch5, 0xFFE0000000000000ull);
        if (err == Error::kOk) err = a.cmp(RA::kScratch3, RA::kScratch5);
        if (err == Error::kOk) err = a.b_eq(nz);
      }
    }
    if (err == Error::kOk) err = a.b(bail);                     // underflowed to zero
    if (err == Error::kOk) err = a.bind(nz);
    if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  }
  if (sgl) err = a.fcvt(a64::d16, a64::s16);                    // register (T) format
  if (err == Error::kOk) err = emit_a64_store_f(context, a64::d16, op.rc);
  if (err == Error::kOk) err = a.b(cont);
  if (err == Error::kOk) err = a.bind(bail);
  if (err == Error::kOk) err = emit_a64_retry_bail(context, index);
  if (err == Error::kOk) err = a.bind(cont);
  return a64_completed_op_receipt(op, err);
}

// FLTI CMPTUN/EQ/LT/LE
static A64OpEmitReceipt emit_a64_flti_cmp(A64EmitContext& context,
                                          const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const uint32_t f = (op.ins >> 5) & 0x7ffu;
  const uint32_t which = f & 3u;   // a4=UN a5=EQ a6=LT a7=LE (low bits of the func)

  const Label bail = a.new_label(), cont = a.new_label();
  Error err = emit_a64_fp_start(context, bail);
  if (err == Error::kOk) err = emit_a64_load_f(context, a64::d16, op.ra);
  if (err == Error::kOk) err = emit_a64_load_f(context, a64::d17, op.rb);
  // Compare-shaped class check: exp==0x7ff or exp==0 with a nonzero mantissa
  // (NaN / denormal) bails; Inf and zero (mantissa 0) compare inline.
  for (int i2 = 0; i2 < 2 && err == Error::kOk; ++i2) {
    const a64::Vec& v = i2 ? a64::d17 : a64::d16;
    const Label ok = a.new_label(), special = a.new_label();
    err = a.fmov(RA::kScratch3, v);
    if (err == Error::kOk) err = a.lsr(RA::kScratch5, RA::kScratch3, imm(52));
    if (err == Error::kOk) err = a.and_(RA::kScratch5, RA::kScratch5, imm(0x7ff));
    if (err == Error::kOk) err = a.cmp(RA::kScratch5, imm(0x7ff));
    if (err == Error::kOk) err = a.b_eq(special);
    if (err == Error::kOk) err = a.cbnz(RA::kScratch5, ok);
    if (err == Error::kOk) err = a.bind(special);
    if (err == Error::kOk) err = a.lsl(RA::kScratch3, RA::kScratch3, imm(12));
    if (err == Error::kOk) err = a.cbnz(RA::kScratch3, bail);
    if (err == Error::kOk) err = a.bind(ok);
  }
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (which == 0) {                       // CMPTUN: both ordered -> false
    err = emit_a64_mov_u64(a, RA::kScratch3, 0);
  } else {
    err = a.fcmp(a64::d16, a64::d17);
    if (err == Error::kOk) {
      const arm::CondCode cond = which == 1 ? arm::CondCode::kEQ
                               : which == 2 ? arm::CondCode::kLO
                                            : arm::CondCode::kLS;
      err = a.cset(RA::kScratch3, imm(static_cast<uint32_t>(cond)));
    }
    if (err == Error::kOk)
      err = a.lsl(RA::kScratch3, RA::kScratch3, imm(62));   // true -> 2.0 bits
  }
  if (err == Error::kOk) err = emit_a64_store_f_bits(context, RA::kScratch3, op.rc);
  if (err == Error::kOk) err = a.b(cont);
  if (err == Error::kOk) err = a.bind(bail);
  if (err == Error::kOk) err = emit_a64_retry_bail(context, index);
  if (err == Error::kOk) err = a.bind(cont);
  return a64_completed_op_receipt(op, err);
}

// FLTI converts. CVTST: valid-T copy (denorm/Inf/NaN bail). 
// CVTTS: narrow+rewiden with result-class and underflow guards. 
// CVTQT/CVTQS: int->FP, exactness via a chop round-trip, else INE-gated. 
// CVTTQ: FP->int; unlike x86's indefinite, 
// A64 saturates and converts NaN to 0 -- so Inf/NaN bail up front and both sat values bail 
static A64OpEmitReceipt emit_a64_flti_cvt(A64EmitContext& context,
                                          const A64DecodedOp& op, uint32_t index)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const uint32_t f = (op.ins >> 5) & 0x7ffu;
  const bool dyn = ((op.ins >> 11) & 3u) == 3u;

  const Label bail = a.new_label(), cont = a.new_label();
  Error err = emit_a64_fp_start(context, bail);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (f == 0x2ac || f == 0x6ac) {                         // CVTST
    err = emit_a64_load_f(context, a64::d16, op.rb);
    if (err == Error::kOk) err = emit_a64_dbl_class_bail(context, a64::d16, true, bail);
    if (err == Error::kOk) err = emit_a64_store_f(context, a64::d16, op.rc);
  } else if ((f & 0x3fu) == 0x2f) {                       // CVTTQ
    const bool chop = ((op.ins >> 11) & 3u) == 0u;
    if (dyn) err = emit_a64_fp_dyn_gate(context, bail);
    if (err == Error::kOk) err = emit_a64_load_f(context, a64::d16, op.rb);
    if (err == Error::kOk)
      err = emit_a64_dbl_class_bail(context, a64::d16, true, bail);  // + Inf/NaN
    if (err == Error::kOk)
      err = chop ? a.fcvtzs(RA::kScratch1, a64::d16)
                 : a.fcvtns(RA::kScratch1, a64::d16);
    if (err == Error::kOk)
      err = emit_a64_mov_u64(a, RA::kScratch5, 0x8000000000000000ull);
    if (err == Error::kOk) err = a.cmp(RA::kScratch1, RA::kScratch5);
    if (err == Error::kOk) err = a.b_eq(bail);            // negative saturation / -2^63
    if (err == Error::kOk)
      err = emit_a64_mov_u64(a, RA::kScratch5, 0x7fffffffffffffffull);
    if (err == Error::kOk) err = a.cmp(RA::kScratch1, RA::kScratch5);
    if (err == Error::kOk) err = a.b_eq(bail);            // positive saturation
    if (err == Error::kOk) {
      const Label exact = a.new_label();
      err = a.scvtf(a64::d17, RA::kScratch1);
      if (err == Error::kOk) err = a.fcmp(a64::d17, a64::d16);
      if (err == Error::kOk) err = a.b_eq(exact);
      if (err == Error::kOk) err = emit_a64_fp_ine_gate(context, bail);
      if (err == Error::kOk) err = a.bind(exact);
    }
    if (err == Error::kOk)
      err = emit_a64_store_f_bits(context, RA::kScratch1, op.rc);
  } else if ((f & 0x3fu) == 0x2c) {                       // CVTTS
    err = emit_a64_fp_ine_gate(context, bail);
    if (err == Error::kOk && dyn) err = emit_a64_fp_dyn_gate(context, bail);
    if (err == Error::kOk) err = emit_a64_load_f(context, a64::d16, op.rb);
    if (err == Error::kOk) err = emit_a64_dbl_class_bail(context, a64::d16, false, bail);
    if (err == Error::kOk) err = a.fcvt(a64::s16, a64::d16);
    if (err == Error::kOk) err = emit_a64_sgl_class_bail(context, a64::s16, true, bail);
    if (err == Error::kOk) {
      const Label nz = a.new_label();
      err = a.fmov(RA::kScratch3.w(), a64::s16);
      if (err == Error::kOk) err = a.lsl(RA::kScratch3.w(), RA::kScratch3.w(), imm(1));
      if (err == Error::kOk) err = a.cbnz(RA::kScratch3.w(), nz);
      if (err == Error::kOk) err = emit_a64_load_f_bits(context, RA::kScratch3, op.rb);
      if (err == Error::kOk) err = a.lsl(RA::kScratch3, RA::kScratch3, imm(1));
      if (err == Error::kOk) err = a.cbz(RA::kScratch3, nz);   // Fb == +-0
      if (err == Error::kOk) err = a.b(bail);                  // underflowed to zero
      if (err == Error::kOk) err = a.bind(nz);
    }
    if (err == Error::kOk) err = a.fcvt(a64::d16, a64::s16);
    if (err == Error::kOk) err = emit_a64_store_f(context, a64::d16, op.rc);
  } else {                                                // CVTQT / CVTQS
    const bool to_single = (f & 0x3fu) == 0x3c;
    if (dyn) err = emit_a64_fp_dyn_gate(context, bail);
    if (err == Error::kOk) {
      if (op.rb == 31) err = emit_a64_mov_u64(a, RA::kScratch1, 0);
      else             err = emit_a64_load_f_bits(context, RA::kScratch1, op.rb);
    }
    if (err == Error::kOk) {
      if (to_single) {
        err = a.scvtf(a64::s16, RA::kScratch1);
        if (err == Error::kOk) err = a.fcvtzs(RA::kScratch5, a64::s16);
      } else {
        err = a.scvtf(a64::d16, RA::kScratch1);
        if (err == Error::kOk) err = a.fcvtzs(RA::kScratch5, a64::d16);
      }
    }
    if (err == Error::kOk) err = a.cmp(RA::kScratch5, RA::kScratch1);
    if (err == Error::kOk) {
      // Round-trip exact goes no INE.
      // A64 FCVTZS saturates, INT64_MAX round-trips equal even though inexact. 
      // X64 CVTTSD2SI redurns indefinite instead, so fix...
      const Label fgate = a.new_label();
      const Label fdone = a.new_label();
      err = a.b_ne(fgate);
      if (err == Error::kOk) err = a.eor(RA::kScratch5, RA::kScratch1, Imm(0x7fffffffffffffffull));
      if (err == Error::kOk) err = a.cbnz(RA::kScratch5, fdone);
      if (err == Error::kOk) err = a.bind(fgate);
      if (err == Error::kOk) err = emit_a64_fp_ine_gate(context, bail);
      if (err == Error::kOk) err = a.bind(fdone);
    }
    if (err == Error::kOk && to_single) err = a.fcvt(a64::d16, a64::s16);
    if (err == Error::kOk) err = emit_a64_store_f(context, a64::d16, op.rc);
  }
  if (err == Error::kOk) err = a.b(cont);
  if (err == Error::kOk) err = a.bind(bail);
  if (err == Error::kOk) err = emit_a64_retry_bail(context, index);
  if (err == Error::kOk) err = a.bind(cont);
  return a64_completed_op_receipt(op, err);
}

// ITFP SQRTS/SQRTT
static A64OpEmitReceipt emit_a64_fsqrt(A64EmitContext& context,
                                       const A64DecodedOp& op, uint32_t index)
{
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  const uint32_t f14 = (op.ins >> 5) & 0x7ffu;
  const bool is_t = (f14 & 0x3fu) == 0x2b;
  const bool dyn = ((f14 >> 6) & 3u) == 3u;

  const Label bail = a.new_label(), cont = a.new_label();
  Error err = emit_a64_fp_start(context, bail);
  if (err == Error::kOk) err = emit_a64_fp_ine_gate(context, bail);
  if (err == Error::kOk && dyn) err = emit_a64_fp_dyn_gate(context, bail);
  if (err == Error::kOk) err = emit_a64_load_f(context, a64::d16, op.rb);
  if (err == Error::kOk) err = emit_a64_dbl_class_bail(context, a64::d16, false, bail);
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);
  if (is_t) {
    err = a.fsqrt(a64::d16, a64::d16);
    if (err == Error::kOk) err = emit_a64_dbl_class_bail(context, a64::d16, true, bail);
  } else {
    err = a.fcvt(a64::s16, a64::d16);
    if (err == Error::kOk) err = a.fsqrt(a64::s16, a64::s16);
    if (err == Error::kOk) err = emit_a64_sgl_class_bail(context, a64::s16, true, bail);
    if (err == Error::kOk) err = a.fcvt(a64::d16, a64::s16);
  }
  if (err == Error::kOk) err = emit_a64_store_f(context, a64::d16, op.rc);
  if (err == Error::kOk) err = a.b(cont);
  if (err == Error::kOk) err = a.bind(bail);
  if (err == Error::kOk) err = emit_a64_retry_bail(context, index);
  if (err == Error::kOk) err = a.bind(cont);
  return a64_completed_op_receipt(op, err);
}

// HW_MFPR: live plain fields inline; derived/asynchronous IPRs and verify use helper.
static A64OpEmitReceipt emit_a64_hw_mfpr(A64EmitContext& context,
                                         const A64DecodedOp& op)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  const A64GprRoute wa =
      a64_guest_gpr_write_route(context.regs, op.ra, context.pal_shadow);
  if (wa.kind == A64GprRouteKind::kInvalid || wa.kind == A64GprRouteKind::kZero)
    return {Error::kInvalidArgument, op.kind};
  const auto descriptor = context.profile.ipr_read.describe(op.ins);
  if (wa.kind == A64GprRouteKind::kDiscard && descriptor.discard_safe)
    return a64_completed_op_receipt(op);

  a64::Assembler& a = context.assembler;
#ifndef JIT_VERIFY
  uint32_t field_offset = 0;
  bool direct_field = wa.kind != A64GprRouteKind::kDiscard && descriptor.discard_safe;
  switch (descriptor.kind) {
    case AlphaIPRReadKind::ExceptionAddress: field_offset = context.offsets.exc_addr; break;
    case AlphaIPRReadKind::ExceptionSummary: field_offset = context.offsets.exc_sum; break;
    case AlphaIPRReadKind::PalBase: field_offset = context.offsets.pal_base; break;
    case AlphaIPRReadKind::MemoryFaultStatus: field_offset = context.offsets.mm_stat; break;
    case AlphaIPRReadKind::FaultAddress: field_offset = context.offsets.fault_va; break;
    default: direct_field = false; break;
  }
  if (direct_field) {
    Error err = emit_a64_load_cpu_u64(a, RA::kScratch2, field_offset);
    if (err == Error::kOk) {
      if (wa.kind == A64GprRouteKind::kPinned)
        err = a.mov(a64::x(static_cast<uint32_t>(wa.host)), RA::kScratch2);
      else
        err = a.str(RA::kScratch2, a64::ptr(RA::kRegs,
                    static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
    }
    return a64_completed_op_receipt(op, err);
  }
#endif
  Error err = emit_a64_helper_call(a, context.offsets, context.helpers,
      context.regs, context.pal_shadow, context.helpers.hw_mfpr_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kImm32, op.ins},
       {A64CallArgKind::kGuestOrZero, op.ra}});
  if (err != Error::kOk) return a64_completed_op_receipt(op, err);

  if (wa.kind == A64GprRouteKind::kPinned)
    err = a.mov(a64::x(static_cast<uint32_t>(wa.host)), a64::x0);
  else if (wa.kind == A64GprRouteKind::kMemory)
    err = a.str(a64::x0, a64::ptr(RA::kRegs,
                static_cast<int32_t>(wa.slot * sizeof(uint64_t))));
  return a64_completed_op_receipt(op, err);
}

static A64OpEmitReceipt emit_a64_dispatch_op(A64EmitContext& context,
    const A64DecodedOp& op, uint32_t index)
{
  if (index >= A64BlockPlan::kMaxOps)
    return {asmjit::Error::kInvalidArgument, op.kind};

  switch (op.kind) {
    case A64OpKind::kUnsupported:
    case A64OpKind::kValidationProbe:
      return {};
    case A64OpKind::kIntlLogical:
      return emit_a64_intl_logical(context, op);
    case A64OpKind::kBranchInt:
      return emit_a64_branch_int(context, op, index);
    case A64OpKind::kHwMfpr:
      return emit_a64_hw_mfpr(context, op);
    case A64OpKind::kIntsShift:
      return emit_a64_ints_shift(context, op);
    case A64OpKind::kLoadAddress:
      return emit_a64_load_address(context, op);
    case A64OpKind::kIntsZap:
      return emit_a64_ints_zap(context, op);
    case A64OpKind::kMemLoad:
      return emit_a64_mem_load(context, op, index);
    case A64OpKind::kMemStore:
      return emit_a64_mem_store(context, op, index);
    case A64OpKind::kJmpIndirect:
      return emit_a64_jmp_indirect(context, op, index);
    case A64OpKind::kIntlCmov:
      return emit_a64_intl_cmov(context, op);
    case A64OpKind::kIntlProbe:
      return emit_a64_intl_probe(context, op);
    case A64OpKind::kIntsByte:
      return emit_a64_ints_byte(context, op);
    case A64OpKind::kFptiInt:
      return emit_a64_fpti_int(context, op);
    case A64OpKind::kFtoi:
      return emit_a64_ftoi(context, op, index);
    case A64OpKind::kLoadLocked:
      return emit_a64_load_locked(context, op, index);
    case A64OpKind::kStoreCond:
      return emit_a64_store_cond(context, op, index);
    case A64OpKind::kInta:
      return emit_a64_inta(context, op);
    case A64OpKind::kMisc:
      return emit_a64_misc(context, op);
    case A64OpKind::kCallPal:
      return emit_a64_call_pal(context, op, index);
    case A64OpKind::kHwMtpr:
      return emit_a64_hw_mtpr(context, op);
    case A64OpKind::kHwLd:
      return emit_a64_hw_ld(context, op, index);
    case A64OpKind::kHwSt:
      return emit_a64_hw_st(context, op, index);
    case A64OpKind::kIntm:
      return emit_a64_intm(context, op);
    case A64OpKind::kFpMem:
      return emit_a64_fp_mem(context, op, index);
    case A64OpKind::kFltl:
      return emit_a64_fltl(context, op, index);
    case A64OpKind::kFltv:
      return emit_a64_fltv(context, op, index);
    case A64OpKind::kItof:
      return emit_a64_itof(context, op, index);
    case A64OpKind::kBranchFp:
      return emit_a64_branch_fp(context, op, index);
    case A64OpKind::kFltiArith:
      return emit_a64_flti_arith(context, op, index);
    case A64OpKind::kFltiCmp:
      return emit_a64_flti_cmp(context, op, index);
    case A64OpKind::kFltiCvt:
      return emit_a64_flti_cvt(context, op, index);
    case A64OpKind::kFsqrt:
      return emit_a64_fsqrt(context, op, index);
  }
  return {};
}

// Wrap the dispatch with the cross-op DPC-reuse.
static A64OpEmitReceipt emit_a64_planned_op(A64EmitContext& context,
    const A64DecodedOp& op, uint32_t index)
{
  context.prev_dpc = context.regs.dpc;
  context.prev_fwd = context.regs.fwd_reg;
  if (!kA64DpcReuse) context.prev_dpc.live = false;   // kill-switches: bisect aids
  if (!kA64ForwardValues) context.prev_fwd = -1;
  context.regs.dpc.live = false;
  context.regs.fwd_reg = -1;
  const A64OpEmitReceipt receipt = emit_a64_dispatch_op(context, op, index);
  if (receipt.error != asmjit::Error::kOk) return receipt;

  // Ops whose emitter returns before emitting anything
  bool emits_nothing = false;
  switch (op.kind) {
    case A64OpKind::kIntlLogical: case A64OpKind::kIntsShift: case A64OpKind::kIntsZap:
    case A64OpKind::kIntlCmov:    case A64OpKind::kIntlProbe: case A64OpKind::kIntsByte:
    case A64OpKind::kFptiInt:     case A64OpKind::kIntm:      case A64OpKind::kInta:
      emits_nothing = op.rc == 31; break;
    case A64OpKind::kLoadAddress:
      emits_nothing = op.ra == 31; break;
    case A64OpKind::kHwMtpr: {
      emits_nothing = context.profile.ipr_write.describe(op.ins) == AlphaIPRWriteKind::Noop;
      break;
    }
    default: break;
  }

  // Value forwarding for ops that don't write scratch 2 or the mirror register
  if (context.prev_fwd >= 0 && context.regs.fwd_reg < 0) {
    bool keep = emits_nothing;
    if (!emits_nothing) switch (op.kind) {
      case A64OpKind::kMemStore: {
#ifndef JIT_VERIFY
        // The fast path routes an unpinned store value through x11. Clobbers unless forwarded. 
        const A64GprRoute v =
            a64_guest_gpr_read_route(context.regs, op.ra, context.pal_shadow);
        keep = v.kind != A64GprRouteKind::kMemory || op.ra == context.prev_fwd;
#endif
        break;
      }
      case A64OpKind::kBranchInt:
        keep = true; break;
      case A64OpKind::kIntlCmov:
        keep = op.rc != context.prev_fwd; break;
      case A64OpKind::kMisc: {
        const uint32_t fn = op.ins & 0xffffu;
        keep = fn != 0xC000 && fn != 0xE000 && fn != 0xF000; break;
      }
      case A64OpKind::kMemLoad:
        keep = op.ra == 31; break;
      default: break;
    }
    if (keep) context.regs.fwd_reg = static_cast<int8_t>(context.prev_fwd);
  }
  if (!context.prev_dpc.live) return receipt;

  bool preserves = emits_nothing;
  uint32_t dest = 31;
  if (!emits_nothing) switch (op.kind) {
    case A64OpKind::kIntlLogical: case A64OpKind::kIntsShift:
    case A64OpKind::kIntlCmov:    case A64OpKind::kIntlProbe:
    case A64OpKind::kFptiInt:     case A64OpKind::kIntm:
      preserves = true; dest = op.rc; break;
    case A64OpKind::kLoadAddress:
      preserves = true; dest = op.ra; break;
    case A64OpKind::kInta: {   // scaled literals and subtracts use the bias register
      const uint32_t f = (op.ins >> 5) & 0x7fu;
      const bool scaled = f == 0x22 || f == 0x2b || f == 0x02 || f == 0x0b
                       || f == 0x32 || f == 0x3b || f == 0x12 || f == 0x1b;
      const bool register_add = !op.is_literal
          && (f == 0x22 || f == 0x02 || f == 0x32 || f == 0x12);
      preserves = !scaled || register_add; dest = op.rc; break;
    }
    case A64OpKind::kIntsZap:   // register-form selector uses kScratch3
      preserves = op.is_literal; dest = op.rc; break;
    case A64OpKind::kMisc: {    // barriers / hints only (the reads call a helper)
      const uint32_t fn = op.ins & 0xffffu;
      preserves = fn != 0xC000 && fn != 0xE000 && fn != 0xF000; break;
    }
    case A64OpKind::kMemLoad:   // the R31 NOP skip emits nothing (real loads set their own)
      preserves = op.ra == 31; break;
    case A64OpKind::kFpMem: {   // likewise the f31 load NOP
      const bool isload = op.opcode == 0x23 || op.opcode == 0x22
                       || op.opcode == 0x20 || op.opcode == 0x21;
      preserves = isload && op.ra == 31; break;
    }
    default: break;
  }
  if (preserves && dest != context.prev_dpc.base)
    context.regs.dpc = context.prev_dpc;
  return receipt;
}

template<typename EmitOne>
static A64BodyEmitReceipt drive_a64_block_body(const A64BlockPlan& plan,
    A64BodyEmitReceipt body, EmitOne&& emit_one)
{
  if (body.error != asmjit::Error::kOk) return body;

  for (uint32_t i = 0; i < plan.count; ++i) {
    if (!a64_begin_op_emission(body, plan, i)) return body;
    const A64OpEmitReceipt op = emit_one(plan.ops[i], i);
    if (!a64_accept_op_emission(body, plan, i, op)) return body;
  }
  (void)a64_finish_body_emission(body, plan);
  return body;
}

template<typename EmitOne>
static A64BodyEmitReceipt emit_a64_block_body(const A64BlockPlan& plan,
                                               EmitOne&& emit_one)
{
  return drive_a64_block_body(
      plan, a64_begin_body_emission(plan), emit_one);
}

template<typename EmitOne>
static A64BodyEmitReceipt validate_a64_block_body(const A64BlockPlan& plan,
                                                   EmitOne&& emit_one)
{
  return drive_a64_block_body(
      plan, a64_begin_body_validation(plan), emit_one);
}

static asmjit::Error emit_a64_chain_gate(asmjit::a64::Assembler& a,
    const CJitEngine::JitOffsets& offsets, A64ChainGate gate,
    const asmjit::Label& bailout)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  if (gate != A64ChainGate::kPollAll
      && gate != A64ChainGate::kDeferInterrupt
      && gate != A64ChainGate::kPollInterruptOnNativeTarget)
    return Error::kInvalidArgument;

  Error err = a.cmp(RA::kChainCount, imm(CJitEngine::kChainCeiling));   // constant ceiling
  if (err != Error::kOk) return err;
  err = a.b_ge(bailout);
  if (err != Error::kOk) return err;

  if (gate == A64ChainGate::kPollAll) {
    // Adjacent, halfword-aligned flags: one acquire load covers both.
    if (offsets.check_timers == offsets.check_int + 1 && (offsets.check_int & 1u) == 0) {
      err = emit_a64_load_cpu_u16(a, RA::kScratch0.w(), offsets.check_int, true);
      return err != Error::kOk ? err : a.cbnz(RA::kScratch0.w(), bailout);
    }
    err = emit_a64_load_cpu_u8(a, RA::kScratch0.w(), offsets.check_int, true);
    if (err != Error::kOk) return err;
    err = a.cbnz(RA::kScratch0.w(), bailout);
    if (err != Error::kOk) return err;
  }
  else if (gate == A64ChainGate::kPollInterruptOnNativeTarget) {
    const Label no_interrupt = a.new_label();
    err = emit_a64_load_cpu_u8(a, RA::kScratch0.w(), offsets.check_int, true);
    if (err != Error::kOk) return err;
    err = a.cbz(RA::kScratch0.w(), no_interrupt);
    if (err != Error::kOk) return err;
    err = a.tst(RA::kNextPc, imm(1));
    if (err != Error::kOk) return err;
    err = a.b_eq(bailout);  // A pending interrupt is deferred only to a PAL target.
    if (err != Error::kOk) return err;
    err = a.bind(no_interrupt);
    if (err != Error::kOk) return err;
  }

  err = emit_a64_load_cpu_u8(a, RA::kScratch0.w(), offsets.check_timers, true);
  return err != Error::kOk ? err : a.cbnz(RA::kScratch0.w(), bailout);
}

static asmjit::Error emit_a64_source_pal_guard(asmjit::a64::Assembler& a,
    const CJitEngine::JitOffsets& offsets, bool enabled, bool expected_shadow,
    const asmjit::Label& bailout)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  if (!enabled) return Error::kOk;

  Error err = emit_a64_load_cpu_u8(a, RA::kScratch6.w(), offsets.sde, false);
  if (err != Error::kOk) return err;
  err = a.cmp(RA::kScratch6.w(), imm(expected_shadow ? 1 : 0));
  return err != Error::kOk ? err : a.b_ne(bailout);
}

// Probe snapshots owned by the source block.
static asmjit::Error emit_a64_link_slots_probe(asmjit::a64::Assembler& a,
    const CJitEngine::JitOffsets& offsets, const CJitEngine::LinkSlot* slots,
    A64LinkVariantPolicy variant_policy, const asmjit::Label& miss)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  using namespace asmjit::a64;

  if (slots == nullptr
      || (variant_policy != A64LinkVariantPolicy::kNone
          && variant_policy != A64LinkVariantPolicy::kAlways
          && variant_policy != A64LinkVariantPolicy::kIfTargetPal))
    return Error::kInvalidArgument;

  Error err = emit_a64_mov_u64(a, RA::kScratch1,
                               reinterpret_cast<uintptr_t>(slots));
  if (err != Error::kOk) return err;
  if (variant_policy != A64LinkVariantPolicy::kNone) {
    err = emit_a64_load_cpu_u8(a, RA::kScratch6.w(), offsets.sde, false);
    if (err != Error::kOk) return err;
  }

  // Mapping/code invalidation clears links. On a mode change, keep links and
  // let native entries check CM.
  for (uint32_t slot = 0; slot < CJitEngine::kLinkSlots; ++slot) {
    const Label next = slot + 1 < CJitEngine::kLinkSlots
        ? a.new_label() : miss;
    const int32_t base = static_cast<int32_t>(slot * sizeof(CJitEngine::LinkSlot));

    err = a.ldr(RA::kScratch2,
                ptr(RA::kScratch1, base + offsetof(CJitEngine::LinkSlot, tag)));
    if (err != Error::kOk) return err;
    err = a.cmp(RA::kScratch2, RA::kNextPc);
    if (err != Error::kOk) return err;
    err = a.b_ne(next);
    if (err != Error::kOk) return err;

    Label variant_ok;
    if (variant_policy == A64LinkVariantPolicy::kIfTargetPal) {
      variant_ok = a.new_label();
      err = a.tst(RA::kNextPc, imm(1));
      if (err != Error::kOk) return err;
      err = a.b_eq(variant_ok);
      if (err != Error::kOk) return err;
    }
    if (variant_policy != A64LinkVariantPolicy::kNone) {
      // Match the register-bank variant recorded in this link against the live SDE state.
      err = a.ldr(RA::kScratch3,
                  ptr(RA::kScratch1, base + offsetof(CJitEngine::LinkSlot, vgen)));
      if (err != Error::kOk) return err;
      err = a.lsr(RA::kScratch4, RA::kScratch3, 63);
      if (err != Error::kOk) return err;
      err = a.cmp(RA::kScratch4.w(), RA::kScratch6.w());
      if (err != Error::kOk) return err;
      err = a.b_ne(next);
      if (err != Error::kOk) return err;
    }
    if (variant_policy == A64LinkVariantPolicy::kIfTargetPal) {
      err = a.bind(variant_ok);
      if (err != Error::kOk) return err;
    }

    err = a.ldr(RA::kScratch4,
                ptr(RA::kScratch1, base + offsetof(CJitEngine::LinkSlot, body)));
    if (err != Error::kOk) return err;
    if (variant_policy == A64LinkVariantPolicy::kIfTargetPal) {
      // A family return transform can produce the empty slot's ~0 tag.
      err = a.cbz(RA::kScratch4, next);
      if (err != Error::kOk) return err;
    }
    err = a.br(RA::kScratch4);
    if (err != Error::kOk) return err;
    if (slot + 1 < CJitEngine::kLinkSlots) {
      err = a.bind(next);
      if (err != Error::kOk) return err;
    }
  }
  return Error::kOk;
}

static asmjit::Error emit_a64_direct_chain_tail(asmjit::a64::Assembler& a,
    const CJitEngine::JitOffsets& offsets,
    const A64DirectChainContract& contract, CJitEngine::LinkSlot* slots,
    uint64_t source_tag,
    const asmjit::Label& body, const asmjit::Label& done,
    bool source_global = true)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  if (!contract.valid() || !contract.eligible()
      || slots == nullptr)
    return Error::kInvalidArgument;

  Error err = a.add(RA::kChainCount, RA::kChainCount,
                    imm(contract.completed_delta));
  if (err != Error::kOk) return err;
  if (contract.publish_pc_before_probe) {
    err = emit_a64_store_cpu_u64(a, RA::kNextPc, offsets.state_pc);
    if (err != Error::kOk) return err;
  }

#ifdef JIT_VERIFY
  (void) source_tag; (void) body; (void) source_global;   // no chain in the verify shape
  if (!contract.publish_pc_before_probe) {
    err = emit_a64_store_cpu_u64(a, RA::kNextPc, offsets.state_pc);
    if (err != Error::kOk) return err;
  }
  return a.b(done);
#else
  const Label bailout = a.new_label();
  const Label miss = a.new_label();
  // Thinned exits (fall-through / forward branch) carry no gate: they cannot
  // revisit code, so the budget/interrupt poll waits for a gated exit downstream.
  if (contract.gate != A64ChainGate::kNone) {
    err = emit_a64_chain_gate(a, offsets, contract.gate, bailout);
    if (err != Error::kOk) return err;
  }
  // A source-variant mismatch must not request a patch: PAL-source slots are
  // tag-keyed and rely on this guard to imply the target register-bank variant.
  err = emit_a64_source_pal_guard(a, offsets, contract.source_pal_guard,
                                  contract.source_pal_shadow, bailout);
  if (err != Error::kOk) return err;

  if (contract.self_loop_candidate) {
    err = emit_a64_mov_u64(a, RA::kScratch0, source_tag);
    if (err != Error::kOk) return err;
    err = a.cmp(RA::kNextPc, RA::kScratch0);
    if (err != Error::kOk) return err;
    err = a.b_eq(body);
    if (err != Error::kOk) return err;
  }

  const A64LinkVariantPolicy variant = contract.target_pal_guard
      ? A64LinkVariantPolicy::kAlways : A64LinkVariantPolicy::kNone;
  err = emit_a64_link_slots_probe(a, offsets, slots,
                                  variant, miss);
  if (err != Error::kOk) return err;

  err = a.bind(miss);
  if (err != Error::kOk) return err;
  err = emit_a64_mov_u64(a, RA::kScratch1,                      // bit 0: non-global source
                         reinterpret_cast<uintptr_t>(slots) | (source_global ? 0u : 1u));
  if (err != Error::kOk) return err;
  err = emit_a64_store_cpu_u64(a, RA::kScratch1, offsets.link_from);
  if (err != Error::kOk) return err;

  err = a.bind(bailout);
  if (err != Error::kOk) return err;
  if (!contract.publish_pc_before_probe) {
    err = emit_a64_store_cpu_u64(a, RA::kNextPc, offsets.state_pc);
    if (err != Error::kOk) return err;
  }
  return a.b(done);
#endif
}

struct A64JumpCache {
  const void* base = nullptr;      // &m_jcache[0]; null = no probe (harness)
  const uint64_t* gen = nullptr;   // &m_jc_gen
  uint64_t* hits = nullptr;        // &m_jc_hits (JIT_STATS)
};

// Target-keyed jump cache: tag, generation, ASN and (PALmode) register-bank variant must match.
// Probed only once this site's PIC is full, so the resolver still fills the PIC first.
static asmjit::Error emit_a64_jump_cache_probe(asmjit::a64::Assembler& a,
    const CJitEngine::JitOffsets& offsets, const A64JumpCache& jc,
    const CJitEngine::LinkSlot* slots, const asmjit::Label& miss,
    bool hardware_return = false)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  using E = CJitEngine::JumpCacheEntry;
  static_assert(sizeof(E) == 32 && CJitEngine::kJumpCacheEntries == 4096,
                "A64 jump-cache probe indexes 32-byte entries by (pc >> 2) & 4095");
  Error err = emit_a64_mov_u64(a, RA::kScratch1,
      reinterpret_cast<uintptr_t>(&slots[CJitEngine::kLinkSlots - 1].body));
  if (err != Error::kOk) return err;
  err = a.ldr(RA::kScratch2, a64::ptr(RA::kScratch1));
  if (err != Error::kOk) return err;
  err = a.cbz(RA::kScratch2, miss);
  if (err != Error::kOk) return err;
  err = emit_a64_mov_u64(a, RA::kScratch1, reinterpret_cast<uintptr_t>(jc.base));
  if (err != Error::kOk) return err;
  err = a.ubfx(RA::kScratch2, RA::kNextPc, 2, 12);
  if (err != Error::kOk) return err;
  err = a.lsl(RA::kScratch2, RA::kScratch2, 5);
  if (err != Error::kOk) return err;
  err = a.add(RA::kScratch1, RA::kScratch1, RA::kScratch2);
  if (err != Error::kOk) return err;
  err = a.ldr(RA::kScratch2, a64::ptr(RA::kScratch1, (int32_t) offsetof(E, tag)));
  if (err != Error::kOk) return err;
  err = a.cmp(RA::kScratch2, RA::kNextPc);
  if (err != Error::kOk) return err;
  err = a.b_ne(miss);
  if (err != Error::kOk) return err;
  err = emit_a64_mov_u64(a, RA::kScratch3, reinterpret_cast<uintptr_t>(jc.gen));
  if (err != Error::kOk) return err;
  err = a.ldr(RA::kScratch3, a64::ptr(RA::kScratch3));
  if (err != Error::kOk) return err;
  err = a.ldr(RA::kScratch2, a64::ptr(RA::kScratch1, (int32_t) offsetof(E, gen)));
  if (err != Error::kOk) return err;
  err = a.ubfx(RA::kScratch4, RA::kScratch2, 0, 63);
  if (err != Error::kOk) return err;
  err = a.cmp(RA::kScratch4, RA::kScratch3);
  if (err != Error::kOk) return err;
  err = a.b_ne(miss);
  if (err != Error::kOk) return err;
  err = emit_a64_load_cpu_u32(a, RA::kScratch4.w(), offsets.state_asn);
  if (err != Error::kOk) return err;
  err = a.ldr(RA::kScratch3, a64::ptr(RA::kScratch1, (int32_t) offsetof(E, asn)));
  if (err != Error::kOk) return err;
  err = a.cmp(RA::kScratch3, RA::kScratch4);
  if (err != Error::kOk) return err;
  err = a.b_ne(miss);
  if (err != Error::kOk) return err;
  const Label mode_ok = a.new_label();
  err = a.tbz(RA::kNextPc, 0, mode_ok);
  if (err != Error::kOk) return err;
  err = a.lsr(RA::kScratch2, RA::kScratch2, 63);
  if (err != Error::kOk) return err;
  err = emit_a64_load_cpu_u8(a, RA::kScratch4.w(), offsets.sde, false);
  if (err != Error::kOk) return err;
  err = a.cmp(RA::kScratch2.w(), RA::kScratch4.w());
  if (err != Error::kOk) return err;
  err = a.b_ne(miss);
  if (err != Error::kOk) return err;
  err = a.bind(mode_ok);
  if (err != Error::kOk) return err;
  if (hardware_return) {
    // Targeted invalidation clears body/tag but retains the matching generation.
    err = a.ldr(RA::kScratch2, a64::ptr(RA::kScratch1, (int32_t) offsetof(E, body)));
    if (err != Error::kOk) return err;
    err = a.cbz(RA::kScratch2, miss);
    if (err != Error::kOk) return err;
  }
#ifdef JIT_STATS
  if (jc.hits) {
    err = emit_a64_mov_u64(a, RA::kScratch3, reinterpret_cast<uintptr_t>(jc.hits));
    if (err != Error::kOk) return err;
    err = a.ldr(RA::kScratch4, a64::ptr(RA::kScratch3));
    if (err != Error::kOk) return err;
    err = a.add(RA::kScratch4, RA::kScratch4, imm(1));
    if (err != Error::kOk) return err;
    err = a.str(RA::kScratch4, a64::ptr(RA::kScratch3));
    if (err != Error::kOk) return err;
  }
#endif
  if (!hardware_return) {
    err = a.ldr(RA::kScratch2, a64::ptr(RA::kScratch1, (int32_t) offsetof(E, body)));
    if (err != Error::kOk) return err;
  }
  return a.br(RA::kScratch2);
}

static asmjit::Error emit_a64_indirect_chain_tail(asmjit::a64::Assembler& a,
    const CJitEngine::JitOffsets& offsets, const CJitEngine::HelperSet& helpers,
    const CJitEngine::RegAlloc& regs,
    const A64IndirectChainContract& contract, CJitEngine::LinkSlot* slots,
    const asmjit::Label& done, bool source_global = true,
    const A64JumpCache& jc = A64JumpCache())
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  if (!contract.valid() || !contract.eligible()
      || slots == nullptr)
    return Error::kInvalidArgument;
#ifndef JIT_VERIFY
  if (helpers.indirect_helper == nullptr) return Error::kInvalidArgument;
#endif

  Error err = a.add(RA::kChainCount, RA::kChainCount,
                    imm(contract.completed_delta));
  if (err != Error::kOk) return err;
  if (contract.publish_pc_before_probe) {
    err = emit_a64_store_cpu_u64(a, RA::kNextPc, offsets.state_pc);
    if (err != Error::kOk) return err;
  }

#ifdef JIT_VERIFY
  (void)helpers;
  (void)regs;
  (void)source_global;
  if (!contract.publish_pc_before_probe) {
    err = emit_a64_store_cpu_u64(a, RA::kNextPc, offsets.state_pc);
    if (err != Error::kOk) return err;
  }
  return a.b(done);
#else
  const Label bailout = a.new_label();
  const Label pic_miss = a.new_label();
  const Label resolver = a.new_label();
  err = emit_a64_chain_gate(a, offsets, contract.gate, bailout);
  if (err != Error::kOk) return err;
  err = emit_a64_link_slots_probe(a, offsets, slots,
                                  contract.target_variant, pic_miss);
  if (err != Error::kOk) return err;
  err = a.bind(pic_miss);
  if (err != Error::kOk) return err;
  if (jc.base && CJitEngine::kJumpCache) {
    err = emit_a64_jump_cache_probe(a, offsets, jc, slots, resolver,
                                    contract.kind == A64IndirectKind::kHwRet);
    if (err != Error::kOk) return err;
  }

  err = a.bind(resolver);
  if (err != Error::kOk) return err;
  if (!contract.publish_pc_before_probe) {
    err = emit_a64_store_cpu_u64(a, RA::kNextPc, offsets.state_pc);
    if (err != Error::kOk) return err;
  }
  err = emit_a64_helper_call(a, offsets, helpers, regs,
      contract.source_pal_shadow, helpers.indirect_helper,
      {{A64CallArgKind::kCpu, 0},
       {A64CallArgKind::kHost, RA::kNextPc.id()},
       {A64CallArgKind::kImm64,                                  // bit 0: non-global source
        reinterpret_cast<uintptr_t>(slots) | (source_global ? 0u : 1u)}});
  if (err != Error::kOk) return err;
  err = a.cbz(a64::x0, done);
  if (err != Error::kOk) return err;
  err = a.br(a64::x0);
  if (err != Error::kOk) return err;

  err = a.bind(bailout);
  if (err != Error::kOk) return err;
  if (!contract.publish_pc_before_probe) {
    err = emit_a64_store_cpu_u64(a, RA::kNextPc, offsets.state_pc);
    if (err != Error::kOk) return err;
  }
  return a.b(done);
#endif
}

struct A64DirectExit {
  asmjit::Label site, stub, desc, gate_fail;
  uint64_t pc = 0;
  bool gate = false;
};

// Direct-patched exits: one `b` site per static successor, aimed at its stub until the
// dispatcher retargets it (CJitEngine::patch_edge). Backward targets keep the gate.
static asmjit::Error emit_a64_direct_patch_tail(A64EmitContext& context,
    const A64BlockExit& exit, uint32_t terminator_ins,
    std::vector<A64DirectExit>& exits)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  if (exit.completed_delta == 0 || exit.completed_delta > kA64MaxBlockOps)
    return Error::kInvalidArgument;

  auto count = [&]() -> Error {
    return a.add(RA::kChainCount, RA::kChainCount, imm(exit.completed_delta));
  };
  auto site = [&](uint64_t pc, bool gate) -> Error {
    A64DirectExit x;
    x.site = a.new_label(); x.stub = a.new_label(); x.desc = a.new_label();
    x.gate_fail = a.new_label(); x.pc = pc; x.gate = gate;
    Error err = Error::kOk;
    if (gate)
      err = emit_a64_chain_gate(a, context.offsets, A64ChainGate::kPollAll, x.gate_fail);
    if (err == Error::kOk) err = a.bind(x.site);
    if (err == Error::kOk) err = a.b(x.stub);          // the patch site
    if (err == Error::kOk) exits.push_back(x);
    return err;
  };
  const bool backward = ((terminator_ins >> 20) & 1u) != 0;

  Error err = Error::kOk;
  switch (context.direct_kind) {
    case 1:   // conditional: both arms count the block (the taken branch skips the fall arm)
      err = count();
      if (err == Error::kOk) err = site(context.direct_fall, false);
      if (err == Error::kOk) err = a.bind(context.direct_taken);
      if (err == Error::kOk) err = count();
      if (err == Error::kOk) err = site(context.direct_target, backward);
      return err;
    case 2:
      err = count();
      if (err == Error::kOk)
        err = site(context.direct_target,
                   backward && context.direct_target != context.direct_fall);
      return err;
    default:
      err = count();
      if (err == Error::kOk) err = site(exit.fallthrough_pc, false);
      return err;
  }
}

// Cold side: exit stubs (x22 = pc, link_from = PatchSite|2|nonglobal) and the descriptors.
static asmjit::Error emit_a64_direct_patch_cold(A64EmitContext& context,
    const std::vector<A64DirectExit>& exits, bool source_global)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  a64::Assembler& a = context.assembler;
  if (exits.empty()) return Error::kOk;

  const Label exit_common = a.new_label();
  Error err = a.bind(exit_common);
  if (err != Error::kOk) return err;
  err = emit_a64_store_cpu_u64(a, RA::kNextPc, context.offsets.state_pc);
  if (err != Error::kOk) return err;
  err = a.b(context.done);
  if (err != Error::kOk) return err;

  for (const A64DirectExit& x : exits) {
    if (x.gate) {                                       // gate failed: plain exit, no request
      err = a.bind(x.gate_fail);
      if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kNextPc, x.pc);
      if (err == Error::kOk) err = a.b(exit_common);
      if (err != Error::kOk) return err;
    }
    err = a.bind(x.stub);
    if (err == Error::kOk) err = emit_a64_mov_u64(a, RA::kNextPc, x.pc);
    if (err == Error::kOk) err = a.adr(RA::kScratch0, x.desc);
    if (err == Error::kOk) err = a.orr(RA::kScratch0, RA::kScratch0, imm(source_global ? 2 : 3));
    if (err == Error::kOk) err = emit_a64_store_cpu_u64(a, RA::kScratch0, context.offsets.link_from);
    if (err == Error::kOk) err = a.b(exit_common);
    if (err != Error::kOk) return err;
  }
  err = a.align(AlignMode::kData, 8);
  if (err != Error::kOk) return err;
  for (const A64DirectExit& x : exits) {                // PatchSite { site, stub, tag, source_tag }
    err = a.bind(x.desc);
    if (err == Error::kOk) err = a.embed_label(x.site);
    if (err == Error::kOk) err = a.embed_label(x.stub);
    if (err == Error::kOk) err = a.embed_uint64(x.pc);
    if (err == Error::kOk) err = a.embed_uint64(context.start_pc);
    if (err != Error::kOk) return err;
  }
  return Error::kOk;
}

static asmjit::Error emit_a64_prologue(asmjit::a64::Assembler& a)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  using namespace asmjit::a64;

  Error err = a.stp(x29, x30, ptr_pre(sp, -A64FrameLayout::kSize));
  if (err != Error::kOk) return err;
  err = emit_a64_add_offset(a, x29, sp, 0);
  if (err != Error::kOk) return err;
  err = a.stp(RA::kCpu, RA::kRegs, ptr(sp, A64FrameLayout::kCpuRegs));
  if (err != Error::kOk) return err;
  err = a.stp(RA::kChainCount, RA::kNextPc, ptr(sp, A64FrameLayout::kCountPc));
  if (err != Error::kOk) return err;
  err = a.stp(RA::kGuestPin0, RA::kGuestPin1, ptr(sp, A64FrameLayout::kPin01));
  if (err != Error::kOk) return err;
  err = a.stp(RA::kGuestPin2, RA::kGuestPin3, ptr(sp, A64FrameLayout::kPin23));
  if (err != Error::kOk) return err;
  err = a.stp(RA::kGuestPin4, RA::kGuestPin5, ptr(sp, A64FrameLayout::kPin45));
  if (err != Error::kOk) return err;

  err = a.mov(RA::kCpu, RA::kArgCpu);
  if (err != Error::kOk) return err;
  err = a.mov(RA::kRegs, RA::kArgRegs);
  if (err != Error::kOk) return err;
  return emit_a64_mov_u64(a, RA::kChainCount, 0);
}

static asmjit::Error emit_a64_epilogue(asmjit::a64::Assembler& a)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;
  using namespace asmjit::a64;

  Error err = a.ldp(RA::kGuestPin4, RA::kGuestPin5, ptr(sp, A64FrameLayout::kPin45));
  if (err != Error::kOk) return err;
  err = a.ldp(RA::kGuestPin2, RA::kGuestPin3, ptr(sp, A64FrameLayout::kPin23));
  if (err != Error::kOk) return err;
  err = a.ldp(RA::kGuestPin0, RA::kGuestPin1, ptr(sp, A64FrameLayout::kPin01));
  if (err != Error::kOk) return err;
  err = a.ldp(RA::kChainCount, RA::kNextPc, ptr(sp, A64FrameLayout::kCountPc));
  if (err != Error::kOk) return err;
  err = a.ldp(RA::kCpu, RA::kRegs, ptr(sp, A64FrameLayout::kCpuRegs));
  if (err != Error::kOk) return err;
  err = a.ldp(x29, x30, ptr_post(sp, A64FrameLayout::kSize));
  if (err != Error::kOk) return err;
  return a.ret(x30);
}

#ifndef NDEBUG
static bool a64_validate_body_driver(const asmjit::Environment& environment,
                                     const asmjit::CpuFeatures& features)
{
  using namespace asmjit;

  A64BlockPlan plan{};
  plan.count = 3;
  plan.stop = A64PlanStop::kBlockEnd;
  for (uint32_t i = 0; i < plan.count; ++i) {
    plan.ops[i].ins = i + 1;
    plan.ops[i].kind = A64OpKind::kValidationProbe;
  }

  CodeHolder complete_code;
  if (complete_code.init(environment, features) != Error::kOk) return false;
  a64::Assembler complete_assembler;
  if (complete_code.attach(&complete_assembler) != Error::kOk) return false;
  complete_assembler.add_diagnostic_options(
      DiagnosticOptions::kValidateAssembler);
  uint32_t complete_calls = 0;
  const A64BodyEmitReceipt complete = validate_a64_block_body(
      plan, [&](const A64DecodedOp& op, uint32_t index) {
        if (index != complete_calls++)
          return A64OpEmitReceipt{};
        return a64_completed_op_receipt(op, complete_assembler.nop());
      });
  if (!complete.complete_for_validation(plan) || complete_calls != plan.count
      || complete_assembler.finalize() != Error::kOk
      || complete_code.has_unresolved_fixups()
      || complete_code.code_size() != plan.count * sizeof(uint32_t))
    return false;

  // Host bytes emitted before a later error are deliberately not rolled back or
  // accepted as a shorter body; the caller abandons this whole CodeHolder.
  CodeHolder partial_code;
  if (partial_code.init(environment, features) != Error::kOk) return false;
  a64::Assembler partial_assembler;
  if (partial_code.attach(&partial_assembler) != Error::kOk) return false;
  partial_assembler.add_diagnostic_options(
      DiagnosticOptions::kValidateAssembler);
  uint32_t partial_calls = 0;
  const A64BodyEmitReceipt partial = validate_a64_block_body(
      plan, [&](const A64DecodedOp& op, uint32_t index) {
        ++partial_calls;
        const Error err = partial_assembler.nop();
        return a64_completed_op_receipt(
            op, index == 1 && err == Error::kOk ? Error::kInvalidState : err);
      });
  if (partial.complete || partial.error != Error::kInvalidState
      || partial.attempted_count != 2 || partial.emitted_count != 1
      || partial_calls != 2
      || partial_code.code_size() != 2 * sizeof(uint32_t))
    return false;

  CJitEngine::JitOffsets offsets{};
  CJitEngine::HelperSet helpers{};
  CJitEngine::RegAlloc regs = make_a64_block_regalloc();
  const Label done = partial_assembler.new_label();
  A64EmitContext context{partial_assembler, offsets, kEv68CBProfile, helpers, regs, done,
                         0x1000, false, false};
  const A64BodyEmitReceipt inert = validate_a64_block_body(
      plan, [&](const A64DecodedOp& op, uint32_t index) {
        return emit_a64_planned_op(context, op, index);
      });
  A64DecodedOp unsupported{};
  const A64OpEmitReceipt unsupported_receipt =
      emit_a64_planned_op(context, unsupported, 0);
  return !inert.complete && inert.error == Error::kInvalidInstruction
      && inert.attempted_count == 1 && inert.emitted_count == 0
      && !unsupported_receipt.accepted_for(unsupported)
      && unsupported_receipt.error == Error::kInvalidInstruction
      && partial_code.code_size() == 2 * sizeof(uint32_t);
}

static bool a64_validate_tail_emitters(const asmjit::Environment& environment,
                                       const asmjit::CpuFeatures& features)
{
  using RA = CJitEngine::RegAlloc;
  using namespace asmjit;

  CJitEngine::JitOffsets offsets{};
  offsets.state_pc = 4095;
  offsets.link_from = 4096;
  offsets.check_timers = 32760;
  offsets.helpers = 32768;
  offsets.sde = 0x00ffffffu;
  offsets.check_int = 0x01000000u;
  offsets.jit_budget = UINT32_MAX;

  CJitEngine::JitBlock block{};
  block.tag = 0x1001;
  block.pal_shadow = true;
  CJitEngine::HelperSet helpers{};
  helpers.indirect_helper = reinterpret_cast<void*>(uintptr_t(1));
  const CJitEngine::RegAlloc regs = make_a64_block_regalloc();

  auto finish = [&](CodeHolder& code, a64::Assembler& a,
                    const Label& done) -> bool {
    Error err = a.bind(done);
    if (err == Error::kOk)
      err = a.mov(RA::kResultCount, RA::kChainCount.w());
    if (err == Error::kOk) err = a.ret(a64::x30);
    if (err == Error::kOk) err = a.finalize();
    return err == Error::kOk && !code.has_unresolved_fixups()
        && code.code_size() != 0;
  };

  auto validate_direct = [&](A64ExitKind kind, uint32_t terminator_ins,
                             bool pal_block, bool pal_shadow) -> bool {
    CodeHolder code;
    if (code.init(environment, features) != Error::kOk) return false;
    a64::Assembler a;
    if (code.attach(&a) != Error::kOk) return false;
    a.add_diagnostic_options(DiagnosticOptions::kValidateAssembler);
    const Label body = a.new_label();
    const Label done = a.new_label();
    if (a.bind(body) != Error::kOk
        || emit_a64_mov_u64(a, RA::kChainCount, 0) != Error::kOk
        || emit_a64_mov_u64(a, RA::kNextPc, 0x2001) != Error::kOk)
      return false;
    const A64BlockExit exit{0x1005, 1, kind};
    const A64DirectChainContract contract =
        plan_a64_direct_chain(exit, terminator_ins, pal_block, pal_shadow);
    if (emit_a64_direct_chain_tail(a, offsets, contract, &block.link[0],
          block.tag, body, done) != Error::kOk)
      return false;
    return finish(code, a, done);
  };

  auto validate_indirect = [&](uint32_t opcode, bool pal_block,
                               bool pal_shadow) -> bool {
    CodeHolder code;
    if (code.init(environment, features) != Error::kOk) return false;
    a64::Assembler a;
    if (code.attach(&a) != Error::kOk) return false;
    a.add_diagnostic_options(DiagnosticOptions::kValidateAssembler);
    const Label body = a.new_label();
    const Label done = a.new_label();
    if (a.bind(body) != Error::kOk
        || emit_a64_mov_u64(a, RA::kChainCount, 0) != Error::kOk
        || emit_a64_mov_u64(a, RA::kNextPc, 0x2001) != Error::kOk)
      return false;
    const A64BlockExit exit{0x1005, 1, A64ExitKind::kIndirect};
    const A64IndirectChainContract contract = plan_a64_indirect_chain(
        exit, opcode << 26, pal_block, pal_shadow);
    if (emit_a64_indirect_chain_tail(a, offsets, helpers, regs, contract,
          &block.link[0], done) != Error::kOk)
      return false;
    return finish(code, a, done);
  };

  auto validate_direct_patch = [&]() -> bool {
    CodeHolder code;
    if (code.init(environment, features) != Error::kOk) return false;
    a64::Assembler a;
    if (code.attach(&a) != Error::kOk) return false;
    a.add_diagnostic_options(DiagnosticOptions::kValidateAssembler);
    const Label done = a.new_label();
    if (emit_a64_mov_u64(a, RA::kChainCount, 0) != Error::kOk) return false;
    CJitEngine::RegAlloc patch_regs = make_a64_block_regalloc();
    A64EmitContext context{a, offsets, kEv68CBProfile, helpers, patch_regs, done,
                           0x1000, false, false};
    context.direct_exits = true;
    const A64BlockExit exit{0x1008, 2, A64ExitKind::kFallthrough};
    std::vector<A64DirectExit> exits;
    if (emit_a64_direct_patch_tail(context, exit, 0x10u << 26, exits) != Error::kOk)
      return false;
    if (a.bind(done) != Error::kOk
        || a.mov(RA::kResultCount, RA::kChainCount.w()) != Error::kOk
        || a.ret(a64::x30) != Error::kOk)
      return false;
    if (emit_a64_direct_patch_cold(context, exits, false) != Error::kOk) return false;
    return a.finalize() == Error::kOk && !code.has_unresolved_fixups()
        && exits.size() == 1 && (code.code_size() & 3u) == 0;
  };

  // Fall-throughs and the forward branch exercise the thinned (gateless) tail;
  // the backward branch, CALL_PAL, and redispatch exercise the gated one.
  return validate_direct_patch()
      && validate_direct(A64ExitKind::kFallthrough, 0x10u << 26, false, false)
      && validate_direct(A64ExitKind::kFallthrough, 0x10u << 26, true, true)
      && validate_direct(A64ExitKind::kDirect, (0x39u << 26) | 0x1fffffu, true, true)
      && validate_direct(A64ExitKind::kDirect, (0x39u << 26) | 0x4u, false, false)
      && validate_direct(A64ExitKind::kCallPal, 0x86u, false, false)
      && validate_direct(A64ExitKind::kRedispatch,
                         (0x1du << 26) | (0x11u << 8), true, true)
      && validate_indirect(0x1a, false, false)
      && validate_indirect(0x1a, true, false)
      && validate_indirect(0x1e, true, true);
}

static bool a64_abi_compatible(const asmjit::Environment& environment)
{
  using namespace asmjit;
  if (!environment.is_family_aarch64()) return false;

  CallConv cc;
  if (cc.init(CallConvId::kCDecl, environment) != Error::kOk) return false;
  const uint8_t* const gp_args = cc.passed_order(RegGroup::kGp);
  if (gp_args[0] != CJitEngine::RegAlloc::kArgCpu.id()
      || gp_args[1] != CJitEngine::RegAlloc::kArgRegs.id()) return false;
  for (uint32_t i = 2; i < 8; ++i) if (gp_args[i] != i) return false;

  const RegMask preserved = cc.preserved_regs(RegGroup::kGp);
  return (preserved & CJitEngine::RegAlloc::kPersistentGpMask)
          == CJitEngine::RegAlloc::kPersistentGpMask
      && (preserved & axl::bit_mask<RegMask>(CJitEngine::RegAlloc::kScratch0.id())) == 0
      && cc.natural_stack_alignment() == 16;
}
#endif

} // namespace

void CJitEngine::emit_op(void* a_ptr, const uint8_t* gpa, void* done_ptr,
                         const HelperSet& hs, bool pal_block, JitBlock* b,
                         uint32_t ins, uint32_t i, RegAlloc& regalloc,
                         void* cold, bool defer_pc)
{
  (void)gpa;
  auto* const context = static_cast<A64EmitContext*>(cold);
  if (context == nullptr) return;

  context->receipt = {};
  context->receipt_index = i;
  if (a_ptr != &context->assembler || done_ptr != &context->done
      || &hs != &context->helpers || &regalloc != &context->regs
      || b == nullptr || b->tag != context->start_pc
      || pal_block != context->pal_block || !defer_pc
      || context->plan == nullptr || i >= context->plan->count
      || context->plan->ops[i].ins != ins) {
    context->receipt.error = asmjit::Error::kInvalidArgument;
    return;
  }

  context->receipt = emit_a64_planned_op(
      *context, context->plan->ops[i], i);
}

void CJitEngine::compile_block(JitBlock* b, const AlphaInstructionSource& sources,
    void* read_helper, void* write_helper, void* opcdec_helper, void* hw_mfpr_helper,
    void* hw_ld_helper, void* hw_mtpr_helper, void* hw_st_helper, void* indirect_helper,
    void* read_locked_helper, void* stc_helper, void* misc_helper, void* read_vpte_helper,
    void* read_wchk_helper, void* itof_helper, void* ftoi_helper, void* fltl_helper,
    void* fp_read_helper, void* fp_write_helper, void* fltv_helper)
{
  // Only the interpreted miss path calls this for an uncompiled block. Keep the
  // first encounter interpreted; compiled dispatch and chaining pay no counter cost.
  if (b->compile_encounters < 2) ++b->compile_encounters;
  if (b->compile_encounters < 2) return;

  // Match the x64 cold-path lifecycle.
  if (m_rt && m_code_bytes >= kReclaimBytes) {
    reclaim_code();
    b->valid = true;
  }

  // Mark the attempt
  b->compiled = true;
  b->code = nullptr;
  b->jit_body = nullptr;
  b->jit_body_same_page = nullptr;
  set_exec_proof(*b, nullptr);
  b->prefix_len = 0;
  b->body_off = 0;
  b->src_sum = 0;
  b->hash_len = 0;

#ifndef NDEBUG
  if (m_rt != nullptr) {
    auto* const validation_rt = static_cast<asmjit::JitRuntime*>(m_rt);
    static const bool emitters_valid =
        a64_validate_body_driver(validation_rt->environment(),
                                 validation_rt->cpu_features())
        && a64_validate_tail_emitters(validation_rt->environment(),
                                      validation_rt->cpu_features());
    assert(emitters_valid);
  }
#endif

  const auto source = sources.view(b->tag);
  if (!source.contains(1) || source.identity != b->phys
      || source.asm_global != b->asm_global) {
    b->compiled = false;
    return;
  }
  const A64BlockPlan plan = plan_a64_block(
      *b, source, m_profile.ipr_read, m_profile.ipr_write, m_profile.pal,
      m_profile.hw_load, m_profile.hw_store);
#ifdef JIT_STATS
  if (plan.stop == A64PlanStop::kUnsupported) {
    const uint32_t bop = plan.breaker_word >> 26;
    m_term_op[bop]++;                 // tally what cut this block (the coverage gap to chase)
    if (bop == 0x00)                  // CALL_PAL: also tally the function code (low 8 bits)
      m_pal_func[plan.breaker_word & 0xFF]++;
    else if (bop == 0x1d)             // HW_MTPR: diagnostic raw instruction[15:8], not a family selector.
      m_mtpr_encoding_hi[(plan.breaker_word >> 8) & 0xFF]++;
    else if (bop == 0x1b)             // HW_LD: tally the form (phys/virt/lock/vpte/chk, ins[15:12])
      m_hwld_func[(plan.breaker_word >> 12) & 0xF]++;
    else if (bop == 0x18)             // MISC: tally the Ra==31 form (ins[15:12]: 0xc RPCC / 0xe RC / 0xf RS)
      m_misc_func[(plan.breaker_word >> 12) & 0xF]++;
    // Punch list: one-shot print of the first ACTIONABLE breaker -- keep the exclusions
    // identical to the x64 backend so both hosts point at the same next translation target.
    if (!m_first_breaker_logged && bop != 0x00 && bop != 0x1b && bop != 0x1d && bop != 0x1f && bop != 0x10 && bop != 0x18 && bop != 0x13 && bop != 0x14 && bop != 0x17) {
      m_first_breaker_logged = true;
      printf("[JIT][PUNCH][CPU%d] first unhandled breaker: %s(0x%02x) ins=%08x at pc=%016llx%s\n",
             m_cpu_id, jit_opcode_name(bop), bop, plan.breaker_word,
             (unsigned long long) ((b->tag & ~(uint64_t) 1) + (uint64_t) plan.count * 4),
             (b->tag & 1) ? "  [PALmode]" : "");
    }
  }
#endif
  if (!a64_plan_ready_for_emission(plan)) return;
  const A64BlockExit exit = plan_a64_exit(b->tag, plan.count, plan.terminator);
  const bool pal_block = (b->tag & 1u) != 0;
  const uint32_t terminator_ins =
      plan.count != 0 ? plan.ops[plan.count - 1].ins : 0;
  const A64DirectChainContract chain = plan_a64_direct_chain(
      exit, terminator_ins, pal_block, b->pal_shadow);
  const A64IndirectChainContract indirect = plan_a64_indirect_chain(
      exit, terminator_ins, pal_block, b->pal_shadow);
  // Exactly one tail owns any supported block: direct-eligible XOR indirect-eligible.
  if (!exit.valid() || !chain.valid() || !indirect.valid()
      || chain.eligible() == indirect.eligible()) return;

  if (m_rt == nullptr) return;
  auto* const rt = static_cast<asmjit::JitRuntime*>(m_rt);
#ifndef NDEBUG
  static const bool abi_compatible =
      a64_abi_compatible(rt->environment());
  assert(abi_compatible);
#endif

  // Build a complete fixed frame.
  asmjit::CodeHolder code;
  if (code.init(rt->environment(), rt->cpu_features()) != asmjit::Error::kOk) {
    m_reclaim_pending = true;   // init only fails on OOM here: free code at the next
    b->compiled = false;        // safe point and retry, don't latch the block interpreted
    return;
  }
  JitErrorHandler eh; eh.cpu_id = m_cpu_id;
  code.set_error_handler(&eh);
#ifdef JIT_DISASM
  // capture this block's disassembly; emit failures go to the trace file
  eh.fp = m_disasm_fp;
  asmjit::StringLogger logger;
  code.set_logger(&logger);
#endif

  asmjit::a64::Assembler a;
  if (code.attach(&a) != asmjit::Error::kOk) return;
#if !defined(NDEBUG) || defined(JIT_DISASM)
  a.add_diagnostic_options(asmjit::DiagnosticOptions::kValidateAssembler);
#endif
  assert(a.is_initialized());

  RegAlloc regalloc = make_a64_block_regalloc();
  [[maybe_unused]] const HelperSet helpers = {
      read_helper, write_helper, opcdec_helper, hw_mfpr_helper, hw_ld_helper,
      hw_mtpr_helper, hw_st_helper, indirect_helper, read_locked_helper, stc_helper,
      misc_helper, read_vpte_helper, read_wchk_helper, itof_helper, ftoi_helper,
      fltl_helper, fp_read_helper, fp_write_helper, fltv_helper};
  if (emit_a64_prologue(a) != asmjit::Error::kOk) return;
  // Cold entry materializes the shared block convention. Chained entries target
  // body directly and inherit these live values plus x19-x22 from their predecessor.
  if (emit_a64_load_guest_pins(a, regalloc) != asmjit::Error::kOk) return;

  asmjit::Label done = a.new_label();
  asmjit::Label body = a.new_label();
  asmjit::Label body_same_page = a.new_label();
  asmjit::Label exec_proof = a.new_label();
  if (a.bind(body) != asmjit::Error::kOk) return;
  if (!pal_block) {
    // Check instruction access for CM. Static same-page branches enter below
    // this guard; their source checked the same 8 KiB page. The mask lives with
    // the generated code, so reusing a cache slot cannot overwrite it.
    const asmjit::Label authorized = a.new_label();
    if (emit_a64_load_cpu_u32(a, RegAlloc::kScratch0.w(), m_off.jit_mode_bit)
        != asmjit::Error::kOk) return;
    if (a.adr(RegAlloc::kScratch1, exec_proof) != asmjit::Error::kOk) return;
    if (a.ldr(RegAlloc::kScratch1.w(), asmjit::a64::ptr(RegAlloc::kScratch1,
              static_cast<int32_t>(offsetof(NativeExecProof, mode_mask))))
        != asmjit::Error::kOk) return;
    if (a.tst(RegAlloc::kScratch1.w(), RegAlloc::kScratch0.w())
        != asmjit::Error::kOk) return;
    if (a.b_ne(authorized)
        != asmjit::Error::kOk) return;
    if (emit_a64_mov_u64(a, RegAlloc::kNextPc, b->tag) != asmjit::Error::kOk)
      return;
    if (emit_a64_store_cpu_u64(a, RegAlloc::kNextPc, m_off.state_pc)
        != asmjit::Error::kOk) return;
    if (a.b(done) != asmjit::Error::kOk) return;
    if (a.bind(authorized) != asmjit::Error::kOk) return;
  }
  if (a.bind(body_same_page) != asmjit::Error::kOk) return;
#ifdef JIT_REGPROF
  // REGPROF: count every execution.
  if (emit_a64_mov_u64(a, RegAlloc::kScratch0,
                       reinterpret_cast<uintptr_t>(&b->rp_hits))
      != asmjit::Error::kOk) return;
  if (a.ldr(RegAlloc::kScratch1, asmjit::a64::ptr(RegAlloc::kScratch0))
      != asmjit::Error::kOk) return;
  if (a.add(RegAlloc::kScratch1, RegAlloc::kScratch1, asmjit::imm(1))
      != asmjit::Error::kOk) return;
  if (a.str(RegAlloc::kScratch1, asmjit::a64::ptr(RegAlloc::kScratch0))
      != asmjit::Error::kOk) return;
#endif
  // Every body begins with its forward default.
  if (emit_a64_mov_u64(a, RegAlloc::kNextPc, exit.fallthrough_pc)
      != asmjit::Error::kOk) return;

  A64EmitContext emit_context{a, m_off, m_profile, helpers, regalloc, done, b->tag,
                              pal_block, b->pal_shadow};
  emit_context.smp_memory = m_smp_memory;
  emit_context.ll_buckets = m_ll_buckets;
  emit_context.ll_watch = m_ll_watch;
  emit_context.plan = &plan;
  // Direct exits: non-PAL fall-throughs and integer branches. Chaining is verify-blind.
  bool direct_patch = false;
#ifndef JIT_VERIFY
  direct_patch = kA64DirectPatch && !pal_block && chain.eligible()
      && (exit.kind == A64ExitKind::kFallthrough
          || (exit.kind == A64ExitKind::kDirect && plan.count != 0
              && plan.ops[plan.count - 1].kind == A64OpKind::kBranchInt));
#endif
  emit_context.direct_exits = direct_patch;
  std::vector<A64DirectExit> direct_exits;
  std::vector<A64ColdStub> cold_stubs;   // production memop slow paths (cold tail)
  emit_context.cold = &cold_stubs;
  const A64BodyEmitReceipt body_emission = emit_a64_block_body(
      plan, [&](const A64DecodedOp& op, uint32_t index) {
        emit_op(&a, nullptr, &done, helpers, pal_block, b, op.ins, index,
                regalloc, &emit_context, true);
        return emit_context.receipt_index == index
            ? emit_context.receipt : A64OpEmitReceipt{};
      });
  if (!body_emission.complete_for(plan)) return;

  const asmjit::Error tail_err = direct_patch
      ? emit_a64_direct_patch_tail(emit_context, exit, terminator_ins, direct_exits)
      : chain.eligible()
      ? emit_a64_direct_chain_tail(a, m_off, chain, &b->link[0],
                                  b->tag, pal_block ? body : body_same_page,
                                  done, b->asm_global)
      : emit_a64_indirect_chain_tail(a, m_off, helpers, regalloc, indirect,
                                    &b->link[0], done, b->asm_global,
                                    A64JumpCache{&m_jcache[0], &m_jc_gen,
#ifdef JIT_STATS
                                                 &m_jc_hits
#else
                                                 nullptr
#endif
                                    });
  if (tail_err != asmjit::Error::kOk) return;

  if (a.bind(done) != asmjit::Error::kOk) return;
  if (a.mov(RegAlloc::kResultCount, RegAlloc::kChainCount.w())
      != asmjit::Error::kOk) return;
  // Every dispatcher or bailout exit converges here while x20 still names state.r[].
  if (emit_a64_sync_guest_pins(a, regalloc) != asmjit::Error::kOk) return;
  if (emit_a64_epilogue(a) != asmjit::Error::kOk) return;
#ifndef JIT_VERIFY
  // Cold tail: the outlined memop slow paths .
  for (const A64ColdStub& s : cold_stubs) {
    if (a.bind(s.slow) != asmjit::Error::kOk) return;
    const asmjit::Error serr = s.kind == A64ColdStub::kFpMem
        ? emit_a64_fp_mem_helper_seq(emit_context, s.op, s.index)
        : emit_a64_int_mem_helper_seq(emit_context, s.op, s.index,
                                      s.kind == A64ColdStub::kStore);
    if (serr != asmjit::Error::kOk) return;
    if (s.rederive) {   // rebuild {va, slot, bias} for a reusing successor
      const bool fa = s.op.opcode == 0x0b || s.op.opcode == 0x0f;
      if (emit_a64_dpc_rederive(emit_context, s.op, fa,
                                s.kind == A64ColdStub::kStore) != asmjit::Error::kOk)
        return;
    }
    if (s.restore_fwd >= 0) {   // the helper clobbered the forwarded mirror
      if (a.ldr(RegAlloc::kScratch2, asmjit::a64::ptr(RegAlloc::kRegs,
              static_cast<int32_t>(a64_guest_gpr_slot(
                  static_cast<uint32_t>(s.restore_fwd), b->pal_shadow) * 8)))
          != asmjit::Error::kOk)
        return;
    }
    if (a.b(s.join) != asmjit::Error::kOk) return;
  }
  if (emit_a64_direct_patch_cold(emit_context, direct_exits, b->asm_global)
      != asmjit::Error::kOk) return;
#endif
  if (!pal_block) {
    if (a.align(asmjit::AlignMode::kData, alignof(NativeExecProof))
        != asmjit::Error::kOk) return;
    if (a.bind(exec_proof) != asmjit::Error::kOk) return;
    for (size_t offset = 0; offset < sizeof(NativeExecProof); offset += sizeof(uint64_t))
      if (a.embed_uint64(0) != asmjit::Error::kOk) return;
  }
  if (a.finalize() != asmjit::Error::kOk) return;

  const A64PendingPublication pending = prepare_a64_block_publication(
      *b, plan, exit, body_emission, sources, code, body, m_code_bytes);
  if (!pending.ready()) {
    // A source race is transient 
    if (pending.source_raced) b->compiled = false;
    return;
  }
  assert(pending.body_off != 0);  // Chained entry must remain past the cold prologue.

#ifdef JIT_DISASM
  {
    FILE* out = m_disasm_fp ? m_disasm_fp : stderr;
    fprintf(out, "[JIT][CPU%d] block @ %016llx%s  (%u instr, %llu bytes)\n%s\n",
            m_cpu_id, (unsigned long long) (b->tag & ~(uint64_t) 1),
            (b->tag & 1) ? " PAL" : "", plan.count,
            (unsigned long long) pending.code_size, logger.data());
    fflush(out);   // per-block flush: preserve the trace if JIT'd code later crashes
  }
#endif
  if (eh.failed) return;   // emit error already reported so don't ship a broken block

  JitFn fn = nullptr;
  const asmjit::Error add_err = rt->add(&fn, &code);
  if (add_err != asmjit::Error::kOk) {
    if (add_err == asmjit::Error::kOutOfMemory) {   // transient: reclaim at the next safe point, then retry
      m_reclaim_pending = true;
      b->compiled = false;
    }                                                // any other add() error is deterministic -> stays terminal
    printf("[JIT][CPU%d] JitRuntime::add failed (err=%u%s) for block @ %016llx\n", m_cpu_id,
           (unsigned) add_err, add_err == asmjit::Error::kOutOfMemory ? ", OOM: will reclaim+retry" : ", terminal",
           (unsigned long long) (b->tag & ~(uint64_t) 1));
    return;
  }
  // add() can allocate/copy for long enough that another CPU modifies the guest source.
  // Never publish old code with a hash of new bytes.
  if (eh.failed) { (void)rt->release(fn); return; }   // late deterministic emit error -> terminal
  const A64PendingPublication committed = prepare_a64_block_publication(
      *b, plan, exit, body_emission, sources, code, body, m_code_bytes);
  if (!committed.ready()) {
    (void)rt->release(fn);
    if (committed.source_raced) b->compiled = false;   // post-add source race -> retry
    return;
  }

  void* const jit_body = static_cast<void*>(
      reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(fn)) + committed.body_off);
  b->jit_body_same_page = pal_block ? nullptr : static_cast<void*>(
      reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(fn))
      + code.label_offset(body_same_page));
  set_exec_proof(*b, pal_block ? nullptr : reinterpret_cast<NativeExecProof*>(
      reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(fn))
      + code.label_offset(exec_proof)));
  b->body_off = static_cast<uint32_t>(committed.body_off);
  b->src_sum = committed.source_hash;
  b->hash_len = committed.hash_len;
  b->prefix_len = committed.prefix_len;
#ifdef JIT_REGPROF
  // Pin-selection mask.
  b->rp_mask = plan.gpr_usage.reads | plan.gpr_usage.writes;
  b->rp_csz = static_cast<uint32_t>(committed.code_size);
#endif
  m_code_bytes = committed.prior_code_bytes + committed.code_size;
#ifdef JIT_STATS
  m_stat_compiled++;
  m_stat_plen_sum += committed.prefix_len;
  m_stat_code_bytes += committed.code_size;
#endif
  b->code = fn;
  b->jit_body = jit_body;  // Runnable chain entry publishes last.
#ifdef JIT_REGPROF
  m_dbg_source = sources;
#endif
}

void CJitEngine::build_trampolines() {}   // A64 blocks carry their own prologue/epilogue

// Trace tier: stub until X86 variant is improved or removed. 
void CJitEngine::compile_trace(TraceFragment*, JitBlock**, uint32_t,
                               const AlphaInstructionSource&, const HelperSet&) {}

#endif // ES40_JIT_A64

#endif // ES40_JIT

// Keeps this translation unit non-empty when ARM64 is not the build host (MSVC LNK4221).
extern const char jit_a64_backend_tu;
const char jit_a64_backend_tu = 0;
