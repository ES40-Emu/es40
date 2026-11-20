/* ES40 emulator -- ISA slot configuration
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

#ifndef ES40_ISA_SLOT_H
#define ES40_ISA_SLOT_H

#include <charconv>
#include <cstdint>
#include <string_view>

constexpr uint16_t wss_default_io_port = 0x530;
constexpr unsigned wss_io_port_count = 8;

inline bool parse_isa_slot(std::string_view name, unsigned& bus, unsigned& slot)
{
    if (name.size() < 6 || (name[0] != 'i' && name[0] != 'I') ||
        (name[1] != 's' && name[1] != 'S') || (name[2] != 'a' && name[2] != 'A'))
        return false;
    const char* end = name.data() + name.size();
    const auto first = std::from_chars(name.data() + 3, end, bus);
    if (first.ec != std::errc() || first.ptr == end || *first.ptr != '.') return false;
    const auto second = std::from_chars(first.ptr + 1, end, slot);
    return second.ec == std::errc() && second.ptr == end;
}

inline bool parse_wss_io_port(const char* text, uint16_t& port)
{
    if (!text) {
        port = wss_default_io_port;
        return true;
    }
    std::string_view value(text);
    const bool hex = value.size() >= 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
    if (hex) value.remove_prefix(2);
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(value.data(), end, port, hex ? 16 : 10);
    return result.ec == std::errc() && result.ptr == end && port <= 0x10000 - wss_io_port_count;
}

inline bool wss_io_ports_overlap(uint16_t a, uint16_t b)
{
    return a < b + wss_io_port_count && b < a + wss_io_port_count;
}

#endif
