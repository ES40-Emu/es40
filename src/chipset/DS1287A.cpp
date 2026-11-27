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

#include "StdAfx.h"
#include "DS1287A.h"
#include "MC146818Calendar.h"
#include "SnapshotFile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>

static constexpr u32 second_ns = 1000000000;
static constexpr u32 first_update = second_ns / 2;
static constexpr u32 uip_lead_ns = 244000;
enum : u8 { reg_a = 10, reg_b, reg_c, reg_d };
enum : u8 { uf = 0x10, af = 0x20, pf = 0x40, set = 0x80, flags = uf | af | pf };

static u64 wall_time_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

DS1287A::DS1287A()
{
  // RCLR clears the 50 RAM bytes to ones; VRT is factory-set for a good battery (DS1287A pp. 350, 360).
  std::fill(state.regs + reg_d + 1, std::end(state.regs), u8{0xff});
  state.regs[reg_d] = 0x80;
  const auto now = std::time(nullptr);
  std::tm date{};
#ifdef _WIN32
  gmtime_s(&date, &now);
#else
  gmtime_r(&now, &date);
#endif
  MC146818Calendar::set_calendar(state.regs, date);
  std::copy_n(state.regs, sizeof(state.visible), state.visible);
}

bool DS1287A::running() const
{
  // Only DV=010 enables timekeeping; the oscillator is stopped for shipping (DS1287 pp. 346, 348).
  return (state.regs[reg_a] & 0x70) == 0x20;
}

bool DS1287A::interrupt() const
{
  return !state.reset_held && (state.regs[reg_b] & state.regs[reg_c] & flags);
}

void DS1287A::set_reset(bool held)
{
  if (held && !state.reset_held) {
    // RESET clears the enables, SQWE and flags while timekeeping continues (DS1287 p. 343).
    state.regs[reg_b] &= 0x87;
    state.regs[reg_c] = 0;
    dirty = true;
  }
  state.reset_held = held;
}

void DS1287A::advance(u64 elapsed_ns)
{
  if (!elapsed_ns || !running())
    return;
  const u32 fraction = u32(elapsed_ns % second_ns);
  const u32 phase = state.phase_ns;
  const unsigned hz = mc146818::periodic_hz(state.regs[reg_a]);
  if (hz && (elapsed_ns >= second_ns || u64(phase + fraction) * hz / second_ns != u64(phase) * hz / second_ns))
    state.regs[reg_c] |= pf;

  const u32 since_update = (phase + second_ns - first_update) % second_ns;
  const u64 updates = elapsed_ns / second_ns + (since_update + fraction) / second_ns;
  state.phase_ns = (phase + fraction) % second_ns;
  if (updates) {
    // SET freezes only the user copy; the internal calendar continues (DS1287 pp. 346, 348).
    MC146818Calendar::advance_calendar(state.regs, updates, state.fall_back, MC146818Calendar::AprilSunday::First);
    if (!(state.regs[reg_b] & set)) {
      std::copy_n(state.regs, sizeof(state.visible), state.visible);
      state.regs[reg_c] |= uf;
    }
  }
  if (state.reset_held)
    state.regs[reg_c] = 0;
}

u64 DS1287A::next_irq_ns() const
{
  if (state.reset_held || !running() || interrupt())
    return 0;
  const u8 enables = state.regs[reg_b];
  u64 timeout = 0;
  if ((enables & af) || ((enables & uf) && !(enables & set))) {
    timeout = state.phase_ns < first_update ? first_update - state.phase_ns :
      second_ns - state.phase_ns + first_update;
  }
  const unsigned hz = mc146818::periodic_hz(state.regs[reg_a]);
  if (hz && (enables & pf)) {
    const u64 next = u64(state.phase_ns) * hz / second_ns + 1;
    const u64 periodic = (next * second_ns + hz - 1) / hz - state.phase_ns;
    if (!timeout || periodic < timeout)
      timeout = periodic;
  }
  return timeout;
}

u8 DS1287A::read(unsigned reg)
{
  if (state.reset_held || reg >= sizeof(state.regs))
    return 0xff;
  if (reg < reg_a)
    return state.visible[reg];
  if (reg == reg_a) {
    // The update transfer follows the rising edge of UIP by 244 µs (DS1287 p. 347).
    const bool uip = running() && !(state.regs[reg_b] & set) &&
      state.phase_ns >= first_update - uip_lead_ns && state.phase_ns < first_update;
    return state.regs[reg] | (uip ? 0x80 : 0);
  }
  if (reg == reg_c) {
    const u8 result = state.regs[reg] | (interrupt() ? 0x80 : 0);
    state.regs[reg] = 0;
    return result;
  }
  return state.regs[reg];
}

void DS1287A::write(unsigned reg, u8 value)
{
  if (state.reset_held || reg >= sizeof(state.regs) || reg == reg_c || reg == reg_d)
    return;
  if (reg < reg_a) {
    if (reg == 0)
      value &= 0x7f;
    state.visible[reg] = value;
    state.regs[reg] = value;
    if (reg >= 6 || !(reg & 1))
      state.fall_back = false;
  } else if (reg == reg_a) {
    const bool was_running = running();
    state.regs[reg] = value & 0x7f;
    if (running() != was_running)
      state.phase_ns = 0;
  } else {
    if (reg == reg_b && (value & set) && !(state.regs[reg] & set))
      value &= ~uf; // SET clears UIE on its rising edge (DS1287 p. 349).
    if (state.regs[reg] == value)
      return;
    state.regs[reg] = value;
  }
  dirty = true;
}

bool DS1287A::valid_state() const
{
  return state.phase_ns < second_ns && !(state.regs[reg_a] & 0x80) && !(state.regs[reg_c] & ~flags) &&
    state.regs[reg_d] == 0x80 && !(state.regs[0] & 0x80) && !(state.visible[0] & 0x80) &&
    (!state.reset_held || (!(state.regs[reg_b] & 0x78) && !state.regs[reg_c]));
}

void DS1287A::load_battery(const std::string& path)
{
  if (path.empty())
    return;
  battery_path = std::filesystem::absolute(path).string();
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(battery_path.c_str(), "rb"), &fclose);
  if (!file) {
    if (errno != ENOENT)
      FAILURE(IO, "Cannot open DS1287A battery image: " + battery_path);
    dirty = true;
    return;
  }
  char magic[4];
  u64 stamp;
  u8 fall_back;
  if (fread(magic, sizeof(magic), 1, file.get()) != 1 || std::memcmp(magic, "DSA1", 4) ||
      fread(&stamp, sizeof(stamp), 1, file.get()) != 1 ||
      fread(state.regs, sizeof(state.regs), 1, file.get()) != 1 ||
      fread(state.visible, sizeof(state.visible), 1, file.get()) != 1 ||
      fread(&state.phase_ns, sizeof(state.phase_ns), 1, file.get()) != 1 ||
      fread(&fall_back, sizeof(fall_back), 1, file.get()) != 1 || fall_back > 1 ||
      fgetc(file.get()) != EOF || ferror(file.get()))
    FAILURE(Configuration, "Invalid DS1287A battery image: " + battery_path);
  state.fall_back = fall_back != 0;
  state.reset_held = false;
  if (!valid_state())
    FAILURE(Configuration, "Invalid DS1287A battery registers: " + battery_path);
  set_reset(true);
  // The battery powers the oscillator and NVR between sessions (EK-D3SYS-PM §9.5.2.7).
  const u64 now = wall_time_ns();
  if (now > stamp)
    advance(now - stamp);
  dirty = false;
}

void DS1287A::save_battery()
{
  if (battery_path.empty() || !dirty)
    return;
  CSnapshotFile file(battery_path.c_str());
  const u64 stamp = wall_time_ns();
  const u8 fall_back = state.fall_back;
  if (fwrite("DSA1", 4, 1, file.get()) != 1 || fwrite(&stamp, sizeof(stamp), 1, file.get()) != 1 ||
      fwrite(state.regs, sizeof(state.regs), 1, file.get()) != 1 ||
      fwrite(state.visible, sizeof(state.visible), 1, file.get()) != 1 ||
      fwrite(&state.phase_ns, sizeof(state.phase_ns), 1, file.get()) != 1 ||
      fwrite(&fall_back, sizeof(fall_back), 1, file.get()) != 1)
    FAILURE(IO, "Cannot save DS1287A battery image: " + battery_path);
  file.publish();
  dirty = false;
}
