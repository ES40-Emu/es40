/* ES40 emulator -- EV4 physical bootstrap profile
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#if !defined(INCLUDED_EV4CPUPROFILE_H)
#define INCLUDED_EV4CPUPROFILE_H

#include "Ev4Reset.h"
#include "Ev4InstructionFetch.h"
#include "Ev4IPR.h"

// 21064 HRM 5.2.3: ICCSR controls FPE, kernel HWE and the I-stream ASN.
constexpr AlphaCPUProfile ev4_ibox_profile(AlphaCPUProfile profile)
{
  if (!profile.fp_start.enabled)
    profile.fp_start.enabled = [](const CAlphaCPU& cpu) { return (cpu.state.ibox_ctl & kEv4IccsrFpe) != 0; };
  if (!profile.pal.access_context)
    profile.pal.access_context = [](const CAlphaCPU& cpu) -> AlphaPALAccessContext {
      return {static_cast<std::uint32_t>(cpu.state.cm), (cpu.state.ibox_ctl & kEv4IccsrHwe) != 0};
    };
  if (!profile.mmu_address.access)
    profile.mmu_address.access = [](const CAlphaCPU& cpu, int flags) -> AlphaMMUAccess {
      return {(flags & ALT) ? cpu.state.alt_cm : cpu.state.cm,
              (flags & ACCESS_EXEC) ? static_cast<int>(ev4_iccsr_asn(cpu.state.ibox_ctl)) : 0};
    };
  return profile;
}

// Select external PAL fetch at reset while preserving the model's execution rules.
constexpr AlphaCPUProfile ev4_no_srom_boot_profile(AlphaCPUProfile profile)
{
  profile.reset = kEv4ResetPolicy;
  profile.instruction_fetch = kEv4InstructionFetchPolicy;
  return ev4_ibox_profile(profile);
}

// rom.srom on the board supplies the packed serial PROM.
constexpr AlphaCPUProfile ev4_srom_boot_profile(AlphaCPUProfile profile)
{
  profile.reset = kEv4SromResetPolicy;
  profile.instruction_fetch = kEv4InstructionFetchPolicy;
  return ev4_ibox_profile(profile);
}

#endif
