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

#include "StdAfx.h"
#include "graphics/CXTurbo.h"
#include "system/System_pelican.h"
#include "es40_endian.h"

enum CXTurboRange { Asic, Ramdac, Display, Alternate, Offscreen, DisplayAlias, OffscreenAlias, GP0, GP1 };

CXTurbo::CXTurbo(CConfigurator* cfg, Pelican& system)
  : CSystemComponent(cfg, &system), CDisplayOutputProvider(0), board(system),
    sfb(machine_config{}, "sfb", nullptr, 25000000), dac(machine_config{}, "ramdac", nullptr, 0)
{
  // Onboard CXTurbo responds only in slot 4 dense space (EK-D3SYS-PM, §6.1 and Table 14).
  board.RegisterMemory(this, Asic, U64(0x1c2100000), 0x20000);
  board.RegisterMemory(this, GP0, U64(0x1c2140000), 0x40000);
  board.RegisterMemory(this, GP1, U64(0x1c2180000), 0x40000);
  board.RegisterMemory(this, Ramdac, U64(0x1c21c0000), 0x40000);
  for (int index = Display; index <= OffscreenAlias; ++index)
    board.RegisterMemory(this, index, U64(0x1c2200000) + (index - Display) * decsfb_device::vram_size, decsfb_device::vram_size);
  sfb.set_save_owner(*this);
  dac.set_save_owner(*this);
  sfb.set_time_source([this] { return clock_ns(); });
  sfb.int_cb().set([this](int) { board.update_io_irq(); });
  dac.map(dac_map);
  save_item(elapsed_ns, "elapsed_ns");
  save_item(frame, "frame");
  save_item(gp_outputs, "gp_outputs");
  save_item(next_retrace_ns, "next_retrace_ns");
}

CXTurbo::~CXTurbo()
{
  if (worker.joinable())
    stop_threads();
}

void CXTurbo::init()
{
  sfb.start();
  dac.start();
  ResetPCI();
}

void CXTurbo::bind_display()
{
  if (bx_gui && !m_output.bound())
    m_output.bind(bx_gui->display_for_output(get_device_path(), 0), myCfg);
}

bool CXTurbo::decodes_memory_access(int index, u64 offset, int dsize, bool write) const noexcept
{
  if (!write && (index == GP0 || index == GP1))
    return false;
  return (dsize == 32 || dsize == 64) && (offset & (dsize / 8 - 1)) == 0;
}

u64 CXTurbo::clock_ns() const
{
  if (resume == Clock::time_point{})
    return elapsed_ns;
  const auto now = board.device_access_time();
  return elapsed_ns + (now > resume ? std::chrono::duration_cast<std::chrono::nanoseconds>(now - resume).count() : 0);
}

u32 CXTurbo::read_word(int index, u32 offset)
{
  if (index == Asic)
    return sfb.read(offset >> 2);
  if (index == Ramdac)
    return dac_map.read_byte((offset >> 2) & 3); // Only D<7:0> connects to the Bt459 (EK-D3SYS-PM, §6.5).
  if (index == Offscreen || index == OffscreenAlias)
    return 0xffffffff; // The second VRAM bank is unpopulated (DEC WRL 93/1, §5.3).
  return sfb.vram_r(offset >> 2);
}

void CXTurbo::write_word(int index, u32 offset, u32 value)
{
  if (index == GP0 || index == GP1) {
    gp_outputs[index - GP0] = value & 1; // GP0 and GP1 each latch one output bit (EK-D3SYS-PM, Table 14).
    return;
  }
  if (index == Asic) {
    const u64 previous = sfb.frame_period_ns();
    sfb.write(offset >> 2, value);
    const u64 period = sfb.frame_period_ns();
    if (previous != period) {
      next_retrace_ns = period ? clock_ns() + period : 0;
      wake.notify_one();
    }
  } else if (index == Ramdac) {
    dac.set_frame_number(frame);
    dac_map.write_byte((offset >> 2) & 3, static_cast<u8>(value));
  } else if (index != Offscreen && index != OffscreenAlias) {
    sfb.vram_w((offset + (index - Display + 1) * decsfb_device::vram_size) >> 2, value);
  } else {
    return;
  }
  dirty = true;
}

u64 CXTurbo::ReadMem(int index, u64 address, int dsize)
{
  u64 value = read_word(index, static_cast<u32>(address));
  if (dsize == 64)
    value |= u64(read_word(index, static_cast<u32>(address) + 4)) << 32;
  return value;
}

void CXTurbo::WriteMem(int index, u64 address, int dsize, u64 value)
{
  write_word(index, static_cast<u32>(address), static_cast<u32>(value));
  if (dsize == 64)
    write_word(index, static_cast<u32>(address) + 4, static_cast<u32>(value >> 32));
}

void CXTurbo::ResetPCI()
{
  sfb.reset();
  frame = 0;
  dac.set_frame_number(frame);
  dac.reset();
  gp_outputs[0] = gp_outputs[1] = 0;
  next_retrace_ns = 0;
  dirty = true;
  board.update_io_irq();
}

int CXTurbo::RestoreState(FILE* file)
{
  if (CSystemComponent::RestoreState(file) || !dac.valid_state())
    return -1;
  dirty = true;
  return 0;
}

void CXTurbo::scanout(bitmap_rgb32& bitmap, unsigned width, unsigned height)
{
  if (unsigned(bitmap.width()) != width || unsigned(bitmap.height()) != height) {
    bitmap.resize(width, height);
    screen.set_visible_area(width, height);
  }
  // CXTurbo cursor alignment follows DEC OSF/1 sfb_bot and Bt459 p. 4-160.
  const u32 h = sfb.read(0x64 >> 2), v = sfb.read(0x68 >> 2);
  const int hsync_to_active = 4 * (((h >> 14) & 127) + ((h >> 21) & 127)) - 3;
  const int vsync_to_active = ((v >> 11) & 31) + ((v >> 16) & 63) + ((v >> 22) & 63);
  dac.set_scanout(hsync_to_active, vsync_to_active);
  screen.set_frame_number(frame);
  const u32 base = sfb.video_base();
  const size_t bytes = size_t((width * dac.bits_per_pixel() + 7) / 8) * height;
  u8* data;
#ifdef ES40_LITTLE_ENDIAN
  if (base + bytes <= decsfb_device::vram_size)
    data = reinterpret_cast<u8*>(sfb.get_vram()) + base;
  else
#endif
  {
    wrapped_pixels.resize(bytes);
    for (size_t i = 0; i < bytes; ++i)
      wrapped_pixels[i] = sfb.pixel((base + u32(i)) & (decsfb_device::vram_size - 1));
    data = wrapped_pixels.data();
  }
  dac.screen_update(screen, bitmap, bitmap.cliprect(), data);
}

void CXTurbo::run()
{
  try {
    if (m_output.bound())
      m_output.initialize(X_TILESIZE, Y_TILESIZE);
    bitmap_rgb32 bitmap;
    std::unique_lock<std::recursive_mutex> lock(board.get_device_bus_mutex());
    while (resume != Clock::time_point{}) {
      const u64 now = clock_ns(), period = sfb.frame_period_ns();
      u64 retraces = 0;
      if (period && now >= next_retrace_ns) {
        retraces = 1 + (now - next_retrace_ns) / period;
        next_retrace_ns += retraces * period;
        frame += retraces;
        sfb.vertical_blank();
      }
      // Keep host input responsive when video is blank or its programmed frame is long.
      const u64 delay = period ? std::clamp<u64>(next_retrace_ns - now, 1000000, 16000000) : 16000000;
      const auto next = Clock::now() + std::chrono::nanoseconds(delay);
      const unsigned width = sfb.width(), height = sfb.height();
      const bool active = sfb.video_valid() && width && height;
      const bool present = m_output.bound() && (dirty || retraces > 1 || (retraces && dac.blink_changed(frame)));
      if (present && active)
        scanout(bitmap, width, height);
      dirty = false;
      lock.unlock();
      if (m_output.bound()) {
        std::lock_guard<bx_gui_c> display_lock(m_output.display());
        m_output.display().handle_events();
        if (present) {
          if (active)
            m_output.present_argb32(bitmap.raw(), width, height);
          else
            m_output.clear_screen();
          m_output.display().flush();
        }
      }
      lock.lock();
      if (resume != Clock::time_point{})
        wake.wait_until(lock, next);
    }
  } catch (...) {
    std::lock_guard<std::recursive_mutex> lock(board.get_device_bus_mutex());
    failure = std::current_exception();
  }
}

void CXTurbo::start_threads()
{
  std::lock_guard<std::recursive_mutex> lock(board.get_device_bus_mutex());
  if (!worker.joinable()) {
    resume = Clock::now();
    dirty = true;
    worker = std::thread(&CXTurbo::run, this);
  }
}

void CXTurbo::stop_threads()
{
  {
    std::lock_guard<std::recursive_mutex> lock(board.get_device_bus_mutex());
    if (resume != Clock::time_point{}) {
      elapsed_ns = clock_ns();
      resume = {};
      wake.notify_all();
    }
  }
  if (worker.joinable())
    worker.join();
}

void CXTurbo::check_state()
{
  std::lock_guard<std::recursive_mutex> lock(board.get_device_bus_mutex());
  if (failure)
    std::rethrow_exception(failure);
}
