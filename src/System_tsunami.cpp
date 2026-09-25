/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
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
  * Tsunami/Typhoon chipset (Cchip, Dchip, Pchip) CSRs, interrupt delivery and PCI window translation.
  *
  * Split out of System.cpp; change history remains there.
  **/
#define __STDC_FORMAT_MACROS 1
#include "StdAfx.h"
#include "System.h"
#include "PCIDevice.h"
#include "AlphaCPU.h"

/**
 * \brief Write to PIO space (address above main memory): a mapped device,
 * internal chipset registers, or nothing.
 *
 * Source: HRM, 10.1.1:
 *
 * System Space and Address Map
 *
 * The system address space is divided into two parts: system memory and PIO. This division
 * is indicated by physical memory bit <43> = 1 for PIO accesses from the CPU, and
 * by the PTP bit in the window registers for PTP accesses from the Pchip. While the operating
 * system may choose bit <40> instead of bit <43> to represent PIO space, bit <43>
 * is used throughout this chapter. In general, bits <42:35> are don’t cares if bit <43> is
 * asserted.
 *
 * There is 16GB of PIO space available on the 21272 chipset with 8GB assigned to each
 * Pchip. The Pchip supports up to bit <34> (35 bits total) of system address. However, the
 * Version 1 Cchip only supports 4GB of system memory (32 bits total). As described in
 * Chapter 6, the CAPbus protocol between the Pchip and Cchip does support up to bit
 * <34>, as does the Cchip’s interface to the CPU. The Typhoon Cchip supports 32GB of
 * system memory (35 bits total).
 *
 * The system address space is divided as shown in the following table:
 *
 * \code
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Space             | Size   | System Address <43:0>         | Comments                        |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | System memory     |    4GB | 000.0000.0000 - 000.FFFF.FFFF | Cacheable and prefetchable.     |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          | 8188GB | 001.0000.0000 - 7FF.FFFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip0 PCI memory |    4GB | 800.0000.0000 - 800.FFFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | TIGbus            |    1GB | 801.0000.0000 - 801.3FFF.FFFF | addr<5:0> = 0. Single byte      |
 * |                   |        |                               | valid in quadword access.       |
 * |                   |        |                               | 16MB accessible.                |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |    1GB | 801.4000.0000 - 801.7FFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip0 CSRs       |  256MB | 801.8000.0000 - 801.8FFF.FFFF | addr<5:0> = 0. Quadword access. |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |  256MB | 801.9000.0000 - 801.9FFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Cchip CSRs        |  256MB | 801.A000.0000 - 801.AFFF.FFFF | addr<5:0> = 0. Quadword access. |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Dchip CSRs        |  256MB | 801.B000.0000 - 801.BFFF.FFFF | addr<5:0> = 0. All eight bytes  |
 * |                   |        |                               | in quadword access must be      |
 * |                   |        |                               | identical.                      |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |  768MB | 801.C000.0000 - 801.EFFF.FFFF | E                              |
 * | Reserved          |  128MB | 801.F000.0000 - 801.F7FF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip 0 PCI IACK  |   64MB | 801.F800.0000 - 801.FBFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip0 PCI I/O    |   32MB | 801.FC00.0000 - 801.FDFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip0 PCI conf   |   16MB | 801.FE00.0000 - 801.FEFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |   16MB | 801.FF00.0000 - 801.FFFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip1 PCI memory |    4GB | 802.0000.0000 - 802.FFFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |    2GB | 803.0000.0000 - 803.7FFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip1 CSRs       |  256MB | 803.8000.0000 - 803.8FFF.FFFF | addr<5:0> = 0, quadword access. |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          | 1536MB | 803.9000.0000 - 803.EFFF.FFFF | E                              |
 * | Reserved          |  128MB | 803.F000.0000 - 803.F7FF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip 1 PCI IACK  |   64MB | 803.F800.0000 - 803.FBFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip1 PCI I/O    |   32MB | 803.FC00.0000 - 803.FDFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip1 PCI conf   |   16MB | 803.FE00.0000 - 803.FEFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |   16MB | 803.FF00.0000 - 803.FFFF.FFFF | E                              |
 * | Reserved          | 8172GB | 804.0000.0000 - FFF.FFFF.FFFF | Bits <42:35> are don’t cares if |
 * |                   |        |                               | bit <43> is asserted.           |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * \endcode
 **/
void CSystem::pio_write(u64 a, int dsize, u64 data, CSystemComponent* source)
{
	std::lock_guard<std::recursive_mutex> bus_lock(device_bus_mutex);
	SDeviceAccessScope time_scope(this);
	SDecodeClaims claims;
	collect_decode_claims(a, dsize, true, claims, source);
	const bool mapped = claims.count != 0;
	if (claims.count > 1)
		prepare_shared_access(a, dsize, true, claims, source);
	if (mapped)
		note_pio_access(a, dsize, true, data, claims.count);
	const u64 io_base = a & ~U64(0x1ffffff);

	const bool observed_io =
		(io_base == U64(0x801fc000000) || io_base == U64(0x803fc000000)) &&
		(dsize == 8 || dsize == 16 || dsize == 32) &&
		(a & 3) + dsize / 8 <= 4;
	if (observed_io)
		dispatch_pci_io_write(a, dsize, data, claims);
	else
		for (int k = 0; k < claims.count; ++k)
		{
			const SMemoryUser* r = &claims.range[k];
			r->component->WriteMem(r->index, a - r->base, dsize, data);
		}
	if (mapped)
		return;

	const u64 config_base = a & ~U64(0xffffff);
	if ((config_base == U64(0x801fe000000) || config_base == U64(0x803fe000000)) &&
		(dsize == 8 || dsize == 16 || dsize == 32 || dsize == 64) &&
		(a & (dsize / 8 - 1)) == 0)
	{
		// No function claimed this configuration write. Even a burst aborts once.
		pchip_config_write_abort(config_base == U64(0x803fe000000) ? 1 : 0,
			(u32)a & 0xffffff);
		return;
	}

	if (a >= U64(0x00000801A0000000) && a <= U64(0x00000801AFFFFFFF))
	{
		cchip_csr_write((u32)a & 0xFFFFFFF, data, source);
		return;
	}

	if (a >= U64(0x0000080180000000) && a <= U64(0x000008018FFFFFFF))
	{
		pchip_csr_write(0, (u32)a & 0xFFFFFFF, data);
		return;
	}

	if (a >= U64(0x0000080380000000) && a <= U64(0x000008038FFFFFFF))
	{
		pchip_csr_write(1, (u32)a & 0xFFFFFFF, data);
		return;
	}

	if (a >= U64(0x00000801B0000000) && a <= U64(0x00000801BFFFFFFF))
	{
		dchip_csr_write((u32)a & 0xFFFFFFF, (u8)data & 0xff);
		return;
	}

	if (a >= U64(0x0000080100000000) && a <= U64(0x000008013FFFFFFF))
	{
		tig_write((u32)a & 0x3FFFFFFF, (u8)data);
		return;
	}

	if (a >= U64(0x801fc000000) && a < U64(0x801fe000000))
	{
		const u64 io_port = a & U64(0x1ffffff);

		if (io_port == U64(0x100))
		{
			if (!m_reported_3c509_probe.exchange(true, std::memory_order_acq_rel))
				printf("%%ISA-I-ELINKPROBE: 3c509 not implemented; ignoring likely BSD probe at IO port 100.\n");
			return;
		}

		// OpenVMS's floppy path retains an Intel 82357/EISA high-count-byte
		// clear even on the M1543C non-EISA machine.
		// The M1543C does not document port 405h so we can safely swallow
		if (io_port == U64(0x405) && dsize == 8 && (data & 0xff) == 0)
			return;

		// Unused PCI I/O space
		if (source)
			printf("Write to unknown IO port %" PRIx64 " (dsize=%d data=%" PRIx64 ") on PCI 0 from %s\n",
				io_port, dsize, data, source->devid_string);
		else
			printf("Write to unknown IO port %" PRIx64 " (dsize=%d data=%" PRIx64 ") on PCI 0\n",
				io_port, dsize, data);
		return;
	}

	if (a >= U64(0x803fc000000) && a < U64(0x803fe000000))
	{

		// Unused PCI I/O space
		if (source)
		{
			printf("Write to unknown IO port %" PRIx64 " on PCI 1 from %s   \n",
				a & U64(0x1ffffff), source->devid_string);
		}
		else
			printf("Write to unknown IO port %" PRIx64 " on PCI 1   \n",
				a & U64(0x1ffffff));
		return;
	}

	if (a >= U64(0x80000000000) && a < U64(0x80100000000))
	{

		// Unused PCI memory space
		u64 paddr = a & U64(0xffffffff);
		if (paddr > 0xb8fff || paddr < 0xb8000)
		{ // skip legacy video
#ifdef DEBUG_UNKMEM
			if (source)
			{
				printf("Write to unknown memory %" PRIx64 " on PCI 0 from %s   \n",
					a & U64(0xffffffff), source->devid_string);
			}
			else
				printf("Write to unknown memory %" PRIx64 " on PCI 0   \n",
					a & U64(0xffffffff));
#endif
		}
	}

	if (a >= U64(0x80200000000) && a < U64(0x80300000000))
	{
#ifdef DEBUG_UNKMEM
		// Unused PCI memory space
		if (source)
		{
			printf("Write to unknown memory %" PRIx64 " on PCI 1 from %s   \n",
				a & U64(0xffffffff), source->devid_string);
		}
		else
			printf("Write to unknown memory %" PRIx64 " on PCI 1   \n",
				a & U64(0xffffffff));
#endif
		return;
	}

#ifdef DEBUG_UNKMEM
	if (source)
		printf("Write to unknown memory %" PRIx64 " from %s   \n", a,
			source->devid_string);
	else
		printf("Write to unknown memory %" PRIx64 "   \n", a);
#endif //defined(DEBUG_UNKMEM)
}

/**
 * \brief Read from PIO space (address above main memory): a mapped device,
 * internal chipset registers, or nothing.
 *
 * Source: HRM, 10.1.1:
 *
 * System Space and Address Map
 *
 * The system address space is divided into two parts: system memory and PIO. This division
 * is indicated by physical memory bit <43> = 1 for PIO accesses from the CPU, and
 * by the PTP bit in the window registers for PTP accesses from the Pchip. While the operating
 * system may choose bit <40> instead of bit <43> to represent PIO space, bit <43>
 * is used throughout this chapter. In general, bits <42:35> are don’t cares if bit <43> is
 * asserted.
 *
 * There is 16GB of PIO space available on the 21272 chipset with 8GB assigned to each
 * Pchip. The Pchip supports up to bit <34> (35 bits total) of system address. However, the
 * Version 1 Cchip only supports 4GB of system memory (32 bits total). As described in
 * Chapter 6, the CAPbus protocol between the Pchip and Cchip does support up to bit
 * <34>, as does the Cchip’s interface to the CPU. The Typhoon Cchip supports 32GB of
 * system memory (35 bits total).
 *
 * The system address space is divided as shown in the following table:
 *
 * \code
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Space             | Size   | System Address <43:0>         | Comments                        |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | System memory     |    4GB | 000.0000.0000 - 000.FFFF.FFFF | Cacheable and prefetchable.     |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          | 8188GB | 001.0000.0000 - 7FF.FFFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip0 PCI memory |    4GB | 800.0000.0000 - 800.FFFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | TIGbus            |    1GB | 801.0000.0000 - 801.3FFF.FFFF | addr<5:0> = 0. Single byte      |
 * |                   |        |                               | valid in quadword access.       |
 * |                   |        |                               | 16MB accessible.                |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |    1GB | 801.4000.0000 - 801.7FFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip0 CSRs       |  256MB | 801.8000.0000 - 801.8FFF.FFFF | addr<5:0> = 0. Quadword access. |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |  256MB | 801.9000.0000 - 801.9FFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Cchip CSRs        |  256MB | 801.A000.0000 - 801.AFFF.FFFF | addr<5:0> = 0. Quadword access. |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Dchip CSRs        |  256MB | 801.B000.0000 - 801.BFFF.FFFF | addr<5:0> = 0. All eight bytes  |
 * |                   |        |                               | in quadword access must be      |
 * |                   |        |                               | identical.                      |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |  768MB | 801.C000.0000 - 801.EFFF.FFFF | E                              |
 * | Reserved          |  128MB | 801.F000.0000 - 801.F7FF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip 0 PCI IACK  |   64MB | 801.F800.0000 - 801.FBFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip0 PCI I/O    |   32MB | 801.FC00.0000 - 801.FDFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip0 PCI conf   |   16MB | 801.FE00.0000 - 801.FEFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |   16MB | 801.FF00.0000 - 801.FFFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip1 PCI memory |    4GB | 802.0000.0000 - 802.FFFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |    2GB | 803.0000.0000 - 803.7FFF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip1 CSRs       |  256MB | 803.8000.0000 - 803.8FFF.FFFF | addr<5:0> = 0, quadword access. |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          | 1536MB | 803.9000.0000 - 803.EFFF.FFFF | E                              |
 * | Reserved          |  128MB | 803.F000.0000 - 803.F7FF.FFFF | E                              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip 1 PCI IACK  |   64MB | 803.F800.0000 - 803.FBFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip1 PCI I/O    |   32MB | 803.FC00.0000 - 803.FDFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Pchip1 PCI conf   |   16MB | 803.FE00.0000 - 803.FEFF.FFFF | Linear addressing.              |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * | Reserved          |   16MB | 803.FF00.0000 - 803.FFFF.FFFF | E                              |
 * | Reserved          | 8172GB | 804.0000.0000 - FFF.FFFF.FFFF | Bits <42:35> are don’t cares if |
 * |                   |        |                               | bit <43> is asserted.           |
 * +-------------------+--------+-------------------------------+---------------------------------+
 * \endcode
 **/
u64 CSystem::pio_read(u64 a, int dsize, CSystemComponent* source)
{
	std::lock_guard<std::recursive_mutex> bus_lock(device_bus_mutex);
	SDeviceAccessScope time_scope(this);
	SDecodeClaims claims;
	collect_decode_claims(a, dsize, false, claims, source);
	if (claims.count == 1)
	{
		const SMemoryUser* r = &claims.range[0];
		const u64 data = r->component->ReadMem(r->index, a - r->base, dsize);
		note_pio_access(a, dsize, false, data, 1);
		return data;
	}
	if (claims.count > 1)
	{
		prepare_shared_access(a, dsize, false, claims, source);
		const u64 data = resolve_shared_read(a, dsize, claims, source);
		note_pio_access(a, dsize, false, data, claims.count);
		return data;
	}

	if (a >= U64(0x00000801A0000000) && a <= U64(0x00000801AFFFFFFF))
		return cchip_csr_read((u32)a & 0xFFFFFFF, source);

	if (a >= U64(0x0000080180000000) && a <= U64(0x000008018FFFFFFF))
		return pchip_csr_read(0, (u32)a & 0xFFFFFFF);

	if (a >= U64(0x0000080380000000) && a <= U64(0x000008038FFFFFFF))
		return pchip_csr_read(1, (u32)a & 0xFFFFFFF);

	if (a >= U64(0x00000801B0000000) && a <= U64(0x00000801BFFFFFFF))
		return dchip_csr_read((u32)a & 0xFFFFFFF) * U64(0x0101010101010101);

	if (a >= U64(0x0000080100000000) && a <= U64(0x000008013FFFFFFF))
		return tig_read((u32)a & 0x3FFFFFFF);

	if ((a >= U64(0x801fe000000) && a < U64(0x801ff000000))
		|| (a >= U64(0x803fe000000) && a < U64(0x803ff000000)))
	{

		// Unused PCI configuration space
		switch (dsize)
		{
		case 8:   return X64_BYTE;
		case 16:  return X64_WORD;
		case 32:  return X64_LONG;
		case 64:  return X64_QUAD;
		}
	}

	if (a >= U64(0x800000c0000) && a < U64(0x801000e0000))
	{

		// Unused PCI ROM BIOS space — return all-ones (PCI master abort)
		switch (dsize)
		{
		case 8:   return X64_BYTE;
		case 16:  return X64_WORD;
		case 32:  return X64_LONG;
		case 64:  return X64_QUAD;
		}
	}

	if (a >= U64(0x801fc000000) && a < U64(0x801fe000000))
	{

		// Unused PCI I/O space — return all-ones (PCI master abort)
		//if (source)
		//  printf("Read from unknown IO port %" PRIx64 " on PCI 0 from %s   \n",a & U64(0x1ffffff),source->devid_string);
		//else
		//  printf("Read from unknown IO port %" PRIx64 " on PCI 0   \n",a & U64(0x1ffffff));
		switch (dsize)
		{
		case 8:   return X64_BYTE;
		case 16:  return X64_WORD;
		case 32:  return X64_LONG;
		case 64:  return X64_QUAD;
		}
	}

	if (a >= U64(0x803fc000000) && a < U64(0x803fe000000))
	{

		// Unused PCI I/O space — return all-ones (PCI master abort)
		if (source)
		{
			printf("Read from unknown IO port %" PRIx64 " on PCI 1 from %s   \n",
				a & U64(0x1ffffff), source->devid_string);
		}
		else
			printf("Read from unknown IO port %" PRIx64 " on PCI 1   \n",
				a & U64(0x1ffffff));
		switch (dsize)
		{
		case 8:   return X64_BYTE;
		case 16:  return X64_WORD;
		case 32:  return X64_LONG;
		case 64:  return X64_QUAD;
		}
	}

	if (a >= U64(0x80000000000) && a < U64(0x80100000000))
	{

		// Unused PCI memory space — return all-ones (PCI master abort)
		u64 paddr = a & U64(0xffffffff);
		if (paddr > 0xb8fff || paddr < 0xb8000)
		{ // skip legacy video
#ifdef DEBUG_UNKMEM
			if (source)
			{
				printf("Read from unknown memory %" PRIx64 " on PCI 0 from %s   \n",
					a & U64(0xffffffff), source->devid_string);
			}
			else
				printf("Read from unknown memory %" PRIx64 " on PCI 0   \n",
					a & U64(0xffffffff));
#endif
		}

		switch (dsize)
		{
		case 8:   return X64_BYTE;
		case 16:  return X64_WORD;
		case 32:  return X64_LONG;
		case 64:  return X64_QUAD;
		}
	}

	if (a >= U64(0x80200000000) && a < U64(0x80300000000))
	{

		// Unused PCI memory space — return all-ones (PCI master abort)
#ifdef DEBUG_UNKMEM
		if (source)
		{
			printf("Read from unknown memory %" PRIx64 " on PCI 1 from %s   \n",
				a & U64(0xffffffff), source->devid_string);
		}
		else
			printf("Read from unknown memory %" PRIx64 " on PCI 1   \n",
				a & U64(0xffffffff));
#endif
		switch (dsize)
		{
		case 8:   return X64_BYTE;
		case 16:  return X64_WORD;
		case 32:  return X64_LONG;
		case 64:  return X64_QUAD;
		}
	}

#if defined(DEBUG_UNKMEM)
	if (source)
		printf("Read from unknown memory %" PRIx64 " from %s   \n", a,
			source->devid_string);
	else
		printf("Read from unknown memory %" PRIx64 "   \n", a);
#endif //defined(DEBUG_UNKMEM)
	return 0x00;

	//                    return 0x77; // 7f
}

/**
 * \brief Read one of the PCHIP registers.
 *
 * Source: HRM, 10.2.5:
 *
 * \code
 * +-------------+---------------+------+----+
 * |Register     | Address       | Type | ## |
 * +-------------+---------------+------+----+
 * | P0-WSBA0    | 801.8000.0000 | RW   | 00 |
 * | P0-WSBA1    | 801.8000.0040 | RW   | 01 |
 * | P0-WSBA2    | 801.8000.0080 | RW   | 02 |
 * | P0-WSBA3    | 801.8000.00C0 | RW   | 03 |
 * +-------------+---------------+------+----+
 * | P0-WSM0     | 801.8000.0100 | RW   | 04 |
 * | P0-WSM1     | 801.8000.0140 | RW   | 05 |
 * | P0-WSM2     | 801.8000.0180 | RW   | 06 |
 * | P0-WSM3     | 801.8000.01C0 | RW   | 07 |
 * +-------------+---------------+------+----+
 * | P0-TBA0     | 801.8000.0200 | RW   | 08 |
 * | P0-TBA1     | 801.8000.0240 | RW   | 09 |
 * | P0-TBA2     | 801.8000.0280 | RW   | 0A |
 * | P0-TBA3     | 801.8000.02C0 | RW   | 0B |
 * +-------------+---------------+------+----+
 * | P0-PCTL     | 801.8000.0300 | RW   | 0C |
 * +-------------+---------------+------+----+
 * | P0-PLAT     | 801.8000.0340 | RW   | 0D |
 * +-------------+---------------+------+----+
 * | P0-RES      | 801.8000.0380 | RW   | 0E |
 * +-------------+---------------+------+----+
 * | P0-PERROR   | 801.8000.03C0 | RW   | 0F |
 * | P0-PERRMASK | 801.8000.0400 | RW   | 10 |
 * | P0-PERRSET  | 801.8000.0440 | WO   | 11 |
 * +-------------+---------------+------+----+
 * | P0-TLBIV    | 801.8000.0480 | WO   | 12 |
 * | P0-TLBIA    | 801.8000.04C0 | WO   | 13 |
 * +-------------+---------------+------+----+
 * | P0-PMONCTL  | 801.8000.0500 | RW   | 14 |
 * | P0-PMONCNT  | 801.8000.0540 | RO   | 15 |
 * +-------------+---------------+------+----+
 * | P0-SPRST    | 801.8000.0800 | WO   | 20 |
 * +-------------+---------------+------+----+
 * \endcode
 *
 * Window Space Base Address Register (WSBAn - RW)
 *
 * Because the information in the WSBAn registers and WSMn registers
 * is used to compare against the PCI address, a clock-domain crossing (from
 * i_sysclk to i_pclko<7:0>) is made when these registers are written. Therefore, for a
 * period of several clock cycles, a window is disabled when its contents are disabled. If
 * PCI bus activity, which accesses the window in question, is not stopped before updating
 * that window, the Pchip might fail to respond with b_devsel_l when it should. This
 * would result in a master abort condition on the PCI bus. Therefore, before a window
 * (base or mask) is updated, all PCI activity accessing that window must be stopped, even
 * if only some activity is being added or deleted.
 *
 * The contents of the window may be read back to confirm that the update has taken
 * place. Then PCI activity through that window can be resumed.
 *
 * \code
 * +-------+---------+---------+------+--------------------------+
 * | Field | Bits    | Type    | Init | Description              |
 * +-------+---------+---------+------+--------------------------+
 * | RES   | <63:40> | MBZ,RAZ | 0    | Reserved                 |
 * +-------+---------+---------+------+--------------------------+
 * | DAC   | <39>    | RW      | 0    | DAC enable (WSBA3 only!) |
 * +-------+---------+---------+------+--------------------------+
 * | RES   | <38:32> | MBZ,RAZ | 0    | Reserved                 |
 * +-------+---------+---------+------+--------------------------+
 * | ADDR  | <31:20> | RW      | 0    | Base address (not used   |
 * |       |         |         |      | DAC enable = 1)          |
 * +-------+---------+---------+------+--------------------------+
 * | RES   | <19:2>  | MBZ,RAZ | 0    | Reserved                 |
 * +-------+---------+---------+------+--------------------------+
 * | SG    | <1>     | RW      | 0    | Scatter-gather           |
 * +-------+---------+---------+------+--------------------------+
 * | ENA   | <0>     | RW      | 0    | Enable                   |
 * +-------+---------+---------+------+--------------------------+
 * \endcode
 *
 * Window Space Mask Register (WSM0, WSM1, WSM2, WSM3 - RW)
 *
 * \code
 * +-------+---------+---------+------+--------------------------+
 * | Field | Bits    | Type    | Init | Description              |
 * +-------+---------+---------+------+--------------------------+
 * | RES   | <63:32> | MBZ,RAZ | 0    | Reserved                 |
 * +-------+---------+---------+------+--------------------------+
 * | AM    | <31:20> | RW      | 0    | Address mask             |
 * +-------+---------+---------+------+--------------------------+
 * | RES   | <19:0>  | MBZ,RAZ | 0    | Reserved                 |
 * +-------+---------+---------+------+--------------------------+
 * \endcode
 *
 * Translated Base Address Register (TBAn - RW)
 *
 * \code
 * +-------+---------+---------+------+--------------------------+
 * | Field | Bits    | Type    | Init | Description              |
 * +-------+---------+---------+------+--------------------------+
 * | RES   | <63:35> | MBZ,RAZ | 0    | Reserved                 |
 * +-------+---------+---------+------+--------------------------+
 * | ADDR  | <34:10> | RW      | 0    | Translated base address  |
 * |       |         |         |      | (if DAC enable = 1, bits |
 * |       |         |         |      | <34:22> are the PT Origin|
 * |       |         |         |      | address <34:22> and bits |
 * |       |         |         |      | <21:10> are ignored)     |
 * +-------+---------+---------+------+--------------------------+
 * | RES   | <9:0>   | MBZ,RAZ | 0    | Reserved                 |
 * +-------+---------+---------+------+--------------------------+
 * \encode
 *
 * Pchip Control Register (PCTL - RW)
 *
 * \code
 * +---------+---------+---------+------+-------------------------------------+
 * | Field   | Bits    | Type    | Init | Description                         |
 * +---------+---------+---------+------+-------------------------------------+
 * | RES     | <63:48> | MBZ,RAZ | 0    | Reserved.                           |
 * +---------+---------+---------+------+-------------------------------------+
 * | PID     | <47:46> | RO      | 1)   | Pchip ID.                           |
 * +---------+---------+---------+------+-------------------------------------+
 * | RPP     | <45>    | RO      | 2)   | Remote Pchip present.               |
 * +---------+---------+---------+------+-------------------------------------+
 * | PTEVRFY | <44>    | RW      |      | PTE verify for DMA read.            |
 * |         |         |         |      |   Val Description                   |
 * |         |         |         |      |   0   If TLB miss, then make DMA    |
 * |         |         |         |      |       read request as soon as possi-|
 * |         |         |         |      |       ble and discard data if PTE   |
 * |         |         |         |      |       was not valid - could cause   |
 * |         |         |         |      |       Cchip nonexistent mem. error. |
 * |         |         |         |      |   1   If TLB miss, then delay read  |
 * |         |         |         |      |       request until PTE is verified |
 * |         |         |         |      |       as valid - no request if not  |
 * |         |         |         |      |       valid.                        |
 * +---------+---------+---------+------+-------------------------------------+
 * | FDWDIS  | <43>    | RW      |      | Fast DMA read cache block wrap      |
 * |         |         |         |      | request disable.                    |
 * |         |         |         |      |   Val Description                   |
 * |         |         |         |      |   0   Normal operation.             |
 * |         |         |         |      |   1   Reserved for testing purposes |
 * |         |         |         |      |       only.                         |
 * +---------+---------+---------+------+-------------------------------------+
 * | FDSDIS  | <42>    | RW      |      | Fast DMA start and SGTE request     |
 * |         |         |         |      | disable.                            |
 * |         |         |         |      |   Val Description                   |
 * |         |         |         |      |   0   Normal operation.             |
 * |         |         |         |      |   1   Reserved for testing purposes |
 * |         |         |         |      |       only.                         |
 * +---------+---------+---------+------+-------------------------------------+
 * | PCLKX   | <41:40> | RO      | 3)   | PCI clock frequency multiplier      |
 * |         |         |         |      |   Val Multiplier                    |
 * |         |         |         |      |   0   x6                            |
 * |         |         |         |      |   1   x4                            |
 * |         |         |         |      |   2   x5                            |
 * |         |         |         |      |   3   Reserved                      |
 * +---------+---------+---------+------+-------------------------------------+
 * | PTPMAX  | <39:36> | RW      | 2    | Maximum PTP requests to Cchip from  |
 * |         |         |         |      | both Pchips until returned on       |
 * |         |         |         |      | CAPbus, modulo 16 (minimum = 2)     |
 * |         |         |         |      | (use 4 for pass 1 Cchip and Dchip). |
 * +---------+---------+---------+------+-------------------------------------+
 * | CRQMAX  | <35:32> | RW      | 1    | Maximum requests to Cchip from both |
 * |         |         |         |      | Pchips until Ack, modulo 16 (use 4  |
 * |         |         |         |      | for Cchip). (Use 3 or less for      |
 * |         |         |         |      | Typhoon because there is one less   |
 * |         |         |         |      | skid buffer in the C4 chip.)        |
 * +---------+---------+---------+------+-------------------------------------+
 * | REV     | <31:24> | RO      | 0    | In conjunction with the state of    |
 * |         |         |         |      | PMONCTL<0>, this field indicates    |
 * |         |         |         |      | the revision of the Pchip.          |
 * +---------+---------+---------+------+-------------------------------------+
 * | CDQMAX  | <23:20> | RW      | 1    | Maximum data transfers to Dchips    |
 * |         |         |         |      | from both Pchips until Ack, modulo  |
 * |         |         |         |      | 16 (use 4 for Dchip). Must be same  |
 * |         |         |         |      | as Cchip CSR CSC<FPQPMAX>.          |
 * +---------+---------+---------+------+-------------------------------------+
 * | PADM    | <19>    | RW      | 4)   | PADbus mode.                        |
 * |         |         |         |      |   Val Mode                          |
 * |         |         |         |      |   0   8-nibble, 8-check bit mode    |
 * |         |         |         |      |   1   4-byte, 4-check bit mode      |
 * +---------+---------+---------+------+-------------------------------------+
 * | ECCEN   | <18>    | RW      | 0    | ECC enable for DMA and SGTE access. |
 * +---------+---------+---------+------+-------------------------------------+
 * | RES     | <17:16> | MBZ,RAZ | 0    | Reserved.                           |
 * +---------+---------+---------+------+-------------------------------------+
 * | PPRI    | <15>    |         | 0    | Arbiter prio group for the Pchip.   |
 * +---------+---------+---------+------+-------------------------------------+
 * | PRIGRP  | <14:8>  | RW      | 0    | Arbiter prio group; one bit per PCI |
 * |         |         |         |      | slot with bits <14:8> corresponding |
 * |         |         |         |      | to input b_req_l<6:0>.              |
 * |         |         |         |      |   Val Group                         |
 * |         |         |         |      |   0   Low-priority group            |
 * |         |         |         |      |   1   High-priority group           |
 * +---------+---------+---------+------+-------------------------------------+
 * | ARBENA  | <7>     | RW      | 0    | Internal arbiter enable.            |
 * +---------+---------+---------+------+-------------------------------------+
 * | MWIN    | <6>     | RW      | 0    | Monster window enable.              |
 * +---------+---------+---------+------+-------------------------------------+
 * | HOLE    | <5>     | RW      | 0    | 512KB-to-1MB window hole enable.    |
 * +---------+---------+---------+------+-------------------------------------+
 * | TGTLAT  | <4>     | RW      | 0    | Target latency timers enable.       |
 * |         |         |         |      |   Val Mode                          |
 * |         |         |         |      |   0   Retry/disconnect after 128    |
 * |         |         |         |      |       PCI clocks without data.      |
 * |         |         |         |      |   1   Retry initial request after   |
 * |         |         |         |      |       32 PCI clocks without data;   |
 * |         |         |         |      |       disconnect subsequent trans-  |
 * |         |         |         |      |       fers after 8 PCI clocks       |
 * |         |         |         |      |       without data.                 |
 * +---------+---------+---------+------+-------------------------------------+
 * | CHAINDIS| <3>     | RW      | 0    | Disable chaining.                   |
 * +---------+---------+---------+------+-------------------------------------+
 * | THDIS   | <2>     | RW      | 0    | Disable antithrash mechan. for TLB. |
 * |         |         |         |      |   Val Mode                          |
 * |         |         |         |      |   0   Normal operation              |
 * |         |         |         |      |   1   Testing purposes only         |
 * +---------+---------+---------+------+-------------------------------------+
 * | FBTB    | <1>     | RW      | 0    | Fast back-to-back enable.           |
 * +---------+---------+---------+------+-------------------------------------+
 * | FDSC    | <0>     | RW      | 0    | Fast discard enable.                |
 * |         |         |         |      |   Val Mode                          |
 * |         |         |         |      |   0   Discard data if no retry      |
 * |         |         |         |      |       after 215 PCI clocks.         |
 * |         |         |         |      |   1   Discard data if no retry      |
 * |         |         |         |      |       after 210 PCI clocks.         |
 * +---------+---------+---------+------+-------------------------------------+
 *
 * 1) This field is initialized from the PID pins.
 * 2) This field is initialized from the assertion of CREQRMT_L pin at system reset.
 * 3) This field is initialized from the PCI i_pclkdiv<1:0> pins.
 * 4) This field is initialized from a decode of the b_cap<1:0> pins.
 * \endcode
 *
 * Pchip Error Register (PERROR - RW)
 *
 * If any of bits <11:0> are set, then this entire register is frozen and the Pchip output
 * signal b_error is asserted. Only bit <0> can be set after that. All other values will
 * be held until all of bits <11:0> are clear. When an error is detected and one of bits
 * <11:0> becomes set, the associated information is captured in bits <63:16> of this
 * register. After the information is captured, the INV bit is cleared, but the information
 * is not valid and should not be used if INV is set.
 *
 * In rare circumstances involving more than one error, INV may remain set because the
 * Pchip cannot correctly capture the SYN, CMD, or ADDR field.
 *
 * Furthermore, if software reads PERROR in a polling loop, or reads PERROR before the
 * Pchip’s error signal is reflected in the Cchip’s DRIR CSR, the INV bit may also be set.
 *
 * To avoid the latter condition, read PERROR only after receiving an IRQ0 interrupt,
 * then read the Cchip DIR CSR to determine that this Pchip has detected an error.
 *
 * \code
 * +---------+---------+---------+------+-------------------------------------+
 * | Field   | Bits    | Type    | Init | Description                         |
 * +---------+---------+---------+------+-------------------------------------+
 * | SYN     | <63:56> | RO      | 0    | errors ECC syndrome if CRE or UECC. |
 * +---------+---------+---------+------+-------------------------------------+
 * | CMD     | <55:52> | RO      | 0    | PCI command of transaction when     |
 * |         |         |         |      | error detected if not CRE and not   |
 * |         |         |         |      | UECC. If CRE or UECC, then:         |
 * |         |         |         |      |   Val    Command                    |
 * |         |         |         |      |   0000   DMA read                   |
 * |         |         |         |      |   0001   DMA RMW                    |
 * |         |         |         |      |   0011   SGTE read                  |
 * |         |         |         |      |   Others Reserved                   |
 * +---------+---------+---------+------+-------------------------------------+
 * | INV     | <51>    | RO Rev1 | 0    | Info Not Valid - only meaningful    |
 * |         |         | RAZ Rev0|      | when one of bits <11:0> is set.     |
 * |         |         |         |      | Indicates validity of <SYN>, <CMD>, |
 * |         |         |         |      | and <ADDR> fields.                  |
 * |         |         |         |      |   Val Mode                          |
 * |         |         |         |      |   0   Info fields are valid.        |
 * |         |         |         |      |   1   Info fields are not valid.    |
 * +---------+---------+---------+------+-------------------------------------+
 * | ADDR    | <50:16> | RO      | 0    | If CRE or UECC, then ADDR<50:19> =  |
 * |         |         |         |      | system address <34:3> of erroneous  |
 * |         |         |         |      | quadword and ADDR<18:16> = 0.       |
 * |         |         |         |      | If not CRE and not UECC, then       |
 * |         |         |         |      | ADDR<50:48> = 0; ADDR<47:18> = star-|
 * |         |         |         |      | ting PCI address <31:2> of trans-   |
 * |         |         |         |      | action when error was detected;     |
 * |         |         |         |      | ADDR<17:16> = 00 --> not a DAC      |
 * |         |         |         |      |                      operation;     |
 * |         |         |         |      | ADDR<17:16> = 01 --> via DAC SG     |
 * |         |         |         |      |                      Window 3;      |
 * |         |         |         |      | ADDR<17> = 1 --> via Monster Window |
 * +---------+---------+---------+------+-------------------------------------+
 * | RES     | <15:12> | MBZ,RAZ | 0    | Reserved.                           |
 * +---------+---------+---------+------+-------------------------------------+
 * | CRE     | <11>    | R,W1C   | 0    | Correctable ECC error.              |
 * +---------+---------+---------+------+-------------------------------------+
 * | UECC    | <10>    | R,W1C   | 0    | Uncorrectable ECC error.            |
 * +---------+---------+---------+------+-------------------------------------+
 * | RES     | <9>     | MBZ,RAZ | 0    | Reserved.                           |
 * +---------+---------+---------+------+-------------------------------------+
 * | NDS     | <8>     | R,W1C   | 0    | No b_devsel_l as PCI master.        |
 * +---------+---------+---------+------+-------------------------------------+
 * | RDPE    | <7>     | R,W1C   | 0    | PCI read data parity error as PCI   |
 * |         |         |         |      | master.                             |
 * +---------+---------+---------+------+-------------------------------------+
 * | TA      | <6>     | R,W1C   | 0    | Target abort as PCI master.         |
 * +---------+---------+---------+------+-------------------------------------+
 * | APE     | <5>     | R,W1C   | 0    | Address parity error detected as    |
 * |         |         |         |      | potential PCI target.               |
 * +---------+---------+---------+------+-------------------------------------+
 * | SGE     | <4>     | R,W1C   | 0    | Scatter-gather had invalid page     |
 * |         |         |         |      | table entry.                        |
 * +---------+---------+---------+------+-------------------------------------+
 * | DCRTO   | <3>     | R,W1C   | 0    | Delayed completion retry timeout as |
 * |         |         |         |      | PCI target.                         |
 * +---------+---------+---------+------+-------------------------------------+
 * | PERR    | <2>     | R,W1C   | 0    | b_perr_l sampled asserted.          |
 * +---------+---------+---------+------+-------------------------------------+
 * | SERR    | <1>     | R,W1C   | 0    | b_serr_l sampled asserted.          |
 * +---------+---------+---------+------+-------------------------------------+
 * | LOST    | <0>     | R,W1C   | 0    | Lost an error because it was detec- |
 * |         |         |         |      | ted after this register was frozen, |
 * |         |         |         |      | or while in the process of clearing |
 * |         |         |         |      | this register.                      |
 * +---------+---------+---------+------+-------------------------------------+
 * \endcode
 *
 * Pchip Error Mask Register (PERRMASK - RW)
 *
 * If any of the MASK bits have the value 0, they prevent the setting of the corresponding
 * bit in the PERROR register, regardless of the detection of errors or writing to PERRSET.
 *
 * The default is for all errors to be disabled.
 *
 * Beside masking the reporting of errors in PERROR, certain bits of PERRMASK have
 * the following additional effects:
 *   - If PERRMASK<RDPE> = 0, the Pchip ignores read data parity as the PCI master.
 *   - If PERRMASK<PERR> = 0, the Pchip ignores write data parity as the PCI target.
 *   - If PERRMASK<APE> = 0, the Pchip ignores address parity.
 *   .
 *
 * \code
 * +---------+---------+---------+------+-------------------------------------+
 * | Field   | Bits    | Type    | Init | Description                         |
 * +---------+---------+---------+------+-------------------------------------+
 * | RES     | <63:12> | MBZ,RAZ | 0    | Reserved                            |
 * +---------+---------+---------+------+-------------------------------------+
 * | MASK    | <11:0>  | RW      | 0    | PERROR register bit enables         |
 * +---------+---------+---------+------+-------------------------------------+
 * \endcode
 *
 * Pchip Master Latency Register (PLAT - RW)
 *
 * Bits <15:8> are the master latency timer.
 *
 * Translation Buffer Invalidate Virtual Register (TLBIV - WO)
 *
 * A write to this register invalidates all scatter-gather TLB entries that correspond to PCI
 * addresses whose bits <31:16> and bit 39 match the value written in bits <19:4> and 27
 * respectively. This invalidates up to eight PTEs at a time, which are the
 * number that can be defined in one 21264 cache block (64 bytes). Because a single TLB
 * PCI tag covers four entries, at most two tags are actually invalidated. PTE bits <22:4>
 * correspond to system address bits <34:16> - where PCI<34:32> must be zeros for scatter-
 * gather window hits - in generating the resulting system address, providing 8-page
 * (8KB) granularity.
 *
 * Translation Buffer Invalidate All Register (TLBIA - WO)
 *
 * A write to this register invalidates the scatter-gather TLB. The value written is ignored.
 **/
// Tsunami HRM Table 10-42: bit 9 is reserved, unlike PERRMASK<9>.
static const u64 pchip_error_flags = U64(0xdff);
static const u64 pchip_error_info = U64(0xffffffffffff0000);

void CSystem::latch_pchip_error(int num, u64 data)
{
	const u64 flags = data & state.pchip[num].perrmask & pchip_error_flags;
	if (!flags)
		return;
	if (state.pchip[num].perr & pchip_error_flags)
		state.pchip[num].perr |= state.pchip[num].perrmask & U64(1); // LOST
	else
		state.pchip[num].perr = (data & pchip_error_info) | flags;
	update_pchip_error_irqs();
}

void CSystem::pchip_config_write_abort(int num, u32 address)
{
	u32 pci_address = address & 0xfffffc;
	if (!(address & 0xff0000))
	{
		// HRM 8.5 / Table 10-3: Type 0 uses one-hot IDSEL, not the device number.
		const unsigned device = (address >> 11) & 31;
		if (device == 21) // Table 10-3 omits this encoding; leave it unmodeled.
			return;
		pci_address = address & 0x7fc;
		if (device <= 20)
			pci_address |= 1U << (device + 11);
		// Devices 22-31 have no IDSEL asserted in Table 10-3.
	}
	// HRM 8.8.2.1 / Table 10-42: configuration write CMD=B, NDS, INV=0,
	// and PCI AD<31:2> in PERROR<47:18>. This is not a dual-address cycle.
	latch_pchip_error(num, (U64(0xb) << 52) | ((u64)pci_address << 16) | U64(0x100));
}

u64 CSystem::pchip_csr_read(int num, u32 a)
{
	switch (a)
	{
	case 0x000:
	case 0x040:
	case 0x080:
	case 0x0c0:
		return state.pchip[num].wsba[(a >> 6) & 3];

	case 0x100:
	case 0x140:
	case 0x180:
	case 0x1c0:
		return state.pchip[num].wsm[(a >> 6) & 3];

	case 0x200:
	case 0x240:
	case 0x280:
	case 0x2c0:
		return state.pchip[num].tba[(a >> 6) & 3];

	case 0x300:
		return state.pchip[num].pctl;

	case 0x340:
		return state.pchip[num].plat;

	case 0x3c0:
		return state.pchip[num].perr & (pchip_error_info | pchip_error_flags);

	case 0x400: // PERRMASK; bits 63:12 are RAZ (HRM Table 10-43).
		return state.pchip[num].perrmask & U64(0xfff);

	case 0x480: // TLBIV
	case 0x4c0: // TLBIA
		return 0;

	case 0x800: // PCI reset
		return 0;

		// HRM 10.2.5.8, Table 10-44 (p. 10-52): PERRSET is WO;
		// read not defined, return 0.
	case 0x440: // PERRSET
		return 0;

		// HRM 10.2.5.11 (p. 10-53): PMONCTL is RW.
		// HRM 10.2.5.12 (p. 10-54): PMONCNT is RO.
		// Not emulated; return power-on default of 0.
	case 0x500: // PMONCTL
	case 0x540: // PMONCNT
		return 0;

	default:
		printf("Unknown PCHIP %d CSR %07x read attempted.\n", num, a);
		return 0;
	}
}

/**
 * \brief Write one of the PCHIP registers.
 *
 * For a description of the PCHIP registers, see pchip_csr_read.
 **/
void CSystem::pchip_csr_write(int num, u32 a, u64 data)
{
	switch (a)
	{
	case 0x000:
	case 0x040:
	case 0x080:
		state.pchip[num].wsba[(a >> 6) & 3] = data & U64(0x00000000fff00003);
		return;

	case 0x0c0:
		// HRM 10.2.5.1, Table 10-36 "Window Space Base Address Register (WSBA3)",
		// p. 10-46:
		//   DAC  <39>   RW   "DAC enable"
		//   ADDR <31:20> RW  "Base address if DAC enable = 0"
		//   SG   <1>    RO   Init=1, "Scatter-gather always enabled"
		//   ENA  <0>    RW   "Enable"
		//
		// Bit 1 must not be writable; force it to 1.
		state.pchip[num].wsba[3] = (data & U64(0x00000080fff00001)) | U64(0x2);
		return;

	case 0x100:
	case 0x140:
	case 0x180:
	case 0x1c0:
		state.pchip[num].wsm[(a >> 6) & 3] = data & U64(0x00000000fff00000);
		return;

	case 0x200:
	case 0x240:
	case 0x280:
	case 0x2c0:
		state.pchip[num].tba[(a >> 6) & 3] = data & U64(0x00000007fffffc00);
		return;

	case 0x300:
		// HRM 10.2.5.4, Table 10-40 "Pchip Control Register (PCTL)", pp. 10-47..10-49:
		//
		//   RO fields (preserve on write):
		//     PID    <47:46>  "Pchip ID. Initialized from PID pins."
		//     RPP    <45>     "Remote Pchip present."
		//     PCLKX  <41:40>  "PCI clock frequency multiplier."
		//     REV    <31:24>  "Indicates the revision of the Pchip (see §8.10)."
		//
		//   RW fields (accept writes):
		//     PTEVRFY  <44>     "PTE verify for DMA read."
		//     FDWDIS   <43>     "Fast DMA read cache block wrap request disable."
		//     FDSDIS   <42>     "Fast DMA start and SGTE request disable."
		//     PTPMAX   <39:36>  "Max PTP requests to Cchip, modulo 16."
		//     CRQMAX   <35:32>  "Max requests to Cchip until Ack, modulo 16."
		//     CDQMAX   <23:20>  "Max data transfers to Dchips until Ack, modulo 16."
		//     PADM     <19>     "PADbus mode."
		//     ECCEN    <18>     "ECC enable for DMA and SGTE accesses."
		//     RES      <17:16>  MBZ,RAZ — written as zero.
		//     PPRI     <15>     "Arbiter priority group for the Pchip."
		//     PRIGRP   <14:8>   "Arbiter priority group; one bit per PCI slot."
		//     ARBENA   <7>      "Internal arbiter enable."
		//     MWIN     <6>      "Monster window enable."
		//     HOLE     <5>      "512KB-to-1MB window hole enable."
		//     TGTLAT   <4>      "Target latency timers enable."
		//     CHAINDIS <3>      "Disable chaining."
		//     THDIS    <2>      "Disable antithrash mechanism for TLB."
		//     FBTB     <1>      "Fast back-to-back enable."
		//     FDSC     <0>      "Fast discard enable."
		//
		//   Preserve mask = bits {63:48(RAZ), 47:45, 41:40, 31:24}
		//   Writable mask = bits {44:42, 39:32, 23:18, 15:0}

		state.pchip[num].pctl &= U64(0xffffe300ff000000);          // preserve RO per Table 10-40
		state.pchip[num].pctl |= (data & U64(0x00001cff00fcffff));  // write RW per Table 10-40
		return;

	case 0x340:
		state.pchip[num].plat = data;
		return;

	case 0x3c0: // PERROR; flags are W1C, INFO is read-only.
		state.pchip[num].perr &= ~(data & pchip_error_flags);
		update_pchip_error_irqs();
		return;

	case 0x400: // PERRMASK; only MASK<11:0> is writable.
		state.pchip[num].perrmask = data & U64(0xfff);
		return;

	case 0x440: // PERRSET (HRM 10.2.5.8).
		// Table 10-44 copies all INFO bits, including software-supplied INV.
		latch_pchip_error(num, data);
		return;

	case 0x480: // TLBIV
	case 0x4c0: // TLBIA
		return;

	case 0x800: // PCI reset
		// Tsunami HRM 8.7: each Pchip resets only its own PCI bus.
		for (int i = 0; i < iNumComponents; i++)
		{
			auto* device = dynamic_cast<CPCIDevice*>(acComponents[i]);
			if (device && device->pci_bus() == num)
				device->ResetPCI();
		}
		return;

	default:
		printf("Unknown PCHIP %d CSR %07x write with %016" PRIx64 " attempted.\n", num,
			a, data);
	}
}

u64 CSystem::cchip_csr_read(u32 a, CSystemComponent* source)
{
	CAlphaCPU* cpu = (CAlphaCPU*)source;
	switch (a)
	{
	case 0x000:
		return state.cchip.csc;

	case 0x080:
		//    printf("MISC: %016" PRIx64 " from CPU %d (@%" PRIx64 ") (other @ %" PRIx64 ").\n",state.cchip.misc | cpu->get_cpuid(),cpu->get_cpuid(), cpu->get_pc()-4, acCPUs[1-cpu->get_cpuid()]->get_pc());
	{
		// Consistent read of MISC against the concurrent RMW in cchip_csr_write()/clear_clock_int().
		std::lock_guard<std::mutex> g(drir_lock);
		return state.cchip.misc | ((CAlphaCPU*)source)->get_cpuid();
	}

	case 0x0c0: { // MPD: bit3=DR (SDA read), bit2=CKR (SCL read), bits1:0 read as 0
		uint8_t v = 0;
		v |= (m_mpd_bus.scl() ? 1 : 0) << 2; // CKR
		v |= (m_mpd_bus.sda() ? 1 : 0) << 3; // DR
		return v;
	}

	case 0x100:
	case 0x140:
	case 0x180:
	case 0x1c0:
	{
		// AAR0-3: memory presented as up to 4 arrays of at most 8GB each
		// (Typhoon ASIZ max, HRM Table 10-15); base address in ADDR<34:24>.
		int           arr = (int)((a >> 6) & 3);
		unsigned int  arr_bits = (iNumMemoryBits > 33) ? 33 : iNumMemoryBits;
		int           n_arr = 1 << (iNumMemoryBits - arr_bits);
		if (arr >= n_arr)
			return 0; // bank disabled
		return ((u64)arr << arr_bits) | ((u64)(arr_bits - 23) << 12);
	}

	case 0x200:
	case 0x240:
	case 0x600:
	case 0x640:
		return state.cchip.dim[((a >> 9) & 2) | ((a >> 6) & 1)];

	case 0x280:
	case 0x2c0:
	case 0x680:
	case 0x6c0:
		return state.cchip.drir & state.cchip.dim[((a >> 9) & 2) | ((a >> 6) & 1)];

	case 0x300:
		return state.cchip.drir;

	default:
		printf("Unknown CCHIP CSR %07x read attempted.\n", a);
		return 0;
	}
}

void CSystem::cchip_csr_write(u32 a, u64 data, CSystemComponent* source)
{
	CAlphaCPU* cpu = (CAlphaCPU*)source;
	switch (a)
	{
	case 0x000: // CSC
		// HRM 10.2.2.1, Table 10-10 "CSC (Typhoon Only)", p. 10-23:
		//   AXD <39> RW, Init=0, "Disable memory XOR. (Typhoon only)"
		// Bit 39 is in nibble <39:36>; change that nibble from 0x7 to 0xf.
		state.cchip.csc &= ~U64(0x077777ffff3f0000);
		state.cchip.csc |= (data & U64(0x077777ffff3f0000));
		return;

	case 0x080: // MISC
	{
		// Serialize the MISC RMW + IPI delivery against other CPUs and against
		// interrupt()/clear_clock_int(); irq_h() is lock-free and never re-enters here.
		std::lock_guard<std::mutex> g(drir_lock);
		state.cchip.misc |= (data & U64(0x00000f0000f00000));     // W1S
		state.cchip.misc &= ~(data & U64(0x0000000010000ff0));    // W1C
		if (data & U64(0x0000000001000000))
		{
			state.cchip.misc &= ~U64(0x0000000000ff0000);           //Arbitration Clear
			printf("Arbitration clear from CPU %d (@%" PRIx64 ").\n", cpu->get_cpuid(),
				cpu->get_pc() - 4);
		}

		if (data & U64(0x00000000000f0000))
		{
			printf("Arbitration %016" PRIx64 " from CPU %d (@%" PRIx64 ")... ", data,
				cpu->get_cpuid(), cpu->get_pc() - 4);
			if (!(state.cchip.misc & U64(0x00000000000f0000)))
			{
				state.cchip.misc |= (data & U64(0x00000000000f0000)); //Arbitration won
				printf("won  %016" PRIx64 "\n", state.cchip.misc);
			}
			else
				printf("lost %016" PRIx64 "\n", state.cchip.misc);
		}

		// stop interval timer interrupt
		if (data & U64(0x00000000000000f0))
		{
			for (int i = 0; i < iNumCPUs; i++)
			{
				if (data & (U64(0x10) << i))
				{
					acCPUs[i]->irq_h(2, false, 0);

					//printf("*** TIMER interrupt cleared for CPU %d\n",i);
				}
			}
		}

		// stop inter processor interrupt
		if (data & U64(0x0000000000000f00))
		{
			for (int i = 0; i < iNumCPUs; i++)
			{
				if (data & (U64(0x100) << i))
				{
					acCPUs[i]->irq_h(3, false, 0);
#ifdef DEBUG_IPI
					printf("*** IP interrupt cleared for CPU %d from CPU %d(@ %" PRIx64 ").\n",
						i, cpu->get_cpuid(), cpu->get_pc() - 4);
#endif
				}
			}
		}

		// set inter processor interrupt
		if (data & U64(0x000000000000f000))
		{
			for (int i = 0; i < iNumCPUs; i++)
			{
				if (data & (U64(0x1000) << i))
				{
					state.cchip.misc |= U64(0x100) << i;
					acCPUs[i]->irq_h(3, true, 0);
#ifdef DEBUG_IPI
					printf("*** IP interrupt set for CPU %d from CPU %d(@ %" PRIx64 ")\n", i,
						cpu->get_cpuid(), cpu->get_pc() - 4);
#endif

					//          CThread::sleep(10);
				}
			}
		}

		return;
	}

	case 0x0c0: { // MPD
		// MPD: bit0=CKS (SCL driver: 1=release, 0=pull low)
		//      bit1=DS  (SDA driver: 1=release, 0=pull low)
		bool new_cks = (data & 0x1) != 0;
		bool new_ds = (data & 0x2) != 0;
		m_mpd.cks_out = new_cks;
		m_mpd.ds_out = new_ds;
		m_mpd_bus.drive_from_host(m_mpd.cks_out, m_mpd.ds_out); // SCL, SDA
		return;
	}


	case 0x200:
	case 0x240:
	case 0x600:
	case 0x640:
	{
		// A DIM change must re-drive the CPU device-interrupt lines 
		std::lock_guard<std::mutex> g(drir_lock);
		state.cchip.dim[((a >> 9) & 2) | ((a >> 6) & 1)] = data;
		recompute_device_irqs();
		return;
	}

	default:
		printf("Unknown CCHIP CSR %07x write with %016" PRIx64 " attempted.\n", a, data);
	}
}

u8 CSystem::dchip_csr_read(u32 a)
{
	switch (a)
	{
	case 0x800: // DSC
		return state.dchip.dsc;
	case 0x840: // STR
		return state.dchip.str;
	case 0x880: // DREV
		return state.dchip.drev;
	case 0x8c0: // DSC2
		return state.dchip.dsc2;
	default:    printf("Unknown DCHIP CSR %07x read attempted.\n", a); return 0;
	}
}

void CSystem::dchip_csr_write(u32 a, u8 data)
{
	printf("Unknown DCHIP CSR %07x write with %02x attempted.\n", a, data);
}

/**
 * \brief Assert or deassert one of 64 possible interrupt lines on the Tsunami chipset.
 *
 * Source: HRM, 6.3:
 *
 * TIGbus and Interrupts
 *
 * The TIGbus supports miscellaneous system logic such as flash ROM and
 * interrupt inputs. The Cchip TIG controller polls interrupts continuously
 * except when a read or write to flash is requested. The 64 possible interrupt inputs are
 * polled eight at a time by selecting a byte with the b_tia<2:0> pins, and asserting
 * b_toe_l to allow the selected byte to be driven onto b_td<7:0>. Using the polled interrupts,
 * the Cchip calculates the b_irq values that should be delivered to the CPUs. When
 * any change occurs in these b_irq values, the Cchip drives the b_irq<3:0> data for both
 * CPUs onto b_td<7:0>, and asserts signal b_tis to strobe it into a register on the module.
 * If there is no flash read or write outstanding, the polling process is repeated. If there
 * is a flash read or write outstanding, it is serviced between interrupt reads after any pending
 * b_irq updates. Thus, the rounds of interrupt polling are not atomic, but the b_irq
 * values reflect the most recently polled interrupts. Furthermore, b_irq<1> may be artificially
 * suppressed for one full polling loop using the CSR bit MISC<DEVSUP>.
 * [...]
 *
 * Device and Error Interrupt Delivery - b_irq<1:0>
 *
 * As interrupts are read into the Cchip through the TIGbus, the corresponding bits are set
 * in DRIR. These bits are ANDed with the mask bits in DIMn and then placed in DIRn. If
 * any bits are set in DIRn<55:0>, then CPUn is interrupted using CPU pin b_irq<1>.
 *
 * Interrupt bits <62:58> cause b_irq<0> to be asserted and are intended for use as error
 * signals. Assertion of interrupt bits <62:58> causes b_irq<0> to be asserted. Interrupt
 * bits <62:61> can be used for Pchip 0 and Pchip 1 errors, respectively. Interrupt bit <63>
 * is special because it is not read from the TIGbus, but is internally generated as the
 * Cchip detected error interrupt (currently used only for NXM requests). Assertion of
 * interrupt bit <63> causes b_irq<0> to be asserted. See Chapter 10 for descriptions of
 * the interrupt-related CSRs (DRIR, DIMn, DIRn, and MISC). A full mask register for
 * each CPU allows software to decide whether to send each of the 64 possible interrupts
 * to either or both CPUs.
 *
 * After handling all known outstanding interrupts, software may suppress b_irq<1>
 * device interrupts to allow the Cchip’s polling mechanism to detect the updated (deasserted)
 * value of the interrupt lines from the PCI devices and thereby avoid giving the
 * CPU “staleEinterrupts, which require passive release. The field MISC<DEVSUP> is
 * provided for this purpose. When a CPU writes a one to its bit in MISC<DEVSY> the
 * Cchip deasserts b_irq<1> to that CPU (regardless of the value in the DIRn) until it has
 * completed an entire polling loop. When the Cchip has completed an entire polling loop,
 * b_irq<1> will again reflect the value of DIRn<55:00>.
 *
 * Interval Timer Interrupts - b_irq<2>
 *
 * The interval timer interrupts the Cchip through a dedicated pin, i_intim_l, and is
 * asserted low. When the Cchip sees an asserting (falling) edge of this pin, it asserts
 * MISC<ITINTR> for both CPUs. Pin b_irq<2> remains asserted for each CPU
 * <ITINTR>. When the CPU has finished handling the interrupt, it writes a one to its
 * MISC<ITINTR> bit to clear it. Software can suppress interval timer interrupts for n
 * cycles by writing n into IICn.
 *
 * This table shows TIG Interrupts and IRQ Lines
 *
 * \code
 * +---------------+-----------------+--------+----------------------------------------+
 * | TIG Interrupt | Assertion Level | irq<n> | Use                                    |
 * +---------------+-----------------+--------+----------------------------------------+
 * |            63 | N/A             | irq<0> | N/C (internally generated Cchip error) |
 * |               |                 |        |     (currently NXM only)               |
 * +---------------+-----------------+--------+----------------------------------------+
 * |         62:58 | High            | irq<0> | Errors (Pchips, and so on)             |
 * |               |                 |        | Recommended:                           |
 * |               |                 |        |   * Bit <62> - Pchip0 error            |
 * |               |                 |        |   * Bit <61> - Pchip1 error            |
 * +---------------+-----------------+--------+----------------------------------------+
 * |         57:56 | N/A             | N/A    | Reserved                               |
 * +---------------+-----------------+--------+----------------------------------------+
 * |          55:0 | Low             | irq<1> | PCI devices (level sensitive)          |
 * +---------------+-----------------+--------+----------------------------------------+
 * \endcode
 *
 * It is not clear from the documentation how exactly the interval timer is connected
 * to the CChip. It looks like this is tied to the interrupt-line from the real-time
 * clock (TOY), as the periodic interval rate for the TOY is set to 1024 Hz by SRM.
 * 1024 Hz is the frequency of the system timer interrupt according to the OpenVMS Alpha
 * Internals and Data Structures Handbook.
 *
 **/
void CSystem::interrupt(int number, bool assert)
{
	int i;

	// Serialize drir RMW + delivery against other device threads; irq_h() is
	// lock-free and never re-enters here, so this is deadlock-free.
	std::lock_guard<std::mutex> drirGuard(drir_lock);

	if (number == -1)
	{

		// timer int...
		state.cchip.misc |= 0xf0;
		for (i = 0; i < iNumCPUs; i++)
			acCPUs[i]->irq_h(2, true, 0);   // timer interrupt is immediate
		m_tick_seq.fetch_add(1, std::memory_order_relaxed);
	}
	else if (assert)
	{

		//    if (!(state.cchip.drir & (1i64<<number)))
		//      printf("%%TYP-I-INTERRUPT: Interrupt %d asserted.\n",number);
		state.cchip.drir |= (U64(0x1) << number);
	}
	else
	{

		//    if (state.cchip.drir & (1i64<<number))
		//      printf("%%TYP-I-INTERRUPT: Interrupt %d deasserted.\n",number);
		state.cchip.drir &= ~(U64(0x1) << number);
	}

	recompute_device_irqs();
}

// Called with device_bus_mutex held; keep the two Pchip levels coherent.
void CSystem::update_pchip_error_irqs()
{
	std::lock_guard<std::mutex> guard(drir_lock);
	// ES40 Service Guide Table D-21: Pchip0 -> DRIR<62>, Pchip1 -> DRIR<61>.
	state.cchip.drir &= ~U64(0x6000000000000000);
	for (int num = 0; num < 2; ++num)
		if (state.pchip[num].perr & pchip_error_flags)
			state.cchip.drir |= U64(1) << (62 - num);
	recompute_device_irqs();
}

/**
 * Re-drive each CPU device-interrupt lines from DRIR & DIM. 
 * C-chip actually does this continuously. 
 **/
void CSystem::recompute_device_irqs()
{
	for (int i = 0; i < iNumCPUs; i++)
	{
		if (state.cchip.drir & state.cchip.dim[i] & U64(0x00ffffffffffffff))
			acCPUs[i]->irq_h(1, true, 100); // device interrupts delayed by 100 clocks
		else
			acCPUs[i]->irq_h(1, false, 0);

		if (state.cchip.drir & state.cchip.dim[i] & U64(0xfc00000000000000))
			acCPUs[i]->irq_h(0, true, 100); // device interrupts delayed by 100 clocks
		else
			acCPUs[i]->irq_h(0, false, 0);
	}
}

/**
 * \brief Translate a 32-bit address coming off the PCI bus into a
 * 64-bit system address. Used by PCI devices when accessing
 * memory (or other PCI devices) as bus master.
 *
 * Source: HRM, 10.1.4:
 *
 * DMA Address Translation (PCI-to-System)
 * The 21272 chipset supports some PCI commands as a target and does not support
 * (ignores) others as a target. The Pchip does not respond as a target when it acts as a PCI
 * master.
 *
 * The Pchip ignores all of the following commands as a target:
 *  - Interrupt acknowledge
 *  - Special cycle
 *  - I/O read
 *  - I/O write
 *  - Configuration read
 *  - Configuration write
 *  .
 *
 * The Pchips may respond to the following commands as a target:
 *  - Memory read
 *  - Memory read line
 *  - Memory write
 *  - Memory write and invalidate
 *  - Memory read multiple
 *  - Dual-address cycle: This command is accepted by the Pchip when the address lies
 *    inside the DMA monster window.
 *
 * There are two kinds of DMA address translation: direct mapped and scatter-gather
 * mapped. Each type starts by comparing the incoming PCI address with the monster
 * window (if it is enabled and if it is a DAC), and with the four window base and window
 * mask registers (the window base registers also have an enable window bit and a scatter-gather
 * enable bit). This process is shown in the next figure:
 *
 * \code
 *              31       n n-1      20 19    13 12       0
 *             +----------+-----------+--------+----------+
 * PCI Address |    Peripheral Page Number     |  Offset  |
 *             +----------+-----------+--------+----------+
 *             |<-------->|
 *                  ^
 *                  +-----------> COMPARE ----> Hit
 *                  v
 *             |<-------->|
 *              31       n n-1      20
 * Window Base +----------+-----------+
 * Register    |          |   xxxx    |
 *             +----------+-----------+
 *              31       n n-1      20
 * Window Mask +----------+-----------+
 * Register    |   0000   |   1111    | (Determines n)
 *             +----------+-----------+
 * \endcode
 *
 * If the address resides in one of the windows, and the window is enabled, then if the
 * scatter-gather enable bit is set, the translation is as described for
 * PCI_Phys_scatter_gather. Otherwise, the translation described for PCI_Phys_direct_mapped
 * is used.
 *
 * In addition, if the matching window has the PTP bit set, then the result of the address
 * translation is treated as if it had bit <43> set. That is, it is treated like a PIO address
 * from the CPU. Otherwise, the address is a system memory address.
 *
 * Window Hole
 *
 * All window registers are simultaneously subject to a hole that inhibits matching, under
 * the control of the PCTL<HOLE> CSR bit described in Section 10.2.5.4. If that bit is
 * set, the hole is enabled in all windows and has the following extent:
 *  - From PCI address base 512K (address<31:0> = 0008.0000)
 *  - To PCI address limit 1M-1 (address<31:0> = 000F.FFFF)
 *  .
 *
 * If enabled, the hole applies whether or not the PTP bit is set for the window.
 *
 * The documentation is not explicit on this, but the assumption was made that if an address
 * coming off the PCI-bus is not matched, the Pchip doesn't respond to that address, and it
 * is up to other PCI devices to respond to the address. So, if no match is found, we
 * treat the address as an address on the local PCI bus.
 *
 * \todo The documentation mentions a PTP bit set for a window, but the register descriptions
 *       don't show a PTP bit in one of the three registers (WSBA, WSM and TBA). So, for now,
 *       we can only do peer-to-peer through a scatter-gather PTE.
 *
 * \todo Dual-Acces-Cycle (DAC) access from the PCI bus is not supported. If a device is ever
 *       added that uses this, we should probably support it.
 **/
u64 CSystem::PCI_Phys(int pcibus, u32 address)
{
	u64 a;
	int j;

#if defined(DEBUG_PCI)
	printf("-------------- PCI MEMORY ACCESS FOR PCI HOSE %d --------------\n",
		pcibus);

	//Step through windows
	for (j = 0; j < 4; j++)
	{
		printf("WSBA%d: %016" PRIx64 " WSM: %016" PRIx64 " TBA: %016" PRIx64 "\n", j,
			state.pchip[pcibus].wsba[j], state.pchip[pcibus].wsm[j],
			state.pchip[pcibus].tba[j]);
	}

	printf("HOLE: %s\n",
		test_bit_64(state.pchip[pcibus].pctl, 5) ? "enabled" : "disabled");
	printf("--------------------------------------------------------------\n");
#endif
	if (!(state.pchip[pcibus].pctl & PCI_PCTL_HOLE)  // hole disabled
		|| (address < PCI_PCTL_HOLE_START) || (address > PCI_PCTL_HOLE_END)) // or address outside hole
	{

		//Step through windows
		for (j = 0; j < 4; j++)
		{
			if ((state.pchip[pcibus].wsba[j] & 1)  // window enabled...
				&& !((address ^ state.pchip[pcibus].wsba[j]
					) & 0xfff00000 & ~state.pchip[pcibus].wsm[j]))  // address in range...
			{
				if (state.pchip[pcibus].wsba[j] & 2)
				{
					try
					{
						a = PCI_Phys_scatter_gather(address, state.pchip[pcibus].wsm[j],
							state.pchip[pcibus].tba[j]);
					}

					catch (char)
					{

						// window disabled...
						// not matched; treat as local PCI bus address
						return U64(0x80000000000) |
							(pcibus * U64(0x200000000)) |
							(u64)address;
					}
				}
				else
					a = PCI_Phys_direct_mapped(address, state.pchip[pcibus].wsm[j],
						state.pchip[pcibus].tba[j]);
#if defined(DEBUG_PCI)
				printf("PCI memory address %08x translated to %016" PRIx64 "\n", address, a);
#endif
				return a;
			}
		}
	}

	// not matched; treat as local PCI bus address
	return U64(0x80000000000) | (pcibus * U64(0x200000000)) | (u64)address;
}

/**
 * Translate a 32-bit address coming off the PCI bus into a 64-bit
 * system address using direct-mapped DMA address translation.
 *
 * Source: HRM, 10.1.4.2:
 *
 * Direct-Mapped DMA Address Translation
 *
 * Direct-mapped addressing uses a base address register, a translated base address (TBA)
 * register, and a mask register. The block of PCI addresses at base address, of a size as
 * determined by the mask register, is translated to a block of addresses at translated base
 * address. Values in the WSMn field other than those shown produce
 * unspecified results.
 *
 * \code
 * +-------------+----------------+---------------------------+
 * | Window Size | WSMn<31:20>    | Translated Address <34:0> |
 * +-------------+----------------+---------------------------+
 * |         1MB | 0000.0000.0000 | TBA<34:20>:ad<19:0>       |
 * +-------------+----------------+---------------------------+
 * |         2MB | 0000.0000.0001 | TBA<34:21>:ad<20:0>       |
 * +-------------+----------------+---------------------------+
 * |         4MB | 0000.0000.0011 | TBA<34:22>:ad<21:0>       |
 * +-------------+----------------+---------------------------+
 * |         8MB | 0000.0000.0111 | TBA<34:23>:ad<22:0>       |
 * |        ...  |           ...  |                ...        |
 * |         2GB | 0111.1111.1111 | TBA<34:31>:ad<30:0>       |
 * +-------------+----------------+---------------------------+
 * |         4GB |            N/A | 000:ad<31:0> (monster     |
 * |             |                | window only)              |
 * +-------------+----------------+---------------------------+
 * \endcode
 **/
u64 CSystem::PCI_Phys_direct_mapped(u32 address, u64 wsm, u64 tba)
{
	u64 a;

	wsm &= PCI_WSM_MASK;

	a = (address & (wsm | PCI_ADD_MASK)) | (tba & ~wsm & PCI_TBA_MASK);

	return a;
}

/**
 * Translate a 32-bit address coming off the PCI bus into a 64-bit
 * system address using scatter-gather DMA address translation.
 *
 * If address can't be matched (PTE is invalid), an exception of type char
 * is thrown. The calling function should catch the exception, and do
 * The Right Thing(tm): treat the address as a local PCI-bus address.
 *
 * Source: HRM, 10.1.4.3:
 *
 * Scatter-Gather DMA Address Translation
 *
 * Scatter-gather addressing uses a base address register, a mask register, a translated base
 * address register, and a page table entry (PTE) in system memory. An 8KB page of PCI
 * addresses at base address is translated to an 8KB page of system addresses through one
 * level of indirection. The PTE contains the address of the 8KB page.
 * [...]
 * At TBA is a region (of size SG PTE AREA) of PTEs, each of which is eight bytes. Bits
 * <22:1> of the PTE become bits <34:13> (the 8KB page) of the system address, and bits
 * <12:0> of the PCI address become bits <12:0> (the page offset) of the system address.
 *
 * The following table shows how the address of the page table entry (to be used as part of the final
 * system address) is generated. Values in the WSM field other than those shown produce
 * unspecified results.
 *
 * \code
 * +-------------+-------------+----------------+----------------------+
 * | Window Size | SG PTE AREA | WSMn<31:20>    | PTE Address <34:3>   |
 * +-------------+-------------+----------------+----------------------+
 * |         1MB |         1KB | 0000.0000.0000 | TBA<34:10>:ad<19:13> |
 * +-------------+-------------+----------------+----------------------+
 * |         2MB |         2KB | 0000.0000.0001 | TBA<34:11>:ad<20:13> |
 * +-------------+-------------+----------------+----------------------+
 * |         4MB |         4KB | 0000.0000.0011 | TBA<34:12>:ad<21:13> |
 * +-------------+-------------+----------------+----------------------+
 * |         8MB |         8KB | 0000.0000.0111 | TBA<34:13>:ad<22:13> |
 * |        ...  |        ...  |           ...  |                ...   |
 * |         2GB |         2MB | 0111.1111.1111 | TBA<34:21>:ad<30:13> |
 * +-------------+-------------+----------------+----------------------+
 * |         4GB |         4MB |            N/A | TBA<34:22>:ad<31:13> |
 * |             |             |                | (Window 3 in DAC     |
 * |             |             |                | mode only)           |
 * +-------------+-------------+----------------+----------------------+
 * \endcode
 *
 * The following figure shows the structure of a page table entry in memory. If either bit <31> or
 * bit <28> is set, the address is interpreted as being a peer-to-peer address.
 *
 * \code
 *  63               32 31 30 29 28 27      23 22                   1 0
 * +-------------------+--+-----+--+----------+----------------------+-+
 * |                   |PP|     |PP|          | Page Address <34:13> |V|
 * +-------------------+--+-----+--+----------+----------------------+-+
 *                                                                    +--> V = valid bit
 * \endcode
 *
 * The last figure shows how a page table entry is used in conjunction with an incoming PCI
 * address to generate a system address.
 *
 * \code
 *        PTE <22:1>              PCI address <12:0>
 *             |                         |
 *  34         v              13 12      v         0
 * +----------------------------+-------------------+
 * |   Page addres <34:13>      |  Offset <12:0>    |
 * +----------------------------+-------------------+
 * \endcode
 **/
u64 CSystem::PCI_Phys_scatter_gather(u32 address, u64 wsm, u64 tba)
{
	u64 pte_a;

	u64 pte;

	u64 a;

	wsm &= PCI_WSM_MASK;

	pte_a = ((address & (wsm | PCI_PTE_ADD_MASK)) >> PCI_PTE_ADD_SHIFT) // ad part of pte address
		| (tba & PCI_PTE_TBA_MASK & ~(wsm >> PCI_PTE_ADD_SHIFT));            // tba part of pte address
	pte = ReadMem(pte_a, 64, 0);
	if (pte & 1)
	{
		a = ((pte << PCI_PTE_SHIFT) & PCI_PTE_MASK) | (address & PCI_PTE_ADD2_MASK);

		if (pte & PCI_PTE_PEER_BIT)  // peer-to-peer
			a |= (PHYS_PIO_ACCESS);   // PIO access.
		return a;
	}
	else
	{
		printf("PCHIP-W-SGPTE: invalid SG PTE %016" PRIx64 " @ %016" PRIx64 " for PCI %08x\n",
			pte, pte_a, address);
		throw((char)'0');
	}
}


/**
 * Clear the clock interrupt for a specific CPU. Used by the CPU to acknowledge the interrupt.
 **/
void CSystem::clear_clock_int(int ProcNum)
{
	std::lock_guard<std::mutex> g(drir_lock);
	state.cchip.misc &= ~(U64(0x10) << ProcNum);
	acCPUs[ProcNum]->irq_h(2, false, 0);
}

/**
 * Acknowledge an interprocessor interrupt for a CPU. The IPI has no device to
 * deassert b_irq<3>, so (like the interval clock) PALcode clears MISC<IPINTR>
 * on dispatch; otherwise it re-fires the instant IPL drops. (HRM 6.3.3)
 **/
void CSystem::clear_ipi(int ProcNum)
{
	std::lock_guard<std::mutex> g(drir_lock);
	state.cchip.misc &= ~(U64(0x100) << ProcNum);
	acCPUs[ProcNum]->irq_h(3, false, 0);
#ifdef DEBUG_IPI
	printf("*** IP interrupt cleared for CPU %d (PALcode dispatch ack).\n", ProcNum);
#endif
}
