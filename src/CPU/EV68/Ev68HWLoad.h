/* ES40 emulator -- EV68 hardware-load rules
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
#if !defined(INCLUDED_EV68HWLOAD_H)
#define INCLUDED_EV68HWLOAD_H

#include "../AlphaHWLoad.h"

constexpr AlphaHWLoadDescriptor ev68_decode_hw_load(std::uint32_t instruction)
{
  AlphaHWLoadDescriptor load{};
  if ((instruction >> 26) != 0x1b)
    return load;

  // EV68 HW_LD TYPE[15:12]: bit 0 selects quadword; 6/7 are unallocated.
  const unsigned form = (instruction >> 12) & 0xf;
  load.size_bits = (form & 1) ? 64 : 32;
  load.displacement = static_cast<std::int32_t>(instruction & 0xfff);
  if (load.displacement & 0x800)
    load.displacement -= 0x1000;
  if (form == 6 || form == 7)
    return load;

  load.address = form < 4 ? AlphaHWLoadAddress::Physical : AlphaHWLoadAddress::Virtual;
  load.locked = form == 2 || form == 3;
  load.vpte = form == 4 || form == 5;
  load.alternate = form >= 12;
  load.write_check = form == 10 || form == 11 || form == 14 || form == 15;
  // Keep physical load-locked forms interpreted; virtual forms retain fault probes.
  load.compilable = !load.locked;
  load.discard_safe = load.address == AlphaHWLoadAddress::Physical && !load.locked;
  return load;
}

inline constexpr AlphaHWLoadPolicy kEv68HWLoadPolicy{&ev68_decode_hw_load};

#endif
