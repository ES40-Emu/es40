/* ES40 emulator -- Alpha CPU identity profiles
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
#if !defined(INCLUDED_ALPHACPUPROFILE_H)
#define INCLUDED_ALPHACPUPROFILE_H

#include <cstdint>
#include "AlphaDataFault.h"
#include "AlphaInstructionSet.h"
#include "AlphaRegisterBank.h"
#include "Ev68InstructionSet.h"
#include "Ev68PAL.h"
#include "Ev68IPR.h"
#include "Ev68MMU.h"
#include "Ev68HWLoad.h"
#include "Ev68HWStore.h"
#include "Ev68Reset.h"
#include "Ev68InstructionFetch.h"
#include "Ev68Interrupt.h"

// Immutable model identity, execution capabilities, and family rules chosen by configuration.
struct AlphaCPUProfile
{
  std::uint64_t amask;       // Execution capabilities; AMASK returns input & ~amask.
  std::uint32_t implver;     // Architectural implementation version.
  std::uint32_t chip_id;     // Implementation-specific chip revision identifier.
  std::uint32_t type_major;  // System-reference processor type.
  std::uint32_t type_minor;  // System-reference processor revision.
  AlphaPALPolicy pal = kEv68PALPolicy;  // Immutable family entry rules.
  AlphaIPRReadPolicy ipr_read = kEv68IPRReadPolicy; // Immutable family IPR reads.
  AlphaIPRWritePolicy ipr_write = kEv68IPRWritePolicy; // Immutable family IPR writes.
  AlphaTBFillPolicy tb_fill = kEv68TBFillPolicy; // Immutable family PTE decoding.
  AlphaMMUAddressPolicy mmu_address = kEv68MMUAddressPolicy; // Immutable address rules.
  AlphaHWLoadPolicy hw_load = kEv68HWLoadPolicy; // Immutable hardware-load forms.
  AlphaHWStorePolicy hw_store = kEv68HWStorePolicy; // Immutable hardware-store forms.
  bool jit_supported = true; // Disable only while the JIT lacks support for this family's fetch or execution rules.
  AlphaResetPolicy reset = kEv68ResetPolicy; // Family state for init and system reset.
  AlphaInstructionFetchPolicy instruction_fetch = kEv68InstructionFetchPolicy;
  AlphaInstructionDecodePolicy instruction_decode = kEv68InstructionDecodePolicy;
  AlphaRegisterBankPolicy register_bank = kEv68RegisterBankPolicy;
  AlphaDataFaultPolicy data_fault = kEv68DataFaultPolicy;
  AlphaTBMissPolicy tb_miss = kEv68TBMissPolicy;
  AlphaInstructionFaultPolicy instruction_fault = kEv68InstructionFaultPolicy;
  AlphaInterruptPolicy interrupt = kEv68InterruptPolicy;

  constexpr bool supports_instruction(std::uint32_t ins) const
  {
    return alpha_instruction_supported(amask, ins);
  }

  constexpr bool retires_instruction_at_decode(std::uint32_t ins) const
  {
    return alpha_instruction_retires_at_decode(instruction_decode, ins);
  }
};

// EV68CB pass 4: BWX | FIX | CIX | MVI | precise traps | prefetch modify.
inline constexpr AlphaCPUProfile kEv68CBProfile{0x1307, 2, 0x21, 12, 6};

#endif
