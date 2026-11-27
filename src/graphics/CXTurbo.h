/* ES40 emulator -- DEC 3000 CXTurbo graphics
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

#ifndef ES40_CXTURBO_H
#define ES40_CXTURBO_H

#include "emu/SystemComponent.h"
#include "graphics/Bt459.h"
#include "graphics/SFB.h"
#include "gui/gui.h"
#include <chrono>
#include <condition_variable>
#include <exception>
#include <thread>

class Pelican;

class CXTurbo final : public CSystemComponent, public CDisplayOutputProvider
{
public:
  CXTurbo(CConfigurator* cfg, Pelican& system);
  ~CXTurbo() override;
  void bind_display();
  void init() override;
  bool interrupt() const { return sfb.interrupt(); }
  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 value) override;
  void ResetPCI() override;
  void start_threads() override;
  void stop_threads() override;
  void check_state() override;
  int RestoreState(FILE* file) override;

private:
  using Clock = std::chrono::steady_clock;
  u64 clock_ns() const;
  u32 read_word(int index, u32 offset);
  void write_word(int index, u32 offset, u32 value);
  void run();
  void scanout(bitmap_rgb32& bitmap, unsigned width, unsigned height);

  Pelican& board;
  decsfb_device sfb;
  bt459_device dac;
  address_map dac_map{4};
  screen_device screen;
  std::vector<u8> wrapped_pixels;
  u8 gp_outputs[2]{};
  u64 frame = 0;
  u64 elapsed_ns = 0;
  u64 next_retrace_ns = 0;
  Clock::time_point resume{};
  bool dirty = true;
  std::condition_variable_any wake;
  std::thread worker;
  std::exception_ptr failure;
};

#endif
