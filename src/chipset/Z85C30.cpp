/* ES40 emulator -- Zilog Z85C30 serial communications controller
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

#include "Z85C30.h"
#include <utility>

// WR3 and WR5 encode character lengths as 5, 7, 6, 8 (UM010901, Tables 5-4, 5-5).
static constexpr u8 character_mask[] = {0x1f, 0x7f, 0x3f, 0xff};

void Z85C30::reset()
{
  chip = {};
  reset_port(Channel::A, true);
  reset_port(Channel::B, true);
}

void Z85C30::reset_port(Channel channel, bool hardware)
{
  auto& p = port(channel);
  // Register bits marked X survive a channel reset (UM010901, Table 2-7).
  p.wr[1] &= 0x24;
  p.wr[3] &= 0xfe;
  p.wr[4] |= 0x04;
  p.wr[5] &= 0x61;
  p.wr[10] &= hardware ? 0 : 0x60;
  if (hardware)
    p.wr[11] = 0x08;
  // Reset disables local loopback and auto echo (UM010901, §5.2.17).
  p.wr[14] = 0x20 | (hardware ? 0 : p.wr[14] & 3);
  p.wr[15] = 0xf8;
  p.wr7_prime = 0x20;
  p.count = p.rx_errors = p.overrun = p.rx_locked = p.last_rx = 0;
  p.tx_full = p.tx_pending = 0;
  p.rx_first = p.underrun = 1;
  chip.in_service &= channel == Channel::A ? 0x07 : 0x38;
}

void Z85C30::set_transmit(Transmit callback)
{
  send = std::move(callback);
}

bool Z85C30::can_receive(Channel channel) const
{
  const auto& p = port(channel);
  return (p.wr[3] & 0x21) == 1 && !(p.wr[14] & 0x10) && p.count < sizeof p.rx;
}

bool Z85C30::transmit_dma_request(Channel channel) const
{
  const auto& p = port(channel);
  return (p.wr[14] & 4) && !p.tx_full; // /DTR/REQ requests a byte when the transmitter buffer empties (UM010901, §2.5.2.3).
}

bool Z85C30::receive_dma_request(Channel channel) const
{
  const auto& p = port(channel);
  return (p.wr[1] & 0xe0) == 0xe0 && p.count && !p.rx_locked; // /W/REQ signals an available receive byte (UM010901, §2.5.2.4).
}

bool Z85C30::receive(Channel channel, u8 value)
{
  auto& p = port(channel);
  // Local loopback disconnects RxD; Auto Enables gates Rx on the inactive /DCD input (UM010901, §5.2.4).
  if ((p.wr[14] & 0x10) || (p.wr[3] & 0x21) != 1)
    return false;
  if ((p.wr[14] & 8) && send)
    send(channel, value);
  return accept_rx(p, value);
}

bool Z85C30::accept_rx(ChannelState& p, u8 value)
{
  // In asynchronous mode unused high data bits read as ones (UM010901, §4.2.2).
  value |= static_cast<u8>(~character_mask[p.wr[3] >> 6]);
  if (p.count == sizeof p.rx) {
    p.rx[p.count - 1] = value;
    p.rx_errors |= 1u << (p.count - 1);
    return false;
  }
  p.rx[p.count++] = value;
  return true;
}

void Z85C30::pop_rx(ChannelState& p)
{
  for (unsigned i = 1; i < p.count; ++i)
    p.rx[i - 1] = p.rx[i];
  --p.count;
  p.rx_errors >>= 1;
}

u8 Z85C30::read_data(Channel channel)
{
  auto& p = port(channel);
  if (p.count) {
    p.last_rx = p.rx[0];
    p.rx_first = 0;
    if (p.rx_errors & 1)
      p.overrun = 1;
    const unsigned mode = (p.wr[1] >> 3) & 3;
    // First/special and special-only modes hold the error byte until WR0 Error Reset (UM010901, §5.2.2).
    p.rx_locked = p.overrun && (mode == 1 || mode == 3);
    if (!p.rx_locked)
      pop_rx(p);
  }
  return p.last_rx;
}

void Z85C30::write_data(Channel channel, u8 value)
{
  auto& p = port(channel);
  p.tx = value;
  p.tx_full = 1;
  p.tx_pending = 0;
  transmit(channel);
}

void Z85C30::transmit(Channel channel)
{
  auto& p = port(channel);
  if (!p.tx_full || !(p.wr[5] & 0x08))
    return;
  if ((p.wr[3] & 0x20) && !(p.wr[14] & 0x18))
    return;
  const u8 value = p.tx & character_mask[(p.wr[5] >> 5) & 3];
  p.tx_full = 0;
  p.tx_pending = (p.wr[1] & 2) != 0;
  if ((p.wr[14] & 8) || (p.wr[5] & 0x10))
    return;
  if ((p.wr[14] & 0x10) && (p.wr[3] & 1))
    accept_rx(p, value);
  if (send)
    send(channel, value);
}

u8 Z85C30::pending() const
{
  u8 bits = 0;
  for (const auto& p : chip.channels) {
    const unsigned mode = (p.wr[1] >> 3) & 3;
    const bool special = p.rx_error();
    const bool rx = (p.count && (mode == 2 || (mode == 1 && p.rx_first))) || (mode && special);
    bits = (bits << 3) | (rx ? 4 : 0) | (p.tx_pending ? 2 : 0);
  }
  return bits;
}

u8 Z85C30::active_source() const
{
  const u8 bits = pending();
  for (u8 bit = 0x20; bit; bit >>= 1) {
    if (chip.in_service & bit)
      break;
    if (bits & bit)
      return bit;
  }
  return 0;
}

bool Z85C30::interrupt() const
{
  return (chip.wr9 & 8) && active_source();
}

u8 Z85C30::read_vector(Channel channel)
{
  const u8 src = active_source();
  if ((chip.wr9 & 0x28) == 0x28)
    chip.in_service |= src;
  if (channel == Channel::A)
    return chip.vector;

  u8 code = 3;
  switch (src) {
  case 0x20: code = port(Channel::A).rx_error() ? 7 : 6; break;
  case 0x10: code = 4; break;
  case 0x04: code = port(Channel::B).rx_error() ? 3 : 2; break;
  case 0x02: code = 0; break;
  }
  // RR2B puts status into V1:V3 or V6:V4 (UM010901, Table 5-6).
  if (chip.wr9 & 0x10)
    return (chip.vector & 0x8f) | ((code & 1) << 6) | ((code & 2) << 4) | ((code & 4) << 2);
  return (chip.vector & 0xf1) | (code << 1);
}

u8 Z85C30::read_control(Channel channel)
{
  auto& p = port(channel);
  const u8 reg = std::exchange(chip.pointer, u8{0});
  switch (reg) {
  case 4: if (p.wr7_prime & 0x40) return p.wr[4]; [[fallthrough]];
  case 0: return (p.underrun ? 0x40 : 0) | (p.tx_full ? 0 : 4) | (p.count ? 1 : 0);
  case 5: if (p.wr7_prime & 0x40) return p.wr[5]; [[fallthrough]];
  case 1: return (p.rx_error() ? 0x20 : 0) | 6 | (p.tx_full ? 0 : 1);
  case 2: case 6: return read_vector(channel);
  case 3: case 7: return channel == Channel::A ? pending() : 0;
  case 8: return read_data(channel);
  case 9: return (p.wr7_prime & 0x40) ? p.wr[3] : p.wr[13];
  case 10: return 0;
  case 11: if (p.wr7_prime & 0x40) return p.wr[10]; [[fallthrough]];
  case 15: return p.wr[15] & 0xfa;
  case 12: case 13: return p.wr[reg];
  case 14: return (p.wr7_prime & 0x40) ? p.wr7_prime : 0;
  }
  return 0;
}

void Z85C30::write_control(Channel channel, u8 value)
{
  auto& p = port(channel);
  const u8 reg = std::exchange(chip.pointer, u8{0});
  if (reg == 0) {
    const unsigned command = (value >> 3) & 7;
    // WR0 selects the register for the next control access on either channel (UM010901, §2.3.4).
    chip.pointer = (value & 7) | (command == 1 ? 8 : 0);
    if ((value >> 6) == 3)
      p.underrun = 0;
    switch (command) {
    case 4: p.rx_first = 1; break;
    case 5: p.tx_pending = 0; break;
    case 6:
      p.overrun = 0;
      p.rx_errors &= 0xfe;
      if (p.rx_locked) {
        p.rx_locked = 0;
        pop_rx(p);
      }
      break;
    case 7:
      for (u8 bit = 0x20; bit; bit >>= 1) {
        if (chip.in_service & bit) {
          chip.in_service &= ~bit;
          break;
        }
      }
      break;
    }
    return;
  }

  switch (reg) {
  case 1:
    if ((p.wr[1] & 0x18) != (value & 0x18))
      p.rx_first = 1;
    if (!(value & 2))
      p.tx_pending = 0;
    break;
  case 2: chip.vector = value; return;
  case 3: case 5: case 14:
    p.wr[reg] = value;
    transmit(channel);
    return;
  case 7:
    if (p.wr[15] & 1) {
      p.wr7_prime = value;
      return;
    }
    break;
  case 8: write_data(channel, value); return;
  case 9:
    chip.wr9 = value;
    if ((value & 0xc0) == 0xc0) {
      reset_port(Channel::A, true);
      reset_port(Channel::B, true);
    } else if (value & 0xc0) {
      reset_port((value & 0x80) ? Channel::A : Channel::B, false);
    }
    return;
  }
  p.wr[reg] = value;
}

bool Z85C30::valid_state() const
{
  if (chip.pointer >= 16 || (chip.in_service & ~0x36))
    return false;
  for (const auto& p : chip.channels) {
    if (p.count > sizeof p.rx || (p.rx_errors >> p.count) || (p.rx_locked && !p.count))
      return false;
    if (p.overrun > 1 || p.rx_locked > 1 || p.tx_full > 1 || p.tx_pending > 1 || p.rx_first > 1 || p.underrun > 1)
      return false;
  }
  return true;
}
