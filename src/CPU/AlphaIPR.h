/* ES40 emulator -- Alpha privileged-register policies
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
#if !defined(INCLUDED_ALPHAIPR_H)
#define INCLUDED_ALPHAIPR_H

#include <cstddef>
#include <cstdint>

class CAlphaCPU;

// A family may expose a plain field to the JIT; all other reads use its callback.
enum class AlphaIPRReadKind
{
  Unsupported,
  Helper,
  IboxControl,
  ExceptionAddress,
  PalTemp,
  ExceptionSummary,
  PalBase,
  MemoryFaultStatus,
  FaultAddress,
  Noop // No read or destination write; privilege checks still apply.
};

struct AlphaIPRReadDescriptor
{
  AlphaIPRReadKind kind;
  bool replay;       // Verification replays an asynchronous value from the interpreter.
  bool discard_safe; // A read whose destination is R31 can be omitted.
  std::uint8_t pal_temp_index = 0;

  constexpr unsigned pal_temp_slot() const { return 32u + (pal_temp_index & 31u); }
};

// Family read behavior, copied with the immutable CPU profile. Every callback
// receives the complete instruction; the family owns selector/bank decoding.
// Descriptors use the executing instruction's PAL mode, including compiled blocks.
struct AlphaIPRReadPolicy
{
  std::uint64_t (*read)(CAlphaCPU& cpu, std::uint32_t instruction);
  AlphaIPRReadDescriptor (*describe)(std::uint32_t instruction, bool pal_mode);
};

// The family selects write behavior and compiled eligibility.
enum class AlphaIPRWriteKind
{
  Interpreter,
  Helper,
  HelperTerminator,
  BcacheControl, // Ends the block after switching the physical memory path.
  AboxControl,
  IboxControl,
  InterruptClear,
  SoftwareInterruptRequest,
  HardwareInterruptEnable,
  SoftwareInterruptEnable,
  AstInterruptEnable,
  ExceptionAddress, // Write PC[63:2] and PAL[0].
  PalTemp,
  Noop // No write callback; privilege checks still apply.
};

inline constexpr std::uint64_t kAlphaIPRExceptionAddressMask = ~std::uint64_t(2);

// Compiled execution may retain caches that the interpreter normally flushes.
enum class AlphaIPRWriteContext
{
  Interpreter,
  Compiled
};

// Write callbacks likewise receive the complete instruction, including any
// family-specific register-bank and qualifier bits.
struct AlphaIPRWritePolicy
{
  using Write = void (*)(CAlphaCPU&, std::uint32_t, std::uint64_t, AlphaIPRWriteContext);
  using Describe = AlphaIPRWriteKind (*)(std::uint32_t, bool);
  using DescribeWithSlot = AlphaIPRWriteKind (*)(std::uint32_t, bool, unsigned*);

  Write write = nullptr;
  Describe plain = nullptr;
  DescribeWithSlot indexed = nullptr; // PAL_TEMP destination, including a parallel helper write.

  constexpr AlphaIPRWritePolicy(Write fn = nullptr, std::nullptr_t = nullptr) : write(fn) {}
  constexpr AlphaIPRWritePolicy(Write fn, Describe decode) : write(fn), plain(decode) {}
  constexpr AlphaIPRWritePolicy(Write fn, DescribeWithSlot decode) : write(fn), indexed(decode) {}

  constexpr AlphaIPRWriteKind describe(std::uint32_t ins, bool pal, unsigned* slot = nullptr) const
  {
    if (indexed) return indexed(ins, pal, slot);
    return plain ? plain(ins, pal) : AlphaIPRWriteKind::Interpreter;
  }
};

#endif
