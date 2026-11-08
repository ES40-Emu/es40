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

    Cirrus Logic GD542x/3x video chipsets

*/
#ifndef INCLUDED_Cirrus_H_
#define INCLUDED_Cirrus_H_

#pragma once

#include "SVGA.h"
#include "gui/gui.h"
#include <atomic>
#include <chrono>
#include "emu/coretmpl.h"
#include "emu/attotime.h"
#include "emu/mame_shims.h"
#include "emu/address_map.h"

// ES40 adapter: frontend integration, compatibility behavior and snapshot state.
// Active declarations below retain their existing ownership and access.
class CCirrus : public svga_device, public CRunnable,
  public CDisplayOutputProvider
{
public:
  virtual int   SaveState(FILE* f) override;
  virtual int   RestoreState(FILE* f) override;
  virtual void  check_state() override;
  void ResetPCI() override;
  virtual void  WriteMem_Legacy(int index, u32 address, int dsize, u32 data) override;
  virtual u32   ReadMem_Legacy(int index, u32 address, int dsize) override;
  bool decodes_memory_access(int index, u64 address, int dsize,
    bool write) const noexcept override;

  virtual void  WriteMem_Bar(int func, int bar, u32 address, int dsize,
    u32 data) override;
  virtual u32   ReadMem_Bar(int func, int bar, u32 address, int dsize) override;

  virtual u64 ReadMem(int index, u64 address, int dsize) override;
  virtual void WriteMem(int index, u64 address, int dsize, u64 data) override;

  virtual       ~CCirrus();

  std::size_t output_count() const override { return 1; }
  const CDisplayOutput* output_at(std::size_t ordinal) const override
  {
    return ordinal == 0 ? &m_output : nullptr;
  }

  void          update(void);
  virtual void  run(void) override;

  virtual u8    get_actl_palette_idx(u8 index) override;
  virtual void  redraw_area(unsigned x0, unsigned y0, unsigned width,
    unsigned height) override;

  virtual void  init() override;
  virtual void  start_threads() override;
  virtual void  stop_threads() override;

protected:
  enum class Model : u32 { GD5401, GD5428, GD5430, GD5446, AVGA1 };
  CCirrus(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev,
    bx_gui_c& display, Model model);

  void recompute_params_clock(int divisor, int xtal);

public:

	virtual uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect) override;

protected:

	// device-level overrides
	virtual uint16_t offset() override;
	virtual uint32_t latch_start_addr() override;

  struct {
    u8 m_hidden_dac_mode = 0;
    u8 m_hidden_dac_phase = 0;
    uint8_t m_chip_id;

    uint8_t gc_mode_ext = 0;
    uint8_t gc_bank[2]{};
    bool gc_locked = false;
    uint8_t m_lock_reg = 0;
    uint8_t m_gr10 = 0;  // high byte of background colour (in 15/16bpp)
    uint8_t m_gr11 = 0;  // high byte of foreground colour (in 15/16bpp)

    uint8_t m_cr19 = 0;
    uint8_t m_cr1a = 0;
    uint8_t m_cr1b = 0;

    // hardware cursor
    uint16_t m_cursor_x = 0;
    uint16_t m_cursor_y = 0;
    uint16_t m_cursor_addr = 0;
    uint8_t m_cursor_attr = 0;
    bool m_ext_palette_enabled = false;
    struct { uint8_t red, green, blue; } m_ext_palette[16];  // extra palette, colour 0 is cursor background, colour 15 is cursor foreground, colour 2 is overscan border colour

    // BitBLT engine
    uint8_t m_blt_status = 0;
    uint8_t m_blt_rop = 0;
    uint8_t m_blt_mode = 0;
    uint8_t m_blt_write_mask = 0;
    uint32_t m_blt_source = 0;
    uint32_t m_blt_dest = 0;
    uint16_t m_blt_source_pitch = 0;
    uint16_t m_blt_dest_pitch = 0;
    uint16_t m_blt_height = 0;
    uint16_t m_blt_width = 0;
    uint32_t m_blt_source_current = 0;
    uint32_t m_blt_dest_current = 0;
    uint16_t m_blt_trans_colour = 0;
    uint16_t m_blt_trans_colour_mask = 0;

    bool m_blt_system_transfer = false;  // blit from system memory
    uint8_t m_blt_system_count = 0;
    uint32_t m_blt_system_buffer = 0;
    uint16_t m_blt_pixel_count = 0;
    uint16_t m_blt_scan_count = 0;

    uint8_t m_scratchpad1 = 0;
    uint8_t m_scratchpad2 = 0;
    uint8_t m_scratchpad3 = 0;
    uint8_t m_vclk_num[4]{};
    uint8_t m_vclk_denom[4]{};

    uint8_t m_cr1d = 0;
    uint8_t m_sr17 = 1;
  } cirrus{};

	virtual uint8_t vga_latch_write(int offs, uint8_t data) override;

	virtual bool get_interlace_mode() override { return BIT(cirrus.m_cr1a, 0); }

private:
  Model m_model;
  bool is_avga() const noexcept { return m_model == Model::GD5401 || m_model == Model::AVGA1; }
  bool is_pci() const noexcept { return m_model == Model::GD5430 || m_model == Model::GD5446; }
  // Cirrus has one modeled output.
  CDisplayOutput m_output;
  // ATC index 0x00..0x0F: Palette registers
  inline u8  atc_palette(u8 idx) const { return vga.attribute.data[idx & 0x0f]; }

  void  get_display_size(unsigned& width, unsigned& height);
  u32   io_read(u32 address, int dsize);
  void  io_write(u32 address, int dsize, u32 data);
  void  io_write_b(u32 address, u8 data);
  u8 scan_status(std::chrono::steady_clock::time_point now);
  void advance_scan(std::chrono::steady_clock::time_point now);
  // CRTC counter phase is guest state; the host-time anchor is rebuilt on resume.
  struct ScanPosition {
    double character = 0.0;
    u32 line = 0;
    u32 field = 0;
  } m_scan;
  struct ScanTiming {
    double characters_per_second;
    double horizontal_retrace_width;
    u32 horizontal_total;
    u32 horizontal_retrace_start;
    u32 vertical_total;
    u32 vertical_retrace_start;
    u32 vertical_retrace_width;
    u32 vertical_divisor;
    u32 interlace_offset;
    bool interlace;
    bool sync_enabled;
  };
  ScanTiming scan_timing() const;
  std::chrono::steady_clock::time_point m_scan_time{};
  bool m_scan_initialized = false;
  bool m_scan_paused = false;
  u32   legacy_read(u32 address, int dsize);
  void  legacy_write(u32 address, int dsize, u32 data);

  u32   rom_read(u32 address, int dsize);
  // Each card loads its own ROM image from configuration.
  unsigned int rom_max = 0;
  u8 option_rom[65536] = {};
  CThread* myThread = nullptr;
  bool  StopThread = false;
  // Keep SDL window alive across firmware reset:
  //  - PauseThread is set by stop_threads() when system reset is in progress
  //  - PauseAck is raised by the Cirrus thread once it is safely paused
  std::atomic<bool> PauseThread{ false };
  std::atomic<bool> PauseAck{ false };
  bool m_gui_initialized = false;
  // Host dimension cache for this card's single scanout; not snapshot data.
  unsigned old_iHeight = 0;
  unsigned old_iWidth = 0;
  // computed video timing, MAME screen().configure() parameters
  struct {
    int      pixel_clock_hz = 0;    // computed pixel clock in Hz
    int      xtal_hz = 0;    // base or PLL-derived crystal frequency
    int      divisor = 1;    // VCLK divisor from color mode
    double   vrefresh_hz;          // vertical refresh rate derived from CRTC
    uint64_t refresh_interval_ms;  // milliseconds between redraws
  } timing{};
  // screen refresh stuff
  std::chrono::steady_clock::time_point m_last_refresh_time{};

};

// Upstream model declarations retain pc_vga_cirrus.h order and layout.
// Members still provided by the ES40 adapter remain commented in place.
// GD5401/AVGA1 retain the existing SVGA-backed compatibility behavior.
class cirrus_gd5401_vga_device : public CCirrus
{
public:
//	cirrus_gd5401_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
	cirrus_gd5401_vga_device(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev,
		bx_gui_c& display, bool avga1 = false);

protected:
	virtual void recompute_params() override;
};

class cirrus_gd5428_vga_device :  public CCirrus
{
public:
	// MT09345 / MT09374, SDD can crash at higher res
//	static constexpr feature_type imperfect_features() { return feature::GRAPHICS; }

	// construction/destruction
//	cirrus_gd5428_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
	cirrus_gd5428_vga_device(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev,
		bx_gui_c& display);

	virtual uint8_t mem_r(offs_t offset) override;
	virtual void mem_w(offs_t offset, uint8_t data) override;

//	virtual uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect) override;

protected:
//	cirrus_gd5428_vga_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);
	cirrus_gd5428_vga_device(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev,
		bx_gui_c& display, Model model);

	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
//	virtual uint16_t offset() override;
//	virtual uint32_t latch_start_addr() override;

	virtual void io_3cx_map(address_map &map) override ATTR_COLD;

	u8 ramdac_hidden_mask_r(offs_t offset);
	void ramdac_hidden_mask_w(offs_t offset, u8 data);
	u8 ramdac_overlay_r(offs_t offset);
	void ramdac_overlay_w(offs_t offset, u8 data);
//	u8 m_hidden_dac_mode = 0;
//	u8 m_hidden_dac_phase = 0;
//	uint8_t m_chip_id;

//	uint8_t gc_mode_ext = 0;
//	uint8_t gc_bank[2]{};
//	bool gc_locked = false;
//	uint8_t m_lock_reg = 0;
//	uint8_t m_gr10 = 0;  // high byte of background colour (in 15/16bpp)
//	uint8_t m_gr11 = 0;  // high byte of foreground colour (in 15/16bpp)

//	uint8_t m_cr19 = 0;
//	uint8_t m_cr1a = 0;
//	uint8_t m_cr1b = 0;

	// hardware cursor
//	uint16_t m_cursor_x = 0;
//	uint16_t m_cursor_y = 0;
//	uint16_t m_cursor_addr = 0;
//	uint8_t m_cursor_attr = 0;
//	bool m_ext_palette_enabled = false;
//	struct { uint8_t red, green, blue; } m_ext_palette[16];  // extra palette, colour 0 is cursor background, colour 15 is cursor foreground, colour 2 is overscan border colour

	// BitBLT engine
//	uint8_t m_blt_status = 0;
//	uint8_t m_blt_rop = 0;
//	uint8_t m_blt_mode = 0;
//	uint8_t m_blt_write_mask = 0;
//	uint32_t m_blt_source = 0;
//	uint32_t m_blt_dest = 0;
//	uint16_t m_blt_source_pitch = 0;
//	uint16_t m_blt_dest_pitch = 0;
//	uint16_t m_blt_height = 0;
//	uint16_t m_blt_width = 0;
//	uint32_t m_blt_source_current = 0;
//	uint32_t m_blt_dest_current = 0;
//	uint16_t m_blt_trans_colour = 0;
//	uint16_t m_blt_trans_colour_mask = 0;

//	bool m_blt_system_transfer = false;  // blit from system memory
//	uint8_t m_blt_system_count = 0;
//	uint32_t m_blt_system_buffer = 0;
//	uint16_t m_blt_pixel_count = 0;
//	uint16_t m_blt_scan_count = 0;

//	uint8_t m_scratchpad1 = 0;
//	uint8_t m_scratchpad2 = 0;
//	uint8_t m_scratchpad3 = 0;
//	uint8_t m_vclk_num[4]{};
//	uint8_t m_vclk_denom[4]{};

//	virtual uint8_t vga_latch_write(int offs, uint8_t data) override;

	virtual void crtc_map(address_map &map) override ATTR_COLD;
	virtual void gc_map(address_map &map) override ATTR_COLD;
	virtual void sequencer_map(address_map &map) override ATTR_COLD;

//	virtual bool get_interlace_mode() override { return BIT(m_cr1a, 0); }

	uint8_t offset_select(offs_t offset);
	virtual void recompute_params() override;

private:

	void start_bitblt();
	void start_reverse_bitblt();
	void start_system_bitblt();
	void blit_dword();
	void blit_byte();  // used for colour expanded system-to-vram bitblts
	void copy_pixel(uint8_t src, uint8_t dst, bool foreground = true, unsigned dst_byte_offset = 0);
};

class cirrus_gd5430_vga_device :  public cirrus_gd5428_vga_device
{
public:
//	cirrus_gd5430_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
	cirrus_gd5430_vga_device(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev,
		bx_gui_c& display);

	virtual uint8_t mem_r(offs_t offset) override;
	virtual void mem_w(offs_t offset, uint8_t data) override;

protected:
//	cirrus_gd5430_vga_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);
	cirrus_gd5430_vga_device(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev,
		bx_gui_c& display, Model model);

	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	virtual void crtc_map(address_map &map) override ATTR_COLD;
	virtual void gc_map(address_map &map) override ATTR_COLD;
	virtual void sequencer_map(address_map &map) override ATTR_COLD;
private:
	static u8 mmio_register(offs_t offset);
	u8 mmio_r(offs_t offset);
	void mmio_w(offs_t offset, u8 data);

//	uint8_t m_cr1d = 0;
//	uint8_t m_sr17 = 1;
};

class cirrus_gd5446_vga_device :  public cirrus_gd5430_vga_device
{
public:
//	cirrus_gd5446_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
	cirrus_gd5446_vga_device(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev,
		bx_gui_c& display);

protected:
	virtual void device_start() override ATTR_COLD;

	virtual void crtc_map(address_map &map) override ATTR_COLD;
	virtual void gc_map(address_map &map) override ATTR_COLD;
	virtual void sequencer_map(address_map &map) override ATTR_COLD;
};


// device type definition
//DECLARE_DEVICE_TYPE(CIRRUS_GD5401_VGA, cirrus_gd5401_vga_device)
//DECLARE_DEVICE_TYPE(CIRRUS_GD5428_VGA, cirrus_gd5428_vga_device)
//DECLARE_DEVICE_TYPE(CIRRUS_GD5430_VGA, cirrus_gd5430_vga_device)
//DECLARE_DEVICE_TYPE(CIRRUS_GD5446_VGA, cirrus_gd5446_vga_device)


#endif // INCLUDED_Cirrus_H_
