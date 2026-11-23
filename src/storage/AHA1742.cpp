/* ES40 emulator -- Adaptec AHA-1742A EISA-to-Fast SCSI host adapter
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
#include "AHA1742.h"
#include "chipset/EISABus.h"

AHA1742::AHA1742(CConfigurator* cfg, CSystem* system, EISABus& bus, unsigned slot)
  : CSystemComponent(cfg, system)
{
  // HID0-HID3 spell "ADP", product 00h, revision 02h for the AHA-1742A (AHA-1740A TRM, pp. 4-11, 4-12).
  bus.register_slot_id(slot, {0x04, 0x90, 0x00, 0x02});
  // The configuration registers are byte-wide and accessible at any time (AHA-1740A TRM, p. 4-12).
  bus.register_slot_io(slot, *this, Configuration, u16((slot << 12) | 0xcc0), sizeof(config), 8);
  save_item(config, "config");
}

u64 AHA1742::ReadMem(int index, u64 address, int dsize)
{
  if (index == Configuration && dsize == 8)
    return config[address];
  return 0xff;
}

void AHA1742::WriteMem(int index, u64 address, int dsize, u64 data)
{
  // Each register is write/read, so a write is kept whole and read back as written.
  if (index == Configuration && dsize == 8)
    config[address] = u8(data);
}
