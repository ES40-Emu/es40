/* ES40 emulator -- EV4 physical instruction fetch
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
#if !defined(INCLUDED_EV4INSTRUCTIONFETCH_H)
#define INCLUDED_EV4INSTRUCTIONFETCH_H

#include "AlphaInstructionFetch.h"

class Ev4InstructionFetch
{
public:
  static int fetch(CAlphaCPU& cpu, std::uint32_t* instruction);
  static AlphaInstructionMapping probe(CAlphaCPU& cpu, std::uint64_t pc);
  static bool matches_interpreted(const CAlphaCPU& cpu, std::uint64_t pc,
                                  std::uint32_t count);
  static bool can_chain(const CAlphaCPU& cpu, std::uint64_t pc);
  static AlphaInstructionSourceView source(CAlphaCPU& cpu, std::uint64_t pc);
  static void invalidate(CAlphaCPU& cpu, bool non_global);

private:
  static AlphaInstructionSourceView cached_source(const CAlphaCPU& cpu, std::uint64_t pc);
};

inline constexpr AlphaInstructionFetchPolicy kEv4InstructionFetchPolicy{
  &Ev4InstructionFetch::fetch, nullptr, &Ev4InstructionFetch::probe, nullptr,
  &Ev4InstructionFetch::matches_interpreted, &Ev4InstructionFetch::can_chain,
  &Ev4InstructionFetch::source, true, false, &Ev4InstructionFetch::invalidate
};

#endif
