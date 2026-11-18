/* ES40 emulator -- EV4 IEEE exceptions
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
#if !defined(INCLUDED_EV4IEEETRAP_H)
#define INCLUDED_EV4IEEETRAP_H

#include "AlphaIEEETrap.h"

constexpr bool ev4_ieee_divide(std::uint32_t instruction)
{
  const auto function = (instruction >> 5) & 0x3f;
  return (instruction >> 26) == 0x16 && (function == 0x03 || function == 0x23);
}

constexpr AlphaIEEETrapResult ev4_ieee_evaluate(std::uint64_t fpcr, std::uint64_t trap,
    std::uint32_t instruction_enable, AlphaIEEETrapMasking, std::uint32_t instruction)
{
  const auto flags = trap & 0x7e;
  // Original 21064 DIV never sets FPCR.INE (HRM 2.6/2.7).
  const auto sticky = ev4_ieee_divide(instruction) ? flags & ~std::uint64_t(0x20) : flags;
  fpcr |= sticky << 51;
  if (instruction_enable && flags)
    return {true, flags | ((instruction & 0x8000) ? 1u : 0u), fpcr};
  return {false, 0, fpcr};
}

constexpr AlphaIEEEInexactRule ev4_ieee_describe_inexact(std::uint32_t instruction)
{
  return ev4_ieee_divide(instruction) ? AlphaIEEEInexactRule{AlphaIEEEInexactKind::Noop, 0}
    : AlphaIEEEInexactRule{AlphaIEEEInexactKind::FpcrBitSet, 56};
}

constexpr std::uint64_t ev4_ieee_underflow_result(bool)
{
  return 0; // Underflow produces positive zero.
}

inline constexpr AlphaIEEETrapPolicy kEv4IEEETrapPolicy{
  &ev4_ieee_evaluate, &ev4_ieee_describe_inexact, &ev4_ieee_underflow_result, nullptr,
  static_cast<std::uint32_t>(AlphaIEEENonfiniteInput::Infinity)
    | static_cast<std::uint32_t>(AlphaIEEENonfiniteInput::QuietNaN)
    | static_cast<std::uint32_t>(AlphaIEEENonfiniteInput::SignalingNaN),
  false, true, true
};

#endif
