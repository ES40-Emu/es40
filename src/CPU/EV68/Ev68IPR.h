/* ES40 emulator -- EV68 privileged-register rules
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
#if !defined(INCLUDED_EV68IPR_H)
#define INCLUDED_EV68IPR_H

#include "../AlphaIPR.h"

#if defined(IDB)
const char* ev68_ipr_name(std::uint32_t instruction);
#endif

class Ev68IPR
{
public:
  static constexpr std::uint32_t selector(std::uint32_t instruction)
  {
    return (instruction >> 8) & 0xff;
  }

  static std::uint64_t read(CAlphaCPU& cpu, std::uint32_t instruction);
  static void write(CAlphaCPU& cpu, std::uint32_t instruction, std::uint64_t value,
                    AlphaIPRWriteContext context);

  static constexpr AlphaIPRReadDescriptor describe(std::uint32_t instruction, bool)
  {
    const auto function = selector(instruction);
    if ((function & 0xc0) == 0x40)
      return {AlphaIPRReadKind::Helper, false, true}; // PCTX group.

    switch (function)
    {
    case 0x06: return {AlphaIPRReadKind::ExceptionAddress, false, true};
    case 0x0f: return {AlphaIPRReadKind::ExceptionSummary, false, true};
    case 0x10: return {AlphaIPRReadKind::PalBase, false, true};
    case 0x27: return {AlphaIPRReadKind::MemoryFaultStatus, false, true};
    case 0xc2: return {AlphaIPRReadKind::FaultAddress, false, true};
    case 0x0d: return {AlphaIPRReadKind::Helper, true, true}; // ISUM is asynchronous.
    case 0x05: case 0x07:
    case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c:
    case 0x11: case 0x14: case 0x16: case 0x2a: case 0x2b:
    case 0xc0: case 0xc3:
      return {AlphaIPRReadKind::Helper, false, true};
    default:
      return {AlphaIPRReadKind::Unsupported, false, true};
    }
  }

  static constexpr AlphaIPRWriteKind describe_write(std::uint32_t instruction, bool)
  {
    const auto function = selector(instruction);
    // ASN changes require interpreter dispatch; the other PCTX fields can compile.
    if ((function & 0xc0) == 0x40)
      return (function & 1) ? AlphaIPRWriteKind::Interpreter : AlphaIPRWriteKind::Helper;

    switch (function)
    {
    case 0x00: case 0x14: case 0x20: case 0x26:
    case 0x29: case 0xa0: case 0xc0: // Field stores.
    case 0x01: case 0x21: case 0xa1: // Translation-buffer fills.
    case 0x02: case 0x03: case 0x04: // ITB invalidation.
    case 0x13:                     // Lazy instruction-cache flush.
    case 0x09: case 0x0a: case 0x0b: case 0x0c: // Mode/interrupt fields.
    case 0x24: case 0xa4:           // Single-entry DTB invalidation.
      return AlphaIPRWriteKind::Helper;
    case 0x11:                     // I_CTL changes compiled assumptions, including SDE.
      return AlphaIPRWriteKind::HelperTerminator;
    case 0x15: case 0x17: case 0x27:
    case 0x2b: case 0x2c: case 0x2d:
      return AlphaIPRWriteKind::Noop;
    default:
      return AlphaIPRWriteKind::Interpreter;
    }
  }
};

inline constexpr AlphaIPRReadPolicy kEv68IPRReadPolicy{
  &Ev68IPR::read, &Ev68IPR::describe
};

inline constexpr AlphaIPRWritePolicy kEv68IPRWritePolicy{
  &Ev68IPR::write, &Ev68IPR::describe_write
};

#endif
