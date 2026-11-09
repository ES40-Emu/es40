/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
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

#include "StdAfx.h"
#include "GloriaSynergy.h"
#include "config/Configurator.h"
#include "system/System.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

class CGloriaDisplayLock
{
public:
	explicit CGloriaDisplayLock(bx_gui_c& display) : m_display(display)
	{
		m_display.lock();
	}

	~CGloriaDisplayLock() { m_display.unlock(); }

private:
	bx_gui_c& m_display;
};

static uint32_t gloria_setting(
	CConfigurator* cfg, const char* key, uint32_t def)
{
	const char* text = cfg->get_text_value(key, nullptr);
	if (!text || !*text)
		return def;
	char* end = nullptr;
	errno = 0;
	const unsigned long long n = std::strtoull(text, &end, 0);
	if (errno || *end || text[0] == '-' || n > 0xffffffffULL)
		throw std::runtime_error(
			std::string("Invalid gloria integer setting: ") + key);
	return static_cast<uint32_t>(n);
}

static CPermedia2::Options gloria_options(CConfigurator* c)
{
	// Enable the on-chip VGA and its fixed decode for this VGA board profile.
	// These are emulator configuration straps, not a physical-board capture.
	CPermedia2::Options o;
	o.chip_config = gloria_setting(c, "chip_config", 6);
	o.mem_control = gloria_setting(c, "mem_control", 0);
	o.mem_config = gloria_setting(c, "mem_config", 0);
	o.input_fifo_entries =
		gloria_setting(c, "fifo_entries", o.input_fifo_entries);
	if (c->get_bool_value("subsystem_from_rom", false))
		o.chip_config |= 0x1000;
	return o;
}

static uint16_t gloria_setting16(
	CConfigurator* c, const char* key, uint16_t def)
{
	const auto v = gloria_setting(c, key, def);
	if (v > 65535)
		throw std::runtime_error(
			std::string("16-bit gloria setting overflow: ") + key);
	return static_cast<uint16_t>(v);
}

static void gloria_write_u32(FILE* f, uint32_t v)
{
	uint8_t b[4];
	for (unsigned i = 0; i < 4; ++i)
		b[i] = uint8_t(v >> (8 * i));
	if (fwrite(b, 1, 4, f) != 4)
		throw std::runtime_error("Snapshot write failed");
}

static uint32_t gloria_read_u32(FILE* f)
{
	uint8_t b[4];
	if (fread(b, 1, 4, f) != 4)
		throw std::runtime_error("Snapshot read failed");
	return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
		(uint32_t(b[3]) << 24);
}

static bool gloria_vga_window_offset(u32& address, unsigned selection)
{
	switch (selection & 3)
	{
	case 1:
		return address < 0x10000;
	case 2:
		if (address < 0x10000 || address >= 0x18000)
			return false;
		address -= 0x10000;
		break;
	case 3:
		if (address < 0x18000 || address >= 0x20000)
			return false;
		address -= 0x18000;
		break;
	default:
		return address < 0x20000;
	}
	return true;
}

// VGA register maps

// vga_device's indexed port handlers dispatch here for both legacy I/O and the
// region-zero VGA aliases. Display invalidation is handled by the card worker.
void CGloriaSynergy::crtc_map(address_map& map)
{
	unimplemented_map(map, "VGA CRTC register");
	vga_device::crtc_map(map);
}

void CGloriaSynergy::sequencer_map(address_map& map)
{
	unimplemented_map(map, "VGA sequencer register");
	// These frontends mask stored bytes, including direct snapshot writes.
	// Limit the canonical map to SR00-04 so unknown indices keep diagnostics.
	auto standard = std::make_shared<address_map>(5);
	vga_device::sequencer_map(*standard);
	map(0x00, 0x04).lrw8(
		NAME([standard](offs_t index) {
			return standard->read_byte(index);
		}),
		NAME(([this, standard](offs_t index, u8 data) {
			static constexpr u8 masks[] = {0x03, 0x3f, 0x0f, 0x3f, 0x0e};
			vga.sequencer.data[index] = data & masks[index];
			standard->write_byte(index, data);
		})));
	map(0x05, 0x05).lr8(NAME([this](offs_t) {
		return vga.sequencer.data[5];
	}));
	// TVP4020 Hardware Reference Manual, issue 6, section 3.5.3, p.61.
	// Host memory/DAC gating, display selection and extended DAC addressing
	// are consumed by the corresponding card access paths. Native output is
	// enabled separately through VideoControl, not the VGA VTG control bit.
	map(0x05, 0x05).lw8(NAME([this](offs_t, u8 data) {
		vga.sequencer.data[5] = data & 0x7f;
	}));
}

void CGloriaSynergy::gc_map(address_map& map)
{
	unimplemented_map(map, "VGA graphics controller register");
	vga_device::gc_map(map);
	// TVP4020 HRM section 3.5.4, p.62. The display-start extension is write-only.
	map(0x09, 0x09)
		.lrw8(
			NAME([this](offs_t) {
				return u8(m_mode640 & 0xbf);
			}),
			NAME([this](offs_t, u8 data) {
				m_mode640 = data;
				vga.crtc.start_addr_latch =
					(vga.crtc.start_addr_latch & ~0x10000u) |
					(u32(data & 0x40) << 10);
			}));
}

void CGloriaSynergy::attribute_map(address_map& map)
{
	unimplemented_map(map, "VGA attribute register");
	vga_device::attribute_map(map);
	// Palette reads remain available when PAS disables CPU writes.
	map(0x20, 0x2f).lr8(NAME([this](offs_t offset) {
		return vga.attribute.data[offset];
	}));
	// These frontends retain mode-control bits 6/7 and mask reserved bit 4.
	map(0x10, 0x10).mirror(0x20).lw8(NAME([this](offs_t, u8 data) {
		vga.attribute.data[0x10] = data & 0xef;
	}));
	// Color select does not change the pending horizontal panning value.
	map(0x14, 0x14).mirror(0x20).lw8(NAME([this](offs_t, u8 data) {
		vga.attribute.data[0x14] = data & 0xf;
	}));
}

address_map& CGloriaSynergy::space(int spacenum)
{
	switch (spacenum)
	{
	case MAIN_IF_REG:
	case CRTC_REG:
	case SEQ_REG:
	case GC_REG:
	case ATC_REG:
		return vga_device::space(spacenum);
	default:
		throw std::invalid_argument("Invalid GLoria VGA register space");
	}
}

// VGA memory and display

u8 CGloriaSynergy::mem_r(offs_t address)
{
	const u32 bank = (m_mode640 & 0x80)
		? ((m_mode640 >> ((address & 0x10000) ? 3 : 0)) & 7) * 0x10000u
		: 0;
	if (!gloria_vga_window_offset(address, vga.gc.memory_map_sel))
		return 0xff;
	if (m_mode640 & 0x80)
		address &= 0xffff;
	unsigned selected = vga.gc.read_map_sel & 3;
	if (vga.sequencer.data[4] & 8)
	{
		// Keep packed chain-four bytes in the layout consumed by vga_device scanout.
		for (unsigned plane = 0; plane < 4; ++plane)
			vga.gc.latch[plane] = vga.memory[bank + (address & ~3u) + plane];
		selected = address & 3;
	}
	else if (!(vga.sequencer.data[4] & 4) && vga.gc.host_oe)
	{
		// vga_device text stores character/attribute pairs together; fonts stay planar.
		vga.gc.latch[0] = vga.memory[bank + (address & ~1u)];
		vga.gc.latch[1] = vga.memory[bank + (address & ~1u) + 1];
		for (unsigned plane = 2; plane < 4; ++plane)
			vga.gc.latch[plane] =
				vga.memory[bank + (address >> 1) + plane * 0x10000];
		selected = (vga.gc.read_map_sel & 2) | (address & 1);
	}
	else
	{
		const u32 offset = bank + (address & 0xffff);
		for (unsigned plane = 0; plane < 4; ++plane)
			vga.gc.latch[plane] = vga.memory[offset + plane * 0x10000];
	}
	if (!vga.gc.read_mode)
		return vga.gc.latch[selected];
	u8 result = 0xff;
	for (unsigned plane = 0; plane < 4; ++plane)
		if (vga.gc.color_dont_care & (1 << plane))
			result &=
				u8(vga.gc.latch[plane] ^
					((vga.gc.color_compare & (1 << plane)) ? 0 : 0xff));
	return result;
}

void CGloriaSynergy::mem_w(offs_t address, u8 data)
{
	const u32 bank = (m_mode640 & 0x80)
		? ((m_mode640 >> ((address & 0x10000) ? 3 : 0)) & 7) * 0x10000u
		: 0;
	if (!gloria_vga_window_offset(address, vga.gc.memory_map_sel))
		return;
	if (m_mode640 & 0x80)
		address &= 0xffff;
	if (vga.sequencer.data[4] & 8)
	{
		if (vga.sequencer.map_mask & (1 << (address & 3)))
			vga.memory[bank + address] = vga_latch_write(address & 3, data);
		return;
	}
	if (!(vga.sequencer.data[4] & 4) && vga.gc.host_oe)
	{
		const unsigned parity = address & 1;
		if (vga.sequencer.map_mask & (1 << parity))
			vga.memory[bank + address] = vga_latch_write(parity, data);
		if (vga.sequencer.map_mask & (1 << (parity + 2)))
			vga.memory[bank + (address >> 1) + (parity + 2) * 0x10000] =
				vga_latch_write(parity + 2, data);
		return;
	}
	const u32 offset = bank + (address & 0xffff);
	for (unsigned plane = 0; plane < 4; ++plane)
		if (vga.sequencer.map_mask & (1 << plane))
			vga.memory[offset + plane * 0x10000] = vga_latch_write(plane, data);
}

void CGloriaSynergy::palette_update()
{
	for (unsigned i = 0; i < 256; ++i)
		m_pen_table[i] = m_permedia2.palette_color(i);
}

void CGloriaSynergy::recompute_params()
{
	// The next display_frame uses decomposed CRTC fields directly.
	vga.dac.dirty = 1;
}

CPermedia2::Frame CGloriaSynergy::render_frame()
{
	std::lock_guard<std::mutex> frame_guard(m_frame_mutex);
	std::unique_lock<std::recursive_mutex> bus_guard(
		cSystem->get_device_bus_mutex());
	if (m_pause.load())
		return {};
	// ChipConfig can disable SVGA entirely; otherwise SR5 bit 3 selects the
	// VGA or graphics-processor display (SLAU011A, section 5.2.3). Native timing
	// and blanking follow VideoControl in scanout(), not SR5's VGA VTG control.
	if (!(m_permedia2.peek(CPermedia2::ChipConfig) & 2) ||
		!(vga.sequencer.data[5] & 8))
	{
		m_permedia2.capture_scanout(m_scanout);
		bus_guard.unlock();
		return CPermedia2::scanout(m_scanout);
	}
	// vga_device renders live CRTC state and updates text-cursor/palette state.
	// Keep that legacy path serialized until it has its own captured renderer.
	if (!vga.crtc.sync_en || (vga.sequencer.data[0] & 3) != 3 ||
		(vga.sequencer.data[1] & 0x20) || !(vga.attribute.index & 0x20) ||
		!vga.crtc.maximum_scan_line)
		return {};
	CPermedia2::Frame frame;
	frame.width = (vga.crtc.horz_disp_end + 1) *
		((vga.gc.alpha_dis || (vga.sequencer.data[1] & 1)) ? 8 : 9);
	frame.height = vga.crtc.vert_disp_end + 1;
	if (!frame.width || frame.width > 4096 || frame.height > 2048)
		return {};
	// vga_device can write the final repeated scanline past the visible-area bottom.
	// Reserve one full character row; only the visible rectangle is submitted.
	m_render_bitmap.resize(frame.width, frame.height + 64);
	screen().set_visible_area(frame.width, frame.height);
	screen().tick_frame();
	vga.crtc.start_addr = latch_start_addr();
	vga.attribute.pel_shift = vga.attribute.pel_shift_latch;
	palette_update();
	svga_device::screen_update(
		screen(), m_render_bitmap, rectangle(0, frame.width - 1, 0, frame.height - 1));
	frame.argb.assign(
		m_render_bitmap.raw(),
		m_render_bitmap.raw() + size_t(frame.width) * frame.height);
	return frame;
}

void CGloriaSynergy::update()
{
	const CPermedia2::Frame frame = render_frame();
	// Never hold the device-bus mutex while acquiring a GUI mutex.
	{
		CGloriaDisplayLock guard(m_output.display());
		if (!frame.argb.empty())
		{
			m_output.present_argb32(frame.argb.data(), frame.width, frame.height);
		}
		else
		{
			m_output.display().clear_screen();
		}
		m_output.display().flush();
	}
}

void CGloriaSynergy::run()
{
	try
	{
		if (!m_gui_initialized)
		{
			m_output.display().init(X_TILESIZE, Y_TILESIZE);
			m_gui_initialized = true;
		}
		while (!m_stop.load())
		{
			{
				CGloriaDisplayLock guard(m_output.display());
				m_output.display().handle_events();
			}
			if (m_pause.load())
			{
				m_pause_ack.store(true);
				CThread::sleep(10);
				continue;
			}
			m_pause_ack.store(false);
			update();
			CThread::sleep(20);
		}
	}
	catch (const CException& e)
	{
		printf(
			"%s: Exception in display thread: %s.\n",
			devid_string,
			e.displayText().c_str());
		m_worker_failed.store(true);
	}
	catch (const std::exception& e)
	{
		printf(
			"%s: Exception in display thread: %s.\n", devid_string, e.what());
		m_worker_failed.store(true);
	}
}

// PCI configuration and option-ROM support

std::array<u32, 64> CGloriaSynergy::PCIConfig::config_data() const
{
	std::array<u32, 64> data{};
	data[0] = u32(vendor) | (u32(device) << 16);
	data[2] = (class_code << 8) | revision;
	data[11] = u32(subsystem_vendor) | (u32(subsystem_device) << 16);
	data[15] = 0x000001ff; // INTA#, interrupt line unassigned
	return data;
}

std::array<u32, 64> CGloriaSynergy::PCIConfig::config_mask()
{
	std::array<u32, 64> mask{};
	mask[1] = 0x00000147; // I/O, memory, bus master, parity response, SERR
	mask[3] = 0x0000ffff; // cache-line size and latency timer
	mask[4] = 0xfffe0000; // 128 KiB non-prefetchable MMIO
	mask[5] = mask[6] = 0xff800000; // two 8 MiB apertures, one backing store
	mask[12] = 0xffff0001;			// 64 KiB ROM decode and enable
	mask[62] = 0xf07fffff;
	mask[63] = 0xffffffff; // IndirectAddress F8 / IndirectData FC
	mask[15] = 0xff;	   // interrupt line only; pin fixed
	return mask;
}

CGloriaSynergy::OptionROM CGloriaSynergy::OptionROM::parse(
	const std::vector<uint8_t>& raw, const std::string& mapping)
{
	auto fail = [](const char* message) {
		throw std::invalid_argument(message);
	};
	if (raw.size() != 32768 && raw.size() != 65536)
		fail("ROM must be an unmodified 32 or 64 KiB image");
	auto word = [&](size_t p) -> uint16_t {
		if (p > raw.size() - 2)
			fail("ROM field out of bounds");
		return uint16_t(uint16_t(raw[p]) | (uint16_t(raw[p + 1]) << 8));
	};
	if (word(0) != 0xaa55)
		fail("ROM missing 55 AA signature");
	const size_t p = word(0x18);
	if (p > raw.size() - 24 || raw[p] != 'P' || raw[p + 1] != 'C' ||
		raw[p + 2] != 'I' || raw[p + 3] != 'R')
		fail("ROM missing/bounded PCIR structure");
	const uint16_t pcir_size = word(p + 10);
	if (pcir_size < 24 || size_t(pcir_size) > raw.size() - p)
		fail("Invalid PCIR structure length");
	const size_t image = size_t(word(p + 16)) * 512,
				 header = size_t(raw[2]) * 512;
	if (!image || image > raw.size() || header != image ||
		p + pcir_size > image)
		fail("Invalid/inconsistent option-ROM image lengths");
	if (raw[p + 20] != 0 || !(raw[p + 21] & 0x80))
		fail("Only a single final x86 option image is supported");
	unsigned sum = 0;
	for (size_t i = 0; i < image; ++i)
		sum += raw[i];
	if (sum & 255)
		fail("Option-ROM checksum failed");
	OptionROM rom;
	rom.file_size = static_cast<uint32_t>(raw.size());
	rom.image_size = static_cast<uint32_t>(image);
	rom.vendor = word(p + 4);
	rom.device = word(p + 6);
	rom.layout = mapping;
	rom.bytes.assign(65536, 0xff);
	if (raw.size() == 65536)
	{
		if (mapping != "exact")
			fail("64 KiB ROM requires rom_layout=exact");
		rom.bytes = raw;
	}
	else
	{
		if (mapping != "mirror32" && mapping != "pad_ff")
			fail(
				"32 KiB ROM requires explicit rom_layout=mirror32 or pad_ff "
				"(upper decode unmeasured)");
		std::copy(raw.begin(), raw.end(), rom.bytes.begin());
		if (mapping == "mirror32")
			std::copy(raw.begin(), raw.end(), rom.bytes.begin() + 32768);
	}
	const std::string content(raw.begin(), raw.end());
	rom.agp_banner = content.find("/AGP/") != std::string::npos ||
		content.find("/AGP]") != std::string::npos;
	rom.subsystem_vendor = uint16_t(
		uint16_t(rom.bytes[0xfffc]) | (uint16_t(rom.bytes[0xfffd]) << 8));
	rom.subsystem_device = uint16_t(
		uint16_t(rom.bytes[0xfffe]) | (uint16_t(rom.bytes[0xffff]) << 8));
	return rom;
}

// Construction, initialization and thread lifecycle

CGloriaSynergy::CGloriaSynergy(
	CConfigurator* cfg, CSystem* sys, int bus, int dev, bx_gui_c& display)
try
	: svga_device(cfg, sys, bus, dev), m_output(0, display),
	  m_permedia2(gloria_options(cfg)), m_rom(65536, 0xff)
{
	reset_vga();
	m_crtc_space_config = address_space_config("crtc_regs", ENDIANNESS_LITTLE, 8, 8, 0,
		address_map_constructor(FUNC(CGloriaSynergy::crtc_map), this));
	m_gc_space_config = address_space_config("gc_regs", ENDIANNESS_LITTLE, 8, 8, 0,
		address_map_constructor(FUNC(CGloriaSynergy::gc_map), this));
	m_seq_space_config = address_space_config("sequencer_regs", ENDIANNESS_LITTLE, 8, 8, 0,
		address_map_constructor(FUNC(CGloriaSynergy::sequencer_map), this));
	m_atc_space_config = address_space_config("attribute_regs", ENDIANNESS_LITTLE, 8, 8, 0,
		address_map_constructor(FUNC(CGloriaSynergy::attribute_map), this));
}
catch (const CException&)
{
	throw;
}
catch (const std::exception& error)
{
	FAILURE_1(
		Configuration, "GLoria configuration failed: %.650s", error.what());
}

void CGloriaSynergy::init()
try
{
	if (m_initialized)
		return;
	CConfigurator* cfg = myCfg;
	init_maps();
	add_legacy_io(LegacyIO, 0x3b0, 0x30);
	add_legacy_mem(LegacyMemory, 0xa0000, 0x20000);
	m_profile.vendor = gloria_setting16(cfg, "vendor", m_profile.vendor);
	m_profile.device = gloria_setting16(cfg, "device", m_profile.device);
	m_profile.subsystem_vendor = gloria_setting16(cfg, "subsystem_vendor", 0);
	m_profile.subsystem_device = gloria_setting16(cfg, "subsystem_device", 0);
	const auto revision = gloria_setting(cfg, "revision", m_profile.revision);
	if (revision > 255)
		throw std::runtime_error("gloria revision exceeds 8 bits");
	m_profile.revision = static_cast<uint8_t>(revision);
	m_service_budget = gloria_setting(cfg, "service_budget", 4096);
	m_scanline_access_divisor =
		gloria_setting(cfg, "scanline_access_divisor", 1024);
	if (m_service_budget == 0 || m_service_budget > 1000000 ||
		m_scanline_access_divisor == 0)
		throw std::runtime_error("Invalid gloria scheduling budget/divisor");
	m_allow_dma = cfg->get_bool_value("allow_dma", true);
	m_trace_apertures = cfg->get_bool_value("trace_apertures", false);
	m_frame_file = cfg->get_text_value("frame_file", "");
	const std::string romname = cfg->get_text_value("rom", "");
	m_rom_layout = cfg->get_text_value("rom_layout", "exact");
	const bool from_rom = cfg->get_bool_value("subsystem_from_rom", false);
	if (!romname.empty())
	{
		std::ifstream f(romname, std::ios::binary | std::ios::ate);
		if (!f)
			throw std::runtime_error("Cannot open gloria ROM: " + romname);
		const auto size = f.tellg();
		if (size != 32768 && size != 65536)
			throw std::runtime_error("gloria ROM must be 32 or 64 KiB");
		f.seekg(0);
		std::vector<uint8_t> raw(static_cast<size_t>(size));
		f.read(
			reinterpret_cast<char*>(raw.data()),
			static_cast<std::streamsize>(raw.size()));
		if (!f)
			throw std::runtime_error("Cannot read full gloria ROM");
		const auto parsed = OptionROM::parse(raw, m_rom_layout);
		if (parsed.vendor != m_profile.vendor ||
			parsed.device != m_profile.device)
			throw std::runtime_error(
				"ROM PCIR identity differs from configured PCI vendor/device");
		if (parsed.agp_banner)
			throw std::runtime_error(
				"AGP ROM rejected for PCI SN-PBXGK-BB "
				"target; choose a PCI image");
		m_rom = parsed.bytes;
		if (from_rom)
		{
			if (cfg->get_text_value("subsystem_vendor", nullptr) ||
				cfg->get_text_value("subsystem_device", nullptr))
				throw std::runtime_error(
					"Choose explicit subsystem IDs OR "
					"subsystem_from_rom, not both");
			if (parsed.subsystem_vendor == 0xffff ||
				parsed.subsystem_vendor == 0 ||
				parsed.subsystem_device == 0xffff)
				throw std::runtime_error(
					"No usable subsystem tuple at decoded "
					"ROM FFFC; review rom_layout");
			m_profile.subsystem_vendor = parsed.subsystem_vendor;
			m_profile.subsystem_device = parsed.subsystem_device;
		}
#ifdef DEBUG_VGA
		printf(
			"%s: ROM %u bytes, layout=%s, PCI %04x:%04x, subsystem "
			"%04x:%04x.\n",
			devid_string,
			parsed.file_size,
			m_rom_layout.c_str(),
			parsed.vendor,
			parsed.device,
			parsed.subsystem_vendor,
			parsed.subsystem_device);
#endif
	}
	else if (from_rom)
		throw std::runtime_error("subsystem_from_rom requires a ROM image");
	m_permedia2.set_rom_reader([this](uint32_t a) {
		return m_rom[a & 0xffffu];
	});
	const std::string tracename = cfg->get_text_value("trace", "");
	if (!tracename.empty())
	{
		if (std::filesystem::exists(tracename))
			throw std::runtime_error(
				"Refusing to overwrite existing gloria trace: " + tracename);
		m_trace.open(tracename);
		if (!m_trace)
			throw std::runtime_error("Cannot create gloria trace");
		m_trace << "# PM2TRACE 1; ES40 access-driven scheduler; all bus values "
				   "hexadecimal\n";
		m_trace << "# Legacy VGA traffic and DMA payloads are not captured for "
				   "standalone core replay.\n";
	}
	const auto profile_data = m_profile.config_data(),
			   profile_mask = PCIConfig::config_mask();
	u32 data[64]{}, mask[64]{};
	for (unsigned i = 0; i < 64; ++i)
	{
		data[i] = profile_data[i];
		mask[i] = profile_mask[i];
	}
	add_function(0, data, mask);
	m_permedia2.set_irq_callback([this](bool level) {
		do_pci_interrupt(0, level);
	});
	m_permedia2.set_diagnostic_callback([this](
											const CPermedia2::Diagnostic& d) {
		// Hardware faults remain visible even when VGA debugging is disabled.
		bool show_diagnostic = d.fatal;
#ifdef DEBUG_VGA
		show_diagnostic = true;
#endif
		if (show_diagnostic)
			printf(
				"%s: %s%s (address=%08" PRIx32 ", value=%08" PRIx32 "): %s.\n",
				devid_string,
				d.fatal ? "Graphics engine halted: " : "",
				d.code.c_str(),
				d.address,
				d.value,
				d.message.c_str());
		if (m_trace)
		{
			m_trace << "# " << devid_string << ": "
					<< (d.fatal ? "Graphics engine halted: " : "") << d.code
					<< " (address=" << std::hex << d.address
					<< ", value=" << d.value << std::dec << "): " << d.message
					<< '\n';
			m_trace.flush();
		}
	});
	m_permedia2.set_unimplemented_callback(
		[this](const char* what, uint32_t address, uint32_t value, bool write) {
			report_unimplemented(what, address, value, write);
		});
	m_permedia2.set_dma_reader(
		[this](uint32_t address, uint8_t* dst, size_t count) {
			// Fetch input commands through the shared PCI/IOMMU path, with the same
			// guest bus-master gate used by other PCI devices.
			if (!m_allow_dma || !(config_read(0, 4, 16) & 4))
				return false;
			if (m_trace)
				m_trace << "# DMA read " << std::hex << address
						<< " bytes=" << count << std::dec << '\n';
			do_pci_read(address, dst, 1, count);
			return true;
		});
	m_permedia2.set_dma_writer(
		[this](uint32_t address, const uint8_t* src, size_t count) {
			if (!m_allow_dma || !(config_read(0, 4, 16) & 4))
				return false;
			if (m_trace)
				m_trace << "# DMA write " << std::hex << address
						<< " bytes=" << count << std::dec << '\n';
			// The shared PCI writer reads its source through a legacy non-const
			// interface. Byte transfers preserve the core's little-endian stream.
			do_pci_write(address, const_cast<uint8_t*>(src), 1, count);
			return true;
		});
	m_initialized = true;
	ResetPCI();
	printf("%s: ELSA GLoria Synergy, Permedia 2, 8 MB VRAM.\n", devid_string);
}
catch (const CException&)
{
	throw;
}
catch (const std::exception& error)
{
	FAILURE_1(
		Configuration, "GLoria initialization failed: %.650s", error.what());
}

CGloriaSynergy::~CGloriaSynergy()
{
	m_stop.store(true);
	if (m_thread)
	{
		m_thread->join();
		delete m_thread;
	}
	if (m_trace)
		m_trace.flush();
}

void CGloriaSynergy::reset_vga()
{
	vga = {};
	svga = {};
	vga_device::device_reset();
	vga.gc.memory_map_sel = 0; // Preserve the native VGA aperture selection.
	vga.memory = m_permedia2.vram_data();
	vga.svga_intf.vram_size = CPermedia2::VramSize;
	vga.crtc.maximum_scan_line = 1;
	vga.gc.bit_mask = 0xff;
	vga.sequencer.char_sel.base[0] = vga.sequencer.char_sel.base[1] = 0x20000;
	vga.sequencer.data[5] = 0x4b; // HRM VGAControlReg reset
	vga.dac.dirty = 1;
	m_mode640 = 0;
}

void CGloriaSynergy::ResetPCI()
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	CPCIDevice::ResetPCI();
	m_permedia2.reset();
	sync_pci_config();
	reset_vga();
	m_access_count = 0;
	if (m_trace)
		m_trace << "RESET\n";
}

void CGloriaSynergy::start_threads()
{
	m_pause.store(false);
	if (!m_thread)
	{
		m_stop.store(false);
		m_worker_failed.store(false);
		m_thread = new CThread("gloria");
		m_thread->start(*this);
	}
}

void CGloriaSynergy::stop_threads()
{
	if (!m_thread)
		return;
	if (cSystem->IsResetInProgress())
	{
		m_pause.store(true);
		// The worker acknowledges only after releasing the device-bus lock.
		while (!m_pause_ack.load() && !m_worker_failed.load())
			CThread::sleep(1);
		return;
	}
	m_stop.store(true);
	m_thread->join();
	delete m_thread;
	m_thread = nullptr;
}

// PCI and legacy access

void CGloriaSynergy::sync_pci_config()
{
	const u32 chip_config = m_permedia2.peek(CPermedia2::ChipConfig);
	const bool fixed = (chip_config & 4) != 0;
	// HRM 2.14.4: non-fixed VGA is an "other" display controller. Keep the
	// board VGA class when fixed decode is enabled.
	const u32 class_code = (chip_config & 1)
		? (fixed ? 0x000100u : 0x000000u)
		: (fixed ? 0x030000u : 0x038000u);
	pci_state.config_data[0][2] = endian_32(
		(class_code << 8) | (endian_32(pci_state.config_data[0][2]) & 0xff));

	// HRM 2.14.6: without fixed VGA decode, I/O and palette snoop enables
	// are read-only zero. Re-enabling decode does not restore their old values.
	const u32 vga_command_bits = 0x21;
	u32 command = endian_32(pci_state.config_data[0][1]);
	u32 mask = endian_32(pci_state.config_mask[0][1]);
	mask = (mask & ~vga_command_bits) |
		(fixed ? endian_32(std_config_mask[0][1]) & vga_command_bits : 0);
	if (!fixed)
		command &= ~vga_command_bits;
	pci_state.config_data[0][1] = endian_32(command);
	pci_state.config_mask[0][1] = endian_32(mask);
}

bool CGloriaSynergy::legacy_enabled(bool memory) const noexcept
{
	const u32 command = endian_32(pci_state.config_data[0][1]);
	if ((m_permedia2.peek(CPermedia2::ChipConfig) & 6) != 6)
		return false;
	return (command & (memory ? 2u : 1u)) &&
		(!memory || (vga.sequencer.data[5] & 1));
}

bool CGloriaSynergy::decodes_memory_access(
	int index, u64 address, int dsize, bool write) const noexcept
{
	if (index >= PCI_RANGE_BASE)
		return CPCIDevice::decodes_memory_access(index, address, dsize, write);
	if (dsize != 8 && dsize != 16 && dsize != 32 && dsize != 64)
		return false;
	if (index == LegacyIO)
	{
		if (!legacy_enabled(false) || address + dsize / 8 > 0x30)
			return false;
		const u32 port = u32(address) + 0x3b0;
		if ((port >= 0x3b0 && port < 0x3c0 && m_ioas) ||
			(port >= 0x3d0 && !m_ioas))
			return false;
		if (port >= 0x3c6 && port <= 0x3c9 && !(vga.sequencer.data[5] & 2))
			return false;
		return true;
	}
	if (index != LegacyMemory || !legacy_enabled(true) ||
		address + dsize / 8 > 0x20000)
		return false;
	switch (vga.gc.memory_map_sel & 3)
	{
	case 1:
		return address + dsize / 8 <= 0x10000;
	case 2:
		return address >= 0x10000 && address + dsize / 8 <= 0x18000;
	case 3:
		return address >= 0x18000;
	default:
		return true;
	}
}

u32 CGloriaSynergy::ReadMem_Legacy(int index, u32 address, int dsize)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (!decodes_memory_access(index, address, dsize, false))
		return 0xffffffff;
	if (dsize != 8 && dsize != 16 && dsize != 32)
		return 0xffffffff;
	tick();
	u32 data = 0;
	for (int i = 0; i < dsize / 8; ++i)
		data |= u32(index == LegacyIO ? io_read_b(0x3b0 + address + i)
									  : mem_r(address + i))
			<< (i * 8);
	return data;
}

void CGloriaSynergy::WriteMem_Legacy(
	int index, u32 address, int dsize, u32 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (!decodes_memory_access(index, address, dsize, true))
		return;
	if (dsize != 8 && dsize != 16 && dsize != 32)
		return;
	tick();
	for (int i = 0; i < dsize / 8; ++i)
	{
		if (index == LegacyIO)
			io_write_b(0x3b0 + address + i, u8(data >> (i * 8)));
		else
			mem_w(address + i, u8(data >> (i * 8)));
	}
}

u32 CGloriaSynergy::ReadMem_Bar(int func, int bar, u32 address, int dsize)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (func != 0)
		return 0xffffffff;
	tick();
	u32 data = 0xffffffff;
	switch (bar)
	{
	case 0:
		if (address < 0x20000 && (address & 0xf000) == 0x6000)
		{
			if (dsize != 8 && dsize != 16 && dsize != 32)
				return 0xffffffff;
			data = 0;
			for (int i = 0; i < dsize / 8; ++i)
			{
				const u32 lane =
					(address + i) ^ ((address & 0x10000) ? 3u : 0u);
				data |= u32(io_read_b(lane & 0x3ff)) << (8 * i);
			}
		}
		else
			data = m_permedia2.ReadMem(address, dsize);
		break;
	case 1:
	case 2:
		data = m_permedia2.mem_read(
			static_cast<unsigned>(bar - 1), address, dsize);
		break;
	case 6:
		if ((dsize == 8 || dsize == 16 || dsize == 32) &&
			address <= m_rom.size() - static_cast<unsigned>(dsize) / 8)
		{
			data = 0;
			for (int i = 0; i < dsize / 8; ++i)
				data |= u32(m_rom[address + static_cast<unsigned>(i)])
					<< (8 * i);
		}
		break;
	}
	trace('R', bar, address, dsize, data);
	return data;
}

void CGloriaSynergy::WriteMem_Bar(
	int func, int bar, u32 address, int dsize, u32 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (func != 0)
		return;
	tick();
	trace('W', bar, address, dsize, data);
	switch (bar)
	{
	case 0:
		if (address < 0x20000 && (address & 0xf000) == 0x6000)
		{
			if (dsize != 8 && dsize != 16 && dsize != 32)
				return;
			for (int i = 0; i < dsize / 8; ++i)
			{
				const u32 lane =
					(address + i) ^ ((address & 0x10000) ? 3u : 0u);
				io_write_b(lane & 0x3ff, u8(data >> (8 * i)));
			}
		}
		else if (m_permedia2.WriteMem(address, dsize, data))
		{
			if ((address & 0xffff) == CPermedia2::ChipConfig ||
				(address & 0xffff) == CPermedia2::ResetStatus)
				sync_pci_config();
			if ((address & 0xffff) == CPermedia2::PaletteRead)
				vga.dac.read = 1;
			if ((address & 0xffff) == CPermedia2::PaletteWrite)
				vga.dac.read = 0;
		}
		break;
	case 1:
	case 2:
		m_permedia2.mem_write(
			static_cast<unsigned>(bar - 1), address, dsize, data);
		break;
	}
}

u64 CGloriaSynergy::ReadMem(int index, u64 address, int dsize)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (dsize == 64)
	{
		const u64 lo = CPCIDevice::ReadMem(index, address, 32);
		const u64 hi = CPCIDevice::ReadMem(index, address + 4, 32);
		return lo | (hi << 32);
	}
	return CPCIDevice::ReadMem(index, address, dsize);
}

void CGloriaSynergy::WriteMem(int index, u64 address, int dsize, u64 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (dsize == 64)
	{
		CPCIDevice::WriteMem(index, address, 32, u32(data));
		CPCIDevice::WriteMem(index, address + 4, 32, u32(data >> 32));
		return;
	}
	CPCIDevice::WriteMem(index, address, dsize, data);
}

u32 CGloriaSynergy::config_read_custom(
	int func, u32 address, int dsize, u32 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (func == 0 && !m_replaying_pci && (address & ~3u) == 0xfc)
	{
		// Narrow initial contract: DWORD port accesses only. Never service a
		// partial port read by destructively reading/discarding a whole DWORD.
		if (address != 0xfc || dsize != 32)
		{
			printf(
				"%s: Unsupported indirect-data read (address=%02x, "
				"dsize=%d).\n",
				devid_string,
				address,
				dsize);
			data = 0xffffffff;
		}
		else
		{
			const u32 indirect = config_read(0, 0xf8, 32),
					  region = indirect >> 28, offset = indirect & 0x7fffff;
			if ((region == 0 && offset < 0x20000) || region == 1 || region == 2)
				data = ReadMem_Bar(0, int(region), offset, 32);
			else if (region == 7 && offset <= 0xfffc)
				data = ReadMem_Bar(0, 6, offset, 32);
			else
				data = 0xffffffff;
		}
	}
	if (m_trace)
		m_trace << "# CFG R " << std::hex << func << ' ' << address << ' '
				<< dsize << ' ' << data << std::dec << '\n';
	return data;
}

void CGloriaSynergy::config_write_custom(
	int func, u32 address, int dsize, u32 old_data, u32 new_data, u32 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	(void)old_data;
	if (m_trace)
		m_trace << "# CFG W " << std::hex << func << ' ' << address << ' '
				<< dsize << ' ' << new_data << std::dec << '\n';
	if (func == 0 && !m_replaying_pci && (address & ~3u) == 0xfc)
	{
		if (address != 0xfc || dsize != 32)
		{
			printf(
				"%s: Unsupported indirect-data write (address=%02x, "
				"dsize=%d, data=%08x).\n",
				devid_string,
				address,
				dsize,
				data);
		}
		else
		{
			const u32 indirect = config_read(0, 0xf8, 32),
					  region = indirect >> 28, offset = indirect & 0x7fffff;
			if ((region == 0 && offset < 0x20000) || region == 1 || region == 2)
				WriteMem_Bar(0, int(region), offset, 32, data);
			// ROM writes intentionally discarded: no flash programming model.
		}
	}
	if (func == 0)
		do_pci_interrupt(0, m_permedia2.irq_asserted());
}

// Device service and saved state

void CGloriaSynergy::check_state()
{
	if (m_worker_failed.load())
		FAILURE(Thread, "GLoria display thread failed");
	// Retain the periodic work allowance, but release the shared bus between
	// bounded batches so audio DMA and other devices can make progress.
	for (size_t remaining = 1000000; remaining;)
	{
		const size_t budget = std::min(remaining, size_t(4096));
		size_t used;
		{
			std::lock_guard<std::recursive_mutex> guard(
				cSystem->get_device_bus_mutex());
			if (m_trace)
				m_trace << "STEP " << std::hex << budget << std::dec << '\n';
			used = m_permedia2.service(budget);
		}
		if (used < budget)
			break;
		remaining -= budget;
		if (remaining)
			std::this_thread::yield();
	}
	// ES40 currently calls this roughly every 100ms. This compatibility pump
	// advances ONE virtual frame per call, not a real 60 Hz monitor model.
	{
		std::lock_guard<std::recursive_mutex> guard(
			cSystem->get_device_bus_mutex());
		const uint32_t lines = (m_permedia2.peek(CPermedia2::VTotal) & 0x7ff) + 1;
		m_permedia2.advance_scanlines(lines);
		if (m_trace)
		{
			m_trace << "LINES " << std::hex << lines << std::dec << '\n';
			m_trace.flush();
		}
	}
	try
	{
		publish_frame();
	}
	catch (const std::exception& e)
	{
		printf(
			"%s: Could not write graphics frame: %s.\n",
			devid_string,
			e.what());
	}
}

void CGloriaSynergy::prepare_snapshot()
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (m_trace)
		m_trace.flush();
}

void CGloriaSynergy::finalize_restore() noexcept
{
	try
	{
		std::lock_guard<std::recursive_mutex> guard(
			cSystem->get_device_bus_mutex());
		do_pci_interrupt(0, m_permedia2.irq_asserted());
	}
	catch (...)
	{
	}
}

std::string CGloriaSynergy::snapshot_identity() const
{
	std::ostringstream out;
	out << "GLoriaSynergy-es40-v1:" << std::hex << m_profile.vendor << ':'
		<< m_profile.device << ':' << unsigned(m_profile.revision) << ':'
		<< m_profile.subsystem_vendor << ':' << m_profile.subsystem_device;
	// ROM content, not just filename, belongs to saved-device identity. This is
	// an identity checksum, not a security/authenticity signature.
	uint64_t hash = 14695981039346656037ULL;
	for (uint8_t b : m_rom)
	{
		hash ^= b;
		hash *= 1099511628211ULL;
	}
	out << ':' << hash << ':' << m_rom_layout << ':' << m_service_budget << ':'
		<< m_scanline_access_divisor << ':' << m_allow_dma;
	const auto reset = gloria_options(myCfg);
	out << ':' << reset.chip_config << ':' << reset.mem_control << ':'
		<< reset.mem_config << ':' << reset.input_fifo_entries;
	// Include the PCI location so otherwise identical cards cannot exchange state.
	out << "|path=" << get_device_path();
	return out.str();
}

int CGloriaSynergy::SaveState(FILE* f)
{
	if (!f)
		return -1;
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	try
	{
		std::ostringstream stream(std::ios::binary);
		m_permedia2.SaveState(stream);
		const std::string bytes = stream.str();
		if (CPCIDevice::SaveState(f))
			return -1;
		gloria_write_u32(
			f, 0x31474c45); // ELG1: native ES40 VGA plus prototype core format
		gloria_write_u32(f, sizeof(vga));
		auto saved_vga = vga;
		saved_vga.memory = nullptr;
		if (fwrite(&saved_vga, sizeof(saved_vga), 1, f) != 1)
			return -1;
		gloria_write_u32(f, m_mode640);
		gloria_write_u32(f, u32(m_access_count));
		gloria_write_u32(f, u32(m_access_count >> 32));
		gloria_write_u32(f, u32(bytes.size()));
		if (fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size())
			return -1;
		return ferror(f) ? -1 : 0;
	}
	catch (const std::exception& e)
	{
		printf(
			"%s: Could not save graphics state: %s.\n", devid_string, e.what());
		return -1;
	}
}

int CGloriaSynergy::RestoreState(FILE* f)
{
	if (!f)
		return -1;
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	try
	{
		// PCI state is restored first, following the other ES40 devices.
		// A failure aborts the whole system restore; it must not resume the VM.
		struct ReplayGuard
		{
			bool& flag;

			ReplayGuard(bool& value) : flag(value) { flag = true; }

			~ReplayGuard() { flag = false; }
		} replay(m_replaying_pci);

		if (CPCIDevice::RestoreState(f))
			return -1;
		if (gloria_read_u32(f) != 0x31474c45 ||
			gloria_read_u32(f) != sizeof(vga))
			return -1;
		decltype(vga) saved_vga{};
		if (fread(&saved_vga, sizeof(saved_vga), 1, f) != 1)
			return -1;
		const u32 mode640 = gloria_read_u32(f), lo = gloria_read_u32(f),
				  hi = gloria_read_u32(f), n = gloria_read_u32(f);
		if (mode640 > 255 ||
			saved_vga.svga_intf.vram_size != CPermedia2::VramSize ||
			saved_vga.crtc.maximum_scan_line == 0 ||
			saved_vga.crtc.maximum_scan_line > 32 ||
			saved_vga.crtc.scan_doubling > 1 || saved_vga.gc.read_map_sel > 3 ||
			saved_vga.gc.rotate_count > 7 || saved_vga.gc.logical_op > 3 ||
			saved_vga.gc.write_mode > 3 || saved_vga.crtc.offset > 255 ||
			saved_vga.crtc.horz_disp_end > 255 ||
			saved_vga.crtc.vert_disp_end > 1023 ||
			saved_vga.crtc.start_addr_latch > 0x1ffff ||
			saved_vga.sequencer.char_sel.base[0] < 0x20000 ||
			saved_vga.sequencer.char_sel.base[0] > 0x2e000 ||
			(saved_vga.sequencer.char_sel.base[0] & 0x1fff) ||
			saved_vga.sequencer.char_sel.base[1] < 0x20000 ||
			saved_vga.sequencer.char_sel.base[1] > 0x2e000 ||
			(saved_vga.sequencer.char_sel.base[1] & 0x1fff) ||
			n < CPermedia2::VramSize || n > CPermedia2::VramSize + 131072)
			return -1;
		std::string bytes(n, '\0');
		if (fread(bytes.data(), 1, n, f) != n)
			return -1;
		std::istringstream stream(bytes, std::ios::binary);
		m_permedia2.RestoreState(stream);
		// The core's transactional loader owns its VRAM; refresh our borrowed view.
		vga.memory = m_permedia2.vram_data();
		if (stream.peek() != std::char_traits<char>::eof())
			return -1;
		saved_vga.memory = m_permedia2.vram_data();
		vga = saved_vga;
		m_mode640 = u8(mode640);
		m_ioas = (vga.miscellaneous_output & 1) != 0;
		m_access_count = u64(lo) | (u64(hi) << 32);
		sync_pci_config();
		vga.dac.dirty = 1;
		return 0;
	}
	catch (const std::exception& e)
	{
		vga.memory = m_permedia2.vram_data();
		printf(
			"%s: Could not restore graphics state: %s.\n",
			devid_string,
			e.what());
		return -1;
	}
}

// Byte I/O and diagnostic output

u8 CGloriaSynergy::io_read_b(u32 port)
{
	// Memory-mapped VGA aliases bypass PCI I/O-enable and fixed-decode gates,
	// but the on-chip VGA-enable and mono/colour port selection still apply.
	if (!(m_permedia2.peek(CPermedia2::ChipConfig) & 2))
		return 0xff;
	if ((port >= 0x3b0 && port < 0x3c0 && m_ioas) ||
		(port >= 0x3d0 && port < 0x3e0 && !m_ioas))
		return 0xff;
	if (port >= 0x3c6 && port <= 0x3c9)
	{
		if (!(vga.sequencer.data[5] & 2))
			return 0xff;
		// HRM table 5.1: the VGA ports supply DAC A0/A1; SR5 supplies A2/A3.
		const unsigned index = ((vga.sequencer.data[5] >> 2) & 12) | (port & 3);
		if (index == 3)
			return vga.dac.read ? 3 : 0;
		return u8(m_permedia2.ReadMem(CPermedia2::PaletteWrite + index * 8, 8));
	}
	switch (port)
	{
	case 0x3ba:
	case 0x3da:
	{
		vga.attribute.state = 0;
		// Access-driven scan phase, matching the core's bounded scheduler.
		// This supplies both phases to firmware polling; it is not cycle timing.
		const u32 total = u32(vga.crtc.vert_total) + 2;
		const u32 line = u32(m_access_count / 32) % total;
		const bool retrace =
			((line + total - vga.crtc.vert_retrace_start % total) % total) <
			(((vga.crtc.vert_retrace_end - vga.crtc.vert_retrace_start) & 15) +
				1u);
		return u8(
			(retrace ? 8 : 0) |
			((line > vga.crtc.vert_disp_end || (m_access_count & 7) == 0) ? 1
																		  : 0));
	}
	default:
	{
		u8 data;
		if (vga_io_read(port, data))
			return data;
		report_unimplemented("VGA port", port, 0, false);
		return 0xff;
	}
	}
}

void CGloriaSynergy::io_write_b(u32 port, u8 data)
{
	if (!(m_permedia2.peek(CPermedia2::ChipConfig) & 2))
		return;
	if ((port >= 0x3b0 && port < 0x3c0 && m_ioas) ||
		(port >= 0x3d0 && port < 0x3e0 && !m_ioas))
		return;
	if (port >= 0x3c6 && port <= 0x3c9)
	{
		if (!(vga.sequencer.data[5] & 2))
			return;
		const unsigned index = ((vga.sequencer.data[5] >> 2) & 12) | (port & 3);
		m_permedia2.WriteMem(CPermedia2::PaletteWrite + index * 8, 8, data);
		if (index == 3)
			vga.dac.read = 1;
		if (index == 0)
			vga.dac.read = 0;
		return;
	}
	if (!vga_io_write(port, data))
		report_unimplemented("VGA port", port, data, true);
}

void CGloriaSynergy::tick()
{
	// The caller may already hold the system bus for a PCI transaction. A
	// larger configured allowance must not make that transaction monopolize it.
	const u32 budget = std::min(m_service_budget, u32(4096));
	if (m_trace)
		m_trace << "STEP " << std::hex << budget << std::dec << '\n';
	m_permedia2.service(budget);
	if (++m_access_count % m_scanline_access_divisor == 0)
	{
		m_permedia2.advance_scanlines(1);
		if (m_trace)
			m_trace << "LINES 1\n";
	}
}

void CGloriaSynergy::trace(char op, int bar, u32 a, int bits, u32 value)
{
	if (!m_trace || (bar != 0 && !m_trace_apertures))
		return;
	if (bar == 0)
		m_trace << op << ' ' << std::hex << a << ' ' << bits << ' ' << value
				<< std::dec << '\n';
	else if (bar == 1 || bar == 2)
		m_trace << (op == 'W' ? "AW" : "AR") << ' ' << std::hex << (bar - 1)
				<< ' ' << a << ' ' << bits << ' ' << value << std::dec << '\n';
	else if (bar == 6)
		m_trace << "# ROM " << op << ' ' << std::hex << a << ' ' << bits << ' '
				<< value << std::dec << '\n';
}

void CGloriaSynergy::trace_unimplemented(const std::string& text)
{
	if (m_trace)
	{
		m_trace << "# " << devid_string << ": " << text << '\n';
		m_trace.flush();
	}
}

void CGloriaSynergy::publish_frame()
{
	if (m_frame_file.empty())
		return;
	const auto f = render_frame();
	if (f.argb.empty())
		return;
	const std::filesystem::path path(m_frame_file), temp(m_frame_file + ".tmp");
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out)
			throw std::runtime_error("Cannot open gloria frame output");
		CPermedia2::write_ppm(out, f);
	}
	std::error_code ec;
	std::filesystem::rename(temp, path, ec);
	if (ec)
	{ // Windows does not replace an existing file with std::filesystem::rename.
		std::filesystem::remove(path, ec);
		ec.clear();
		std::filesystem::rename(temp, path, ec);
	}
	if (ec)
		throw std::runtime_error(
			"Cannot publish gloria frame: " + ec.message());
}
