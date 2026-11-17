/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2025-2026 by gdwnldsKSC
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
#include "Ev68PAL.h"

AlphaPALAccessContext Ev68PALAccess::context(const CAlphaCPU& cpu)
{
  return {(std::uint32_t)cpu.state.cm, cpu.state.hwe};
}

bool Ev68RegisterBank::pal_shadow_enabled(const CAlphaCPU& cpu)
{
  return cpu.state.sde;
}

bool Ev68PALCompatibility::instruction_pacing(const CAlphaCPU& cpu)
{
  // Gated off for the firmware/VMS PALcode (base 0x8000, and the pre-PAL reset state):
  // SRM runs under it, and SRM's per-CPU speed calibration counts cycles
  // per tick in a tight spin. NT and OSF-style PALcodes load at other bases and are paced.
  return cpu.state.pal_base && cpu.state.pal_base != U64(0x8000);
}

bool Ev68PALCompatibility::wait_for_interrupt(CAlphaCPU& cpu)
{
  if (cpu.idle_nap_enabled && cpu.state.pal_base == U64(0x8000))
  {
    /* WTINT before the VMS vs native PAL decision, to accommodate
     * the WTINT usage scenario under JIT */
    cpu.vmspal_call_wtint();
    return true;
  }
  if (!cpu.state.pal_vms)
  {
    /* Non-VMS WTINT implementation */
    cpu.idle_nap();
    return true;
  }
  return false;
}

bool Ev68PALCompatibility::call_pal(CAlphaCPU& cpu, std::uint32_t function)
{
  if (!cpu.state.pal_vms) return false;
  switch (function)
  {
  case 0x01:  /* CFLUSH */
    cpu.vmspal_call_cflush();
    return true;

  case 0x02:  /* DRAINA */
    cpu.vmspal_call_draina();
    return true;

  case 0x03:  /* LDQP */
    cpu.vmspal_call_ldqp();
    return true;

  case 0x04:  /* STQP */
    cpu.vmspal_call_stqp();
    return true;

  case 0x05:  /* SWPCTX */
    cpu.vmspal_call_swpctx();
    return true;

  case 0x06:  /* MFPR_ASN */
    cpu.vmspal_call_mfpr_asn();
    return true;

  case 0x07:  /* MTPR_ASTEN */
    cpu.vmspal_call_mtpr_asten();
    return true;

  case 0x08:  /* MTPR_ASTSR */
    cpu.vmspal_call_mtpr_astsr();
    return true;

  case 0x09:  /* CSERVE */
    return false;

  case 0x0b:  /* MFPR_FEN */
    cpu.vmspal_call_mfpr_fen();
    return true;

  case 0x0c:  /* MTPR_FEN */
    cpu.vmspal_call_mtpr_fen();
    return true;

  case 0x0e:  /* MFPR_IPL */
    cpu.vmspal_call_mfpr_ipl();
    return true;

  case 0x0f:  /* MTPR_IPL */
    cpu.vmspal_call_mtpr_ipl();
    return true;

  case 0x10:  /* MFPR_MCES */
    cpu.vmspal_call_mfpr_mces();
    return true;

  case 0x11:  /* MTPR_MCES */
    cpu.vmspal_call_mtpr_mces();
    return true;

  case 0x12:  /* MFPR_PCBB */
    cpu.vmspal_call_mfpr_pcbb();
    return true;

  case 0x13:  /* MFPR_PRBR */
    cpu.vmspal_call_mfpr_prbr();
    return true;

  case 0x14:  /* MTPR_PRBR */
    cpu.vmspal_call_mtpr_prbr();
    return true;

  case 0x15:  /* MFPR_PTBR */
    cpu.vmspal_call_mfpr_ptbr();
    return true;

  case 0x16:  /* MFPR_SCBB */
    cpu.vmspal_call_mfpr_scbb();
    return true;

  case 0x17:  /* MTPR_SCBB */
    cpu.vmspal_call_mtpr_scbb();
    return true;

  case 0x18:  /* MTPR_SIRR */
    cpu.vmspal_call_mtpr_sirr();
    return true;

  case 0x19:  /* MFPR_SISR */
    cpu.vmspal_call_mfpr_sisr();
    return true;

  case 0x1a:  /* MFPR_TBCHK */
    cpu.vmspal_call_mfpr_tbchk();
    return true;

  case 0x1b:  /* MTPR_TBIA */
    cpu.vmspal_call_mtpr_tbia();
    return true;

  case 0x1c:  /* MTPR_TBIAP */
    cpu.vmspal_call_mtpr_tbiap();
    return true;

  case 0x1d:  /* MTPR_TBIS */
    cpu.vmspal_call_mtpr_tbis();
    return true;

  case 0x1e:  /* MFPR_ESP */
    cpu.vmspal_call_mfpr_esp();
    return true;

  case 0x1f:  /* MTPR_ESP */
    cpu.vmspal_call_mtpr_esp();
    return true;

  case 0x20:  /* MFPR_SSP */
    cpu.vmspal_call_mfpr_ssp();
    return true;

  case 0x21:  /* MTPR_SSP */
    cpu.vmspal_call_mtpr_ssp();
    return true;

  case 0x22:  /* MFPR_USP */
    cpu.vmspal_call_mfpr_usp();
    return true;

  case 0x23:  /* MTPR_USP */
    cpu.vmspal_call_mtpr_usp();
    return true;

  case 0x24:  /* MTPR_TBISD */
    cpu.vmspal_call_mtpr_tbisd();
    return true;

  case 0x25:  /* MTPR_TBISI */
    cpu.vmspal_call_mtpr_tbisi();
    return true;

  case 0x26:  /* MFPR_ASTEN */
    cpu.vmspal_call_mfpr_asten();
    return true;

  case 0x27:  /* MFPR_ASTSR */
    cpu.vmspal_call_mfpr_astsr();
    return true;

  case 0x29:  /* MFPR_VPTB */
    cpu.vmspal_call_mfpr_vptb();
    return true;

  case 0x2e:  /* MTPR_DATFX */
    cpu.vmspal_call_mtpr_datfx();
    return true;

  case 0x3f:  /* MFPR_WHAMI */
    cpu.vmspal_call_mfpr_whami();
    return true;

  case 0x86:  /* IMB */
    cpu.vmspal_call_imb();
    return true;

  case 0x8f:  /* PROBER */
    cpu.vmspal_call_prober();
    return true;

  case 0x90:  /* PROBEW */
    cpu.vmspal_call_probew();
    return true;

  case 0x91:  /* RD_PS */
    cpu.vmspal_call_rd_ps();
    return true;

  case 0x92:  /* REI */
    cpu.vmspal_call_rei();
    return true;

  case 0x9b:  /* SWASTEN */
    cpu.vmspal_call_swasten();
    return true;

  case 0x9c:  /* WR_PS_SW */
    cpu.vmspal_call_wr_ps_sw();
    return true;

  case 0x9d:  /* RSCC */
    cpu.vmspal_call_rscc();
    return true;

  case 0x9e:  /* READ_UNQ */
    cpu.vmspal_call_read_unq();
    return true;

  case 0x9f:  /* WRITE_UNQ */
    cpu.vmspal_call_write_unq();
    return true;

  default:
    return false;
  }
}

void Ev68PALCompatibility::base_changed(CAlphaCPU& cpu, std::uint64_t old_base, bool old_hle)
{
  auto& state = cpu.state;
  const u64 pb = state.pal_base;

  // VMS PALcode uses base 0x8000
  state.pal_vms = (pb == U64(0x8000)) && !cpu.vmspal_lle_enabled;
  //state.pal_vms = false;

#ifdef DEBUG_PAL
  if (pb != old_base || old_hle != state.pal_vms)
  {
    // Only the VMS PALcode (base 0x8000) is a type the emulator knows about.
    const char* type = (pb == U64(0x8000)) ? "VMS PALcode" : "unknown PALcode type";
    printf("%%CPU-I-PALSWITCH: cpu%d PAL_BASE %016" PRIx64 " -> %016" PRIx64 ": %s, %s\n",
      cpu.get_cpuid(), old_base, pb, type,
      state.pal_vms        ? "vmspal replacement routines engaged" :
      cpu.vmspal_lle_enabled   ? "running native (replacements disabled)" :
                             "running native (no replacements for this type)");
  }
#endif

#ifdef DEBUG_PAL
  printf("%%CPU-I-PALSWITCH: PAL=%016" PRIx64 " p21=%016" PRIx64 " p22=%016" PRIx64 " r22=%016" PRIx64 "\n", pb, state.r[53], state.r[54], state.r[22]);
  // Dump PAL scratch area contents for non-VMS PAL
  if (!state.pal_vms && state.r[53] != 0) {

    u64 scratch = U64(0x7cf420);

    printf("%%CPU-I-PALSCR: Scratch area at %016" PRIx64 ":\n", scratch);
    printf("%%CPU-I-PALSCR:   +0x00 VPTB = %016" PRIx64 "\n", cpu.cSystem->ReadMem(scratch + 0x00, 64, &cpu));
    printf("%%CPU-I-PALSCR:   +0x08 PTBR = %016" PRIx64 "\n", cpu.cSystem->ReadMem(scratch + 0x08, 64, &cpu));
    printf("%%CPU-I-PALSCR:   +0x10 PCBB = %016" PRIx64 "\n", cpu.cSystem->ReadMem(scratch + 0x10, 64, &cpu));
    printf("%%CPU-I-PALSCR:   +0x18 KSP  = %016" PRIx64 "\n", cpu.cSystem->ReadMem(scratch + 0x18, 64, &cpu));
    printf("%%CPU-I-PALSCR:   +0x98 WHAMI= %016" PRIx64 "\n", cpu.cSystem->ReadMem(scratch + 0x98, 64, &cpu));
    printf("%%CPU-I-PALSCR:   +0x170 SCBB= %016" PRIx64 "\n", cpu.cSystem->ReadMem(scratch + 0x170, 64, &cpu));
  }
  else if (!state.pal_vms && state.r[53] == 0) {
    printf("%%CPU-W-NOP21: PAL switched but p21=0! Scratch area not available.\n");
  }
#endif
}
