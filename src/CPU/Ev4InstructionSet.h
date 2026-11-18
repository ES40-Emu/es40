/* ES40 emulator -- EV4 instruction decode rules
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
#if !defined(INCLUDED_EV4INSTRUCTIONSET_H)
#define INCLUDED_EV4INSTRUCTIONSET_H

#include "AlphaInstructionSet.h"

class Ev4InstructionSet
{
public:
  static constexpr bool fen_at_decode(std::uint32_t ins)
  {
    if ((ins >> 26) != 0x16) return false;
    // IEEE formats other than S/T/Q and /M take FEN (HRM Table 4-1).
    const auto datatype = (ins >> 9) & 3u;
    const auto rounding = (ins >> 11) & 3u;
    return datatype == 1 || rounding == 1;
  }

  static constexpr std::uint32_t fen_rounding_modes(std::uint32_t ins)
  {
    // IEEE /D takes FEN for either infinity rounding mode.
    return (ins >> 26) == 0x16 && ((ins >> 11) & 3u) == 3 ? 0xau : 0u;
  }
};

inline constexpr AlphaInstructionDecodePolicy kEv4InstructionDecodePolicy{
  nullptr, &Ev4InstructionSet::fen_at_decode, &Ev4InstructionSet::fen_rounding_modes
};

#endif
