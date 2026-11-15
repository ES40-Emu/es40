/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023, 2025-2026 by gdwnldsKSC
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
#if !defined(INCLUDED_EV68LLSC_H)
#define INCLUDED_EV68LLSC_H

#include "AlphaLLSC.h"

namespace Ev68LLSC
{
// EV6/EV68 Dcache blocks are 64 bytes; LDx_L/STx_C monitor that cache line.
constexpr std::uint64_t match_mask = 0x00000807ffffffc0;
constexpr std::uint64_t io_mask = 0x0000080000000000;

constexpr bool matches(std::uint64_t locked_address, std::uint64_t address)
{
  return !((locked_address ^ address) & match_mask);
}

// I/O-space conditional stores have no cache line to watch; treat as held.
constexpr bool unreserved_store(std::uint64_t address)
{
  return (address & io_mask) != 0;
}
}

inline constexpr AlphaLLSCPolicy kEv68LLSCPolicy{
  &Ev68LLSC::matches, &Ev68LLSC::unreserved_store
};

#endif
