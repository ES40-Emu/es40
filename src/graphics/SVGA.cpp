/* ES40 Emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2003-2014 Nathan Woods from MAME
 * Copyright (C) 2000 Peter Trauner from MAME
 * Copyright (C) 2011-2026 Angelo Salese from MAME
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
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
#include "SVGA.h"

#include "emu.h"

/**************************************
 *
 * SVGA overrides
 *
 *************************************/

svga_device::svga_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: vga_device(mconfig, type, tag, owner, clock)
{
}

svga_device::svga_device(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev)
	: svga_device(machine_config(cfg, c, pcibus, pcidev), nullptr, nullptr, nullptr, 0)
{
}

void svga_device::zero()
{
	vga_device::zero();
	memset(&svga, 0, sizeof(svga));
}

void svga_device::device_start()
{
	vga_device::device_start();
	memset(&svga, 0, sizeof(svga));

	save_item(NAME(svga.bank_r));
	save_item(NAME(svga.bank_w));
	save_item(NAME(svga.rgb8_en));
	save_item(NAME(svga.rgb15_en));
	save_item(NAME(svga.rgb16_en));
	save_item(NAME(svga.rgb24_en));
	save_item(NAME(svga.rgb32_en));
	save_item(NAME(svga.id));
}

u16 svga_device::line_compare_mask()
{
	return 0x3ff;
}

void svga_device::svga_vh_rgb8(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	const int height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);

	const uint16_t mask_comp = line_compare_mask();
	int curr_addr = 0;

	// clear the backbuffer for 320x240 linear modes, avoid transitional artifacts
	// bitmap.fill(0, cliprect);

	//  uint16_t line_length;
	//  if(vga.crtc.dw)
	//      line_length = vga.crtc.offset << 3;  // doubleword mode
	//  else
	//  {
	//      line_length = vga.crtc.offset << 4;
	//  }

	const int VGA_COLUMNS = vga.crtc.horz_disp_end + 1;
	const int LINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);

	uint8_t start_shift = (!(vga.sequencer.data[4] & 0x08) || svga.ignore_chain4) ? 2 : 0;
	for (int addr = vga.crtc.start_addr << start_shift, line = 0; line < LINES; line += height, addr += offset(), curr_addr += offset())
	{
		for (int yi = 0; yi < height; yi++)
		{
			if ((line + yi) < (vga.crtc.line_compare & mask_comp))
				curr_addr = addr;
			if ((line + yi) == (vga.crtc.line_compare & mask_comp))
				curr_addr = 0;
			uint32_t* const bitmapline = &bitmap.pix(line + yi);
			addr %= vga.svga_intf.vram_size;
			for (int pos = curr_addr, c = 0, column = 0; column < VGA_COLUMNS; column++, c += 8, pos += 0x8)
			{
				if (pos + 0x08 >= vga.svga_intf.vram_size)
					return;

				for (int xi = 0; xi < 8; xi++)
				{
					const int dest_x = c + xi;

					if (!screen().visible_area().contains(dest_x, line + yi))
						continue;
					bitmapline[dest_x] = pen(vga.memory[pos + xi]);
				}
			}
		}
	}
}

void svga_device::svga_vh_rgb15(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	constexpr uint32_t IV = 0xff000000;
	const unsigned height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);

	const unsigned TLINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const unsigned TGA_COLUMNS = vga.crtc.horz_disp_end + 1;
	/* line compare is screen sensitive */
//  uint16_t mask_comp = 0xff | (TLINES & 0x300);
	int curr_addr = 0;
	// (void)curr_addr;
	int yi = 0;

	// bitmap.fill(0, cliprect);

	for (unsigned addr = vga.crtc.start_addr << 2, line = 0; line < TLINES; line += height, addr += offset(), curr_addr += offset())
	{
		uint32_t* const bitmapline = &bitmap.pix(line);
		addr %= vga.svga_intf.vram_size;
		for (unsigned pos = addr, c = 0, column = 0; column < TGA_COLUMNS; column++, c += 8, pos += 0x10)
		{
			if (pos + 0x10 >= vga.svga_intf.vram_size)
				return;
			for (int xi = 0, xm = 0; xi < 8; xi++, xm += 2)
			{
				const int dest_x = c + xi;

				if (!screen().visible_area().contains(dest_x, line + yi))
					continue;

				const u16 MV = (vga.memory[pos + xm] + (vga.memory[pos + xm + 1] << 8));
				int r = (MV & 0x7c00) >> 10;
				int g = (MV & 0x03e0) >> 5;
				int b = (MV & 0x001f) >> 0;
				r = (r << 3) | (r & 0x7);
				g = (g << 3) | (g & 0x7);
				b = (b << 3) | (b & 0x7);
				bitmapline[dest_x] = IV | (r << 16) | (g << 8) | (b << 0);
			}
		}
	}
}

void svga_device::svga_vh_rgb16(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	constexpr uint32_t IV = 0xff000000;
	const unsigned height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);
	const unsigned TLINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const unsigned TGA_COLUMNS = vga.crtc.horz_disp_end + 1;

	/* line compare is screen sensitive */
//  uint16_t mask_comp = 0xff | (TLINES & 0x300);
	int curr_addr = 0;
	// (void)curr_addr;
	int yi = 0;

	// bitmap.fill(0, cliprect);

	for (unsigned addr = vga.crtc.start_addr << 2, line = 0; line < TLINES; line += height, addr += offset(), curr_addr += offset())
	{
		uint32_t* const bitmapline = &bitmap.pix(line);
		addr %= vga.svga_intf.vram_size;
		for (unsigned pos = addr, c = 0, column = 0; column < TGA_COLUMNS; column++, c += 8, pos += 0x10)
		{
			if (pos + 0x10 >= vga.svga_intf.vram_size)
				return;
			for (int xi = 0, xm = 0; xi < 8; xi++, xm += 2)
			{
				const int dest_x = c + xi;

				if (!screen().visible_area().contains(dest_x, line + yi))
					continue;

				const u16 MV = (vga.memory[pos + xm] + (vga.memory[pos + xm + 1] << 8));
				int r = (MV & 0xf800) >> 11;
				int g = (MV & 0x07e0) >> 5;
				int b = (MV & 0x001f) >> 0;
				r = (r << 3) | (r & 0x7);
				g = (g << 2) | (g & 0x3);
				b = (b << 3) | (b & 0x7);
				bitmapline[dest_x] = IV | (r << 16) | (g << 8) | (b << 0);
			}
		}
	}
}

void svga_device::svga_vh_rgb24(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	constexpr uint32_t ID = 0xff000000;
	const int height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);
	const int TLINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const int TGA_COLUMNS = vga.crtc.horz_disp_end + 1;

	/* line compare is screen sensitive */
//  uint16_t mask_comp = 0xff | (TLINES & 0x300);
	int curr_addr = 0;
	// (void)curr_addr;
	int yi = 0;

	// bitmap.fill(0, cliprect);

	for (int addr = vga.crtc.start_addr << 3, line = 0; line < TLINES; line += height, addr += offset(), curr_addr += offset())
	{
		uint32_t* const bitmapline = &bitmap.pix(line);
		addr %= vga.svga_intf.vram_size;
		for (int pos = addr, c = 0, column = 0; column < TGA_COLUMNS; column++, c += 8, pos += 24)
		{
			if (pos + 24 >= vga.svga_intf.vram_size)
				return;
			for (int xi = 0, xm = 0; xi < 8; xi++, xm += 3)
			{
				const int dest_x = c + xi;

				if (!screen().visible_area().contains(dest_x, line + yi))
					continue;

				const u32 MD = (vga.memory[pos + xm] + (vga.memory[pos + xm + 1] << 8) + (vga.memory[pos + xm + 2] << 16));

				int r = (MD & 0xff0000) >> 16;
				int g = (MD & 0x00ff00) >> 8;
				int b = (MD & 0x0000ff) >> 0;
				bitmapline[dest_x] = ID | (r << 16) | (g << 8) | (b << 0);
			}
		}
	}
}

void svga_device::svga_vh_rgb32(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	constexpr uint32_t ID = 0xff000000;
	const int height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);
	const int TLINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const int TGA_COLUMNS = vga.crtc.horz_disp_end + 1;

	//  uint16_t mask_comp;

		/* line compare is screen sensitive */
	//  mask_comp = 0xff | (TLINES & 0x300);
	int curr_addr = 0;
	// (void)curr_addr;
	int yi = 0;

	// bitmap.fill(0, cliprect);

	for (int addr = vga.crtc.start_addr << 2, line = 0; line < TLINES; line += height, addr += offset(), curr_addr += offset())
	{
		uint32_t* const bitmapline = &bitmap.pix(line);
		addr %= vga.svga_intf.vram_size;
		for (int pos = addr, c = 0, column = 0; column < TGA_COLUMNS; column++, c += 8, pos += 0x20)
		{
			if (pos + 0x20 >= vga.svga_intf.vram_size)
				return;
			for (int xi = 0, xm = 0; xi < 8; xi++, xm += 4)
			{
				if (!screen().visible_area().contains(c + xi, line + yi))
					continue;

				const u32 MD = (vga.memory[pos + xm] + (vga.memory[pos + xm + 1] << 8) + (vga.memory[pos + xm + 2] << 16));
				int r = (MD & 0xff0000) >> 16;
				int g = (MD & 0x00ff00) >> 8;
				int b = (MD & 0x0000ff) >> 0;
				bitmapline[c + xi] = ID | (r << 16) | (g << 8) | (b << 0);
			}
		}
	}
}

// TODO: inherit from base class
uint8_t svga_device::pc_vga_choosevideomode()
{
	if (vga.crtc.sync_en)
	{
		if (vga.dac.dirty)
		{
			palette_update();
			vga.dac.dirty = 0;
		}

		if (vga.attribute.data[0x10] & 0x80)
		{
			for (int i = 0; i < 16; i++)
			{
				vga.pens[i] = pen((vga.attribute.data[i] & 0x0f)
					| ((vga.attribute.data[0x14] & 0xf) << 4));
			}
		}
		else
		{
			for (int i = 0; i < 16; i++)
			{
				vga.pens[i] = pen((vga.attribute.data[i] & 0x3f)
					| ((vga.attribute.data[0x14] & 0xc) << 4));
			}
		}

		if (svga.rgb32_en)
		{
			return RGB32_MODE;
		}
		else if (svga.rgb24_en)
		{
			return RGB24_MODE;
		}
		else if (svga.rgb16_en)
		{
			return RGB16_MODE;
		}
		else if (svga.rgb15_en)
		{
			return RGB15_MODE;
		}
		else if (svga.rgb8_en)
		{
			return RGB8_MODE;
		}
		else if (!vga.gc.alpha_dis) // !GRAPHIC_MODE
		{
			return TEXT_MODE;
		}
		else if (vga.gc.shift256)
		{
			return VGA_MODE;
		}
		else if (vga.gc.shift_reg)
		{
			return CGA_MODE;
		}
		else if (vga.gc.memory_map_sel == 0x03)
		{
			return MONO_MODE;
		}
		else
		{
			return EGA_MODE;
		}
	}

	return SCREEN_OFF;
}

uint8_t svga_device::get_video_depth()
{
	switch(pc_vga_choosevideomode())
	{
		case VGA_MODE:
		case RGB8_MODE:
			return 8;
		case RGB15_MODE:
		case RGB16_MODE:
			return 16;
		case RGB24_MODE:
		case RGB32_MODE:
			return 32;
	}
	return 0;
}

// TODO: inherit from base class
uint32_t svga_device::screen_update(screen_device& screen, bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	uint8_t cur_mode = pc_vga_choosevideomode();

	switch (cur_mode)
	{
	case SCREEN_OFF:   bitmap.fill(black_pen(), cliprect); break;
	case TEXT_MODE:    vga_vh_text(bitmap, cliprect); break;
	case VGA_MODE:     vga_vh_vga(bitmap, cliprect); break;
	case EGA_MODE:     vga_vh_ega(bitmap, cliprect); break;
	case CGA_MODE:     vga_vh_cga(bitmap, cliprect); break;
	case MONO_MODE:    vga_vh_mono(bitmap, cliprect); break;
	case RGB8_MODE:    svga_vh_rgb8(bitmap, cliprect); break;
	case RGB15_MODE:   svga_vh_rgb15(bitmap, cliprect); break;
	case RGB16_MODE:   svga_vh_rgb16(bitmap, cliprect); break;
	case RGB24_MODE:   svga_vh_rgb24(bitmap, cliprect); break;
	case RGB32_MODE:   svga_vh_rgb32(bitmap, cliprect); break;
	}

	return 0;
}
