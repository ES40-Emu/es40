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
 * The MCP tools: screenshots, waiting for the screen, the keyboard, the
 * mouse, power and several calls in one (steps). Their schemas are in
 * McpSchemas.cpp.
 **/

#include "StdAfx.h"
#include "McpInternal.h"
#include "gui/gui.h"
#include "io/Keyboard.h"
#include "system/System.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

#if !defined(PACKAGE_VERSION)
#define PACKAGE_VERSION "unknown"
#endif

namespace es40mcp
{

namespace
{

// ---------------------------------------------------------------- input

std::mutex g_input_mutex;
std::multiset<u32> g_held_keys;
unsigned g_buttons = 0;
bool g_ptr_known = false;
int g_ptr_x = 0;
int g_ptr_y = 0;

bool have_keyboard(std::string& err)
{
  if (!theKeyboard)
  {
    err = "this es40 has no keyboard controller";
    return false;
  }
  return true;
}

// The keyboard buffer holds 16 bytes and drops what does not fit. One key
// makes at most 6 (Pause), so a key goes in only while 8 are free.
const int kKeyBytes = 8;
// How long a key or a mouse packet waits for the guest to read the last ones,
// and how long once the guest has been seen not to read them
const int kInputWaitMs = 3000;
const int kStalledWaitMs = 40;
std::atomic<bool> g_kbd_stalled{ false };
std::atomic<bool> g_mouse_stalled{ false };

/// Waits while busy() is true; false if the call was cancelled or the guest
/// did not catch up in time. After one such timeout the next waits are short,
/// until the guest reads again.
template <typename Busy>
bool wait_guest(Call& call, std::atomic<bool>& stalled, Busy busy)
{
  const int limit = stalled.load() ? kStalledWaitMs : kInputWaitMs;

  for (int waited = 0; busy(); waited += 2)
  {
    if (!call.sleep(2))
      return false;
    if (waited >= limit)
    {
      stalled = true;
      return false;
    }
  }
  stalled = false;
  return true;
}

/// Waits until the keyboard buffer has room for one more key
bool keyboard_ready(Call& call)
{
  return wait_guest(call, g_kbd_stalled, [] { return theKeyboard->agent_kbd_room() < kKeyBytes; });
}

/// Waits until the last motion or buttons went out in a packet of their own,
/// so a new button state or step does not merge with them
bool mouse_sent(Call& call)
{
  return wait_guest(call, g_mouse_stalled, [] { return theKeyboard->agent_mouse_busy(); });
}

/// The keys one call pressed; it lets go of the rest when the call ends
class KeyPresser
{
public:
  explicit KeyPresser(Call& call) : m_call(call) {}

  ~KeyPresser()
  {
    while (!m_down.empty())
      up(m_down.back());
  }

  /// false if the key could not go in: see keyboard_ready()
  bool down(u32 key)
  {
    if (!keyboard_ready(m_call))
      return false;
    std::lock_guard<std::mutex> lock(g_input_mutex);
    theKeyboard->gen_scancode(key);
    g_held_keys.insert(key);
    m_down.push_back(key);
    return true;
  }

  /// A key goes up even when the buffer stays full or the call was cancelled
  void up(u32 key)
  {
    keyboard_ready(m_call);
    std::lock_guard<std::mutex> lock(g_input_mutex);
    theKeyboard->gen_scancode(key | BX_KEY_RELEASED);
    auto held = g_held_keys.find(key);
    if (held != g_held_keys.end())
      g_held_keys.erase(held);
    auto mine = std::find(m_down.rbegin(), m_down.rend(), key);
    if (mine != m_down.rend())
      m_down.erase(std::next(mine).base());
  }

private:
  Call& m_call;
  std::vector<u32> m_down;
};

/// Moves the mouse by dx, dy screen pixels (down is positive); false if the
/// call was cancelled
bool mouse_send(Call& call, int dx, int dy, int dz)
{
  {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    theKeyboard->agent_mouse_motion(dx, -dy, dz, g_buttons);  // PS/2 Y is positive up
  }
  return mouse_sent(call) || !call.cancelled();
}

/// Presses or lets go of a button; false if the call was cancelled
bool set_button(Call& call, unsigned bit, bool down)
{
  {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (down)
      g_buttons |= bit;
    else
      g_buttons &= ~bit;
    theKeyboard->agent_mouse_motion(0, 0, 0, g_buttons);
  }
  return mouse_sent(call) || !call.cancelled();
}

struct NamedKey
{
  const char* name;
  u32 key;
};

// The key names, and a few aliases
const NamedKey g_key_names[] = {
  { "shift", BX_KEY_SHIFT_L }, { "shift_l", BX_KEY_SHIFT_L }, { "shift_r", BX_KEY_SHIFT_R },
  { "ctrl", BX_KEY_CTRL_L }, { "ctrl_l", BX_KEY_CTRL_L }, { "ctrl_r", BX_KEY_CTRL_R },
  { "control", BX_KEY_CTRL_L },
  { "alt", BX_KEY_ALT_L }, { "alt_l", BX_KEY_ALT_L }, { "alt_r", BX_KEY_ALT_R },
  { "altgr", BX_KEY_ALT_R }, { "altgr_r", BX_KEY_ALT_R },
  { "meta_l", BX_KEY_WIN_L }, { "meta_r", BX_KEY_WIN_R }, { "win", BX_KEY_WIN_L },
  { "menu", BX_KEY_MENU }, { "compose", BX_KEY_MENU },
  { "esc", BX_KEY_ESC }, { "escape", BX_KEY_ESC },
  { "minus", BX_KEY_MINUS }, { "equal", BX_KEY_EQUALS },
  { "backspace", BX_KEY_BACKSPACE }, { "bksp", BX_KEY_BACKSPACE },
  { "tab", BX_KEY_TAB },
  { "bracket_left", BX_KEY_LEFT_BRACKET }, { "bracket_right", BX_KEY_RIGHT_BRACKET },
  { "ret", BX_KEY_ENTER }, { "enter", BX_KEY_ENTER }, { "return", BX_KEY_ENTER },
  { "semicolon", BX_KEY_SEMICOLON }, { "apostrophe", BX_KEY_SINGLE_QUOTE },
  { "grave_accent", BX_KEY_GRAVE }, { "backslash", BX_KEY_BACKSLASH },
  { "comma", BX_KEY_COMMA }, { "dot", BX_KEY_PERIOD }, { "slash", BX_KEY_SLASH },
  { "asterisk", BX_KEY_KP_MULTIPLY },
  { "spc", BX_KEY_SPACE }, { "space", BX_KEY_SPACE },
  { "caps_lock", BX_KEY_CAPS_LOCK }, { "num_lock", BX_KEY_NUM_LOCK },
  { "scroll_lock", BX_KEY_SCRL_LOCK },
  { "kp_divide", BX_KEY_KP_DIVIDE }, { "kp_multiply", BX_KEY_KP_MULTIPLY },
  { "kp_subtract", BX_KEY_KP_SUBTRACT }, { "kp_add", BX_KEY_KP_ADD },
  { "kp_enter", BX_KEY_KP_ENTER }, { "kp_decimal", BX_KEY_KP_DELETE },
  { "kp_0", BX_KEY_KP_INSERT }, { "kp_1", BX_KEY_KP_END }, { "kp_2", BX_KEY_KP_DOWN },
  { "kp_3", BX_KEY_KP_PAGE_DOWN }, { "kp_4", BX_KEY_KP_LEFT }, { "kp_5", BX_KEY_KP_5 },
  { "kp_6", BX_KEY_KP_RIGHT }, { "kp_7", BX_KEY_KP_HOME }, { "kp_8", BX_KEY_KP_UP },
  { "kp_9", BX_KEY_KP_PAGE_UP },
  { "less", BX_KEY_LEFT_BACKSLASH },
  { "print", BX_KEY_PRINT }, { "sysrq", BX_KEY_PRINT }, { "pause", BX_KEY_PAUSE },
  { "home", BX_KEY_HOME }, { "end", BX_KEY_END },
  { "pgup", BX_KEY_PAGE_UP }, { "pageup", BX_KEY_PAGE_UP },
  { "pgdn", BX_KEY_PAGE_DOWN }, { "pagedown", BX_KEY_PAGE_DOWN },
  { "insert", BX_KEY_INSERT }, { "delete", BX_KEY_DELETE }, { "del", BX_KEY_DELETE },
  { "up", BX_KEY_UP }, { "down", BX_KEY_DOWN }, { "left", BX_KEY_LEFT }, { "right", BX_KEY_RIGHT },
};

/// A character on the US keyboard: its key, and whether it needs Shift
bool char_key(char c, u32& key, bool& shift)
{
  static const struct
  {
    char plain;
    char shifted;
    u32 key;
  } pairs[] = {
    { '`', '~', BX_KEY_GRAVE }, { '-', '_', BX_KEY_MINUS }, { '=', '+', BX_KEY_EQUALS },
    { '[', '{', BX_KEY_LEFT_BRACKET }, { ']', '}', BX_KEY_RIGHT_BRACKET },
    { '\\', '|', BX_KEY_BACKSLASH }, { ';', ':', BX_KEY_SEMICOLON },
    { '\'', '"', BX_KEY_SINGLE_QUOTE }, { ',', '<', BX_KEY_COMMA },
    { '.', '>', BX_KEY_PERIOD }, { '/', '?', BX_KEY_SLASH },
  };
  static const char shifted_digits[] = ")!@#$%^&*(";

  shift = false;
  if (c >= 'a' && c <= 'z')
  {
    key = BX_KEY_A + (u32)(c - 'a');
    return true;
  }
  if (c >= 'A' && c <= 'Z')
  {
    key = BX_KEY_A + (u32)(c - 'A');
    shift = true;
    return true;
  }
  if (c >= '0' && c <= '9')
  {
    key = BX_KEY_0 + (u32)(c - '0');
    return true;
  }
  for (int d = 0; d < 10; d++)
  {
    if (c == shifted_digits[d])
    {
      key = BX_KEY_0 + (u32)d;
      shift = true;
      return true;
    }
  }
  for (const auto& p : pairs)
  {
    if (c == p.plain || c == p.shifted)
    {
      key = p.key;
      shift = c == p.shifted;
      return true;
    }
  }
  switch (c)
  {
  case ' ':
    key = BX_KEY_SPACE;
    return true;
  case '\n':
  case '\r':
    key = BX_KEY_ENTER;
    return true;
  case '\t':
    key = BX_KEY_TAB;
    return true;
  default:
    return false;
  }
}

std::string lower(std::string s)
{
  for (auto& c : s)
    c = (char)tolower((unsigned char)c);
  return s;
}

bool lookup_key(const std::string& raw, u32& key, bool& shift)
{
  const std::string n = lower(raw);

  shift = false;
  if (raw.size() == 1)
  {
    if (isalpha((unsigned char)raw[0]))
    {
      key = BX_KEY_A + (u32)(n[0] - 'a');
      return true;
    }
    return char_key(raw[0], key, shift);
  }
  for (const auto& k : g_key_names)
  {
    if (n == k.name)
    {
      key = k.key;
      return true;
    }
  }
  if (n.size() <= 3 && n[0] == 'f' && isdigit((unsigned char)n[1]))
  {
    int f = atoi(n.c_str() + 1);
    if (f >= 1 && f <= 12 && n.find_first_not_of("0123456789", 1) == std::string::npos)
    {
      key = BX_KEY_F1 + (u32)(f - 1);
      return true;
    }
  }
  return false;
}

struct Combo
{
  std::string text;
  std::vector<u32> keys;  // the modifiers, then the key
  int repeat = 1;
};

bool parse_combo(const std::string& item, Combo& c, std::string& err)
{
  std::string s = item;
  std::vector<std::string> parts;
  std::string cur;
  size_t star = s.rfind('*');

  c.text = item;
  if (star != std::string::npos && star > 0 && star + 1 < s.size() &&
    s.find_first_not_of("0123456789", star + 1) == std::string::npos)
  {
    c.repeat = atoi(s.c_str() + star + 1);
    if (c.repeat < 1 || c.repeat > 100)
    {
      err = "'" + item + "': repeat from 1 to 100 times";
      return false;
    }
    s.erase(star);
  }
  for (char ch : s)
  {
    if ((ch == '-' || ch == '+') && !cur.empty())
    {
      parts.push_back(cur);
      cur.clear();
    }
    else
    {
      cur += ch;
    }
  }
  if (!cur.empty())
    parts.push_back(cur);
  if (parts.empty())
  {
    err = "empty key";
    return false;
  }
  for (size_t i = 0; i < parts.size(); i++)
  {
    u32 key;
    bool shift;

    if (!lookup_key(parts[i], key, shift))
    {
      err = "unknown key '" + parts[i] + "' in '" + item + "'";
      return false;
    }
    if (shift && std::find(c.keys.begin(), c.keys.end(), (u32)BX_KEY_SHIFT_L) == c.keys.end() &&
      std::find(c.keys.begin(), c.keys.end(), (u32)BX_KEY_SHIFT_R) == c.keys.end())
      c.keys.push_back(BX_KEY_SHIFT_L);
    c.keys.push_back(key);
  }
  return true;
}

// ---------------------------------------------------------------- arguments

bool arg_int(const Json& args, const char* key, int64_t& out, int64_t lo, int64_t hi, std::string& err)
{
  const Json* v = args.find(key);

  if (!v || v->is_null())
    return true;
  if (!v->is_int())
  {
    err = std::string(key) + " must be a whole number";
    return false;
  }
  out = v->as_int();
  if (out < lo || out > hi)
  {
    err = std::string(key) + " must be from " + std::to_string(lo) + " to " + std::to_string(hi);
    return false;
  }
  return true;
}

bool arg_int(const Json& args, const char* key, int& out, int lo, int hi, std::string& err)
{
  int64_t v = out;
  if (!arg_int(args, key, v, (int64_t)lo, (int64_t)hi, err))
    return false;
  out = (int)v;
  return true;
}

bool arg_bool(const Json& args, const char* key, bool& out, std::string& err)
{
  const Json* v = args.find(key);

  if (!v || v->is_null())
    return true;
  if (!v->is_bool())
  {
    err = std::string(key) + " must be true or false";
    return false;
  }
  out = v->as_bool();
  return true;
}

bool arg_string(const Json& args, const char* key, std::string& out, std::string& err)
{
  const Json* v = args.find(key);

  if (!v || v->is_null())
    return true;
  if (!v->is_string())
  {
    err = std::string(key) + " must be a string";
    return false;
  }
  out = v->as_string();
  return true;
}

bool parse_rect(const Json* j, const char* what, Rect& r, std::string& err)
{
  int64_t v[4];
  const char* names[4] = { "x", "y", "w", "h" };

  if (!j || !j->is_object())
  {
    err = std::string(what) + " must be an object with x, y, w and h";
    return false;
  }
  for (int i = 0; i < 4; i++)
  {
    const Json* f = j->find(names[i]);
    if (!f || !f->is_int())
    {
      err = std::string(what) + " needs a whole number " + names[i];
      return false;
    }
    v[i] = f->as_int();
  }
  if (v[2] <= 0 || v[3] <= 0 || v[2] > 16384 || v[3] > 16384 || std::llabs(v[0]) > 16384 ||
    std::llabs(v[1]) > 16384)
  {
    err = std::string(what) + " has no area, or is too big";
    return false;
  }
  r.x = (int)v[0];
  r.y = (int)v[1];
  r.w = (int)v[2];
  r.h = (int)v[3];
  return true;
}

bool parse_console(const Json& args, unsigned& console, std::string& err)
{
  const Json* c = args.find("console");

  console = 0;
  if (!c || c->is_null())
    return true;
  if (c->is_int() && c->as_int() >= 0 && c->as_int() < 64)
  {
    console = (unsigned)c->as_int();
    return true;
  }
  if (c->is_string() && !c->as_string().empty() &&
    c->as_string().find_first_not_of("0123456789") == std::string::npos)
  {
    console = (unsigned)atoi(c->as_string().c_str());
    return true;
  }
  err = "es40 names consoles by number (0 is the first display); see vm-info";
  return false;
}

// ---------------------------------------------------------------- screenshots

struct Shot
{
  bool want = false;
  bool has_region = false;
  Rect region;
  double scale = 1.0;
  bool pointer = false;
};

bool parse_scale(const Json& args, double& scale, std::string& err)
{
  const Json* s = args.find("scale");

  if (!s || s->is_null())
    return true;
  if (!s->is_number() || s->as_double() < 0.1 || s->as_double() > 2.0)
  {
    err = "scale must be from 0.1 to 2.0";
    return false;
  }
  scale = s->as_double();
  return true;
}

/// screenshot: true, false or { region, scale }
bool parse_shot(const Json* j, bool def, Shot& s, std::string& err)
{
  s.want = def;
  if (!j || j->is_null())
    return true;
  if (j->is_bool())
  {
    s.want = j->as_bool();
    return true;
  }
  if (!j->is_object())
  {
    err = "screenshot must be true, false or an object with region and scale";
    return false;
  }
  s.want = true;
  if (const Json* r = j->find("region"))
  {
    if (!parse_rect(r, "region", s.region, err))
      return false;
    s.has_region = true;
  }
  return parse_scale(*j, s.scale, err);
}

std::string fmt_secs(double s)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f s", s);
  return buf;
}

/// Puts a screenshot into r; returns a line that describes it
std::string take_shot(const Shot& s, unsigned console, ToolResult& r)
{
  Frame f;
  int px, py;
  bool known;

  if (!latest_frame(console, f))
    return console < console_count() ? "console " + std::to_string(console) + " shows nothing yet"
    : "no display has shown anything yet";
  {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    px = g_ptr_x;
    py = g_ptr_y;
    known = g_ptr_known;
  }

  Image img = (s.pointer && known) ? mark(f.img, px, py) : std::move(f.img);
  const unsigned full_w = img.w, full_h = img.h;
  std::string line = "Screen " + std::to_string(full_w) + "x" + std::to_string(full_h);

  if (s.has_region)
  {
    img = crop(img, s.region);
    if (img.empty())
      return line + "; the region is off the screen, so no screenshot";
    line += ", region " + std::to_string(std::max(s.region.x, 0)) + "," +
      std::to_string(std::max(s.region.y, 0)) + " " + std::to_string(img.w) + "x" +
      std::to_string(img.h);
  }
  if (s.scale != 1.0)
  {
    char buf[64];
    img = scale(img, s.scale);
    snprintf(buf, sizeof(buf), ", scaled by %.2g: divide image coordinates by %.2g", s.scale,
      s.scale);
    line += buf;
  }
  if (s.pointer)
  {
    line += known ? ", pointer marked at " + std::to_string(px) + "," + std::to_string(py)
      : ", pointer position not known yet";
  }
  r.png = png(img);
  return line + ".";
}

// ---------------------------------------------------------------- waiting

struct WaitSpec
{
  std::string until = "none";
  int stable_ms = 2000;
  int timeout_ms = 60000;
  int min_pixels = 64;
  bool has_region = false;
  Rect region;
  std::vector<Rect> ignore;
  bool ignore_animation = false;
  bool ignore_blink = true;
  int px = 0;
  int py = 0;
  uint32_t rgb = 0;
  int tolerance = 16;
  unsigned console = 0;
};

const int kTile = 32;
// A cursor that blinks goes back to how it looked within this many seconds
const double kBlinkSecs = 0.7;

class Waiter
{
public:
  Waiter(const WaitSpec& spec, Call& call) : m_spec(spec), m_call(call) {}

  /// Waits; true if what it waited for happened. ref is the screen before
  /// the input, for "change", or null for the screen now.
  bool run(const Image* ref, std::string& summary)
  {
    Frame start;
    auto t0 = std::chrono::steady_clock::now();
    auto now_s = [&] {
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    const double timeout = m_spec.timeout_ms / 1000.0;
    const double stable = m_spec.stable_ms / 1000.0;
    const std::string& until = m_spec.until;

    while (!latest_frame(m_spec.console, start))
    {
      if (now_s() >= timeout || m_call.cancelled())
      {
        summary = "Nothing was on the screen in " + fmt_secs(now_s()) + ".";
        return false;
      }
      wait_frame(m_spec.console, 0, 100);
    }

    Image base = ref && !ref->empty() ? *ref : start.img;
    Image prev = start.img;
    Image prev2;
    uint64_t seq = start.seq;
    bool changed = until == "stable";
    double changed_at = 0;
    double last_change = 0;
    double small_since = -1;  // since when only a cursor-sized area has changed

    for (;;)
    {
      const double t = now_s();

      if (until == "pixel")
      {
        if (m_spec.px >= 0 && m_spec.py >= 0 && m_spec.px < (int)prev.w && m_spec.py < (int)prev.h &&
          close_to(prev.at((unsigned)m_spec.px, (unsigned)m_spec.py)))
        {
          summary = "The pixel at " + std::to_string(m_spec.px) + "," + std::to_string(m_spec.py) +
            " got the colour after " + fmt_secs(t) + ".";
          return true;
        }
      }
      else if (!changed)
      {
        Diff d = diff(base, prev, false);
        // A cursor that blinks is known as one only once it blinks back, so
        // a cursor-sized change counts only after it stayed that long
        const bool small = m_spec.ignore_blink && d.count >= m_spec.min_pixels && blink_sized(d);
        if (!small)
          small_since = -1;
        else if (small_since < 0)
          small_since = t;
        if (d.count >= m_spec.min_pixels && (!small || t - small_since >= kBlinkSecs))
        {
          changed = true;
          changed_at = small ? small_since : t;
          last_change = std::max(last_change, changed_at);
          if (until == "change")
          {
            summary = "The screen changed after " + fmt_secs(changed_at) + " (" + describe(d) + ").";
            return true;
          }
        }
      }
      if (changed && until != "change" && until != "pixel" && t - last_change >= stable)
      {
        if (until == "stable")
          summary = "The screen was stable after " + fmt_secs(t) + ".";
        else
          summary = "The screen changed after " + fmt_secs(changed_at) + " and was stable after " +
          fmt_secs(t) + ".";
        return true;
      }
      if (t >= timeout)
      {
        if (until == "pixel")
          summary = "The pixel did not get the colour in " + fmt_secs(t) + ".";
        else if (!changed)
          summary = "The screen did not change in " + fmt_secs(t) + ".";
        else
          summary = "The screen was not stable in " + fmt_secs(t) + ": it last changed " +
          fmt_secs(t - last_change) + " before the end.";
        return false;
      }
      if (m_call.cancelled())
      {
        summary = "Cancelled.";
        return false;
      }
      m_call.progress("waiting for the screen (" + until + "), " + fmt_secs(t));

      double left_ms = (timeout - t) * 1000.0;
      wait_frame(m_spec.console, seq, (int)std::max(1.0, std::min(50.0, left_ms)));
      Frame f;
      if (!latest_frame(m_spec.console, f) || f.seq == seq)
        continue;

      Diff d = diff(prev, f.img, true);
      if (d.count >= m_spec.min_pixels && m_spec.ignore_blink && is_blink(d, prev2, f.img))
        d.count = 0;
      if (d.count >= m_spec.min_pixels)
      {
        last_change = now_s();
        if (m_spec.ignore_animation)
          note_animation(d, last_change);
      }
      prev2 = std::move(prev);
      prev = std::move(f.img);
      seq = f.seq;
    }
  }

private:
  struct Diff
  {
    long count = 0;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = -1, y1 = -1;
    std::vector<unsigned> tiles;
  };

  // A tile's changes, each within 2.5 s of the one before
  struct Run
  {
    double first = 0, last = 0;
    int count = 0;
  };

  const WaitSpec& m_spec;
  Call& m_call;
  unsigned m_w = 0, m_h = 0, m_tiles_x = 0;
  std::vector<uint8_t> m_animated;
  std::vector<Run> m_runs;
  std::vector<Rect> m_blinks;

  bool close_to(uint32_t p) const
  {
    const int tol = m_spec.tolerance;
    return std::abs((int)((p >> 16) & 0xff) - (int)((m_spec.rgb >> 16) & 0xff)) <= tol &&
      std::abs((int)((p >> 8) & 0xff) - (int)((m_spec.rgb >> 8) & 0xff)) <= tol &&
      std::abs((int)(p & 0xff) - (int)(m_spec.rgb & 0xff)) <= tol;
  }

  void resize(unsigned w, unsigned h)
  {
    if (w == m_w && h == m_h)
      return;
    m_w = w;
    m_h = h;
    m_tiles_x = (w + kTile - 1) / kTile;
    m_animated.assign((size_t)m_tiles_x * ((h + kTile - 1) / kTile), 0);
    m_runs.assign(m_animated.size(), Run());
    m_blinks.clear();
  }

  bool excluded(int x, int y) const
  {
    if (m_spec.has_region && !m_spec.region.contains(x, y))
      return true;
    for (const auto& r : m_spec.ignore)
    {
      if (r.contains(x, y))
        return true;
    }
    for (const auto& r : m_blinks)
    {
      if (r.contains(x, y))
        return true;
    }
    return m_animated[(size_t)(y / kTile) * m_tiles_x + (size_t)(x / kTile)] != 0;
  }

  Diff diff(const Image& a, const Image& b, bool tiles)
  {
    Diff d;

    if (a.w != b.w || a.h != b.h)
    {
      // a new screen mode counts as a change of the whole screen
      resize(b.w, b.h);
      d.count = (long)b.w * b.h;
      d.x0 = 0;
      d.y0 = 0;
      d.x1 = (int)b.w - 1;
      d.y1 = (int)b.h - 1;
      return d;
    }
    resize(b.w, b.h);

    std::vector<uint8_t> marks;
    if (tiles)
      marks.assign(m_animated.size(), 0);
    for (unsigned y = 0; y < b.h; y++)
    {
      const uint32_t* ra = &a.px[(size_t)y * a.w];
      const uint32_t* rb = &b.px[(size_t)y * b.w];
      if (!memcmp(ra, rb, (size_t)b.w * sizeof(uint32_t)))
        continue;
      for (unsigned x = 0; x < b.w; x++)
      {
        if (((ra[x] ^ rb[x]) & 0xffffff) == 0 || excluded((int)x, (int)y))
          continue;
        d.count++;
        d.x0 = std::min(d.x0, (int)x);
        d.y0 = std::min(d.y0, (int)y);
        d.x1 = std::max(d.x1, (int)x);
        d.y1 = std::max(d.y1, (int)y);
        if (tiles)
          marks[(size_t)(y / kTile) * m_tiles_x + x / kTile] = 1;
      }
    }
    for (size_t i = 0; i < marks.size(); i++)
    {
      if (marks[i])
        d.tiles.push_back((unsigned)i);
    }
    return d;
  }

  static std::string describe(const Diff& d)
  {
    return std::to_string(d.count) + " pixels in " + std::to_string(d.x0) + "," +
      std::to_string(d.y0) + " " + std::to_string(d.x1 - d.x0 + 1) + "x" +
      std::to_string(d.y1 - d.y0 + 1);
  }

  /// A change no bigger than a cursor
  static bool blink_sized(const Diff& d)
  {
    return d.x1 - d.x0 + 1 <= 24 && d.y1 - d.y0 + 1 <= 32;
  }

  /// A small area that went back to how it looked two frames ago: a blinking cursor
  bool is_blink(const Diff& d, const Image& before, const Image& now)
  {
    const int w = d.x1 - d.x0 + 1, h = d.y1 - d.y0 + 1;

    if (!blink_sized(d) || before.w != now.w || before.h != now.h)
      return false;
    for (int y = d.y0; y <= d.y1; y++)
    {
      for (int x = d.x0; x <= d.x1; x++)
      {
        if (((before.at((unsigned)x, (unsigned)y) ^ now.at((unsigned)x, (unsigned)y)) & 0xffffff))
          return false;
      }
    }
    Rect r;
    r.x = d.x0 - 1;
    r.y = d.y0 - 1;
    r.w = w + 2;
    r.h = h + 2;
    m_blinks.push_back(r);
    return true;
  }

  /// An area that changed 3 or more times, each within 2.5 s of the last,
  /// over most of a second, is an animation: it is skipped from then on
  void note_animation(const Diff& d, double t)
  {
    for (unsigned i : d.tiles)
    {
      Run& run = m_runs[i];
      if (!run.count || t - run.last > 2.5)
      {
        run.first = t;
        run.count = 0;
      }
      run.last = t;
      run.count++;
      if (run.count >= 3 && run.last - run.first >= 0.6)
        m_animated[i] = 1;
    }
  }
};

bool parse_wait(const Json& args, WaitSpec& w, std::string& err)
{
  const Json* v;

  if (!arg_string(args, "until", w.until, err) || !arg_int(args, "stable-ms", w.stable_ms, 0, kMaxWaitMs, err) ||
    !arg_int(args, "timeout-ms", w.timeout_ms, 0, kMaxWaitMs, err) ||
    !arg_int(args, "min-pixels", w.min_pixels, 1, 100000000, err) ||
    !arg_bool(args, "ignore-animation", w.ignore_animation, err) ||
    !arg_bool(args, "ignore-blink", w.ignore_blink, err) || !parse_console(args, w.console, err))
    return false;
  if (w.until != "none" && w.until != "change" && w.until != "stable" &&
    w.until != "change-then-stable" && w.until != "pixel" && w.until != "text")
  {
    err = "until must be none, change, stable, change-then-stable or pixel";
    return false;
  }
  if (w.until == "text")
  {
    err = "es40 cannot read the screen as text yet: wait for change, stable or pixel, and read the"
      " screenshot";
    return false;
  }
  if ((v = args.find("region")) && !v->is_null())
  {
    if (!parse_rect(v, "region", w.region, err))
      return false;
    w.has_region = true;
  }
  if ((v = args.find("ignore")) && !v->is_null())
  {
    if (!v->is_array())
    {
      err = "ignore must be a list of rectangles";
      return false;
    }
    for (const auto& r : v->items())
    {
      Rect rect;
      if (!parse_rect(&r, "an ignore rectangle", rect, err))
        return false;
      w.ignore.push_back(rect);
    }
  }
  if (w.until == "pixel")
  {
    const Json* p = args.find("pixel");
    int64_t x, y, tol = 16;
    std::string rgb;

    if (!p || !p->is_object() || !p->find("x") || !p->find("y") || !p->find("rgb"))
    {
      err = "until pixel needs pixel with x, y and rgb";
      return false;
    }
    x = p->find("x")->as_int(-1);
    y = p->find("y")->as_int(-1);
    rgb = p->find("rgb")->as_string();
    if (!arg_int(*p, "tolerance", tol, 0, 255, err))
      return false;
    if (rgb.size() != 7 || rgb[0] != '#' || rgb.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
    {
      err = "rgb must look like #rrggbb";
      return false;
    }
    w.px = (int)x;
    w.py = (int)y;
    w.rgb = (uint32_t)strtoul(rgb.c_str() + 1, nullptr, 16);
    w.tolerance = (int)tol;
  }
  return true;
}

// ---------------------------------------------------------------- "then"

struct Then
{
  WaitSpec wait;
  int delay_ms = 0;
  Shot shot;
};

bool parse_then(const Json& args, Then& t, std::string& err)
{
  const Json* j = args.find("then");

  t.wait.until = "none";
  t.wait.stable_ms = 1000;
  t.wait.timeout_ms = 10000;
  if (!j || j->is_null())
    return true;
  if (!j->is_object())
  {
    err = "then must be an object";
    return false;
  }
  if (!arg_string(*j, "wait", t.wait.until, err) || !arg_int(*j, "delay-ms", t.delay_ms, 0, kMaxWaitMs, err) ||
    !arg_int(*j, "stable-ms", t.wait.stable_ms, 0, kMaxWaitMs, err) ||
    !arg_int(*j, "timeout-ms", t.wait.timeout_ms, 0, kMaxWaitMs, err) ||
    !arg_bool(*j, "ignore-animation", t.wait.ignore_animation, err) ||
    !parse_shot(j->find("screenshot"), false, t.shot, err))
    return false;
  if (t.wait.until == "pixel")
  {
    err = "then cannot wait for a pixel; use wait-screen";
    return false;
  }
  if (t.wait.until == "text")
  {
    err = "es40 cannot read the screen as text yet: wait for change or stable";
    return false;
  }
  if (t.wait.until != "none" && t.wait.until != "change" && t.wait.until != "stable" &&
    t.wait.until != "change-then-stable")
  {
    err = "then.wait must be none, change, stable or change-then-stable";
    return false;
  }
  return true;
}

/// The screen before the input, for a "change" wait after it
Image screen_before(const Then& t)
{
  Frame f;
  if (t.wait.until != "none" && latest_frame(t.wait.console, f))
    return f.img;
  return Image();
}

/// The screen once it shows the pointer where a move left it: no new picture
/// for 150 ms, or 1.5 s at most
Image screen_after_move(Call& call, const Then& t)
{
  Frame f;

  if (t.wait.until == "none" || !latest_frame(t.wait.console, f))
    return Image();
  for (int i = 0; i < 10 && !call.cancelled() && wait_frame(t.wait.console, f.seq, 150); i++)
    latest_frame(t.wait.console, f);
  return f.img;
}

/// Runs the wait and the screenshot of "then"; false if the wait was not met
bool finish_then(Call& call, const Then& t, const Image& before, ToolResult& r)
{
  bool met = true;

  if (t.delay_ms && !call.sleep(t.delay_ms))
  {
    r.text += " Cancelled.";
    r.error = true;
    return false;
  }
  if (t.wait.until != "none")
  {
    std::string summary;
    Waiter w(t.wait, call);
    met = w.run(&before, summary);
    r.text += " " + summary;
    if (!met)
      r.error = true;
  }
  if (t.shot.want)
    r.text += " " + take_shot(t.shot, t.wait.console, r);
  return met;
}

// ---------------------------------------------------------------- the mouse

bool screen_size(unsigned& w, unsigned& h)
{
  Frame f;
  if (!latest_frame(0, f))
  {
    w = 1024;
    h = 768;
    return false;
  }
  w = f.img.w;
  h = f.img.h;
  return true;
}

bool move_steps(Call& call, int dx, int dy, int step, int delay_ms)
{
  while (dx || dy)
  {
    int sx = std::max(-step, std::min(step, dx));
    int sy = std::max(-step, std::min(step, dy));
    const double start = call.elapsed();

    // each step goes out in a packet of its own: two in one packet could
    // cross the guest's acceleration threshold
    if (!mouse_send(call, sx, sy, 0))
      return false;
    dx -= sx;
    dy -= sy;
    const int left = delay_ms - (int)((call.elapsed() - start) * 1000.0);
    if (left > 0 && !call.sleep(left))
      return false;
  }
  return true;
}

/// Pushes the pointer far into the top-left corner, where the guest stops it
bool home_pointer(Call& call)
{
  unsigned w, h;

  screen_size(w, h);
  const int steps = (int)(std::max(w, h) / 40) * 2 + 10;
  for (int i = 0; i < steps; i++)
  {
    if (!mouse_send(call, -40, -40, 0))
      return false;
  }
  if (!call.sleep(100))
    return false;
  std::lock_guard<std::mutex> lock(g_input_mutex);
  g_ptr_x = 0;
  g_ptr_y = 0;
  g_ptr_known = true;
  return true;
}

struct MoveSpec
{
  bool has_xy = false;
  bool has_dxy = false;
  int x = 0, y = 0, dx = 0, dy = 0;
  std::string home = "auto";
  int step = 4;
  int step_delay = 20;
};

bool parse_move(const Json& args, MoveSpec& m, bool need_target, std::string& err)
{
  const bool x = args.has("x"), y = args.has("y"), dx = args.has("dx"), dy = args.has("dy");

  if (!arg_int(args, "x", m.x, -16384, 16384, err) || !arg_int(args, "y", m.y, -16384, 16384, err) ||
    !arg_int(args, "dx", m.dx, -16384, 16384, err) || !arg_int(args, "dy", m.dy, -16384, 16384, err) ||
    !arg_string(args, "home", m.home, err) || !arg_int(args, "step-px", m.step, 1, 64, err) ||
    !arg_int(args, "step-delay-ms", m.step_delay, 0, 1000, err))
    return false;
  if (x != y || dx != dy || ((x || y) && (dx || dy)))
  {
    err = "give either x and y, or dx and dy";
    return false;
  }
  m.has_xy = x;
  m.has_dxy = dx;
  if (need_target && !m.has_xy && !m.has_dxy)
  {
    err = "give x and y, or dx and dy";
    return false;
  }
  if (m.home != "auto" && m.home != "always" && m.home != "never")
  {
    err = "home must be auto, always or never";
    return false;
  }
  return true;
}

/// Moves the pointer; adds what it did to text
bool do_move(Call& call, const MoveSpec& m, std::string& text, std::string& err)
{
  unsigned w, h;
  bool known;
  int px, py;

  screen_size(w, h);
  if (m.has_xy)
  {
    {
      std::lock_guard<std::mutex> lock(g_input_mutex);
      known = g_ptr_known;
    }
    if (m.home == "always" || (m.home == "auto" && !known))
    {
      if (!home_pointer(call))
      {
        err = "cancelled";
        return false;
      }
      text += "Homed the pointer to 0,0. ";
    }
    else if (!known)
    {
      err = "the pointer position is not known; let es40 home it (home auto)";
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(g_input_mutex);
      px = g_ptr_x;
      py = g_ptr_y;
    }
    int tx = std::max(0, std::min(m.x, (int)w - 1));
    int ty = std::max(0, std::min(m.y, (int)h - 1));
    if (!move_steps(call, tx - px, ty - py, m.step, m.step_delay))
    {
      err = "cancelled";
      return false;
    }
    std::lock_guard<std::mutex> lock(g_input_mutex);
    g_ptr_x = tx;
    g_ptr_y = ty;
    text += "Moved the pointer to " + std::to_string(tx) + "," + std::to_string(ty) + ".";
  }
  else if (m.has_dxy)
  {
    if (!move_steps(call, m.dx, m.dy, m.step, m.step_delay))
    {
      err = "cancelled";
      return false;
    }
    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (g_ptr_known)
    {
      g_ptr_x = std::max(0, std::min(g_ptr_x + m.dx, (int)w - 1));
      g_ptr_y = std::max(0, std::min(g_ptr_y + m.dy, (int)h - 1));
      text += "Moved the pointer by " + std::to_string(m.dx) + "," + std::to_string(m.dy) +
        " to about " + std::to_string(g_ptr_x) + "," + std::to_string(g_ptr_y) + ".";
    }
    else
    {
      text += "Moved the pointer by " + std::to_string(m.dx) + "," + std::to_string(m.dy) + ".";
    }
  }
  return true;
}

bool parse_button(const Json& args, unsigned& bit, int& wheel, std::string& name, std::string& err)
{
  name = "left";
  if (!arg_string(args, "button", name, err))
    return false;
  bit = 0;
  wheel = 0;
  if (name == "left")
    bit = 1;
  else if (name == "right")
    bit = 2;
  else if (name == "middle")
    bit = 4;
  else if (name == "wheel-up")
    wheel = 1;
  else if (name == "wheel-down")
    wheel = -1;
  else
  {
    err = "the es40 PS/2 mouse has left, middle, right, wheel-up and wheel-down";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- tools

ToolResult fail(const std::string& text)
{
  ToolResult r;
  r.text = text;
  r.error = true;
  return r;
}

/// A key that could not go in: the call was cancelled, or the guest stopped
/// reading the keyboard after `done`
ToolResult not_typed(Call& call, const std::string& done)
{
  if (call.cancelled())
    return fail("Cancelled.");
  return fail("The guest is not reading the keyboard (its buffer is full), so the rest was not"
    " sent. Sent before that: " + (done.empty() ? std::string("nothing") : done) + ".");
}

ToolResult tool_vm_info(Call&, const Json&)
{
  const VmInfo& vm = vm_info();
  ToolResult r;
  std::string state = "running";
  bool known;
  int px, py;

  if (theSystem && theSystem->IsShutdownRequested())
    state = "stopping";
  else if (theSystem && theSystem->IsSystemResetRequested())
    state = "resetting";
  {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    known = g_ptr_known;
    px = g_ptr_x;
    py = g_ptr_y;
  }

  r.text = "VM " + vm.name + " (pid " + std::to_string(vm.pid) + "), started " + vm.started + ".\n";
  r.text += "Machine: AlphaServer ES40, emulated by es40 " PACKAGE_VERSION ".\n";
  r.text += "State: " + state + ".\n";
  r.text += "Folder: " + vm.cwd + "\n";
  r.text += "Configuration: " + vm.config + " (it lists the disks, CD-ROMs, serial ports and network).\n";
  const unsigned n = console_count();
  if (!n)
    r.text += "Consoles: none has shown anything yet.\n";
  for (unsigned i = 0; i < n; i++)
  {
    Frame f;
    if (latest_frame(i, f))
      r.text += "Console " + std::to_string(i) + ": " + std::to_string(f.img.w) + "x" +
      std::to_string(f.img.h) + (i ? "" : " (the default)") + ".\n";
  }
  r.text += "Keyboard: PS/2, US layout.\n";
  r.text += "Mouse: PS/2, relative; ";
  r.text += known ? "es40 thinks the pointer is at " + std::to_string(px) + "," + std::to_string(py) + ".\n"
    : "pointer position not known yet (the next move to x, y homes it first).\n";
  r.text += "MCP socket: " + vm.socket;
  return r;
}

ToolResult tool_screenshot(Call&, const Json& args)
{
  ToolResult r;
  Shot s;
  unsigned console;
  std::string err;

  s.want = true;
  if (!parse_console(args, console, err) || !parse_scale(args, s.scale, err) ||
    !arg_bool(args, "pointer", s.pointer, err))
    return fail(err);
  if (const Json* reg = args.find("region"))
  {
    if (!reg->is_null())
    {
      if (!parse_rect(reg, "region", s.region, err))
        return fail(err);
      s.has_region = true;
    }
  }
  r.text = take_shot(s, console, r);
  if (r.png.empty())
    r.error = true;
  return r;
}

ToolResult tool_wait_screen(Call& call, const Json& args)
{
  ToolResult r;
  WaitSpec w;
  Shot shot;
  std::string err;

  if (!args.has("until"))
    return fail("until is needed: none, change, stable, change-then-stable or pixel");
  if (!parse_wait(args, w, err) || !parse_shot(args.find("screenshot"), true, shot, err))
    return fail(err);
  if (w.until != "none")
  {
    Waiter waiter(w, call);
    std::string summary;
    if (!waiter.run(nullptr, summary))
      r.error = true;
    r.text = summary;
  }
  if (shot.want)
    r.text += (r.text.empty() ? "" : " ") + take_shot(shot, w.console, r);
  return r;
}

ToolResult tool_key(Call& call, const Json& args)
{
  ToolResult r;
  std::vector<Combo> combos;
  Then then;
  int delay = 80, hold = 40, lead = 20;
  std::string err;
  const Json* keys = args.find("keys");

  if (!have_keyboard(err))
    return fail(err);
  if (!keys || !keys->is_array() || keys->size() == 0 || keys->size() > 200)
    return fail("keys must be a list of 1 to 200 keys or combinations");
  if (!arg_int(args, "delay-ms", delay, 0, 10000, err) || !arg_int(args, "hold-ms", hold, 0, 10000, err) ||
    !arg_int(args, "modifier-lead-ms", lead, 0, 10000, err) || !parse_then(args, then, err))
    return fail(err);
  for (const auto& k : keys->items())
  {
    Combo c;
    if (!k.is_string() || !parse_combo(k.as_string(), c, err))
      return fail(err.empty() ? "each key must be a string" : err);
    combos.push_back(c);
  }

  const Image before = screen_before(then);
  KeyPresser keyboard(call);
  std::string pressed;
  for (const auto& c : combos)
  {
    for (int n = 0; n < c.repeat; n++)
    {
      for (size_t i = 0; i + 1 < c.keys.size(); i++)
      {
        if (!keyboard.down(c.keys[i]))
          return not_typed(call, pressed);
        if (!call.sleep(lead))
          return fail("Cancelled.");
      }
      if (!keyboard.down(c.keys.back()))
        return not_typed(call, pressed);
      if (!call.sleep(hold))
        return fail("Cancelled.");
      for (size_t i = c.keys.size(); i-- > 0;)
        keyboard.up(c.keys[i]);
      if (!call.sleep(delay))
        return fail("Cancelled.");
    }
    pressed += (pressed.empty() ? "" : ", ") + c.text;
  }
  r.text = "Pressed " + pressed + ".";
  finish_then(call, then, before, r);
  return r;
}

ToolResult tool_type_text(Call& call, const Json& args)
{
  ToolResult r;
  Then then;
  int char_delay = 30, hold = 20, lead = 20;
  std::string text, layout = "en-us", err;
  std::vector<std::pair<u32, bool>> strokes;

  if (!have_keyboard(err))
    return fail(err);
  if (!args.has("text"))
    return fail("text is needed");
  if (!arg_string(args, "text", text, err) || !arg_string(args, "layout", layout, err) ||
    !arg_int(args, "char-delay-ms", char_delay, 0, 10000, err) ||
    !arg_int(args, "hold-ms", hold, 0, 10000, err) ||
    !arg_int(args, "modifier-lead-ms", lead, 0, 10000, err) || !parse_then(args, then, err))
    return fail(err);
  if (layout != "en-us" && layout != "us")
    return fail("es40 types with the US layout only (en-us)");
  if (text.size() > 8192)
    return fail("text is too long; type at most 8192 characters at a time");
  for (size_t i = 0; i < text.size(); i++)
  {
    u32 key;
    bool shift;

    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
      continue;
    if (!char_key(text[i], key, shift))
    {
      char buf[96];
      snprintf(buf, sizeof(buf), "character %zu (byte 0x%02x) cannot be typed on a US keyboard; nothing was typed",
        i + 1, (unsigned char)text[i]);
      return fail(buf);
    }
    strokes.emplace_back(key, shift);
  }

  const Image before = screen_before(then);
  KeyPresser keyboard(call);
  size_t typed = 0;
  for (const auto& s : strokes)
  {
    if (s.second)
    {
      if (!keyboard.down(BX_KEY_SHIFT_L))
        return not_typed(call, std::to_string(typed) + " characters");
      if (!call.sleep(lead))
        return fail("Cancelled.");
    }
    if (!keyboard.down(s.first))
      return not_typed(call, std::to_string(typed) + " characters");
    typed++;
    if (!call.sleep(hold))
      return fail("Cancelled.");
    keyboard.up(s.first);
    if (s.second)
      keyboard.up(BX_KEY_SHIFT_L);
    if (!call.sleep(char_delay))
      return fail("Cancelled.");
  }
  r.text = "Typed " + std::to_string(strokes.size()) + " characters.";
  finish_then(call, then, before, r);
  return r;
}

ToolResult tool_mouse_move(Call& call, const Json& args)
{
  ToolResult r;
  MoveSpec m;
  Then then;
  std::string err;

  if (!have_keyboard(err) || !parse_move(args, m, true, err) || !parse_then(args, then, err))
    return fail(err);
  const Image before = screen_before(then);
  if (!do_move(call, m, r.text, err))
    return fail(err);
  finish_then(call, then, before, r);
  return r;
}

ToolResult tool_mouse_click(Call& call, const Json& args)
{
  ToolResult r;
  MoveSpec m;
  Then then;
  std::string err, name;
  unsigned bit;
  int wheel, count = 1, hold = 60, interval = 100;

  if (!have_keyboard(err) || !parse_move(args, m, false, err) ||
    !parse_button(args, bit, wheel, name, err) || !arg_int(args, "count", count, 1, 3, err) ||
    !arg_int(args, "hold-ms", hold, 0, 10000, err) ||
    !arg_int(args, "interval-ms", interval, 0, 10000, err) || !parse_then(args, then, err))
    return fail(err);
  if (m.has_dxy)
    return fail("mouse-click moves to x, y; use mouse-move for dx, dy");

  if (m.has_xy && !do_move(call, m, r.text, err))
    return fail(err);
  // after the move: a "change" wait must see the click, not the pointer moving
  const Image before = m.has_xy ? screen_after_move(call, then) : screen_before(then);
  for (int i = 0; i < count; i++)
  {
    if (i && !call.sleep(interval))
      return fail("Cancelled.");
    if (wheel)
    {
      if (!mouse_send(call, 0, 0, wheel))
        return fail("Cancelled.");
      continue;
    }
    bool ok = set_button(call, bit, true) && call.sleep(hold);
    set_button(call, bit, false);  // it goes up after a cancel too
    if (!ok)
      return fail("Cancelled.");
  }
  r.text += (r.text.empty() ? "" : " ") + std::string(count == 1 ? "Clicked " : count == 2 ? "Double-clicked " : "Triple-clicked ") + name + ".";
  finish_then(call, then, before, r);
  return r;
}

ToolResult tool_mouse_button(Call& call, const Json& args)
{
  ToolResult r;
  Then then;
  std::string err, name, action;
  unsigned bit;
  int wheel;

  if (!have_keyboard(err) || !parse_button(args, bit, wheel, name, err) ||
    !arg_string(args, "action", action, err) || !parse_then(args, then, err))
    return fail(err);
  if (action != "down" && action != "up")
    return fail("action must be down or up");
  if (wheel)
    return fail("a wheel has no down and up; use mouse-click with wheel-up or wheel-down");

  const Image before = screen_before(then);
  set_button(call, bit, action == "down");
  r.text = (action == "down" ? "Pressed " : "Released ") + name + ".";
  finish_then(call, then, before, r);
  return r;
}

ToolResult tool_vm_power(Call&, const Json& args)
{
  ToolResult r;
  std::string action, err;

  if (!arg_string(args, "action", action, err))
    return fail(err);
  if (!theSystem)
    return fail("the system is not there");
  if (action == "quit")
  {
    theSystem->RequestShutdown();
    r.text = "es40 is quitting. It saves the flash, DPR and TOY files first. Use vm-wait until gone"
      " before you copy the disks.";
  }
  else if (action == "reset")
  {
    theSystem->RequestSystemReset();
    r.text = "The system is resetting.";
  }
  else if (action == "pause" || action == "resume")
  {
    return fail("es40 cannot pause yet");
  }
  else if (action == "powerdown")
  {
    return fail("the ES40 has no power button the guest can see; shut the guest down from inside,"
      " then use quit");
  }
  else
  {
    return fail("action must be quit or reset (pause, resume and powerdown are not in es40)");
  }
  return r;
}

ToolResult tool_steps(Call& call, const Json& args);

struct ToolEntry
{
  const char* name;
  ToolResult (*fn)(Call&, const Json&);
  bool in_steps;
};

const ToolEntry g_tools[] = {
  { "vm-info", tool_vm_info, true },
  { "screenshot", tool_screenshot, true },
  { "wait-screen", tool_wait_screen, true },
  { "key", tool_key, true },
  { "type-text", tool_type_text, true },
  { "mouse-move", tool_mouse_move, true },
  { "mouse-click", tool_mouse_click, true },
  { "mouse-button", tool_mouse_button, true },
  { "vm-power", tool_vm_power, true },
  { "steps", tool_steps, false },
};

const ToolEntry* find_tool(const std::string& name)
{
  for (const auto& t : g_tools)
  {
    if (name == t.name)
      return &t;
  }
  return nullptr;
}

std::string first_line(const std::string& s)
{
  size_t nl = s.find('\n');
  return nl == std::string::npos ? s : s.substr(0, nl);
}

ToolResult tool_steps(Call& call, const Json& args)
{
  ToolResult r;
  const Json* steps = args.find("steps");
  std::string last_png;

  if (!steps || !steps->is_array() || steps->size() == 0 || steps->size() > 50)
    return fail("steps must be a list of 1 to 50 calls");
  for (size_t i = 0; i < steps->size(); i++)
  {
    const Json& s = (*steps)[i];
    const Json* tool = s.find("tool");
    const Json* a = s.find("args");
    const ToolEntry* t = tool ? find_tool(tool->as_string()) : nullptr;

    if (!s.is_object() || !tool || !tool->is_string())
      return fail("step " + std::to_string(i + 1) + " needs a tool name");
    if (!t || !t->in_steps)
      return fail("step " + std::to_string(i + 1) + ": es40 has no tool " + tool->as_string() +
        " for steps");
    if (a && !a->is_null() && !a->is_object())
      return fail("step " + std::to_string(i + 1) + ": args must be an object");
  }

  r.text = "";
  for (size_t i = 0; i < steps->size(); i++)
  {
    const Json& s = (*steps)[i];
    const Json* a = s.find("args");
    const std::string name = s.find("tool")->as_string();
    ToolResult one = find_tool(name)->fn(call, a && a->is_object() ? *a : Json::object());

    if (!one.png.empty())
      last_png = one.png;
    r.text += std::to_string(i + 1) + ". " + name + ": " + first_line(one.text) + "\n";
    if (one.error || call.cancelled())
    {
      r.error = true;
      r.text = "Stopped at step " + std::to_string(i + 1) + " of " +
        std::to_string(steps->size()) + " (" + name + ").\n" + r.text;
      r.png = last_png;
      return r;
    }
    if (call.elapsed() > kMaxWaitMs / 1000.0)
    {
      r.error = true;
      r.text = "Stopped after step " + std::to_string(i + 1) + ": the steps ran 25 minutes.\n" + r.text;
      r.png = last_png;
      return r;
    }
  }
  r.text = "All " + std::to_string(steps->size()) + " steps done.\n" + r.text;
  r.png = last_png;
  return r;
}

} // namespace

ToolResult call_tool(Call& call, const std::string& name, const Json& args)
{
  const ToolEntry* t = find_tool(name);

  if (t)
    return t->fn(call, args);
  return fail("es40 has no tool " + name);
}

void release_input()
{
  std::lock_guard<std::mutex> lock(g_input_mutex);

  if (!theKeyboard)
    return;
  for (auto it = g_held_keys.rbegin(); it != g_held_keys.rend(); ++it)
    theKeyboard->gen_scancode(*it | BX_KEY_RELEASED);
  g_held_keys.clear();
  if (g_buttons)
  {
    g_buttons = 0;
    theKeyboard->agent_mouse_motion(0, 0, 0, 0);
  }
}

} // namespace es40mcp
