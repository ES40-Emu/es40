/* ES40 emulator -- EV4 CPU state
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
#ifndef INCLUDED_EV4CPUSTATE_H
#define INCLUDED_EV4CPUSTATE_H

#include "../AlphaCPU.h"
#include <type_traits>

template<typename State, typename BIU, typename Visit>
inline void ev4_visit_ipr_state(State& state, BIU& biu, Visit visit)
{
  visit(state.fault_va);
  visit(state.mm_stat);
  visit(state.ev4_va_locked);
  visit(state.dc_stat);
  visit(state.ev4_dc_stat_unlocked);
  visit(state.exc_sum);
  visit(state.va_form_va); // EV4 EXC_SUM serial mask.
  visit(state.ev4_swc_blocked);
  visit(state.cm);
  visit(state.alt_cm);
  visit(state.pal_base);
  visit(state.cc_offset);
  visit(biu.bus_address);
  visit(biu.fill_address);
  visit(biu.status);
  visit(biu.fill_syndrome);
  visit(biu.bus_unlocked);
  visit(biu.fill_unlocked);
  visit(biu.bc_tag);
  visit(biu.bc_tag_unlocked);
  visit(state.ev4_tb_gh);
  visit(state.last_tb_virt);
  visit(state.ev4_itb_next[0]);
  visit(state.ev4_itb_next[1]);
  visit(state.ev4_itb_pte_temp);
  visit(state.next_tb[TB_INDEX_DATA]);
  auto visit_pte = [&](auto& entry) {
    visit(entry.virt);
    visit(entry.phys);
    visit(entry.match_mask);
    visit(entry.keep_mask);
    visit(entry.asm_bit);
    for (auto& permission : entry.access[0]) visit(permission);
    visit(entry.valid);
  };
  // ITBZAP preserves the PTE payload for diagnostic reads (21064 HRM 5.2.2, 5.2.8).
  for (unsigned i = 0; i < kEv4ItbEntries; ++i) visit_pte(state.tb[TB_INDEX_ITB][i]);
  for (unsigned i = 0; i < kEv4DtbEntries; ++i) {
    auto& entry = state.tb[TB_INDEX_DATA][i];
    visit_pte(entry);
    for (auto& permission : entry.access[1]) visit(permission);
    visit(entry.fault[0]);
    visit(entry.fault[1]);
  }
}

inline void ev4_capture_ipr_state(const CAlphaCPU& cpu, u64* words)
{
  ev4_visit_ipr_state(cpu.state, cpu.m_ev4_biu, [&](const auto& field) { *words++ = field; });
}

inline void ev4_restore_ipr_state(CAlphaCPU& cpu, const u64* words)
{
  ev4_visit_ipr_state(cpu.state, cpu.m_ev4_biu, [&](auto& field) {
    field = static_cast<std::remove_reference_t<decltype(field)>>(*words++);
  });
  cpu.flush_data_page_cache();
}

inline u32 ev4_capture_memory_state(const CAlphaCPU& cpu)
{
  // A D-stream access can advance the DTB pointer (21064 HRM 5.3.2).
  return (u32(cpu.state.next_tb[TB_INDEX_DATA]) << 24) | (cpu.m_ev4_biu.bc_tag << 1)
      | ((cpu.state.dc_stat & Ev4Dcache::dc_hit) != 0);
}

inline void ev4_replay_memory_state(CAlphaCPU& cpu, u32 value)
{
  if (u32(cpu.state.next_tb[TB_INDEX_DATA]) != (value >> 24)) cpu.ev4_set_dtb_pointer(value >> 24);
  if (cpu.m_ev4_biu.bc_tag_unlocked) cpu.m_ev4_biu.bc_tag = (value >> 1) & 0x7fffff;
  cpu.note_ev4_dcache_access((value & 1) != 0);
}

inline constexpr AlphaJitVerifyPolicy kEv4JitVerifyPolicy{
  26 + kEv4ItbEntries * 10 + kEv4DtbEntries * 16,
  &ev4_capture_ipr_state, &ev4_restore_ipr_state, &ev4_restore_ipr_state,
  [](int, u64 left, u64 right, bool) { return left != right; },
  &ev4_capture_memory_state, &ev4_replay_memory_state
};

#endif
