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

#if !defined(INCLUDED_RV200_H_)
#define INCLUDED_RV200_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

class CRV200
{
public:
  struct Options
  {
    uint32_t vram_bytes = 64u * 1024 * 1024;
    // Unknown register writes fault instead of being shadowed.
    bool strict_mmio = false;
  };

  struct Frame
  {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> argb;
  };

  struct Counters
  {
    uint64_t reads = 0, writes = 0, draws = 0, pixels = 0, shadow_writes = 0;
  };

  // Numeric hardware interface: Linux include/video/radeon.h and
  // xf86-video-ati radeon_reg.h, filtered to the R100/RV200 family.
  static constexpr uint32_t
    MM_INDEX = 0x0000,
    MM_DATA = 0x0004, CLOCK_CNTL_INDEX = 0x0008, CLOCK_CNTL_DATA = 0x000c,
    BIOS_SCRATCH0 = 0x0010, BIOS_SCRATCH7 = 0x002c,
    BUS_CNTL = 0x0030, BUS_CNTL1 = 0x0034,
    GEN_INT_CNTL = 0x0040, GEN_INT_STATUS = 0x0044,
    CRTC_GEN_CNTL = 0x0050, CRTC_EXT_CNTL = 0x0054, DAC_CNTL = 0x0058,
    CRTC_STATUS = 0x005c, GPIO_VGA_DDC = 0x0060, GPIO_DVI_DDC = 0x0064,
    GPIO_MONID = 0x0068, GPIO_CRT2_DDC = 0x006c, DAC_CNTL2 = 0x007c,
    PALETTE_INDEX = 0x00b0,
    PALETTE_DATA = 0x00b4, CONFIG_CNTL = 0x00e0, RBBM_CNTL = 0x00ec,
    RBBM_SOFT_RESET = 0x00f0, CONFIG_MEMSIZE = 0x00f8,
    CONFIG_APER_0_BASE = 0x0100, CONFIG_APER_1_BASE = 0x0104,
    CONFIG_APER_SIZE = 0x0108, CONFIG_REG_1_BASE = 0x010c,
    CONFIG_REG_APER_SIZE = 0x0110, TEST_DEBUG_MUX = 0x0124,
    HOST_PATH_CNTL = 0x0130, MEM_CNTL = 0x0140, MEM_TIMING_CNTL = 0x0144,
    MC_FB_LOCATION = 0x0148, MC_AGP_LOCATION = 0x014c, MC_STATUS = 0x0150,
    MEM_INIT_LAT_TIMER = 0x0154, MEM_SDRAM_MODE_REG = 0x0158,
    PAD_CTLR_STRENGTH = 0x0168, AGP_BASE = 0x0170, AGP_CNTL = 0x0174,
    MEM_IO_CNTL_A0 = 0x0178, MEM_IO_CNTL_A1 = 0x017c,
    MC_DEBUG = 0x0188, MEM_IO_OE_CNTL = 0x018c, SEPROM_CNTL1 = 0x01c0,
    AIC_CNTL = 0x01d0, MC_IND_INDEX = 0x01f8, MC_IND_DATA = 0x01fc,
    CRTC_H_TOTAL_DISP = 0x0200,
    CRTC_H_SYNC_STRT_WID = 0x0204, CRTC_V_TOTAL_DISP = 0x0208,
    CRTC_V_SYNC_STRT_WID = 0x020c, CRTC_VLINE_CRNT_VLINE = 0x0210,
    CRTC_CRNT_FRAME = 0x0214, CRTC_OFFSET = 0x0224, CRTC_OFFSET_CNTL = 0x0228,
    CRTC_PITCH = 0x022c, OVR_CLR = 0x0230, OVR_WID_LEFT_RIGHT = 0x0234,
    OVR_WID_TOP_BOTTOM = 0x0238, DISPLAY_BASE_ADDR = 0x023c,
    CUR_OFFSET = 0x0260,
    CUR_HORZ_VERT_POSN = 0x0264, CUR_HORZ_VERT_OFF = 0x0268, CUR_CLR0 = 0x026c,
    CUR_CLR1 = 0x0270, CRTC_MORE_CNTL = 0x027c, DAC_EXT_CNTL = 0x0280,
    FP_GEN_CNTL = 0x0284, FP2_GEN_CNTL = 0x0288,
    FP_HORZ_STRETCH = 0x028c, FP_VERT_STRETCH = 0x0290,
    TMDS_PLL_CNTL = 0x02a8, LVDS_GEN_CNTL = 0x02d0,
    CRTC2_H_TOTAL_DISP = 0x0300, CRTC2_H_SYNC_STRT_WID = 0x0304,
    CRTC2_V_TOTAL_DISP = 0x0308, CRTC2_V_SYNC_STRT_WID = 0x030c,
    CRTC2_VLINE_CRNT_VLINE = 0x0310, CRTC2_CRNT_FRAME = 0x0314,
    CRTC2_OFFSET = 0x0324, CRTC2_OFFSET_CNTL = 0x0328, CRTC2_PITCH = 0x032c,
    CRTC2_DISPLAY_BASE_ADDR = 0x033c, CUR2_OFFSET = 0x0360,
    CUR2_HORZ_VERT_POSN = 0x0364, CUR2_HORZ_VERT_OFF = 0x0368,
    CUR2_CLR0 = 0x036c, CUR2_CLR1 = 0x0370,
    CRTC2_GEN_CNTL = 0x03f8, CRTC2_STATUS = 0x03fc, CP_RB_BASE = 0x0700,
    CP_RB_CNTL = 0x0704, CP_RB_RPTR_ADDR = 0x070c,
    CP_RB_RPTR = 0x0710, CP_RB_WPTR = 0x0714,
    CP_RB_WPTR_DELAY = 0x0718, CP_RB_RPTR_WR = 0x071c,
    CP_IB_BASE = 0x0738, CP_IB_BUFSZ = 0x073c, CP_CSQ_CNTL = 0x0740,
    SCRATCH_UMSK = 0x0770, SCRATCH_ADDR = 0x0774,
    CP_ME_CNTL = 0x07d0, CP_ME_RAM_ADDR = 0x07d4,
    CP_ME_RAM_RADDR = 0x07d8, CP_ME_RAM_DATAH = 0x07dc,
    CP_ME_RAM_DATAL = 0x07e0,
    OV0_SCALE_CNTL = 0x0420, OV0_FLAG_CNTL = 0x04dc,
    TV_MASTER_CNTL = 0x0800, TV_PRE_DAC_MUX_CNTL = 0x0888,
    TV_DAC_CNTL = 0x088c, FCP_CNTL = 0x0910,
    CAP0_TRIG_CNTL = 0x0950, SURFACE_CNTL = 0x0b00,
    VIPH_CONTROL = 0x0c40, DISP_MISC_CNTL = 0x0d00,
    DAC_MACRO_CNTL = 0x0d04, DISP_PWR_MAN = 0x0d08,
    DISP_TEST_DEBUG_CNTL = 0x0d10,
    DISP_OUTPUT_CNTL = 0x0d64, RBBM_STATUS = 0x0e40,
    DST_OFFSET = 0x1404,
    DST_PITCH = 0x1408, DST_X = 0x141c, DST_Y = 0x1420, SRC_X = 0x1414,
    SRC_Y = 0x1418, SRC_PITCH_OFFSET = 0x1428, DST_PITCH_OFFSET = 0x142c,
    SRC_Y_X = 0x1434, DST_Y_X = 0x1438, DST_HEIGHT_WIDTH = 0x143c,
    DP_GUI_MASTER_CNTL = 0x146c, BRUSH_Y_X = 0x1474, BRUSH_DATA0 = 0x1480,
    BRUSH_DATA1 = 0x1484, DP_BRUSH_BKGD_CLR = 0x1478,
    DP_BRUSH_FRGD_CLR = 0x147c, SRC_X_Y = 0x1590, DST_X_Y = 0x1594,
    DST_WIDTH_HEIGHT = 0x1598, SRC_OFFSET = 0x15ac, SRC_PITCH = 0x15b0,
    CLR_CMP_CNTL = 0x15c0, CLR_CMP_CLR_SRC = 0x15c4, CLR_CMP_CLR_DST = 0x15c8,
    CLR_CMP_MASK = 0x15cc, DP_SRC_ENDIAN = 0x15d4, DP_SRC_FRGD_CLR = 0x15d8,
    DP_SRC_BKGD_CLR = 0x15dc, SCRATCH_REG0 = 0x15e0,
    SCRATCH_REG5 = 0x15f4, DST_LINE_START = 0x1600, DST_LINE_END = 0x1604,
    DST_LINE_PATCOUNT = 0x1608, SC_LEFT = 0x1640, SC_RIGHT = 0x1644,
    SC_TOP = 0x1648, SC_BOTTOM = 0x164c, DP_CNTL = 0x16c0, DP_DATATYPE = 0x16c4,
    DP_MIX = 0x16c8, DP_WRITE_MSK = 0x16cc, DP_CNTL_XDIR_YDIR_YMAJOR = 0x16d0,
    DEFAULT_PITCH_OFFSET = 0x16e0, DEFAULT_SC_BOTTOM_RIGHT = 0x16e8,
    SC_TOP_LEFT = 0x16ec, SC_BOTTOM_RIGHT = 0x16f0,
    SRC_SC_BOTTOM_RIGHT = 0x16f4, DSTCACHE_MODE = 0x1710,
    DSTCACHE_CTLSTAT = 0x1714, WAIT_UNTIL = 0x1720, ISYNC_CNTL = 0x1724,
    HOST_DATA0 = 0x17c0, HOST_DATA7 = 0x17dc, HOST_DATA_LAST = 0x17e0,
    RB3D_CNTL = 0x1c3c, RB3D_ZCACHE_CTLSTAT = 0x3254,
    RB2D_DSTCACHE_MODE = 0x3428, RB2D_DSTCACHE_CTLSTAT = 0x342c;

  static constexpr uint32_t GUI_ACTIVE = 0x80000000u, CRTC_EN = 1u << 25,
                            CRTC_CUR_EN = 1u << 16, CRTC_EXT_DISP_EN = 1u << 24,
                            DISPLAY_DIS = 1u << 10, DAC_8BIT_EN = 1u << 8,
                            BUS_BIOS_DIS_ROM = 1u << 12, CRTC_VBLANK_CUR = 1u,
                            CRTC_VBLANK_SAVE = 2u, HDP_APER_CNTL = 1u << 23,
                            GMC_SRC_PITCH = 1u, GMC_DST_PITCH = 2u,
                            GMC_SRC_CLIP = 4u, GMC_DST_CLIP = 8u,
                            GMC_BRUSH_SOLID = 13u << 4,
                            GMC_BRUSH_NONE = 15u << 4,
                            GMC_COLOR_SOURCE = 3u << 12,
                            GMC_SOURCE_MEMORY = 2u << 24,
                            GMC_SOURCE_HOST = 3u << 24,
                            GMC_CLR_CMP_DISABLE = 1u << 28,
                            GMC_AUX_CLIP_DISABLE = 1u << 29,
                            GMC_WR_MSK_DISABLE = 1u << 30,
                            ROP_PATCOPY = 0xf0u << 16,
                            ROP_SRCCOPY = 0xccu << 16, WAIT_2D = 1u << 16,
                            WAIT_DMA = 1u << 9,
                            DAC2_DAC_CLK_SEL = 1u,
                            DAC2_PALETTE_ACC_CTL = 1u << 5,
                            DAC2_CMP_OUTPUT_MASK = 15u << 8,
                            FP_DETECT_SENSE = 1u << 8,
                            CRTC2_DISPLAY_DIS = 1u << 23,
                            CRTC2_VBLANK_INT = 1u << 9;

  // Indices behind CLOCK_CNTL_INDEX/CLOCK_CNTL_DATA.
  static constexpr uint32_t PPLL_REF_DIV = 0x03, PLL_TEST_CNTL = 0x13,
                            P2PLL_REF_DIV = 0x2b;

  // The framebuffer BAR holds two host apertures of this size.
  static constexpr uint32_t RegisterSpaceSize = 0x4000,
                            ApertureSize = 64u * 1024 * 1024;

  using IRQCallback = std::function<void(bool)>;
  using DiagnosticCallback =
    std::function<void(const std::string& message, bool fatal)>;
  using UnimplementedCallback = std::function<void(
    const char* what, uint32_t address, uint32_t value, bool write)>;
  using VGAReader = std::function<uint8_t(uint32_t)>;
  using VGAWriter = std::function<void(uint32_t, uint8_t)>;
  using PaletteIndexCallback = std::function<void()>;
  using ConfigReader = std::function<uint32_t(uint32_t)>;
  using DMAReader = std::function<bool(uint32_t, uint8_t*, size_t)>;
  using DMAWriter = std::function<bool(uint32_t, const uint8_t*, size_t)>;

  CRV200();
  explicit CRV200(const Options& options);
  CRV200(const CRV200&) = delete;
  CRV200& operator=(const CRV200&) = delete;
  void reset(bool clear_vram = false);
  uint32_t ReadMem(uint32_t address, int dsize = 32);
  void WriteMem(uint32_t address, int dsize, uint32_t data);
  uint64_t mem_read(uint32_t address, int dsize = 32);
  void mem_write(uint32_t address, int dsize, uint64_t data);
  // Deterministic *scanline* advancement; no host-wall-clock dependency in core.
  void advance_scanlines(uint32_t lines = 1);
  bool irq_asserted() const;
  bool busy() const;

  bool halted() const { return !m_fault.empty(); }

  const std::string& last_fault() const { return m_fault; }

  const Options& options() const { return m_options; }

  const Counters& counters() const { return m_counters; }

  const std::vector<uint8_t>& vram() const { return m_vram; }

  // Borrowed by vga_device; RestoreState replaces the allocation.
  uint8_t* vram_data() { return m_vram.data(); }

  uint32_t peek(uint32_t address) const
  {
    return address < RegisterSpaceSize && !(address & 3u) ? m_regs[address / 4]
                                                          : 0;
  }

  uint32_t palette_color(unsigned index, unsigned controller = 0) const
  {
    return (controller ? m_palette2 : m_palette)[index & 255];
  }

  void set_palette_color(unsigned index, uint32_t value, unsigned controller = 0)
  {
    (controller ? m_palette2 : m_palette)[index & 255] = value & 0x00ffffff;
  }

  unsigned palette_bank() const
  {
    return (r(DAC_CNTL2) & DAC2_PALETTE_ACC_CTL) ? 1 : 0;
  }

  uint32_t scanline(unsigned controller = 0) const
  {
    return controller ? m_scanline2 : m_scanline;
  }

  // RV200's primary analog connector can be driven by either CRTC. This
  // selection does not attach a monitor to the secondary connector.
  unsigned display_controller() const
  {
    return (r(DAC_CNTL2) & DAC2_DAC_CLK_SEL) ? 1 : 0;
  }

  bool native_display() const
  {
    return display_controller() || (r(CRTC_GEN_CNTL) & CRTC_EXT_DISP_EN);
  }

  // Output power/routing applies to the primary connector in VGA and native
  // modes. It does not stop the internal CRTC counters or the drawing engine.
  bool display_output_enabled() const;

  uint32_t vertical_total(unsigned controller = 0) const
  {
    return (r(controller ? CRTC2_V_TOTAL_DISP : CRTC_V_TOTAL_DISP) & 0x7ff) + 1;
  }

  uint32_t vertical_display(unsigned controller = 0) const
  {
    return ((r(controller ? CRTC2_V_TOTAL_DISP : CRTC_V_TOTAL_DISP) >> 16) &
            0x7ff) + 1;
  }

  // Board-owned byte ports. MMIO aliases and MM_INDEX/MM_DATA use the same
  // VGA frontend; without it those registers fault.
  void set_vga_callbacks(VGAReader reader, VGAWriter writer)
  {
    m_vga_read = std::move(reader);
    m_vga_write = std::move(writer);
  }

  void set_palette_index_callback(PaletteIndexCallback fn)
  {
    m_palette_index = std::move(fn);
  }

  void set_config_reader(ConfigReader fn) { m_config_read = std::move(fn); }
  void set_dma_reader(DMAReader fn) { m_dma_read = std::move(fn); }
  void set_dma_writer(DMAWriter fn) { m_dma_write = std::move(fn); }

  void set_irq_callback(IRQCallback fn) { m_irq_callback = std::move(fn); }

  void set_diagnostic_callback(DiagnosticCallback fn)
  {
    m_diagnostic = std::move(fn);
  }

  void set_unimplemented_callback(UnimplementedCallback fn)
  {
    m_unimplemented = std::move(fn);
  }

  // PCI BAR placement, readable through the CONFIG_APER registers.
  void set_aperture_bases(uint32_t fb, uint32_t mmio);
  Frame scanout(std::string* error = nullptr) const;
  static void write_ppm(std::ostream&, const Frame&);
  // Versioned little-endian snapshot. Loading validates into a temporary object.
  void SaveState(std::ostream&) const;
  void RestoreState(std::istream&);
  static uint32_t rop3(uint8_t op, uint32_t p, uint32_t s, uint32_t d);

private:
  // Host-data uploads latch their command state, including across snapshots.
  struct Pending
  {
    bool active = false, packed = false;
    uint32_t width = 0, height = 0, word = 0, words_per_row = 0;
    int32_t x = 0, y = 0, dx = 1, dy = 1;
    uint32_t base = 0, pitch = 0, bpp = 0, mask = 0;
    uint32_t pattern = 0, rop = 0, sc_tl = 0, sc_br = 0;
    uint32_t source_format = 3, mono_lsb = 0, fg = 0, bg = 0;
    uint32_t compare = 0, compare_color = 0, compare_mask = 0;
  } m_pending;

  Options m_options;
  std::vector<uint8_t> m_vram;
  std::array<uint32_t, RegisterSpaceSize / 4> m_regs{};
  std::array<uint32_t, 64> m_pll{};
  std::array<uint32_t, 32> m_mc{};
  std::array<uint32_t, 256> m_palette{};
  std::array<uint32_t, 256> m_palette2{};
  std::array<bool, RegisterSpaceSize / 4> m_warned{};
  bool m_aperture_warned = false;
  // One rectangle following a full datatype write from 32-bit format to zero.
  bool m_inferred_clear_armed = false;
  // Packet execution is synchronous, but a stream can stop between DWORDs with incomplete/not finished formation.
  struct CPPacket
  {
    uint32_t header = 0, remaining = 0, reg = 0;
    std::vector<uint32_t> body; // Bounded by the 14-bit packet count.
  };
  std::array<CPPacket, 2> m_cp_packet{};
  std::array<std::array<uint32_t, 2>, 256> m_cp_microcode{};
  bool m_cp_ram_read = false, m_cp_running = false;
  uint32_t m_cp_ib_cursor = 0, m_cp_ib_left = 0, m_cp_since_update = 0;
  std::string m_fault;
  Counters m_counters{};
  uint32_t m_scanline = 0, m_frame = 0, m_scanline2 = 0, m_frame2 = 0;
  VGAReader m_vga_read;
  VGAWriter m_vga_write;
  PaletteIndexCallback m_palette_index;
  ConfigReader m_config_read;
  DMAReader m_dma_read;
  DMAWriter m_dma_write;
  IRQCallback m_irq_callback;
  DiagnosticCallback m_diagnostic;
  UnimplementedCallback m_unimplemented;

  uint32_t r(uint32_t a) const { return m_regs[a / 4]; }

  uint32_t& r(uint32_t a) { return m_regs[a / 4]; }

  void fail(const std::string& why);
  void note(const std::string& why);
  uint32_t read32(uint32_t a);
  uint32_t dac_comparators(bool secondary) const;
  void write32(uint32_t a, uint32_t value, uint32_t lanes);
  bool indexed_target(uint32_t lane, uint32_t& target);
  bool aperture_index(uint64_t address, size_t& index);
  void update_irq();
  void run_cp();
  bool cp_busy() const;
  bool cp_memory(uint64_t address, uint32_t& value, bool write,
    unsigned swap);
  bool cp_writeback();
  void cp_hostdata(const std::vector<uint32_t>& body);
  void draw(uint32_t width, uint32_t height, bool positive_host = false);
  void draw_line(uint32_t start, uint32_t end);
  void host_word(uint32_t value, bool last);
  bool translate(uint64_t gpu, size_t bytes, size_t& index) const;
  bool pixel_address(
    uint32_t base, uint32_t pitch, int32_t x, int32_t y, uint32_t bpp,
    size_t& index) const;
  uint32_t pixel_read(size_t index, uint32_t bytes) const;
  void pixel_write(size_t index, uint32_t bytes, uint32_t value);
  bool clipped(int32_t x, int32_t y, uint32_t tl, uint32_t br) const;
  void composite_cursor(Frame& frame, unsigned controller) const;
};

#endif // INCLUDED_RV200_H_
