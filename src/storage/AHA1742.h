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
  enum { G2CNTRL = 5, G2STAT = 7, MBOXIN0 = 8 };
  enum { HardReset = 0x80 }; // G2CNTRL bit 7 (AHA-1740A TRM, p. 4-22).
  enum { Busy = 0x01 }; // G2STAT bit 0 (AHA-1740A TRM, p. 4-23).
  EISABus& bus;
  const unsigned slot;
  // PORTADDR, BIOSADDR, INTDEF, SCSIDEF, BUSDEF and three reserved bytes, zCC0-zCC7 (AHA-1740A TRM, pp. 4-13 to 4-16).
  u8 config[8]{};
  u8 g2stat = 0;
  u8 mboxin[4]{};
  bool in_reset = false;
};

#endif
