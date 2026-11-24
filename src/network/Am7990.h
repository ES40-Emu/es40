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
// QEMU hw/net/pcnet.c supplies the legacy LANCE initialization and descriptor formats.

#ifndef ES40_AM7990_H
#define ES40_AM7990_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include "datatypes.h"

class Am7990
{
public:
  using ReadWord = std::function<bool(u32, u16&)>;
  using WriteWord = std::function<bool(u32, u16)>;
  using Irq = std::function<void(bool)>;
  using Transmit = std::function<void(const u8*, std::size_t)>;

  Am7990(ReadWord read_word, WriteWord write_word, Irq irq, Transmit transmit);

  void reset();
  u16 read_rap() const { return state.rap; }
  void write_rap(u16 value) { state.rap = value & 3; }
  u16 read_rdp() const;
  void write_rdp(u16 value);
  void sync_irq();
  void poll();
  void receive(const u8* frame, std::size_t length);

  struct Ring {
    u32 address;
    u16 count;
    u16 current;
  };
  struct State {
    u16 rap;
    u16 csr[4];
    u16 mode;
    u8 mac[6];
    u16 filter[4];
    Ring rx, tx;
    bool loop_carrier_lost;
  } state{};

private:
  struct Descriptor {
    u32 address;
    u16 status;
    u16 capacity;
  };

  bool load_word(u32 address, u16& value);
  bool store_word(u32 address, u16 value);
  bool read_bytes(u32 address, u8* buffer, std::size_t length);
  bool write_bytes(u32 address, const u8* buffer, std::size_t length);
  bool get_descriptor(const Ring& ring, Descriptor& descriptor);
  bool release_descriptor(Ring& ring, const Descriptor& descriptor, u16 flags, u16 result);
  bool initialize();
  bool accepts(const u8* frame) const;
  void receive_wire(const u8* frame, std::size_t length, u16 errors);
  void transmit_frame(Descriptor descriptor);
  void memory_error();

  ReadWord read_word;
  WriteWord write_word;
  Irq irq;
  Transmit send;
};

#endif
