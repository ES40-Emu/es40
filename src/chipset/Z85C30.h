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

#ifndef ES40_Z85C30_H
#define ES40_Z85C30_H

#include <cstdint>
#include <functional>
#include "datatypes.h"

class Z85C30
{
public:
  enum class Channel { A, B };
  using Transmit = std::function<void(Channel, u8)>;

  struct ChannelState {
    u8 wr[16]{}, wr7_prime{};
    // Three receive FIFO bytes plus the shift register (UM010901, p. 7-3).
    u8 rx[4]{}, count{}, rx_errors{}, overrun{}, rx_locked{}, last_rx{};
    u8 tx{}, tx_full{}, tx_pending{}, rx_first{}, underrun{};

    bool rx_error() const { return overrun || (rx_errors & 1); }
  };
  struct State {
    ChannelState channels[2]{};
    u8 pointer{}, vector{}, wr9{}, in_service{};
  };

  Z85C30() { reset(); }
  void reset();
  u8 read_control(Channel channel);
  void write_control(Channel channel, u8 value);
  u8 read_data(Channel channel);
  void write_data(Channel channel, u8 value);
  bool can_receive(Channel channel) const;
  bool receive(Channel channel, u8 value);
  bool transmit_dma_request(Channel channel) const;
  bool receive_dma_request(Channel channel) const;
  bool interrupt() const;
  void set_transmit(Transmit callback);
  State& state() { return chip; }
  bool valid_state() const;

private:
  ChannelState& port(Channel channel) { return chip.channels[static_cast<unsigned>(channel)]; }
  const ChannelState& port(Channel channel) const { return chip.channels[static_cast<unsigned>(channel)]; }
  void reset_port(Channel channel, bool hardware);
  void transmit(Channel channel);
  bool accept_rx(ChannelState& p, u8 value);
  void pop_rx(ChannelState& p);
  u8 pending() const;
  u8 active_source() const;
  u8 read_vector(Channel channel);

  State chip{};
  Transmit send;
};

#endif
