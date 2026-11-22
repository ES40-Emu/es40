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
  std::function<void(bool)> interrupt_output)
  : CSystemComponent(cfg, system), memory_host(memory_host),
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
}

void Intel82357::finalize_restore() noexcept
{
  pic.sync_output();
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

bool Intel82357::decodes_port(u16 port, bool write) const
{
  return decode_pic_port(port) != PicPort::None || decodes_dma_port(port, write);
}

bool Intel82357::read_port(u16 port, u8& data)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  switch (decode_pic_port(port)) {
  case PicPort::Controller:
    data = pic.read(port >> 7, port & 1);
    return true;
  case PicPort::EdgeLevel:
    data = pic.read_edge_level(port & 1);
    return true;
  default:
    return access_dma_port(port, data, false);
  }
}

bool Intel82357::write_port(u16 port, u8 data)
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  switch (decode_pic_port(port)) {
  case PicPort::Controller:
    if (!(port & 1) && (data & 0x10))
      data &= ~0x0a; // ICW1: fixed cascade, trigger modes selected by ELCR.
    pic.write(port >> 7, port & 1, data);
    return true;
  case PicPort::EdgeLevel:
    pic.write_edge_level(port & 1, data & ((port & 1) ? 0xde : 0xf8));
    return true;
  default:
    return access_dma_port(port, data, true);
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
  if (!vector_pending) {
    vector_pending = true;
    ack = pic.begin_acknowledge();
    return 0xff; // The first INTA pulse leaves the data bus undriven.
  }

  vector_pending = false;
  pic.end_acknowledge(ack);
  return ack.vector;
}
