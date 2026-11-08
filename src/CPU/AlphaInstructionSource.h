/* ES40 emulator -- bounded instruction sources
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
#if !defined(INCLUDED_ALPHAINSTRUCTIONSOURCE_H)
#define INCLUDED_ALPHAINSTRUCTIONSOURCE_H

#include <cassert>
#include <cstddef>
#include <cstdint>

// One contiguous family-approved instruction span, beginning at the queried PC.
// Bytes are little endian and need not be host-aligned. 
struct AlphaInstructionSourceView
{
  const std::uint8_t* bytes = nullptr;
  std::uint64_t identity = 0; // Stable start-source key; EV68 uses its physical PC.
  std::uint32_t word_count = 0;
  bool asm_global = false;

  bool contains(std::uint32_t count) const
  {
    return bytes && count != 0 && count <= word_count;
  }

  std::uint32_t word(std::uint32_t index) const
  {
    assert(bytes && index < word_count);
    const auto* p = bytes + static_cast<std::size_t>(index) * 4;
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8)
      | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
  }

  bool copy_words(std::uint32_t* out, std::uint32_t count) const
  {
    if (!out || !contains(count)) return false;
    for (std::uint32_t i = 0; i < count; ++i) out[i] = word(i);
    return true;
  }
};

// A bound reader may be retained; its context must outlive the JIT using it.
struct AlphaInstructionSource
{
  void* context = nullptr;
  AlphaInstructionSourceView (*query)(void*, std::uint64_t guest_pc) = nullptr;

  AlphaInstructionSourceView view(std::uint64_t pc) const
  {
    return query ? query(context, pc) : AlphaInstructionSourceView{};
  }

  // Copy a candidate word before another query or instruction can change the view.
  // A failed observation leaves the caller's word unchanged.
  bool read_word(std::uint64_t pc, std::uint64_t identity, bool asm_global,
                 std::uint32_t index, std::uint32_t& word) const
  {
    const auto current = view(pc);
    if (current.identity != identity || current.asm_global != asm_global
      || !current.contains(1) || index >= current.word_count) return false;
    word = current.word(index);
    return true;
  }

  bool matches(std::uint64_t pc, std::uint64_t identity, bool asm_global,
               const std::uint32_t* words, std::uint32_t count) const
  {
    const auto current = view(pc);
    if (!words || current.identity != identity || current.asm_global != asm_global
      || !current.contains(count)) return false;
    for (std::uint32_t i = 0; i < count; ++i)
      if (current.word(i) != words[i]) return false;
    return true;
  }
};

#endif
