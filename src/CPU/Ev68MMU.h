/* ES40 emulator -- EV68 translation and fault rules
 * Copyright (C) 2026 by gdwnldsKSC
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#if !defined(INCLUDED_EV68MMU_H)
#define INCLUDED_EV68MMU_H

#include "AlphaMMU.h"
#include "AlphaDataFault.h"

class Ev68MMU
{
public:
  static AlphaMMUAccess access(const CAlphaCPU& cpu, int access_flags);
  static AlphaMMUAddress map_address(const CAlphaCPU& cpu, std::uint64_t virt,
                                     int access_flags, int mode);
  static AlphaTBFill decode_fill(const CAlphaCPU& cpu, std::uint64_t virt,
                                 std::uint64_t pte_phys, std::uint64_t pte_flags,
                                 int access_flags);
  static int tb_miss(CAlphaCPU& cpu, std::uint64_t virt, int access_flags,
                      std::uint32_t instruction);
  static int access_violation(CAlphaCPU& cpu, std::uint64_t virt,
                               int access_flags, std::uint32_t instruction,
                               AlphaDataFaultSource source);
  static void unaligned(CAlphaCPU& cpu, std::uint64_t virt,
                         int access_flags, std::uint32_t instruction, int alignment_mask);
};

inline constexpr AlphaTBFillPolicy kEv68TBFillPolicy{&Ev68MMU::decode_fill};
inline constexpr AlphaTBMissPolicy kEv68TBMissPolicy{&Ev68MMU::tb_miss};
inline constexpr AlphaMMUAddressPolicy kEv68MMUAddressPolicy{
  &Ev68MMU::access, &Ev68MMU::map_address
};
inline constexpr AlphaDataFaultPolicy kEv68DataFaultPolicy{
  &Ev68MMU::access_violation, &Ev68MMU::unaligned
};

#endif
