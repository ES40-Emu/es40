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

#if !defined(INCLUDED_REALimage2100_H_)
#define INCLUDED_REALimage2100_H_

#include "emu/address_map.h"
#include <array>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// REALimage 2100 transport, VGA storage and supported native 2D/3D profiles.
class CRealImage2100
{
public:
  struct Diagnostic
  {
    std::string code, message;
    uint32_t address, value;
    bool fatal;
  };

  struct Frame
  {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> argb;
  };

  static constexpr uint32_t VGAMemorySize = 0x40000;
  // ROM file offsets 0x378a, 0x37a1 and 0x37b8 load these indirect addresses.
  static constexpr uint32_t DACIndexLow = 0x00838010, DACIndexHigh = 0x00838014,
                            DACIndexedData = 0x00838018;
  static constexpr uint32_t DACRegisterCount = 0x10000;
  // BAR0 offsets (also indexed by BAR2); miniports map registers at BAR0+0x800000.

  // Active-low per-unit resets: drivers write 0 (or clear one bit), then ones.
  static constexpr uint32_t UnitReset = 0x0080041c;
  // Read-only busy bits 0/3/25-31; timing roles for bits 23/24 are inferred.
  static constexpr uint32_t Status = 0x00800420, StatusVBlank = 1u << 23,
                            StatusVRetrace = 1u << 24,
                            StatusUpperWritable = 0xff800000u;
  // Status reads per virtual frame; the timing flags need no host clock.
  static constexpr uint32_t StatusFrameReads = 64;
  // Bytes 0-1: frame counter; 2: chip straps; 3: control (the only writable byte).
  static constexpr uint32_t BoardStatus = 0x008380bc;
  // Code 0 = 12 3D-RAM chips (15 MB), the PowerStorm 300 complement.
  static constexpr uint8_t BoardStraps = 0;
  // Bytes 1-2: WID display banks; byte 3: board ID on read, timing on write.
  static constexpr uint32_t BoardIO = 0x008380b0;
  // PCGA3, which the Compaq PowerStorm 300 driver reports as "PC3".
  static constexpr uint8_t BoardIDPCGA3 = 0xfe;
  // Only ever written 0; no NT miniport installs an ISR (inferred enable mask).
  static constexpr uint32_t InterruptEnable = 0x00800424;
  static constexpr uint32_t SyncCommand = 0x0080042c;
  static constexpr uint32_t DisplaySelect = 0x008380a8,
                            BoardSetup = 0x00800434,
                            InitializationPort = 0x003800b8;
  // BIOS and miniport exit write VGAControlVGA; miniport entry writes Native.
  static constexpr uint32_t VGAControl = 0x00800430, VGAControlVGA = 0x000a0000,
                            VGAControlNative = 0x00100000;
  // Read back by the miniport: clock/memory configuration and monitor pins.
  static constexpr uint32_t DisplayControl = 0x00840000;
  // NT miniport timing bytes and 2D command ports, as BAR0 offsets.
  static constexpr uint32_t TimingBase = 0x00838080,
                            BoardTiming = 0x008380b4,
                            WindowMask = 0x008380b8;
  static constexpr uint32_t ClipXMax = 0x00800400, ClipYMax = 0x00800404,
                            ClipXMin = 0x00800408, ClipYMin = 0x0080040c,
                            GlobalControl0 = 0x00800410,
                            GlobalControl1 = 0x00800414,
                            GlobalControl2 = 0x00800418;
  static constexpr uint32_t TextureBase = 0x00800500;
  static constexpr uint32_t PipelineControl0 = 0x008005c0,
                            PipelineControl1 = 0x008005c4,
                            PipelineControl2 = 0x008005c8,
                            PipelineControl3 = 0x008005d8,
                            PipelineControl4 = 0x008005d0,
                            PipelineControl5 = 0x008005e0;
  // Four source bitmap words; the driver launches a separate drawing command.
  static constexpr uint32_t MonoPattern0 = 0x00800618,
                            MonoPattern1 = 0x0080061c,
                            MonoPattern2 = 0x00800620,
                            MonoPattern3 = 0x00800624;
  // Driver context links are inert metadata preceding DrawControl.
  static constexpr uint32_t ContextLink = 0x008005fc;
  // Integer vertex ARGB components; NT writes each color byte shifted left by two.
  static constexpr uint32_t IntegerVertexColorBase = 0x00800120;
  static constexpr uint32_t VertexBase = 0x00800220, VertexStride = 0x40,
                            VertexTextureBase = 0x0080020c, FlatVertexBase = 0x0080030c;
  static constexpr uint32_t DrawControl = 0x00800600,
                            MemoryControl = 0x00800604,
                            PixelControl = 0x00800608,
                            Foreground = 0x00800610,
                            Background = 0x00800614,
                            HostOrigin = 0x00800628,
                            HostExtent = 0x0080062c,
                            HostCommand = 0x00800630,
                            ContextControl = 0x00800634,
                            FillOrigin = 0x00800644,
                            FillExtent = 0x00800648,
                            FillCommand = 0x0080064c;
  static constexpr uint32_t BitmapForeground = 0x00800660,
                            BitmapBackground = 0x00800664,
                            BitmapPattern0 = 0x00800668,
                            BitmapPattern1 = 0x0080066c,
                            BitmapOrigin = 0x00800670,
                            BitmapExtent = 0x00800674,
                            BitmapCommand = 0x00800678;
  static constexpr uint32_t BlockSource = 0x00800680,
                            BlockDestination = 0x00800684,
                            BlockExtent = 0x00800688,
                            BlockCommand = 0x0080068c;
  static constexpr uint32_t HostData = 0x00c00000, HostDataSize = 0x2000;
  static constexpr uint32_t HostReadSize = 0x400000, HostUploadSize = 0x400000;
  // Bounded logical color banks; physical 3D-RAM layout is not modeled.
  static constexpr uint32_t ColorWidth = 1280, ColorHeight = 1024,
                            ColorPixels = ColorWidth * ColorHeight;
  static constexpr uint32_t MaxColorWidth = 1920, MaxColorHeight = 1200,
                            MaxColorPixels = MaxColorWidth * MaxColorHeight;
  static constexpr uint32_t PlaneStateBase = 0x00ffe700,
                            PlaneClearColor = 0x00ffe100,
                            PlanePixelMask = 0x00ffe400,
                            PlanePixelMask0 = 0x00c02400,
                            PlanePixelMask01 = 0x00c06400,
                            PlanePixelMask012 = 0x00c0e400,
                            PlanePixelMask0123 = 0x00c1e400,
                            PlanePixelMask3 = 0x00c10400,
                            PlanePixelMask23 = 0x00c18400,
                            PlanePixelMask123 = 0x00c1c400,
                            PlaneCount = 4, LegacyPlaneRegisterCount = 28,
                            PlaneRegisterCount = 32;
  static constexpr uint32_t DMABase = 0x00801000, DMARegisterCount = 19,
                            DMAControl = DMABase, DMAInterruptEnable = DMABase + 4,
                            DMACommand = 0x0080101c, DMAReset = 0x0080103c;
  // Outer descriptors count DWORDs in bits 21:0; packet counts are 16-bit.
  static constexpr uint32_t DMACommandListCountMask = 0x003fffff,
                            DMACommandListMaxBytes = DMACommandListCountMask * 4;
  static constexpr uint32_t MinTextureSize = 16u * 1024 * 1024,
                            MaxTextureSize = 32u * 1024 * 1024;
  // Bounds shadow storage; BAR0 decodes far more than any register file.
  static constexpr uint32_t MaxShadowRegisters = 4096;
  // Bounds for supported snapshot versions, header included.
  static constexpr uint32_t MinStateSize =
    16 + 52 + 4 + 24 + 256 * 4 + DACRegisterCount + 4 + VGAMemorySize +
    ColorPixels * (2 * 4 + 1) + 4 + DMARegisterCount * 4 + MinTextureSize +
    PlaneCount * (LegacyPlaneRegisterCount + 2) * 4 + 2 * 3 * 4;
  static constexpr uint32_t PatternStateSize = PlaneCount * (8 + 64) * 4 + 3 * (1 + 64) * 4;
  static constexpr uint32_t MaxStateSize = MinStateSize + 24 + MaxShadowRegisters * 8 +
    MaxTextureSize - MinTextureSize + (MaxColorPixels - ColorPixels) * 9 +
    PlaneCount * (PlaneRegisterCount - LegacyPlaneRegisterCount) * 4 +
    MaxColorPixels * 3 + 24 + PatternStateSize;

  using IRQCallback = std::function<void(bool)>;
  using DiagnosticCallback = std::function<void(const Diagnostic&)>;
  using UnimplementedCallback = std::function<void(
    const char* what, uint32_t address, uint32_t value, bool write)>;
  // Write payload, then zero to the completion address.
  using DMAWriter = std::function<bool(uint32_t, const uint8_t*, size_t, uint32_t)>;
  // Preflight source and completion RAM; read payload without completing.
  using DMAReader = std::function<bool(uint32_t, uint8_t*, size_t, uint32_t)>;
  using DMACompleter = std::function<bool(uint32_t)>;

  CRealImage2100();
  CRealImage2100(const CRealImage2100&) = delete;
  CRealImage2100& operator=(const CRealImage2100&) = delete;
  void reset(bool clear_vga_memory = false);
  // Widths are BITS, like CPermedia2/CRV200 and CPCIDevice callbacks.
  uint32_t ReadMem(uint32_t address, int dsize = 32);
  void WriteMem(uint32_t address, int dsize, uint32_t data);
  // BAR2 index/data pair.
  uint32_t io_read(uint32_t offset, int dsize);
  void io_write(uint32_t offset, int dsize, uint32_t data);
  void configure_framebuffer(uint32_t ram_chips);
  void configure_texture_memory(uint32_t bytes);
  uint8_t board_straps() const { return m_board_straps; }
  uint32_t color_width() const { return m_color_width; }
  uint32_t color_height() const { return m_color_height; }
  uint32_t color_pixels() const { return m_color_pixels; }
  // BAR1 texture memory, using the driver's 16 KiB row pitch.
  uint32_t mem_read(uint32_t address, int dsize);
  void mem_write(uint32_t address, int dsize, uint32_t data);

  uint32_t io_index() const { return m_io_index; }

  uint16_t dac_index() const { return m_dac_index; }

  uint32_t vga_control() const { return m_vga_control; }

  bool native_display() const { return m_vga_control & VGAControlNative; }

  uint16_t frame_counter() const { return m_frame_counter; }

  // One virtual frame of time from the board's periodic service.
  void advance_frame();

  Frame scanout(std::string* error = nullptr) const;
  Frame scanout(Frame frame, std::string* error = nullptr) const;

  void set_diagnostic_callback(DiagnosticCallback fn)
  {
    m_diagnostic = std::move(fn);
  }

  void set_unimplemented_callback(UnimplementedCallback fn)
  {
    m_unimplemented = std::move(fn);
  }

  bool irq_asserted() const { return m_irq; }
  void set_irq_callback(IRQCallback fn) { m_irq_callback = std::move(fn); }

  void set_dma_writer(DMAWriter fn) { m_dma_writer = std::move(fn); }
  void set_dma_reader(DMAReader fn) { m_dma_reader = std::move(fn); }
  void set_dma_completer(DMACompleter fn) { m_dma_completer = std::move(fn); }

  // Borrowed by vga_device. No claim that native BAR1 aliases this storage.
  uint8_t* vram_data() { return m_vga_memory.data(); }

  const std::vector<uint8_t>& vram() const { return m_vga_memory; }

  uint32_t palette_color(unsigned i) const { return m_palette[i & 255]; }

  void set_palette_color(unsigned i, uint32_t c)
  {
    m_palette[i & 255] = c & 0xffffff;
  }

  uint8_t palette_read_index() const { return m_palette_read; }

  uint8_t palette_write_index() const { return m_palette_write; }

  void set_palette_read_index(uint8_t i) { m_palette_read = i; }

  void set_palette_write_index(uint8_t i) { m_palette_write = i; }

  // Versioned little-endian snapshot. Loading validates before committing.
  void SaveState(std::ostream&) const;
  void RestoreState(std::istream&);
  static void write_ppm(std::ostream&, const Frame&);

private:
  struct ColorWriteContext
  {
    std::array<uint32_t, 2> masks{}, rops{};
  };
  struct PixelOperation
  {
    ColorWriteContext context;
    uint32_t x, y, value, banks, lanes;
    bool invalidate_cache;
  };
  ColorWriteContext prepare_color_write() const;
  PixelOperation prepare_pixel(uint32_t x, uint32_t y, uint32_t value,
    uint32_t banks, uint32_t lanes = 0xffffffffu, bool invalidate_cache = false) const;
  void execute_pixel(const PixelOperation& operation);
  void dac_port_map(address_map& map);
  uint32_t dac_data_offset() const;
  void advance_dac_data();
  uint8_t dac_data_read();
  void dac_data_write(uint8_t value);
  static uint32_t misr_step(uint32_t signature, uint32_t pixel);
  static uint32_t dac_rgb30(uint32_t pixel);
  bool capture_misr(uint32_t& signature);
  uint32_t* native_register(uint32_t dword_address);
  bool framebuffer_access(uint32_t address, int bits, uint32_t& value, bool write);
  bool auxiliary_framebuffer_profile() const;
  uint32_t texture_offset(uint32_t address) const;
  uint32_t status_read();
  uint32_t peek(uint32_t address) const;
  bool native_storage_register(uint32_t address) const;
  static int plane_register(uint32_t address);
  bool plane_write(uint32_t address, uint32_t lanes, uint32_t value);
  uint32_t plane_value(unsigned bank, unsigned index, uint32_t fallback) const;
  uint32_t plane_clear_value(unsigned bank, unsigned group, unsigned word) const;
  bool plane_clear_written(unsigned bank, unsigned group, unsigned word) const;
  bool plane_profile(unsigned bank, uint32_t format = 0x100, uint32_t rop_high = 0,
    uint32_t multiply_control = 0) const;
  bool native_pixel_profile() const;
  uint32_t block_width() const;
  bool native_copy_control_profile(bool clear = false, bool integer = false) const;
  bool block_copy_profile() const;
  bool block_transfer_profile(bool upload, bool native_command = false) const;
  bool bitmap_profile() const;
  bool auxiliary_mono_profile() const;
  bool integer_rgb_profile(bool configuration = false) const;
  bool fill_profile(bool initialization = false) const;
  bool copy_profile() const;
  bool fast_copy_profile(bool block = false) const;
  enum class TrianglePreparation { Rejected, NoOp, Ready };
  struct TriangleOperation
  {
    struct Vertex { double alpha, red, green, blue, x, y, z, s, t; };
    struct RowEdge { unsigned p, q; double slope; bool inclusive; };
    Vertex vertex[3]{};
    double flat_color[4]{}, area = 0;
    RowEdge row_edges[3]{};
    int left = 0, top = 0, right = 0, bottom = 0;
    bool textured = false, flat = false, blend = false, no_depth = false, depth_less = false;
    bool affine = false, texture_alpha = false;
    uint32_t wid = 0, bank = 0, color_width = 0, color_rop = 0;
    size_t color_offset = 0;
    uint32_t texture_base = 0, texture_width = 0, texture_height = 0;
    uint32_t texture_format = 0, texture_mode = 0, texture_environment = 0;
    uint32_t blend_control = 0, texture_clamp = 0, texture_border = 0;
    unsigned texture_row_shift = 0;
  };
  bool triangle_profile() const;
  TrianglePreparation prepare_triangle(uint32_t address, uint32_t value,
    TriangleOperation& operation) const;
  void execute_triangle(const TriangleOperation& operation);
  void triangle_command(uint32_t address, uint32_t value);
  void raster_vertex_command(uint32_t address, uint32_t value);
  uint32_t texture_color(double s, double t, uint32_t base,
    uint32_t width, uint32_t height, unsigned row_shift) const;
  std::array<double, 4> texture_sample(double s, double t, uint32_t base,
    uint32_t width, uint32_t height, unsigned row_shift, uint32_t format,
    uint32_t clamp, uint32_t border) const;
  struct BlockOperation
  {
    uint32_t value = 0, banks = 0, source = 0, clear_width = 0, groups = 0,
      x = 0, y = 0, right = 0, bottom = 0, seed_key = 0,
      auxiliary_mask = 0, auxiliary_value = 0;
    bool valid = false, geometry_known = false, copy = false, seed = false,
      auxiliary_clear = false, allow_pattern = false;
    ColorWriteContext color;
    std::array<uint32_t, 2> colors{};
    std::array<bool, 2> replace_color{};
    std::array<std::array<uint32_t, 8>, 3> pixel_masks{};
    std::array<std::array<uint32_t, 64>, 3> clear_values{};
    std::array<bool, 3> patterned{};
  };
  BlockOperation prepare_block(uint32_t value) const;
  void execute_block(const BlockOperation& operation);

  struct StartPrefix
  {
    uint32_t address, value, selected, destination_banks;
  };
  StartPrefix begin_start(uint32_t address, uint32_t value);
  struct StartOperation
  {
    enum class Kind { Empty, Rejected, Readback, Upload, Copy, Pattern };
    enum class Rejection { Profile, ReadbackSource, HostBounds,
      CopyAlignment, CopySource, MonoLayout };
    Kind kind = Kind::Empty;
    Rejection rejection = Rejection::Profile;
    uint32_t address = 0, value = 0, banks = 0, destination_banks = 0,
      width = 0, height = 0, source_bank = 0,
      destination_bank = 0, foreground = 0, background = 0, mono_offset = 0,
      auxiliary_mask = 0, auxiliary_rop = 0, auxiliary_reference = 0,
      auxiliary_compare_mask = 0;
    int32_t x = 0, y = 0, sx = 0, sy = 0, left = 0, right = 0,
      top = 0, bottom = 0;
    bool fast_copy = false, right_to_left = false, bottom_to_top = false,
      mono = false, transparent = false, auxiliary = false;
    std::array<uint32_t, 4> pattern{};
    ColorWriteContext color;
  };
  StartOperation prepare_start(const StartPrefix& prefix) const;
  void execute_start(const StartOperation& operation);
  void start_command(uint32_t address, uint32_t value);
  void block_command(uint32_t value);
  void host_data(uint32_t value);
  uint32_t host_read();
  uint32_t readback_pixel(uint32_t word) const;
  struct DMAListOperation
  {
    struct Packet { uint32_t address, wait_mask, first, count; };
    uint32_t command = 0, completion = 0;
    std::vector<uint8_t> data;
    std::vector<Packet> packets;
  };
  bool prepare_dma_list(uint32_t value, DMAListOperation& operation);
  void execute_dma_list(const DMAListOperation& operation);
  void update_irq();
  void dma_completed();
  bool dma_setup_supported(uint32_t value) const;
  void dma_command(uint32_t value);
  void dma_command_list(uint32_t value);
  void dma_texture_upload(uint32_t value);
  bool dma_list_target(uint32_t address) const;
  void color_write(const ColorWriteContext& context, uint32_t x, uint32_t y,
    uint32_t color, uint32_t banks, uint32_t lanes = 0xffffffffu);
  Frame dac_frame(Frame frame, std::string* error, bool vram_input = false) const;
  void composite_cursor(Frame& frame, bool ten_bit = false) const;
  void report(
    const char* code, uint32_t address, uint32_t value, const char* message,
    bool fatal = false);
  void unimplemented(const char* what, uint32_t a, uint32_t v, bool write);
  // Once per register; the set is bounded like the shadow.
  void unimplemented_once(const char* what, uint32_t a, uint32_t v, bool write);

  address_map m_dac_ports{0x20};
  std::vector<uint8_t> m_vga_memory;
  std::array<uint32_t, 256> m_palette{};
  // RGB640 byte registers and flattened native table/color streams.
  std::vector<uint8_t> m_dac_regs;
  uint32_t m_color_width = ColorWidth, m_color_height = ColorHeight,
           m_color_pixels = ColorPixels;
  uint8_t m_board_straps = BoardStraps;
  std::vector<uint32_t> m_color;
  // Packed plane 4, with WID in bits 12..15.
  std::vector<uint32_t> m_auxiliary;
  std::vector<uint8_t> m_texture;
  std::array<uint32_t, DMARegisterCount> m_dma_regs{};
  struct PlaneState
  {
    uint32_t written = 0;
    uint32_t unknown_masks = 0;
    std::array<uint32_t, PlaneRegisterCount> regs{};
    std::array<std::array<uint32_t, 8>, 8> clear_colors{};
    std::array<uint8_t, 8> clear_written{};
  };
  std::array<PlaneState, PlaneCount> m_planes{};
  // Offscreen sources seeded by the driver's block-clear path.
  struct ClearCache
  {
    uint32_t source = 0, color = 0, known = 0;
    bool patterned = false;
    std::array<uint32_t, 64> pattern{};
  };
  std::array<ClearCache, 3> m_clear_cache{};
  // Host data is a stream of RGB dwords, not a framebuffer address.
  struct Pending
  {
    uint32_t x = 0, y = 0, width = 0, height = 0, word = 0, banks = 0;
    uint32_t auxiliary_compare_mask = 0, auxiliary_reference = 0;
  } m_pending, m_readback;
  // Unknown native registers, keyed by dword-aligned address.
  std::map<uint32_t, uint32_t> m_shadow;
  std::set<uint32_t> m_warned;
  bool m_aperture_warned = false, m_shadow_full_warned = false;
  uint32_t m_unit_reset = 0, m_interrupt_enable = 0,
           m_vga_control = VGAControlVGA, m_display_control = 0;
  uint32_t m_status_phase = 0;
  uint32_t m_status_mask = ~0u;
  uint16_t m_frame_counter = 0;
  uint8_t m_board_control = 0, m_board_timing = 0;
  uint32_t m_board_io = 0;
  uint32_t m_io_index = 0;
  uint16_t m_dac_index = 0;
  uint8_t m_dac_component = 0; // RGB byte within a streamed color entry.
  uint8_t m_palette_read = 0, m_palette_write = 0;
  DiagnosticCallback m_diagnostic;
  UnimplementedCallback m_unimplemented;
  bool m_irq = false, m_dma_irq_pending = false;
  IRQCallback m_irq_callback;
  DMAWriter m_dma_writer;
  DMAReader m_dma_reader;
  DMACompleter m_dma_completer;
  bool m_dma_list_active = false, m_dma_list_rejected = false;
};

#endif // INCLUDED_REALimage2100_H_
