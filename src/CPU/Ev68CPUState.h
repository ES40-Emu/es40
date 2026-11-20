/* ES40 emulator -- EV68 state lifecycle
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
#if !defined(INCLUDED_EV68CPUSTATE_H)
#define INCLUDED_EV68CPUSTATE_H

#include "AlphaCPUState.h"

class Ev68CPUState
{
public:
  static void replaced(CAlphaCPU& cpu);
  static void capture(const CAlphaCPU& cpu, std::uint64_t* words);
  static void prepare_replay(CAlphaCPU& cpu, const std::uint64_t* words);
  static void restore(CAlphaCPU& cpu, const std::uint64_t* words);
  static bool differs(int slot, std::uint64_t left, std::uint64_t right, bool side_exit);
};

inline constexpr AlphaCPUStatePolicy kEv68CPUStatePolicy{
  &Ev68CPUState::replaced,
  {28, &Ev68CPUState::capture, &Ev68CPUState::prepare_replay,
   &Ev68CPUState::restore, &Ev68CPUState::differs},
  AlphaCPUSnapshotSignature{0x2126468C, 0xC8646212}
};

#endif
