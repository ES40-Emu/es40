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

// EV68 startup state extracted from AlphaCPU.cpp, including the existing
// SROM bypass used by the ES40 firmware loader.
#include "StdAfx.h"
#include "../AlphaCPU.h"
#include "Ev68Reset.h"

void Ev68Reset::initialize(CAlphaCPU& cpu)
{
  auto& state = cpu.state;
  state.fpen = true;
  state.ibox_ctl = U64(0x502086);
  state.smc = 1;

  // Preserve the synthetic SROM data mapping through the normal TB-fill path.
  cpu.add_tb(0, 0, U64(0xff61), ACCESS_READ, state.asn0);

  state.r[22] = state.r[22 + 32] = state.iProcNum;
}
