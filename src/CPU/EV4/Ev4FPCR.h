/* ES40 emulator -- EV4 FPCR access
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
#if !defined(INCLUDED_EV4FPCR_H)
#define INCLUDED_EV4FPCR_H

#include "../AlphaFPCR.h"

class Ev4FPCR
{
public:
  static constexpr std::uint64_t normalize(std::uint64_t value)
  {
    // Original 21064: DYN and exception bits only; INED is 21064A-only.
    const auto stored = value & UINT64_C(0x0ff0000000000000);
    return stored | ((stored & UINT64_C(0x03f0000000000000))
      ? UINT64_C(0x8000000000000000) : 0);
  }

  static constexpr std::uint64_t cvtql_overflow(std::uint64_t stored)
  {
    // CVTQL overflow also records INE without delivering an INE trap.
    return stored | UINT64_C(0x0300000000000000);
  }
};

inline constexpr AlphaFPCRPolicy kEv4FPCRPolicy{
  &Ev4FPCR::normalize, &Ev4FPCR::normalize, &Ev4FPCR::cvtql_overflow
};

#endif
