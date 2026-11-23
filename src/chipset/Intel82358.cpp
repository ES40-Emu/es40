/* ES40 emulator -- Intel 82358 EISA Bus Controller
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
#include "Intel82358.h"
#include "Intel82357.h"
#include <algorithm>

Intel82358::Intel82358(Intel82357& isp) : isp(isp), bus(isp)
{
  bus.bind_dma(isp.dma_controller());
}

bool Intel82358::read_port(u16 port, u8& data)
{
  u32 value;
  if (!read_port(port, 8, value))
    return false;
  data = (u8)value;
  return true;
}

bool Intel82358::slot_io_cycle(u16 port, int dsize, bool write) const
{
  if (dsize != 16 && dsize != 32)
    return false;
  const u32 end = (u32)port + dsize / 8;
  if (end > 0x10000)
    return false;
  for (u32 byte_port = port; byte_port < end; ++byte_port)
    if (isp.decodes_port((u16)byte_port, write))
      return false;
  return true;
}

template <typename Cycle>
bool Intel82358::split_cycle(u32 address, int dsize, int unit, Cycle cycle)
{
  bool claimed = false;
  for (int done = 0; done < dsize; ) {
    // A cycle never crosses a unit boundary, so an odd start begins with a single byte.
    const int step = (std::min)(unit - int((address & 1) * 8 + done) % unit, dsize - done);
    claimed |= cycle(address + done / 8, step, done);
    done += step;
  }
  return claimed;
}

bool Intel82358::read_port(u16 port, int dsize, u16& data)
{
  u32 value;
  if (dsize > 16 || !read_port(port, dsize, value))
    return false;
  data = (u16)value;
  return true;
}

bool Intel82358::read_port(u16 port, int dsize, u32& data)
{
  if (dsize == 8) {
    u8 byte;
    if (isp.read_port(port, byte)) {
      data = byte;
      return true;
    }
  } else if (!slot_io_cycle(port, dsize, false)) {
    return false;
  }
  const int unit = bus.io_slave_width(port);
  if (unit >= dsize)
    return bus.read_port(port, dsize, data);
  // A slave narrower than the cycle is read one unit at a time, lowest address first (MindShare EISA System Architecture, ch. 12).
  u32 value = 0xffffffffU >> (32 - dsize); // Unclaimed units read as ones, like an unclaimed cycle.
  const bool claimed = split_cycle(port, dsize, unit, [&](u32 at, int width, int shift) {
    u32 part;
    if (!read_port((u16)at, width, part))
      return false;
    value = (value & ~((0xffffffffU >> (32 - width)) << shift)) | (part << shift);
    return true;
  });
  if (claimed)
    data = value;
  return claimed;
}

bool Intel82358::write_port(u16 port, int dsize, u32 data)
{
  if (dsize == 8) {
    if (isp.write_port(port, (u8)data))
      return true;
  } else if (!slot_io_cycle(port, dsize, true)) {
    return false;
  }
  const int unit = bus.io_slave_width(port);
  if (unit >= dsize)
    return bus.write_port(port, dsize, data);
  // The same ascending order applies to writes, each unit on the slave's own data lanes.
  return split_cycle(port, dsize, unit, [&](u32 at, int width, int shift) {
    return write_port((u16)at, width, data >> shift);
  });
}

bool Intel82358::read_mem(u32 address, int dsize, u32& data)
{
  const int unit = bus.mem_slave_width(address);
  if (unit >= dsize)
    return bus.read_mem(address, dsize, data);
  u32 value = 0xffffffffU >> (32 - dsize);
  const bool claimed = split_cycle(address, dsize, unit, [&](u32 at, int width, int shift) {
    u32 part;
    if (!read_mem(at, width, part))
      return false;
    value = (value & ~((0xffffffffU >> (32 - width)) << shift)) | (part << shift);
    return true;
  });
  if (claimed)
    data = value;
  return claimed;
}

bool Intel82358::write_mem(u32 address, int dsize, u32 data)
{
  const int unit = bus.mem_slave_width(address);
  if (unit >= dsize)
    return bus.write_mem(address, dsize, data);
  return split_cycle(address, dsize, unit, [&](u32 at, int width, int shift) {
    return write_mem(at, width, data >> shift);
  });
}
