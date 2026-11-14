/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
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

#include "../StdAfx.h"
#include "gui.h"
#include "config/Configurator.h"

// Headless sink; CDisplayOutput handles framebuffer export.
class bx_null_gui_c : public bx_gui_c
{
public:
    explicit bx_null_gui_c(CConfigurator*) {}

    void specific_init(unsigned, unsigned) override
    {
        // Skip the legacy tile framebuffer.
        new_gfx_api = true;
        host_bpp = 32;
        host_pitch = host_xres * 4;
    }

    void text_update(u8*, u8*, unsigned long, unsigned long,
        bx_vga_tminfo_t, unsigned) override {}
    void graphics_tile_update(u8*, unsigned, unsigned) override {}
    bx_svga_tileinfo_t* graphics_tile_info(bx_svga_tileinfo_t*) override
    {
        return nullptr;
    }
    u8* graphics_tile_get(unsigned, unsigned, unsigned*, unsigned*) override
    {
        return nullptr;
    }
    void graphics_tile_update_in_place(unsigned, unsigned, unsigned, unsigned) override {}
    void graphics_frame_update(const u32*, unsigned, unsigned) override {}

    void handle_events() override {}
    void flush() override {}
    void clear_screen() override {}
    bool palette_change(unsigned, unsigned, unsigned, unsigned) override
    {
        return true;
    }
    void dimension_update(unsigned width, unsigned height,
        unsigned = 0, unsigned = 0, unsigned = 8) override
    {
        host_xres = width;
        host_yres = height;
        host_bpp = 32;
        host_pitch = width * 4;
    }
    void mouse_enabled_changed_specific(bool) override {}
    void beep_on(float) override {}
    void beep_off() override {}
    void exit() override {}
};

static bx_null_gui_c* theGui;
IMPLEMENT_GUI_PLUGIN_CODE(null)
