/* ES40 emulator -- EV4 privileged-register rules
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#if !defined(INCLUDED_EV4IPR_H)
#define INCLUDED_EV4IPR_H

#include "AlphaIPR.h"

struct Ev4IPRSelector
{
  std::uint32_t index;
  std::uint32_t banks;
};

// PAL[7], ABX[6], IBX[5], INDEX[4:0] (HRM Table 4-6).
constexpr Ev4IPRSelector ev4_ipr_selector(std::uint32_t instruction)
{
  return {instruction & 0x1f, instruction & 0xe0};
}

constexpr bool ev4_ipr_ignored(Ev4IPRSelector selector, bool pal_mode)
{
  // HRM 4.6(10); mixed-bank accesses stay with their handlers.
  return selector.banks == 0 || (!pal_mode && selector.banks == 0x20
    && (selector.index == 0 || selector.index == 1 || selector.index == 3));
}

// Unimplemented registers stay with the interpreter.
constexpr AlphaIPRReadDescriptor ev4_describe_ipr_read(std::uint32_t instruction,
                                                      bool pal_mode)
{
  if ((instruction >> 26) != 0x19)
    return {AlphaIPRReadKind::Unsupported, false, false};
  const auto selector = ev4_ipr_selector(instruction);
  if (ev4_ipr_ignored(selector, pal_mode))
    return {AlphaIPRReadKind::Noop, false, true};
  if (selector.banks == 0x80) // PAL_TEMP only.
    return {AlphaIPRReadKind::Helper, false, true};
  if (selector.banks == 0x20 && selector.index == 4) // EXC_ADDR.
    return {AlphaIPRReadKind::ExceptionAddress, false, true};
  if (selector.banks == 0x20 && selector.index == 10) // EXC_SUM shifts MSK, even to R31.
    return {AlphaIPRReadKind::Helper, false, false};
  if (selector.banks == 0x20 && selector.index == 11) // PAL_BASE.
    return {AlphaIPRReadKind::PalBase, false, true};
  return {AlphaIPRReadKind::Unsupported, false, false};
}

constexpr AlphaIPRWriteKind ev4_describe_ipr_write(std::uint32_t instruction,
                                                 bool pal_mode)
{
  if ((instruction >> 26) != 0x1d)
    return AlphaIPRWriteKind::Interpreter;
  const auto selector = ev4_ipr_selector(instruction);
  if (ev4_ipr_ignored(selector, pal_mode))
    return AlphaIPRWriteKind::Noop;
  if (selector.banks == 0x80) // PAL_TEMP only.
    return AlphaIPRWriteKind::Helper;
  if (selector.banks == 0x20 && selector.index == 10) // EXC_SUM.
    return AlphaIPRWriteKind::Helper;
  return AlphaIPRWriteKind::Interpreter;
}

// PAL_TEMP reset contents are architecturally undefined.
struct Ev4PALTempState
{
  std::uint64_t r[32]{};
};

constexpr std::uint64_t ev4_read_pal_temp(const Ev4PALTempState& state,
                                         std::uint32_t index)
{
  return state.r[index & 31];
}

constexpr void ev4_write_pal_temp(Ev4PALTempState& state, std::uint32_t index,
                                  std::uint64_t value)
{
  state.r[index & 31] = value;
}

#endif
