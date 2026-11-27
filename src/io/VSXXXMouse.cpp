/* ES40 emulator -- DEC VSXXX workstation mouse
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

#include "VSXXXMouse.h"
#include <algorithm>
#include <cstdlib>
#include <limits>

static constexpr u64 stream_interval_ns = 1000000000ULL / 55;

static s32 add_motion(s32 previous, int delta)
{
  return static_cast<s32>(std::clamp<s64>(s64(previous) + delta,
    (std::numeric_limits<s32>::min)(), (std::numeric_limits<s32>::max)()));
}

void VSXXXMouse::set_mouse_capture(bool enabled)
{
  std::lock_guard<std::mutex> lock(input_mutex);
  captured = enabled;
  if (!captured && host_buttons)
  {
    buttons_changed = true;
    host_buttons = 0;
  }
}

void VSXXXMouse::mouse_motion(int dx, int dy, int, unsigned buttons)
{
  std::lock_guard<std::mutex> lock(input_mutex);
  if (!captured) return;
  pending_x = add_motion(pending_x, dx);
  pending_y = add_motion(pending_y, dy);
  // Left, middle and right occupy bits 2, 1 and 0 (VCB02 TM Fig. C-4).
  const u8 current = u8(((buttons & 1) << 2) | ((buttons & 4) >> 1) | ((buttons & 2) >> 1));
  buttons_changed |= host_buttons != current;
  host_buttons = current;
}

void VSXXXMouse::collect_motion()
{
  std::lock_guard<std::mutex> lock(input_mutex);
  device.dx = add_motion(device.dx, pending_x);
  device.dy = add_motion(device.dy, pending_y);
  pending_x = pending_y = 0;
  if (buttons_changed)
  {
    device.buttons = host_buttons;
    device.buttons_changed = 1;
    buttons_changed = false;
  }
}

void VSXXXMouse::start_self_test()
{
  const u8 buttons = device.buttons;
  device = {};
  device.buttons = buttons;
  // Device 2 identifies a mouse and held buttons produce diagnostic 3D (VCB02 TM C.6.5).
  device.report[0] = 0xa0;
  device.report[1] = 0x02;
  device.report[2] = buttons ? 0x3d : 0;
  device.report[3] = buttons;
  device.length = 4;
  device.testing = 1;
  device.wire_wait_ns = frame_ns;
}

void VSXXXMouse::reset()
{
  collect_motion();
  start_self_test();
}

void VSXXXMouse::position_report()
{
  const int dx = std::clamp<int>(device.dx, -127, 127);
  const int dy = std::clamp<int>(device.dy, -127, 127);
  // Bits 4 and 3 mark right and up, with magnitudes in bytes 2 and 3 (VCB02 TM Fig. C-4).
  device.report[0] = u8(0x80 | (dx > 0 ? 0x10 : 0) | (dy > 0 ? 0x08 : 0) | device.buttons);
  device.report[1] = u8(std::abs(dx));
  device.report[2] = u8(std::abs(dy));
  device.position = 0;
  device.length = 3;
  device.dx = device.dy = 0;
  device.buttons_changed = 0;
  device.wire_wait_ns = frame_ns;
}

void VSXXXMouse::command(u8 byte)
{
  if (device.testing) return;
  // An incoming byte aborts a position report and its high bit is ignored (VCB02 TM C.6.1).
  device.position = device.length = 0;
  collect_motion();
  switch (byte & 0x7f) // VCB02 TM Table C-2.
  {
  case 'R': device.streaming = 1; break;
  case 'D': device.streaming = 0; break;
  case 'P': device.streaming = 0; position_report(); break;
  case 'T': start_self_test(); break;
  }
}

bool VSXXXMouse::next_byte(u8& byte, u64 now_ns)
{
  const u64 elapsed = now_ns >= last_ns ? now_ns - last_ns : 0;
  last_ns = now_ns;
  collect_motion();
  device.stream_wait_ns -= (std::min)(device.stream_wait_ns, elapsed);
  if (device.position == device.length)
  {
    if (!device.streaming || device.stream_wait_ns) return false;
    if (!device.dx && !device.dy && !device.buttons_changed) return false;
    position_report();
    // Incremental reports are limited to 55 per second (VCB02 TM C.6.3).
    device.stream_wait_ns = stream_interval_ns;
  }
  else
  {
    const s64 ticks = static_cast<s64>((std::min<u64>)(elapsed, 5 * frame_ns));
    device.wire_wait_ns = (std::max)(-4 * frame_ns, device.wire_wait_ns - ticks);
  }
  if (device.wire_wait_ns > 0) return false;
  byte = device.report[device.position++];
  device.wire_wait_ns += frame_ns;
  if (device.position == device.length) device.testing = 0;
  return true;
}

void VSXXXMouse::rebase_clock(u64 now_ns)
{
  std::lock_guard<std::mutex> lock(input_mutex);
  last_ns = now_ns;
  pending_x = pending_y = 0;
  buttons_changed = host_buttons != device.buttons;
}

bool VSXXXMouse::valid_state() const
{
  return (device.length == 0 || device.length == 3 || device.length == 4)
    && device.position <= device.length && device.buttons < 8
    && device.streaming <= 1 && device.buttons_changed <= 1 && device.testing <= 1
    && device.testing == (device.length == 4 && device.position < device.length)
    && device.stream_wait_ns <= stream_interval_ns
    && device.wire_wait_ns >= -4 * frame_ns && device.wire_wait_ns <= frame_ns;
}
