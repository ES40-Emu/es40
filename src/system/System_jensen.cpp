/* ES40 emulator -- Jensen host bus
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
#include "system/System.h"
#include "chipset/DMA.h"
#include "chipset/Intel82358.h"
#include <algorithm>

namespace {
enum { SparseIO, HostAddressExtension };

class JensenHostBus : public CSystemComponent, public CDMAHost
{
public:
  JensenHostBus(CConfigurator* cfg, CSystem* system);
  const CSystemComponent* isa_decode_owner() const noexcept override { return this; }
  void set_isa_irq(int, bool) override {} // Jensen PIC routing is pending.
  bool dma_transfer_enabled() override { return true; }
  void dma_read_memory(u32 address, void* dest, size_t bytes) override;
  void dma_write_memory(u32 address, const void* source, size_t bytes) override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;
  void ResetPCI() override { hae = 0; }
  Intel82358 ebc;

private:
  u8 hae = 0;
};

JensenHostBus::JensenHostBus(CConfigurator* cfg, CSystem* system)
  : CSystemComponent(cfg, system), ebc(*this)
{
  system->RegisterMemory(this, SparseIO, U64(0x300000000), U64(0x10000) << 7);
  system->RegisterMemory(this, HostAddressExtension, U64(0x1d0000000), 4);
  save_item(hae, "hae");
}

u64 JensenHostBus::ReadMem(int index, u64 address, int dsize)
{
  if (index == HostAddressExtension && !address && dsize == 32)
    return hae;
  // Jensen byte I/O uses a longword at port << 7, with the port selecting the lane.
  if (index != SparseIO || dsize != 32 || (address & 0x7f) || hae)
    return dsize == 64 ? ~U64(0) : (U64(1) << dsize) - 1;

  const u16 port = (u16)(address >> 7);
  u8 data;
  if (!ebc.read_port(port, data))
    return 0xffffffffU;
  return (u32)data << ((port & 3) * 8);
}

void JensenHostBus::WriteMem(int index, u64 address, int dsize, u64 data)
{
  if (index == HostAddressExtension && !address && dsize == 32) {
    hae = (u8)(data & 0x7f);
    return;
  }
  if (index != SparseIO || dsize != 32 || (address & 0x7f) || hae)
    return;

  const u16 port = (u16)(address >> 7);
  const u8 value = (u8)(data >> ((port & 3) * 8));
  ebc.write_port(port, value);
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

void CSystem::init_jensen_eisa()
{
  auto* host = new JensenHostBus(myCfg, this);
  auto* dma = new CDMA(myCfg, this, *host, CDMA::Model::Intel82357);
  auto& bus = host->ebc.eisa_bus();
  bus.bind_dma(*dma);
  register_isa_bus(0, bus);
}
