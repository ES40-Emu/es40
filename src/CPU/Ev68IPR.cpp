/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
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

// EV68 HW_MFPR/HW_MTPR behavior extracted from cpu_pal.h.
#include "StdAfx.h"
#include "AlphaCPU.h"
#include "Ev68IPR.h"
#include "Ev68MMU.h"

std::uint64_t Ev68IPR::read(CAlphaCPU& cpu, std::uint32_t instruction)
{
  const auto function = selector(instruction);
  const auto& state = cpu.state;
  if ((function & 0xc0) == 0x40) // PCTX
    return ((u64)state.asn << 39) | ((u64)state.astrr << 9) | ((u64)state.aster << 5)
      | (state.fpen ? U64(0x1) << 2 : 0) | (state.ppcen ? U64(0x1) << 1 : 0);

  switch (function)
  {
  case 0x05: return state.pmpc;                         // PMPC
  case 0x06: return state.exc_addr;                     // EXC_ADDR
  case 0x07: return Ev68MMU::va_form(cpu, state.exc_addr, true); // IVA_FORM
  case 0x08: case 0x09: case 0x0a: case 0x0b:            // IER_CM / CM / IER
    return (((u64)state.eien) << 33) | (((u64)state.slen) << 32)
      | (((u64)state.cren) << 31) | (((u64)state.pcen) << 29)
      | (((u64)state.sien) << 13) | (((u64)state.asten) << 13)
      | (((u64)state.cm) << 3);
  case 0x0c: return ((u64)state.sir) << 13;              // SIRR
  case 0x0d:                                           // ISUM
    return (((u64)(state.eir & state.eien)) << 33)
      | (((u64)(state.slr & state.slen)) << 32)
      | (((u64)(state.crr & state.cren)) << 31)
      | (((u64)(state.pcr & state.pcen)) << 29)
      | (((u64)(state.sir & state.sien)) << 13)
      | (((u64)(((U64(0x1) << (state.cm + 1)) - 1) & state.aster & state.astrr & (state.asten * 0x3))) << 3)
      | (((u64)(((U64(0x1) << (state.cm + 1)) - 1) & state.aster & state.astrr & (state.asten * 0xc))) << 7);
  case 0x0f: return state.exc_sum;                      // EXC_SUM
  case 0x10: return state.pal_base;                     // PAL_BASE
  case 0x11:                                           // I_CTL
    return state.i_ctl_other | (((u64)cpu.get_profile().chip_id) << 24) | (u64)state.i_ctl_vptb
      | (((u64)state.i_ctl_va_mode) << 15) | (state.hwe ? U64(0x1) << 12 : 0)
      | (state.sde ? U64(0x1) << 7 : 0) | (((u64)state.i_ctl_spe) << 3);
  case 0x14: return state.pctr_ctl;                     // PCTR_CTL
  case 0x16: return state.i_stat;                       // I_STAT
  case 0x27: return state.mm_stat;                      // MM_STAT
  case 0x2a: return state.dc_stat;                      // DC_STAT
  case 0x2b: return 0;                                  // C_DATA
  case 0xc0: return (((u64)state.cc_offset) << 32) | (state.cc & U64(0xffffffff)); // CC
  case 0xc2: return state.fault_va;                     // VA
  case 0xc3: return Ev68MMU::va_form(cpu, state.va_form_va, false); // VA_FORM
  default:
    // Unimplemented EV68 indices read zero and warn once per index.
    static bool warned_ipr[256];
    if (!warned_ipr[function & 0xff])
    {
      warned_ipr[function & 0xff] = true;
      printf("%%CPU-W-IPR: unimplemented HW_MFPR index %02x at pc=%016" PRIx64 " (ignored)\n",
        (unsigned)function, state.current_pc);
    }
    return 0;
  }
}

void Ev68IPR::write(CAlphaCPU& cpu, std::uint32_t instruction, std::uint64_t value,
                   AlphaIPRWriteContext context)
{
  const auto function = selector(instruction);
  // Keep writes that may change dispatch context out of a compiled frame.
  if (context == AlphaIPRWriteContext::Compiled
      && describe_write(instruction) == AlphaIPRWriteKind::Interpreter)
    return;

  auto& state = cpu.state;
  if ((function & 0xc0) == 0x40)
  {
    if (function & 1)
    {
      state.asn = (int) (value >> 39) & 0xff;
      cpu.flush_data_page_cache();
      cpu.jit_note_asn_change();
    }
    if (function & 2)
    {
      state.aster = (int) (value >> 5) & 0xf;
    }
    if (function & 4)
    {
      state.astrr = (int) (value >> 9) & 0xf;
    }
    if (function & 8)
      state.ppcen = (int) (value >> 1) & 1;
    if (function & 16)
      state.fpen = (int) (value >> 2) & 1;
    if (function & 6)
      cpu.kick_int_if_pending();
  }
  else
  {
    switch (function)
    {
    case 0x00:  /* ITB_TAG */
      state.last_tb_virt = value;
      break;

    case 0x01:  /* ITB_PTE */
      cpu.add_tb_i(state.last_tb_virt, value);
      break;

    case 0x02:  /* ITB_IAP */
      cpu.tbiap(ACCESS_EXEC);
      break;

    case 0x03:  /* ITB_IA */
      cpu.tbia(ACCESS_EXEC);
      break;

    case 0x04:  /* ITB_IS */
      cpu.tbis(value, ACCESS_EXEC);
      break;

    case 0x09:  /* CM */
    {
      const int old_cm = state.cm;
      state.cm = (int) (value >> 3) & 3;
#ifdef ES40_JIT
      if (state.cm != old_cm)
        cpu.jit_note_mode_change();
#else
      (void)old_cm;
#endif
      if (context == AlphaIPRWriteContext::Compiled)
        cpu.dpc_sync_bank();
      else
        cpu.flush_data_page_cache();
      cpu.kick_int_if_pending();
      break;
    }

    case 0x0b:  /* IER_CM */
    {
      const int old_cm = state.cm;
      state.cm = (int) (value >> 3) & 3;
#ifdef ES40_JIT
      if (state.cm != old_cm)
        cpu.jit_note_mode_change();
#else
      (void)old_cm;
#endif
      if (context == AlphaIPRWriteContext::Compiled)
        cpu.dpc_sync_bank();
      else
        cpu.flush_data_page_cache();
    }
      [[fallthrough]];
    case 0x0a:  /* IER */
      state.asten = (int) (value >> 13) & 1;
      state.sien = (int) (value >> 13) & 0xfffe;
      state.pcen = (int) (value >> 29) & 3;
      state.cren = (int) (value >> 31) & 1;
      state.slen = (int) (value >> 32) & 1;
      state.eien = (int) (value >> 33) & 0x3f;
      cpu.kick_int_if_pending();
      break;

    case 0x0c:  /* SIRR */
      state.sir = (int) (value >> 13) & 0xfffe;
      cpu.kick_int_if_pending();
      break;

    case 0x0e:  /* HW_INT_CLR */
      state.pcr &= ~((value >> 29) & U64(0x3));
      state.crr &= ~((value >> 31) & U64(0x1));
      state.slr &= ~((value >> 32) & U64(0x1));
      break;

    case 0x10:  /* PAL_BASE */
      cpu.set_PAL_BASE(value & U64(0x00000fffffff8000));
      break;

    case 0x11:  /* i_ctl */
      /* Bit 20 (CALL_PAL_R23 / ST_WAIT_64K) is hardwired-on for EV6/EV68 */
      state.i_ctl_other = (value & U64(0x00000000006e2f67))
                          | U64(0x0000000000100000);
      state.i_ctl_vptb = sext_u64_48(value & U64(0x0000ffffc0000000));
      state.i_ctl_spe = (int) ((value >> 3) & 7);
      state.sde = (value >> 7) & 1;
      state.hwe = (value >> 12) & 1;
      state.i_ctl_va_mode = (int) (value >> 15) & 3;
      break;

    case 0x12:  /* ic_flush_asm */
      cpu.flush_icache_asm();
      break;

    case 0x13:  /* IC_FLUSH */
      cpu.flush_icache();
      break;

    case 0x14:  /* PCTR_CTL */
      state.pctr_ctl = value & U64(0xffffffffffffffdf);
      break;

    case 0x08:  /* IER_CM, neither field selected (HRM index 0000 10xx) */
    case 0x0f:  /* EXC_SUM: RO, implicitly written at trap delivery -- the W2K
                   build 1855 PALcode writes it and EV6 ignores it (issue #169) */
    case 0x15:  /* CLR_MAP */
    case 0x17:  /* SLEEP   */
    case 0x27:  /* MM_STAT */
    case 0x2b:  /* C_DATA  */
    case 0x2c:  /* C_SHIFT */
    case 0x2d:  /* M_FIX */
      break;

    case 0x16:  /* I_STAT */
      state.i_stat &= ~value;  /* W1C */
      break;

    case 0x20:  /* DTB_TAG0 */
      cpu.last_dtb_virt[0] = value;
      break;

    case 0x21:  /* DTB_PTE0 */
      cpu.add_tb_d(cpu.last_dtb_virt[0], value, 0);
      break;

    case 0x24:  /* DTB_IS0 */
      cpu.tbis_d(value, state.asn0);
      break;

    case 0x25:  /* DTB_ASN0 */
      state.asn0 = (int) (value >> 56);
      cpu.flush_data_page_cache();
      break;

    case 0x26:  /* DTB_ALTMODE */
      state.alt_cm = (int) (value & 3);
      break;

    case 0x28:  /* M_CTL */
      state.smc = (int) (value >> 4) & 3;
      state.m_ctl_spe = (int) (value >> 1) & 7;
      cpu.flush_data_page_cache();
      break;

    case 0x29:  /* DC_CTL */
      state.dc_ctl = value;
      break;

    case 0x2a:  /* DC_STAT */
      state.dc_stat &= ~value;
      break;

    case 0xa0:  /* DTB_TAG1 */
      cpu.last_dtb_virt[1] = value;
      break;

    case 0xa1:  /* DTB_PTE1 */
      cpu.add_tb_d(cpu.last_dtb_virt[1], value, 1);
      break;

    case 0xa2:  /* DTB_IAP */
      cpu.tbiap(ACCESS_READ);
      break;

    case 0xa3:  /* DTB_IA */
      cpu.tbia(ACCESS_READ);
      break;

    case 0xa4:  /* DTB_IS1 */
      cpu.tbis_d(value, state.asn1);
      break;

    case 0xa5:  /* DTB_ASN1 */
      state.asn1 = (int) (value >> 56);
      cpu.flush_data_page_cache();
      break;

    case 0xc0:  /* CC */
      state.cc_offset = (u32) (value >> 32);
      break;

    case 0xc1:  /* CC_CTL */
      state.cc_ena = (value >> 32) & 1;
      state.cc = (u32) (value & U64(0xfffffff0));
      cpu.cc_last_read = state.cc;  /* counter rewritten: rebase the rpcc_read floor
                                   epoch or the next read would jump/false-borrow
                                   against the old counter value */
      cpu.cc_borrow = 0;
      cpu.cc_wall_remainder = 0;
      break;

    case 0xc4:  /* VA_CTL */
      state.va_ctl_vptb = sext_u64_48(value & U64(0x0000ffffc0000000));
      state.va_ctl_va_mode = (int) (value >> 1) & 3;
      cpu.flush_data_page_cache();
      cpu.tbia(ACCESS_READ);
      break;

    default:
      // Unimplemented EV68 indices ignore writes and warn once per index.
      static bool warned_ipr[256];
      if (!warned_ipr[function & 0xff])
      {
        warned_ipr[function & 0xff] = true;
        printf("%%CPU-W-IPR: unimplemented HW_MTPR index %02x at pc=%016" PRIx64 " (ignored)\n",
          (unsigned)function, state.current_pc);
      }
      break;
    }
  }
}
