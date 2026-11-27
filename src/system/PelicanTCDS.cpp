/* ES40 emulator -- DEC 3000 Model 300X TURBOchannel Dual SCSI ASIC
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
#include "system/PelicanTCDS.h"
#include "system/System_pelican.h"
#include "storage/Disk.h"
#include "storage/SCSIBus.h"

enum { Dense, Sparse };
static constexpr u32 cir_offset = 0x40000;
static constexpr u32 sda_offset = 0x41000;
static constexpr u32 asc_offset = 0x80000;
static constexpr u32 buffer_offset = 0xc0000;
static constexpr u32 cir_controls = 0x0000ff0f;
static constexpr u32 dma_enable = 1U << 8;
static constexpr u32 dma_reset_n = 1U << 10;
static constexpr u32 cir_dreq = 1U << 16;
static constexpr u32 cir_irq = 1U << 18;
static constexpr u32 dma_error = 1U << 22;
static constexpr u8 read_prefetch = 0x40;
static constexpr u8 write_memory = 0x80;

PelicanTCDS::PelicanTCDS(CConfigurator* cfg, Pelican* system)
  : CSystemComponent(cfg, system), CDiskController(1, 8), board(*system), scsi(new CSCSIBus(cfg, system)),
    asc(machine_config{}, "asc", nullptr, 25000000)
{
  // Both aliases address the onboard TCDS in slot 4 (EK-D3SYS-PM, Tables 5, 31–32).
  board.RegisterMemory(this, Dense, U64(0x180000000), buffer_offset + 0x180);
  board.RegisterMemory(this, Sparse, U64(0x190000000), 2 * (buffer_offset + 0x180));
  asc.set_save_owner(*this);
  asc.set_busmd(ncr53c94_device::BUSMD_3); // The TCDS uses 16-bit NCR DMA transfers (EK-D3SYS-PM, §§8.2.6, 9.6.3).
  asc.scsi_register(0, scsi, 7);
  asc.drq_handler_cb().set([this](int level) {
    if (level) cir |= (imer << 16) & cir_dreq;
  });
  asc.irq_handler_cb().set([this](int level) {
    if (level) pending_irq = true;
  });
  save_item(cir, "cir");
  save_item(imer, "imer");
  save_item(lane, "lane");
  save_item(buffer_index, "buffer_index");
  save_item(prefetched, "prefetched");
  save_item(first_partial, "first_partial");
  save_item(dma_active, "dma_active");
  save_item(pending_irq, "pending_irq");
  for (int i = 0; i < 2; ++i) {
    save_item(channel[i].address, "sda", i);
    save_item(channel[i].control, "dic", i);
    save_item(channel[i].first, "dud0", i);
    save_item(channel[i].last, "dud1", i);
    save_item(channel[i].buffer, "buffer", i);
  }
}

void PelicanTCDS::init()
{
  asc.start();
  ResetPCI();
}

void PelicanTCDS::register_disk(CDisk* disk, int bus, int id)
{
  if (bus || id < 0 || id >= 7)
    FAILURE(Configuration, "Pelican SCSI disks use disk0.0 through disk0.6 (host ID 7)");
  CDiskController::register_disk(disk, bus, id);
  disk->scsi_register(0, scsi, id);
  disk->scsi_reset();
}

bool PelicanTCDS::interrupt() const
{
  return (cir & imer & 0xffff0000U) != 0; // IMER<31:16> gates the latched CIR reasons (EK-D3SYS-PM, §8.2.2).
}

bool PelicanTCDS::decodes_memory_access(int index, u64 address, int dsize, bool) const noexcept
{
  if ((dsize != 32 && dsize != 64) || (address & ((index == Sparse || dsize == 64) ? 7 : 3)))
    return false;
  const u64 offset = index == Sparse ? address >> 1 : address;
  const u64 bank_offset = offset & ~u64{0x100};
  return offset == cir_offset || offset == cir_offset + 4 ||
    (index == Sparse && offset >= asc_offset && offset < asc_offset + 64) || // NCR registers require sparse I/O (EK-D3SYS-PM, §9.6.2).
    (bank_offset >= sda_offset && bank_offset < sda_offset + 16) ||
    (bank_offset >= buffer_offset && bank_offset < buffer_offset + 128);
}

u32 PelicanTCDS::read_word(u32 offset)
{
  if (offset == cir_offset)
    return cir | 0xf0; // GPI<3:0> are high, including the 25 MHz oscillator strap (EK-D3SYS-PM, §§8.2.1, 8.3).
  if (offset == cir_offset + 4)
    return imer;
  if (offset >= asc_offset && offset < asc_offset + 64)
    return (cir & dma_reset_n) ? asc.read((offset - asc_offset) >> 2) : 0;
  auto& port = channel[(offset >> 8) & 1];
  offset &= ~0x100U;
  if (offset >= buffer_offset && offset < buffer_offset + sizeof(port.buffer))
    return port.buffer[(offset - buffer_offset) >> 2];
  switch (offset) {
  case sda_offset: return port.address;
  case sda_offset + 4: return port.control;
  case sda_offset + 8: return port.first;
  case sda_offset + 12: return port.last;
  default: return 0xffffffffU;
  }
}

void PelicanTCDS::write_word(u32 offset, u32 value, u32 mask)
{
  if (offset == cir_offset) {
    const u32 previous = cir;
    const u32 controls = mask & cir_controls;
    cir = (cir & ~controls) | (value & controls);
    cir &= value | ~mask | 0xffffU; // CIR reasons clear on a zero write (EK-D3SYS-PM, §8.2.1).
    for (unsigned i = 0; i < 2; ++i) {
      if ((previous & ~cir) & (dma_reset_n << i)) {
        channel[i].first = channel[i].last = 0;
        std::fill_n(channel[i].buffer, 32, 0); // The DMA engine reset clears its buffer (EK-D3SYS-PM, §8.5).
        if (!i) {
          asc.reset();
          dma_active = pending_irq = false;
        }
      }
    }
  } else if (offset == cir_offset + 4) {
    const u32 previous = imer;
    imer = (imer & ~mask) | (value & mask);
    const u32 enabled = (~previous & imer) << 16;
    if (asc.drq_state()) cir |= enabled & cir_dreq;
    if (asc.irq_state() && (enabled & cir_irq)) pending_irq = true;
  } else if (offset >= asc_offset && offset < asc_offset + 64) {
    if (cir & dma_reset_n)
      asc.write((offset - asc_offset) >> 2, u8(value)); // NCR register writes ignore byte enables (EK-D3SYS-PM, §8.3).
  } else {
    const unsigned index = (offset >> 8) & 1;
    auto& port = channel[index];
    offset &= ~0x100U;
    if (offset == sda_offset) {
      port.address = (port.address & ~mask) | (value & mask);
      if (!index && mask) dma_active = false;
    } else if (offset == sda_offset + 4) {
      port.control = u8((port.control & ~mask) | (value & mask));
      if (!index && (mask & 0xff)) dma_active = false;
    } else if (offset >= buffer_offset && offset < buffer_offset + sizeof(port.buffer))
      port.buffer[(offset - buffer_offset) >> 2] = value; // Diagnostic buffer writes ignore byte enables (EK-D3SYS-PM, §8.5).
  }
}

u64 PelicanTCDS::ReadMem(int index, u64 address, int dsize)
{
  refresh_controller();
  const u32 offset = u32(index == Sparse ? address >> 1 : address);
  const u32 low = read_word(offset);
  const u32 high = dsize == 64 ? (index == Sparse ? low : read_word(offset + 4)) : 0; // EK-D3SYS-PM, Tables 75–76.
  service_dma();
  return low | (u64(high) << 32);
}

void PelicanTCDS::WriteMem(int index, u64 address, int dsize, u64 data)
{
  refresh_controller();
  const u32 offset = u32(index == Sparse ? address >> 1 : address);
  u32 mask = ~0U;
  if (index == Sparse && dsize == 64) {
    mask = 0;
    for (unsigned i = 0; i < 4; ++i)
      if (data & (u64{1} << (32 + i))) mask |= 0xffU << (8 * i);
  }
  write_word(offset, u32(data), mask);
  if (index == Dense && dsize == 64)
    write_word(offset + 4, u32(data >> 32), ~0U);
  service_dma();
}

void PelicanTCDS::ResetPCI()
{
  cir = imer = 0;
  for (auto& port : channel)
    port = {};
  asc.reset();
  lane = buffer_index = prefetched = 0;
  first_partial = dma_active = pending_irq = false;
  board.update_io_irq();
}

void PelicanTCDS::dma_write_byte(u8 data)
{
  Channel& port = channel[0];
  const u32 byte = u32(data) << (8 * lane);
  if (first_partial) {
    port.first |= byte | (1U << lane); // DUD0 reports leading byte lanes for software to merge (EK-D3SYS-PM, §8.2.5).
  } else {
    u32& word = port.buffer[buffer_index];
    if (!lane) word = 0;
    word |= byte;
    if (lane == 3) {
      board.WriteMem(u64(port.address) << 2, 32, word, this);
      port.last = 0;
    } else {
      port.last = word | (((2U << lane) - 1) << 24); // DUD1 exposes the final partial longword (EK-D3SYS-PM, §8.2.6).
    }
  }
  advance_dma_address();
}

u8 PelicanTCDS::dma_read_byte()
{
  if (!prefetched) fill_read_buffer();
  const u8 data = u8(channel[0].buffer[buffer_index] >> (8 * lane));
  advance_dma_address();
  return data;
}

void PelicanTCDS::advance_dma_address()
{
  if (++lane == 4) {
    lane = 0;
    first_partial = false;
    ++channel[0].address;
    buffer_index = (buffer_index + 1) & 31;
    if (prefetched) --prefetched;
  }
}

void PelicanTCDS::fill_read_buffer()
{
  Channel& port = channel[0];
  const unsigned limit = (port.control & read_prefetch) ? 32 : 1; // DIC<6> enables DMA read prefetch (EK-D3SYS-PM, §8.2.4).
  for (unsigned i = 0; i < limit; ++i) {
    const u64 address = (u64(port.address) + i) << 2;
    if (board.ram_offset(address) == ~u64{0})
      break;
    port.buffer[(buffer_index + i) & 31] = u32(board.ReadMem(address, 32, this));
    ++prefetched;
  }
}

void PelicanTCDS::service_dma()
{
  Channel& port = channel[0];
  const bool receiving = (port.control & write_memory) != 0;
  while ((cir & dma_enable) && asc.drq_state() && receiving == asc.dma_to_memory()) {
    if (!dma_active) {
      dma_active = true;
      lane = port.control & 3;
      first_partial = lane != 0;
      prefetched = buffer_index = 0;
      port.first = port.last = 0;
    }
    const u64 address = (u64(port.address) << 2) + lane;
    if (board.ram_offset(address) == ~u64{0} || board.ram_offset(address + 1) == ~u64{0}) {
      cir |= (imer << 16) & dma_error;
      break;
    }
    if (receiving) {
      const u16 data = asc.dma16_r();
      dma_write_byte(u8(data));
      dma_write_byte(u8(data >> 8));
    } else {
      const u8 low = dma_read_byte();
      const u8 high = dma_read_byte();
      asc.dma16_w(low | (u16(high) << 8));
    }
  }
  if (cir & imer & dma_error)
    cir &= ~dma_enable; // An unmasked DMA error disables its channel (EK-D3SYS-PM, §8.2.1).
  if (pending_irq && (!(cir & dma_enable) || !asc.drq_state())) {
    cir |= (imer << 16) & cir_irq; // Post the NCR interrupt after pending DMA has drained (EK-D3SYS-PM, §8.2.1).
    pending_irq = dma_active = false;
  }
  board.update_io_irq();
}

void PelicanTCDS::refresh_controller()
{
  if (device_sample == std::chrono::steady_clock::time_point{})
    return;
  const auto now = board.device_access_time();
  if (now > device_sample) {
    asc.advance(std::chrono::duration_cast<std::chrono::nanoseconds>(now - device_sample).count());
    device_sample = now;
    service_dma();
  }
}

void PelicanTCDS::start_threads()
{
  std::lock_guard<std::recursive_mutex> guard(board.get_device_bus_mutex());
  device_sample = board.device_access_time();
}

void PelicanTCDS::stop_threads()
{
  std::lock_guard<std::recursive_mutex> guard(board.get_device_bus_mutex());
  refresh_controller();
  device_sample = {};
}

void PelicanTCDS::check_state()
{
  std::lock_guard<std::recursive_mutex> guard(board.get_device_bus_mutex());
  refresh_controller();
}

int PelicanTCDS::RestoreState(FILE* file)
{
  if (CSystemComponent::RestoreState(file))
    return -1;
  return lane < 4 && buffer_index < 32 && prefetched <= 32 && asc.valid_state() ? 0 : -1;
}
