/* ES40 emulator -- Alpha register-bank routing
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
#if !defined(INCLUDED_ALPHAREGISTERBANK_H)
#define INCLUDED_ALPHAREGISTERBANK_H

#include <cstdint>

// Operand routing into the existing two banks. R31 never shadows.
struct AlphaRegisterBankPolicy
{
  std::uint32_t pal_shadow_mask = 0;

  constexpr bool shadowable(std::uint32_t reg) const
  {
    return reg < 31 && (pal_shadow_mask & (std::uint32_t{1} << reg)) != 0;
  }

  constexpr std::uint32_t slot(std::uint32_t raw_reg, bool pal_shadow) const
  {
    const auto reg = raw_reg & 31u;
    return reg + (pal_shadow && shadowable(reg) ? 32u : 0u);
  }
};

#endif
