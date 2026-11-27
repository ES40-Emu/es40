/* ES40 emulator.
 * Copyright (C) Patrick Mackinlay (MAME Bt459)
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
// license:BSD-3-Clause
// copyright-holders:Patrick Mackinlay

#include "Bt459.h"

#include <algorithm>

DEFINE_DEVICE_TYPE(BT459, bt459_device, "bt459", "Brooktree Bt459 256 Color RAMDAC")

void bt459_device::map(address_map &map)
{
  map(0x00, 0x00).rw(FUNC(bt459_device::address_lo_r), FUNC(bt459_device::address_lo_w));
  map(0x01, 0x01).rw(FUNC(bt459_device::address_hi_r), FUNC(bt459_device::address_hi_w));
  map(0x02, 0x02).rw(FUNC(bt459_device::register_r), FUNC(bt459_device::register_w));
  map(0x03, 0x03).rw(FUNC(bt459_device::palette_r), FUNC(bt459_device::palette_w));
}

bt459_device::bt459_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock)
  : device_t(mconfig, BT459, tag, owner, clock)
{
}

void bt459_device::device_start()
{
  save_item(NAME(m_palette_ram));
  save_item(NAME(m_overlay_color));
  save_item(NAME(m_cursor_color));
  save_item(NAME(m_cursor_ram));
  save_item(NAME(m_command_0));
  save_item(NAME(m_command_1));
  save_item(NAME(m_command_2));
  save_item(NAME(m_pixel_read_mask));
  save_item(NAME(m_pixel_blink_mask));
  save_item(NAME(m_overlay_read_mask));
  save_item(NAME(m_overlay_blink_mask));
  save_item(NAME(m_interleave));
  save_item(NAME(m_test));
  save_item(NAME(m_signature));
  save_item(NAME(m_cursor_command));
  save_item(NAME(m_coordinates));
  save_item(NAME(m_display_coordinates));
  save_item(NAME(m_address));
  save_item(NAME(m_address_rgb));
  save_item(NAME(m_blink_start));
  save_item(NAME(m_contrast));
}

void bt459_device::device_reset()
{
  m_blink_start = current_frame;
  m_contrast = 0xff;
}

void bt459_device::set_scanout(int horizontal, int vertical)
{
  hsync_to_active = horizontal;
  vsync_to_active = vertical;
}

u32 bt459_device::rgb(const u8* color) const
{
  const unsigned gain = m_contrast + 1;
  return 0xff000000u | ((color[0] * gain >> 8) << 16) |
    ((color[1] * gain >> 8) << 8) | (color[2] * gain >> 8);
}

bool bt459_device::valid_state() const
{
  if (m_address > ADDRESS_MASK || m_address_rgb > 2)
    return false;
  for (unsigned i = 0; i < 6; ++i)
    if (m_coordinates[i * 2 + 1] > 15 || m_display_coordinates[i] > ADDRESS_MASK)
      return false;
  return true;
}

u8* bt459_device::indirect_color()
{
  if (m_address >= REG_OVERLAY_COLOR_0 && m_address <= REG_OVERLAY_COLOR_15)
    return &m_overlay_color[(m_address - REG_OVERLAY_COLOR_0) * 3];
  if (m_address >= REG_CURSOR_COLOR_1 && m_address <= REG_CURSOR_COLOR_3)
    return &m_cursor_color[(m_address - REG_CURSOR_COLOR_1) * 3];
  return nullptr;
}

u8 bt459_device::indirect(u8 value, bool writing)
{
  // The indirect port selects control registers and cursor RAM (Bt459 Table 1).
  const unsigned address = m_address;
  if (address == REG_ID)
    return BT459_ID;
  if (address == REG_REVISION)
    return BT459_REV;
  if (address >= REG_CURSOR_X_LO && address <= REG_WINDOW_H_HI)
  {
    const unsigned i = address - REG_CURSOR_X_LO;
    if (writing)
    {
      m_coordinates[i] = (i & 1) ? value & 15 : value;
      // The Y or height high byte latches the pair for retrace (Bt459 pp. 4-160–162).
      if ((i & 3) == 3)
      {
        for (unsigned j = i - 3; j < i; j += 2)
          m_display_coordinates[j / 2] = m_coordinates[j] | (m_coordinates[j + 1] << 8);
      }
    }
    return m_coordinates[i];
  }

  u8* reg;
  u8 mask = 0xff;
  switch (address)
  {
  case REG_COMMAND_0:
    if (writing)
      m_blink_start = current_frame;
    reg = &m_command_0;
    mask = static_cast<u8>(~CR04);
    break;
  case REG_COMMAND_1: reg = &m_command_1; mask = static_cast<u8>(~CR14); break;
  case REG_COMMAND_2: reg = &m_command_2; break;
  case REG_PIXEL_READ_MASK: reg = &m_pixel_read_mask; break;
  case REG_PIXEL_BLINK_MASK: reg = &m_pixel_blink_mask; break;
  case REG_OVERLAY_READ_MASK: reg = &m_overlay_read_mask; mask = 0x0f; break;
  case REG_OVERLAY_BLINK_MASK: reg = &m_overlay_blink_mask; mask = 0x0f; break;
  case REG_INTERLEAVE: reg = &m_interleave; break;
  case REG_TEST: reg = &m_test; mask = 0xf7; break;
  case REG_RED_SIGNATURE:
  case REG_GREEN_SIGNATURE:
  case REG_BLUE_SIGNATURE:
    reg = &m_signature[address - REG_RED_SIGNATURE];
    break;
  case REG_CURSOR_COMMAND: reg = &m_cursor_command; break;
  default:
    if (address < CURSOR_RAM_START || address > CURSOR_RAM_END)
      return 0;
    reg = &m_cursor_ram[address - CURSOR_RAM_START];
    break;
  }

  if (writing)
    *reg = value & mask;
  return *reg;
}

// An address access restarts the RGB sequence (Bt459 Table 1).
u8 bt459_device::address_lo_r()
{
  if (!machine().side_effects_disabled())
    m_address_rgb = 0;
  return m_address & ADDRESS_LSB;
}

void bt459_device::address_lo_w(u8 data)
{
  m_address_rgb = 0;
  m_address = (m_address & ADDRESS_MSB) | data;
}

u8 bt459_device::address_hi_r()
{
  if (!machine().side_effects_disabled())
    m_address_rgb = 0;
  return (m_address & ADDRESS_MSB) >> 8;
}

void bt459_device::address_hi_w(u8 data)
{
  m_address_rgb = 0;
  m_address = ((data << 8) | (m_address & ADDRESS_LSB)) & ADDRESS_MASK;
}

void bt459_device::next_component(bool palette)
{
  if (++m_address_rgb == 3)
  {
    m_address_rgb = 0;
    // The primary palette advances its low address byte after blue (DEC 3000 SPM §6.5).
    if (palette)
      m_address = (m_address & ADDRESS_MSB) | ((m_address + 1) & ADDRESS_LSB);
    else
      m_address = (m_address + 1) & ADDRESS_MASK;
  }
}

u8 bt459_device::get_component(const u8* color, bool palette)
{
  // A single-channel part uses the selected MPU cycle for green (Bt459 p. 4-152).
  const unsigned selected = (m_command_2 & CR2524) >> 4;
  const u8 result = !selected || selected == m_address_rgb + 1u ? color[selected ? 1 : m_address_rgb] : 0;
  if (!machine().side_effects_disabled())
    next_component(palette);
  return result;
}

void bt459_device::set_component(u8* color, u8 data, bool palette)
{
  const unsigned selected = (m_command_2 & CR2524) >> 4;
  if (!selected || selected == m_address_rgb + 1u)
    color[selected ? 1 : m_address_rgb] = data;
  next_component(palette);
}

u8 bt459_device::register_r()
{
  if (const auto* color = indirect_color())
    return get_component(color, false);
  const u8 result = indirect(0, false);
  if (!machine().side_effects_disabled())
    m_address = (m_address + 1) & ADDRESS_MASK;
  return result;
}

void bt459_device::register_w(u8 data)
{
  if (auto* color = indirect_color())
    set_component(color, data, false);
  else
  {
    indirect(data, true);
    m_address = (m_address + 1) & ADDRESS_MASK;
  }
}

u8 bt459_device::palette_r()
{
  return get_component(&m_palette_ram[(m_address & ADDRESS_LSB) * 3], true);
}

void bt459_device::palette_w(u8 data)
{
  set_component(&m_palette_ram[(m_address & ADDRESS_LSB) * 3], data, true);
}

bool bt459_device::blink_on(u64 frame) const
{
  // CR03:02 selects the on/off duration in vertical retraces (Bt459 p. 4-150).
  const unsigned rate = (m_command_0 >> 2) & 3;
  const unsigned on = rate < 2 ? 16 : 8u << rate;
  const unsigned period = rate == 0 ? 64 : on * 2;
  return (frame - m_blink_start) % period < on;
}

bool bt459_device::blink_changed(u64 frame) const
{
  const bool cursor = (m_cursor_command & CR40) && (m_cursor_command & (CR47 | CR46 | CR45 | CR44));
  return (cursor || (m_pixel_blink_mask & m_pixel_read_mask)) &&
    blink_on(frame) != blink_on(frame - 1);
}

u32 bt459_device::indexed_color(u8 index, u64 frame) const
{
  // With overlay inputs low, CR05 selects overlay zero in normal mode (Bt459 Table 4).
  if ((m_command_0 & CR05) && !(m_command_2 & CR22))
    return rgb(m_overlay_color);
  index &= m_pixel_read_mask;
  if (m_pixel_blink_mask && !blink_on(frame))
    index &= ~m_pixel_blink_mask;
  return rgb(&m_palette_ram[index * 3]);
}

void bt459_device::draw_cursor(u32* pixels, int stride, const rectangle& clip, int x_origin, int y_origin,
  int horizontal, int vertical, u64 frame) const
{
  const unsigned planes = m_cursor_command >> 6;
  const unsigned cross = (m_cursor_command >> 4) & 3;
  if ((!planes && !cross) || ((m_cursor_command & CR40) && !blink_on(frame)))
    return;

  const unsigned mux_select = m_command_0 & CR0706;
  if (mux_select == CR0706_RESERVED)
    return;
  const int mux = mux_select == CR0706_41MPX ? 4 : mux_select == CR0706_11MPX ? 1 : 5;
  // The programmed position names pixel (31,31) relative to sync (Bt459 pp. 4-144, 4-160).
  const int cx = x_origin + m_display_coordinates[0] - horizontal + 32 + mux * 5;
  int cy = m_display_coordinates[1];
  if (cy >= 0xfc0)
    cy -= 4096;
  cy += y_origin + 32 - vertical;

  const int left = cx - 31, top = cy - 31;
  const int win_x = x_origin + m_display_coordinates[2] - horizontal + mux * 5;
  const int win_y = y_origin + m_display_coordinates[3] - vertical;
  const int win_right = win_x + m_display_coordinates[4] + 2 * mux;
  const int win_bottom = win_y + m_display_coordinates[5] + 2;
  const int radius = (m_cursor_command >> 1) & 3;

  const int xmin = (std::max)(clip.min_x, cross ? (std::min)(left, win_x) : left);
  const int ymin = (std::max)(clip.min_y, cross ? (std::min)(top, win_y) : top);
  const int xmax = (std::min)(clip.max_x + 1, cross ? (std::max)(left + 64, win_right) : left + 64);
  const int ymax = (std::min)(clip.max_y + 1, cross ? (std::max)(top + 64, win_bottom) : top + 64);
  for (int y = ymin; y < ymax; ++y)
  {
    for (int x = xmin; x < xmax; ++x)
    {
      unsigned color = 0;
      const unsigned sx = unsigned(x - left), sy = unsigned(y - top);
      if (sx < 64 && sy < 64)
      {
        // Each byte describes four pixels, most significant pair first (Bt459 Fig. 12).
        const auto bits = m_cursor_ram[sy * 16 + sx / 4];
        color = (bits >> (6 - (sx % 4) * 2)) & planes;
      }
      if (cross && x >= win_x && x < win_right && y >= win_y && y < win_bottom &&
          ((x >= cx - radius && x <= cx + radius) || (y >= cy - radius && y <= cy + radius)))
        color = (m_cursor_command & CR43) ? color | cross : color ^ cross;
      if (color && (!(m_command_2 & CR21) || (color & 2)))
        pixels[y * stride + x] = rgb(&m_cursor_color[(color - 1) * 3]);
    }
  }
}

void bt459_device::screen_update(screen_device& screen, bitmap_rgb32& bitmap, const rectangle& cliprect, u8* pixel_data)
{
  current_frame = screen.frame_number();
  const auto& area = screen.visible_area();
  const rectangle clip((std::max)({area.min_x, cliprect.min_x, 0}),
    (std::min)({area.max_x, cliprect.max_x, bitmap.width() - 1}),
    (std::max)({area.min_y, cliprect.min_y, 0}),
    (std::min)({area.max_y, cliprect.max_y, bitmap.height() - 1}));
  if (clip.min_x > clip.max_x || clip.min_y > clip.max_y)
    return;

  const unsigned bpp = bits_per_pixel();
  const unsigned per_byte = 8 / bpp;
  const size_t stride = (area.width() + per_byte - 1) / per_byte;
  u32 colors[BT459_PIXEL_COLORS];
  for (unsigned i = 0; i < (1u << bpp); ++i)
    colors[i] = indexed_color(static_cast<u8>(i), current_frame);

  for (int y = clip.min_y; y <= clip.max_y; ++y)
  {
    const u8* source = pixel_data + (y - area.min_y) * stride;
    u32* output = bitmap.rowptr(y);
    if (bpp == 8)
    {
      for (int x = clip.min_x; x <= clip.max_x; ++x)
        output[x] = colors[source[x - area.min_x]];
    }
    else
    {
      // Block mode shifts the most significant pixel out first (Bt459 Table 3).
      for (int x = clip.min_x; x <= clip.max_x; ++x)
      {
        const unsigned column = x - area.min_x;
        const unsigned shift = (per_byte - 1 - column % per_byte) * bpp;
        output[x] = colors[(source[column / per_byte] >> shift) & ((1u << bpp) - 1)];
      }
    }
  }
  draw_cursor(bitmap.raw(), bitmap.width(), clip, area.min_x, area.min_y,
    hsync_to_active, vsync_to_active, current_frame);
}
