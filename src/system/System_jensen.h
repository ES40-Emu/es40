/* ES40 emulator -- Jensen system
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

#ifndef ES40_SYSTEM_JENSEN_H
#define ES40_SYSTEM_JENSEN_H

#include "System.h"
#include <atomic>
#include <mutex>

class CSerial;
class VL82C106;

class Jensen final : public CSystem
{
public:
  explicit Jensen(CConfigurator* cfg);
  ~Jensen() override;
  void attach_board_devices(CConfigurator* cfg) override;
  void register_legacy_io(CSystemComponent& device, int index, int bus, u32 port, u32 length) override;
  void ResetMem(unsigned int membits) override;
  CSerial& serial_port(unsigned number) const;
  u8 read_sysctl() const noexcept { return sysctl.load(std::memory_order_relaxed); }
  void write_sysctl(u8 value);

private:
  u64 ram_offset(u64 address) const noexcept override;
  u64 linear_ram_size() const noexcept override;
  void set_serial_irq(unsigned channel, bool asserted);
  void finish_board_restore() override;
  std::atomic<u8> sysctl{0};
  VL82C106* combo = nullptr;
  std::mutex serial_irq_mutex;
  unsigned serial_irqs = 0;
};

#endif
