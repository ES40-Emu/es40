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

void EISABus::register_isa_io(CSystemComponent& device, int index, u16 port, u32 length)
{
  if (!length || length > 0x10000U - port)
    FAILURE(Configuration, "Invalid EISA I/O port range");
  for (const auto& range : io_ranges)
    if (port < range.base + range.length && range.base < port + length)
      FAILURE(Configuration, "Overlapping EISA I/O port ranges");
  io_ranges.push_back({&device, index, port, length});
}

const EISABus::IoRange* EISABus::find_io(u32 port) const noexcept
{
  for (const auto& range : io_ranges)
    if (port >= range.base && port - range.base < range.length)
      return &range;
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

void EISABus::set_isa_irq(int irq, bool asserted)
{
  host.set_isa_irq(irq, asserted);
}

bool EISABus::read_port(u16 port, u8& data)
{
  const auto* range = find_io(port);
  if (!range || !range->device->decodes_memory_access(range->index, port - range->base, 8, false))
    return false;
  data = (u8)range->device->ReadMem(range->index, port - range->base, 8);
  return true;
}

bool EISABus::write_port(u16 port, u8 data)
{
  const auto* range = find_io(port);
  if (!range || !range->device->decodes_memory_access(range->index, port - range->base, 8, true))
    return false;
  range->device->WriteMem(range->index, port - range->base, 8, data);
  return true;
}
