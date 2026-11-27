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
#include "system/PelicanIOCTL.h"
#include "system/PelicanTCDS.h"
#include "graphics/CXTurbo.h"
#include <sstream>

class Pelican::Registers final : public CSystemComponent
{
  enum { MemoryAndLeds, Interrupt, SparseInterrupt };
public:
  Registers(CConfigurator* cfg, Pelican& system) : CSystemComponent(cfg, &system), board(system)
  {
    // MCR and LED occupy consecutive dense quadwords in slot 7 (EK-D3SYS-PM, Tables 6 and 9).
    board.RegisterMemory(this, MemoryAndLeds, U64(0x1e0000010), 16);
    board.RegisterMemory(this, Interrupt, U64(0x1e0000000), 8);
    board.RegisterMemory(this, SparseInterrupt, U64(0x1f0000000), 8); // PELI_IO I21F's SCSI interrupt test reads IR via I/O descriptor 4 (offset 0x19c50).
    save_item(board.lit_leds, "lit_leds");
  }

  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override
  {
    if (index != MemoryAndLeds)
      return !write && address == 0 && (dsize == 32 || (index == Interrupt && dsize == 64));
    return (dsize == 32 || dsize == 64) && address == (write ? 8u : 0u);
  }

  u64 ReadMem(int index, u64, int) override
  {
    if (index != MemoryAndLeds)
      return board.interrupt_reasons(); // IR<4:2> report slot 5 I/O, TCDS and CXTurbo (EK-D3SYS-PM, §3.2.1).
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
  io_control = new PelicanIOCTL(cfg, this);
  graphics = new CXTurbo(cfg, *this);
  tcds = new PelicanTCDS(cfg, this);
}

CDiskController& Pelican::scsi_controller()
{
  return *tcds;
}

void Pelican::bind_board_devices()
{
  graphics->bind_display();
}

u32 Pelican::interrupt_reasons() const
{
  return (io_control && io_control->interrupt() ? 1U << 4 : 0)
       | (tcds && tcds->interrupt() ? 1U << 3 : 0)
       | (graphics && graphics->interrupt() ? 1U << 2 : 0);
}

void Pelican::update_io_irq(bool force)
{
  const bool level = interrupt_reasons() != 0;
  if (get_cpu_num() && (force || level != io_irq_asserted)) {
    io_irq_asserted = level;
    get_cpu(0)->irq_h(3, level, 0); // TURBOchannel interrupt sources share IRQ3 (EK-D3SYS-PM, §§3.2.1, 17.5).
  }
}

void Pelican::update_clock_irq(bool force)
{
  const bool level = io_control && io_control->clock_interrupt();
  if (get_cpu_num() && (force || level != clock_irq_asserted)) {
    clock_irq_asserted = level;
    get_cpu(0)->irq_h(1, level, 0); // The TOY drives IRQ1 directly (EK-D3SYS-PM, §17.5).
  }
}

void Pelican::finish_board_restore()
{
  update_io_irq(true);
  update_clock_irq(true);
}

MouseInput* Pelican::mouse_input()
{
  return io_control ? io_control->mouse_input() : nullptr;
}

KeyboardInput* Pelican::keyboard_input()
{
  return io_control ? io_control->keyboard_input() : nullptr;
}

int Pelican::LoadROM()
{
  if (direct_srm) {
    // V7's decompressor runs in PALmode in low RAM before MACHINE_RESET (EK-D3SYS-PM, §§12.1, 14.1).
    constexpr u64 system_base = 0x4000;
    io_control->load_firmware(PtrToMem(system_base), PtrToMem(0x44000));
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

bool Pelican::lock_addresses_match(int, u64 locked, u64 address) const
{
  // A RAM LDx_L/STx_C pair is guaranteed within one aligned 16-byte block (Alpha A.R.M. §§4.2.4–5).
  return (locked ^ address) < 16 && ram_offset(address) < population.bytes;
}

u8 Pelican::diagnostic_leds()
{
  std::lock_guard<std::recursive_mutex> guard(get_device_bus_mutex());
  return lit_leds;
}
