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
  u16 value;
  if (!read_port(port, 8, value))
    return false;
  data = (u8)value;
  return true;
}

bool Intel82358::slot_word_port(u16 port, bool write) const
{
  return port != 0xffff && !isp.decodes_port(port, write) && !isp.decodes_port(port + 1, write);
}

bool Intel82358::read_port(u16 port, int dsize, u16& data)
{
  if (dsize == 8) {
    u8 byte;
    if (isp.read_port(port, byte)) {
      data = byte;
      return true;
    }
  } else if (dsize != 16 || !slot_word_port(port, false)) {
    return false;
  }
  return bus.read_port(port, dsize, data);
}

bool Intel82358::write_port(u16 port, int dsize, u16 data)
{
  if (dsize == 8) {
    if (isp.write_port(port, (u8)data))
      return true;
  } else if (dsize != 16 || !slot_word_port(port, true)) {
    return false;
  }
  return bus.write_port(port, dsize, data);
}
