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

#ifndef ES40_AHA1742_H
#define ES40_AHA1742_H

#include "emu/SystemComponent.h"
#include "DiskController.h"
#include "SCSIDevice.h"

class EISABus;

// Host interface of the AHA-1742A as system software sees it; the card's own microcode is not run.
class AHA1742 final : public CSystemComponent, public CDiskController, public CSCSIDevice
{
public:
  AHA1742(CConfigurator* cfg, CSystem* system, EISABus& bus, unsigned slot,
    CConfigurator* floppy_cfg, bool secondary_floppy);
  CDiskController* floppy_controller() const { return floppy; }
  void register_disk(CDisk* dsk, int bus, int dev) override;
  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;

private:
  enum { Configuration, Group2 };
  enum { INTDEF = 2, INTEN = 0x10, INTSEL = 0x07 }; // Offset of INTDEF from zCC0, then its bit 4 and bits 2-0 (AHA-1740A TRM, p. 4-15).
  enum { SCSIDEF = 3, HSCSIID = 0x0f }; // Offset of SCSIDEF from zCC0, then its bits 3-0 (AHA-1740A TRM, p. 4-16).
  // Offsets from zCD0 of the Enhanced Mode registers (AHA-1740A TRM, pp. 4-21 to 4-23).
  enum { MBOXOUT0 = 0, ATTN = 4, G2CNTRL = 5, G2INTST = 6, G2STAT = 7, MBOXIN0 = 8 };
  enum { StartCCB = 4 }; // ATTN bits 7-4 (AHA-1740A TRM, p. 4-22).
  enum { HardReset = 0x80, ClearInterrupt = 0x40 }; // G2CNTRL bits 7 and 6 (AHA-1740A TRM, p. 4-22).
  enum { CompletedGood = 0x1, CompletedError = 0xc }; // G2INTST bits 7-4 (AHA-1740A TRM, p. 4-22).
  enum { MailboxOutEmpty = 0x04, InterruptPending = 0x02, Busy = 0x01 }; // G2STAT bits 2, 1 and 0 (AHA-1740A TRM, p. 4-23).
  // Byte offsets in the 48-byte control block and the 32-byte status block (AHA-1740A TRM, pp. 6-3, 6-13).
  enum { CommandWord = 0x00, FlagWord1 = 0x02, FlagWord2 = 0x04, DataPointer = 0x08, DataLength = 0x0c, StatusBlockPointer = 0x10, SensePointer = 0x1c, SenseLength = 0x20, CDBLength = 0x21, CDBBytes = 0x24 };
  enum { StatusWord = 0x00, HostAdapterStatus = 0x02, TargetStatus = 0x03, ResidualCount = 0x04, ResidualAddress = 0x08, AdditionalStatusLength = 0x0c, StoredSenseLength = 0x0e };
  enum { InitiatorCommand = 0x01, ReadSenseInformation = 0x08, ReadInquiryData = 0x0a }; // Operation codes (AHA-1740A TRM, p. 6-4).
  // Flag word 1 bits 7, 10, 12, 14 and 15 (AHA-1740A TRM, pp. 6-5, 6-6).
  enum { DisableInterrupt = 0x0080, SuppressUnderrunError = 0x0400, ScatterGather = 0x1000, DisableStatusBlock = 0x4000, AutomaticRequestSense = 0x8000 };
  enum { LogicalUnit = 0x0007, NoDisconnect = 0x0040 }; // Flag word 2 bits 2-0 and 6 (AHA-1740A TRM, p. 6-7).
  // Status word bits 0, 1, 3, 4, 5, 7, 8, 9 and 12; the status word figure and p. 6-19 put Additional Status Available at bit 8 (AHA-1740A TRM, pp. 6-14 to 6-16, 6-19).
  enum { CommandDone = 0x0001, DataUnderrun = 0x0002, QueueFull = 0x0008, SpecificationCheck = 0x0010, DataOverrun = 0x0020, InterruptIssued = 0x0080, AdditionalStatus = 0x0100, SenseStored = 0x0200, MajorError = 0x1000 };
  // Host adapter status codes (AHA-1740A TRM, p. 6-17).
  enum { SelectionTimeout = 0x11, OverrunOrUnderrun = 0x12, InvalidParameter = 0x18, InvalidList = 0x1a, RequestSenseFailed = 0x1b };
  enum { CheckCondition = 0x02 }; // Target status (AHA-1740A TRM, p. 6-17).
  enum { QueueSize = 64 }; // Control blocks the adapter holds at once (AHA-1740A TRM, p. 6-2).
  // What one command on the SCSI bus came to: whether a target answered, its status byte, the bytes moved and whether it wanted to move more.
  struct scsi_result { bool selected; u8 status; u32 moved; bool overrun; };
  void start_ccb(u32 address, u8 target);
  void read_inquiry_data(const u8* ecb, u8* status);
  void initiator_command(const u8* ecb, u8 target, u8* status);
  void read_sense_information(const u8* ecb, u8 target, u8* status);
  bool report_target(const scsi_result& result, u8* status);
  scsi_result request_sense(const u8* ecb, u8 target, u8* pair);
  void sense_stored(u8* status, u32 count);
  scsi_result scsi_command(const u8* ecb, u8 target, const u8* cdb, u8 cdb_length, const u8* list, u32 entries);
  // The data area is given as 8-byte {pointer, length} pairs: the control block's own, or its scatter/gather list (AHA-1740A TRM, pp. 6-10, 6-11).
  u32 move_data(const u8* list, u32 entries, u32 skip, u8* data, u32 count, bool to_host);
  void account_transfer(const u8* ecb, u8* status, const u8* list, u32 entries, u32 moved, bool overrun);
  void post_completion();
  void drive_irq(bool pending);
  EISABus& bus;
  const unsigned slot;
  CDiskController* floppy = nullptr; // The 1742A carries a separate Intel 82077A (Adaptec TRM, Fig. 2-1).
  // PORTADDR, BIOSADDR, INTDEF, SCSIDEF, BUSDEF and three reserved bytes, zCC0-zCC7 (AHA-1740A TRM, pp. 4-13 to 4-16).
  u8 config[8]{};
  u8 mboxout[4]{};
  u8 attn = 0;
  // The host starts by waiting for Mailbox Out Empty, so power-on and a completed reset leave it set (AHA-1740A TRM, p. 4-20).
  u8 g2stat = MailboxOutEmpty;
  u8 g2intst = 0;
  u8 mboxin[4]{};
  // Completions wait here, oldest first, for the mailbox in: G2INTST value in bits 39-32 over the control block address (AHA-1740A TRM, p. 4-20).
  u64 completions[QueueSize]{};
  u8 completion_head = 0;
  u8 completion_count = 0;
  // Control blocks kept while their target is in contingent allegiance, oldest first: the target in bits 35-32 over the address (AHA-1740A TRM, p. 6-18).
  u64 held[QueueSize]{};
  u8 held_count = 0;
  u16 allegiance = 0; // One bit for each SCSI ID that is in contingent allegiance.
  bool in_reset = false;
};

#endif
