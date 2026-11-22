/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2025-2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
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
 *
 */

#pragma once

#include "System.h"
#include "emu/PCIDevice.h"
#include "i2c_spd.h"

/**
 * \brief Emulated Typhoon 21272 chipset.
 *
 * Documentation consulted:
 *  - Tsunami/Typhoon 21272 Chipset Hardware Reference Manual  [HRM] (http://download.majix.org/dec/tsunami_typhoon_21272_hrm.pdf)
 *  - AlphaServer ES40 and AlphaStation ES40 Service Guide [SG] (http://www.dec-store.com/PD_00158.aspx)
 *  - Tru64 include file dc104x.h [T64] (http://samy.pl/packet/MISC/tru64/usr/include/alpha/dc104x.h)
 *  .
 *
 * The ES40 emulator has the following chipset configuration:
 *   - 1 x 21274-C1 Cchip (controller chip) - The Cchip controls the other chips in the
 *     chipset, as well as the DRAM memory array in a system. The Cchip interfaces with
 *     the CPU’s command and address buses.
 *   - 8 x 21274-D1 Dchip (data slice chip) - The Dchips interface with the system data
 *     bus and provide the data path between the CPU, DRAM memory, and the Pchip(s).
 *   - 2 x 21272-P1 Pchip (peripheral interface chip) - The interface to the PCI bus.
 *   .
 **/
class Tsunami final : public CSystem, public PCIHost
{
public:
  explicit Tsunami(CConfigurator* cfg);
  ~Tsunami() override { destroy_components(); }
  void attach_board_devices(CConfigurator* cfg) override;
  int LoadROM() override;
  bool has_interval_timer() const noexcept override { return true; }
  u64 interval_timer_period_ns() override;
  void interval_timer_tick() override { interrupt(-1, true); }
  u32 get_tick_seq() override { return m_tick_seq.load(std::memory_order_relaxed); }
  void interrupt(int number, bool assert) override;
  u64 PCI_Phys(int pcibus, u32 address) override;

  void clear_clock_int(int ProcNum);
  void clear_ipi(int ProcNum);   // ack interprocessor interrupt: clear MISC<IPINTR>, drop b_irq<3>
  u64 get_c_misc() const { return state.cchip.misc; }
  u64 get_c_dir(int ProcNum) const { return state.cchip.drir & state.cchip.dim[ProcNum]; }
  u64 get_c_dim(int ProcNum) const { return state.cchip.dim[ProcNum]; }
  void set_c_dim(int ProcNum, u64 value) { state.cchip.dim[ProcNum] = value; }

  // DIMM topology: 4 MMBs of 8 slots; array a lives on MMB a as sets of 4
  // identical DIMMs (J1-4 lower set, J5-8 upper set). Max config = 4 arrays
  // x 8 x 1GB DIMMs = 32GB (all 32 slots).
  struct SDimmLayout
  {
    int      n_arrays;        // populated arrays == populated MMBs (1, 2 or 4)
    int      dimms_per_array; // 8 (twice-split) or 4 (lower set only)
    uint32_t dimm_mb;         // capacity of each (identical) DIMM
  };
  const SDimmLayout& get_dimm_layout() const { return m_dimm_layout; }
  const std::vector<uint8_t>& get_dimm_spd() const { return m_dimm_spd; }

private:
  void bind_board_devices() override;
  u64 read_unmapped(u64 address, int dsize, CSystemComponent* source) override;
  void write_unmapped(u64 address, int dsize, u64 data, CSystemComponent* source) override;
  bool allows_shared_access(u64 address, int dsize) const override;
  void dispatch_mapped_write(u64 address, int dsize, u64 data, const SDecodeClaims& claims) override;
  void reset_chipset() override;
  void finish_board_restore() override;
  void flush_boot_rom() override;

  u64 PCI_Phys_direct_mapped(u32 address, u64 wsm, u64 tba);
  u64 PCI_Phys_scatter_gather(u32 address, u64 wsm, u64 tba);
  u64 cchip_csr_read(u32 address, CSystemComponent* source);
  void cchip_csr_write(u32 address, u64 data, CSystemComponent* source);
  u64 pchip_csr_read(int num, u32 address);
  void pchip_csr_write(int num, u32 address, u64 data);
  void latch_pchip_error(int num, u64 data);
  void pchip_config_write_abort(int num, u32 address);
  u8 dchip_csr_read(u32 address);
  void dchip_csr_write(u32 address, u8 data);
  u8 tig_read(u32 address);
  void tig_write(u32 address, u8 data);
  void tig_update_halt_lines();
  void update_pchip_error_irqs();
  void recompute_device_irqs();

  // Serializes DRIR updates and interrupt delivery across device threads, outside saved state.
  std::mutex drir_lock;
  std::atomic<u32> m_tick_seq{ 0 };  // interval-tick sequence; CPU instruction pacing
  std::atomic<bool> m_reported_3c509_probe{ false };

  struct MPDState {
    // Host open-drain drivers (1 = released high, 0 = pulling low)
    bool cks_out = true;   // SCL
    bool ds_out = true;    // SDA
  } m_mpd;
  I2CBus m_mpd_bus;

  // Build SPD images that match configured memory.
  void init_spd_from_config_mb(uint32_t total_mb);
  SDimmLayout m_dimm_layout{};
  std::vector<uint8_t> m_dimm_spd; // shared SPD image (all DIMMs identical)
};
