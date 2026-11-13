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
 * A small JSON value with a parser and a writer (see McpJson.h).
 **/

#include "McpJson.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace es40mcp
{

Json Json::array()
{
  Json j;
  j.m_type = Type::Array;
  return j;
}

Json Json::object()
{
  Json j;
  j.m_type = Type::Object;
  return j;
}

bool Json::is_int() const
{
  if (m_type == Type::Int)
    return true;
  return m_type == Type::Double && std::isfinite(m_double) &&
    m_double == std::floor(m_double) && std::fabs(m_double) < 9.0e15;
}

bool Json::as_bool(bool def) const
{
  return m_type == Type::Bool ? m_bool : def;
}

int64_t Json::as_int(int64_t def) const
{
  if (m_type == Type::Int)
    return m_int;
  if (is_int())
    return (int64_t)m_double;
  return def;
}

double Json::as_double(double def) const
{
  if (m_type == Type::Int)
    return (double)m_int;
  if (m_type == Type::Double)
    return m_double;
  return def;
}

const std::string& Json::as_string() const
{
  static const std::string empty;
  return m_type == Type::String ? m_string : empty;
}

size_t Json::size() const
{
  if (m_type == Type::Array)
    return m_array.size();
  if (m_type == Type::Object)
    return m_object.size();
  return 0;
}

const Json& Json::operator[](size_t i) const
{
  static const Json null_value;
  return (m_type == Type::Array && i < m_array.size()) ? m_array[i] : null_value;
}

Json& Json::push(Json v)
{
  if (m_type != Type::Array)
  {
    *this = array();
  }
  m_array.push_back(std::move(v));
  return m_array.back();
}

const Json* Json::find(const std::string& key) const
{
  if (m_type != Type::Object)
    return nullptr;
  for (const auto& m : m_object)
  {
    if (m.first == key)
      return &m.second;
  }
  return nullptr;
}

Json& Json::set(const std::string& key, Json v)
{
  if (m_type != Type::Object)
  {
    *this = object();
  }
  for (auto& m : m_object)
  {
    if (m.first == key)
    {
      m.second = std::move(v);
      return m.second;
    }
  }
  m_object.emplace_back(key, std::move(v));
  return m_object.back().second;
}

void Json::erase(const std::string& key)
{
  for (auto it = m_object.begin(); it != m_object.end(); ++it)
  {
    if (it->first == key)
    {
      m_object.erase(it);
      return;
    }
  }
}

bool Json::operator==(const Json& o) const
{
  // two whole numbers compare exactly: a double cannot tell big ids apart
  if (m_type == Type::Int && o.m_type == Type::Int)
    return m_int == o.m_int;
  if (is_number() && o.is_number())
    return as_double() == o.as_double();
  if (m_type != o.m_type)
    return false;
  switch (m_type)
  {
  case Type::Null:
    return true;
  case Type::Bool:
    return m_bool == o.m_bool;
  case Type::String:
    return m_string == o.m_string;
  case Type::Array:
    return m_array == o.m_array;
  case Type::Object:
    return m_object == o.m_object;
  default:
    return false;
  }
}

static void dump_string(std::string& out, const std::string& s)
{
  out += '"';
  for (unsigned char c : s)
  {
    switch (c)
    {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    default:
      if (c < 0x20)
      {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      }
      else
      {
        out += (char)c;
      }
    }
  }
  out += '"';
}

void Json::dump_to(std::string& out) const
{
  switch (m_type)
  {
  case Type::Null:
    out += "null";
    break;
  case Type::Bool:
    out += m_bool ? "true" : "false";
    break;
  case Type::Int:
    out += std::to_string(m_int);
    break;
  case Type::Double:
    if (!std::isfinite(m_double))
    {
      out += "null";
    }
    else
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.17g", m_double);
      out += buf;
    }
    break;
  case Type::String:
    dump_string(out, m_string);
    break;
  case Type::Array:
    out += '[';
    for (size_t i = 0; i < m_array.size(); i++)
    {
      if (i)
        out += ',';
      m_array[i].dump_to(out);
    }
    out += ']';
    break;
  case Type::Object:
    out += '{';
    for (size_t i = 0; i < m_object.size(); i++)
    {
      if (i)
        out += ',';
      dump_string(out, m_object[i].first);
      out += ':';
      m_object[i].second.dump_to(out);
    }
    out += '}';
    break;
  }
}

std::string Json::dump() const
{
  std::string out;
  dump_to(out);
  return out;
}

namespace
{

class Parser
{
public:
  Parser(const std::string& text) : p(text.data()), end(text.data() + text.size()) {}

  bool document(Json& out)
  {
    if (!value(out))
      return false;
    ws();
    if (p != end)
      return fail("text after the value");
    return true;
  }

  std::string error;

private:
  const char* p;
  const char* end;
  int depth = 0;

  bool fail(const char* why)
  {
    if (error.empty())
      error = why;
    return false;
  }

  void ws()
  {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
      p++;
  }

  bool literal(const char* word)
  {
    for (const char* w = word; *w; w++, p++)
    {
      if (p >= end || *p != *w)
        return fail("unknown word");
    }
    return true;
  }

  static void put_utf8(std::string& out, unsigned cp)
  {
    if (cp < 0x80)
    {
      out += (char)cp;
    }
    else if (cp < 0x800)
    {
      out += (char)(0xC0 | (cp >> 6));
      out += (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
      out += (char)(0xE0 | (cp >> 12));
      out += (char)(0x80 | ((cp >> 6) & 0x3F));
      out += (char)(0x80 | (cp & 0x3F));
    }
    else
    {
      out += (char)(0xF0 | (cp >> 18));
      out += (char)(0x80 | ((cp >> 12) & 0x3F));
      out += (char)(0x80 | ((cp >> 6) & 0x3F));
      out += (char)(0x80 | (cp & 0x3F));
    }
  }

  bool hex4(unsigned& v)
  {
    if (end - p < 4)
      return fail("short \\u escape");
    v = 0;
    for (int i = 0; i < 4; i++, p++)
    {
      char c = *p;
      v <<= 4;
      if (c >= '0' && c <= '9')
        v |= (unsigned)(c - '0');
      else if (c >= 'a' && c <= 'f')
        v |= (unsigned)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        v |= (unsigned)(c - 'A' + 10);
      else
        return fail("bad \\u escape");
    }
    return true;
  }

  bool string(std::string& out)
  {
    p++;  // the opening quote
    while (p < end)
    {
      char c = *p++;
      if (c == '"')
        return true;
      if (c != '\\')
      {
        out += c;
        continue;
      }
      if (p >= end)
        break;
      char e = *p++;
      switch (e)
      {
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      case '/':
        out += '/';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u':
      {
        unsigned cp;
        if (!hex4(cp))
          return false;
        if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
        {
          const char* save = p;
          unsigned lo;
          p += 2;
          if (!hex4(lo))
            return false;
          if (lo >= 0xDC00 && lo <= 0xDFFF)
          {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          else
          {
            p = save;  // a lone high surrogate; the next escape stands alone
            cp = 0xFFFD;
          }
        }
        else if (cp >= 0xD800 && cp <= 0xDFFF)
        {
          cp = 0xFFFD;
        }
        put_utf8(out, cp);
        break;
      }
      default:
        return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }

  static bool digit(char c) { return c >= '0' && c <= '9'; }

  bool number(Json& out)
  {
    const char* start = p;
    bool whole = true;

    if (p < end && *p == '-')
      p++;
    if (p >= end || !digit(*p))
      return fail("unexpected character");
    while (p < end && digit(*p))
      p++;
    if (p < end && *p == '.')
    {
      whole = false;
      p++;
      if (p >= end || !digit(*p))
        return fail("bad number");
      while (p < end && digit(*p))
        p++;
    }
    if (p < end && (*p == 'e' || *p == 'E'))
    {
      whole = false;
      p++;
      if (p < end && (*p == '+' || *p == '-'))
        p++;
      if (p >= end || !digit(*p))
        return fail("bad number");
      while (p < end && digit(*p))
        p++;
    }

    std::string token(start, p);
    if (whole)
    {
      char* stop;
      errno = 0;
      long long v = strtoll(token.c_str(), &stop, 10);
      if (errno == 0 && *stop == '\0')
      {
        out = Json(v);
        return true;
      }
    }
    out = Json(strtod(token.c_str(), nullptr));
    return true;
  }

  bool array(Json& out)
  {
    p++;  // [
    out = Json::array();
    ws();
    if (p < end && *p == ']')
    {
      p++;
      return true;
    }
    for (;;)
    {
      Json v;
      if (!value(v))
        return false;
      out.push(std::move(v));
      ws();
      if (p < end && *p == ',')
      {
        p++;
        continue;
      }
      if (p < end && *p == ']')
      {
        p++;
        return true;
      }
      return fail("expected , or ]");
    }
  }

  bool object(Json& out)
  {
    p++;  // {
    out = Json::object();
    ws();
    if (p < end && *p == '}')
    {
      p++;
      return true;
    }
    for (;;)
    {
      std::string key;
      Json v;

      ws();
      if (p >= end || *p != '"')
        return fail("expected a key");
      if (!string(key))
        return false;
      ws();
      if (p >= end || *p != ':')
        return fail("expected :");
      p++;
      if (!value(v))
        return false;
      out.set(key, std::move(v));
      ws();
      if (p < end && *p == ',')
      {
        p++;
        continue;
      }
      if (p < end && *p == '}')
      {
        p++;
        return true;
      }
      return fail("expected , or }");
    }
  }

  bool value(Json& out)
  {
    bool ok;

    ws();
    if (p >= end)
      return fail("unexpected end");
    if (++depth > 64)
      return fail("nested too deep");
    switch (*p)
    {
    case '{':
      ok = object(out);
      break;
    case '[':
      ok = array(out);
      break;
    case '"':
    {
      std::string s;
      ok = string(s);
      if (ok)
        out = Json(std::move(s));
      break;
    }
    case 't':
      ok = literal("true");
      if (ok)
        out = Json(true);
      break;
    case 'f':
      ok = literal("false");
      if (ok)
        out = Json(false);
      break;
    case 'n':
      ok = literal("null");
      if (ok)
        out = Json();
      break;
    default:
      ok = number(out);
    }
    depth--;
    return ok;
  }
};

} // namespace

bool Json::parse(const std::string& text, Json& out, std::string& error)
{
  Parser parser(text);

  out = Json();
  if (!parser.document(out))
  {
    error = parser.error;
    return false;
  }
  return true;
}

} // namespace es40mcp
