/* ES40 emulator -- EV4 privileged registers
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
#include "StdAfx.h"
#include "AlphaCPU.h"
#include "Ev4ArithmeticTrap.h"
#include "Ev4IPR.h"

u64 ev4_read_ipr(CAlphaCPU& cpu, u32 instruction)
{
  auto& state = cpu.state;
  auto& biu = cpu.m_ev4_biu;
  switch (instruction & 0xff) {
  case 0x29: return ev4_read_processor_status(state.cm);
  case 0x2a: return ev4_read_exception_summary(cpu.ev4_arithmetic_state());
  case 0x2b: return state.pal_base;
  case 0x2c: // HIRR, SIRR and ASTRR share the read format (21064 HRM 5.2.14-16, Fig. 5-11).
  case 0x2d:
  case 0x2e: {
    const unsigned irq = state.eir.load(); // HWR must agree with the IRQ pins from this read.
    return (u64(state.astrr & 0xf) << 29) | (u64(state.sir & 0xfffe) << 13)
         | ((state.slr & 1) << 13) | ((irq & 7) << 10)
         | ((state.pcr & 1) << 9) | ((state.pcr & 2) << 7)
         | ((irq & 0x38) << 2) | ((state.crr & 1) << 4)
         | ev4_interrupt_summary(state, irq);
  }
  case 0x45: // VA (21064 HRM 5.3.5).
    state.ev4_va_locked = 0;
    return state.fault_va;
  case 0x49: // BIU_ADDR: PA[33:5]; model the unpredictable read-block/LDx_L bits as zero (HRM 5.3.19).
    biu.bus_unlocked = true;
    return biu.bus_address & U64(0x3ffffffe0);
  case 0x4a: // BIU_STAT (21064 HRM Fig. 5-29).
    return biu.status & 0x7fff;
  case 0x4b: // DC_ADDR (21064-AA data sheet, 3.10.2).
    state.ev4_dc_stat_unlocked = 1;
    return 0; // The data sheet specifies only the unlock.
  case 0x4c: // DC_STAT: DC_HIT[3] and CHIP_ID[2:0] (21064 HRM 5.3.16, Table 5-15).
    return (state.dc_stat & Ev4Dcache::dc_hit) | (cpu.get_profile().chip_id & 7);
  case 0x4d: // FILL_ADDR: the low PA[4:2] are defined for D-stream fills (21064 HRM 5.3.20).
    biu.fill_unlocked = true;
    return biu.fill_address & ((biu.status & (1u << 11)) ? U64(0x3ffffffe0) : U64(0x3fffffffc));
  case 0x50: // HW_MFPR CC returns offset[63:32] and counter[31:0] (21064 HRM Fig. 5-24).
    cpu.sync_cc_wallclock();
    return alpha_read_cycle_counter(cpu);
  case 0x53: // FILL_SYNDROME: BIU_CTL.ECC selects two syndromes or two parity bits (21064 HRM 5.3.15, 5.3.21).
    return biu.fill_syndrome & ((cpu.m_ev4_bcache.control & 2) ? 0x3fff : 0x81);
  case 0x54: { // 21064 HRM 5.3.22: read the LSB, then shift; PALcode spaces reads by a null cycle.
    const auto bit = biu.bc_tag & 1;
    biu.bc_tag >>= 1;
    return bit;
  }
  default:
    FAILURE_2(NotImplemented, "Unsupported EV4 HW_MFPR %08" PRIx32 " at PC %016" PRIx64,
              instruction, state.current_pc);
  }
}

void ev4_write_ipr(CAlphaCPU& cpu, u32 instruction, u64 value, AlphaIPRWriteContext context)
{
  const auto selector = ev4_ipr_selector(instruction);
  if (selector.index == 0) {
    // TB_CTL.GH[6:5] selects the page size for ITB/DTB PTE reads and writes (21064 HRM 5.3.1, Fig. 5-18).
    if (selector.banks & 0x40) cpu.state.ev4_tb_gh = (value >> 5) & 3;
    // TB_TAG stages VA[42:13] for either PTE write, in PALmode only (21064 HRM 5.2.1).
    const bool pal_mode = context == AlphaIPRWriteContext::Compiled || (cpu.state.current_pc & 1);
    if ((selector.banks & 0x20) && pal_mode)
      cpu.state.last_tb_virt = value & cpu.get_profile().tb_fill.base_page_match_mask;
    return;
  }
  if (ev4_selects_icache_flush(selector)) {
    const bool non_global = selector.index == 23; // FLUSH_IC_ASM keeps ASM lines (21064 HRM 5.3.9-10).
#if defined(ES40_JIT) && defined(JIT_VERIFY)
    ++cpu.m_verify_icache_flush[non_global];
    if (cpu.m_jit_vreplay) return; // The reference pass already flushed the cache.
#endif
    if (non_global) cpu.flush_icache_asm();
    else cpu.flush_icache();
    return;
  }
  if (ev4_selects_bc_tag(selector)) {
    cpu.m_ev4_biu.bc_tag_unlocked = 1; // Write data is ignored (21064 HRM 5.3.22).
    return;
  }
  if (ev4_cycle_counter_index(selector.index) && (selector.banks & 0x60)) {
    auto& state = cpu.state;
    if (selector.banks & 0x40) {
      if (selector.index == 16) {
        state.cc_offset = ev4_decode_cycle_counter_offset(value);
      } else {
        const auto control = ev4_decode_cycle_counter_control(value); // 21064 HRM 5.3.14, Fig. 5-25.
        cpu.set_cycle_counter(control.counter, control.enabled);
      }
    }
    if (selector.banks & 0x20) { // IBX also selects HIER or SIER (21064 HRM 4.8.1, Table 4-7).
      if (selector.index == 16)
        ev4_write_hier(value, state.eien, state.pcen, state.slen, state.cren);
      else
        ev4_write_sier(value, state.sien, state.asten);
      cpu.kick_int_if_pending();
    }
    return;
  }
  switch (instruction & 0xff) {
  case 0x29: cpu.set_current_mode(ev4_decode_processor_status(value), context); return;
  case 0x2a: ev4_write_exception_summary(cpu.ev4_arithmetic_state(), value); return;
  case 0x2b: cpu.set_PAL_BASE(ev4_normalize_pal_base(value)); return;
  case 0x4f: cpu.state.alt_cm = ev4_decode_alternate_mode(value); return;
  default:
    FAILURE_2(NotImplemented, "Unsupported EV4 HW_MTPR %08" PRIx32 " at PC %016" PRIx64,
              instruction, cpu.state.current_pc);
  }
}
