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

#if !defined(INCLUDED_PowerStorm3xx_H_)
#define INCLUDED_PowerStorm3xx_H_

#include "SVGA.h"
#include "REALimage2100.h"
#include "base/Runnable.h"
#include "gui/gui.h"
#include <array>
#include <atomic>
#include <fstream>
#include <string>
#include <vector>

// PowerStorm 300/350 PCI board with VGA and REALimage 2100 native display.
class CPowerStorm3xx :
  public svga_device,
  public CRunnable,
  public CDisplayOutputProvider
{
public:
  // ROM identifies the board as 10BA:0304; revision is unmeasured.
  struct PCIConfig
  {
    u16 vendor = 0x10ba, device = 0x0304, subsystem_vendor = 0,
        subsystem_device = 0;
    u8 revision = 0;
    u32 class_code = 0x030000;
    // BAR0 includes the front, back and depth apertures through 0x03a00000.
    u32 bar0_size = 0x04000000, bar1_size = 0x02000000;
    std::array<u32, 64> config_data() const;
    std::array<u32, 64> config_mask(u32 rom_size) const;
  };

  struct OptionROM
  {
    std::vector<u8> bytes;
    u32 file_size = 0, image_bytes = 0, pcir_class = 0;
    static OptionROM parse(
      const std::vector<u8>& raw, const std::string& layout);
  };

  // BAR2 is the native index/data I/O pair observed in the BIOS.
  static constexpr u32 IOBARSize = 8;

  CPowerStorm3xx(
    CConfigurator* cfg, CSystem* c, int pcibus, int pcidev, bx_gui_c& display);
  ~CPowerStorm3xx() override;

  void init() override;
  void ResetPCI() override;
  void start_threads() override;
  void stop_threads() override;
  void run() override;
  void update();
  void check_state() override;

  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;
  u32 ReadMem_Legacy(int index, u32 address, int dsize) override;
  void WriteMem_Legacy(int index, u32 address, int dsize, u32 data) override;
  u32 ReadMem_Bar(int func, int bar, u32 address, int dsize) override;
  void WriteMem_Bar(
    int func, int bar, u32 address, int dsize, u32 data) override;
  bool decodes_memory_access(
    int index, u64 address, int dsize, bool write) const noexcept override;
  u32 config_read_custom(int func, u32 address, int dsize, u32 data) override;
  void config_write_custom(
    int func, u32 address, int dsize, u32 old_data, u32 new_data,
    u32 data) override;

  int SaveState(FILE* f) override;
  int RestoreState(FILE* f) override;
  void prepare_snapshot() override;
  void finalize_restore() noexcept override;
  std::string snapshot_identity() const override;

  std::size_t output_count() const override { return 1; }

  const CDisplayOutput* output_at(std::size_t ordinal) const override
  {
    return ordinal == 0 ? &m_output : nullptr;
  }

  u8 mem_r(offs_t offset) override;
  void mem_w(offs_t offset, u8 data) override;

protected:
  address_map& space(int spacenum) override;
  void crtc_map(address_map& map) override;
  void sequencer_map(address_map& map) override;
  void gc_map(address_map& map) override;
  void attribute_map(address_map& map) override;
  void palette_update() override;
  void recompute_params() override;

private:
  enum
  {
    LegacyIO = 1,
    LegacyMemory = 2
  };

  // Portable frontend snapshot: the Radeon layout plus CR1C/CR1D.
  static constexpr size_t VGAStateSize = 96;

  void reset_vga();
  bool legacy_enabled(bool memory) const noexcept;
  bool vga_port_enabled(u32 port) const noexcept;
  u8 io_read_b(u32 port);
  void io_write_b(u32 port, u8 data);
  u8 input_status();
  void advance_vga_scanlines(u32 lines);
  CRealImage2100::Frame render_frame();
  CRealImage2100::Frame render_frame(CRealImage2100::Frame frame);
  void update(CRealImage2100::Frame& frame);
  std::vector<u8> save_vga_state() const;
  void restore_vga_state(const std::vector<u8>& state);
  void trace(const char* op, int region, u32 address, int bits, u32 value);
  void publish_frame();
  void diagnostic(const CRealImage2100::Diagnostic& d);
  bool dma_write(u32 address, const uint8_t* source, size_t count, u32 completion);
  bool dma_ram_range(u32 address, size_t count);
  bool dma_read(u32 address, uint8_t* destination, size_t count, u32 completion);
  bool dma_complete(u32 completion);
  void trace_unimplemented(const std::string& text) override;

  CDisplayOutput m_output;
  CRealImage2100 m_realimage;
  PCIConfig m_profile;
  std::vector<u8> m_rom;
  std::ofstream m_trace;
  std::string m_frame_file, m_rom_layout;
  u64 m_access_count = 0;
  u32 m_vga_scanline = 0, m_vga_status_phase = 0, m_model = 300;
  u16 m_crtc_io_base = 0; // BIOS scratch copy of BAR2, not a decoder.
  u8 m_vga_enable = 1;
  bool m_board_vga_enabled = true, m_initialized = false;
  bool m_replaying_pci = false, m_trace_apertures = false;

  CThread* m_thread = nullptr;
  std::atomic<bool> m_stop{false}, m_pause{false}, m_pause_ack{false},
    m_worker_failed{false};
};

#endif // INCLUDED_PowerStorm3xx_H_
