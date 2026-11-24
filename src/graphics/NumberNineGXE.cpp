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

#include "StdAfx.h"
#include "NumberNineGXE.h"
#include "system/System.h"
#include <algorithm>

NumberNineGXE::NumberNineGXE(CConfigurator* cfg, CSystem* system, int bus, int slot, bx_gui_c& display)
  : CS3Trio64(cfg, system, bus, slot, display, Chip::S3_86C928D)
{
  dac.reset();
  clock.reset();
}

void NumberNineGXE::ResetPCI()
{
  dac.reset();
  clock.reset(cSystem->device_access_time());
  CS3Trio64::ResetPCI();
}

bool NumberNineGXE::crtc_accessible() const
{
  const unsigned index = vga.crtc.index;
  if (index < 0x19 || index == 0x38)
    return true;
  // CR38 unlocks S3 VGA registers; CR39 unlocks the system registers (DB02-C §§7–9).
  if (index < 0x30 || index > 0x62 || (s3.reg_lock1 & 0xcc) != 0x48)
    return false;
  return index < 0x40 || (s3.reg_lock2 & 0xe0) == 0xa0;
}

u8 NumberNineGXE::crtc_data_r(offs_t offset)
{
  return crtc_accessible() ? CS3Trio64::crtc_data_r(offset) : 0xff;
}

void NumberNineGXE::crtc_data_w(offs_t offset, u8 value)
{
  const unsigned index = vga.crtc.index;
  if (crtc_accessible() && index != 0x36 && index != 0x37) // DB02-C sec. 7: power-on straps are read-only.
    CS3Trio64::crtc_data_w(offset, value);
}

unsigned NumberNineGXE::dac_register(unsigned port) const
{
  // RS3:2 come from CR55; CR43.1 supplies RS2 when CR55 selects bank zero (DB02-C §§8–9).
  unsigned bank = s3.extended_dac_ctrl & 3;
  if (!bank && (s3.cr43 & 2))
    bank = 1;
  return (bank << 2) | (port & 3);
}

bool NumberNineGXE::external_dac_read(u32 port, u8& value)
{
  const unsigned reg = dac_register(port);
  if (s3.extended_dac_ctrl & 4)
    value = 0xff; // CR55.2 selects the unconnected general input port (DB02-C §9).
  else if (reg == 3)
    value = dac.state.read_mode ? 3 : 0; // Standard DAC status is inside the 928 (DB02-C §6.7).
  else
    value = dac.read(reg);
  return true;
}

void NumberNineGXE::ramdac_write_b(u32 port, u8 value)
{
  const unsigned reg = dac_register(port);
  dac.write(reg, value);
  vga.dac.dirty = 1;
  if (reg == 6 || reg == 8 || reg == 9 || reg == 10)
    s3_define_video_mode();
  mark_video_dirty();
}

void NumberNineGXE::s3_define_video_mode()
{
  unsigned select = (vga.miscellaneous_output >> 2) & 3;
  clock.set_pins(select == 3 ? (s3.cr42 & 3) : select, cSystem->device_access_time()); // DB02-C §§8, 12.6.
  const bool enhanced = (s3.memory_config & 8) || (get_8514()->ibm8514.advfunction_ctrl & 1);
  const unsigned depth = dac.bits_per_pixel();
  svga.rgb8_en = enhanced && depth <= 8;
  svga.rgb15_en = 0;
  svga.rgb16_en = enhanced && depth == 16;
  svga.rgb24_en = 0;
  svga.rgb32_en = enhanced && depth == 24; // Bt485 24-bit RGB occupies a 32-bit port (Table 9).
  recompute_params_clock(1, external_clock_hz());
}

int NumberNineGXE::external_clock_hz() const
{
  return int(clock.frequency_hz(cSystem->device_access_time()) * (dac.clock_doubled() ? 2 : 1) / pixels_per_clock());
}

unsigned NumberNineGXE::pixels_per_clock() const
{
  // External SID feeds the Bt485 multiplexed pixel bus (DB02-C sec. 9, Bt485 Table 3).
  if (!(s3.extended_dac_ctrl & 8) || !(dac.state.command[2] & 0x20))
    return 1;
  const unsigned bits = dac.bits_per_pixel();
  if (bits == 24 || (bits == 16 && (dac.state.command[1] & 4)))
    return 1;
  return 32 / bits;
}

void NumberNineGXE::palette_update()
{
  for (unsigned i = 0; i < 256; ++i) {
    const u32 color = dac.indexed_color(u8(i));
    set_pen_color(i, u8(color >> 16), u8(color >> 8), u8(color));
  }
}

uint32_t NumberNineGXE::screen_update(screen_device& screen, bitmap_rgb32& bitmap, const rectangle& clip)
{
  const auto mode = pc_vga_choosevideomode();
  const bool packed = mode == RGB8_MODE || mode == RGB16_MODE || mode == RGB32_MODE;
  if (!packed) {
    CS3Trio64::screen_update(screen, bitmap, clip);
  } else {
    const bool attribute_pixels = !(s3.extended_dac_ctrl & 8) && !(s3.cr3a & 0x10);
    const unsigned depth = attribute_pixels ? 4 : dac.bits_per_pixel();
    const unsigned bytes = depth == 24 ? 4 : depth / 8;
    const u32 vram_mask = vram_display_mask();
    const int repeat = std::max(1, int(vga.crtc.maximum_scan_line) * (1 + vga.crtc.scan_doubling));
    const int split = (vga.crtc.line_compare & line_compare_mask()) + 1;
    for (int y = std::max(clip.min_y, 0); y <= std::min(clip.max_y, bitmap.height() - 1); ++y) {
      // CR31/51 extend the VGA display start, CR13/51 set the eight-byte pitch (DB02-C secs. 6, 7, 9).
      const u32 row = y < split ? (vga.crtc.start_addr << 2) + (y / repeat) * offset()
                                : ((y - split) / repeat) * offset();
      for (int x = std::max(clip.min_x, 0); x <= std::min(clip.max_x, bitmap.width() - 1); ++x) {
        if (depth == 4) {
          const u8 pixel = (vga.memory[(row + x / 2) & vram_mask] >> ((x & 1) ? 0 : 4)) & 0xf;
          bitmap.pix(y, x) = attribute_pixels ? vga.pens[pixel] : pen(pixel);
        } else if (depth == 8) {
          bitmap.pix(y, x) = pen(vga.memory[(row + x) & vram_mask]);
        } else {
          u32 pixel = 0;
          for (unsigned i = 0; i < bytes; ++i)
            pixel |= u32(vga.memory[(row + x * bytes + i) & vram_mask]) << (8 * i);
          bitmap.pix(y, x) = dac.direct_color(pixel);
        }
      }
    }
    if ((s3.cursor_mode & 1) && !(s3.extended_dac_ctrl & 0x20))
      s3_draw_hardware_cursor(bitmap.raw(), bitmap.width(), bitmap.width(), bitmap.height(), mode);
  }
  if (dac.state.command[2] & 3) {
    const int size = (dac.state.command[3] & 4) ? 64 : 32;
    const int left = int(dac.state.cursor_x) - size;
    const int top = int(dac.state.cursor_y) - size;
    for (int y = std::max({clip.min_y, top, 0}); y <= std::min({clip.max_y, top + size - 1, bitmap.height() - 1}); ++y)
      for (int x = std::max({clip.min_x, left, 0}); x <= std::min({clip.max_x, left + size - 1, bitmap.width() - 1}); ++x)
        bitmap.pix(y, x) = dac.cursor_pixel(x, y, bitmap.pix(y, x));
  }
  return 0;
}

// DB02-C secs. 11.3.6-7: image reads pad each row to the selected host transfer width.
u32 NumberNineGXE::read_transfer_word()
{
  auto& a = get_8514()->ibm8514;
  const bool across = (a.current_cmd & 2) != 0;
  const unsigned transfer = (a.current_cmd & 0x200) ? 2 : 1;
  const unsigned pixel_bits = a.color_bpp ? 8 * (a.color_bpp + 1) : (s3.cr3a & 0x10) ? 8 : 4;
  const unsigned bits = across ? 1 : pixel_bits;
  const unsigned width = (a.rect_width & 0xfff) + 1;
  const unsigned height = (a.rect_height & 0xfff) + 1;
  const unsigned stride = ((width * bits + transfer * 8 - 1) / (transfer * 8)) * transfer;
  if (!a.data_avail || a.fifo_idx <= 0 || unsigned(a.fifo_idx) > height * stride)
    return a.pixel_xfer;
  const unsigned done = height * stride - unsigned(a.fifo_idx);
  const unsigned row = done / stride;
  const int y = a.prev_y + ((a.current_cmd & 0x80) ? int(row) : -int(row));
  auto pixel_at = [&](unsigned column) -> u32 {
    if (column >= width)
      return 0;
    const int x = a.prev_x + ((a.current_cmd & 0x20) ? int(column) : -int(column));
    const u32 base = (a.multifunc_misc & 0x0c) << 18;
    const u32 address = base + (y & 0xfff) * offset() + (x & 0xfff) * pixel_bits / 8;
    if (pixel_bits == 4)
      return (vga.memory[address & vram_display_mask()] >> ((x & 1) ? 0 : 4)) & 15;
    u32 pixel = 0;
    for (unsigned i = 0; i < pixel_bits / 8; ++i)
      pixel |= u32(vga.memory[(address + i) & vram_display_mask()]) << (8 * i);
    return pixel;
  };
  u32 word = 0;
  for (unsigned lane = 0; lane < transfer; ++lane) {
    const unsigned row_byte = done % stride + lane;
    unsigned byte = 0;
    if (across) {
      for (unsigned i = 0; i < 8 && row_byte * 8 + i < width; ++i)
        if ((pixel_at(row_byte * 8 + i) & a.read_mask) == a.read_mask)
          byte |= 0x80 >> i;
    } else if (pixel_bits == 4) {
      byte = (pixel_at(row_byte * 2) << 4) | pixel_at(row_byte * 2 + 1);
    } else {
      const unsigned bpp = pixel_bits / 8;
      byte = (pixel_at(row_byte / bpp) >> ((row_byte % bpp) * 8)) & 0xff;
    }
    const unsigned output_lane = (transfer == 2 && !(a.current_cmd & 0x1000)) ? lane ^ 1 : lane;
    word |= byte << (8 * output_lane);
  }
  a.fifo_idx -= transfer;
  if (!a.fifo_idx) {
    a.data_avail = false;
    a.gpbusy = false;
    a.state = 0; // IBM8514_IDLE.
  }
  return word;
}

u8 NumberNineGXE::AccelIORead(u32 port)
{
  auto& a = get_8514()->ibm8514;
  if (port == 0x4ae8 || port == 0x4ae9)
    return u8((a.advfunction_ctrl | 2) >> ((port & 1) * 8)); // DB02-C sec. 10, ADVFUNC_CNTL.
  if (port == 0x9ae9) {
    const u16 status = get_8514()->ibm8514_gpstatus_r();
    const u8 data_available = !(a.current_cmd & 1) && a.data_avail ? 1 : 0;
    return (u8(status >> 8) & 2) | 4 | data_available; // DB02-C sec. 10, GP_STAT.
  }
  // Four-byte color and mask registers share the RSF halfword selector (DB02-C sec. 10).
  const unsigned reg = port & ~1u;
  if (a.color_bpp == 3 && (reg == 0xa2e8 || reg == 0xa6e8 || reg == 0xaae8 || reg == 0xaee8 || reg == 0xb2e8)) {
    const u8 value = CS3Trio64::AccelIORead(port + ((a.multifunc_misc & 0x10) ? 2 : 0));
    if (port & 1)
      a.multifunc_misc ^= 0x10;
    return value;
  }
  if (port >= 0xe2e8 && port <= 0xe2eb) {
    if (!(port & 1) && (a.current_cmd & 0xe101) == 0x4100)
      a.pixel_xfer = read_transfer_word();
    return u8(a.pixel_xfer >> ((port & 1) * 8));
  }
  return CS3Trio64::AccelIORead(port);
}

void NumberNineGXE::AccelIOWrite(u32 port, u8 data)
{
  auto& a = get_8514()->ibm8514;
  const unsigned reg = port & ~1u;
  if (a.color_bpp == 3 && (reg == 0xa2e8 || reg == 0xa6e8 || reg == 0xaae8 || reg == 0xaee8 || reg == 0xb2e8)) {
    CS3Trio64::AccelIOWrite(port + ((a.multifunc_misc & 0x10) ? 2 : 0), data);
    if (port & 1)
      a.multifunc_misc ^= 0x10;
    return;
  }
  CS3Trio64::AccelIOWrite(port, data);
  if (port == 0x9ae9 && (a.current_cmd & 0xe103) == 0x4100 && !a.color_bpp && !(s3.cr3a & 0x10)) {
    const int transfer = (a.current_cmd & 0x200) ? 2 : 1;
    const int row_bytes = ((a.rect_width & 0xfff) + 2) / 2;
    a.fifo_idx = ((row_bytes + transfer - 1) / transfer) * transfer * ((a.rect_height & 0xfff) + 1);
  }
}

int NumberNineGXE::SaveState(FILE* f)
{
  clock.checkpoint(cSystem->device_access_time());
  const u32 tag = 0x47584531; // GXE1.
  if (!f || fwrite(&tag, sizeof(tag), 1, f) != 1 ||
      fwrite(&dac.state, sizeof(dac.state), 1, f) != 1 ||
      fwrite(&clock.state, sizeof(clock.state), 1, f) != 1)
    return -1;
  return CS3Trio64::SaveState(f);
}

int NumberNineGXE::RestoreState(FILE* f)
{
  u32 tag = 0;
  Bt485::State saved_dac{};
  Icd2061A::State saved_clock{};
  if (!f || fread(&tag, sizeof(tag), 1, f) != 1 || tag != 0x47584531 ||
      fread(&saved_dac, sizeof(saved_dac), 1, f) != 1 || !Bt485::valid_state(saved_dac) ||
      fread(&saved_clock, sizeof(saved_clock), 1, f) != 1 || !Icd2061A::valid_state(saved_clock))
    return -1;
  if (CS3Trio64::RestoreState(f))
    return -1;
  dac.state = saved_dac;
  clock.state = saved_clock;
  clock.resume(cSystem->device_access_time());
  s3_define_video_mode();
  palette_update();
  return 0;
}
