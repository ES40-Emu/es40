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

// The small and large ITB arrays have independent replacement pointers (21064 HRM 5.2.2).
inline constexpr unsigned kEv4SmallItbEntries = 8, kEv4LargeItbEntries = 4;
inline constexpr unsigned kEv4ItbEntries = kEv4SmallItbEntries + kEv4LargeItbEntries;

// The two BIU error groups can power up locked and survive reset (21064 HRM 5.3.18, Table 5-20).
struct Ev4BIUDiagnostics
{
  std::uint64_t bus_address = 0, fill_address = 0;
  std::uint16_t status = 0, fill_syndrome = 0;
  std::uint16_t bus_unlocked = 0, fill_unlocked = 0;
  std::uint32_t bc_tag = 0, bc_tag_unlocked = 0; // Potentially locked at reset (21064 HRM Table 5-20).
};

// PAL[7], ABX[6], IBX[5], INDEX[4:0] (HRM Table 4-6).
constexpr Ev4IPRSelector ev4_ipr_selector(std::uint32_t instruction)
{
  return {instruction & 0x1f, instruction & 0xe0};
}

constexpr bool ev4_selects_bc_tag(Ev4IPRSelector selector)
{
  return selector.index == 20 && (selector.banks & 0x40);
}

constexpr bool ev4_selects_icache_flush(Ev4IPRSelector selector)
{
  return (selector.banks & 0x40) && (selector.index == 21 || selector.index == 23);
}

constexpr bool ev4_cycle_counter_index(std::uint32_t index)
{
  return index == 16 || index == 17;
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
  if (selector.banks == 0x20 && selector.index == 1) // ITB_PTE advances the selected pointer even to R31 (21064 HRM 5.2.2).
    return {AlphaIPRReadKind::Helper, false, false};
  if (selector.banks == 0x20 && selector.index == 2) // ICCSR (21064 HRM Table 4-7).
    return {AlphaIPRReadKind::IboxControl, false, true};
  if (selector.banks == 0x20 && selector.index == 3) // ITB_PTE_TEMP returns the latched PTE (21064 HRM 5.2.4).
    return {AlphaIPRReadKind::Helper, false, true};
  if (selector.banks == 0x80) // PAL_TEMP only.
    return {AlphaIPRReadKind::PalTemp, false, true, static_cast<std::uint8_t>(selector.index)};
  if (selector.banks == 0x20 && selector.index == 4) // EXC_ADDR.
    return {AlphaIPRReadKind::ExceptionAddress, false, true};
  if (selector.banks == 0x40) {
    switch (selector.index) {
    case 4: // MM_CSR (21064 HRM 5.3.4).
      return {AlphaIPRReadKind::MemoryFaultStatus, false, true};
    // Reading VA unlocks VA and MM_CSR even to R31 (21064 HRM Table 4-7, 5.3.4, 5.3.5).
    case 5:
    // BIU_ADDR and FILL_ADDR unlock their groups even to R31 (21064 HRM 5.3.19-20).
    case 9:
    case 13:
    // DC_ADDR unlocks DC_STAT even to R31 (21064-AA data sheet, Table 3-2 and 3.10.2).
    case 11:
    case 20: // BC_TAG shifts even to R31 (21064 HRM 5.3.22).
      return {AlphaIPRReadKind::Helper, false, false};
    case 10: // BIU_STAT
    case 12: // DC_STAT
    case 19: // FILL_SYNDROME
      return {AlphaIPRReadKind::Helper, false, true};
    case 16: // CC reads the running counter and offset (21064 HRM 5.3.13).
      return {AlphaIPRReadKind::Helper, true, true};
    }
  }
  if (selector.banks == 0x20 && selector.index == 9) // PS.
    return {AlphaIPRReadKind::Helper, false, true};
  if (selector.banks == 0x20 && selector.index == 10) // EXC_SUM shifts MSK, even to R31.
    return {AlphaIPRReadKind::Helper, false, false};
  if (selector.banks == 0x20 && selector.index == 11) // PAL_BASE.
    return {AlphaIPRReadKind::PalBase, false, true};
  if (selector.banks == 0x20 && selector.index >= 12 && selector.index <= 14) // HIRR, SIRR, ASTRR.
    return {AlphaIPRReadKind::Helper, true, true};
  if (selector.banks == 0x20 && selector.index >= 16 && selector.index <= 18) // HIER, SIER, ASTER (21064 HRM 5.2.17-19).
    return {AlphaIPRReadKind::Helper, false, true};
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
  if (selector.index == 0) { // TB_CTL, PALmode TB_TAG and PAL_TEMP0 (21064 HRM 4.6, Table 4-7).
    const bool pal_temp = (selector.banks & 0x80) != 0;
    if (slot) *slot = pal_temp ? 32u : 0;
    if ((selector.banks & 0x40) || (pal_mode && (selector.banks & 0x20)))
      return AlphaIPRWriteKind::Helper;
    return pal_temp ? AlphaIPRWriteKind::PalTemp : AlphaIPRWriteKind::Noop;
  }
  // The PAL-only ITB_PTE write can also select PAL_TEMP1 (21064 HRM 4.6, 4.8.1, 5.2.2).
  if (selector.index == 1 && (selector.banks & 0x20)) {
    const bool pal_temp = (selector.banks & 0x80) != 0;
    if (slot) *slot = pal_temp ? 33u : 0;
    if (pal_mode) return AlphaIPRWriteKind::HelperTerminator;
    return pal_temp ? AlphaIPRWriteKind::PalTemp : AlphaIPRWriteKind::Noop;
  }
  // DTB_PTE can be written together with ICCSR and PAL_TEMP2 (21064 HRM 4.8.1, Table 4-7).
  if (selector.index == 2 && (selector.banks & 0x60)) {
    if (slot) *slot = (selector.banks & 0x80) ? 34u : 0;
    return (selector.banks & 0x20) ? AlphaIPRWriteKind::IboxControl : AlphaIPRWriteKind::HelperTerminator;
  }
  if (ev4_selects_icache_flush(selector)) { // PAL also selects PAL_TEMP21/23 (21064 HRM 4.8.1, Table 4-7).
    if (slot) *slot = (selector.banks & 0x80) ? 32u + selector.index : 0;
    return AlphaIPRWriteKind::HelperTerminator;
  }
  // PAL can write the same-index PAL_TEMP along with the Abox IPR (21064 HRM 4.8.1).
  if (ev4_selects_bc_tag(selector) || ((selector.banks & 0x40) && ev4_cycle_counter_index(selector.index))) {
    if (slot) *slot = (selector.banks & 0x80) ? 32u + selector.index : 0;
    return AlphaIPRWriteKind::Helper;
  }
  // Index 6 clears every entry and resets replacement in each selected TB (21064 HRM 4.8.1, 5.2.8, 5.3.6).
  if (selector.index == 6) {
    const bool itb = pal_mode && (selector.banks & 0x20); // ITBZAP is PAL-only.
    const bool dtb = (selector.banks & 0x40) != 0;
    const bool pal_temp = (selector.banks & 0x80) != 0;
    if (slot) *slot = pal_temp ? 32u + selector.index : 0;
    if (itb) return dtb ? AlphaIPRWriteKind::InvalidateBothTbs : AlphaIPRWriteKind::InvalidateInstructionTb;
    if (dtb) return AlphaIPRWriteKind::InvalidateDataTb;
    return pal_temp ? AlphaIPRWriteKind::PalTemp : AlphaIPRWriteKind::Noop;
  }
  // HIRR and DC_STAT ignore writes, leaving only PAL_TEMP12 writable (21064 HRM 4.8.1, 5.2.14, 5.3.16).
  if (selector.index == 12) {
    const bool pal_temp = (selector.banks & 0x80) != 0;
    if (slot) *slot = pal_temp ? 32u + selector.index : 0;
    return pal_temp ? AlphaIPRWriteKind::PalTemp : AlphaIPRWriteKind::Noop;
  }
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
  if (selector.banks == 0x20 && selector.index == 10) // EXC_SUM.
    return AlphaIPRWriteKind::Helper;
  if (selector.banks == 0x20 && selector.index == 11) // PAL_BASE.
    return AlphaIPRWriteKind::Helper;
  // IBX and PAL together also write the same-index PAL_TEMP (21064 HRM 4.8.1, Table 4-7).
  if (selector.banks == 0x20 || selector.banks == 0xa0) {
    auto kind = AlphaIPRWriteKind::Interpreter;
    switch (selector.index) {
    case 9:  kind = AlphaIPRWriteKind::Helper; break; // PS (21064 HRM 5.2.11).
    case 13: kind = AlphaIPRWriteKind::SoftwareInterruptRequest; break;
    case 14: kind = AlphaIPRWriteKind::AstInterruptRequest; break;
    case 16: kind = AlphaIPRWriteKind::HardwareInterruptEnable; break;
    case 17: kind = AlphaIPRWriteKind::SoftwareInterruptEnable; break;
    case 18: kind = AlphaIPRWriteKind::AstInterruptEnable; break;
    case 19: kind = AlphaIPRWriteKind::InterruptClear; break;
    default: return kind;
    }
    if (slot) *slot = (selector.banks & 0x80) ? 32u + selector.index : 0;
    return kind;
  }
  if (selector.banks == 0x40 && selector.index == 15) // ALT_MODE.
    return AlphaIPRWriteKind::Helper;
  return AlphaIPRWriteKind::Interpreter;
}

// HWR, SWR and ATR reflect enabled requests; ASTs also depend on mode and SIER[2] (21064 HRM 2.3.3, Table 5-7).
template<typename State>
inline unsigned ev4_interrupt_summary(const State& state, unsigned irq)
{
  const bool hwr = (state.eien & irq) || (state.pcen & state.pcr) ||
                   (state.slen & state.slr) || (state.cren & state.crr);
  const bool swr = (state.sien & state.sir) != 0;
  const bool atr = state.asten && (state.aster & state.astrr & ((1 << (state.cm + 1)) - 1));
  return (hwr << 1) | (swr << 2) | (atr << 3);
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

// SIRR writes replace requests 15:1 using the same field as SIER (21064 HRM 5.2.15, Fig. 5-12).
constexpr int ev4_software_interrupts_from_write(std::uint64_t value)
{
  return static_cast<int>((value >> 32) & 0xfffe);
}

// Bits 47:33 write SIER[15:1]. SIER[2] also gates ASTs (21064 HRM 5.2.16 and 5.2.18, Fig. 5-15).
inline void ev4_write_sier(std::uint64_t value, int& sien, int& asten)
{
  sien = ev4_software_interrupts_from_write(value);
  asten = (sien >> 2) & 1;
}

// ASTRR and ASTER use bits 51:48 for user, supervisor, executive and kernel (21064 HRM Figs. 5-13 and 5-16).
constexpr int ev4_ast_mask_from_write(std::uint64_t value)
{
  return static_cast<int>((value >> 48) & 0xf);
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

std::uint64_t ev4_read_ipr(CAlphaCPU& cpu, std::uint32_t instruction);
void ev4_write_ipr(CAlphaCPU& cpu, std::uint32_t instruction, std::uint64_t value,
                   AlphaIPRWriteContext context);

inline constexpr AlphaIPRReadPolicy kEv4IPRReadPolicy{&ev4_read_ipr, &ev4_describe_ipr_read};
inline constexpr AlphaIPRWritePolicy kEv4IPRWritePolicy{&ev4_write_ipr, &ev4_describe_ipr_write};

#endif
