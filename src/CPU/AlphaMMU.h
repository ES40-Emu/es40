/* ES40 emulator -- Alpha translation policies
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
#if !defined(INCLUDED_ALPHAMMU_H)
#define INCLUDED_ALPHAMMU_H

#include <cstdint>

class CAlphaCPU;

// Decoded input to the shared translation cache, outside the saved CPU state.
struct AlphaTBFill
{
  std::uint64_t virt;       // Keep offset bits for refill hints; stored tags use match_mask.
  std::uint64_t phys;       // Masked physical base.
  std::uint64_t match_mask;
  std::uint64_t keep_mask;
  // Nonzero permits/faults. Retain EV68's integer masks for snapshot compatibility.
  int access[2][4];         // [read/write][current mode].
  int fault[3];            // [read/write/execute].
  int asm_bit;             // Global mapping, independent of the caller's ASN.
};

// Read-only family decoding. The caller owns ASN selection and fill side effects.
struct AlphaTBFillPolicy
{
  AlphaTBFill (*decode)(const CAlphaCPU& cpu, std::uint64_t virt,
                        std::uint64_t pte_phys, std::uint64_t pte_flags,
                        int access_flags);
};

// Family fault state and PAL/HLE delivery, called only for real TB misses.
struct AlphaTBMissPolicy
{
  // Zero retries translation with RECUR; any nonzero result ends this lookup.
  // The handler must stop an unresolved RECUR miss.
  int (*handle)(CAlphaCPU& cpu, std::uint64_t virt, int access_flags,
                 std::uint32_t instruction);
};

// Effective permissions and address space for a translation-buffer lookup.
struct AlphaMMUAccess
{
  int mode;
  int asn;
};

enum class AlphaMMUAddressKind
{
  TranslationBuffer,
  Direct,
  Invalid,
  AccessViolation
};

struct AlphaMMUAddress
{
  AlphaMMUAddressKind kind;
  std::uint64_t phys; // Used only for Direct; direct mappings are ASN-independent.
};

// Read-only family address rules. The shared core performs lookup and faults.
// Changes to family address controls must invalidate the relevant host caches.
struct AlphaMMUAddressPolicy
{
  AlphaMMUAccess (*access)(const CAlphaCPU& cpu, int access_flags);
  AlphaMMUAddress (*map)(const CAlphaCPU& cpu, std::uint64_t virt,
                         int access_flags, int mode);
};

#endif
