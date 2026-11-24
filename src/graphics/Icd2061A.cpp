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

#include "Icd2061A.h"
#include <algorithm>

static constexpr double reference_hz = 14318180.0;

bool Icd2061A::valid_state(const State& saved)
{
  return saved.timing.selected < 3 && saved.pins < 4 && saved.falling_data < 2 &&
    saved.unlock_edges <= 5 && saved.phase <= Phase::Stop &&
    saved.bits <= (saved.phase == Phase::Data ? 23 : 24) && saved.serial_word < (1u << 24) &&
    saved.control < 512 && saved.timing.watchdog_ns <= 10000000 && saved.timing.settle_ns <= 10000000 &&
    std::all_of(saved.divisor_shift.begin(), saved.divisor_shift.end(), [](unsigned shift) { return shift < 8; }) &&
    std::all_of(saved.video_index.begin(), saved.video_index.end(), [](unsigned index) { return index < 16; }) &&
    std::all_of(saved.vco_hz.begin(), saved.vco_hz.end(), [](double hz) { return hz > 0 && hz <= reference_hz * 130; });
}

void Icd2061A::reset(Time now)
{
  state = {};
  m_anchor = now;
}

std::uint32_t Icd2061A::timeout_ns() const
{
  // C3 doubles the nominal 5 ms watchdog and PLL settling interval (ICD2061A Fig. 3, Table 16).
  return (state.control & 0x40) ? 10000000 : 5000000;
}

Icd2061A::Timing Icd2061A::timing_at(Time now) const
{
  Timing result = state.timing;
  if (now <= m_anchor || (!result.watchdog_ns && !result.settle_ns))
    return result;

  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_anchor).count();
  result.settle_ns = elapsed < result.settle_ns ? std::uint32_t(result.settle_ns - elapsed) : 0;
  if (!result.watchdog_ns)
    return result;
  if (elapsed < result.watchdog_ns) {
    result.watchdog_ns -= std::uint32_t(elapsed);
    return result;
  }

  // With INTCLK high, SEL1 selects REG2 for either SEL0 level (ICD2061A Table 4).
  const unsigned selected = (state.pins & 2) ? 2 : (state.pins & 1);
  if (selected != result.selected) {
    const auto after_selection = elapsed - result.watchdog_ns;
    result.selected = std::uint8_t(selected);
    result.settle_ns = after_selection < timeout_ns() ? std::uint32_t(timeout_ns() - after_selection) : 0;
  }
  result.watchdog_ns = 0;
  return result;
}

void Icd2061A::checkpoint(Time now)
{
  if (now < m_anchor)
    return;
  const Timing updated = timing_at(now);
  if (state.timing.watchdog_ns && !updated.watchdog_ns) {
    state.phase = Phase::Locked;
    state.unlock_edges = 0;
  }
  state.timing = updated;
  m_anchor = now;
}

void Icd2061A::set_pins(unsigned pins, Time now)
{
  pins &= 3;
  const unsigned changed = pins ^ state.pins;
  if (!changed)
    return;
  checkpoint(now);
  state.pins = std::uint8_t(pins);
  state.timing.watchdog_ns = timeout_ns();
  if (changed & 1) {
    // SEL0 is CLK and SEL1 is DATA (ICD2061A Figs. 5, 14).
    if (pins & 1)
      clock_rising((pins & 2) != 0);
    else
      state.falling_data = (pins & 2) != 0;
  }
}

void Icd2061A::clock_rising(bool data)
{
  switch (state.phase) {
  case Phase::Locked:
    if (data) {
      if (state.unlock_edges < 5)
        ++state.unlock_edges;
    } else {
      if (state.unlock_edges == 5)
        state.phase = Phase::Start;
      state.unlock_edges = 0;
    }
    break;
  case Phase::Start:
    state.bits = 0;
    state.serial_word = 0;
    state.phase = (!data && !state.falling_data) ? Phase::Data : Phase::Locked;
    break;
  case Phase::Data:
    // Each rising-edge data bit must complement the preceding falling edge (ICD2061A Fig. 6).
    if (data == (state.falling_data != 0)) {
      state.phase = Phase::Locked;
      break;
    }
    if (data)
      state.serial_word |= 1u << state.bits;
    if (++state.bits == 24)
      state.phase = Phase::Stop;
    break;
  case Phase::Stop:
    if (state.falling_data && data)
      load_word();
    state.phase = Phase::Locked;
    break;
  }
}

void Icd2061A::load_word()
{
  const unsigned reg = state.serial_word >> 21;
  if (reg == 6) {
    state.control = (state.serial_word >> 12) & 0x1ff;
    return;
  }
  if (reg >= 4)
    return;

  // W contains Q', M, P', and I from least to most significant (ICD2061A pp. 140-142).
  const unsigned p = ((state.serial_word >> 10) & 0x7f) + 3;
  const unsigned q = (state.serial_word & 0x7f) + 2;
  state.vco_hz[reg] = 2.0 * reference_hz * p / q;
  state.divisor_shift[reg] = (state.serial_word >> 7) & 7;
  if (reg < 3)
    state.video_index[reg] = (state.serial_word >> 17) & 15;
  if (reg == state.timing.selected || (reg == 3 && state.video_index[state.timing.selected] == 15))
    state.timing.settle_ns = timeout_ns();
}

double Icd2061A::frequency_hz(Time now) const
{
  const auto timing = timing_at(now);
  if (timing.settle_ns) {
    // C4 selects the reference or memory clock while the video PLL settles (ICD2061A p. 135).
    return (state.control & 0x80) ? state.vco_hz[3] / (1u << state.divisor_shift[3]) : reference_hz;
  }
  const unsigned reg = timing.selected;
  if (state.video_index[reg] == 14)
    return 0.0;
  const double vco = state.video_index[reg] == 15 ? state.vco_hz[3] :
    state.vco_hz[reg] * ((state.control & (1u << reg)) ? 2.0 : 1.0);
  return vco / (1u << state.divisor_shift[reg]);
}
