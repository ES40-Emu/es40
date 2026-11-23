/* ES40 emulator -- VL82C106 keyboard, mouse, serial, parallel and real-time clock controller
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
#include "VL82C106.h"
#include "io/Serial.h"
#include "system/System.h"

namespace {
enum { Unselected = -1, Data, CommandStatus, RtcIndex, RtcData, Control0, ComA, ComB, Lpt };
// P17 is tied high and P16 senses the unlocked front-panel switch (PB22H-KB, p. 1-11).
constexpr u8 input_port = 0xc0;

bool selects_programmed(const u8 (&cs)[3], u8 written, u16 port) noexcept
{
  // Battery-backed bases and masks have no specified initial values (PB22H-KB, p. 24-7).
  if (written != 7)
    return false;
  // Each set range bit masks its matching address bit (PB22H-KB, Tables 24-1 to 24-3).
  const u16 base = cs[0] | (u16(cs[1]) << 8);
  return ((port ^ base) & ~(cs[2] & 0x1f)) == 0;
}
}

VL82C106::VL82C106(CConfigurator* cfg, CSystem* system, u64 base,
  std::function<void(bool)> irq_output, std::function<void(unsigned, bool)> serial_irq_output,
  std::function<void(bool)> rtc_irq_output, std::function<void(bool)> lpt_irq_output)
  : CKeyboard(cfg, system), local_base(base), irq_output(std::move(irq_output)),
    rtc_irq_output(std::move(rtc_irq_output)), lpt_irq_output(std::move(lpt_irq_output))
{
  for (unsigned i = 0; i < 2; ++i)
    serial[i] = new CSerial(cfg, system, i, [serial_irq_output, i](bool level) { serial_irq_output(i, level); });
  save_item(rtc_index, "rtc_index");
  save_item(control0, "control0");
  save_item(control1, "control1");
  save_item(serial_cs, "serial_cs");
  save_item(serial_cs_written, "serial_cs_written");
  save_item(at_inhibit_override, "at_inhibit_override");
  save_item(ram, "ram");
  save_item(output_latch, "output_latch");
  save_item(pulse_mask, "pulse_mask");
  save_item(pulse_remaining_ns, "pulse_remaining_ns");
  save_item(password, "password");
  save_item(password_length, "password_length");
  save_item(recent_keys, "recent_keys");
  save_item(recent_count, "recent_count");
  save_item(locked, "locked");
}

VL82C106::~VL82C106()
{
  stop_threads();
  try {
    flush_storage();
  } catch (const CException& error) {
    printf("%%RTC-F-NOSAVE: %s\n", error.displayText().c_str());
  }
}

void VL82C106::init()
{
  load_battery();
  CKeyboard::init();
  rtc_reset();
}

void VL82C106::ResetPCI()
{
  reset_controller();
  rtc_reset();
  rtc_wakeup.notify_one();
}

void VL82C106::map_ports()
{
  // Local port n uses CA[24:9]; only the low data byte is wired (PB22H-KB, Table 8-3).
  cSystem->RegisterMemory(this, 0, local_base, U64(0x10000) << 9);
}

bool VL82C106::selects_serial(unsigned channel, u16 port) const noexcept
{
  // COMA needs SYS BD EN and COM1 EN, while COMB needs COM2 EN (PB22H-KB, Tables 24-7 and 24-9).
  if (channel == 0 ? (control0 & 5) != 5 : !(control1 & 1))
    return false;
  if (control1 & 8)
    return selects_programmed(serial_cs[channel], serial_cs_written[channel], port);
  // COM1 DEF exchanges the two hardwired bases (PB22H-KB, Table 24-5).
  const bool at_3f8 = (channel == 0) == ((control0 & 8) != 0);
  return (port & 0xfff8) == (at_3f8 ? 0x3f8 : 0x2f8);
}

bool VL82C106::selects_lpt(u16 port) const noexcept
{
  // SYS BD EN and LPT EN gate CS3 in both address modes (PB22H-KB, Table 24-7).
  if ((control0 & 0x11) != 0x11)
    return false;
  if (control1 & 8)
    return selects_programmed(lpt_cs, lpt_cs_written, port);
  // LPT DEF[1:0] selects the fixed window (PB22H-KB, Table 24-5 and VL82C106, p. 6-207).
  constexpr u16 bases[] = {0x3bc, 0x378, 0x278};
  const unsigned index = (control0 >> 5) & 3;
  return index < 3 && (port & 0xfffc) == bases[index];
}

void VL82C106::set_parallel_inputs(u8 data, u8 status_pins)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  // ACK_L returning high latches an enabled request (PB22H-KB, Table 21-2).
  if ((lpt_control & 0x10) && !(lpt_status_pins & 0x40) && (status_pins & 0x40))
    lpt_irq_pending = true;
  lpt_status_pins = status_pins & 0xf8;
  lpt_input_data = data;
  sync_lpt_irq();
}

void VL82C106::sync_lpt_irq()
{
  // AT passes ACK_L through and PS/2 holds IRQP until a status read (VL82C106, p. 6-192).
  const bool level = (lpt_control & 0x10) && (at_mode() ? (lpt_status_pins & 0x40) != 0 : lpt_irq_pending);
  if (!lpt_irq_level || *lpt_irq_level != level) {
    lpt_irq_level = level;
    lpt_irq_output(level);
  }
}

VL82C106::ParallelOutputs VL82C106::parallel_outputs() const
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  ParallelOutputs pins{std::nullopt, u8((lpt_control ^ 0x0b) & 0x0f)};
  if ((control0 & 0x80) || !(lpt_control & 0x20))
    pins.data = lpt_data;
  return pins;
}

u8 VL82C106::read_lpt_status()
{
  // BUSY is inverted and extended IRQ status is active low, cleared on read (PB22H-KB, pp. 21-2, 21-4–5).
  const u8 irq = (control0 & 0x80) || !lpt_irq_pending ? 4 : 0;
  const u8 status = (lpt_status_pins ^ 0x80) | irq | 3;
  lpt_irq_pending = false;
  sync_lpt_irq();
  return status;
}

int VL82C106::decode_port(u16 port, bool write) const noexcept
{
  int selected = Unselected;
  switch (port) {
  case 0x60: selected = Data; break;
  case 0x64: selected = CommandStatus; break;
  case 0x102:
    // PS/2 mode exposes control register 0 at port 102h (VL82C106, p. 6-192).
    if (write && !at_mode())
      selected = Control0;
    break;
  case 0x170:
    if (write)
      selected = RtcIndex;
    break;
  case 0x171:
    // This port selects the clock, standby RAM and chip controls (PB22H-KB, Tables 22-1 and 24-6).
    if (rtc_index < sizeof(rtc_bytes) || (rtc_index >= 0x69 && rtc_index <= 0x73))
      selected = RtcData;
    break;
  }
  for (unsigned channel = 0; channel < 2; ++channel) {
    if (selects_serial(channel, port)) {
      if (selected != Unselected)
        return Unselected;
      selected = ComA + channel;
    }
  }
  if (selects_lpt(port)) {
    if (selected != Unselected || (port & 3) == 3)
      return Unselected;
    return Lpt;
  }
  return selected;
}

bool VL82C106::decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept
{
  return index == 0 && !(address & 0x1ff) && (dsize == 32 || dsize == 64) &&
    decode_port(u16(address >> 9), write) != Unselected;
}

u64 VL82C106::ReadMem(int, u64 address, int dsize)
{
  const u16 port = u16(address >> 9);
  const int selected = decode_port(port, false);
  if (selected == RtcData) {
    // SRM 2.2 reads both live controls during LPT setup (PB22H-KB, Tables 24-6 to 24-9).
    if (rtc_index == 0x69) return control0;
    if (rtc_index == 0x6a) return control1;
    if (rtc_index >= 0x6b) return 0; // W fields read as zero (PB22H-KB, Tables 1 and 22-1).
    return rtc_read(rtc_index);
  }
  if (selected == Lpt) {
    switch (port & 3) {
    case 0: return parallel_outputs().data.value_or(lpt_input_data);
    case 1: return read_lpt_status();
    case 2: return lpt_control | 0xe0; // DIR is write-only and reads high (VL82C106, p. 6-192).
    }
  }
  if (selected == ComA || selected == ComB)
    return serial_port(selected - ComA).ReadMem(0, port & 7, 8);
  return CKeyboard::ReadMem(selected, 0, dsize);
}

void VL82C106::WriteMem(int, u64 address, int dsize, u64 data)
{
  const u16 port = u16(address >> 9);
  const int selected = decode_port(port, true);
  if (selected == ComA || selected == ComB) {
    serial_port(selected - ComA).WriteMem(0, port & 7, 8, u8(data));
  } else if (selected == Lpt) {
    if ((port & 3) == 0)
      lpt_data = u8(data); // The output latch is writable in either direction (PB22H-KB, p. 21-3).
    else if ((port & 3) == 2) {
      lpt_control = u8(data) & 0x3f;
      if (!(lpt_control & 0x10))
        lpt_irq_pending = false;
      sync_lpt_irq();
    }
  } else if (selected == RtcIndex) {
    rtc_index = data & 0x7f;
  } else if (selected == RtcData && rtc_index < sizeof(rtc_bytes)) {
    rtc_write(rtc_index, u8(data));
  } else if (selected == Control0 || (selected == RtcData && rtc_index == 0x69)) {
    control0 = u8(data);
  } else if (selected == RtcData && rtc_index == 0x6a) {
    CFastMutex::ScopedLock guard(kbdLock);
    control1 = u8(data);
    kbd_update_irq();
    sync_lpt_irq();
  } else if (selected == RtcData) {
    const unsigned reg = rtc_index - 0x6b;
    u8* cs = reg < 6 ? serial_cs[reg / 3] : lpt_cs;
    u8& written = reg < 6 ? serial_cs_written[reg / 3] : lpt_cs_written;
    if (cs[reg % 3] != u8(data) || !(written & (1u << (reg % 3))))
      battery_dirty = true;
    cs[reg % 3] = u8(data);
    written |= u8(1u << (reg % 3));
  } else {
    CKeyboard::WriteMem(selected, 0, dsize, data);
  }
}

void VL82C106::reset_controller_state()
{
  rtc_index = 0;
  control0 = 0x9d; // Reset enables COMA at 3F8h and LPT at 3BCh (PB22H-KB, Tables 24-5 and 24-7).
  control1 = 0x07; // COMB uses hardwired selects and AT mode lasts until bit 1 clears (PB22H-KB, Table 24-9).
  lpt_data = lpt_control = 0;
  lpt_irq_pending = false;
  lpt_irq_level.reset();
  sync_lpt_irq();
  at_inhibit_override = 0;
  memset(ram, 0, sizeof(ram));
  output_latch = 1;
  pulse_mask = 0;
  pulse_end = {};
  memset(password, 0, sizeof(password));
  memset(recent_keys, 0, sizeof(recent_keys));
  password_length = recent_count = 0;
  locked = false;
  state.status.c_d = false;
  state.allow_irq1 = state.allow_irq12 = false;
  state.scancodes_translate = false;
  irq_level.reset();
  kbd_update_irq();
}

void VL82C106::set_irq_lines(bool keyboard, bool mouse)
{
  const bool asserted = keyboard || (mouse && !at_mode());
  if (!irq_level || *irq_level != asserted) {
    irq_output(asserted);
    irq_level = asserted;
  }
}

u8 VL82C106::status_byte(u8 normal) const
{
  // C/D clears on completion; a parameter write completes a multi-byte command (PB22H-KB, Table 23-4).
  if (!state.expecting_port60h)
    normal &= ~0x08;
  if (at_mode())
    return normal & ~0x20; // AT status bit 5 reports transmit timeout (VL82C106, Fig. 3).
  // C1/C2 substitute the selected P1 nibble until the next command (PB22H-KB, p. 23-13).
  if (state.last_comm == 0xc1)
    return (normal & 0x0f) | ((input_port & 0x0f) << 4);
  if (state.last_comm == 0xc2)
    return (normal & 0x0f) | (input_port & 0xf0);
  return normal;
}

u8 VL82C106::output_port() const
{
  // P2 reports serial lines, interrupt outputs, RC_L and the front LED (PB22H-KB, Table 23-8).
  const bool keyboard_irq = state.status.outb && !state.status.auxb && state.allow_irq1;
  const bool mouse_irq = state.status.auxb && state.allow_irq12 && !at_mode();
  const u8 pulses = cSystem->device_access_time() < pulse_end ? pulse_mask : 0;
  return (output_latch & ~pulses) | 0x84 | (state.aux_clock_enabled << 3) |
    (keyboard_irq << 4) | (mouse_irq << 5) | (state.kbd_clock_enabled << 6);
}

bool VL82C106::handle_controller_command(u8 command)
{
  // Bytes 1-31 are indexed by the low five command bits (PB22H-KB, p. 23-10).
  if (command > 0x20 && command <= 0x3f) {
    controller_enQ(ram[(command & 31) - 1], 0);
    return true;
  }
  if (command > 0x60 && command <= 0x7f) {
    state.expecting_port60h = 1;
    return true;
  }
  if (command >= 0xf0) {
    // P20/P21 pulse low for approximately 6 us (PB22H-KB, p. 23-14).
    if (command != 0xff) {
      pulse_mask = ~command & 3;
      pulse_end = cSystem->device_access_time() + std::chrono::microseconds(6);
    }
    return true;
  }
  if (at_mode()) {
    if (command == 0x20) {
      controller_enQ((mode_byte() & 0x75) | at_inhibit_override, 0);
      return true;
    }
    // The mouse, password, P1 poll and buffer injection commands are PS/2 additions (VL82C106, p. 6-203).
    if ((command >= 0xa4 && command <= 0xa9) || command == 0xc1 || command == 0xc2 ||
        (command >= 0xd2 && command <= 0xd4))
      return true;
  }
  switch (command) {
  case 0xa4: // Test, load and enable password (PB22H-KB, pp. 23-10 and 23-11).
    controller_enQ(password_length ? 0xfa : 0xf1, 0);
    return true;
  case 0xa5:
    memset(password, 0, sizeof(password));
    password_length = 0;
    state.expecting_port60h = 1;
    return true;
  case 0xa6:
    if (password_length) {
      recent_count = 0;
      discard_device_input();
      locked = true;
    }
    return true;
  case 0xa9: // The mouse interface test uses MIRQ (PB22H-KB, Table 23-6).
    controller_enQ(0, 1);
    return true;
  case 0xaa:
    controller_enQ(0x55, 0); // Self-test supplies a byte; SYS mirrors the mode register (pp. 23-5, 23-12).
    return true;
  case 0xac: // Diagnostic dump is unused on PS/2 (PB22H-KB, p. 23-12).
  case 0xc1:
  case 0xc2:
    return true;
  case 0xc0: // P1 overwrites the current output byte (PB22H-KB, p. 23-12).
    state.status.outb = false;
    state.irq1_requested = state.irq12_requested = false;
    controller_enQ(input_port, 0);
    return true;
  case 0xd0:
    controller_enQ(output_port(), 0);
    return true;
  case 0xe0: // T0 and T1 sample the keyboard and mouse clocks (PB22H-KB, Table 23-9).
    controller_enQ(u8(state.kbd_clock_enabled) | (state.aux_clock_enabled << 1), 0);
    return true;
  default:
    return false;
  }
}

bool VL82C106::handle_controller_parameter(u8 command, u8 data)
{
  if (command == 0)
    return !state.kbd_clock_enabled; // AD inhibits keyboard transmission (PB22H-KB, p. 23-12).
  if (at_mode() && command == 0x60) {
    at_inhibit_override = data & 8;
    set_mode_byte(data & 0x75); // Bit 1 is reserved in AT mode (VL82C106, Fig. 1).
    return true;
  }
  if (command > 0x60 && command <= 0x7f) {
    ram[(command & 31) - 1] = data;
    return true;
  }
  if (command == 0xa5) {
    if (data && password_length < sizeof(password))
      password[password_length++] = data;
    state.expecting_port60h = data && password_length < sizeof(password);
    return true;
  }
  if (command == 0xd1) {
    output_latch = data & 3; // D1 changes only P20/P21 (PB22H-KB, p. 23-13).
    return true;
  }
  return false;
}

void VL82C106::password_scancodes(const u8* bytes, bool released)
{
  // Match untranslated make codes and discard device input until the sequence matches (p. 23-11).
  if (released)
    return;
  for (; *bytes; ++bytes) {
    if (recent_count == password_length) {
      --recent_count;
      memmove(recent_keys, recent_keys + 1, recent_count);
    }
    recent_keys[recent_count++] = *bytes;
    if (recent_count == password_length && !memcmp(recent_keys, password, password_length)) {
      recent_count = 0;
      locked = false;
      return;
    }
  }
}

int VL82C106::SaveState(FILE* f)
{
  CFastMutex::ScopedLock guard(kbdLock);
  const auto remaining = pulse_end - cSystem->device_access_time();
  pulse_remaining_ns = remaining > decltype(remaining)::zero() ?
    std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() : 0;
  rtc_refresh(cSystem->device_access_time());
  const u8 port[]{lpt_data, lpt_control, lpt_input_data, lpt_status_pins, u8(lpt_irq_pending)};
  if (fwrite("VRT2", 4, 1, f) != 1 || CKeyboard::SaveState(f) || CSystemComponent::SaveState(f))
    return -1;
  return save_rtc(f) && save_lpt_cs(f) && fwrite(port, sizeof(port), 1, f) == 1 ? 0 : -1;
}

int VL82C106::RestoreState(FILE* f)
{
  battery_ready = false;
  const int first = fgetc(f);
  if (first == EOF || ungetc(first, f) == EOF)
    return -1;
  const bool legacy = first != 'V';
  char tag[4]{};
  const bool has_lpt = !legacy && fread(tag, sizeof(tag), 1, f) == 1 && !memcmp(tag, "VRT2", sizeof(tag));
  if (!legacy && !has_lpt && memcmp(tag, "VRT1", sizeof(tag)))
    return -1;
  if (CKeyboard::RestoreState(f) || CSystemComponent::RestoreState(f))
    return -1;
  if (rtc_index > 0x7f || serial_cs_written[0] > 7 || serial_cs_written[1] > 7 ||
      password_length > sizeof(password) || (locked && !password_length) ||
      recent_count > sizeof(recent_keys) || (locked && recent_count > password_length) ||
      pulse_remaining_ns > 6000)
    return -1;
  pulse_end = cSystem->device_access_time() + std::chrono::nanoseconds(pulse_remaining_ns);
  // Older VL82C106 snapshots contain the index and chip selects but no clock (PB22H-KB, Table 22-1).
  if (legacy)
    seed_clock();
  else if (!restore_rtc(f))
    return -1;
  u8 port[]{0, 0, 0xff, 0xf8, 0};
  if (has_lpt) {
    if (!restore_lpt_cs(f) || fread(port, sizeof(port), 1, f) != 1 ||
        (port[1] & 0xc0) || (port[3] & 7) || port[4] > 1)
      return -1;
  } else {
    memset(lpt_cs, 0, sizeof(lpt_cs));
    lpt_cs_written = 0;
  }
  lpt_data = port[0];
  lpt_control = port[1];
  lpt_input_data = port[2];
  lpt_status_pins = port[3];
  lpt_irq_pending = port[4] != 0;
  rtc_sample = {};
  battery_dirty = true;
  return 0;
}

void VL82C106::finalize_restore() noexcept
{
  CFastMutex::ScopedLock guard(kbdLock);
  irq_level.reset();
  kbd_update_irq();
  rtc_irq_level.reset();
  rtc_sync_irq();
  lpt_irq_level.reset();
  sync_lpt_irq();
  battery_ready = true;
}
