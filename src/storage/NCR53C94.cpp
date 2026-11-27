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

#include "NCR53C94.h"
#include "emu/address_map.h"
#include "storage/SCSIBus.h"
#include <cstring>

DEFINE_DEVICE_TYPE(NCR53C94, ncr53c94_device, "ncr53c94", "NCR 53C94 Advanced SCSI Controller")

ncr53c94_device::ncr53c94_device(const machine_config& mconfig, const char* tag, device_t* owner, u32 clock)
  : device_t(mconfig, NCR53C94, tag, owner, clock), m_irq_handler(*this), m_drq_handler(*this), m_clock(clock)
{
}

void ncr53c94_device::device_start()
{
  save_item(NAME(delay_ns));
  save_item(NAME(tcount));
  save_item(NAME(tcounter));
  save_item(NAME(command));
  save_item(NAME(fifo));
  save_item(NAME(command_pos));
  save_item(NAME(fifo_pos));
  save_item(NAME(seq));
  save_item(NAME(status));
  save_item(NAME(istatus));
  save_item(NAME(config));
  save_item(NAME(config2));
  save_item(NAME(config3));
  save_item(NAME(clock_conv));
  save_item(NAME(bus_id));
  save_item(NAME(initiator_id));
  save_item(NAME(select_timeout));
  save_item(NAME(sync_offset));
  save_item(NAME(sync_period));
  save_item(NAME(fifo_align));
  save_item(NAME(state));
  save_item(NAME(mode));
  save_item(NAME(dma_dir));
  save_item(NAME(xfr_phase));
  save_item(NAME(message_bytes));
  save_item(NAME(cdb_remaining));
  save_item(NAME(dma_command));
  save_item(NAME(irq));
  save_item(NAME(drq));
  save_item(NAME(ack));
  save_item(NAME(test_mode));
}

bool ncr53c94_device::valid_state() const
{
  return command_pos <= 2 && fifo_pos <= sizeof(fifo) && mode <= MODE_T && state <= INIT_XFR &&
    dma_dir <= DMA_OUT && bus_id < 8 && initiator_id < 8 && xfr_phase < 8 && cdb_remaining <= 16 && message_bytes <= 3;
}

void ncr53c94_device::device_reset()
{
  auto* bus = scsi_bus[0];
  if (bus && mode == MODE_I && bus->state.initiator == initiator_id && phase() != SCSI_PHASE_FREE)
    bus->free_bus(phase() == SCSI_PHASE_ARBITRATION ? bus->state.initiator : bus->state.target);
  std::memset(fifo, 0, sizeof(fifo));
  command[0] = command[1] = 0;
  fifo_pos = command_pos = 0;
  config &= 7;
  config2 = config3 = fifo_align = 0;
  clock_conv = 2;
  sync_period = 5;
  sync_offset = 0;
  seq = status = istatus = 0;
  tcount = tcounter = 0;
  delay_ns = 0;
  state = IDLE;
  mode = MODE_D;
  dma_dir = DMA_NONE;
  xfr_phase = message_bytes = cdb_remaining = 0;
  dma_command = test_mode = ack = false;
  irq = drq = false;
  m_irq_handler(0);
  m_drq_handler(0);
}

int ncr53c94_device::phase() const
{
  return scsi_bus[0] ? scsi_bus[0]->get_phase() : SCSI_PHASE_FREE;
}

void ncr53c94_device::check_irq()
{
  const bool level = istatus != 0;
  if (irq != level) {
    irq = level;
    m_irq_handler(irq);
  }
}

void ncr53c94_device::check_drq()
{
  const bool word = m_busmd != BUSMD_0;
  const bool at_end = ack || phase() != xfr_phase || (status & S_TC0) || tcounter == 1;
  const bool receive_ready = fifo_pos >= 2 || (fifo_pos && (!word || (at_end && !save_residual())));
  const bool transmit_ready = fifo_pos <= (word ? 14 : 15) && !(status & S_TC0) &&
    !(tcounter == 1 && save_residual());
  const bool level = (dma_dir == DMA_IN && receive_ready) || (dma_dir == DMA_OUT && transmit_ready);
  if (drq != level) {
    drq = level;
    m_drq_handler(drq);
  }
}

bool ncr53c94_device::save_residual() const
{
  return state == INIT_XFR && m_busmd != BUSMD_0 && (config3 & 4) && xfr_phase <= SCSI_PHASE_DATA_IN;
}

void ncr53c94_device::finish(u8 reason)
{
  state = IDLE;
  dma_dir = DMA_NONE;
  check_drq();
  istatus |= reason;
  check_irq();
}

u8 ncr53c94_device::fifo_pop()
{
  if (!fifo_pos)
    return fifo[0];
  const u8 value = fifo[0];
  std::memmove(fifo, fifo + 1, --fifo_pos);
  return value;
}

void ncr53c94_device::fifo_push(u8 data)
{
  if (fifo_pos < sizeof(fifo))
    fifo[fifo_pos++] = data;
  else
    status |= S_GROSS_ERROR;
}

u8 ncr53c94_device::read(offs_t offset)
{
  switch (offset) {
  case 0: return u8(tcounter);
  case 1: return u8(tcounter >> 8);
  case 2: {
    const u8 data = fifo_pos ? fifo_pop() : 0;
    step();
    return data;
  }
  case 3: return command[0];
  case 4: {
    const int p = phase();
    const u8 data = status | (irq ? S_INTERRUPT : 0) | (p >= 0 ? p : 0);
    if (irq)
      status &= ~(S_GROSS_ERROR | S_PARITY | S_TCC);
    return data;
  }
  case 5: {
    const u8 data = istatus;
    if (irq) {
      status &= ~(S_GROSS_ERROR | S_PARITY | S_TCC);
      istatus = seq = 0;
      check_irq();
      command_pop_and_chain();
    }
    return data;
  }
  case 6: return seq;
  case 7: return (seq << 5) | fifo_pos; // FIFO flags and sequence step (NCR 53C94, Fig. 11).
  case 8: return config;
  case 11: return config2;
  case 12: return config3;
  case 14: return 0;
  default: return 0xff;
  }
}

void ncr53c94_device::write(offs_t offset, u8 data)
{
  switch (offset) {
  case 0: tcount = (tcount & 0xff00) | data; break;
  case 1: tcount = (tcount & 0x00ff) | (u16(data) << 8); break;
  case 2:
    if (dma_dir == DMA_OUT && tcounter == 1 && save_residual())
      decrement_tcounter();
    fifo_push(data);
    step();
    break;
  case 3: command_w(data); break;
  case 4: bus_id = data & 7; break;
  case 5: select_timeout = data; break;
  case 6: sync_period = data & 0x1f; break;
  case 7: sync_offset = data & 0xf; break;
  case 8:
    config = data;
    test_mode |= (data & 0x08) != 0;
    break;
  case 9: clock_conv = data & 7; break;
  case 10:
    if (test_mode)
      mode = (data & 2) ? MODE_I : (data & 1) ? MODE_T : MODE_D;
    break;
  case 11: config2 = data; break;
  case 12: config3 = data; break;
  case 15: fifo_align = data; break;
  }
}

void ncr53c94_device::command_w(u8 data)
{
  const u8 cmd = data & 0x7f;
  if (cmd == CM_RESET || cmd == CM_RESET_BUS)
    command_pos = 0;
  if (command_pos == 2) {
    status |= S_GROSS_ERROR;
    return;
  }
  command[command_pos++] = data;
  if (command_pos == 1)
    start_command();
}

void ncr53c94_device::command_pop_and_chain()
{
  if (command_pos && --command_pos) {
    command[0] = command[1];
    start_command();
  }
}

void ncr53c94_device::decrement_tcounter()
{
  if (dma_command && !(status & S_TC0) && !--tcounter)
    status |= S_TC0;
}

void ncr53c94_device::select_target(u8 cmd)
{
  seq = cdb_remaining = 0;
  ack = false;
  auto* bus = scsi_bus[0];
  initiator_id = config & 7;
  const bool won = bus && bus->get_phase() == SCSI_PHASE_FREE && bus->arbitrate(initiator_id);
  if (won && bus_id != initiator_id && bus->targets[bus_id] != this &&
      bus->select(initiator_id, bus_id, cmd != CD_SELECT)) {
    mode = MODE_I;
    state = DISC_SEL_SEND_BYTE;
    message_bytes = cmd == CD_SELECT ? 0 : cmd == CD_SELECT_ATN3 ? 3 : 1;
    dma_dir = dma_command ? DMA_OUT : DMA_NONE;
    step();
    return;
  }
  if (won)
    bus->free_bus(initiator_id);
  // The selection interval is RV * 8192 * CCF clocks (NCR 53C94, Select/Reselect Timeout).
  const u32 rv = select_timeout ? select_timeout : 256;
  const u32 ccf = clock_conv ? clock_conv : 8;
  delay_ns = (u64(rv) * 8192 * ccf * 1000000000 + m_clock - 1) / m_clock;
  state = ARB_TIMEOUT_BUSY;
}

void ncr53c94_device::advance(u64 elapsed_ns)
{
  if (!delay_ns)
    return;
  if (elapsed_ns < delay_ns) {
    delay_ns -= elapsed_ns;
    return;
  }
  delay_ns = 0;
  if (state == ARB_TIMEOUT_BUSY)
    finish(I_DISCONNECT);
  else if (state == BUSRESET_WAIT_INT) {
    if (config & 0x40) {
      state = IDLE;
      command_pop_and_chain();
    } else
      finish(I_SCSI_RESET);
  }
}

void ncr53c94_device::start_command()
{
  const u8 cmd = command[0] & 0x7f;
  dma_command = (command[0] & 0x80) != 0;
  if (dma_command) {
    tcounter = tcount;
    status &= ~S_TC0;
  }

  switch (cmd) {
  case CM_NOP:
    command_pop_and_chain();
    return;
  case CM_FLUSH_FIFO:
    fifo_pos = fifo[0] = 0;
    command_pop_and_chain();
    return;
  case CM_RESET:
    device_reset();
    return;
  case CM_RESET_BUS:
    if (scsi_bus[0])
      scsi_bus[0]->reset_bus();
    mode = MODE_D;
    ack = false;
    dma_dir = DMA_NONE;
    check_drq();
    state = BUSRESET_WAIT_INT;
    delay_ns = (u64(130) * (clock_conv ? clock_conv : 8) * 1000000000 + m_clock - 1) / m_clock;
    return;
  }

  if (mode == MODE_D) {
    switch (cmd) {
    case CD_SELECT: case CD_SELECT_ATN: case CD_SELECT_ATN_STOP: case CD_SELECT_ATN3:
      select_target(cmd);
      return;
    case CD_ENABLE_SEL:
      command_pop_and_chain();
      return;
    case CD_DISABLE_SEL:
      finish(I_FUNCTION);
      return;
    }
  }

  if (mode == MODE_I) {
    const int p = phase();
    switch (cmd) {
    case CI_SET_ATN: case CI_RESET_ATN:
      if (cmd == CI_RESET_ATN && p == SCSI_PHASE_MSG_OUT && !message_bytes)
        scsi_xfer_done(0);
      command_pop_and_chain();
      return;
    case CI_MSG_ACCEPT:
      if (ack) {
        ack = false;
        scsi_xfer_done(0);
      }
      if (phase() == SCSI_PHASE_FREE) {
        mode = MODE_D;
        finish(I_DISCONNECT);
      } else
        finish(I_BUS);
      return;
    case CI_COMPLETE:
      if (p != SCSI_PHASE_STATUS) {
        finish(I_BUS);
        return;
      }
      fifo_push(recv_byte(false));
      if (phase() != SCSI_PHASE_MSG_IN) {
        finish(I_BUS);
        return;
      }
      fifo_push(recv_byte(true));
      if (dma_command) {
        xfr_phase = SCSI_PHASE_MSG_IN;
        state = INIT_XFR;
        dma_dir = DMA_IN;
        check_drq();
      } else
        finish(I_FUNCTION);
      return;
    case CI_XFER: case CI_PAD:
      if (p < 0 || ack) {
        finish(I_BUS);
        return;
      }
      xfr_phase = u8(p);
      state = INIT_XFR;
      dma_dir = dma_command && cmd != CI_PAD ? (p & 1 ? DMA_IN : DMA_OUT) : DMA_NONE;
      step();
      return;
    }
  }
  finish(I_ILLEGAL);
}

bool ncr53c94_device::send_byte(u8 data, bool end_message)
{
  const int p = phase();
  if (p < 0 || (p & 1) || !scsi_expected_xfer(0))
    return false;
  *static_cast<u8*>(scsi_xfer_ptr(0, 1)) = data;
  switch (p) {
  case SCSI_PHASE_MSG_OUT:
    if (end_message)
      scsi_xfer_done(0);
    break;
  case SCSI_PHASE_COMMAND: {
    // The target requests the CDB length implied by the opcode group (SCSI-2, section 7.2.1).
    static constexpr u8 length[] = {6, 10, 10, 6, 16, 12, 6, 10};
    if (!cdb_remaining)
      cdb_remaining = length[data >> 5];
    if (!--cdb_remaining)
      scsi_xfer_done(0);
    break;
  }
  default:
    if (!scsi_expected_xfer(0))
      scsi_xfer_done(0);
    break;
  }
  return true;
}

u8 ncr53c94_device::recv_byte(bool hold_ack)
{
  const u8 data = *static_cast<u8*>(scsi_xfer_ptr(0, 1));
  if (hold_ack)
    ack = true;
  else if (!scsi_expected_xfer(0))
    scsi_xfer_done(0);
  return data;
}

void ncr53c94_device::step()
{
  const u8 cmd = command[0] & 0x7f;
  if (state == DISC_SEL_SEND_BYTE) {
    while (message_bytes && phase() == SCSI_PHASE_MSG_OUT && fifo_pos) {
      --message_bytes;
      send_byte(fifo_pop(), !message_bytes && cmd != CD_SELECT_ATN_STOP);
    }
    if (!message_bytes && cmd == CD_SELECT_ATN_STOP) {
      seq = 1;
      finish(I_BUS | I_FUNCTION);
      return;
    }
    if (!message_bytes) {
      while (phase() == SCSI_PHASE_COMMAND && fifo_pos) {
        seq = 3;
        send_byte(fifo_pop(), false);
      }
      if (phase() != SCSI_PHASE_COMMAND) {
        seq = cdb_remaining || fifo_pos || (dma_command && !(status & S_TC0)) ? 2 : 4;
        finish(I_BUS | I_FUNCTION);
        return;
      }
    }
    check_drq();
    return;
  }

  if (state != INIT_XFR) {
    check_drq();
    return;
  }

  if (cmd == CI_PAD) {
    while (phase() == xfr_phase && scsi_expected_xfer(0)) {
      if (xfr_phase & 1)
        recv_byte(false);
      else
        send_byte(0, tcounter == 1);
      decrement_tcounter();
      if (!dma_command || (status & S_TC0))
        break;
    }
    finish(I_BUS);
    return;
  }

  if (cmd == CI_COMPLETE) {
    if (!fifo_pos || (status & S_TC0))
      finish(I_FUNCTION);
    else
      check_drq();
    return;
  }

  if (xfr_phase & 1) {
    // Asynchronous DATA IN counts SCSI handshakes; other phases count DMA handshakes (NCR 53C94, p. 13).
    const bool count_on_bus = dma_command && !sync_offset && xfr_phase == SCSI_PHASE_DATA_IN;
    while (phase() == xfr_phase && !ack && scsi_expected_xfer(0) && fifo_pos < sizeof(fifo)) {
      if (dma_command && ((status & S_TC0) || (!count_on_bus && tcounter && fifo_pos >= tcounter)))
        break;
      const bool hold = xfr_phase == SCSI_PHASE_MSG_IN && (!dma_command || tcounter == fifo_pos + 1);
      fifo_push(recv_byte(hold));
      if (count_on_bus)
        decrement_tcounter();
      if (!dma_command)
        break;
    }
    const bool saved = fifo_pos == 1 && save_residual();
    if (!dma_command || ((!fifo_pos || saved) && (ack || phase() != xfr_phase ||
        (status & S_TC0) || (saved && !count_on_bus && tcounter == 1)))) {
      finish(ack ? I_FUNCTION : I_BUS);
      return;
    }
  } else {
    while (phase() == xfr_phase && fifo_pos) {
      const bool last = fifo_pos == 1 && (!dma_command || (status & S_TC0));
      if (!send_byte(fifo_pop(), last))
        break;
    }
    if (phase() != xfr_phase || (!fifo_pos && (!dma_command || (status & S_TC0)))) {
      finish(I_BUS);
      return;
    }
  }
  check_drq();
}

u8 ncr53c94_device::dma_r()
{
  if (dma_dir != DMA_IN || !drq)
    return 0xff;
  const u8 value = fifo_pop();
  if (sync_offset || xfr_phase != SCSI_PHASE_DATA_IN)
    decrement_tcounter();
  step();
  return value;
}

void ncr53c94_device::dma_w(u8 data)
{
  if (dma_dir != DMA_OUT || !drq)
    return;
  fifo_push(data);
  decrement_tcounter();
  step();
}

u16 ncr53c94_device::dma16_r()
{
  if (dma_dir != DMA_IN || !drq)
    return 0xffff;
  if (fifo_pos < 2)
    return 0xff00 | dma_r(); // A final single byte occupies the low lane (NCR 53C94, Config 3 bit 2).
  const u8 low = fifo_pop();
  const u8 high = fifo_pop();
  if (sync_offset || xfr_phase != SCSI_PHASE_DATA_IN) {
    decrement_tcounter();
    decrement_tcounter();
  }
  step();
  return u16(high) << 8 | low;
}

void ncr53c94_device::dma16_w(u16 data)
{
  if (dma_dir != DMA_OUT || !drq)
    return;
  if (tcounter == 1 || fifo_pos > 14) {
    dma_w(u8(data));
    return;
  }
  fifo_push(u8(data));
  fifo_push(u8(data >> 8));
  decrement_tcounter();
  decrement_tcounter();
  step();
}
