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

#ifndef ES40_UART_H
#define ES40_UART_H

#include <cstdint>
#include "datatypes.h"
#include <optional>

class Uart
{
public:
  enum class Model { Legacy16550, VL16C450B };
  struct Registers {
    u8 thr, rbr, dll, dlm, ier, iir, fcr, lcr, mcr, lsr, msr, scratch;
  };
  struct ReceiveQueue {
    char bytes[1024];
    int write, read;
  };
  struct Transmitter {
    u64 remaining_ns;
    u8 shift;
    bool loopback;
    bool suppress;
  };
  struct Output {
    u8 bytes[2];
    unsigned size;
  };

  explicit Uart(Model model);
  bool is_vl16c450() const { return model == Model::VL16C450B; }
  void reset();
  u8 read(unsigned offset);
  std::optional<u8> write(unsigned offset, u8 data);
  bool receive(u8 data);
  bool data_ready() const;
  bool interrupt_pending();
  Output advance(u64 elapsed_ns);
  void stage_input(const char* data, unsigned size);
  bool input_staged() const { return queue.read != queue.write; }
  u64 next_event_ns() const;

  // Serialized in the original serial component's field order.
  Registers regs{};
  ReceiveQueue queue{};
  bool thre_pending = false;
  Transmitter tx{};
  u64 rx_remaining_ns = 0;

private:
  const Model model;
  u8 character(u8 data) const;
  bool store_received(u8 data);
  void set_modem_control(u8 data);
  void start_shift();
  u64 character_time_ns() const;
  void start_receive();
};
#endif
