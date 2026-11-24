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

class EISABus;

// Host interface of the AHA-1742A as system software sees it; the card's own microcode is not run.
class AHA1742 final : public CSystemComponent
{
public:
  AHA1742(CConfigurator* cfg, CSystem* system, EISABus& bus, unsigned slot);
  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;

private:
  enum { Configuration, Group2 };
  // Offsets from zCD0 of the Enhanced Mode registers (AHA-1740A TRM, pp. 4-21 to 4-23).
  enum { MBOXOUT0 = 0, ATTN = 4, G2CNTRL = 5, G2STAT = 7, MBOXIN0 = 8 };
  enum { StartCCB = 4 }; // ATTN bits 7-4 (AHA-1740A TRM, p. 4-22).
  enum { HardReset = 0x80 }; // G2CNTRL bit 7 (AHA-1740A TRM, p. 4-22).
  enum { MailboxOutEmpty = 0x04, Busy = 0x01 }; // G2STAT bits 2 and 0 (AHA-1740A TRM, p. 4-23).
  // Byte offsets in the 48-byte control block and the 32-byte status block (AHA-1740A TRM, pp. 6-3, 6-13).
  enum { CommandWord = 0x00, FlagWord1 = 0x02, DataPointer = 0x08, DataLength = 0x0c, StatusBlockPointer = 0x10 };
  enum { StatusWord = 0x00, HostAdapterStatus = 0x02, ResidualCount = 0x04, ResidualAddress = 0x08 };
  enum { ReadInquiryData = 0x0a }; // Operation code (AHA-1740A TRM, p. 6-4).
  enum { SuppressUnderrunError = 0x0400, DisableStatusBlock = 0x4000 }; // Flag word 1 bits 10 and 14 (AHA-1740A TRM, pp. 6-5, 6-6).
  enum { CommandDone = 0x0001, DataUnderrun = 0x0002, MajorError = 0x1000 }; // Status word bits 0, 1 and 12 (AHA-1740A TRM, pp. 6-14, 6-16).
  enum { UnderrunOccurred = 0x12 }; // Host adapter status (AHA-1740A TRM, p. 6-17).
  void start_ccb(u32 address);
  EISABus& bus;
  const unsigned slot;
  // PORTADDR, BIOSADDR, INTDEF, SCSIDEF, BUSDEF and three reserved bytes, zCC0-zCC7 (AHA-1740A TRM, pp. 4-13 to 4-16).
  u8 config[8]{};
  u8 mboxout[4]{};
  u8 attn = 0;
  // The host starts by waiting for Mailbox Out Empty, so power-on and a completed reset leave it set (AHA-1740A TRM, p. 4-20).
  u8 g2stat = MailboxOutEmpty;
  u8 mboxin[4]{};
  bool in_reset = false;
};

#endif
