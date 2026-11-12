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

// EV68 interrupt eligibility and routing extracted from AlphaCPU.cpp/AlphaCPU.h.
#include "StdAfx.h"
#include "AlphaCPU.h"
#include "Ev68Interrupt.h"

bool Ev68Interrupt::pending(CAlphaCPU& cpu)
{
  const auto& state = cpu.state;
  if ((state.eien & state.eir) || (state.sien & state.sir)
      || (state.asten
          && (state.aster & state.astrr & ((1 << (state.cm + 1)) - 1))))
  {
#ifdef JIT_STATS
    if (state.eien & state.eir) cpu.m_stat_kick[0]++;
    else if (state.sien & state.sir) cpu.m_stat_kick[1]++;
    else cpu.m_stat_kick[2]++;
#endif
    return true;
  }
  return false;
}

AlphaInterruptResult Ev68Interrupt::service(CAlphaCPU& cpu)
{
  auto& state = cpu.state;
  if (state.pal_vms) {
    // PALcode base is set to 0x8000; meaning OpenVMS PALcode is currently active. In this
    // case, our VMS PALcode replacement routines are valid, and should be used as it is
    // faster than using the original PALcode.

    // Native PAL handles chipset errors on irq<0> (Tsunami HRM 6.3.1)
    // and halt / MP work requests on irq<4>, before lower-priority interpreter work.
    if (state.eir & state.eien & 0x11)
      return AlphaInterruptResult::EnterPAL;

    // irq<1>=device, irq<2>=timer, irq<3>=IPI. (Was 0x6 = device+timer only,
    // which stranded incoming IPIs under VMS PALcode -> CPUSPINWAIT.)
    if (state.eir & state.eien & 0xe)
      if (cpu.vmspal_ent_ext_int(state.eir & state.eien & 0xe))
        return AlphaInterruptResult::Return;

    if (state.sir & state.sien & 0xfffc)
      if (cpu.vmspal_ent_sw_int(state.sir & state.sien))
        return AlphaInterruptResult::Return;

    if (state.asten && (state.aster & state.astrr & ((1 << (state.cm + 1)) - 1)))
      if (cpu.vmspal_ent_ast_int(state.aster & state.astrr & ((1 << (state.cm + 1)) - 1)))
        return AlphaInterruptResult::Return;

    if (state.sir & state.sien)
      if (cpu.vmspal_ent_sw_int(state.sir & state.sien))
        return AlphaInterruptResult::Return;
  }
  else
  {

    // VMS PALcode replacement is inactive, either because the PALcode base is unsupported
    // or native PALcode is selected. Transfer control to the PALcode interrupt entry point.
    //        if (state.eir & 8)
    //        {
    //          printf("%s: IP interrupt received%s...\n",devid_string, (state.eien&8)?"(enabled)":"(masked)");
    //        }
    if ((state.eien & state.eir) || (state.sien & state.sir) || (state.asten
      && (state.aster & state.astrr & ((1 << (state.cm + 1)) - 1))))
    {
#ifdef JIT_STATS
      cpu.m_stat_kick[5]++;
#endif
      return AlphaInterruptResult::EnterPAL;
    }
#ifdef JIT_STATS
    else cpu.m_stat_kick[6]++;   // flag was set but nothing deliverable at the poll
#endif
  }
  return AlphaInterruptResult::Continue;
}
