/* ES40 emulator -- EV4 hardware stores
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
#if !defined(INCLUDED_EV4HWSTORE_H)
#define INCLUDED_EV4HWSTORE_H

#include "AlphaHWStore.h"

constexpr AlphaHWStoreDescriptor ev4_decode_hw_store(std::uint32_t instruction)
{
  AlphaHWStoreDescriptor store{};
  if ((instruction >> 26) != 0x1f)
    return store;

  // HW_ST uses PHY[15], ALT[14] and QW[12]; RWC[13] is ignored.
  const unsigned form = (instruction >> 12) & 0xf;
  const bool physical = (form & 8) != 0;
  store.size_bits = (form & 1) ? 64 : 32;
  store.displacement = static_cast<std::int32_t>(instruction & 0xfff);
  if (store.displacement & 0x800)
    store.displacement -= 0x1000;
  store.address = physical ? AlphaHWStoreAddress::Physical : AlphaHWStoreAddress::Virtual;
  store.alternate = !physical && (form & 4) != 0;
  store.conditional = physical && (form & 4) != 0;
  store.compilable = physical && !store.conditional;
  return store;
}

inline constexpr AlphaHWStorePolicy kEv4HWStorePolicy{&ev4_decode_hw_store};

#endif
