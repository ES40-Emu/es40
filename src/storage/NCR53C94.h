/* ES40 emulator -- NCR 53C94 advanced SCSI controller.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) Olivier Galibert (MAME src/devices/machine/ncr53c90.h and ncr53c90.cpp)
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
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

#ifndef ES40_NCR53C94_H
#define ES40_NCR53C94_H

#include "emu/mame_shims.h"
#include "storage/SCSIDevice.h"

class ncr53c94_device : public device_t, public CSCSIDevice
{
public:
  ncr53c94_device(const machine_config& config, const char* tag, device_t* owner, u32 clock = 25000000);
  auto irq_handler_cb() { return m_irq_handler.bind(); }
  auto drq_handler_cb() { return m_drq_handler.bind(); }
  bool irq_state() const { return irq; }
  bool drq_state() const { return drq; }
  bool dma_to_memory() const { return dma_dir == DMA_IN; }
  bool valid_state() const;
  enum busmd_t : u8 { BUSMD_0, BUSMD_1, BUSMD_2, BUSMD_3 };
  void set_busmd(busmd_t busmd) { m_busmd = busmd; }
  u8 read(offs_t offset);
  void write(offs_t offset, u8 data);
  u8 dma_r();
  void dma_w(u8 data);
  u16 dma16_r();
  void dma16_w(u16 data);
  void advance(u64 elapsed_ns);

protected:
  void device_start() override ATTR_COLD;
  void device_reset() override ATTR_COLD;

private:
  enum { MODE_D, MODE_I, MODE_T };
  enum { IDLE, ARB_TIMEOUT_BUSY, BUSRESET_WAIT_INT, DISC_SEL_SEND_BYTE, INIT_XFR };
  enum { DMA_NONE, DMA_IN, DMA_OUT };
  enum {
    S_INTERRUPT = 0x80, S_GROSS_ERROR = 0x40, S_PARITY = 0x20, S_TC0 = 0x10, S_TCC = 0x08,
    I_SCSI_RESET = 0x80, I_ILLEGAL = 0x40, I_DISCONNECT = 0x20, I_BUS = 0x10, I_FUNCTION = 0x08
  };
  enum {
    CM_NOP = 0, CM_FLUSH_FIFO = 1, CM_RESET = 2, CM_RESET_BUS = 3,
    CI_XFER = 0x10, CI_COMPLETE = 0x11, CI_MSG_ACCEPT = 0x12, CI_PAD = 0x18,
    CI_SET_ATN = 0x1a, CI_RESET_ATN = 0x1b,
    CD_SELECT = 0x41, CD_SELECT_ATN = 0x42, CD_SELECT_ATN_STOP = 0x43,
    CD_ENABLE_SEL = 0x44, CD_DISABLE_SEL = 0x45, CD_SELECT_ATN3 = 0x46
  };
  int phase() const;
  void check_irq();
  void check_drq();
  bool save_residual() const;
  void finish(u8 reason);
  void command_pop_and_chain();
  void command_w(u8 data);
  void start_command();
  void select_target(u8 cmd);
  void step();
  bool send_byte(u8 data, bool end_message);
  u8 recv_byte(bool hold_ack);
  void decrement_tcounter();
  u8 fifo_pop();
  void fifo_push(u8 data);

  devcb_write_line m_irq_handler, m_drq_handler;
  const u32 m_clock;
  busmd_t m_busmd = BUSMD_0;
  u64 delay_ns = 0;
  u16 tcount = 0, tcounter = 0;
  u8 command[2]{}, fifo[16]{};
  u8 command_pos = 0, fifo_pos = 0, seq = 0, status = 0, istatus = 0;
  u8 config = 0, config2 = 0, config3 = 0, clock_conv = 0;
  u8 bus_id = 0, initiator_id = 0, select_timeout = 0, sync_offset = 0, sync_period = 0, fifo_align = 0;
  u8 state = IDLE, mode = MODE_D, dma_dir = DMA_NONE, xfr_phase = 0;
  u8 message_bytes = 0, cdb_remaining = 0;
  bool dma_command = false, irq = false, drq = false, ack = false, test_mode = false;
};

DECLARE_DEVICE_TYPE(NCR53C94, ncr53c94_device)

#endif
