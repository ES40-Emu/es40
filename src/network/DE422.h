/* ES40 emulator -- DEC EtherWORKS EISA Ethernet adapter
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

#ifndef ES40_DE422_H
#define ES40_DE422_H

#include "emu/SystemComponent.h"
#include "Am7990.h"
#include <memory>

class CNetworkBackend;
class EISABus;

class DE422 final : public CSystemComponent
{
public:
  DE422(CConfigurator* cfg, CSystem* system, EISABus& bus, unsigned slot);
  ~DE422() override;
  void init() override;
  void ResetPCI() override;
  void check_state() override;
  std::string snapshot_identity() const override;
  void finalize_restore() noexcept override;
  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;

private:
  enum { NiCsr, Rdp, Rap, AddressProm, Buffer };
  enum { IrqEnable = 0x02, IrqMask = 0x04, LowBank = 0x40, ShadowEnable = 0x80 };
  static constexpr u16 default_config = 0x10a; // IRQ5, D0000-DFFFF and 30-second timeout (EK-DE422-OM, Table 1-1).
  void set_station_address(const char* mac);
  void route_irq(bool asserted);
  u32 window_base() const noexcept;
  u32 buffer_offset(u32 address) const noexcept;

  EISABus& bus;
  const unsigned slot;
  u8 ram[128 * 1024]{}; // 128 KiB of packet RAM on the adapter (EK-DE422-OM, §A.2.1).
  u8 prom[32]{};
  u8 prom_address = 0;
  u16 config = default_config;
  u16 nicsr = 0;
  std::unique_ptr<CNetworkBackend> backend;
  Am7990 lance;
};

#endif
