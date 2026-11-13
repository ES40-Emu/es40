/* ES40 emulator.
 * Copyright (C) 2026 by Antoni Sawicki
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
 * MCP server: lets an AI agent see the screen and use the keyboard and mouse
 * of the emulated machine, through the Model Context Protocol.
 *
 * It is off unless the system section of the configuration turns it on:
 *
 *   sys0 = tsunami { mcp = true; }
 *
 * or the environment has ES40_MCP=1. es40 then listens on a Unix socket and
 * speaks MCP's JSON-RPC there, one message per line, as MCP's stdio transport
 * does. It writes <pid>.json and <pid>.sock into a private registry directory
 * (/tmp/es40-<uid> by default), where es40-mcp (McpRelay.cpp), the MCP
 * server an agent starts, finds every running VM. Any MCP client can also
 * connect to the socket directly, for example with "nc -U <socket>" as its
 * command.
 **/

#ifndef ES40_MCP_SERVER_H
#define ES40_MCP_SERVER_H

#include <atomic>
#include <cstdint>

class CConfigurator;

/// Reads the mcp settings from the system section of the configuration
void es40_mcp_configure(CConfigurator* system_cfg);

/// Opens the socket and the registry entry if MCP is on; once the devices exist
void es40_mcp_start(const char* argv0, const char* config_file);

/// Ends the calls, closes the socket and removes the registry entry; call it
/// while the devices still exist
void es40_mcp_stop();

/// True while the MCP server wants the frames the displays show
extern std::atomic<bool> es40_mcp_tap;

/// A display (output) shows a new ARGB32 frame
void es40_mcp_frame(const void* output, const uint32_t* pixels, unsigned width, unsigned height);

#endif
