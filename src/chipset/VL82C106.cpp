/* ES40 emulator -- VL82C106 keyboard and mouse controller
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
#include "system/System.h"

namespace {
enum { Data, CommandStatus, RtcIndex, RtcData };
// P17 is tied high and P16 senses the unlocked front-panel switch (PB22H-KB, p. 1-11).
constexpr u8 input_port = 0xc0;
}

VL82C106::VL82C106(CConfigurator* cfg, CSystem* system, u64 base,
  std::function<void(bool)> irq_output)
  : CKeyboard(cfg, system), local_base(base), irq_output(std::move(irq_output))
{
  save_item(rtc_index, "rtc_index");
  save_item(at_mode, "at_mode");
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

void VL82C106::map_ports()
{
  // Local port n uses CA[24:9]; only the low data byte is wired (PB22H-KB, Table 8-3).
  const u16 ports[] = { 0x60, 0x64, 0x170, 0x171 };
  for (unsigned i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i)
    cSystem->RegisterMemory(this, i, local_base + (u64(ports[i]) << 9), 1);
}

bool VL82C106::decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept
{
  if (address || (dsize != 32 && dsize != 64))
    return false;
  // Control register 1 is write-only through the RTC index (PB22H-KB, Tables 22-1 and 24-9).
  return index == Data || index == CommandStatus ||
    (write && (index == RtcIndex || (index == RtcData && rtc_index == 0x6a)));
}

void VL82C106::WriteMem(int index, u64 address, int dsize, u64 data)
{
  if (index == RtcIndex) {
    rtc_index = data & 0x7f;
  } else if (index == RtcData) {
    CFastMutex::ScopedLock guard(kbdLock);
    at_mode = (data & 2) != 0;
    kbd_update_irq();
  } else {
    CKeyboard::WriteMem(index, address, dsize, data);
  }
}

void VL82C106::reset_controller_state()
{
  rtc_index = 0;
  at_mode = true; // Power-on selects AT until firmware clears bit 1 (PB22H-KB, Table 24-9).
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
  const bool asserted = keyboard || (mouse && !at_mode);
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
  if (at_mode)
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
  const bool mouse_irq = state.status.auxb && state.allow_irq12 && !at_mode;
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
  if (at_mode) {
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
  if (at_mode && command == 0x60) {
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
  if (CKeyboard::SaveState(f))
    return -1;
  return CSystemComponent::SaveState(f);
}

int VL82C106::RestoreState(FILE* f)
{
  if (CKeyboard::RestoreState(f) || CSystemComponent::RestoreState(f))
    return -1;
  if (password_length > sizeof(password) || (locked && !password_length) ||
      recent_count > sizeof(recent_keys) || (locked && recent_count > password_length) ||
      pulse_remaining_ns > 6000)
    return -1;
  pulse_end = cSystem->device_access_time() + std::chrono::nanoseconds(pulse_remaining_ns);
  return 0;
}

void VL82C106::finalize_restore() noexcept
{
  CFastMutex::ScopedLock guard(kbdLock);
  irq_level.reset();
  kbd_update_irq();
}
