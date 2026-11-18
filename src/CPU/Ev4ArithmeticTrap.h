/* ES40 emulator -- EV4 arithmetic-trap state
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
#if !defined(INCLUDED_EV4ARITHMETICTRAP_H)
#define INCLUDED_EV4ARITHMETICTRAP_H

#include "AlphaArithmeticTrap.h"

struct Ev4ArithmeticTrapState
{
  std::uint64_t summary = 0;
  std::uint64_t write_mask = 0;
  bool swc_blocked = false;
};

constexpr void ev4_record_arithmetic_trap(Ev4ArithmeticTrapState& state,
    std::uint64_t flags, std::uint32_t destination, AlphaArithmeticTrapKind kind)
{
  const bool floating = kind == AlphaArithmeticTrapKind::FloatingPoint;
  state.summary |= (flags & 0x7e) << 2;
  state.write_mask |= std::uint64_t(1) << ((destination & 31) + (floating ? 32 : 0));
  if (floating)
  {
    // A non-/S floating trap blocks SWC until EXC_SUM is written.
    state.swc_blocked |= (flags & 1) == 0;
    if (state.swc_blocked)
      state.summary &= ~std::uint64_t(4);
    else
      state.summary |= 4;
  }
}

// EXC_SUM.MSK shifts out F31-F0, then I31-I0 (HRM 5.2.12).
constexpr std::uint64_t ev4_read_exception_summary(Ev4ArithmeticTrapState& state)
{
  const auto value = (state.summary & 0x1fc) | ((state.write_mask >> 63) << 33);
  state.write_mask <<= 1;
  return value;
}

// Writes leave the serial register mask intact.
constexpr void ev4_write_exception_summary(Ev4ArithmeticTrapState& state, std::uint64_t)
{
  state.summary = 0;
  state.swc_blocked = false;
}

#endif
