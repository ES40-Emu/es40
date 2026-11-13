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
 * What the MCP server (McpServer.cpp) and its tools (McpTools.cpp) share.
 **/

#ifndef ES40_MCP_INTERNAL_H
#define ES40_MCP_INTERNAL_H

#include "McpImage.h"
#include "McpJson.h"
#include "McpSchemas.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace es40mcp
{

class Connection;

/// One tools/call; each runs on its own thread
class Call
{
public:
  Call(std::shared_ptr<Connection> conn, Json progress_token,
    std::shared_ptr<std::atomic<bool>> cancelled);

  bool cancelled() const { return m_cancelled->load(); }

  /// Sleeps ms; false if the call was cancelled meanwhile
  bool sleep(int ms);

  /// Seconds since the call began
  double elapsed() const;

  /// Tells the client the call still runs, if it asked for progress; at
  /// most every few seconds
  void progress(const std::string& message);

private:
  std::shared_ptr<Connection> m_conn;
  Json m_progress_token;
  std::shared_ptr<std::atomic<bool>> m_cancelled;
  std::chrono::steady_clock::time_point m_start;
  std::chrono::steady_clock::time_point m_last_progress;
};

struct ToolResult
{
  std::string text;
  bool error = false;
  std::string png;  // a screenshot, or empty

  Json to_json() const;
};

// McpTools.cpp
ToolResult call_tool(Call& call, const std::string& name, const Json& args);
void release_input();     // lets go of every key and button an agent still holds
unsigned held_buttons();  // the mouse buttons an agent holds down (1 left, 2 right, 4 middle)

// McpServer.cpp: the frames the displays show
struct Frame
{
  Image img;
  uint64_t seq = 0;  // counts the frames that differ from the one before
};

/// The newest frame of a console (0: the first display that showed one)
bool latest_frame(unsigned console, Frame& out);

/// Waits at most ms for a console frame newer than seq; true if there is one
bool wait_frame(unsigned console, uint64_t seq, int ms);

unsigned console_count();

// McpServer.cpp: this VM
struct VmInfo
{
  int pid = 0;
  std::string name;
  std::string cwd;
  std::string config;
  std::string binary;
  std::string socket;
  std::string registry;
  std::string started;
};

const VmInfo& vm_info();

} // namespace es40mcp

#endif
