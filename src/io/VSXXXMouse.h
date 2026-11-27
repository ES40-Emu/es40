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

#ifndef ES40_VSXXXMOUSE_H
#define ES40_VSXXXMOUSE_H

#include <cstdint>
#include "datatypes.h"
#include "MouseInput.h"
#include <mutex>

class VSXXXMouse : public MouseInput
{
public:
  // One start, eight data, odd parity and one stop bit at 4800 baud (VCB02 TM C.6.1).
  static constexpr s64 frame_ns = (11LL * 1000000000 + 4799) / 4800;
  struct State
  {
    u64 stream_wait_ns;
    s64 wire_wait_ns;
    s32 dx, dy;
    u8 buttons, buttons_changed, streaming, testing;
    u8 report[4], position, length;
  };

  void reset();
  void command(u8 byte);
  bool next_byte(u8& byte, u64 now_ns);
  void rebase_clock(u64 now_ns);
  void prepare_snapshot() { collect_motion(); }
  bool valid_state() const;
  State& state() { return device; }

  void mouse_motion(int dx, int dy, int dz, unsigned buttons) override;
  void set_mouse_capture(bool captured) override;

private:
  void collect_motion();
  void start_self_test();
  void position_report();

  State device{};
  u64 last_ns = 0;
  std::mutex input_mutex;
  bool captured = false, buttons_changed = false;
  s32 pending_x = 0, pending_y = 0;
  u8 host_buttons = 0;
};

#endif
