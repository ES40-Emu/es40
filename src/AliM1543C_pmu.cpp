/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
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
  * Stub PMU/ACPI device at PCI 0:17:0 for the ALi M1543C bridge.
  *
  **/
#include "StdAfx.h"
#include "AliM1543C_pmu.h"
#include "AliM1543C.h"
#include "System.h"
#include <chrono>

// PCI config-space defaults.
//   CFID  = 0x710110b9 : ALi M7101 power-management function
//   CFCS  = 0x02800000 : DEVSEL=medium; OS programs CMD bits as it goes
//   CFRV  = 0x06800000 : class 0x06 / subclass 0x80 (bridge / other)
//   CFIT  = 0x00000000 : M1543C datasheet page 78: PMU config 30h-3Fh is reserved
//   BAR0  = 0x00000001 : 64-byte I/O region (PM1 + GPE0 block)
//   BAR1  = 0x00000001 : 32-byte I/O region (SMBus host)
static u32 pmu_cfg_data[64] = {
	/*00*/  0x710110b9,
	/*04*/  0x02000000,
	/*08*/  0x06800000,
	/*0c*/  0x00000000,
	/*10*/  0x00000001,
	/*14*/  0x00000001,
	/*18*/  0x00000000,
	/*1c*/  0x00000000,
	/*20*/  0x00000000,
	/*24*/  0x00000000,
	/*28*/  0x00000000,
	/*2c*/  0x00000000,
	/*30*/  0x00000000,
	/*34*/  0x00000000,
	/*38*/  0x00000000,
	/*3c*/  0x00000000,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// PCI config-space write masks.
//   CFCS  bit 0x1 only : M1543C datasheet page 77: BME/MEM always 0
//         I/O Space Enable is the sole R/W command bit
//   CFLT  bits 0xff00 : latency-timer field
//   BAR0  mask 0xffffffc0 : 64-byte alignment, low bit (IO type) read-only
//   BAR1  mask 0xffffffe0 : 32-byte alignment
//   CFIT  read-only zero (reserved range)
//   68h, 6Ch, 70h, A4h: address selectors used by M1543C docking decode.
static u32 pmu_cfg_mask[64] = {
	/*00*/  0x00000000,
	/*04*/  0x00000001,
	/*08*/  0x00000000,
	/*0c*/  0x0000ff00,
	/*10*/  0xffffffc0,
	/*14*/  0xffffffe0,
	/*18*/  0x00000000,
	/*1c*/  0x00000000,
	/*20*/  0x00000000,
	/*24*/  0x00000000,
	/*28*/  0x00000000,
	/*2c*/  0x00000000,
	/*30*/  0x00000000,
	/*34*/  0x00000000,
	/*38*/  0x00000000,
	/*3c*/  0x00000000,
	/*40*/  0,0,0,0,0,0,0,0,
	/*60*/  0,0,
	/*68*/  0x00000001, // FDD address selector
	/*6c*/  0x0000fffc, // Audio I/O address selectors
	/*70*/  0x000007ff, // Serial and parallel I/O address selectors
	/*74*/  0,0,0,
	/*80*/  0,0,0,0,0,0,0,0,
	/*a0*/  0,
	/*a4*/  0x0000ffff, // I/O Group C address and mask
	/*a8*/  0,0,0,0,0,0,
	/*c0*/  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

CAliM1543C_pmu::CAliM1543C_pmu(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev)
	: CPCIDevice(cfg, c, pcibus, pcidev)
{
	add_function(0, pmu_cfg_data, pmu_cfg_mask);

	for (int i = 0; i < (int)sizeof(state.pm_block); i++)
		state.pm_block[i] = 0;
	for (int i = 0; i < (int)sizeof(state.smb_block); i++)
		state.smb_block[i] = 0;

	// SMBus host status: bit 0 = HOST_BUSY, leave clear; bit 1 = INTR done,
	// leave clear so the first probe sees an idle controller.
	state.smb_block[0x00] = 0x00;

	state.pm_timer_anchor_us = 0;

	ResetPCI();

	printf("%s: $Id$\n", devid_string);
}

CAliM1543C_pmu::~CAliM1543C_pmu() {}

void CAliM1543C_pmu::bind_isa_bridge(CAliM1543C* bridge)
{
	isa_bridge = bridge;
}

const CSystemComponent* CAliM1543C_pmu::memory_decode_owner() const noexcept
{
	return isa_bridge ? static_cast<const CSystemComponent*>(isa_bridge) : this;
}

bool CAliM1543C_pmu::uses_subtractive_decode(int index, u64, int, bool) const noexcept
{
	// M1543C 3.5(e/h): ACPI/SMBus I/O follows 44h<6>, except in docking mode.
	return isa_bridge && (index == PCI_RANGE_BASE || index == PCI_RANGE_BASE + 1) &&
		isa_bridge->programmable_io_is_subtractive();
}

bool CAliM1543C_pmu::decodes_memory_access(int index, u64 address, int dsize,
	bool write) const noexcept
{
	// 5Fh<2> hides PCI configuration; PMU operation remains programmable via ALi.
	if (index == PCI_RANGE_BASE + 7 && isa_bridge && isa_bridge->is_pmu_hidden())
		return false;
	return CPCIDevice::decodes_memory_access(index, address, dsize, write);
}

u32 CAliM1543C_pmu::docking_config(u32 aligned_offset) const noexcept
{
	if (aligned_offset >= 0x100 || (aligned_offset & 3))
		return 0;
	return endian_32(pci_state.config_data[0][aligned_offset / 4]);
}

u32 CAliM1543C_pmu::ReadMem_Bar(int func, int bar, u32 address, int dsize)
{
	switch (bar)
	{
	case 0:   return pm_io_read(address, dsize);
	case 1:   return smb_io_read(address, dsize);
	default:
		printf("%%PMU-W-READBAR: bad BAR %d.\n", bar);
		return 0;
	}
}

void CAliM1543C_pmu::WriteMem_Bar(int func, int bar, u32 address, int dsize, u32 data)
{
	switch (bar)
	{
	case 0:   pm_io_write(address, dsize, data); return;
	case 1:   smb_io_write(address, dsize, data); return;
	default:
		printf("%%PMU-W-WRITEBAR: bad BAR %d.\n", bar);
	}
}

// Free-running 32-bit timer at 3.579545 MHz, the ACPI-fixed PM-timer rate.
// Returned as ticks since first read of the device, so the count starts at
// 0 the first time anyone looks and advances monotonically thereafter.
u32 CAliM1543C_pmu::pm_timer_value()
{
	using clk = std::chrono::steady_clock;
	auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
		clk::now().time_since_epoch()).count();
	if (state.pm_timer_anchor_us == 0)
		state.pm_timer_anchor_us = (u64)now_us;
	u64 elapsed_us = (u64)now_us - state.pm_timer_anchor_us;
	// 3.579545 MHz → 3.579545 ticks per us.  Use a /1000 factor that's close
	// enough for delay-calibration loops without needing floating-point.
	return (u32)((elapsed_us * 3579545ull) / 1000000ull);
}

// PM I/O block, 64 bytes wide.  Layout the OS expects:
//   0x00-0x01  PM1_STS    (RW1C in real silicon, plain RW here)
//   0x02-0x03  PM1_EN
//   0x04-0x05  PM1_CNT
//   0x08-0x0B  PM1_TMR    (32-bit, the only register with live behaviour)
//   0x18-0x19  GPE0_STS
//   0x1A-0x1B  GPE0_EN
u32 CAliM1543C_pmu::pm_io_read(u32 address, int dsize)
{
	const u32 off = address & 0x3f;

	if (off == 0x08 && dsize == 32)
	{
		u32 v = pm_timer_value();
#ifdef DEBUG_PMU
		printf("%%PMU-I-PMTMR: read %08x\n", v);
#endif
		return v;
	}

	u32 v = 0;
	const int bytes = dsize / 8;
	for (int i = 0; i < bytes && (off + i) < (int)sizeof(state.pm_block); i++)
		v |= ((u32)state.pm_block[off + i]) << (8 * i);

#ifdef DEBUG_PMU
	printf("%%PMU-I-PMRD: off=%02x dsize=%d -> %08x\n", off, dsize, v);
#endif
	return v;
}

void CAliM1543C_pmu::pm_io_write(u32 address, int dsize, u32 data)
{
	const u32 off = address & 0x3f;

#ifdef DEBUG_PMU
	printf("%%PMU-I-PMWR: off=%02x dsize=%d data=%08x\n", off, dsize, data);
#endif

	const int bytes = dsize / 8;
	for (int i = 0; i < bytes && (off + i) < (int)sizeof(state.pm_block); i++)
		state.pm_block[off + i] = (u8)(data >> (8 * i));
}

// SMBus I/O block, 32 bytes wide.  Stub: stores writes, returns last
// written value on read.  No transactions are actually performed.
u32 CAliM1543C_pmu::smb_io_read(u32 address, int dsize)
{
	const u32 off = address & 0x1f;

	u32 v = 0;
	const int bytes = dsize / 8;
	for (int i = 0; i < bytes && (off + i) < (int)sizeof(state.smb_block); i++)
		v |= ((u32)state.smb_block[off + i]) << (8 * i);

#ifdef DEBUG_PMU
	printf("%%PMU-I-SMBRD: off=%02x dsize=%d -> %08x\n", off, dsize, v);
#endif
	return v;
}

void CAliM1543C_pmu::smb_io_write(u32 address, int dsize, u32 data)
{
	const u32 off = address & 0x1f;

#ifdef DEBUG_PMU
	printf("%%PMU-I-SMBWR: off=%02x dsize=%d data=%08x\n", off, dsize, data);
#endif

	const int bytes = dsize / 8;
	for (int i = 0; i < bytes && (off + i) < (int)sizeof(state.smb_block); i++)
		state.smb_block[off + i] = (u8)(data >> (8 * i));

	// SMBus host status (offset 0).  Bit 0 (HOST_BUSY) auto-clears so the
	// next status read shows the (stub) transaction completed; bit 1
	// (INTR/done) gets set so polled drivers see success.
	if (off == 0x00)
	{
		state.smb_block[0] &= ~0x01;
		state.smb_block[0] |= 0x02;
	}
}

static u32 pmu_magic1 = 0x71011533;
static u32 pmu_magic2 = 0x33151071;

int CAliM1543C_pmu::SaveState(FILE* f)
{
	long ss = sizeof(state);
	int  res;

	if ((res = CPCIDevice::SaveState(f)))
		return res;

	fwrite(&pmu_magic1, sizeof(u32), 1, f);
	fwrite(&ss, sizeof(long), 1, f);
	fwrite(&state, sizeof(state), 1, f);
	fwrite(&pmu_magic2, sizeof(u32), 1, f);
	printf("%s: %d bytes saved.\n", devid_string, (int)ss);
	return 0;
}

int CAliM1543C_pmu::RestoreState(FILE* f)
{
	long   ss;
	u32    m1, m2;
	int    res;
	size_t r;

	if ((res = CPCIDevice::RestoreState(f)))
		return res;

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1) { printf("%s: unexpected end of file!\n", devid_string); return -1; }
	if (m1 != pmu_magic1) { printf("%s: MAGIC 1 does not match!\n", devid_string); return -1; }

	r = fread(&ss, sizeof(long), 1, f);
	if (r != 1) { printf("%s: unexpected end of file!\n", devid_string); return -1; }
	if (ss != sizeof(state)) { printf("%s: STRUCT SIZE does not match!\n", devid_string); return -1; }

	r = fread(&state, sizeof(state), 1, f);
	if (r != 1) { printf("%s: unexpected end of file!\n", devid_string); return -1; }

	r = fread(&m2, sizeof(u32), 1, f);
	if (r != 1) { printf("%s: unexpected end of file!\n", devid_string); return -1; }
	if (m2 != pmu_magic2) { printf("%s: MAGIC 2 does not match!\n", devid_string); return -1; }

	printf("%s: %d bytes restored.\n", devid_string, (int)ss);
	return 0;
}
