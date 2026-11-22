/* ES40 emulator -- EV4 primary data cache
 * Copyright (C) 2026 by gdwnldsKSC
 * SPDX-License-Identifier: BSD-1-Clause
 */
#ifndef INCLUDED_EV4DCACHE_H
#define INCLUDED_EV4DCACHE_H

#include <cstdint>
#include <vector>

struct Ev4Dcache
{
  // 21064 HRM 5.3.11, Fig. 5-22 and Table 5-10. Bits 15:12 belong to the 21064A.
  static constexpr std::uint64_t control_mask = 0xfff;
  static constexpr unsigned spe_1 = 1u << 4, spe_2 = 1u << 5;
  static constexpr unsigned enable = 1u << 10, force_hit = 1u << 11;
  static constexpr unsigned ncache_ndisturb = 1u << 8;
  static constexpr unsigned dc_hit = 1u << 3; // DC_STAT (21064 HRM Table 5-15).
  // HRM 2.8.2.1: 8 KiB, physically direct-mapped, 32-byte lines, read-allocate and write-through.
  static constexpr unsigned line_bytes = 32, line_count = 256;
  struct Line
  {
    std::uint64_t tag = UINT64_MAX, backing = UINT64_MAX;
    std::uint64_t overlay = 0;
    std::uint8_t data[line_bytes]{};
  };
  static_assert(sizeof(Line) == 56);
  // Retain the other 21064 ABOX_CTL fields for their Abox subsystems (HRM Table 5-10).
  std::uint64_t control = 0;
  std::vector<Line> lines;

  bool active() const { return (control & (enable | force_hit)) != 0; }
  static unsigned index(std::uint64_t phys) { return (phys >> 5) & (line_count - 1); }
  static std::uint64_t base(std::uint64_t phys) { return phys & ~std::uint64_t(line_bytes - 1); }

  bool hits(std::uint64_t phys) const
  {
    return (control & force_hit) ||
           ((control & enable) && !lines.empty() && lines[index(phys)].tag == base(phys));
  }

  void invalidate()
  {
    for (auto& line : lines) line.tag = UINT64_MAX;
  }

  template<typename ReadRam>
  bool read_hit(std::uint64_t phys, int bits, ReadRam read_ram, std::uint64_t& value) const
  {
    if (!hits(phys)) return false;
    if (lines.empty()) {
      value = 0; // Deterministic cold SRAM for a forced hit.
      return true;
    }
    const auto& line = lines[index(phys)];
    const unsigned offset = phys & (line_bytes - 1);
    value = line.backing != UINT64_MAX ? read_ram(line.backing + offset) : 0;
    if (!line.overlay) return true;
    for (unsigned i = 0; i < unsigned(bits / 8); ++i) {
      if ((line.overlay >> (offset + i)) & 1) {
        const auto byte = std::uint64_t(line.data[offset + i]) << (i * 8);
        value = (value & ~(UINT64_C(0xff) << (i * 8))) | byte;
      }
    }
    return true;
  }

  void fill(std::uint64_t phys, std::uint64_t backing, std::uint64_t ram_size)
  {
    if (backing >= ram_size || phys >> 34) {
      // HRM 6.4.10.3: a noncached read normally invalidates the indexed Dcache line.
      if (!(control & ncache_ndisturb) && !lines.empty()) lines[index(phys)].tag = UINT64_MAX;
      return;
    }
    if (lines.empty()) lines.resize(line_count);
    lines[index(phys)] = {base(phys), base(backing)};
  }

  bool store(std::uint64_t phys, std::uint64_t backing, int bits, std::uint64_t value)
  {
    if (!hits(phys)) return false;
    if (lines.empty()) lines.resize(line_count);
    auto& line = lines[index(phys)];
    // Normal hits share coherent RAM; a forced alias changes SRAM and writes through separately.
    const unsigned offset = phys & (line_bytes - 1);
    const unsigned count = bits / 8;
    const auto mask = ((UINT64_C(1) << count) - 1) << offset;
    if (line.backing == base(backing)) {
      line.overlay &= ~mask;
    } else {
      line.overlay |= mask;
      for (unsigned i = 0; i < count; ++i)
        line.data[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
    }
    return true;
  }

  bool valid_lines(std::uint64_t ram_size) const
  {
    if (lines.empty()) return true;
    if (lines.size() != line_count) return false;
    for (unsigned i = 0; i < line_count; ++i) {
      const auto& line = lines[i];
      if (line.tag != UINT64_MAX && (!(control & enable) || (line.tag >> 34) || line.tag != base(line.tag)
          || index(line.tag) != i || line.backing == UINT64_MAX)) return false;
      if (line.backing != UINT64_MAX && (line.backing >= ram_size || (line.backing >> 34)
          || line.backing != base(line.backing) || index(line.backing) != i)) return false;
      if (line.overlay >> line_bytes) return false;
    }
    return true;
  }
};

#endif
