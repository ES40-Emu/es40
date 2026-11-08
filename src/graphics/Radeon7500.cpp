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
#include "Radeon7500.h"
#include "config/Configurator.h"
#include "system/System.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

class CRadeonDisplayLock
{
public:
	explicit CRadeonDisplayLock(bx_gui_c& display) : m_display(display)
	{
		m_display.lock();
	}

	~CRadeonDisplayLock() { m_display.unlock(); }

private:
	bx_gui_c& m_display;
};

static uint32_t radeon_setting(
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
			std::string("Invalid radeon integer setting: ") + key);
	return static_cast<uint32_t>(n);
}

static uint32_t radeon_bounded_setting(
	CConfigurator* cfg, const char* key, uint32_t def, uint32_t max)
{
	const auto v = radeon_setting(cfg, key, def);
	if (v > max)
		throw std::runtime_error(
			std::string("radeon setting out of range: ") + key);
	return v;
}

static CRV200::Options radeon_options(CConfigurator* c)
{
	CRV200::Options o;
	const u64 vram = c->get_num_value("vram", false, o.vram_bytes);
	if (vram > 0xffffffffULL)
		throw std::runtime_error("radeon vram exceeds 64 MiB");
	o.vram_bytes = u32(vram);
	o.strict_mmio = c->get_bool_value("strict_mmio", false);
	return o;
}

static void radeon_write_u32(FILE* f, uint32_t v)
{
	uint8_t b[4];
	for (unsigned i = 0; i < 4; ++i)
		b[i] = uint8_t(v >> (8 * i));
	if (fwrite(b, 1, 4, f) != 4)
		throw std::runtime_error("Snapshot write failed");
}

static uint32_t radeon_read_u32(FILE* f)
{
	uint8_t b[4];
	if (fread(b, 1, 4, f) != 4)
		throw std::runtime_error("Snapshot read failed");
	return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
		(uint32_t(b[3]) << 24);
}

static bool radeon_vga_window_offset(u32& address, unsigned selection)
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

// CVGA's indexed port handlers dispatch here for legacy I/O, the MMIO VGA
// aliases and MM_INDEX/MM_DATA. Only the standard VGA registers are mapped.
void CRadeon7500::crtc_map(address_map& map)
{
	unimplemented_map(map, "VGA CRTC register");
	CVGA::crtc_map(map);
}

void CRadeon7500::sequencer_map(address_map& map)
{
	unimplemented_map(map, "VGA sequencer register");
	// These frontends mask stored bytes, including direct snapshot writes.
	// Limit the canonical map to SR00-04 so unknown indices keep diagnostics.
	auto standard = std::make_shared<address_map>(5);
	CVGA::sequencer_map(*standard);
	map(0x00, 0x04).lrw8(
		NAME([standard](offs_t index) {
			return standard->read_byte(index);
		}),
		NAME(([this, standard](offs_t index, u8 data) {
			static constexpr u8 masks[] = {0x03, 0x3f, 0x0f, 0x3f, 0x0e};
			vga.sequencer.data[index] = data & masks[index];
			standard->write_byte(index, data);
		})));
}

void CRadeon7500::gc_map(address_map& map)
{
	unimplemented_map(map, "VGA graphics controller register");
	CVGA::gc_map(map);
}

void CRadeon7500::attribute_map(address_map& map)
{
	unimplemented_map(map, "VGA attribute register");
	CVGA::attribute_map(map);
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

address_map& CRadeon7500::space(int spacenum)
{
	switch (spacenum)
	{
	case MAIN_IF_REG:
	case CRTC_REG:
	case SEQ_REG:
	case GC_REG:
	case ATC_REG:
		return CVGA::space(spacenum);
	default:
		throw std::invalid_argument("Invalid Radeon VGA register space");
	}
}

// VGA memory and display

u8 CRadeon7500::mem_r(offs_t address)
{
	if (!radeon_vga_window_offset(address, vga.gc.memory_map_sel))
		return 0xff;
	unsigned selected = vga.gc.read_map_sel & 3;
	if (vga.sequencer.data[4] & 8)
	{
		// Keep packed chain-four bytes in the layout consumed by CVGA scanout.
		for (unsigned plane = 0; plane < 4; ++plane)
			vga.gc.latch[plane] = vga.memory[(address & ~3u) + plane];
		selected = address & 3;
	}
	else if (!(vga.sequencer.data[4] & 4) && vga.gc.host_oe)
	{
		// CVGA text stores character/attribute pairs together; fonts stay planar.
		vga.gc.latch[0] = vga.memory[address & ~1u];
		vga.gc.latch[1] = vga.memory[(address & ~1u) + 1];
		for (unsigned plane = 2; plane < 4; ++plane)
			vga.gc.latch[plane] = vga.memory[(address >> 1) + plane * 0x10000];
		selected = (vga.gc.read_map_sel & 2) | (address & 1);
	}
	else
	{
		for (unsigned plane = 0; plane < 4; ++plane)
			vga.gc.latch[plane] =
				vga.memory[(address & 0xffff) + plane * 0x10000];
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

void CRadeon7500::mem_w(offs_t address, u8 data)
{
	if (!radeon_vga_window_offset(address, vga.gc.memory_map_sel))
		return;
	if (vga.sequencer.data[4] & 8)
	{
		if (vga.sequencer.map_mask & (1 << (address & 3)))
			vga.memory[address] = vga_latch_write(address & 3, data);
		return;
	}
	if (!(vga.sequencer.data[4] & 4) && vga.gc.host_oe)
	{
		const unsigned parity = address & 1;
		if (vga.sequencer.map_mask & (1 << parity))
			vga.memory[address] = vga_latch_write(parity, data);
		if (vga.sequencer.map_mask & (1 << (parity + 2)))
			vga.memory[(address >> 1) + (parity + 2) * 0x10000] =
				vga_latch_write(parity + 2, data);
		return;
	}
	for (unsigned plane = 0; plane < 4; ++plane)
		if (vga.sequencer.map_mask & (1 << plane))
			vga.memory[(address & 0xffff) + plane * 0x10000] =
				vga_latch_write(plane, data);
}

u8 CRadeon7500::get_actl_palette_idx(u8 index)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	return m_atc_map.read_byte(index & 0x1f);
}

void CRadeon7500::redraw_area(unsigned, unsigned, unsigned, unsigned)
{
	// Complete frames are submitted through the same output API as S3Trio64.
}

void CRadeon7500::palette_update()
{
	// Legacy DAC writes and native PALETTE_DATA share the core's palette.
	for (unsigned i = 0; i < 256; ++i)
		m_pen_table[i] = 0xff000000u | m_rv200.palette_color(i & vga.dac.mask);
	vga.dac.dirty = 0;
}

void CRadeon7500::recompute_params()
{
	// The next render_frame uses decomposed CRTC fields directly.
	vga.dac.dirty = 1;
}

CRV200::Frame CRadeon7500::render_frame()
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (m_pause.load() || !m_rv200.display_output_enabled())
		return {};
	// The primary DAC may select CRTC2 even while CRTC1 is in VGA mode.
	if (m_rv200.native_display())
		return m_rv200.scanout();
	if (!m_vga_enable || !vga.crtc.sync_en ||
		(vga.sequencer.data[0] & 3) != 3 || (vga.sequencer.data[1] & 0x20) ||
		!(vga.attribute.index & 0x20) || !vga.crtc.maximum_scan_line ||
		(m_rv200.peek(CRV200::CRTC_EXT_CNTL) & CRV200::DISPLAY_DIS))
		return {};
	CRV200::Frame frame;
	frame.width = (vga.crtc.horz_disp_end + 1) *
		((vga.gc.alpha_dis || (vga.sequencer.data[1] & 1)) ? 8 : 9);
	frame.height = vga.crtc.vert_disp_end + 1;
	if (!frame.width || frame.width > 4096 || frame.height > 1024)
		return {};
	// CVGA can write repeated scanlines past the visible-area bottom. Cover CGA
	// double-scan plus one character row; only the visible rectangle is used.
	m_render_bitmap.resize(frame.width, frame.height * 2 + 64);
	screen().set_visible_area(frame.width, frame.height);
	screen().tick_frame();
	vga.crtc.start_addr = latch_start_addr();
	vga.attribute.pel_shift = vga.attribute.pel_shift_latch;
	palette_update();
	CSVGA::screen_update(
		screen(), m_render_bitmap, rectangle(0, frame.width - 1, 0, frame.height - 1));
	frame.argb.assign(
		m_render_bitmap.raw(),
		m_render_bitmap.raw() + size_t(frame.width) * frame.height);
	return frame;
}

void CRadeon7500::update()
{
	const CRV200::Frame frame = render_frame();
	// Never hold the device-bus mutex while acquiring a GUI mutex.
	CRadeonDisplayLock guard(m_output.display());
	if (!frame.argb.empty())
	{
		if (frame.width != m_host_width || frame.height != m_host_height)
		{
			m_output.display().dimension_update(
				frame.width, frame.height, 0, 0, 32);
			m_host_width = frame.width;
			m_host_height = frame.height;
		}
		m_output.display().graphics_frame_update(
			frame.argb.data(), frame.width, frame.height);
	}
	else
	{
		m_output.display().clear_screen();
	}
	m_output.display().flush();
}

void CRadeon7500::run()
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
				CRadeonDisplayLock guard(m_output.display());
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

std::array<u32, 64> CRadeon7500::PCIConfig::config_data() const
{
	std::array<u32, 64> data{};
	data[0] = u32(vendor) | (u32(device) << 16);
	data[2] = (class_code << 8) | revision;
	data[4] = 0x00000008; // prefetchable framebuffer
	data[5] = 0x00000001; // I/O register window
	data[11] = u32(subsystem_vendor) | (u32(subsystem_device) << 16);
	data[15] = 0x000001ff; // INTA#, interrupt line unassigned
	// No AGP or power-management capability chain is modeled.
	return data;
}

std::array<u32, 64> CRadeon7500::PCIConfig::config_mask(u32 rom_size)
{
	std::array<u32, 64> mask{};
	mask[1] = 0x00000147; // I/O, memory, bus master, parity response, SERR
	mask[3] = 0x0000ffff; // cache-line size and latency timer
	mask[4] = ~(FramebufferBARSize - 1);
	mask[5] = ~(IOBARSize - 1);
	mask[6] = ~(RegisterBARSize - 1);
	if (rom_size)
		mask[12] = (~(rom_size - 1) & 0xfffff800u) | 1; // ROM decode and enable
	mask[15] = 0xff; // interrupt line only; pin fixed
	return mask;
}

CRadeon7500::OptionROM CRadeon7500::OptionROM::parse(
	const std::vector<uint8_t>& raw, const std::string& layout)
{
	auto fail = [](const char* message) {
		throw std::invalid_argument(message);
	};
	const size_t n = raw.size();
	if (n < 2048 || n > 1024 * 1024)
		fail("ROM must be 2 KiB to 1 MiB");
	if (layout != "exact" && layout != "pad_ff")
		fail("rom_layout must be exact or pad_ff");
	if (layout == "exact" && (n & (n - 1)))
		fail("Non-power-of-two ROM image requires rom_layout=pad_ff");
	auto word = [&](size_t p) -> uint16_t {
		if (p > n - 2)
			fail("ROM field out of bounds");
		return uint16_t(uint16_t(raw[p]) | (uint16_t(raw[p + 1]) << 8));
	};
	size_t at = 0;
	bool last = false;
	for (unsigned images = 0; images < 32 && !last; ++images)
	{
		if (at + 0x1a > n || raw[at] != 0x55 || raw[at + 1] != 0xaa)
			fail("ROM missing 55 AA signature");
		const size_t p = at + word(at + 0x18);
		if (p < at + 0x1a || p + 0x18 > n || raw[p] != 'P' ||
			raw[p + 1] != 'C' || raw[p + 2] != 'I' || raw[p + 3] != 'R')
			fail("ROM missing/bounded PCIR structure");
		if (word(p + 4) != 0x1002 || word(p + 6) != 0x5157)
			fail("ROM PCIR identity is not ATI 1002:5157 (RV200)");
		const size_t length = size_t(word(p + 0x10)) * 512;
		if (!length || length > n - at || p + 0x18 > at + length)
			fail("Invalid option-ROM image length");
		const size_t pcir_length = word(p + 0x0a);
		if (pcir_length < 0x18 || pcir_length > at + length - p)
			fail("Invalid PCIR structure length");
		if (raw[p + 0x14] == 0 &&
			(!raw[at + 2] || size_t(raw[at + 2]) * 512 > length))
			fail("Invalid x86 initialization image length");
		unsigned sum = 0;
		for (size_t i = at; i < at + length; ++i)
			sum += raw[i];
		if (sum & 255)
			fail("Option-ROM checksum failed");
		if (raw[p + 0x14] != 0 && raw[p + 0x14] != 3)
			fail("Only x86 and EFI option-ROM images are supported");
		last = (raw[p + 0x15] & 0x80) != 0;
		at += length;
	}
	if (!last)
		fail("Unterminated option-ROM image chain");
	OptionROM rom;
	rom.file_size = static_cast<uint32_t>(n);
	const std::string agp = "V200AGP";
	if (std::search(raw.begin(), raw.end(), agp.begin(), agp.end()) !=
		raw.end())
		fail("AGP ROM rejected for PCI 3X-PBXGG-AA target; choose a PCI image");
	rom.bytes = raw;
	if (layout == "pad_ff")
	{
		// Erased padding to the next power of two; the upper decode is unmeasured.
		size_t decoded = 2048;
		while (decoded < n)
			decoded *= 2;
		rom.bytes.resize(decoded, 0xff);
	}
	return rom;
}

// Construction, initialization and thread lifecycle

CRadeon7500::CRadeon7500(
	CConfigurator* cfg, CSystem* sys, int bus, int dev, bx_gui_c& display)
try
	: CSVGA(cfg, sys, bus, dev), m_output(0, display),
	  m_rv200(radeon_options(cfg))
{
	reset_vga();
	m_crtc_space_config = address_space_config("crtc_regs", ENDIANNESS_LITTLE, 8, 8, 0,
		address_map_constructor(FUNC(CRadeon7500::crtc_map), this));
	m_gc_space_config = address_space_config("gc_regs", ENDIANNESS_LITTLE, 8, 8, 0,
		address_map_constructor(FUNC(CRadeon7500::gc_map), this));
	m_seq_space_config = address_space_config("sequencer_regs", ENDIANNESS_LITTLE, 8, 8, 0,
		address_map_constructor(FUNC(CRadeon7500::sequencer_map), this));
	m_atc_space_config = address_space_config("attribute_regs", ENDIANNESS_LITTLE, 8, 8, 0,
		address_map_constructor(FUNC(CRadeon7500::attribute_map), this));
}
catch (const CException&)
{
	throw;
}
catch (const std::exception& error)
{
	FAILURE_1(
		Configuration, "Radeon configuration failed: %.650s", error.what());
}

void CRadeon7500::init()
try
{
	if (m_initialized)
		return;
	CConfigurator* cfg = myCfg;
	init_maps();
	add_legacy_io(LegacyIO, 0x3b0, 0x30);
	add_legacy_mem(LegacyMemory, 0xa0000, 0x20000);
	m_profile.revision =
		static_cast<uint8_t>(radeon_bounded_setting(cfg, "revision", 0, 255));
	m_profile.subsystem_vendor = static_cast<uint16_t>(
		radeon_bounded_setting(cfg, "subsystem_vendor", 0, 65535));
	m_profile.subsystem_device = static_cast<uint16_t>(
		radeon_bounded_setting(cfg, "subsystem_device", 0, 65535));
	m_scanline_access_divisor =
		radeon_setting(cfg, "scanline_access_divisor", 1024);
	if (m_scanline_access_divisor == 0)
		throw std::runtime_error("Invalid radeon scanline_access_divisor");
	m_trace_apertures = cfg->get_bool_value("trace_apertures", false);
	m_frame_file = cfg->get_text_value("frame_file", "");
	const std::string romname = cfg->get_text_value("rom", "");
	m_rom_layout = cfg->get_text_value("rom_layout", "exact");
	if (!romname.empty())
	{
		std::ifstream f(romname, std::ios::binary | std::ios::ate);
		if (!f)
			throw std::runtime_error("Cannot open radeon ROM: " + romname);
		const auto size = f.tellg();
		if (size < 2048 || size > 1024 * 1024)
			throw std::runtime_error("radeon ROM must be 2 KiB to 1 MiB");
		f.seekg(0);
		std::vector<uint8_t> raw(static_cast<size_t>(size));
		f.read(
			reinterpret_cast<char*>(raw.data()),
			static_cast<std::streamsize>(raw.size()));
		if (!f)
			throw std::runtime_error("Cannot read full radeon ROM");
		const auto parsed = OptionROM::parse(raw, m_rom_layout);
		m_rom = parsed.bytes;
#ifdef DEBUG_VGA
		printf(
			"%s: ROM %u bytes, layout=%s, decoded %u bytes.\n",
			devid_string,
			parsed.file_size,
			m_rom_layout.c_str(),
			unsigned(m_rom.size()));
#endif
	}
	const std::string tracename = cfg->get_text_value("trace", "");
	if (!tracename.empty())
	{
		if (std::filesystem::exists(tracename))
			throw std::runtime_error(
				"Refusing to overwrite existing radeon trace: " + tracename);
		m_trace.open(tracename);
		if (!m_trace)
			throw std::runtime_error("Cannot create radeon trace");
		m_trace << "# RV200 ES40 PCI/BAR traffic; values hex, sizes bits\n";
	}
	const auto profile_data = m_profile.config_data(),
			   profile_mask = PCIConfig::config_mask(u32(m_rom.size()));
	u32 data[64]{}, mask[64]{};
	for (unsigned i = 0; i < 64; ++i)
	{
		data[i] = profile_data[i];
		mask[i] = profile_mask[i];
	}
	add_function(0, data, mask);
	m_rv200.set_vga_callbacks(
		[this](uint32_t port) {
			return io_read_b(port);
		},
		[this](uint32_t port, uint8_t value) {
			io_write_b(port, value);
		});
	m_rv200.set_palette_index_callback([this]() {
		vga.dac.state = 0;
	});
	m_rv200.set_config_reader([this](uint32_t address) {
		return endian_32(pci_state.config_data[0][(address & 0xfc) / 4]);
	});
	m_rv200.set_dma_reader(
		[this](uint32_t address, uint8_t* destination, size_t count) {
			if (!(config_read(0, 4, 16) & 4))
				return false;
			// Use the shared PCI/IOMMU path: OpenVMS allocates its ring and
			// indirect buffers in scatter-gather mapped system memory.
			do_pci_read(address, destination, 1, count);
			if (m_trace && count == 4)
			{
				const u32 word = u32(destination[0]) | (u32(destination[1]) << 8) |
					(u32(destination[2]) << 16) | (u32(destination[3]) << 24);
				trace("DR", 0, address, 32, word);
			}
			return true;
		});
	m_rv200.set_dma_writer(
		[this](uint32_t address, const uint8_t* source, size_t count) {
			if (!(config_read(0, 4, 16) & 4))
				return false;
			do_pci_write(address, const_cast<uint8_t*>(source), 1, count);
			if (m_trace && count == 4)
			{
				const u32 word = u32(source[0]) | (u32(source[1]) << 8) |
					(u32(source[2]) << 16) | (u32(source[3]) << 24);
				trace("DW", 0, address, 32, word);
			}
			return true;
		});
	m_rv200.set_irq_callback([this](bool level) {
		if (!m_replaying_pci)
			do_pci_interrupt(0, level);
	});
	m_rv200.set_unimplemented_callback(
		[this](const char* what, uint32_t address, uint32_t value, bool write) {
			report_unimplemented(what, address, value, write);
		});
	m_rv200.set_diagnostic_callback(
		[this](const std::string& message, bool fatal) {
			// Hardware faults remain visible even when VGA debugging is disabled.
			bool show_diagnostic = fatal;
#ifdef DEBUG_VGA
			show_diagnostic = true;
#endif
			if (show_diagnostic)
				printf(
					"%s: %s%s.\n",
					devid_string,
					fatal ? "Graphics engine halted: " : "",
					message.c_str());
			if (m_trace)
			{
				m_trace << "# " << devid_string << ": "
						<< (fatal ? "Graphics engine halted: " : "") << message
						<< '\n';
				m_trace.flush();
			}
		});
	m_initialized = true;
	ResetPCI();
	printf(
		"%s: ATI Radeon 7500, RV200, %u MB VRAM.\n",
		devid_string,
		m_rv200.options().vram_bytes / (1024 * 1024));
}
catch (const CException&)
{
	throw;
}
catch (const std::exception& error)
{
	FAILURE_1(
		Configuration, "Radeon initialization failed: %.650s", error.what());
}

CRadeon7500::~CRadeon7500()
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

void CRadeon7500::reset_vga()
{
	// A blank VGA initialization, not a measured RV200 reset dump.
	vga = {};
	svga = {};
	vga.memory = m_rv200.vram_data();
	vga.svga_intf.vram_size = m_rv200.options().vram_bytes;
	vga.crtc.maximum_scan_line = 1;
	vga.crtc.line_compare = 0x3ff;
	vga.gc.bit_mask = 0xff;
	vga.sequencer.char_sel.base[0] = vga.sequencer.char_sel.base[1] = 0x20000;
	vga.dac.mask = 0xff;
	vga.dac.dirty = 1;
	m_ioas = false;
	m_vga_enable = 1;
	m_vga_scanline = m_vga_status_phase = 0;
}

void CRadeon7500::ResetPCI()
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (!m_initialized)
		return;
	m_rv200.reset();
	CPCIDevice::ResetPCI();
	sync_apertures();
	reset_vga();
	m_access_count = 0;
	if (m_trace)
		m_trace << "RESET\n";
}

void CRadeon7500::start_threads()
{
	m_pause_ack.store(false);
	m_pause.store(false);
	if (!m_thread)
	{
		m_stop.store(false);
		m_worker_failed.store(false);
		m_thread = new CThread("radeon");
		m_thread->start(*this);
	}
}

void CRadeon7500::stop_threads()
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

void CRadeon7500::sync_apertures()
{
	m_rv200.set_aperture_bases(
		endian_32(pci_state.config_data[0][4]) & ~(FramebufferBARSize - 1),
		endian_32(pci_state.config_data[0][6]) & ~(RegisterBARSize - 1));
}

bool CRadeon7500::legacy_enabled(bool memory) const noexcept
{
	if (!m_initialized)
		return false;
	const u32 command = endian_32(pci_state.config_data[0][1]);
	if (memory)
		return (command & 2) && m_vga_enable && (vga.miscellaneous_output & 2);
	return (command & 1) != 0;
}

bool CRadeon7500::vga_port_enabled(u32 port) const noexcept
{
	// The VGA enable register stays addressable while the frontend is off.
	if (port == 0x3c3)
		return true;
	if (!m_vga_enable)
		return false;
	if ((port == 0x3b4 || port == 0x3b5 || port == 0x3ba) && !m_ioas)
		return true;
	if ((port == 0x3d4 || port == 0x3d5 || port == 0x3da) && m_ioas)
		return true;
	return (port >= 0x3c0 && port <= 0x3ca) || port == 0x3cc || port == 0x3ce ||
		port == 0x3cf;
}

bool CRadeon7500::decodes_memory_access(
	int index, u64 address, int dsize, bool write) const noexcept
{
	if (index >= PCI_RANGE_BASE)
		return CPCIDevice::decodes_memory_access(index, address, dsize, write);
	if (dsize != 8 && dsize != 16 && dsize != 32 && dsize != 64)
		return false;
	const unsigned bytes = unsigned(dsize) / 8;
	if (index == LegacyIO)
	{
		if (!legacy_enabled(false) || address >= 0x30 || bytes > 0x30 - address)
			return false;
		// Claim an access only when every byte belongs to this device; reserved
		// port bytes must not make us a competing positive decoder.
		for (unsigned i = 0; i < bytes; ++i)
			if (!vga_port_enabled(0x3b0 + u32(address) + i))
				return false;
		return true;
	}
	if (index != LegacyMemory || !legacy_enabled(true) || address >= 0x20000 ||
		bytes > 0x20000 - address)
		return false;
	switch (vga.gc.memory_map_sel & 3)
	{
	case 1:
		return address + bytes <= 0x10000;
	case 2:
		return address >= 0x10000 && address + bytes <= 0x18000;
	case 3:
		return address >= 0x18000;
	default:
		return true;
	}
}

u32 CRadeon7500::ReadMem_Legacy(int index, u32 address, int dsize)
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
	if (m_trace)
		m_trace << "# VGA R " << index << ' ' << std::hex << address << ' '
				<< data << std::dec << " bits=" << dsize << '\n';
	return data;
}

void CRadeon7500::WriteMem_Legacy(int index, u32 address, int dsize, u32 data)
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
	if (m_trace)
		m_trace << "# VGA W " << index << ' ' << std::hex << address << ' '
				<< data << std::dec << " bits=" << dsize << '\n';
}

u32 CRadeon7500::ReadMem_Bar(int func, int bar, u32 address, int dsize)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (func != 0 || (dsize != 8 && dsize != 16 && dsize != 32))
		return 0xffffffff;
	tick();
	u32 data = 0xffffffff;
	switch (bar)
	{
	case 0:
		data = u32(m_rv200.mem_read(address, dsize));
		break;
	case 1:
		if (address <= IOBARSize - u32(dsize) / 8)
			data = m_rv200.ReadMem(address, dsize);
		break;
	case 2:
		data = m_rv200.ReadMem(address, dsize);
		break;
	case 6:
		// BUS_CNTL can hide the option ROM from the BAR.
		if (!(m_rv200.peek(CRV200::BUS_CNTL) & CRV200::BUS_BIOS_DIS_ROM) &&
			!m_rom.empty() && address <= m_rom.size() - u32(dsize) / 8)
		{
			data = 0;
			for (int i = 0; i < dsize / 8; ++i)
				data |= u32(m_rom[address + static_cast<unsigned>(i)])
					<< (8 * i);
		}
		break;
	}
	trace("BR", bar, address, dsize, data);
	return data;
}

void CRadeon7500::WriteMem_Bar(
	int func, int bar, u32 address, int dsize, u32 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (func != 0 || (dsize != 8 && dsize != 16 && dsize != 32))
		return;
	tick();
	trace("BW", bar, address, dsize, data);
	switch (bar)
	{
	case 0:
		m_rv200.mem_write(address, dsize, data);
		break;
	case 1:
		if (address <= IOBARSize - u32(dsize) / 8)
			m_rv200.WriteMem(address, dsize, data);
		break;
	case 2:
		m_rv200.WriteMem(address, dsize, data);
		break;
		// ROM writes intentionally discarded: no flash programming model.
	}
}

u64 CRadeon7500::ReadMem(int index, u64 address, int dsize)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (dsize == 64)
	{
		// Validate the whole legacy access so it is never half-applied.
		if (index < PCI_RANGE_BASE &&
			!decodes_memory_access(index, address, dsize, false))
			return ~u64(0);
		const u64 lo = CPCIDevice::ReadMem(index, address, 32);
		const u64 hi = CPCIDevice::ReadMem(index, address + 4, 32);
		return lo | (hi << 32);
	}
	return CPCIDevice::ReadMem(index, address, dsize);
}

void CRadeon7500::WriteMem(int index, u64 address, int dsize, u64 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (dsize == 64)
	{
		if (index < PCI_RANGE_BASE &&
			!decodes_memory_access(index, address, dsize, true))
			return;
		CPCIDevice::WriteMem(index, address, 32, u32(data));
		CPCIDevice::WriteMem(index, address + 4, 32, u32(data >> 32));
		return;
	}
	CPCIDevice::WriteMem(index, address, dsize, data);
}

u32 CRadeon7500::config_read_custom(int func, u32 address, int dsize, u32 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (func == 0 && !m_replaying_pci)
		trace("PR", func, address, dsize, data);
	return data;
}

void CRadeon7500::config_write_custom(
	int func, u32 address, int dsize, u32 old_data, u32 new_data, u32 data)
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	(void)old_data;
	(void)new_data;
	if (func != 0 || m_replaying_pci)
		return;
	trace("PW", func, address, dsize, data);
	sync_apertures();
	do_pci_interrupt(0, m_rv200.irq_asserted());
}

// Device service and saved state

void CRadeon7500::check_state()
{
	if (m_worker_failed.load())
		FAILURE(Thread, "Radeon display thread failed");
	// ES40 currently calls this roughly every 100ms. This compatibility pump
	// advances ONE virtual frame per call, not a real 60 Hz monitor model.
	{
		std::lock_guard<std::recursive_mutex> guard(
			cSystem->get_device_bus_mutex());
		const u32 lines = m_rv200.vertical_total(m_rv200.display_controller());
		m_rv200.advance_scanlines(lines);
		if (m_trace)
		{
			m_trace << "STEP " << lines << '\n';
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

void CRadeon7500::prepare_snapshot()
{
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	if (m_trace)
		m_trace.flush();
}

void CRadeon7500::finalize_restore() noexcept
{
	try
	{
		std::lock_guard<std::recursive_mutex> guard(
			cSystem->get_device_bus_mutex());
		do_pci_interrupt(0, m_rv200.irq_asserted());
	}
	catch (...)
	{
	}
}

std::string CRadeon7500::snapshot_identity() const
{
	std::ostringstream out;
	out << "Radeon7500-es40-v1:" << std::hex << m_profile.vendor << ':'
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
	const auto& options = m_rv200.options();
	out << ':' << hash << ':' << m_rom_layout << ':' << m_rom.size() << ':'
		<< m_scanline_access_divisor << ':' << options.vram_bytes << ':'
		<< options.strict_mmio;
	// Include the PCI location so otherwise identical cards cannot exchange state.
	out << "|path=" << get_device_path();
	return out.str();
}

std::vector<u8> CRadeon7500::save_vga_state() const
{
	std::vector<u8> bytes{1};
	for (unsigned i = 0; i < 25; ++i)
		bytes.push_back(m_crtc_map.read_byte(i));
	for (unsigned i = 0; i < 5; ++i)
		bytes.push_back(m_seq_map.read_byte(i));
	for (unsigned i = 0; i < 9; ++i)
		bytes.push_back(m_gc_map.read_byte(i));
	for (unsigned i = 0; i < 21; ++i)
		bytes.push_back(m_atc_map.read_byte(i));
	bytes.insert(
		bytes.end(),
		{vga.crtc.index,
		 vga.sequencer.index,
		 vga.gc.index,
		 vga.attribute.index,
		 vga.miscellaneous_output,
		 vga.feature_control,
		 m_vga_enable,
		 vga.dac.mask,
		 u8(vga.attribute.state),
		 u8(vga.dac.read),
		 u8(vga.dac.state)});
	for (const u8 value : vga.dac.loading)
		bytes.push_back(value);
	for (const u8 value : vga.gc.latch)
		bytes.push_back(value);
	for (unsigned i = 0; i < 4; ++i)
		bytes.push_back(u8(vga.crtc.start_addr >> (8 * i)));
	bytes.push_back(vga.attribute.pel_shift);
	bytes.push_back(vga.cursor.visible);
	bytes.push_back(vga.crtc.irq_latch);
	for (unsigned i = 0; i < 4; ++i)
		bytes.push_back(u8(m_vga_scanline >> (8 * i)));
	for (unsigned i = 0; i < 4; ++i)
		bytes.push_back(u8(m_vga_status_phase >> (8 * i)));
	return bytes;
}

void CRadeon7500::restore_vga_state(const std::vector<u8>& bytes)
{
	auto dword = [&](size_t offset) {
		return u32(bytes[offset]) | (u32(bytes[offset + 1]) << 8) |
			(u32(bytes[offset + 2]) << 16) | (u32(bytes[offset + 3]) << 24);
	};
	if (bytes.size() != VGAStateSize || bytes[0] != 1 || bytes[67] > 1 ||
		bytes[69] > 1 || bytes[70] > 1 || bytes[71] > 2 || dword(79) > 0xffff ||
		bytes[83] > 15 || bytes[84] > 1 || bytes[85] > 1 || dword(86) > 1024 ||
		dword(90) > 31)
		throw std::runtime_error("Invalid VGA snapshot fields");
	reset_vga();
	// Restore CR11 protection LAST so CR0-7 are not discarded while loading.
	for (unsigned i = 0; i < 25; ++i)
		if (i != 0x11)
			m_crtc_map.write_byte(i, bytes[1 + i]);
	m_crtc_map.write_byte(0x11, bytes[1 + 0x11]);
	for (unsigned i = 0; i < 5; ++i)
		m_seq_map.write_byte(i, bytes[26 + i]);
	for (unsigned i = 0; i < 9; ++i)
		m_gc_map.write_byte(i, bytes[31 + i]);
	for (unsigned i = 0; i < 21; ++i)
		m_atc_map.write_byte(i, bytes[40 + i]);
	// Every register must read back exactly as saved.
	for (unsigned i = 0; i < 25; ++i)
		if (m_crtc_map.read_byte(i) != bytes[1 + i])
			throw std::runtime_error("Invalid CRTC snapshot encoding");
	for (unsigned i = 0; i < 5; ++i)
		if (m_seq_map.read_byte(i) != bytes[26 + i])
			throw std::runtime_error("Invalid sequencer snapshot encoding");
	for (unsigned i = 0; i < 9; ++i)
		if (m_gc_map.read_byte(i) != bytes[31 + i])
			throw std::runtime_error(
				"Invalid graphics-controller snapshot encoding");
	for (unsigned i = 0; i < 21; ++i)
		if (m_atc_map.read_byte(i) != bytes[40 + i])
			throw std::runtime_error("Invalid attribute snapshot encoding");
	vga.crtc.index = bytes[61];
	vga.sequencer.index = bytes[62];
	vga.gc.index = bytes[63];
	vga.attribute.index = bytes[64];
	vga.miscellaneous_output = bytes[65];
	m_ioas = (bytes[65] & 1) != 0;
	vga.feature_control = bytes[66];
	m_vga_enable = bytes[67];
	vga.dac.mask = bytes[68];
	vga.attribute.state = bytes[69];
	vga.dac.read = bytes[70];
	vga.dac.state = bytes[71];
	for (unsigned i = 0; i < 3; ++i)
		vga.dac.loading[i] = bytes[72 + i];
	for (unsigned i = 0; i < 4; ++i)
		vga.gc.latch[i] = bytes[75 + i];
	vga.crtc.start_addr = dword(79);
	vga.attribute.pel_shift = bytes[83];
	vga.cursor.visible = bytes[84];
	vga.crtc.irq_latch = bytes[85];
	m_vga_scanline = dword(86);
	m_vga_status_phase = dword(90);
	vga.dac.dirty = 1;
}

int CRadeon7500::SaveState(FILE* f)
{
	if (!f)
		return -1;
	std::lock_guard<std::recursive_mutex> guard(
		cSystem->get_device_bus_mutex());
	try
	{
		std::ostringstream stream(std::ios::binary);
		m_rv200.SaveState(stream);
		const std::string bytes = stream.str();
		const std::vector<u8> vga_bytes = save_vga_state();
		if (CPCIDevice::SaveState(f))
			return -1;
		radeon_write_u32(
			f, 0x31563752); // R7V1: portable VGA frontend plus core
		radeon_write_u32(f, u32(vga_bytes.size()));
		if (fwrite(vga_bytes.data(), 1, vga_bytes.size(), f) !=
			vga_bytes.size())
			return -1;
		radeon_write_u32(f, u32(m_access_count));
		radeon_write_u32(f, u32(m_access_count >> 32));
		radeon_write_u32(f, u32(bytes.size()));
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

int CRadeon7500::RestoreState(FILE* f)
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
		if (radeon_read_u32(f) != 0x31563752 ||
			radeon_read_u32(f) != VGAStateSize)
			return -1;
		std::vector<u8> vga_bytes(VGAStateSize);
		if (fread(vga_bytes.data(), 1, vga_bytes.size(), f) != vga_bytes.size())
			return -1;
		const u32 lo = radeon_read_u32(f), hi = radeon_read_u32(f),
				  n = radeon_read_u32(f);
		const u32 vram = m_rv200.options().vram_bytes;
		if (n < vram || n > vram + 65536 + 16)
			return -1;
		std::string bytes(n, '\0');
		if (fread(bytes.data(), 1, n, f) != n)
			return -1;
		std::istringstream stream(bytes, std::ios::binary);
		m_rv200.RestoreState(stream);
		// The core's transactional loader owns its VRAM; refresh our borrowed view.
		vga.memory = m_rv200.vram_data();
		if (stream.peek() != std::char_traits<char>::eof())
			return -1;
		restore_vga_state(vga_bytes);
		m_access_count = u64(lo) | (u64(hi) << 32);
		sync_apertures();
		// IRQ output is committed by finalize_restore after all devices restore.
		return 0;
	}
	catch (const std::exception& e)
	{
		vga.memory = m_rv200.vram_data();
		printf(
			"%s: Could not restore graphics state: %s.\n",
			devid_string,
			e.what());
		return -1;
	}
}

// Byte I/O and diagnostic output

void CRadeon7500::advance_vga_scanlines(u32 lines)
{
	const u32 total = u32(vga.crtc.vert_total) + 2;
	m_vga_scanline = (m_vga_scanline + (lines % total)) % total;
}

u8 CRadeon7500::input_status()
{
	vga.attribute.state = 0;
	// Deterministic compatibility timing: 32 status reads per synthetic
	// scanline. Decode probes and host frame delivery do not clock it.
	const bool native = m_rv200.native_display();
	if (++m_vga_status_phase == 32)
	{
		m_vga_status_phase = 0;
		if (native)
			m_rv200.advance_scanlines(1);
		else
			advance_vga_scanlines(1);
	}
	if (native)
	{
		// Approximate retrace with the native vertical blank.
		const unsigned controller = m_rv200.display_controller();
		const bool blank = m_rv200.scanline(controller) >=
			m_rv200.vertical_display(controller);
		return u8((blank ? 9 : 0) | (m_vga_status_phase >= 24 ? 1 : 0));
	}
	const u32 total = u32(vga.crtc.vert_total) + 2;
	const u32 start = vga.crtc.vert_retrace_start % total;
	u32 length = (vga.crtc.vert_retrace_end - vga.crtc.vert_retrace_start) & 15;
	if (!length)
		length = 16;
	const bool retrace = ((m_vga_scanline + total - start) % total) < length;
	const bool blank =
		m_vga_scanline > vga.crtc.vert_disp_end || m_vga_status_phase >= 24;
	return u8((retrace ? 8 : 0) | (blank ? 1 : 0));
}

u8 CRadeon7500::io_read_b(u32 port)
{
	// MMIO aliases bypass the PCI I/O enable, but not the VGA enable or the
	// monochrome/colour port selection.
	if (!m_initialized || !vga_port_enabled(port))
		return 0xff;
	switch (port)
	{
	case 0x3ba:
	case 0x3da:
		return input_status();
	case 0x3c3:
		return m_vga_enable;
	case 0x3c6:
		return vga.dac.mask;
	case 0x3c7:
		return vga.dac.read ? 3 : 0;
	case 0x3c8:
		return u8(m_rv200.peek(CRV200::PALETTE_INDEX));
	case 0x3c9:
	{
		// PALETTE_INDEX holds the write index in bits 7:0, read index in 23:16.
		const u32 index = (m_rv200.peek(CRV200::PALETTE_INDEX) >> 16) & 255;
		u8 value = u8(m_rv200.palette_color(index, m_rv200.palette_bank()) >>
			(16 - 8 * vga.dac.state));
		if (!(m_rv200.peek(CRV200::DAC_CNTL) & CRV200::DAC_8BIT_EN))
			value >>= 2;
		if (++vga.dac.state == 3)
		{
			vga.dac.state = 0;
			m_rv200.WriteMem(
				CRV200::PALETTE_INDEX,
				32,
				(m_rv200.peek(CRV200::PALETTE_INDEX) & ~0xff0000u) |
					(((index + 1) & 255) << 16));
		}
		return value;
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

void CRadeon7500::io_write_b(u32 port, u8 data)
{
	if (!m_initialized || !vga_port_enabled(port))
		return;
	switch (port)
	{
	case 0x3c3:
		m_vga_enable = data & 1;
		break;
	case 0x3c6:
		vga.dac.mask = data;
		vga.dac.dirty = 1;
		break;
	case 0x3c7:
		vga.dac.state = 0;
		vga.dac.read = 1;
		m_rv200.WriteMem(
			CRV200::PALETTE_INDEX,
			32,
			(m_rv200.peek(CRV200::PALETTE_INDEX) & ~0xff0000u) |
				(u32(data) << 16));
		break;
	case 0x3c8:
		vga.dac.state = 0;
		vga.dac.read = 0;
		m_rv200.WriteMem(
			CRV200::PALETTE_INDEX,
			32,
			(m_rv200.peek(CRV200::PALETTE_INDEX) & ~255u) | data);
		break;
	case 0x3c9:
	{
		const u32 index = m_rv200.peek(CRV200::PALETTE_INDEX) & 255;
		// DAC_8BIT_EN clear: expand six-bit VGA components.
		if (!(m_rv200.peek(CRV200::DAC_CNTL) & CRV200::DAC_8BIT_EN))
			data = u8(((data & 63) << 2) | ((data & 63) >> 4));
		vga.dac.loading[vga.dac.state++] = data;
		if (vga.dac.state == 3)
		{
			vga.dac.state = 0;
			m_rv200.set_palette_color(
				index,
				(u32(vga.dac.loading[0]) << 16) |
					(u32(vga.dac.loading[1]) << 8) | vga.dac.loading[2],
				m_rv200.palette_bank());
			m_rv200.WriteMem(
				CRV200::PALETTE_INDEX,
				32,
				(m_rv200.peek(CRV200::PALETTE_INDEX) & ~255u) |
					((index + 1) & 255));
			vga.dac.dirty = 1;
		}
		break;
	}
	default:
		if (!vga_io_write(port, data))
			report_unimplemented("VGA port", port, data, true);
		break;
	}
}

void CRadeon7500::tick()
{
	// Access-driven scanline phase for guests that poll native CRTC status.
	if (++m_access_count % m_scanline_access_divisor == 0)
	{
		m_rv200.advance_scanlines(1);
		if (m_trace)
			m_trace << "STEP 1\n";
	}
}

void CRadeon7500::trace(
	const char* op, int bar, u32 address, int bits, u32 value)
{
	// Framebuffer traffic is voluminous; it is traced only on request.
	if (!m_trace || (bar == 0 && op[0] == 'B' && !m_trace_apertures))
		return;
	m_trace << op << ' ' << bar << " 0x" << std::hex << address << std::dec
			<< ' ' << bits << " 0x" << std::hex << value << std::dec << '\n';
}

void CRadeon7500::trace_unimplemented(const std::string& text)
{
	if (m_trace)
	{
		m_trace << "# " << devid_string << ": " << text << '\n';
		m_trace.flush();
	}
}

void CRadeon7500::publish_frame()
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
			throw std::runtime_error("Cannot open radeon frame output");
		CRV200::write_ppm(out, f);
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
			"Cannot publish radeon frame: " + ec.message());
}
