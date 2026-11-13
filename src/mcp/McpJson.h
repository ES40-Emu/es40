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
 * A small JSON value with a parser and a writer, enough for the JSON-RPC
 * messages of the MCP server (McpServer.cpp).
 **/

#ifndef ES40_MCP_JSON_H
#define ES40_MCP_JSON_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace es40mcp
{

class Json
{
public:
  enum class Type { Null, Bool, Int, Double, String, Array, Object };

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool v) : m_type(Type::Bool), m_bool(v) {}
  Json(int v) : m_type(Type::Int), m_int(v) {}
  Json(unsigned v) : m_type(Type::Int), m_int(v) {}
  Json(long v) : m_type(Type::Int), m_int(v) {}
  Json(unsigned long v) : m_type(Type::Int), m_int((int64_t)v) {}
  Json(long long v) : m_type(Type::Int), m_int(v) {}
  Json(unsigned long long v) : m_type(Type::Int), m_int((int64_t)v) {}
  Json(double v) : m_type(Type::Double), m_double(v) {}
  Json(const char* v) : m_type(Type::String), m_string(v ? v : "") {}
  Json(std::string v) : m_type(Type::String), m_string(std::move(v)) {}

  static Json array();
  static Json object();

  Type type() const { return m_type; }
  bool is_null() const { return m_type == Type::Null; }
  bool is_bool() const { return m_type == Type::Bool; }
  bool is_int() const;
  bool is_number() const { return m_type == Type::Int || m_type == Type::Double; }
  bool is_string() const { return m_type == Type::String; }
  bool is_array() const { return m_type == Type::Array; }
  bool is_object() const { return m_type == Type::Object; }

  // The value, or def when it is missing or of another type. A whole
  // Double counts as an Int.
  bool as_bool(bool def = false) const;
  int64_t as_int(int64_t def = 0) const;
  double as_double(double def = 0) const;
  const std::string& as_string() const;

  // Arrays
  size_t size() const;
  const Json& operator[](size_t i) const;
  const std::vector<Json>& items() const { return m_array; }
  Json& push(Json v);

  // Objects keep their keys in the order they were set
  const Json* find(const std::string& key) const;
  bool has(const std::string& key) const { return find(key) != nullptr; }
  Json& set(const std::string& key, Json v);
  void erase(const std::string& key);
  const std::vector<std::pair<std::string, Json>>& members() const { return m_object; }

  bool operator==(const Json& o) const;
  bool operator!=(const Json& o) const { return !(*this == o); }

  std::string dump() const;
  void dump_to(std::string& out) const;

  /// Parses one JSON text; on failure says why in error
  static bool parse(const std::string& text, Json& out, std::string& error);

private:
  Type m_type = Type::Null;
  bool m_bool = false;
  int64_t m_int = 0;
  double m_double = 0;
  std::string m_string;
  std::vector<Json> m_array;
  std::vector<std::pair<std::string, Json>> m_object;
};

} // namespace es40mcp

#endif
