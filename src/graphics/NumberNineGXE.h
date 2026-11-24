/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

#ifndef INCLUDED_NUMBER_NINE_GXE_H
#define INCLUDED_NUMBER_NINE_GXE_H

#include "S3Trio64.h"
#include "Bt485.h"
#include "Icd2061A.h"

class NumberNineGXE : public CS3Trio64
{
public:
  NumberNineGXE(CConfigurator* cfg, CSystem* system, int bus, int slot, bx_gui_c& display);
  void ResetPCI() override;
  int SaveState(FILE* f) override;
  int RestoreState(FILE* f) override;
  uint32_t screen_update(screen_device& screen, bitmap_rgb32& bitmap, const rectangle& clip) override;

protected:
  void crtc_data_w(offs_t offset, u8 value) override;
  u8 crtc_data_r(offs_t offset) override;
  void s3_define_video_mode() override;
  int external_clock_hz() const override;
  unsigned pixels_per_clock() const override;
  u8 AccelIORead(u32 port) override;
  void AccelIOWrite(u32 port, u8 data) override;
  bool external_dac_read(u32 port, u8& value) override;
  void ramdac_write_b(u32 port, u8 value) override;
  void palette_update() override;

private:
  bool crtc_accessible() const;
  u32 read_transfer_word();
  unsigned dac_register(unsigned port) const;
  Bt485 dac;
  Icd2061A clock;
};

#endif
