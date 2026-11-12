/* ES40 emulator -- Alpha data-fault delivery
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
#if !defined(INCLUDED_ALPHADATAFAULT_H)
#define INCLUDED_ALPHADATAFAULT_H

#include <cstdint>

class CAlphaCPU;

enum class AlphaDataFaultSource
{
  AddressCheck,
  TranslationBuffer
};

// Family fault state and PAL/HLE delivery, called only for real faults.
struct AlphaDataFaultPolicy
{
  // Zero continues with the selected TB entry; address checks still return failure.
  int (*access_violation)(CAlphaCPU& cpu, std::uint64_t address,
                           int access_flags, std::uint32_t instruction,
                           AlphaDataFaultSource source);

  // Always ends the instruction, before translation or memory side effects.
  void (*unaligned)(CAlphaCPU& cpu, std::uint64_t address,
                    int access_flags, std::uint32_t instruction, int alignment_mask);
};

#endif
