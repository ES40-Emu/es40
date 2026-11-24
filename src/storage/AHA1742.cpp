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
  : CSystemComponent(cfg, system), bus(bus), slot(slot)
{
  // HID0-HID3 spell "ADP", product 00h, revision 02h for the AHA-1742A (AHA-1740A TRM, pp. 4-11, 4-12).
  bus.register_slot_id(slot, {0x04, 0x90, 0x00, 0x02});
  // The configuration registers are byte-wide and accessible at any time (AHA-1740A TRM, p. 4-12).
  bus.register_slot_io(slot, *this, Configuration, u16((slot << 12) | 0xcc0), sizeof(config), 8);
  // The Enhanced Mode (Group 2) registers occupy zCD0-zCDC (AHA-1740A TRM, pp. 4-21 to 4-23).
  bus.register_slot_io(slot, *this, Group2, u16((slot << 12) | 0xcd0), 13, 8);
  save_item(config, "config");
  save_item(mboxout, "mboxout");
  save_item(attn, "attn");
  save_item(g2stat, "g2stat");
  save_item(mboxin, "mboxin");
  save_item(in_reset, "in_reset");
}

bool AHA1742::decodes_memory_access(int index, u64, int, bool) const noexcept
{
  // With CDEN clear only the ID and configuration registers can be accessed (AHA-1740A TRM, p. 4-12).
  return index == Configuration || bus.slot_enabled(slot);
}

u64 AHA1742::ReadMem(int index, u64 address, int dsize)
{
  if (dsize != 8)
    return 0xff;
  if (index == Configuration)
    return config[address];
  if (address < MBOXOUT0 + sizeof(mboxout))
    return mboxout[address - MBOXOUT0];
  if (address == ATTN)
    return attn;
  if (address == G2STAT)
    return g2stat;
  if (address >= MBOXIN0 && address < MBOXIN0 + sizeof(mboxin))
    return mboxin[address - MBOXIN0];
  return 0xff;
}

void AHA1742::WriteMem(int index, u64 address, int dsize, u64 data)
{
  if (dsize != 8)
    return;
  // Each configuration register is write/read, so a write is kept whole and read back as written.
  if (index == Configuration) {
    config[address] = u8(data);
    return;
  }
  if (address < MBOXOUT0 + sizeof(mboxout)) {
    // Writing any mailbox out byte clears Mailbox Out Empty (AHA-1740A TRM, p. 4-23).
    mboxout[address - MBOXOUT0] = u8(data);
    g2stat &= ~MailboxOutEmpty;
    return;
  }
  if (address == ATTN) {
    attn = u8(data);
    // The local processor reads ATTN at once, so the Busy bit this write sets is never seen set (AHA-1740A TRM, p. 4-23).
    // For Start CCB it then reads the mailbox out address and sets Mailbox Out Empty (AHA-1740A TRM, p. 4-20).
    if (!in_reset && attn >> 4 == StartCCB)
      g2stat |= MailboxOutEmpty;
    return;
  }
  if (address != G2CNTRL)
    return;
  // Hard Reset holds the adapter in reset, with Busy set, until the bit is cleared (AHA-1740A TRM, p. 4-22).
  if (data & HardReset) {
    in_reset = true;
    g2stat |= Busy;
  } else if (in_reset) {
    // Released with diagnostics passing: 00 is loaded in MBOXIN0, Busy clears and the mailbox out is empty (AHA-1740A TRM, pp. 4-20, 4-21).
    in_reset = false;
    mboxin[0] = 0;
    g2stat = MailboxOutEmpty;
  }
}
