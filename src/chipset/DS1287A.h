/* ES40 emulator -- Dallas DS1287A real-time clock and battery-backed RAM
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

#ifndef ES40_DS1287A_H
#define ES40_DS1287A_H

#include <cstdint>
#include "datatypes.h"
#include <string>

class DS1287A
{
public:
  struct State
  {
    u8 regs[64]{};
    u8 visible[10]{};
    u32 phase_ns = 0;
    bool fall_back = false;
    bool reset_held = true;
  } state;

  DS1287A();
  u8 read(unsigned reg);
  void write(unsigned reg, u8 value);
  void set_reset(bool held);
  void advance(u64 elapsed_ns);
  bool interrupt() const;
  u64 next_irq_ns() const;
  bool valid_state() const;
  void load_battery(const std::string& path);
  void save_battery();
  void restored() { dirty = true; }

private:
  bool running() const;
  std::string battery_path;
  bool dirty = false;
};

#endif
