/* ES40 emulator -- EV4 backup cache
 * Copyright (C) 2026 by gdwnldsKSC
 * SPDX-License-Identifier: BSD-1-Clause
 */
#ifndef INCLUDED_EV4BCACHE_H
#define INCLUDED_EV4BCACHE_H
#include <cstdint>
#include <vector>

struct Ev4Bcache
{
  std::uint64_t control = 0;
  std::vector<std::uint64_t> tags;
  // HRM Fig. 5-26/Table 5-12: later 21064 steppings add bits 43:40, 38 and 12.
  static constexpr std::uint64_t control_mask = UINT64_C(0xf5fffffffff);

  // 21064 HRM 6.5.4 and Table 5-13 specify direct-mapped 32-byte tags and BC_SIZE.
  static constexpr unsigned line_count(std::uint64_t ctl)
  {
    const unsigned size = (ctl >> 28) & 7;
    return 4096u << size;
  }

  std::uint64_t probe(std::uint64_t phys, std::uint64_t ram_size)
  {
    // HRM Tables 5-12/5-14: BC_ENA and the PA[33:32] disable take priority over FHIT.
    const unsigned quadrant = (phys >> 32) & 3;
    if (!(control & 1) || phys >> 34 || ((control >> (32 + quadrant)) & 1))
      return phys;
    const unsigned count = line_count(control);
    if (tags.size() != count)
      tags.assign(count, UINT64_MAX);
    auto& tag = tags[(phys >> 5) & (count - 1)];
    // HRM A.5 describes writeback. The emulator shares one coherent RAM copy with DMA.
    const auto base = phys & ~std::uint64_t(31);
    if (tag == base)
      return phys;
    if ((control & 8) && tag != UINT64_MAX)
      return tag | (phys & 31);
    // An uninitialized SRAM slot takes its first RAM line as deterministic cold data.
    if (phys < ram_size)
      tag = base;
    return phys;
  }
};

#endif
