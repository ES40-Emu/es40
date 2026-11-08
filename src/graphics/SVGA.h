/* ES40 Emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2003-2014 Nathan Woods from MAME
 * Copyright (C) 2000 Peter Trauner from MAME
 * Copyright (C) 2011-2026 Angelo Salese from MAME
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
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#if !defined(__SVGA_H__)
#define __SVGA_H__

#include "VGA.h"

/**
 * \brief Abstract base class for PCI SVGA cards.
 **/
class CSVGA : public CVGA
{
public:
  virtual void zero() override;
  virtual uint32_t screen_update(screen_device& screen, bitmap_rgb32& bitmap, const rectangle& cliprect) override;
  virtual uint8_t get_video_depth();

protected:
  // construction/destruction
  //svga_device(const machine_config& mconfig, device_type type, const char* tag, device_t* owner, uint32_t clock);
  CSVGA(class CConfigurator* cfg, class CSystem* c, int pcibus, int pcidev);

  void svga_vh_rgb8(bitmap_rgb32& bitmap, const rectangle& cliprect);
  void svga_vh_rgb15(bitmap_rgb32& bitmap, const rectangle& cliprect);
  void svga_vh_rgb16(bitmap_rgb32& bitmap, const rectangle& cliprect);
  void svga_vh_rgb24(bitmap_rgb32& bitmap, const rectangle& cliprect);
  void svga_vh_rgb32(bitmap_rgb32& bitmap, const rectangle& cliprect);
  virtual uint8_t pc_vga_choosevideomode() override;
  //virtual void device_start() override ATTR_COLD;
  virtual u16 line_compare_mask();

  struct
  {
    uint8_t bank_r, bank_w;
    uint8_t rgb8_en;
    uint8_t rgb15_en;
    uint8_t rgb16_en;
    uint8_t rgb24_en;
    uint8_t rgb32_en;
    uint8_t id;
    bool ignore_chain4;
  } svga;
};

#endif
