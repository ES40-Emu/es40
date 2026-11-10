/* ES40 emulator -- Alpha PAL policy
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
#if !defined(INCLUDED_ALPHAPAL_H)
#define INCLUDED_ALPHAPAL_H

#include <cstdint>

// Shared exceptions; each CPU supplies its own PAL entry rules.
enum class AlphaPALException
{
  DataTlbDoubleMiss43,
  DataTlbDoubleMiss48,
  FloatingPointDisabled,
  Unaligned,
  DataTlbMiss,
  DataFault,
  OpcodeDecode,
  InstructionAccessViolation,
  MachineCheck,
  InstructionTlbMiss,
  Arithmetic,
  Interrupt,
  MtFpcr,
  Reset
};

struct AlphaPALExceptionEntry
{
  std::uint64_t offset;    // Exception vector offset, withuot PALmode bit 0.
  std::uint64_t saved_pc;  // Value for EXC_ADDR.
  bool clear_lock;
};

struct AlphaPALCallEntry
{
  std::uint64_t offset;    // CALL_PAL entry offset, with PALmode bit 0.
  std::uint64_t saved_pc;  // Value for EXC_ADDR.
  // Link value and slots are used only when write_link is true.
  std::uint64_t return_pc;
  unsigned link_register;
  unsigned shadow_link_register;
  bool write_link = true;
};

enum class AlphaPALReturnSource
{
  RegisterB,
  ExceptionAddress
};

// Decoded PAL return rules, outside saved CPU state. Read the selected source
// at execution time; forms with side effects stay interpreted.
struct AlphaPALReturnDescriptor
{
  bool valid = false;
  std::uint64_t target_mask = ~std::uint64_t(0);
  std::uint64_t target_bits = 0;
  bool clear_lock = false;
  bool compilable = false;
  AlphaPALReturnSource source = AlphaPALReturnSource::RegisterB;
};

constexpr bool alpha_pal_return_valid(const AlphaPALReturnDescriptor& entry)
{
  return entry.valid && (entry.source == AlphaPALReturnSource::RegisterB
    || entry.source == AlphaPALReturnSource::ExceptionAddress);
}

constexpr std::uint64_t alpha_pal_return_target(
    const AlphaPALReturnDescriptor& entry, std::uint64_t register_b,
    std::uint64_t exception_address)
{
  const std::uint64_t source = entry.source == AlphaPALReturnSource::ExceptionAddress
    ? exception_address : register_b;
  return (source & entry.target_mask) | entry.target_bits;
}

constexpr bool alpha_pal_return_compilable(const AlphaPALReturnDescriptor& entry)
{
  return alpha_pal_return_valid(entry) && entry.compilable && !entry.clear_lock;
}

// Family access rules for the PAL-reserved hardware instructions.
enum class AlphaPALInstructionAccess
{
  Denied,
  PalOnly,
  PalOrKernelHwe
};

constexpr bool alpha_pal_instruction_allowed(AlphaPALInstructionAccess access,
                                              bool pal_mode, std::uint32_t mode,
                                              bool hwe)
{
  switch (access)
  {
  case AlphaPALInstructionAccess::Denied: return false;
  case AlphaPALInstructionAccess::PalOnly: return pal_mode;
  case AlphaPALInstructionAccess::PalOrKernelHwe:
    return pal_mode || (mode == 0 && hwe);
  }
  return false;
}

// These rules guarantee access throughout a PAL block. 
// Native HWE execution stays interpreted.
constexpr bool alpha_pal_instruction_compilable(AlphaPALInstructionAccess access)
{
  return access == AlphaPALInstructionAccess::PalOnly
    || access == AlphaPALInstructionAccess::PalOrKernelHwe;
}

// Stateless family PAL rules, copied with the CPU profile.
struct AlphaPALPolicy
{
  AlphaPALExceptionEntry (*exception_entry)(AlphaPALException reason,
                                            std::uint64_t current_pc);
  bool (*call_pal_valid)(std::uint32_t function, std::uint32_t mode);
  AlphaPALCallEntry (*call_pal_entry)(std::uint32_t function,
                                     std::uint64_t current_pc,
                                     std::uint64_t next_pc);
  // Pass the complete instruction so restrictions on individual forms apply.
  // An absent access rule denies all.
  AlphaPALInstructionAccess (*hardware_instruction_access)(std::uint32_t instruction) = nullptr;
  AlphaPALReturnDescriptor (*hardware_return)(std::uint32_t instruction) = nullptr;
};

constexpr AlphaPALReturnDescriptor alpha_pal_return_descriptor(
    const AlphaPALPolicy& policy, std::uint32_t instruction)
{
  return policy.hardware_return ? policy.hardware_return(instruction)
                                : AlphaPALReturnDescriptor{};
}

#endif
