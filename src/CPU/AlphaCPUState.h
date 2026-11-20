/* ES40 emulator -- Alpha state lifecycle policy
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
#if !defined(INCLUDED_ALPHACPUSTATE_H)
#define INCLUDED_ALPHACPUSTATE_H

#include <cstdint>
#include <optional>

class CAlphaCPU;

// Model-sized verifier snapshots; zero words require no callbacks.
struct AlphaJitVerifyPolicy
{
  int word_count = 0;
  void (*capture)(const CAlphaCPU& cpu, std::uint64_t* words) = nullptr;
  // Restore replay inputs from the pre-interpreter snapshot.
  void (*prepare_replay)(CAlphaCPU& cpu, const std::uint64_t* words) = nullptr;
  // Restore the authoritative post-interpreter snapshot.
  void (*restore)(CAlphaCPU& cpu, const std::uint64_t* words) = nullptr;
  bool (*differs)(int slot, std::uint64_t left, std::uint64_t right, bool side_exit) = nullptr;

  constexpr bool valid() const
  {
    return word_count >= 0 && (!word_count || (capture && prepare_replay && restore && differs));
  }
};

struct AlphaCPUSnapshotSignature
{
  std::uint32_t begin;
  std::uint32_t end;
};

struct AlphaCPUStatePolicy
{
  // Clear transient family state after init/reset clearing or successful restore.
  void (*replaced)(CAlphaCPU& cpu) = nullptr;
  AlphaJitVerifyPolicy verify{};
  std::optional<AlphaCPUSnapshotSignature> snapshot_signature{};
};

#endif
