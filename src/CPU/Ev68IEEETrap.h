/* ES40 emulator -- EV68 IEEE exceptions
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
#if !defined(INCLUDED_EV68IEEETRAP_H)
#define INCLUDED_EV68IEEETRAP_H

#include "AlphaIEEETrap.h"

class Ev68IEEETrap
{
public:
  static AlphaIEEETrapResult evaluate(CAlphaCPU& cpu, std::uint64_t trap,
      std::uint32_t instruction_enable, std::uint64_t fpcr_disable,
      std::uint32_t instruction);

  static constexpr AlphaIEEEInexactRule describe_inexact(std::uint32_t)
  {
    return {AlphaIEEEInexactKind::FpcrBitSet, 56}; // FPCR.INE already set.
  }
};

inline constexpr AlphaIEEETrapPolicy kEv68IEEETrapPolicy{
  &Ev68IEEETrap::evaluate, &Ev68IEEETrap::describe_inexact
};

#endif
