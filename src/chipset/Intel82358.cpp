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
  return bus.read_port(port, dsize, data);
}

bool Intel82358::write_port(u16 port, int dsize, u32 data)
{
  if (dsize == 8) {
    if (isp.write_port(port, (u8)data))
      return true;
  } else if (!slot_io_cycle(port, dsize, true)) {
    return false;
  }
  return bus.write_port(port, dsize, data);
}
