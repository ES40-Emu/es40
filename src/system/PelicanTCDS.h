/* ES40 emulator -- DEC 3000 Model 300X TURBOchannel Dual SCSI ASIC
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

#ifndef ES40_PELICANTCDS_H
#define ES40_PELICANTCDS_H

#include "emu/SystemComponent.h"
#include "storage/DiskController.h"
#include "storage/NCR53C94.h"
#include <chrono>

class CSCSIBus;
class Pelican;

class PelicanTCDS final : public CSystemComponent, public CDiskController
{
public:
  PelicanTCDS(CConfigurator* cfg, Pelican* system);
  void init() override;
  void register_disk(CDisk* disk, int bus, int id) override;
  bool interrupt() const;
  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;
  void ResetPCI() override;
  void start_threads() override;
  void stop_threads() override;
  void check_state() override;
  int RestoreState(FILE* file) override;

private:
  struct Channel {
    u32 address = 0;
    u8 control = 0;
    u32 first = 0;
    u32 last = 0;
    u32 buffer[32]{};
  };

  u32 read_word(u32 offset);
  void write_word(u32 offset, u32 value, u32 mask);
  void refresh_controller();
  void service_dma();
  void dma_write_byte(u8 data);
  u8 dma_read_byte();
  void advance_dma_address();
  void fill_read_buffer();
  Pelican& board;
  CSCSIBus* scsi;
  ncr53c94_device asc;
  u32 cir = 0;
  u32 imer = 0;
  Channel channel[2]{};
  u8 lane = 0;
  u8 buffer_index = 0;
  u8 prefetched = 0;
  bool first_partial = false;
  bool dma_active = false;
  bool pending_irq = false;
  std::chrono::steady_clock::time_point device_sample{};
};

#endif
