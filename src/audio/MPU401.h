/* ES40 emulator.
 * Copyright (C) 2026 by Cacodemon345
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Derived from the formerly stubbed MPU-401 emulation of VSBHDA
 * (https://github.com/Baron-von-Riedesel/VSBHDA), a fork of SBEMU, both
 * distributed under the GNU General Public License version 2.
 */

#pragma once

#ifdef _WIN32
#include "StdAfx.h"
#include "emu/SystemComponent.h"

#include <windows.h>
#include <mmsystem.h>

class CISABus;

class CMPU401 : public CSystemComponent
{
  virtual int   RestoreState(FILE* f) override
  {
    return 0;
  }
  virtual int   SaveState(FILE* f) override
  {
    return 0;
  }

private:
  CISABus& bus;
  const u16 io_port;
  HMIDIOUT hmo{};
  MIDIHDR hdr{};

  const int midi_lengths[8] = { 3, 3, 3, 3, 2, 2, 3, 1 };
  unsigned char midi_buffer[32] = {};
  unsigned char midi_ptr = 0;
  unsigned char midi_status_byte = 0x80;
  unsigned char midi_mpu_status = 0x80;
  bool bReset = false;
  std::vector<uint8_t> sysex_data;

public:
  CMPU401(CConfigurator* cfg, CSystem* system, CISABus& bus, u16 io_port);
  ~CMPU401() override;
  void init() override;
  std::string snapshot_identity() const override;
  const CSystemComponent* memory_decode_owner() const noexcept override;
  bool decodes_memory_access(int index, u64 address, int dsize,
    bool write) const noexcept override;
  bool uses_subtractive_decode(int index, u64 address, int dsize,
    bool write) const noexcept override;

  virtual u64   ReadMem(int index, u64 address, int dsize) override;
  virtual void  WriteMem(int index, u64 address, int dsize, u64 data) override;
};

#endif
