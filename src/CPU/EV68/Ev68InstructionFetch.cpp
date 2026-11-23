/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023-2026 by gdwnldsKSC
 * Copyright (C) 2026 by Scott Klein
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 *
 */

// EV68 instruction fetch and sequential-cache handling extracted from the shared core.
#include "StdAfx.h"
#include "../AlphaCPU.h"
#include "Ev68InstructionFetch.h"

// EV68 cache predicates used by the family fetch and source-observation policy.
bool Ev68InstructionFetch::icache_line_matches(const CAlphaCPU& cpu, int i, u64 address)
{
  const auto& line = cpu.state.icache[i];
  return line.valid && (line.asn == cpu.state.asn || line.asm_bit)
    && line.address == (address & ICACHE_MATCH_MASK);
}

bool Ev68InstructionFetch::icache_hit(const CAlphaCPU& cpu, int i, u64 address)
{
  return cpu.icache_enabled && icache_line_matches(cpu, i, address)
    && ((address & 1) || (cpu.icache_valid_modes[i] & (1u << cpu.state.cm)));
}

void Ev68InstructionFetch::invalidate(CAlphaCPU& cpu, bool non_global)
{
  auto& state = cpu.state;
  if (cpu.icache_enabled)
  {
    if (non_global)
    {
      int i;
      for (i = 0; i < ICACHE_ENTRIES; i++)
        if (!state.icache[i].asm_bit)
          state.icache[i].valid = false;
    }
    else
    {
      for (int i = 0; i < ICACHE_ENTRIES; i++) {
        state.icache[i].valid = false;
      }
      state.next_icache = 0; // old version, may be relied on elsewhere
      state.last_found_icache = 0;
    }
  }
  cpu.seq_remaining = 0;
}

void Ev68InstructionFetch::lifecycle(CAlphaCPU& cpu, AlphaInstructionFetchEvent event)
{
  switch (event)
  {
  case AlphaInstructionFetchEvent::Initialize:
    cpu.seq_line_ptr = nullptr;
    cpu.seq_offset = 0;
    cpu.seq_remaining = 0;
    cpu.seq_next_pc = 0;
    break;
  case AlphaInstructionFetchEvent::AdvancePC:
    cpu.state.pc_phys += 4;
    if (cpu.state.rem_ins_in_page)
      cpu.state.rem_ins_in_page--;
    break;
  case AlphaInstructionFetchEvent::RedirectPC:
    cpu.state.rem_ins_in_page = 0;
    cpu.seq_remaining = 0;
    break;
  case AlphaInstructionFetchEvent::CompiledExit:
    cpu.seq_remaining = 0;
    break;
  case AlphaInstructionFetchEvent::TranslationChanged:
  case AlphaInstructionFetchEvent::Restore:
    memset(cpu.icache_valid_modes, 0, sizeof(cpu.icache_valid_modes));
    cpu.seq_remaining = 0;
    cpu.state.rem_ins_in_page = 0;
    break;
  }
}

void Ev68InstructionFetch::prepare_dispatch(CAlphaCPU& cpu)
{
  // Preserve the ES40 firmware entry behavior after an in-place image rewrite.
  if (cpu.state.pc == (cpu.state.pal_base | 1))
    cpu.flush_icache();
}

AlphaInstructionMapping Ev68InstructionFetch::probe(CAlphaCPU& cpu, u64 pc)
{
  AlphaInstructionMapping mapping;
  if (pc & 1)
  {
    // PAL instruction addresses are physical; retain the dispatcher's alignment.
    mapping.physical = pc & ~U64(1);
    mapping.asm_global = true;
  }
  else
  {
    const int line = (int)(((pc & ~U64(3)) >> 11) & (ICACHE_ENTRIES - 1));
    if (icache_hit(cpu, line, pc))
    {
      mapping.physical = cpu.state.icache[line].p_address + (pc & ICACHE_BYTE_MASK);
      mapping.asm_global = cpu.state.icache[line].asm_bit;
    }
    else if (cpu.virt2phys(pc, &mapping.physical, ACCESS_EXEC | FAKE,
                           &mapping.asm_global, 0) != 0)
      return mapping;
  }
  mapping.valid = true;
  return mapping;
}

bool Ev68InstructionFetch::peek_cached(const CAlphaCPU& cpu, u32* instruction)
{
  if (cpu.state.pc == cpu.seq_next_pc && cpu.seq_remaining > 0 && cpu.seq_cm == cpu.state.cm)
  {
    *instruction = endian_32(cpu.seq_line_ptr[cpu.seq_offset]);
    return true;
  }
  if (cpu.icache_enabled)
  {
    const int line = (int)((cpu.state.pc >> 11) & (ICACHE_ENTRIES - 1));
    if (icache_hit(cpu, line, cpu.state.pc))
    {
      *instruction = endian_32(cpu.state.icache[line].data[(cpu.state.pc >> 2) & ICACHE_INDEX_MASK]);
      return true;
    }
  }
  return false;
}

bool Ev68InstructionFetch::matches_interpreted(const CAlphaCPU& cpu, u64 start_pc, u32 count)
{
  if (cpu.icache_enabled)
  {
    const u64 vs = start_pc & ~U64(3);
    const u64 ve = vs + 4 * (u64)count;
    for (u64 v = vs & ~U64(0x7ff); v < ve; v += 0x800)
    {
      const int li = (int)((v >> 11) & (ICACHE_ENTRIES - 1));
      const auto& line = cpu.state.icache[li];
      // Compare cached bytes even if the current mode has no cached permission.
      // This deliberately differs from the execution/peek hit predicate.
      if (line.valid && (line.asn == cpu.state.asn || line.asm_bit)
        && line.address == ((v | (start_pc & 1)) & ICACHE_MATCH_MASK))
      {
        const u64 lo = (v > vs) ? v : vs;
        const u64 hi = (v + 0x800 < ve) ? v + 0x800 : ve;
        const u64 poff = line.p_address + (lo - v);
        if (poff + (hi - lo) <= cpu.dram_size
          && memcmp((const uint8_t*)cpu.dram_ptr + poff,
                    (const uint8_t*)line.data + (lo - v), (size_t)(hi - lo)))
          return false;
      }
    }
  }
  return true;
}

bool Ev68InstructionFetch::can_chain(const CAlphaCPU& cpu, u64 target)
{
  // Reset entry must return through the dispatcher so prepare_dispatch can flush.
  return target != (cpu.state.pal_base | 1);
}

AlphaInstructionSourceView Ev68InstructionFetch::source(CAlphaCPU& cpu, u64 pc)
{
  const auto mapping = probe(cpu, pc);
  if (!mapping.valid || !cpu.dram_ptr || (mapping.physical & 3)
    || mapping.physical >= cpu.dram_size)
    return {};

  // The EV68 JIT uses backing RAM so its interpreted-span check separately rejects
  // cached insn that differ. Never cross the current 8 KiB translation.
  const u64 ram_words = (cpu.dram_size - mapping.physical) / sizeof(u32);
  const u64 page_words = (U64(0x2000) - (mapping.physical & U64(0x1fff))) / sizeof(u32);
  const u32 count = (u32)((ram_words < page_words) ? ram_words : page_words);
  if (!count) return {};
  return {reinterpret_cast<const u8*>(cpu.dram_ptr) + mapping.physical,
          mapping.physical, count, mapping.asm_global};
}

int Ev68InstructionFetch::fetch(CAlphaCPU& cpu, u32* instruction)
{
  if (cpu.state.pc == cpu.seq_next_pc && cpu.seq_remaining > 0 && cpu.seq_cm == cpu.state.cm)
  {
    *instruction = endian_32(cpu.seq_line_ptr[cpu.seq_offset]);
#if defined(DEBUG_ARC) && !defined(ES40_JIT)
    if (cpu.state.current_pc < 0x10000)
      printf("PC=%016" PRIx64 " ins=%08x\n", cpu.state.current_pc, *instruction);
#endif
    cpu.seq_offset++;
    cpu.seq_remaining--;
    cpu.seq_next_pc += 4;
  }
  else
  {
    prepare_dispatch(cpu);
    const int result = read_cache(cpu, cpu.state.pc, instruction);
    if (result)
      return result;

    if (!(cpu.state.pc & 1) && icache_hit(cpu, cpu.state.last_found_icache, cpu.state.pc))
    {
      const int line = cpu.state.last_found_icache;
      cpu.seq_line_ptr = cpu.state.icache[line].data;
      cpu.seq_offset = (int)((cpu.state.pc >> 2) & ICACHE_INDEX_MASK) + 1;
      cpu.seq_remaining = ICACHE_LINE_SIZE - cpu.seq_offset;
      cpu.seq_next_pc = (cpu.state.pc & ~U64(0x3)) + 4;
      cpu.seq_cm = cpu.state.cm;
    }
    else
      cpu.seq_remaining = 0;
  }

#if defined(IDB)
  cpu.current_pc_physical = cpu.state.pc_phys;
#endif
  return 0;
}

/**
 * Get an instruction from the instruction cache.
 * If necessary, fill a new cache block from memory.
 *
 * read_cache checks all cache entries, to see if there is a
 * cache entry that matches the current address space number,
 * and that contains the address we're looking for. If it
 * exists, the instruction is fetched from this cache,
 * otherwise, the physical address for the instruction is
 * calculated, and the cache block is filled.
 *
 * The last cache entry that was a hit is remembered, so that
 * cache entry is checked first on the next instruction. (very
 * likely to be the same cache block)
 *
 * It would be easiest to do without the instruction cache
 * altogether, but unfortunately SRM uses self-modifying
 * code, that relies on the correct instruction stream to
 * remain in the cache.
 **/
int Ev68InstructionFetch::read_cache(CAlphaCPU& cpu, u64 address, u32* data)
{
  // Direct-map the icache: 2 KiB lines (ICACHE_LINE_SIZE * 4 == 2048 bytes).
  // Use VA[...:11] as the set index. The PAL bit (VA<0>) remains part of the tag.
  const u64 kLineShift = 11;
  const u64 v_aligned = address & ~U64(0x3);
  const int i = (int)((v_aligned >> kLineShift) & (ICACHE_ENTRIES - 1));
  u64   v_a;
  u64   p_a;
  int   result;
  bool  asm_bit;

  if (cpu.icache_enabled)
  {
    bool hit = icache_line_matches(cpu, i, address);
    if (hit && !(address & 1) && !(cpu.icache_valid_modes[i] & (1u << cpu.state.cm))) {
      // A cached instruction is still subject to the current mode's access
      // checks, including for ASM lines. Keep its bytes until IMB when the
      // translation is unchanged; SRM can depend on the cached instruction.
      result = cpu.virt2phys(address, &p_a, ACCESS_EXEC, &asm_bit, 0);
      if (result) return result;
      hit = p_a == cpu.state.icache[i].p_address + (address & ICACHE_BYTE_MASK);
      if (hit) {
        if (asm_bit != cpu.state.icache[i].asm_bit) {
          // A newly checked process-specific mapping must not grant access
          // to other ASNs through an older global cache line (or vice versa).
          cpu.icache_valid_modes[i] = 0;
          cpu.state.icache[i].asn = cpu.state.asn;
          cpu.state.icache[i].asm_bit = asm_bit;
        }
        cpu.icache_valid_modes[i] |= (u8)(1u << cpu.state.cm);
      }
    }

    // ---- Fast hit probe
    if (hit) {

      *data = endian_32(cpu.state.icache[i].data[(address >> 2) & ICACHE_INDEX_MASK]);

      // keep debug/pc_phys coherent even when icache is enabled
      cpu.state.pc_phys = cpu.state.icache[i].p_address + (address & ICACHE_BYTE_MASK);

#ifdef IDB
      cpu.current_pc_physical = cpu.state.pc_phys;
#endif
      cpu.state.last_found_icache = i;
      return 0;
    }

    // ---- Miss: translate + fill
    v_a = address & ICACHE_MATCH_MASK;
    if (address & 1) {
      // PALmode: VA<0> is PAL marker; physical is VA with bit0 cleared
      p_a = v_a & ~U64(0x1);
      asm_bit = true;
    }
    else {
      result = cpu.virt2phys(v_a, &p_a, ACCESS_EXEC, &asm_bit, 0);
      if (result) return result;
    }

    // Attempt to get a pointer into DRAM. If this is PIO (e.g., TIG flash),
      // PtrToMem returns null and we must *not* try to memcpy from it.
    char* mem = cpu.cSystem->PtrToMem(p_a);
    if (mem && cpu.cSystem->PtrToMem(p_a + ((ICACHE_LINE_SIZE * 4) - 1))) {
      // DRAM-backed: fill the direct-mapped icache line.
      memcpy(cpu.state.icache[i].data, mem, ICACHE_LINE_SIZE * 4);
      cpu.state.icache[i].valid = true;
      cpu.state.icache[i].asn = cpu.state.asn;
      cpu.state.icache[i].asm_bit = asm_bit;
      cpu.state.icache[i].address = address & ICACHE_MATCH_MASK;
      cpu.state.icache[i].p_address = p_a;
      cpu.icache_valid_modes[i] = (u8)(1u << cpu.state.cm);

      *data = endian_32(cpu.state.icache[i].data[(address >> 2) & ICACHE_INDEX_MASK]);

      // same pc_phys update on fill
      cpu.state.pc_phys = p_a + (address & ICACHE_BYTE_MASK);
#ifdef IDB
      cpu.current_pc_physical = cpu.state.pc_phys;
#endif
      cpu.state.last_found_icache = i;
      return 0;
    }
    else {
      // PIO/TIG-backed: cannot fill icache lines.
        // Read exactly the requested instruction as 4 byte reads via the system bus.
      const u64 p_instr = p_a + (address & ICACHE_BYTE_MASK);
      u32 ins = 0;
      ins |= (u8)cpu.cSystem->ReadMem(p_instr + 0, 8, &cpu);
      ins |= ((u8)cpu.cSystem->ReadMem(p_instr + 1, 8, &cpu)) << 8;
      ins |= ((u8)cpu.cSystem->ReadMem(p_instr + 2, 8, &cpu)) << 16;
      ins |= ((u8)cpu.cSystem->ReadMem(p_instr + 3, 8, &cpu)) << 24;
      *data = ins; // already in target little-endian form

      cpu.state.pc_phys = p_instr;
#ifdef IDB
      cpu.current_pc_physical = cpu.state.pc_phys;
#endif
      return 0;
    }
  }

  // ---- Icache disabled (unchanged)
  if (address & 1) {
    cpu.state.pc_phys = address & ~U64(0x3);
    cpu.state.rem_ins_in_page = 1;
  }
  else {
    if (!cpu.state.rem_ins_in_page || cpu.ins_page_cm != cpu.state.cm) {
      result = cpu.virt2phys(address, &cpu.state.pc_phys, ACCESS_EXEC, &asm_bit, 0);
      if (result) return result;
      cpu.state.rem_ins_in_page = 2048 - ((((u32)address) >> 2) & 2047);
      cpu.ins_page_cm = cpu.state.cm;
    }
  }

  *data = (u32)cpu.cSystem->ReadMem(cpu.state.pc_phys, 32, &cpu);
  return 0;
}
