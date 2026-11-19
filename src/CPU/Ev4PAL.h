/* ES40 emulator -- EV4 PAL rules
 * Copyright (C) 2026 by gdwnldsKSC
 * All rights reserved.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#if !defined(INCLUDED_EV4PAL_H)
#define INCLUDED_EV4PAL_H

#include "AlphaPAL.h"

// EV4 PAL entry offsets; reservations remain with PALcode and system logic.
constexpr AlphaPALExceptionEntry ev4_pal_exception_entry(AlphaPALException reason,
                                                        std::uint64_t current_pc)
{
  const auto saved_pc = current_pc & ~std::uint64_t(2);
  switch (reason)
  {
  case AlphaPALException::Reset:                       return {0x0000, saved_pc, false};
  // EXC_ADDR[1] marks the restart PC as correct for MCHK/ARITH.
  case AlphaPALException::MachineCheck:                return {0x0020, saved_pc | 2, false};
  // Arithmetic completes with no trap shadow.
  case AlphaPALException::Arithmetic:                  return {0x0060, (saved_pc + 4) | 2, false};
  case AlphaPALException::Interrupt:                   return {0x00e0, saved_pc, false};
  case AlphaPALException::DataFault:                    return {0x01e0, saved_pc, false};
  case AlphaPALException::InstructionTlbMiss:           return {0x03e0, saved_pc, false};
  case AlphaPALException::InstructionAccessViolation:  return {0x07e0, saved_pc, false};
  case AlphaPALException::DataTlbMiss:
    return {(current_pc & 1) ? 0x09e0u : 0x08e0u, saved_pc, false};
  case AlphaPALException::Unaligned:                   return {0x11e0, saved_pc, false};
  case AlphaPALException::OpcodeDecode:                return {0x13e0, saved_pc, false};
  case AlphaPALException::FloatingPointDisabled:       return {0x17e0, saved_pc, false};
  default: return {0, saved_pc, false, false};
  }
}

constexpr bool ev4_call_pal_valid(std::uint32_t function, std::uint32_t mode)
{
  return function <= 0x3f ? mode == 0 : function >= 0x80 && function <= 0xbf;
}

// CALL_PAL saves the next PC in EXC_ADDR without a GPR link.
constexpr AlphaPALCallEntry ev4_call_pal_entry(std::uint32_t function,
                                               std::uint64_t,
                                               std::uint64_t next_pc)
{
  const std::uint64_t offset = std::uint64_t(0x2000)
    | (std::uint64_t(function & 0x80) << 5)
    | (std::uint64_t(function & 0x3f) << 6) | 1;
  return {offset, next_pc & ~std::uint64_t(2), 0, 0, 0, false};
}

// HW_REI uses EXC_ADDR; PALcode handles reservation clearing.
constexpr AlphaPALReturnDescriptor ev4_hardware_return(std::uint32_t instruction)
{
  if ((instruction >> 26) != 0x1e)
    return {};
  AlphaPALReturnDescriptor entry{true, ~std::uint64_t(2), 0, false, true,
                                 AlphaPALReturnSource::ExceptionAddress};
#if defined(IDB)
  entry.mnemonic = "HW_REI";
#endif
  return entry;
}

constexpr AlphaPALInstructionAccess ev4_hardware_instruction_access(std::uint32_t instruction)
{
  switch (instruction >> 26)
  {
  case 0x19: // HW_MFPR.
  case 0x1b: // HW_LD.
  case 0x1d: // HW_MTPR.
  case 0x1e: // HW_REI.
  case 0x1f: // HW_ST.
    return AlphaPALInstructionAccess::PalOrKernelHwe;
  default:
    return AlphaPALInstructionAccess::Denied;
  }
}

#endif
