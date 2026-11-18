/* ES40 emulator -- EV4 hardware loads
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
#if !defined(INCLUDED_EV4HWLOAD_H)
#define INCLUDED_EV4HWLOAD_H

#include "AlphaHWLoad.h"

namespace Ev4HWLoad
{
constexpr AlphaHWLoadDescriptor decode(std::uint32_t instruction)
{
  AlphaHWLoadDescriptor load{};
  if ((instruction >> 26) != 0x1b)
    return load;

  // HW_LD bits 15:12 are PHY, ALT, RWC, QW (HRM Table 4-8).
  const unsigned form = (instruction >> 12) & 0xf;
  const bool physical = (form & 8) != 0;
  load.size_bits = (form & 1) ? 64 : 32;
  load.displacement = static_cast<std::int32_t>(instruction & 0xfff);
  if (load.displacement & 0x800)
    load.displacement -= 0x1000;
  load.address = physical ? AlphaHWLoadAddress::Physical : AlphaHWLoadAddress::Virtual;
  load.alternate = !physical && (form & 4) != 0;
  load.write_check = !physical && (form & 2) != 0;
  load.locked = physical && (form & 4) != 0;
  load.compilable = !load.locked;
  return load;
}
}

inline constexpr AlphaHWLoadPolicy kEv4HWLoadPolicy{&Ev4HWLoad::decode};

#endif
