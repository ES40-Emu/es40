/* ES40 emulator -- DEC 3000 Model 300X IOCTL flash window and SCC interface
 * Copyright (C) 2026 by gdwnldsKSC
 * All rights reserved.
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

#include "StdAfx.h"
#include "CPU/AlphaCPU.h"
#include "system/PelicanIOCTL.h"
#include "system/System.h"
#include <fstream>
#include <utility>

enum PelicanIOCTLRange { DenseROM, SparseROM, DenseCSR, SparseCSR, DenseSCC0, SparseSCC0, DenseSCC1, SparseSCC1 };
static constexpr u32 scc_reset_n = 1U << 11; // SSR<11> releases both SCCs from reset (EK-D3SYS-PM, §7.3.11).
static constexpr u32 support_mask = 0x0f0002bf | scc_reset_n; // ROM controls, read mask, SCC reset and system-OK LED (§7.3.11).
static constexpr u32 scc0_interrupt = 1U << 6; // SIR<6> and SIR<7> are the SCC0 and SCC1 interrupt pins (EK-D3SYS-PM, §7.3.12).
static constexpr u32 scc1_interrupt = 1U << 7;

static bool is_sparse(int index)
{
  return index == SparseROM || index == SparseCSR || index == SparseSCC0 || index == SparseSCC1;
}

static bool is_scc(int index)
{
  return index == DenseSCC0 || index == SparseSCC0 || index == DenseSCC1 || index == SparseSCC1;
}

static unsigned scc_number(int index)
{
  return (index == DenseSCC1 || index == SparseSCC1) ? 1 : 0;
}

PelicanIOCTL::PelicanIOCTL(CConfigurator* cfg, CSystem* system) : CSystemComponent(cfg, system)
{
  unsigned fitted = 0;
  for (size_t bank = 0; bank < chips.size(); ++bank) {
    const std::string key = "rom.flash" + std::to_string(bank);
    const char* path = cfg->get_text_value(key.c_str(), "");
    if (!*path)
      continue;
    if (++fitted > 3)
      FAILURE(Configuration, "The 300X has three 256 KiB flash devices");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
      FAILURE(Configuration, std::string("Cannot open 300X flash image: ") + path);
    if (file.tellg() != std::streamoff(Intel28F020::capacity))
      FAILURE(Configuration, std::string("300X flash image must be a raw 256 KiB device: ") + path);
    std::vector<u8> image(Intel28F020::capacity);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(image.data()), Intel28F020::capacity))
      FAILURE(IO, std::string("Cannot read 300X flash image: ") + path);
    chips[bank].emplace(std::move(image));
  }

  // Slot 5 maps each dense longword to an even sparse longword (EK-D3SYS-PM, Table 16 and App. A.1).
  system->RegisterMemory(this, DenseROM, U64(0x1a0000000), Intel28F020::capacity);
  system->RegisterMemory(this, SparseROM, U64(0x1b0000000), Intel28F020::capacity * 2);
  system->RegisterMemory(this, DenseCSR, U64(0x1a0040100), 0x24);
  system->RegisterMemory(this, SparseCSR, U64(0x1b0080200), 0x48);
  system->RegisterMemory(this, DenseSCC0, U64(0x1a0100000), 16);
  system->RegisterMemory(this, SparseSCC0, U64(0x1b0200000), 32);
  system->RegisterMemory(this, DenseSCC1, U64(0x1a0180000), 16);
  system->RegisterMemory(this, SparseSCC1, U64(0x1b0300000), 32);
  scc[0].set_transmit([this](Z85C30::Channel channel, u8 value) {
    if (channel == Z85C30::Channel::A)
      mouse.command(value);
  });
  scc[1].set_transmit([this](Z85C30::Channel channel, u8 value) {
    if (channel == Z85C30::Channel::A)
      keyboard.command(value);
  });
  save_item(support, "ssr");
  save_item(interrupt_mask, "simr");
  mouse.reset();
  mouse.rebase_clock(serial_clock_ns);
  keyboard.reset();
  keyboard.rebase_clock(serial_clock_ns);
  auto& pointer = mouse.state();
  save_item(pointer.stream_wait_ns, "mouse.stream_wait_ns");
  save_item(pointer.wire_wait_ns, "mouse.wire_wait_ns");
  save_item(pointer.dx, "mouse.dx");
  save_item(pointer.dy, "mouse.dy");
  save_item(pointer.buttons, "mouse.buttons");
  save_item(pointer.buttons_changed, "mouse.buttons_changed");
  save_item(pointer.streaming, "mouse.streaming");
  save_item(pointer.testing, "mouse.testing");
  save_item(pointer.report, "mouse.report");
  save_item(pointer.position, "mouse.position");
  save_item(pointer.length, "mouse.length");
  register_scc_state(0);
  register_scc_state(1);
  register_keyboard_state();
}

void PelicanIOCTL::register_keyboard_state()
{
  auto& k = keyboard.state();
  save_item(k.mode, "lk401.mode");
  save_item(k.buffer, "lk401.buffer");
  save_item(k.delay, "lk401.delay");
  save_item(k.rate, "lk401.rate");
  save_item(k.held, "lk401.held");
  save_item(k.held_code, "lk401.held_code");
  save_item(k.held_count, "lk401.held_count");
  save_item(k.deferred, "lk401.deferred");
  save_item(k.deferred_count, "lk401.deferred_count");
  save_item(k.fifo, "lk401.fifo");
  save_item(k.count, "lk401.count");
  save_item(k.locked_ack, "lk401.locked_ack");
  save_item(k.locked, "lk401.locked");
  save_item(k.overflow, "lk401.overflow");
  save_item(k.repeat_key, "lk401.repeat_key");
  save_item(k.repeat_enabled, "lk401.repeat_enabled");
  save_item(k.repeat_inhibit, "lk401.repeat_inhibit");
  save_item(k.last_code, "lk401.last_code");
  save_item(k.pending, "lk401.pending");
  save_item(k.pending_count, "lk401.pending_count");
  save_item(k.extended, "lk401.extended");
  save_item(k.testing, "lk401.testing");
  save_item(k.stuck, "lk401.stuck");
  save_item(k.leds, "lk401.leds");
  save_item(k.click, "lk401.click");
  save_item(k.ctrl_click, "lk401.ctrl_click");
  save_item(k.bell, "lk401.bell");
  save_item(k.repeat_wait_ns, "lk401.repeat_wait_ns");
  save_item(k.parameter_wait_ns, "lk401.parameter_wait_ns");
  save_item(k.wire_wait_ns, "lk401.wire_wait_ns");
}

void PelicanIOCTL::register_scc_state(unsigned number)
{
  auto& chip = scc[number].state();
  save_item(chip.pointer, "scc.pointer", number);
  save_item(chip.vector, "scc.vector", number);
  save_item(chip.wr9, "scc.wr9", number);
  save_item(chip.in_service, "scc.in_service", number);
  for (int channel = 0; channel < 2; ++channel) {
    const int i = 2 * number + channel;
    auto& p = chip.channels[channel];
    save_item(p.wr, "scc.wr", i);
    save_item(p.wr7_prime, "scc.wr7_prime", i);
    save_item(p.rx, "scc.rx", i);
    save_item(p.count, "scc.count", i);
    save_item(p.rx_errors, "scc.rx_errors", i);
    save_item(p.overrun, "scc.overrun", i);
    save_item(p.rx_locked, "scc.rx_locked", i);
    save_item(p.last_rx, "scc.last_rx", i);
    save_item(p.tx, "scc.tx", i);
    save_item(p.tx_full, "scc.tx_full", i);
    save_item(p.tx_pending, "scc.tx_pending", i);
    save_item(p.rx_first, "scc.rx_first", i);
    save_item(p.underrun, "scc.underrun", i);
  }
}

PelicanIOCTL::~PelicanIOCTL()
{
  stop_threads();
}

std::string PelicanIOCTL::snapshot_identity() const
{
  std::string id = CSystemComponent::snapshot_identity() + ":LK401";
  for (const auto& chip : chips) {
    id += ':';
    if (!chip)
      continue;
    u64 hash = U64(14695981039346656037);
    for (u8 byte : chip->bytes())
      hash = (hash ^ byte) * U64(1099511628211);
    id += std::to_string(hash);
  }
  return id;
}

static u32 flash_longword(const u8* p)
{
  return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

void PelicanIOCTL::load_firmware(char* system_ram, char* io_ram) const
{
  constexpr size_t header = 0x40;
  auto body_length = [&](unsigned bank, const char* module, const char* object, u32 edit) {
    const u8* image = chips[bank] ? chips[bank]->bytes().data() : nullptr;
    // The V7 payload follows the packed TURBOchannel and ROM object headers (EK-D3SYS-PM, §13.3).
    if (!image || flash_longword(&image[4]) != 0xffaa0055 ||
        memcmp(&image[8], "V7.0    DEC     ", 16) || memcmp(&image[0x18], module, 8) ||
        memcmp(&image[0x20], "ALPH", 4) || (image[0x29] & 0xf) != 1 || image[0x2b] == 0 ||
        memcmp(&image[0x30], object, 12) || flash_longword(&image[0x3c]) != edit)
      FAILURE_2(Configuration, "Pelican direct SRM requires V7.0 %s for EV4 pass 3 in rom.flash%u", module, bank);
    const size_t length = flash_longword(&image[0x2c]);
    if (length == 0 || (length & 3) || length > Intel28F020::capacity - header - 1)
      FAILURE_1(Configuration, "Invalid Pelican ROM object length in rom.flash%u", bank);
    // DEC's M300 V7.0 updater stores the rotate-and-add checksum in the last chip byte (EK-D3SYS-PM, §13.3).
    u8 checksum = 0;
    for (size_t i = 0; i < header + length; ++i)
      checksum = static_cast<u8>(((checksum << 1) | (checksum >> 7)) + image[i]);
    if (checksum != image[Intel28F020::capacity - 1])
      FAILURE_1(Configuration, "Invalid Pelican firmware checksum in rom.flash%u", bank);
    return length;
  };

  const size_t sys_length = body_length(2, "PELI_SYS", "SYSTEM_ROM  ", 0x889);
  const size_t io_length = body_length(0, "PELI_IO ", "IO_ROM      ", 0x21f);
  // The SROM clears low memory before loading the two flash objects (EK-D3SYS-PM, §14.1, Table 46).
  memset(system_ram + sys_length, 0, Intel28F020::capacity - sys_length);
  memset(io_ram + io_length, 0, Intel28F020::capacity - io_length);
  memcpy(system_ram, chips[2]->bytes().data() + header, sys_length);
  memcpy(io_ram, chips[0]->bytes().data() + header, io_length);
}

unsigned PelicanIOCTL::selected_bank() const noexcept
{
  return ((support >> 8) & 2) | ((support >> 7) & 1);
}

bool PelicanIOCTL::decodes_memory_access(int index, u64 address, int dsize, bool) const noexcept
{
  if (dsize != 32 && dsize != 64)
    return false;
  const bool sparse = is_sparse(index);
  if (address & ((sparse || dsize == 64) ? 7 : 3))
    return false;
  if (index == DenseCSR || index == SparseCSR) {
    const u64 offset = sparse ? address >> 1 : address;
    return offset == 0 || offset == 0x10 || offset == 0x20;
  }
  if (is_scc(index))
    return (sparse ? address >> 1 : address) < 16;
  return chips[selected_bank()].has_value();
}

u32 PelicanIOCTL::read_word(u32 offset) const
{
  const auto& chip = *chips[selected_bank()];
  // SSR<26> selects one ROM byte at SSR<25:24>, otherwise IOCTL packs four bytes (EK-D3SYS-PM, §7.2).
  if (support & (1U << 26))
    return chip.read(offset + ((support >> 24) & 3));
  u32 word = 0;
  for (unsigned lane = 0; lane < 4; ++lane)
    word |= u32(chip.read(offset + lane)) << (lane * 8);
  return word;
}

static u32 enabled_bytes(u32 enables)
{
  u32 result = 0;
  for (unsigned lane = 0; lane < 4; ++lane)
    if (enables & (1U << lane))
      result |= 0xffU << (lane * 8);
  return result;
}

u32 PelicanIOCTL::read_register(int index, u32 offset, u32 lanes)
{
  if (index == DenseROM || index == SparseROM)
    return read_word(offset);
  if (index == DenseCSR || index == SparseCSR) {
    switch (offset) {
    case 0:    return support;
    case 0x10: return serial_interrupts();
    case 0x20: return interrupt_mask;
    default:   return 0xffffffffU;
    }
  }
  // Channel B precedes A on both SCCs; both transfer on D<15:8> (EK-D3SYS-PM, Table 23 and §9.5.2.4).
  if (!(lanes & 0xff00))
    return 0;
  const auto channel = (offset & 8) ? Z85C30::Channel::A : Z85C30::Channel::B;
  auto& controller = scc[scc_number(index)];
  return u32((offset & 4) ? controller.read_data(channel) : controller.read_control(channel)) << 8;
}

u64 PelicanIOCTL::ReadMem(int index, u64 address, int dsize)
{
  const bool sparse = is_sparse(index);
  const u32 offset = static_cast<u32>(sparse ? address >> 1 : address);
  // Set bits suppress their sparse read lanes (EK-D3SYS-PM, §7.3.11).
  const u32 lanes = sparse && (support & 0x10) ? ~enabled_bytes(support) : 0xffffffffU;
  const bool serial = is_scc(index);
  if (serial)
    refresh_serial();
  const u32 low = read_register(index, offset, lanes) & lanes;
  u64 value = low;
  if (dsize == 64) {
    // Dense LDQ reads two longwords; sparse LDQ duplicates one (EK-D3SYS-PM, Tables 75–76).
    const u32 high = sparse ? low : read_register(index, offset + 4, lanes);
    value |= u64(high) << 32;
  }
  if (serial)
    update_irq();
  return value;
}

void PelicanIOCTL::write_register(int index, u32 offset, u32 value, u32 mask)
{
  if (index == DenseCSR || index == SparseCSR) {
    if (offset == 0) {
      mask &= support_mask;
      const u32 previous = support;
      support = (support & ~mask) | (value & mask);
      if ((previous & ~support) & scc_reset_n)
        for (auto& controller : scc)
          controller.reset();
    } else if (offset == 0x20) {
      interrupt_mask = (interrupt_mask & ~mask) | (value & mask);
    }
    return;
  }
  if (!(support & scc_reset_n) || !(mask & 0xff00))
    return;
  const auto channel = (offset & 8) ? Z85C30::Channel::A : Z85C30::Channel::B;
  const u8 byte = static_cast<u8>(value >> 8);
  auto& controller = scc[scc_number(index)];
  if (offset & 4)
    controller.write_data(channel, byte);
  else
    controller.write_control(channel, byte);
}

void PelicanIOCTL::WriteMem(int index, u64 address, int dsize, u64 data)
{
  if (index == DenseROM || index == SparseROM)
    return; // W1 in the write-disable position inhibits FEPROM writes (EK-D3SYS-PM, §9.5.2).
  const bool sparse = is_sparse(index);
  const u32 offset = static_cast<u32>(sparse ? address >> 1 : address);
  // Sparse STQ carries byte enables in the high longword (EK-D3SYS-PM, App. A.1).
  const u32 mask = sparse && dsize == 64 ? enabled_bytes(u32(data >> 32)) : 0xffffffffU;
  refresh_serial();
  write_register(index, offset, u32(data), mask);
  if (!sparse && dsize == 64)
    write_register(index, offset + 4, u32(data >> 32), 0xffffffffU);
  update_irq();
}

u32 PelicanIOCTL::serial_interrupts() const
{
  return (scc[0].interrupt() ? scc0_interrupt : 0) | (scc[1].interrupt() ? scc1_interrupt : 0);
}

bool PelicanIOCTL::interrupt() const
{
  // SIMR gates the CPU interrupt without clearing the pending SIR bit (EK-D3SYS-PM, §7.3.13).
  return (interrupt_mask & serial_interrupts()) != 0;
}

void PelicanIOCTL::update_irq()
{
  const bool asserted = interrupt();
  if (asserted != irq_asserted) {
    irq_asserted = asserted;
    if (cSystem->get_cpu_num())
      cSystem->get_cpu(0)->irq_h(3, asserted, 0); // Slot 5 drives IR<4> and CPU IRQ3 (EK-D3SYS-PM, §§3.2.1, 17.5).
  }
}

void PelicanIOCTL::refresh_serial()
{
  if (serial_sample == std::chrono::steady_clock::time_point{})
    return;
  const auto now = cSystem->device_access_time();
  if (now > serial_sample) {
    serial_clock_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(now - serial_sample).count();
    serial_sample = now;
  }
  u8 byte;
  while (mouse.next_byte(byte, serial_clock_ns))
    scc[0].receive(Z85C30::Channel::A, byte);
  while (keyboard.next_byte(byte, serial_clock_ns))
    scc[1].receive(Z85C30::Channel::A, byte);
}

void PelicanIOCTL::run_serial()
{
  std::unique_lock<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  while (serial_sample != std::chrono::steady_clock::time_point{}) {
    refresh_serial();
    update_irq();
    serial_wakeup.wait_for(lock, std::chrono::nanoseconds(LK401::frame_ns));
  }
}

void PelicanIOCTL::start_threads()
{
  std::lock_guard<std::recursive_mutex> guard(cSystem->get_device_bus_mutex());
  if (!serial_thread.joinable()) {
    serial_sample = cSystem->device_access_time();
    serial_thread = std::thread(&PelicanIOCTL::run_serial, this);
  }
}

void PelicanIOCTL::stop_threads()
{
  {
    std::lock_guard<std::recursive_mutex> guard(cSystem->get_device_bus_mutex());
    if (serial_sample != std::chrono::steady_clock::time_point{}) {
      refresh_serial();
      update_irq();
      serial_sample = {};
      serial_wakeup.notify_all();
    }
  }
  if (serial_thread.joinable())
    serial_thread.join();
}

void PelicanIOCTL::ResetPCI()
{
  support = interrupt_mask = 0;
  for (auto& controller : scc)
    controller.reset();
  mouse.reset();
  mouse.rebase_clock(serial_clock_ns);
  keyboard.reset();
  keyboard.rebase_clock(serial_clock_ns);
  update_irq();
}

void PelicanIOCTL::prepare_snapshot()
{
  mouse.prepare_snapshot();
  keyboard.prepare_snapshot();
}

int PelicanIOCTL::RestoreState(FILE* file)
{
  if (CSystemComponent::RestoreState(file))
    return -1;
  return scc[0].valid_state() && scc[1].valid_state() && mouse.valid_state() && keyboard.valid_state() ? 0 : -1;
}

void PelicanIOCTL::finalize_restore() noexcept
{
  mouse.rebase_clock(serial_clock_ns);
  keyboard.rebase_clock(serial_clock_ns);
  irq_asserted = !interrupt();
  update_irq();
}
