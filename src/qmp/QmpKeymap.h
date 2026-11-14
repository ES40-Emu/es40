/* ES40 Emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Keycode data adapted from keycodemapdb/data/keymaps.csv.
 * QEMU numeric conventions follow keycodemapdb/tools/keymap-gen.
 * Source: https://github.com/qemu/keycodemapdb
 *
 * Copyright (c) Individual contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     1. Redistributions of source code must retain the above copyright notice,
 *        this list of conditions and the following disclaimer.
 *     2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 *     3. Neither the name of PyCA Cryptography nor the names of its contributors
 *        may be used to endorse or promote products derived from this software
 *        without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef ES40_QMP_KEYMAP_H
#define ES40_QMP_KEYMAP_H

#include "gui/gui.h"
#include <string>

namespace es40_qmp_keymap
{
struct Key
{
    const char* name;
    unsigned number;
    u32 key;
};

// QEMU qcode/qnum values follow keycodemapdb/data/keymaps.csv and tools/keymap-gen.
inline constexpr Key keys[] = {
    { "esc", 0x01, BX_KEY_ESC },
    { "1", 0x02, BX_KEY_1 },
    { "2", 0x03, BX_KEY_2 },
    { "3", 0x04, BX_KEY_3 },
    { "4", 0x05, BX_KEY_4 },
    { "5", 0x06, BX_KEY_5 },
    { "6", 0x07, BX_KEY_6 },
    { "7", 0x08, BX_KEY_7 },
    { "8", 0x09, BX_KEY_8 },
    { "9", 0x0a, BX_KEY_9 },
    { "0", 0x0b, BX_KEY_0 },
    { "minus", 0x0c, BX_KEY_MINUS },
    { "equal", 0x0d, BX_KEY_EQUALS },
    { "backspace", 0x0e, BX_KEY_BACKSPACE },
    { "tab", 0x0f, BX_KEY_TAB },
    { "q", 0x10, BX_KEY_Q },
    { "w", 0x11, BX_KEY_W },
    { "e", 0x12, BX_KEY_E },
    { "r", 0x13, BX_KEY_R },
    { "t", 0x14, BX_KEY_T },
    { "y", 0x15, BX_KEY_Y },
    { "u", 0x16, BX_KEY_U },
    { "i", 0x17, BX_KEY_I },
    { "o", 0x18, BX_KEY_O },
    { "p", 0x19, BX_KEY_P },
    { "bracket_left", 0x1a, BX_KEY_LEFT_BRACKET },
    { "bracket_right", 0x1b, BX_KEY_RIGHT_BRACKET },
    { "ret", 0x1c, BX_KEY_ENTER },
    { "ctrl", 0x1d, BX_KEY_CTRL_L },
    { "a", 0x1e, BX_KEY_A },
    { "s", 0x1f, BX_KEY_S },
    { "d", 0x20, BX_KEY_D },
    { "f", 0x21, BX_KEY_F },
    { "g", 0x22, BX_KEY_G },
    { "h", 0x23, BX_KEY_H },
    { "j", 0x24, BX_KEY_J },
    { "k", 0x25, BX_KEY_K },
    { "l", 0x26, BX_KEY_L },
    { "semicolon", 0x27, BX_KEY_SEMICOLON },
    { "apostrophe", 0x28, BX_KEY_SINGLE_QUOTE },
    { "grave_accent", 0x29, BX_KEY_GRAVE },
    { "shift", 0x2a, BX_KEY_SHIFT_L },
    { "backslash", 0x2b, BX_KEY_BACKSLASH },
    { "z", 0x2c, BX_KEY_Z },
    { "x", 0x2d, BX_KEY_X },
    { "c", 0x2e, BX_KEY_C },
    { "v", 0x2f, BX_KEY_V },
    { "b", 0x30, BX_KEY_B },
    { "n", 0x31, BX_KEY_N },
    { "m", 0x32, BX_KEY_M },
    { "comma", 0x33, BX_KEY_COMMA },
    { "dot", 0x34, BX_KEY_PERIOD },
    { "slash", 0x35, BX_KEY_SLASH },
    { "shift_r", 0x36, BX_KEY_SHIFT_R },
    { "asterisk", 0x37, BX_KEY_KP_MULTIPLY },
    { "kp_multiply", 0x37, BX_KEY_KP_MULTIPLY },
    { "alt", 0x38, BX_KEY_ALT_L },
    { "spc", 0x39, BX_KEY_SPACE },
    { "caps_lock", 0x3a, BX_KEY_CAPS_LOCK },
    { "f1", 0x3b, BX_KEY_F1 },
    { "f2", 0x3c, BX_KEY_F2 },
    { "f3", 0x3d, BX_KEY_F3 },
    { "f4", 0x3e, BX_KEY_F4 },
    { "f5", 0x3f, BX_KEY_F5 },
    { "f6", 0x40, BX_KEY_F6 },
    { "f7", 0x41, BX_KEY_F7 },
    { "f8", 0x42, BX_KEY_F8 },
    { "f9", 0x43, BX_KEY_F9 },
    { "f10", 0x44, BX_KEY_F10 },
    { "num_lock", 0x45, BX_KEY_NUM_LOCK },
    { "scroll_lock", 0x46, BX_KEY_SCRL_LOCK },
    { "kp_7", 0x47, BX_KEY_KP_HOME },
    { "kp_8", 0x48, BX_KEY_KP_UP },
    { "kp_9", 0x49, BX_KEY_KP_PAGE_UP },
    { "kp_subtract", 0x4a, BX_KEY_KP_SUBTRACT },
    { "kp_4", 0x4b, BX_KEY_KP_LEFT },
    { "kp_5", 0x4c, BX_KEY_KP_5 },
    { "kp_6", 0x4d, BX_KEY_KP_RIGHT },
    { "kp_add", 0x4e, BX_KEY_KP_ADD },
    { "kp_1", 0x4f, BX_KEY_KP_END },
    { "kp_2", 0x50, BX_KEY_KP_DOWN },
    { "kp_3", 0x51, BX_KEY_KP_PAGE_DOWN },
    { "kp_0", 0x52, BX_KEY_KP_INSERT },
    { "kp_decimal", 0x53, BX_KEY_KP_DELETE },
    { "print", 0x54, BX_KEY_PRINT },
    { "sysrq", 0x54, BX_KEY_PRINT },
    { "less", 0x56, BX_KEY_LEFT_BACKSLASH },
    { "f11", 0x57, BX_KEY_F11 },
    { "f12", 0x58, BX_KEY_F12 },
    { "kp_enter", 0x9c, BX_KEY_KP_ENTER },
    { "ctrl_r", 0x9d, BX_KEY_CTRL_R },
    { "calculator", 0xa1, BX_KEY_POWER_CALC },
    { "ac_home", 0xb2, BX_KEY_INT_HOME },
    { "kp_divide", 0xb5, BX_KEY_KP_DIVIDE },
    { "alt_r", 0xb8, BX_KEY_ALT_R },
    { "pause", 0xc6, BX_KEY_PAUSE },
    { "home", 0xc7, BX_KEY_HOME },
    { "up", 0xc8, BX_KEY_UP },
    { "pgup", 0xc9, BX_KEY_PAGE_UP },
    { "left", 0xcb, BX_KEY_LEFT },
    { "right", 0xcd, BX_KEY_RIGHT },
    { "end", 0xcf, BX_KEY_END },
    { "down", 0xd0, BX_KEY_DOWN },
    { "pgdn", 0xd1, BX_KEY_PAGE_DOWN },
    { "insert", 0xd2, BX_KEY_INSERT },
    { "delete", 0xd3, BX_KEY_DELETE },
    { "meta_l", 0xdb, BX_KEY_WIN_L },
    { "meta_r", 0xdc, BX_KEY_WIN_R },
    { "compose", 0xdd, BX_KEY_MENU },
    { "power", 0xde, BX_KEY_POWER_POWER },
    { "sleep", 0xdf, BX_KEY_POWER_SLEEP },
    { "wake", 0xe3, BX_KEY_POWER_WAKE },
    { "ac_bookmarks", 0xe6, BX_KEY_INT_FAV },
    { "stop", 0xe8, BX_KEY_INT_STOP },
    { "ac_forward", 0xe9, BX_KEY_INT_FORWARD },
    { "ac_back", 0xea, BX_KEY_INT_BACK },
    { "computer", 0xeb, BX_KEY_POWER_MYCOMP },
    { "mail", 0xec, BX_KEY_INT_MAIL },
    { nullptr, 0xb7, BX_KEY_PRINT },
    { nullptr, 0xe5, BX_KEY_INT_SEARCH },
};
}

inline bool qmp_key_code(const std::string& name, u32& key)
{
    for (const auto& entry : es40_qmp_keymap::keys)
        if (entry.name && name == entry.name)
        {
            key = entry.key;
            return true;
        }
    return false;
}

inline bool qmp_key_number(unsigned number, u32& key)
{
    for (const auto& entry : es40_qmp_keymap::keys)
        if (number == entry.number)
        {
            key = entry.key;
            return true;
        }
    return false;
}

#endif

