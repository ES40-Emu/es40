/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2015-2026 MAME project
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

// license:BSD-3-Clause
// copyright-holders:Barry Rodewald
/*
 *
 * Cirrus Logic CL-GD542x/3x video chipsets
 *
 * TODO:
 * - Original Acumos AVGA1/2 chipsets (Cirrus Logic eventually bought Acumos and rebranded);
 * - Fix or implement hidden DAC modes (15bpp + mixed, true color, others);
 * - zorro/picasso2: many blitting errors, verify HW cursor;
 * - Merge with trs/vis.cpp implementation (CL-GD5200 RAMDAC with custom VGA controller)
 *
 */

#include "StdAfx.h"
#include "emu.h"
#include "Cirrus.h"

//#include "screen.h"
#include "system/System.h"
#include "emu/xtal.h"
#include <chrono>
#include <type_traits>


#define LOG_REGS (1U << 1)
#define LOG_BLIT (1U << 2)
#define LOG_HDAC (1U << 3) // log hidden DAC
#define LOG_BANK (1U << 4) // log offset registers
#define LOG_PLL  (1U << 5)

#define VERBOSE (LOG_GENERAL | LOG_HDAC | LOG_REGS | LOG_BLIT)
//#define LOG_OUTPUT_FUNC osd_printf_info
#include "logmacro.h"

// NOTE: use gd54xx shortname here, clgd54xx for ISA16 cards to avoid validation clashing
DEFINE_DEVICE_TYPE(CIRRUS_GD5401_VGA, cirrus_gd5401_vga_device, "gd5401", "Cirrus Logic GD5401 / Acumos AVGA1 i/f")
DEFINE_DEVICE_TYPE(CIRRUS_GD5428_VGA, cirrus_gd5428_vga_device, "gd5428", "Cirrus Logic GD5428 VGA i/f")
DEFINE_DEVICE_TYPE(CIRRUS_GD5430_VGA, cirrus_gd5430_vga_device, "gd5430", "Cirrus Logic GD5430 VGA i/f")
DEFINE_DEVICE_TYPE(CIRRUS_GD5446_VGA, cirrus_gd5446_vga_device, "gd5446", "Cirrus Logic GD5446 VGA i/f")

/*
 * Acumos AVGA1
 *
 * Bog standard VGA interface, with 132-char text mode (cfr. page 64),
 * provision to feature connector and actually defined external video clocks.
 *
 */

cirrus_gd5401_vga_device::cirrus_gd5401_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
//	: vga_device(mconfig, CIRRUS_GD5401_VGA, tag, owner, clock)
	: CCirrus(mconfig, CIRRUS_GD5401_VGA, tag, owner, clock)
{
}

// TODO: also can select external video clock modes thru strapped CF(5) = 0
// cfr. page 63
void cirrus_gd5401_vga_device::recompute_params()
{
	u8 xtal_select = (vga.miscellaneous_output & 0x0c) >> 2;
	int xtal;

	switch(xtal_select & 3)
	{
		case 0: xtal = XTAL(25'174'800).value(); break;
		case 1: xtal = XTAL(28'636'363).value(); break;
		case 2: xtal = XTAL(41'164'000).value(); break;
		case 3: xtal = XTAL(35'795'000).value(); break;
	}

	recompute_params_clock(1, xtal);
}


cirrus_gd5428_vga_device::cirrus_gd5428_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: cirrus_gd5428_vga_device(mconfig, CIRRUS_GD5428_VGA, tag, owner, clock)
{
	m_crtc_space_config = address_space_config("crtc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5428_vga_device::crtc_map), this));
	m_gc_space_config = address_space_config("gc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5428_vga_device::gc_map), this));
	m_seq_space_config = address_space_config("sequencer_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5428_vga_device::sequencer_map), this));
}

cirrus_gd5428_vga_device::cirrus_gd5428_vga_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
//	: svga_device(mconfig, type, tag, owner, clock)
	: CCirrus(mconfig, type, tag, owner, clock)
{
	// Preserve ES40 constructor clearing for members without initializers.
	m_chip_id = 0;
	memset(m_ext_palette, 0, sizeof(m_ext_palette));
}

cirrus_gd5430_vga_device::cirrus_gd5430_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: cirrus_gd5430_vga_device(mconfig, CIRRUS_GD5430_VGA, tag, owner, clock)
{
	m_crtc_space_config = address_space_config("crtc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5430_vga_device::crtc_map), this));
	m_gc_space_config = address_space_config("gc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5430_vga_device::gc_map), this));
	m_seq_space_config = address_space_config("sequencer_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5430_vga_device::sequencer_map), this));
}

cirrus_gd5430_vga_device::cirrus_gd5430_vga_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: cirrus_gd5428_vga_device(mconfig, type, tag, owner, clock)
{
}

cirrus_gd5446_vga_device::cirrus_gd5446_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: cirrus_gd5430_vga_device(mconfig, CIRRUS_GD5446_VGA, tag, owner, clock)
{
	m_crtc_space_config = address_space_config("crtc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5446_vga_device::crtc_map), this));
	m_gc_space_config = address_space_config("gc_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5446_vga_device::gc_map), this));
	m_seq_space_config = address_space_config("sequencer_regs", ENDIANNESS_LITTLE, 8, 8, 0, address_map_constructor(FUNC(cirrus_gd5446_vga_device::sequencer_map), this));
}

void cirrus_gd5428_vga_device::io_3cx_map(address_map &map)
{
	svga_device::io_3cx_map(map);
	map(0x06, 0x06).rw(FUNC(cirrus_gd5428_vga_device::ramdac_hidden_mask_r), FUNC(cirrus_gd5428_vga_device::ramdac_hidden_mask_w));
	map(0x09, 0x09).rw(FUNC(cirrus_gd5428_vga_device::ramdac_overlay_r), FUNC(cirrus_gd5428_vga_device::ramdac_overlay_w));
}

u8 cirrus_gd5428_vga_device::ramdac_hidden_mask_r(offs_t offset)
{
	if (!machine().side_effects_disabled())
		m_hidden_dac_phase ++;
	if (m_hidden_dac_phase >= 4)
	{
		u8 res;
		// TODO: '5420 doesn't have this
		// TODO: '5428 reads do not lock the Hidden DAC
		res = m_hidden_dac_mode;
		//m_hidden_dac_phase = 0;
		LOGMASKED(LOG_HDAC, "CL: Hidden DAC read (%02x)\n", res);
		return res;
	}

	return vga_device::ramdac_mask_r(0);
}

void cirrus_gd5428_vga_device::ramdac_hidden_mask_w(offs_t offset, u8 data)
{
	if (m_hidden_dac_phase >= 4)
	{
		// TODO: '5420 doesn't have this
		// TODO: '5428 reads do not lock the Hidden DAC
		m_hidden_dac_mode = data;
		m_hidden_dac_phase = 0;
		recompute_params();
		LOGMASKED(LOG_HDAC, "CL: Hidden DAC write %02x\n", data);
		return;
	}

	vga_device::ramdac_mask_w(0, data);
}

u8 cirrus_gd5428_vga_device::ramdac_overlay_r(offs_t offset)
{
	if(!m_ext_palette_enabled)
		return vga_device::ramdac_data_r(0);

	u8 res = 0xff;
	if (vga.dac.read && !machine().side_effects_disabled())
	{
		switch (vga.dac.state++)
		{
			case 0:
				res = m_ext_palette[vga.dac.read_index & 0x0f].red;
				break;
			case 1:
				res = m_ext_palette[vga.dac.read_index & 0x0f].green;
				break;
			case 2:
				res = m_ext_palette[vga.dac.read_index & 0x0f].blue;
				break;
		}

		if (vga.dac.state == 3)
		{
			vga.dac.state = 0;
			vga.dac.read_index++;
		}
	}

	return res;
}

void cirrus_gd5428_vga_device::ramdac_overlay_w(offs_t offset, u8 data)
{
	if(!m_ext_palette_enabled)
	{
		vga_device::ramdac_data_w(0, data);
		return;
	}

	if (!vga.dac.read)
	{
		switch (vga.dac.state++) {
		case 0:
			m_ext_palette[vga.dac.write_index & 0x0f].red=data;
			break;
		case 1:
			m_ext_palette[vga.dac.write_index & 0x0f].green=data;
			break;
		case 2:
			m_ext_palette[vga.dac.write_index & 0x0f].blue=data;
			break;
		}
		vga.dac.dirty=1;
		if (vga.dac.state==3)
		{
			vga.dac.state=0;
			vga.dac.write_index++;
		}
	}
}

void cirrus_gd5428_vga_device::crtc_map(address_map &map)
{
	svga_device::crtc_map(map);
	// VGA Vertical Blank end
	// some SVGA chipsets use all 8 bits, and this is one of them (according to MFGTST CRTC tests)
	map(0x16, 0x16).lrw8(
		NAME([this] (offs_t offset) {
			return vga.crtc.vert_blank_end & 0x00ff;
		}),
		NAME([this] (offs_t offset, u8 data) {
			vga.crtc.vert_blank_end &= ~0x00ff;
			vga.crtc.vert_blank_end |= data;
			recompute_params();
		})
	);
	map(0x19, 0x19).lrw8(
		NAME([this] (offs_t offset) {
			return m_cr19;
		}),
		NAME([this] (offs_t offset, u8 data) {
			LOGMASKED(LOG_REGS, "CR19: Interlace End %02x\n", data);
			m_cr19 = data;
		})
	);
	map(0x1a, 0x1a).lrw8(
		NAME([this] (offs_t offset) {
			return m_cr1a;
		}),
		NAME([this] (offs_t offset, u8 data) {
			LOGMASKED(LOG_REGS, "CR1A: Interlace Control %02x\n", data);
			m_cr1a = data;
			vga.crtc.horz_blank_end = (vga.crtc.horz_blank_end & 0xff3f) | ((data & 0x30) << 2);
			vga.crtc.vert_blank_end = (vga.crtc.vert_blank_end & 0xfcff) | ((data & 0xc0) << 2);
			recompute_params();
		})
	);
	map(0x1b, 0x1b).lrw8(
		NAME([this] (offs_t offset) {
			return m_cr1b;
		}),
		NAME([this] (offs_t offset, u8 data) {
			LOGMASKED(LOG_REGS, "CR1B: Extended Display Controls %02x\n", data);
			m_cr1b = data;
			vga.crtc.start_addr_latch &= ~0x070000;
			vga.crtc.start_addr_latch |= ((data & 0x01) << 16);
			vga.crtc.start_addr_latch |= ((data & 0x0c) << 15);
			vga.crtc.offset = (vga.crtc.offset & 0x00ff) | ((data & 0x10) << 4);
			recompute_params();
		})
	);
//  map(0x25, 0x25) PSR Part Status (r/o, "factory testing and internal tracking only")
	map(0x27, 0x27).lr8(
		NAME([this] (offs_t offset) {
			// NOTE: verbose
			//LOGMASKED(LOG_REGS, "CR27: Read ID\n");
			return m_chip_id;
		})
	);
}

void cirrus_gd5428_vga_device::gc_map(address_map &map)
{
	svga_device::gc_map(map);
	map(0x00, 0x00).lrw8(
		NAME([this](offs_t offset) {
			return vga.gc.set_reset & ((gc_mode_ext & 0x04) ? 0xff : 0x0f);
		}),
		NAME([this](offs_t offset, u8 data) {
			// if extended writes are enabled (bit 2 of index 0bh), then index 0 and 1 are extended to 8 bits,
			// however XFree86 does not appear to do this...
			vga.gc.set_reset = data & 0xff;
		})
	);
	map(0x01, 0x01).lrw8(
		NAME([this](offs_t offset) {
			return vga.gc.enable_set_reset & ((gc_mode_ext & 0x04) ? 0xff : 0x0f);
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.enable_set_reset = data & 0xff;
		})
	);
	map(0x05, 0x05).lrw8(
		NAME([this](offs_t offset) {
			u8 res = (vga.gc.shift256 & 1) << 6;
			res |= (vga.gc.shift_reg & 1) << 5;
			res |= (vga.gc.host_oe & 1) << 4;
			res |= (vga.gc.read_mode & 1) << 3;
			if(gc_mode_ext & 0x04)
				res |= (vga.gc.write_mode & 7);
			else
				res |= (vga.gc.write_mode & 3);
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.shift256 = BIT(data, 6);
			vga.gc.shift_reg = BIT(data, 5);
			vga.gc.host_oe = BIT(data, 4);
			vga.gc.read_mode = BIT(data, 3);
			if(gc_mode_ext & 0x04)
				vga.gc.write_mode = data & 7;
			else
				vga.gc.write_mode = data & 3;
		})
	);
	// Offset register 0/1
	map(0x09, 0x0a).lrw8(
		NAME([this](offs_t offset) {
			return gc_bank[offset];
		}),
		NAME([this](offs_t offset, u8 data) {
			gc_bank[offset] = data;
			LOGMASKED(LOG_BANK, "GR%d: Offset register %d set to %02x\n", offset + 9, offset, data);
		})
	);
	// Graphics controller mode extensions
	map(0x0b, 0x0b).lrw8(
		NAME([this](offs_t offset) {
			return gc_mode_ext;
		}),
		NAME([this](offs_t offset, u8 data) {
			LOGMASKED(LOG_REGS, "GRB: Graphics Controller Mode Extensions %02x\n", data);
			gc_mode_ext = data;
			if(!(data & 0x04))
			{
				vga.gc.set_reset &= 0x0f;
				vga.gc.enable_set_reset &= 0x0f;
			}
			if(!(data & 0x08))
				vga.sequencer.map_mask &= 0x0f;
		})
	);
	// Colour Key
	// map(0x0c, 0x0c)
	// Colour Key Mask
	// map(0x0d, 0x0d)
	// Miscellaneous Control
	// map(0x0e, 0x0e)
	// Background Colour Byte 1
	map(0x10, 0x10).lrw8(
		NAME([this](offs_t offset) {
			return m_gr10;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_gr10 = data;
		})
	);
	// Foreground Colour Byte 1
	map(0x11, 0x11).lrw8(
		NAME([this](offs_t offset) {
			return m_gr11;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_gr11 = data;
		})
	);
	// BLT Width 0
	map(0x20, 0x20).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_width & 0x00ff;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_width = (m_blt_width & 0xff00) | data;
			LOGMASKED(LOG_BLIT, "CL: blt_width %02x [0] (%04x)\n", data, m_blt_width);
		})
	);
	// BLT Width 1
	map(0x21, 0x21).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_width >> 8;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_width = (m_blt_width & 0x00ff) | (data << 8);
			LOGMASKED(LOG_BLIT, "CL: blt_width %02x [1] (%04x)\n", data, m_blt_width);
		})
	);
	// BLT Height 0
	map(0x22, 0x22).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_height & 0x00ff;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_height = (m_blt_height & 0xff00) | data;
			LOGMASKED(LOG_BLIT, "CL: m_blt_height %02x [0] (%04x)\n", data, m_blt_height);
		})
	);
	// BLT Height 1
	map(0x23, 0x23).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_height >> 8;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_height = (m_blt_height & 0x00ff) | (data << 8);
			LOGMASKED(LOG_BLIT, "CL: m_blt_height %02x [1] (%04x)\n", data, m_blt_height);
		})
	);
	// BLT Destination Pitch 0
	map(0x24, 0x24).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_dest_pitch & 0x00ff;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_dest_pitch = (m_blt_dest_pitch & 0xff00) | data;
		})
	);
	// BLT Destination Pitch 1
	map(0x25, 0x25).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_dest_pitch >> 8;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_dest_pitch = (m_blt_dest_pitch & 0x00ff) | (data << 8);
		})
	);
	// BLT Source Pitch 0
	map(0x26, 0x26).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_source_pitch & 0x00ff;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_source_pitch = (m_blt_source_pitch & 0xff00) | data;
		})
	);
	// BLT Source Pitch 1
	map(0x27, 0x27).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_source_pitch >> 8;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_source_pitch = (m_blt_source_pitch & 0x00ff) | (data << 8);
		})
	);
	// BLT Destination start 0/1/2
	map(0x28, 0x2a).lrw8(
		NAME([this](offs_t offset) {
			return (m_blt_dest >> (8 * (offset & 3))) & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			const u8 byte_access = (8 * (offset & 3));
			const u32 old_mask = ~(0xff << byte_access);
			m_blt_dest = (m_blt_dest & old_mask) | (data << byte_access);
		})
	);
	// BLT source start 0/1/2
	map(0x2c, 0x2e).lrw8(
		NAME([this](offs_t offset) {
			return (m_blt_source >> (8 * (offset & 3))) & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			const u8 byte_access = (8 * (offset & 3));
			const u32 old_mask = ~(0xff << byte_access);
			m_blt_source = (m_blt_source & old_mask) | (data << byte_access);
		})
	);
	// BLT destination write mask (GD5430/36/40 only)
	// map(0x2f, 0x2f)
	// BLT Mode
	map(0x30, 0x30).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_mode;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_mode = data;
		})
	);
	// BitBLT Start / Status
	map(0x31, 0x31).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_status;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_status = data & ~0xf2;
			if (BIT(data, 2))
			{
				m_blt_status &= ~9;
				m_blt_system_transfer = false;
				m_blt_system_count = 0;
				m_blt_system_buffer = 0;
			}
			else if (BIT(data, 1))
			{
				if(m_blt_mode & 0x04)  // blit source is system memory
					start_system_bitblt();
				else
					start_bitblt();
			}
		})
	);
	// BitBLT ROP mode
	map(0x32, 0x32).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_rop;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_rop = data;
		})
	);
	// BitBLT Transparent Colour 0
	map(0x34, 0x34).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_trans_colour & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_trans_colour = (m_blt_trans_colour & 0xff00) | data;
		})
	);
	// BitBLT Transparent Colour 1
	map(0x35, 0x35).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_trans_colour >> 8;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_trans_colour = (m_blt_trans_colour & 0x00ff) | (data << 8);
		})
	);
	// BitBLT Transparent Colour Mask 0
	map(0x38, 0x38).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_trans_colour_mask & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_trans_colour_mask = (m_blt_trans_colour_mask & 0xff00) | data;
		})
	);
	// BitBLT Transparent Colour Mask 1
	map(0x39, 0x39).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_trans_colour_mask >> 8;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_trans_colour_mask = (m_blt_trans_colour_mask & 0x00ff) | (data << 8);
		})
	);
}

void cirrus_gd5428_vga_device::sequencer_map(address_map &map)
{
	svga_device::sequencer_map(map);
	map(0x02, 0x02).lrw8(
		NAME([this] (offs_t offset) {
			return vga.sequencer.map_mask & ((gc_mode_ext & 0x08) ? 0xff : 0x0f);
		}),
		NAME([this] (offs_t offset, u8 data) {
			vga.sequencer.map_mask = data & ((gc_mode_ext & 0x08) ? 0xff : 0x0f);
		})
	);
	map(0x06, 0x06).lrw8(
		NAME([this] (offs_t offset) {
			return m_lock_reg;
		}),
		NAME([this] (offs_t offset, u8 data) {
			// bits 3,5,6,7 ignored
			m_lock_reg = ((data & 0x17) == 0x12) ? 0x12 : 0x0f;
			if (m_chip_id < 0x9c)
				gc_locked = (m_lock_reg != 0x12);
			LOG("Cirrus register extensions %s\n", gc_locked ? "unlocked" : "locked");
			recompute_params();
		})
	);
	map(0x07, 0x07).lrw8(
		NAME([this] (offs_t offset) {
			return vga.sequencer.data[0x07];
		}),
		NAME([this] (offs_t offset, u8 data) {
			// xxxx ---- Memory Segment
			// 0000 ---- VGA segmenting
			// llll ---- (any other value) Linear addressing
			// ---- -xx- CRTC Character Clock Divider
			// ---- ---x Select 8bpp High Resolution Mode
			vga.sequencer.data[0x07] = data;
			LOGMASKED(LOG_REGS, "SR7: Extended Sequencer Mode %02x\n", data);
			recompute_params();
		})
	);
	// TODO: check me
	map(0x09, 0x09).lrw8(
		NAME([this] (offs_t offset) {
			return vga.sequencer.data[0x09];
		}),
		NAME([this] (offs_t offset, u8 data) {
			vga.sequencer.data[0x09] = data;
		})
	);
	map(0x0a, 0x0a).lrw8(
		NAME([this] (offs_t offset) {
			return m_scratchpad1;
		}),
		NAME([this] (offs_t offset, u8 data) {
			// GD5402/GD542x BIOS writes VRAM size here
			m_scratchpad1 = data;
		})
	);
	map(0x0b, 0x0e).lrw8(
		NAME([this] (offs_t offset) {
			return m_vclk_num[offset];
		}),
		NAME([this] (offs_t offset, u8 data) {
			m_vclk_num[offset] = data;
			recompute_params();
		})
	);
	map(0x0f, 0x0f).lrw8(
		NAME([this] (offs_t offset) {
			u8 res = vga.sequencer.data[0x0f] & ~0x98;
			u32 vram_k = vga.svga_intf.vram_size >> 10;
			if (vram_k == 512)
				res |= 0x08; // 16-bit DRAM
			else if (vram_k == 1024)
				res |= 0x10; // 32-bit DRAM (1MB)
			else if (vram_k == 2048)
				res |= 0x18; // 32-bit DRAM (2MB)
			else if (vram_k == 4096)
				res |= 0x98; // 64-bit DRAM (4MB)
			return res;
		}),
		NAME([this] (offs_t offset, u8 data) {
			vga.sequencer.data[0x0f] = data;
		})
	);
	// bits 5-7 of the register index are the low bits of the X co-ordinate
	map(0x10, 0x10).select(0xe0).lw8(
		NAME([this] (offs_t offset, u8 data) {
			m_cursor_x = (data << 3) | ((offset & 0xe0) >> 5);
		})
	);
	// bits 5-7 of the register index are the low bits of the Y co-ordinate
	map(0x11, 0x11).select(0xe0).lw8(
		NAME([this] (offs_t offset, u8 data) {
			m_cursor_y = (data << 3) | ((offset & 0xe0) >> 5);
		})
	);
	map(0x12, 0x12).lrw8(
		NAME([this] (offs_t offset) {
			return m_cursor_attr;
		}),
		NAME([this] (offs_t offset, u8 data) {
			// bit 0 - enable cursor
			// bit 1 - enable extra palette (cursor colours are there)
			// bit 2 - 64x64 cursor (32x32 if clear, GD5422+)
			// bit 7 - overscan colour protect - if set, use colour 2 in the extra palette for the border (GD5424+)
			m_cursor_attr = data;
			m_ext_palette_enabled = data & 0x02;
		})
	);
	map(0x13, 0x13).lw8(
		NAME([this] (offs_t offset, u8 data) {
			// bits 0 and 1 are ignored if using 64x64 cursor
			m_cursor_addr = data;
		})
	);
	map(0x14, 0x14).lrw8(
		NAME([this] (offs_t offset) {
			return m_scratchpad2;
		}),
		NAME([this] (offs_t offset, u8 data) {
			m_scratchpad2 = data;
		})
	);
	map(0x15, 0x15).lrw8(
		NAME([this] (offs_t offset) {
			return m_scratchpad3;
		}),
		NAME([this] (offs_t offset, u8 data) {
			// GD543x BIOS writes VRAM size here
			m_scratchpad3 = data;
		})
	);
	//TODO: SR17: Configuration Readback and Extended Control Register (Except CL-GD5420)
	map(0x1b, 0x1e).lrw8(
		NAME([this] (offs_t offset) {
			return m_vclk_denom[offset];
		}),
		NAME([this] (offs_t offset, u8 data) {
			m_vclk_denom[offset] = data;
			recompute_params();
		})
	);
}

void cirrus_gd5428_vga_device::device_start()
{
	svga_device::device_start();
	zero();

	for (int i = 0; i < 0x100; i++)
		set_pen_color(i, 0, 0, 0);

	// Avoid an infinite loop when displaying.  0 is not possible anyway.
	vga.crtc.maximum_scan_line = 1;

	// copy over interfaces
	//vga.memory = std::make_unique<uint8_t []>(vga.svga_intf.vram_size);
	//memset(&vga.memory[0], 0, vga.svga_intf.vram_size);

	//save_pointer(NAME(vga.memory), vga.svga_intf.vram_size);
	save_pointer(vga.crtc.data,"CRTC Registers",0x100);
	save_pointer(vga.sequencer.data,"Sequencer Registers",0x100);
	save_pointer(vga.attribute.data,"Attribute Registers", 0x15);
	save_item(NAME(m_chip_id));
	save_item(NAME(m_hidden_dac_phase));
	save_item(NAME(m_hidden_dac_mode));
	save_pointer(NAME(gc_bank), 2);
	save_item(NAME(m_blt_write_mask));

	//m_vblank_timer = timer_alloc(FUNC(cirrus_gd5428_vga_device::vblank_timer_cb), this);

	m_chip_id = 0x98;  // GD5428 - Rev 0
}

void cirrus_gd5430_vga_device::device_start()
{
	cirrus_gd5428_vga_device::device_start();
	m_chip_id = 0xa0;  // GD5430 - Rev 0
	save_item(NAME(m_sr17));
}

void cirrus_gd5446_vga_device::device_start()
{
	cirrus_gd5430_vga_device::device_start();
	m_chip_id = 0xb8;  // GD5446
}


void cirrus_gd5428_vga_device::device_reset()
{
	svga_device::device_reset();
	gc_locked = (m_chip_id < 0x9c);
	gc_mode_ext = 0;
	gc_bank[0] = gc_bank[1] = 0;
	m_lock_reg = 0x0f;
	m_blt_status = 0;
	m_blt_write_mask = 0;
	m_cursor_attr = 0x00;  // disable hardware cursor and extra palette
	m_cursor_x = m_cursor_y = 0;
	m_cursor_addr = 0;
	m_scratchpad1 = m_scratchpad2 = m_scratchpad3 = 0;
	m_cr19 = m_cr1a = m_cr1b = 0;
	m_vclk_num[0] = 0x4a;
	m_vclk_denom[0] = 0x2b;
	m_vclk_num[1] = 0x5b;
	m_vclk_denom[1] = 0x2f;
	m_blt_source = m_blt_dest = m_blt_source_current = m_blt_dest_current = 0;
	memset(m_ext_palette, 0, sizeof(m_ext_palette));
	m_ext_palette_enabled = false;
	m_blt_system_transfer = false;
	m_hidden_dac_phase = 0;
	m_hidden_dac_mode = 0;
}

uint32_t cirrus_gd5428_vga_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	uint32_t ptr = (vga.svga_intf.vram_size - 0x4000);  // cursor patterns are stored in the last 16kB of VRAM
	svga_device::screen_update(screen, bitmap, cliprect);

	if(m_cursor_attr & 0x01)  // hardware cursor enabled
	{
		// draw hardware graphics cursor
		if(m_cursor_attr & 0x04)  // 64x64
		{
			ptr += ((m_cursor_addr & 0x3c) * 256);
			for(int y=0;y<64;y++)
			{
				for(int x=0;x<64;x+=8)
				{
					for(int bit=0;bit<8;bit++)
					{
						uint8_t pixel1 = vga.memory[ptr % vga.svga_intf.vram_size] >> (7-bit);
						uint8_t pixel2 = vga.memory[(ptr+512) % vga.svga_intf.vram_size] >> (7-bit);
						uint8_t output = ((pixel1 & 0x01) << 1) | (pixel2 & 0x01);
						switch(output)
						{
						case 0:  // transparent - do nothing
							break;
						case 1:  // background
							bitmap.pix(m_cursor_y+y,m_cursor_x+x+bit) = (m_ext_palette[0].red << 16) | (m_ext_palette[0].green << 8) | (m_ext_palette[0].blue);
							break;
						case 2:  // XOR
							bitmap.pix(m_cursor_y+y,m_cursor_x+x+bit) = ~bitmap.pix(m_cursor_y+y,m_cursor_x+x+bit);
							break;
						case 3:  // foreground
							bitmap.pix(m_cursor_y+y,m_cursor_x+x+bit) = (m_ext_palette[15].red << 16) | (m_ext_palette[15].green << 8) | (m_ext_palette[15].blue);
							break;
						}
					}
				}
			}
		}
		else
		{
			ptr += ((m_cursor_addr & 0x3f) * 256);
			for(int y=0;y<32;y++)
			{
				for(int x=0;x<32;x+=8)
				{
					for(int bit=0;bit<8;bit++)
					{
						uint8_t pixel1 = vga.memory[ptr % vga.svga_intf.vram_size] >> (7-bit);
						uint8_t pixel2 = vga.memory[(ptr+128) % vga.svga_intf.vram_size] >> (7-bit);
						uint8_t output = ((pixel1 & 0x01) << 1) | (pixel2 & 0x01);
						switch(output)
						{
						case 0:  // transparent - do nothing
							break;
						case 1:  // background
							bitmap.pix(m_cursor_y+y,m_cursor_x+x+bit) = (m_ext_palette[0].red << 18) | (m_ext_palette[0].green << 10) | (m_ext_palette[0].blue << 2);
							break;
						case 2:  // XOR
							bitmap.pix(m_cursor_y+y,m_cursor_x+x+bit) = ~bitmap.pix(m_cursor_y+y,m_cursor_x+x+bit);
							break;
						case 3:  // foreground
							bitmap.pix(m_cursor_y+y,m_cursor_x+x+bit) = (m_ext_palette[15].red << 18) | (m_ext_palette[15].green << 10) | (m_ext_palette[15].blue << 2);
							break;
						}
					}
					ptr++;
				}
			}
		}
	}
	return 0;
}

void cirrus_gd5428_vga_device::recompute_params()
{
	uint8_t divisor = 1;
	float clock;
	// TODO: coming from OSC, expose as this->clock()
	const XTAL xtal = XTAL(14'318'181);
	uint8_t clocksel = (vga.miscellaneous_output & 0xc) >> 2;

	if(gc_locked || m_vclk_num[clocksel] == 0 || m_vclk_denom[clocksel] == 0)
		clock = ((vga.miscellaneous_output & 0xc) ? xtal*2: xtal*1.75).dvalue();
	else
	{
		int numerator = m_vclk_num[clocksel] & 0x7f;
		int denominator = (m_vclk_denom[clocksel] & 0x3e) >> 1;
		int mul = (m_vclk_denom[clocksel] & 0x01) ? 2 : 1;
		clock = (xtal * numerator / denominator / mul).dvalue();
		LOGMASKED(LOG_PLL, "CL: PLL setting %d num %d denom %d mul %d -> %f\n", clocksel, numerator, denominator, mul, clock);
	}

	svga.rgb8_en = svga.rgb15_en = svga.rgb16_en = svga.rgb24_en = svga.rgb32_en = 0;

	if (!gc_locked)
	{
		// gambl186 relies on this, don't setup any hidden DAC but only this
		if (vga.sequencer.data[0x07] & 0x01)
			svga.rgb8_en = 1;

		if (BIT(m_hidden_dac_mode, 7))
		{
			// TODO: needs subclassing, earlier chips don't have all of these modes
			if (BIT(m_hidden_dac_mode, 4))
				popmessage("pc_vga_cirrus: Unsupported mixed 5-5-5 / 8bpp mode selected");
			switch(m_hidden_dac_mode & 0x4f)
			{
				case 0x00:
				case 0x40:
					// 5-5-5 Sierra
					svga.rgb15_en = 1;
					break;
				case 0x41:
					svga.rgb16_en = 1;
					break;
				case 0x43: // CCIR601 YUV422 16-bit
				case 0x44: // YUV411 8-bit
				case 0x4a: // 16bpp + YUV422 overlay
				case 0x4b: // 16bpp + YUV411 overlay
					popmessage("pc_vga_cirrus: CL-GD545 YUV mode selected %02x", m_hidden_dac_mode);
					break;
				case 0x45:
					svga.rgb24_en = 1;
					break;
				case 0x46:
				case 0x47:
					popmessage("pc_vga_cirrus: CL-GD545+ DAC power down selected %02x", m_hidden_dac_mode);
					break;
				case 0x48:
					popmessage("pc_vga_cirrus: CL-GD545+ 8-bit grayscale selected");
					break;
				case 0x49:
					svga.rgb8_en = 1;
					break;
				default:
					// TODO: 0xff in pciagp (alias for a DAC power down?)
					if (m_hidden_dac_mode != 0xff)
						popmessage("pc_vga_cirrus: reserved mode selected %02x", m_hidden_dac_mode);
					break;
			}
		}

		switch(vga.sequencer.data[0x07] & 0x06)  // bit 3 is reserved on GD542x
		{
			case 0x00: break;
			case 0x02: clock /= 2; break;  // Clock / 2 for 16-bit data
			case 0x04: clock /= 3; break; // Clock / 3 for 24-bit data
			case 0x06:
				// Clock rate for 16-bit data = VCLK
				// TODO: verify clock, may just be clock / 2 again?
				//divisor = 2;
				break;
		}
	}
	recompute_params_clock(divisor, (int)clock);
}

uint16_t cirrus_gd5428_vga_device::offset()
{
	// TODO: check true enable condition
	if (svga.rgb8_en || svga.rgb15_en || svga.rgb16_en || svga.rgb24_en || svga.rgb32_en)
		return vga.crtc.offset << 3;
	return svga_device::offset();
}

uint32_t cirrus_gd5428_vga_device::latch_start_addr()
{
	if (svga.rgb24_en || svga.rgb32_en)
		return vga.crtc.start_addr_latch >> 1;

	// FIXME: need to explicitly return earlier because rgb8_en is '1' in tandem with these
	if (svga.rgb15_en || svga.rgb16_en)
		return vga.crtc.start_addr_latch;

	if (svga.rgb8_en)
		return vga.crtc.start_addr_latch << 2;
	return vga.crtc.start_addr_latch;
}

void cirrus_gd5428_vga_device::start_bitblt()
{
	uint32_t x,y;

	// Bit 4: 16-bit Color / Transparency
	// Bit 6: 8x8 Pattern Copy
	// Bit 7: Color Expand
	const unsigned bytes_per_pixel = BIT(m_blt_mode, 4) ? 2 : 1;
	const unsigned pattern_pitch = BIT(m_blt_mode, 7) ? 1 : (8 * bytes_per_pixel);
	const u32 pattern_base = m_blt_source & ~7U;

	if(m_blt_mode & 0x01)
	{
		start_reverse_bitblt();
		return;
	}

	LOGMASKED(LOG_BLIT, "CL: BitBLT started: Src: %06x Dst: %06x Width: %i Height %i ROP: %02x Mode: %02x\n",m_blt_source,m_blt_dest,m_blt_width,m_blt_height,m_blt_rop,m_blt_mode);

	m_blt_source_current = m_blt_source;
	m_blt_dest_current = m_blt_dest;

	for(y=0;y<=m_blt_height;y++)
	{
		const u32 pattern_row_base = BIT(m_blt_mode, 6) ? (pattern_base + (((m_blt_source + y) & 7) * pattern_pitch)) : 0;
		for(x=0;x<=m_blt_width;x++)
		{
			if (BIT(m_blt_mode, 6))
				m_blt_source_current = pattern_row_base + (BIT(m_blt_mode, 7) ? 0 : (x % pattern_pitch));
			if(m_blt_mode & 0x80)  // colour expand
			{
				if(m_blt_mode & 0x10)  // 16-bit colour expansion / transparency width
				{
					// use GR0/1/10/11 background/foreground regs
					const bool foreground = BIT(vga.memory[m_blt_source_current % vga.svga_intf.vram_size], ~(x/2) & 7);
					uint16_t pixel = foreground ? ((m_gr11 << 8) | vga.gc.enable_set_reset) : ((m_gr10 << 8) | vga.gc.set_reset);

					if(m_blt_dest_current & 1)
						copy_pixel(pixel >> 8, vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], foreground, x);
					else
						copy_pixel(pixel & 0xff, vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], foreground, x);
					if((x % 8) == 7 && !(m_blt_mode & 0x40))  // don't increment if a pattern (it's only 8 bits)
						m_blt_source_current++;
				}
				else
				{
					const bool foreground = BIT(vga.memory[m_blt_source_current % vga.svga_intf.vram_size], ~x & 7);
					uint8_t pixel = foreground ? vga.gc.enable_set_reset : vga.gc.set_reset;  // use GR0/1/10/11 background/foreground regs

					copy_pixel(pixel, vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], foreground, x);
					if((x % 8) == 7 && !(m_blt_mode & 0x40))  // don't increment if a pattern (it's only 8 bits)
						m_blt_source_current++;
				}
			}
			else
			{
				copy_pixel(vga.memory[m_blt_source_current % vga.svga_intf.vram_size], vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], true, x);
				m_blt_source_current++;
			}

			m_blt_dest_current++;
		}
		if (!BIT(m_blt_mode, 6))
			m_blt_source_current = m_blt_source + (m_blt_source_pitch*(y+1));
		m_blt_dest_current = m_blt_dest + (m_blt_dest_pitch*(y+1));
	}
	m_blt_status &= ~0x09;
}

void cirrus_gd5428_vga_device::start_reverse_bitblt()
{
	uint32_t x,y;

	LOGMASKED(LOG_BLIT, "CL: Reverse BitBLT started: Src: %06x Dst: %06x Width: %i Height %i ROP: %02x Mode: %02x\n",m_blt_source,m_blt_dest,m_blt_width,m_blt_height,m_blt_rop,m_blt_mode);

	// Start at end of blit
	m_blt_source_current = m_blt_source;
	m_blt_dest_current = m_blt_dest;

	for(y=0;y<=m_blt_height;y++)
	{
		for(x=0;x<=m_blt_width;x++)
		{
			if(m_blt_mode & 0x80)  // colour expand
			{
				if(m_blt_mode & 0x10)  // 16-bit colour expansion / transparency width
				{
					// use GR0/1/10/11 background/foreground regs
					const bool foreground = BIT(vga.memory[m_blt_source_current % vga.svga_intf.vram_size], ~(x/2) & 7);
					uint16_t pixel = foreground ? ((m_gr11 << 8) | vga.gc.enable_set_reset) : ((m_gr10 << 8) | vga.gc.set_reset);

					if(m_blt_dest_current & 1)
						copy_pixel(pixel >> 8, vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], foreground, m_blt_width - x);
					else
						copy_pixel(pixel & 0xff, vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], foreground, m_blt_width - x);
					if((x % 8) == 7 && !(m_blt_mode & 0x40))  // don't increment if a pattern (it's only 8 bits)
						m_blt_source_current--;
				}
				else
				{
					const bool foreground = BIT(vga.memory[m_blt_source_current % vga.svga_intf.vram_size], ~x & 7);
					uint8_t pixel = foreground ? vga.gc.enable_set_reset : vga.gc.set_reset;  // use GR0/1/10/11 background/foreground regs

					copy_pixel(pixel, vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], foreground, m_blt_width - x);
					if((x % 8) == 7 && !(m_blt_mode & 0x40))  // don't decrement if a pattern (it's only 8 bits)
						m_blt_source_current--;
				}
			}
			else
			{
				copy_pixel(vga.memory[m_blt_source_current % vga.svga_intf.vram_size], vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], true, m_blt_width - x);
				m_blt_source_current--;
			}
			m_blt_dest_current--;
			if(m_blt_mode & 0x40 && (x % 8) == 7)  // 8x8 pattern - reset pattern source location
			{
				if(m_blt_mode & 0x80) // colour expand
					m_blt_source_current = m_blt_source - (1*(y % 8)); // patterns are linear data
				else if(svga.rgb15_en || svga.rgb16_en)
				{
					if(m_blt_mode & 0x40 && (x % 16) == 15)
						m_blt_source_current = m_blt_source - (16*(y % 8));
				}
				else
					m_blt_source_current = m_blt_source - (8*(y % 8));
			}
		}
		if(m_blt_mode & 0x40)  // 8x8 pattern
		{
			if(m_blt_mode & 0x80) // colour expand
				m_blt_source_current = m_blt_source - (1*(y % 8)); // patterns are linear data
			else if(svga.rgb15_en || svga.rgb16_en)
				{
					if(m_blt_mode & 0x40 && (x % 16) == 15)
						m_blt_source_current = m_blt_source - (16*(y % 8));
				}
			else
				m_blt_source_current = m_blt_source - (8*(y % 8));
		}
		else
			m_blt_source_current = m_blt_source - (m_blt_source_pitch*(y+1));
		m_blt_dest_current = m_blt_dest - (m_blt_dest_pitch*(y+1));
	}
	m_blt_status &= ~0x09;
}

void cirrus_gd5428_vga_device::start_system_bitblt()
{
	LOGMASKED(LOG_BLIT, "CL: BitBLT from system memory started: Src: %06x Dst: %06x Width: %i Height %i ROP: %02x Mode: %02x\n",m_blt_source,m_blt_dest,m_blt_width,m_blt_height,m_blt_rop,m_blt_mode);
	m_blt_system_transfer = true;
	m_blt_system_count = 0;
	m_blt_system_buffer = 0;
	m_blt_pixel_count = m_blt_scan_count = 0;
	m_blt_source_current = m_blt_source;
	m_blt_dest_current = m_blt_dest;
	m_blt_status |= 0x09;
}

// non colour-expanded BitBLTs from system memory must be doubleword sized, extra bytes are ignored
void cirrus_gd5428_vga_device::blit_dword()
{
	// TODO: add support for reverse direction
	uint8_t x,pixel;

	for(x=0;x<32;x+=8)
	{
		pixel = ((m_blt_system_buffer & (0x000000ff << x)) >> x);
		if(m_blt_pixel_count <= m_blt_width)
			copy_pixel(pixel,vga.memory[m_blt_dest_current % vga.svga_intf.vram_size]);
		m_blt_dest_current++;
		m_blt_pixel_count++;
	}
	if(m_blt_pixel_count > m_blt_width)
	{
		m_blt_pixel_count = 0;
		m_blt_scan_count++;
		m_blt_dest_current = m_blt_dest + (m_blt_dest_pitch*m_blt_scan_count);
	}
	if(m_blt_scan_count > m_blt_height)
	{
		m_blt_system_transfer = false;  //  BitBLT complete
		m_blt_status &= ~0x0b;
	}
}

// colour-expanded BitBLTs from system memory are on a byte boundary, unused bits are ignored
void cirrus_gd5428_vga_device::blit_byte()
{
	// Width is stored as a byte count minus one.  Each source bit expands
	// to one pixel; GR10/11 supply the second byte only in 16-bit mode.
	const unsigned bytes_per_pixel = BIT(m_blt_mode, 4) ? 2 : 1;
	for (unsigned bit = 0; bit < 8; bit++)
	{
		const bool foreground = BIT(m_blt_system_buffer, 7 - bit);
		for (unsigned byte = 0; byte < bytes_per_pixel; byte++)
		{
			const u8 pixel = byte ? (foreground ? m_gr11 : m_gr10) :
				(foreground ? vga.gc.enable_set_reset : vga.gc.set_reset);
			if (m_blt_pixel_count <= m_blt_width)
				copy_pixel(pixel, vga.memory[m_blt_dest_current % vga.svga_intf.vram_size], foreground, m_blt_pixel_count);
			m_blt_dest_current++;
			m_blt_pixel_count++;
		}
	}
	if(m_blt_pixel_count > m_blt_width)
	{
		m_blt_pixel_count = 0;
		m_blt_scan_count++;
		m_blt_dest_current = m_blt_dest + (m_blt_dest_pitch*m_blt_scan_count);
	}
	if(m_blt_scan_count > m_blt_height)
	{
		m_blt_system_transfer = false;  //  BitBLT complete
		m_blt_status &= ~0x0b;
	}
}

void cirrus_gd5428_vga_device::copy_pixel(uint8_t src, uint8_t dst, bool foreground, unsigned dst_byte_offset)
{
	// GR2F suppresses the first n pixels on each expanded or pattern scan line.
	if ((m_blt_mode & 0xc0) && dst_byte_offset < m_blt_write_mask * (BIT(m_blt_mode, 4) ? 2 : 1))
		return;

	uint8_t res = src;

	switch(m_blt_rop)
	{
	case 0x00:  // BLACK
		res = 0x00;
		break;
	case 0x05:  // SRCAND
		res = src & dst;
		break;
	case 0x06:  // DST
		res = dst;
		break;
	case 0x09:  // SDna
		res = src & ~dst;
		break;
	case 0x0b:  // DSTINVERT
		res = ~dst;
		break;
	case 0x0d:  // SRC
		res = src;
		break;
	case 0x0e:  // WHITE
		res = 0xff;
		break;
	case 0x50:  // DSna / DPna
		// used by zorro2:picasso2p, unknown purpose
		res = (dst & (~src));
		break;
	case 0x59:  // SRCINVERT
		res = src ^ dst;
		break;
	case 0x6d:  // SRCPAINT / DSo
		// zorro2:picasso2p on VGA Workbench (upper right icon)
		res = src | dst;
		break;
	case 0x90:  // NOTSRCERASE
		res = ~(src | dst);
		break;
	case 0x95:  // NOTSRCINVERT
		res = ~(src ^ dst);
		break;
	case 0xad:  // SDno
		res = src | ~dst;
		break;
	case 0xd0:  // NOTSRCCOPY
		res = ~src;
		break;
	case 0xd6:  // MERGEPAINT
		res = ~src | dst;
		break;
	case 0xda:  // NOTSRCAND
		res = ~(src & dst);
		break;
	default:
		popmessage("pc_vga_cirrus: Unsupported BitBLT ROP mode %02x",m_blt_rop);
	}

	// handle transparency compare
	if (m_chip_id >= 0xa0 && BIT(m_blt_mode, 7) && BIT(m_blt_mode, 3))
	{
		if (!foreground)
			return;
	}
	else if(m_blt_mode & 0x08)  // TODO: 16-bit compare
	{
		// if ROP result matches the transparency colour, don't change the pixel
		if((res & (~m_blt_trans_colour_mask & 0xff)) == ((m_blt_trans_colour & 0xff) & (~m_blt_trans_colour_mask & 0xff)))
			return;
	}

	vga.memory[m_blt_dest_current % vga.svga_intf.vram_size] = res;
}

uint8_t cirrus_gd5428_vga_device::vga_latch_write(int offs, uint8_t data)
{
	uint8_t res = 0;
	uint8_t mode_mask = (gc_mode_ext & 0x04) ? 0x07 : 0x03;

	switch (vga.gc.write_mode & mode_mask) {
	case 0:
	case 1:
	case 2:
	case 3:
		res = vga_device::vga_latch_write(offs, data);
		break;
	case 4:
		res = vga.gc.latch[offs];
		popmessage("pc_vga_cirrus: Unimplemented VGA write mode 4 enabled");
		break;
	case 5:
		res = vga.gc.latch[offs];
		popmessage("pc_vga_cirrus: Unimplemented VGA write mode 5 enabled");
		break;
	}

	return res;
}

// 0xa0000-0xa7fff offset 0
// 0xa8000-0xaffff offset 1 (if enabled with GRB bit 0)
// notice that "offset" in this context doesn't mean pitch like everything else in the
// (S)VGA realm but it's really intended as a window bank base here.
uint8_t cirrus_gd5428_vga_device::offset_select(offs_t offset)
{
	const uint8_t sa15 = BIT(offset, 15);
	return gc_bank[sa15 & BIT(gc_mode_ext, 0)];
}

uint8_t cirrus_gd5428_vga_device::mem_r(offs_t offset)
{
	uint32_t addr;
	uint8_t cur_mode = pc_vga_choosevideomode();

	const uint8_t bank = offset_select(offset);

	// TODO: incomplete, just enough for zorro2:picasso2p to not outright crash at display init
	// value should be a base to apply, also GRB[5] remaps banking granularity
	// NOTE: bebox also wants this
	if(vga.sequencer.data[0x07] & 0xf0)
	{
		return svga_device::mem_linear_r((offset & 0xffff) + bank * 0x10000);
	}

	if(gc_locked || offset >= 0x10000 || cur_mode == TEXT_MODE || cur_mode == SCREEN_OFF)
	{
		return vga_device::mem_r(offset & 0x1ffff);
	}

	if(gc_mode_ext & 0x20)  // 16kB bank granularity
		addr = bank * 0x4000;
	else  // 4kB bank granularity
		addr = bank * 0x1000;

	// Is the display address adjusted automatically when not using Chain-4 addressing?
	// The GD542x BIOS doesn't do it, but Virtual Pool expects it.
	if(!(vga.sequencer.data[4] & 0x8))
		addr <<= 2;

	if(svga.rgb8_en || svga.rgb15_en || svga.rgb16_en || svga.rgb24_en)
	{
		uint8_t data = 0;
		if(gc_mode_ext & 0x01)
		{
			if(offset & 0x10000)
				return 0;
			if(offset < 0x8000)
				offset &= 0x7fff;
			else
			{
				offset -= 0x8000;
				offset &= 0x7fff;
			}
		}
		else
			offset &= 0xffff;

		if(vga.sequencer.data[4] & 0x8)
			data = vga.memory[(offset+addr) % vga.svga_intf.vram_size];
		else
		{
			// Unchained packed pixels still use the VGA data path.  Each
			// plane/latch holds one of four adjacent eight-bit pixels.
			if (!machine().side_effects_disabled())
			{
				for (unsigned plane = 0; plane < 4; plane++)
					vga.gc.latch[plane] = vga.memory[(offset * 4 + plane + addr) % vga.svga_intf.vram_size];
			}
			if (vga.gc.read_mode)
			{
				for (unsigned bit = 0; bit < 8; bit++)
				{
					u8 colour = 0;
					for (unsigned plane = 0; plane < 4; plane++)
						colour |= BIT(vga.gc.latch[plane], bit) << plane;
					if ((colour & vga.gc.color_dont_care) == (vga.gc.color_compare & vga.gc.color_dont_care))
						data |= 1 << bit;
				}
			}
			else
				data = vga.gc.latch[vga.gc.read_map_sel];
		}
		return data;
	}

	switch(vga.gc.memory_map_sel & 0x03)
	{
		case 0: break;
		case 1: if(gc_mode_ext & 0x01) offset &= 0x7fff; else offset &= 0x0ffff; break;
		case 2: offset -= 0x10000; offset &= 0x07fff; break;
		case 3: offset -= 0x18000; offset &= 0x07fff; break;
	}

	if(vga.sequencer.data[4] & 4)
	{
		int data;
		if (!machine().side_effects_disabled())
		{
			vga.gc.latch[0]=vga.memory[(offset+addr) % vga.svga_intf.vram_size];
			vga.gc.latch[1]=vga.memory[((offset+addr)+0x10000) % vga.svga_intf.vram_size];
			vga.gc.latch[2]=vga.memory[((offset+addr)+0x20000) % vga.svga_intf.vram_size];
			vga.gc.latch[3]=vga.memory[((offset+addr)+0x30000) % vga.svga_intf.vram_size];
		}

		if (vga.gc.read_mode)
		{
			uint8_t byte,layer;
			uint8_t fill_latch;
			data=0;

			for(byte=0;byte<8;byte++)
			{
				fill_latch = 0;
				for(layer=0;layer<4;layer++)
				{
					if(vga.gc.latch[layer] & 1 << byte)
						fill_latch |= 1 << layer;
				}
				fill_latch &= vga.gc.color_dont_care;
				if(fill_latch == vga.gc.color_compare)
					data |= 1 << byte;
			}
		}
		else
			data=vga.gc.latch[vga.gc.read_map_sel];

		return data;
	}
	else
	{
		// TODO: Lines up in 16-colour mode, likely different for 256-colour modes (docs say video addresses are shifted right 3 places)
		//uint8_t bits = ((gc_mode_ext & 0x08) && (vga.gc.write_mode == 1)) ? 8 : 4;

		uint8_t data = 0;
		//printf("%08x\n",offset);

		if(gc_mode_ext & 0x02)
		{
			for(uint8_t i = 0; i < 8; i++)
			{
				if(BIT(vga.sequencer.map_mask, i))
					data |= vga.memory[(((offset+addr))+i*0x10000) % vga.svga_intf.vram_size];
			}
		}
		else
		{
			for(uint8_t i = 0; i < 4; i++)
			{
				if(BIT(vga.sequencer.map_mask, i))
					data |= vga.memory[(((offset+addr))+i*0x10000) % vga.svga_intf.vram_size];
			}
		}

		return data;
	}
}

void cirrus_gd5428_vga_device::mem_w(offs_t offset, uint8_t data)
{
	uint32_t addr;
	uint8_t cur_mode = pc_vga_choosevideomode();

	if(m_blt_system_transfer)
	{
		if(m_blt_mode & 0x80)  // colour expand
		{
			m_blt_system_buffer &= ~(0x000000ff);
			m_blt_system_buffer |= data;
			blit_byte();
			m_blt_system_count = 0;
		}
		else
		{
			m_blt_system_buffer &= ~(0x000000ff << (m_blt_system_count * 8));
			m_blt_system_buffer |= (data << (m_blt_system_count * 8));
			m_blt_system_count++;
			if(m_blt_system_count >= 4)
			{
				blit_dword();
				m_blt_system_count = 0;
			}
		}
		return;
	}

	const uint8_t bank = offset_select(offset);

	// TODO: as above
	if(vga.sequencer.data[0x07] & 0xf0)
	{
		svga_device::mem_linear_w((offset + bank * 0x10000), data);
		return;
	}

	if(gc_locked || offset >= 0x10000 || cur_mode == TEXT_MODE || cur_mode == SCREEN_OFF)
	{
		vga_device::mem_w(offset & 0x1ffff,data);
		return;
	}

	if(gc_mode_ext & 0x20)  // 16kB bank granularity
		addr = bank * 0x4000;
	else  // 4kB bank granularity
		addr = bank * 0x1000;

	// Is the display address adjusted automatically when using Chain-4 addressing?  The GD542x BIOS doesn't do it, but Virtual Pool expects it.
	if(!(vga.sequencer.data[4] & 0x8))
		addr <<= 2;

	if(svga.rgb8_en || svga.rgb15_en || svga.rgb16_en || svga.rgb24_en)
	{
		if(offset & 0x10000)
			return;
		if(gc_mode_ext & 0x01)
		{
			if(offset < 0x8000)
				offset &= 0x7fff;
			else
			{
				offset -= 0x8000;
				offset &= 0x7fff;
			}
		}
		else
			offset &= 0xffff;

		// GR0 (and GR10 for 16-bit) = background colour in write mode 5
		// GR1 (and GR11 for 16-bit) = foreground colour in write modes 4 or 5
		// GRB = colour expansion width
		// SR2 = pixel mask for the eight expanded pixels, MSB first
		if (vga.gc.write_mode == 4)
		{
			const unsigned bytes_per_pixel = BIT(gc_mode_ext, 4) ? 2 : 1;
			const u32 destination = (addr + offset) * 8 * bytes_per_pixel;
			for (unsigned pixel = 0; pixel < 8; pixel++)
			{
				if (BIT(vga.sequencer.map_mask, 7 - pixel) && BIT(data, 7 - pixel))
				{
					const u32 pixel_address = destination + pixel * bytes_per_pixel;
					vga.memory[pixel_address % vga.svga_intf.vram_size] = vga.gc.enable_set_reset;
					if (bytes_per_pixel == 2)
						vga.memory[(pixel_address + 1) % vga.svga_intf.vram_size] = m_gr11;
				}
			}
			return;
		}

		if (vga.gc.write_mode == 5)
		{
			const unsigned bytes_per_pixel = BIT(gc_mode_ext, 4) ? 2 : 1;
			const u32 destination = (addr + offset) * 8 * bytes_per_pixel;
			for (unsigned pixel = 0; pixel < 8; pixel++)
			{
				if (BIT(vga.sequencer.map_mask, 7 - pixel))
				{
					const bool foreground = BIT(data, 7 - pixel);
					const u32 pixel_address = destination + pixel * bytes_per_pixel;
					vga.memory[pixel_address % vga.svga_intf.vram_size] = foreground ? vga.gc.enable_set_reset : vga.gc.set_reset;
					if (bytes_per_pixel == 2)
						vga.memory[(pixel_address + 1) % vga.svga_intf.vram_size] = foreground ? m_gr11 : m_gr10;
				}
			}
			return;
		}

		if(vga.sequencer.data[4] & 0x8)
			vga.memory[(offset+addr) % vga.svga_intf.vram_size] = data;
		else
		{
			for(int i = 0; i < 4; i++)
			{
				if(BIT(vga.sequencer.map_mask, i))
					vga.memory[((offset*4+i)+addr) % vga.svga_intf.vram_size] =
						BIT(vga.sequencer.data[4], 2) ? vga_latch_write(i, data) : data;
			}
		}
	}
	else
	{
		//Inside each case must prevent writes to non-mapped VGA memory regions, not only mask the offset.
		switch(vga.gc.memory_map_sel & 0x03)
		{
			case 0: break;
			case 1:
				if(offset & 0x10000)
					return;

				if(gc_mode_ext & 0x01)
					offset &= 0x7fff;
				else
					offset &= 0xffff;
				break;
			case 2:
				if((offset & 0x18000) != 0x10000)
					return;

				offset &= 0x07fff;
				break;
			case 3:
				if((offset & 0x18000) != 0x18000)
					return;

				offset &= 0x07fff;
				break;
		}

		{
			// TODO: Lines up in 16-colour mode, likely different for 256-colour modes (docs say video addresses are shifted right 3 places)
			//uint8_t bits = ((gc_mode_ext & 0x08) && (vga.gc.write_mode == 1)) ? 8 : 4;

			for(uint8_t i = 0; i < 4; i++)
			{
				if(BIT(vga.sequencer.map_mask, i))
				{
					if(gc_mode_ext & 0x02)
					{
						vga.memory[(((offset+addr) << 1)+i*0x10000) % vga.svga_intf.vram_size] = (vga.sequencer.data[4] & 4) ? vga_latch_write(i,data) : data;
						vga.memory[(((offset+addr) << 1)+i*0x10000+1) % vga.svga_intf.vram_size] = (vga.sequencer.data[4] & 4) ? vga_latch_write(i,data) : data;
					}
					else
						vga.memory[(((offset+addr))+i*0x10000) % vga.svga_intf.vram_size] = (vga.sequencer.data[4] & 4) ? vga_latch_write(i,data) : data;
				}
			}
			return;
		}
	}
}

/*
 * CL-GD5430 overrides
 */

void cirrus_gd5430_vga_device::device_reset()
{
	cirrus_gd5428_vga_device::device_reset();
	m_sr17 = 1;
}

uint8_t cirrus_gd5430_vga_device::mem_r(offs_t offset)
{
	if (BIT(m_sr17, 2) && (!BIT(m_sr17, 6) || !(vga.sequencer.data[7] & 0xf0)) && (offset & 0x18000) == 0x18000)
		return mmio_r(offset & 0xff);

	return cirrus_gd5428_vga_device::mem_r(offset);
}

void cirrus_gd5430_vga_device::mem_w(offs_t offset, uint8_t data)
{
	if (BIT(m_sr17, 2) && (!BIT(m_sr17, 6) || !(vga.sequencer.data[7] & 0xf0)) && (offset & 0x18000) == 0x18000)
	{
		mmio_w(offset & 0xff, data);
		return;
	}

	cirrus_gd5428_vga_device::mem_w(offset, data);
}

/*
 * GD5430 BitBLT registers:
 * GR0:     Background colour byte 0 (00h).
 * GR1:     Foreground colour byte 0 (04h).
 * GR10:    Background colour byte 1 (01h).
 * GR11:    Foreground colour byte 1 (05h).
 * GR20/21: BLT width bytes 0/1 (08h/09h).
 * GR22/23: BLT height bytes 0/1 (0Ah/0Bh).
 * GR24/25: BLT destination pitch bytes 0/1 (0Ch/0Dh).
 * GR26/27: BLT source pitch bytes 0/1 (0Eh/0Fh).
 * GR28-2A: BLT destination start bytes 0-2 (10h-12h).
 * GR2C-2E: BLT source start bytes 0-2 (14h-16h).
 * GR2F:    BLT destination left-side clipping (17h).
 * GR30:    BLT mode (18h).
 * GR31:    BLT start/status (40h), the only register readable through MMIO.
 * GR32:    BLT raster operation (1Ah).
 *
 * Notes:
 * - This is the GD5430/'40 subset from the Alpine Family TRM. Colour
 *   bytes are grouped by background/foreground rather than GR index.
 *
 * TODO:
 * - GD5430 reserves 02h/03h/06h/07h (GR12-GR15) and 1Bh-3Fh. GD5446
 *   also defines those colour bytes, GR33 mode extensions and GR34/35
 *   colour keys; these are not implemented by inheriting this decoder.
 * - Give later chips their own MMIO address map and replace the GR
 *   lambdas with named handlers shared by indexed and MMIO maps, allowing
 *   each chip to define its register set and MMIO access permissions.
 */
u8 cirrus_gd5430_vga_device::mmio_register(offs_t offset)
{
	if ((offset >= 0x08 && offset <= 0x12) ||
		(offset >= 0x14 && offset <= 0x18) || offset == 0x1a)
		return offset + 0x18;

	switch (offset)
	{
	case 0x00: return 0x00;
	case 0x01: return 0x10;
	case 0x04: return 0x01;
	case 0x05: return 0x11;
	case 0x40: return 0x31;
	default: return 0xff;
	}
}

u8 cirrus_gd5430_vga_device::mmio_r(offs_t offset)
{
	// Only start/status is readable;
	// the remaining MMIO registers are write-only.
	return offset == 0x40 ? space(GC_REG).read_byte(0x31) : 0xff;
}

void cirrus_gd5430_vga_device::mmio_w(offs_t offset, u8 data)
{
	const u8 index = mmio_register(offset);
	if (index != 0xff)
		space(GC_REG).write_byte(index, data);
}

void cirrus_gd5430_vga_device::crtc_map(address_map &map)
{
	cirrus_gd5428_vga_device::crtc_map(map);
	map(0x1d, 0x1d).lrw8(
		NAME([this] (offs_t offset) {
			return m_cr1d;
		}),
		NAME([this] (offs_t offset, u8 data) {
			LOGMASKED(LOG_REGS, "CR1D: Overlay Extended Control %02x\n", data);
			m_cr1d = data;
			// TODO: '34/'36 onward
			vga.crtc.start_addr_latch = (vga.crtc.start_addr_latch & 0xf7ffff) | (BIT(data, 7) << 19);
		})
	);
}

void cirrus_gd5430_vga_device::gc_map(address_map &map)
{
	cirrus_gd5428_vga_device::gc_map(map);
	map(0x2f, 0x2f).lrw8(
		NAME([this](offs_t offset) {
			return m_blt_write_mask;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_blt_write_mask = data & 7;
		})
	);
}

void cirrus_gd5430_vga_device::sequencer_map(address_map &map)
{
	cirrus_gd5428_vga_device::sequencer_map(map);
	map(0x17, 0x17).lrw8(
		NAME([this](offs_t offset) {
			return m_sr17;
		}),
		NAME([this](offs_t offset, u8 data) {
			m_sr17 = data & 0x47;
		})
	);
}

/*
 * CL-GD5446 overrides
 */

void cirrus_gd5446_vga_device::crtc_map(address_map &map)
{
	cirrus_gd5430_vga_device::crtc_map(map);
}

void cirrus_gd5446_vga_device::gc_map(address_map &map)
{
	cirrus_gd5430_vga_device::gc_map(map);
}

void cirrus_gd5446_vga_device::sequencer_map(address_map &map)
{
	cirrus_gd5430_vga_device::sequencer_map(map);
}

// ES40 adapter.

const machine_config& CCirrus::config_for(const machine_config& mconfig)
{
	if (!mconfig.cfg || !mconfig.system || !mconfig.display)
		FAILURE(Configuration, "Cirrus requires an ES40 construction context");
	return mconfig;
}

bool CCirrus::is_avga() const noexcept
{
	return m_type == CIRRUS_GD5401_VGA;
}

bool CCirrus::is_pci() const noexcept
{
	return m_type == CIRRUS_GD5430_VGA || m_type == CIRRUS_GD5446_VGA;
}

/**
 * Constructor.
 *
 * Bind the card's sole display output; device initialization is done by init().
 **/
CCirrus::CCirrus(const machine_config& mconfig, device_type type, const char* tag,
	device_t* owner, uint32_t clock)
	: svga_device(config_for(mconfig), type, tag, owner, clock),
	  CDisplayOutputProvider(0, *mconfig.display),
	  m_type(type), m_snapshot_id(mconfig.snapshot_id)
{
	memset(&vga, 0, sizeof(vga));
	memset(&svga, 0, sizeof(svga));
	memset(&pci_state, 0, sizeof(pci_state));
	memset(std_config_data, 0, sizeof(std_config_data));
	memset(std_config_mask, 0, sizeof(std_config_mask));
	screen().set_before_clock_change([this] {
		advance_scan(cSystem->device_access_time());
	});
}

/**
 * Destructor.
 **/
CCirrus::~CCirrus()
{
	stop_threads();
}

/**
 * Initialize the Cirrus device.
 **/
void CCirrus::init()
{
	if (vga.memory)
		return;
	reset_state_registration();
	const char* model = myCfg->get_text_value("model", "GD5430");

	const u32 vram_size = is_avga() ? 256u * 1024u :
		m_type == CIRRUS_GD5446_VGA ? 4u * 1024u * 1024u : 2u * 1024u * 1024u;
	vga.svga_intf.vram_size = vram_size;
	init_maps();
	// ES40 retains the existing Oak register readback and storage.
	m_3cx_map(0x03, 0x03).lrw8(
		NAME([this](offs_t) { return vga.oak.reg; }),
		NAME([this](offs_t, u8 data) { vga.oak.reg = data; })
	);
	device_start();
	register_chipset_state();
	device_reset();

	vga.crtc.line_compare = 1023;
	vga.crtc.vert_disp_end = 399;
	vga.dac.mask = 0xff;
	vga.dac.dirty = 1;
	vga.gc.bit_mask = 0xff;
	vga.gc.memory_map_sel = 3;
	vga.sequencer.data[0] = 0x03;
	vga.sequencer.data[4] = 0x06;
	vga.sequencer.char_sel.base[0] = 0x20000;
	vga.sequencer.char_sel.base[1] = 0x20000;
	vga.attribute.state = 1;

	// Register VGA I/O ports at 3b4, 3b5, 3ba, 3c0..cf, 3d4, 3d5, 3da
	add_legacy_io(1, 0x3b4, 2);
	add_legacy_io(3, 0x3ba, 2);
	add_legacy_io(2, 0x3c0, 16);
	add_legacy_io(8, 0x3d4, 2);
	add_legacy_io(9, 0x3da, 1);
	// Legacy video address space: A0000 -> bffff
	add_legacy_mem(4, 0xa0000, 128 * 1024);
	if (!is_pci())
		add_legacy_mem(5, 0xc0000, sizeof(option_rom));

	if (is_pci())
	{
		u32 data[64]{};
		u32 mask[64]{};
		data[0x00 / 4] = m_type == CIRRUS_GD5446_VGA ? 0x00b81013 : 0x00a01013;
		data[0x04 / 4] = m_type == CIRRUS_GD5446_VGA ? 0x02000000 : 0;
		data[0x08 / 4] = 0x03000000;
		data[0x2c / 4] = m_type == CIRRUS_GD5446_VGA ? 0x00001013 : 0;
		data[0x30 / 4] = 0x000c0000;
		data[0x3c / 4] = 0x000001ff;
		mask[0x04 / 4] = 0x00000023;
		mask[0x0c / 4] = 0x0000ffff;
		mask[0x10 / 4] = m_type == CIRRUS_GD5446_VGA ? 0xfe000000 : 0xffc00000;
		if (m_type == CIRRUS_GD5446_VGA)
		{
			mask[0x14 / 4] = 0xfffffe00;
			mask[0x18 / 4] = 0xfffffe00;
		}
		mask[0x30 / 4] = 0xffff0001;
		mask[0x3c / 4] = 0x000000ff;
		add_function(0, data, mask);
		ResetPCI();
	}

	/* The configuration file variable "rom" should point to a VGA BIOS
	   image. If not, try "vgabios.bin". */
	FILE* rom = fopen(myCfg->get_text_value("rom", "vgabios.bin"), "rb");
	if (!rom)
	{
		FAILURE_1(FileNotFound, "cirrus rom file %s not found",
			myCfg->get_text_value("rom", "vgabios.bin"));
	}

	rom_max = (unsigned)fread(option_rom, 1, sizeof(option_rom), rom);
	fclose(rom);

	vga.memory = new u8[vga.svga_intf.vram_size];
	memset(vga.memory, 0, vga.svga_intf.vram_size);

	// lets make it decent - redraw time
	screen().vrefresh_hz = 60.0;
	screen().refresh_interval_ms = 16;   // ~60Hz
	screen().xtal_hz = screen().pixel_clock_hz = 25'174'800;
	m_last_refresh_time = std::chrono::steady_clock::now();

	printf("%s: Cirrus %s, %u KiB video RAM\n", devid_string, model, vram_size / 1024);
}

void CCirrus::ResetPCI()
{
	CPCIDevice::ResetPCI();
	if (vga.memory)
	{
		device_reset();
		recompute_params();
	}
}

bool CCirrus::decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept
{
	if (index >= PCI_RANGE_BASE)
		return is_pci() && CPCIDevice::decodes_memory_access(index, address, dsize, write);
	if (index < 0 || index >= MAX_DEV_RANGES)
		return false;
	if (is_pci())
	{
		const u32 command = endian_32(pci_state.config_data[0][1]);
		if (!(command & (dev_range_is_io[index] ? 1u : 2u)))
			return false;
	}
	if (index == 5)
		return !is_pci() && address < sizeof(option_rom);
	if (index == 4)
	{
		if (address >= 0x20000)
			return false;
		const auto* gd5430 = dynamic_cast<const cirrus_gd5430_vga_device*>(this);
		if (is_pci() && gd5430 && BIT(gd5430->m_sr17, 2) &&
			(!BIT(gd5430->m_sr17, 6) || !(vga.sequencer.data[7] & 0xf0)) && address >= 0x18000)
			return true;
		switch (vga.gc.memory_map_sel & 3)
		{
		case 0: return true;
		case 1: return address < 0x10000;
		case 2: return address >= 0x10000 && address < 0x18000;
		default: return address >= 0x18000;
		}
	}
	switch (index)
	{
	case 1: case 3: return !BIT(vga.miscellaneous_output, 0);
	case 8: case 9: return BIT(vga.miscellaneous_output, 0);
	case 2: return true;
	default: return false;
	}
}

u64 CCirrus::ReadMem(int index, u64 address, int dsize)
{
	if (is_pci() || index >= PCI_RANGE_BASE)
		return CPCIDevice::ReadMem(index, address, dsize);
	if (dsize == 64)
		return ReadMem(index, address, 32) | (ReadMem(index, address + 4, 32) << 32);
	return ReadMem_Legacy(index, (u32)address, dsize);
}

void CCirrus::WriteMem(int index, u64 address, int dsize, u64 data)
{
	if (is_pci() || index >= PCI_RANGE_BASE)
	{
		CPCIDevice::WriteMem(index, address, dsize, data);
		return;
	}
	if (dsize == 64)
	{
		WriteMem(index, address, 32, (u32)data);
		WriteMem(index, address + 4, 32, data >> 32);
		return;
	}
	WriteMem_Legacy(index, (u32)address, dsize, (u32)data);
}

/**
 * Read from one of the Legacy (fixed-address) memory ranges.
 **/
u32 CCirrus::ReadMem_Legacy(int index, u32 address, int dsize)
{
	if (!decodes_memory_access(index, address, dsize, false))
		return dsize == 8 ? 0xffU : dsize == 16 ? 0xffffU : 0xffffffffU;
	u32 data = 0;
	switch (index)
	{
		// IO Port 0x3b4
	case 1:
		data = io_read(address + 0x3b4, dsize);
		break;

		// IO Port 0x3c0..0x3cf
	case 2:
		data = io_read(address + 0x3c0, dsize);
		break;

		// IO Port 0x3ba
	case 3:
		data = io_read(address + 0x3ba, dsize);
		break;

		// VGA Memory
	case 4:
		data = legacy_read(address, dsize);
		break;

		// Option ROM
	case 5:
		data = rom_read(address, dsize);
		break;

		// IO Port 0x3d4
	case 8:
		data = io_read(address + 0x3d4, dsize);
		break;

		// IO Port 0x3da
	case 9:
		data = io_read(address + 0x3da, dsize);
		break;
	}

	return data;
}

/**
 * Write to one of the Legacy (fixed-address) memory ranges.
 **/
void CCirrus::WriteMem_Legacy(int index, u32 address, int dsize, u32 data)
{
	if (!decodes_memory_access(index, address, dsize, true))
		return;
	switch (index)
	{
		// IO Port 0x3b4
	case 1:
		io_write(address + 0x3b4, dsize, data);
		return;

		// IO Port 0x3c0..0x3cf
	case 2:
		io_write(address + 0x3c0, dsize, data);
		return;

		// IO Port 0x3ba
	case 3:
		io_write(address + 0x3ba, dsize, data);
		return;

		// VGA Memory
	case 4:
		legacy_write(address, dsize, data);
		return;

		// Option ROM
	case 5:
		return;

		// IO Port 0x3d4
	case 8:
		io_write(address + 0x3d4, dsize, data);
		return;

		// IO Port 0x3da
	case 9:
		io_write(address + 0x3da, dsize, data);
		return;
	}
}

u32 CCirrus::ReadMem_Bar(int func, int bar, u32 address, int dsize)
{
	if (func != 0)
		return 0xffffffffu;
	if (bar == 6)
		return rom_read(address, dsize);
	u32 data = 0;
	for (int byte = 0; byte < dsize / 8 && byte < 4; ++byte)
	{
		const u64 offset = (u64)address + byte;
		const u8 value = bar == 0 && offset < 0x400000 ?
			mem_linear_r((u32)offset) : 0xff;
		data |= (u32)value << (byte * 8);
	}
	return data;
}

void CCirrus::WriteMem_Bar(int func, int bar, u32 address, int dsize, u32 data)
{
	if (func != 0 || bar != 0)
		return;
	for (int byte = 0; byte < dsize / 8 && byte < 4; ++byte)
	{
		const u64 offset = (u64)address + byte;
		if (offset < 0x400000)
			mem_linear_w((u32)offset, u8(data >> (byte * 8)));
	}
}

/**
 * Read from Legacy VGA Memory
 *
 * Calls vga_mem_read to read the data 1 byte at a time.
 **/
u32 CCirrus::legacy_read(u32 address, int dsize)
{
	u32 data = 0;
	switch (dsize)
	{
	case 32:
		data |= (u32)mem_r(address + 3) << 24;
		data |= (u32)mem_r(address + 2) << 16;
		[[fallthrough]];
	case 16:
		data |= (u32)mem_r(address + 1) << 8;
		[[fallthrough]];
	case 8:
		data |= (u32)mem_r(address + 0);
		break;
	default:
		FAILURE(InvalidArgument, "Unsupported dsize");
	}

	return data;

}

/**
 * Write to Legacy VGA Memory
 *
 * Calls vga_mem_write to write the data 1 byte at a time.
 **/
void CCirrus::legacy_write(u32 address, int dsize, u32 data)
{
	switch (dsize) {
	case 8:
		mem_w(address, (u8)data);
		break;

	case 16:
		mem_w(address, (u8)data);
		mem_w(address + 1, (u8)(data >> 8));
		break;

	case 32:
		mem_w(address, (u8)data);
		mem_w(address + 1, (u8)(data >> 8));
		mem_w(address + 2, (u8)(data >> 16));
		mem_w(address + 3, (u8)(data >> 24));
		break;

	default:
		FAILURE(InvalidArgument, "Unsupported dsize");
	}
}

/**
 * Read from Option ROM
 */
u32 CCirrus::rom_read(u32 address, int dsize)
{
	if (dsize != 8 && dsize != 16 && dsize != 32)
		return 0;

	// Preserve zero padding beyond the loaded image. Assemble bytes so reads
	// at the end of an image or the 64 KiB aperture never overrun the buffer.
	u32 data = 0;
	for (int byte = 0; byte < dsize / 8; ++byte)
	{
		const u64 offset = (u64)address + byte;
		if (offset < rom_max)
			data |= (u32)option_rom[offset] << (byte * 8);
	}
	return data;
}

/**
 * Read from I/O Port
 */
u32 CCirrus::io_read(u32 address, int dsize)
{
	u32 data = 0;
	for (int byte = 0; byte < dsize / 8 && byte < 4; ++byte, ++address)
	{
		u8 value = 0xff;
		switch (address)
		{
		case 0x3ba:
		case 0x3da:
			value = scan_status(cSystem->device_access_time());
			vga.attribute.state = 0;  // ATC flip-flop reset
			break;
		default:
			vga_io_read(address, value);
			break;
		}
		data |= (u32)value << (byte * 8);
	}
	return data;
}

/**
 * Write to I/O Port
 *
 * Calls io_write_b to write the data 1 byte at a time.
 */
void CCirrus::io_write(u32 address, int dsize, u32 data)
{
	switch (dsize)
	{
	case 8:
		io_write_b(address, (u8)data);
		break;

	case 16:
		io_write_b(address, (u8)data);
		io_write_b(address + 1, (u8)(data >> 8));
		break;

	case 32:
#ifdef DEBUG_VGA_NOISY
		printf("%s: I/O write port=%04" PRIx32 " size=%d data=%08" PRIx32 "\n", devid_string, address, dsize, data);
#endif
		io_write_b(address, (u8)data);
		io_write_b(address + 1, (u8)(data >> 8));
		io_write_b(address + 2, (u8)(data >> 16));
		io_write_b(address + 3, (u8)(data >> 24));
		break;

	default:
#ifdef DEBUG_VGA
		printf("%s: I/O write port=%04" PRIx32 " size=%d data=%08" PRIx32 "\n", devid_string, address, dsize, data);
#endif
		FAILURE(InvalidArgument, "Weird IO size");
	}
}

/**
 * Write one byte to a VGA I/O port.
 **/
void CCirrus::io_write_b(u32 address, u8 data)
{
	// Finish elapsed scan time with the old registers before a timing write.
	// Every claimant in this PCI transaction receives the same sampled instant.
	if (address == 0x3c2 || address == 0x3c5 ||
		address == 0x3b5 || address == 0x3d5)
		advance_scan(cSystem->device_access_time());
	vga_io_write(address, data);
}

#define LINES ((vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1))
#define GRAPHIC_MODE (vga.gc.alpha_dis) /* else text mode */
#define VGA_CH_WIDTH ((vga.sequencer.data[1]&1)?8:9)

CCirrus::ScanTiming CCirrus::scan_timing() const
{
	ScanTiming t{};
	t.horizontal_total = vga.crtc.horz_total + 5u;
	t.horizontal_retrace_start = vga.crtc.horz_retrace_start;
	t.horizontal_retrace_width = ((vga.crtc.horz_retrace_end - t.horizontal_retrace_start - 1) & 31) + 1;
	if (t.horizontal_retrace_start < t.horizontal_total)
		t.horizontal_retrace_start = (t.horizontal_retrace_start + vga.crtc.horz_retrace_skew) % t.horizontal_total;
	t.vertical_total = vga.crtc.vert_total + 2u;
	t.vertical_retrace_start = vga.crtc.vert_retrace_start;
	t.vertical_retrace_width = ((vga.crtc.vert_retrace_end - t.vertical_retrace_start - 1) & 15) + 1;
	t.vertical_divisor = vga.crtc.sldiv ? 2u : 1u;
	if (const auto* gd5428 = dynamic_cast<const cirrus_gd5428_vga_device*>(this))
	{
		t.interlace = BIT(gd5428->m_cr1a, 0);
		t.interlace_offset = gd5428->m_cr19;
	}
	t.sync_enabled = vga.crtc.sync_en != 0;
	const double dots_per_character = BIT(vga.sequencer.data[1], 0) ? 8.0 : 9.0;
	t.characters_per_second = (double)screen().pixel_clock_hz * screen().divisor / dots_per_character;
	return t;
}

void CCirrus::advance_scan(std::chrono::steady_clock::time_point now)
{
	// A nested bus access can sample a later instant before the outer access
	// resumes. Never rewind a running counter's anchor and count time twice.
	if (m_scan_initialized && !m_scan_paused && now < m_scan_time)
		return;
	const ScanTiming t = scan_timing();
	const u32 field_lines = t.vertical_total * t.vertical_divisor;
	// Mode programming does not restart the oscillator. Retain the counters
	// and reduce out-of-range positions when software changes the totals.
	m_scan.character = std::fmod(m_scan.character, double(t.horizontal_total));
	m_scan.line %= field_lines;
	if (!t.interlace)
		m_scan.field = 0;
	if (m_scan_initialized && !m_scan_paused && now > m_scan_time &&
		t.characters_per_second > 0.0) {
		const double seconds = std::chrono::duration<double>(now - m_scan_time).count();
		const double field_characters = double(t.horizontal_total) * field_lines;
		const double cycle_characters = field_characters * (t.interlace ? 2 : 1);
		// Reduce elapsed time before combining it with the saved position; a
		// long host pause must not overflow an integer scan counter.
		double position = m_scan.character + double(m_scan.line) * t.horizontal_total;
		if (t.interlace)
			position += m_scan.field * field_characters;
		position = std::fmod(position + std::fmod(seconds * t.characters_per_second,
			cycle_characters), cycle_characters);
		m_scan.field = t.interlace && position >= field_characters;
		position = std::fmod(position, field_characters);
		m_scan.line = u32(position / t.horizontal_total);
		m_scan.character = std::fmod(position, double(t.horizontal_total));
	}
	m_scan_time = now;
	m_scan_initialized = true;
}

u8 CCirrus::scan_status(std::chrono::steady_clock::time_point now)
{
	advance_scan(now);
	const ScanTiming t = scan_timing();
	if (!t.sync_enabled)
		return 0x04;
	auto pulse = [](double position, double start, double width, double total) {
		return start < total && width > 0.0 &&
			std::fmod(position + total - start, total) < width;
	};
	const bool horizontal = pulse(m_scan.character, t.horizontal_retrace_start,
		t.horizontal_retrace_width, t.horizontal_total);
	const double line = m_scan.line + m_scan.character / t.horizontal_total;
	const double field_lines = t.vertical_total * t.vertical_divisor;
	const double start = t.vertical_retrace_start * t.vertical_divisor;
	const double width = t.vertical_retrace_width * t.vertical_divisor;
	bool vertical = false;
	if (start < field_lines) {
		if (t.interlace) {
			// Evaluate both pulses over the full frame so a pulse crossing a
			// field boundary retains the offset of the field that started it.
			const double position = m_scan.field * field_lines + line;
			const double odd_start = field_lines + start +
				double(t.interlace_offset) / t.horizontal_total;
			vertical = pulse(position, start, width, field_lines * 2.0) ||
				pulse(position, odd_start, width, field_lines * 2.0);
		} else {
			vertical = pulse(line, start, width, field_lines);
		}
	}
	return 0x04 | ((horizontal || vertical) ? 0x01 : 0) | (vertical ? 0x08 : 0);
}

void CCirrus::update(void)
{
	unsigned iWidth = 0, iHeight = 0;

	auto now = std::chrono::steady_clock::now();
	auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_last_refresh_time).count();

	if (elapsed_ms < (long long)screen().refresh_interval_ms)
		return;
	m_last_refresh_time = now;

	vga.crtc.start_addr = latch_start_addr();
	vga.attribute.pel_shift = vga.attribute.pel_shift_latch;

	get_display_size(iWidth, iHeight);

	if (iWidth == 0 || iHeight == 0)
		return;

	// Update screen shim's visible area
	screen().set_visible_area(iWidth, iHeight);

	// Ensure bitmap is large enough
	m_render_bitmap.allocate(iWidth, iHeight);

	// Render via MAME's screen_update pipeline
	rectangle clip = m_render_bitmap.cliprect();
	screen_update(screen(), m_render_bitmap, clip);

	// Tick the frame counter (for cursor blink)
	screen().tick_frame();

	m_output.present_argb32(m_render_bitmap.raw(), iWidth, iHeight);
}

void CCirrus::get_display_size(unsigned& width, unsigned& height)
{
	width = height = 0;
	if (!vga.crtc.horz_disp_end || !vga.crtc.vert_disp_end)
		return;

	const unsigned char_clocks = GRAPHIC_MODE ? 8 : VGA_CH_WIDTH;
	const unsigned divisor = screen().divisor > 0 ? (unsigned)screen().divisor : 1;
	width = (vga.crtc.horz_disp_end + 1u) * char_clocks / divisor;
	height = LINES;
}

/**
 * Thread entry point.
 *
 * The thread first initializes the GUI, and then starts looping the
 * following actions until interrupted (by StopThread being set to true)
 *   - Handle any GUI events (mouse moves, keypresses)
 *   - Update the GUI to match the screen buffer
 *   - Flush the updated GUI content to the screen
 *   .
 **/
void CCirrus::run()
{
	try
	{
		m_output.initialize(X_TILESIZE, Y_TILESIZE);
		bool was_paused = false;
		PauseAck.store(false, std::memory_order_release);
		for (;;)
		{
			// Terminate thread if StopThread is set to true
			if (StopThread)
				return;
			// Handle GUI events (50 times per second)
			m_output.display().lock();
			m_output.display().handle_events();
			m_output.display().unlock();
			CThread::sleep(10);

			// During firmware reset: keep pumping events (window stays alive),
			// but do NOT touch emulated VGA state.
			if (PauseThread.load(std::memory_order_acquire))
			{
				if (!was_paused)
				{
					m_output.display().lock();
					m_output.display().clear_screen();   // optional; comment out if you want last frame to remain
					m_output.display().unlock();
					was_paused = true;
				}
				PauseAck.store(true, std::memory_order_release);
				continue;
			}
			PauseAck.store(false, std::memory_order_release);
			was_paused = false;

			//Update the screen (50 times per second)
			m_output.display().lock();
			update();
			m_output.display().flush();
			m_output.display().unlock();
		}
	}

	catch (CException& e)
	{
		printf("%s: Exception in display thread: %s.\n", devid_string, e.displayText().c_str());

		// Let the thread die...
	}
}

/**
 * Create and start thread.
 **/
void CCirrus::start_threads()
{
	{
		std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
		advance_scan(cSystem->device_access_time());
		m_scan_paused = false;
	}
	// Repaint and resize through the normal GUI path after any resume.
	m_output.invalidate_dimensions();
	// Resume after reset if the thread already exists
	PauseThread.store(false, std::memory_order_release);

	if (!myThread)
	{
		myThread = new CThread("cirrus");
		printf(" %s", myThread->getName().c_str());
		StopThread = false;
		myThread->start(*this);
	}
}

/**
 * Stop and destroy thread.
 **/
void CCirrus::stop_threads()
{
	{
		std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
		advance_scan(cSystem->device_access_time());
		m_scan_paused = true;
	}
	// During firmware reset, do NOT kill the Cirrus thread (it owns the SDL window).
	// Just pause it so the window stays alive.
	if (cSystem && cSystem->IsResetInProgress())
	{
		PauseThread.store(true, std::memory_order_release);

		// Wait briefly until the Cirrus thread acknowledges pause
		if (myThread)
		{
			for (int spin = 0; spin < 600; spin++) // up to ~600ms
			{
				if (PauseAck.load(std::memory_order_acquire))
					break;
				CThread::sleep(1);
			}
			// Make it visible in the log whether we paused or stopped
			printf(" cirrus(pause)");
		}
		return;
	}

	// Normal shutdown: actually stop the thread.
	StopThread = true;
	if (myThread)
	{
		printf(" %s", myThread->getName().c_str());
		myThread->join();
		delete myThread;
		myThread = 0;
	}
}

/**
 * Check if threads are still running.
 **/
void CCirrus::check_state()
{
	if (myThread && !myThread->isRunning())
		FAILURE(Thread, "Cirrus thread has died");
}

// Fields not registered by upstream device_start().
void CCirrus::register_chipset_state()
{
	save_item(NAME(vga.crtc.horz_blank_end));
	save_item(NAME(vga.cursor.visible));
	save_item(NAME(vga.oak.reg));
	save_item(NAME(svga.ignore_chain4));
	if (auto* gd5428 = dynamic_cast<cirrus_gd5428_vga_device*>(this))
	{
		save_item(gd5428->gc_mode_ext, "gc_mode_ext");
		save_item(gd5428->gc_locked, "gc_locked");
		save_item(gd5428->m_lock_reg, "m_lock_reg");
		save_item(gd5428->m_gr10, "m_gr10");
		save_item(gd5428->m_gr11, "m_gr11");
		save_item(gd5428->m_cr19, "m_cr19");
		save_item(gd5428->m_cr1a, "m_cr1a");
		save_item(gd5428->m_cr1b, "m_cr1b");
		save_item(gd5428->m_cursor_x, "m_cursor_x");
		save_item(gd5428->m_cursor_y, "m_cursor_y");
		save_item(gd5428->m_cursor_addr, "m_cursor_addr");
		save_item(gd5428->m_cursor_attr, "m_cursor_attr");
		save_item(gd5428->m_ext_palette_enabled, "m_ext_palette_enabled");
		using palette_entry = std::remove_reference_t<decltype(gd5428->m_ext_palette[0])>;
		save_item(gd5428->m_ext_palette, &palette_entry::red, "m_ext_palette.red");
		save_item(gd5428->m_ext_palette, &palette_entry::green, "m_ext_palette.green");
		save_item(gd5428->m_ext_palette, &palette_entry::blue, "m_ext_palette.blue");
		save_item(gd5428->m_blt_status, "m_blt_status");
		save_item(gd5428->m_blt_rop, "m_blt_rop");
		save_item(gd5428->m_blt_mode, "m_blt_mode");
		save_item(gd5428->m_blt_source, "m_blt_source");
		save_item(gd5428->m_blt_dest, "m_blt_dest");
		save_item(gd5428->m_blt_source_pitch, "m_blt_source_pitch");
		save_item(gd5428->m_blt_dest_pitch, "m_blt_dest_pitch");
		save_item(gd5428->m_blt_height, "m_blt_height");
		save_item(gd5428->m_blt_width, "m_blt_width");
		save_item(gd5428->m_blt_source_current, "m_blt_source_current");
		save_item(gd5428->m_blt_dest_current, "m_blt_dest_current");
		save_item(gd5428->m_blt_trans_colour, "m_blt_trans_colour");
		save_item(gd5428->m_blt_trans_colour_mask, "m_blt_trans_colour_mask");
		save_item(gd5428->m_blt_system_transfer, "m_blt_system_transfer");
		save_item(gd5428->m_blt_system_count, "m_blt_system_count");
		save_item(gd5428->m_blt_system_buffer, "m_blt_system_buffer");
		save_item(gd5428->m_blt_pixel_count, "m_blt_pixel_count");
		save_item(gd5428->m_blt_scan_count, "m_blt_scan_count");
		save_item(gd5428->m_scratchpad1, "m_scratchpad1");
		save_item(gd5428->m_scratchpad2, "m_scratchpad2");
		save_item(gd5428->m_scratchpad3, "m_scratchpad3");
		save_item(gd5428->m_vclk_num, "m_vclk_num");
		save_item(gd5428->m_vclk_denom, "m_vclk_denom");
	}
	if (auto* gd5430 = dynamic_cast<cirrus_gd5430_vga_device*>(this))
		save_item(gd5430->m_cr1d, "m_cr1d");
}

template <typename T>
static bool cirrus_write_record(FILE* f, const T& value)
{
	static_assert(std::is_trivially_copyable<T>::value,
		"Saved graphics records must be trivially copyable");
	const u32 size = (u32)sizeof(T);
	return fwrite(&size, sizeof(size), 1, f) == 1 &&
		fwrite(&value, sizeof(value), 1, f) == 1;
}

template <typename T>
static bool cirrus_read_record(FILE* f, T& value)
{
	static_assert(std::is_trivially_copyable<T>::value,
		"Saved graphics records must be trivially copyable");
	u32 size = 0;
	return fread(&size, sizeof(size), 1, f) == 1 && size == sizeof(T) &&
		fread(&value, sizeof(value), 1, f) == 1;
}

static constexpr u32 cirrus_video_magic = 0x32564c43; // CLV2

/**
 * Save state to a Virtual Machine State file.
 **/
int CCirrus::SaveState(FILE* f)
{
	if (!f || !vga.memory || !vga.svga_intf.vram_size)
		return -1;
	advance_scan(cSystem->device_access_time());
	const int res = CPCIDevice::SaveState(f);
	if (res)
		return res;

	if (fwrite(&cirrus_video_magic, sizeof(cirrus_video_magic), 1, f) != 1 ||
		!cirrus_write_record(f, m_snapshot_id) ||
		!save_registered_state(f) ||
		!cirrus_write_record(f, m_scan))
	{
		printf("%s: Could not save graphics state.\n", devid_string);
		return -1;
	}
	printf("%s: Graphics registers and %u bytes of VRAM saved.\n",
		devid_string, (u32)vga.svga_intf.vram_size);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CCirrus::RestoreState(FILE* f)
{
	if (!f || !vga.memory || !vga.svga_intf.vram_size)
		return -1;
	int res;
	res = CPCIDevice::RestoreState(f);
	if (res)
		return res;

	// Stage the complete graphics section so a short read or layout mismatch can't corrupt state.
	RegisteredState saved_cirrus;
	ScanPosition saved_scan{};
	u32 magic = 0, model = 0;
	if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != cirrus_video_magic ||
		!cirrus_read_record(f, model) || model != m_snapshot_id ||
		!read_registered_state(f, saved_cirrus) ||
		!cirrus_read_record(f, saved_scan) ||
		!std::isfinite(saved_scan.character) || saved_scan.character < 0.0 ||
		saved_scan.character >= 65541.0 || saved_scan.line >= 131076 ||
		saved_scan.field > 1)
	{
		printf("%s: Invalid or incompatible graphics state.\n", devid_string);
		return -1;
	}

	if (!commit_registered_state(saved_cirrus))
	{
		printf("%s: Could not restore registered chipset state.\n", devid_string);
		return -1;
	}
	m_scan = saved_scan;
	m_scan_time = cSystem->device_access_time();
	m_scan_initialized = true;
	// Preserve whether the caller stopped the VM. Normal System restore
	// resumes through start_threads; direct device restores remain runnable.

	// Rebuild host rendering caches from the restored registers.
	recompute_params();
	vga.dac.dirty = 1;
	vga_device::palette_update();
	m_output.invalidate_dimensions();
	m_last_refresh_time = std::chrono::steady_clock::time_point{};
	printf("%s: Graphics registers and %u bytes of VRAM restored.\n",
		devid_string, (u32)vga.svga_intf.vram_size);
	return 0;
}
