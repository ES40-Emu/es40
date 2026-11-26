/* ES40 emulator -- DEC 3000 Model 300X system
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

#ifndef ES40_SYSTEM_PELICAN_H
#define ES40_SYSTEM_PELICAN_H

#include "System.h"

class PelicanIOCTL;

class Pelican final : public CSystem
{
public:
  explicit Pelican(CConfigurator* cfg);
  void attach_board_devices(CConfigurator* cfg) override;
  u64 default_cpu_hz() const noexcept override { return 175000000; }
  int LoadROM() override;
  void ResetMem(unsigned int membits) override;
  u64 ram_offset(u64 address) const noexcept override;
  // Table 5 maps PA<31:30> = 01 to the non-cacheable memory window (EK-D3SYS-PM).
  bool bypasses_cpu_cache(u64 address) const noexcept override { return (address >> 30) == 1; }
  u64 linear_ram_size() const noexcept override { return population.bytes; }
  u8 diagnostic_leds();

private:
  class Registers;

  struct MemoryPopulation {
    u64 bytes;
    u8 size_straps;
    unsigned backing_bits() const;
    static MemoryPopulation from_bits(u64 bits);
    static MemoryPopulation from_config(CConfigurator* cfg);
  };

  Pelican(CConfigurator* cfg, MemoryPopulation installed);
  MemoryPopulation population;
  PelicanIOCTL* io_control = nullptr;
  const bool direct_srm;
  u8 lit_leds = 0xff;
};

#endif
