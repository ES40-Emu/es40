/* ES40 emulator -- DEC EtherWORKS EISA Ethernet adapter
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
#include "DE422.h"
#include "NetworkBackend.h"
#include "chipset/EISABus.h"
#include "system/System.h"
#include <algorithm>
#include <charconv>
#include <cstring>

DE422::DE422(CConfigurator* cfg, CSystem* system, EISABus& bus, unsigned slot)
  : CSystemComponent(cfg, system), bus(bus), slot(slot),
    lance([this](u32 addr, u16& word) {
            if (addr >= sizeof(ram) - 1) return false;
            word = u16(ram[addr] | (u16(ram[addr + 1]) << 8));
            return true;
          },
          [this](u32 addr, u16 word) {
            if (addr >= sizeof(ram) - 1) return false;
            ram[addr] = u8(word);
            ram[addr + 1] = u8(word >> 8);
            return true;
          },
          [this](bool asserted) { route_irq(asserted); },
          [this](const u8* frame, size_t length) {
            if (backend) backend->send(frame, int(length));
          })
{
  set_station_address(cfg->get_text_value("mac"));
  bus.register_slot_id(slot, {0x10, 0xa3, 0x42, 0x20}); // DEC4220 (Digital UNIX EISA guide, §9.2.1).
  const u16 base = u16((slot << 12) | 0xc00);
  // zC00 NI CSR, zC04 RDP, zC06 RAP, zC0C PROM/config (EK-DEPCA-PR, pp. 2-7–2-9; Digital UNIX EISA guide, §9.6.1).
  bus.register_slot_io(slot, *this, NiCsr, base, 2, 16);
  bus.register_slot_io(slot, *this, Rdp, base + 4, 2, 16);
  bus.register_slot_io(slot, *this, Rap, base + 6, 2, 16);
  bus.register_slot_io(slot, *this, AddressProm, base + 12, 2, 16);
  bus.register_slot_mem(slot, *this, Buffer, 0xc0000, 0x30000, 32);

  save_item(ram, "ram");
  save_item(prom_address, "prom_address");
  save_item(config, "config");
  save_item(nicsr, "nicsr");
  auto& s = lance.state;
  save_item(s.rap, "lance.rap");
  save_item(s.csr, "lance.csr");
  save_item(s.mode, "lance.mode");
  save_item(s.loop_carrier_lost, "lance.loop_carrier_lost");
  save_item(s.mac, "lance.mac");
  save_item(s.filter, "lance.filter");
  save_item(s.rx.address, "lance.rx.address");
  save_item(s.rx.count, "lance.rx.count");
  save_item(s.rx.current, "lance.rx.current");
  save_item(s.tx.address, "lance.tx.address");
  save_item(s.tx.count, "lance.tx.count");
  save_item(s.tx.current, "lance.tx.current");
}

DE422::~DE422()
{
  if (backend) backend->close();
}

void DE422::ResetPCI()
{
  config = default_config;
  nicsr = 0;
  prom_address = 0; // The station PROM counter clears on system reset (EK-DEPCA-PR, p. 2-9).
  lance.reset();
}

void DE422::init()
{
  if (strcasecmp(myCfg->get_text_value("type", "none"), "none") == 0) return;
#if defined(HAVE_PCAP) || defined(HAVE_TAP_NET) || defined(HAVE_VMNET) || defined(HAVE_SLIRP)
  backend.reset(create_network_backend(devid_string, myCfg));
  if (!backend || !backend->init(devid_string, myCfg))
    FAILURE(Configuration, "Could not initialize the DE422 network backend");
  // The LANCE applies the guest's address and multicast filter to each received frame.
  backend->set_filter(nullptr, 0, true, false, false, false);
#else
  FAILURE(Configuration, "No host network backend is available for the DE422 in this build");
#endif
}

void DE422::check_state()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  if (!bus.slot_enabled(slot)) return;
  lance.poll();
  if (!backend) return;
  for (unsigned i = 0; i < 64; ++i) {
    const u8* frame;
    int length;
    if (backend->receive(&frame, &length) != 1) break;
    if (length > 0) lance.receive(frame, size_t(length));
  }
}

void DE422::set_station_address(const char* mac)
{
  u8 station[6] = {0x08, 0x00, 0x2b, 0xe5, 0x22, u8(slot)};
  if (mac) {
    if (std::strlen(mac) != 17 || !std::strchr(":-.", mac[2]))
      FAILURE(Configuration, "Invalid DE422 Ethernet address");
    for (unsigned i = 0; i < 6; ++i) {
      const char* pair = mac + 3 * i;
      unsigned value;
      auto parsed = std::from_chars(pair, pair + 2, value, 16);
      if (parsed.ec != std::errc() || parsed.ptr != pair + 2 || (i < 5 && pair[2] != mac[2]))
        FAILURE(Configuration, "Invalid DE422 Ethernet address");
      station[i] = u8(value);
    }
  }
  // DEC station PROM format repeats the address and checksum around a reversed copy (PMAD-AA spec, §6.4, Table 6-1).
  u32 checksum = 0;
  for (unsigned i = 0; i < 6; i += 2)
    checksum = (2 * checksum + station[i] + (u32(station[i + 1]) << 8)) % 0xffff;
  std::copy_n(station, 6, prom);
  prom[6] = u8(checksum);
  prom[7] = u8(checksum >> 8);
  std::reverse_copy(prom, prom + 8, prom + 8);
  std::copy_n(prom, 8, prom + 16);
  const u8 pattern[] = {0xff, 0x00, 0x55, 0xaa, 0xff, 0x00, 0x55, 0xaa};
  std::copy_n(pattern, 8, prom + 24);
}

std::string DE422::snapshot_identity() const
{
  std::string identity = CSystemComponent::snapshot_identity() + ":";
  static constexpr char hex[] = "0123456789abcdef";
  for (unsigned i = 0; i < 6; ++i) {
    identity += hex[prom[i] >> 4];
    identity += hex[prom[i] & 15];
  }
  return identity;
}

void DE422::finalize_restore() noexcept
{
  lance.sync_irq();
}

void DE422::route_irq(bool asserted)
{
  // CFG[6:3] select IRQ11/10/9/5; NI CSR enables and masks the output (Digital UNIX EISA guide, §9.5.1).
  constexpr int irqs[] = {5, 9, 10, 11};
  const bool enabled = asserted && (nicsr & (IrqMask | IrqEnable)) == IrqEnable;
  for (unsigned i = 0; i < 4; ++i)
    bus.set_slot_irq(slot, irqs[i], enabled && (config & (8U << i)));
}

u32 DE422::window_base() const noexcept
{
  // Relative to C0000, CFG[1:0] select C/E/D and CFG[2] selects the upper half (Digital UNIX EISA guide, §9.7.1).
  static constexpr u32 bases[] = {0, 0x20000, 0x10000};
  return bases[config & 3] + ((config & 4) ? 0x8000 : 0);
}

u32 DE422::buffer_offset(u32 address) const noexcept
{
  const u32 bank = (config & 4) && !(nicsr & LowBank) ? 0x8000 : 0;
  return address - window_base() + bank;
}

bool DE422::decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept
{
  if (index == AddressProm) return address == 0 && dsize == (write ? 16 : 8);
  if (index == NiCsr) return address + dsize / 8 <= 2;
  if (index == Rap || index == Rdp) return bus.slot_enabled(slot) && address == 0 && dsize == 16;
  if (index != Buffer || !(nicsr & ShadowEnable) || (config & 3) == 3) return false;
  const u32 window = window_base();
  const u32 size = (config & 4) ? 0x8000 : 0x10000;
  return address >= window && address - window + dsize / 8 <= size;
}

u64 DE422::ReadMem(int index, u64 address, int dsize)
{
  switch (index) {
  case NiCsr: {
    u16 status = nicsr | (config & 0x100) | ((config & 0x80) >> 3) | ((config & 4) << 3);
    return dsize == 16 ? status : u8(status >> (8 * address));
  }
  case Rdp: return lance.read_rdp();
  case Rap: return lance.read_rap();
  case AddressProm: {
    u8 value = prom[prom_address & 31]; // EARDP increments and wraps after 32 reads (EK-DEPCA-PR, p. 2-9).
    prom_address = (prom_address + 1) & 31;
    return value;
  }
  case Buffer: {
    u32 value = 0;
    u32 offset = buffer_offset(u32(address));
    for (int i = 0; i < dsize / 8; ++i)
      value |= u32(ram[offset + i]) << (8 * i);
    return value;
  }
  }
  return 0;
}

void DE422::WriteMem(int index, u64 address, int dsize, u64 data)
{
  switch (index) {
  case NiCsr:
    if (address == 0) nicsr = u16(data) & 0xcf;
    lance.sync_irq();
    break;
  case Rdp: lance.write_rdp(u16(data)); break;
  case Rap: lance.write_rap(u16(data)); break;
  case AddressProm:
    config = u16(data) & 0x1ff;
    lance.sync_irq();
    break;
  case Buffer: {
    u32 offset = buffer_offset(u32(address));
    for (int i = 0; i < dsize / 8; ++i)
      ram[offset + i] = u8(data >> (8 * i));
    break;
  }
  }
}
