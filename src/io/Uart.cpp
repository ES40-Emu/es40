/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023-2026 by gdwnldsKSC
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
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Uart.h"
#include <algorithm>
#include <cstring>

Uart::Uart(Model model) : model(model)
{
  reset();
}

void Uart::reset()
{
  // PB22H-KB, ch. 20, Master Reset: divisor, data registers and line format survive.
  regs.ier = 0;
  regs.iir = 0x01;
  regs.fcr = 0;
  regs.mcr = 0;
  regs.lsr = 0x60;
  regs.msr = is_vl16c450() ? 0 : 0x30;
  queue.write = queue.read = 0;
  thre_pending = false;
  tx = {};
  rx_remaining_ns = 0;
}

u8 Uart::character(u8 data) const
{
  // PB22H-KB, table 20-2: CH1:CH0 select five through eight bits per character.
  return is_vl16c450() ? data & (0xff >> (3 - (regs.lcr & 3))) : data;
}

bool Uart::data_ready() const
{
  return is_vl16c450() ? (regs.lsr & 1) != 0 : queue.read != queue.write;
}

bool Uart::store_received(u8 data)
{
  if (is_vl16c450())
  {
    // PB22H-KB, table 20-3: a new character overwrites unread RBR and latches OE.
    if (data_ready())
      regs.lsr |= 0x02;
    regs.rbr = character(data);
    regs.lsr |= 0x01;
    return true;
  }

  int next = (queue.write + 1) % sizeof(queue.bytes);
  if (next == queue.read)
    return false;
  queue.bytes[queue.write] = (char)data;
  queue.write = next;
  return true;
}

bool Uart::receive(u8 data)
{
  // PB22H-KB, table 20-4: diagnostic loopback disconnects the serial input pin.
  if (is_vl16c450() && (regs.mcr & 0x10))
    return true;
  return store_received(data);
}

void Uart::set_modem_control(u8 data)
{
  regs.mcr = is_vl16c450() ? data & 0x1f : data;
  u8 inputs = is_vl16c450() ? 0 : 0x30;
  if (data & 0x10)
  {
    // PC16550D and VL16C450B loop RTS/DTR/OUT1/OUT2 back to CTS/DSR/RI/DCD (PB22H-KB, table 20-5).
    inputs = ((data & 2) << 3) | ((data & 1) << 5) | ((data & 0x0c) << 4);
  }
  u8 delta = regs.msr & 0x0f;
  if (is_vl16c450())
  {
    u8 changed = (regs.msr ^ inputs) >> 4;
    delta |= changed & 0x0b;
    if ((regs.msr & 0x40) && !(inputs & 0x40))
      delta |= 0x04;
  }
  regs.msr = inputs | delta;
}

bool Uart::interrupt_pending()
{
  // PB22H-KB, table 20-6: line status, received data, THRE, then modem status.
  if (is_vl16c450() && (regs.ier & 4) && (regs.lsr & 0x1e))
    regs.iir = 0x06;
  else if ((regs.ier & 1) && data_ready())
    regs.iir = 0x04;
  else if ((regs.ier & 2) && thre_pending)
    regs.iir = 0x02;
  else if (is_vl16c450() && (regs.ier & 8) && (regs.msr & 0x0f))
    regs.iir = 0x00;
  else
    regs.iir = 0x01;
  return regs.iir != 0x01;
}

u64 Uart::character_time_ns() const
{
  const unsigned divisor = (regs.dlm << 8) | regs.dll;
  // PB22H-KB, pp. 20-1, 20-16: XTAL1/10 supplies the 1.8432 MHz, 16x baud clock.
  const unsigned bits = 5 + (regs.lcr & 3);
  const unsigned stop_halves = !(regs.lcr & 4) ? 2 : (bits == 5 ? 3 : 4);
  const unsigned half_bits = 2 * (1 + bits + ((regs.lcr >> 3) & 1)) + stop_halves;
  return (u64(half_bits) * 8 * divisor * 1000000000 + 1843199) / 1843200;
}

void Uart::start_shift()
{
  if (tx.remaining_ns || (regs.lsr & 0x20))
    return;
  tx.remaining_ns = character_time_ns();
  if (!tx.remaining_ns)
    return;
  tx.shift = character(regs.thr);
  tx.loopback = (regs.mcr & 0x10) != 0;
  tx.suppress = (regs.lcr & 0x40) != 0;
  regs.lsr |= 0x20;
  thre_pending = true;
  // PB22H-KB, table 20-4: diagnostic loopback immediately receives the transmitted byte.
  if (tx.loopback && !tx.suppress)
    store_received(tx.shift);
}

void Uart::start_receive()
{
  if (input_staged() && !rx_remaining_ns)
    rx_remaining_ns = character_time_ns();
}

void Uart::stage_input(const char* data, unsigned size)
{
  // Host bytes wait at SIN until the programmed clock shifts a character into RBR (PB22H-KB, p. 20-17).
  size = (std::min)(size, (unsigned)sizeof(queue.bytes) - queue.write);
  std::memcpy(queue.bytes + queue.write, data, size);
  queue.write += size;
  start_receive();
}

u64 Uart::next_event_ns() const
{
  if (!rx_remaining_ns) return tx.remaining_ns;
  if (!tx.remaining_ns) return rx_remaining_ns;
  return (std::min)(tx.remaining_ns, rx_remaining_ns);
}

Uart::Output Uart::advance(u64 elapsed_ns)
{
  Output output{};
  if (rx_remaining_ns && elapsed_ns >= rx_remaining_ns)
  {
    rx_remaining_ns = 0;
    receive(queue.bytes[queue.read++]);
    if (!input_staged())
      queue.read = queue.write = 0;
    // Start the next host byte at SIN when this frame reaches RBR (PB22H-KB, p. 20-17).
    start_receive();
  }
  else if (rx_remaining_ns)
    rx_remaining_ns -= elapsed_ns;
  while (tx.remaining_ns && elapsed_ns >= tx.remaining_ns)
  {
    elapsed_ns -= tx.remaining_ns;
    tx.remaining_ns = 0;
    if (!tx.loopback && !tx.suppress)
      output.bytes[output.size++] = tx.shift;
    if (regs.lsr & 0x20)
      regs.lsr |= 0x40;  // PB22H-KB, table 20-3: TEMT requires both THR and TSR empty.
    else
      start_shift();
  }
  if (tx.remaining_ns)
    tx.remaining_ns -= elapsed_ns;
  return output;
}

u8 Uart::read(unsigned offset)
{
  switch (offset)
  {
  case 0:
    if (regs.lcr & 0x80)
      return regs.dll;
    if (is_vl16c450())
      regs.lsr &= ~0x01;
    else if (data_ready())
    {
      regs.rbr = queue.bytes[queue.read];
      queue.read = (queue.read + 1) % sizeof(queue.bytes);
    }
    return regs.rbr;
  case 1:
    return regs.lcr & 0x80 ? regs.dlm : regs.ier;
  case 2:
    interrupt_pending();
    if (regs.iir == 0x02)
      thre_pending = false;
    return regs.iir;
  case 3:
    return regs.lcr;
  case 4:
    return regs.mcr;
  case 5:
  {
    if (!is_vl16c450())
      regs.lsr = data_ready() ? 0x61 : 0x60;
    u8 value = regs.lsr;
    regs.lsr &= ~0x1e;
    return value;
  }
  case 6:
  {
    u8 value = regs.msr;
    if (is_vl16c450())
      regs.msr &= 0xf0;
    return value;
  }
  default:
    return regs.scratch;
  }
}

std::optional<u8> Uart::write(unsigned offset, u8 data)
{
  switch (offset)
  {
  case 0:
    if (regs.lcr & 0x80)
    {
      regs.dll = data;
      if (is_vl16c450())
      {
        start_shift();
        start_receive();
      }
    }
    else
    {
      regs.thr = data;
      if (is_vl16c450())
      {
        regs.lsr &= ~0x60;
        thre_pending = false;
        start_shift();
        break;
      }
      // The legacy UART drains THR immediately, rearming the THRE interrupt.
      thre_pending = true;
      // Windows SERIAL.SYS verifies MCR.LOOP by reading back a THR sentinel through RBR.
      if (regs.mcr & 0x10)
        store_received(data);
      else
        return character(data);
    }
    break;
  case 1:
    if (regs.lcr & 0x80)
    {
      regs.dlm = data;
      if (is_vl16c450())
      {
        start_shift();
        start_receive();
      }
    }
    else
    {
      if ((data & 2) && !(regs.ier & 2) && (regs.lsr & 0x20))
        thre_pending = true;
      regs.ier = is_vl16c450() ? data & 0x0f : data;
    }
    break;
  case 2:
    if (!is_vl16c450())
      regs.fcr = data;
    break;
  case 3:
    regs.lcr = data;
    // PB22H-KB, table 20-2: break forces SOUT to spacing while the transmitter runs.
    if (is_vl16c450() && tx.remaining_ns && (data & 0x40))
      tx.suppress = true;
    break;
  case 4:
    set_modem_control(data);
    break;
  default:
    if (offset == 7 || !is_vl16c450())
      regs.scratch = data;
    break;
  }
  return std::nullopt;
}
