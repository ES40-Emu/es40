/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2010-2012 Aaron Giles from MAME
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * address_map for ES40 VGA register dispatch.
 * Adapted from MAME for code re-use compatibility in VGA/S3 Implementation
 *
 *     map(lo, hi).rw(FUNC(device_class::read), FUNC(device_class::write))
 *     map(lo, hi).r(FUNC(device_class::read))
 *     map(lo, hi).w(FUNC(device_class::write))
 *     map(lo, hi).lrw8(NAME(read_lambda), NAME(write_lambda))
 *     map(lo, hi).lr8(NAME(read_lambda))
 *     map(lo, hi).lw8(NAME(write_lambda))
 *     map(lo, hi).noprw()
 *     map(lo, hi).mirror(bits).lrw8(...)
 *     map.unmap_value_high() / map.unmap_value_low()
 *     map.global_mask(mask)
 *
 *     u8  val  = m_crtc_map.read_byte(addr);
 *     m_crtc_map.write_byte(addr, data);
 *
 */

#ifndef ES40_ADDRESS_MAP_H
#define ES40_ADDRESS_MAP_H

#include <functional>
#include <memory>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>
#include "mame_shims.h"

using offs_t = uint32_t;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

#ifndef NAME
#define NAME(x) x, #x
#endif

// MAME addrmap.h binds member handlers through device_t and rw_delegate.
// ES40 byte maps have no device_t owner, so FUNC binds the current object.
// Typed invocation retains virtual dispatch and base adjustments.
template <typename Owner, typename Member>
auto bind_map_member(Owner* owner, Member member)
{
  static_assert(std::is_member_function_pointer_v<Member>,
    "FUNC requires a member function");
  return [owner, member](auto&&... args) -> decltype(auto) {
    return std::invoke(member, owner, std::forward<decltype(args)>(args)...);
  };
}

// Map compatibility: use inside the owning object's methods or constructors.
// Upstream emucore.h: #define FUNC(x) &x, #x
#ifndef FUNC
#define FUNC(x) ::bind_map_member(this, &x), #x
#endif

class address_map;


// address_map_entry — returned by map(lo, hi), chains handler bindings/.mirror

class address_map_entry
{
public:
  address_map_entry(address_map& parent, unsigned lo, unsigned hi)
    : m_map(parent), m_lo(lo), m_hi(hi), m_mirror(0), m_select(0) {
  }

  // Byte member handlers use the same dispatch and offset rules as lambdas.
  template <typename T>
  address_map_entry& r(T&& read_fn, const char* name)
  {
    return lr8(std::forward<T>(read_fn), name);
  }

  template <typename T>
  address_map_entry& w(T&& write_fn, const char* name)
  {
    return lw8(std::forward<T>(write_fn), name);
  }

  template <typename T, typename U>
  address_map_entry& rw(T&& read_fn, const char* read_name,
    U&& write_fn, const char* write_name)
  {
    return lrw8(std::forward<T>(read_fn), read_name,
      std::forward<U>(write_fn), write_name);
  }

  /// .lrw8(read_fn, read_name, write_fn, write_name)
  template <typename T, typename U>
  address_map_entry& lrw8(T&& read_fn, const char* read_name,
    U&& write_fn, const char* write_name);

  /// .lr8(read_fn, read_name)
  template <typename T>
  address_map_entry& lr8(T&& read_fn, const char* name);

  /// .lw8(write_fn, write_name)
  template <typename T>
  address_map_entry& lw8(T&& write_fn, const char* name);

  /// .noprw() 
  address_map_entry& noprw();

  /// .mirror(bits)
  address_map_entry& mirror(unsigned bits)
  {
    m_mirror = bits;
    return *this;
  }

  address_map_entry& select(unsigned bits)
  {
    m_select = bits;
    return *this;
  }

private:
  offs_t handler_mask() const
  {
    offs_t changing_bits = m_lo ^ m_hi;
    changing_bits |= changing_bits >> 1;
    changing_bits |= changing_bits >> 2;
    changing_bits |= changing_bits >> 4;
    changing_bits |= changing_bits >> 8;
    changing_bits |= changing_bits >> 16;
    return changing_bits | m_select;
  }

  /// Expand mirror bits and call fn for each effective address
  void for_each_addr(std::function<void(unsigned)> fn) const;

  address_map& m_map;
  unsigned      m_lo;
  unsigned      m_hi;
  unsigned      m_mirror;
  unsigned      m_select;
};


// address_map — dispatch table

class address_map
{
public:
  explicit address_map(unsigned size = 256)
    : m_size(size), m_slots(size), m_unmapval(0xFF), m_globalmask(0)
  {
  }

  address_map_entry operator()(unsigned lo, unsigned hi)
  {
    return address_map_entry(*this, lo, hi);
  }

  // map-level 

  void unmap_value_high() { m_unmapval = 0xFF; }
  void unmap_value_low() { m_unmapval = 0x00; }
  void global_mask(offs_t mask) { m_globalmask = mask; }

  // Dispatch 

  u8 read_byte(unsigned addr) const
  {
    if (m_globalmask) addr &= m_globalmask;
    if (addr < m_size && m_slots[addr].read_fn)
      return m_slots[addr].read_fn(static_cast<offs_t>(addr - m_slots[addr].read_base));
    return m_unmapval;
  }

  void write_byte(unsigned addr, u8 data)
  {
    if (m_globalmask) addr &= m_globalmask;
    if (addr < m_size && m_slots[addr].write_fn)
      m_slots[addr].write_fn(static_cast<offs_t>(addr - m_slots[addr].write_base), data);
  }

  bool has_handler(unsigned addr) const
  {
    if (m_globalmask) addr &= m_globalmask;
    if (addr >= m_size) return false;
    return m_slots[addr].read_fn || m_slots[addr].write_fn;
  }

  bool has_read_handler(unsigned addr) const
  {
    if (m_globalmask) addr &= m_globalmask;
    return addr < m_size && bool(m_slots[addr].read_fn);
  }

  bool has_write_handler(unsigned addr) const
  {
    if (m_globalmask) addr &= m_globalmask;
    return addr < m_size && bool(m_slots[addr].write_fn);
  }

  const char* debug_name(unsigned addr) const
  {
    if (m_globalmask) addr &= m_globalmask;
    return (addr < m_size) ? m_slots[addr].name : nullptr;
  }

  u8 unmapval() const { return m_unmapval; }

  // called by address_map_entry

  void install_read(unsigned addr, std::function<u8(offs_t)> fn, unsigned base, const char* name)
  {
    if (addr < m_size)
    {
      m_slots[addr].read_fn = std::move(fn);
      m_slots[addr].read_base = base;
      if (name) m_slots[addr].name = name;
    }
  }

  void install_write(unsigned addr, std::function<void(offs_t, u8)> fn, unsigned base, const char* name)
  {
    if (addr < m_size)
    {
      m_slots[addr].write_fn = std::move(fn);
      m_slots[addr].write_base = base;
      if (name) m_slots[addr].name = name;
    }
  }

  void install_nop(unsigned addr, unsigned base, const char* name)
  {
    if (addr < m_size)
    {
      m_slots[addr].read_fn = [](offs_t) -> u8 { return 0; };
      m_slots[addr].write_fn = [](offs_t, u8) {};
      m_slots[addr].read_base = base;
      m_slots[addr].write_base = base;
      if (name) m_slots[addr].name = name;
    }
  }

  unsigned size() const { return m_size; }

private:
  struct Slot
  {
    std::function<u8(offs_t)>       read_fn;
    std::function<void(offs_t, u8)> write_fn;
    unsigned                        read_base = 0;
    unsigned                        write_base = 0;
    const char* name = nullptr;
  };

  unsigned           m_size;
  std::vector<Slot>  m_slots;
  u8                 m_unmapval;
  offs_t             m_globalmask;
};


//  mirror expansion helper

inline void address_map_entry::for_each_addr(std::function<void(unsigned)> fn) const
{
  const unsigned expanded_bits = m_mirror | m_select;
  if (expanded_bits == 0)
  {
    for (unsigned a = m_lo; a <= m_hi; ++a)
      fn(a);
  }
  else
  {
    // combinations of mirror bits
    // any mirror bit can be 0 or 1 independently... i think
    std::vector<unsigned> mirror_bits;
    for (unsigned b = 0; b < 32; ++b)
      if (expanded_bits & (1u << b))
        mirror_bits.push_back(1u << b);

    unsigned combos = 1u << mirror_bits.size();
    for (unsigned c = 0; c < combos; ++c)
    {
      unsigned mirror_val = 0;
      for (unsigned i = 0; i < mirror_bits.size(); ++i)
        if (c & (1u << i))
          mirror_val |= mirror_bits[i];

      for (unsigned a = m_lo; a <= m_hi; ++a)
        fn(a | mirror_val);
    }
  }
}


// address_map_entry templates

template <typename T, typename U>
address_map_entry& address_map_entry::lrw8(T&& read_fn, const char* read_name,
  U&& write_fn, const char* write_name)
{
  auto rfn = std::make_shared<std::function<u8(offs_t)>>(std::forward<T>(read_fn));
  auto wfn = std::make_shared<std::function<void(offs_t, u8)>>(std::forward<U>(write_fn));
  unsigned base = m_lo;
  const offs_t mask = handler_mask();

  for_each_addr([&](unsigned addr) {
    m_map.install_read(addr, [rfn, mask](offs_t off) { return (*rfn)(off & mask); }, base, read_name);
    m_map.install_write(addr, [wfn, mask](offs_t off, u8 d) { (*wfn)(off & mask, d); }, base, write_name);
    });
  return *this;
}

template <typename T>
address_map_entry& address_map_entry::lr8(T&& read_fn, const char* name)
{
  auto rfn = std::make_shared<std::function<u8(offs_t)>>(std::forward<T>(read_fn));
  unsigned base = m_lo;
  const offs_t mask = handler_mask();

  for_each_addr([&](unsigned addr) {
    m_map.install_read(addr, [rfn, mask](offs_t off) { return (*rfn)(off & mask); }, base, name);
    });
  return *this;
}

template <typename T>
address_map_entry& address_map_entry::lw8(T&& write_fn, const char* name)
{
  auto wfn = std::make_shared<std::function<void(offs_t, u8)>>(std::forward<T>(write_fn));
  unsigned base = m_lo;
  const offs_t mask = handler_mask();

  for_each_addr([&](unsigned addr) {
    m_map.install_write(addr, [wfn, mask](offs_t off, u8 d) { (*wfn)(off & mask, d); }, base, name);
    });
  return *this;
}

inline address_map_entry& address_map_entry::noprw()
{
  unsigned base = m_lo;
  for_each_addr([&](unsigned addr) {
    m_map.install_nop(addr, base, "noprw");
    });
  return *this;
}


#endif // ES40_ADDRESS_MAP_H
