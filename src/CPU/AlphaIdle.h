/* ES40 emulator -- Alpha idle observation policy
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
#if !defined(INCLUDED_ALPHAIDLE_H)
#define INCLUDED_ALPHAIDLE_H

#include <cstdint>

class CAlphaCPU;

struct AlphaIdleContext
{
  bool available = false; // Explicit opt-in to the shared idle heuristics.
  int mode = 3;
  int ipl = 0;
  int external_enables = 0;
  int software_enables = 0;
};

// Read-only PAL calls with no GPR inputs and at most one GPR output.
struct AlphaIdlePALCall
{
  bool allowed = false;
  int destination = -1; // -1 means no GPR output.
};

struct AlphaIdlePolicy
{
  AlphaIdleContext (*observe)(const CAlphaCPU& cpu) = nullptr; // Null disables detection.
  // Pure, stable classification; null rejects PAL-containing loops.
  AlphaIdlePALCall (*describe_pal_call)(std::uint32_t function) = nullptr;
};

constexpr AlphaIdlePALCall alpha_idle_pal_call(const AlphaIdlePolicy& policy,
                                              std::uint32_t function)
{
  const auto call = policy.describe_pal_call
    ? policy.describe_pal_call(function) : AlphaIdlePALCall{};
  return call.allowed && call.destination >= -1 && call.destination < 32
    ? call : AlphaIdlePALCall{};
}

#endif
