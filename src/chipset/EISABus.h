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

#ifndef ES40_EISA_BUS_H
#define ES40_EISA_BUS_H

#include "ISABus.h"
#include <vector>

class CDMAHost;

class EISABus : public CISABus
{
public:
  explicit EISABus(CDMAHost& host) : host(host) {}
  void bind_dma(CDMA& controller) { dma = &controller; }
  CDMA* dma_controller() noexcept override { return dma; }
  const CSystemComponent* isa_decode_owner() const noexcept override;
  void register_isa_io(CSystemComponent& device, int index, u16 port, u32 length) override;
  IsaIoDecode isa_io_decode(u32 port) const noexcept override;
  void set_isa_irq(int irq, bool asserted) override;

  // Logical byte ports. Unclaimed reads leave data untouched.
  bool read_port(u16 port, u8& data);
  bool write_port(u16 port, u8 data);

private:
  struct IoRange {
    CSystemComponent* device;
    int index;
    u16 base;
    u32 length;
  };

  const IoRange* find_io(u32 port) const noexcept;
  std::vector<IoRange> io_ranges;
  CDMAHost& host;
  CDMA* dma = nullptr;
};

#endif
