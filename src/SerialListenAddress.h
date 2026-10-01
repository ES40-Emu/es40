/* ES40 emulator.
 * Copyright (c) 2026 the ES40 Emulator Project contributors
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

/**
 * \file
 * Serial listener address parsing.
 */

#ifndef ES40_SERIAL_LISTEN_ADDRESS_H
#define ES40_SERIAL_LISTEN_ADDRESS_H

#include <algorithm>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__VMS)
#include <socket.h>
#include <in.h>
#include <in6.h>
#include <inet.h>
#include <netdb.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif

static inline bool serial_parse_listen_addresses(const std::string &value,
    std::vector<std::string> &addresses, std::string &error)
{
    addresses.clear();
    error.clear();
    if (value.find_first_not_of(" \t\r\n") == std::string::npos) {
        addresses.push_back("0.0.0.0");
        addresses.push_back("::");
        return true;
    }
    size_t start = 0;
    for (;;) {
        const size_t end = value.find(',', start);
        std::string address = value.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        const size_t first = address.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            error = "Empty address in listen_address";
            addresses.clear();
            return false;
        }
        address = address.substr(first,
            address.find_last_not_of(" \t\r\n") - first + 1);
        addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICHOST;
        addrinfo *resolved = nullptr;
        char canonical[NI_MAXHOST];
#if defined(_WIN32)
        WSADATA wsa;
        const int startup_error = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (startup_error != 0) {
            error = "Winsock startup failed (error " + std::to_string(startup_error) + ")";
            addresses.clear();
            return false;
        }
#endif
        int result = address.find("]:") != std::string::npos
            ? EAI_NONAME : getaddrinfo(address.c_str(), nullptr, &hints, &resolved);
        if (result == 0) {
            if (resolved->ai_family == AF_INET &&
                ((sockaddr_in *)resolved->ai_addr)->sin_addr.s_addr == INADDR_BROADCAST)
                result = EAI_NONAME;
            else
                result = getnameinfo(resolved->ai_addr, (socklen_t)resolved->ai_addrlen,
                    canonical, sizeof(canonical), nullptr, 0, NI_NUMERICHOST);
        }
        if (resolved)
            freeaddrinfo(resolved);
#if defined(_WIN32)
        WSACleanup();
#endif
        if (result != 0) {
            error = "Invalid IPv4/IPv6 address: \"" + address + "\"";
            addresses.clear();
            return false;
        }
        const std::string canonical_address = canonical;
        if (std::find(addresses.begin(), addresses.end(), canonical_address) ==
            addresses.end())
            addresses.push_back(canonical_address);
        if (end == std::string::npos)
            return true;
        start = end + 1;
    }
}

#endif
