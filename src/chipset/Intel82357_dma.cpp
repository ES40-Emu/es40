/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#include "StdAfx.h"
#include "Intel82357.h"
#include "system/System.h"

static constexpr u16 dma_low_page_base = 0x81;
static constexpr u16 dma_high_page_base = 0x481;
static constexpr u16 dma0_chaining_mode_port = 0x40a;
static constexpr u16 dma1_chaining_mode_port = 0x4d4;
static constexpr u16 dma_chain_expiration_control_port = 0x40c;
static constexpr u8 dma_chain_enable = 0x04;
static constexpr u8 dma_programming_complete = 0x08;
static constexpr u8 dma_chain_master_tc = 0x10;
static constexpr u16 dma0_extended_mode_port = 0x40b;
static constexpr u16 dma1_extended_mode_port = 0x4d6;
static constexpr u8 dma_cascade_mode = 0xc0;

class Intel82357::Dma final : public CDMA
{
public:
  Dma(CConfigurator* cfg, CSystem* system, Intel82357& isp);
  enum class Register { None, Legacy, HighPage, HighCount, Stop, Chaining, Expiration, ExtendedMode };
  struct DecodedPort {
    Register reg;
    int channel = 0;
    unsigned shift = 0;
  };
  static DecodedPort decode_port(u16 port, bool write);
  bool access_port(u16 port, u8& data, bool write);
  void ResetPCI() override;
  int SaveState(FILE* f) override;
  int RestoreState(FILE* f) override;

private:
  static bool decodes_legacy_port(u16 port, bool write);
  void reset_extension();
  void select_compatible_address(int num, bool update_current);
  void program_address(int num, u16 keep, u16 bits) override;
  void program_count(int num, u32 keep, u32 bits) override;
  void program_page(int num, u16 keep, u16 bits) override;
  void program_mode(int num, u8 data) override;
  void master_clear_channel(int num) override;
  SDMA_transfer_properties transfer_properties(int num) const override;
  bool accepts_external_eop(int num) const override;
  bool accepts_software_request(int num) const override;
  size_t bytes_through_stop(int num, const SDMA_transfer_properties& properties) const;
  size_t limit_transfer(int num, size_t length, size_t remaining,
    const SDMA_transfer_properties& properties, SDMA_result& result) const override;
  void complete_transfer(int num, SDMA_result& result) override;
  void load_current_from_base(int num) override;
  void transfer_serviced(int num, SDMA_result& result) override;

  bool chaining_enabled(int num) const;
  void reset_chaining_mode(int num);
  void write_chaining_mode(int num, u8 data);
  void write_extended_mode(int num, u8 data);
  u8 chaining_channels(u8 flag) const;
  u8 pending_chaining_irqs() const;
  void update_chaining_irq(bool force = false);

  Intel82357& isp;
  bool chaining_irq_asserted = false;
  struct {
    bool extended_address[8]; // 82357 high page was written last for the current buffer.
    bool base_extended_address[8]; // Address carry mode for the base registers.
    u8 extended_mode[8]; // Bits 7:2 of each channel's extended-mode register.
    u8 chaining_mode[8]; // Bits 4:2 select TC output, programming complete and chaining enable.
    u8 chaining_pending; // Expired buffers awaiting Programming Complete.
    u32 stop_address[8]; // Address bits 23:2 for channels 0-3 and 5-7.
  } state{};
};

void Intel82357::create_dma(CConfigurator* cfg, CSystem* system)
{
  dma = new Dma(cfg, system, *this);
}

CDMA& Intel82357::dma_controller() const
{
  return *dma;
}

bool Intel82357::decodes_dma_port(u16 port, bool write)
{
  return Dma::decode_port(port, write).reg != Dma::Register::None;
}

bool Intel82357::access_dma_port(u16 port, u8& data, bool write)
{
  return dma->access_port(port, data, write);
}

Intel82357::Dma::Dma(CConfigurator* cfg, CSystem* system, Intel82357& isp)
  : CDMA(cfg, system, isp, 0xffffffU), isp(isp)
{
  save_item(state.extended_address, "dma.extended_address");
  save_item(state.base_extended_address, "dma.base_extended_address");
  save_item(state.extended_mode, "dma.extended_mode");
  save_item(state.chaining_mode, "dma.chaining_mode");
  save_item(state.chaining_pending, "dma.chaining_pending");
  save_item(state.stop_address, "dma.stop_address");
  reset_extension();
}

void Intel82357::Dma::reset_extension()
{
  state = {};
  channel(4).mode = dma_cascade_mode;
  for (int num = 4; num < 8; ++num)
    state.extended_mode[num] = 0x04;
  update_chaining_irq();
}

void Intel82357::Dma::ResetPCI()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  CDMA::ResetPCI();
  reset_extension();
}

int Intel82357::Dma::SaveState(FILE* f)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  return CSystemComponent::SaveState(f) ? -1 : CDMA::SaveState(f);
}

int Intel82357::Dma::RestoreState(FILE* f)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  const auto previous = state;
  if (CSystemComponent::RestoreState(f))
    return -1;
  if (CDMA::RestoreState(f)) {
    state = previous;
    return -1;
  }
  channel(4).mode |= dma_cascade_mode;
  state.chaining_pending &= chaining_channels(dma_chain_enable);
  update_chaining_irq(true);
  return 0;
}

static int high_count_channel(u16 port)
{
  switch (port) {
  case 0x401: return 0;
  case 0x403: return 1;
  case 0x405: return 2;
  case 0x407: return 3;
  case 0x4c6: return 5;
  case 0x4ca: return 6;
  case 0x4ce: return 7;
  default: return -1;
  }
}

static bool stop_register(u16 port, int& num, unsigned& shift)
{
  if (port < 0x4e0 || port > 0x4ff)
    return false;
  num = (port - 0x4e0) / 4;
  const unsigned byte = port & 3;
  shift = byte * 8;
  return num != 4 && byte < 3;
}

bool Intel82357::Dma::decodes_legacy_port(u16 port, bool write)
{
  int ctrlr, reg;
  if (!legacy_register(port, ctrlr, reg))
    return page_channel(port, dma_low_page_base) >= 0;
  // The upper bank uses even ports and omits channel 4's address and count.
  if (ctrlr == 1 && ((port & 1) || reg < 2))
    return false;
  return write || reg <= 0x08 || reg == 0x0f;
}

Intel82357::Dma::DecodedPort Intel82357::Dma::decode_port(u16 port, bool write)
{
  if (decodes_legacy_port(port, write))
    return {Register::Legacy};
  if (page_channel(port, dma_high_page_base) >= 0)
    return {Register::HighPage};
  int num = high_count_channel(port);
  if (num >= 0)
    return {Register::HighCount, num};
  unsigned shift;
  if (stop_register(port, num, shift))
    return {Register::Stop, num, shift};
  switch (port) {
  case dma0_chaining_mode_port:
  case dma1_chaining_mode_port:
    return {Register::Chaining};
  case dma_chain_expiration_control_port:
    return {write ? Register::None : Register::Expiration};
  case dma0_extended_mode_port:
  case dma1_extended_mode_port:
    return {write ? Register::ExtendedMode : Register::None};
  default:
    return {Register::None};
  }
}

bool Intel82357::Dma::access_port(u16 port, u8& data, bool write)
{
  const auto decoded = decode_port(port, write);
  switch (decoded.reg) {
  case Register::Legacy:
    if (write)
      write_legacy_port(port, data);
    else
      data = read_legacy_port(port);
    return true;
  case Register::HighPage:
    if (write)
      write_high_page_port(port, data);
    else
      data = read_high_page_port(port);
    return true;
  case Register::HighCount:
    if (write)
      program_count(decoded.channel, 0xffff, (u32)data << 16);
    else
      data = (u8)(channel(decoded.channel).count >> 16);
    return true;
  case Register::Stop: {
    auto& stop = state.stop_address[decoded.channel];
    if (write)
      stop = (stop & ~(0xffU << decoded.shift)) | (((u32)data << decoded.shift) & 0x00fffffcU);
    else
      data = (u8)(stop >> decoded.shift);
    return true;
  }
  case Register::Chaining:
    if (write)
      write_chaining_mode((port == dma1_chaining_mode_port ? 4 : 0) + (data & 3), data);
    else
      data = port == dma0_chaining_mode_port ? pending_chaining_irqs() : chaining_channels(dma_chain_enable);
    return true;
  case Register::Expiration:
    data = chaining_channels(dma_chain_master_tc);
    return true;
  case Register::ExtendedMode:
    write_extended_mode((port == dma1_extended_mode_port ? 4 : 0) + (data & 3), data);
    return true;
  default:
    return false;
  }
}

void Intel82357::Dma::select_compatible_address(int num, bool update_current)
{
  auto& ch = channel(num);
  state.base_extended_address[num] = false;
  ch.pagebase &= 0xff;
  if (update_current) {
    state.extended_address[num] = false;
    ch.current_page &= 0xff;
  }
}

void Intel82357::Dma::program_address(int num, u16 keep, u16 bits)
{
  auto& ch = channel(num);
  const bool update_current = !chaining_enabled(num);
  select_compatible_address(num, update_current);
  ch.base = (ch.base & keep) | bits;
  if (update_current) {
    release_service(num);
    ch.current = (ch.current & keep) | bits;
  }
}

void Intel82357::Dma::program_count(int num, u32 keep, u32 bits)
{
  auto& ch = channel(num);
  // Either legacy count byte clears the 82357 high count byte.
  ch.base_count = (ch.base_count & keep) | bits;
  if (!chaining_enabled(num)) {
    release_service(num);
    ch.count = (ch.count & keep) | bits;
  }
}

void Intel82357::Dma::program_page(int num, u16 keep, u16 bits)
{
  auto& ch = channel(num);
  const bool high_page = keep == 0x00ff;
  const bool update_current = !chaining_enabled(num);
  // A low-page write clears the high page and selects compatible addressing.
  const u16 page_mask = high_page ? keep : 0;
  ch.pagebase = (ch.pagebase & page_mask) | bits;
  state.base_extended_address[num] = high_page;
  if (update_current) {
    release_service(num);
    ch.current_page = (ch.current_page & page_mask) | bits;
    state.extended_address[num] = high_page;
  }
}

void Intel82357::Dma::program_mode(int num, u8 data)
{
  if (num == 4 && (data & 0xc0) != dma_cascade_mode)
    return;
  reset_chaining_mode(num);
  CDMA::program_mode(num, data);
}

void Intel82357::Dma::master_clear_channel(int num)
{
  CDMA::master_clear_channel(num);
  select_compatible_address(num, true);
  reset_chaining_mode(num);
  channel(num).mode = num == 4 ? dma_cascade_mode : 0;
}

void Intel82357::Dma::write_extended_mode(int num, u8 data)
{
  reset_chaining_mode(num);
  if ((state.extended_mode[num] ^ data) & 0x0c)
    release_service(num);
  state.extended_mode[num] = data & 0xfc;
}

bool Intel82357::Dma::chaining_enabled(int num) const
{
  return num != 4 && (state.chaining_mode[num] & dma_chain_enable);
}

void Intel82357::Dma::reset_chaining_mode(int num)
{
  state.chaining_mode[num] = 0;
  state.chaining_pending &= ~(1 << num);
  update_chaining_irq();
}

void Intel82357::Dma::write_chaining_mode(int num, u8 data)
{
  if (num == 4)
    return;
  state.chaining_mode[num] = data & 0x1c;
  if (!(data & dma_chain_enable) || (data & dma_programming_complete))
    state.chaining_pending &= ~(1 << num);
  update_chaining_irq();
}

u8 Intel82357::Dma::chaining_channels(u8 flag) const
{
  u8 channels = 0;
  for (int num = 0; num < 8; ++num)
    if (num != 4 && (state.chaining_mode[num] & flag))
      channels |= 1 << num;
  return channels;
}

u8 Intel82357::Dma::pending_chaining_irqs() const
{
  return state.chaining_pending & ~chaining_channels(dma_chain_master_tc);
}

void Intel82357::Dma::update_chaining_irq(bool force)
{
  const bool asserted = pending_chaining_irqs() != 0;
  if (force || asserted != chaining_irq_asserted) {
    chaining_irq_asserted = asserted;
    isp.set_dma_chaining_irq(asserted);
  }
}

CDMA::SDMA_transfer_properties Intel82357::Dma::transfer_properties(int num) const
{
  validate_device_channel(num);
  // 82357 3.7.9: only the shifted word mode counts words.
  static constexpr SDMA_transfer_properties sizes[] = {
    { 1, 1, 1, 0, 0xffff }, { 2, 2, 2, 1, 0x1ffff },
    { 4, 1, 4, 0, 0xffff }, { 2, 1, 2, 0, 0xffff }
  };
  auto properties = sizes[(state.extended_mode[num] >> 2) & 3];
  if (state.extended_address[num])
    properties.address_mask = 0xffffffffU;
  return properties;
}

bool Intel82357::Dma::accepts_external_eop(int num) const
{
  return (state.extended_mode[num] & 0x40) != 0;
}

bool Intel82357::Dma::accepts_software_request(int num) const
{
  return (channel(num).mode & 0xc0) == 0x80;
}

size_t Intel82357::Dma::bytes_through_stop(int num, const SDMA_transfer_properties& properties) const
{
  if (!(state.extended_mode[num] & 0x80))
    return 0;
  const auto& ch = channel(num);
  const u32 address = dma_address(ch.current_page, ch.current, properties);
  const u32 compare_mask = 0x00fffffcU;
  const u32 stop = state.stop_address[num] & compare_mask;
  // In compatible address mode, the stop may lie outside the channel's fixed page.
  if ((address ^ stop) & compare_mask & ~properties.address_mask)
    return 0;

  const u32 width = (u32)properties.data_width;
  const u32 cycle = address & ~(width - 1);
  const u32 lanes = width - (address - cycle);
  if ((address & compare_mask) == stop)
    return lanes;

  // Decrementing channels enter the matching dword at its highest bus unit.
  const bool decrement = (ch.mode & 0x20) != 0;
  const u32 first = decrement ? stop + 4 - width : stop;
  const u32 wrap = properties.address_mask & 0x00ffffffU;
  return ((decrement ? cycle - first : first - cycle) & wrap) + lanes;
}

size_t Intel82357::Dma::limit_transfer(int num, size_t length, size_t remaining,
  const SDMA_transfer_properties& properties, SDMA_result& result) const
{
  const size_t stop = bytes_through_stop(num, properties);
  if (stop && length >= stop) {
    result.stop_match = true;
    return stop;
  }
  if (stop && (length == remaining || result.external_eop)) {
    // TC or EOP can finish the matching cycle with only some lanes active.
    result.stop_match = stop - length < properties.data_width;
  }
  return length;
}

void Intel82357::Dma::complete_transfer(int num, SDMA_result& result)
{
  if (!(channel(num).mode & 0x10) && chaining_enabled(num) &&
      (state.chaining_mode[num] & dma_programming_complete)) {
    load_current_from_base(num);
    state.chaining_mode[num] &= ~dma_programming_complete;
    result.chained = true;
    return;
  }
  CDMA::complete_transfer(num, result);
}

void Intel82357::Dma::load_current_from_base(int num)
{
  CDMA::load_current_from_base(num);
  state.extended_address[num] = state.base_extended_address[num];
}

void Intel82357::Dma::transfer_serviced(int num, SDMA_result& result)
{
  if (result.stop_match)
    mask_channel(num);
  if (!chaining_enabled(num))
    return;
  const u8 bit = 1 << num;
  if ((result.terminal_count || result.external_eop) && !(channel(num).mode & 0x10)) {
    state.chaining_pending |= bit;
    update_chaining_irq();
  }

  // INTOUT drives TC only during service of the channel with the pending request.
  if ((state.chaining_pending & bit) && (state.chaining_mode[num] & dma_chain_master_tc) &&
      !accepts_external_eop(num)) {
    result.chaining_tc = true;
    isp.dma_master_tc(num);
  }
}
