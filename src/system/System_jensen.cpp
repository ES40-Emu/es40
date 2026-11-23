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
enum { SparseIO, HostAddressExtension, InterruptAcknowledge, Feprom0, Feprom1, SystemControl, SparseMemory };
constexpr u64 sparse_io_size = U64(0x10000) << 7;
constexpr u64 small_bank_size = U64(1) << 24;

unsigned int checked_memory_bits(u64 bits)
{
  if (bits < 24 || bits > 27)
    FAILURE(Configuration, "Jensen memory.bits must be 24 through 27 (16 to 128 MB)");
  return static_cast<unsigned int>(bits);
}

// CA<6:5> give the length and CA<8:7> the offset in both EISA spaces (PB22H-KB, Table 8-2).
int sparse_width(u64 address)
{
  const unsigned lane = (address >> 7) & 3;
  switch (address & 0x7f) {
  case 0x00: return 8;
  case 0x20: return lane == 3 ? 0 : 16;
  case 0x60: return lane == 0 ? 32 : 0;
  default: return 0;
  }
}

int sparse_io_width(u64 address)
{
  return address < sparse_io_size ? sparse_width(address) : 0;
}

class JensenHostBus : public CSystemComponent, public CDMAHost
{
  Jensen& board;
  JensenFlash feprom[2];
public:
  JensenHostBus(CConfigurator* cfg, Jensen* system);
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
    board.write_sysctl(0);
    for (auto& socket : feprom)
      socket.reset();
    ebc.eisa_bus().reset();
  }
  Intel82357* const isp;
  Intel82358 ebc;

private:
  u8 hae = 0;
};

JensenHostBus::JensenHostBus(CConfigurator* cfg, Jensen* system)
  : CSystemComponent(cfg, system), board(*system),
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
  system->RegisterMemory(this, SparseMemory, U64(0x200000000), U64(1) << 32); // PB22H-KB, Table 8-1.
  system->RegisterMemory(this, HostAddressExtension, U64(0x1d0000000), 4);
  system->RegisterMemory(this, SystemControl, U64(0x1e0000000), 8); // PB22H-KB, Table 8-1.
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
  if (fwrite("JFL2", 4, 1, file) != 1 || fputc(board.read_sysctl(), file) == EOF ||
      CSystemComponent::SaveState(file))
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
  // Older snapshots used a contiguous array of installed RAM.
  const auto bits = board.get_memory_bits();
  int sysctl = bits == 27 ? 0xa0 : bits == 26 ? 0x20 : 0;
  if (!legacy) {
    char tag[4];
    if (fread(tag, sizeof(tag), 1, file) != 1)
      return -1;
    if (memcmp(tag, "JFL2", sizeof(tag)) == 0) {
      sysctl = fgetc(file);
      if (sysctl == EOF)
        return -1;
    } else if (memcmp(tag, "JFL1", sizeof(tag)) != 0)
      return -1;
  }
  if (CSystemComponent::RestoreState(file) || !feprom[0].restore(file, legacy) ||
      !feprom[1].restore(file, legacy))
    return -1;
  board.write_sysctl(u8(sysctl));
  return 0;
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
  if (index == SystemControl && !address && (dsize == 32 || dsize == 64))
    return board.read_sysctl();
  // Each aligned H_BUS read supplies one INTA pulse; the vector is in the low byte.
  if (index == InterruptAcknowledge && !address && (dsize == 32 || dsize == 64))
    return isp->interrupt_acknowledge();
  // EISA memory uses a CPU longword too; HAE<6:0> extends CA<31:7> to the EISA address (PB22H-KB, Table 8-1).
  if (index == SparseMemory && dsize == 32) {
    const u32 target = ((u32)hae << 25) | (u32)(address >> 7);
    u32 data;
    if (!ebc.read_mem(target, sparse_width(address), data))
      return 0xffffffffU;
    return data << ((target & 3) * 8);
  }
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
  if (index == SystemControl && !address && (dsize == 32 || dsize == 64)) {
    board.write_sysctl(u8(data));
    return;
  }
  if (index == SparseMemory && dsize == 32) {
    const u32 target = ((u32)hae << 25) | (u32)(address >> 7);
    ebc.write_mem(target, sparse_width(address), (u32)data >> ((target & 3) * 8));
    return;
  }
  if (index != SparseIO || dsize != 32 || hae)
    return;

  const u16 port = (u16)(address >> 7);
  const u32 value = (u32)data >> ((port & 3) * 8);
  ebc.write_port(port, sparse_io_width(address), value);
}

// Split at SIMM decode boundaries and the EISA hole in the first MiB (PB22H-KB, pp. 4-4, 7-2).
static char* dma_ram_span(CSystem& system, u64 address, size_t& bytes)
{
  if (address >= (U64(1) << 31))
    return nullptr;
  address &= (U64(1) << 27) - 1; // HA[30:27] are ignored (PB22H-KB, p. 7-2).
  u64 end = (address | (small_bank_size - 1)) + 1;
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

void Jensen::write_sysctl(u8 value)
{
  // SYSCTL[7:4] selects the banks and [3:0] drives the LEDs (PB22H-KB, Fig. 5-1).
  if ((sysctl.exchange(value, std::memory_order_relaxed) ^ value) & 0xf0)
    ram_mapping_changed();
}

u64 Jensen::ram_offset(u64 address) const noexcept
{
  if (address >> 32)
    return ~U64(0);
  address &= 0x0fffffff; // Local memory ignores CA[31:28] (PB22H-KB, p. 4-4).

  const unsigned config = read_sysctl();
  const unsigned bank_config[2] = {(config >> 4) & 3, config >> 6};
  // The larger bank starts at zero, with bank 0 first for equal sizes (PB22H-KB, Table 4-2).
  unsigned bank = bank_config[1] > bank_config[0] ? 1 : 0;
  const u64 first_size = small_bank_size << bank_config[bank];
  if (address >= first_size) {
    address -= first_size;
    bank ^= 1;
  }
  // Single-sided SIMMs respond only in the first side of the window (PB22H-KB, Fig. 5-1).
  const u64 side_size = small_bank_size << (bank_config[bank] & 2);
  if (address >= side_size)
    return ~U64(0);

  // Each populated bank holds four identical 1M x 36 or 4M x 36 SIMMs (PB22H-KB, Table 4-1).
  const u64 installed_size = U64(1) << get_memory_bits();
  const u64 installed_bank_size = installed_size < 4 * small_bank_size ? small_bank_size : 4 * small_bank_size;
  const u64 bank_offset = bank * installed_bank_size;
  if (bank_offset >= installed_size)
    return ~U64(0);
  return bank_offset + (address & (installed_bank_size - 1));
}

u64 Jensen::linear_ram_size() const noexcept
{
  if (get_cpu_num() > 1)
    return 0;
  const u64 installed_size = U64(1) << get_memory_bits();
  u64 end = 0;
  while (end < installed_size && ram_offset(end) == end)
    end += small_bank_size;
  return end;
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
  }, [isp = host->isp](bool asserted) {
    isp->set_isa_irq(1, asserted); // IRQP drives ISP IRQ1 (PB22H-KB, p. 1-11 and Table 17-2).
  });
}

void Jensen::register_legacy_io(CSystemComponent& device, int index, int bus, u32 port, u32 length)
{
  // Fixed-address ports are ISA ports, which the EISA bus decodes.
  CISABus* owner = isa_bus(bus);
  if (!owner || port > 0xffff)
    FAILURE_2(Configuration, "Port %x is not on ISA bus isa%d", port, bus);
  owner->register_isa_io(device, index, (u16)port, length);
}

void Jensen::register_legacy_mem(CSystemComponent& device, int index, int bus, u32 address, u32 length)
{
  // Fixed-address memory is ISA memory; like the ports it gives no size signal, so it is an 8-bit slave.
  auto* owner = dynamic_cast<EISABus*>(isa_bus(bus));
  if (!owner)
    FAILURE_2(Configuration, "Address %x is not on ISA bus isa%d", address, bus);
  owner->register_eisa_mem(device, index, address, length, 8);
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
