/* ES40 emulator -- DEC LK401 workstation keyboard
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
#include "LK401.h"
#include "gui/gui.h"
#include <algorithm>
#include <iterator>

enum KeyMode : u8 { Down, Repeat, RepeatUp, DownUp };

static u8 matrix_code(u32 key, bool extended)
{
  // Matrix positions and key divisions come from VCB02 TM Table B-3.
  static constexpr u8 letters[] = {
    0xc2, 0xd9, 0xce, 0xcd, 0xcc, 0xd2, 0xd8, 0xdd, 0xe6, 0xe2, 0xe7, 0xec, 0xe3,
    0xde, 0xeb, 0xf0, 0xc1, 0xd1, 0xc7, 0xd7, 0xe1, 0xd3, 0xc6, 0xc8, 0xdc, 0xc3
  };
  static constexpr u8 digits[] = {0xef, 0xc0, 0xc5, 0xcb, 0xd0, 0xd6, 0xdb, 0xe0, 0xe5, 0xea};
  if (key >= BX_KEY_A && key <= BX_KEY_Z) return letters[key - BX_KEY_A];
  if (key >= BX_KEY_0 && key <= BX_KEY_9) return digits[key - BX_KEY_0];
  if (key >= BX_KEY_F1 && key <= BX_KEY_F5) return u8(0x56 + key - BX_KEY_F1);
  if (key >= BX_KEY_F6 && key <= BX_KEY_F10) return u8(0x64 + key - BX_KEY_F6);
  switch (key) {
  case BX_KEY_F11: case BX_KEY_ESC: return 0x71;
  case BX_KEY_F12: return 0x72;
  case BX_KEY_SCRL_LOCK: return 0x56;
  case BX_KEY_PRINT: return 0x57;
  case BX_KEY_PAUSE: return 0x5a;
  case BX_KEY_HOME: return 0x8a;
  case BX_KEY_INSERT: return 0x8b;
  case BX_KEY_DELETE: return 0x8c;
  case BX_KEY_END: return 0x8d;
  case BX_KEY_PAGE_UP: return 0x8e;
  case BX_KEY_PAGE_DOWN: return 0x8f;
  case BX_KEY_KP_INSERT: return 0x92;
  case BX_KEY_KP_DELETE: return 0x94;
  case BX_KEY_KP_ENTER: return 0x95;
  case BX_KEY_KP_END: return 0x96;
  case BX_KEY_KP_DOWN: return 0x97;
  case BX_KEY_KP_PAGE_DOWN: return 0x98;
  case BX_KEY_KP_LEFT: return 0x99;
  case BX_KEY_KP_5: return 0x9a;
  case BX_KEY_KP_RIGHT: return 0x9b;
  case BX_KEY_KP_ADD: return 0x9c;
  case BX_KEY_KP_HOME: return 0x9d;
  case BX_KEY_KP_UP: return 0x9e;
  case BX_KEY_KP_PAGE_UP: return 0x9f;
  case BX_KEY_KP_SUBTRACT: return 0xa0;
  case BX_KEY_NUM_LOCK: return 0xa1;
  case BX_KEY_KP_DIVIDE: return 0xa2;
  case BX_KEY_KP_MULTIPLY: return 0xa3;
  case BX_KEY_LEFT: return 0xa7;
  case BX_KEY_RIGHT: return 0xa8;
  case BX_KEY_DOWN: return 0xa9;
  case BX_KEY_UP: return 0xaa;
  case BX_KEY_SHIFT_R: return extended ? 0xab : 0xae;
  case BX_KEY_ALT_L: return extended ? 0xac : 0;
  case BX_KEY_WIN_R: return extended ? 0xad : 0xb1;
  case BX_KEY_SHIFT_L: return 0xae;
  case BX_KEY_CTRL_L: case BX_KEY_CTRL_R: return 0xaf;
  case BX_KEY_CAPS_LOCK: return 0xb0;
  case BX_KEY_WIN_L: return 0xb1;
  case BX_KEY_ALT_R: return extended ? 0xb2 : 0;
  case BX_KEY_BACKSPACE: return 0xbc;
  case BX_KEY_ENTER: return 0xbd;
  case BX_KEY_TAB: return 0xbe;
  case BX_KEY_GRAVE: return 0xbf;
  case BX_KEY_LEFT_BACKSLASH: return 0xc9;
  case BX_KEY_SPACE: return 0xd4;
  case BX_KEY_COMMA: return 0xe8;
  case BX_KEY_PERIOD: return 0xed;
  case BX_KEY_SEMICOLON: return 0xf2;
  case BX_KEY_SLASH: return 0xf3;
  case BX_KEY_EQUALS: return 0xf5;
  case BX_KEY_RIGHT_BRACKET: return 0xf6;
  case BX_KEY_BACKSLASH: return 0xf7;
  case BX_KEY_MINUS: return 0xf9;
  case BX_KEY_LEFT_BRACKET: return 0xfa;
  case BX_KEY_SINGLE_QUOTE: return 0xfb;
  default: return 0;
  }
}

static unsigned division(u8 code)
{
  if (code >= 0xbf) return 1;
  if (code >= 0xbd) return 4;
  if (code == 0xbc) return 3;
  if (code >= 0xab) return code == 0xb0 || code == 0xb1 || code == 0xad ? 5 : 6;
  if (code >= 0xa9) return 8;
  if (code >= 0xa7) return 7;
  if (code >= 0x92) return 2;
  if (code >= 0x8a) return 9;
  if (code >= 0x80) return 14;
  if (code >= 0x7c) return 13;
  if (code >= 0x71) return 12;
  return code >= 0x64 ? 11 : 10;
}

static bool repeats(u8 mode)
{
  return mode == Repeat || mode == RepeatUp;
}

static constexpr u64 parameter_timeout_ns = 100000000; // VCB02 TM §B.5.6.1.

void LK401::defaults()
{
  // Shift and Ctrl are division six on the workstation console (EK-D3SYS-PM §16.1.4).
  std::fill(std::begin(device.mode), std::end(device.mode), Down);
  for (unsigned i : {1, 2, 3, 7, 8}) device.mode[i] = Repeat;
  for (unsigned i : {6, 9}) device.mode[i] = DownUp;
  // The four repeat buffers have independent delays and rates (VCB02 TM Tables B-10–11).
  std::fill(std::begin(device.buffer), std::end(device.buffer), 0);
  for (unsigned i : {3, 7, 8}) device.buffer[i] = 1;
  for (unsigned i = 0; i < 4; ++i) {
    device.delay[i] = (i & 1) ? 60 : 100;
    device.rate[i] = (i & 2) ? 40 : 30;
  }
  device.repeat_enabled = 1;
  device.repeat_inhibit = 0;
  device.click = device.bell = 2;
  device.ctrl_click = 0;
  set_repeat(0);
}

void LK401::queue(u8 byte)
{
  const size_t capacity = device.locked ? 4 : sizeof(device.fifo);
  if (device.count >= capacity) {
    device.overflow = 1;
    return;
  }
  if (!device.count && !device.locked_ack)
    device.wire_wait_ns = frame_ns;
  device.fifo[device.count++] = byte;
  device.last_code = byte;
}

void LK401::power_up_reply()
{
  // Self-test uses the compatible power-up ID and reports a held key (LK401 PS §3.3.4.6).
  queue(1);
  queue(0);
  queue(device.held_count ? 0x3d : 0);
  queue(device.held_count ? device.held_code[0] : 0);
  device.stuck = device.held_count != 0;
}

void LK401::reset()
{
  std::lock_guard<std::mutex> lock(input_mutex);
  input.clear();
  device = {};
  defaults();
  for (unsigned i = 0; i < BX_KEY_NBKEYS; ++i) {
    const u8 code = matrix_code(i, false);
    if (host_down[i] && code) {
      device.held[device.held_count] = u8(i);
      device.held_code[device.held_count++] = code;
    }
  }
  power_up_reply();
}

void LK401::gen_scancode(u32 key)
{
  const u32 base = key & ~BX_KEY_RELEASED;
  if (base >= BX_KEY_NBKEYS) return;
  const bool down = !(key & BX_KEY_RELEASED);
  std::lock_guard<std::mutex> lock(input_mutex);
  if (host_down[base] != down) {
    host_down[base] = down;
    input.push_back(key);
  }
}

void LK401::prepare_snapshot()
{
  std::lock_guard<std::mutex> lock(input_mutex);
  for (u32 key : input)
    key_event(key);
  input.clear();
}

void LK401::rebase_clock(u64 now_ns)
{
  last_ns = now_ns;
  std::lock_guard<std::mutex> lock(input_mutex);
  input.clear();
  for (unsigned key = 0; key < BX_KEY_NBKEYS; ++key) {
    const auto end = device.held + device.held_count;
    const bool held = std::find(device.held, end, key) != end;
    if (held != host_down[key])
      input.push_back(host_down[key] ? key : key | BX_KEY_RELEASED);
  }
}

void LK401::set_repeat(u8 code)
{
  device.repeat_key = code;
  device.repeat_wait_ns = code ? u64(device.delay[device.buffer[division(code)]]) * 5000000 : 0;
}

void LK401::key_event(u32 key)
{
  const u8 base = u8(key);
  u8* end = device.held + device.held_count;
  u8* match = std::find(device.held, end, base);
  u8 code;
  const bool down = !(key & BX_KEY_RELEASED);
  if (down) {
    code = matrix_code(base, device.extended != 0);
    if (!code || match != end) return;
    device.held[device.held_count] = base;
    device.held_code[device.held_count++] = code;
  } else {
    if (match == end) return;
    u8* position = device.held_code + (match - device.held);
    code = *position;
    std::move(position + 1, device.held_code + device.held_count, position);
    std::move(match + 1, end, match);
    --device.held_count;
  }
  if (device.stuck) {
    if (!device.held_count) power_up_reply();
    return;
  }
  if (device.testing) return;
  // PC keys can share one LK switch, including both shifts in compatibility mode.
  const u8* other_end = device.held_code + device.held_count - unsigned(down);
  if (std::find(static_cast<const u8*>(device.held_code), other_end, code) != other_end) return;

  const u8 mode = device.mode[division(code)];
  if (down) {
    queue(code);
    device.repeat_inhibit = 0;
    if (repeats(mode)) set_repeat(code);
  } else {
    if (mode == DownUp || mode == RepeatUp) {
      const bool other = std::any_of(device.held_code, device.held_code + device.held_count, [this](u8 c) {
        return device.mode[division(c)] >= RepeatUp;
      });
      queue(other ? code : 0xb3);
    }
    if (device.repeat_key == code) {
      set_repeat(0);
      for (unsigned j = device.held_count; j > 0; --j) {
        const u8 earlier = device.held_code[j - 1];
        if (repeats(device.mode[division(earlier)])) {
          queue(earlier);
          set_repeat(earlier);
          break;
        }
      }
    }
  }
}

void LK401::set_mode(u8 div, u8 mode)
{
  const u8 previous = device.mode[div];
  device.mode[div] = mode;
  queue(0xba);
  for (unsigned i = 0; i < device.held_count; ++i) {
    const u8 key = device.held_code[i];
    if (division(key) != div) continue;
    if (mode >= RepeatUp && previous < RepeatUp) {
      queue(0xb9); // Prefix to an already-pressed key when entering down/up (VCB02 TM §B.5.4.2).
      queue(key);
    }
    if (repeats(mode)) set_repeat(key);
    else if (device.repeat_key == key) set_repeat(0);
  }
}

void LK401::execute(u8 cmd, const u8* params, unsigned count)
{
  if (!(cmd & 1)) {
    const unsigned div = (cmd >> 3) & 15;
    const u8 mode = (cmd >> 1) & 3;
    // Division 15 programs a repeat buffer with delay in 5 ms units and rate in Hz (VCB02 TM §B.5.5.4).
    if (div == 15 && count == 2 && params[0] && params[0] < 127 && params[1] && params[1] <= 124) {
      device.delay[mode] = params[0];
      device.rate[mode] = params[1];
      return;
    }
    if (div && div < 15 && (mode != RepeatUp || device.extended) &&
        (count == 0 || (count == 1 && repeats(mode) && params[0] < 4))) {
      if (count) device.buffer[div] = params[0];
      set_mode(u8(div), mode);
      return;
    }
  } else if (count == 1) {
    switch (cmd) { // VCB02 TM Table B-7.
    case 0x11: device.leds &= ~(params[0] & 12); return;
    case 0x13: device.leds |= params[0] & 12; return;
    case 0x1b: device.click = params[0] & 7; return;
    case 0x23: device.bell = params[0] & 7; return;
    }
  } else if (count == 0) {
    switch (cmd) {
    case 0x89:
      device.locked = device.locked_ack = 1;
      device.wire_wait_ns = frame_ns;
      return;
    case 0x8b:
      if (device.locked) device.wire_wait_ns = frame_ns;
      device.locked = 0;
      return;
    case 0x99: device.click = 8; return;
    case 0x9f: case 0xa7: return;
    case 0xa1: device.bell = 8; return;
    case 0xab:
      // LK401 firmware ID 02, hardware ID 00 (LK401 PS §3.3.4.5).
      queue(2);
      queue(0);
      return;
    case 0xb9: device.ctrl_click = 0; return;
    case 0xbb: device.ctrl_click = 1; return;
    case 0xc1: device.repeat_inhibit = 1; return;
    case 0xcb: device.testing = 1; set_repeat(0); queue(0xb8); return;
    case 0xd3: defaults(); queue(0xba); return;
    case 0xd9:
      for (auto& mode : device.mode)
        if (repeats(mode)) mode = Down;
      set_repeat(0);
      return;
    case 0xe1: device.repeat_enabled = 0; return;
    case 0xe3: device.repeat_enabled = 1; return;
    case 0xe9: device.extended = 1; queue(0xba); return;
    }
  }
  queue(0xb6);
}

void LK401::command(u8 byte)
{
  if (byte == 0xfd || (device.testing && byte == 0x80)) {
    reset();
    return;
  }
  if (device.testing) return;
  // Bit 7 terminates a sequence of one command and up to two parameters (VCB02 TM §B.5.5).
  if (!(byte & 0x80)) {
    if (device.pending_count < sizeof(device.pending)) {
      device.pending[device.pending_count++] = byte;
      device.parameter_wait_ns = parameter_timeout_ns;
      return;
    }
    queue(0xb6);
  } else if (device.pending_count) {
    const u8 params[] = {device.pending_count == 1 ? u8(byte & 0x7f) : device.pending[1], u8(byte & 0x7f)};
    execute(device.pending[0], params, device.pending_count);
  } else {
    execute(byte, nullptr, 0);
  }
  device.pending_count = 0;
  device.parameter_wait_ns = 0;
}

bool LK401::next_byte(u8& byte, u64 now_ns)
{
  const u64 elapsed = now_ns >= last_ns ? now_ns - last_ns : 0;
  last_ns = now_ns;
  if (device.locked_ack || (!device.locked && device.count)) {
    const s64 ticks = static_cast<s64>((std::min<u64>)(elapsed, sizeof(device.fifo) * frame_ns));
    device.wire_wait_ns = (std::max<s64>)(-s64(sizeof(device.fifo)) * frame_ns, device.wire_wait_ns - ticks);
  }
  if (device.pending_count) {
    device.parameter_wait_ns -= (std::min)(device.parameter_wait_ns, elapsed);
    if (!device.parameter_wait_ns) {
      device.pending_count = 0;
      queue(0xb6);
    }
  }
  if (device.repeat_key) {
    device.repeat_wait_ns -= (std::min)(device.repeat_wait_ns, elapsed);
    if (!device.repeat_wait_ns) {
      // Retransmit the repeating key after an intervening nonrepeat code (VCB02 TM §B.5.2.1).
      if (device.repeat_enabled && !device.repeat_inhibit)
        queue(device.last_code == 0xb4 || device.last_code == device.repeat_key ? 0xb4 : device.repeat_key);
      device.repeat_wait_ns = 1000000000ULL / device.rate[device.buffer[division(device.repeat_key)]];
    }
  }
  prepare_snapshot();
  if (device.wire_wait_ns > 0) return false;
  if (device.locked_ack) {
    byte = 0xb7;
    device.locked_ack = 0;
  } else {
    if (device.locked || !device.count) return false;
    byte = device.fifo[0];
    std::move(device.fifo + 1, device.fifo + device.count, device.fifo);
    --device.count;
    if (device.overflow) {
      device.overflow = 0;
      device.fifo[device.count++] = 0xb5; // Lost output follows the buffered keycodes (VCB02 TM §B.5.6.2).
    }
  }
  device.wire_wait_ns += frame_ns;
  return true;
}

bool LK401::valid_state() const
{
  if (device.held_count > BX_KEY_NBKEYS || device.count > sizeof(device.fifo) ||
      device.pending_count > sizeof(device.pending) || device.leds & ~12 ||
      device.click > 8 || device.bell > 8 || device.ctrl_click > 1 ||
      device.locked > 1 || device.locked_ack > 1 || device.overflow > 1 || device.stuck > 1 ||
      device.testing > 1 || device.extended > 1 || device.repeat_enabled > 1 || device.repeat_inhibit > 1)
    return false;
  if (device.parameter_wait_ns > parameter_timeout_ns || device.repeat_wait_ns > 1000000000ULL ||
      device.wire_wait_ns < -s64(sizeof(device.fifo)) * frame_ns || device.wire_wait_ns > frame_ns)
    return false;
  for (unsigned i = 0; i < 15; ++i)
    if (device.mode[i] > DownUp || device.buffer[i] >= 4) return false;
  for (unsigned i = 0; i < 4; ++i)
    if (!device.delay[i] || device.delay[i] > 126 || !device.rate[i] || device.rate[i] > 124) return false;
  bool down[128]{};
  for (unsigned i = 0; i < device.held_count; ++i) {
    const u8 key = device.held[i], code = device.held_code[i];
    if (key >= BX_KEY_NBKEYS || down[key] || !code ||
        (matrix_code(key, false) != code && matrix_code(key, true) != code)) return false;
    down[key] = true;
  }
  return true;
}
