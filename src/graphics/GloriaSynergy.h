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

#if !defined(INCLUDED_GloriaSynergy_H_)
#define INCLUDED_GloriaSynergy_H_

#include "SVGA.h"
#include "Permedia2.h"
#include "base/Runnable.h"
#include "gui/gui.h"
#include <array>
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

/** ELSA GLoria Synergy PCI video card with a Permedia 2 graphics processor. */
class CGloriaSynergy :
  public svga_device,
  public CRunnable,
  public CDisplayOutputProvider
{
public:
  // Board identity and firmware metadata are owned by the card, not the chip.
  // SN-PBXGK-BB is the intended board; reset straps remain configurable.
  struct PCIConfig
  {
    u16 vendor = 0x104c, device = 0x3d07, subsystem_vendor = 0,
        subsystem_device = 0;
    u8 revision = 1; // TVP4020 HRM: CFGRevisionId.
    u32 class_code = 0x030000;
    std::array<u32, 64> config_data() const;
    static std::array<u32, 64> config_mask();
  };

  struct OptionROM
  {
    std::vector<u8> bytes;
    u16 vendor = 0, device = 0, subsystem_vendor = 0, subsystem_device = 0;
    u32 file_size = 0, image_size = 0;
    bool agp_banner = false;
    std::string layout;
    static OptionROM parse(
      const std::vector<u8>& raw, const std::string& mapping);
  };

  CGloriaSynergy(
    CConfigurator* cfg, CSystem* c, int pcibus, int pcidev, bx_gui_c& display);
  ~CGloriaSynergy() override;

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

  void reset_vga();
  void sync_pci_config();
  bool legacy_enabled(bool memory) const noexcept;
  u8 io_read_b(u32 port);
  void io_write_b(u32 port, u8 data);
  void tick();
  CPermedia2::Frame render_frame();
  void trace(char op, int bar, u32 address, int bits, u32 value);
  void publish_frame();
  void trace_unimplemented(const std::string& text) override;

  CDisplayOutput m_output;
  CPermedia2 m_permedia2;
  // Host scanout storage is shared only by the display and frame-file paths.
  // Acquire this mutex before the device bus; native conversion releases the bus.
  std::mutex m_frame_mutex;
  CPermedia2::Scanout m_scanout;
  PCIConfig m_profile;
  std::vector<u8> m_rom;
  std::ofstream m_trace;
  std::string m_frame_file, m_rom_layout;
  u64 m_access_count = 0;
  u32 m_service_budget = 4096, m_scanline_access_divisor = 1024;
  bool m_allow_dma = true, m_trace_apertures = false, m_initialized = false;
  bool m_replaying_pci = false;
  u8 m_mode640 = 0;

  CThread* m_thread = nullptr;
  std::atomic<bool> m_stop{false}, m_pause{false}, m_pause_ack{false},
    m_worker_failed{false};
  bool m_gui_initialized = false;
  unsigned m_host_width = 0, m_host_height = 0;
};

#endif // INCLUDED_GloriaSynergy_H_
