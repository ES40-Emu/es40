/* ES40 emulator -- Intel 82357 Integrated System Peripheral
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

#ifndef ES40_INTEL82357_H
#define ES40_INTEL82357_H

#include "DMA.h"
#include "Pic8259.h"

class Intel82357 : public CSystemComponent, public CDMAHost
{
public:
  Intel82357(CConfigurator* cfg, CSystem* system, CDMAHost& memory_host,
    std::function<void(bool)> interrupt_output);
  CDMA& dma_controller() const;
  bool decodes_port(u16 port, bool write) const;
  bool read_port(u16 port, u8& data);
  bool write_port(u16 port, u8 data);
  u8 interrupt_acknowledge();

  void set_isa_irq(int irq, bool asserted) override;
  void ResetPCI() override;
  void finalize_restore() noexcept override;

  const CSystemComponent* isa_decode_owner() const noexcept override
    { return memory_host.isa_decode_owner(); }
  bool dma_transfer_enabled() override { return memory_host.dma_transfer_enabled(); }
  void dma_read_memory(u32 address, void* dest, size_t bytes) override
    { memory_host.dma_read_memory(address, dest, bytes); }
  void dma_write_memory(u32 address, const void* source, size_t bytes) override
    { memory_host.dma_write_memory(address, source, bytes); }
  void dma_master_tc(int channel) override { memory_host.dma_master_tc(channel); }

private:
  class Dma;
  void create_dma(CConfigurator* cfg, CSystem* system);
  static bool decodes_dma_port(u16 port, bool write);
  bool access_dma_port(u16 port, u8& data, bool write);
  void set_dma_chaining_irq(bool asserted);

  CDMAHost& memory_host;
  Pic8259::State pic_state{};
  Pic8259::Acknowledge ack{};
  bool vector_pending = false;
  Pic8259 pic;
  Dma* dma;
};

#endif
