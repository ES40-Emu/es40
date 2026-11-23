/* ES40 emulator -- Jensen flash socket
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

#ifndef ES40_JENSEN_FLASH_H
#define ES40_JENSEN_FLASH_H

#include "chipset/Intel28F008SA.h"
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>

class CConfigurator;
class CSystem;

class JensenFlash
{
public:
  JensenFlash(CConfigurator* cfg, CSystem& system, unsigned bank);
  size_t size() const { return seed.size(); }
  u8 read(u32 address);
  void write(u32 address, u8 data);
  void reset();
  void service(bool flush);
  void pause();
  void resume();
  bool save(FILE* file);
  bool restore(FILE* file, bool legacy);
  void finalize_restore();
  void check_paths(const JensenFlash& other) const;
  bool uses_path(const std::string& path) const;

private:
  using Clock = std::chrono::steady_clock;
  void advance(Clock::time_point now);
  void flush();

  CSystem& system;
  std::string seed_path;
  std::string backing_path;
  std::vector<u8> seed;
  std::optional<Intel28F008SA> chip;
  Clock::time_point sampled{}, next_flush{};
  bool dirty = false;
  bool restoring = false;
  bool vpp_high = true;
};

#endif

