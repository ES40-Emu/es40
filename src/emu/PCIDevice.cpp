/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2025-2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 *
 */

 /**
  * \file
  * Contains the code for the PCI device class.
  *
  * $Id$
  *
  * X-1.17       Camiel Vanderhoeven                             02-APR-2008
  *      Fixed compiler warnings.
  *
  * X-1.16       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.15       Brian Wheeler                                   27-FEB-2008
  *      Avoid compiler warnings.
  *
  * X-1.14       Camiel Vanderhoeven                             13-FEB-2008
  *      Added more DEBUG_PCI messages.
  *
  * X-1.13       Camiel Vanderhoeven                             08-FEB-2008
  *      Show originating device name on memory errors.
  *
  * X-1.12       Camiel Vanderhoeven                             06-FEB-2008
  *      Fixed registration of ROM expansion address.
  *
  * X-1.11       Camiel Vanderhoeven                             24-JAN-2008
  *      Added do_pci_read and do_pci_write. Thanks to David Hittner for
  *      suggesting this.
  *
  * X-1.10       Fang Zhe                                        03-JAN-2008
  *      Fixed semicolon error.
  *
  * X-1.9        Camiel Vanderhoeven                             03-JAN-2008
  *      Attempt to make PCI base device endianess-correct.
  *
  * X-1.8        Camiel Vanderhoeven                             29-DEC-2007
  *      Avoid referencing uninitialized data.
  *
  * X-1.7        Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.6        Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.5        Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.4        Camiel Vanderhoeven                             10-DEC-2007
  *      Translate a 64-bit PCI access into 2 32-bit accesses.
  *
  * X-1.3        Brian Wheeler                                   10-DEC-2007
  *      More verbose error reporting.
  *
  * X-1.2        Camiel Vanderhoeven                             10-DEC-2007
  *      Removed some printf's.
  *
  * X-1.1        Camiel Vanderhoeven                             10-DEC-2007
  *      Initial version in CVS.
  **/
#include "StdAfx.h"
#include "PCIDevice.h"
#include "system/System.h"
#include "CPU/diag_rpcc.h"

#include <chrono>
#include <cstdarg>

// Definition of the per-thread diagnostic-stall accumulator declared in diag_rpcc.h.
thread_local int64_t g_diag_excluded_ns = 0;

// printf() that times its console-I/O stall into g_diag_excluded_ns so jit_run keeps it out of the
// guest RPCC -- the PCI decode-off diagnostics below can spam during VGA init and jump the counter.
static void diag_printf(const char* fmt, ...)
{
	const auto t0 = std::chrono::steady_clock::now();
	va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
	g_diag_excluded_ns += (int64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - t0).count();
}

#define DEBUG_PCI 0

#define PCI_MEM_TYPE_MASK 0x00000006U
#define PCI_MEM_TYPE_64   0x00000004U

static bool pci_bar_is_upper_64bit(const u32* config, int bar)
{
	// Walk BAR boundaries: an upper address dword has no type attributes.
	for (int low = 0; low < bar; ++low)
	{
		const u32 attributes = endian_32(config[4 + low]);
		if ((attributes & 1U) == 0 &&
			(attributes & PCI_MEM_TYPE_MASK) == PCI_MEM_TYPE_64)
		{
			if (low + 1 == bar)
				return true;
			++low;
		}
	}
	return false;
}

static size_t pci_dma_chunk_limit(u64 phys_addr, size_t remaining)
{
	const size_t dma_page = 8192;
	size_t page_remaining = dma_page - (size_t)(phys_addr & (dma_page - 1));
	return (remaining < page_remaining) ? remaining : page_remaining;
}

CPCIDevice::CPCIDevice(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev) : CSystemComponent(cfg, c)
{
	int i;

	int j;

	for (i = 0; i < 8; i++)
	{
		device_at[i] = false;
		for (j = 0; j < 8; j++)
			pci_range_is_io[i][j] = false;
	}

	for (i = 0; i < MAX_DEV_RANGES; i++)
		dev_range_is_io[i] = false;

	myPCIBus = pcibus;
	myPCIDev = pcidev;
}

CPCIDevice::~CPCIDevice(void)
{
}

void CPCIDevice::add_function(int func, u32 data[64], u32 mask[64])
{
	memcpy(std_config_data[func], data, 64 * sizeof(u32));
	memcpy(std_config_mask[func], mask, 64 * sizeof(u32));
#if defined(ES40_BIG_ENDIAN)
	int i;
	for (i = 0; i < 64; i++)
	{
		std_config_data[func][i] = endian_32(std_config_data[func][i]);
		std_config_mask[func][i] = endian_32(std_config_mask[func][i]);
	}
#endif
	device_at[func] = true;
}

void CPCIDevice::add_legacy_io(int id, u32 base, u32 length)
{
	if (id < 0 || id >= MAX_DEV_RANGES)
		FAILURE(InvalidArgument, "Legacy PCI range index out of bounds");
	dev_range_is_io[id] = true;
	cSystem->RegisterMemory(this, id,
		U64(0x00000801fc000000) + (U64(0x0000000200000000) * myPCIBus) + base,
		length);
}

void CPCIDevice::add_legacy_mem(int id, u32 base, u32 length)
{
	if (id < 0 || id >= MAX_DEV_RANGES)
		FAILURE(InvalidArgument, "Legacy PCI range index out of bounds");
	dev_range_is_io[id] = false;
	cSystem->RegisterMemory(this, id,
		U64(0x0000080000000000) + (U64(0x0000000200000000) * myPCIBus) + base,
		length);
}

u32 CPCIDevice::config_read(int func, u32 address, int dsize)
{
	u8* x;

	u32   data = 0;

	x = (u8*)pci_state.config_data[func];
	x += address;

	switch (dsize)
	{
	case 8:   data = endian_8(*x); break;
	case 16:  data = endian_16(*((u16*)x)); break;
	case 32:  data = endian_32(*((u32*)x)); break;
	}

	data = config_read_custom(func, address, dsize, data);

	//  printf("%s(%s).%d config read  %d bytes @ %x = %x\n",myCfg->get_myName(), myCfg->get_myValue(), func,dsize/8,address, data);
	return data;
}

void CPCIDevice::config_write(int func, u32 address, int dsize, u32 data)
{

	//  printf("%s(%s).%d config write %d bytes @ %x = %x\n",myCfg->get_myName(), myCfg->get_myValue(), func,dsize/8,address, data);
	u8* x;
	u8* y;

	u32   mask = 0;
	u32   old_data = 0;
	u32   new_data = 0;

	x = (u8*)pci_state.config_data[func];
	x += address;
	y = (u8*)pci_state.config_mask[func];
	y += address;

#if defined(DEBUG_PCI)
	if (address == 0x3c && (data & 0xff) != 0xff)
		printf("%s.%d PCI Interrupt set to %02x.\n", devid_string, func, data & 0xff);
#endif
	switch (dsize)
	{
	case 8:
		data = endian_8(data);
		old_data = (*x) & 0xff;
		mask = (*y) & 0xff;
		new_data = (old_data & ~mask) | (data & mask);
		*x = (u8)new_data;
		break;

	case 16:
		data = endian_16(data);
		old_data = (*((u16*)x)) & 0xffff;
		mask = (*((u16*)y)) & 0xffff;
		new_data = (old_data & ~mask) | (data & mask);
		*((u16*)x) = (u16)new_data;
		break;

	case 32:
		data = endian_32(data);
		old_data = (*((u32*)x));
		mask = (*((u32*)y));
		new_data = (old_data & ~mask) | (data & mask);
		*((u32*)x) = new_data;
		break;
	}

	bool upper_64bit_bar = false;
	if (dsize == 32 && address >= 0x14 && address <= 0x24)
	{
		const int bar = (address - 0x10) / 4;
		upper_64bit_bar = pci_bar_is_upper_64bit(std_config_data[func], bar);
	}
	const u32 access_bytes = (u32)dsize / 8U;
	const bool rom_bar_write = address < 0x34U &&
		address + access_bytes > 0x30U;
	const u32 write_ones = dsize == 8 ? 0xFFU :
		(dsize == 16 ? 0xFFFFU : 0xFFFFFFFFU);

	/* Preserve the existing ROM sizing-probe handling. */
	if (rom_bar_write && data != write_ones)
	{
		const u32 rom_data = endian_32(pci_state.config_data[func][0x30 / 4]);
		const u32 rom_mask = endian_32(pci_state.config_mask[func][0x30 / 4]);
		if (rom_mask != 0)
			register_bar(func, 6, rom_data, rom_mask);
	}
	else if ((dsize == 8 || (dsize == 16 && (address & 1U) == 0)) &&
		address >= 0x10U && address + access_bytes <= 0x28U && mask != 0)
	{
		// Byte enables change the complete 32-bit BAR's decoded address.
		// Skip both halves of 64-bit BARs; their partial-write handling is separate.
		for (int bar = 0; bar < 6; ++bar)
		{
			const u32 attributes = endian_32(std_config_data[func][4 + bar]);
			if ((attributes & 1U) == 0 &&
				(attributes & PCI_MEM_TYPE_MASK) == PCI_MEM_TYPE_64)
			{
				++bar;
				continue;
			}
			if ((address - 0x10U) / 4U == (u32)bar)
			{
				// An all-ones byte/word is still an address write, not a dword probe.
				register_bar(func, bar, endian_32(pci_state.config_data[func][4 + bar]),
					endian_32(pci_state.config_mask[func][4 + bar]));
				break;
			}
		}
	}
	else if (dsize == 32 && upper_64bit_bar && ((data & mask) != mask))
	{
		register_bar(func, (address - 0x10) / 4,
			endian_32(new_data), endian_32(mask));
	}
	// A masked maximum address is a valid assignment (including on restore).
	// Only the unmasked all-ones dword retains the existing size-probe policy.
	else if (dsize == 32 && mask != 0 && data != write_ones)
	{
		switch (address)
		{
		case 0x10:
		case 0x14:
		case 0x18:
		case 0x1c:
		case 0x20:
		case 0x24:
			register_bar(func, (address - 0x10) / 4, endian_32(new_data),
				endian_32(mask));
			break;
		}
	}

	config_write_custom(func, address, dsize, old_data, new_data, data);
}

void CPCIDevice::register_bar(int func, int bar, u32 data, u32 mask)
{
	/* A 64-bit BAR's upper dword belongs to the preceding BAR. */
	if (bar > 0 && bar < 6 && pci_bar_is_upper_64bit(std_config_data[func], bar))
	{
		const u32 low =
			endian_32(pci_state.config_data[func][4 + bar - 1]);
		register_bar(func, bar - 1, low,
			endian_32(pci_state.config_mask[func][4 + bar - 1]));
		return;
	}

	int id = PCI_RANGE_BASE + (func * 8) + bar;

	// IO BAR here, never BAR 6 though
	if ((data & 1u) && bar != 6) {
		pci_range_is_io[func][bar] = true;

		u32 length = (~(mask & PCI_IO_ADDRESS_MASK)) + 1u; // Size probe from mask
		if (length < 4u) length = 4u;

		u32 base = (data & PCI_IO_ADDRESS_MASK) & ~(length - 1u); //  IO BAR alignment  
		if (base == 0)
		{
			cSystem->RegisterMemory(this, id, 0, 0);
			return;
		}

		const u64 t = U64(0x00000801fc000000) + (U64(0x0000000200000000) * myPCIBus) + base;

		cSystem->RegisterMemory(this, id, t, length);
		printf("%s(%s).%d PCI BAR %d set to IO   %" PRIx64 ", len %x.\n",
			myCfg->get_myName(), myCfg->get_myValue(), func, bar, t, length);
		return;
	}

	// Everything else is memory BAR ......
	pci_range_is_io[func][bar] = false;

	// PCI Option ROM BAR, bit 0 = enable, address 31:11, 10:1 reserve by spec
	if (bar == 6) {
		const bool rom_enable = (data & PCI_ROM_ADDRESS_ENABLE) != 0;

		if (!rom_enable) {
			cSystem->RegisterMemory(this, id, 0, 0);
			printf("%s(%s).%d PCI BAR 6 ROM disabled.\n", myCfg->get_myName(), myCfg->get_myValue(), func);
			return;
		}

		// Size from probe mask; ignore enable & reserved bits via ROM mask
		u32 length = (~(mask & PCI_ROM_ADDRESS_MASK)) + 1u;
		if (length < 0x800u) length = 0x800u;  // spec minimum 2 KiB

		// Base: drop enable/reserved via mask, then align to size
		u32 base = (data & PCI_ROM_ADDRESS_MASK) & ~(length - 1u);

		const u64 t = U64(0x0000080000000000) + (U64(0x0000000200000000) * myPCIBus) + base;

		cSystem->RegisterMemory(this, id, t, length);
		printf("%s(%s).%d PCI BAR 6 set to MEM %" PRIx64 " (ROM), len %x.\n",
			myCfg->get_myName(), myCfg->get_myValue(), func, t, length);
		return;
	}

	// Normal memory BAR (0..5 that are memory)
	{
		// Size from probe mask; clear attr bits via MEM mask
		u32 length = (~(mask & PCI_MEM_ADDRESS_MASK)) + 1u;
		if (length < 0x10u) length = 0x10u; // spec minimum 16 bytes

		const bool is_64bit = bar < 5 &&
			(data & PCI_MEM_TYPE_MASK) == PCI_MEM_TYPE_64;
		u32 high = 0;
		if (is_64bit)
		{
			high = endian_32(pci_state.config_data[func][4 + bar + 1]);
			pci_range_is_io[func][bar + 1] = false;

			/* Ignore all-ones size probes on either half. */
			if (high == 0xFFFFFFFFU ||
				(data & PCI_MEM_ADDRESS_MASK) ==
				(mask & PCI_MEM_ADDRESS_MASK))
				return;
		}

		// Base: clear attribute bits, combine the pair, and align to size.
		u64 base = ((u64)high << 32) | (data & PCI_MEM_ADDRESS_MASK);
		base &= ~((u64)length - 1U);
		if (base == 0)
		{
			cSystem->RegisterMemory(this, id, 0, 0);
			return;
		}

		const u64 t = U64(0x0000080000000000) + (U64(0x0000000200000000) * myPCIBus) + base;

		cSystem->RegisterMemory(this, id, t, length);
		printf("%s(%s).%d PCI BAR %d set to MEM %" PRIx64 ", len %x.\n",
			myCfg->get_myName(), myCfg->get_myValue(), func, bar, t, length);
	}
}

void CPCIDevice::ResetPCI()
{
	int i;

	for (i = 0; i < 8; i++)
	{
		if (device_at[i])
		{
			cSystem->RegisterMemory(this, PCI_RANGE_BASE + (i * 8) + 7,
				U64(0x00000801fe000000) + (U64(0x0000000200000000) * myPCIBus) +
				(U64(0x0000000000000800) * myPCIDev) +
				(U64(0x0000000000000100) * i), 0x100);
			memcpy(pci_state.config_data[i], std_config_data[i], 64 * sizeof(u32));
			memcpy(pci_state.config_mask[i], std_config_mask[i], 64 * sizeof(u32));

			config_write(i, 0x10, 32, endian_32(pci_state.config_data[i][4]));
			config_write(i, 0x14, 32, endian_32(pci_state.config_data[i][5]));
			config_write(i, 0x18, 32, endian_32(pci_state.config_data[i][6]));
			config_write(i, 0x1c, 32, endian_32(pci_state.config_data[i][7]));
			config_write(i, 0x20, 32, endian_32(pci_state.config_data[i][8]));
			config_write(i, 0x24, 32, endian_32(pci_state.config_data[i][9]));
			config_write(i, 0x30, 32, endian_32(pci_state.config_data[i][12]));
		}
	}
}

bool CPCIDevice::decodes_memory_access(int index, u64, int, bool) const noexcept
{
	if (index < 0)
		return false;
	// Legacy registrations can include special cycles so their decode rules can belong to the device.
	if (index < PCI_RANGE_BASE)
		return true;

	const int range = index - PCI_RANGE_BASE;
	if (range >= 8 * 8)
		return false;
	const int func = range / 8;
	const int bar = range & 7;
	if (!device_at[func])
		return false;
	// PCI configuration cycles remain available when I/O and memory are disabled.
	if (bar == 7)
		return true;

	const u32 command = endian_32(pci_state.config_data[func][1]);
	const u32 enable = bar != 6 && pci_range_is_io[func][bar] ? 1U : 2U;
	if (!(command & enable))
		return false;
	// Expansion ROM decoding requires both Memory Space Enable and ROM Enable.
	return bar != 6 ||
		(pci_state.config_data[func][0x30 / 4] & endian_32(PCI_ROM_ADDRESS_ENABLE));
}

u64 CPCIDevice::ReadMem(int index, u64 address, int dsize)
{
	int func;
	int bar;

	if (dsize == 64)
		return ReadMem(index, address, 32) | (((u64)ReadMem(index, address + 4, 32)) << 32);

	if (dsize != 8 && dsize != 16 && dsize != 32)
	{
		FAILURE_5(InvalidArgument,
			"ReadMem: %s(%s) Unsupported dsize %d. (%d, %" PRIx64 ")\n",
			myCfg->get_myName(), myCfg->get_myValue(), dsize, index, address);
	}

	if (index < PCI_RANGE_BASE)
	{
		if (dev_range_is_io[index] && !(pci_state.config_data[0][1] & endian_32(1)))
		{
			diag_printf("%s(%s) Legacy IO access with IO disabled from PCI config.\n",
				myCfg->get_myName(), myCfg->get_myValue());
			return 0;
		}

		if (!dev_range_is_io[index] && !(pci_state.config_data[0][1] & endian_32(2)))
		{
			diag_printf("%s(%s) Legacy memory access with memory disabled from PCI config.\n",
				myCfg->get_myName(), myCfg->get_myValue());
			return 0;
		}

		//    printf("%s(%s) Calling ReadMem_Legacy(%d).\n",myCfg->get_myName(), myCfg->get_myValue(), index);
		return ReadMem_Legacy(index, (u32)address, dsize);
	}

	index -= PCI_RANGE_BASE;

	bar = index & 7;
	func = (index / 8) & 7;

	if (bar == 7)
		return config_read(func, (u32)address, dsize);

	if (pci_range_is_io[func][bar] && !(pci_state.config_data[func][1] & endian_32(1)))
	{
		diag_printf("%s(%s).%d PCI IO access with IO disabled from PCI config.\n",
			myCfg->get_myName(), myCfg->get_myValue(), func);
		return 0;
	}

	if (!pci_range_is_io[func][bar]
		&& !(pci_state.config_data[func][1] & endian_32(2)))
	{
		diag_printf("%s(%s).%d PCI memory access with memory disabled from PCI config.\n",
			myCfg->get_myName(), myCfg->get_myValue(), func);
		return 0;
	}

	if (bar == 6)
		cSystem->PrepareX86OptionROMMemory();

	//  printf("%s(%s).%d Calling ReadMem_Bar(%d,%d).\n",myCfg->get_myName(), myCfg->get_myValue(), func,func,bar);
	return ReadMem_Bar(func, bar, (u32)address, dsize);
}

void CPCIDevice::WriteMem(int index, u64 address, int dsize, u64 data)
{
	int func;
	int bar;

	if (dsize == 64)
	{
		WriteMem(index, address, 32, data & U64(0xffffffff));
		WriteMem(index, address + 4, 32, (data >> 32) & U64(0xffffffff));
		return;
	}

	if (dsize != 8 && dsize != 16 && dsize != 32)
	{
		FAILURE_6(InvalidArgument,
			"WriteMem: %s(%s) Unsupported dsize %d. (%d,%" PRIx64 ",%" PRIx64 ")\n",
			myCfg->get_myName(), myCfg->get_myValue(), dsize, index, address,
			data);
	}

	if (index < PCI_RANGE_BASE)
	{
		if (dev_range_is_io[index] && !(pci_state.config_data[0][1] & endian_32(1)))
		{
			diag_printf("%s(%s) Legacy IO access with IO disabled from PCI config.\n",
				myCfg->get_myName(), myCfg->get_myValue());
			return;
		}

		if (!dev_range_is_io[index] && !(pci_state.config_data[0][1] & endian_32(2)))
		{
			diag_printf("%s(%s) Legacy memory access with memory disabled from PCI config.\n",
				myCfg->get_myName(), myCfg->get_myValue());
			return;
		}

		WriteMem_Legacy(index, (u32)address, dsize, (u32)data);
		return;
	}

	index -= PCI_RANGE_BASE;

	bar = index & 7;
	func = (index / 8) & 7;

	if (bar == 7)
	{
		config_write(func, (u32)address, dsize, (u32)data);
		return;
	}

	if (pci_range_is_io[func][bar] && !(pci_state.config_data[func][1] & endian_32(1)))
	{
		diag_printf("%s(%s).%d PCI IO access with IO disabled from PCI config.\n",
			myCfg->get_myName(), myCfg->get_myValue(), func);
		return;
	}

	if (!pci_range_is_io[func][bar]
		&& !(pci_state.config_data[func][1] & endian_32(2)))
	{
		diag_printf("%s(%s).%d PCI memory access with memory disabled from PCI config.\n",
			myCfg->get_myName(), myCfg->get_myValue(), func);
		return;
	}
#if DEBUG_PCI
	printf("[PCI::WriteMem] func=%d bar=%d addr=%08X dsize=%d data=%08X\n",
		func, bar, (uint32_t)address, dsize, (uint32_t)data);
#endif
	WriteMem_Bar(func, bar, (u32)address, dsize, (u32)data);
}

bool CPCIDevice::do_pci_interrupt(int func, bool asserted)
{
	// Per Tsunami HRM section 6.3, the 64 TIGbus interrupt inputs (DRIR
	// bits 0-63) are board-wired to PCI INTx pins.  ES40 uses the slot-
	// based pattern SRM programs at first pass:
	//   DRIR_bit = ((slot + 1) * 4 + bus * 0x10 + (pin - 1)) & 0x3f.
	//
	// Two gates the cfg-space CFIT longword tells us:
	//   - Pin (cfg+0x3D) selects which INTx (1=A, 2=B, 3=C, 4=D); 0 means
	//     the device declares no INTx capability and we early-return.
	//   - Line (cfg+0x3C) == 0xFF is PCI spec's "no IRQ assigned" — that's
	//     also the reset value, so it doubles as "SRM has not configured
	//     this device's IRQ yet".  Honouring it prevents firing INTx into
	//     the OS during the pre-bootstrap window where exception handlers
	//     aren't installed yet (OpenVMS in particular crashes hard with
	//     ACV through vector 0x80 if INTx delivers in that window).
	//
	// Once a real value lands in Line (any 8-bit value < 0xFF), we ignore
	// it and use the slot formula instead
	const u32 cfit = endian_32(pci_state.config_data[func][0x0f]);
	const u8  line = cfit & 0xff;
	const u8  pin  = (cfit >> 8) & 0xff;
	if (pin == 0)
		return false;

	const int intx       = (pin - 1) & 0x3;
	const int slot       = myPCIDev & 0x1f;
	const int bus_offset = (myPCIBus & 0x3) * 0x10;
	const int drir_bit   = ((slot + 1) * 4 + bus_offset + intx) & 0x3f;

#ifdef DEBUG_PCI_IRQ
	printf("PCI-IRQ: %s.%d %s line=0x%02x pin=%d slot=%d bus=%d -> DRIR bit %d\n",
		devid_string, func, asserted ? "ASSERT  " : "DEASSERT",
		line, pin, myPCIDev, myPCIBus, drir_bit);
#endif
	cSystem->interrupt(drir_bit, asserted);
	return true;
}

static u32  pci_magic1 = 0xC1095A78;
static u32  pci_magic2 = 0x87A5901C;

/**
 * Save state to a Virtual Machine State file.
 **/
int CPCIDevice::SaveState(FILE* f)
{
	long  ss = sizeof(pci_state);

	fwrite(&pci_magic1, sizeof(u32), 1, f);
	fwrite(&ss, sizeof(long), 1, f);
	fwrite(&pci_state, sizeof(pci_state), 1, f);
	fwrite(&pci_magic2, sizeof(u32), 1, f);
	printf("%s: %d PCI bytes saved.\n", devid_string, (int)ss);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CPCIDevice::RestoreState(FILE* f)
{
	long    ss;
	u32     m1;
	u32     m2;
	size_t  r;
	SPCI_state restored_state;

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m1 != pci_magic1)
	{
		printf("%s: PCI MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	r = fread(&ss, sizeof(long), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (ss != sizeof(pci_state))
	{
		printf("%s: PCI STRUCT SIZE does not match!\n", devid_string);
		return -1;
	}

	r = fread(&restored_state, sizeof(restored_state), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	r = fread(&m2, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m2 != pci_magic2)
	{
		printf("%s: PCI MAGIC 2 does not match!\n", devid_string);
		return -1;
	}

	pci_state = restored_state;
	// Config-space bytes alone do not move the active I/O/MMIO apertures.
	// Rebuild them through the normal write path, including its 64-bit BAR,
	for (int func = 0; func < 8; ++func)
	{
		if (!device_at[func])
			continue;

		for (int bar = 0; bar < 7; ++bar)
		{
			cSystem->RegisterMemory(this, PCI_RANGE_BASE + func * 8 + bar, 0, 0);
			pci_range_is_io[func][bar] = false;
		}
		for (int bar = 0; bar < 7; ++bar)
		{
			const u32 address = bar == 6 ? 0x30U : 0x10U + bar * 4U;
			if (pci_state.config_mask[func][address / 4] != 0)
				config_write(func, address, 32,
					endian_32(pci_state.config_data[func][address / 4]));
		}
	}

	printf("%s: %d PCI bytes restored.\n", devid_string, (int)ss);
	return 0;
}

u32 CPCIDevice::ReadMem_Legacy(int index, u32 address, int dsize)
{
	FAILURE_2(NotImplemented, "%s(%s) No Legacy read handler installed",
		myCfg->get_myName(), myCfg->get_myValue());
}

void CPCIDevice::WriteMem_Legacy(int index, u32 address, int dsize, u32 data)
{
	FAILURE_2(NotImplemented, "%s(%s) No Legacy write handler installed",
		myCfg->get_myName(), myCfg->get_myValue());
}

u32 CPCIDevice::ReadMem_Bar(int func, int bar, u32 address, int dsize)
{
	FAILURE_3(NotImplemented, "%s(%s).%d No BAR read handler installed",
		myCfg->get_myName(), myCfg->get_myValue(), func);
}

void CPCIDevice::WriteMem_Bar(int func, int bar, u32 address, int dsize, u32 data)
{
	FAILURE_3(NotImplemented, "%s(%s).%d No BAR write handler installed",
		myCfg->get_myName(), myCfg->get_myValue(), func);
}

/**
 * \brief Read data from the PCI bus.
 *
 * Called by the PCI-device to read data off the PCI bus. address is the
 * 32-bit address put on the PCI bus. element_count elements of element_size
 * bytes each will be read in an endian-aware manner.
 **/
void CPCIDevice::do_pci_read(u32 address, void* dest, size_t element_size,
	size_t element_count)
{
	size_t  el;
	char* dst = (char*)dest;

	if (element_count == 0)
		return;

	// PCI Command.BME gates all device-initiated DMA. 
	// SRM early probe writes Command=0 to every device
	if (!pci_bus_master_enabled())
	{
		memset(dest, 0, element_size * element_count);
		return;
	}

	// get the 64-bit system wide address
	u64 phys_addr = cSystem->PCI_Phys(myPCIBus, address);

	// if there is only one element to read, this is a simple ReadMem operation.
	if (element_count == 1)
	{
		switch (element_size)
		{
		case 1:
			*(u8*)dest = (u8)cSystem->ReadMem(phys_addr, 8, this);
			break;

		case 2:
			*(u16*)dest = (u16)cSystem->ReadMem(phys_addr, 16, this);
			break;

		case 4:
			*(u32*)dest = (u32)cSystem->ReadMem(phys_addr, 32, this);
			break;

		default:
			FAILURE(InvalidArgument, "Strange element size");
		}

		return;
	}

#if defined(ES40_BIG_ENDIAN)

	// if this is a big-endian host machine, the memcpy method is only valid
	// if we're transferring bytes. Otherwise, endian-conversions need to be done.
	if (element_size == 1)
	{
#endif
		size_t remaining = element_size * element_count;
		u32 cur_address = address;

		while (remaining != 0)
		{
			u64 cur_phys = cSystem->PCI_Phys(myPCIBus, cur_address);
			size_t chunk = pci_dma_chunk_limit(cur_phys, remaining);

			// get a pointer to system memory if the address is inside main memory
			char* memptr = cSystem->PtrToMem(cur_phys);
			char* memptr2 = cSystem->PtrToMem(cur_phys + chunk - 1);

			// Copy only within a single translated DMA page. Scatter-gather DMA does
			// not guarantee that adjacent PCI bus addresses map to contiguous host
			// physical memory beyond the current page.
			if (memptr && memptr2)
			{
				memcpy(dst, memptr, chunk);
			}
			else
			{
				for (el = 0; el < chunk; el++)
					dst[el] = (u8)cSystem->ReadMem(cur_phys + el, 8, this);
			}

			dst += chunk;
			cur_address += (u32)chunk;
			remaining -= chunk;
		}
		return;

#if defined(ES40_BIG_ENDIAN)
	}

	// outside main memory, or inside main memory with endian-conversion
	// required we need to do the transfer element-by-element.
	switch (element_size)
	{
	case 1:
	{
		for (el = 0; el < element_count; el++)
		{
			*(u8*)dst = (u8)cSystem->ReadMem(phys_addr, 8, this);
			dst++;
			phys_addr++;
		}
		break;
	}

	case 2:
	{
		*(u16*)dst = endian_16((u16)cSystem->ReadMem(phys_addr, 16, this));
		dst += 2;
		phys_addr += 2;
		break;
	}

	case 4:
	{
		*(u32*)dst = endian_32((u32)cSystem->ReadMem(phys_addr, 32, this));
		dst += 4;
		phys_addr += 4;
		break;
	}

	default:
		FAILURE(InvalidArgument, "Strange element size");
		break;
	}
#endif
}

/**
 * \brief Write data to the PCI bus.
 *
 * Called by the PCI-device to write data to the PCI bus. address is the
 * 32-bit address put on the PCI bus. element_count elements of element_size
 * bytes each will be written in an endian-aware manner.
 **/
void CPCIDevice::do_pci_write(u32 address, const void* source, size_t element_size,
	size_t element_count)
{
	size_t  el;
	const u8* src = (const u8*)source;

	if (element_count == 0)
		return;

	// PCI Command.BME gates all device-initiated DMA (see do_pci_read).
	if (!pci_bus_master_enabled())
		return;

	// get the 64-bit system wide address
	u64 phys_addr = cSystem->PCI_Phys(myPCIBus, address);

	// if there is only one element to read, this is a simple ReadMem operation.
	if (element_count == 1)
	{
		switch (element_size)
		{
		case 1:   cSystem->WriteMem(phys_addr, 8, *src, this); break;
		case 2:   cSystem->WriteMem(phys_addr, 16, *(const u16*)source, this); break;
		case 4:   cSystem->WriteMem(phys_addr, 32, *(const u32*)source, this); break;
		default:  FAILURE(InvalidArgument, "Strange element size");
		}

		return;
	}

#if defined(ES40_BIG_ENDIAN)

	// if this is a big-endian host machine, the memcpy method is only valid
	// if we're transferring bytes. Otherwise, endian-conversions need to be done.
	if (element_size == 1)
	{
#endif
		size_t remaining = element_size * element_count;
		u32 cur_address = address;

		while (remaining != 0)
		{
			u64 cur_phys = cSystem->PCI_Phys(myPCIBus, cur_address);
			size_t chunk = pci_dma_chunk_limit(cur_phys, remaining);

			// get a pointer to system memory if the address is inside main memory
			char* memptr = cSystem->PtrToMem(cur_phys);
			char* memptr2 = cSystem->PtrToMem(cur_phys + chunk - 1);

			// Copy only within a single translated DMA page. Scatter-gather DMA does
			// not guarantee that adjacent PCI bus addresses map to contiguous host
			// physical memory beyond the current page.
			// Also, make sure we aren't trying to copy past allocated memory.
			if (memptr && memptr2)
			{
				memcpy(memptr, src, chunk);
			}
			else
			{
				for (el = 0; el < chunk; el++)
					cSystem->WriteMem(cur_phys + el, 8, src[el], this);
			}

			src += chunk;
			cur_address += (u32)chunk;
			remaining -= chunk;
		}
		return;

#if defined(ES40_BIG_ENDIAN)
	}

	// outside main memory, or inside main memory with endian-conversion
	// required we need to do the transfer element-by-element.
	switch (element_size)
	{
	case 1:
	{
		for (el = 0; el < element_count; el++)
		{
			cSystem->WriteMem(phys_addr, 8, *src, this);
			src++;
			phys_addr++;
		}
		break;
	}

	case 2:
	{
		cSystem->WriteMem(phys_addr, 16, endian_16(*(const u16*)src), this);
		src += 2;
		phys_addr += 2;
		break;
	}

	case 4:
	{
		cSystem->WriteMem(phys_addr, 32, endian_32(*(const u32*)src), this);
		src += 4;
		phys_addr += 4;
		break;
	}

	default:
		FAILURE(InvalidArgument, "Strange element size");
		break;
	}
#endif
}
