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

#ifndef INCLUDED_BT485_H
#define INCLUDED_BT485_H

#include <array>
#include <cstdint>

class Bt485
{
public:
  using Color = std::array<std::uint8_t, 3>;

  struct State
  {
    std::array<Color, 256> palette{};
    std::array<Color, 4> cursor_colors{};
    std::array<std::uint8_t, 1024> cursor_ram{};
    std::array<std::uint8_t, 4> command{};
    std::array<std::uint8_t, 4> cursor_position{};
    std::uint16_t cursor_x = 0;
    std::uint16_t cursor_y = 0;
    std::uint8_t mask = 0;
    std::uint8_t address = 0;
    std::uint8_t component = 0;
    std::uint8_t read_mode = 0;
    std::uint8_t prefetched = 0;
    Color latch{};
  } state;

  static bool valid_state(const State& saved);
  void reset();
  std::uint8_t read(unsigned reg, bool sense_high = true);
  void write(unsigned reg, std::uint8_t data);
  unsigned bits_per_pixel() const;
  bool clock_doubled() const;
  std::uint32_t indexed_color(std::uint8_t index) const;
  std::uint32_t direct_color(std::uint32_t pixel) const;
  std::uint32_t cursor_pixel(int x, int y, std::uint32_t background) const;

private:
  Color& color_entry(bool cursor);
  void prefetch_color(bool cursor);
  unsigned next_cursor_address();
  bool command3_selected() const;
  std::uint32_t rgb(const Color& color) const;
};

#endif
