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

#ifndef ES40_LK401_H
#define ES40_LK401_H

#include "KeyboardInput.h"
#include <deque>
#include <mutex>

class LK401 : public KeyboardInput
{
public:
  // Ten bit times per character at 4800 baud (LK401 PS §§3.5.4–5).
  static constexpr s64 frame_ns = (10LL * 1000000000 + 4799) / 4800;
  struct State
  {
    u8 mode[15], buffer[15], delay[4], rate[4];
    u8 held[128], held_code[128], held_count;
    u8 deferred[128], deferred_count;
    u8 fifo[32], count, locked_ack, locked, overflow;
    u8 repeat_key, repeat_enabled, repeat_inhibit, last_code;
    u8 pending[2], pending_count, extended, testing, stuck;
    u8 leds, click, ctrl_click, bell;
    u64 repeat_wait_ns, parameter_wait_ns;
    s64 wire_wait_ns;
  };

  void reset();
  void command(u8 byte);
  bool next_byte(u8& byte, u64 now_ns);
  void rebase_clock(u64 now_ns);
  void prepare_snapshot() { collect_input(); }
  bool valid_state() const;
  State& state() { return device; }
  void gen_scancode(u32 key) override;

private:
  void collect_input();
  void defaults();
  void power_up_reply();
  void queue(u8 byte);
  void key_event(u32 key);
  void set_repeat(u8 code);
  void set_mode(u8 division, u8 mode);
  void execute(u8 cmd, const u8* params, unsigned count);

  State device{};
  u64 last_ns = 0;
  std::mutex input_mutex;
  std::deque<u32> input;
  bool host_down[128]{};
};

#endif
