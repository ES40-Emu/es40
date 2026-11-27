/* ES40 emulator -- DEC smart frame buffer ASIC.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) R. Belmont (MAME src/mame/dec/sfb.h and sfb.cpp)
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
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

#include "SFB.h"
#include "emu/address_map.h"
#include <algorithm>

DEFINE_DEVICE_TYPE(DECSFB, decsfb_device, "decsfb", "Digital Equipment Corporation Smart Frame Buffer")

static constexpr u64 dot_clock = 130810000; // 1280 × 1024 at 72 Hz (DEC WRL 93/1, Table 2).

// Word indices in the SFB register window (EK-D3SYS-PM, Table 15).
enum SFBRegister
{
  Foreground = 8, Background, Planemask, Pixelmask, Mode, RasterOp, PixelShift,
  Address, Bres1, Bres2, Bres3, Bcont, Deep, Start, ClearInterrupt, Reserved,
  Refresh, Horizontal, Vertical, VideoBase, VideoValid, EnableInterrupt,
  TcClockCount, VideoClockCount
};

decsfb_device::decsfb_device(const machine_config& mconfig, const char* tag, device_t* owner, u32 clock)
  : device_t(mconfig, DECSFB, tag, owner, clock), m_tc_clock(clock), m_int_cb(*this)
{
}

void decsfb_device::device_start()
{
  m_vram = std::make_unique<u32[]>(vram_size / sizeof(u32));
  save_pointer(NAME(m_vram), vram_size / sizeof(u32));
  save_item(NAME(m_regs));
  save_item(NAME(m_copy_src));
  save_item(NAME(m_residue));
  save_item(NAME(m_copy_low));
  save_item(NAME(m_clock_start_ns));
  save_item(NAME(m_measuring_clock));
  save_item(NAME(m_irq_pending));
}

void decsfb_device::device_reset()
{
  bool asserted = interrupt();
  std::fill_n(m_regs, 32, 0);
  m_regs[Pixelmask] = 0xffffffff; // EK-D3SYS-PM §6.4.4.
  m_copy_src = 1;
  m_residue = 0;
  m_copy_low = 0;
  m_clock_start_ns = 0;
  m_measuring_clock = m_irq_pending = false;
  if (asserted)
    m_int_cb(0);
}

u32 decsfb_device::width() const { return (m_regs[Horizontal] & 0x1ff) * 4; }
u32 decsfb_device::height() const { return m_regs[Vertical] & 0x7ff; }
u32 decsfb_device::video_base() const { return (m_regs[VideoBase] & 0x1ff) << 12; }
bool decsfb_device::video_valid() const { return (m_regs[VideoValid] & 1) != 0; }
bool decsfb_device::interrupt() const { return m_irq_pending && (m_regs[EnableInterrupt] & 1); }

u64 decsfb_device::frame_period_ns() const
{
  if (!video_valid() || !width() || !height())
    return 0;
  // Horizontal fields count four dot clocks each; vertical fields count lines (EK-D3SYS-PM §§6.4.10.3–4).
  u32 h = m_regs[Horizontal], v = m_regs[Vertical];
  u32 dots = width() + 4 * (((h >> 9) & 31) + ((h >> 14) & 127) + ((h >> 21) & 127));
  u32 lines = height() + ((v >> 11) & 31) + ((v >> 16) & 63) + ((v >> 22) & 63);
  return (u64(dots) * lines * 1000000000 + dot_clock / 2) / dot_clock;
}

void decsfb_device::vertical_blank()
{
  if (video_valid() && (m_regs[EnableInterrupt] & 1) && !m_irq_pending)
  {
    m_irq_pending = true;
    m_int_cb(1);
  }
}

u32 decsfb_device::read(offs_t offset)
{
  // Register sets repeat every 128 bytes (DEC WRL 93/1 §4.8).
  u32 reg = offset & 31;
  switch (reg)
  {
    case TcClockCount:
    case VideoClockCount:
    {
      u64 now_ns = m_now ? m_now() : 0;
      if (!m_measuring_clock || !m_tc_clock || now_ns < m_clock_start_ns)
        return 0;
      // Count dot clock / 4 for 256 TC cycles (EK-D3SYS-PM §6.4.11, WRL 93/1 Table 2).
      u64 duration = (256000000000ULL + m_tc_clock - 1) / m_tc_clock;
      u64 elapsed = std::min<u64>(now_ns - m_clock_start_ns, duration);
      if (reg == TcClockCount)
        return elapsed == duration ? 0xff : 0;
      return static_cast<u32>((elapsed * (dot_clock / 1000) + 2000000) / 4000000) & 0x3ff;
    }
    case Bcont:
    case Start:
    case ClearInterrupt:
    case Reserved:
    case VideoValid:
    case EnableInterrupt:
      return 0;
    default:
      return m_regs[reg];
  }
}

void decsfb_device::write(offs_t offset, u32 data, u32 mem_mask)
{
  if (!mem_mask)
    return;
  u32 reg = offset & 31;
  u32 value = (m_regs[reg] & ~mem_mask) | (data & mem_mask);
  if (reg < Foreground)
  {
    // A pair of CPU writes feeds one quadword through the shifter (EK-D3SYS-PM §6.4.1).
    if (reg & 1)
    {
      shift_quad(reg / 2, (static_cast<u64>(value) << 32) | m_copy_low);
      m_copy_src = 0;
    }
    else
      m_copy_low = (m_copy_low & ~mem_mask) | (data & mem_mask);
    return;
  }
  switch (reg)
  {
    case Mode: value &= 7; break;
    case RasterOp: value &= 15; break;
    case PixelShift:
      m_copy_src = 1;
      value &= 15;
      break;
    case Address: value &= 0xffffff; break;
    case Bres3: value &= 0xffff800f; break;
    case Bcont:
    case Start:
      draw(m_regs[Address], data & mem_mask, mem_mask);
      return;
    case Deep: value &= 3; break;
    case ClearInterrupt:
    {
      bool asserted = interrupt();
      m_irq_pending = false;
      if (asserted)
        m_int_cb(0);
      return;
    }
    case Reserved: return;
    case Refresh: value &= 0x3ff; break;
    case Horizontal:
    case Vertical: value &= 0x0fffffff; break;
    case VideoBase: value &= 0x1ff; break;
    case VideoValid:
      value &= 1;
      break;
    case EnableInterrupt:
    {
      bool asserted = interrupt();
      m_regs[reg] = value & 1;
      if (asserted != interrupt())
        m_int_cb(interrupt());
      return;
    }
    case TcClockCount:
      m_clock_start_ns = m_now ? m_now() : 0;
      m_measuring_clock = true;
      return;
    case VideoClockCount: return;
  }
  m_regs[reg] = value;
}

u32 decsfb_device::read_vram(u32 offset) const
{
  if (offset > vram_size - 4)
    return 0xffffffff;
  return m_vram[offset >> 2];
}

static u8 raster_result(u32 operation, u8 src, u8 dst)
{
  // All 16 Boolean raster functions apply before the plane mask (EK-D3SYS-PM §§6.4.2–3).
  switch (operation)
  {
    case 0: return 0;
    case 1: return src & dst;
    case 2: return src & ~dst;
    case 3: return src;
    case 4: return ~src & dst;
    case 5: return dst;
    case 6: return src ^ dst;
    case 7: return src | dst;
    case 8: return ~(src | dst);
    case 9: return ~(src ^ dst);
    case 10: return ~dst;
    case 11: return src | ~dst;
    case 12: return ~src;
    case 13: return ~src | dst;
    case 14: return ~(src & dst);
    default: return 0xff;
  }
}

static u8 byte_lane(u32 word, u32 address)
{
  return static_cast<u8>(word >> ((address & 3) * 8));
}

static u32 framebuffer_offset(u32 address)
{
  // The 2, 4 and 8 MiB windows select the same display bank (EK-D3SYS-PM, Table 14).
  switch ((address & 0xffffff) >> 21)
  {
    case 0:
    case 1:
    case 2:
    case 4: return address & (decsfb_device::vram_size - 1);
    default: return decsfb_device::vram_size;
  }
}

u32 decsfb_device::vram_r(offs_t offset)
{
  return read_vram(framebuffer_offset(offset << 2));
}

u8 decsfb_device::pixel(u32 offset) const
{
  return byte_lane(m_vram[(offset & (vram_size - 1)) >> 2], offset);
}

void decsfb_device::paint(u32 address, u8 source, u8 mask)
{
  if (address >= vram_size)
    return;
  mask &= byte_lane(m_regs[Planemask], address);
  u32& word = m_vram[address >> 2];
  u32 shift = (address & 3) * 8;
  u8 dest = byte_lane(word, address);
  u32 bits = raster_result(m_regs[RasterOp], source, dest) & mask;
  word = (word & ~(u32(mask) << shift)) | (bits << shift);
}

void decsfb_device::stipple(u32 address, u32 pattern)
{
  u32 mask = m_regs[Mode] == 5 ? pattern : m_regs[Pixelmask];
  for (u32 i = 0; i < 32; ++i)
  {
    if (mask & (1u << i))
    {
      u32 color = m_regs[(pattern & (1u << i)) ? Foreground : Background];
      paint(address + i, byte_lane(color, address + i));
    }
  }
}

void decsfb_device::line(u32 address, u32 pattern)
{
  u32 length = m_regs[Bres3] & 15;
  u32 error = m_regs[Bres3] >> 15;
  // Each Bresenham step advances the address and signed 17-bit error (EK-D3SYS-PM §6.4.1).
  for (u32 i = 0; i < (length ? length : 16); ++i)
  {
    bool foreground = (pattern & (1u << i)) != 0;
    if (foreground || m_regs[Mode] == 2)
      paint(framebuffer_offset(address), byte_lane(m_regs[foreground ? Foreground : Background], address));
    bool negative = (error & 0x10000) != 0;
    u32 step = m_regs[negative ? Bres1 : Bres2];
    address += static_cast<s16>(step >> 16);
    u32 increment = step & 0xffff;
    error = negative ? error + increment : error - increment;
  }
  m_regs[Address] = address & 0xffffff;
  m_regs[Bres3] = error << 15;
}

void decsfb_device::shift_quad(u32 index, u64 incoming)
{
  // The shifter combines consecutive VRAM quadwords and retains the incoming one (DEC WRL 93/1 §4.6).
  u32 shift = m_regs[PixelShift] & 15;
  u64 result = incoming;
  if (shift == 8)
    result = m_residue;
  else if (shift > 8)
    result = (incoming >> ((16 - shift) * 8)) | (m_residue << ((shift - 8) * 8));
  else if (shift)
    result = (incoming << (shift * 8)) | (m_residue >> ((8 - shift) * 8));
  for (u32 quad = index; quad < 4; ++quad)
  {
    m_regs[quad * 2] = static_cast<u32>(result);
    m_regs[quad * 2 + 1] = static_cast<u32>(result >> 32);
  }
  m_residue = incoming;
}

void decsfb_device::copy(u32 address, u32 mask)
{
  address &= ~7u;
  bool backward = (m_regs[PixelShift] & 8) != 0;
  u32 next_quad = 0;
  for (u32 i = 0; i < 4; ++i)
  {
    u32 bytes = (mask >> (i * 8)) & 0xff;
    u32 location = backward ? address - i * 8 : address + i * 8;
    if (!bytes)
      continue;
    if (!m_copy_src)
    {
      for (u32 j = 0; j < 8; ++j)
        if (bytes & (1u << j))
          paint(location + j, byte_lane(m_regs[i * 2 + j / 4], j));
    }
    else
    {
      u64 incoming = read_vram(location);
      incoming |= static_cast<u64>(read_vram(location + 4)) << 32;
      shift_quad(next_quad++, incoming);
    }
  }
  m_copy_src ^= 1;
}

void decsfb_device::draw(u32 address, u32 value, u32 mask)
{
  u32 offset = framebuffer_offset(address);
  // The CXTurbo has one populated eight-plane bank (EK-D3SYS-PM §§6.1, 6.4.8).
  if (offset >= vram_size || (m_regs[Deep] & 2))
    return;
  switch (m_regs[Mode])
  {
    case 0:
      for (u32 i = 0; i < 4; ++i)
        paint(offset + i, byte_lane(value, i), byte_lane(mask, i));
      break;
    case 1:
    case 5: stipple(offset, value); break;
    case 2:
    case 6: line(address, value); break;
    case 7: copy(offset, value); break;
  }
  m_regs[Pixelmask] = 0xffffffff;
}

void decsfb_device::vram_w(offs_t offset, u32 data, u32 mem_mask)
{
  if (!mem_mask)
    return;
  // A line write carries the low two address bits in data bits 17:16 (EK-D3SYS-PM §6.4.1).
  u32 value = data & mem_mask;
  u32 address = (offset << 2) & 0xffffff;
  if (m_regs[Mode] == 2 || m_regs[Mode] == 6)
    address |= (value >> 16) & 3;
  draw(address, value, mem_mask);
}

