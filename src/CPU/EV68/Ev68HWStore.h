/* ES40 emulator -- EV68 hardware-store rules
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
#if !defined(INCLUDED_EV68HWSTORE_H)
#define INCLUDED_EV68HWSTORE_H

#include "../AlphaHWStore.h"

constexpr AlphaHWStoreDescriptor ev68_decode_hw_store(std::uint32_t instruction)
{
  AlphaHWStoreDescriptor store{};
  if ((instruction >> 26) != 0x1f)
    return store;

  // EV68 HW_ST TYPE[15:12]: bit 0 selects quadword; only 0..5/12/13 exist.
  const unsigned form = (instruction >> 12) & 0xf;
  store.size_bits = (form & 1) ? 64 : 32;
  store.displacement = static_cast<std::int32_t>(instruction & 0xfff);
  if (store.displacement & 0x800)
    store.displacement -= 0x1000;
  if (form > 5 && form != 12 && form != 13)
    return store;

  store.address = form < 4 ? AlphaHWStoreAddress::Physical
                           : AlphaHWStoreAddress::Virtual;
  store.alternate = form == 12 || form == 13;
  store.conditional = form == 2 || form == 3;
  // Virtual and conditional forms retain their interpreter fallback.
  store.compilable = form == 0 || form == 1;
  return store;
}

inline constexpr AlphaHWStorePolicy kEv68HWStorePolicy{&ev68_decode_hw_store};

#endif
