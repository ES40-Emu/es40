/*
 * QEMU AMD PC-Net II (Am79C970A) emulation
 *
 * Copyright (c) 2004 Antony T Curtis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// SPDX-License-Identifier: MIT
// Adapted from Antony T Curtis's QEMU hw/net/pcnet.c and the AMD LANCE Technical Manual.

#include "StdAfx.h"
#include "Am7990.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

constexpr u16 Init = 0x0001, Start = 0x0002, Stop = 0x0004, Demand = 0x0008;
constexpr u16 TxOn = 0x0010, RxOn = 0x0020, InterruptEnable = 0x0040, Interrupt = 0x0080;
constexpr u16 InitDone = 0x0100, TxInterrupt = 0x0200, RxInterrupt = 0x0400;
constexpr u16 MemoryError = 0x0800, Miss = 0x1000, CarrierError = 0x2000, Babble = 0x4000, Error = 0x8000;
constexpr u16 Causes = InitDone | TxInterrupt | RxInterrupt | MemoryError | Miss | Babble;
constexpr u16 Errors = MemoryError | Miss | CarrierError | Babble;
constexpr u16 DisableRx = 1, DisableTx = 2, Loop = 4, DisableTxCrc = 8, ForceCollision = 16;
constexpr u16 InternalLoop = 0x0040, Promiscuous = 0x8000;
constexpr u16 Owned = 0x8000, DescriptorError = 0x4000, First = 0x0200, Last = 0x0100;
constexpr u16 RxFraming = 0x2000, RxOverflow = 0x1000, RxCrc = 0x0800, RxBuffer = 0x0400;
constexpr u16 TxBuffer = 0x8000, TxUnderflow = 0x4000, LostCarrier = 0x0800, Retry = 0x0400;
constexpr u32 AddressMask = 0xffffff;
constexpr std::size_t MaxFrame = 1518; // Includes the four wire CRC bytes (AMD LANCE Technical Manual, sec. 1.8.3).

static u32 crc_shift_register(const u8* data, std::size_t length)
{
  u32 crc = 0xffffffffU;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
  }
  return crc;
}

static void append_fcs(u8* frame, std::size_t length)
{
  const u32 fcs = ~crc_shift_register(frame, length);
  for (unsigned i = 0; i < 4; ++i)
    frame[length + i] = u8(fcs >> (8 * i));
}

Am7990::Am7990(ReadWord read_word, WriteWord write_word, Irq irq, Transmit transmit)
  : read_word(std::move(read_word)), write_word(std::move(write_word)), irq(std::move(irq)), send(std::move(transmit))
{
  state.csr[0] = Stop;
}

void Am7990::reset()
{
  state = {};
  state.csr[0] = Stop;
  sync_irq();
}

u16 Am7990::read_rdp() const
{
  if (state.rap != 0)
    return (state.csr[0] & Stop) ? state.csr[state.rap & 3] : 0;
  u16 status = state.csr[0];
  if (status & Causes) status |= Interrupt;
  if (status & Errors) status |= Error;
  return status;
}

void Am7990::sync_irq()
{
  irq((state.csr[0] & InterruptEnable) && (state.csr[0] & Causes));
}

void Am7990::memory_error()
{
  state.csr[0] = (state.csr[0] & ~(RxOn | TxOn)) | MemoryError;
  sync_irq();
}

bool Am7990::load_word(u32 address, u16& value)
{
  if (read_word(address & AddressMask, value)) return true;
  memory_error();
  return false;
}

bool Am7990::store_word(u32 address, u16 value)
{
  if (write_word(address & AddressMask, value)) return true;
  memory_error();
  return false;
}

bool Am7990::read_bytes(u32 address, u8* buffer, std::size_t length)
{
  const unsigned swap = (state.csr[3] & 4) ? 8 : 0; // BSWP exchanges the FIFO data lanes only (AMD LANCE TM, sec. 1.6).
  while (length) {
    const u32 aligned = address & ~1U;
    u16 word;
    if (!load_word(aligned, word)) return false;
    do {
      *buffer++ = u8(word >> (((address & 1) * 8) ^ swap));
      ++address;
      --length;
    } while (length && (address & 1));
  }
  return true;
}

bool Am7990::write_bytes(u32 address, const u8* buffer, std::size_t length)
{
  const unsigned swap = (state.csr[3] & 4) ? 8 : 0;
  while (length) {
    const u32 aligned = address & ~1U;
    u16 word = 0;
    if (((address & 1) || length == 1) && !load_word(aligned, word)) return false;
    do {
      const unsigned shift = ((address & 1) * 8) ^ swap;
      word = u16((word & ~(0xffU << shift)) | (u16(*buffer++) << shift));
      ++address;
      --length;
    } while (length && (address & 1));
    if (!store_word(aligned, word)) return false;
  }
  return true;
}

void Am7990::write_rdp(u16 value)
{
  u16& status = state.csr[0];
  if (state.rap != 0) {
    static constexpr u16 masks[] = {0, 0xffff, 0x00ff, 0x0007};
    if (status & Stop)
      state.csr[state.rap & 3] = value & masks[state.rap & 3];
    return;
  }

  if (value & Stop) {
    status = Stop;
    state.csr[3] = 0; // STOP clears CSR3 and preserves the initialization block for STRT (AMD LANCE TM, sec. 1.2).
    sync_irq();
    return;
  }
  status &= ~(value & (Causes | CarrierError)); // CSR0 events are write-one-to-clear (AMD LANCE TM, sec. 1.9.2).
  if ((value & Init) && (status & Stop)) {
    status = Init;
    if (initialize()) status |= InitDone;
  }
  if ((value & Start) && !(status & (Start | MemoryError))) {
    status = (status & ~Stop) | Start;
    if (!(state.mode & DisableRx)) status |= RxOn;
    if (!(state.mode & DisableTx)) status |= TxOn;
  }
  status = u16((status & ~InterruptEnable) | ((status & Stop) ? 0 : (value & InterruptEnable)));
  if (value & (Demand | Start)) poll();
  sync_irq();
}

bool Am7990::initialize()
{
  // CSR1 reads back the written word; IADR uses bits 15:1 for word DMA (AMD LANCE TM, sec. 1.9.2).
  const u32 address = (u32(state.csr[2]) << 16) | (state.csr[1] & 0xfffeU);
  u16 words[12]{};
  for (unsigned i = 0; i < 12; ++i)
    if (!load_word(address + 2 * i, words[i])) return false;

  state.mode = words[0]; // 24-byte initialization block (AMD LANCE TM, sec. 1.9.3 and QEMU pcnet_init).
  for (unsigned i = 0; i < 3; ++i) {
    state.mac[2 * i] = u8(words[1 + i]);
    state.mac[2 * i + 1] = u8(words[1 + i] >> 8);
  }
  std::copy_n(words + 4, 4, state.filter);
  auto ring = [](u16 low, u16 high) -> Ring {
    return {(u32(high & 0xff) << 16) | (low & 0xfff8U), u16(1U << (high >> 13)), 0};
  };
  state.rx = ring(words[8], words[9]);
  state.tx = ring(words[10], words[11]);
  state.loop_carrier_lost = false;
  return true;
}

bool Am7990::get_descriptor(const Ring& ring, Descriptor& descriptor)
{
  const u32 address = ring.address + u32(ring.current) * 8;
  u16 low, high, count;
  if (!load_word(address + 2, high)) return false;
  descriptor.status = high & 0xff00;
  if (!(high & Owned)) return true;
  if (!load_word(address, low) || !load_word(address + 4, count)) return false;
  descriptor.address = (u32(high & 0xff) << 16) | low;
  descriptor.capacity = u16(4096U - (count & 0xfff)); // Negative 12-bit buffer count (AMD LANCE TM, sec. 1.9.4).
  return true;
}

bool Am7990::release_descriptor(Ring& ring, const Descriptor& descriptor, u16 flags, u16 result)
{
  const u32 address = ring.address + u32(ring.current) * 8;
  if (!store_word(address + 6, result) ||
      !store_word(address + 2, u16((descriptor.address >> 16) | flags))) return false;
  ring.current = u16((ring.current + 1) & (ring.count - 1));
  return true;
}

bool Am7990::accepts(const u8* frame) const
{
  static constexpr u8 broadcast[] = {255,255,255,255,255,255};
  if ((state.mode & Promiscuous) || !std::memcmp(frame, state.mac, 6) || !std::memcmp(frame, broadcast, 6))
    return true;
  if (!(frame[0] & 1)) return false;
  const unsigned hash = crc_shift_register(frame, 6) >> 26;
  return (state.filter[hash >> 4] & (1U << (hash & 15))) != 0;
}

void Am7990::receive(const u8* frame, std::size_t length)
{
  if (!(state.csr[0] & RxOn) || ((state.mode & (Loop | InternalLoop)) == (Loop | InternalLoop))) return;
  if (length < 6 || length > MaxFrame - 4 || !accepts(frame)) return;
  std::array<u8, MaxFrame> wire{};
  std::copy_n(frame, length, wire.data());
  length = std::max<std::size_t>(length, 60); // Host backends may omit Ethernet padding and FCS.
  append_fcs(wire.data(), length);
  receive_wire(wire.data(), length + 4, 0);
}

void Am7990::receive_wire(const u8* frame, std::size_t length, u16 errors)
{
  if (!(state.csr[0] & RxOn) || !state.rx.count || length < 6 || !accepts(frame)) return;
  std::size_t offset = 0;
  Descriptor descriptor{};
  if (!get_descriptor(state.rx, descriptor)) return;
  if (!(descriptor.status & Owned)) {
    state.csr[0] |= Miss;
    sync_irq();
    return;
  }
  while (offset < length) {
    const std::size_t count = std::min<std::size_t>(descriptor.capacity, length - offset);
    u16 flags = offset == 0 ? First : 0;
    if (!write_bytes(descriptor.address, frame + offset, count)) return;
    offset += count;
    if (offset == length) {
      flags |= Last;
      if (errors) flags |= DescriptorError | errors;
      if (!release_descriptor(state.rx, descriptor, flags, u16(length))) return;
      break;
    }
    Ring next = state.rx;
    next.current = (next.current + 1) & (next.count - 1);
    Descriptor following{};
    if (!get_descriptor(next, following)) return;
    if (next.current == state.rx.current || !(following.status & Owned)) {
      if (!release_descriptor(state.rx, descriptor, flags | DescriptorError | RxBuffer | RxOverflow, 0)) return;
      break;
    }
    if (!release_descriptor(state.rx, descriptor, flags, 0)) return;
    descriptor = following;
  }
  state.csr[0] |= RxInterrupt;
  sync_irq();
}

void Am7990::poll()
{
  // The LANCE continues polling successive owned transmit entries (AMD LANCE TM, sec. 4.5.2).
  for (unsigned i = 0; i < state.tx.count && (state.csr[0] & TxOn); ++i) {
    Descriptor descriptor{};
    if (!get_descriptor(state.tx, descriptor) || !(descriptor.status & Owned)) break;
    transmit_frame(descriptor);
  }
}

void Am7990::transmit_frame(Descriptor descriptor)
{
  if (!(descriptor.status & First)) {
    if (!release_descriptor(state.tx, descriptor, 0, 0)) return;
    state.csr[0] |= TxInterrupt;
    sync_irq();
    return;
  }

  std::array<u8, MaxFrame + 4> frame{};
  std::size_t length = 0;
  u16 result = state.loop_carrier_lost ? LostCarrier : 0;
  for (;;) {
    const auto count = descriptor.capacity;
    if (!result && length + count <= MaxFrame && !read_bytes(descriptor.address, frame.data() + length, count)) return;
    length += count;
    if (descriptor.status & Last) break;

    Ring next = state.tx;
    next.current = (next.current + 1) & (next.count - 1);
    Descriptor following{};
    if (!get_descriptor(next, following)) return;
    if (next.current == state.tx.current || !(following.status & Owned)) {
      result = TxBuffer | TxUnderflow;
      state.csr[0] &= ~TxOn;
      break;
    }
    if (!release_descriptor(state.tx, descriptor, descriptor.status & First, 0)) return;
    descriptor = following;
  }

  const bool loop = (state.mode & Loop) != 0;
  const bool internal = loop && (state.mode & InternalLoop);
  const bool generate_crc = !(state.mode & DisableTxCrc);
  if (!result && internal && (state.mode & ForceCollision)) {
    result = Retry; // Sixteen forced collisions exhaust the retry count (AMD LANCE TM, sec. 4.3.4).
  } else if (!result && length + (generate_crc ? 4 : 0) > MaxFrame) {
    state.csr[0] |= Babble;
  } else if (!result && length >= (generate_crc ? 6U : 10U)) {
    const std::size_t data_length = generate_crc ? length : length - 4;
    if (generate_crc) append_fcs(frame.data(), data_length);
    const bool bad_crc = !generate_crc && crc_shift_register(frame.data(), data_length + 4) != 0xdebb20e3U;
    if (internal) {
      if (!accepts(frame.data())) {
        state.loop_carrier_lost = true; // Later loopback transmits report LCAR until INIT (PMAD-AA, sec. 6.1.7.11, note 4).
      } else {
        // Internal loopback also sets FRAM on a bad CRC (PMAD-AA, sec. 6.1.7.11, note 8).
        receive_wire(frame.data(), data_length + 4, bad_crc ? RxFraming | RxCrc : 0);
      }
    } else if (!bad_crc) {
      send(frame.data(), data_length);
    }
  }

  u16 flags = descriptor.status & (First | Last);
  if (result) flags |= DescriptorError;
  if (!release_descriptor(state.tx, descriptor, flags, result)) return;
  state.csr[0] |= TxInterrupt;
  sync_irq();
}
