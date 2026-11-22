/* ES40 emulator -- EV4 CPU identity profile
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
#if !defined(INCLUDED_EV4CPUPROFILE_H)
#define INCLUDED_EV4CPUPROFILE_H

#include "Ev4Reset.h"
#include "Ev4InstructionFetch.h"
#include "Ev4IPR.h"
#include "Ev4CPUState.h"
#include "Ev4InstructionSet.h"
#include "Ev4PAL.h"
#include "Ev4HWLoad.h"
#include "Ev4HWStore.h"
#include "Ev4FPCR.h"
#include "Ev4IEEETrap.h"

inline void ev4_report_arithmetic_trap(CAlphaCPU& cpu, u64 flags, u32 dest, AlphaArithmeticTrapKind kind)
{
  ev4_record_arithmetic_trap(cpu.ev4_arithmetic_state(), flags, dest, kind);
}

inline constexpr AlphaArithmeticTrapPolicy kEv4ArithmeticTrapPolicy{&ev4_report_arithmetic_trap};

// The first D-stream fault latches VA and MM_CSR together (21064 HRM 4.7.1.2, 5.3.4, 5.3.5).
inline int ev4_memory_fault(CAlphaCPU& cpu, u64 address, int flags, u32 ins, unsigned status,
                            AlphaPALException reason)
{
  auto& state = cpu.state;
  if (!(flags & ACCESS_EXEC) && !state.ev4_va_locked) {
    state.fault_va = address;
    state.mm_stat = (I_GETOP(ins) << 9) | (I_GETRA(ins) << 4) | status | (flags & ACCESS_WRITE);
    state.ev4_va_locked = 1;
  }
  // EXC_ADDR saves the ITB miss target or the issuing D-stream PC (21064 HRM 4.7.1).
  const u64 restart_pc = (flags & ACCESS_EXEC) ? address : state.current_pc;
  cpu.enter_pal_exception(cpu.pal_exception_entry(reason, restart_pc));
  return -1;
}

// MM_CSR reports FOR and FOW even alongside ACV (21064 HRM Tables 4-8, 5-9).
constexpr unsigned ev4_mm_fault_bits(int flags, bool on_read, bool on_write)
{
  const unsigned for_bit = on_read && !(flags & ACCESS_WRITE) ? 4 : 0;
  const unsigned fow_bit = on_write && (flags & (ACCESS_WRITE | WRCHK)) ? 8 : 0;
  return for_bit | fow_bit;
}

constexpr AlphaCPUProfile ev4_cpu_profile()
{
  // Production 21064, pass 3 (21064 HRM Table 5-15; Alpha A.R.M. Table E-1).
  AlphaCPUProfile profile{0, 0, 7, 2, 1};
  profile.pal = {&ev4_pal_exception_entry, &ev4_call_pal_valid, &ev4_call_pal_entry,
                 &ev4_hardware_instruction_access, &ev4_hardware_return};
  profile.ipr_read = kEv4IPRReadPolicy;
  profile.ipr_write = kEv4IPRWritePolicy;
  profile.hw_load = kEv4HWLoadPolicy;
  profile.hw_store = kEv4HWStorePolicy;
  profile.reset = kEv4ResetPolicy;
  profile.instruction_fetch = kEv4InstructionFetchPolicy;
  profile.instruction_decode = kEv4InstructionDecodePolicy;
  profile.fpcr = kEv4FPCRPolicy;
  profile.arithmetic_trap = kEv4ArithmeticTrapPolicy;
  profile.ieee_trap = kEv4IEEETrapPolicy;
  profile.cycle_counter = kAlphaCycleCounterPolicy;
  profile.cpu_state = {nullptr, kEv4JitVerifyPolicy, AlphaCPUSnapshotSignature{0x210640e4, 0x4e046012}};

  // 21064 HRM 5.2.3: ICCSR controls FPE, kernel HWE and the I-stream ASN.
  profile.fp_start.enabled = [](const CAlphaCPU& cpu) { return (cpu.state.ibox_ctl & kEv4IccsrFpe) != 0; };
  profile.pal.access_context = [](const CAlphaCPU& cpu) -> AlphaPALAccessContext {
    return {static_cast<std::uint32_t>(cpu.state.cm), (cpu.state.ibox_ctl & kEv4IccsrHwe) != 0};
  };
  profile.mmu_address.access = [](const CAlphaCPU& cpu, int flags) -> AlphaMMUAccess {
    return {(flags & ALT) ? cpu.state.alt_cm : cpu.state.cm,
            (flags & ACCESS_EXEC) ? static_cast<int>(ev4_iccsr_asn(cpu.state.ibox_ctl)) : 0};
  };
  // 21064 HRM 2.3.2, 2.5.1 and Table 5-10: I-stream MAP and D-stream superpages.
  profile.mmu_address.map = [](const CAlphaCPU& cpu, u64 va, int flags, int mode) -> AlphaMMUAddress {
    if ((va >> 42) != 0 && (va >> 42) != (UINT64_MAX >> 42))
      return {AlphaMMUAddressKind::Invalid, 0};
    const auto control = cpu.m_ev4_dcache.control;
    const bool exec = (flags & ACCESS_EXEC) != 0;
    if (((va >> 41) & 3) == 2 && (exec ? (cpu.state.ibox_ctl & kEv4IccsrMap) : (control & Ev4Dcache::spe_2)))
      return {mode ? AlphaMMUAddressKind::AccessViolation : AlphaMMUAddressKind::Direct, va & U64(0x3ffffffff)};
    if (!exec && (control & Ev4Dcache::spe_1) && ((va >> 30) & 0x1fff) == 0x1ffe)
      return {mode ? AlphaMMUAddressKind::AccessViolation : AlphaMMUAddressKind::Direct, va & U64(0x3fffffff)};
    return {AlphaMMUAddressKind::TranslationBuffer, 0};
  };
  profile.tb_fill.base_page_match_mask = U64(0x7ffffffe000);
  profile.tb_fill.decode = [](const CAlphaCPU&, u64, u64, u64, int) -> AlphaTBFill {
    FAILURE(NotImplemented, "EV4 translation buffer fill");
  };
  profile.tb_miss.handle = [](CAlphaCPU& cpu, u64 va, int flags, u32 ins) {
    const auto reason = (flags & ACCESS_EXEC) ? AlphaPALException::InstructionTlbMiss : AlphaPALException::DataTlbMiss;
    return ev4_memory_fault(cpu, va, flags, ins, 0, reason);
  };
  profile.instruction_fault.handle = [](CAlphaCPU& cpu, u64 va, int flags, u32 ins, AlphaInstructionFaultSource) {
    return ev4_memory_fault(cpu, va, flags, ins, 0, AlphaPALException::InstructionAccessViolation);
  };
  profile.data_fault.unaligned = [](CAlphaCPU& cpu, u64 va, int flags, u32 ins, int) {
    ev4_memory_fault(cpu, va, flags, ins, 0, AlphaPALException::Unaligned);
  };
  profile.data_fault.access_violation = [](CAlphaCPU& cpu, u64 va, int flags, u32 ins, AlphaDataFaultSource source) {
    const auto status = 2 | ev4_mm_fault_bits(flags, source.fault_on_read, source.fault_on_write);
    return ev4_memory_fault(cpu, va, flags, ins, status, AlphaPALException::DataFault);
  };
  profile.data_fault.access_fault = [](CAlphaCPU& cpu, u64 va, int flags, u32 ins, bool on_read, bool on_write) {
    const auto status = ev4_mm_fault_bits(flags, on_read, on_write);
    return ev4_memory_fault(cpu, va, flags, ins, status, AlphaPALException::DataFault);
  };

  // 21064 HRM 2.3.3 and 5.2.14-19: only enabled requests interrupt native mode.
  profile.interrupt.pending = [](CAlphaCPU& cpu) {
    const auto& state = cpu.state;
    return ev4_interrupt_summary(state, state.eir.load()) != 0;
  };
  profile.interrupt.service = [](CAlphaCPU& cpu) {
    return cpu.get_profile().interrupt.pending(cpu) ? AlphaInterruptResult::EnterPAL : AlphaInterruptResult::Continue;
  };
  // Use the 21064 HRM Fig. A-26 example until Jensen supplies its own lock flag (PB22H-KB, p. 3-2).
  profile.llsc.matches = [](u64 locked, u64 address) { return (locked >> 5) == (address >> 5); };
  return profile;
}

inline constexpr AlphaCPUProfile kEv4Profile = ev4_cpu_profile();

#endif
