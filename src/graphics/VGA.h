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

#if !defined(__VGA_H__)
#define __VGA_H__

#include "emu/PCIDevice.h"
#include "emu/mame_shims.h"
#include "emu/address_map.h"
#include "emu/coretmpl.h"
#include "ibm8514a.h"
#include <set>
#include <string>
#include <tuple>

using offs_t = uint32_t;

/**
 * \brief Abstract base class for PCI VGA cards.
 **/
class CVGA : public CPCIDevice, public mame_machine_provider
{
  friend class ibm8514a_device;
public:
  CVGA(class CConfigurator* cfg, class CSystem* c, int pcibus, int pcidev);
  ~CVGA(void);

  virtual u8    get_actl_palette_idx(u8 index) = 0;
  virtual void  redraw_area(unsigned x0, unsigned y0, unsigned width,
    unsigned height) = 0;

  // construction/destruction
  //vga_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock);

  virtual void zero();
  virtual uint32_t screen_update(screen_device& screen, bitmap_rgb32& bitmap, const rectangle& cliprect);

  //void io_map(address_map& map) ATTR_COLD;

  // $46e8, $56e8, $66e8, $76e8 for ISA bus
  //void mode_setup_w(offs_t offset, uint8_t data);
  // $102 ISA bus / $3c3 MCA bus
  //void wakeup_w(offs_t offset, uint8_t data);

  virtual uint8_t mem_r(offs_t offset);
  virtual void mem_w(offs_t offset, uint8_t data);
  virtual uint8_t mem_linear_r(offs_t offset);
  virtual void mem_linear_w(offs_t offset, uint8_t data);

  void set_offset(uint16_t val) { vga.crtc.offset = val; }
  void set_vram_size(size_t vram_size) { vga.svga_intf.vram_size = vram_size; }
  //auto vsync_cb() { return m_vsync_cb.bind(); }
  // FIXME: should be protected, but virge_pci.cpp violates this
  //inline uint16_t get_crtc_port() { return BIT(vga.miscellaneous_output, 0) ? 0x3d0 : 0x3b0; }

protected:
  // es40 specific
  pen_t m_pen_table[256] = {};

  inline pen_t pen(uint8_t index) const { return m_pen_table[index & 0xFF]; }

  inline pen_t black_pen() const { return 0xFF000000u; }

  inline void set_pen_color(int index, uint8_t r, uint8_t g, uint8_t b)
  {
    m_pen_table[index & 0xFF] = 0xFF000000u
      | (uint32_t(r) << 16)
      | (uint32_t(g) << 8)
      | uint32_t(b);
  }

  screen_device m_screen_shim;
  screen_device& screen() { return m_screen_shim; }
  bitmap_rgb32 m_render_bitmap;

  // Reports an unimplemented register or port once per card: printed (up to
  // a limit) and passed to trace_unimplemented().
  void report_unimplemented(const char* what, u32 index, u32 value, bool write);
  // Catch-all for indices no entry claims; install before the real entries.
  void unimplemented_map(address_map& map, const char* what);
  // Called from each card's init(); construction only stores map callbacks.
  void init_maps();
  // Call after the card's register maps are installed. PCI decode, access
  // widths and card-specific port effects remain in the frontend.
  void init_io_maps();
  bool vga_io_read(u32 port, u8& data);
  bool vga_io_write(u32 port, u8 data);
  address_map m_main_if_map{16};
  address_map m_3cx_map{16};
  address_map m_crtc_map{256};
  address_map m_seq_map{256};
  address_map m_gc_map{256};
  address_map m_atc_map{64};
  virtual void trace_unimplemented(const std::string&) {}
  std::set<std::tuple<std::string, u32, bool>> m_unimplemented_seen;
  unsigned m_unimplemented_printed = 0;

  // end es40 specific
  enum
  {
    SCREEN_OFF = 0,
    TEXT_MODE,
    VGA_MODE,
    EGA_MODE,
    CGA_MODE,
    MONO_MODE,
    RGB8_MODE,
    RGB15_MODE,
    RGB16_MODE,
    RGB24_MODE,
    RGB32_MODE
  };

  //vga_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

  //TIMER_CALLBACK_MEMBER(vblank_timer_cb);

  // device-level overrides
  //virtual void device_start() override ATTR_COLD;
  virtual void device_reset() ATTR_COLD;
  //virtual ioport_constructor device_input_ports() const override ATTR_COLD;

  // device_palette_interface overrides
  //virtual uint32_t palette_entries() const noexcept override { return 0x100; }

  void vga_vh_text(bitmap_rgb32& bitmap, const rectangle& cliprect);
  void vga_vh_ega(bitmap_rgb32& bitmap, const rectangle& cliprect);
  void vga_vh_vga(bitmap_rgb32& bitmap, const rectangle& cliprect);
  void vga_vh_cga(bitmap_rgb32& bitmap, const rectangle& cliprect);
  void vga_vh_mono(bitmap_rgb32& bitmap, const rectangle& cliprect);
  virtual uint8_t pc_vga_choosevideomode();
  // ES40 keeps timing storage in each card; false means incomplete CRTC geometry.
  bool recompute_params_clock(int divisor, int xtal,
    double& vrefresh_hz, uint64_t& refresh_interval_ms);
  virtual void recompute_params();
  uint8_t vga_vblank();
  //virtual void enter_setup_mode();

  //virtual space_config_vector memory_space_config() const override;

  virtual void io_3bx_3dx_map(address_map& map);

  u8 crtc_address_r(offs_t offset);
  void crtc_address_w(offs_t offset, u8 data);
  virtual u8 crtc_data_r(offs_t offset);
  virtual void crtc_data_w(offs_t offset, u8 data);
  u8 input_status_1_r(offs_t offset);
  void feature_control_w(offs_t offset, u8 data);

  virtual void io_3cx_map(address_map& map);

  u8 atc_address_r(offs_t offset);
  void atc_address_data_w(offs_t offset, u8 data);
  u8 atc_data_r(offs_t offset);
  u8 input_status_0_r(offs_t offset);
  void miscellaneous_output_w(offs_t offset, u8 data);
  u8 sequencer_address_r(offs_t offset);
  void sequencer_address_w(offs_t offset, u8 data);
  virtual u8 sequencer_data_r(offs_t offset);
  virtual void sequencer_data_w(offs_t offset, u8 data);
  u8 ramdac_mask_r(offs_t offset);
  void ramdac_mask_w(offs_t offset, u8 data);
  u8 ramdac_state_r(offs_t offset);
  void ramdac_read_index_w(offs_t offset, u8 data);
  u8 ramdac_write_index_r(offs_t offset);
  void ramdac_write_index_w(offs_t offset, u8 data);
  u8 ramdac_data_r(offs_t offset);
  void ramdac_data_w(offs_t offset, u8 data);
  u8 feature_control_r(offs_t offset);
  u8 miscellaneous_output_r(offs_t offset);
  virtual u8 gc_address_r(offs_t offset);
  virtual void gc_address_w(offs_t offset, u8 data);
  virtual u8 gc_data_r(offs_t offset);
  virtual void gc_data_w(offs_t offset, u8 data);

  // TODO: MAME pc_vga.h has these here
  virtual void crtc_map(address_map& map);
  virtual void gc_map(address_map& map);
  virtual void sequencer_map(address_map& map);
  virtual void attribute_map(address_map& map);

  // NOTE: do not use the subclassed result when determining pitch in SVGA modes.
  // dw & word mode should apply to normal VGA modes only.
  virtual uint16_t offset();
  virtual uint32_t latch_start_addr() { return vga.crtc.start_addr_latch; }
  virtual uint8_t vga_latch_write(int offs, uint8_t data);
  inline uint8_t rotate_right(uint8_t val) { return (val >> vga.gc.rotate_count) | (val << (8 - vga.gc.rotate_count)); }
  inline uint8_t vga_logical_op(uint8_t data, uint8_t plane, uint8_t mask)
  {
    uint8_t res = 0;
    switch (vga.gc.logical_op & 3)
    {
    case 0: /* NONE */
      res = (data & mask) | (vga.gc.latch[plane] & ~mask);
      break;
    case 1: /* AND */
      res = (data | ~mask) & (vga.gc.latch[plane]);
      break;
    case 2: /* OR */
      res = (data & mask) | (vga.gc.latch[plane]);
      break;
    case 3: /* XOR */
      res = (data & mask) ^ (vga.gc.latch[plane]);
      break;
    }
    return res;
  }
  virtual bool get_interlace_mode() { return false; }
  virtual void palette_update();

  struct vga_t
  {
    //vga_t(device_t &owner) { }
    struct
    {
      size_t vram_size;
    } svga_intf;

    // Non-owning pointer to the actual VRAM allocation.
    // Subclass sets this to its own backing store during init().
    // Layout: planar — plane 0 at +0, plane 1 at +0x10000,
    //                  plane 2 at +0x20000, plane 3 at +0x30000.
    uint8_t* memory = nullptr;

    uint32_t pens[16]; /* the current 16 pens */

    uint8_t miscellaneous_output;
    uint8_t feature_control;

    struct
    {
      uint8_t index;
      uint8_t data[0x100];
      uint8_t map_mask;
      struct
      {
        uint8_t A, B;
        u32 base[2];
      }char_sel;
    } sequencer;

    /* An empty comment at the start of the line indicates that register is currently unused */
    struct
    {
      uint8_t index;
      uint8_t data[0x100];
      uint16_t horz_total;
      uint16_t horz_disp_end;
      /**/    uint8_t horz_blank_start;
      /**/    uint8_t horz_blank_end;
      /**/    uint8_t horz_retrace_start;
      /**/    uint8_t horz_retrace_skew;
      /**/    uint8_t horz_retrace_end;
      /**/    uint8_t disp_enable_skew;
      /**/    uint8_t evra;
      uint16_t vert_total;
      uint16_t vert_disp_end;
      /**/    uint16_t vert_retrace_start;
      /**/    uint8_t vert_retrace_end;
      uint16_t vert_blank_start;
      uint16_t line_compare;
      uint32_t cursor_addr;
      /**/    uint8_t byte_panning;
      uint8_t preset_row_scan;
      uint8_t scan_doubling;
      uint8_t maximum_scan_line;
      uint8_t cursor_enable;
      uint8_t cursor_scan_start;
      /**/    uint8_t cursor_skew;
      uint8_t cursor_scan_end;
      uint32_t start_addr;
      uint32_t start_addr_latch;
      uint8_t protect_enable;
      /**/    uint8_t bandwidth;
      uint16_t offset;
      uint8_t word_mode;
      uint8_t dw;
      /**/    uint8_t div4;
      /**/    uint8_t underline_loc;
      uint16_t vert_blank_end;
      uint8_t sync_en;
      /**/    uint8_t aw;
      uint8_t div2;
      /**/    uint8_t sldiv;
      /**/    uint8_t map14;
      /**/    uint8_t map13;
      /**/    uint8_t irq_clear;
      /**/    uint8_t irq_disable;
      uint8_t irq_latch;
      uint8_t no_wrap;
    } crtc;

    struct
    {
      uint8_t index;
      uint8_t latch[4];
      uint8_t set_reset;
      uint8_t enable_set_reset;
      uint8_t color_compare;
      uint8_t logical_op;
      uint8_t rotate_count;
      uint8_t shift256;
      uint8_t shift_reg;
      uint8_t read_map_sel;
      uint8_t read_mode;
      uint8_t write_mode;
      uint8_t color_dont_care;
      uint8_t bit_mask;
      uint8_t alpha_dis;
      uint8_t memory_map_sel;
      uint8_t host_oe;
      uint8_t chain_oe;
    } gc;

    struct
    {
      uint8_t index, data[0x15]; int state;
      uint8_t prot_bit;
      uint8_t pel_shift;
      uint8_t pel_shift_latch;
    } attribute;

    struct {
      uint8_t read_index, write_index, mask;
      int read;
      int state;
      uint8_t color[0x300]; /* flat RGB triplets */
      int dirty;
      uint8_t loading[3];
    } dac;

    struct {
      uint8_t visible;
    } cursor;

    /* oak vga */
    struct { uint8_t reg; } oak;
  } vga;

  //required_ioport m_input_sense;

  //emu_timer* m_vblank_timer;

  enum {
    MAIN_IF_REG = 0,
    CRTC_REG,
    GC_REG,
    SEQ_REG,
    ATC_REG,
    // pointer for subclasses to declare further spaces
    EXT_REG
  };

  virtual address_map& space(int spacenum); // ES40 register-space adapter

  address_space_config m_main_if_space_config;
  address_space_config m_crtc_space_config;
  address_space_config m_gc_space_config;
  address_space_config m_seq_space_config;
  address_space_config m_atc_space_config;

  bool m_ioas = false;

  // ES40 has no VGA retrace IRQ callback yet.
  nop_callback m_vsync_cb;

private:
  address_map* io_port_map(u32 port);
  uint32_t start_addr();
};

extern CVGA* theVGA;
#endif
