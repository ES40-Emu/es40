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
