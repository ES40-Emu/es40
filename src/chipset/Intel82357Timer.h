/* ES40 emulator -- Intel 82357 interval timer counter
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

#ifndef ES40_INTEL82357TIMER_H
#define ES40_INTEL82357TIMER_H

#include <cstdio>
#include <cstdint>

// Each implemented ISP channel has the 82C54 counter interface (PB22H-KB, pp. 19-2, 19-4).
class Intel82357Timer {
public:
  bool output() const { return out; }
  bool gate_high() const { return gate; }
  bool programmed() const { return reload != 0; }

  bool save(FILE* file) const
  {
    const std::uint32_t counts[] = {reload, period, phase, count_value};
    const std::uint16_t words[] = {count_latch, static_cast<std::uint16_t>(
      unsigned(active) | load_next << 1 | terminal << 2 | null_count << 3 | gate << 4 | out << 5 |
      write_high << 6 | read_high << 7 | count_latched << 8 | status_latched << 9)};
    const std::uint8_t bytes[] = {control, status_latch, write_low};
    return std::fwrite(counts, sizeof(counts), 1, file) == 1 &&
      std::fwrite(words, sizeof(words), 1, file) == 1 && std::fwrite(bytes, sizeof(bytes), 1, file) == 1;
  }

  bool restore(FILE* file)
  {
    std::uint32_t counts[4];
    std::uint16_t words[2];
    std::uint8_t bytes[3];
    if (std::fread(counts, sizeof(counts), 1, file) != 1 ||
        std::fread(words, sizeof(words), 1, file) != 1 || std::fread(bytes, sizeof(bytes), 1, file) != 1)
      return false;
    const auto flags = words[1];
    const auto limit = bytes[0] & 1 ? 10000u : 65536u;
    Intel82357Timer next;
    next.control = bytes[0];
    if (flags > 0x3ff || next.control > 0x3f || (next.control && !next.access()) ||
        counts[0] > limit || counts[1] > limit || counts[3] > limit ||
        (counts[2] && (!next.periodic() || counts[2] >= counts[1])) ||
        ((flags & 1) && (!counts[1] || !next.access())) || ((flags & 2) && !counts[0]) ||
        (next.periodic() && (counts[0] == 1 || counts[1] == 1)) ||
        (!next.periodic() && (flags & 5) == 1 && !counts[3]) ||
        (next.access() != 3 && (flags & 0xc0)))
      return false;
    next.reload = counts[0];
    next.period = counts[1];
    next.phase = counts[2];
    next.count_value = counts[3];
    next.count_latch = words[0];
    next.status_latch = bytes[1];
    next.write_low = bytes[2];
    next.active = flags & 1;
    next.load_next = flags & 2;
    next.terminal = flags & 4;
    next.null_count = flags & 8;
    next.gate = flags & 0x10;
    next.out = flags & 0x20;
    next.write_high = flags & 0x40;
    next.read_high = flags & 0x80;
    next.count_latched = flags & 0x100;
    next.status_latched = flags & 0x200;
    *this = next;
    return true;
  }

  void program(std::uint8_t word)
  {
    if (!(word & 0x30)) {
      latch(true, false);
      return;
    }
    const bool gate_level = gate;
    *this = Intel82357Timer{};
    gate = gate_level;
    control = word & 0x3f;
    out = mode() != 0; // OUT takes its initial level immediately (82C54, "Programming").
  }

  void latch(bool count, bool status)
  {
    // Pending count and status latches are independent (PB22H-KB, Figs. 19-2 to 19-4).
    if (count && !count_latched && !read_high) {
      count_latch = read_count();
      count_latched = true;
    }
    if (status && !status_latched) {
      status_latch = control | (null_count ? 0x40 : 0) | (out ? 0x80 : 0);
      status_latched = true;
    }
  }

  std::uint8_t read()
  {
    if (status_latched) {
      status_latched = false;
      return status_latch;
    }
    const std::uint16_t value = count_latched ? count_latch : read_count();
    const bool high = access() == 2 || read_high;
    read_high = access() == 3 && !read_high;
    if (!read_high)
      count_latched = false;
    return static_cast<std::uint8_t>(high ? value >> 8 : value);
  }

  void write(std::uint8_t value)
  {
    if (!access())
      return;
    if (mode() == 0) {
      active = false;
      load_next = false;
      out = false;
    }
    if (access() == 3 && !write_high) {
      write_low = value;
      write_high = true;
      return;
    }
    std::uint16_t word = value;
    if (access() == 2)
      word = static_cast<std::uint16_t>(value << 8);
    else if (write_high)
      word = static_cast<std::uint16_t>((value << 8) | write_low);
    write_high = false;
    reload = decode(word);
    // A divisor of one is outside the range for modes 2 and 3 (82C54, Fig. 22).
    if (periodic() && reload == 1)
      reload = 0;
    null_count = true;
    if (!reload)
      load_next = false;
    else if (mode() == 0 || mode() == 4 || (periodic() && !active))
      load_next = true;
  }

  void set_gate(bool level)
  {
    if (gate != level) {
      if (level && reload && (periodic() || mode() == 1 || mode() == 5))
        load_next = true;
      if (!level && periodic())
        out = true; // GATE low forces OUT high in modes 2 and 3 (82C54, Fig. 21).
    }
    gate = level;
  }

  std::uint32_t clocks_to_change() const
  {
    if (load_next)
      return 1;
    if (!counting())
      return 0;
    if (periodic())
      return phase < high_clocks() ? high_clocks() - phase : period - phase;
    if (!terminal)
      return count_value;
    return out ? 0 : 1;
  }

  std::uint64_t advance(std::uint64_t clocks)
  {
    std::uint64_t rises = 0;
    if (clocks && load_next && reload) {
      load_next = false;
      null_count = false;
      active = true;
      terminal = false;
      period = reload;
      phase = 0;
      count_value = reload;
      rises += set_output(mode() != 0 && mode() != 1);
      --clocks; // CR loads CE on a clock edge without decrementing (82C54, Figs. 15-20).
    }
    if (!clocks || !counting())
      return rises;
    if (periodic()) {
      // Mode 2 takes a new divisor on reload, mode 3 at the next half-cycle (82C54, Figs. 17-18).
      if (null_count && reload) {
        const auto until_reload = mode() == 3 ? clocks_to_change() : period - phase;
        if (clocks >= until_reload) {
          rises += advance_period(until_reload);
          clocks -= until_reload;
          period = reload;
          phase = out ? 0 : high_clocks();
          null_count = false;
        }
      }
      rises += advance_period(clocks);
    } else {
      const bool strobe = mode() == 4 || mode() == 5;
      if (!terminal && clocks >= count_value) {
        clocks -= count_value;
        count_value = 0;
        terminal = true;
        rises += set_output(!strobe);
      }
      if (terminal && strobe && clocks)
        rises += set_output(true);
      // After terminal count CE keeps counting through wrap (82C54, "Operation Common to All Modes").
      count_value = (count_value + modulus() - static_cast<std::uint32_t>(clocks % modulus())) % modulus();
    }
    return rises;
  }

private:
  unsigned access() const { return control >> 4; }
  unsigned mode() const
  {
    const unsigned field = (control >> 1) & 7;
    return field < 6 ? field : field - 4; // Alias encodings X10/X11 (PB22H-KB, Fig. 19-1).
  }
  bool periodic() const { return mode() == 2 || mode() == 3; }
  bool counting() const { return active && (gate || mode() == 1 || mode() == 5); }
  std::uint32_t modulus() const { return control & 1 ? 10000 : 65536; }
  std::uint32_t high_clocks() const { return mode() == 2 ? period - 1 : (period + 1) / 2; }

  std::uint32_t decode(std::uint16_t word) const
  {
    if (!word)
      return modulus();
    if (!(control & 1))
      return word;
    std::uint32_t value = 0;
    for (int shift = 12; shift >= 0; shift -= 4) {
      const unsigned digit = (word >> shift) & 15;
      if (digit > 9)
        return 0;
      value = value * 10 + digit;
    }
    return value;
  }

  std::uint16_t read_count() const
  {
    std::uint32_t value = count_value;
    if (active && periodic()) {
      value = period - phase;
      if (mode() == 3) {
        const auto half_phase = phase < high_clocks() ? phase : phase - high_clocks();
        value = (period & ~1u) - 2 * half_phase; // Odd counts reload N-1 (82C54, Fig. 18).
      }
    }
    if (!(control & 1))
      return static_cast<std::uint16_t>(value);
    std::uint16_t word = 0;
    for (unsigned shift = 0; shift < 16; shift += 4) {
      word |= static_cast<std::uint16_t>((value % 10) << shift);
      value /= 10;
    }
    return word;
  }

  bool set_output(bool level)
  {
    const bool rise = level && !out;
    out = level;
    return rise;
  }

  std::uint64_t advance_period(std::uint64_t clocks)
  {
    const std::uint64_t cycles = clocks / period;
    const auto end = phase + static_cast<std::uint32_t>(clocks % period);
    phase = end % period;
    out = phase < high_clocks();
    return cycles + (end >= period);
  }

  std::uint32_t reload = 0, period = 0, phase = 0, count_value = 0;
  std::uint16_t count_latch = 0;
  std::uint8_t control = 0, status_latch = 0, write_low = 0;
  bool active = false, load_next = false, terminal = false, null_count = true;
  bool gate = true, out = false, write_high = false, read_high = false;
  bool count_latched = false, status_latched = false;
};

#endif
