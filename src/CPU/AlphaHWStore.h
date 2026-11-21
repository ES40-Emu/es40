/* ES40 emulator -- Alpha hardware-store policy
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
#if !defined(INCLUDED_ALPHAHWSTORE_H)
#define INCLUDED_ALPHAHWSTORE_H

#include <cstdint>

enum class AlphaHWStoreAddress
{
  Invalid,
  Physical,
  Virtual
};

// Decoded hardware store, outside saved CPU state. Physical writes use the aligned PA.
struct AlphaHWStoreDescriptor
{
  AlphaHWStoreAddress address = AlphaHWStoreAddress::Invalid;
  std::uint32_t size_bits = 0;
  std::int32_t displacement = 0;
  bool alternate = false;
  bool conditional = false;
  bool compilable = false;
  bool align_before_translation = false;
};

constexpr bool alpha_hw_store_valid(const AlphaHWStoreDescriptor& store)
{
  return (store.address == AlphaHWStoreAddress::Physical
          || store.address == AlphaHWStoreAddress::Virtual)
    && (store.size_bits == 32 || store.size_bits == 64);
}

// The shared JIT currently implements only plain physical hardware stores.
constexpr bool alpha_hw_store_compilable(const AlphaHWStoreDescriptor& store)
{
  return alpha_hw_store_valid(store) && store.compilable
    && store.address == AlphaHWStoreAddress::Physical && !store.conditional;
}

struct AlphaHWStorePolicy
{
  AlphaHWStoreDescriptor (*decode)(std::uint32_t instruction) = nullptr;
};

constexpr AlphaHWStoreDescriptor alpha_decode_hw_store(
    const AlphaHWStorePolicy& policy, std::uint32_t instruction)
{
  return policy.decode ? policy.decode(instruction) : AlphaHWStoreDescriptor{};
}

#endif
