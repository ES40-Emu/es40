/* ES40 emulator -- EISA bus
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
#include "EISABus.h"
#include "DMA.h"

static constexpr unsigned last_expansion_slot = 15;
static constexpr unsigned slot_control_offset = 4; // xC84 follows the four product ID bytes.

static constexpr u16 slot_id_port(unsigned slot)
{
  return (u16)((slot << 12) | 0xc80);
}

static bool valid_io_width(int dsize)
{
  return dsize == 8 || dsize == 16 || dsize == 32;
}

void EISABus::register_isa_io(CSystemComponent& device, int index, u16 port, u32 length)
{
  register_eisa_io(device, index, port, length, 8);
}

void EISABus::register_eisa_io(CSystemComponent& device, int index, u16 port, u32 length, int max_dsize)
{
  register_io(0, device, index, port, length, max_dsize);
}

void EISABus::register_slot_io(unsigned slot, CSystemComponent& device, int index,
  u16 port, u32 length, int max_dsize)
{
  if (!slot_registered(slot))
    FAILURE(Configuration, "EISA I/O range needs a registered expansion slot");
  register_io(slot, device, index, port, length, max_dsize);
}

void EISABus::register_io(unsigned owner_slot, CSystemComponent& device, int index,
  u16 port, u32 length, int max_dsize)
{
  if (!valid_io_width(max_dsize))
    FAILURE(Configuration, "Invalid EISA I/O width");
  if (!length || length > 0x10000U - port)
    FAILURE(Configuration, "Invalid EISA I/O port range");
  for (unsigned slot = 1; slot <= last_expansion_slot; ++slot) {
    const u32 base = slot_id_port(slot);
    if (port < base + slot_control_offset + 1 && base < port + length)
      FAILURE(Configuration, "EISA I/O range overlaps reserved slot ID or control ports");
  }
  for (const auto& range : io_ranges)
    if (port < range.base + range.length && range.base < port + length)
      FAILURE(Configuration, "Overlapping EISA I/O port ranges");
  io_ranges.push_back({&device, index, port, length, max_dsize, owner_slot});
}

void EISABus::register_slot_id(unsigned slot, const std::array<u8, 4>& product_id)
{
  if (!slot || slot > last_expansion_slot)
    FAILURE(Configuration, "EISA expansion slot must be in the range 1-15");
  const u16 base = slot_id_port(slot);
  if (find_io(base))
    FAILURE(Configuration, "EISA slot product ID already registered");
  u32 value = 0;
  for (unsigned i = 0; i < product_id.size(); ++i)
    value |= (u32)product_id[i] << (i * 8);
  io_ranges.push_back({nullptr, 0, base, slot_control_offset + 1, 32, slot, value});
}

bool EISABus::slot_enabled(unsigned slot) const noexcept
{
  return slot && slot <= last_expansion_slot && (state.enabled_slots & (1U << slot));
}

bool EISABus::slot_registered(unsigned slot) const noexcept
{
  return slot && slot <= last_expansion_slot && find_io(slot_id_port(slot));
}

const EISABus::IoRange* EISABus::find_io(u32 port, unsigned bytes) const noexcept
{
  for (const auto& range : io_ranges) {
    if (port < range.base || port - range.base >= range.length)
      continue;
    if (bytes > range.length - (port - range.base))
      return nullptr;
    if (range.device && range.slot && !slot_enabled(range.slot)) {
      // Disabled boards still decode their own x000, x400, x800 and xC00 I/O windows.
      const u32 slot_base = range.slot << 12;
      const u32 last = port + bytes - 1;
      if ((port & 0xf300) != slot_base || (last & 0xf300) != slot_base)
        return nullptr;
    }
    return &range;
  }
  return nullptr;
}

CISABus::IsaIoDecode EISABus::isa_io_decode(u32 port) const noexcept
{
  return find_io(port) ? IsaIoDecode::Positive : IsaIoDecode::None;
}

const CSystemComponent* EISABus::isa_decode_owner() const noexcept
{
  return host.isa_decode_owner();
}

EISABus::RequestLines EISABus::active_requests() const noexcept
{
  RequestLines active{state.irq_requests[0], state.drq_requests[0]};
  for (unsigned slot = 1; slot <= last_expansion_slot; ++slot) {
    if (slot_enabled(slot)) {
      active.irqs |= state.irq_requests[slot];
      active.drqs |= state.drq_requests[slot];
    }
  }
  return active;
}

static void drive_drqs(CDMA& dma, u8 previous, u8 active)
{
  const u8 changed = previous ^ active;
  for (int channel = 0; channel < 8; ++channel) {
    const u8 bit = (u8)(1U << channel);
    if (changed & bit)
      dma.set_drq(channel, (active & bit) != 0);
  }
}

void EISABus::bind_dma(CDMA& controller)
{
  if (dma == &controller)
    return;
  const u8 active = active_requests().drqs;
  if (dma)
    drive_drqs(*dma, active, 0);
  dma = &controller;
  drive_drqs(controller, 0, active);
}

void EISABus::update_request_lines(RequestLines previous)
{
  const RequestLines active = active_requests();
  const u16 changed = previous.irqs ^ active.irqs;
  for (int irq = 0; irq < 16; ++irq) {
    const u16 bit = (u16)(1U << irq);
    if (changed & bit)
      host.set_isa_irq(irq, (active.irqs & bit) != 0);
  }
  if (dma && previous.drqs != active.drqs)
    drive_drqs(*dma, previous.drqs, active.drqs);
}

template <typename Mask>
void EISABus::set_request(Mask& requests, int line, bool asserted)
{
  const RequestLines previous = active_requests();
  const Mask bit = static_cast<Mask>(1U << line);
  if (asserted)
    requests |= bit;
  else
    requests &= ~bit;
  update_request_lines(previous);
}

void EISABus::set_slot_irq(unsigned slot, int irq, bool asserted)
{
  if (irq < 3 || irq > 15 || irq == 8 || irq == 13 || !slot_registered(slot))
    return;
  set_request(state.irq_requests[slot], irq, asserted);
}

void EISABus::set_isa_irq(int irq, bool asserted)
{
  if (irq >= 0 && irq < 16)
    set_request(state.irq_requests[0], irq, asserted);
}

static bool valid_drq_channel(int channel)
{
  return channel >= 0 && channel < 8 && channel != 4;
}

void EISABus::set_slot_drq(unsigned slot, int channel, bool asserted)
{
  if (valid_drq_channel(channel) && slot_registered(slot))
    set_request(state.drq_requests[slot], channel, asserted);
}

void EISABus::set_isa_drq(int channel, bool asserted)
{
  if (valid_drq_channel(channel))
    set_request(state.drq_requests[0], channel, asserted);
}

void EISABus::reset()
{
  const RequestLines previous = active_requests();
  state = {};
  update_request_lines(previous);
}

const EISABus::IoRange* EISABus::decode_io(u16 port, int dsize, bool write) const noexcept
{
  if (!valid_io_width(dsize))
    return nullptr;
  const unsigned bytes = dsize / 8;
  const auto* range = find_io(port, bytes);
  if (!range || dsize > range->max_dsize)
    return nullptr;
  const u32 offset = port - range->base;
  // The four-byte ID and one-byte control register are separate transfers.
  if (!range->device && offset < slot_control_offset && offset + bytes > slot_control_offset)
    return nullptr;
  if (range->device && !range->device->decodes_memory_access(range->index, offset, dsize, write))
    return nullptr;
  return range;
}

bool EISABus::read_port(u16 port, u8& data)
{
  u32 value;
  if (!read_port(port, 8, value))
    return false;
  data = (u8)value;
  return true;
}

bool EISABus::read_port(u16 port, int dsize, u16& data)
{
  u32 value;
  if (dsize > 16 || !read_port(port, dsize, value))
    return false;
  data = (u16)value;
  return true;
}

bool EISABus::read_port(u16 port, int dsize, u32& data)
{
  const auto* range = decode_io(port, dsize, false);
  if (!range)
    return false;
  const u32 offset = port - range->base;
  if (!range->device && offset == slot_control_offset) {
    data = slot_enabled(range->slot);
    return true;
  }
  const u64 value = range->device ? range->device->ReadMem(range->index, offset, dsize) :
    range->product_id >> (offset * 8);
  data = (u32)value & (0xffffffffU >> (32 - dsize));
  return true;
}

bool EISABus::write_port(u16 port, int dsize, u32 data)
{
  const auto* range = decode_io(port, dsize, true);
  if (!range)
    return false;
  const u32 offset = port - range->base;
  if (range->device) {
    range->device->WriteMem(range->index, offset, dsize, data & (0xffffffffU >> (32 - dsize)));
  } else if (offset == slot_control_offset) {
    const RequestLines previous = active_requests();
    const u16 bit = (u16)(1U << range->slot);
    if (data & 1)
      state.enabled_slots |= bit;
    else
      state.enabled_slots &= ~bit;
    update_request_lines(previous);
  }
  return true;
}
