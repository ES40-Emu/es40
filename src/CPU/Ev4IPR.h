/* ES40 emulator -- EV4 privileged-register rules
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
#if !defined(INCLUDED_EV4IPR_H)
#define INCLUDED_EV4IPR_H

#include "AlphaIPR.h"

struct Ev4IPRSelector
{
  std::uint32_t index;
  std::uint32_t banks;
};

// PAL[7], ABX[6], IBX[5], INDEX[4:0] (HRM Table 4-6).
constexpr Ev4IPRSelector ev4_ipr_selector(std::uint32_t instruction)
{
  return {instruction & 0x1f, instruction & 0xe0};
}

constexpr bool ev4_ipr_ignored(Ev4IPRSelector selector, bool pal_mode)
{
  // HRM 4.6(10); mixed-bank accesses stay with their handlers.
  return selector.banks == 0 || (!pal_mode && selector.banks == 0x20
    && (selector.index == 0 || selector.index == 1 || selector.index == 3));
}

// Unimplemented registers stay with the interpreter.
constexpr AlphaIPRReadDescriptor ev4_describe_ipr_read(std::uint32_t instruction,
                                                      bool pal_mode)
{
  if ((instruction >> 26) != 0x19)
    return {AlphaIPRReadKind::Unsupported, false, false};
  const auto selector = ev4_ipr_selector(instruction);
  if (ev4_ipr_ignored(selector, pal_mode))
    return {AlphaIPRReadKind::Noop, false, true};
  if (selector.banks == 0x20 && selector.index == 2) // ICCSR (21064 HRM Table 4-7).
    return {AlphaIPRReadKind::IboxControl, false, true};
  if (selector.banks == 0x80) // PAL_TEMP only.
    return {AlphaIPRReadKind::PalTemp, false, true, static_cast<std::uint8_t>(selector.index)};
  if (selector.banks == 0x20 && selector.index == 4) // EXC_ADDR.
    return {AlphaIPRReadKind::ExceptionAddress, false, true};
  if (selector.banks == 0x20 && selector.index == 9) // PS.
    return {AlphaIPRReadKind::Helper, false, true};
  if (selector.banks == 0x20 && selector.index == 10) // EXC_SUM shifts MSK, even to R31.
    return {AlphaIPRReadKind::Helper, false, false};
  if (selector.banks == 0x20 && selector.index == 11) // PAL_BASE.
    return {AlphaIPRReadKind::PalBase, false, true};
  return {AlphaIPRReadKind::Unsupported, false, false};
}

constexpr AlphaIPRWriteKind ev4_describe_ipr_write(std::uint32_t instruction,
                                                  bool pal_mode, unsigned* slot = nullptr)
{
  if ((instruction >> 26) != 0x1d)
    return AlphaIPRWriteKind::Interpreter;
  const auto selector = ev4_ipr_selector(instruction);
  if (ev4_ipr_ignored(selector, pal_mode))
    return AlphaIPRWriteKind::Noop;
  // 21064 HRM 4.8.1, Table 4-7: ABX selects ABOX_CTL (14) or BIU_CTL (18), PAL writes the same-index PAL_TEMP.
  if ((selector.banks == 0x40 || selector.banks == 0xc0) &&
      (selector.index == 14 || selector.index == 18)) {
    if (slot) *slot = (selector.banks & 0x80) ? 32u + selector.index : 0;
    return selector.index == 14 ? AlphaIPRWriteKind::AboxControl : AlphaIPRWriteKind::BcacheControl;
  }
  if (selector.banks == 0x80) { // PAL_TEMP only.
    if (slot) *slot = 32u + selector.index;
    return AlphaIPRWriteKind::PalTemp;
  }
  if (selector.banks == 0x20 && selector.index == 4) // EXC_ADDR.
    return AlphaIPRWriteKind::ExceptionAddress;
  if (selector.banks == 0x20 && selector.index == 9) // PS.
    return AlphaIPRWriteKind::Helper;
  if (selector.banks == 0x20 && selector.index == 10) // EXC_SUM.
    return AlphaIPRWriteKind::Helper;
  if (selector.banks == 0x20 && selector.index == 11) // PAL_BASE.
    return AlphaIPRWriteKind::Helper;
  // IBX and PAL together also write the same-index PAL_TEMP (21064 HRM 4.8.1, Table 4-7).
  if (selector.banks == 0x20 || selector.banks == 0xa0) {
    auto kind = AlphaIPRWriteKind::Interpreter;
    switch (selector.index) {
    case 2:  kind = AlphaIPRWriteKind::IboxControl; break;
    case 16: kind = AlphaIPRWriteKind::HardwareInterruptEnable; break;
    case 17: kind = AlphaIPRWriteKind::SoftwareInterruptEnable; break;
    case 19: kind = AlphaIPRWriteKind::InterruptClear; break;
    default: return kind;
    }
    if (slot) *slot = (selector.banks & 0x80) ? 32u + selector.index : 0;
    return kind;
  }
  if (selector.banks == 0x40 && selector.index == 15) // ALT_MODE.
    return AlphaIPRWriteKind::Helper;
  if (selector.banks == 0x40 && selector.index == 16) // CC.
    return AlphaIPRWriteKind::Helper;
  return AlphaIPRWriteKind::Interpreter;
}

// 21064 HRM 5.2.6, Fig. 5-6 and Table 5-5: SLC, PC1, PC0 and CRD are write-zero-to-clear.
inline void ev4_write_sl_clr(std::uint64_t value, int& slr, int& pcr, int& crr)
{
  if (!(value & (UINT64_C(1) << 32))) slr = 0;
  if (!(value & (UINT64_C(1) << 15))) pcr &= ~2;
  if (!(value & (UINT64_C(1) << 8)))  pcr &= ~1;
  if (!(value & (UINT64_C(1) << 2)))  crr = 0;
}

// HIER replaces the IRQ, PC1/0, SLE and CRE enables (21064 HRM 5.2.17, Fig. 5-14 and Table 5-8).
inline void ev4_write_hier(std::uint64_t value, int& eien, int& pcen, int& slen, int& cren)
{
  eien = (value >> 9) & 0x3f;
  pcen = (((value >> 15) & 1) << 1) | ((value >> 8) & 1);
  slen = (value >> 32) & 1;
  cren = (value >> 2) & 1;
}

// Bits 47:33 write SIER[15:1]. SIER[2] also gates ASTs (21064 HRM 5.2.16 and 5.2.18, Fig. 5-15).
inline void ev4_write_sier(std::uint64_t value, int& sien, int& asten)
{
  sien = (value >> 32) & 0xfffe;
  asten = (sien >> 2) & 1;
}

// 21064 HRM 5.2.3, Fig. 5-3: ICCSR has distinct HW_MTPR and HW_MFPR layouts.
// Keep the defined controls and read the Digital-reserved fields as zero.
constexpr std::uint64_t ev4_iccsr_from_write(std::uint64_t value)
{
  return (((value >> 47) & 0x3f) << 28)   // ASN
       | (value & (UINT64_C(3) << 44))     // PCE
       | (((value >> 32) & 0x7ff) << 13)  // FPE..PIPE and PCMUX1
       | (((value >> 8) & 0xf) << 9)      // PCMUX0
       | (((value >> 3) & 1) << 1)        // PC0
       | ((value & 1) << 2);              // PC1
}

inline constexpr std::uint64_t kEv4IccsrFpe = UINT64_C(1) << 23;
inline constexpr std::uint64_t kEv4IccsrMap = UINT64_C(1) << 22;
inline constexpr std::uint64_t kEv4IccsrHwe = UINT64_C(1) << 21;
inline constexpr std::uint64_t kEv4IccsrAsnMask = UINT64_C(0x3f) << 28;

constexpr unsigned ev4_iccsr_asn(std::uint64_t iccsr)
{
  return static_cast<unsigned>((iccsr & kEv4IccsrAsnMask) >> 28);
}

// PS reads use bits 34/1; writes use bits 4:3 (HRM 5.2.11).
constexpr std::uint64_t ev4_read_processor_status(std::uint32_t mode)
{
  return (std::uint64_t(mode & 1) << 1) | (std::uint64_t(mode & 2) << 33);
}

constexpr std::uint32_t ev4_decode_processor_status(std::uint64_t value)
{
  return static_cast<std::uint32_t>((value >> 3) & 3);
}

// ALT_MODE selects the alternate mode with bits 4:3 (HRM 5.3.12).
constexpr std::uint32_t ev4_decode_alternate_mode(std::uint64_t value)
{
  return static_cast<std::uint32_t>((value >> 3) & 3);
}

// CC writes update only the offset in bits 63:32 (HRM 5.3.13).
constexpr std::uint32_t ev4_decode_cycle_counter_offset(std::uint64_t value)
{
  return static_cast<std::uint32_t>(value >> 32);
}

struct Ev4CycleCounterControl
{
  std::uint32_t counter;
  bool enabled;
};

// CC_CTL: counter[31:0], enable[32]; bits 3:0 must be zero (HRM 5.3.14).
constexpr Ev4CycleCounterControl ev4_decode_cycle_counter_control(std::uint64_t value)
{
  return {static_cast<std::uint32_t>(value), ((value >> 32) & 1) != 0};
}

// PAL_BASE holds bits 33:14 (HRM 5.2.13).
constexpr std::uint64_t ev4_normalize_pal_base(std::uint64_t value)
{
  return value & UINT64_C(0x00000003ffffc000);
}

// PAL_TEMP reset contents are architecturally undefined.
struct Ev4PALTempState
{
  std::uint64_t r[32]{};
};

constexpr std::uint64_t ev4_read_pal_temp(const Ev4PALTempState& state,
                                         std::uint32_t index)
{
  return state.r[index & 31];
}

constexpr void ev4_write_pal_temp(Ev4PALTempState& state, std::uint32_t index,
                                  std::uint64_t value)
{
  state.r[index & 31] = value;
}

#endif
