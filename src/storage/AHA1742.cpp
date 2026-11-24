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

// Control and status block fields are stored least significant byte first (AHA-1740A TRM, p. 6-10).
static u16 get_u16le(const u8* p) { return u16(p[0] | (p[1] << 8)); }
static u32 get_u32le(const u8* p) { return u32(get_u16le(p)) | (u32(get_u16le(p + 2)) << 16); }
static void put_u16le(u8* p, u16 value) { p[0] = u8(value); p[1] = u8(value >> 8); }
static void put_u32le(u8* p, u32 value) { put_u16le(p, u16(value)); put_u16le(p + 2, u16(value >> 16)); }

// What Read Host Adapter Inquiry Data returns; a real board's name and release fields are not known (AHA-1740A TRM, pp. 6-31 to 6-33).
static const u8 inquiry_data[] = {
  0x23, 0x00, // Processor device; Target Mode is supported and a reset leaves it disabled (AHA-1740A TRM, pp. 5-10, 6-32).
  0x02, 0x00, // SCSI-2, with no asynchronous event notification accepted.
  0x33, 0x00, // Bytes that follow, then no LUNs enabled for Target Mode.
  0x40, 0x18, // 64 control blocks (AHA-1740A TRM, p. 6-2); linked commands and synchronous transfers, single-ended, 8-bit.
  'A', 'd', 'a', 'p', 't', 'e', 'c', ' ', // Vendor.
  'A', 'H', 'A', '-', '1', '7', '4', '2', // Product.
  'E', 'n', 'h', 'a', 'n', 'c', 'e', 'd', // Firmware type.
  'A', ' ', ' ', ' ', // Firmware revision level, which starts at "A".
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', // Release date.
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', // Release time.
  0x00, 0x00, 0x00, 0x00, // Firmware checksum, then a reserved word.
};

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
    if (!in_reset && attn >> 4 == StartCCB) {
      g2stat |= MailboxOutEmpty;
      start_ccb(get_u32le(mboxout));
    }
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

void AHA1742::start_ccb(u32 address)
{
  u8 ecb[48];
  // Only Read Host Adapter Inquiry Data is carried out; the option flags in bits 14-12 are not part of the operation code (AHA-1740A TRM, p. 6-4).
  if (!bus.slot_master_read(slot, address, ecb, sizeof(ecb)) || (get_u16le(ecb + CommandWord) & 0x0fff) != ReadInquiryData)
    return;
  const u16 flags = get_u16le(ecb + FlagWord1);
  const u32 pointer = get_u32le(ecb + DataPointer), length = get_u32le(ecb + DataLength);
  // The data is cut to the length the host allows, and a longer buffer is left short (AHA-1740A TRM, pp. 6-30, 6-32).
  const u32 count = length < sizeof(inquiry_data) ? length : u32(sizeof(inquiry_data));
  bus.slot_master_write(slot, pointer, inquiry_data, count);
  u8 status[32]{};
  u16 word = CommandDone;
  if (count < length) {
    // An underrun reports the residual count and buffer address, and is an error unless the host suppressed it (AHA-1740A TRM, pp. 6-14, 6-18).
    const bool error = !(flags & SuppressUnderrunError);
    word = u16(error ? DataUnderrun | MajorError : CommandDone | DataUnderrun);
    status[HostAdapterStatus] = error ? UnderrunOccurred : 0;
    put_u32le(status + ResidualCount, length - count);
    put_u32le(status + ResidualAddress, pointer);
  }
  put_u16le(status + StatusWord, word);
  // The status block is left out only when the command is done without error and the host disabled it (AHA-1740A TRM, p. 6-11).
  if (!(word & CommandDone) || !(flags & DisableStatusBlock))
    bus.slot_master_write(slot, get_u32le(ecb + StatusBlockPointer), status, sizeof(status));
}
