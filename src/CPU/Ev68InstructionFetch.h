/* ES40 emulator -- EV68 instruction fetching
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
#if !defined(INCLUDED_EV68INSTRUCTIONFETCH_H)
#define INCLUDED_EV68INSTRUCTIONFETCH_H

#include "AlphaInstructionFetch.h"

class Ev68InstructionFetch
{
public:
  static int fetch(CAlphaCPU& cpu, std::uint32_t* instruction);
  static void prepare_dispatch(CAlphaCPU& cpu);
  static AlphaInstructionMapping probe(CAlphaCPU& cpu, std::uint64_t pc);
  static bool peek_cached(const CAlphaCPU& cpu, std::uint32_t* instruction);
  static bool matches_interpreted(const CAlphaCPU& cpu, std::uint64_t start_pc,
                                  std::uint32_t count);
  static bool can_chain(const CAlphaCPU& cpu, std::uint64_t target);
  static AlphaInstructionSourceView source(CAlphaCPU& cpu, std::uint64_t pc);

private:
  static int read_cache(CAlphaCPU& cpu, std::uint64_t address, std::uint32_t* data);
};

inline constexpr AlphaInstructionFetchPolicy kEv68InstructionFetchPolicy{
  &Ev68InstructionFetch::fetch, &Ev68InstructionFetch::prepare_dispatch,
  &Ev68InstructionFetch::probe, &Ev68InstructionFetch::peek_cached,
  &Ev68InstructionFetch::matches_interpreted, &Ev68InstructionFetch::can_chain,
  &Ev68InstructionFetch::source,
  true, // Coherent EV68 RAM/cache sources participate in JIT invalidation.
  true  // Execute permissions are uniform within an 8 KiB translation.
};

#endif
