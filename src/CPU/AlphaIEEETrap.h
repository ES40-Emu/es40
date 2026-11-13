/* ES40 emulator -- Alpha IEEE exception policy
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
#if !defined(INCLUDED_ALPHAIEEETRAP_H)
#define INCLUDED_ALPHAIEEETRAP_H

#include <cstdint>

class CAlphaCPU;

struct AlphaIEEETrapResult
{
  bool deliver;
  std::uint64_t flags;
};

enum class AlphaIEEEInexactKind
{
  Interpreter,
  Noop,
  FpcrBitSet
};

struct AlphaIEEEInexactRule
{
  AlphaIEEEInexactKind kind = AlphaIEEEInexactKind::Interpreter;
  std::uint32_t fpcr_bit = 0;

  constexpr bool operator==(const AlphaIEEEInexactRule& other) const
  {
    return kind == other.kind && fpcr_bit == other.fpcr_bit;
  }
};

struct AlphaIEEETrapPolicy
{
  // Updates family state before the caller reports a delivered arithmetic trap.
  AlphaIEEETrapResult (*evaluate)(CAlphaCPU& cpu, std::uint64_t trap,
      std::uint32_t instruction_enable, std::uint64_t fpcr_disable,
      std::uint32_t instruction);

  // Pure rule for effect-free INE skips (enable=0); keep paired with evaluate.
  // Missing rules defer; exact paths need no rule.
  AlphaIEEEInexactRule (*describe_inexact)(std::uint32_t instruction) = nullptr;
};

inline AlphaIEEEInexactRule alpha_ieee_inexact_rule(
    const AlphaIEEETrapPolicy& policy, std::uint32_t instruction)
{
  if (!policy.describe_inexact) return {};
  const auto rule = policy.describe_inexact(instruction);
  if (rule.kind == AlphaIEEEInexactKind::Noop)
    return {AlphaIEEEInexactKind::Noop, 0};
  if (rule.kind == AlphaIEEEInexactKind::FpcrBitSet && rule.fpcr_bit < 64)
    return rule;
  return {};
}

#endif
