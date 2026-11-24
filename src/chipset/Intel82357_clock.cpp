/* ES40 emulator -- Intel 82357 interval timer clocks
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
#include "Intel82357.h"
#include "system/System.h"
#include <algorithm>

namespace {
struct TimerClock {
  u64 frequency;
  u64 denominator_ns;

  u64 advance(u64 ns, u64& phase) const
  {
    const u64 accrued = (ns % denominator_ns) * frequency + phase;
    phase = accrued % denominator_ns;
    return (ns / denominator_ns) * frequency + accrued / denominator_ns;
  }

  u64 until(u32 ticks, u64 phase) const
  {
    return ticks ? (ticks * denominator_ns - phase + frequency - 1) / frequency : 0;
  }
};

// Timer 1 uses OSC/12, fail-safe OSC/48, and speed control BCLK (82357 §2.7, Fig. 2-4).
constexpr TimerClock clocks[] = {{14318180, 12000000000ULL}, {14318180, 48000000000ULL}, {8000000, 1000000000ULL}};
}

Intel82357::~Intel82357()
{
  stop_threads();
}

void Intel82357::reset_timers()
{
  std::fill_n(timer, 5, Intel82357Timer{});
  std::fill_n(clock_phase, 3, u64{0});
  port_b = 0;
  extended_nmi = 0;
  nmi_masked = true;
  nmi_acknowledged = false;
  timer[2].set_gate(false);
  timer[4].set_gate(false);
  if (timer_sample != Clock::time_point{})
    timer_sample = cSystem->device_access_time();
  timer_settings_changed();
}

void Intel82357::advance_timers(u64 ns)
{
  while (ns) {
    u64 step = ns;
    // Refresh drives the speed counter's trigger input (PB22H-KB, p. 19-3).
    if (timer[4].programmed()) {
      const u64 edge = clocks[0].until(timer[1].clocks_to_change(), clock_phase[0]);
      if (edge)
        step = (std::min)(step, edge);
    }
    const u64 ticks = clocks[0].advance(step, clock_phase[0]);
    const bool irq_high = timer[0].output();
    const auto system = timer[0].advance(ticks);
    const auto refresh = timer[1].advance(ticks);
    timer[2].advance(ticks);
    const auto failsafe = timer[3].advance(clocks[1].advance(step, clock_phase[1]));
    timer[4].advance(clocks[2].advance(step, clock_phase[2]));
    timer[4].set_gate(timer[1].output());
    if (system || irq_high != timer[0].output())
      apply_timer_edges(0, system);
    apply_timer_edges(1, refresh);
    apply_timer_edges(3, failsafe);
    ns -= step;
  }
}

void Intel82357::refresh_timers()
{
  const auto now = cSystem->device_access_time();
  if (timer_sample != Clock::time_point{} && now > timer_sample) {
    advance_timers(std::chrono::duration_cast<std::chrono::nanoseconds>(now - timer_sample).count());
    timer_sample = now;
  }
}

u64 Intel82357::next_timer_event_ns() const
{
  const u64 irq = (pic_state.irr[0] & 1) ? 0 : clocks[0].until(timer[0].clocks_to_change(), clock_phase[0]);
  const u64 nmi = (extended_nmi & 0x84) == 4 ? clocks[1].until(timer[3].clocks_to_change(), clock_phase[1]) : 0;
  return irq && nmi ? (std::min)(irq, nmi) : (irq ? irq : nmi);
}

void Intel82357::run_timers()
{
  std::unique_lock<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  while (timer_sample != Clock::time_point{}) {
    refresh_timers();
    const u64 delay = next_timer_event_ns();
    if (delay)
      timer_wakeup.wait_until(lock, timer_sample + std::chrono::nanoseconds(delay));
    else
      timer_wakeup.wait(lock);
  }
}

void Intel82357::start_threads()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  if (!timer_worker.joinable()) {
    timer_sample = cSystem->device_access_time();
    try {
      timer_worker = std::thread(&Intel82357::run_timers, this);
    } catch (const std::system_error& error) {
      timer_sample = {};
      FAILURE(Thread, std::string("Cannot start Jensen interval timer: ") + error.what());
    }
  }
}

void Intel82357::stop_threads()
{
  {
    std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
    if (timer_sample != Clock::time_point{}) {
      refresh_timers();
      timer_sample = {};
      timer_settings_changed();
    }
  }
  if (timer_worker.joinable())
    timer_worker.join();
}

int Intel82357::SaveState(FILE* file)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  refresh_timers();
  if (fwrite("PIT1", 4, 1, file) != 1 || CSystemComponent::SaveState(file))
    return -1;
  for (const auto& counter : timer)
    if (!counter.save(file))
      return -1;
  const u8 nmi = u8(nmi_masked) | (u8(nmi_acknowledged) << 1);
  return fwrite(clock_phase, sizeof(clock_phase), 1, file) == 1 &&
    fputc(port_b, file) != EOF && fputc(extended_nmi, file) != EOF && fputc(nmi, file) != EOF ? 0 : -1;
}

int Intel82357::RestoreState(FILE* file)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  const int first = fgetc(file);
  if (first == EOF || ungetc(first, file) == EOF)
    return -1;
  if (first != 'P') {
    // Earlier ISP snapshots contain only the PIC and INTA state.
    if (CSystemComponent::RestoreState(file))
      return -1;
    reset_timers();
    return 0;
  }
  char tag[4];
  RegisteredState saved_pic;
  Intel82357Timer counters[5];
  u64 phases[3];
  if (fread(tag, sizeof(tag), 1, file) != 1 || memcmp(tag, "PIT1", 4) || !read_registered_state(file, saved_pic))
    return -1;
  for (auto& counter : counters)
    if (!counter.restore(file))
      return -1;
  if (fread(phases, sizeof(phases), 1, file) != 1)
    return -1;
  for (unsigned i = 0; i < 3; ++i)
    if (phases[i] >= clocks[i].denominator_ns)
      return -1;
  const int b = fgetc(file), ext = fgetc(file), nmi = fgetc(file);
  if (b < 0 || (b & ~0x1f) || ext < 0 || (ext & ~0xa6) || nmi < 0 || nmi > 3 ||
      counters[2].gate_high() != bool(b & 1) || counters[4].gate_high() != counters[1].output() ||
      !commit_registered_state(saved_pic))
    return -1;
  std::copy_n(counters, 5, timer);
  std::copy_n(phases, 3, clock_phase);
  port_b = u8(b);
  extended_nmi = u8(ext);
  nmi_masked = (nmi & 1) != 0;
  nmi_acknowledged = (nmi & 2) != 0;
  if (timer_sample != Clock::time_point{})
    timer_sample = cSystem->device_access_time();
  timer_settings_changed();
  return 0;
}
