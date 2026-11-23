/* ES40 emulator -- Intel 28F008SA FlashFile memory
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

#ifndef ES40_INTEL28F008SA_H
#define ES40_INTEL28F008SA_H

#include <cstdint>
#include "datatypes.h"
#include <vector>

class Intel28F008SA
{
public:
  static constexpr u32 capacity = 1u << 20;
  static constexpr u32 block_size = 1u << 16;

  struct State {
    u8 mode = 0;
    u8 setup = 0;
    u8 operation = 0;
    u8 errors = 0;
    u8 data = 0;
    u32 address = 0;
    u64 remaining_ns = 0;
  };

  explicit Intel28F008SA(std::vector<u8> image);
  u8 read(u32 address) const;
  void write(u32 address, u8 data, bool vpp_high);
  bool advance(u64 elapsed_ns);
  void set_vpp(bool high);
  void reset();

  std::vector<u8>& bytes() { return array; }
  const std::vector<u8>& bytes() const { return array; }
  State& state() { return chip; }
  const State& state() const { return chip; }
  bool valid_state() const;

private:
  std::vector<u8> array;
  State chip;
};

#endif
