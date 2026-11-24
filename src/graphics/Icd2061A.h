/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

#ifndef INCLUDED_ICD2061A_H
#define INCLUDED_ICD2061A_H

#include <array>
#include <chrono>
#include <cstdint>

class Icd2061A
{
public:
  using Clock = std::chrono::steady_clock;
  using Time = Clock::time_point;
  enum class Phase : std::uint8_t { Locked, Start, Data, Stop };

  struct Timing
  {
    std::uint32_t watchdog_ns = 0;
    std::uint32_t settle_ns = 0;
    std::uint8_t selected = 0;
  };

  struct State
  {
    // ROM option 1 with INIT1:0 pulled low (ICD2061A Tables 3, 7).
    std::array<double, 4> vco_hz{50350000, 56644000, 56644000, 65000000};
    std::array<std::uint8_t, 4> divisor_shift{1, 1, 1, 1};
    std::array<std::uint8_t, 3> video_index{};
    std::uint16_t control = 0x10;
    std::uint32_t serial_word = 0;
    std::uint8_t bits = 0;
    std::uint8_t unlock_edges = 0;
    std::uint8_t pins = 0;
    std::uint8_t falling_data = 0;
    Phase phase = Phase::Locked;
    Timing timing{};
  } state;

  static bool valid_state(const State& saved);
  void reset(Time now = Clock::now());
  void set_pins(unsigned pins, Time now = Clock::now());
  double frequency_hz(Time now = Clock::now()) const;
  void checkpoint(Time now);
  void resume(Time now) { m_anchor = now; }

private:
  Time m_anchor = Clock::now();

  Timing timing_at(Time now) const;
  std::uint32_t timeout_ns() const;
  void clock_rising(bool data);
  void load_word();
};

#endif
