/* ES40 Emulator -- Cascaded 8259 interrupt controllers.
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

#ifndef ES40_PIC8259_H
#define ES40_PIC8259_H

#include <cstdint>
#include <functional>
#include "base/Mutex.h"
#include "datatypes.h"

class Pic8259
{
public:
  // The owning chipset saves these registers with its device state.
  struct State {
    u8    irr[2];               // raw interrupt request register
    u8    imr[2];               // interrupt mask register (1 = masked)
    u8    isr[2];               // in-service register
    u8    last_irr[2];          // line history for edge detection
    u8    irq_base[2];          // ICW2 vector base
    u8    priority_add[2];      // priority rotation offset
    u8    read_reg_select[2];   // OCW3 bit 0: 0=IRR, 1=ISR readback
    u8    poll[2];              // OCW3 poll-mode pending
    u8    special_mask[2];      // OCW3 special mask mode
    u8    init_state[2];        // 0=normal, 1=ICW2, 2=ICW3, 3=ICW4
    u8    auto_eoi[2];          // ICW4 AEOI bit
    u8    rotate_on_aeoi[2];    // OCW2 rotate-on-AEOI
    u8    special_fnm[2];       // ICW4 special fully nested mode
    u8    init4[2];             // ICW1 bit 0: ICW4 needed
    u8    single_mode[2];       // ICW1 bit 1: single PIC, no slave
    u8    elcr[2];              // edge/level control register (ELCR)
    u8    ltim[2];              // ICW1 bit 3: level-trigger global mode
  };

  enum class EdgeRelease { Latch, Withdraw };

  // The output callback runs under the PIC lock.
  Pic8259(State& state, std::function<void(bool)> output,
    EdgeRelease cascade_release = EdgeRelease::Latch);
  void reset();
  // Bank 0 is the master, bank 1 the slave. Offset 0 is command, 1 is data.
  u8 read(int index, u32 address);
  void write(int index, u32 address, u8 data);
  u8 read_vector();
  u8 read_edge_level(int index);
  void write_edge_level(int index, u8 data);
  // Post a fresh request even when the input is already high.
  void latch_request(int index, int irq);
  void set_line(int index, int irq, bool active, EdgeRelease release = EdgeRelease::Latch);

private:
  void init_reset(int index);
  int get_irq(int index) const;
  void update_output(int index);
  void intack(int index, int irq);
  void drive_line(int index, int irq, bool active, EdgeRelease release);

  State& state;
  std::function<void(bool)> output;
  EdgeRelease cascade_release;
  CFastMutex picLock{ "pic" };
  bool pic_messages = false;
};

#endif
