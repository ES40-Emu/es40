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
#include "../AlphaCPU.h"
#include "Ev68PAL.h"

static constexpr std::uint64_t kVmsPalBase = 0x8000;

#if defined(IDB)
const char* ev68_call_pal_name(std::uint32_t function, std::uint64_t pal_base)
{
  if (pal_base != kVmsPalBase)
    return nullptr;

  static constexpr const char* privileged[] = {
    "HALT", "CFLUSH", "DRAINA", "LDQP", "STQP", "SWPCTX", "MFPR_ASN",
    "MTPR_ASTEN",
    "MTPR_ASTSR", "CSERVE", "SWPPAL", "MFPR_FEN", "MTPR_FEN", "MTPR_IPIR",
    "MFPR_IPL", "MTPR_IPL",
    "MFPR_MCES", "MTPR_MCES", "MFPR_PCBB", "MFPR_PRBR", "MTPR_PRBR",
    "MFPR_PTBR", "MFPR_SCBB", "MTPR_SCBB",
    "MTPR_SIRR", "MFPR_SISR", "MFPR_TBCHK", "MTPR_TBIA", "MTPR_TBIAP",
    "MTPR_TBIS", "MFPR_ESP", "MTPR_ESP",
    "MFPR_SSP", "MTPR_SSP", "MFPR_USP", "MTPR_USP", "MTPR_TBISD", "MTPR_TBISI",
    "MFPR_ASTEN", "MFPR_ASTSR",
    "28", "MFPR_VPTB", "MTPR_VPTB", "MTPR_PERFMON", "2C", "2D", "MTPR_DATFX",
    "2F",
    "30", "31", "32", "33", "34", "35", "36", "37",
    "38", "39", "3A", "3B", "3C", "3D", "WTINT", "MFPR_WHAMI",
  };
  static constexpr const char* unprivileged[] = {
    "BPT", "BUGCHK", "CHME", "CHMK", "CHMS", "CHMU", "IMB", "INSQHIL",
    "INSQTIL", "INSQHIQ", "INSQTIQ", "INSQUEL", "INSQUEQ", "INSQUEL/D",
    "INSQUEQ/D", "PROBER",
    "PROBEW", "RD_PS", "REI", "REMQHIL", "REMQTIL", "REMQHIQ", "REMQTIQ",
    "REMQUEL",
    "REMQUEQ", "REMQUEL/D", "REMQUEQ/D", "SWASTEN", "WR_PS_SW", "RSCC",
    "READ_UNQ", "WRITE_UNQ",
    "AMOVRR", "AMOVRM", "INSQHILR", "INSQTILR", "INSQHIQR", "INSQTIQR",
    "REMQHILR", "REMQTILR",
    "REMQHIQR", "REMQTIQR", "GENTRAP", "AB", "AC", "AD", "CLRFEN", "AF",
    "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "BA", "BB",
    "BC", "BD", "BE", "BF"
  };
  static_assert(sizeof(privileged) / sizeof(privileged[0]) == 0x40);
  static_assert(sizeof(unprivileged) / sizeof(unprivileged[0]) == 0x40);
  if (function < 0x40)
    return privileged[function];
  if (function >= 0x80 && function <= 0xbf)
    return unprivileged[function - 0x80];
  return nullptr;
}

std::uint64_t ev68_trace_address_fallback(std::uint64_t address, bool instruction)
{
  const auto region = address >> 28;
  if (region == 2)
    return address - 0x1fe00000; // This trace alias preserves the low bits.
  if (region == 1)
    address -= 0x0fffe000;
  return instruction ? address & ~U64(3) : address;
}

void Ev68PALCompatibility::trace_process_name(const CAlphaCPU& cpu, std::uint64_t prbr,
                                             char* name, std::size_t capacity)
{
  if (!capacity)
    return;
  name[0] = '\0';
  if (cpu.state.pal_base != kVmsPalBase || !prbr)
    return;

  constexpr std::uint64_t offset = 0x154;
  constexpr std::size_t field_size = 20;
  const auto ram_size = U64(1) << cpu.cSystem->get_memory_bits();
  if (prbr >= ram_size || ram_size - prbr < offset + field_size)
    return;

  const auto length = capacity > field_size ? field_size : capacity - 1;
  strncpy(name, cpu.cSystem->PtrToMem(prbr + offset), length);
  name[length] = '\0';
}
#endif

AlphaPALAccessContext Ev68PALAccess::context(const CAlphaCPU& cpu)
{
  return {(std::uint32_t)cpu.state.cm, cpu.state.hwe};
}

bool Ev68RegisterBank::pal_shadow_enabled(const CAlphaCPU& cpu)
{
  return cpu.state.sde;
}

std::uint64_t Ev68PALCompatibility::trace_prbr(CAlphaCPU& cpu)
{
  u64   v_prbr; // virtual
  u64   p_prbr; // physical
  bool  b;
  if (cpu.state.r[21 + 32] && ((u64)(cpu.state.r[21 + 32] + 0xaf) < (u64)
    ((U64(0x1) << cpu.cSystem->get_memory_bits()))))
    v_prbr = cpu.cSystem->ReadMem(cpu.state.r[21 + 32] + 0xa8, 64, &cpu);
  else
    v_prbr = cpu.cSystem->ReadMem(0x70a8 + (0x200 * cpu.get_cpuid()), 64, &cpu);
  if (cpu.virt2phys(v_prbr, &p_prbr, ACCESS_READ | FAKE | NO_CHECK, &b, 0))
    p_prbr = v_prbr;
  if ((u64)p_prbr > (u64)(U64(0x1) << cpu.cSystem->get_memory_bits()))
    p_prbr = 0;
  return p_prbr;
}

std::uint64_t Ev68PALCompatibility::trace_hwpcb(CAlphaCPU& cpu)
{
  u64   v_pcb;  // virtual
  u64   p_pcb;  // physical
  bool  b;
  if (cpu.state.r[21 + 32] && ((u64)(cpu.state.r[21 + 32] + 0x17) < (u64)
    ((U64(0x1) << cpu.cSystem->get_memory_bits()))))
    v_pcb = cpu.cSystem->ReadMem(cpu.state.r[21 + 32] + 0x10, 64, &cpu);
  else
    v_pcb = cpu.cSystem->ReadMem(0x7010 + (0x200 * cpu.get_cpuid()), 64, &cpu);
  if (cpu.virt2phys(v_pcb, &p_pcb, ACCESS_READ | NO_CHECK | FAKE, &b, 0))
    p_pcb = v_pcb;
  if (p_pcb > (u64)(U64(0x1) << cpu.cSystem->get_memory_bits()))
    p_pcb = 0;
  return p_pcb;
}

void Ev68PALCompatibility::interpreter_step(CAlphaCPU& cpu)
{
  //--------------------------------------------------------------------------------
  // Interpreter-only SRM memory-test shortcuts.
  //--------------------------------------------------------------------------------
#if defined(SKIP_SRM_MEMTEST) && !defined(ES40_JIT)
  auto& state = cpu.state;
  // All five SRM mem-test patch points are in page 0x8b000 and only fire
  // during early SRM boot. Gate on the page so every other instruction pays
  // one compare instead of five.
  // JIT builds must NOT use these hooks - a block-cache miss interprets a few
  // iterations, thus only one hook like 0x8bb90 fires alone, and aborts the
  // fill while a still-compiled verify loop then runs, warm inits show mem errors
  // JIT is fast enough, even 16GB configs don't take long.
  if ((state.current_pc & ~U64(0xFFF)) == U64(0x8b000))
  {
    if (state.current_pc == U64(0x8bb90))
    {
      if (state.r[5] != U64(0xaaaaaaaaaaaaaaaa))
      {
        printf("wrong memory check skip!\n");
      }
      else
      {
        state.r[0] = state.r[4];
      }
    }

    if (state.current_pc == U64(0x8bbe0))
    {
      if (state.r[5] != U64(0xaaaaaaaaaaaaaaaa))
      {
        printf("wrong memory check skip!\n");
      }
      else
      {
        state.r[16] = 0;
      }
    }

    if (state.current_pc == U64(0x8bc28))
    {
      if (state.r[5] != U64(0xaaaaaaaaaaaaaaaa))
      {
        printf("wrong memory check skip!\n");
      }
      else
      {
        state.r[8] = state.r[4];
      }
    }

    if (state.current_pc == U64(0x8bc70))
    {
      if (state.r[7] != U64(0x5555555555555555))
      {
        printf("wrong memory check skip1!\n");
      }
      else
      {
        state.r[0] = 0;
      }
    }

    if (state.current_pc == U64(0x8bcb0))
    {
      if (state.r[7] != U64(0x5555555555555555))
      {
        printf("wrong memory check skip2!\n");
      }
      else
      {
        state.r[3] = state.r[4];
      }
    }
  }
#else
  (void)cpu;
#endif
  //--------------------------------------------------------------------------------
  // end of skip memory test section
  //--------------------------------------------------------------------------------
}

bool Ev68PALCompatibility::instruction_pacing(const CAlphaCPU& cpu)
{
  // Gated off for the firmware/VMS PALcode (base 0x8000, and the pre-PAL reset state):
  // SRM runs under it, and SRM's per-CPU speed calibration counts cycles
  // per tick in a tight spin. NT and OSF-style PALcodes load at other bases and are paced.
  return cpu.state.pal_base && cpu.state.pal_base != kVmsPalBase;
}

bool Ev68PALCompatibility::wait_for_interrupt(CAlphaCPU& cpu)
{
  if (cpu.idle_nap_enabled && cpu.state.pal_base == kVmsPalBase)
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
  state.pal_vms = (pb == kVmsPalBase) && !cpu.vmspal_lle_enabled;
  //state.pal_vms = false;

#ifdef DEBUG_PAL
  if (pb != old_base || old_hle != state.pal_vms)
  {
    // Only the VMS PALcode (base 0x8000) is a type the emulator knows about.
    const char* type = (pb == kVmsPalBase) ? "VMS PALcode" : "unknown PALcode type";
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
