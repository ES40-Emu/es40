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
#include <array>
#include <vector>

class CDMAHost;

class EISABus : public CISABus
{
public:
  struct State {
    u16 enabled_slots = 0;
    u16 irq_requests[16]{}; // Legacy ISA input at index 0, EISA slots at 1-15.
  };

  explicit EISABus(CDMAHost& host) : host(host) {}
  State& snapshot_state() noexcept { return state; }
  void reset();
  void bind_dma(CDMA& controller) { dma = &controller; }
  CDMA* dma_controller() noexcept override { return dma; }
  const CSystemComponent* isa_decode_owner() const noexcept override;
  void register_isa_io(CSystemComponent& device, int index, u16 port, u32 length) override;
  // Register the maximum native I/O width in bits.
  void register_eisa_io(CSystemComponent& device, int index, u16 port, u32 length, int max_dsize);
  // Product ID bytes are in ascending port order.
  void register_slot_id(unsigned slot, const std::array<u8, 4>& product_id);
  // Attach an I/O range after the card registers its slot ID.
  void register_slot_io(unsigned slot, CSystemComponent& device, int index,
    u16 port, u32 length, int max_dsize);
  bool slot_enabled(unsigned slot) const noexcept;
  IsaIoDecode isa_io_decode(u32 port) const noexcept override;
  // Cards update their IRQ levels under the system device-bus lock.
  void set_slot_irq(unsigned slot, int irq, bool asserted);
  void set_isa_irq(int irq, bool asserted) override;

  // Unclaimed logical port reads leave data untouched.
  bool read_port(u16 port, u8& data);
  bool write_port(u16 port, u8 data) { return write_port(port, 8, data); }
  bool read_port(u16 port, int dsize, u16& data);
  bool read_port(u16 port, int dsize, u32& data);
  bool write_port(u16 port, int dsize, u32 data);

private:
  struct IoRange {
    CSystemComponent* device; // Null for the slot ID and control registers.
    int index;
    u16 base;
    u32 length;
    int max_dsize;
    unsigned slot;
    u32 product_id = 0;
  };

  void register_io(unsigned owner_slot, CSystemComponent& device, int index,
    u16 port, u32 length, int max_dsize);
  bool slot_registered(unsigned slot) const noexcept;
  u16 active_irqs() const noexcept;
  void update_irq_lines(u16 previous);
  void set_irq_request(unsigned slot, int irq, bool asserted);
  const IoRange* find_io(u32 port, unsigned bytes = 1) const noexcept;
  const IoRange* decode_io(u16 port, int dsize, bool write) const noexcept;
  std::vector<IoRange> io_ranges;
  State state;
  CDMAHost& host;
  CDMA* dma = nullptr;
};

#endif
