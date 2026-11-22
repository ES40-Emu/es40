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

#include "StdAfx.h"
#include "AlphaCPU.h"
#include "Ev68CPUState.h"

void Ev68CPUState::replaced(CAlphaCPU& cpu)
{
  cpu.last_dtb_virt[0] = cpu.last_dtb_virt[1] = 0;
}

void Ev68CPUState::capture(const CAlphaCPU& cpu, std::uint64_t* d)
{
  const auto& state = cpu.state;
  d[0] = state.last_tb_virt; d[1] = cpu.last_dtb_virt[0]; d[2] = cpu.last_dtb_virt[1];
  d[3] = state.pctr_ctl; d[4] = state.dc_ctl; d[5] = (u64)state.cc_offset; d[6] = (u64)(u32)state.alt_cm;
  // IER enables; check_int not snapshotted (rolling it back could suppress a poll)
  d[7] = (u64)(u32)state.asten; d[8] = (u64)(u32)state.sien; d[9] = (u64)(u32)state.pcen;
  d[10] = (u64)(u32)state.cren; d[11] = (u64)(u32)state.slen; d[12] = (u64)(u32)state.eien;
  d[13] = state.exc_sum;   // FPSTART may clear it (compiled ITOFx/FTOIx/FLTL)
  d[14] = state.fpcr;      // MT_FPCR (compiled FLTL) writes it
  d[15] = (u64)state.sde; d[16] = (u64)state.hwe;            // I_CTL (compiled as terminator)
  d[17] = (u64)(u32)state.i_ctl_spe; d[18] = (u64)(u32)state.i_ctl_va_mode;
  d[19] = state.i_ctl_vptb; d[20] = state.ibox_ctl;
  d[21] = (u64)(u32)state.cm;    d[22] = (u64)(u32)state.sir;     // CM, SIRR
  d[23] = (u64)(u32)state.aster; d[24] = (u64)(u32)state.astrr;   // 0x40-group ASTER/ASTRR
  d[25] = (u64)(u32)state.fpen;  d[26] = (u64)(u32)state.ppcen;   // 0x40-group FPEN/PPCEN
  d[27] = state.exc_addr;   // PAL entry writes it; family returns may read it.
}

void Ev68CPUState::prepare_replay(CAlphaCPU& cpu, const std::uint64_t* s)
{
  auto& state = cpu.state;
  state.sde = (bool)s[15]; state.hwe = (bool)s[16];   // restore I_CTL too: a compiled HW_MFPR must read the pre-interp value
  state.i_ctl_spe = (int)s[17]; state.i_ctl_va_mode = (int)s[18];
  state.i_ctl_vptb = s[19]; state.ibox_ctl = s[20];
  state.cm = (int)s[21]; state.sir = (int)s[22];                      // ...and CM/SIRR
  cpu.flush_data_page_cache();
  state.aster = (int)s[23]; state.astrr = (int)s[24];            // ...and the PCTX read-backs (a
  state.fpen = (int)s[25]; state.ppcen = (int)s[26];              // HW_MFPR PCTX reads these live)
  state.exc_addr = s[27];
  state.exc_sum = s[13];
  state.fpcr = s[14];
}

void Ev68CPUState::restore(CAlphaCPU& cpu, const std::uint64_t* s)
{
  auto& state = cpu.state;
  state.last_tb_virt = s[0]; cpu.last_dtb_virt[0] = s[1]; cpu.last_dtb_virt[1] = s[2];
  state.pctr_ctl = s[3]; state.dc_ctl = s[4]; state.cc_offset = (u32)s[5]; state.alt_cm = (int)s[6];
  state.asten = (int)s[7]; state.sien = (int)s[8]; state.pcen = (int)s[9];
  state.cren = (int)s[10]; state.slen = (int)s[11]; state.eien = (int)s[12];
  state.exc_sum = s[13];
  state.fpcr = s[14];
  state.sde = (bool)s[15]; state.hwe = (bool)s[16];
  state.i_ctl_spe = (int)s[17]; state.i_ctl_va_mode = (int)s[18];
  state.i_ctl_vptb = s[19]; state.ibox_ctl = s[20];
  state.cm = (int)s[21]; state.sir = (int)s[22];
  cpu.flush_data_page_cache();
  state.aster = (int)s[23]; state.astrr = (int)s[24];
  state.fpen = (int)s[25]; state.ppcen = (int)s[26];
  state.exc_addr = s[27];
}

bool Ev68CPUState::differs(int ii, std::uint64_t x, std::uint64_t y, bool side_exit)
{
  // Slots 0-2 (last TB fills) are replay-invisible (verify loads skip translation),
  // so they're only sound at full-span compares (by inheritance) -- skip here.
  if (side_exit && ii < 3) return false;
  // EXC_SUM REG[12:8] (HRM 5.2.13) reports the trapping op's dest register -- report state,
  // not execution state, and which pass recorded it differs benignly. Compare the exception
  // bits strictly, mask the REG field.
  if (ii == 13) { x &= ~U64(0x1f00); y &= ~U64(0x1f00); }
  return x != y;
}
