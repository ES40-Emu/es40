/* ES40 emulator -- Alpha hardware-load policy
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
#if !defined(INCLUDED_ALPHAHWLOAD_H)
#define INCLUDED_ALPHAHWLOAD_H

#include <cstdint>

enum class AlphaHWLoadAddress
{
  Invalid,
  Physical,
  Virtual
};

// Decoded hardware load, outside saved CPU state. Physical reads are aligned and longwords sign-extend.
struct AlphaHWLoadDescriptor
{
  AlphaHWLoadAddress address = AlphaHWLoadAddress::Invalid;
  std::uint32_t size_bits = 0;
  std::int32_t displacement = 0;
  bool vpte = false;
  bool alternate = false;
  bool write_check = false;
  bool locked = false;
  bool compilable = false;
  bool discard_safe = false; // Compiled physical loads may omit a discarded read.
  bool align_before_translation = false;
};

constexpr bool alpha_hw_load_valid(const AlphaHWLoadDescriptor& load)
{
  return (load.address == AlphaHWLoadAddress::Physical
          || load.address == AlphaHWLoadAddress::Virtual)
    && (load.size_bits == 32 || load.size_bits == 64);
}

constexpr bool alpha_hw_load_compilable(const AlphaHWLoadDescriptor& load)
{
  return alpha_hw_load_valid(load) && load.compilable && !load.locked;
}

constexpr std::uint64_t alpha_hw_load_translation_address(const AlphaHWLoadDescriptor& load,
                                                          std::uint64_t va)
{
  return load.align_before_translation ? va & ~std::uint64_t(load.size_bits / 8 - 1) : va;
}

// Shared virtual-load helper ABI; these bits are independent of guest encoding.
inline constexpr std::uint32_t kAlphaHWLoadAlignBeforeTranslation = 0x800u;

constexpr std::uint32_t alpha_hw_load_helper_descriptor(const AlphaHWLoadDescriptor& load)
{
  return load.size_bits | (load.vpte ? 0x100u : 0)
    | (load.alternate ? 0x200u : 0) | (load.write_check ? 0x400u : 0)
    | (load.align_before_translation ? kAlphaHWLoadAlignBeforeTranslation : 0);
}

struct AlphaHWLoadPolicy
{
  AlphaHWLoadDescriptor (*decode)(std::uint32_t instruction) = nullptr;
};

constexpr AlphaHWLoadDescriptor alpha_decode_hw_load(const AlphaHWLoadPolicy& policy,
                                                     std::uint32_t instruction)
{
  return policy.decode ? policy.decode(instruction) : AlphaHWLoadDescriptor{};
}

#endif
