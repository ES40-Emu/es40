/* ES40 emulator.
 * Copyright (C) 2007-2025 by the ES40 Emulator Project & Others
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
  * Contains the definitions for emulated S3 Trio 64 Video Card device.
  *
  * $Id$
  *
  * X-1.12       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.11       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init() start_threads() and stop_threads() functions.
  *
  * X-1.10       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.9        Camiel Vanderhoeven                             20-JAN-2008
  *      Added X11 GUI.
  *
  * X-1.8        Camiel Vanderhoeven                             08-JAN-2008
  *      Comments.
  *
  * X-1.7        Camiel Vanderhoeven                             02-JAN-2008
  *      Cleanup.
  *
  * X-1.6        Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.5        Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.4        Brian Wheeler                                   10-DEC-2007
  *      Added SDL.h.
  *
  * X-1.3        Camiel Vanderhoeven                             10-DEC-2007
  *      Use new base class VGA.
  *
  * X-1.2        Camiel Vanderhoeven/Brian Wheeler               6-DEC-2007
  *      Changed implementation (with thanks to the Bochs project!!)
  *
  * X-1.1        Camiel Vanderhoeven                             1-DEC-2007
  *      Initial version in CVS.
  **/
#if !defined(INCLUDED_S3Trio64_H_)
#define INCLUDED_S3Trio64_H_

#include "VGA.h"
#include "gui/gui.h"
#include <atomic>
#include "coretmpl.h"
#include "attotime.h"
#include "mame_shims.h"
#include "address_map.h"
#include "ibm8514a.h"

  /* video card has 4M of ram */
#define VIDEO_RAM_SIZE  22
#define CRTC_MAX        0x70


/**
 * \brief S3 Trio 64 Video Card
 *
 * Documentation consulted:
 *  - VGADOC4b
 *   (http://home.worldonline.dk/~finth/)
 *  .
 **/
class CS3Trio64 : public CVGA, public CRunnable, public mame_machine_provider,
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
  u64 capture_pci_io_write(const PciIoWrite& write) const noexcept override;
  void observe_pci_io_write(const PciIoWrite& write, u64 captured,
    PciIoWriteDispatch dispatch) override;
  std::string describe_access_context(int index, u64 address) const override;

  virtual void  WriteMem_Bar(int func, int bar, u32 address, int dsize,
    u32 data) override;
  virtual u32   ReadMem_Bar(int func, int bar, u32 address, int dsize) override;

  virtual u64 ReadMem(int index, u64 address, int dsize) override;
  virtual void WriteMem(int index, u64 address, int dsize, u64 data) override;

  // Observe PCI config-space accesses (BARs and COMMAND)
  u32  config_read_custom(int func, u32 address, int dsize, u32 cur) override;
  void config_write_custom(int func, u32 address, int dsize, u32 old_data, u32 new_data, u32 raw) override;

  // MAME header stuff

  // construction/destruction
  //s3vision864_vga_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock);

  //virtual uint8_t mem_r(offs_t offset) override;
  //virtual void mem_w(offs_t offset, uint8_t data) override;

  uint32_t screen_update(bitmap_rgb32& bitmap, const rectangle& cliprect) override;

  ibm8514a_device* get_8514() { return &m_8514; }

  // end MAME header stuff

  CS3Trio64(CConfigurator* cfg, class CSystem* c, int pcibus, int pcidev,
    bx_gui_c& display);
  virtual       ~CS3Trio64();

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
  virtual u16      line_compare_mask() override;

  // MAME S3 state
  struct {
    uint8_t memory_config;
    uint8_t ext_misc_ctrl_2;
    uint8_t crt_reg_lock;
    uint8_t reg_lock1;
    uint8_t reg_lock2;
    uint8_t enable_8514;
    uint8_t enable_s3d;
    uint8_t cr3a;
    uint8_t cr42;
    uint8_t cr43;
    uint8_t cr51;
    uint8_t cr53;
    uint8_t id_high;
    uint8_t id_low;
    uint8_t revision;
    uint8_t id_cr30;
    uint32_t strapping;
    uint8_t sr10;
    uint8_t sr11;
    uint8_t sr12;
    uint8_t sr13;
    uint8_t sr15;
    uint8_t sr17;
    uint8_t clk_pll_r;
    uint8_t clk_pll_m;
    uint8_t clk_pll_n;

    // data for memory-mapped I/O 
    uint16_t mmio_9ae8;
    uint16_t mmio_bee8;
    uint16_t mmio_96e8;

    // hardware graphics cursor 
    uint8_t cursor_mode;
    uint16_t cursor_x;
    uint16_t cursor_y;
    uint16_t cursor_start_addr;
    uint8_t cursor_pattern_x;
    uint8_t cursor_pattern_y;
    uint8_t cursor_fg[4];
    uint8_t cursor_bg[4];
    uint8_t cursor_fg_ptr;
    uint8_t cursor_bg_ptr;
    uint8_t extended_dac_ctrl;

    // Trio64-specific CRTC registers (ES40 extensions( beyond MAME) )
    uint8_t cr32;     // Backward Compatibility 1 (BKWD_1)
    uint8_t cr33;     // Backward Compatibility 2 (BKWD_2)
    uint8_t cr34;     // Backward Compatibility 3 (BKWD_3)
    uint8_t cr3b;     // Data Transfer Position (DT_EX-POS)
    uint8_t cr3c;     // Interlace Retrace Start
    uint8_t cr40;     // System Configuration (enable 8514)
    uint8_t cr41;     // BIOS Flag Register
    uint8_t cr50;     // Extended System Control 1
    uint8_t cr52;     // Extended BIOS Flag 1
    uint8_t cr54;     // Extended Memory Control 2
    uint8_t cr56;     // External Sync Control 1
    uint8_t cr57;     // External Sync Control 2
    uint8_t cr58;     // Linear Address Window Control
    uint8_t cr59;     // Linear Address Window Position High
    uint8_t cr5a;     // Linear Address Window Position Low
    uint8_t cr5b;     // undocumented
    uint8_t cr5d;     // Extended Horizontal Overflow
    uint8_t cr5f;     // undocumented
    uint8_t cr60;     // Extended Memory Control 3
    uint8_t cr61;     // Extended Memory Control 4
    uint8_t cr62;     // undocumented
    uint8_t cr63;     // External Sync Control 3
    uint8_t cr64;     // undocumented
    uint8_t cr65;     // Extended Miscellaneous Control
    uint8_t cr66;     // Extended Miscellaneous Control 1
    uint8_t cr6b;     // Extended BIOS Flag 3
    uint8_t cr6c;     // Extended BIOS Flag 4
    uint8_t cr6d;     // undocumented

    // Trio64-specific MMIO staging (ES40 extensions)
    uint16_t mmio_42e8;
    uint16_t mmio_4ae8;
    uint16_t mmio_92e8;
    uint16_t mmio_9ee8;

    // Trio64-specific sequencer extensions
    uint8_t sr18;
    uint8_t sr1a;
    uint8_t sr1b;
  } s3;
  virtual uint16_t offset() override;

  virtual uint32_t latch_start_addr() override; // below is MAME's base VGA implementation, but S3 Trio in MAME overrides it with the version we have
  virtual bool get_interlace_mode() override { return BIT(s3.cr42, 5); }

  virtual void palette_update() override;
  virtual void s3_define_video_mode(void);

  nop_callback m_vsync_cb;

  address_map m_crtc_map{ 256 };
  address_map m_seq_map{ 256 };
  address_map m_gc_map{ 256 };
  address_map m_atc_map{ 64 };

  address_map& space(int spacenum) override
  {
    switch (spacenum) {
    case CRTC_REG: return m_crtc_map;
    case GC_REG:   return m_gc_map;
    case SEQ_REG:  return m_seq_map;
    case ATC_REG:  return m_atc_map;
    default:
      FAILURE_1(NotImplemented, "Unknown register space %d", spacenum);
      //return m_crtc_map; // unreachable
    }
  }

  void crtc_map(address_map& map);
  void sequencer_map(address_map& map);
  void gc_map(address_map& map);
  void attribute_map(address_map& map);

  void recompute_params() override;

  void init_maps() {
    crtc_map(m_crtc_map);
    sequencer_map(m_seq_map);
    gc_map(m_gc_map);
    attribute_map(m_atc_map);
  }

  // Video mode detection (MAME: svga_device::pc_vga_choosevideomode)
  uint8_t get_video_depth();

  // SVGA-aware banked memory access (MAME: s3vision864_vga_device::mem_r/w)
  uint8_t  mem_r(uint32_t offset) override;
  void     mem_w(uint32_t offset, uint8_t data) override;

  // Linear framebuffer access (MAME: vga_device::mem_linear_r/w)
  void     mem_linear_w(uint32_t offset, uint8_t data) override;

  // Hardware cursor overlay (MAME: screen_update cursor portion)
  void s3_draw_hardware_cursor(uint32_t* pixels, int pitch_px,
    int clip_width, int clip_height,
    uint8_t cur_mode);

  inline void vram_write_dirty(uint32_t addr, uint8_t v) {
    vga.memory[addr % vga.svga_intf.vram_size] = v;
    state.vga_mem_updated = 1;
  }

  inline bool dtp_enabled() const { return (s3.cr34 & 0x10) != 0; }

  inline bool ilrt_enabled() const { return (s3.cr42 & 0x20) != 0; }

  inline uint32_t vram_display_mask() const {
    return (uint32_t)(vga.svga_intf.vram_size - 1);
  }


private:
  // Trio64 has one modeled output.
  CDisplayOutput m_output;

  // MAME CODE HERE
  ibm8514a_device m_8514;
  void refresh_pitch_offset();
  // END MAME CODE - rest is es40 specific or pending removal

  // DB014-B 14-47/48: one setup register, selected by CR65.2, and
  // its option byte. Both power on with CPU display access disabled.
  u8 m_video_subsys_enable = 0x00;
  u8 m_setup_option_select_0102 = 0x00;
  bool normal_access_enabled() const noexcept;
  bool io_access_enabled(u32 port, bool write) const noexcept;

  u32   mem_read(u32 address, int dsize);
  void  mem_write(u32 address, int dsize, u32 data);

  // Keep SDL window alive across firmware reset:
  //  - PauseThread is set by stop_threads() when system reset is in progress
  //  - PauseAck is raised by the S3 thread once it is safely paused
  std::atomic<bool> PauseThread{ false };
  std::atomic<bool> PauseAck{ false };

  // screen refresh stuff
  std::chrono::steady_clock::time_point m_last_refresh_time;
  // Host dimension cache for this card's single scanout; not snapshot data.
  unsigned old_iHeight = 0;
  unsigned old_iWidth = 0;
  // Dirty-gate state: skip the per-refresh rasterize + GPU upload when nothing visible changed.
  uint64_t m_last_cursor_sig = 0;     // HW-cursor (mode/pos/data-addr) folded in -- not tracked by vga_mem_updated
  int      m_frames_since_render = 0;  // forced-refresh counter so cursor/text blink still animate when static

  // accel I/O (S3 Trio uses 0x42E8/0x4AE8)
  void          AccelIOWrite(u32 port, u8 data);
  u8            AccelIORead(u32 port);
  u32           AccelIORead(u32 port, int dsize);
  bool    IsAccelPort(u32 port) const;
  int     BytesPerPixel() const;
  u32     PitchBytes() const;   // from CRTC 13h + hi bits

  // Register helpers
  void recompute_scanline_layout();
  inline uint8_t current_char_width_px() const;
  void recompute_params_clock(int divisor, int xtal);

  void  update_linear_mapping();
  void  update_pci_bar0_from_crtc(u32 value, u32 register_bits);
  void  on_crtc_linear_regs_changed(const char* reason = "CR58/59/5A");


  u32   io_read(u32 address, int dsize);
  void  io_write(u32 address, int dsize, u32 data);

  void  io_write_b(u32 address, u8 data);
  void  ramdac_write_b(u32 address, u8 data);

  void  write_b_3c2(u8 data);

  u8    read_b_3c2();
  u8    read_b_3ca();

  u32   legacy_read(u32 address, int dsize);
  void  legacy_write(u32 address, int dsize, u32 data);

  u32   rom_read(u32 address, int dsize);

  // Each card loads its own ROM image from configuration.
  unsigned int rom_max = 0;
  u8 option_rom[65536] = {};

  void  determine_screen_dimensions(unsigned* piHeight, unsigned* piWidth);

  char  bios_message[200];
  int   bios_message_size;

  inline uint32_t s3_vram_mask() const;

  void lfb_recalc_and_cache();  // recompute enable/base/size from COMMAND+BAR0 (and CR regs if you wish)
  bool uses_sized_linear_bar_window() const noexcept;
  u8 cpu_bank() const noexcept;
  bool linear_bar_offset(u64 address, u32& offset) const noexcept;
  void trace_lfb_if_changed(const char* reason);

  inline bool seq_chain_four()   const { return (vga.sequencer.data[4] & 0x08) != 0; }
  inline bool seq_odd_even()     const { return (vga.sequencer.data[4] & 0x04) != 0; }
  inline bool seq_extended_mem() const { return (vga.sequencer.data[4] & 0x02) != 0; }
  inline bool seq_reset1()       const { return (vga.sequencer.data[0] & 0x01) != 0; }
  inline bool seq_reset2()       const { return (vga.sequencer.data[0] & 0x02) != 0; }
  inline bool seq_dotperchar()   const { return (vga.sequencer.data[1] & 0x01) != 0; }
  inline bool x_dotclockdiv2()   const { return (vga.sequencer.data[1] & 0x08) != 0; }

  // cached state for LFB
  u32  lfb_base_ = 0;
  u32  lfb_size_ = 0;
  bool lfb_enabled_ = false;


  // LFB bookkeeping
  enum { DEV_LFB_IDX = 6 };      // free in this device (legacy used 4/5/7 etc.)
  u32   lfb_base = 0;            // guest-visible base (32-bit)
  u32   lfb_size = 0;            // 64K/1M/2M/4M
  u64   lfb_phys = 0;            // full physical mapping base we registered
  bool  lfb_active = false;      // CR58.ENB LA | ADVFUNC.LA; PCI MSE is gated separately

  bool  pci_mem_enable = false;  // PCI Command.MSE cached
  u32   pci_bar0 = 0;            // cached BAR0 (optional; we treat CR58..5A as truth)
  bool m_replaying_pci_state = false; // host-only guard; saved S3 aliases restore separately

  void  lfb_recalc_and_map();    // (un)map according to CR58..5A & PCI
  inline u32 lfb_offset_from(u64 phys_addr) const {
    const u64 off = phys_addr - lfb_phys;
    return (u32)(off % vga.svga_intf.vram_size); // VRAM wraps modulo real size
  }

  bool lfb_trace_needs_first_access_note = false;
  bool lfb_trace_initialized = false;
  bool lfb_trace_enabled_prev = false;
  uint32_t lfb_trace_base_prev = 0;
  uint32_t lfb_trace_size_prev = 0;

  CThread* myThread;
  bool  StopThread;
  bool m_gui_initialized = false;

  /// The state structure contains all elements that need to be saved to the statefile.
  struct SS3_state
  {
    // SDL/GUI dirty tracking 
    bool      vga_mem_updated;
    unsigned  x_tilesize;
    unsigned  y_tilesize;
    u8        last_bpp;

    u8* memory; // the actual vram... probably should have notated this earlier
    u32       memsize;
  } state;

  // TODO: migrate all  usage and then remove state.sequencer entirely.

  inline void set_seq_pll_lock(u8 v) { vga.sequencer.data[0x08] = v; }

  // SR09 Extended -> vga.sequencer.data[0x09]
  inline u8  seq_sr9() const { return vga.sequencer.data[0x09]; }

  // SR0A External Bus Control -> vga.sequencer.data[0x0A]
  inline u8  seq_srA() const { return vga.sequencer.data[0x0A]; }

  // SR0B Misc Extended -< vga.sequencer.data[0x0B]
  inline u8  seq_srB() const { return vga.sequencer.data[0x0B]; }

  // SR0D Extended ->vga.sequencer.data[0x0D]
  inline u8  seq_srD() const { return vga.sequencer.data[0x0D]; }

  // SR10/SR11 MCLK PLL -> s3.sr10, s3.sr11
  inline u8  seq_mclkn() const { return s3.sr10 & 0x1f; }
  inline u8  seq_mclkr() const { return s3.sr10 >> 5; }
  inline u8  seq_mclkm() const { return s3.sr11; }

  // ATC index 0x00..0x0F: Palette registers
  inline u8  atc_palette(u8 idx) const { return vga.attribute.data[idx & 0x0f]; }

  // ATC index 0x10: Mode Control (decomposed bit accessors)
  inline bool atc_graphics_alpha()       const { return BIT(vga.attribute.data[0x10], 0); }
  inline bool atc_display_type()         const { return BIT(vga.attribute.data[0x10], 1); }
  inline bool atc_enable_line_graphics() const { return BIT(vga.attribute.data[0x10], 2); }
  inline bool atc_blink_intensity()      const { return BIT(vga.attribute.data[0x10], 3); }
  inline bool atc_pixel_panning_compat() const { return BIT(vga.attribute.data[0x10], 5); }
  inline bool atc_pixel_clock_select()   const { return BIT(vga.attribute.data[0x10], 6); }
  inline bool atc_internal_palette_size() const { return BIT(vga.attribute.data[0x10], 7); }

  // ATC index 0x11: Overscan Color
  inline u8  atc_overscan_color()   const { return vga.attribute.data[0x11]; }

  // ATC index 0x12: Color Plane Enable
  inline u8  atc_color_plane_enable() const { return vga.attribute.data[0x12] & 0x0f; }

  // ATC index 0x13: Horizontal PEL Panning
  inline u8  atc_horiz_pel_panning() const { return vga.attribute.data[0x13] & 0x0f; }

  // ATC index 0x14: Color Select
  inline u8  atc_color_select()     const { return vga.attribute.data[0x14] & 0x0f; }

  // Video output enabled (ATC index byte bit 5 = palette address source)
  // MAME: this is the "prot_bit" / palette RAM address source.
  // When 0, video output is disabled (CPU can access palette RAM).
  // When 1, video output is enabled (ATC drives display).
  inline bool atc_video_enabled()   const { return BIT(vga.attribute.index, 5); }

  // Flip-flop state (0=index phase, nonzero=data phase)
  inline bool atc_flip_flop()       const { return vga.attribute.state != 0; }


  inline uint32_t s3_lfb_base_from_regs();

  // computed video timing, MAME screen().configure() parameters
  struct {
    int      pixel_clock_hz = 0;    // computed pixel clock in Hz
    int      xtal_hz = 0;    // base or PLL-derived crystal frequency
    int      divisor = 1;    // VCLK divisor from color mode
    double   dclk_freq_mhz = 0.0;  // PLL output frequency in MHz (for debug)
    double   vrefresh_hz;          // vertical refresh rate derived from CRTC
    uint64_t refresh_interval_ms;  // milliseconds between redraws
  } timing;

  inline uint32_t s3_mmio_base_off(SS3_state& s);
  void accel_reset();
  inline bool s3_new_mmio_enabled();
};

// ----- Debug tracing for Data Transfer Position (CR3B/CR34 bit4) -----
#ifndef S3_TRACE_DTP
#define S3_TRACE_DTP 1
#endif
#if S3_TRACE_DTP
#define DTP_TRACE(...) do { printf(__VA_ARGS__); } while (0)
#else
#define DTP_TRACE(...) do {} while (0)
#endif

#ifndef S3_ACCEL_TRACE
#define S3_ACCEL_TRACE 1
#endif

#endif // !defined(INCLUDED_S3Trio64_H_)
