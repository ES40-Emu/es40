/* ES40 emulator -- JIT engine internals.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#if !defined(INCLUDED_JITENGINE_INTERNAL_H)
#define INCLUDED_JITENGINE_INTERNAL_H

#ifdef ES40_JIT

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "../AlphaCPUProfile.h"

// FNV-1a/64 over a block's source words -- record() uses it to revalidate kept code after a flush
// instead of recompiling, and to catch self-modifying code (changed bytes -> different hash).
static inline uint64_t src_hash(const uint8_t* p, uint32_t n_instr)
{
  uint64_t h = 1469598103934665603ULL;
  for (uint32_t i = 0; i < n_instr; ++i) {
    const auto* w = p + static_cast<std::size_t>(i) * 4;
    const uint32_t word = uint32_t(w[0]) | (uint32_t(w[1]) << 8)
      | (uint32_t(w[2]) << 16) | (uint32_t(w[3]) << 24);
    h ^= word;
    h *= 1099511628211ULL;
  }
  return h;
}

// Alpha instruction patterns for the counted-substring and full-word scans.
static constexpr uint32_t kJitCountedScanHead[15] = {
  0x40020536, 0x47e0341c, 0x400203b7, 0x46df04dc, 0x4797041c, 0xe3800022,
  0x2ffe0000, 0x2ffe0000, 0x2ae10000, 0x2b830000, 0x40003120, 0x20630001,
  0x20210001, 0x42fc0536, 0xf6c00011
};
static constexpr uint32_t kJitCountedScanRestore[7] = {
  0x40c03126, 0x47e40400, 0x47e50401, 0x20e70001,
  0x47e60402, 0x47e70403, 0xc3ffffd9
};
static constexpr uint32_t kJitWordScanLoop[10] = {
  0xa6e10000, 0x42fc01f6, 0x439701f8, 0x46d80016, 0xf6c00028,
  0x40011120, 0x40211401, 0xf81ffff8, 0x40011400, 0xe4000007
};
static constexpr uint32_t kJitCountedScanWords = 39;
static constexpr uint32_t kJitWordScanWords = 10;

static inline bool jit_matches_counted_scan(const uint32_t* words) {
  return std::memcmp(words, kJitCountedScanHead, sizeof(kJitCountedScanHead)) == 0
      && std::memcmp(words + 32, kJitCountedScanRestore, sizeof(kJitCountedScanRestore)) == 0;
}

static inline bool jit_matches_word_scan(const uint32_t* words) {
  return std::memcmp(words, kJitWordScanLoop, sizeof(kJitWordScanLoop)) == 0;
}

// Check all fusion words, including lookahead.
static inline bool jit_scan_span_supported(const AlphaCPUProfile& profile, bool shadow, uint64_t pc,
                                           const uint32_t* words, uint32_t count) {
  // Internal branches share the head's execute check and bypass dispatch.
  const auto& fetch = profile.instruction_fetch;
  if (shadow || (pc & 1) || !fetch.jit_same_page_access || !fetch.jit_fuse_nonpal_blocks
      || (pc >> 13) != ((pc + 4 * (count - 1)) >> 13)) return false;
  for (uint32_t i = 0; i < count; ++i) {
    if (!profile.supports_instruction(words[i]) || profile.retires_instruction_at_decode(words[i])
        || profile.requires_fen_at_decode(words[i]) || profile.fen_rounding_modes(words[i]))
      return false;
  }
  return true;
}

#ifdef JIT_STATS
// Short mnemonic for the opcode that ended a block's compiled prefix (the coverage gap).
// Defined in jitengine.cpp; the backends use it for their punch-list prints.
const char* jit_opcode_name(unsigned op);
#endif

#endif // ES40_JIT

#endif // INCLUDED_JITENGINE_INTERNAL_H
