/* ES40 emulator -- shared Alpha instruction extension requirements
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
#if !defined(INCLUDED_ALPHAINSTRUCTIONSET_H)
#define INCLUDED_ALPHAINSTRUCTIONSET_H

#include <cstdint>

// Architectural AMASK assignments, shared by feature reporting and execution.
inline constexpr std::uint64_t kAlphaBWX = 0x001;
inline constexpr std::uint64_t kAlphaFIX = 0x002;
inline constexpr std::uint64_t kAlphaCIX = 0x004;
inline constexpr std::uint64_t kAlphaMVI = 0x100;

// Only extension availability is decided here. Reserved encodings, operand
// restrictions, privilege, and family-specific HW forms remain decoder rules.
constexpr std::uint64_t alpha_instruction_extensions(std::uint32_t ins)
{
  switch (ins >> 26) {
    case 0x0a: case 0x0c: case 0x0d: case 0x0e: // LDBU/LDWU/STW/STB
      return kAlphaBWX;
    case 0x14: // ITFP: ITOF* and all SQRT* forms; absent without FIX.
      return kAlphaFIX;
    case 0x1c:
      switch ((ins >> 5) & 0x7f) {
        case 0x00: case 0x01: // SEXTB/SEXTW
          return kAlphaBWX;
        case 0x30: case 0x32: case 0x33: // CTPOP/CTLZ/CTTZ
          return kAlphaCIX;
        case 0x31: // PERR
        case 0x34: case 0x35: case 0x36: case 0x37: // UNPK*/PK*
        case 0x38: case 0x39: case 0x3a: case 0x3b: // MIN*
        case 0x3c: case 0x3d: case 0x3e: case 0x3f: // MAX*
          return kAlphaMVI;
        case 0x70: case 0x78: // FTOIT/FTOIS
          return kAlphaFIX;
      }
  }
  return 0;
}

constexpr bool alpha_instruction_supported(std::uint64_t amask, std::uint32_t ins)
{
  return (alpha_instruction_extensions(ins) & ~amask) == 0;
}

// Pure, immutable family rule, after extension checks and zero-register cleanup.
// True skips operand checks, execution, and exceptions; null decodes normally.
struct AlphaInstructionDecodePolicy
{
  bool (*retire_at_decode)(std::uint32_t ins) = nullptr;
};

constexpr bool alpha_instruction_retires_at_decode(
    const AlphaInstructionDecodePolicy& policy, std::uint32_t ins)
{
  return policy.retire_at_decode && policy.retire_at_decode(ins);
}

#endif
