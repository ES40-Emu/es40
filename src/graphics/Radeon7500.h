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

#if !defined(INCLUDED_Radeon7500_H_)
#define INCLUDED_Radeon7500_H_

#include "SVGA.h"
#include "RV200.h"
#include "base/Runnable.h"
#include "gui/gui.h"
#include <array>
#include <atomic>
#include <fstream>
#include <string>
#include <vector>

/** ATI Radeon 7500 PCI video card with an RV200 graphics processor. */
class CRadeon7500 : public svga_device, public CRunnable, public CDisplayOutputProvider
{
public:
  // Board identity is owned by the card, not the chip. HP 3X-PBXGG-AA is the
  // intended board; revision and subsystem IDs are unmeasured defaults.
  struct PCIConfig
  {
    u16 vendor = 0x1002, device = 0x5157, subsystem_vendor = 0,
        subsystem_device = 0;
    u8 revision = 0;
    u32 class_code = 0x030000;
    std::array<u32, 64> config_data() const;
    static std::array<u32, 64> config_mask(u32 rom_size);
  };

  struct OptionROM
  {
    std::vector<u8> bytes;
    u32 file_size = 0;
    static OptionROM parse(
      const std::vector<u8>& raw, const std::string& layout);
  };

  // 128 MiB framebuffer (two apertures), 256-byte I/O, 16 KiB registers.
  static constexpr u32 FramebufferBARSize = 2 * CRV200::ApertureSize,
                       IOBARSize = 0x100,
                       RegisterBARSize = CRV200::RegisterSpaceSize;

  CRadeon7500(
    CConfigurator* cfg, CSystem* c, int pcibus, int pcidev, bx_gui_c& display);
  ~CRadeon7500() override;

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

  u8 get_actl_palette_idx(u8 index) override;
  void redraw_area(
    unsigned x0, unsigned y0, unsigned width, unsigned height) override;
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

  // Portable frontend snapshot: indexed registers, selectors and latches.
  static constexpr size_t VGAStateSize = 94;

  void reset_vga();
  void sync_apertures();
  bool legacy_enabled(bool memory) const noexcept;
  bool vga_port_enabled(u32 port) const noexcept;
  u8 io_read_b(u32 port);
  void io_write_b(u32 port, u8 data);
  u8 input_status();
  void advance_vga_scanlines(u32 lines);
  void tick();
  CRV200::Frame render_frame();
  std::vector<u8> save_vga_state() const;
  void restore_vga_state(const std::vector<u8>& state);
  void trace(const char* op, int bar, u32 address, int bits, u32 value);
  void publish_frame();
  void trace_unimplemented(const std::string& text) override;

  CDisplayOutput m_output;
  CRV200 m_rv200;
  PCIConfig m_profile;
  std::vector<u8> m_rom;
  std::ofstream m_trace;
  std::string m_frame_file, m_rom_layout;
  u64 m_access_count = 0;
  u32 m_scanline_access_divisor = 1024;
  u32 m_vga_scanline = 0, m_vga_status_phase = 0;
  u8 m_vga_enable = 1;
  bool m_trace_apertures = false, m_initialized = false;
  bool m_replaying_pci = false;

  CThread* m_thread = nullptr;
  std::atomic<bool> m_stop{false}, m_pause{false}, m_pause_ack{false},
    m_worker_failed{false};
  bool m_gui_initialized = false;
  unsigned m_host_width = 0, m_host_height = 0;
};

#endif // INCLUDED_Radeon7500_H_
