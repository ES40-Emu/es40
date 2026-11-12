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

// EV68 address, translation-fill and data-fault rules extracted from AlphaCPU.cpp/cpu_defs.h.
#include "StdAfx.h"
#include "AlphaCPU.h"
#include "Ev68MMU.h"

AlphaMMUAccess Ev68MMU::access(const CAlphaCPU& cpu, int flags)
{
  const auto& state = cpu.state;
  // HRM 6.4.1: VPTE fetches check kernel permissions; /alt uses DTB_ALT_MODE.
  // VPTE takes precedence over ALT. Neither changes the selected lookup ASN.
  return {
    (flags & VPTE) ? 0 : (flags & ALT) ? state.alt_cm : state.cm,
    (flags & ACCESS_EXEC) ? state.asn : state.asn0
  };
}

AlphaMMUAddress Ev68MMU::map_address(const CAlphaCPU& cpu, std::uint64_t virt,
                                     int flags, int mode)
{
  const auto& state = cpu.state;
  const bool exec = (flags & ACCESS_EXEC) != 0;
  const bool va48 = (exec ? state.i_ctl_va_mode : state.va_ctl_va_mode) & 1;
  const u64 canonical = va48 ? sext_u64_48(virt)
    : (virt & U64(0x0000040000000000)) ? (virt | U64(0xfffff80000000000))
    : (virt & U64(0x000007ffffffffff));
  if (canonical != virt)
    return {AlphaMMUAddressKind::Invalid, 0};

  const int spe = exec ? state.i_ctl_spe : state.m_ctl_spe;
  u64 phys;
  // HRM 5.3.9: SPE[2] maps VA[43:0], ignoring VA[45:44].
  if ((spe & 4) && (virt & U64(0x0000c00000000000)) == U64(0x0000800000000000))
    phys = virt & U64(0x00000fffffffffff);
  // SPE[1] maps VA[40:0], sign-extending bit 40 through PA[43:41].
  else if ((spe & 2) && (virt & U64(0x0000fe0000000000)) == U64(0x0000fc0000000000))
    phys = (virt & U64(0x000001ffffffffff))
      | ((virt & U64(0x0000010000000000)) ? U64(0x00000e0000000000) : 0);
  // SPE[0] maps VA[29:0], clearing PA[43:30].
  else if ((spe & 1) && (virt & U64(0x0000ffffc0000000)) == U64(0x0000ffff80000000))
    phys = virt & U64(0x000000003fffffff);
  else
    return {AlphaMMUAddressKind::TranslationBuffer, 0};

  // Superpages are kernel-only even for accesses carrying NO_CHECK.
  return mode ? AlphaMMUAddress{AlphaMMUAddressKind::AccessViolation, 0}
              : AlphaMMUAddress{AlphaMMUAddressKind::Direct, phys};
}

AlphaTBFill Ev68MMU::decode_fill(const CAlphaCPU&, std::uint64_t virt,
                                std::uint64_t pte_phys, std::uint64_t pte_flags,
                                int)
{
  // EV68CB/EV68DC HRM 5.3.1/5.3.2: VA[47:13], PA[43:13], GH[1:0].
  // GH widens the granule by three bits per step (13/16/19/22 offset bits).
  static constexpr std::uint64_t match_masks[4] = {
    U64(0x0000ffffffffe000), U64(0x0000ffffffff0000),
    U64(0x0000fffffff80000), U64(0x0000ffffffc00000)
  };
  static constexpr std::uint64_t phys_masks[4] = {
    U64(0x00000fffffffe000), U64(0x00000fffffff0000),
    U64(0x00000ffffff80000), U64(0x00000fffffc00000)
  };
  static constexpr std::uint64_t keep_masks[4] = {
    U64(0x0000000000001fff), U64(0x000000000000ffff),
    U64(0x000000000007ffff), U64(0x00000000003fffff)
  };
  const unsigned gh = (unsigned)((pte_flags >> 5) & 3);

  AlphaTBFill fill{};
  fill.virt = virt;
  fill.phys = pte_phys & phys_masks[gh];
  fill.match_mask = match_masks[gh];
  fill.keep_mask = keep_masks[gh];
  fill.fault[0] = (int)pte_flags & 2;
  fill.fault[1] = (int)pte_flags & 4;
  fill.fault[2] = (int)pte_flags & 8;
  fill.access[0][0] = (int)pte_flags & 0x100;
  fill.access[1][0] = (int)pte_flags & 0x1000;
  fill.access[0][1] = (int)pte_flags & 0x200;
  fill.access[1][1] = (int)pte_flags & 0x2000;
  fill.access[0][2] = (int)pte_flags & 0x400;
  fill.access[1][2] = (int)pte_flags & 0x4000;
  fill.access[0][3] = (int)pte_flags & 0x800;
  fill.access[1][3] = (int)pte_flags & 0x8000;
  fill.asm_bit = (int)pte_flags & 0x10;
  return fill;
}

int Ev68MMU::access_violation(CAlphaCPU& cpu, std::uint64_t virt, int flags,
                              std::uint32_t ins, [[maybe_unused]] AlphaDataFaultSource source)
{
  auto& state = cpu.state;
  state.exc_addr = state.current_pc;
  state.fault_va = virt;
  /* HRM 5.1.5/D.24: VA_FORM is derived from VA, which is written
     on every D-stream fault (incl. DFAULT), not just TB miss.
     Keep the VA_FORM snapshot current so the OS fault handler
     computes the right PTE. */
  state.va_form_va = virt;
  state.exc_sum = (u64)I_GETRA(ins) << 8;  /* HRM 5.2.13: EXC_SUM REG[12:8] is 5 bits */

  const u32 opcode = I_GETOP(ins);
  state.mm_stat = ((opcode == 0x1b || opcode == 0x1f) ? opcode - 0x18 : opcode) << 4
    | (flags & ACCESS_WRITE) | 2;
  if (state.pal_vms)
    return cpu.vmspal_ent_dfault(flags);

#ifdef JIT_STATS
  if (source == AlphaDataFaultSource::TranslationBuffer)
    cpu.m_stat_tb[3]++;
#endif
  cpu.set_pc(state.pal_base + cpu.pal_exception_offset(AlphaPALException::DataFault) + 1);
  return -1;
}

#if defined(DEBUG_UNALIGN)
// pc/opcode identify the guest instruction; n counts alignment faults.
#define TRACE_UNALIGN(flags, align)                                              \
  do { static u64 _ua_n = 0;                                                     \
    printf("unaligned access %d, %d -> trap! exc_sum=0x%04" PRIx64               \
      ", fault_va=0x%016" PRIx64 ", mm_stat=0x%03" PRIx64                        \
      ", pc=0x%016" PRIx64 ", op=0x%02x, n=%" PRIu64 "\n",                       \
      (flags), (align), state.exc_sum, state.fault_va, state.mm_stat,            \
      state.current_pc, (unsigned) I_GETOP(ins), ++_ua_n);                        \
  } while (0)
#else
#define TRACE_UNALIGN(flags, align)
#endif

void Ev68MMU::unaligned(CAlphaCPU& cpu, std::uint64_t virt, int flags,
                        std::uint32_t ins, [[maybe_unused]] int align)
{
  auto& state = cpu.state;
  state.fault_va = virt;
  state.va_form_va = virt;
  state.exc_sum = (u64)I_GETRA(ins) << 8;
  /* HRM 5.3.8: pure misalignment is not an access violation (ACV). */
  state.mm_stat = (I_GETOP(ins) << 4) | ((flags & ACCESS_WRITE) ? 1 : 0);
  TRACE_UNALIGN(flags, align);
  cpu.enter_unaligned_exception();
}

#undef TRACE_UNALIGN
