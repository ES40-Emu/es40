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

#ifndef MAME_VIDEO_BT459_H
#define MAME_VIDEO_BT459_H

#include "emu/mame_shims.h"
#include "emu/address_map.h"

class bt459_device : public device_t
{
public:
  bt459_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock);

  static const u8 BT459_ID  = 0x4a; // magic number found in the id register
  static const u8 BT459_REV = 0xb0; // device revision B found in revision register

  static const int BT459_PIXEL_COLORS   = 256;
  static const int BT459_OVERLAY_COLORS = 16;
  static const int BT459_CURSOR_COLORS  = 3;

  enum address_mask
  {
    REG_OVERLAY_COLOR_0    = 0x0100,
    REG_OVERLAY_COLOR_1    = 0x0101,
    REG_OVERLAY_COLOR_2    = 0x0102,
    REG_OVERLAY_COLOR_3    = 0x0103,
    REG_OVERLAY_COLOR_4    = 0x0104,
    REG_OVERLAY_COLOR_5    = 0x0105,
    REG_OVERLAY_COLOR_6    = 0x0106,
    REG_OVERLAY_COLOR_7    = 0x0107,
    REG_OVERLAY_COLOR_8    = 0x0108,
    REG_OVERLAY_COLOR_9    = 0x0109,
    REG_OVERLAY_COLOR_10   = 0x010a,
    REG_OVERLAY_COLOR_11   = 0x010b,
    REG_OVERLAY_COLOR_12   = 0x010c,
    REG_OVERLAY_COLOR_13   = 0x010d,
    REG_OVERLAY_COLOR_14   = 0x010e,
    REG_OVERLAY_COLOR_15   = 0x010f,

    REG_CURSOR_COLOR_1     = 0x0181,
    REG_CURSOR_COLOR_2     = 0x0182,
    REG_CURSOR_COLOR_3     = 0x0183,

    REG_ID                 = 0x0200,
    REG_COMMAND_0          = 0x0201,
    REG_COMMAND_1          = 0x0202,
    REG_COMMAND_2          = 0x0203,
    REG_PIXEL_READ_MASK    = 0x0204,

    REG_PIXEL_BLINK_MASK   = 0x0206,

    REG_OVERLAY_READ_MASK  = 0x0208,
    REG_OVERLAY_BLINK_MASK = 0x0209,
    REG_INTERLEAVE         = 0x020a,
    REG_TEST               = 0x020b,
    REG_RED_SIGNATURE      = 0x020c,
    REG_GREEN_SIGNATURE    = 0x020d,
    REG_BLUE_SIGNATURE     = 0x020e,

    REG_REVISION           = 0x0220,

    REG_CURSOR_COMMAND     = 0x0300,
    REG_CURSOR_X_LO        = 0x0301,
    REG_CURSOR_X_HI        = 0x0302,
    REG_CURSOR_Y_LO        = 0x0303,
    REG_CURSOR_Y_HI        = 0x0304,
    REG_WINDOW_X_LO        = 0x0305,
    REG_WINDOW_X_HI        = 0x0306,
    REG_WINDOW_Y_LO        = 0x0307,
    REG_WINDOW_Y_HI        = 0x0308,
    REG_WINDOW_W_LO        = 0x0309,
    REG_WINDOW_W_HI        = 0x030a,
    REG_WINDOW_H_LO        = 0x030b,
    REG_WINDOW_H_HI        = 0x030c,

    CURSOR_RAM_START       = 0x0400,
    CURSOR_RAM_END         = 0x07ff,
    CURSOR_RAM_MASK        = 0x03ff,

    ADDRESS_LSB            = 0x00ff,
    ADDRESS_MSB            = 0x0f00,
    ADDRESS_MASK           = 0x0fff
  };

  enum command_0_mask
  {
    CR0706 = 0xc0, // multiplex select
    CR05   = 0x20, // overlay 0 enable
    CR04   = 0x10, // reserved
    CR0302 = 0x0c, // blink rate selection
    CR0100 = 0x03  // block mode
  };
  enum cr0706_mask
  {
    CR0706_51MPX    = 0xc0, // 5:1 multiplex
    CR0706_11MPX    = 0x80, // 1:1 multiplex
    CR0706_41MPX    = 0x40, // 4:1 multiplex
    CR0706_RESERVED = 0x00  // reserved
  };
  enum cr0302_mask
  {
    CR0302_6464 = 0x0c, // 64 on 64 off, 50/50
    CR0302_3232 = 0x08, // 32 on 32 off, 50/50
    CR0302_1616 = 0x04, // 16 on 16 off, 50/50
    CR0302_1648 = 0x00  // 16 on 48 off, 25/75
  };
  enum cr0100_mask
  {
    CR0100_1BPP = 0x03, // 1 bit per pixel
    CR0100_2BPP = 0x02, // 2 bits per pixel
    CR0100_4BPP = 0x01, // 4 bits per pixel
    CR0100_8BPP = 0x00  // 8 bits per pixel
  };

  enum command_1_mask
  {
    CR1715 = 0xe0, // pan select
    CR14   = 0x10, // reserved
    CR1310 = 0x0f  // zoom factor
  };

  enum command_2_mask
  {
    CR27   = 0x80, // sync enable
    CR26   = 0x40, // pedestal enable
    CR2524 = 0x30, // load palette RAM select
    CR23   = 0x08, // PLL select
    CR22   = 0x04, // X Windows overlay select
    CR21   = 0x02, // X Windows cursor select
    CR20   = 0x01  // test mode select
  };
  enum cr2524_mask
  {
    CR2524_BLUE   = 0x30,
    CR2524_GREEN  = 0x20,
    CR2524_RED    = 0x10,
    CR2524_NORMAL = 0x00
  };

  enum interleave_mask
  {
    CR3735 = 0xe0, // interleave select
    CR3432 = 0x1c, // first pixel select
    CR31   = 0x02, // overlay interleave enable
    CR30   = 0x01  // underlay enable
  };
  enum cr3735_mask
  {
    CR3735_4PIX = 0x80, // 4 pixels
    CR3735_3PIX = 0x60, // 3 pixels
    CR3735_2PIX = 0x40, // 2 pixels
    CR3735_1PIX = 0x20, // 1 pixel
    CR3735_0PIX = 0x00  // 0 pixels
  };
  enum cr3432_mask
  {
    CR3432_PIX_E = 0x10, // pixel E
    CR3432_PIX_D = 0x0c, // pixel D
    CR3432_PIX_C = 0x08, // pixel C
    CR3432_PIX_B = 0x04, // pixel B
    CR3432_PIX_A = 0x00  // pixel A
  };

  enum cursor_command_mask
  {
    CR47   = 0x80, // 64x64 cursor plane 1 enable
    CR46   = 0x40, // 64x64 cursor plane 0 enable
    CR45   = 0x20, // cross hair plane 1 enable
    CR44   = 0x10, // cross hair plane 0 enable
    CR43   = 0x08, // cursor format
    CR4241 = 0x06, // cross hair thickness
    CR40   = 0x01  // cursor blink enable
  };

  enum cr4241_mask
  {
    CR4241_1PIX = 0x00, // cross hair thickness 1 pixel
    CR4241_3PIX = 0x02, // cross hair thickness 3 pixels
    CR4241_5PIX = 0x04, // cross hair thickness 5 pixels
    CR4241_7PIX = 0x06  // cross hair thickness 7 pixels
  };

  void map(address_map& map) ATTR_COLD;
  u8 address_lo_r();
  void address_lo_w(u8 data);
  u8 address_hi_r();
  void address_hi_w(u8 data);
  u8 register_r();
  void register_w(u8 data);
  u8 palette_r();
  void palette_w(u8 data);

  void screen_update(screen_device& screen, bitmap_rgb32& bitmap, const rectangle& cliprect, u8* pixel_data);
  void set_contrast(u8 data) { m_contrast = data; }

  bool valid_state() const;
  void set_frame_number(u64 frame) { current_frame = frame; }
  void set_scanout(int horizontal, int vertical);
  unsigned bits_per_pixel() const { return 8u >> (m_command_0 & CR0100); }
  bool blink_changed(u64 frame) const;

protected:
  void device_start() override ATTR_COLD;
  void device_reset() override ATTR_COLD;

private:
  u32 indexed_color(u8 index, u64 frame) const;
  u8* indirect_color();
  u8 get_component(const u8* color, bool palette);
  void set_component(u8* color, u8 data, bool palette);
  void next_component(bool palette);
  u8 indirect(u8 data, bool writing);
  bool blink_on(u64 frame) const;
  u32 rgb(const u8* color) const;
  void draw_cursor(u32* pixels, int stride, const rectangle& clip, int x_origin, int y_origin,
    int hsync_to_active, int vsync_to_active, u64 frame) const;

  u8 m_palette_ram[BT459_PIXEL_COLORS * 3]{};
  u8 m_overlay_color[BT459_OVERLAY_COLORS * 3]{};
  u8 m_cursor_color[BT459_CURSOR_COLORS * 3]{};
  u8 m_cursor_ram[1024]{};
  u8 m_command_0 = 0;
  u8 m_command_1 = 0;
  u8 m_command_2 = 0;
  u8 m_pixel_read_mask = 0;
  u8 m_pixel_blink_mask = 0;
  u8 m_overlay_read_mask = 0;
  u8 m_overlay_blink_mask = 0;
  u8 m_interleave = 0;
  u8 m_test = 0;
  u8 m_signature[3]{};
  u8 m_cursor_command = 0;
  u8 m_coordinates[12]{};
  u16 m_display_coordinates[6]{};
  u16 m_address = 0;
  u8 m_address_rgb = 0;
  u64 m_blink_start = 0;
  u8 m_contrast = 0xff;

  u64 current_frame = 0;
  int hsync_to_active = 0;
  int vsync_to_active = 0;
};

DECLARE_DEVICE_TYPE(BT459, bt459_device)

#endif
