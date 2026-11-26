/* ES40 emulator -- DEC 3000 Model 300X system
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
#include "CPU/AlphaCPU.h"
#include "system/System_pelican.h"
#include "system/PelicanFlash.h"
#include <sstream>

class Pelican::Registers final : public CSystemComponent
{
public:
  Registers(CConfigurator* cfg, Pelican& system) : CSystemComponent(cfg, &system), board(system)
  {
    // MCR and LED occupy consecutive dense quadwords in slot 7 (EK-D3SYS-PM, Tables 6 and 9).
    board.RegisterMemory(this, 0, U64(0x1e0000010), 16);
    save_item(board.lit_leds, "lit_leds");
  }

  bool decodes_memory_access(int, u64 address, int dsize, bool write) const noexcept override
  {
    return (dsize == 32 || dsize == 64) && address == (write ? 8u : 0u);
  }

  u64 ReadMem(int, u64, int) override
  {
    return board.population.size_straps;
  }

  void WriteMem(int, u64, int, u64 data) override
  {
    // LED[7:0] occupies bits 23:16 and each zero lights one LED (EK-D3SYS-PM, §3.2.4).
    const u8 lit = static_cast<u8>(~(data >> 16));
    if (board.lit_leds != lit) {
      board.lit_leds = lit;
      printf("%%PEL-I-LED: %02X\n", static_cast<unsigned>(lit));
    }
  }

  void ResetPCI() override { board.lit_leds = 0xff; }

  std::string snapshot_identity() const override
  {
    return CSystemComponent::snapshot_identity() + ":" + std::to_string(board.population.bytes)
      + ":" + std::to_string(board.population.size_straps);
  }

private:
  Pelican& board;
};

unsigned Pelican::MemoryPopulation::backing_bits() const
{
  unsigned bits = 24;
  while ((U64(1) << bits) < bytes)
    ++bits;
  return bits;
}

Pelican::MemoryPopulation Pelican::MemoryPopulation::from_bits(u64 bits)
{
  if (bits < 24 || bits > 28)
    FAILURE(Configuration, "Pelican memory.bits must be 24 through 28 (16 to 256 MiB)");
  const u64 bytes = U64(1) << bits;
  const unsigned large_pairs = static_cast<unsigned>(bytes >> 26);
  return {bytes, static_cast<u8>((1u << large_pairs) - 1)};
}

Pelican::MemoryPopulation Pelican::MemoryPopulation::from_config(CConfigurator* cfg)
{
  const char* pairs = cfg->get_text_value("memory.pairs");
  if (!pairs)
    return from_bits(cfg->get_num_value("memory.bits", false, 25));
  if (cfg->get_text_value("memory.bits"))
    FAILURE(Configuration, "Choose either memory.pairs or memory.bits for Pelican RAM");

  // Up to four matched SIMM pairs are populated from slot zero, largest first (EK-PELCN-OG, p. 2-34).
  std::istringstream input(pairs);
  MemoryPopulation installed{};
  unsigned previous = 64;
  for (unsigned slot = 0; ; ++slot) {
    unsigned mib;
    if (slot == 4 || !(input >> mib) || (mib != 16 && mib != 64) || mib > previous)
      FAILURE(Configuration, "Pelican memory.pairs requires one to four 64 or 16 MiB pairs, largest first");
    previous = mib;
    installed.bytes += u64(mib) << 20;
    // Each set MCR bit identifies a 64 MiB pair; small and empty pairs both read zero (EK-D3SYS-PM, §3.2.3).
    if (mib == 64)
      installed.size_straps |= 1u << slot;
    input >> std::ws;
    if (input.eof())
      return installed;
    if (input.get() != ',')
      FAILURE(Configuration, "Separate Pelican memory.pairs capacities with commas");
  }
}

Pelican::Pelican(CConfigurator* cfg) : Pelican(cfg, MemoryPopulation::from_config(cfg))
{
}

Pelican::Pelican(CConfigurator* cfg, MemoryPopulation installed)
  : CSystem(cfg, ~U64(0), installed.backing_bits()), population(installed),
    direct_srm(cfg->get_bool_value("boot.direct_srm", false))
{
  if (direct_srm && cfg->get_text_value("rom.srom"))
    FAILURE(Configuration, "Choose either Pelican boot.direct_srm or rom.srom");
}

void Pelican::attach_board_devices(CConfigurator* cfg)
{
  new Registers(cfg, *this);
  flash = new PelicanFlash(cfg, this);
}

int Pelican::LoadROM()
{
  if (direct_srm) {
    // V7's decompressor runs in PALmode in low RAM before MACHINE_RESET (EK-D3SYS-PM, §§12.1, 14.1).
    constexpr u64 system_base = 0x4000;
    flash->load_firmware(PtrToMem(system_base), PtrToMem(0x44000));
    // S889 saves R20 at 0x580c8 and reads its low longword as installed MiB at 0x6659c (EK-D3SYS-PM, §12.1).
    get_cpu(0)->set_r(20, population.bytes >> 20);
    get_cpu(0)->set_PAL_BASE(system_base);
    get_cpu(0)->set_pc(system_base | 1);
    printf("%%PEL-I-DIRECTSRM: Starting SYSROM in PALmode at physical 0x4000.\n");
    return 0;
  }
  const char* path = config()->get_text_value("rom.srom");
  if (!path || !*path)
    FAILURE(Configuration, "Pelican cold boot requires a DEC 3000 Model 300X CPU serial ROM (rom.srom)");
  return 0;
}

void Pelican::ResetMem(unsigned int membits)
{
  if (membits != population.backing_bits())
    population = MemoryPopulation::from_bits(membits);
  CSystem::ResetMem(membits);
}

u64 Pelican::ram_offset(u64 address) const noexcept
{
  // Physical bit 30 selects the uncached window onto the same installed RAM (EK-D3SYS-PM, Table 5).
  const u64 offset = address & ~U64(0x40000000);
  return offset < population.bytes ? offset : ~U64(0);
}

u8 Pelican::diagnostic_leds()
{
  std::lock_guard<std::recursive_mutex> guard(get_device_bus_mutex());
  return lit_leds;
}
