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
#include "VGA.h"
#include "system/System.h"

#include "emu.h"

#define LOG_REGS (1U << 1)
#define LOG_CRTC (1U << 2)
// Shared MAME register logging follows ES40's VGA debug controls.
static constexpr unsigned vga_log_mask = 0
#if defined(DEBUG_VGA) || defined(DEBUG_VGA_NOISY)
  | (1U << 0)
#endif
#ifdef DEBUG_VGA_NOISY
  | LOG_REGS
#endif
#ifdef DEBUG_VGA_RENDER
  | LOG_CRTC
#endif
  ;
#define VERBOSE vga_log_mask
#include "logmacro.h"
#define LOGREGS(...) LOGMASKED(LOG_REGS, __VA_ARGS__)
#define LOGCRTC(...) LOGMASKED(LOG_CRTC, __VA_ARGS__)

  /**
   * Constructor.
   *
   * Multiple VGA cards require explicit configuration permission.
   **/
CVGA::CVGA(class CConfigurator* cfg, class CSystem* c, int pcibus, int pcidev) : CPCIDevice(cfg, c, pcibus, pcidev)
{
	const bool another_vga = c->has_vga_device(this);
	if (another_vga && !c->graphics_multihead_enabled())
		FAILURE(Configuration,
			"Multiple VGA cards require graphics.multihead=true in the system configuration");
	// The X11 single-display backend still uses this alias. It does not select
	// a firmware console when multiple cards are present.
	theVGA = another_vga ? nullptr : this;
}

/**
 * Destructor.
 **/
CVGA::~CVGA(void)
{
	if (theVGA == this)
		theVGA = nullptr;
}

/**************************************
 *
 * 3bx/3dx implementation
 *
 *************************************/

u8 CVGA::crtc_address_r(offs_t offset)
{
	return vga.crtc.index;
}

void CVGA::crtc_address_w(offs_t offset, u8 data)
{
	vga.crtc.index = data;
}

u8 CVGA::crtc_data_r(offs_t offset)
{
	return space(CRTC_REG).read_byte(vga.crtc.index);
}

void CVGA::crtc_data_w(offs_t offset, u8 data)
{
	vga.crtc.data[vga.crtc.index] = data;
	space(CRTC_REG).write_byte(vga.crtc.index, data);
}

u8 CVGA::input_status_1_r(offs_t offset)
{
	// ES40 uses CS3Trio64::read_b_3da() 
	// MAME needs screen().hblank() and vga_vblank() 
	u8 res = 0;
	vga.attribute.state = 0;
	// Return a plausible value.
	return res;
}

void CVGA::feature_control_w(offs_t offset, u8 data)
{
	vga.feature_control = data;
}

/**************************************
 *
 * 3cx implementation
 *
 *************************************/

u8 CVGA::atc_address_r(offs_t offset)
{
	return vga.attribute.index;
}

u8 CVGA::atc_data_r(offs_t offset)
{
	return space(ATC_REG).read_byte(vga.attribute.index);
}

void CVGA::atc_address_data_w(offs_t offset, u8 data)
{
	if (vga.attribute.state == 0)
	{
		vga.attribute.index = data;
	}
	else
	{
		space(ATC_REG).write_byte(vga.attribute.index, data);
	}

	vga.attribute.state = !vga.attribute.state;
}

u8 CVGA::input_status_0_r(offs_t offset)
{
	// ES40 uses CS3Trio64::read_b_3c2() with hardcoded sense 
	// MAME version needs m_input_sense ioport 
	u8 res = 0x60;  // bit 5-6 = "is VGA"
	res |= vga.crtc.irq_latch << 7;
	return res;

}

void CVGA::miscellaneous_output_w(offs_t offset, u8 data)
{
	vga.miscellaneous_output = data;
	recompute_params();
	m_ioas = bool(BIT(data, 0));
}

u8 CVGA::sequencer_address_r(offs_t offset)
{
	return vga.sequencer.index;
}

u8 CVGA::sequencer_data_r(offs_t offset)
{
	return space(SEQ_REG).read_byte(vga.sequencer.index);
}

void CVGA::sequencer_address_w(offs_t offset, u8 data)
{
	vga.sequencer.index = data;
}

void CVGA::sequencer_data_w(offs_t offset, u8 data)
{
	// TODO: temporary cheat for read-back
	vga.sequencer.data[vga.sequencer.index] = data;
	space(SEQ_REG).write_byte(vga.sequencer.index, data);
	recompute_params();
}

u8 CVGA::ramdac_mask_r(offs_t offset)
{
#ifdef DEBUG_VGA_NOISY
	printf("%s: VGA ramdac_mask_r mask=%02x\n", devid_string, vga.dac.mask);
#endif
	return vga.dac.mask;
}

u8 CVGA::ramdac_state_r(offs_t offset)
{
	return (vga.dac.read) ? 3 : 0;
}

u8 CVGA::ramdac_write_index_r(offs_t offset)
{
	return vga.dac.write_index;
}

u8 CVGA::ramdac_data_r(offs_t offset)
{
	u8 res = 0xFF;
	if (vga.dac.read)
	{
		switch (vga.dac.state++)
		{
		case 0:
			res = vga.dac.color[3 * vga.dac.read_index];
			break;
		case 1:
			res = vga.dac.color[3 * vga.dac.read_index + 1];
			break;
		case 2:
			res = vga.dac.color[3 * vga.dac.read_index + 2];
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

u8 CVGA::feature_control_r(offs_t offset)
{
	return vga.feature_control;
}

u8 CVGA::miscellaneous_output_r(offs_t offset)
{
	return vga.miscellaneous_output;
}

u8 CVGA::gc_address_r(offs_t offset)
{
	return vga.gc.index;
}

u8 CVGA::gc_data_r(offs_t offset)
{
	return space(GC_REG).read_byte(vga.gc.index);
}


void CVGA::ramdac_mask_w(offs_t offset, u8 data)
{
	vga.dac.mask = data;
	vga.dac.dirty = 1;
}

void CVGA::ramdac_read_index_w(offs_t offset, u8 data)
{
	vga.dac.read_index = data;
	vga.dac.state = 0;
	vga.dac.read = 1;
}

void CVGA::ramdac_write_index_w(offs_t offset, u8 data)
{
#ifdef DEBUG_VGA_NOISY
	printf("%s: VGA ramdac_write_index_w data=%02x\n", devid_string, data);
#endif
	vga.dac.write_index = data;
	vga.dac.state = 0;
	vga.dac.read = 0;
}

void CVGA::ramdac_data_w(offs_t offset, u8 data)
{
#ifdef DEBUG_VGA_NOISY
	printf("%s: VGA ramdac_data_w data=%02x read=%d state=%d write_index=%02x\n",
		devid_string, data, vga.dac.read, vga.dac.state, vga.dac.write_index);
#endif
	if (!vga.dac.read)
	{
		vga.dac.loading[vga.dac.state] = data;
		vga.dac.state++;

		if (vga.dac.state == 3)
		{
			vga.dac.color[3 * vga.dac.write_index] = vga.dac.loading[0];
			vga.dac.color[3 * vga.dac.write_index + 1] = vga.dac.loading[1];
			vga.dac.color[3 * vga.dac.write_index + 2] = vga.dac.loading[2];
			vga.dac.dirty = 1;
			vga.dac.state = 0;
			vga.dac.write_index++;
		}
	}
}

void CVGA::gc_address_w(offs_t offset, u8 data)
{
	vga.gc.index = data;
}

void CVGA::gc_data_w(offs_t offset, u8 data)
{
	space(GC_REG).write_byte(vga.gc.index, data);
}

/**************************************
 *
 * CRTC
 *
 *************************************/

void CVGA::crtc_map(address_map &map)
{
	map(0x00, 0x00).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.horz_total & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			// doom (DOS) tries to write to protected regs
			LOGCRTC("CR00 H total %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if(vga.crtc.protect_enable)
				return;
			vga.crtc.horz_total = (vga.crtc.horz_total & ~0xff) | (data & 0xff);
			LOGCRTC("%04d\n", vga.crtc.horz_total);
			recompute_params();
		})
	);
	map(0x01, 0x01).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.horz_disp_end & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			LOGCRTC("CR01 H display end %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if(vga.crtc.protect_enable)
				return;
			vga.crtc.horz_disp_end = (vga.crtc.horz_disp_end & ~0xff) | (data & 0xff);
			LOGCRTC("%04d\n", vga.crtc.horz_disp_end);
			recompute_params();
		})
	);
	map(0x02, 0x02).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.horz_blank_start & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			LOGCRTC("CR02 H start blank %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if(vga.crtc.protect_enable)
				return;
			vga.crtc.horz_blank_start = (vga.crtc.horz_blank_start & ~0xff) | (data & 0xff);
			LOGCRTC("%04d\n", vga.crtc.horz_blank_start);
		})
	);
	map(0x03, 0x03).lrw8(
		NAME([this](offs_t offset) {
			u8 res = vga.crtc.horz_blank_end & 0x1f;
			res   |= (vga.crtc.disp_enable_skew & 3) << 5;
			res   |= (vga.crtc.evra & 1) << 7;
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			LOGCRTC("CR03 H blank end %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if(vga.crtc.protect_enable)
				return;
			vga.crtc.horz_blank_end &= ~0x1f;
			vga.crtc.horz_blank_end |= data & 0x1f;
			vga.crtc.disp_enable_skew = (data & 0x60) >> 5;
			vga.crtc.evra = BIT(data, 7);
			LOGCRTC("%04d evra %d display enable skew %01x\n", vga.crtc.horz_blank_end, vga.crtc.evra, vga.crtc.disp_enable_skew);
		})
	);
	map(0x04, 0x04).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.horz_retrace_start & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			LOGCRTC("CR04 H retrace start %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if(vga.crtc.protect_enable)
				return;
			vga.crtc.horz_retrace_start = (vga.crtc.horz_retrace_start & ~0xff) | (data & 0xff);
			LOGCRTC("%04d\n", vga.crtc.horz_retrace_start);
		})
	);
	map(0x05, 0x05).lrw8(
		NAME([this](offs_t offset) {
			u8 res = (vga.crtc.horz_blank_end & 0x20) << 2;
			res   |= (vga.crtc.horz_retrace_skew & 3) << 5;
			res   |= (vga.crtc.horz_retrace_end & 0x1f);
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			LOGCRTC("CR05 H blank end %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if(vga.crtc.protect_enable)
				return;
			vga.crtc.horz_blank_end &= ~0x20;
			vga.crtc.horz_blank_end |= ((data & 0x80) >> 2);
			vga.crtc.horz_retrace_skew = ((data & 0x60) >> 5);
			vga.crtc.horz_retrace_end = (vga.crtc.horz_retrace_end & ~0x1f) | (data & 0x1f);
			LOGCRTC("%04d retrace skew %01d retrace end %02d\n"
				, vga.crtc.horz_blank_end
				, vga.crtc.horz_retrace_skew
				, vga.crtc.horz_retrace_end
			);
		})
	);
	map(0x06, 0x06).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.vert_total & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			LOGCRTC("CR06 V total %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if(vga.crtc.protect_enable)
				return;
			vga.crtc.vert_total &= ~0xff;
			vga.crtc.vert_total |= data & 0xff;
			LOGCRTC("%04d\n", vga.crtc.vert_total);
			recompute_params();
		})
	);
	// Overflow Register
	map(0x07, 0x07).lrw8(
		NAME([this](offs_t offset) {
			u8 res  = (vga.crtc.line_compare & 0x100) >> 4;
			res    |= (vga.crtc.vert_retrace_start & 0x200) >> 2;
			res    |= (vga.crtc.vert_disp_end & 0x200) >> 3;
			res    |= (vga.crtc.vert_total & 0x200) >> 4;
			res    |= (vga.crtc.vert_blank_start & 0x100) >> 5;
			res    |= (vga.crtc.vert_retrace_start & 0x100) >> 6;
			res    |= (vga.crtc.vert_disp_end & 0x100) >> 7;
			res    |= (vga.crtc.vert_total & 0x100) >> 8;
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.line_compare       &= ~0x100;
			vga.crtc.line_compare       |= ((data & 0x10) << (8-4));
			LOGCRTC("CR07 Overflow %02x -> line compare %04d %s", data, vga.crtc.line_compare, vga.crtc.protect_enable ? "P?\n" : "");
			if(vga.crtc.protect_enable)
				return;
			vga.crtc.vert_total         &= ~0x300;
			vga.crtc.vert_retrace_start &= ~0x300;
			vga.crtc.vert_disp_end      &= ~0x300;
			vga.crtc.vert_blank_start   &= ~0x100;
			vga.crtc.vert_retrace_start |= ((data & 0x80) << (9-7));
			vga.crtc.vert_disp_end      |= ((data & 0x40) << (9-6));
			vga.crtc.vert_total         |= ((data & 0x20) << (9-5));
			vga.crtc.vert_blank_start   |= ((data & 0x08) << (8-3));
			vga.crtc.vert_retrace_start |= ((data & 0x04) << (8-2));
			vga.crtc.vert_disp_end      |= ((data & 0x02) << (8-1));
			vga.crtc.vert_total         |= ((data & 0x01) << (8-0));
			LOGCRTC("V total %04d V retrace start %04d V display end %04d V blank start %04d\n"
				, vga.crtc.vert_total
				, vga.crtc.vert_retrace_start
				, vga.crtc.vert_disp_end
				, vga.crtc.vert_blank_start
			);
			recompute_params();
		})
	);
	// Preset Row Scan Register
	map(0x08, 0x08).lrw8(
		NAME([this](offs_t offset) {
			u8 res = (vga.crtc.byte_panning & 3) << 5;
			res |= (vga.crtc.preset_row_scan & 0x1f);
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.byte_panning = (data & 0x60) >> 5;
			vga.crtc.preset_row_scan = (data & 0x1f);
			LOGCRTC("CR08 Preset Row Scan %02x -> %02d byte panning %d\n"
				, data
				, vga.crtc.preset_row_scan
				, vga.crtc.byte_panning
			);
		})
	);
	// Maximum Scan Line Register
	map(0x09, 0x09).lrw8(
		NAME([this](offs_t offset) {
			u8 res  = (vga.crtc.maximum_scan_line - 1) & 0x1f;
			res    |= (vga.crtc.scan_doubling & 1) << 7;
			res    |= (vga.crtc.line_compare & 0x200) >> 3;
			res    |= (vga.crtc.vert_blank_start & 0x200) >> 4;
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.line_compare      &= ~0x200;
			vga.crtc.vert_blank_start  &= ~0x200;
			vga.crtc.scan_doubling      = ((data & 0x80) >> 7);
			vga.crtc.line_compare      |= ((data & 0x40) << (9-6));
			vga.crtc.vert_blank_start  |= ((data & 0x20) << (9-5));
			vga.crtc.maximum_scan_line  = (data & 0x1f) + 1;
			LOGCRTC("CR09 Maximum Scan Line %02x -> %02d V blank start %04d line compare %04d scan doubling %d\n"
				, data
				, vga.crtc.maximum_scan_line
				, vga.crtc.vert_blank_start
				, vga.crtc.line_compare
				, vga.crtc.scan_doubling
			);
		})
	);
	map(0x0a, 0x0a).lrw8(
		NAME([this](offs_t offset) {
			u8 res  = (vga.crtc.cursor_scan_start & 0x1f);
			res    |= ((vga.crtc.cursor_enable & 1) ^ 1) << 5;
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.cursor_enable = ((data & 0x20) ^ 0x20) >> 5;
			vga.crtc.cursor_scan_start = data & 0x1f;
		})
	);
	map(0x0b, 0x0b).lrw8(
		NAME([this](offs_t offset) {
			u8 res  = (vga.crtc.cursor_skew & 3) << 5;
			res    |= (vga.crtc.cursor_scan_end & 0x1f);
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.cursor_skew = (data & 0x60) >> 5;
			vga.crtc.cursor_scan_end = data & 0x1f;
		})
	);
	map(0x0c, 0x0d).lrw8(
		NAME([this](offs_t offset) {
			return (vga.crtc.start_addr_latch >> ((offset & 1) ^ 1) * 8) & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.start_addr_latch &= ~(0xff << (((offset & 1) ^ 1) * 8));
			vga.crtc.start_addr_latch |= (data << (((offset & 1) ^ 1) * 8));
		})
	);
	map(0x0e, 0x0f).lrw8(
		NAME([this](offs_t offset) {
			return (vga.crtc.cursor_addr >> ((offset & 1) ^ 1)*8) & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.cursor_addr &= ~(0xff << (((offset & 1) ^ 1) * 8));
			vga.crtc.cursor_addr |= (data << (((offset & 1) ^ 1) * 8));
		})
	);
	map(0x10, 0x10).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.vert_retrace_start & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.vert_retrace_start &= ~0xff;
			vga.crtc.vert_retrace_start |= data & 0xff;
			LOGCRTC("CR10 V retrace start %02x -> %04d\n", data, vga.crtc.vert_retrace_start);
		})
	);
	map(0x11, 0x11).lrw8(
		NAME([this](offs_t offset) {
			u8 res  = (vga.crtc.protect_enable & 1) << 7;
			res    |= (vga.crtc.bandwidth & 1) << 6;
			res    |= (vga.crtc.vert_retrace_end & 0xf);
			res    |= (vga.crtc.irq_clear & 1)  << 4;
			res    |= (vga.crtc.irq_disable & 1) << 5;
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.protect_enable = BIT(data, 7);
			vga.crtc.bandwidth = BIT(data, 6);
			// IRQ: Original VGA only supports this for PS/2, but clone cards may supports this on ISA too
			// see https://scalibq.wordpress.com/2022/12/06/the-myth-of-the-vertical-retrace-interrupt/
			vga.crtc.irq_disable = BIT(data, 5);
			vga.crtc.irq_clear = BIT(data, 4);
			vga.crtc.vert_retrace_end = (vga.crtc.vert_retrace_end & ~0xf) | (data & 0x0f);

			if (vga.crtc.irq_clear == 0)
			{
				vga.crtc.irq_latch = 0;
				m_vsync_cb(0);
			}

			LOGCRTC("CR11 V retrace end %02x -> %02d protect enable %d bandwidth %d irq %02x\n"
				, data
				, vga.crtc.vert_retrace_end
				, vga.crtc.protect_enable
				, vga.crtc.bandwidth
				, data & 0x30
			);
		})
	);
	map(0x12, 0x12).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.vert_disp_end & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.vert_disp_end &= ~0xff;
			vga.crtc.vert_disp_end |= data & 0xff;
			LOGCRTC("CR12 V display end %02x -> %04d\n", data, vga.crtc.vert_disp_end);
			recompute_params();
		})
	);
	map(0x13, 0x13).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.offset & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.offset &= ~0xff;
			vga.crtc.offset |= data & 0xff;
		})
	);
	map(0x14, 0x14).lrw8(
		NAME([this](offs_t offset) {
			u8 res  = (vga.crtc.dw & 1) << 6;
			res    |= (vga.crtc.div4 & 1) << 5;
			res    |= (vga.crtc.underline_loc & 0x1f);
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.dw = (data & 0x40) >> 6;
			vga.crtc.div4 = (data & 0x20) >> 5;
			vga.crtc.underline_loc = (data & 0x1f);
		})
	);
	map(0x15, 0x15).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.vert_blank_start & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.vert_blank_start &= ~0xff;
			vga.crtc.vert_blank_start |= data & 0xff;
			LOGCRTC("CR15 V blank start %02x -> %04d\n", data, vga.crtc.vert_blank_start);
		})
	);
	map(0x16, 0x16).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.vert_blank_end & 0x7f;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.vert_blank_end = (vga.crtc.vert_blank_end & ~0x7f) | (data & 0x7f);
			LOGCRTC("CR16 V blank end %02x -> %04d\n", data, vga.crtc.vert_blank_end);
		})
	);
	map(0x17, 0x17).lrw8(
		NAME([this](offs_t offset) {
			u8 res  = (vga.crtc.sync_en & 1) << 7;
			res    |= (vga.crtc.word_mode & 1) << 6;
			res    |= (vga.crtc.aw & 1) << 5;
			res    |= (vga.crtc.div2 & 1) << 3;
			res    |= (vga.crtc.sldiv & 1) << 2;
			res    |= (vga.crtc.map14 & 1) << 1;
			res    |= (vga.crtc.map13 & 1) << 0;
			return res;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.sync_en = BIT(data, 7);
			vga.crtc.word_mode = BIT(data, 6);
			vga.crtc.aw = BIT(data, 5);
			vga.crtc.div2 = BIT(data, 3);
			vga.crtc.sldiv = BIT(data, 2);
			vga.crtc.map14 = BIT(data, 1);
			vga.crtc.map13 = BIT(data, 0);
			LOGCRTC("CR17 Mode control %02x -> Sync Enable %d Word/Byte %d Address Wrap select %d\n"
				, data
				, vga.crtc.sync_en
				, vga.crtc.word_mode
				, vga.crtc.aw
			);
			LOGCRTC("\tDIV2 %d Scan Line Divide %d MAP14 %d MAP13 %d\n"
				, vga.crtc.div2
				, vga.crtc.sldiv
				, vga.crtc.map14
				, vga.crtc.map13
			);
		})
	);
	map(0x18, 0x18).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.line_compare & 0xff;
		}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.line_compare &= ~0xff;
			vga.crtc.line_compare |= data & 0xff;
			LOGCRTC("CR18 Line Compare %02x -> %04d\n", data, vga.crtc.line_compare);
		})
	);
	// TODO: (undocumented) CR22 Memory Data Latch Register (read only)
	// map(0x22, 0x22).lr8(
	// (undocumented) CR24 Attribute Controller Toggle Register (read only)
		// 0--- ---- index
		// 1--- ---- data
	map(0x24, 0x24).lr8(
		NAME([this](offs_t offset) {
			if (!machine().side_effects_disabled())
				LOG("CR24 read undocumented Attribute reg\n");
			return vga.attribute.state << 7;
		})
	);
}

uint16_t CVGA::offset()
{
	if (vga.crtc.dw)
		return vga.crtc.offset << 3;
	if (vga.crtc.word_mode)
		return vga.crtc.offset << 1;
	else
		return vga.crtc.offset << 2;
}

uint32_t CVGA::start_addr()
{
	//  popmessage("Offset: %04x  %s %s **",vga.crtc.offset,vga.crtc.dw?"DW":"--",vga.crtc.word_mode?"BYTE":"WORD");
	if (vga.crtc.dw)
		return vga.crtc.start_addr << 2;
	if (vga.crtc.word_mode)
		return vga.crtc.start_addr << 0;
	else
		return vga.crtc.start_addr << 1;
}

uint8_t CVGA::vga_latch_write(int offs, uint8_t data)
{
    uint8_t res = 0;

    switch (vga.gc.write_mode & 3)
    {
    case 0:
        data = rotate_right(data);
        if (vga.gc.enable_set_reset & 1 << offs)
            res = vga_logical_op((vga.gc.set_reset & 1 << offs) ? vga.gc.bit_mask : 0, offs, vga.gc.bit_mask);
        else
            res = vga_logical_op(data, offs, vga.gc.bit_mask);
        break;
    case 1:
        res = vga.gc.latch[offs];
        break;
    case 2:
        res = vga_logical_op((data & 1 << offs) ? 0xff : 0x00, offs, vga.gc.bit_mask);
        break;
    case 3:
        data = rotate_right(data);
        res = vga_logical_op((vga.gc.set_reset & 1 << offs) ? 0xff : 0x00, offs, data & vga.gc.bit_mask);
        break;
    }

    return res;
}

void CVGA::vga_vh_text(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	// TODO: make it a subclassable function, cfr. PR2 Video Select in Paradise family
#define TEXT_COPY_9COLUMN(ch) (((ch & 0xe0) == 0xc0) && (vga.attribute.data[0x10] & 4))

	int width = BIT(vga.sequencer.data[1], 0) ? 8 : 9, height = (vga.crtc.maximum_scan_line) * (vga.crtc.scan_doubling + 1);

	const u32 frame_number = (u32)(screen().frame_number());

	if (vga.crtc.cursor_enable)
		vga.cursor.visible = frame_number & 0x10;
	else
		vga.cursor.visible = 0;

	bool blink_rate = frame_number & 0x20;
	const int TEXT_LINES = vga.crtc.vert_disp_end + 1;

	unsigned addr = 0;
	int line = 0;
	for (addr = vga.crtc.start_addr, line = -vga.crtc.preset_row_scan; line < TEXT_LINES;
		line += height, addr += (offset() >> 1))
	{
		for (unsigned pos = addr, column = 0; column < vga.crtc.horz_disp_end + 1; column++, pos++)
		{
			const uint8_t ch = vga.memory[(pos << 1) + 0];
			const uint8_t attr = vga.memory[(pos << 1) + 1];
			const uint32_t font_base = vga.sequencer.char_sel.base[BIT(attr, 3)] + (ch << 5);
			const bool blink_sel = !!BIT(vga.attribute.data[0x10], 3);
			const uint8_t blink_en = (blink_sel && blink_rate) ? BIT(attr, 7) : 0;

			uint8_t fore_col = attr & 0xf;
			uint8_t back_col = (attr & 0x70) >> 4;
			// blink disabled translates MSB of attribute as extra intensity for background color
			back_col |= (blink_sel) ? 0 : (BIT(attr, 7) >> 4);

			for (int h = std::max(-line, 0); (h < height) && (line + h < std::min(TEXT_LINES, bitmap.height())); h++)
			{
				uint32_t* const bitmapline = &bitmap.pix(line + h);
				uint8_t bits = vga.memory[font_base + (h >> (vga.crtc.scan_doubling))];

				int mask, w;
				for (mask = 0x80, w = 0; (w < width) && (w < 8); w++, mask >>= 1)
				{
					pen_t pen;
					if (bits & mask)
						pen = vga.pens[blink_en ? back_col : fore_col];
					else
						pen = vga.pens[back_col];

					const int dest_x = column * width + w;
					if (!screen().visible_area().contains(dest_x, line + h))
						continue;
					bitmapline[dest_x] = pen;

				}
				if (w < width)
				{
					/* 9 column */
					pen_t pen;
					if (TEXT_COPY_9COLUMN(ch) && (bits & 1))
						pen = vga.pens[blink_en ? back_col : fore_col];
					else
						pen = vga.pens[back_col];

					const int dest_x = column * width + w;

					if (!screen().visible_area().contains(dest_x, line + h))
						continue;
					bitmapline[dest_x] = pen;
				}
			}
			if (vga.cursor.visible && (pos == vga.crtc.cursor_addr))
			{
				for (int h = vga.crtc.cursor_scan_start;
					(h <= vga.crtc.cursor_scan_end) && (h < height) && (line + h < TEXT_LINES);
					h++)
				{
					const int dest_x = column * width;

					if (!screen().visible_area().contains(dest_x, line + h))
						continue;
					bitmap.plot_box(dest_x, line + h, width, 1, vga.pens[attr & 0xf]);
				}
			}
		}
	}
}

void CVGA::vga_vh_ega(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	const int height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);
	const int pel_shift = (vga.attribute.pel_shift & 7);
	const int LINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const int EGA_COLUMNS = vga.crtc.horz_disp_end + 1;

	for (int addr = vga.crtc.start_addr, line = 0; line < LINES; line += height, addr += offset())
	{
		for (int yi = 0; yi < height; yi++)
		{
			uint32_t* const bitmapline = &bitmap.pix(line + yi);
			// ibm_5150:batmanmv uses this on gameplay for both EGA and "VGA" modes
			// NB: EGA mode in that game sets 663, should be 303 like the other mode
			// causing no status bar to appear. This is a known btanb in how VGA
			// handles EGA mode, cfr. https://www.os2museum.com/wp/fantasyland-on-vga/
			if ((line + yi) == (vga.crtc.line_compare & 0x3ff))
				addr = 0;

			for (int pos = addr, c = 0, column = 0; column < EGA_COLUMNS + 1; column++, c += 8, pos = (pos + 1) & 0xffff)
			{
				int data[4] = {
						vga.memory[(pos & 0xffff)],
						vga.memory[(pos & 0xffff) + 0x10000] << 1,
						vga.memory[(pos & 0xffff) + 0x20000] << 2,
						vga.memory[(pos & 0xffff) + 0x30000] << 3 };

				for (int i = 7; i >= 0; i--)
				{
					pen_t pen = vga.pens[(data[0] & 1) | (data[1] & 2) | (data[2] & 4) | (data[3] & 8)];

					data[0] >>= 1;
					data[1] >>= 1;
					data[2] >>= 1;
					data[3] >>= 1;

					if (!screen().visible_area().contains(c + i - pel_shift, line + yi))
						continue;
					bitmapline[c + i - pel_shift] = pen;
				}
			}
		}
	}
}

// In mode 13h (256 colors) every pixel actually double height/width
// i.e. a 320x200 is really 640x400
void CVGA::vga_vh_vga(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	int height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);
	int pel_shift = (vga.attribute.pel_shift & 6);
	int addrmask = vga.crtc.no_wrap ? -1 : 0xffff;

	const int LINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	/* line compare is screen sensitive */
	uint16_t mask_comp = 0x3ff; //| (LINES & 0x300);
	const int VGA_COLUMNS = vga.crtc.horz_disp_end + 1;
	//  popmessage("%02x %02x",vga.attribute.pel_shift,vga.sequencer.data[4] & 0x08);

	int curr_addr = 0;
	if (!(vga.sequencer.data[4] & 0x08))
	{
		for (int addr = start_addr(), line = 0; line < LINES; line += height, addr += offset(), curr_addr += offset())
		{
			for (int yi = 0; yi < height; yi++)
			{
				if ((line + yi) < (vga.crtc.line_compare & mask_comp))
					curr_addr = addr;
				if ((line + yi) == (vga.crtc.line_compare & mask_comp))
				{
					curr_addr = 0;
					pel_shift = 0;
				}
				uint32_t* const bitmapline = &bitmap.pix(line + yi);
				for (int pos = curr_addr, c = 0, column = 0; column < VGA_COLUMNS + 1; column++, c += 8, pos++)
				{
					if (pos > 0x80000 / 4)
						return;

					for (int xi = 0; xi < 8; xi++)
					{
						if (!screen().visible_area().contains(c + xi - (pel_shift), line + yi))
							continue;
						bitmapline[c + xi - (pel_shift)] = pen(vga.memory[(pos & addrmask) + ((xi >> 1) * 0x10000)]);
					}
				}
			}
		}
	}
	else
	{
		for (int addr = start_addr(), line = 0; line < LINES; line += height, addr += offset(), curr_addr += offset())
		{
			for (int yi = 0; yi < height; yi++)
			{
				if ((line + yi) < (vga.crtc.line_compare & mask_comp))
					curr_addr = addr;
				if ((line + yi) == (vga.crtc.line_compare & mask_comp))
					curr_addr = 0;
				uint32_t* const bitmapline = &bitmap.pix(line + yi);
				//addr %= 0x80000;
				for (int pos = curr_addr, c = 0, column = 0; column < VGA_COLUMNS + 1; column++, c += 0x10, pos += 0x8)
				{
					if (pos + 0x08 > 0x80000)
						return;

					for (int xi = 0; xi < 0x10; xi++)
					{
						if (!screen().visible_area().contains(c + xi - (pel_shift), line + yi))
							continue;
						bitmapline[c + xi - pel_shift] = pen(vga.memory[(pos + (xi >> 1)) & addrmask]);
					}
				}
			}
		}
	}
}

void CVGA::vga_vh_cga(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	const int LINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);

	const int height = (vga.crtc.scan_doubling + 1);
	const int width = (vga.crtc.horz_disp_end + 1) * 8;

	for (int y = 0; y < LINES; y++)
	{
		uint32_t addr = ((y & 1) * 0x2000) + (((y & ~1) >> 1) * width / 4);

		for (int x = 0; x < width; x += 4)
		{
			for (int yi = 0; yi < height; yi++)
			{
				uint32_t* const bitmapline = &bitmap.pix(y * height + yi);

				for (int xi = 0; xi < 4; xi++)
				{
					pen_t pen = vga.pens[(vga.memory[addr] >> (6 - xi * 2)) & 3];
					if (!screen().visible_area().contains(x + xi, y * height + yi))
						continue;
					bitmapline[x + xi] = pen;
				}
			}

			addr++;
		}
	}
}

void CVGA::vga_vh_mono(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	const int LINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);

	const int height = (vga.crtc.scan_doubling + 1);
	const int width = (vga.crtc.horz_disp_end + 1) * 8;

	for (int y = 0; y < LINES; y++)
	{
		uint32_t addr = ((y & 1) * 0x2000) + (((y & ~1) >> 1) * width / 8);

		for (int x = 0; x < width; x += 8)
		{
			for (int yi = 0; yi < height; yi++)
			{
				uint32_t* const bitmapline = &bitmap.pix(y * height + yi);

				for (int xi = 0; xi < 8; xi++)
				{
					pen_t pen = vga.pens[(vga.memory[addr] >> (7 - xi)) & 1];
					if (!screen().visible_area().contains(x + xi, y * height + yi))
						continue;
					bitmapline[x + xi] = pen;
				}
			}

			addr++;
		}
	}
}

void CVGA::palette_update()
{
	for (int i = 0; i < 256; i++)
	{
		set_pen_color(i,
			pal6bit(vga.dac.color[3 * (i & vga.dac.mask) + 0] & 0x3f),
			pal6bit(vga.dac.color[3 * (i & vga.dac.mask) + 1] & 0x3f),
			pal6bit(vga.dac.color[3 * (i & vga.dac.mask) + 2] & 0x3f)
		);
	}
#ifdef DEBUG_VGA_RENDER
	// dump first few palette entries
	static bool palette_dumped = false;
	if (!palette_dumped) {
		for (int i = 0; i < 8; i++) {
			printf("%s: VGA DAC[%d] = R:%02x G:%02x B:%02x (mask=%02x)\n",
				devid_string, i,
				vga.dac.color[3 * (i & vga.dac.mask) + 0],
				vga.dac.color[3 * (i & vga.dac.mask) + 1],
				vga.dac.color[3 * (i & vga.dac.mask) + 2],
				vga.dac.mask);
		}
		palette_dumped = true;
	}
#endif
}

u16 CVGA::line_compare_mask()
{
	return 0x3ff;
}

void CVGA::svga_vh_rgb8(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	const int height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);

	const uint16_t mask_comp = line_compare_mask();
	int curr_addr = 0;
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

void CVGA::svga_vh_rgb15(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	constexpr uint32_t IV = 0xff000000;
	const unsigned height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);

	const unsigned TLINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const unsigned TGA_COLUMNS = vga.crtc.horz_disp_end + 1;
	/* line compare is screen sensitive */
//  uint16_t mask_comp = 0xff | (TLINES & 0x300);
	int curr_addr = 0;
	int yi = 0;

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

void CVGA::svga_vh_rgb16(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	constexpr uint32_t IV = 0xff000000;
	const unsigned height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);
	const unsigned TLINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const unsigned TGA_COLUMNS = vga.crtc.horz_disp_end + 1;

	/* line compare is screen sensitive */
//  uint16_t mask_comp = 0xff | (TLINES & 0x300);
	int curr_addr = 0;
	int yi = 0;

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

void CVGA::svga_vh_rgb24(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	constexpr uint32_t ID = 0xff000000;
	const int height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);
	const int TLINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const int TGA_COLUMNS = vga.crtc.horz_disp_end + 1;

	/* line compare is screen sensitive */
//  uint16_t mask_comp = 0xff | (TLINES & 0x300);
	int curr_addr = 0;
	int yi = 0;

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

void CVGA::svga_vh_rgb32(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	constexpr uint32_t ID = 0xff000000;
	const int height = vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1);
	const int TLINES = (vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1);
	const int TGA_COLUMNS = vga.crtc.horz_disp_end + 1;

	//  uint16_t mask_comp;

		/* line compare is screen sensitive */
	//  mask_comp = 0xff | (TLINES & 0x300);
	int curr_addr = 0;
	int yi = 0;

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

uint8_t CVGA::pc_vga_choosevideomode()
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

uint32_t CVGA::screen_update(bitmap_rgb32& bitmap, const rectangle& cliprect)
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

// recompute_params_clock

void CVGA::recompute_params()
{
//	if (vga.miscellaneous_output & 8)
//		LOGWARN("Warning: VGA external clock latch selected\n");
//	else
//		recompute_params_clock(1, ((vga.miscellaneous_output & 0xc) ? XTAL(28'636'363) : XTAL(25'174'800)).value());
}

uint8_t CVGA::vga_vblank()
{
	uint8_t res;
	uint16_t vblank_start, vblank_end, vpos;

	/* calculate vblank start / end positions */
	res = 0;
	vblank_start = vga.crtc.vert_blank_start;
	vblank_end = vga.crtc.vert_blank_start + vga.crtc.vert_blank_end;
	vpos = screen().vpos();

	/* check if we are under vblank period */
	if (vblank_end > vga.crtc.vert_total)
	{
		vblank_end -= vga.crtc.vert_total;
		if (vpos >= vblank_start || vpos <= vblank_end)
			res = 1;
	}
	else
	{
		if (vpos >= vblank_start && vpos <= vblank_end)
			res = 1;
	}

	//popmessage("%d %d %d - SR1=%02x",vblank_start,vblank_end,vga.crtc.vert_total,vga.sequencer.data[1]);

	return res;
}

// vga_vblank

// device_input_ports

// TODO: convert to mapped space
uint8_t CVGA::mem_r(offs_t offset)
{
	switch (vga.gc.memory_map_sel & 0x03)
	{
	case 0: break;
	case 1: offset &= 0x0ffff; break;
	case 2: offset -= 0x10000; offset &= 0x07fff; break;
	case 3: offset -= 0x18000; offset &= 0x07fff; break;
	}

	if (vga.sequencer.data[4] & 4)
	{
		int data;
		if (/*!machine().side_effects_disabled() move this over in a refactor */ true)
		{
			vga.gc.latch[0] = vga.memory[(offset)];
			vga.gc.latch[1] = vga.memory[(offset)+0x10000];
			vga.gc.latch[2] = vga.memory[(offset)+0x20000];
			vga.gc.latch[3] = vga.memory[(offset)+0x30000];
		}

		if (vga.gc.read_mode)
		{
			// In Read Mode 1 latch is checked against this
			// cfr. lombrall & intsocch where they RMW sprite-like objects
			// and anything outside this formula goes transparent.
			const u8 target_color = (vga.gc.color_compare & vga.gc.color_dont_care);
			data = 0;

			for (u8 byte = 0; byte < 8; byte++)
			{
				u8 fill_latch = 0;
				for (u8 layer = 0; layer < 4; layer++)
				{
					if (vga.gc.latch[layer] & 1 << byte)
						fill_latch |= 1 << layer;
				}
				fill_latch &= vga.gc.color_dont_care;
				if (fill_latch == target_color)
					data |= 1 << byte;
			}
		}
		else
			data = vga.gc.latch[vga.gc.read_map_sel];

		return data;
	}
	else
	{
		uint8_t i, data;

		data = 0;

		for (i = 0; i < 4; i++)
		{
			if (vga.sequencer.map_mask & 1 << i)
				data |= vga.memory[offset + i * 0x10000];
		}

		return data;
	}

	// never executed
	//return 0;
}

void CVGA::mem_w(offs_t offset, uint8_t data)
{
	//Inside each case must prevent writes to non-mapped VGA memory regions, not only mask the offset.
	switch (vga.gc.memory_map_sel & 0x03)
	{
	case 0: break;
	case 1:
		if (offset & 0x10000)
			return;

		offset &= 0x0ffff;
		break;
	case 2:
		if ((offset & 0x18000) != 0x10000)
			return;

		offset &= 0x07fff;
		break;
	case 3:
		if ((offset & 0x18000) != 0x18000)
			return;

		offset &= 0x07fff;
		break;
	}

	{
		uint8_t i;

		for (i = 0; i < 4; i++)
		{
			if (vga.sequencer.map_mask & 1 << i)
				vga.memory[offset + i * 0x10000] = (vga.sequencer.data[4] & 4) ? vga_latch_write(i, data) : data;
		}
		return;
	}
}

// TODO: is there any non-SVGA capable board capable of linear access?
uint8_t CVGA::mem_linear_r(offs_t offset)
{
	return vga.memory[offset % vga.svga_intf.vram_size];
}

void CVGA::mem_linear_w(offs_t offset, uint8_t data)
{
	vga.memory[offset % vga.svga_intf.vram_size] = data;
}

// es40 specific: unimplemented register reporting

void CVGA::report_unimplemented(const char* what, u32 index, u32 value, bool write)
{
	if (!m_unimplemented_seen.emplace(what, index, write).second)
		return;
	char text[192];
	if (write)
		snprintf(text, sizeof text, "Unimplemented %s write at 0x%x (value 0x%x)",
			what, index, value);
	else
		snprintf(text, sizeof text, "Unimplemented %s read at 0x%x", what, index);
	// Bounded so an unmodeled aperture cannot flood the console.
	if (m_unimplemented_printed < 256)
		printf("%s: %s.\n", devid_string, text);
	else if (m_unimplemented_printed == 256)
		printf("%s: Further unimplemented accesses are not printed.\n", devid_string);
	if (m_unimplemented_printed <= 256)
		++m_unimplemented_printed;
	trace_unimplemented(text);
}

void CVGA::unimplemented_map(address_map& map, const char* what)
{
	map.unmap_value_high();
	map(0, map.size() - 1).lrw8(
		NAME(([this, what](offs_t index) -> u8 {
			report_unimplemented(what, index, 0, false);
			return 0xff;
			})),
		NAME(([this, what](offs_t index, u8 data) {
			report_unimplemented(what, index, data, true);
			})));
}

/**
 * Variable pointer to the one and only VGA card.
 **/
CVGA* theVGA = 0;
