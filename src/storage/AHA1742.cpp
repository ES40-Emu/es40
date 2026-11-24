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
#include "Disk.h"
#include "Intel82077A.h"
#include "SCSIBus.h"

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

AHA1742::AHA1742(CConfigurator* cfg, CSystem* system, EISABus& bus, unsigned slot,
  CConfigurator* floppy_cfg, bool secondary_floppy)
  : CSystemComponent(cfg, system), CDiskController(1, 8), bus(bus), slot(slot)
{
  // The adapter is the initiator on its SCSI bus and takes no ID as a target, since Target Mode stays disabled (AHA-1740A TRM, p. 5-10).
  scsi_bus[0] = new CSCSIBus(cfg, system);
  // HID0-HID3 spell ADP0002 with J6 floppy enable fitted, ADP0001 without (Adaptec TRM, pp. 3-15, 4-11).
  bus.register_slot_id(slot, {0x04, 0x90, 0x00, u8(floppy_cfg ? 0x02 : 0x01)});
  if (floppy_cfg)
    floppy = new Intel82077A(floppy_cfg, system, bus, slot, secondary_floppy);
  // The configuration registers are byte-wide and accessible at any time (AHA-1740A TRM, p. 4-12).
  bus.register_slot_io(slot, *this, Configuration, u16((slot << 12) | 0xcc0), sizeof(config), 8);
  // The Enhanced Mode (Group 2) registers occupy zCD0-zCDC (AHA-1740A TRM, pp. 4-21 to 4-23).
  bus.register_slot_io(slot, *this, Group2, u16((slot << 12) | 0xcd0), 13, 8);
  save_item(config, "config");
  save_item(mboxout, "mboxout");
  save_item(attn, "attn");
  save_item(g2stat, "g2stat");
  save_item(g2intst, "g2intst");
  save_item(mboxin, "mboxin");
  save_item(completions, "completions");
  save_item(completion_head, "completion_head");
  save_item(completion_count, "completion_count");
  save_item(in_reset, "in_reset");
}

void AHA1742::register_disk(CDisk* dsk, int bus, int dev)
{
  // The SCSI data bus is 8 bits wide, which gives eight IDs (AHA-1740A TRM, p. 6-33).
  if (bus != 0 || dev < 0 || dev > 7)
    FAILURE(Configuration, "AHA-1742A disks are named disk0.0 to disk0.7");
  CDiskController::register_disk(dsk, bus, dev);
  dsk->scsi_register(0, scsi_bus[0], dev);
  dsk->scsi_reset();
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
  if (address == G2INTST)
    return g2intst;
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
    // A new INTSEL moves the interrupt to another line, so the old line is let go first (AHA-1740A TRM, p. 4-15).
    if (address == INTDEF && ((config[INTDEF] ^ data) & INTSEL))
      drive_irq(false);
    config[address] = u8(data);
    if (address == INTDEF)
      drive_irq((g2stat & InterruptPending) != 0);
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
      start_ccb(get_le32(mboxout));
    }
    return;
  }
  if (address != G2CNTRL)
    return;
  // Clear EISA Interrupt drops a pending interrupt, and a Hard Reset returns the registers to their reset condition (AHA-1740A TRM, pp. 4-8, 4-22).
  if (data & (ClearInterrupt | HardReset)) {
    g2stat &= ~InterruptPending;
    drive_irq(false);
  }
  // Hard Reset holds the adapter in reset, with Busy set, until the bit is cleared (AHA-1740A TRM, p. 4-22).
  if (data & HardReset) {
    in_reset = true;
    g2stat |= Busy;
    // The local processor loses what it held, waiting completions included (AHA-1740A TRM, p. 4-8).
    completion_count = 0;
  } else if (in_reset) {
    // Released with diagnostics passing: 00 is loaded in MBOXIN0, Busy clears and the mailbox out is empty (AHA-1740A TRM, pp. 4-20, 4-21).
    in_reset = false;
    mboxin[0] = 0;
    g2stat = MailboxOutEmpty;
  }
  // Clearing the interrupt tells the local processor the mailbox in is free for the next completion (AHA-1740A TRM, p. 4-20).
  post_completion();
}

void AHA1742::start_ccb(u32 address)
{
  // Control and status block fields are stored least significant byte first (AHA-1740A TRM, p. 6-10).
  u8 ecb[48];
  if (!bus.slot_master_read(slot, address, ecb, sizeof(ecb)))
    return;
  // The option flags in bits 14-12 are not part of the operation code (AHA-1740A TRM, p. 6-4).
  const u16 operation = get_le16(ecb + CommandWord) & 0x0fff, flags = get_le16(ecb + FlagWord1);
  // Other operations are not carried out yet, nor is an Initiator SCSI Command whose data is a scatter/gather list.
  if (operation != ReadInquiryData && (operation != InitiatorCommand || (flags & ScatterGather)))
    return;
  u8 status[32]{};
  // With every completion slot taken the control block is refused with Queue Full and no action is taken on it (AHA-1740A TRM, p. 6-14).
  if (completion_count == QueueSize) {
    put_le16(status + StatusWord, QueueFull);
    bus.slot_master_write(slot, get_le32(ecb + StatusBlockPointer), status, sizeof(status));
    return;
  }
  if (operation == InitiatorCommand)
    initiator_command(ecb, status);
  else
    read_inquiry_data(ecb, status);
  // The host is interrupted unless the command is done without error and it set Disable Interrupt (AHA-1740A TRM, pp. 6-5, 6-15).
  const u16 word = get_le16(status + StatusWord);
  const bool interrupt = !(word & CommandDone) || !(flags & DisableInterrupt);
  put_le16(status + StatusWord, u16(word | (interrupt ? InterruptIssued : 0)));
  // The status block is left out only when the command is done without error and the host disabled it (AHA-1740A TRM, p. 6-11).
  if (!(word & CommandDone) || !(flags & DisableStatusBlock))
    bus.slot_master_write(slot, get_le32(ecb + StatusBlockPointer), status, sizeof(status));
  if (!interrupt)
    return;
  // The completion waits its turn with its status code and target, behind any the host has not yet taken (AHA-1740A TRM, pp. 4-20, 4-22).
  const u8 code = u8(((word & CommandDone) ? CompletedGood : CompletedError) << 4 | (attn & 0x0f));
  completions[(completion_head + completion_count++) % QueueSize] = u64(code) << 32 | address;
  post_completion();
}

// Read Host Adapter Inquiry Data: the data is cut to the length the host allows, and a longer buffer is left short (AHA-1740A TRM, pp. 6-30, 6-32).
void AHA1742::read_inquiry_data(const u8* ecb, u8* status)
{
  const u32 length = get_le32(ecb + DataLength);
  const u32 count = length < sizeof(inquiry_data) ? length : u32(sizeof(inquiry_data));
  bus.slot_master_write(slot, get_le32(ecb + DataPointer), inquiry_data, count);
  put_le16(status + StatusWord, CommandDone);
  account_transfer(ecb, status, count, false);
}

// Initiator SCSI Command: the adapter selects the target and follows it through the bus phases of one command (AHA-1740A TRM, p. 6-26).
void AHA1742::initiator_command(const u8* ecb, u8* status)
{
  const u8 target = attn & 0x0f, cdb_length = ecb[CDBLength];
  const u16 flags2 = get_le16(ecb + FlagWord2);
  const u32 pointer = get_le32(ecb + DataPointer), length = get_le32(ecb + DataLength);
  // A CDB is 6, 10 or 12 bytes long; another length is an invalid parameter, and no action is taken on the control block (AHA-1740A TRM, pp. 6-12, 6-15, 6-17).
  if (cdb_length != 6 && cdb_length != 10 && cdb_length != 12) {
    put_le16(status + StatusWord, SpecificationCheck);
    status[HostAdapterStatus] = InvalidParameter;
    return;
  }
  // The adapter is the only initiator on its bus, so it wins arbitration with the ID in SCSIDEF bits 3-0 (AHA-1740A TRM, p. 4-16).
  scsi_initiator_id[0] = config[SCSIDEF] & HSCSIID;
  scsi_arbitrate(0);
  // With no target at the ID in ATTN bits 3-0 the selection times out, which is a major error (AHA-1740A TRM, pp. 6-16, 6-17, 6-26).
  if (!scsi_select(0, target)) {
    scsi_free(0);
    put_le16(status + StatusWord, MajorError);
    status[HostAdapterStatus] = SelectionTimeout;
    return;
  }
  u32 moved = 0;
  bool overrun = false;
  // The target chooses each bus phase and the adapter answers it, until the target frees the bus.
  for (int phase; (phase = scsi_get_phase(0)) != SCSI_PHASE_FREE; scsi_xfer_done(0)) {
    const size_t bytes = scsi_expected_xfer(0);
    const u32 room = length - moved, count = bytes < room ? u32(bytes) : room;
    if (phase == SCSI_PHASE_MSG_OUT) {
      // The identify message names the LUN and allows disconnection, the default, unless No Disconnect is set (AHA-1740A TRM, pp. 4-29, 6-7).
      *static_cast<u8*>(scsi_xfer_ptr(0, 1)) = u8(0x80 | ((flags2 & NoDisconnect) ? 0 : 0x40) | (flags2 & LogicalUnit));
    } else if (phase == SCSI_PHASE_COMMAND) {
      memcpy(scsi_xfer_ptr(0, cdb_length), ecb + CDBBytes, cdb_length);
    } else if (phase == SCSI_PHASE_DATA_OUT) {
      // A target that asks for more than the data length is sent none of it, and the command ends there as a data overrun (AHA-1740A TRM, pp. 6-9, 6-15).
      if (bytes > room) {
        overrun = true;
        scsi_bus[0]->free_bus(target);
        break;
      }
      bus.slot_master_read(slot, pointer + moved, scsi_xfer_ptr(0, bytes), bytes);
      moved += count;
    } else if (phase == SCSI_PHASE_DATA_IN) {
      // Data past the data length is taken from the target without being stored, which is also a data overrun (AHA-1740A TRM, p. 6-15).
      bus.slot_master_write(slot, pointer + moved, scsi_xfer_ptr(0, bytes), count);
      overrun = overrun || bytes > room;
      moved += count;
    } else {
      // The status phase gives the target status; the Command Complete message that follows needs no answer (AHA-1740A TRM, p. 6-17).
      const u8* data = static_cast<u8*>(scsi_xfer_ptr(0, bytes));
      if (phase == SCSI_PHASE_STATUS && bytes)
        status[TargetStatus] = data[0];
    }
  }
  // Only Good status leaves the command done without error, and a check condition is not a major error (AHA-1740A TRM, pp. 6-14, 6-16).
  put_le16(status + StatusWord, status[TargetStatus] ? 0 : CommandDone);
  account_transfer(ecb, status, moved, overrun);
}

// Fewer bytes moved than the data length is an underrun, an error unless the host suppressed it; an overrun always is one (AHA-1740A TRM, pp. 6-14, 6-15).
void AHA1742::account_transfer(const u8* ecb, u8* status, u32 moved, bool overrun)
{
  const u32 length = get_le32(ecb + DataLength);
  if (!overrun && moved == length)
    return;
  u16 word = u16(get_le16(status + StatusWord) | (overrun ? DataOverrun : DataUnderrun));
  if (overrun || !(get_le16(ecb + FlagWord1) & SuppressUnderrunError)) {
    word = u16((word & ~CommandDone) | MajorError);
    status[HostAdapterStatus] = OverrunOrUnderrun;
  }
  put_le16(status + StatusWord, word);
  // The residual count is zero after an overrun, and without a scatter/gather list the address is the data pointer (AHA-1740A TRM, p. 6-18).
  put_le32(status + ResidualCount, overrun ? 0 : length - moved);
  put_le32(status + ResidualAddress, get_le32(ecb + DataPointer));
}

// With the EISA interrupt clear, the oldest completion is loaded in the mailbox in and its status code sets the interrupt (AHA-1740A TRM, pp. 4-20, 4-22).
void AHA1742::post_completion()
{
  if ((g2stat & InterruptPending) || !completion_count)
    return;
  const u64 completion = completions[completion_head];
  completion_head = u8((completion_head + 1) % QueueSize);
  --completion_count;
  put_le32(mboxin, u32(completion));
  g2intst = u8(completion >> 32);
  g2stat |= InterruptPending;
  drive_irq(true);
}

// The pending interrupt reaches the line INTSEL names while INTEN is set; INTHIGH's electrical sense is not carried by the bus (AHA-1740A TRM, p. 4-15).
void AHA1742::drive_irq(bool pending)
{
  static const u8 line[8] = {9, 10, 11, 12, 0, 14, 15, 0}; // 0 where the manual has "Not used"; the bus ignores it.
  bus.set_slot_irq(slot, line[config[INTDEF] & INTSEL], pending && (config[INTDEF] & INTEN));
}
