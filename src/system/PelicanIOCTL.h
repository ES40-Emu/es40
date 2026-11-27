/* ES40 emulator -- DEC 3000 Model 300X IOCTL flash window and SCC interface
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

#ifndef ES40_PELICAN_IOCTL_H
#define ES40_PELICAN_IOCTL_H

#include "chipset/Intel28F020.h"
#include "chipset/Z85C30.h"
#include "emu/SystemComponent.h"
#include "io/VSXXXMouse.h"
#include <array>
#include <condition_variable>
#include <optional>
#include <thread>

class PelicanIOCTL : public CSystemComponent
{
public:
  PelicanIOCTL(CConfigurator* cfg, CSystem* system);
  ~PelicanIOCTL() override;
  MouseInput* mouse_input() { return &mouse; }
  bool interrupt() const;
  void load_firmware(char* system_ram, char* io_ram) const;
  std::string snapshot_identity() const override;
  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;
  void ResetPCI() override;
  void start_threads() override;
  void stop_threads() override;
  void prepare_snapshot() override;
  int RestoreState(FILE* file) override;
  void finalize_restore() noexcept override;

private:
  unsigned selected_bank() const noexcept;
  u32 read_word(u32 offset) const;
  u32 read_register(int index, u32 offset, u32 lanes);
  void write_register(int index, u32 offset, u32 value, u32 mask);
  void refresh_mouse();
  void update_irq();
  void run_mouse();

  // Each configured image corresponds to the literal SSR<9,7> chip select (EK-D3SYS-PM, §7.3.11).
  std::array<std::optional<Intel28F020>, 4> chips;
  u32 support = 0;
  u32 interrupt_mask = 0;
  Z85C30 scc;
  VSXXXMouse mouse;
  bool irq_asserted = false;
  std::thread mouse_thread;
  std::condition_variable_any mouse_wakeup;
  u64 mouse_clock_ns = 0;
  std::chrono::steady_clock::time_point mouse_sample{};
};

#endif
