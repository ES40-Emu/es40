/* ES40 emulator -- DEC smart frame buffer ASIC.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) R. Belmont (MAME src/mame/dec/sfb.h and sfb.cpp)
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
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

#ifndef ES40_SFB_H
#define ES40_SFB_H

#include "emu/mame_shims.h"
#include <memory>

class decsfb_device : public device_t
{
public:
  decsfb_device(const machine_config& mconfig, const char* tag, device_t* owner, u32 clock);
  auto int_cb() { return m_int_cb.bind(); }

  u32 read(offs_t offset);
  void write(offs_t offset, u32 data, u32 mem_mask = ~0u);
  u32 vram_r(offs_t offset);
  void vram_w(offs_t offset, u32 data, u32 mem_mask = ~0u);
  u32* get_vram() { return m_vram.get(); }
  const u32* get_vram() const { return m_vram.get(); }

  static constexpr u32 vram_size = 2u << 20;
  u8 pixel(u32 offset) const;
  u32 width() const;
  u32 height() const;
  u32 video_base() const;
  bool video_valid() const;
  u64 frame_period_ns() const;
  bool interrupt() const;
  void vertical_blank();
  void set_time_source(std::function<u64()> now) { m_now = std::move(now); }

protected:
  void device_start() override ATTR_COLD;
  void device_reset() override ATTR_COLD;

private:
  void paint(u32 address, u8 source, u8 mask = 0xff);
  void stipple(u32 address, u32 pattern);
  void line(u32 address, u32 pattern);
  void copy(u32 address, u32 mask);
  void shift_quad(u32 index, u64 incoming);
  void draw(u32 address, u32 data, u32 mask = ~0u);
  u32 read_vram(u32 offset) const;

  std::unique_ptr<u32[]> m_vram;
  u32 m_regs[32]{};
  int m_copy_src = 1;
  u64 m_residue = 0;
  u64 m_clock_start_ns = 0;
  u32 m_copy_low = 0;
  bool m_measuring_clock = false;
  bool m_irq_pending = false;
  const u32 m_tc_clock;
  std::function<u64()> m_now;
  devcb_write_line m_int_cb;
};

DECLARE_DEVICE_TYPE(DECSFB, decsfb_device)

#endif
