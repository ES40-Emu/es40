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
 * The tools' schemas and the agent's instructions (see McpSchemas.h).
 **/

#include "McpSchemas.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace es40mcp
{

const char server_instructions[] =
  "You are operating an emulated AlphaServer ES40 through es40.\n"
  "- Look before you act: take a screenshot first. After an input, use its"
  " 'then' to wait and look in the same call.\n"
  "- steps runs several tool calls in one. It stops at the first wait that"
  " is not met, so put a wait after each action.\n"
  "- Use wait-screen instead of sleeping. For long installs use"
  " change-then-stable with a long stable-ms and timeout-ms; one call can"
  " wait up to 25 minutes.\n"
  "- es40 draws text screens (SRM console, AlphaBIOS, setup) as pixels: read"
  " them from screenshots.\n"
  "- The keyboard is a PS/2 keyboard with the US layout. The mouse is a"
  " relative PS/2 mouse: es40 homes the pointer to the top-left corner,"
  " then moves it in small steps. Check where it went with screenshot"
  " pointer=true.\n"
  "- vm-power quit stops es40 cleanly, saving its flash, DPR and TOY files."
  " Never stop or reset a VM you did not start unless the user asked.\n";

namespace
{

const char kRect[] =
  "{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\",\"description\":\"left edge\"},"
  "\"y\":{\"type\":\"integer\",\"description\":\"top edge\"},"
  "\"w\":{\"type\":\"integer\",\"description\":\"width\"},"
  "\"h\":{\"type\":\"integer\",\"description\":\"height\"}},"
  "\"required\":[\"x\",\"y\",\"w\",\"h\"]";

std::string rect_schema(const char* description)
{
  return std::string(kRect) + ",\"description\":\"" + description + "\"}";
}

std::string shot_schema(const char* description)
{
  return "{\"description\":\"" + std::string(description) + "\",\"anyOf\":[{\"type\":\"boolean\"},"
    "{\"type\":\"object\",\"properties\":{\"region\":" + rect_schema("capture only this rectangle") +
    ",\"scale\":{\"type\":\"number\",\"description\":\"from 0.1 to 2.0 (default: 1.0)\"}}}]}";
}

std::string then_schema(const char* description)
{
  return std::string("{\"type\":\"object\",\"description\":\"") + description + "\",\"properties\":{"
    "\"wait\":{\"type\":\"string\",\"enum\":[\"none\",\"change\",\"stable\",\"change-then-stable\"],"
    "\"description\":\"what to wait for (default: none)\"},"
    "\"delay-ms\":{\"type\":\"integer\",\"description\":\"sleep this long after the input, before"
    " waiting (default: 0)\"},"
    "\"stable-ms\":{\"type\":\"integer\",\"description\":\"how long the screen must not change to be"
    " stable (default: 1000)\"},"
    "\"timeout-ms\":{\"type\":\"integer\",\"description\":\"give up waiting after this long, at most"
    " 1500000, 25 minutes (default: 10000)\"},"
    "\"ignore-animation\":{\"type\":\"boolean\",\"description\":\"do not count areas that keep"
    " changing on their own, as in wait-screen (default: false)\"},"
    "\"screenshot\":" + shot_schema("take a screenshot at the end (default: false)") + "}}";
}

const char kConsole[] =
  "\"console\":{\"description\":\"console index (default: 0, the first display)\"}";

std::string move_props()
{
  return "\"x\":{\"type\":\"integer\",\"description\":\"target x in framebuffer pixels\"},"
    "\"y\":{\"type\":\"integer\",\"description\":\"target y in framebuffer pixels\"},"
    "\"home\":{\"type\":\"string\",\"enum\":[\"auto\",\"always\",\"never\"],\"description\":\"when to"
    " home the pointer to the top-left corner first; auto: when its position is not known"
    " (default: auto)\"},"
    "\"step-px\":{\"type\":\"integer\",\"description\":\"largest step in mouse counts; keep it below"
    " the guest's acceleration (default: 4)\"},"
    "\"step-delay-ms\":{\"type\":\"integer\",\"description\":\"pause between steps (default: 20)\"}";
}

const char kButton[] =
  "\"button\":{\"type\":\"string\",\"enum\":[\"left\",\"middle\",\"right\",\"wheel-up\",\"wheel-down\"],"
  "\"description\":\"which button (default: left)\"}";

std::vector<std::string> tool_schemas()
{
  std::vector<std::string> t;

  t.push_back(
    "{\"name\":\"vm-info\",\"description\":\"Describe the VM: name, machine, run state, consoles"
    " (index, size), where the pointer is thought to be, and the configuration file that lists the"
    " disks, serial ports and network. Use it first.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},"
    "\"annotations\":{\"readOnlyHint\":true,\"openWorldHint\":false}}");
  t.push_back(
    "{\"name\":\"screenshot\",\"description\":\"Capture a console as PNG. Coordinates in all tools are"
    " framebuffer pixels of this console, origin top-left. If scale is not 1, divide image"
    " coordinates by scale.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{" +
    std::string(kConsole) + ",\"region\":" + rect_schema("capture only this rectangle") +
    ",\"scale\":{\"type\":\"number\",\"description\":\"from 0.1 to 2.0 (default: 1.0)\"},"
    "\"pointer\":{\"type\":\"boolean\",\"description\":\"draw a small marker where es40 thinks the"
    " pointer is (default: false)\"}}},"
    "\"annotations\":{\"readOnlyHint\":true,\"openWorldHint\":false}}");
  t.push_back(
    "{\"name\":\"wait-screen\",\"description\":\"Wait until the screen changes, becomes stable, or a"
    " pixel has a colour. Use it instead of sleeping. change-then-stable waits for something to"
    " happen, then for it to settle. Changes smaller than min-pixels and blinking cursors are not"
    " counted. One call can wait up to 25 minutes; for a longer install, call it again. If a clock or"
    " a graph keeps the screen from settling, use ignore-animation.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"until\":{\"type\":\"string\",\"enum\":[\"none\",\"change\",\"stable\",\"change-then-stable\","
    "\"pixel\"],\"description\":\"what to wait for\"},"
    "\"stable-ms\":{\"type\":\"integer\",\"description\":\"how long the screen must not change to be"
    " stable (default: 2000)\"},"
    "\"timeout-ms\":{\"type\":\"integer\",\"description\":\"give up after this long, at most 1500000,"
    " 25 minutes (default: 60000)\"},"
    "\"min-pixels\":{\"type\":\"integer\",\"description\":\"a change must touch at least this many"
    " pixels (default: 64)\"},"
    "\"region\":" + rect_schema("only changes inside this rectangle count") + ","
    "\"ignore\":{\"type\":\"array\",\"items\":" + std::string(kRect) + "},\"description\":\"changes"
    " inside these rectangles do not count\"},"
    "\"ignore-animation\":{\"type\":\"boolean\",\"description\":\"do not count areas that keep"
    " changing on their own, such as a clock or a spinner (default: false)\"},"
    "\"ignore-blink\":{\"type\":\"boolean\",\"description\":\"do not count blinking cursors"
    " (default: true)\"},"
    "\"pixel\":{\"type\":\"object\",\"description\":\"the pixel to watch, for pixel\",\"properties\":{"
    "\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"rgb\":{\"type\":\"string\","
    "\"description\":\"colour as #rrggbb\"},\"tolerance\":{\"type\":\"integer\",\"description\":"
    "\"allowed difference per colour channel (default: 16)\"}},\"required\":[\"x\",\"y\",\"rgb\"]},"
    "\"screenshot\":" + shot_schema("take a screenshot at the end (default: true)") + "," +
    std::string(kConsole) + "},\"required\":[\"until\"]},"
    "\"annotations\":{\"readOnlyHint\":true,\"openWorldHint\":false}}");
  t.push_back(
    "{\"name\":\"key\",\"description\":\"Press keys and key combinations, one after another. Key"
    " names: \\\"ret\\\", \\\"spc\\\", \\\"esc\\\", \\\"tab\\\", \\\"bksp\\\", \\\"up\\\","
    " \\\"pgdn\\\", \\\"f1\\\", \\\"a\\\", \\\"1\\\", \\\"minus\\\", \\\"ctrl\\\", \\\"alt\\\","
    " \\\"shift\\\", \\\"meta_l\\\". Join a combination with - or + (\\\"ctrl-alt-delete\\\","
    " \\\"alt-f\\\"); \\\"down*5\\\" presses Down five times. Aliases: enter, space, escape, del,"
    " backspace, pageup, pagedown, win.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"keys\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"keys or"
    " combinations to press, in order\"},"
    "\"delay-ms\":{\"type\":\"integer\",\"description\":\"pause after each combination (default: 80)\"},"
    "\"hold-ms\":{\"type\":\"integer\",\"description\":\"how long the key stays down (default: 40)\"},"
    "\"modifier-lead-ms\":{\"type\":\"integer\",\"description\":\"pause after each modifier goes down"
    " (default: 20)\"},"
    "\"then\":" + then_schema("wait and look after the input") + "},\"required\":[\"keys\"]}}");
  t.push_back(
    "{\"name\":\"type-text\",\"description\":\"Type a string on the guest keyboard (US layout)."
    " \\\"\\\\n\\\" presses Enter, \\\"\\\\t\\\" Tab. Every character is checked first; if one"
    " cannot be typed, nothing is typed. Check that the right field has the focus first.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"text\":{\"type\":\"string\",\"description\":\"the text to type\"},"
    "\"char-delay-ms\":{\"type\":\"integer\",\"description\":\"pause after each character"
    " (default: 30)\"},"
    "\"hold-ms\":{\"type\":\"integer\",\"description\":\"how long each key stays down (default: 20)\"},"
    "\"modifier-lead-ms\":{\"type\":\"integer\",\"description\":\"pause after Shift goes down"
    " (default: 20)\"},"
    "\"layout\":{\"type\":\"string\",\"description\":\"the guest's keyboard layout; es40 types"
    " en-us only (default: en-us)\"},"
    "\"then\":" + then_schema("wait and look after the input") + "},\"required\":[\"text\"]}}");
  t.push_back(
    "{\"name\":\"mouse-move\",\"description\":\"Move the mouse pointer to x, y, or by dx, dy. The"
    " es40 mouse is a relative PS/2 mouse: to reach x, y es40 first homes the pointer to the"
    " top-left corner, then moves in small paced steps.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{" + move_props() + ","
    "\"dx\":{\"type\":\"integer\",\"description\":\"move right by this much (negative: left)\"},"
    "\"dy\":{\"type\":\"integer\",\"description\":\"move down by this much (negative: up)\"},"
    "\"then\":" + then_schema("wait and look after the move") + "}}}");
  t.push_back(
    "{\"name\":\"mouse-click\",\"description\":\"Click a mouse button, after moving to x, y if"
    " given (see mouse-move).\",\"inputSchema\":{\"type\":\"object\",\"properties\":{" +
    move_props() + "," + kButton + ","
    "\"count\":{\"type\":\"integer\",\"description\":\"1 to 3; 2 is a double-click (default: 1)\"},"
    "\"hold-ms\":{\"type\":\"integer\",\"description\":\"time the button stays down (default: 60)\"},"
    "\"interval-ms\":{\"type\":\"integer\",\"description\":\"time between clicks (default: 100)\"},"
    "\"then\":" + then_schema("wait and look after the click") + "}}}");
  t.push_back(
    "{\"name\":\"mouse-button\",\"description\":\"Press or release a mouse button without moving,"
    " for menus that need press-and-hold: mouse-button down, mouse-move, mouse-button up.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"action\":{\"type\":\"string\",\"enum\":[\"down\",\"up\"],\"description\":\"press or release\"},"
    + std::string(kButton) + ",\"then\":" + then_schema("wait and look after the input") +
    "},\"required\":[\"action\"]}}");
  t.push_back(
    "{\"name\":\"vm-power\",\"description\":\"Reset the system, or quit es40 cleanly (it saves its"
    " flash, DPR and TOY files). Never do this to a VM you did not start unless the user asked.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\","
    "\"enum\":[\"reset\",\"quit\"],\"description\":\"what to do\"}},\"required\":[\"action\"]},"
    "\"annotations\":{\"destructiveHint\":true,\"openWorldHint\":false}}");
  t.push_back(
    "{\"name\":\"steps\",\"description\":\"Run several tool calls in one, in order: keys, typing,"
    " mouse, waits, screenshots. Use it for a sequence you know, such as the next pages of an"
    " installer. It stops at the first step that fails or whose wait does not get what it waits"
    " for, and says which. All steps are checked before the first one runs. The result has each"
    " step's summary and the last screenshot taken.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"steps\":{\"type\":\"array\","
    "\"minItems\":1,\"maxItems\":50,\"description\":\"the calls, in order\",\"items\":{"
    "\"type\":\"object\",\"properties\":{\"tool\":{\"type\":\"string\",\"description\":\"a tool, such"
    " as key, type-text, mouse-click or wait-screen\"},\"args\":{\"type\":\"object\","
    "\"description\":\"its arguments, as in a call of its own\"}},\"required\":[\"tool\"]}}},"
    "\"required\":[\"steps\"]}}");
  return t;
}

} // namespace

Json tool_list()
{
  static Json list;
  static std::once_flag once;

  std::call_once(once, [] {
    list = Json::array();
    for (const auto& s : tool_schemas())
    {
      Json tool;
      std::string err;
      if (Json::parse(s, tool, err))
        list.push(tool);
      else
        fprintf(stderr, "%%MCP-E-SCHEMA: a tool schema does not parse: %s\n", err.c_str());
    }
  });
  return list;
}

} // namespace es40mcp
