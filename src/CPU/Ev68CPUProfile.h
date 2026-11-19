/* ES40 emulator -- EV68 CPU identity profile
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
#if !defined(INCLUDED_EV68CPUPROFILE_H)
#define INCLUDED_EV68CPUPROFILE_H

#include "AlphaCPUProfile.h"
#include "Ev68InstructionSet.h"
#include "Ev68PAL.h"
#include "Ev68IPR.h"
#include "Ev68MMU.h"
#include "Ev68HWLoad.h"
#include "Ev68HWStore.h"
#include "Ev68LLSC.h"
#include "Ev68Reset.h"
#include "Ev68CPUState.h"
#include "Ev68InstructionFetch.h"
#include "Ev68Interrupt.h"
#include "Ev68FPCR.h"
#include "Ev68FPStart.h"
#include "AlphaCycleCounter.h"
#include "Ev68Idle.h"
#include "Ev68ArithmeticTrap.h"
#include "Ev68IEEETrap.h"

// EV68CB pass 4: BWX | FIX | CIX | MVI | precise traps | prefetch modify.
inline constexpr AlphaCPUProfile kEv68CBProfile = [] {
  AlphaCPUProfile profile{0x1307, 2, 0x21, 12, 6};
  profile.pal = kEv68PALPolicy;
  profile.ipr_read = kEv68IPRReadPolicy;
  profile.ipr_write = kEv68IPRWritePolicy;
  profile.tb_fill = kEv68TBFillPolicy;
  profile.mmu_address = kEv68MMUAddressPolicy;
  profile.hw_load = kEv68HWLoadPolicy;
  profile.hw_store = kEv68HWStorePolicy;
  profile.reset = kEv68ResetPolicy;
  profile.instruction_fetch = kEv68InstructionFetchPolicy;
  profile.instruction_decode = kEv68InstructionDecodePolicy;
  profile.register_bank = kEv68RegisterBankPolicy;
  profile.data_fault = kEv68DataFaultPolicy;
  profile.tb_miss = kEv68TBMissPolicy;
  profile.instruction_fault = kEv68InstructionFaultPolicy;
  profile.interrupt = kEv68InterruptPolicy;
  profile.fpcr = kEv68FPCRPolicy;
  profile.fp_start = kEv68FPStartPolicy;
  profile.arithmetic_trap = kEv68ArithmeticTrapPolicy;
  profile.ieee_trap = kEv68IEEETrapPolicy;
  profile.llsc = kEv68LLSCPolicy;
  profile.cpu_state = kEv68CPUStatePolicy;
  profile.cycle_counter = kAlphaCycleCounterPolicy;
  profile.idle = kEv68IdlePolicy;
#if defined(IDB)
  profile.ipr_name = &ev68_ipr_name;
  profile.call_pal_name = &ev68_call_pal_name;
  profile.trace_address_fallback = &ev68_trace_address_fallback;
#endif
  return profile;
}();

#endif
