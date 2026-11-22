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
#include "StdAfx.h"
#include "AlphaCPU.h"
#include "Ev4InstructionFetch.h"

// 21064 I-cache: 256 direct-mapped lines of eight longwords (HRM 2.8.1.1).
static constexpr unsigned line_words = 8;
static constexpr unsigned line_count = 256;
static constexpr unsigned line_bytes = line_words * 4;
static_assert(ICACHE_ENTRIES >= line_count && ICACHE_LINE_SIZE >= line_words);

static AlphaInstructionMapping ev4_pal_mapping(u64 pc)
{
  if ((pc & 3) != 1 || (pc >> 34) != 0)
    return {};
  return {pc & ~U64(3), true, true};
}

static unsigned ev4_cache_index(u64 physical)
{
  return (physical / line_bytes) % line_count;
}

int Ev4InstructionFetch::fetch(CAlphaCPU& cpu, u32* instruction)
{
  const auto mapping = ev4_pal_mapping(cpu.state.pc);
  if (!mapping.valid)
    FAILURE_1(NotImplemented, "EV4 instruction fetch at PC %016" PRIx64
      " requires a word-aligned 34-bit PAL address", cpu.state.pc);

  const u64 physical = mapping.physical;
  const u64 base = physical & ~u64(line_bytes - 1);
  auto& line = cpu.state.icache[ev4_cache_index(physical)];
  if (!line.valid || line.p_address != base) {
    u32 words[line_words];
    for (unsigned i = 0; i < line_words; ++i) {
      const u32 word = static_cast<u32>(cpu.cSystem->ReadMem(base + 4 * i, 32, &cpu));
      words[i] = endian_32(word);
    }
#ifdef ES40_JIT
    if (line.valid)
      cpu.jit_flush_blocks();
#endif
    memcpy(line.data, words, sizeof(words));
    line.p_address = base;
    line.valid = true;
  }

  cpu.state.pc_phys = physical;
#ifdef IDB
  cpu.current_pc_physical = physical;
#endif
  *instruction = endian_32(line.data[(physical - base) / 4]);
  return 0;
}

AlphaInstructionMapping Ev4InstructionFetch::probe(CAlphaCPU&, u64 pc)
{
  return ev4_pal_mapping(pc);
}

AlphaInstructionSourceView Ev4InstructionFetch::cached_source(const CAlphaCPU& cpu, u64 pc)
{
  const auto mapping = ev4_pal_mapping(pc);
  if (!mapping.valid)
    return {};
  const u64 physical = mapping.physical;
  const auto& line = cpu.state.icache[ev4_cache_index(physical)];
  if (!line.valid || line.p_address != (physical & ~u64(line_bytes - 1)))
    return {};
  const unsigned offset = (physical % line_bytes) / 4;
  return {reinterpret_cast<const u8*>(&line.data[offset]), physical, line_words - offset, true};
}

AlphaInstructionSourceView Ev4InstructionFetch::source(CAlphaCPU& cpu, u64 pc)
{
  return cached_source(cpu, pc);
}

bool Ev4InstructionFetch::matches_interpreted(const CAlphaCPU& cpu, u64 pc, u32 count)
{
  while (count) {
    const auto view = cached_source(cpu, pc);
    if (!view.contains(1)) return false;
    if (view.contains(count)) return true;
    count -= view.word_count;
    pc += 4 * view.word_count;
  }
  return false;
}

bool Ev4InstructionFetch::can_chain(const CAlphaCPU& cpu, u64 pc)
{
  return cached_source(cpu, pc).contains(1);
}

void Ev4InstructionFetch::invalidate(CAlphaCPU& cpu, bool non_global)
{
  if (non_global) return;
  for (unsigned i = 0; i < line_count; ++i)
    cpu.state.icache[i].valid = false;
}
