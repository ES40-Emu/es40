/* ES40 emulator -- Jensen system and host bus
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
#include "system/System_jensen.h"
#include "system/JensenFlash.h"
#include "CPU/AlphaCPU.h"
#include "chipset/Intel82357.h"
#include "chipset/Intel82358.h"
#include "chipset/VL82C106.h"
#include <algorithm>

namespace {
enum { SparseIO, HostAddressExtension, InterruptAcknowledge, Feprom0, Feprom1 };
constexpr u64 sparse_io_size = U64(0x10000) << 7;

unsigned int checked_memory_bits(u64 bits)
{
  if (bits < 24 || bits > 27)
    FAILURE(Configuration, "Jensen memory.bits must be 24 through 27 (16 to 128 MB)");
  return static_cast<unsigned int>(bits);
}

int sparse_io_width(u64 address)
{
  if (address >= sparse_io_size)
    return 0;
  const unsigned lane = (address >> 7) & 3;
  switch (address & 0x7f) {
  case 0x00: return 8;
  case 0x20: return lane == 3 ? 0 : 16;
  case 0x60: return lane == 0 ? 32 : 0;
  default: return 0;
  }
}

class JensenHostBus : public CSystemComponent, public CDMAHost
{
  JensenFlash feprom[2];
public:
  JensenHostBus(CConfigurator* cfg, CSystem* system);
  ~JensenHostBus() override;
  void start_threads() override;
  void stop_threads() override;
  void check_state() override;
  void flush_storage() override;
  int SaveState(FILE* file) override;
  int RestoreState(FILE* file) override;
  void finalize_restore() noexcept override;
  const CSystemComponent* isa_decode_owner() const noexcept override { return this; }
  bool dma_transfer_enabled() override { return true; }
  void dma_read_memory(u32 address, void* dest, size_t bytes) override;
  void dma_write_memory(u32 address, const void* source, size_t bytes) override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;
  void ResetPCI() override
  {
    hae = 0;
    for (auto& socket : feprom)
      socket.reset();
    ebc.eisa_bus().reset();
  }
  Intel82357* const isp;
  Intel82358 ebc;

private:
  u8 hae = 0;
};

JensenHostBus::JensenHostBus(CConfigurator* cfg, CSystem* system)
  : CSystemComponent(cfg, system),
    feprom{{cfg, *system, 0}, {cfg, *system, 1}},
    isp(new Intel82357(cfg, system, *this, [system](bool asserted) {
      if (system->get_cpu_num())
        system->get_cpu(0)->irq_h(1, asserted, 0);
    })),
    ebc(*isp)
{
  feprom[0].check_paths(feprom[1]);
  const char* battery_path = cfg->get_text_value("rom.toy", VL82C106::default_battery_path);
  if (feprom[0].uses_path(battery_path) || feprom[1].uses_path(battery_path))
    FAILURE(Configuration, "Jensen RTC and flash require separate image files");
  system->RegisterMemory(this, SparseIO, U64(0x300000000), sparse_io_size);
  system->RegisterMemory(this, HostAddressExtension, U64(0x1d0000000), 4);
  system->RegisterMemory(this, InterruptAcknowledge, U64(0x100000000), 8);
  // PB22H-KB Tables 8-1 and 8-3: CA[28:9] selects the ROM byte within each socket.
  system->RegisterMemory(this, Feprom0, U64(0x180000000), u64(feprom[0].size()) << 9);
  system->RegisterMemory(this, Feprom1, U64(0x1a0000000), u64(feprom[1].size()) << 9);
  save_item(hae, "hae");
  save_item(ebc.eisa_bus().snapshot_state().enabled_slots, "eisa.enabled_slots");
  save_item(ebc.eisa_bus().snapshot_state().irq_requests, "eisa.irq_requests");
  save_item(ebc.eisa_bus().snapshot_state().drq_requests, "eisa.drq_requests");
}

JensenHostBus::~JensenHostBus()
{
  for (auto& socket : feprom) {
    try {
      socket.pause();
    } catch (const CException& error) {
      printf("%%FLS-F-NOSAVE: %s\n", error.displayText().c_str());
    }
  }
}

void JensenHostBus::start_threads()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  for (auto& socket : feprom)
    socket.resume();
}

void JensenHostBus::stop_threads()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  for (auto& socket : feprom)
    socket.pause();
}

void JensenHostBus::check_state()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  for (auto& socket : feprom)
    socket.service(false);
}

void JensenHostBus::flush_storage()
{
  for (auto& socket : feprom)
    socket.service(true);
}

int JensenHostBus::SaveState(FILE* file)
{
  if (fwrite("JFL1", 4, 1, file) != 1 || CSystemComponent::SaveState(file))
    return -1;
  return feprom[0].save(file) && feprom[1].save(file) ? 0 : -1;
}

int JensenHostBus::RestoreState(FILE* file)
{
  // Older host-bus records contain only registered state and used the original ROM bytes.
  const int first = fgetc(file);
  if (first == EOF || ungetc(first, file) == EOF)
    return -1;
  const bool legacy = first != 'J';
  char tag[4];
  if (!legacy && (fread(tag, sizeof(tag), 1, file) != 1 || memcmp(tag, "JFL1", sizeof(tag))))
    return -1;
  if (CSystemComponent::RestoreState(file))
    return -1;
  return feprom[0].restore(file, legacy) && feprom[1].restore(file, legacy) ? 0 : -1;
}

void JensenHostBus::finalize_restore() noexcept
{
  for (auto& socket : feprom)
    socket.finalize_restore();
}

u64 JensenHostBus::ReadMem(int index, u64 address, int dsize)
{
  if ((index == Feprom0 || index == Feprom1) && !(address & 0x1ff) && (dsize == 32 || dsize == 64))
    return feprom[index - Feprom0].read(u32(address >> 9)); // L_BUS drives the low byte (PB22H-KB, p. 8-8).
  if (index == HostAddressExtension && !address && dsize == 32)
    return hae;
  // Each aligned H_BUS read supplies one INTA pulse; the vector is in the low byte.
  if (index == InterruptAcknowledge && !address && (dsize == 32 || dsize == 64))
    return isp->interrupt_acknowledge();
  // Sparse I/O uses a CPU longword, with the port selecting the first byte lane.
  if (index != SparseIO || dsize != 32 || hae)
    return dsize == 64 ? ~U64(0) : (U64(1) << dsize) - 1;

  const u16 port = (u16)(address >> 7);
  u32 data;
  if (!ebc.read_port(port, sparse_io_width(address), data))
    return 0xffffffffU;
  return data << ((port & 3) * 8);
}

void JensenHostBus::WriteMem(int index, u64 address, int dsize, u64 data)
{
  if ((index == Feprom0 || index == Feprom1) && !(address & 0x1ff) && (dsize == 32 || dsize == 64)) {
    feprom[index - Feprom0].write(u32(address >> 9), u8(data));
    return;
  }
  if (index == HostAddressExtension && !address && dsize == 32) {
    hae = (u8)(data & 0x7f);
    return;
  }
  if (index != SparseIO || dsize != 32 || hae)
    return;

  const u16 port = (u16)(address >> 7);
  const u32 value = (u32)data >> ((port & 3) * 8);
  ebc.write_port(port, sparse_io_width(address), value);
}

// Split at installed RAM and the EISA memory hole in the upper half of the first MiB.
static char* dma_ram_span(CSystem& system, u64 address, size_t& bytes)
{
  u64 end = U64(1) << system.get_memory_bits();
  if (address >= end)
    return nullptr;
  if (address < 0x80000)
    end = 0x80000;
  else if (address < 0x100000) {
    bytes = (std::min)(bytes, (size_t)(0x100000 - address));
    return nullptr;
  }
  bytes = (std::min)(bytes, (size_t)(end - address));
  return system.PtrToMem(address);
}

void JensenHostBus::dma_read_memory(u32 address, void* dest, size_t bytes)
{
  auto* out = static_cast<u8*>(dest);
  for (size_t offset = 0; offset < bytes; ) {
    size_t count = bytes - offset;
    const char* ram = dma_ram_span(*cSystem, (u64)address + offset, count);
    if (ram)
      memcpy(out + offset, ram, count);
    else
      memset(out + offset, 0xff, count);
    offset += count;
  }
}

void JensenHostBus::dma_write_memory(u32 address, const void* source, size_t bytes)
{
  const auto* in = static_cast<const u8*>(source);
  for (size_t offset = 0; offset < bytes; ) {
    size_t count = bytes - offset;
    char* ram = dma_ram_span(*cSystem, (u64)address + offset, count);
    if (ram)
      memcpy(ram, in + offset, count);
    offset += count;
  }
}
}

Jensen::Jensen(CConfigurator* cfg)
  : CSystem(cfg, ~U64(0), checked_memory_bits(cfg->get_num_value("memory.bits", false, 27)))
{
}

Jensen::~Jensen()
{
  if (combo)
    combo->stop_threads();
  destroy_components();
}

void Jensen::ResetMem(unsigned int membits)
{
  CSystem::ResetMem(checked_memory_bits(membits));
}

void Jensen::attach_board_devices(CConfigurator* cfg)
{
  auto* host = new JensenHostBus(cfg, this);
  register_isa_bus(0, host->ebc.eisa_bus());
  // IRQK and IRQM share CPU IRQ3 on Jensen (PB22H-KB, Table 6-4).
  combo = new VL82C106(cfg, this, U64(0x1c0000000), [this](bool asserted) {
    if (get_cpu_num())
      get_cpu(0)->irq_h(3, asserted, 0);
  }, [this](unsigned channel, bool asserted) { set_serial_irq(channel, asserted); },
  [this, isp = host->isp](bool asserted) {
    // The RTC drives CPU IRQ0 and ISP IRQ8(L) (PB22H-KB, Tables 6-4 and 17-2).
    isp->set_isa_irq(8, asserted);
    if (get_cpu_num())
      get_cpu(0)->irq_h(0, asserted, 0);
  });
}

CSerial& Jensen::serial_port(unsigned number) const
{
  return combo->serial_port(number);
}

void Jensen::set_serial_irq(unsigned channel, bool asserted)
{
  // IRQA and IRQB share CPU IRQ5 on Jensen (PB22H-KB, Table 6-4).
  std::lock_guard<std::mutex> guard(serial_irq_mutex);
  const bool previous = serial_irqs != 0;
  const unsigned bit = 1u << channel;
  serial_irqs = asserted ? serial_irqs | bit : serial_irqs & ~bit;
  if (previous != (serial_irqs != 0) && get_cpu_num())
    get_cpu(0)->irq_h(5, serial_irqs != 0, 0);
}

void Jensen::finish_board_restore()
{
  std::lock_guard<std::mutex> guard(serial_irq_mutex);
  if (get_cpu_num())
    get_cpu(0)->irq_h(5, serial_irqs != 0, 0);
}
