/* ES40 emulator -- Intel 82357 Integrated System Peripheral
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
#include "Intel82357.h"
#include "system/System.h"
#include <utility>

Intel82357::Intel82357(CConfigurator* cfg, CSystem* system, CDMAHost& memory_host,
  std::function<void(bool)> interrupt_output, std::function<void(bool)> nmi_output)
  : CSystemComponent(cfg, system), nmi_output(std::move(nmi_output)), memory_host(memory_host),
    pic(pic_state, [this, output = std::move(interrupt_output)](bool high) {
      output(high && !vector_pending);
    })
{
  save_item(pic_state.irr, "pic.irr");
  save_item(pic_state.imr, "pic.imr");
  save_item(pic_state.isr, "pic.isr");
  save_item(pic_state.last_irr, "pic.last_irr");
  save_item(pic_state.irq_base, "pic.irq_base");
  save_item(pic_state.priority_add, "pic.priority_add");
  save_item(pic_state.read_reg_select, "pic.read_reg_select");
  save_item(pic_state.poll, "pic.poll");
  save_item(pic_state.special_mask, "pic.special_mask");
  save_item(pic_state.init_state, "pic.init_state");
  save_item(pic_state.auto_eoi, "pic.auto_eoi");
  save_item(pic_state.rotate_on_aeoi, "pic.rotate_on_aeoi");
  save_item(pic_state.special_fnm, "pic.special_fnm");
  save_item(pic_state.init4, "pic.init4");
  save_item(pic_state.single_mode, "pic.single_mode");
  save_item(pic_state.elcr, "pic.elcr");
  save_item(pic_state.ltim, "pic.ltim");
  save_item(ack.vector, "inta.vector");
  save_item(ack.auto_eoi, "inta.auto_eoi");
  save_item(vector_pending, "inta.pending");

  ResetPCI();
  create_dma(cfg, system);
}

void Intel82357::ResetPCI()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  vector_pending = false;
  ack = {};
  pic.reset();
  reset_timers();
  sync_nmi_output();
}

void Intel82357::finalize_restore() noexcept
{
  pic.sync_output();
  sync_nmi_output();
}

enum class PicPort { None, Controller, EdgeLevel };

static PicPort decode_pic_port(u16 port)
{
  switch (port) {
  case 0x20: case 0x21: case 0xa0: case 0xa1:
    return PicPort::Controller;
  case 0x4d0: case 0x4d1:
    return PicPort::EdgeLevel;
  default:
    return PicPort::None;
  }
}

namespace {
bool decodes_timer_port(u16 port, bool write)
{
  switch (port) {
  case 0x40: case 0x41: case 0x42: case 0x48: case 0x4a: // PB22H-KB, Table 19-1.
  case 0x61: case 0x461:
    return true;
  case 0x43: case 0x4b: case 0x70: case 0x462:
    return write;
  default:
    return false;
  }
}

int counter_index(bool second_timer, unsigned channel)
{
  if (!second_timer) return int(channel);
  return channel == 1 ? -1 : (channel == 0 ? 3 : 4);
}
}

void Intel82357::sync_nmi_output()
{
  // Port 70h masks the NMI pin while individual enabled requests remain latched (PB22H-KB, p. 18-4).
  if (nmi_output)
    nmi_output(!nmi_masked && !nmi_acknowledged && (extended_nmi & 0xa0));
}

void Intel82357::apply_timer_edges(unsigned index, u64 rises)
{
  if (index == 0) {
    // Preserve a PIC IRQ0 edge even if several timer periods elapsed (PB22H-KB, p. 19-2).
    if (rises)
      pic.latch_request(0, 0);
    if (bool(pic_state.last_irr[0] & 1) != timer[0].output())
      pic.set_line(0, 0, timer[0].output());
  } else if (index == 1 && (rises & 1)) {
    port_b ^= 0x10; // RFSH toggles on each refresh cycle (PB22H-KB, Table 18-2).
  } else if (index == 3 && rises && (extended_nmi & 0x04) && !(extended_nmi & 0x80)) {
    extended_nmi |= 0x80; // The fail-safe timeout uses a rising-edge latch (PB22H-KB, p. 18-2).
    nmi_acknowledged = false;
    sync_nmi_output();
  }
}

bool Intel82357::access_timer_port(u16 port, u8& data, bool write)
{
  if (!decodes_timer_port(port, write))
    return false;
  refresh_timers();
  if (port == 0x61) {
    if (write) {
      port_b = (port_b & 0x10) | (data & 0x0f);
      timer[2].set_gate(data & 1);
    } else {
      // TMR reads the counter output independently of speaker-data enable (PB22H-KB, Table 18-2).
      data = port_b | (timer[2].output() ? 0x20 : 0);
    }
  } else if (port == 0x70) {
    if (nmi_masked && !(data & 0x80)) nmi_acknowledged = false;
    nmi_masked = (data & 0x80) != 0;
    sync_nmi_output();
  } else if (port == 0x461) {
    if (write) {
      // Clearing an enable clears that source latch (PB22H-KB, Table 18-3).
      const u8 keep = ((data & 2) ? 0x20 : 0) | ((data & 4) ? 0x80 : 0);
      extended_nmi = (data & 0x06) | (extended_nmi & keep);
      if (!(extended_nmi & 0xa0)) nmi_acknowledged = false;
      sync_nmi_output();
    } else {
      data = extended_nmi;
    }
  } else if (port == 0x462) {
    if ((extended_nmi & 0x22) == 2) {
      extended_nmi |= 0x20; // Any write requests a software NMI when enabled (PB22H-KB, p. 18-9).
      nmi_acknowledged = false;
      sync_nmi_output();
    }
  } else {
    const bool second_timer = (port & 8) != 0;
    const unsigned select = port & 3;
    if (select == 3 && data >> 6 == 3) {
      // Bits 1..3 select counters and bits 5/4 latch count/status when clear (PB22H-KB, Fig. 19-3).
      for (unsigned channel = 0; channel < 3; ++channel) {
        const int index = counter_index(second_timer, channel);
        if (index >= 0 && (data & (2u << channel)))
          timer[index].latch(!(data & 0x20), !(data & 0x10));
      }
    } else {
      const int index = counter_index(second_timer, select == 3 ? data >> 6 : select);
      if (index < 0) return true; // Timer 2 has no counter 1 (PB22H-KB, Table 19-1).
      auto& counter = timer[index];
      const bool previous = counter.output();
      if (select == 3) {
        if (data & 0x30) counter.program(data);
        else counter.latch(true, false);
      } else if (write) {
        counter.write(data);
      } else {
        data = counter.read();
      }
      if (counter.output() != previous) {
        apply_timer_edges(index, counter.output());
        if (index == 1)
          timer[4].set_gate(counter.output()); // A refresh request triggers timer 2 counter 2 (PB22H-KB, p. 19-3).
      }
    }
  }
  // Reading an NMI status port releases the pin while the source stays latched (82357, sec. 8.4).
  if (!write && (port == 0x61 || port == 0x461) && (extended_nmi & 0xa0) &&
      !nmi_masked && !nmi_acknowledged) {
    nmi_acknowledged = true;
    sync_nmi_output();
  }
  if (write) timer_settings_changed();
  return true;
}

bool Intel82357::decodes_port(u16 port, bool write) const
{
  return decode_pic_port(port) != PicPort::None || decodes_timer_port(port, write) || decodes_dma_port(port, write);
}

bool Intel82357::read_port(u16 port, u8& data)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  switch (decode_pic_port(port)) {
  case PicPort::Controller: {
    refresh_timers();
    const bool poll = !(port & 1) && pic_state.poll[port >> 7];
    data = pic.read(port >> 7, port & 1);
    if (poll) timer_settings_changed();
    return true;
  }
  case PicPort::EdgeLevel:
    data = pic.read_edge_level(port & 1);
    return true;
  default:
    return access_timer_port(port, data, false) || access_dma_port(port, data, false);
  }
}

bool Intel82357::write_port(u16 port, u8 data)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  switch (decode_pic_port(port)) {
  case PicPort::Controller:
    refresh_timers();
    if (!(port & 1) && (data & 0x10))
      data &= ~0x0a; // ICW1: fixed cascade, trigger modes selected by ELCR.
    pic.write(port >> 7, port & 1, data);
    timer_settings_changed();
    return true;
  case PicPort::EdgeLevel:
    pic.write_edge_level(port & 1, data & ((port & 1) ? 0xde : 0xf8));
    return true;
  default:
    return access_timer_port(port, data, true) || access_dma_port(port, data, true);
  }
}

void Intel82357::set_isa_irq(int irq, bool asserted)
{
  if (irq < 0 || irq >= 16 || irq == 2)
    return;
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  pic.set_line(irq >> 3, irq & 7, asserted);
}

void Intel82357::set_dma_chaining_irq(bool asserted)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  // The chaining request joins IRQ13 after the external pin's edge detector.
  pic.set_level_request(1, 5, asserted);
}

u8 Intel82357::interrupt_acknowledge()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  refresh_timers();
  if (!vector_pending) {
    vector_pending = true;
    ack = pic.begin_acknowledge();
    timer_settings_changed();
    return 0xff; // The first INTA pulse leaves the data bus undriven.
  }

  vector_pending = false;
  pic.end_acknowledge(ack);
  return ack.vector;
}
