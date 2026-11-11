/* ES40 emulator -- EV68 instruction decode rules
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
#if !defined(INCLUDED_EV68INSTRUCTIONSET_H)
#define INCLUDED_EV68INSTRUCTIONSET_H

#include "AlphaInstructionSet.h"

class Ev68InstructionSet
{
public:
  static constexpr bool retire_at_decode(std::uint32_t ins)
  {
    // FP-to-F31 retirement, including reserved forms (HRM 2.5, Table 2-6).
    // MT_FPCR has no destination; MF_FPCR writes Fa.
    const auto opcode = ins >> 26;
    return opcode >= 0x14 && opcode <= 0x17 && (ins & 0x1f) == 31
        && !(opcode == 0x17 && ((ins >> 5) & 0x7fe) == 0x024);
  }
};

inline constexpr AlphaInstructionDecodePolicy kEv68InstructionDecodePolicy{
    &Ev68InstructionSet::retire_at_decode};

#endif
