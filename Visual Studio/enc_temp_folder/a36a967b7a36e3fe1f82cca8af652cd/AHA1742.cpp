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
  save_item(immediate_status, "immediate_status");
  save_item(immediate_error, "immediate_error");
  save_item(held, "held");
  save_item(held_count, "held_count");
  save_item(allegiance, "allegiance");
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
    // It then reads the mailbox out, a control block address for Start CCB or else an immediate command, and sets Mailbox Out Empty (AHA-1740A TRM, p. 4-20).
    if (!in_reset && (attn >> 4 == StartCCB || attn >> 4 == ImmediateCommand)) {
      g2stat |= MailboxOutEmpty;
      if (attn >> 4 == StartCCB)
        start_ccb(get_le32(mboxout), attn & 0x0f);
      else
        immediate_command(get_le32(mboxout), attn & 0x0f);
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
    // The local processor loses what it held, waiting completions and kept control blocks included, and a reset ends every allegiance (AHA-1740A TRM, pp. 4-8, 6-18).
    completion_count = 0;
    immediate_status = 0;
    held_count = 0;
    allegiance = 0;
  } else if (in_reset) {
    // Released with diagnostics passing: 00 is loaded in MBOXIN0, Busy clears and the mailbox out is empty (AHA-1740A TRM, pp. 4-20, 4-21).
    in_reset = false;
    mboxin[0] = 0;
    g2stat = MailboxOutEmpty;
    // With RSTPWR set the power-on sequence after a Hard Reset resets the SCSI bus, and the host is not notified (AHA-1740A TRM, pp. 4-16, 6-46).
    if (config[SCSIDEF] & RSTPWR)
      reset_scsi_bus();
  }
  // Clearing the interrupt tells the local processor the mailbox in is free for the next completion (AHA-1740A TRM, p. 4-20).
  post_completion();
}

// An immediate command is carried out at once on the 32 bits given in the mailbox out, for the SCSI ID in ATTN bits 3-0 (AHA-1740A TRM, p. 6-42).
void AHA1742::immediate_command(u32 command, u8 target)
{
  u8 error = 0;
  if (u8(command) == ImmediateReset && target == (config[SCSIDEF] & HSCSIID)) {
    // Reset with the adapter's own ID drops the control blocks and queued interrupts of every target, and resets the SCSI bus unless the Adapter Reset Option is set (AHA-1740A TRM, p. 6-45).
    forget(true, target);
    allegiance = 0;
    if (!(command & AdapterResetOption))
      reset_scsi_bus();
  } else if (u8(command) == ImmediateReset) {
    // Reset with another ID drops that target's control blocks and queued interrupts (AHA-1740A TRM, p. 6-43).
    forget(false, target);
    if (!(command & DeviceResetOption)) {
      // Unless the Device Reset Option is set the target is then sent a bus device reset, which also ends its allegiance; no answer to the selection is an error (AHA-1740A TRM, pp. 6-18, 6-44).
      allegiance &= u16(~(1 << target));
      if (CDisk* disk = target < 8 ? get_disk(0, target) : nullptr)
        disk->scsi_reset();
      else
        error = SelectionTimeout;
    }
  } else if (u8(command) != ImmediateResume || (command & ResumeNoInterrupt)) {
    // Resume ends an extended contingent allegiance, which never arises here, so it does nothing and may ask for no interrupt; other commands are not supported (AHA-1740A TRM, pp. 6-42, 6-46).
    return;
  }
  // The host is told with the SCSI ID in the status code, and after an error with its code in the first mailbox in byte (AHA-1740A TRM, pp. 4-22, 6-43).
  immediate_status = u8((error ? ImmediateError : ImmediateGood) << 4 | target);
  immediate_error = error;
  post_completion();
}

// The adapter drops the control blocks it keeps and the completions it has not yet presented, for one target or for all (AHA-1740A TRM, pp. 6-43, 6-45).
void AHA1742::forget(bool all, u8 target)
{
  u8 kept = 0;
  for (u8 i = 0; i < held_count; ++i)
    if (!all && u8(held[i] >> 32) != target)
      held[kept++] = held[i];
  held_count = kept;
  u8 waiting = 0;
  for (u8 i = 0; i < completion_count; ++i) {
    // A waiting completion carries its target in the low four bits of its status code.
    const u64 completion = completions[(completion_head + i) % QueueSize];
    if (!all && (completion >> 32 & 0x0f) != target)
      completions[(completion_head + waiting++) % QueueSize] = completion;
  }
  completion_count = waiting;
}

// A SCSI bus reset returns every target on the bus to its power-on state.
void AHA1742::reset_scsi_bus()
{
  for (int id = 0; id < 8; ++id)
    if (CDisk* disk = get_disk(0, id))
      disk->scsi_reset();
}

// A control block is started with the SCSI ID that was in ATTN bits 3-0 when its address was given (AHA-1740A TRM, p. 6-20).
void AHA1742::start_ccb(u32 address, u8 target)
{
  // Control and status block fields are stored least significant byte first (AHA-1740A TRM, p. 6-10).
  u8 ecb[48];
  if (!bus.slot_master_read(slot, address, ecb, sizeof(ecb)))
    return;
  // The option flags in bits 14-12 are not part of the operation code (AHA-1740A TRM, p. 6-4).
  const u16 operation = get_le16(ecb + CommandWord) & 0x0fff, flags = get_le16(ecb + FlagWord1);
  // Other operations are not carried out yet.
  if (operation != InitiatorCommand && operation != ReadSenseInformation && operation != ReadInquiryData)
    return;
  u8 status[32]{};
  // With every slot taken, by waiting completions and kept control blocks, the control block is refused with Queue Full and no action is taken on it (AHA-1740A TRM, p. 6-14).
  if (completion_count + held_count == QueueSize) {
    put_le16(status + StatusWord, QueueFull);
    bus.slot_master_write(slot, get_le32(ecb + StatusBlockPointer), status, sizeof(status));
    return;
  }
  // While its target is in contingent allegiance a control block is accepted and kept, not carried out, unless it is the Read Sense Information that ends it (AHA-1740A TRM, p. 6-18).
  if ((allegiance >> target & 1) && operation != ReadSenseInformation) {
    held[held_count++] = u64(target) << 32 | address;
    return;
  }
  if (operation == InitiatorCommand)
    initiator_command(ecb, target, status);
  else if (operation == ReadSenseInformation)
    read_sense_information(ecb, target, status);
  else
    read_inquiry_data(ecb, status);
  const u16 word = get_le16(status + StatusWord);
  // A check condition whose sense information was not stored puts the target in contingent allegiance (AHA-1740A TRM, p. 6-18).
  if (status[TargetStatus] == CheckCondition && !(word & SenseStored))
    allegiance |= u16(1 << target);
  // The host is interrupted unless the command is done without error and it set Disable Interrupt (AHA-1740A TRM, pp. 6-5, 6-15).
  const bool interrupt = !(word & CommandDone) || !(flags & DisableInterrupt);
  put_le16(status + StatusWord, u16(word | (interrupt ? InterruptIssued : 0)));
  // The status block is left out only when the command is done without error and the host disabled it (AHA-1740A TRM, p. 6-11).
  if (!(word & CommandDone) || !(flags & DisableStatusBlock))
    bus.slot_master_write(slot, get_le32(ecb + StatusBlockPointer), status, sizeof(status));
  if (interrupt) {
    // The completion waits its turn with its status code and target, behind any the host has not yet taken (AHA-1740A TRM, pp. 4-20, 4-22).
    const u8 code = u8(((word & CommandDone) ? CompletedGood : CompletedError) << 4 | target);
    completions[(completion_head + completion_count++) % QueueSize] = u64(code) << 32 | address;
    post_completion();
  }
  // Completion of Read Sense Information releases the allegiance, and the target's kept control blocks are started in the order they came (AHA-1740A TRM, pp. 6-18, 6-34).
  if (operation != ReadSenseInformation || !(allegiance >> target & 1))
    return;
  allegiance &= u16(~(1 << target));
  const u8 kept = held_count;
  held_count = 0;
  for (u8 i = 0; i < kept; ++i) {
    // One of them can bring the allegiance back, and those after it are then kept again in the same order.
    const u64 entry = held[i];
    if (u8(entry >> 32) == target)
      start_ccb(u32(entry), target);
    else
      held[held_count++] = entry;
  }
}

// Read Host Adapter Inquiry Data: the data is cut to the length the host allows, and a longer buffer is left short (AHA-1740A TRM, pp. 6-30, 6-32).
void AHA1742::read_inquiry_data(const u8* ecb, u8* status)
{
  // Read Host Adapter Inquiry Data returns fixed-width ASCII identification fields (AHA-1740A TRM, pp. 6-31 to 6-33).
  const u8 inquiry_data[] = {
    0x23, 0x00, // Processor device; Target Mode is supported and a reset leaves it disabled (AHA-1740A TRM, pp. 5-10, 6-32).
    0x02, 0x00, // SCSI-2, with no asynchronous event notification accepted.
    0x33, 0x00, // Bytes that follow, then no LUNs enabled for Target Mode.
    0x40, 0x18, // 64 control blocks (AHA-1740A TRM, p. 6-2); linked commands and synchronous transfers, single-ended, 8-bit.
    'A', 'D', 'A', 'P', 'T', 'E', 'C', ' ', // Vendor.
    'A', 'H', 'A', '1', '7', '4', u8(floppy ? '2' : '0'), 'A', // J6 disabled identifies a 1740A (AHA-1740A TRM, p. 3-15).
    'E', 'N', 'H', 'A', 'N', 'C', 'E', 'D', // Firmware type.
    'H', ' ', ' ', ' ', // Minimum firmware revision in the OSF/1 aha driver.
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', // Release date, unknown for H.
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', // Release time, unknown for H.
    0x00, 0x00, 0x00, 0x00, // Firmware checksum, unknown for H, then a reserved word.
  };
  const u32 length = get_le32(ecb + DataLength);
  const u32 count = length < sizeof(inquiry_data) ? length : u32(sizeof(inquiry_data));
  bus.slot_master_write(slot, get_le32(ecb + DataPointer), inquiry_data, count);
  put_le16(status + StatusWord, CommandDone);
  account_transfer(ecb, status, ecb + DataPointer, 1, count, false);
}

// Initiator SCSI Command: the adapter selects the target and follows it through the bus phases of one command (AHA-1740A TRM, p. 6-26).
void AHA1742::initiator_command(const u8* ecb, u8 target, u8* status)
{
  const u8 cdb_length = ecb[CDBLength];
  const u16 flags1 = get_le16(ecb + FlagWord1), flags2 = get_le16(ecb + FlagWord2);
  // A CDB is 6, 10 or 12 bytes long, and Suppress Transfer is for data in: anything else is an invalid parameter, and no action is taken on the control block (AHA-1740A TRM, pp. 6-9, 6-12, 6-15, 6-17).
  const bool suppress_out = (flags2 & (DataTransfer | Direction | SuppressTransfer)) == (DataTransfer | SuppressTransfer);
  if ((cdb_length != 6 && cdb_length != 10 && cdb_length != 12) || suppress_out) {
    put_le16(status + StatusWord, SpecificationCheck);
    status[HostAdapterStatus] = InvalidParameter;
    return;
  }
  // The data area is the control block's own pointer and length, or with Scatter/Gather set the list of such pairs they locate (AHA-1740A TRM, pp. 6-5, 6-10).
  u8 gathered[1024];
  const u8* list = ecb + DataPointer;
  u32 entries = 1;
  if (flags1 & ScatterGather) {
    // A list holds up to 128 pairs: a length that is zero, not a multiple of 8 or over 1 KByte makes it invalid (AHA-1740A TRM, pp. 6-10, 6-11, 6-17).
    const u32 length = get_le32(ecb + DataLength);
    if (!length || (length & 7) || length > sizeof(gathered)) {
      put_le16(status + StatusWord, SpecificationCheck);
      status[HostAdapterStatus] = InvalidList;
      return;
    }
    bus.slot_master_read(slot, get_le32(ecb + DataPointer), gathered, length);
    list = gathered;
    entries = length / 8;
  }
  const scsi_result command = scsi_command(target, flags2, ecb + CDBBytes, cdb_length, list, entries);
  if (!report_target(command, status))
    return;
  // With Automatic Request Sense set, a check condition is followed at once by a Request Sense (AHA-1740A TRM, p. 6-6).
  if (command.status == CheckCondition && (flags1 & AutomaticRequestSense)) {
    u8 pair[8];
    const scsi_result sense = request_sense(ecb, target, pair);
    if (sense.selected && !sense.status) {
      sense_stored(status, sense.moved);
    } else {
      // A Request Sense that does not end with Good status is a major error of its own (AHA-1740A TRM, p. 6-17).
      put_le16(status + StatusWord, MajorError);
      status[HostAdapterStatus] = RequestSenseFailed;
    }
  }
  account_transfer(ecb, status, list, entries, command.moved, command.overrun);
}

// Read Sense Information: the adapter issues a Request Sense to the target, as the host must ask after a check condition without Automatic Request Sense (AHA-1740A TRM, p. 6-34).
void AHA1742::read_sense_information(const u8* ecb, u8 target, u8* status)
{
  u8 pair[8];
  const scsi_result sense = request_sense(ecb, target, pair);
  if (!report_target(sense, status))
    return;
  if (!sense.status)
    sense_stored(status, sense.moved);
  // Suppress Error on Underrun is supported, so the sense bytes are counted against the sense length as data is against its length (AHA-1740A TRM, p. 6-35).
  account_transfer(ecb, status, pair, 1, sense.moved, sense.overrun);
}

// What the target made of a command: no answer to the selection is a timeout and a data phase against the Direction flag an invalid phase, both major errors; only Good status leaves the command done without error (AHA-1740A TRM, pp. 6-8, 6-14, 6-16, 6-17).
bool AHA1742::report_target(const scsi_result& result, u8* status)
{
  if (!result.selected || result.wrong_phase) {
    put_le16(status + StatusWord, MajorError);
    status[HostAdapterStatus] = result.selected ? InvalidPhase : SelectionTimeout;
    return false;
  }
  status[TargetStatus] = result.status;
  put_le16(status + StatusWord, result.status ? 0 : CommandDone);
  return true;
}

// Request Sense for as many bytes as the sense length allows, stored at the sense information pointer; pair is filled with that data area (AHA-1740A TRM, pp. 6-6, 6-12).
AHA1742::scsi_result AHA1742::request_sense(const u8* ecb, u8 target, u8* pair)
{
  put_le32(pair, get_le32(ecb + SensePointer));
  put_le32(pair + 4, ecb[SenseLength]);
  const u8 cdb[6] = {0x03, 0, 0, 0, ecb[SenseLength], 0};
  // It goes to the same LUN under the same disconnect rule; the data flags of the host's own command do not apply to it.
  return scsi_command(target, u16(get_le16(ecb + FlagWord2) & (LogicalUnit | NoDisconnect)), cdb, u8(sizeof(cdb)), pair, 1);
}

// Sense Information Stored comes with one byte of additional status, byte 14, which counts the sense bytes stored (AHA-1740A TRM, pp. 6-16, 6-19).
void AHA1742::sense_stored(u8* status, u32 count)
{
  put_le16(status + StatusWord, u16(get_le16(status + StatusWord) | AdditionalStatus | SenseStored));
  put_le16(status + AdditionalStatusLength, 1);
  status[StoredSenseLength] = u8(count);
}

// One command on the adapter's SCSI bus, from selection to the target's status byte, under the options of flag word 2.
AHA1742::scsi_result AHA1742::scsi_command(u8 target, u16 flags2, const u8* cdb, u8 cdb_length, const u8* list, u32 entries)
{
  scsi_result result{};
  // The adapter is the only initiator on its bus, so it wins arbitration with the ID in SCSIDEF bits 3-0 (AHA-1740A TRM, p. 4-16).
  scsi_initiator_id[0] = config[SCSIDEF] & HSCSIID;
  scsi_arbitrate(0);
  if (!scsi_select(0, target)) {
    scsi_free(0);
    return result;
  }
  result.selected = true;
  // The target chooses each bus phase and the adapter answers it, until the target frees the bus.
  for (int phase; (phase = scsi_get_phase(0)) != SCSI_PHASE_FREE; scsi_xfer_done(0)) {
    const size_t bytes = scsi_expected_xfer(0);
    if (phase == SCSI_PHASE_MSG_OUT) {
      // The identify message names the LUN and allows disconnection, the default, unless No Disconnect is set (AHA-1740A TRM, pp. 4-29, 6-7).
      *static_cast<u8*>(scsi_xfer_ptr(0, 1)) = u8(0x80 | ((flags2 & NoDisconnect) ? 0 : 0x40) | (flags2 & LogicalUnit));
    } else if (phase == SCSI_PHASE_COMMAND) {
      memcpy(scsi_xfer_ptr(0, cdb_length), cdb, cdb_length);
    } else if (phase == SCSI_PHASE_DATA_OUT || phase == SCSI_PHASE_DATA_IN) {
      const bool in = phase == SCSI_PHASE_DATA_IN;
      // With Data Transfer set, a data phase against the Direction flag is not carried out and ends the command as an invalid phase (AHA-1740A TRM, pp. 6-8, 6-9).
      if ((flags2 & DataTransfer) && in != ((flags2 & Direction) != 0)) {
        result.wrong_phase = true;
        scsi_bus[0]->free_bus(target);
        break;
      }
      // Suppress Transfer takes the data from the target and counts it against the data area without storing it (AHA-1740A TRM, p. 6-9).
      u8* data = static_cast<u8*>(scsi_xfer_ptr(0, bytes));
      if ((flags2 & DataTransfer) && (flags2 & SuppressTransfer))
        data = nullptr;
      const u32 done = move_data(list, entries, result.moved, data, u32(bytes), in);
      result.moved += done;
      // A target that wants more than the data area holds is a data overrun: data in is taken without being stored, and data out ends the command there (AHA-1740A TRM, pp. 6-9, 6-15).
      if (done < bytes) {
        result.overrun = true;
        if (phase == SCSI_PHASE_DATA_OUT) {
          scsi_bus[0]->free_bus(target);
          break;
        }
      }
    } else {
      // The status phase gives the target status; the Command Complete message that follows needs no answer (AHA-1740A TRM, p. 6-17).
      const u8* data = static_cast<u8*>(scsi_xfer_ptr(0, bytes));
      if (phase == SCSI_PHASE_STATUS && bytes)
        result.status = data[0];
    }
  }
  return result;
}

// Moves up to count bytes between the target's buffer and the data area, from skip bytes into the area, and returns how many the area had room for; with no buffer they are only counted.
u32 AHA1742::move_data(const u8* list, u32 entries, u32 skip, u8* data, u32 count, bool to_host)
{
  u32 done = 0;
  for (u32 i = 0; i < entries && done < count; ++i) {
    const u32 address = get_le32(list + 8 * i), length = get_le32(list + 8 * i + 4);
    // The pairs are used in list order, so those already used up are passed over (AHA-1740A TRM, p. 6-10).
    if (skip >= length) {
      skip -= length;
      continue;
    }
    const u32 part = length - skip < count - done ? length - skip : count - done;
    if (data && to_host)
      bus.slot_master_write(slot, address + skip, data + done, part);
    else if (data)
      bus.slot_master_read(slot, address + skip, data + done, part);
    done += part;
    skip = 0;
  }
  return done;
}

// Fewer bytes moved than the data area holds is an underrun, an error unless the host suppressed it; an overrun always is one (AHA-1740A TRM, pp. 6-14, 6-15).
void AHA1742::account_transfer(const u8* ecb, u8* status, const u8* list, u32 entries, u32 moved, bool overrun)
{
  // The pair used last holds the last byte moved, or is the first one when nothing moved; total ends as the size of the data area.
  u32 last = 0;
  u64 total = 0, end = get_le32(list + 4);
  for (u32 i = 0; i < entries; ++i) {
    const u32 length = get_le32(list + 8 * i + 4);
    if (total < moved) {
      last = i;
      end = total + length;
    }
    total += length;
  }
  if (!overrun && moved == total)
    return;
  u16 word = u16(get_le16(status + StatusWord) | (overrun ? DataOverrun : DataUnderrun));
  if (overrun || !(get_le16(ecb + FlagWord1) & SuppressUnderrunError)) {
    word = u16((word & ~CommandDone) | MajorError);
    status[HostAdapterStatus] = OverrunOrUnderrun;
  }
  put_le16(status + StatusWord, word);
  // The residual count is what is left of the pair used last, or zero after an overrun (AHA-1740A TRM, p. 6-18).
  put_le32(status + ResidualCount, overrun ? 0 : u32(end - moved));
  // The residual address is the area's own pointer, or with Scatter/Gather set the address in the list of the pair used last (AHA-1740A TRM, pp. 6-18, 6-19).
  put_le32(status + ResidualAddress, (get_le16(ecb + FlagWord1) & ScatterGather) ? get_le32(ecb + DataPointer) + 8 * last : get_le32(list));
}

// With the EISA interrupt clear, the next completion is loaded in the mailbox in and its status code sets the interrupt: an immediate command's first, then the oldest control block's (AHA-1740A TRM, pp. 4-20, 4-22, 6-44).
void AHA1742::post_completion()
{
  if ((g2stat & InterruptPending) || (!immediate_status && !completion_count))
    return;
  if (immediate_status) {
    put_le32(mboxin, immediate_error);
    g2intst = immediate_status;
    immediate_status = 0;
  } else {
    const u64 completion = completions[completion_head];
    completion_head = u8((completion_head + 1) % QueueSize);
    --completion_count;
    put_le32(mboxin, u32(completion));
    g2intst = u8(completion >> 32);
  }
  g2stat |= InterruptPending;
  drive_irq(true);
}

// The pending interrupt reaches the line INTSEL names while INTEN is set; INTHIGH's electrical sense is not carried by the bus (AHA-1740A TRM, p. 4-15).
void AHA1742::drive_irq(bool pending)
{
  static const u8 line[8] = {9, 10, 11, 12, 0, 14, 15, 0}; // 0 where the manual has "Not used"; the bus ignores it.
  bus.set_slot_irq(slot, line[config[INTDEF] & INTSEL], pending && (config[INTDEF] & INTEN));
}
