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
#include "Ev4IPR.h"
#include "config/Configurator.h"
#include <fstream>

// 21064 I-cache: 256 direct-mapped lines of eight longwords (HRM 2.8.1.1).
static constexpr unsigned line_words = 8;
static constexpr unsigned line_count = 256;
static constexpr unsigned line_bytes = line_words * 4;
static_assert(ICACHE_ENTRIES >= line_count && ICACHE_LINE_SIZE >= line_words);

AlphaInstructionMapping Ev4InstructionFetch::mapping(const CAlphaCPU& cpu, u64 pc)
{
  if ((pc & 3) == 1 && !(pc >> 34))
    return {pc & ~U64(3), true, true};
  // 21064 HRM 2.3.2: MAP bypasses the ITB for kernel VPC[42:41] = 2 and sets ASM.
  const auto access = cpu.get_profile().mmu_address.access;
  if (!(pc & 3) && ((pc >> 41) & 3) == 2 && (cpu.state.ibox_ctl & kEv4IccsrMap) &&
      access && access(cpu, ACCESS_EXEC).mode == 0)
    return {pc & U64(0x3ffffffff), true, true};
  return {};
}

static unsigned ev4_cache_index(u64 physical)
{
  return (physical / line_bytes) % line_count;
}

void Ev4InstructionFetch::preload_srom(CAlphaCPU& cpu)
{
  const char* path = cpu.myCfg->get_myParent()->get_text_value("rom.srom");
  if (!path || !*path)
    FAILURE(Configuration, "EV4 SROM preload requires rom.srom");
  std::ifstream rom(path, std::ios::binary);
  if (!rom)
    FAILURE(Configuration, std::string("Cannot open EV4 SROM: ") + path);

  u8 image[line_count * 293 / 8];
  rom.read(reinterpret_cast<char*>(image), sizeof(image));
  const auto size = static_cast<size_t>(rom.gcount());
  if (rom.bad())
    FAILURE(Configuration, std::string("Error reading EV4 SROM: ") + path);
  if (size == 8192) // The Jensen PROM returns ones after its last bit.
    memset(image + size, 0xff, sizeof(image) - size);
  else if (size != sizeof(image))
    FAILURE(Configuration, std::string("Incomplete EV4 SROM (expected 9376 bytes or an 8192-byte Jensen PROM): ") + path);

  unsigned bit = 0;
  auto read_bits = [&](unsigned count) {
    u32 value = 0;
    for (unsigned i = 0; i < count; ++i, ++bit)
      value |= u32((image[bit / 8] >> (bit % 8)) & 1) << i;
    return value;
  };
  auto read_word = [&]() {
    const u32 value = read_bits(32);
    return endian_32(value);
  };

  // The serial chain places the tag between the even and odd words (HRM 6.5.7).
  for (unsigned i = 0; i < line_count; ++i) {
    auto& line = cpu.state.icache[i];
    for (unsigned word = 0; word < line_words; word += 2)
      line.data[word] = read_word();
    line.p_address = (u64(read_bits(21)) << 13) | (i * line_bytes);
    line.asn = read_bits(6);
    line.asm_bit = read_bits(1) != 0;
    line.valid = read_bits(1) != 0;
    for (unsigned word = 1; word < line_words; word += 2)
      line.data[word] = read_word();
    bit += 8; // Branch history.
  }
}

bool Ev4InstructionFetch::cache_hit(const CAlphaCPU& cpu, u64 physical)
{
  const auto& line = cpu.state.icache[ev4_cache_index(physical)];
  if (!line.valid || line.p_address != (physical & ~u64(line_bytes - 1)))
    return false;
  const auto access = cpu.get_profile().mmu_address.access;
  return line.asm_bit || (access && line.asn == (access(cpu, ACCESS_EXEC).asn & 63));
}

int Ev4InstructionFetch::fetch(CAlphaCPU& cpu, u32* instruction)
{
  const auto address = mapping(cpu, cpu.state.pc);
  if (!address.valid)
    FAILURE_1(NotImplemented, "Unsupported EV4 instruction fetch at PC %016" PRIx64, cpu.state.pc);

  const u64 physical = address.physical;
  const u64 base = physical & ~u64(line_bytes - 1);
  auto& line = cpu.state.icache[ev4_cache_index(physical)];
  if (!cache_hit(cpu, physical)) {
    u32 words[line_words];
    for (unsigned i = 0; i < line_words; ++i) {
      const u32 word = static_cast<u32>(cpu.cSystem->ReadMem(cpu.bus_address(base + 4 * i), 32, &cpu));
      words[i] = endian_32(word);
    }
#ifdef ES40_JIT
    if (line.valid)
      cpu.jit_flush_blocks();
#endif
    memcpy(line.data, words, sizeof(words));
    line.p_address = base;
    const auto access = cpu.get_profile().mmu_address.access;
    line.asn = access ? (access(cpu, ACCESS_EXEC).asn & 63) : 0;
    line.asm_bit = address.asm_global;
    line.valid = true;
  }

  cpu.state.pc_phys = physical;
#ifdef IDB
  cpu.current_pc_physical = physical;
#endif
  *instruction = endian_32(line.data[(physical - base) / 4]);
  return 0;
}

AlphaInstructionMapping Ev4InstructionFetch::probe(CAlphaCPU& cpu, u64 pc)
{
  auto address = mapping(cpu, pc);
  if (address.valid && cache_hit(cpu, address.physical))
    address.asm_global = cpu.state.icache[ev4_cache_index(address.physical)].asm_bit;
  return address;
}

AlphaInstructionSourceView Ev4InstructionFetch::cached_source(const CAlphaCPU& cpu, u64 pc)
{
  const auto address = mapping(cpu, pc);
  if (!address.valid)
    return {};
  const u64 physical = address.physical;
  const auto& line = cpu.state.icache[ev4_cache_index(physical)];
  if (!cache_hit(cpu, physical))
    return {};
  const unsigned offset = (physical % line_bytes) / 4;
  return {reinterpret_cast<const u8*>(&line.data[offset]), physical, line_words - offset, line.asm_bit};
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
  for (unsigned i = 0; i < line_count; ++i) {
    auto& line = cpu.state.icache[i];
    if (!non_global || !line.asm_bit)
      line.valid = false;
  }
}
