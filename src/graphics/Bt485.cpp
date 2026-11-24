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

#include "Bt485.h"

bool Bt485::valid_state(const State& saved)
{
  return saved.component < 3 && saved.read_mode <= 1 && saved.prefetched <= 1 &&
    saved.cursor_x < 4096 && saved.cursor_y < 4096 &&
    (saved.cursor_position[1] | saved.cursor_position[3]) < 16 &&
    (saved.command[1] & 0x80) == 0 && saved.command[3] < 16;
}

void Bt485::reset()
{
  // RESET* clears the commands and selects the VGA port (Bt485 p. 4-219).
  state.command.fill(0);
}

Bt485::Color& Bt485::color_entry(bool cursor)
{
  return cursor ? state.cursor_colors[state.address & 3] : state.palette[state.address];
}

void Bt485::prefetch_color(bool cursor)
{
  // A read address loads all three components before advancing (Bt485 pp. 4-199–200).
  state.latch = color_entry(cursor);
  ++state.address;
  state.prefetched = 1;
}

unsigned Bt485::next_cursor_address()
{
  // Cursor access begins at the address written, independent of RGB prefetch (Bt485 p. 4-201).
  if (state.prefetched) {
    --state.address;
    state.prefetched = 0;
  }
  unsigned address = state.address++;
  if (state.command[3] & 4) {
    address |= (state.command[3] & 3) << 8;
    state.command[3] = (state.command[3] & 0x0c) | (((address + 1) >> 8) & 3);
  }
  return address;
}

bool Bt485::command3_selected() const
{
  return (state.command[0] & 0x80) && state.address == 1;
}

std::uint8_t Bt485::read(unsigned reg, bool sense_high)
{
  switch (reg) {
  case 0x0:
  case 0x3:
  case 0x4:
  case 0x7:
    return state.address;
  case 0x1:
  case 0x5: {
    const auto value = state.latch[state.component];
    if (++state.component == 3) {
      state.component = 0;
      prefetch_color(reg == 0x5);
    }
    return (state.command[0] & 2) ? value : (value >> 2);
  }
  case 0x2:
    return state.mask;
  case 0x6:
    return state.command[0];
  case 0x8:
    return state.command[1];
  case 0x9:
    return state.command[2];
  case 0xa:
    if (command3_selected())
      return state.command[3];
    // SR7:6 identify the Bt485 and SR2:0 report MPU access state (Bt485 p. 4-216).
    return 0x40 | (sense_high ? 8 : 0) | (state.read_mode << 2) | state.component;
  case 0xb:
    return state.cursor_ram[next_cursor_address()];
  case 0xc:
  case 0xd:
  case 0xe:
  case 0xf:
    return state.cursor_position[reg - 0xc];
  default:
    return 0xff;
  }
}

void Bt485::write(unsigned reg, std::uint8_t data)
{
  switch (reg) {
  case 0x0:
  case 0x3:
  case 0x4:
  case 0x7:
    state.address = data;
    state.component = 0;
    state.prefetched = 0;
    state.read_mode = (reg == 0x3 || reg == 0x7);
    if (state.read_mode)
      prefetch_color(reg == 0x7);
    break;
  case 0x1:
  case 0x5:
    // The blue write commits the complete RGB entry (Bt485 p. 4-199).
    state.latch[state.component] = (state.command[0] & 2) ? data : std::uint8_t(data << 2);
    if (++state.component == 3) {
      state.component = 0;
      color_entry(reg == 0x5) = state.latch;
      ++state.address;
    }
    break;
  case 0x2:
    state.mask = data;
    break;
  case 0x6:
    state.command[0] = data;
    break;
  case 0x8:
    state.command[1] = data & 0x7f;
    break;
  case 0x9:
    state.command[2] = data;
    break;
  case 0xa:
    if (command3_selected())
      state.command[3] = data & 0x0f;
    break;
  case 0xb:
    state.cursor_ram[next_cursor_address()] = data;
    break;
  case 0xc:
  case 0xd:
  case 0xe:
  case 0xf:
    state.cursor_position[reg - 0xc] = (reg & 1) ? (data & 0x0f) : data;
    // Y high latches both 12-bit display coordinates (Bt485 pp. 4-206, 4-217–218).
    if (reg == 0xf) {
      state.cursor_x = state.cursor_position[0] | (state.cursor_position[1] << 8);
      state.cursor_y = state.cursor_position[2] | (state.cursor_position[3] << 8);
    }
    break;
  }
}

unsigned Bt485::bits_per_pixel() const
{
  // CR25 masks PORTSEL to VGA, otherwise CR16:15 set the pixel width (Bt485 pp. 4-213–214).
  constexpr unsigned widths[] = {24, 16, 8, 4};
  return (state.command[2] & 0x20) ? widths[(state.command[1] >> 5) & 3] : 8;
}

bool Bt485::clock_doubled() const
{
  return (state.command[3] & 8) != 0;
}

std::uint32_t Bt485::rgb(const Color& color) const
{
  if (state.command[0] & 1)
    return 0xff000000;
  const unsigned mask = (state.command[0] & 2) ? 0xff : 0xfc;
  return 0xff000000 | ((color[0] & mask) << 16) | ((color[1] & mask) << 8) | (color[2] & mask);
}

std::uint32_t Bt485::indexed_color(std::uint8_t index) const
{
  return rgb(state.palette[index & state.mask]);
}

std::uint32_t Bt485::direct_color(std::uint32_t pixel) const
{
  const unsigned width = bits_per_pixel();
  if (width == 4)
    return indexed_color(pixel & 0x0f);
  if (width == 8)
    return indexed_color(std::uint8_t(pixel));

  const bool bypass = (state.command[1] & 0x10) != 0;
  Color color;
  if (width == 16) {
    // CR13 selects 555/565; bypass and sparse indexing use the MSBs (Bt485 Tables 4, 5, 7).
    const unsigned green_bits = (state.command[1] & 8) ? 6 : 5;
    color[0] = (pixel >> (5 + green_bits)) & 0x1f;
    color[1] = (pixel >> 5) & ((1u << green_bits) - 1);
    color[2] = pixel & 0x1f;
    if (bypass || !(state.command[2] & 4)) {
      color[0] <<= 3;
      color[1] <<= 8 - green_bits;
      color[2] <<= 3;
    }
  } else {
    color = {std::uint8_t(pixel >> 16), std::uint8_t(pixel >> 8), std::uint8_t(pixel)};
  }

  if (!bypass) {
    for (unsigned channel = 0; channel < color.size(); ++channel)
      color[channel] = state.palette[color[channel] & state.mask][channel];
  }
  return rgb(color);
}

std::uint32_t Bt485::cursor_pixel(int x, int y, std::uint32_t background) const
{
  if (state.command[0] & 1)
    return 0xff000000;
  const unsigned mode = state.command[2] & 3;
  const int size = (state.command[3] & 4) ? 64 : 32;
  const int column = x - (int(state.cursor_x) - size);
  const int row = y - (int(state.cursor_y) - size);
  if (!mode || column < 0 || column >= size || row < 0 || row >= size)
    return background;

  const int address = (row * size + column) / 8;
  const unsigned bit = 0x80u >> (column & 7);
  const unsigned plane0 = (state.cursor_ram[address] & bit) ? 1 : 0;
  const unsigned plane1 = (state.cursor_ram[size * size / 8 + address] & bit) ? 2 : 0;

  // Table 8 maps each two-plane pattern to palette data, a cursor color, or highlight.
  constexpr int overlay[3][4] = {{0, 1, 2, 3}, {1, 2, 0, -1}, {0, 0, 1, 2}};
  const int selection = overlay[mode - 1][plane1 | plane0];
  if (selection < 0)
    return background ^ ((state.command[0] & 2) ? 0x00ffffff : 0x00fcfcfc);
  return selection ? rgb(state.cursor_colors[selection]) : background;
}
