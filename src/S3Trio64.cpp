/* ES40 emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project & Others
 * Copyright (C) 2020-2025 by gdwnldsKSC
 * Copyright (C) 2014-2024 by Barry Rodewald, MAME project
 *
 * WWW    : https://github.com/gdwnldsKSC/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 */

 /**
  * \file
  * Contains the code for emulated S3 Trio 64 Video Card device.
  *
  * $Id$
  *
  * X-1.21       gdwnldsKSC                                      27-AUG-2025
  *      Real S3 BIOS boots now! Not 100% implemented but....
  *      Quite a lot of CRTC implementation and behavior added and fixed
  *
  * X-1.20       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.19       Camiel Vanderhoeven                             13-APR-2008
  *      Fixed Doxygen comment.
  *
  * X-1.18       Camiel Vanderhoeven                             25-MAR-2008
  *      Added comments on VGA registers.
  *
  * X-1.17       Camiel Vanderhoeven                             16-MAR-2008
  *      Fixed threading problems with SDL (I hope).
  *
  * X-1.16       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.15       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.14       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.13       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.12       Brian Wheeler                                   27-FEB-2008
  *      Avoid compiler warnings.
  *
  * X-1.11       Fang Zhe                                        08-JAN-2008
  *      Endianess.
  *
  * X-1.10       Camiel Vanderhoeven                             02-JAN-2008
  *      Cleanup.
  *
  * X-1.9        Camiel Vanderhoeven                             30-DEC-2007
  *      Print file id on initialization.
  *
  * X-1.8        Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.7        Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.6        Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.5        Brian Wheeler                                   10-DEC-2007
  *      Made refresh function name unique.
  *
  * X-1.4        Camiel Vanderhoeven                             10-DEC-2007
  *      Use new base class VGA.
  *
  * X-1.3        Camiel Vanderhoeven                             7-DEC-2007
  *      Code cleanup.
  *
  * X-1.2        Camiel Vanderhoeven/Brian Wheeler               6-DEC-2007
  *      Changed implementation (with thanks to the Bochs project!!)
  *
  * X-1.1        Camiel Vanderhoeven                             1-DEC-2007
  *      Initial version in CVS.
  **/
#include "StdAfx.h"
#include "S3Trio64.h"
#include <cmath>
#include "System.h"
#include "AliM1543C.h"
#include <algorithm>
#include <chrono>
#include <type_traits>
#include <vector>
#include "gui/gui.h"
#include "xtal.h"
#include "emu.h"

// begin MAME code

#define LOG_WARN      (1U << 1)
#define LOG_REGS      (1U << 2) // deprecated
#define LOG_DSW       (1U << 3) // Input sense at $3c2
#define LOG_CRTC      (1U << 4) // CRTC setups with monitor geometry

//#define VERBOSE (LOG_GENERAL | LOG_CRTC | LOG_WARN | LOG_REGS)
  //#define LOG_OUTPUT_FUNC osd_printf_info
#include "logmacro.h"

// TODO: remove this enum
enum
{
	IBM8514_IDLE = 0,
	IBM8514_DRAWING_RECT,
	IBM8514_DRAWING_LINE,
	IBM8514_DRAWING_BITBLT,
	IBM8514_DRAWING_PATTERN,
	IBM8514_DRAWING_SSV_1,
	IBM8514_DRAWING_SSV_2,
	MACH8_DRAWING_SCAN
};

#define LOGWARN(...)           LOGMASKED(LOG_WARN, __VA_ARGS__)
#define LOGREGS(...)           LOGMASKED(LOG_REGS, __VA_ARGS__)
#define LOGDSW(...)            LOGMASKED(LOG_DSW, __VA_ARGS__)
#define LOGCRTC(...)           LOGMASKED(LOG_CRTC, __VA_ARGS__)


/***************************************************************************

	Local variables

***************************************************************************/

//#define TEXT_LINES (LINES_HELPER)
#define LINES ((vga.crtc.vert_disp_end + 1) * (get_interlace_mode() + 1))
#define TEXT_LINES (vga.crtc.vert_disp_end+1)

#define GRAPHIC_MODE (vga.gc.alpha_dis) /* else text mode */

#define EGA_COLUMNS (vga.crtc.horz_disp_end+1)
#define EGA_LINE_LENGTH (vga.crtc.offset<<1)

#define VGA_COLUMNS (vga.crtc.horz_disp_end+1)
#define VGA_LINE_LENGTH (vga.crtc.offset<<3)

#define VGA_CH_WIDTH ((vga.sequencer.data[1]&1)?8:9)

#define TEXT_COLUMNS (vga.crtc.horz_disp_end+1)
#define TEXT_START_ADDRESS (vga.crtc.start_addr<<3)
#define TEXT_LINE_LENGTH (vga.crtc.offset<<1)

#define TEXT_COPY_9COLUMN(ch) (((ch & 0xe0) == 0xc0)&&(vga.attribute.data[0x10]&4))

// Special values for SVGA Trident - Mode Vesa 110h
#define TLINES (LINES)
#define TGA_COLUMNS (EGA_COLUMNS)
#define TGA_LINE_LENGTH (vga.crtc.offset<<3)

static int s3_diag_update_counter = 0;
static int s3_diag_frame_counter = 0;

// MAME FUNCTIONS - not all present yet

//s3vision864_vga_device::s3vision864_vga_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock)
//	: s3vision864_vga_device(mconfig, S3_VISION864_VGA, tag, owner, clock)

//s3vision864_vga_device::s3vision864_vga_device(const machine_config& mconfig, device_type type, const char* tag, device_t* owner, uint32_t clock)
//	: svga_device(mconfig, type, tag, owner, clock)

// void s3vision864_vga_device::device_add_mconfig(machine_config &config)

uint32_t CS3Trio64::latch_start_addr()
{
	if (s3.memory_config & 0x08)
	{
		// - SDD scrolling test expects a << 2 for 8bpp and no shift for anything else
		// - Slackware 3.x XF86_S3 expect a << 2 shift (to be confirmed)
		// - przonegd expect no shift (RGB16)
		return vga.crtc.start_addr_latch << (svga.rgb8_en ? 2 : 0);
	}
	return vga.crtc.start_addr_latch;
}

// void s3vision864_vga_device::device_start()

// void s3vision864_vga_device::device_reset()

u16 CS3Trio64::line_compare_mask()
{
	// TODO: pinpoint condition
	return svga.rgb8_en ? 0x7ff : 0x3ff;
}

uint16_t CS3Trio64::offset()
{
	if (s3.memory_config & 0x08)
		return vga.crtc.offset << 3;
	return CVGA::offset();
}

void CS3Trio64::s3_define_video_mode()
{
	int divisor = 1;
	int xtal = ((vga.miscellaneous_output & 0xc) ? XTAL(28'636'363) : XTAL(25'174'800)).value();
	double freq;

	if ((vga.miscellaneous_output & 0xc) == 0x0c)
	{
		// DCLK calculation
		freq = ((double)(s3.clk_pll_m + 2) / (double)((s3.clk_pll_n + 2) * (pow(2.0, s3.clk_pll_r)))) * 14.318; // clock between XIN and XOUT
		xtal = freq * 1000000;
	}

	if ((s3.ext_misc_ctrl_2) >> 4)
	{
		svga.rgb8_en = 0;
		svga.rgb15_en = 0;
		svga.rgb16_en = 0;
		svga.rgb32_en = 0;
		// FIXME: vision864 has only first 7 modes
		switch ((s3.ext_misc_ctrl_2) >> 4)
		{
			// 0001 Mode 8: 2x 8-bit 1 VCLK/2 pixels
		case 0x01: svga.rgb8_en = 1; break;
			// 0010 Mode 1: 15-bit 2 VCLK/pixel
		case 0x02: svga.rgb15_en = 1; break;
			// 0011 Mode 9: 15-bit 1 VCLK/pixel
		case 0x03: svga.rgb15_en = 1; divisor = 2; break;
			// 0100 Mode 2: 24-bit 3 VCLK/pixel
		case 0x04: svga.rgb24_en = 1; break;
			// 0101 Mode 10: 16-bit 1 VCLK/pixel
		case 0x05: svga.rgb16_en = 1; divisor = 2; break;
			// 0110 Mode 3: 16-bit 2 VCLK/pixel
		case 0x06: svga.rgb16_en = 1; break;
			// 0111 Mode 11: 24/32-bit 2 VCLK/pixel
		case 0x07: svga.rgb32_en = 1; divisor = 4; break;
		case 0x0d: svga.rgb32_en = 1; divisor = 1; break;
		default:
			popmessage("pc_vga_s3: PA16B-COLOR-MODE %02x\n", ((s3.ext_misc_ctrl_2) >> 4));
			break;
		}
	}
	else
	{
		// 0000: Mode 0 8-bit 1 VCLK/pixel
		svga.rgb8_en = (s3.memory_config & 8) >> 3;
		svga.rgb15_en = 0;
		svga.rgb16_en = 0;
		svga.rgb32_en = 0;
	}

#ifdef DEBUG_VGA_RENDER
	LOG("S3: define_video_mode: ext_misc_ctrl_2=%02x memory_config=%02x "
		"rgb8=%d rgb15=%d rgb16=%d rgb32=%d crtc_offset=%03x offset()=%d\n",
		s3.ext_misc_ctrl_2, s3.memory_config,
		svga.rgb8_en, svga.rgb15_en, svga.rgb16_en, svga.rgb32_en,
		vga.crtc.offset, offset());
#endif

	recompute_params_clock(divisor, xtal);
}

void CS3Trio64::refresh_pitch_offset()
{
	// bit 2 = bit 8 of offset register, but only if bits 4-5 of CR51 are 00h.
	vga.crtc.offset &= 0xff;
	if ((s3.cr51 & 0x30) == 0)
		vga.crtc.offset |= (s3.cr43 & 0x04) << 6;
	else
		vga.crtc.offset |= (s3.cr51 & 0x30) << 4;
}

void CS3Trio64::crtc_map(address_map& map)
{
	map(0x00, 0x00).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.horz_total & 0xff;
			}),
		NAME([this](offs_t offset, u8 data) {
			// doom (DOS) tries to write to protected regs
			LOGCRTC("CR00 H total %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if (vga.crtc.protect_enable)
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
			if (vga.crtc.protect_enable)
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
			if (vga.crtc.protect_enable)
				return;
			vga.crtc.horz_blank_start = (vga.crtc.horz_blank_start & ~0xff) | (data & 0xff);
			LOGCRTC("%04d\n", vga.crtc.horz_blank_start);
			})
	);
	map(0x03, 0x03).lrw8(
		NAME([this](offs_t offset) {
			u8 res = vga.crtc.horz_blank_end & 0x1f;
			res |= (vga.crtc.disp_enable_skew & 3) << 5;
			res |= (vga.crtc.evra & 1) << 7;
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			LOGCRTC("CR03 H blank end %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if (vga.crtc.protect_enable)
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
			if (vga.crtc.protect_enable)
				return;
			vga.crtc.horz_retrace_start = (vga.crtc.horz_retrace_start & ~0xff) | (data & 0xff);
			LOGCRTC("%04d\n", vga.crtc.horz_retrace_start);
			})
	);
	map(0x05, 0x05).lrw8(
		NAME([this](offs_t offset) {
			u8 res = (vga.crtc.horz_blank_end & 0x20) << 2;
			res |= (vga.crtc.horz_retrace_skew & 3) << 5;
			res |= (vga.crtc.horz_retrace_end & 0x1f);
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			LOGCRTC("CR05 H blank end %02x %s", data, vga.crtc.protect_enable ? "P?\n" : "-> ");
			if (vga.crtc.protect_enable)
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
			if (vga.crtc.protect_enable)
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
			u8 res = (vga.crtc.line_compare & 0x100) >> 4;
			res |= (vga.crtc.vert_retrace_start & 0x200) >> 2;
			res |= (vga.crtc.vert_disp_end & 0x200) >> 3;
			res |= (vga.crtc.vert_total & 0x200) >> 4;
			res |= (vga.crtc.vert_blank_start & 0x100) >> 5;
			res |= (vga.crtc.vert_retrace_start & 0x100) >> 6;
			res |= (vga.crtc.vert_disp_end & 0x100) >> 7;
			res |= (vga.crtc.vert_total & 0x100) >> 8;
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.line_compare &= ~0x100;
			vga.crtc.line_compare |= ((data & 0x10) << (8 - 4));
			LOGCRTC("CR07 Overflow %02x -> line compare %04d %s", data, vga.crtc.line_compare, vga.crtc.protect_enable ? "P?\n" : "");
			if (vga.crtc.protect_enable)
				return;
			vga.crtc.vert_total &= ~0x300;
			vga.crtc.vert_retrace_start &= ~0x300;
			vga.crtc.vert_disp_end &= ~0x300;
			vga.crtc.vert_blank_start &= ~0x100;
			vga.crtc.vert_retrace_start |= ((data & 0x80) << (9 - 7));
			vga.crtc.vert_disp_end |= ((data & 0x40) << (9 - 6));
			vga.crtc.vert_total |= ((data & 0x20) << (9 - 5));
			vga.crtc.vert_blank_start |= ((data & 0x08) << (8 - 3));
			vga.crtc.vert_retrace_start |= ((data & 0x04) << (8 - 2));
			vga.crtc.vert_disp_end |= ((data & 0x02) << (8 - 1));
			vga.crtc.vert_total |= ((data & 0x01) << (8 - 0));
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
			u8 res = (vga.crtc.maximum_scan_line - 1) & 0x1f;
			res |= (vga.crtc.scan_doubling & 1) << 7;
			res |= (vga.crtc.line_compare & 0x200) >> 3;
			res |= (vga.crtc.vert_blank_start & 0x200) >> 4;
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.line_compare &= ~0x200;
			vga.crtc.vert_blank_start &= ~0x200;
			vga.crtc.scan_doubling = ((data & 0x80) >> 7);
			vga.crtc.line_compare |= ((data & 0x40) << (9 - 6));
			vga.crtc.vert_blank_start |= ((data & 0x20) << (9 - 5));
			vga.crtc.maximum_scan_line = (data & 0x1f) + 1;
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
			u8 res = (vga.crtc.cursor_scan_start & 0x1f);
			res |= ((vga.crtc.cursor_enable & 1) ^ 1) << 5;
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.cursor_enable = ((data & 0x20) ^ 0x20) >> 5;
			vga.crtc.cursor_scan_start = data & 0x1f;
			state.vga_mem_updated = 1; // text mode: cursor shape change trigger
			})
	);
	map(0x0b, 0x0b).lrw8(
		NAME([this](offs_t offset) {
			u8 res = (vga.crtc.cursor_skew & 3) << 5;
			res |= (vga.crtc.cursor_scan_end & 0x1f);
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.cursor_skew = (data & 0x60) >> 5;
			vga.crtc.cursor_scan_end = data & 0x1f;
			state.vga_mem_updated = 1; // text mode: cursor shape change trigger
			})
	);
	map(0x0c, 0x0d).lrw8(
		NAME([this](offs_t offset) {
			return (vga.crtc.start_addr_latch >> ((offset & 1) ^ 1) * 8) & 0xff;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.start_addr_latch &= ~(0xff << (((offset & 1) ^ 1) * 8));
			vga.crtc.start_addr_latch |= (data << (((offset & 1) ^ 1) * 8));
			state.vga_mem_updated = 1; // text mode: cursor shape change trigger
			})
	);
	map(0x0e, 0x0f).lrw8(
		NAME([this](offs_t offset) {
			return (vga.crtc.cursor_addr >> ((offset & 1) ^ 1) * 8) & 0xff;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.cursor_addr &= ~(0xff << (((offset & 1) ^ 1) * 8));
			vga.crtc.cursor_addr |= (data << (((offset & 1) ^ 1) * 8));
			state.vga_mem_updated = 1; // text mode: cursor shape change trigger
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
			u8 res = (vga.crtc.protect_enable & 1) << 7;
			res |= (vga.crtc.bandwidth & 1) << 6;
			res |= (vga.crtc.vert_retrace_end & 0xf);
			res |= (vga.crtc.irq_clear & 1) << 4;
			res |= (vga.crtc.irq_disable & 1) << 5;
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
			u8 res = (vga.crtc.dw & 1) << 6;
			res |= (vga.crtc.div4 & 1) << 5;
			res |= (vga.crtc.underline_loc & 0x1f);
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
			u8 res = (vga.crtc.sync_en & 1) << 7;
			res |= (vga.crtc.word_mode & 1) << 6;
			res |= (vga.crtc.aw & 1) << 5;
			res |= (vga.crtc.div2 & 1) << 3;
			res |= (vga.crtc.sldiv & 1) << 2;
			res |= (vga.crtc.map14 & 1) << 1;
			res |= (vga.crtc.map13 & 1) << 0;
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
	map(0x2d, 0x2d).lr8(
		NAME([this](offs_t offset) {
			return s3.id_high;
			})
	);
	map(0x2e, 0x2e).lr8(
		NAME([this](offs_t offset) {
			return s3.id_low;
			})
	);
	map(0x2f, 0x2f).lr8(
		NAME([this](offs_t offset) {
			return s3.revision;
			})
	);
	// CR30 Chip ID/REV register
	map(0x30, 0x30).lr8(
		NAME([this](offs_t offset) {
			return s3.id_cr30;
			})
	);
	// CR31 Memory Configuration Register
	map(0x31, 0x31).lrw8(
		NAME([this](offs_t offset) {
			return s3.memory_config;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.memory_config = data;
			vga.crtc.start_addr_latch &= ~0x30000;
			vga.crtc.start_addr_latch |= ((data & 0x30) << 12);
			s3_define_video_mode();
			})
	);
	// TODO: CR32, CR33 & CR34 (backward compatibility) - in our ES40 we do these ! :)
	// CR32: Backward Compatibility 1 (BKWD_1)
	map(0x32, 0x32).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr32;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr32 = data;
			})
	);
	// CR33: Backward Compatibility 2 (BKWD_2)
	map(0x33, 0x33).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr33;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr33 = data;
			// ES40 extension: CR33 bit5 forces 8-dot chars
			recompute_scanline_layout();
			state.vga_mem_updated = 1;
			})
	);
	// CR34: Backward Compatibility 3 (BKWD_3)
	map(0x34, 0x34).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr34;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr34 = data;
			})
	);
	map(0x35, 0x35).lrw8(
		NAME([this](offs_t offset) {
			return s3.crt_reg_lock;
			}),
		NAME([this](offs_t offset, u8 data) {
			// lock register
			if ((s3.reg_lock1 & 0xc) != 8 || ((s3.reg_lock1 & 0xc0) == 0))
				return;
			s3.crt_reg_lock = data;
			svga.bank_w = data & 0xf;
			svga.bank_r = svga.bank_w;
			})
	);
	// Configuration register 1
	map(0x36, 0x36).lrw8(
		NAME([this](offs_t offset) {
			// PCI (not really), Fast Page Mode DRAM
			return s3.strapping & 0x000000ff;
			}),
		NAME([this](offs_t offset, u8 data) {
			if (s3.reg_lock2 == 0xa5)
			{
				s3.strapping = (s3.strapping & 0xffffff00) | data;
				LOG("CR36: Strapping data = %08x\n", s3.strapping);
			}
			})
	);
	// Configuration register 2
	map(0x37, 0x37).lrw8(
		NAME([this](offs_t offset) {
			return (s3.strapping & 0x0000ff00) >> 8;  // enable chipset, 64k BIOS size, internal DCLK/MCLK
			}),
		NAME([this](offs_t offset, u8 data) {
			// TODO: monitor ID at 7-5 (PD15-13)
			if (s3.reg_lock2 == 0xa5)
			{
				s3.strapping = (s3.strapping & 0xffff00ff) | (data << 8);
				LOG("CR37: Strapping data = %08x\n", s3.strapping);
			}
			})
	);
	map(0x38, 0x38).lrw8(
		NAME([this](offs_t offset) {
			return s3.reg_lock1;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.reg_lock1 = data;
			})
	);
	map(0x39, 0x39).lrw8(
		NAME([this](offs_t offset) {
			return s3.reg_lock2;
			}),
		NAME([this](offs_t offset, u8 data) {
			// TODO: reg lock mechanism
			s3.reg_lock2 = data;
			})
	);
	// CR3A: Miscellaneous 1 Register (MISC_1)
	map(0x3a, 0x3a).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr3a;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr3a = data;
			})
	);
	// CR3B: Data Transfer Position (DT_EX-POS)
	map(0x3b, 0x3b).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr3b;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr3b = data;
			})
	);
	// CR3C: Interlace Retrace Start (IL_RTSTART)
	map(0x3c, 0x3c).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr3c;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr3c = data;
			})
	);
	// CR40: System Configuration Register (SYS_CNFG)
	map(0x40, 0x40).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr40;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr40 = data;
			// enable 8514/A registers (x2e8, x6e8, xae8, xee8)
			s3.enable_8514 = BIT(data, 0);
			})
	);
	// CR41 
	map(0x41, 0x41).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr41;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr41 = data;
			})
	);
	// CR42 Mode Control
	map(0x42, 0x42).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr42;
			}),
		NAME([this](offs_t offset, u8 data) {
			// bit 5 = interlace, bits 0-3 = dot clock (seems to be undocumented)
			s3.cr42 = data;
			s3_define_video_mode();
			})
	);
	map(0x43, 0x43).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr43;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr43 = data;  // bit 2 = bit 8 of offset register, but only if bits 4-5 of CR51 are 00h.
			refresh_pitch_offset();
			s3_define_video_mode();
			})
	);
	/*
	CR45 Hardware Graphics Cursor Mode
	bit    0  HWGC ENB. Hardware Graphics Cursor Enable. Set to enable the
			  HardWare Cursor in VGA and enhanced modes.
		   1  (911/24) Delay Timing for Pattern Data Fetch
		   2  (801/5,928) Hardware Cursor Horizontal Stretch 2. If set the cursor
			   pixels are stretched horizontally to two bytes and items 0 and 1 of
			   the fore/background stacks in 3d4h index 4Ah/4Bh are used.
		   3  (801/5,928) Hardware Cursor Horizontal Stretch 3. If set the cursor
			   pixels are stretched horizontally to three bytes and items 0,1 and
			   2 of the fore/background stacks in 3d4h index 4Ah/4Bh are used.
		 2-3  (805i,864/964) HWC-CSEL. Hardware Cursor Color Select.
				0: 4/8bit, 1: 15/16bt, 2: 24bit, 3: 32bit
			  Note: So far I've had better luck with: 0: 8/15/16bit, 1: 32bit??
		   4  (80x +) Hardware Cursor Right Storage. If set the cursor data is
			   stored in the last 256 bytes of 4 1Kyte lines (4bits/pixel) or the
			   last 512 bytes of 2 2Kbyte lines (8bits/pixel). Intended for
			   1280x1024 modes where there are no free lines at the bottom.
		   5  (928) Cursor Control Enable for Brooktree Bt485 DAC. If set and 3d4h
			   index 55h bit 5 is set the HC1 output becomes the ODF and the HC0
			   output becomes the CDE
			  (964) BT485 ODF Selection for Bt485A RAMDAC. If set pin 185 (RS3
			   /ODF) is the ODF output to a Bt485A compatible RamDAC (low for even
			   fields and high for odd fields), if clear pin185 is the RS3 output.
	 */
	map(0x45, 0x45).lrw8(
		NAME([this](offs_t offset) {
			const u8 res = s3.cursor_mode;
			if (!machine().side_effects_disabled())
			{
				s3.cursor_fg_ptr = 0;
				s3.cursor_bg_ptr = 0;
			}
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_mode = data;
			})
	);
	/*
	CR46/7 Hardware Graphics Cursor Origin-X
	bit 0-10  The HardWare Cursor X position. For 64k modes this value should be
			  twice the actual X co-ordinate.
	 */
	map(0x46, 0x46).lrw8(
		NAME([this](offs_t offset) {
			return (s3.cursor_x & 0xff00) >> 8;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_x = (s3.cursor_x & 0x00ff) | (data << 8);
			})
	);
	map(0x47, 0x47).lrw8(
		NAME([this](offs_t offset) {
			return s3.cursor_x & 0x00ff;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_x = (s3.cursor_x & 0xff00) | data;
			})
	);
	/*
	CR48/9 Hardware Graphics Cursor Origin-Y
	bit  0-9  (911/24) The HardWare Cursor Y position.
		0-10  (80x +) The HardWare Cursor Y position.
	Note: The position is activated when the high byte of the Y coordinate (index
		  48h) is written, so this byte should be written last (not 911/924 ?)
	 */
	map(0x48, 0x48).lrw8(
		NAME([this](offs_t offset) {
			return (s3.cursor_y & 0xff00) >> 8;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_y = (s3.cursor_y & 0x00ff) | (data << 8);
			})
	);
	map(0x49, 0x49).lrw8(
		NAME([this](offs_t offset) {
			return s3.cursor_y & 0x00ff;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_y = (s3.cursor_y & 0xff00) | data;
			})
	);
	/*
	CR4A Hardware Graphics Cursor Foreground Stack       (80x +)
	bit  0-7  The Foreground Cursor color. Three bytes (4 for the 864/964) are
			  stacked here. When the Cursor Mode register (3d4h index 45h) is read
			  the stackpointer is reset. When a byte is written the byte is
			  written into the current top of stack and the stackpointer is
			  increased. The first byte written (item 0) is allways used, the
			  other two(3) only when Hardware Cursor Horizontal Stretch (3d4h
			  index 45h bit 2-3) is enabled.
	 */
	map(0x4a, 0x4a).lrw8(
		NAME([this](offs_t offset) {
			const u8 res = s3.cursor_fg[s3.cursor_fg_ptr];
			if (!machine().side_effects_disabled())
			{
				s3.cursor_fg_ptr++;
				s3.cursor_fg_ptr %= 4;
			}
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_fg[s3.cursor_fg_ptr++] = data;
			s3.cursor_fg_ptr %= 4;
			})
	);
	/*
	CR4B Hardware Graphics Cursor Background Stack       (80x +)
	bit  0-7  The Background Cursor color. Three bytes (4 for the 864/964) are
			  stacked here. When the Cursor Mode register (3d4h index 45h) is read
			  the stackpointer is reset. When a byte is written the byte is
			  written into the current top of stack and the stackpointer is
			  increased. The first byte written (item 0) is allways used, the
			  other two(3) only when Hardware Cursor Horizontal Stretch (3d4h
			  index 45h bit 2-3) is enabled.
	 */
	map(0x4b, 0x4b).lrw8(
		NAME([this](offs_t offset) {
			const u8 res = s3.cursor_bg[s3.cursor_bg_ptr];
			if (!machine().side_effects_disabled())
			{
				s3.cursor_bg_ptr++;
				s3.cursor_bg_ptr %= 4;
			}
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_bg[s3.cursor_bg_ptr++] = data;
			s3.cursor_bg_ptr %= 4;
			})
	);
	/*
	CR4C/D Hardware Graphics Cursor Storage Start Address
	bit  0-9  (911,924) HCS_STADR. Hardware Graphics Cursor Storage Start Address
		0-11  (80x,928) HWGC_STA. Hardware Graphics Cursor Storage Start Address
		0-12  (864,964) HWGC_STA. Hardware Graphics Cursor Storage Start Address
			  Address of the HardWare Cursor Map in units of 1024 bytes (256 bytes
			  for planar modes). The cursor map is a 64x64 bitmap with 2 bits (A
			  and B) per pixel. The map is stored as one word (16 bits) of bit A,
			  followed by one word with the corresponding 16 B bits.
			  The bits are interpreted as:
				 A    B    MS-Windows:         X-11:
				 0    0    Background          Screen data
				 0    1    Foreground          Screen data
				 1    0    Screen data         Background
				 1    1    Inverted screen     Foreground
			  The Windows/X11 switch is only available for the 80x +.
			  (911/24) For 64k color modes the cursor is stored as one byte (8
				bits) of A bits, followed by the 8 B-bits, and each bit in the
				cursor should be doubled to provide a consistent cursor image.
			  (801/5,928) For Hi/True color modes use the Horizontal Stretch bits
				(3d4h index 45h bits 2 and 3).
	 */
	map(0x4c, 0x4c).lrw8(
		NAME([this](offs_t offset) {
			return (s3.cursor_start_addr & 0xff00) >> 8;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_start_addr = (s3.cursor_start_addr & 0x00ff) | (data << 8);
			//popmessage("HW Cursor Data Address %04x\n",s3.cursor_start_addr);
			})
	);
	map(0x4d, 0x4d).lrw8(
		NAME([this](offs_t offset) {
			return s3.cursor_start_addr & 0x00ff;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_start_addr = (s3.cursor_start_addr & 0xff00) | data;
			//popmessage("HW Cursor Data Address %04x\n",s3.cursor_start_addr);
			})
	);
	/*
	CR4E HGC Pattern Disp Start X-Pixel Position
	bit  0-5  Pattern Display Start X-Pixel Position.
	 */
	map(0x4e, 0x4e).lrw8(
		NAME([this](offs_t offset) {
			return s3.cursor_pattern_x;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_pattern_x = data;
			})
	);
	/*
	CR4F HGC Pattern Disp Start Y-Pixel Position
	bit  0-5  Pattern Display Start Y-Pixel Position.
	 */
	map(0x4f, 0x4f).lrw8(
		NAME([this](offs_t offset) {
			return s3.cursor_pattern_y;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cursor_pattern_y = data;
			})
	);
	// CR50
	map(0x50, 0x50).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr50;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr50 = data;
			ibm8514a_device* dev = get_8514();
			dev->ibm8514.color_bpp = (data >> 4) & 3;
			})
	);
	map(0x51, 0x51).lrw8(
		NAME([this](offs_t offset) {
			u8 res = (vga.crtc.start_addr_latch & 0x0c0000) >> 18;
			res |= s3.cr51 & 0x0c;
			//          res   |= ((vga.crtc.offset & 0x0300) >> 4);
			res |= (s3.cr51 & 0x30);
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr51 = data;
			vga.crtc.start_addr_latch &= ~0xc0000;
			vga.crtc.start_addr_latch |= ((data & 0x3) << 18);
			svga.bank_w = (svga.bank_w & 0xcf) | ((data & 0x0c) << 2);
			svga.bank_r = svga.bank_w;
			refresh_pitch_offset();
			s3_define_video_mode();
			})
	);
	// Extended BIOS flag 1 register (EXT_BBFLG1) (CR52)
	map(0x52, 0x52).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr52;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr52 = data;
			})
	);
	map(0x53, 0x53).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr53;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr53 = data;
			})
	);
	// Extended Memory Control 2 Register (EX_MCTL_2) (CR54) 
	map(0x54, 0x54).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr54;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr54 = data;
			})
	);
	/*
	CR55 Extended Video DAC Control Register             (80x +)
	bit 0-1  DAC Register Select Bits. Passed to the RS2 and RS3 pins on the
			 RAMDAC, allowing access to all 8 or 16 registers on advanced RAMDACs.
			 If this field is 0, 3d4h index 43h bit 1 is active.
		  2  Enable General Input Port Read. If set DAC reads are disabled and the
			 STRD strobe for reading the General Input Port is enabled for reading
			 while DACRD is active, if clear DAC reads are enabled.
		  3  (928) Enable External SID Operation if set. If set video data is
			   passed directly from the VRAMs to the DAC rather than through the
			   VGA chip
		  4  Hardware Cursor MS/X11 Mode. If set the Hardware Cursor is in X11
			 mode, if clear in MS-Windows mode
		  5  (80x,928) Hardware Cursor External Operation Mode. If set the two
			  bits of cursor data ,is output on the HC[0-1] pins for the video DAC
			  The SENS pin becomes HC1 and the MID2 pin becomes HC0.
		  6  ??
		  7  (80x,928) Disable PA Output. If set PA[0-7] and VCLK are tristated.
			 (864/964) TOFF VCLK. Tri-State Off VCLK Output. VCLK output tri
			  -stated if set
	 */
	map(0x55, 0x55).lrw8(
		NAME([this](offs_t offset) {
			return s3.extended_dac_ctrl;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.extended_dac_ctrl = data;
			})
	);
	// External Sync Control 1 Register (EX_SYNC_1) (CR56)
	map(0x56, 0x56).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr56;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr56 = data & 0x1F; // bits 7-5 reserved
			})
	);
	// External Sync Control 2 Register (EX_SYNC_2) (CR57)
	map(0x57, 0x57).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr57;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr57 = data;
			})
	);
	// Linear Address Window Control Register (LAW_CTL) (CR58) - dosbox calls VGA_StartUpdateLFB() after storing the value
	map(0x58, 0x58).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr58;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr58 = data;
			on_crtc_linear_regs_changed();
			redraw_area(0, 0, old_iWidth, old_iHeight);
			})
	);
	// Linear Address Window Position High
	map(0x59, 0x59).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr59;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr59 = data;
			update_pci_bar0_from_crtc(u32(data) << 24, 0xff000000U);
			on_crtc_linear_regs_changed();
			})
	);
	// Linear Address Window Position Low
	map(0x5a, 0x5a).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr5a;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr5a = data;
			update_pci_bar0_from_crtc(u32(data) << 16, 0x00800000U);
			on_crtc_linear_regs_changed();
			})
	);
	// undocumented on trio64?
	map(0x5b, 0x5b).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr5b;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr5b = data;
			})
	);
	// TODO: bits 7-4 (w/o?) for GPIO
	map(0x5c, 0x5c).lr8(
		NAME([this](offs_t offset) {
			u8 res = 0;
			// if VGA dot clock is set to 3 (misc reg bits 2-3), then selected dot clock is read, otherwise read VGA clock select
			if ((vga.miscellaneous_output & 0xc) == 0x0c)
				res = s3.cr42 & 0x0f;
			else
				res = (vga.miscellaneous_output & 0xc) >> 2;
			return res;
			})
	);
	// TODO: following two registers must be read-backable
	/*
	CR5D Extended Horizontal Overflow Register           (80x +)
	bit    0  Horizontal Total bit 8. Bit 8 of the Horizontal Total register (3d4h
			  index 0)
		   1  Horizontal Display End bit 8. Bit 8 of the Horizontal Display End
			  register (3d4h index 1)
		   2  Start Horizontal Blank bit 8. Bit 8 of the Horizontal Start Blanking
			  register (3d4h index 2).
		   3  (864,964) EHB+64. End Horizontal Blank +64. If set the /BLANK pulse
			   is extended by 64 DCLKs. Note: Is this bit 6 of 3d4h index 3 or
			   does it really extend by 64 ?
		   4  Start Horizontal Sync Position bit 8. Bit 8 of the Horizontal Start
			  Retrace register (3d4h index 4).
		   5  (864,964) EHS+32. End Horizontal Sync +32. If set the HSYNC pulse
			   is extended by 32 DCLKs. Note: Is this bit 5 of 3d4h index 5 or
			   does it really extend by 32 ?
		   6  (928,964) Data Transfer Position bit 8. Bit 8 of the Data Transfer
				Position register (3d4h index 3Bh)
		   7  (928,964) Bus-Grant Terminate Position bit 8. Bit 8 of the Bus Grant
				Termination register (3d4h index 5Fh).
	*/
	map(0x5d, 0x5d).lrw8(
		NAME([this](offs_t offset) {
			// Recompose CR5D from the extended CRTC fields
			u8 res = 0;
			res |= (vga.crtc.horz_total >> 8) & 0x01;           // bit 0
			res |= ((vga.crtc.horz_disp_end >> 7) & 0x02);      // bit 1
			res |= s3.cr5d & 0x04;                          // bit 2
			// bit 3: EHB+64 extension — stored in s3.cr5d
			res |= (s3.cr5d & 0x08);
			res |= s3.cr5d & 0x10;                          // bit 4
			// bit 5: EHS+32 extension — stored in s3.cr5d
			res |= (s3.cr5d & 0x20);
			// bits 6-7: DTP bit8, BGT bit8 — stored in s3.cr5d
			res |= (s3.cr5d & 0xC0);
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr5d = data;  // ES40: cache for readback of non-decomposed bits
			vga.crtc.horz_total = (vga.crtc.horz_total & 0xfeff) | ((data & 0x01) << 8);
			vga.crtc.horz_disp_end = (vga.crtc.horz_disp_end & 0xfeff) | ((data & 0x02) << 7);
			vga.crtc.horz_blank_start = (vga.crtc.horz_blank_start & 0xfeff) | ((data & 0x04) << 6);
			vga.crtc.horz_blank_end = (vga.crtc.horz_blank_end & 0xffbf) | ((data & 0x08) << 3);
			vga.crtc.horz_retrace_start = (vga.crtc.horz_retrace_start & 0xfeff) | ((data & 0x10) << 4);
			vga.crtc.horz_retrace_end = (vga.crtc.horz_retrace_end & 0xffdf) | (data & 0x20);
			s3_define_video_mode();
			// ES40 extension: recompute derived layout
			recompute_scanline_layout();
			})
	);

	/*
	CR5E: Extended Vertical Overflow Register             (80x +)
	bit    0  Vertical Total bit 10. Bit 10 of the Vertical Total register (3d4h
			  index 6). Bits 8 and 9 are in 3d4h index 7 bit 0 and 5.
		   1  Vertical Display End bit 10. Bit 10 of the Vertical Display End
			  register (3d4h index 12h). Bits 8 and 9 are in 3d4h index 7 bit 1
			  and 6
		   2  Start Vertical Blank bit 10. Bit 10 of the Vertical Start Blanking
			  register (3d4h index 15h). Bit 8 is in 3d4h index 7 bit 3 and bit 9
			  in 3d4h index 9 bit 5
		   4  Vertical Retrace Start bit 10. Bit 10 of the Vertical Start Retrace
			  register (3d4h index 10h). Bits 8 and 9 are in 3d4h index 7 bit 2
			  and 7.
		   6  Line Compare Position bit 10. Bit 10 of the Line Compare register
			  (3d4h index 18h). Bit 8 is in 3d4h index 7 bit 4 and bit 9 in 3d4h
			  index 9 bit 6.
	 */
	map(0x5e, 0x5e).lrw8(
		NAME([this](offs_t offset) {
			// Recompose CR5E from the extended CRTC fields
			u8 res = 0;
			res |= (vga.crtc.vert_total >> 10) & 0x01;           // bit 0
			res |= ((vga.crtc.vert_disp_end >> 9) & 0x02);       // bit 1
			res |= ((vga.crtc.vert_blank_start >> 8) & 0x04);    // bit 2
			// bit 3: reserved
			res |= ((vga.crtc.vert_retrace_start >> 6) & 0x10);  // bit 4
			// bit 5: reserved
			res |= ((vga.crtc.line_compare >> 4) & 0x40);        // bit 6
			// bit 7: reserved
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.vert_total = (vga.crtc.vert_total & 0xfbff) | ((data & 0x01) << 10);
			vga.crtc.vert_disp_end = (vga.crtc.vert_disp_end & 0xfbff) | ((data & 0x02) << 9);
			vga.crtc.vert_blank_start = (vga.crtc.vert_blank_start & 0xfbff) | ((data & 0x04) << 8);
			vga.crtc.vert_retrace_start = (vga.crtc.vert_retrace_start & 0xfbff) | ((data & 0x10) << 6);
			vga.crtc.line_compare = (vga.crtc.line_compare & 0xfbff) | ((data & 0x40) << 4);
			s3_define_video_mode();
			})
	);
	map(0x5f, 0x5f).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr5f;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr5f = data;
			})
	);
	// Extended Memory Control 3 Register (EXT-MCTL-3) (CR60) 
	map(0x60, 0x60).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr60;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr60 = data;
			})
	);
	// Extended Memory Control 4 Register (EXT-MCTL-4) (CR61)
	map(0x61, 0x61).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr61;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr61 = data;
			})
	);
	// undocumented?
	map(0x62, 0x62).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr62;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr62 = data;
			})
	);
	// External Sync Control 3 Register (EX-SYNC-3) (CR63) 
	map(0x63, 0x63).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr63;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr63 = data;
			})
	);
	// undocumented?
	map(0x64, 0x64).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr64;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr64 = data;
			})
	);
	// Extended Miscellaneous Control Register (EXT-MISC-CTL) (CR6S)
	map(0x65, 0x65).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr65;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr65 = data;
			})
	);
	// Extended Miscellaneous Control 1 Register (EXT-MISC-1) (CR66) - S3 BIOS writes 0 here - normal operation & PCI bus disconnect disabled
	map(0x66, 0x66).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr66;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr66 = data;
			if (data & 0x02) {
				accel_reset();
			}
			})
	);
	map(0x67, 0x67).lrw8(
		NAME([this](offs_t offset) {
			return s3.ext_misc_ctrl_2;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.ext_misc_ctrl_2 = data;
			s3_define_video_mode();
			})
	);
	map(0x68, 0x68).lrw8(
		NAME([this](offs_t offset) {  // Configuration register 3
			// no /CAS,/OE stretch time, 32-bit data bus size
			return (s3.strapping & 0x00ff0000) >> 16;
			}),
		NAME([this](offs_t offset, u8 data) {
			if (s3.reg_lock2 == 0xa5)
			{
				s3.strapping = (s3.strapping & 0xff00ffff) | (data << 16);
				LOG("CR68: Strapping data = %08x\n", s3.strapping);
			}
			})
	);
	map(0x69, 0x69).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.start_addr_latch >> 16;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.start_addr_latch &= ~0x1f0000;
			vga.crtc.start_addr_latch |= ((data & 0x1f) << 16);
			s3_define_video_mode();
			})
	);
	map(0x6a, 0x6a).lrw8(
		NAME([this](offs_t offset) {
			return vga.crtc.data[0x6a] & 0x3f;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.crtc.data[0x6a] = data;
			svga.bank_w = data & 0x3f;
			svga.bank_r = svga.bank_w;
			})
	);
	// Extended BIOS Flag 3 (CR6B), R/W BIOS scratchpad (DB014-B 17-14).
	// Trio64's BAR0 address aliases are CR59/CR5A; CR53[3] is reserved.
	map(0x6b, 0x6b).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr6b;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr6b = data;
			})
	);
	// Extended BIOS Flag 4 Register (EBIOS-FLG3) (CR6C) - Bios scratchpad
	map(0x6c, 0x6c).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr6c;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr6c = data;
			})
	);
	// undocumented
	map(0x6d, 0x6d).lrw8(
		NAME([this](offs_t offset) {
			return s3.cr6d;
			}),
		NAME([this](offs_t offset, u8 data) {
			s3.cr6d = data;
			})
	);
	// Configuration register 4 (Trio64V+)
	map(0x6f, 0x6f).lrw8(
		NAME([this](offs_t offset) {
			// LPB(?) mode, Serial port I/O at port 0xe8, Serial port I/O disabled (MMIO only), no WE delay
			return (s3.strapping & 0xff000000) >> 24;
			}),
		NAME([this](offs_t offset, u8 data) {
			if (s3.reg_lock2 == 0xa5)
			{
				s3.strapping = (s3.strapping & 0x00ffffff) | (data << 24);
				LOG("CR6F: Strapping data = %08x\n", s3.strapping);
			}
			})
	);
}

void CS3Trio64::sequencer_map(address_map& map)
{
	// TODO: legacy fallback trick
	map(0x00, 0xff).lr8(
		NAME([this](offs_t offset) {
			const u8 res = vga.sequencer.data[offset];
			if (!machine().side_effects_disabled())
				LOGREGS("Reading unmapped sequencer read register [%02x] -> %02x (SVGA?)\n", offset, res);
			return res;
			})
	);
	//  map(0x00, 0x00) Reset Register
	map(0x00, 0x00).lw8( // es40 deviation
		NAME([this](offs_t offset, u8 data) {
			if (seq_reset1() && ((data & 0x01) == 0))
			{
				vga.sequencer.data[3] = 0;
				vga.sequencer.char_sel.A = 0;
				vga.sequencer.char_sel.B = 0;
				vga.sequencer.char_sel.base[0] = 0x20000;
				vga.sequencer.char_sel.base[1] = 0x20000;
				m_output.display().lock();
				m_output.display().set_text_charmap(&vga.memory[0x20000]);
				m_output.display().unlock();
				state.vga_mem_updated = 1;
			}
			vga.sequencer.data[0] = data;
			})
	);
	//  map(0x01, 0x01) Clocking Mode Register
	// SR01: Clocking Mode Register
	map(0x01, 0x01).lw8( // es40 deviation
		NAME([this](offs_t offset, u8 data) {
			u8 newreg1 = data & 0x3f;
			if (m_crtc_map.read_byte(0x34) & 0x20) {
				newreg1 = (newreg1 & ~0x01) | (vga.sequencer.data[1] & 0x01);
			}
			vga.sequencer.data[1] = newreg1;
			})
	);

	map(0x02, 0x02).lw8(
		NAME([this](offs_t offset, u8 data) {
			vga.sequencer.map_mask = data & 0xf;
			})
	);
	// SR03: Character Map Select
	map(0x03, 0x03).lw8(
		NAME([this](offs_t offset, u8 data) {
			/* --2- 84-- character select A
			   ---2 --84 character select B */
			vga.sequencer.char_sel.A = (((data & 0xc) >> 2) << 1) | ((data & 0x20) >> 5);
			vga.sequencer.char_sel.B = (((data & 0x3) >> 0) << 1) | ((data & 0x10) >> 4);
			// optimization for screen update inner loop
			vga.sequencer.char_sel.base[0] = 0x20000 + (vga.sequencer.char_sel.B * 0x2000);
			vga.sequencer.char_sel.base[1] = 0x20000 + (vga.sequencer.char_sel.A * 0x2000);
			//if(data)
			//	popmessage("Char SEL checker (%02x %02x)\n",vga.sequencer.char_sel.A,vga.sequencer.char_sel.B);
			})
	);
	// Sequencer Memory Mode Register
//  map(0x04, 0x04)
	map(0x04, 0x04).lw8(
		NAME([this](offs_t offset, u8 data) {
			vga.sequencer.data[4] = data;
			})
	);

	// (undocumented) Sequencer Horizontal Character Counter Reset
	// Any write strobe to this register will lock the character generator until another write to other regs happens.
	//  map(0x07, 0x07)
	// TODO: SR8 (unlocks SRD)
	// SR08: PLL Unlock
	map(0x08, 0x08).lrw8(
		NAME([this](offs_t offset) { return vga.sequencer.data[0x08]; }),
		NAME([this](offs_t offset, u8 data) {
			vga.sequencer.data[0x08] = data;
			})
	);
	// SR09: Extended (reserved)
	map(0x09, 0x09).lrw8(
		NAME([this](offs_t offset) { return vga.sequencer.data[0x09]; }),
		NAME([this](offs_t offset, u8 data) {
			vga.sequencer.data[0x09] = data;
			})
	);
	// SR0A
	map(0x0a, 0x0a).lrw8(
		NAME([this](offs_t offset) { return vga.sequencer.data[0x0a]; }),
		NAME([this](offs_t offset, u8 data) {
			vga.sequencer.data[0x0a] = data;
			})
	);
	// SR0B
	map(0x0b, 0x0b).lrw8(
		NAME([this](offs_t offset) { return vga.sequencer.data[0x0b]; }),
		NAME([this](offs_t offset, u8 data) {
			vga.sequencer.data[0x0b] = data;
			})
	);
	// SR0D
	map(0x0d, 0x0d).lrw8(
		NAME([this](offs_t offset) { return vga.sequencer.data[0x0d]; }),
		NAME([this](offs_t offset, u8 data) {
			vga.sequencer.data[0x0d] = data;
			})
	);
	// Memory CLK PLL
	map(0x10, 0x10).lrw8(
		NAME([this](offs_t offset) { return s3.sr10; }),
		NAME([this](offs_t offset, u8 data) { s3.sr10 = data; })
	);
	map(0x11, 0x11).lrw8(
		NAME([this](offs_t offset) { return s3.sr11; }),
		NAME([this](offs_t offset, u8 data) { s3.sr11 = data; })
	);
	// Video CLK PLL
	map(0x12, 0x12).lrw8(
		NAME([this](offs_t offset) { return s3.sr12; }),
		NAME([this](offs_t offset, u8 data) { s3.sr12 = data; load_dclk(); })
	);
	map(0x13, 0x13).lrw8(
		NAME([this](offs_t offset) { return s3.sr13; }),
		NAME([this](offs_t offset, u8 data) { s3.sr13 = data; load_dclk(); })
	);
	// SR14: CLKSYN Control 1
	map(0x14, 0x14).lrw8(
		NAME([this](offs_t offset) { return vga.sequencer.data[0x14]; }),
		NAME([this](offs_t offset, u8 data) {
			vga.sequencer.data[0x14] = data;
			})
	);
	map(0x15, 0x15).lrw8(
		NAME([this](offs_t offset) { return s3.sr15; }),
		NAME([this](offs_t offset, u8 data) {
			s3.sr15 = data;
			load_dclk();
			})
	);
	map(0x17, 0x17).lr8(
		NAME([this](offs_t offset) {
			// CLKSYN test register
			const u8 res = s3.sr17;
			// who knows what it should return, docs only say it defaults to 0, and is reserved for testing of the clock synthesiser
			if (!machine().side_effects_disabled())
				s3.sr17--;
			return res;
			})
	);
	// SR18: RAMDAC/CLKSYN Control
	map(0x18, 0x18).lrw8(
		NAME([this](offs_t offset) { return s3.sr18; }),
		NAME([this](offs_t offset, u8 data) {
			s3.sr18 = data;
			vga.sequencer.data[0x18] = data;
			})
	);
}

uint8_t CS3Trio64::mem_r(uint32_t offset)
{
	// Standard MMIO (DB014-B 13.3.1): enhanced registers read at A8000h + their I/O address.
	// The packed 81xxh aliases are write-only and read back through AccelIORead's default.
	if ((s3.cr53 & 0x10) && offset >= 0x8000 && offset < 0x10000)
		return AccelIORead(offset);

	if (svga.rgb8_en || svga.rgb15_en || svga.rgb16_en || svga.rgb32_en)
	{
		int data;
		if (offset & 0x10000)
			return 0;
		data = 0;
		if (vga.sequencer.data[4] & 0x8)
		{
			// DB014-B 15-2/17-13: enhanced CPU access shares the bank controls.
			const u8 bank = uses_sized_linear_bar_window() ?
				((s3.memory_config & 0x01) ? cpu_bank() : 0) : svga.bank_r;
			if ((offset + (bank * 0x10000)) < vga.svga_intf.vram_size)
				data = vga.memory[(offset + (bank * 0x10000))];
		}
		else
		{
			for (int i = 0; i < 4; i++)
			{
				if (vga.sequencer.map_mask & 1 << i)
				{
					if ((offset * 4 + i + (svga.bank_r * 0x10000)) < vga.svga_intf.vram_size)
						data |= vga.memory[(offset * 4 + i + (svga.bank_r * 0x10000))];
				}
			}
		}
		return (uint8_t)data;
	}

	// Standard VGA fallback — full read-mode/latch pipeline via base class
	if ((offset + (svga.bank_r * 0x10000)) < vga.svga_intf.vram_size)
		return CVGA::mem_r(offset);
	return 0xff;
}

void CS3Trio64::mem_w(offs_t offset, uint8_t data)
{
	ibm8514a_device* dev = get_8514();
	// bit 4 of CR53 enables memory-mapped I/O
	// 0xA0000-0xA7fff maps to port 0xE2E8 (pixel transfer)
	if (s3.cr53 & 0x10)
	{
		if (offset < 0x8000) {
			// Lower half of the MMIO window is PIX_TRANS FIFO.
			// Feed it through the same bus-size aware path as port I/O.
			AccelIOWrite(0xE2E8 + (offset & 3), data);
			return;
		}

#ifdef DEBUG_VGA_MEMW
		printf("mem_w offset: 0x%05x data: 0x%02x\n", (unsigned)offset, (unsigned)data);
#endif

		switch (offset)
		{
		case 0x8100:
		case 0x82e8:
			dev->ibm8514.curr_y = (dev->ibm8514.curr_y & 0xff00) | data;
			dev->ibm8514.prev_y = (dev->ibm8514.prev_y & 0xff00) | data;
			break;
		case 0x8101:
		case 0x82e9:
			dev->ibm8514.curr_y = (dev->ibm8514.curr_y & 0x00ff) | (data << 8);
			dev->ibm8514.prev_y = (dev->ibm8514.prev_y & 0x00ff) | (data << 8);
			break;
		case 0x8102:
		case 0x86e8:
			dev->ibm8514.curr_x = (dev->ibm8514.curr_x & 0xff00) | data;
			dev->ibm8514.prev_x = (dev->ibm8514.prev_x & 0xff00) | data;
			break;
		case 0x8103:
		case 0x86e9:
			dev->ibm8514.curr_x = (dev->ibm8514.curr_x & 0x00ff) | (data << 8);
			dev->ibm8514.prev_x = (dev->ibm8514.prev_x & 0x00ff) | (data << 8);
			break;
		case 0x8108:
		case 0x8ae8:
			dev->ibm8514.line_axial_step = (dev->ibm8514.line_axial_step & 0xff00) | data;
			dev->ibm8514.dest_y = (dev->ibm8514.dest_y & 0xff00) | data;
			break;
		case 0x8109:
		case 0x8ae9:
			dev->ibm8514.line_axial_step = (dev->ibm8514.line_axial_step & 0x00ff) | ((data & 0x3f) << 8);
			dev->ibm8514.dest_y = (dev->ibm8514.dest_y & 0x00ff) | ((data & 0x0f) << 8);
			break;
		case 0x810a:
		case 0x8ee8:
			dev->ibm8514.line_diagonal_step = (dev->ibm8514.line_diagonal_step & 0xff00) | data;
			dev->ibm8514.dest_x = (dev->ibm8514.dest_x & 0xff00) | data;
			break;
		case 0x810b:
		case 0x8ee9:
			dev->ibm8514.line_diagonal_step = (dev->ibm8514.line_diagonal_step & 0x00ff) | ((data & 0x3f) << 8);
			dev->ibm8514.dest_x = (dev->ibm8514.dest_x & 0x00ff) | ((data & 0x0f) << 8);
			break;
		case 0x8118:
		case 0x9ae8:
			s3.mmio_9ae8 = (s3.mmio_9ae8 & 0xff00) | data;
			break;
		case 0x8119:
		case 0x9ae9:
			s3.mmio_9ae8 = (s3.mmio_9ae8 & 0x00ff) | (data << 8);
			dev->ibm8514_cmd_w(s3.mmio_9ae8);
			break;
		case 0x8120:
		case 0xa2e8:
			dev->ibm8514.bgcolour = (dev->ibm8514.bgcolour & 0xffffff00) | data;
			break;
		case 0x8121:
		case 0xa2e9:
			dev->ibm8514.bgcolour = (dev->ibm8514.bgcolour & 0xffff00ff) | (data << 8);
			break;
		case 0x8122:
		case 0xa2ea:
			dev->ibm8514.bgcolour = (dev->ibm8514.bgcolour & 0xff00ffff) | (data << 16);
			break;
		case 0x8123:
		case 0xa2eb:
			dev->ibm8514.bgcolour = (dev->ibm8514.bgcolour & 0x00ffffff) | (data << 24);
			break;
		case 0x8124:
		case 0xa6e8:
			dev->ibm8514.fgcolour = (dev->ibm8514.fgcolour & 0xffffff00) | data;
			break;
		case 0x8125:
		case 0xa6e9:
			dev->ibm8514.fgcolour = (dev->ibm8514.fgcolour & 0xffff00ff) | (data << 8);
			break;
		case 0x8126:
		case 0xa6ea:
			dev->ibm8514.fgcolour = (dev->ibm8514.fgcolour & 0xff00ffff) | (data << 16);
			break;
		case 0x8127:
		case 0xa6eb:
			dev->ibm8514.fgcolour = (dev->ibm8514.fgcolour & 0x00ffffff) | (data << 24);
			break;
		case 0x8128:
		case 0xaae8:
			dev->ibm8514.write_mask = (dev->ibm8514.write_mask & 0xffffff00) | data;
			break;
		case 0x8129:
		case 0xaae9:
			dev->ibm8514.write_mask = (dev->ibm8514.write_mask & 0xffff00ff) | (data << 8);
			break;
		case 0x812a:
		case 0xaaea:
			dev->ibm8514.write_mask = (dev->ibm8514.write_mask & 0xff00ffff) | (data << 16);
			break;
		case 0x812b:
		case 0xaaeb:
			dev->ibm8514.write_mask = (dev->ibm8514.write_mask & 0x00ffffff) | (data << 24);
			break;
		case 0x812c:
		case 0xaee8:
			dev->ibm8514.read_mask = (dev->ibm8514.read_mask & 0xffffff00) | data;
			break;
		case 0x812d:
		case 0xaee9:
			dev->ibm8514.read_mask = (dev->ibm8514.read_mask & 0xffff00ff) | (data << 8);
			break;
		case 0x812e:
		case 0xaeea:
			dev->ibm8514.read_mask = (dev->ibm8514.read_mask & 0xff00ffff) | (data << 16);
			break;
		case 0x812f:
		case 0xaeeb:
			dev->ibm8514.read_mask = (dev->ibm8514.read_mask & 0x00ffffff) | (data << 24);
			break;
		case 0x8130:
		case 0xb2e8:
			dev->ibm8514.color_cmp = (dev->ibm8514.color_cmp & 0xffffff00) | data;
			break;
		case 0x8131:
		case 0xb2e9:
			dev->ibm8514.color_cmp = (dev->ibm8514.color_cmp & 0xffff00ff) | (data << 8);
			break;
		case 0x8132:
		case 0xb2ea:
			dev->ibm8514.color_cmp = (dev->ibm8514.color_cmp & 0xff00ffff) | (data << 16);
			break;
		case 0x8133:
		case 0xb2eb:
			dev->ibm8514.color_cmp = (dev->ibm8514.color_cmp & 0x00ffffff) | (data << 24);
			break;
		case 0xb6e8:
		case 0x8134:
			dev->ibm8514.bgmix = (dev->ibm8514.bgmix & 0xff00) | data;
			break;
		case 0x8135:
		case 0xb6e9:
			dev->ibm8514.bgmix = (dev->ibm8514.bgmix & 0x00ff) | (data << 8);
			dev->ibm8514.bkgd_sel = (dev->ibm8514.bgmix >> 5) & 3;
			dev->ibm8514.bkgd_mix_mode = dev->ibm8514.bgmix & 0x0f;
			break;
		case 0x8136:
		case 0xbae8:
			dev->ibm8514.fgmix = (dev->ibm8514.fgmix & 0xff00) | data;
			break;
		case 0x8137:
		case 0xbae9:
			dev->ibm8514.fgmix = (dev->ibm8514.fgmix & 0x00ff) | (data << 8);
			dev->ibm8514.frgd_sel = (dev->ibm8514.fgmix >> 5) & 3;
			dev->ibm8514.frgd_mix_mode = dev->ibm8514.fgmix & 0x0f;
			break;
		case 0x8138:
			dev->ibm8514.scissors_top = (dev->ibm8514.scissors_top & 0xff00) | data;
			break;
		case 0x8139:
			dev->ibm8514.scissors_top = (dev->ibm8514.scissors_top & 0x00ff) | (data << 8);
			break;
		case 0x813a:
			dev->ibm8514.scissors_left = (dev->ibm8514.scissors_left & 0xff00) | data;
			break;
		case 0x813b:
			dev->ibm8514.scissors_left = (dev->ibm8514.scissors_left & 0x00ff) | (data << 8);
			break;
		case 0x813c:
			dev->ibm8514.scissors_bottom = (dev->ibm8514.scissors_bottom & 0xff00) | data;
			break;
		case 0x813d:
			dev->ibm8514.scissors_bottom = (dev->ibm8514.scissors_bottom & 0x00ff) | (data << 8);
			break;
		case 0x813e:
			dev->ibm8514.scissors_right = (dev->ibm8514.scissors_right & 0xff00) | data;
			break;
		case 0x813f:
			dev->ibm8514.scissors_right = (dev->ibm8514.scissors_right & 0x00ff) | (data << 8);
			break;
		case 0x8140:
			dev->ibm8514.pixel_control = (dev->ibm8514.pixel_control & 0xff00) | data;
			break;
		case 0x8141:
			dev->ibm8514.pixel_control = (dev->ibm8514.pixel_control & 0x00ff) | (data << 8);
			break;
		case 0x8146:
			dev->ibm8514.multifunc_sel = (dev->ibm8514.multifunc_sel & 0xff00) | data;
			break;
		case 0x8148:
			dev->ibm8514.rect_height = (dev->ibm8514.rect_height & 0xff00) | data;
			break;
		case 0x8149:
			dev->ibm8514.rect_height = (dev->ibm8514.rect_height & 0x00ff) | (data << 8);
			break;
		case 0x814a:
			dev->ibm8514.rect_width = (dev->ibm8514.rect_width & 0xff00) | data;
			break;
		case 0x814b:
			dev->ibm8514.rect_width = (dev->ibm8514.rect_width & 0x00ff) | (data << 8);
			break;
		case 0x8150: case 0x8151: case 0x8152: case 0x8153:
			// MMIO mirror of PIX_TRANS inside the upper half window.
			AccelIOWrite(0xE2E8 + (offset - 0x8150), data);
			break;
		case 0xbee8:
			s3.mmio_bee8 = (s3.mmio_bee8 & 0xff00) | data;
			break;
		case 0xbee9:
			s3.mmio_bee8 = (s3.mmio_bee8 & 0x00ff) | (data << 8);
			dev->ibm8514_multifunc_w(s3.mmio_bee8);
			break;
		case 0x96e8:
			s3.mmio_96e8 = (s3.mmio_96e8 & 0xff00) | data;
			break;
		case 0x96e9:
			s3.mmio_96e8 = (s3.mmio_96e8 & 0x00ff) | (data << 8);
			dev->ibm8514_width_w(s3.mmio_96e8);
			break;
		case 0x92e8:
			s3.mmio_92e8 = (s3.mmio_92e8 & 0xff00) | data;
			break;
		case 0x92e9:
			s3.mmio_92e8 = (s3.mmio_92e8 & 0x00ff) | (data << 8);
			dev->ibm8514_line_error_w(s3.mmio_92e8);
			break;
		case 0xe2e8: case 0xe2e9: case 0xe2ea: case 0xe2eb:
			// Normal PIX_TRANS MMIO addresses used by the VMS X11 driver
			AccelIOWrite(offset, data);
			break;
		default:
			LOG("S3: MMIO offset %05x write %02x\n", offset + 0xa0000, data);
			break;
		}
		return;
	}

	if (svga.rgb8_en || svga.rgb15_en || svga.rgb16_en || svga.rgb32_en)
	{
		//printf("%08x %02x (%02x %02x) %02X\n",offset,data,vga.sequencer.map_mask,svga.bank_w,(vga.sequencer.data[4] & 0x08));
		if (offset & 0x10000)
			return;
		if (vga.sequencer.data[4] & 0x8)
		{
			const u8 bank = uses_sized_linear_bar_window() ?
				((s3.memory_config & 0x01) ? cpu_bank() : 0) : svga.bank_w;
			if ((offset + (bank * 0x10000)) < vga.svga_intf.vram_size)
				vga.memory[(offset + (bank * 0x10000))] = data;
		}
		else
		{
			int i;
			for (i = 0; i < 4; i++)
			{
				if (vga.sequencer.map_mask & 1 << i)
				{
					if ((offset * 4 + i + (svga.bank_w * 0x10000)) < vga.svga_intf.vram_size)
						vga.memory[(offset * 4 + i + (svga.bank_w * 0x10000))] = data;
				}
			}
		}
		return;
	}

	if ((offset + (svga.bank_w * 0x10000)) < vga.svga_intf.vram_size)
		CVGA::mem_w(offset, data);
}

uint32_t CS3Trio64::screen_update(bitmap_rgb32& bitmap, const rectangle& cliprect)
{
	CVGA::screen_update(bitmap, cliprect);

	uint8_t cur_mode = pc_vga_choosevideomode();

#ifdef DEBUG_VGA_RENDER
	static uint8_t last_mode = 0xff;
	if (cur_mode != last_mode) {
		LOG("S3: screen mode changed to %d (rgb8=%d sync=%d graphic=%d)\n",
			cur_mode, svga.rgb8_en, vga.crtc.sync_en, vga.gc.alpha_dis);
		last_mode = cur_mode;
	}

	// log screen configuration  
	static bool screen_diag = false;
	if (cur_mode == 6 && !screen_diag) {  // 6 = RGB8_MODE
		const rectangle& vis = cliprect;
		LOG("DIAG: screen visible_area=(%d,%d)-(%d,%d) bitmap=%dx%d\n",
			vis.min_x, vis.min_y, vis.max_x, vis.max_y,
			bitmap.width(), bitmap.height());
		LOG("DIAG: CRTC offset=%03x seq4=%02x chain4=%d height=%d LINES=%d VGA_COLUMNS=%d\n",
			vga.crtc.offset, vga.sequencer.data[4],
			(vga.sequencer.data[4] & 0x08) ? 1 : 0,
			vga.crtc.maximum_scan_line * (vga.crtc.scan_doubling + 1),
			(vga.crtc.vert_disp_end + 1), (vga.crtc.horz_disp_end + 1));
		screen_diag = true;

		static int dac_frame_count = 0;
		if (cur_mode == 6 && (dac_frame_count % 60 == 0) && dac_frame_count < 300) {
			LOG("DIAG[frame %d]: dac.dirty=%d DAC[0]=%02x,%02x,%02x DAC[2]=%02x,%02x,%02x\n",
				dac_frame_count, vga.dac.dirty,
				vga.dac.color[0], vga.dac.color[1], vga.dac.color[2],
				vga.dac.color[6], vga.dac.color[7], vga.dac.color[8]);
		}
		dac_frame_count++;
	}


	printf("PALETTE: dirty=%d DAC[0]=%02x,%02x,%02x DAC[2]=%02x,%02x,%02x DAC[130]=%02x,%02x,%02x\n",
		vga.dac.dirty,
		vga.dac.color[0], vga.dac.color[1], vga.dac.color[2],
		vga.dac.color[6], vga.dac.color[7], vga.dac.color[8],
		vga.dac.color[390], vga.dac.color[391], vga.dac.color[392]);


#endif

	// draw hardware graphics cursor
	// TODO: support 16 bit and greater video modes
	// TODO: should be a derived function from svga_device
	if (s3.cursor_mode & 0x01)  // if cursor is enabled
	{
		uint16_t cx = s3.cursor_x & 0x07ff;
		uint16_t cy = s3.cursor_y & 0x07ff;

		if (cur_mode == SCREEN_OFF || cur_mode == TEXT_MODE || cur_mode == MONO_MODE || cur_mode == CGA_MODE || cur_mode == EGA_MODE)
			return 0;  // cursor only works in VGA or SVGA modes

		uint32_t src = s3.cursor_start_addr * 1024;  // start address is in units of 1024 bytes

		uint32_t bg_col;
		uint32_t fg_col;
		int r, g, b;
		uint32_t datax;
		switch (cur_mode)
		{
		case RGB15_MODE:
		case RGB16_MODE:
			datax = s3.cursor_bg[0] | s3.cursor_bg[1] << 8;
			r = (datax & 0xf800) >> 11;
			g = (datax & 0x07e0) >> 5;
			b = (datax & 0x001f) >> 0;
			r = (r << 3) | (r & 0x7);
			g = (g << 2) | (g & 0x3);
			b = (b << 3) | (b & 0x7);
			bg_col = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);

			datax = s3.cursor_fg[0] | s3.cursor_fg[1] << 8;
			r = (datax & 0xf800) >> 11;
			g = (datax & 0x07e0) >> 5;
			b = (datax & 0x001f) >> 0;
			r = (r << 3) | (r & 0x7);
			g = (g << 2) | (g & 0x3);
			b = (b << 3) | (b & 0x7);
			fg_col = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);
			break;
		case RGB24_MODE:
		case RGB32_MODE:
			datax = s3.cursor_bg[0] | s3.cursor_bg[1] << 8 | s3.cursor_bg[2] << 16;
			r = (datax & 0xff0000) >> 16;
			g = (datax & 0x00ff00) >> 8;
			b = (datax & 0x0000ff) >> 0;
			bg_col = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);

			datax = s3.cursor_fg[0] | s3.cursor_fg[1] << 8 | s3.cursor_fg[2] << 16;
			r = (datax & 0xff0000) >> 16;
			g = (datax & 0x00ff00) >> 8;
			b = (datax & 0x0000ff) >> 0;
			fg_col = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);
			break;
		case RGB8_MODE:
		default:
			bg_col = pen(s3.cursor_bg[0]);
			fg_col = pen(s3.cursor_fg[0]);
			break;
		}

		//popmessage("%08x %08x",(s3.cursor_bg[0])|(s3.cursor_bg[1]<<8)|(s3.cursor_bg[2]<<16)|(s3.cursor_bg[3]<<24)
		//                    ,(s3.cursor_fg[0])|(s3.cursor_fg[1]<<8)|(s3.cursor_fg[2]<<16)|(s3.cursor_fg[3]<<24));
//      for(x=0;x<64;x++)
//          printf("%08x: %02x %02x %02x %02x\n",src+x*4,vga.memory[src+x*4],vga.memory[src+x*4+1],vga.memory[src+x*4+2],vga.memory[src+x*4+3]);
		for (int y = 0; y < 64; y++)
		{
			if (cy + y < cliprect.max_y && cx < cliprect.max_x)
			{
				uint32_t* const dst = &bitmap.pix(cy + y, cx);
				for (int x = 0; x < 64; x++)
				{
					uint16_t bita = (vga.memory[(src + 1) % vga.svga_intf.vram_size]
						| ((vga.memory[(src + 0) % vga.svga_intf.vram_size]) << 8))
						>> (15 - (x % 16));
					uint16_t bitb = (vga.memory[(src + 3) % vga.svga_intf.vram_size]
						| ((vga.memory[(src + 2) % vga.svga_intf.vram_size]) << 8))
						>> (15 - (x % 16));
					uint8_t val = ((bita & 0x01) << 1) | (bitb & 0x01);

					if (s3.extended_dac_ctrl & 0x10)
					{  // X11 mode
						switch (val)
						{
						case 0x00: break;                // no change
						case 0x01: break;                // no change
						case 0x02: dst[x] = bg_col; break;
						case 0x03: dst[x] = fg_col; break;
						}
					}
					else
					{  // Windows mode
						switch (val)
						{
						case 0x00: dst[x] = bg_col; break;
						case 0x01: dst[x] = fg_col; break;
						case 0x02: break;                // screen data
						case 0x03: dst[x] = ~(dst[x]); break; // inverted
						}
					}
					if (x % 16 == 15)
						src += 4;
				}
			}
		}
	}
	return 0;
}

// vision 964 stuff

// vision 968 stuff

// s3trio64_vga_device::s3trio64_vga_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock)
//	: s3trio64_vga_device(mconfig, S3_TRIO64_VGA, tag, owner, clock)

// s3trio64_vga_device::s3trio64_vga_device(const machine_config& mconfig, device_type type, const char* tag, device_t* owner, uint32_t clock)
//	: s3vision968_vga_device(mconfig, type, tag, owner, clock)

// void s3trio64_vga_device::device_start()

// END MAME pc_vga_s3.cpp

// ES40 specific functions

// Returns (A<<1)|B for pixel (sx,sy) from a 6464 cursor map.
// Supports:
//   Standard layout (16 bytes/row): A[15:0] then B[15:0] per 16 px (all non-16bpp modes)
//   64k layout (16bpp): A byte then B byte per 8 px, each bit horizontally doubled. 
//   Right-storage addressing when CR45 bit4 is set - via MAME 
static inline u8 s3_cursor_ab(const u8* vram, u32 vram_mask, u32 src_base, unsigned sx, unsigned sy, bool is16bpp, bool right_storage)
{
	// Compute row base with optional "right storage" skew.
	auto row_base_16bytes = [&](unsigned row) -> u32 {
		if (!right_storage) {
			return src_base + row * 16;                    // normal: 16 bytes/row
		}
		// right storage (non-16bpp path): last 256B of each 1KiB line, 4 lines  1KiB
		const unsigned lane = (row >> 4) & 0x3;            // 0..3
		const unsigned r = row & 0x0F;                  // 0..15 within lane
		return src_base + lane * 1024u + (1024u - 256u) + r * 16u;
		};

	if (!is16bpp) {
		// Standard 16B/row path: A[15:0] then B[15:0] per 16 pixels
		const unsigned group = sx >> 4;                    // 0..3
		const unsigned bit = 15 - (sx & 15);
		const u32 rb = row_base_16bytes(sy);
		const u32 src = rb + group * 4u;
		const u16 A = (u16)vram[(src + 1) & vram_mask] |
			((u16)vram[(src + 0) & vram_mask] << 8);
		const u16 B = (u16)vram[(src + 3) & vram_mask] |
			((u16)vram[(src + 2) & vram_mask] << 8);
		return (u8)(((A >> bit) & 1) << 1 | ((B >> bit) & 1));
	}
	else {
		// 64k (16bpp) path: one A byte + one B byte per 8 pixels, bits doubled horizontally. 
		// We still keep 16 bytes per row total: 8 groups  2 bytes.
		// Right storage variant uses last 512B of 22KiB lines.
		u32 rb;
		if (!right_storage) {
			rb = src_base + sy * 16u;
		}
		else {
			const unsigned lane = (sy >> 5) & 0x1;         // 0..1
			const unsigned r = sy & 0x1F;               // 0..31
			rb = src_base + lane * 2048u + (2048u - 512u) + r * 16u;
		}
		const unsigned group8 = sx >> 3;                   // 0..7 (8px groups)
		const u32 src = rb + group8 * 2u;                  // A byte, then B byte
		const u8 Abits = vram[(src + 0) & vram_mask];
		const u8 Bbits = vram[(src + 1) & vram_mask];
		const unsigned bit = 7 - ((sx & 7) >> 1);          // horizontal bit-doubling
		const u8 A = (Abits >> bit) & 1;
		const u8 B = (Bbits >> bit) & 1;
		return (u8)((A << 1) | B);
	}
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
void CS3Trio64::run()
{
	try
	{
		// The GUI outlives this worker. Serial-menu save/restore stops and joins the worker. 
		// Next run must retain the existing window.
		if (!m_gui_initialized)
		{
			// initialize the GUI (and let it know our tilesize)
			m_output.display().init(state.x_tilesize, state.y_tilesize);
			m_gui_initialized = true;
		}
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
		printf("Exception in S3 thread: %s.\n", e.displayText().c_str());

		// Let the thread die...
	}
}

/** PCI Configuration Space data block */
static u32                 s3_cfg_data[64] = {
	/*00*/ 0x88115333,            // CFID: vendor + device
	/*04*/ 0x02000000,            // COMMAND=0; STATUS: medium DEVSEL (DB014-B 19-3)
	/*08*/ 0x03000002,            // CFRV: class + revision
	/*0c*/ 0x00000000,            // CFLT: latency timer + cache line size
	/*10*/ 0x00000000,            // BAR0: FB
	/*14*/ 0x00000000,            // BAR1:
	/*18*/ 0x00000000,            // BAR2:
	/*1c*/ 0x00000000,            // BAR3:
	/*20*/ 0x00000000,            // BAR4:
	/*24*/ 0x00000000,            // BAR5:
	/*28*/ 0x00000000,            // CCIC: CardBus
	/*2c*/ 0x00000000,            // CSID: subsystem + vendor
	/*30*/ 0x000c0000,            // BIOS ROM base; ADE clear (DB014-B 19-5)
	/*34*/ 0x00000000,            // CCAP: capabilities pointer
	/*38*/ 0x00000000,
	/*3c*/ 0x281401ff,            // CFIT: interrupt configuration
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/** PCI Configuration Space mask block. Restore retains the saved mask;
 *  initialization and PCI reset use these hardware defaults. */
static u32                 s3_cfg_mask[64] = {
	/*00*/ 0x00000000,            // CFID: vendor + device
	/*04*/ 0x00000023,            // COMMAND: I/O, memory, DAC snoop only; STATUS fixed (19-2/3)
	/*08*/ 0x00000000,            // CFRV: class + revision
	/*0c*/ 0x0000ffff,            // CFLT: latency timer + cache line size
	/*10*/ 0xff800000,            // BAR0: bits 31:23, 8 MiB allocation (DB014-B 19-4)
	/*14*/ 0x00000000,            // BAR1:
	/*18*/ 0x00000000,            // BAR2:
	/*1c*/ 0x00000000,            // BAR3:
	/*20*/ 0x00000000,            // BAR4:
	/*24*/ 0x00000000,            // BAR5:
	/*28*/ 0x00000000,            // CCIC: CardBus
	/*2c*/ 0x00000000,            // CSID: subsystem + vendor
	/*30*/ 0xffff0001,            // BIOS ROM: 64 KiB aperture and ADE (19-5)
	/*34*/ 0x00000000,            // CCAP: capabilities pointer
	/*38*/ 0x00000000,
	/*3c*/ 0x000000ff,            // CFIT: interrupt configuration
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/**
 * Constructor.
 *
 * Bind the card's sole display output; device initialization is done by init().
 **/
CS3Trio64::CS3Trio64(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev,
	bx_gui_c& display) : CVGA(cfg, c, pcibus, pcidev), m_output(0, display)
{
	// PCI setup consults linear-enable state before the accelerator starts.
	m_8514.ibm8514.advfunction_ctrl = 0;
	// The first PCI reset precedes init()'s full graphics-state initialization.
	s3.cr59 = 0;
	s3.cr5a = 0;
}

// --- S3 CR36 -----------------------------------------------------------------
// CR36 (Reset State Read 1) encodes DRAM type in the low and the actual VRAM 
// size in the high. 
//   EDO: <1M=0xFE, 1M=0xDE, 2M=0x9E, 3M=0x5E, 4M=0x1E, 8M=0x7E
// Trio64 max display memory is 4 MiB
// see DOSBox-X Reset State Read 1
// `use_edo` to false for FPM instead of EDO memory
static inline uint8_t s3_cr36_from_memsize(uint32_t bytes, bool use_edo /*=true*/) {
	const uint8_t type_nibble = use_edo ? 0x0E : 0x0A; // low nibble
	uint8_t size_nibble_high;
	if (bytes < (1u * 1024 * 1024))       size_nibble_high = 0xF0; // <1MB
	else if (bytes < (2u * 1024 * 1024))  size_nibble_high = 0xD0; // 1MB
	else if (bytes < (3u * 1024 * 1024))  size_nibble_high = 0x90; // 2MB
	else if (bytes < (4u * 1024 * 1024))  size_nibble_high = 0x50; // 3MB
	else                                  size_nibble_high = 0x10; // 4MB (or more -> clamp)
	return uint8_t(size_nibble_high | type_nibble);
}

// CR32 (Backward Compatibility 1 - BKWD_1).
// Writing 01xx10xx (e.g. 0x48) unlocks S3 VGA regs.
static inline bool s3_cr32_is_unlock(uint8_t v) {
	return ((v & 0xC0) == 0x40) && ((v & 0x0C) == 0x08);
}

inline uint32_t CS3Trio64::s3_lfb_base_from_regs() {
	// CR59 high, CR5A low, reported as (la_window << 16)
	const uint16_t la_window = (uint16_t(m_crtc_map.read_byte(0x59)) << 8) | uint16_t(m_crtc_map.read_byte(0x5A));
	return uint32_t(la_window) << 16;
}

inline uint8_t CS3Trio64::current_char_width_px() const {
	// If special blanking (CR33 bit5) is set, S3 forces 8-dot chars.
	if (m_crtc_map.read_byte(0x33) & 0x20) return 8;
	// Otherwise, use Sequencer reg1 bit0 like the rest of our text logic.
	return (vga.sequencer.data[1] & 0x01) ? 8 : 9;
}

// MMIO alias base offset helper (CR53 bit5: 0=A0000, 1=B8000).
inline uint32_t CS3Trio64::s3_mmio_base_off(SS3_state& s) {
	// Trio64 CR53 bit5 (A0000/B8000 MMIO base select):
	//  - 0: A0000...AFFFF is MMIO alias space when CR53 bit4=0
	//  - 1: B8000..BFFFF is MMIO alias space when CR53 bit4=1
	return (s3.cr53 & 0x20) ? 0x18000u : 0u;
}

// Trio64: "new" MMIO 64 KiB window at LFB + 0x0100_0000 when CR53 bit3 = 1.
// We'll at minimum wire the lower 0x8000 to PIXTRANS FIFO (0xE2E8 port semantics).
inline bool CS3Trio64::s3_new_mmio_enabled()
{
	// CR53 - see 86Box behavior gating several "new-mmio" aliases. 
	return (s3.cr53 & 0x08) != 0;
}

static inline uint32_t s3_lfb_size_from_cr58(uint8_t cr58) {
	switch (cr58 & 0x03) {
	case 0: return 64 * 1024;
	case 1: return 1u * 1024 * 1024;
	case 2: return 2u * 1024 * 1024;
	default: return 4u * 1024 * 1024;
	}
}

static inline bool s3_lfb_enabled(uint8_t cr58, uint16_t advfunc) {
	// Trio32/Trio64 DB014-B 18-3: ADVFUNC.LA is ORed with CR58.ENB LA.
	return ((cr58 | advfunc) & 0x10) != 0;
}

bool CS3Trio64::uses_sized_linear_bar_window() const noexcept
{
	// DB014-B 13-1: enhanced mapping/functions precede linear addressing.
	const u16 advfunc = m_8514.ibm8514.advfunction_ctrl;
	return device_at[0] && (s3.memory_config & 0x08) && (advfunc & 0x01) &&
		!(s3.cr53 & 0x18) && !(advfunc & 0x20);
}

u8 CS3Trio64::cpu_bank() const noexcept
{
	// DB014-B 17-13: nonzero CR6A overrides the older CR35/CR51 bank.
	const u8 extended = vga.crtc.data[0x6a] & 0x3f;
	return extended ? extended :
		(s3.crt_reg_lock & 0x0f) | ((s3.cr51 & 0x0c) << 2);
}

bool CS3Trio64::linear_bar_offset(u64 address, u32& offset) const noexcept
{
	if (!(vga.miscellaneous_output & 0x02) ||
		!s3_lfb_enabled(s3.cr58, m_8514.ibm8514.advfunction_ctrl))
		return false;

	// DB014-B 17-7/19-4: CR59:5A selects a size-aligned window inside BAR0.
	const u32 size = s3_lfb_size_from_cr58(s3.cr58);
	const u32 mask = endian_32(pci_state.config_mask[0][4]) & 0xfffffff0U;
	const u32 position = (u32(s3.cr59) << 24) | (u32(s3.cr5a) << 16);
	const u32 start = (position & ~mask) & ~(size - 1U);
	if (address < start || address - start >= size)
		return false;
	offset = u32(address - start);
	// DB014-B 15-2/17-13: CR31.CPUA BASE enables the 64 KiB page offset.
	if (!(s3.cr58 & 0x03) && (s3.memory_config & 0x01))
		offset += u32(cpu_bank()) << 16;
	return true;
}

void CS3Trio64::update_linear_mapping()
{
	// BAR-only mode: no per-device mapping. PCI core decodes BAR0 and gates
	// access via COMMAND.MSE. lfb_active gates the framebuffer handlers.
	lfb_active = s3_lfb_enabled(m_crtc_map.read_byte(0x58), m_8514.ibm8514.advfunction_ctrl);
	lfb_size = s3_lfb_size_from_cr58(m_crtc_map.read_byte(0x58));
	lfb_base = s3_lfb_base_from_regs();
#ifdef S3_LFB_TRACE
	printf("LFB (BAR-only): CR58=%02x base=%08x size=%x active=%d\n",
		m_crtc_map.read_byte(0x58), lfb_base, lfb_size, lfb_active);
#endif
}

void CS3Trio64::update_pci_bar0_from_crtc(u32 value, u32 register_bits)
{
	if (!device_at[0])
		return;
	// DB014-B 17-7/19-4: reflect only this register's implemented shared bits.
	const u32 mask = endian_32(pci_state.config_mask[0][4]);
	const u32 shared = register_bits & mask & 0xff800000U;
	if (!shared)
		return;
	const u32 bar0 = (endian_32(pci_state.config_data[0][4]) & ~shared) | (value & shared);
	pci_state.config_data[0][4] = endian_32(bar0);
	// This is an address assignment, including a same-value write after a PCI
	// sizing probe. Avoid PCI callbacks that would rewrite the other CR alias.
	register_bar(0, 0, bar0, mask);
}

void CS3Trio64::on_crtc_linear_regs_changed(const char* reason)
{
	const u8 cr58 = m_crtc_map.read_byte(0x58);
	const u8 cr59 = m_crtc_map.read_byte(0x59);
	const u8 cr5a = m_crtc_map.read_byte(0x5A);

	// Either CR58.ENB LA or ADVFUNC.LA enables linear addressing.
	lfb_active = s3_lfb_enabled(cr58, m_8514.ibm8514.advfunction_ctrl);

	// Trio64 size: CR58[1:0] 00=64K, 01=1M, 10=2M, 11=4M
	lfb_size = s3_lfb_size_from_cr58(cr58);

	// Base: CR59:CR5A form LA_WINDOW; reported/programmed as (la_window << 16)
	const u32 la_window = (u32(cr59) << 8) | u32(cr5a);
	lfb_base = la_window << 16;

	// Apply/unapply the mapping now that CR regs changed
	lfb_recalc_and_map();
	trace_lfb_if_changed(reason);
}

/**
 * Initialize the S3 device.
 **/
void CS3Trio64::init()
{
	// Register PCI device
	add_function(0, s3_cfg_data, s3_cfg_mask);

	// Make the 21272/21274 PCI config window visible for this device now.
	// NetBSD reads PCI_ID_REG at 0/1/0 during console bring-up; without this
	// mapping it sees ~0/0 and panics with "no device at 255/255/0".
	ResetPCI();


	// Initialize all state variables to 0
	memset((void*)&state, 0, sizeof(state));

	// initialize MAME S3 and VGA fields and values to 0
	memset(&s3, 0, sizeof(s3));
	memset(&vga, 0, sizeof(vga));
	memset(&svga, 0, sizeof(svga));
	memset(&timing, 0, sizeof(timing));
	m_scan = {};
	m_scan_initialized = false;
	m_scan_paused = false;
	memset(vga.dac.color, 0, sizeof(vga.dac.color));
	memset(vga.dac.loading, 0, sizeof(vga.dac.loading));

	// Register VGA I/O ports at 3b4, 3b5, 3ba, 3c0..cf, 3d4, 3d5, 3da
	add_legacy_io(1, 0x3b4, 2);
	add_legacy_io(3, 0x3ba, 2);
	add_legacy_io(2, 0x3c0, 16);
	add_legacy_io(8, 0x3d4, 2);
	add_legacy_io(9, 0x3da, 1);
	add_legacy_io(32, 0x102, 1);

	// Register CRTC address-map handlers.  Must be called before any
	// m_crtc_map.write_byte() so that writes dispatch through handlers
	// and update decomposed vga.crtc.* / s3.* fields + trigger recomputes.
	init_maps();

	vga.attribute.state = atc_flip_flop() ? 1 : 0;
	vga.attribute.index = vga.attribute.index & 0x1f;

	vga.gc.bit_mask = 0xFF;

	vga.sequencer.char_sel.base[0] = 0x20000;  // font B (attr bit3=0)
	vga.sequencer.char_sel.base[1] = 0x20000;  // font A (attr bit3=1)

	// 8514/A-style S3 accel ports (byte-wide) - always register;
	// runtime gating is done via CR40 (state.accel.enabled).
	add_legacy_io(10, 0x42E8, 2); // SUBSYS_CNTL/STAT
	add_legacy_io(11, 0x4AE8, 2); // ADVFUNC_CNTL
	add_legacy_io(12, 0x46E8, 2); // MODE_SETUP / video subsystem enable
	add_legacy_io(13, 0x4EE8, 2); // legacy compatibility stub
	add_legacy_io(14, 0x86E8, 2); // CUR_X
	add_legacy_io(15, 0x8EE8, 2); // DESTX_DIASTP
	add_legacy_io(16, 0x96E8, 2); // MAJ_AXIS_PCNT
	add_legacy_io(17, 0x9AE8, 2); // CMD
	add_legacy_io(18, 0xA2E8, 4); // BKGD_COLOR
	add_legacy_io(19, 0xA6E8, 4); // FRGD_COLOR
	add_legacy_io(20, 0xAAE8, 4); // WRT_MASK
	add_legacy_io(21, 0xAEE8, 4); // RD_MASK
	add_legacy_io(22, 0xB6E8, 2); // BKGD_MIX
	add_legacy_io(23, 0xBAE8, 2); // FRGD_MIX
	add_legacy_io(24, 0xE2E8, 8); // PIX_TRANS (0xE2E8..0xE2EF)
	add_legacy_io(25, 0xB2E8, 4); // PIX_CNTL or ALT PIX_TRANS (gated by MULTIFUNC[E].bit8)
	add_legacy_io(26, 0xBEE8, 2); // MULTIFUNC_CNTL (word)
	add_legacy_io(27, 0xD2E8, 2); // ROP_MIX       (word; some paths read this)
	add_legacy_io(28, 0x9EE8, 2); // SHORT_STROKE  (word; latch)
	add_legacy_io(29, 0xCAE8, 2); // DESTY/AXSTP alias (might be wrong)
	add_legacy_io(30, 0x82E8, 2); // CUR_Y
	add_legacy_io(31, 0x92E8, 2); // ERR_TERM
	add_legacy_io(33, 0x8AE8, 2); // DESTY_AXSTP


	/* The VGA BIOS we use sends text messages to port 0x500.
	   We listen for these messages at port 500. */
	add_legacy_io(7, 0x500, 1);
	bios_message_size = 0;
	bios_message[0] = '\0';

	// Legacy video address space: A0000 -> bffff
	add_legacy_mem(4, 0xa0000, 128 * 1024);

	// Default: no linear window until guest enables CR58 or ADVFUNC bit 4.
	// Seed base/size from PCI config defaults; CR58/59 will override when written.
	lfb_active = false;
	lfb_base = s3_cfg_data[0x10 >> 2] & s3_cfg_mask[0x10 >> 2];
	lfb_size = vga.svga_intf.vram_size;                        // clamp to VRAM for now

	// Reset the base PCI device
	ResetPCI();

	/* The configuration file variable "rom" should point to a VGA BIOS
	   image. If not, try "vgabios.bin". */
	FILE* rom = fopen(myCfg->get_text_value("rom", "vgabios.bin"), "rb");
	if (!rom)
	{
		FAILURE_1(FileNotFound, "s3 rom file %s not found",
			myCfg->get_text_value("rom", "vgabios.bin"));
	}

	rom_max = (unsigned)fread(option_rom, 1, sizeof(option_rom), rom);
	fclose(rom);

	vga.attribute.state = 1;

	vga.crtc.line_compare = 1023;
	vga.crtc.vert_disp_end = 399;

	vga.dac.mask = 0xff;
	vga.dac.dirty = 1;
	vga.dac.state = 0;
	vga.dac.read = 0;

	vga.gc.memory_map_sel = 2; // monochrome text mode

	vga.sequencer.data[0] = 0x03;  // reset1=1, reset2=1
	vga.sequencer.data[4] = 0x06;  // extended_mem=1, odd_even=1, chain_four=0
	s3.sr15 = 0;               // CLKSYN Control 2 Register (SR15) 00H poweron
	vga.sequencer.data[0x0A] = 0;                // External Bus Control Register (SRA) 00H poweron
	vga.sequencer.data[0x0B] = 0;                // Miscellaneous Extended Sequencer Register 00H poweron
	vga.sequencer.data[0x0D] = 0;                // Extended Sequencer Register (EX_SR_D) (SRD) 00H poweron
	vga.sequencer.data[0x09] = 0;                // Extended Sequencer Register 9 (SR9) poweron 00H

	// MCLK PLL defaults (MAME values)
	s3.sr10 = 0x42;
	s3.sr11 = 0x41;

	// DCLK PLL defaults (MAME values)
	s3.sr12 = 0x00;
	s3.sr13 = 0x00;
	s3.clk_pll_n = 0x00;
	s3.clk_pll_r = 0x00;

	// Use VIDEO_RAM_SIZE (in bits) to size VRAM. With 22 this is 4 MB.
	vga.svga_intf.vram_size = 1u << VIDEO_RAM_SIZE;
	vga.memory = new u8[vga.svga_intf.vram_size];
	memset(vga.memory, 0, vga.svga_intf.vram_size);

	state.last_bpp = 8;

	state.x_tilesize = X_TILESIZE;
	state.y_tilesize = Y_TILESIZE;

	s3.id_cr30 = 0xE1; // Chip ID/REV register CR30, dosbox-x implementation returns 0x00 for our use case. poweron default is E1H however.
	m_crtc_map.write_byte(0x32, 0x00); // Locked by default
	m_crtc_map.write_byte(0x33, 0x00); // CR33 (Backward Compatibility 2) default 00h (no locks).
	m_crtc_map.write_byte(0x36, s3_cr36_from_memsize(vga.svga_intf.vram_size, true)); // Configuration 2 Register (CONFG_REG1) (CR36) - bootstrap config
	m_crtc_map.write_byte(0x37, 0xE5); // Configuration 2 Register (CONFG_REG2) (CR37)  - bootstrap read, sane value from 86box
	m_crtc_map.write_byte(0x3B, 0x00); // CR3B: Data Transfer Position (DTPC)
	m_crtc_map.write_byte(0x3C, 0x00); // CR3C: IL_RTSTART defaulting
	m_crtc_map.write_byte(0x40, 0x30); // System Configuration Register, power on default 30h
	m_crtc_map.write_byte(0x42, 0x00); // Mode Control 2- can set interlace vs non
	s3.strapping = (uint32_t)s3_cr36_from_memsize(vga.svga_intf.vram_size, true);
	vga.gc.memory_map_sel = 3; // color text mode
	state.vga_mem_updated = 1;

	// init MAME fields
	s3.id_high = 0x88;    // 86C764 Trio64
	s3.id_low = 0x11;
	s3.revision = 0x00;
	s3.id_cr30 = 0xE1;    // Trio64 (per datasheet)

	// lets make it decent - redraw time
	timing.vrefresh_hz = 60.0;
	timing.refresh_interval_ms = 16;   // ~60Hz
	m_last_refresh_time = std::chrono::steady_clock::now();

	// Hardware cursor defaults (MAME says windows 95 doesn't program these but it applies it regardless to everything)
	for (int i = 0; i < 4; i++) {
		s3.cursor_fg[i] = 0xFF;
		s3.cursor_bg[i] = 0x00;
	}

	// CR56: External Sync Control 1 (EX_SYNC_1) power-on default 00h
	m_crtc_map.write_byte(0x56, 0x00);

	// CR57: EX_SYNC_2 (VSYNC reset adjust), power-on default 00h
	m_crtc_map.write_byte(0x57, 0x00);

	// EX_SYNC_3 (CR63)
	m_crtc_map.write_byte(0x63, 0x00);

	// CNFG-REG-3 (CR68) poweron strap; datasheet says power-on samples PD[23:16].
	// 00h per 86Box-compatible. If needed, set CRTC.reg[0x68] before 
	// recompute_config3() for different.
	m_crtc_map.write_byte(0x68, 0x00);

	// CR65: Extended Miscellaneous Control Register (EXT-MISC-CTL) (CR65)
	m_crtc_map.write_byte(0x65, 0x00);

	m_8514.set_vga_ptr(this);
	m_8514.start();

	refresh_pitch_offset(); // do it initially, just for sanity sake

	myThread = 0;

	printf("%s: $Id$\n",
		devid_string);
}


void CS3Trio64::recompute_scanline_layout()
{
	const uint8_t cr5d = m_crtc_map.read_byte(0x5d);

	auto xbit = [&](int b) -> uint16_t { return (cr5d >> b) & 1u; };

	// still some compute/reliance, need to sync up here...

	// vga.crtc.horz_total - MAME sets low 8, we overlay bit8 from CR5D.
	vga.crtc.horz_total = (vga.crtc.horz_total & 0xff) | (xbit(0) << 8);

	// vga.crtc.horz_disp_end — same pattern.
	vga.crtc.horz_disp_end = (vga.crtc.horz_disp_end & 0xff) | (xbit(1) << 8);

	// vga.crtc.horz_blank_start — can only hold low 8 bits.
	// Compose full 9-bit value 
	vga.crtc.horz_blank_start = uint16_t(vga.crtc.horz_blank_start) | (xbit(2) << 8);

	// vga.crtc.horz_retrace_start — same.
	vga.crtc.horz_retrace_start = uint16_t(vga.crtc.horz_retrace_start) | (xbit(4) << 8);

	// vga.crtc.horz_blank_end, holds bits 0-5 (CR03 + CR05).
	uint16_t hb_end_base = uint16_t(vga.crtc.horz_blank_end & 0x3f);
	vga.crtc.horz_blank_end = hb_end_base + (xbit(3) ? 64 : 0);

	// vga.crtc.horz_retrace_end, holds bits 0-4 (from CR05).
	uint16_t hs_end_base = uint16_t(vga.crtc.horz_retrace_end & 0x1f);
	vga.crtc.horz_retrace_end = hs_end_base + (xbit(5) ? 32 : 0);

	// S3 special blanking (CR33 bit5)
	if (m_crtc_map.read_byte(0x33) & 0x20) {
		vga.crtc.horz_blank_start = vga.crtc.horz_disp_end;
		vga.crtc.horz_blank_end = (vga.crtc.horz_total - 1) & 0x1FF;
	}

	// 9-bit masking
	vga.crtc.horz_total &= 0x1FF;
	vga.crtc.horz_disp_end &= 0x1FF;
	vga.crtc.horz_blank_start &= 0x1FF;
	vga.crtc.horz_blank_end &= 0x1FF;
	vga.crtc.horz_retrace_start &= 0x1FF;
	vga.crtc.horz_retrace_end &= 0x1FF;
}

void CS3Trio64::recompute_params()
{
	recompute_scanline_layout();
	refresh_pitch_offset();
	redraw_area(0, 0, old_iWidth, old_iHeight);
}

void CS3Trio64::attribute_map(address_map& map)
{
	map.global_mask(0x3f);
	map.unmap_value_high();

	// Palette Index Registers 0x00..0x0f
	map(0x00, 0x0f).lrw8(
		NAME([this](offs_t offset) {
			return vga.attribute.data[offset & 0x1f];
			}),
		NAME([this](offs_t offset, u8 data) {
			// CR33 bit6: Lock Palette/Overscan Registers
			if (m_crtc_map.read_byte(0x33) & 0x40)
				return;
			u8 idx = offset & 0x0f;
			if (vga.attribute.data[idx] != (data & 0x3f)) {
				vga.attribute.data[idx] = data & 0x3f;
				redraw_area(0, 0, old_iWidth, old_iHeight);
			}
			})
	);

	// 0x20-0x2f mirrors — NOP (MAME: map(0x20, 0x2f).noprw())
	// Our address_map doesn't have noprw(); unmap_value_high covers reads.
	// Writes to this range are simply ignored by having no handler installed.

	// Mode Control (index 0x10, mirrored at 0x30)
	map(0x10, 0x10).mirror(0x20).lrw8(
		NAME([this](offs_t offset) {
			return vga.attribute.data[0x10];
			}),
		NAME([this](offs_t offset, u8 data) {
			const u8 prev = vga.attribute.data[0x10];
			vga.attribute.data[0x10] = data & 0x3f; // MAME canonical

			// ES40 side-effects: detect bit changes from previous value
			if (BIT(data, 2) != BIT(prev, 2)) {  // enable_line_graphics
				m_output.display().lock();
				m_output.display().set_text_charmap(
					&vga.memory[0x20000 + vga.sequencer.char_sel.A]);
				m_output.display().unlock();
				state.vga_mem_updated = 1;
			}
			if (BIT(data, 7) != BIT(prev, 7)) {  // internal_palette_size
				redraw_area(0, 0, old_iWidth, old_iHeight);
			}
			})
	);

	// Overscan Color (index 0x11, mirrored at 0x31)
	map(0x11, 0x11).mirror(0x20).lrw8(
		NAME([this](offs_t offset) {
			return vga.attribute.data[0x11];
			}),
		NAME([this](offs_t offset, u8 data) {
			// CR33 bit6: Lock Palette/Overscan Registers
			if (m_crtc_map.read_byte(0x33) & 0x40)
				return;
			vga.attribute.data[0x11] = data & 0x3f; // MAME canonical
			})
	);

	// Color Plane Enable (index 0x12, mirrored at 0x32)
	map(0x12, 0x12).mirror(0x20).lrw8(
		NAME([this](offs_t offset) {
			return vga.attribute.data[0x12];
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.attribute.data[0x12] = data & 0x3f; // MAME canonical
			redraw_area(0, 0, old_iWidth, old_iHeight);
			})
	);

	// Horizontal PEL Shift (index 0x13, mirrored at 0x33)
	map(0x13, 0x13).mirror(0x20).lrw8(
		NAME([this](offs_t offset) {
			return vga.attribute.data[0x13];
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.attribute.pel_shift_latch = data & 0xf; // MAME canonical
			vga.attribute.data[0x13] = data & 0xf;      // MAME canonical
			redraw_area(0, 0, old_iWidth, old_iHeight);
			})
	);

	// Color Select (index 0x14, mirrored at 0x34)
	map(0x14, 0x14).mirror(0x20).lrw8(
		NAME([this](offs_t offset) {
			return vga.attribute.data[0x14];
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.attribute.pel_shift_latch = data & 0xf; // MAME canonical (yes, same field)
			vga.attribute.data[0x14] = data & 0xf;      // MAME canonical
			redraw_area(0, 0, old_iWidth, old_iHeight);
			})
	);
}

/**************************************
 *
 * GC
 *
 *************************************/

void CS3Trio64::gc_map(address_map& map)
{
	map.unmap_value_high();
	map(0x00, 0x00).lrw8(
		NAME([this](offs_t offset) {
			return vga.gc.set_reset & 0xf;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.set_reset = data & 0xf;
			})
	);
	map(0x01, 0x01).lrw8(
		NAME([this](offs_t offset) {
			return vga.gc.enable_set_reset & 0xf;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.enable_set_reset = data & 0xf;
			})
	);
	map(0x02, 0x02).lrw8(
		NAME([this](offs_t offset) {
			return vga.gc.color_compare & 0xf;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.color_compare = data & 0xf;
			})
	);
	map(0x03, 0x03).lrw8(
		NAME([this](offs_t offset) {
			return ((vga.gc.logical_op & 3) << 3) | (vga.gc.rotate_count & 7);
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.logical_op = (data & 0x18) >> 3;
			vga.gc.rotate_count = data & 7;
			})
	);
	map(0x04, 0x04).lrw8(
		NAME([this](offs_t offset) {
			return vga.gc.read_map_sel & 3;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.read_map_sel = data & 3;
			})
	);
	map(0x05, 0x05).lrw8(
		NAME([this](offs_t offset) {
			u8 res = (vga.gc.shift256 & 1) << 6;
			res |= (vga.gc.shift_reg & 1) << 5;
			res |= (vga.gc.host_oe & 1) << 4;
			res |= (vga.gc.read_mode & 1) << 3;
			res |= (vga.gc.write_mode & 3);
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.shift256 = BIT(data, 6);
			vga.gc.shift_reg = BIT(data, 5);
			vga.gc.host_oe = BIT(data, 4);
			vga.gc.read_mode = BIT(data, 3);
			vga.gc.write_mode = data & 3;
			//if(data & 0x10 && vga.gc.alpha_dis)
			//  popmessage("Host O/E enabled, contact MAMEdev");
			})
	);
	map(0x06, 0x06).lrw8(
		NAME([this](offs_t offset) {
			u8 res = (vga.gc.memory_map_sel & 3) << 2;
			res |= (vga.gc.chain_oe & 1) << 1;
			res |= (vga.gc.alpha_dis & 1);
			return res;
			}),
		NAME([this](offs_t offset, u8 data) {
			u8 prev_memory_mapping = vga.gc.memory_map_sel; //es40ism
			bool prev_alpha_dis = vga.gc.alpha_dis; // es40ism
			// MAME
			vga.gc.memory_map_sel = (data & 0xc) >> 2;
			vga.gc.chain_oe = BIT(data, 1);
			vga.gc.alpha_dis = BIT(data, 0);
			//if(data & 2 && vga.gc.alpha_dis)
			//  popmessage("Chain O/E enabled, contact MAMEdev");
			// ES40 side-effects: redraw on mapping/mode change
			if (prev_memory_mapping != vga.gc.memory_map_sel)
				redraw_area(0, 0, old_iWidth, old_iHeight);
			if (prev_alpha_dis != vga.gc.alpha_dis) {
				redraw_area(0, 0, old_iWidth, old_iHeight);
				old_iHeight = 0;
			}
			})
	);
	map(0x07, 0x07).lrw8(
		NAME([this](offs_t offset) {
			return vga.gc.color_dont_care & 0xf;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.color_dont_care = data & 0xf;
			})
	);
	map(0x08, 0x08).lrw8(
		NAME([this](offs_t offset) {
			return vga.gc.bit_mask & 0xff;
			}),
		NAME([this](offs_t offset, u8 data) {
			vga.gc.bit_mask = data & 0xff;
			})
	);
}

// DB014-B 9-1 and 14-13/16. PLL lock delay is not simulated; a permitted
// load takes effect at this transaction's sampled time. SR15.1 permits repeated
// loading, while SR15.5 requests an immediate load. SR12/13 alone are staging
// registers while both controls are clear.
void CS3Trio64::load_dclk()
{
	if (!(s3.sr15 & 0x22))
		return;
	const u32 source = (vga.miscellaneous_output >> 2) & 3;
	if (source == 2)
		return; // reserved selection: leave the active oscillator unchanged
	m_scan.dclk_source = source;
	if (source == 3) {
		s3.clk_pll_n = s3.sr12 & 0x1f;
		s3.clk_pll_r = (s3.sr12 >> 5) & 3;
		s3.clk_pll_m = s3.sr13 & 0x7f;
	}
	// The book specifies the two nominal VGA rates but not the automatic
	// SR12/13 preset encodings; their inherited readback is left unchanged.
	s3_define_video_mode();
}

CS3Trio64::ScanTiming CS3Trio64::scan_timing() const
{
	ScanTiming t{};
	// Horizontal start fields in the inherited VGA structure are only bytes.
	// CR5D retains the additional bits, independently of those render caches.
	t.horizontal_total = ((vga.crtc.horz_total & 0xff) |
		((s3.cr5d & 1) << 8)) + 5;
	t.horizontal_retrace_start = vga.crtc.horz_retrace_start |
		((s3.cr5d & 0x10) << 4);
	t.vertical_total = (vga.crtc.vert_total & 0x7ff) + 2;
	t.vertical_retrace_start = vga.crtc.vert_retrace_start & 0x7ff;
	t.vertical_retrace_width = ((vga.crtc.vert_retrace_end -
		t.vertical_retrace_start - 1) & 15) + 1;
	t.vertical_divisor = vga.crtc.sldiv ? 2 : 1;
	t.interlace = (s3.cr42 & 0x20) != 0;
	t.interlace_offset = s3.cr3c;
	t.sync_enabled = vga.crtc.sync_en != 0;

	double dclk = 0.0;
	switch (m_scan.dclk_source) {
	case 0: dclk = 25175000.0; break;
	case 1: dclk = 28322000.0; break;
	case 3:
		dclk = 14318180.0 * (s3.clk_pll_m + 2) /
			((s3.clk_pll_n + 2) * (1u << (s3.clk_pll_r & 3)));
		break;
	default: break; // rejected when restoring a saved clock source
	}
	// SR14.0 powers down DCLK; no external test clock is connected to SR14.7.
	if (vga.sequencer.data[0x14] & 0x81)
		dclk = 0.0;
	// SR15.4 divides the RAMDAC VCLK path, not the CRTC character clock
	// (DB014-B 9-3). Clock-doubled DAC output does not double these counters.
	double dots_per_character = (vga.sequencer.data[1] & 1) ? 8.0 : 9.0;
	if (vga.sequencer.data[1] & 8)
		dots_per_character *= 2.0;
	if (s3.cr43 & 0x80)
		dots_per_character *= 2.0;
	// The Trio's one-clock 15/16-bit modes use half-sized horizontal units.
	// Their drivers program twice as many CRTC character counts (CR67=3/5).
	const u8 color_mode = s3.ext_misc_ctrl_2 >> 4;
	if (color_mode == 3 || color_mode == 5)
		dots_per_character /= 2.0;
	t.characters_per_second = dclk / dots_per_character;
	// Modular end comparators wrap to the next match, including equal low
	// bits (32 horizontal / 16 vertical). The equal-edge priority is not
	// specified by the book; this follows the conventional VGA counter model.
	// DB014-B labels CR5D.5 "+32 DCLKs", but the S3 Xorg driver programs it
	// for sync widths exceeding 32 CHARACTER clocks. Add a width extension;
	// ORing an endpoint bit loses it when the low comparator already wraps.
	t.horizontal_retrace_width = ((vga.crtc.horz_retrace_end -
		t.horizontal_retrace_start - 1) & 31) + 1;
	if (s3.cr5d & 0x20)
		t.horizontal_retrace_width += 32.0;
	// CR05.6:5 delays the complete HSYNC pulse by up to three character
	// clocks; it does not change the end comparator's pulse width (14-22).
	if (t.horizontal_retrace_start < t.horizontal_total)
		t.horizontal_retrace_start = (t.horizontal_retrace_start +
			vga.crtc.horz_retrace_skew) % t.horizontal_total;
	return t;
}

void CS3Trio64::advance_scan(std::chrono::steady_clock::time_point now)
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

u8 CS3Trio64::scan_status(std::chrono::steady_clock::time_point now)
{
	advance_scan(now);
	const ScanTiming t = scan_timing();
	// DB014-B 14-3: bit 0 is horizontal OR vertical retrace; bit 3 is
	// vertical retrace alone. Neither display-end nor blanking substitutes
	// for these pulses. Bit 2 reads one. Screen-off and DAC power-down do
	// not stop the CRTC; SR00's VGA reset bits have no function on Trio64.
	if (!t.sync_enabled)
		return 0x04;
	auto pulse = [](double position, double start, double width, double total) {
		return start < total && width > 0.0 &&
			std::fmod(position + total - start, total) < width;
	};
	const bool horizontal = pulse(m_scan.character, t.horizontal_retrace_start,
		t.horizontal_retrace_width, t.horizontal_total);
	const double line = m_scan.line + m_scan.character / t.horizontal_total;
	// Interlaced timings already describe a field. CR3C offsets alternate
	// field start/end by character clocks (DB014-B 15-11); no second halving
	// of the programmed vertical total is applied. External genlock and
	// sub-character PLL/sync propagation delays are not modeled.
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

void CS3Trio64::recompute_params_clock(int divisor, int xtal)
{
	// Store timing parameters for renderer/debug use -- ES40 specific
	timing.xtal_hz = xtal;
	timing.divisor = divisor;
	// es40 specific end

	int vblank_period, hblank_period;
	attoseconds_t refresh;
	uint8_t hclock_m = (!GRAPHIC_MODE) ? VGA_CH_WIDTH : 8;
	int pixel_clock;

	/* safety check */
	if (!vga.crtc.horz_disp_end || !vga.crtc.vert_disp_end || !vga.crtc.horz_total) // check needs 'vga.crtc.vert_total' but we don't implement.... yet
		return;

	const u8 is_interlace_mode = get_interlace_mode() + 1;
	const int display_lines = vga.crtc.vert_disp_end * is_interlace_mode;

	//rectangle visarea(0, ((vga.crtc.horz_disp_end + 1) * ((float)(hclock_m) / divisor)) - 1, 0, display_lines);

	vblank_period = (vga.crtc.vert_total + 2) * is_interlace_mode;
	hblank_period = ((vga.crtc.horz_total + 5) * ((float)(hclock_m) / divisor));

	// TODO: improve/complete clocking modes
	pixel_clock = xtal / ((x_dotclockdiv2() >> 3) + 1);

	refresh = HZ_TO_ATTOSECONDS(pixel_clock) * (hblank_period)*vblank_period;
	//screen().configure((hblank_period), (vblank_period), visarea, refresh);
	//m_vblank_timer->adjust(screen().time_until_pos(vga.crtc.vert_blank_start + vga.crtc.vert_blank_end));

	if (hblank_period > 0 && vblank_period > 0 && pixel_clock > 0)
	{
		timing.vrefresh_hz = (double)pixel_clock / ((double)hblank_period * (double)vblank_period);
		// Clamp to sane range
		if (timing.vrefresh_hz < 1.0)   timing.vrefresh_hz = 1.0;
		if (timing.vrefresh_hz > 240.0) timing.vrefresh_hz = 240.0;
		timing.refresh_interval_ms = (uint64_t)(1000.0 / timing.vrefresh_hz);
		if (timing.refresh_interval_ms < 4) timing.refresh_interval_ms = 4;  // cap at ~250Hz
	}

	// ES40 specific here - MAME: pixel_clock = xtal / (((vga.sequencer.data[1]&8) >> 3) + 1);
	const int seq_div = ((vga.sequencer.data[1] & 0x08) >> 3) + 1;
	timing.pixel_clock_hz = (seq_div > 0) ? (xtal / seq_div) : xtal;

	// Recompute line offset (pitch) — ES40's existing function
	refresh_pitch_offset();

	// Mark display dirty so the renderer picks up changes
	state.vga_mem_updated = 1;
}

/**
 * Create and start thread.
 **/
void CS3Trio64::start_threads()
{
	{
		std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
		advance_scan(cSystem->device_access_time());
		m_scan_paused = false;
	}
	// Repaint and resize through the normal GUI path after any resume.
	state.vga_mem_updated = true;
	state.last_bpp = 0;
	// Resume after reset if the thread already exists
	PauseThread.store(false, std::memory_order_release);

	if (!myThread)
	{
		myThread = new CThread("s3");
		printf(" %s", myThread->getName().c_str());
		StopThread = false;
		myThread->start(*this);
	}
}

/**
 * Stop and destroy thread.
 **/
void CS3Trio64::stop_threads()
{
	{
		std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
		advance_scan(cSystem->device_access_time());
		m_scan_paused = true;
	}
	// During firmware reset, do NOT kill the S3 thread (it owns the SDL window).
	// Just pause it so the window stays alive.
	if (cSystem && cSystem->IsResetInProgress())
	{
		PauseThread.store(true, std::memory_order_release);

		// Wait briefly until the S3 thread acknowledges pause
		if (myThread)
		{
			for (int spin = 0; spin < 600; spin++) // up to ~600ms
			{
				if (PauseAck.load(std::memory_order_acquire))
					break;
				CThread::sleep(1);
			}
			// Make it visible in the log whether we paused or stopped
			printf(" s3(pause)");
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
 * Destructor.
 **/
CS3Trio64::~CS3Trio64()
{
	stop_threads();
}

void CS3Trio64::ResetPCI()
{
	// Reset the setup controls and their address selector together (DB014-B
	// sections 5, 14.6 and 17-10). Scanout has its own enable controls.
	m_video_subsys_enable = 0;
	m_setup_option_select_0102 = 0;
	s3.cr65 = 0;
	CPCIDevice::ResetPCI();
}

bool CS3Trio64::normal_access_enabled() const noexcept
{
	return (m_video_subsys_enable & 0x18) == 0x08 &&
		(m_setup_option_select_0102 & 0x01);
}

bool CS3Trio64::io_access_enabled(u32 port, bool write) const noexcept
{
	// The selected setup register must remain writable to enter and leave
	// setup, including while asleep (DB014-B 11-1, 14-47/48).
	if (port == 0x3c3 || port == 0x46e8)
		return write && port == ((s3.cr65 & 0x04) ? 0x3c3U : 0x46e8U);
	if ((port > 0x46e8 && port <= 0x46eb) || (port > 0x0102 && port <= 0x0105))
		return false; // wider writes have no additional setup-register lanes
	if (port == 0x0102)
		return (m_video_subsys_enable & 0x10) != 0;
	return normal_access_enabled();
}

bool CS3Trio64::decodes_memory_access(int index, u64 address, int dsize,
	bool write) const noexcept
{
	if (index >= PCI_RANGE_BASE)
	{
		if (!CPCIDevice::decodes_memory_access(index, address, dsize, write))
			return false;
		// PCI configuration and expansion-ROM decoding remain independent:
		// the ROM must be accessible before its code wakes the video core.
		if ((index - PCI_RANGE_BASE) % 8 < 6 && !normal_access_enabled())
			return false;
		if (index == PCI_RANGE_BASE && uses_sized_linear_bar_window())
		{
			u32 offset;
			return linear_bar_offset(address, offset);
		}
		return true;
	}
	if (index < 0 || index >= MAX_DEV_RANGES || !device_at[0])
		return false;

	// Trio32/Trio64 DB014-B, sections 6.1.2 and 19 (COMMAND, bits 0/1):
	// PCI I/O and memory responses require their respective command enable.
	const u32 enable = dev_range_is_io[index] ? 1U : 2U;
	if ((pci_state.config_data[0][1] & endian_32(enable)) == 0)
		return false;

	if (index == 12 || index == 32)
		return address == 0 && io_access_enabled(index == 12 ? 0x46e8U : 0x0102U, write);
	// PCI 2.1 section 3.7.1: the first addressed byte selects the target.
	// A later enabled lane (for example 3C3 in a word at disabled 3C2)
	// cannot make this card claim the access. Handlers still gate the lanes
	// of transactions that this card does claim.
	if (index == 2)
	{
		// COMMAND.5 makes RAMDAC writes passive: never assert DEVSEL for
		// a transaction starting at 3C6-3C9 (DB014-B 8-4, 19-2). 
		if (write && address >= 6 && address <= 9 &&
			(pci_state.config_data[0][1] & endian_32(0x20U)))
			return false;
		return address < 16 && io_access_enabled(0x3c0U + (u32)address, write);
	}
	if (!normal_access_enabled())
		return false;

	// DB014-B 14-1: MISC bit 0 selects the 3Bx/3Dx port group.
	const bool color_io = (vga.miscellaneous_output & 0x01) != 0;
	switch (index)
	{
	case 1: // 3B4/3B5
	case 3: // 3BA
		return !color_io;
	case 8: // 3D4/3D5
	case 9: // 3DA
		return color_io;
	case 4: // A0000-BFFFF, address is relative to A0000
		// MMIO decoding remains separate
		if ((s3.cr53 & 0x18) ||
			(m_8514.ibm8514.advfunction_ctrl & 0x20))
			return true;

		// DB014-B 17-6 and 18-3: either linear-enable bit suppresses the
		// enhanced legacy window unless banking, 64 KiB size, and A0000
		// base are all selected.
		if (s3_lfb_enabled(s3.cr58, m_8514.ibm8514.advfunction_ctrl))
		{
			if (!(s3.memory_config & 0x08))
				return true;
			if (!(s3.memory_config & 0x01) || (s3.cr58 & 0x03) ||
				s3.cr59 != 0 || s3.cr5a != 0x0a)
				return false;
		}

		// DB014-B 15-3: CR31 bit 3 overrides GR6 with a 64 KiB map at
		// A0000. MISC bit 1 still enables CPU display-memory access (14-1).
		if (s3.memory_config & 0x08)
			return (vga.miscellaneous_output & 0x02) && address < 0x10000;

		// Packed/enhanced formats without CR31's forced map remain pending.
		if ((s3.ext_misc_ctrl_2 & 0xf0) ||
			(m_8514.ibm8514.advfunction_ctrl & 0x01))
			return true;

		// DB014-B 14-1: MISC bit 1 enables CPU display-memory access.
		if (!(vga.miscellaneous_output & 0x02))
			return false;

		// DB014-B 14-40: GR6 bits 3:2 select the CPU memory window.
		// Decline excluded addresses before reads load VGA latches or
		// writes consume an access intended for another responding device.
		switch (vga.gc.memory_map_sel & 0x03)
		{
		case 0: return address < 0x20000;
		case 1: return address < 0x10000;
		case 2: return address >= 0x10000 && address < 0x18000;
		case 3: return address >= 0x18000 && address < 0x20000;
		}
		return false;
	default:
		return true;
	}
}

u64 CS3Trio64::capture_pci_io_write(const PciIoWrite& write) const noexcept
{
	// Freeze eligibility before any target callback. The token contains only
	// DAC byte lanes and CR34's master-abort handling, not serialized state.
	if (write.bus != myPCIBus || !device_at[0] ||
		(endian_32(pci_state.config_data[0][1]) & 0x21U) != 0x21U ||
		!normal_access_enabled() || (s3.cr33 & 0x10))
		return 0;
	if ((write.dsize != 8 && write.dsize != 16 && write.dsize != 32) ||
		(write.port & 3U) + write.dsize / 8 > 4 ||
		write.port < 0x3c4 || write.port > 0x3c9)
		return 0;

	u64 captured = 0;
	for (int byte = 0; byte < write.dsize / 8; ++byte)
		if (write.port + byte >= 0x3c6 && write.port + byte <= 0x3c9)
			captured |= U64(1) << byte;
	if (!captured)
		return 0;
	// DB014-B 15-5: CR34.0/1 disable master-abort handling. Retry is
	// not represented by the synchronous dispatcher.
	if ((s3.cr34 & 3) == 0)
		captured |= 0x10;
	return captured;
}

void CS3Trio64::observe_pci_io_write(const PciIoWrite& write, u64 captured,
	PciIoWriteDispatch dispatch)
{
	// PCI palette snoopers latch completed and master-aborted writes. 
	// DB014-B does not give a disabled-mode timing table. 
	// Observing never makes that unclaimed transaction a claimed one.
	if (dispatch == PciIoWriteDispatch::NoMappedTarget && !(captured & 0x10))
		return;
	for (int byte = 0; byte < write.dsize / 8; ++byte)
		if (captured & (U64(1) << byte))
			ramdac_write_b(write.port + byte, u8(write.data >> (byte * 8)));
}

std::string CS3Trio64::describe_access_context(int index, u64 address) const
{
	char text[160];
	snprintf(text, sizeof(text),
		"cmd %04x setup %02x opt %02x cr65 %02x misc %02x seq %02x gc %02x "
		"crtc %02x atc %02x/%u",
		(unsigned)(endian_32(pci_state.config_data[0][1]) & 0xffff),
		m_video_subsys_enable, m_setup_option_select_0102, s3.cr65,
		vga.miscellaneous_output, vga.sequencer.index, vga.gc.index,
		vga.crtc.index, vga.attribute.index, (unsigned)vga.attribute.state);
	return text;
}

/**
 * Read from one of the Legacy (fixed-address) memory ranges.
 **/
u32 CS3Trio64::ReadMem_Legacy(int index, u32 address, int dsize)
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

		// IO Port 0x3d4
	case 8:
		data = io_read(address + 0x3d4, dsize);
		break;

		// IO Port 0x3da
	case 9:
		data = io_read(address + 0x3da, dsize);
		break;

	case 10: data = io_read(address + 0x42E8, dsize); break;
	case 11: data = io_read(address + 0x4AE8, dsize); break;
	case 12: data = io_read(address + 0x46E8, dsize); break;
	case 13: data = io_read(address + 0x4EE8, dsize); break;
	case 14: data = io_read(address + 0x86E8, dsize); break;
	case 15: data = io_read(address + 0x8EE8, dsize); break;
	case 16: data = io_read(address + 0x96E8, dsize); break;
	case 17: data = io_read(address + 0x9AE8, dsize); break;
	case 18: data = io_read(address + 0xA2E8, dsize); break; // BKGD_COLOR
	case 19: data = io_read(address + 0xA6E8, dsize); break; // FRGD_COLOR
	case 20: data = io_read(address + 0xAAE8, dsize); break; // WRT_MASK
	case 21: data = io_read(address + 0xAEE8, dsize); break; // RD_MASK
	case 22: data = io_read(address + 0xB6E8, dsize); break; // BKGD_MIX
	case 23: data = io_read(address + 0xBAE8, dsize); break; // FRGD_MIX
	case 24: data = io_read(address + 0xE2E8, dsize); break; // PIX_TRANS (8 bytes)
	case 25: data = io_read(address + 0xB2E8, dsize); break;
	case 26: data = io_read(address + 0xBEE8, dsize); break;
	case 27: data = io_read(address + 0xD2E8, dsize); break;
	case 28: data = io_read(address + 0x9EE8, dsize); break;
	case 29: data = io_read(address + 0xCAE8, dsize); break;
	case 30: data = io_read(address + 0x82E8, dsize); break;
	case 31: data = io_read(address + 0x92E8, dsize); break;
	case 33: data = io_read(address + 0x8AE8, dsize); break;

	case 32:
		data = io_read(address + 0x102, dsize);
		break;
	}

	return data;
}

/**
 * Write to one of the Legacy (fixed-address) memory ranges.
 **/
void CS3Trio64::WriteMem_Legacy(int index, u32 address, int dsize, u32 data)
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

		// BIOS Message IO Port (0x500)
	case 7:
		bios_message[bios_message_size++] = (char)data & 0xff;
		if (((data & 0xff) == 0x0a) || ((data & 0xff) == 0x0d))
		{
			if (bios_message_size > 1)
			{
				bios_message[bios_message_size - 1] = '\0';
				printf("s3: %s\n", bios_message);
			}

			bios_message_size = 0;
		}

		return;

		// IO Port 0x3d4
	case 8:
		io_write(address + 0x3d4, dsize, data);
		return;

		// IO Port 0x3da
	case 9:
		io_write(address + 0x3da, dsize, data);
		return;

	case 10: io_write(address + 0x42E8, dsize, data); break;
	case 11: io_write(address + 0x4AE8, dsize, data); break;
	case 12: io_write(address + 0x46E8, dsize, data); break;
	case 13: io_write(address + 0x4EE8, dsize, data); break;
	case 14: io_write(address + 0x86E8, dsize, data); break;
	case 15: io_write(address + 0x8EE8, dsize, data); break;
	case 16: io_write(address + 0x96E8, dsize, data); break;
	case 17: io_write(address + 0x9AE8, dsize, data); break;
	case 18: io_write(address + 0xA2E8, dsize, data); break; // BKGD_COLOR
	case 19: io_write(address + 0xA6E8, dsize, data); break; // FRGD_COLOR
	case 20: io_write(address + 0xAAE8, dsize, data); break; // WRT_MASK
	case 21: io_write(address + 0xAEE8, dsize, data); break; // RD_MASK
	case 22: io_write(address + 0xB6E8, dsize, data); break; // BKGD_MIX
	case 23: io_write(address + 0xBAE8, dsize, data); break; // FRGD_MIX
	case 24: io_write(address + 0xE2E8, dsize, data); break; // PIX_TRANS (8 bytes)
	case 25: io_write(address + 0xB2E8, dsize, data); break;
	case 26: io_write(address + 0xBEE8, dsize, data); break;
	case 27: io_write(address + 0xD2E8, dsize, data); break;
	case 28: io_write(address + 0x9EE8, dsize, data); break;
	case 29: io_write(address + 0xCAE8, dsize, data); break;
	case 30: io_write(address + 0x82E8, dsize, data); break;
	case 31: io_write(address + 0x92E8, dsize, data); break;
	case 33: io_write(address + 0x8AE8, dsize, data); break;

	case 32:
		io_write(address + 0x102, dsize, data);
		return;
	}
}

int CS3Trio64::BytesPerPixel() const
{
	const uint8_t pf = (s3.ext_misc_ctrl_2 >> 4) & 0x0F;
	switch (pf) {
	case 0x01: return 1; // Mode 8: 8-bit packed
	case 0x02: return 2; // Mode 1: 15-bit (2 VCLK/pixel)
	case 0x03: return 2; // Mode 9: 15-bit (1 VCLK/pixel)
	case 0x04: return 3; // Mode 2: 24-bit (3 VCLK/pixel)
	case 0x05: return 2; // Mode 10: 16-bit (1 VCLK/pixel)
	case 0x06: return 2; // Mode 3: 16-bit (2 VCLK/pixel)
	case 0x07: return 4; // Mode 11: 32-bit (2 VCLK/pixel) 
	case 0x0D: return 4; // Mode 13: 32-bit alternate
	default:   return 1; // Mode 0: 8bpp indexed (or VGA)
	}
}

u32 CS3Trio64::PitchBytes() const
{
	return (uint32_t)vga.crtc.offset;
}

static inline u32 clamp_vram_addr(u32 a, u32 vram_size) {
	return (vram_size == 0) ? a : (a % vram_size);
}

void CS3Trio64::accel_reset() {
	m_8514.start();
	m_8514.ibm8514.enabled = (s3.cr40 & 0x01);
	on_crtc_linear_regs_changed("accelerator reset");
}


// -------------------------
// Minimal accel window I/O
// -------------------------
u8 CS3Trio64::AccelIORead(u32 port)
{
	ibm8514a_device* dev = get_8514();
	if (!s3.enable_8514) return 0xFF;

	switch (port & 0xFFFE) {
	case 0x9AE8: {
		// Trio32/64 GP_STAT (DB014-B 18-10): bit 8 is reserved, and AE only tracks the command
		// FIFO, which is always drained here -- it stays set while HDW BSY waits on PIX_TRANS.
		const uint16_t ret = (dev->ibm8514_gpstatus_r() | 0x0400) & ~0x0100;
		return (port & 1) ? (uint8_t)(ret >> 8) : (uint8_t)(ret & 0xff);
	}

	case 0x42E8: {
		uint16_t v = dev->ibm8514_substatus_r();
		return (port & 1) ? (v >> 8) : (v & 0xff);
	}

	// coordinate & size readbacks
	case 0x82E8: { uint16_t v = dev->ibm8514_currenty_r();      return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0x86E8: { uint16_t v = dev->ibm8514_currentx_r();      return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0x8AE8: { uint16_t v = dev->ibm8514_desty_r();         return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0x8EE8: { uint16_t v = dev->ibm8514_destx_r();         return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0x92E8: { uint16_t v = dev->ibm8514_line_error_r();    return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0x96E8: { uint16_t v = dev->ibm8514_width_r();         return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0x9EE8: { uint16_t v = dev->ibm8514_ssv_r();           return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xA2E8: { uint16_t v = dev->ibm8514_bgcolour_r();      return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xA2EA: { uint16_t v = dev->ibm8514_bgcolour_r_hi();   return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xA6E8: { uint16_t v = dev->ibm8514_fgcolour_r();      return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xA6EA: { uint16_t v = dev->ibm8514_fgcolour_r_hi();   return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xAAE8: { uint16_t v = dev->ibm8514_write_mask_r();    return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xAAEA: { uint16_t v = dev->ibm8514_write_mask_r_hi(); return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xAEE8: { uint16_t v = dev->ibm8514_read_mask_r();     return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xAEEA: { uint16_t v = dev->ibm8514_read_mask_r_hi();  return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xB6E8: { uint16_t v = dev->ibm8514_backmix_r();       return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xBAE8: { uint16_t v = dev->ibm8514_foremix_r();       return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xB2E8: { uint16_t v = dev->ibm8514_color_cmp_r();     return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xB2EA: { uint16_t v = dev->ibm8514_color_cmp_r_hi();  return (port & 1) ? (v >> 8) : (v & 0xff); }
	case 0xBEE8: { uint16_t v = dev->ibm8514_multifunc_r(m_video_subsys_enable);     return (port & 1) ? (v >> 8) : (v & 0xff); }

	default:
		return 0x00;
	}
}

u32 CS3Trio64::AccelIORead(u32 port, int dsize)
{
	if (dsize != 8 && dsize != 16 && dsize != 32)
		FAILURE(InvalidArgument, "Unsupported dsize");
	u32 result = 0;
	for (int lane = 0; lane < dsize / 8; ++lane)
	{
		// BEE8 is one register read, not two byte reads: READ_SEL advances
		// once for a word transaction (DB014-B 18-19/18-24).
		if (port + lane == 0xbee8 && lane + 1 < dsize / 8 && s3.enable_8514)
		{
			result |= (u32)m_8514.ibm8514_multifunc_r(m_video_subsys_enable) << (lane * 8);
			++lane;
		}
		else
			result |= (u32)AccelIORead(port + lane) << (lane * 8);
	}
	return result;
}

static inline void write16_low_high(u16& reg, u32 port, u8 data) {
	if ((port & 1) == 0) reg = (reg & 0xFF00u) | data;
	else                 reg = (reg & 0x00FFu) | (u16)data << 8;
}

void CS3Trio64::AccelIOWrite(u32 port, u8 data)
{
	ibm8514a_device* dev = get_8514();
	if (!s3.enable_8514) return;

	switch (port & 0xFFFE) {

		// SUBSYS_CNTL (42E8h write)
	case 0x42E8: {
		const unsigned shift = (port & 1) * 8;
		dev->ibm8514_subcontrol_w(u16(data) << shift, u16(0xff) << shift);
		s3.mmio_42e8 = dev->ibm8514_subcontrol_r();
		break;
	}

		// ADVFUNC_CNTL (4AE8h)
	case 0x4AE8:
		// The assembly mirror can be stale after accelerator reset or restore.
		s3.mmio_4ae8 = dev->ibm8514.advfunction_ctrl;
		write16_low_high(s3.mmio_4ae8, port, data);
		dev->ibm8514_advfunc_w(s3.mmio_4ae8);
		on_crtc_linear_regs_changed("ADVFUNC");
		break;

		// CUR_Y (82E8h) — MAME sets prev_y too
	case 0x82E8:
		if ((port & 1) == 0) {
			dev->ibm8514.curr_y = (dev->ibm8514.curr_y & 0xff00) | data;
			dev->ibm8514.prev_y = (dev->ibm8514.prev_y & 0xff00) | data;
		}
		else {
			dev->ibm8514.curr_y = (dev->ibm8514.curr_y & 0x00ff) | (data << 8);
			dev->ibm8514.prev_y = (dev->ibm8514.prev_y & 0x00ff) | (data << 8);
		}
		break;

		// CUR_X (86E8h) — MAME sets prev_x too
	case 0x86E8:
		if ((port & 1) == 0) {
			dev->ibm8514.curr_x = (dev->ibm8514.curr_x & 0xff00) | data;
			dev->ibm8514.prev_x = (dev->ibm8514.prev_x & 0xff00) | data;
		}
		else {
			dev->ibm8514.curr_x = (dev->ibm8514.curr_x & 0x00ff) | (data << 8);
			dev->ibm8514.prev_x = (dev->ibm8514.prev_x & 0x00ff) | (data << 8);
		}
		break;

		// DESTY/AXSTP (8AE8h) — dual-purpose register
	case 0x8AE8:
		if ((port & 1) == 0) {
			dev->ibm8514.line_axial_step = (dev->ibm8514.line_axial_step & 0xff00) | data;
			dev->ibm8514.dest_y = (dev->ibm8514.dest_y & 0xff00) | data;
		}
		else {
			dev->ibm8514.line_axial_step = (dev->ibm8514.line_axial_step & 0x00ff) | ((data & 0x3f) << 8);
			dev->ibm8514.dest_y = (dev->ibm8514.dest_y & 0x00ff) | ((data & 0x0f) << 8);
		}
		break;

		// COLOR_CMP (B2E8h)
	case 0xB2E8:
	case 0xB2EA:
	{
		unsigned s = (port & 3) * 8;
		dev->ibm8514.color_cmp = (dev->ibm8514.color_cmp & ~(0xFFu << s)) | ((u32)data << s);
	}
	break;

	// DESTX/DIASTP (8EE8h) — dual-purpose register
	case 0x8EE8:
		if ((port & 1) == 0) {
			dev->ibm8514.line_diagonal_step = (dev->ibm8514.line_diagonal_step & 0xff00) | data;
			dev->ibm8514.dest_x = (dev->ibm8514.dest_x & 0xff00) | data;
		}
		else {
			dev->ibm8514.line_diagonal_step = (dev->ibm8514.line_diagonal_step & 0x00ff) | ((data & 0x3f) << 8);
			dev->ibm8514.dest_x = (dev->ibm8514.dest_x & 0x00ff) | ((data & 0x0f) << 8);
		}
		break;

		// ERR_TERM (92E8h)
	case 0x92E8:
		if ((port & 1) == 0)
			s3.mmio_92e8 = (s3.mmio_92e8 & 0xff00) | data;
		else {
			s3.mmio_92e8 = (s3.mmio_92e8 & 0x00ff) | (data << 8);
			dev->ibm8514_line_error_w(s3.mmio_92e8);
		}
		break;

		// MAJ_AXIS_PCNT (96E8h)
	case 0x96E8:
		if ((port & 1) == 0)
			s3.mmio_96e8 = (s3.mmio_96e8 & 0xff00) | data;
		else {
			s3.mmio_96e8 = (s3.mmio_96e8 & 0x00ff) | (data << 8);
			dev->ibm8514_width_w(s3.mmio_96e8);
		}
		break;

		// CMD (9AE8h) — high byte triggers execution!
	case 0x9AE8:
		if ((port & 1) == 0)
			s3.mmio_9ae8 = (s3.mmio_9ae8 & 0xff00) | data;
		else {
			s3.mmio_9ae8 = (s3.mmio_9ae8 & 0x00ff) | (data << 8);
			dev->ibm8514_cmd_w(s3.mmio_9ae8);
		}
		break;

		// SSV (9EE8h) — high byte triggers execution
	case 0x9EE8:
		if ((port & 1) == 0)
			s3.mmio_9ee8 = (s3.mmio_9ee8 & 0xff00) | data;
		else {
			s3.mmio_9ee8 = (s3.mmio_9ee8 & 0x00ff) | (data << 8);
			dev->ibm8514_ssv_w(s3.mmio_9ee8);
		}
		break;

		// BKGD_COLOR (A2E8h)
	case 0xA2E8:
	case 0xA2EA:
	{
		unsigned s = (port & 3) * 8;
		dev->ibm8514.bgcolour = (dev->ibm8514.bgcolour & ~(0xFFu << s)) | ((u32)data << s);
	}
	break;

	// FRGD_COLOR (A6E8h)
	case 0xA6E8:
	case 0xA6EA:
	{
		unsigned s = (port & 3) * 8;
		dev->ibm8514.fgcolour = (dev->ibm8514.fgcolour & ~(0xFFu << s)) | ((u32)data << s);
	}
	break;

	// WRT_MASK (AAE8h)
	case 0xAAE8:
	case 0xAAEA:
	{
		unsigned s = (port & 3) * 8;
		dev->ibm8514.write_mask = (dev->ibm8514.write_mask & ~(0xFFu << s)) | ((u32)data << s);
	}
	break;

	// RD_MASK (AEE8h)
	case 0xAEE8:
	case 0xAEEA:
	{
		unsigned s = (port & 3) * 8;
		dev->ibm8514.read_mask = (dev->ibm8514.read_mask & ~(0xFFu << s)) | ((u32)data << s);
	}
	break;

	// BKGD_MIX (B6E8h)
	case 0xB6E8:
		if ((port & 1) == 0)
			dev->ibm8514.bgmix = (dev->ibm8514.bgmix & 0xff00) | data;
		else {
			dev->ibm8514.bgmix = (dev->ibm8514.bgmix & 0x00ff) | (data << 8);
			dev->ibm8514.bkgd_sel = (dev->ibm8514.bgmix >> 5) & 3;
			dev->ibm8514.bkgd_mix_mode = dev->ibm8514.bgmix & 0x0f;
		}
		break;

		// FRGD_MIX (BAE8h)
	case 0xBAE8:
		if ((port & 1) == 0)
			dev->ibm8514.fgmix = (dev->ibm8514.fgmix & 0xff00) | data;
		else {
			dev->ibm8514.fgmix = (dev->ibm8514.fgmix & 0x00ff) | (data << 8);
			dev->ibm8514.frgd_sel = (dev->ibm8514.fgmix >> 5) & 3;
			dev->ibm8514.frgd_mix_mode = dev->ibm8514.fgmix & 0x0f;
		}
		break;

		// MULTIFUNC_CNTL (BEE8h) — high byte triggers dispatch
	case 0xBEE8:
		if ((port & 1) == 0)
			s3.mmio_bee8 = (s3.mmio_bee8 & 0xff00) | data;
		else {
			s3.mmio_bee8 = (s3.mmio_bee8 & 0x00ff) | (data << 8);
			dev->ibm8514_multifunc_w(s3.mmio_bee8);
		}
		break;

		// PIX_TRANS (E2E8h..E2EFh) — host data upload
		// Uses the ibm8514a's bus_size-aware accumulation + wait_draw()
	case 0xE2E8: case 0xE2EA: case 0xE2EC: case 0xE2EE:
	{
		if (dev->ibm8514.fifo_idx > 0)   // byte-wise feed: drain the WAIT command's COUNT here
			dev->ibm8514.fifo_idx--;
		if (dev->ibm8514.bus_size == 0) {
			dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xffffff00) | data;
			dev->ibm8514_pixel_xfer_complete();
		}
		else if (dev->ibm8514.bus_size == 1) {
			if ((dev->ibm8514.current_cmd & 0x02) || (dev->ibm8514.color_bpp == 0)) {
				switch (port & 0x0001) {
				case 0: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xffffff00) | data; break;
				case 1: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xffff00ff) | (data << 8);
					dev->ibm8514_pixel_xfer_complete();
					break;
				}
			} 
			else {
				switch (port & 0x0003) {
				case 0: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xffffff00) | data; break;
				case 1: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xffff00ff) | (data << 8); break;
				case 2: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xff00ffff) | (data << 16); break;
				case 3: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0x00ffffff) | (data << 24);
					dev->ibm8514.bus_size = 2; // Windows NT background pattern hack
					dev->ibm8514_pixel_xfer_complete();
				break;
				}
			}
		}
		else if (dev->ibm8514.bus_size >= 2) {
			switch (port & 0x0003) {
			case 0: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xffffff00) | data; break;
			case 1: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xffff00ff) | (data << 8); break;
			case 2: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0xff00ffff) | (data << 16); break;
			case 3: dev->ibm8514.pixel_xfer = (dev->ibm8514.pixel_xfer & 0x00ffffff) | (data << 24);
				dev->ibm8514_pixel_xfer_complete();
				break;
			}
		}
		break;
	}

	default:
		LOG("S3 Accel: unhandled I/O write port=%04x data=%02x\n", port, data);
		break;
	}
}

bool CS3Trio64::IsAccelPort(u32 p) const {
	switch (p & 0xFFFE) { // word regs, we accept low/high bytes
		// status/control
	case 0x42E8: // SUBSYS_CNTL / SUBSYS_STAT (w/r)
	case 0x4AE8: // ADVFUNC_CNTL
		// coordinates
	case 0x4EE8: // legacy compatibility stub
	case 0x82E8: // CUR_Y
	case 0x86E8: // CUR_X
	case 0x8AE8: // DESTY_AXSTP
	case 0x8EE8: // DESTX_DIASTP
	case 0xCAE8: // DESTY / AXSTP alias (seen in 86Box mappings)
		// dimensions / count
	case 0x96E8: // MAJ_AXIS_PCNT
		// mixes, masks, colors
	case 0xA2E8: // BKGD_COLOR
	case 0xA6E8: // FRGD_COLOR
	case 0xAAE8: // WRT_MASK
	case 0xAEE8: // RD_MASK
	case 0xB6E8: // BKGD_MIX
	case 0xBAE8: // FRGD_MIX
		// extra control regs used by some S3 paths
	case 0xD2E8: // ROP_MIX (word)
	case 0xBEE8: // MULTIFUNC_CNTL (word)
	case 0x9EE8: // SHORT_STROKE (word)
	case 0x9D48: // SSV (older window alias)
	case 0xB2E8: // COLOR_CMP (Color Compare register)
	case 0x9AE8: // CMD
	case 0x92E8:
		return true;
	default:
		break;
	}
	// Host data (PIX_TRANS): accept 0xE2E8..0xE2EF byte-wise for color fills
	if ((p & 0xFFF0u) == 0xE2E0u) return true; // PIX_TRANS/host
	return false;
}

/**
 * Read from one of the PCI BAR (configurable address) memory ranges.
 **/
u32 CS3Trio64::ReadMem_Bar(int func, int bar, u32 address, int dsize)
{
	if (bar == 0 && !normal_access_enabled())
		return dsize == 8 ? 0xffU : dsize == 16 ? 0xffffU : 0xffffffffU;
#ifdef S3_LFB_TRACE
	if (lfb_trace_needs_first_access_note) {
		printf("%s: LFB first BAR access @+%llx size=%d\n",
			devid_string, (unsigned long long)address, dsize);
		lfb_trace_needs_first_access_note = false;
	}
#endif

	switch (bar)
	{
		// PCI memory range
	case 0:
		if (func == 0 && uses_sized_linear_bar_window())
		{
			u32 offset;
			if (!linear_bar_offset(address, offset))
				return dsize == 8 ? 0xffU : dsize == 16 ? 0xffffU : 0xffffffffU;
			return mem_read(offset, dsize);
		}
		if (!lfb_active) {
			// No decode when LFB disabled  mimic bus-float/read-as-FFs
			return (dsize == 1) ? 0xFFu : (dsize == 2) ? 0xFFFFu : 0xFFFFFFFFu;
		}
		// Preserve the preceding data path for modes outside the scoped decoder.
		if (address >= 0xa0000 && address <= 0xbffff)
			return mem_r(address - 0xa0000);
		return mem_read(address, dsize);
	case 6:
		return func == 0 ? rom_read(address, dsize) : 0;
	}

	return 0;
}

/**
 * Write to one of the PCI BAR (configurable address) memory ranges.
 **/
void CS3Trio64::WriteMem_Bar(int func, int bar, u32 address, int dsize, u32 data)
{
	if (bar == 0 && !normal_access_enabled())
		return;
#ifdef DEBUG_PCI
	printf("[S3::WriteMem_Bar] func=%d bar=%d addr=%08X dsize=%d data=%08X\n",
		func, bar, address, dsize, data);
#endif
#ifdef S3_LFB_TRACE
	if (lfb_trace_needs_first_access_note) {
		printf("%s: LFB first BAR access @+%llx size=%d (W)\n",
			devid_string, (unsigned long long)address, dsize);
		lfb_trace_needs_first_access_note = false;
	}
#endif

	switch (bar)
	{
		// PCI Memory range
	case 0:
		if (func == 0 && uses_sized_linear_bar_window())
		{
			u32 offset;
			if (linear_bar_offset(address, offset))
				mem_write(offset, dsize, data);
			return;
		}
		if (!lfb_active) {
			// Ignore writes when LFB disabled
			return;
		}
		mem_write(address, dsize, data);
		return;
	}
}

// --- Only include LFB here; legacy VGA paths fall through to CVGA ---
u64 CS3Trio64::ReadMem(int index, u64 address, int dsize)
{
	if (index == DEV_LFB_IDX && !normal_access_enabled())
		return dsize == 8 ? 0xffU : dsize == 16 ? 0xffffU : dsize == 32 ? 0xffffffffU : ~U64(0);
	// LFB window (registered by update_linear_mapping)
	if (index == DEV_LFB_IDX && lfb_active && lfb_size)
	{
		const u64 off = address; // dispatcher already subtracts base
		// Trio64 "new MMIO" 128 KiB window at LFB+0x0100_0000 (CR53 bit3)
		//  - Lower half : PIX_TRANS FIFO (0xE2E8..0xE2EB) for 8/16/32-bit reads
		//  - Upper half : 8514/A register mirror at *E8 offsets (IsAccelPort())
		if (s3_new_mmio_enabled()) {
			printf("NEW MMIO READ !!!\n");
			const u64 win_lo = 0x01000000ull;
			const u64 win_mid = 0x01008000ull;
			const u64 win_hi = 0x01020000ull;
			if (off >= win_lo && off < win_hi) {
				if (off < win_mid) {
					switch (dsize) {
					case 8:
						return (u64)AccelIORead(0xE2E8);
					case 16:
						return (u64)AccelIORead(0xE2E8) | ((u64)AccelIORead(0xE2E9) << 8);
					case 32:
						return (u64)AccelIORead(0xE2E8) | ((u64)AccelIORead(0xE2E9) << 8) | ((u64)AccelIORead(0xE2EA) << 16) | ((u64)AccelIORead(0xE2EB) << 24);
					default: FAILURE(InvalidArgument, "Unsupported dsize");
					}
				}
				else {
					const u32 p = (u32)(off - win_lo); // ports by offset
					if (IsAccelPort(p))
						return AccelIORead(p, dsize);
				}
			}
		}
		if (off >= lfb_size) return 0;

		// Read little-endian from linear VRAM
		switch (dsize)
		{
		case 1:  return vga.memory[off];
		case 2:  return *(u16*)(vga.memory + off);
		case 4:  return *(u32*)(vga.memory + off);
		case 8: {
			u64 v = *(u32*)(vga.memory + off);
			v |= (u64) * (u32*)(vga.memory + off + 4) << 32;
#ifdef S3_LFB_TRACE
			printf("%s: LFB R size=%d @%llx => %08" PRIx64 " (off=%llx)\n",
				devid_string, dsize, (unsigned long long)address,
				v, (unsigned long long)(address - lfb_base));
#endif
			return v;
		}
		default: // fall back byte-by-byte
		{
			u64 v = 0;
			for (int i = 0; i < dsize; ++i) v |= (u64)vga.memory[off + i] << (i * 8);
			return v;
		}
		}
	}

	// Everything else (all legacy VGA ports & A0000 region, option ROM, etc.)
	return CVGA::ReadMem(index, address, dsize);
}

void CS3Trio64::WriteMem(int index, u64 address, int dsize, u64 data)
{
	if (index == DEV_LFB_IDX && !normal_access_enabled())
		return;
	if (index == DEV_LFB_IDX && lfb_active && lfb_size)
	{
		const u64 off = address; // dispatcher already subtracts base
		// Trio64 "new MMIO" 128 KiB window at LFB+0x0100_0000 (CR53 bit3)
		//  - Lower half : PIX_TRANS FIFO (0xE2E8..0xE2EB)
		//  - Upper half : 8514/A registers mirrored at *E8 offsets
		if (s3_new_mmio_enabled()) {
			const u64 win_lo = 0x01000000ull;
			const u64 win_hi = 0x01020000ull;
			if (off >= win_lo && off < win_hi) {
				const u32 mmio_off = (u32)(off - win_lo);
				switch (dsize) {
				case 8:  mem_w(mmio_off, (u8)(data)); return;
				case 16: mem_w(mmio_off, (u8)(data)); mem_w(mmio_off + 1, (u8)(data >> 8)); return;
				case 32: mem_w(mmio_off, (u8)(data)); mem_w(mmio_off + 1, (u8)(data >> 8));
					mem_w(mmio_off + 2, (u8)(data >> 16)); mem_w(mmio_off + 3, (u8)(data >> 24)); return;
				default: FAILURE(InvalidArgument, "Unsupported dsize");
				}
			}
		}
		if (off >= lfb_size) return;

		// Write little-endian into linear VRAM
		switch (dsize)
		{
#ifdef S3_LFB_TRACE
			printf("%s: LFB W size=%d @%llx <= %08" PRIx64 " (off=%llx)\n",
				devid_string, dsize, (unsigned long long)address,
				data, (unsigned long long)(address));
#endif
		case 1:  vga.memory[off] = (u8)data; break;
		case 2:  *(u16*)(vga.memory + off) = (u16)data; break;
		case 4:  *(u32*)(vga.memory + off) = (u32)data; break;
		case 8: {
			*(u32*)(vga.memory + off) = (u32)(data & 0xffffffffu);
			*(u32*)(vga.memory + off + 4) = (u32)(data >> 32);
			break;
		}
		default:
			for (int i = 0; i < dsize; ++i)
				vga.memory[off + i] = (u8)(data >> (i * 8));
			break;
		}

		state.vga_mem_updated = 1;
		return;
	}

	CVGA::WriteMem(index, address, dsize, data);
}


static inline u64 alpha_pio_phys_from_linear_base(u32 base)
{
	// Typhoon: PIO vs system memory is selected by physical bit<43>.
	// We map PCI memory space windows by setting that bit.
	// 0x0000_0800_0000_0000 is the simplest way to assert <43>.
	return U64(0x0000080000000000) | (u64)base;
}

void CS3Trio64::lfb_recalc_and_map()
{
	// BAR-only implementation: rely on BAR0 decoding in PCI core.
	// Just refresh cached enable/base/size; no RegisterMemory calls here.
	lfb_recalc_and_cache();
}


u32 CS3Trio64::config_read_custom(int func, u32 address, int dsize, u32 cur)
{
	// Return stored bits, including literal values restored from older saves.
	// Shared address bits are reconciled when their PCI or CRTC register is written.
	return cur;
}

void CS3Trio64::config_write_custom(int func, u32 address, int dsize,
	u32 old_data, u32 new_data, u32 raw)
{
	// Watch COMMAND (0x04..0x05) and BAR0 (0x10..0x13)..
	const bool is_command = (address == 0x04 || address == 0x05);
	const bool is_bar0 = (address >= 0x10 && address <= 0x13);

	if (func == 0 && is_bar0 && !m_replaying_pci_state)
	{
		u32 lanes = 0;
		if (dsize == 8)
			lanes = 0xffU << ((address - 0x10) * 8);
		else if (dsize == 16 && (address & 1U) == 0)
			lanes = 0xffffU << ((address - 0x10) * 8);
		else if (dsize == 32 && address == 0x10)
			lanes = 0xffffffffU;

		// DB014-B 17-7/19-4: BAR0[31:23] is shared with CR59/CR5A[7].
		// Keep unwritten bits and bits absent from the current BAR mask intact.
		const u32 shared = lanes & endian_32(pci_state.config_mask[0][4]) & 0xff800000U;
		if (shared)
		{
			const u32 bar0 = endian_32(pci_state.config_data[0][4]);
			s3.cr59 = u8((s3.cr59 & ~(shared >> 24)) | ((bar0 & shared) >> 24));
			s3.cr5a = u8((s3.cr5a & ~(shared >> 16)) | ((bar0 & shared) >> 16));
			lfb_base = (u32(s3.cr59) << 24) | (u32(s3.cr5a) << 16);
		}
	}

	if (is_command || is_bar0) {
		lfb_recalc_and_cache();
		// Apply/unapply DEV_LFB mapping when MSE or BAR0 changes
		lfb_recalc_and_map();
		trace_lfb_if_changed(is_command ? "PCI COMMAND" : "PCI BAR0");
	}
}

void CS3Trio64::trace_lfb_if_changed(const char* reason) {
	const bool linear_on = s3_lfb_enabled(m_crtc_map.read_byte(0x58), m_8514.ibm8514.advfunction_ctrl);
	const uint32_t sz = s3_lfb_size_from_cr58(m_crtc_map.read_byte(0x58));
	const uint32_t base = pci_bar0;  // BAR-only base of truth
	const bool eff = pci_mem_enable && linear_on && (base != 0);

	if (!lfb_trace_initialized ||
		eff != lfb_trace_enabled_prev ||
		base != lfb_trace_base_prev ||
		sz != lfb_trace_size_prev) {

#ifdef S3_LFB_TRACE
		printf("%s: LFB %s - MSE=%d CR58=%02x ADVFUNC=%04x base=%08x size=%x (reason=%s)\n",
			devid_string, eff ? "ACTIVE(BAR)" : "INACTIVE(BAR)",
			(int)pci_mem_enable, m_crtc_map.read_byte(0x58), m_8514.ibm8514.advfunction_ctrl,
			base, sz, reason ? reason : "n/a");
#endif

		lfb_trace_initialized = true;
		lfb_trace_enabled_prev = eff;
		lfb_trace_base_prev = base;
		lfb_trace_size_prev = sz;
	}
	if (eff) lfb_trace_needs_first_access_note = true;
}

void CS3Trio64::lfb_recalc_and_cache()
{
	// COMMAND bit 1 (Memory Space Enable)
	const u32 cmd = config_read(0, 0x04, 16);            // 16-bit read is enough for COMMAND

	// BAR0: 32-bit memory BAR, mask off attribute bits
	const u32 bar0 = config_read(0, 0x10, 32) & 0xFFFFFFF0u;

	pci_mem_enable = (cmd & 0x0002) != 0; // saner, i think...

	pci_bar0 = bar0;

	// Honor both linear enables and CR58 size; BAR0 remains the mapping base.
	const u8 cr58 = m_crtc_map.read_byte(0x58);
	lfb_base_ = bar0;                        // effective CPU-visible base = BAR0
	lfb_size_ = s3_lfb_size_from_cr58(cr58); // 64K/1M/2M/4M per Trio64
	lfb_enabled_ = pci_mem_enable && s3_lfb_enabled(cr58, m_8514.ibm8514.advfunction_ctrl) && (bar0 != 0);
}



/**
 * Check if threads are still running.
 **/
void CS3Trio64::check_state()
{
	if (myThread && !myThread->isRunning())
		FAILURE(Thread, "S3 thread has died");
}

static u32  s3_magic1 = 0x53338811;
static u32  s3_magic2 = 0x88115333;
static const u32 s3_video_magic = 0x53335632; // S3V2, complete graphics state

// The surrounding system format identifies compatible snapshots. 
// Per-record sizes additionally reject a different structure layout.
template <typename T>
static bool s3_write_record(FILE* f, const T& value)
{
	static_assert(std::is_trivially_copyable<T>::value,
		"Saved graphics records must be trivially copyable");
	const u32 size = (u32)sizeof(T);
	return fwrite(&size, sizeof(size), 1, f) == 1 &&
		fwrite(&value, sizeof(value), 1, f) == 1;
}

template <typename T>
static bool s3_read_record(FILE* f, T& value)
{
	static_assert(std::is_trivially_copyable<T>::value,
		"Saved graphics records must be trivially copyable");
	u32 size = 0;
	return fread(&size, sizeof(size), 1, f) == 1 && size == sizeof(T) &&
		fread(&value, sizeof(value), 1, f) == 1;
}

/**
 * Save state to a Virtual Machine State file.
 **/
int CS3Trio64::SaveState(FILE* f)
{
	if (!f || !vga.memory || !vga.svga_intf.vram_size)
		return -1;
	advance_scan(cSystem->device_access_time());
	const int res = CPCIDevice::SaveState(f);
	if (res)
		return res;

	// Only serialize guest state. The VRAM allocation belongs to this process.
	auto saved_state = state;
	auto saved_vga = vga;
	saved_state.memory = nullptr;
	saved_vga.memory = nullptr;
	const u8 io_state[] = { (u8)m_ioas,
		m_video_subsys_enable, m_setup_option_select_0102 };
	const u32 vram_size = (u32)vga.svga_intf.vram_size;

	if (fwrite(&s3_magic1, sizeof(s3_magic1), 1, f) != 1 ||
		fwrite(&s3_video_magic, sizeof(s3_video_magic), 1, f) != 1 ||
		!s3_write_record(f, saved_state) || !s3_write_record(f, saved_vga) ||
		!s3_write_record(f, s3) || !s3_write_record(f, svga) ||
		!s3_write_record(f, m_8514.ibm8514) || !s3_write_record(f, io_state) ||
		!s3_write_record(f, m_scan) ||
		fwrite(&vram_size, sizeof(vram_size), 1, f) != 1 ||
		fwrite(vga.memory, vram_size, 1, f) != 1 ||
		fwrite(&s3_magic2, sizeof(s3_magic2), 1, f) != 1)
	{
		printf("%s: Could not save graphics state.\n", devid_string);
		return -1;
	}
	printf("%s: Graphics registers and %u bytes of VRAM saved.\n",
		devid_string, vram_size);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CS3Trio64::RestoreState(FILE* f)
{
	if (!f || !vga.memory || !vga.svga_intf.vram_size)
		return -1;
	int res;
	{
		// Saved S3 aliases are loaded below. PCI replay must not normalize old
		// copies or mutate live graphics registers if the S3 section is rejected.
		struct ReplayGuard {
			bool& flag;
			bool previous;
			~ReplayGuard() { flag = previous; }
		} guard{m_replaying_pci_state, m_replaying_pci_state};
		m_replaying_pci_state = true;
		res = CPCIDevice::RestoreState(f);
	}
	if (res)
		return res;

	// Stage the complete graphics section so a short read or layout mismatch can't corrupt state.
	decltype(state) saved_state{};
	decltype(vga) saved_vga{};
	decltype(s3) saved_s3{};
	decltype(svga) saved_svga{};
	decltype(m_8514.ibm8514) saved_accel{};
	u8 io_state[3]{};
	ScanPosition saved_scan{};
	u32 magic = 0, video_magic = 0, vram_size = 0;
	if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != s3_magic1 ||
		fread(&video_magic, sizeof(video_magic), 1, f) != 1 ||
		video_magic != s3_video_magic ||
		!s3_read_record(f, saved_state) || !s3_read_record(f, saved_vga) ||
		!s3_read_record(f, saved_s3) || !s3_read_record(f, saved_svga) ||
		!s3_read_record(f, saved_accel) || !s3_read_record(f, io_state) ||
		!s3_read_record(f, saved_scan) ||
		fread(&vram_size, sizeof(vram_size), 1, f) != 1 ||
		vram_size != vga.svga_intf.vram_size ||
		saved_vga.svga_intf.vram_size != vga.svga_intf.vram_size ||
		saved_state.memsize != state.memsize || io_state[0] > 1 ||
		(io_state[1] & ~0x18) || (io_state[2] & ~0x01) ||
		!std::isfinite(saved_scan.character) || saved_scan.character < 0.0 ||
		saved_scan.character >= 516.0 || saved_scan.line >= 4098 ||
		saved_scan.field > 1 ||
		(saved_scan.dclk_source != 0 && saved_scan.dclk_source != 1 &&
		 saved_scan.dclk_source != 3))
	{
		printf("%s: Invalid or incompatible graphics state.\n", devid_string);
		return -1;
	}

	std::vector<u8> saved_vram(vram_size);
	if (fread(saved_vram.data(), vram_size, 1, f) != 1 ||
		fread(&magic, sizeof(magic), 1, f) != 1 || magic != s3_magic2)
	{
		printf("%s: Incomplete graphics state.\n", devid_string);
		return -1;
	}

	saved_state.memory = state.memory;
	saved_vga.memory = vga.memory;
	state = saved_state;
	vga = saved_vga;
	s3 = saved_s3;
	svga = saved_svga;
	m_8514.ibm8514 = saved_accel;
	m_ioas = io_state[0] != 0;
	m_video_subsys_enable = io_state[1];
	m_setup_option_select_0102 = io_state[2];
	m_scan = saved_scan;
	m_scan_time = cSystem->device_access_time();
	m_scan_initialized = true;
	// Preserve whether the caller stopped the VM. Normal System restore
	// resumes through start_threads; direct device restores remain runnable.
	memcpy(vga.memory, saved_vram.data(), vram_size);

	// Rebuild host rendering caches from the restored registers. 
	s3_define_video_mode();
	recompute_params();
	vga.dac.dirty = 1;
	if (m_gui_initialized)
		palette_update();
	else
		CVGA::palette_update();
	on_crtc_linear_regs_changed();
	state.last_bpp = 0;
	state.vga_mem_updated = true;
	m_frames_since_render = 0;
	m_last_refresh_time = std::chrono::steady_clock::time_point{};
	printf("%s: Graphics registers and %u bytes of VRAM restored.\n",
		devid_string, vram_size);
	return 0;
}

/**
 * Read from linear VRAM using a framebuffer-relative byte offset.
 **/
u32 CS3Trio64::mem_read(u32 address, int dsize)
{
	const u32 mv = s3_vram_mask();          // (memsize - 1)
	const u32 off = address & mv;

	switch (dsize) {
	case 8:
		return (u32)vga.memory[off];
	case 16:
		return (u32)vga.memory[off] | ((u32)vga.memory[(off + 1) & mv] << 8);
	case 32:
		return (u32)vga.memory[off] | ((u32)vga.memory[(off + 1) & mv] << 8) | ((u32)vga.memory[(off + 2) & mv] << 16) | ((u32)vga.memory[(off + 3) & mv] << 24);
	default:
		FAILURE(InvalidArgument, "Unsupported dsize");
	}
}

/**
 * Write to linear VRAM using a framebuffer-relative byte offset.
 **/
void CS3Trio64::mem_write(u32 address, int dsize, u32 data)
{
	const u32 mv = s3_vram_mask();         // (memsize - 1)
	const u32 off = address & mv;

	// Little-endian store into VRAM
	switch (dsize) {
	case 8:
		vga.memory[off] = (u8)data;
		break;
	case 16:
		vga.memory[off] = (u8)(data);
		vga.memory[(off + 1) & mv] = (u8)(data >> 8);
		break;
	case 32:
		vga.memory[off] = (u8)(data);
		vga.memory[(off + 1) & mv] = (u8)(data >> 8);
		vga.memory[(off + 2) & mv] = (u8)(data >> 16);
		vga.memory[(off + 3) & mv] = (u8)(data >> 24);
		break;
	default:
		FAILURE(InvalidArgument, "Unsupported dsize");
	}

	// Mark the affected tiles dirty so update() will serialize to the screen.
	state.vga_mem_updated = 1;
	if (vga.crtc.offset) {
		const unsigned nbytes = (dsize == 8) ? 1u : (dsize == 16 ? 2u : 4u);
		for (unsigned i = 0; i < nbytes; ++i) {
			const u32 p = (off + i) & mv;
			const u32 line = (vga.crtc.offset ? (p / vga.crtc.offset) : 0);
			const u32 col = (vga.crtc.offset ? (p % vga.crtc.offset) : 0);
			const unsigned xti = col / X_TILESIZE;
			const unsigned yti = line / Y_TILESIZE;
		}
	}
}


/**
 * Read from Legacy VGA Memory
 *
 * Calls vga_mem_read to read the data 1 byte at a time.
 **/
u32 CS3Trio64::legacy_read(u32 address, int dsize)
{
	// MMIO alias active ?
	if (s3.cr53 & 0x10) {
		const u32 base = s3_mmio_base_off(state);
		if (address >= base && address <= base + 0xFFFFu) {
			const u32 off = address - base;
			if (off < 0x8000) {
				// PIX_TRANS read — same as MAME
				switch (dsize) {
				case 8:  return (u32)AccelIORead(0xE2E8);
				case 16: return (u32)AccelIORead(0xE2E8) | ((u32)AccelIORead(0xE2E9) << 8);
				case 32: return (u32)AccelIORead(0xE2E8) | ((u32)AccelIORead(0xE2E9) << 8) |
					((u32)AccelIORead(0xE2EA) << 16) | ((u32)AccelIORead(0xE2EB) << 24);
				default: FAILURE(InvalidArgument, "Unsupported dsize");
				}
			}
			// Upper half: register reads via AccelIORead
			if (IsAccelPort(off))
				return AccelIORead(off, dsize);
		}
	}

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
 // --- Legacy VGA memory write with S3 MMIO alias support ---
void CS3Trio64::legacy_write(u32 address, int dsize, u32 data)
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
u32 CS3Trio64::rom_read(u32 address, int dsize)
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
	if (address < rom_max)
	{
		//printf("S3 rom read: %" PRIx64 ", %d, %" PRIx64 "\n", address, dsize,data);
	}
	else
	{
		//printf("S3 (BAD) rom read: %" PRIx64 ", %d, %" PRIx64 "\n", address, dsize,data);
	}
	return data;
}

/**
 * Read from I/O Port
 */
u32 CS3Trio64::io_read(u32 address, int dsize)
{
	if (!io_access_enabled(address, false))
		return dsize == 8 ? 0xffU : dsize == 16 ? 0xffffU : 0xffffffffU;
	if (address == 0x0102)
	{
		// Only the option byte is implemented; unused read lanes float high.
		switch (dsize) {
		case 8: return m_setup_option_select_0102;
		case 16: return 0xff00U | m_setup_option_select_0102;
		case 32: return 0xffffff00U | m_setup_option_select_0102;
		default: FAILURE(InvalidArgument, "Unsupported dsize");
		}
	}
	u32 data = 0;
	// Always intercept S3 8514/A-style ports. If the port block is not enabled
	// yet (CR40 == 0), hardware behaves benignly: reads return bus pull-ups,
	// writes are ignored.
	if (IsAccelPort(address)) {
		const bool ge_enabled = (m_crtc_map.read_byte(0x40) & 0x01) != 0;
		if (!ge_enabled) {
			switch (dsize) {
			case 8: return 0xFF;
			case 16: return 0xFFFF;
			case 32: return 0xFFFFFFFF;
			default: FAILURE(InvalidArgument, "Unsupported dsize");
			}
		}
		return AccelIORead(address, dsize);
	}

	if (dsize != 8)
		FAILURE(InvalidArgument, "Unsupported dsize");


	switch (address)
	{
	case 0x3c0:
		data = atc_address_r(0);
		break;

	case 0x3c1:
		data = atc_data_r(0);
		break;

	case 0x3c2:
		data = read_b_3c2();
		break;

	case 0x3c4:
		data = sequencer_address_r(0);
		break;

	case 0x3c5:
		if (vga.sequencer.index > 0x08 && vga.sequencer.data[0x08] != 0x06)
			data = vga.sequencer.data[vga.sequencer.index];
		else
			data = sequencer_data_r(0);
		break;

	case 0x3c6:
		data = ramdac_mask_r(0);
		break;

	case 0x3c7:
		data = ramdac_state_r(0);
		break;

	case 0x3c8:
		data = ramdac_write_index_r(0);
		break;

	case 0x3c9:
		data = ramdac_data_r(0);
		// DB014-B 14-49: valid palette reads expose six-bit components, even
		// from older saved bytes. Preserve undefined reads during write mode.
		if (vga.dac.read) data &= 0x3f;
		break;

	case 0x3ca:
		data = read_b_3ca();
		break;

	case 0x3cc:
		data = miscellaneous_output_r(0);
		break;

	case 0x3ce:
		data = gc_address_r(0);
		break;

	case 0x3cf:
		data = gc_data_r(0);
		break;

	case 0x3b4:
	case 0x3d4:
		data = crtc_address_r(0);
		break;

	case 0x3b5:
	case 0x3d5:
		data = crtc_data_r(0);
		break;

	case 0x3ba:
	case 0x3da:
	{
		data = scan_status(cSystem->device_access_time());
		vga.attribute.state = 0;  // ATC flip-flop reset
		break;
	}

	case 0x3bb: /* Feature Control (mono) readback; mirror 3CA behavior */
		data = read_b_3ca();
		break;
	case 0x3db: /* Feature Control (color) readback; same treatment */
		data = read_b_3ca();
		break;

	case 0x3b6:
	case 0x3b7:
	case 0x3b8:
	case 0x3b9:
	case 0x3d6:
	case 0x3d7:
	case 0x3d8:
	case 0x3d9:
		data = 0xFF;  // open bus
		break;

	default:
		printf("S3: Unhandled io port %x read\n", address);
	}

	return data;
}

/**
 * Write to I/O Port
 *
 * Calls io_write_b to write the data 1 byte at a time.
 */
void CS3Trio64::io_write(u32 address, int dsize, u32 data)
{
	if (IsAccelPort(address) && !normal_access_enabled())
		return;
	// 8514/A-style accel window (S3 engine). Intercept first, and swallow writes
	// until CR40 enables the port block (to avoid falling through to VGA path).
	if (IsAccelPort(address)) {
		const bool ge_enabled = (m_crtc_map.read_byte(0x40) & 0x01) != 0;
		if (!ge_enabled) {
			// Ignore early probes safely (hardware no-op)
			return;
		}
#ifdef DEBUG_VGA
		printf("ACCEL HIT @%04X dsize=%d data=%08X\n",
			(unsigned)address, dsize, (unsigned)data);
#endif
		switch (dsize) {
		case 8:
			AccelIOWrite(address, (u8)data);
			return;
		case 16:
			if (address == 0x42e8) {
				// SUBSYS_CNTL has action fields: apply both lanes once.
				m_8514.ibm8514_subcontrol_w(u16(data));
				s3.mmio_42e8 = m_8514.ibm8514_subcontrol_r();
				return;
			}
			AccelIOWrite(address + 0, (u8)(data & 0xFF));
			AccelIOWrite(address + 1, (u8)((data >> 8) & 0xFF));
			return;
		case 32:
			AccelIOWrite(address + 0, (u8)((data >> 0) & 0xFF));
			AccelIOWrite(address + 1, (u8)((data >> 8) & 0xFF));
			AccelIOWrite(address + 2, (u8)((data >> 16) & 0xFF));
			AccelIOWrite(address + 3, (u8)((data >> 24) & 0xFF));
			return;
		default:
			FAILURE(InvalidArgument, "Unsupported dsize");
		}
	}

	//  printf("S3 io write: %" PRIx64 ", %d, %" PRIx64 "   \n", address+VGA_BASE, dsize, data);
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
		printf("S3 Weird Size io write: %" PRIx32 ", %d, %" PRIx32 "   \n", address, dsize, data);
		io_write_b(address, (u8)data);
		io_write_b(address + 1, (u8)(data >> 8));
		io_write_b(address + 2, (u8)(data >> 16));
		io_write_b(address + 3, (u8)(data >> 24));
		break;

	default:
#ifdef DEBUG_VGA
		printf("S3 Weird Size io write: %" PRIx32 ", %d, %" PRIx32 "   \n", address, dsize, data);
#endif
		FAILURE(InvalidArgument, "Weird IO size");
	}
}

// Shared by normal target writes and already-qualified passive writes.
// Keep access/lock checks at the caller so observers use captured eligibility.
void CS3Trio64::ramdac_write_b(u32 address, u8 data)
{
	switch (address)
	{
	case 0x3c6: ramdac_mask_w(0, data); break;
	case 0x3c7: ramdac_read_index_w(0, data); break;
	case 0x3c8: ramdac_write_index_w(0, data); break;
	case 0x3c9: ramdac_data_w(0, data); break;
	}
}

/**
 * Write one byte to a VGA I/O port.
 **/
void CS3Trio64::io_write_b(u32 address, u8 data)
{
	if (!io_access_enabled(address, true))
		return;
	// Finish elapsed scan time with the old registers before a timing write.
	// Every claimant in this PCI transaction receives the same sampled instant.
	if (address == 0x3c2 || address == 0x3c5 ||
		address == 0x3b5 || address == 0x3d5)
		advance_scan(cSystem->device_access_time());
	switch (address)
	{
	case 0x3c0:
	{
		bool was_index_phase = (vga.attribute.state == 0);
		// Snapshot previous video-enabled state BEFORE the MAME canonical write
		bool prev_ve = atc_video_enabled();
		atc_address_data_w(0, data);
		if (was_index_phase) {
			// Detect video enable/disable transitions from MAME canonical source
			bool new_ve = atc_video_enabled();
			if (!new_ve && prev_ve) {
				m_output.display().lock();
				m_output.display().clear_screen();
				m_output.display().unlock();
			}
			else if (new_ve && !prev_ve) {
				redraw_area(0, 0, old_iWidth, old_iHeight);
			}
		}
		break;
	}

	case 0x3c2:
		write_b_3c2(data);
		m_ioas = bool(BIT(data, 0));
		break;

	case 0x3c3:
	case 0x46E8:
		// Address selection and access were checked above; reserved bits read 0.
		m_video_subsys_enable = data & 0x18;
		break;

	case 0x3c4:
		sequencer_address_w(0, data);
		break;

	case 0x3c5:
		// PLL lock gate: SR09+ requires SR08 == 0x06
		if (vga.sequencer.index > 0x08 && vga.sequencer.data[0x08] != 0x06)
			break;
		// SR1A/SR1B: not in sequencer_map, but in 86box
		if (vga.sequencer.index == 0x1a) { s3.sr1a = data; break; }
		if (vga.sequencer.index == 0x1b) { s3.sr1b = data; break; }
		sequencer_data_w(0, data);
		break;

	case 0x3c6:
	case 0x3c7:
	case 0x3c8:
	case 0x3c9:
		// CR33.4 locks all RAMDAC writes, including 3C7 (DB014-B 12-2).
		if (!(s3.cr33 & 0x10))
			ramdac_write_b(address, data);
		break;

	case 0x3ce:
		gc_address_w(0, data);
		break;

	case 0x3cf:
		gc_data_w(0, data);
		break;

	case 0x3ba:
	case 0x3da:
		feature_control_w(0, data);
		break;

	case 0x3b4:
	case 0x3d4:
		vga.crtc.index = data & 0x7f;
		break;

	case 0x3b5:
	case 0x3d5:
		crtc_data_w(0, data);
		break;

	case 0x3bb:
		break;

	case 0x3b6:
	case 0x3b7:
	case 0x3b8:
	case 0x3b9:
	case 0x3d6:
	case 0x3d7:
	case 0x3d8:
	case 0x3d9:
		// Dead ports — 32-bit writes to the CRTC pair (3D4/3D5) spill here.
		// Real hardware silently ignores them.
		break;

	case 0x0102:
		m_setup_option_select_0102 = data & 0x01;
		break;

	default:
#ifdef DEBUG_VGA
		printf("\nFAILURE ON BELOW LISTED PORT BINARY VALUE=" PRINTF_BINARY_PATTERN_INT8 " HEX VALUE=0x%02x\n", PRINTF_BYTE_TO_BINARY_INT8(data), data);
#endif
		FAILURE_1(NotImplemented, "Unhandled port %x write", address);
	}
}

/**
 * Write to the VGA Miscellaneous Output Register (0x3c2)
 *
 * \code
 * +-+-+-+-+---+-+-+
 * |7|6|5| |3 2|1|0|
 * +-+-+-+-+---+-+-+
 *  ^ ^ ^    ^  ^ ^
 *  | | |    |  | +- 0: I/OAS -- Input/Output Address Select: Selects the CRT
 *  | | |    |  |       controller addresses.
 *  | | |    |  |         0: Compatibility with monochrome adapter
 *  | | |    |  |            (0x3b4,0x3b5,0x03ba)
 *  | | |    |  |         1: Compatibility with color graphics adapter (CGA)
 *  | | |    |  |            (0x3d4,0x3d5,0x03da)
 *  | | |    |  +--- 1: RAM Enable: Controls access from the system:
 *  | | |    |            0: Disables access to the display buffer
 *  | | |    |            1: Enables access to the display buffer
 *  | | |    +--- 2..3: Clock Select: Controls the selection of the dot clocks
 *  | | |               used in driving the display timing:
 *  | | |                 00: Select 25 Mhz clock (320/640 pixel wide modes)
 *  | | |                 01: Select 28 Mhz clock (360/720 pixel wide modes)
 *  | | |                 10: Undefined (possible external clock)
 *  | | |                 11: Undefined (possible external clock)
 *  | | +----------- 5: Odd/Even Page Select: Selects the upper/lower 64K page
 *  | |                 of memory when the system is in an even/odd mode.
 *  | |                   0: Selects the low page.
 *  | |                   1: Selects the high page.
 *  | +------------- 6: Horizontal Sync Polarity
 *  |                     0: Positive sync pulse.
 *  |                     1: Negative sync pulse.
 *  +--------------- 7: Vertical Sync Polarity
 *                        0: Positive sync pulse.
 *                        1: Negative sync pulse.
 * \endcode
 **/
void CS3Trio64::write_b_3c2(u8 value)
{
	// ES40 extension: CR34 bit7 locks clock select bits
	if (m_crtc_map.read_byte(0x34) & 0x80) {
		// Preserve current clock_select (bits 3:2), take everything else from value
		value = (value & ~0x0C) | (vga.miscellaneous_output & 0x0C);
	}

	// MAME canonical store (flat byte)
	vga.miscellaneous_output = value;
	load_dclk();

#if DEBUG_VGA_NOISY
	printf("io write 3c2: misc_output = 0x%02x\n", value);
	printf("  color_emulation = %u, enable_ram = %u, clock_select = %u\n",
		(unsigned)state.misc_output.color_emulation,
		(unsigned)state.misc_output.enable_ram,
		(unsigned)state.misc_output.clock_select);
	printf("  select_high_bank = %u, horiz_sync_pol = %u, vert_sync_pol = %u\n",
		(unsigned)state.misc_output.select_high_bank,
		(unsigned)state.misc_output.horiz_sync_pol,
		(unsigned)state.misc_output.vert_sync_pol);
#endif
}

/**
 * Read from the VGA Input Status register (0x3c2)
 *
 * \code
 * +-----+-+-------+
 * |     |4|       |
 * +-----+-+-------+
 *        ^
 *        +--------- 4: Switch Sense:
 *                      Returns the status of the four sense switches as selected by the
 *                      Clock Select field of the Miscellaneous Output Register (See
 *                      CCirrus::write_b_3c2)
 * \endcode
 **/
u8 CS3Trio64::read_b_3c2()
{
	u8 res = 0x60; // is VGA (bits 5-6 set)

	// Sense bit readback: select which of 4 sense switches based on clock select
	// MAME: const u8 sense_bit = (3 - (vga.miscellaneous_output >> 2)) & 3;
	//        if(BIT(m_input_sense->read(), sense_bit)) res |= 0x10;
	const u8 sense_bit = (3 - ((vga.miscellaneous_output >> 2) & 3)) & 3;
	if (BIT(0x0F, sense_bit))  // all sense pins active
		res |= 0x10;

	res |= vga.crtc.irq_latch << 7;
	return res;
}

u8 CS3Trio64::read_b_3ca()
{
	return 0;
}

u8 CS3Trio64::get_actl_palette_idx(u8 index)
{
	return atc_palette(index);
}

void CS3Trio64::redraw_area(unsigned x0, unsigned y0, unsigned width,
	unsigned height)
{
	if ((width == 0) || (height == 0))
		return;

	state.vga_mem_updated = 1;
}

void CS3Trio64::update(void)
{
	unsigned iWidth = 0, iHeight = 0;

	/* no screen update necessary
	   Trio32/Trio64: SR0 reset bits are not functional
	   Gate on ATC video enable and SR1 "Screen Off" */
	// Setup/sleep controls CPU access; existing video output continues (14-47).
	if (!atc_video_enabled())
		return;

	const bool screen_off = (vga.sequencer.data[1] & 0x20) != 0; // SR1 bit5
	if (screen_off)
		return;

	auto now = std::chrono::steady_clock::now();
	auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_last_refresh_time).count();

	if (elapsed_ms < (long long)timing.refresh_interval_ms)
		return;
	m_last_refresh_time = now;

	const uint8_t cur_mode = pc_vga_choosevideomode();

	if (cur_mode == SCREEN_OFF)
	{
		state.vga_mem_updated = 0;
		return;
	}

	// Dirty-gate: re-rasterize + re-upload only when something visible changed. vga_mem_updated covers
	// VRAM + CRTC text-cursor + palette + mode writes; the S3 hardware cursor (mode/pos/data-addr) is not
	// flagged, so fold it into a signature. Force a refresh every few frames so the cursor / blinking text
	// still animate on an otherwise static screen; tick_frame() keeps the blink counter advancing on the
	// skip path so blink timing stays correct.
	const int kBlinkRefreshFrames = 8;   // >= 2x the ~1.9 Hz VGA blink toggle at a 60 Hz refresh
	const uint64_t cursor_sig = ((uint64_t) s3.cursor_mode << 56)
	                          | ((uint64_t) s3.cursor_start_addr << 24)
	                          | ((uint64_t) (s3.cursor_x & 0x7FF) << 12)
	                          | (uint64_t) (s3.cursor_y & 0x7FF);
	if (!state.vga_mem_updated && cursor_sig == m_last_cursor_sig
	    && ++m_frames_since_render < kBlinkRefreshFrames)
	{
		screen().tick_frame();   // keep cursor/text-blink timing alive while skipping the render
		return;
	}
	m_frames_since_render = 0;
	m_last_cursor_sig = cursor_sig;

	vga.crtc.start_addr = vga.crtc.start_addr_latch; // FIXME: Figure out proper handling, but makes BSD happy again....
	vga.attribute.pel_shift = vga.attribute.pel_shift_latch;

	determine_screen_dimensions(&iHeight, &iWidth);

	if (iWidth == 0 || iHeight == 0)
		return;

	// Update screen shim's visible area
	screen().set_visible_area(iWidth, iHeight);

	// Ensure bitmap is large enough
	m_render_bitmap.allocate(iWidth, iHeight);

	// Render via MAME's screen_update pipeline
	rectangle clip = m_render_bitmap.cliprect();
	screen_update(m_render_bitmap, clip);

	// Tick the frame counter (for cursor blink)
	screen().tick_frame();

	// MAME always produces ARGB32 — tell SDL we're in 32bpp mode.
	if (state.last_bpp != 32 || iWidth != old_iWidth || iHeight != old_iHeight)
	{
		m_output.display().dimension_update(iWidth, iHeight, 0, 0, 32);
		old_iWidth = iWidth;
		old_iHeight = iHeight;
		state.last_bpp = 32;
	}

	m_output.display().graphics_frame_update(m_render_bitmap.raw(), iWidth, iHeight);

	state.vga_mem_updated = 0;
}

void CS3Trio64::determine_screen_dimensions(unsigned* piHeight,
	unsigned* piWidth)
{
	int ai[0x20];
	int i;
	int h;
	int v;
	for (i = 0; i < 0x20; i++)
		ai[i] = m_crtc_map.read_byte(i);

	h = (ai[1] + 1) * (seq_dotperchar() ? 8 : 9) / timing.divisor;
	v = (ai[18] | ((ai[7] & 0x02) << 7) | ((ai[7] & 0x40) << 3)) + 1;
	// S3 CR5D extends H* with bit8 (0x100) and CR5E extends V* with bit10 (0x400)
	if (m_crtc_map.read_byte(0x5D) & 0x02) h |= 0x400;  // multiplied by 8/2 = 4
	if (m_crtc_map.read_byte(0x5E) & 0x02) v |= 0x400;
	v *= (get_interlace_mode() + 1);  // interlaced mode

	if (vga.gc.shift256)
	{
		// was shift_reg == 2 mode 13h / 256-color byte mode
		// chain_four vs modeX 
		*piWidth = h;
		*piHeight = v;
	}
	else if (vga.gc.shift_reg)
	{
		// was shift_reg == 1 CGA 4-color interleave
		if (x_dotclockdiv2())
			h <<= 1;
		*piWidth = h;
		*piHeight = v;
	}
	else
	{
		// was shift_reg == 0 standard VGA planar / EGA
		*piWidth = 640;
		*piHeight = 480;
		if (m_crtc_map.read_byte(0x06) == 0xBF)
		{
			if (m_crtc_map.read_byte(0x17) == 0xA3 && m_crtc_map.read_byte(0x14) == 0x40
				&& m_crtc_map.read_byte(0x09) == 0x41)
			{
				*piWidth = 320;
				*piHeight = 240;
			}
			else
			{
				if (x_dotclockdiv2())
					h <<= 1;
				*piWidth = h;
				*piHeight = v;
			}
		}
		else if ((h >= 640) && (v >= 480))
		{
			*piWidth = h;
			*piHeight = v;
		}
	}
}

inline uint32_t CS3Trio64::s3_vram_mask() const
{
	const uint32_t sz = vga.svga_intf.vram_size ? vga.svga_intf.vram_size : (8u * 1024u * 1024u);
	return sz - 1u;
}

void CS3Trio64::palette_update()
{
	CVGA::palette_update();

	for (int i = 0; i < 256; i++) {
		// pal6bit: expand 6-bit color to 8-bit
		u8 r = (vga.dac.color[3 * (i & vga.dac.mask) + 0] & 0x3f);
		u8 g = (vga.dac.color[3 * (i & vga.dac.mask) + 1] & 0x3f);
		u8 b = (vga.dac.color[3 * (i & vga.dac.mask) + 2] & 0x3f);
		// Expand 6-bit to 8-bit: (val << 2) | (val >> 4)
		r = (r << 2) | (r >> 4);
		g = (g << 2) | (g >> 4);
		b = (b << 2) | (b >> 4);
		m_output.display().palette_change((unsigned)i, (unsigned)r, (unsigned)g, (unsigned)b);
	}
}

uint8_t CS3Trio64::get_video_depth()
{
	switch (pc_vga_choosevideomode())
	{
	case VGA_MODE:
	case RGB8_MODE:    return 8;
	case RGB15_MODE:
	case RGB16_MODE:   return 16;
	case RGB24_MODE:
	case RGB32_MODE:   return 32;
	default:           return 0;
	}
}

void CS3Trio64::mem_linear_w(uint32_t offset, uint8_t data)
{
	CVGA::mem_linear_w(offset, data);
	state.vga_mem_updated = 1;
}


// Draws the 64x64 S3 hardware graphics cursor over a pre-rendered framebuffer.
// Supports Windows mode and X11 mode (CR55 bit 4), and all color depths.
void CS3Trio64::s3_draw_hardware_cursor(
	uint32_t* pixels, int pitch_px,
	int clip_width, int clip_height,
	uint8_t cur_mode)
{
	// Only draw if cursor is enabled
	if (!(s3.cursor_mode & 0x01))
		return;

	// Cursor only works in VGA or SVGA modes
	if (cur_mode == SCREEN_OFF || cur_mode == TEXT_MODE ||
		cur_mode == MONO_MODE || cur_mode == CGA_MODE ||
		cur_mode == EGA_MODE)
		return;

	uint16_t cx = s3.cursor_x & 0x07FF;
	uint16_t cy = s3.cursor_y & 0x07FF;

	// Start address is in units of 1024 bytes
	uint32_t src = (uint32_t)s3.cursor_start_addr * 1024;

	// Decode foreground/background colors 
	uint32_t bg_col, fg_col;

	auto decode_rgb16 = [](const uint8_t* raw) -> uint32_t {
		uint32_t datax = raw[0] | (raw[1] << 8);
		int r = (datax & 0xF800) >> 11;
		int g = (datax & 0x07E0) >> 5;
		int b = (datax & 0x001F) >> 0;
		r = (r << 3) | (r & 0x7);
		g = (g << 2) | (g & 0x3);
		b = (b << 3) | (b & 0x7);
		return 0xFF000000u | (r << 16) | (g << 8) | b;
		};

	auto decode_rgb24 = [](const uint8_t* raw) -> uint32_t {
		uint32_t datax = raw[0] | (raw[1] << 8) | (raw[2] << 16);
		int r = (datax & 0xFF0000) >> 16;
		int g = (datax & 0x00FF00) >> 8;
		int b = (datax & 0x0000FF) >> 0;
		return 0xFF000000u | (r << 16) | (g << 8) | b;
		};

	switch (cur_mode)
	{
	case RGB15_MODE:
	case RGB16_MODE:
		bg_col = decode_rgb16(s3.cursor_bg);
		fg_col = decode_rgb16(s3.cursor_fg);
		break;

	case RGB24_MODE:
	case RGB32_MODE:
		bg_col = decode_rgb24(s3.cursor_bg);
		fg_col = decode_rgb24(s3.cursor_fg);
		break;

	case RGB8_MODE:
	default:
		bg_col = pen(s3.cursor_bg[0]);
		fg_col = pen(s3.cursor_fg[0]);
		break;
	}

	// Draw the 64x64 cursor bitmap 
	// Cursor data: 64 rows, each row = 16 bytes (4 words of 16 bits A + 16 bits B)
	// Pattern origin offset from CR4E/CR4F
	const int pat_x = s3.cursor_pattern_x & 0x3F;
	const int pat_y = s3.cursor_pattern_y & 0x3F;

	for (int y = 0; y < 64; y++)
	{
		int screen_y = cy + y - pat_y;
		if (screen_y < 0 || screen_y >= clip_height)
		{
			// Still need to advance src through the row's cursor data
			// Each row: 4 groups of 4 bytes = 16 bytes
			// But we advance per-group below, so just skip
			// We need 4 groups * 4 bytes = 16 bytes per row
			src += 16; // skip this row's data
			continue;
		}

		uint32_t* dst = pixels + (screen_y * pitch_px);
		uint32_t row_src = src;

		for (int x = 0; x < 64; x++)
		{
			// Each 16-pixel group uses 4 bytes: 2 bytes for A-plane, 2 for B-plane
			// Bit extraction from MAME:
			uint16_t bita = (vga.memory[(row_src + 1) % vga.svga_intf.vram_size] |
				((vga.memory[(row_src + 0) % vga.svga_intf.vram_size]) << 8))
				>> (15 - (x % 16));
			uint16_t bitb = (vga.memory[(row_src + 3) % vga.svga_intf.vram_size] |
				((vga.memory[(row_src + 2) % vga.svga_intf.vram_size]) << 8))
				>> (15 - (x % 16));
			uint8_t val = ((bita & 0x01) << 1) | (bitb & 0x01);

			int screen_x = cx + x - pat_x;
			if (screen_x >= 0 && screen_x < clip_width)
			{
				if (s3.extended_dac_ctrl & 0x10)
				{
					// X11 mode
					switch (val)
					{
					case 0x00: /* no change - transparent */   break;
					case 0x01: /* no change - transparent */   break;
					case 0x02: dst[screen_x] = bg_col;        break;
					case 0x03: dst[screen_x] = fg_col;        break;
					}
				}
				else
				{
					// Windows mode
					switch (val)
					{
					case 0x00: dst[screen_x] = bg_col;            break;
					case 0x01: dst[screen_x] = fg_col;            break;
					case 0x02: /* screen data - no change */       break;
					case 0x03: dst[screen_x] = ~(dst[screen_x]);  break; // invert
					}
				}
			}

			// Advance source pointer every 16 pixels
			if (x % 16 == 15)
				row_src += 4;
		}
		src = row_src; // advance to next row
	}
}
