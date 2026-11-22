/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
  * Contains the code for the emulated DMA controller.
  *
  * $Id$
  *
  * X-1.9        Camiel Vanderhoeven                             29-APR-2008
  *      Removed double function bodies. (patch issue)
  *
  * X-1.8        Brian Wheeler                                   29-APR-2008
  *      Fixed floppy disk implementation.
  *
  * X-1.7        Brian Wheeler                                   29-APR-2008
  *      DMA now supports floppy device.
  *
  * X-1.6        Brian Wheeler                                   18-APR-2008
  *      Rewrote DMA code to make it ready for floppy support.
  *
  * X-1.5        Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.4        Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.3        Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.2        Brian Wheeler                                   26-FEB-2008
  *      Debugging statements conditionalized.
  *
  * X-1.1        Camiel Vanderhoeven                             26-FEB-2008
  *      Created. Contains code previously found in AliM1543C.cpp
  *
  * \author Camiel Vanderhoeven
  **/
#include "StdAfx.h"
#include "system/System.h"
#include "DMA.h"

static constexpr u16 dma_low_page_base = 0x81;
static constexpr u16 dma_high_page_base = 0x481;
static constexpr u8 dma_cascade_mode = 0xc0;

/**
 * Constructor.
 **/
CDMA::CDMA(CConfigurator* cfg, CSystem* c, CDMAHost& host) :
	CDMA(cfg, c, host, 0xffff)
{
}

CDMA::CDMA(CConfigurator* cfg, CSystem* c, CDMAHost& host, u32 count_mask) :
	CSystemComponent(cfg, c), dma_host(host), count_mask(count_mask)
{
	ResetPCI();
	printf("dma: $Id$\n");
}

/**
 * Destructor.
 **/
CDMA::~CDMA()
{
}

/**
 * Restore power-on DMA state when the whole system resets.
 **/
void CDMA::ResetPCI()
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	// While guest master clear, system reset also resets channel programming,
	// request lines and any accept/pending demand/block/cascade service.
	memset(&state, 0, sizeof(state));
	for (int controller = 0; controller < 2; controller++)
	{
		state.controller[controller].mask = 0x0f;
		state.controller[controller].lobyte = true;
	}
}

bool CDMA::uses_subtractive_decode(int index, u64 address, int dsize, bool write) const noexcept
{
	const auto* owner = dma_host.isa_decode_owner();
	return owner && index >= DMA_IO_BASE && index <= DMA1_IO_EXT &&
		owner->uses_subtractive_decode(index, address, dsize, write);
}

const CSystemComponent* CDMA::memory_decode_owner() const noexcept
{
	const auto* owner = dma_host.isa_decode_owner();
	return owner ? owner : this;
}

int CDMA::DoClock()
{
	return 0;
}

std::string dma_index_names[] = {
	"DMA0_IO_MAIN",
	"DMA1_IO_MAIN",
	"DMA_IO_LPAGE",
	"DMA_IO_HPAGE",
	"DMA0_IO_CHANNEL",
	"DMA1_IO_CHANNEL",
	"DMA0_IO_EXT",
	"DMA1_IO_EXT"
};

int CDMA::page_channel(u16 port, u16 base)
{
	static const int channelmap[] = { 2, 3, 1, 0xff, 0xff, 0xff, 0,
				 0xff, 6, 7, 5 };

	if (port < base)
		return -1;
	const unsigned address = port - base;
	if (address >= (sizeof(channelmap) / sizeof(channelmap[0])) ||
		channelmap[address] == 0xff)
		return -1;

	return channelmap[address];
}

static bool dma_mapped_port(int index, u64 offset, u16& port)
{
	switch (index)
	{
	case DMA0_IO_CHANNEL: port = 0x00; break;
	case DMA0_IO_MAIN:    port = 0x08; break;
	case DMA_IO_LPAGE:    port = dma_low_page_base; break;
	case DMA_IO_HPAGE:    port = dma_high_page_base; break;
	case DMA1_IO_CHANNEL: port = 0xc0; break;
	case DMA1_IO_MAIN:    port = 0xd0; break;
	default: return false;
	}
	// Keep an out-of-bank offset from wrapping into a valid port.
	port = offset < 0x20 ? (u16)(port + offset) : 0xffff;
	return true;
}

bool CDMA::legacy_register(u16 port, int& ctrlr, int& reg)
{
	if (port <= 0x0f)
	{
		ctrlr = 0;
		reg = port;
		return true;
	}
	if (port >= 0xc0 && port <= 0xdf)
	{
		ctrlr = 1;
		reg = (port - 0xc0) >> 1;
		return true;
	}
	return false;
}

void CDMA::validate_device_channel(int channel)
{
	if (channel < 0 || channel >= 8 || channel == 4)
		FAILURE(InvalidArgument, "dma: invalid device channel (channel 4 is cascade)");
}

CDMA::SDMA_transfer_properties CDMA::transfer_properties(int channel) const
{
	validate_device_channel(channel);
	static constexpr SDMA_transfer_properties byte = { 1, 1, 1, 0, 0xffff };
	static constexpr SDMA_transfer_properties shifted_word = { 2, 2, 2, 1, 0x1ffff };
	// ALi's ISA channels count bytes or shifted words. Timing selection does
	// not change data width, count units or addressing on this controller.
	return channel < 4 ? byte : shifted_word;
}

u32 CDMA::dma_address(u16 pagebase, u32 current,
	const SDMA_transfer_properties& properties)
{
	// Shifted addressing drives A16 from current, not low-page bit 0.
	pagebase &= (u16)(0xffffU << properties.address_shift);

	return ((u32)pagebase << 16) | (current << properties.address_shift);
}

u32 CDMA::advance_address(u32 address, size_t bytes, bool decrement,
	bool finish_cycle, const SDMA_transfer_properties& properties)
{
	const u32 lane_mask = (u32)properties.data_width - 1;
	const size_t span = (address & lane_mask) + bytes;
	size_t cycles = span / properties.data_width;
	u32 lane = (u32)span & lane_mask;
	// TC or EOP finishes the last bus unit even if only some lanes were used.
	if (finish_cycle && lane)
	{
		++cycles;
		lane = 0;
	}
	address &= ~lane_mask;
	u32 delta = (u32)(cycles * properties.address_step);
	u32 next = decrement ? address - delta : address + delta;
	return (address & ~properties.address_mask) | (next & properties.address_mask) | lane;
}

size_t CDMA::increment_chunk_size(u32 address, size_t length,
	const SDMA_transfer_properties& properties)
{
	const u64 bytes_to_wrap = (u64)properties.address_mask + 1 - (address & properties.address_mask);
	return length < bytes_to_wrap ? length : (size_t)bytes_to_wrap;
}

#define DMA_INDEX(n) dma_index_names[n - DMA_IO_BASE].c_str()

#if defined(DEBUG_DMA)
#define DMA_TRACE_CHANNEL(channel) true
#define DMA_TRACE_CONTROLLER(controller) true
#elif defined(DEBUG_FDC)
#define DMA_TRACE_CHANNEL(channel) ((channel) == 2)
#define DMA_TRACE_CONTROLLER(controller) ((controller) == 0)
#else
#define DMA_TRACE_CHANNEL(channel) false
#define DMA_TRACE_CONTROLLER(controller) false
#endif


u8 CDMA::read_ali_shadow(u8 selector) const
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const int device = selector & 0xe0;
	if (device != 0x80 && device != 0xa0)
		return 0;

	const int ctrlr = device == 0x80 ? 1 : 0;
	const int reg = selector & 0x1f;
	if (reg < 4)
		return state.channel[ctrlr * 4 + reg].mode;
	if (reg == 4)
	{
		const auto& controller = state.controller[ctrlr];
		// ALi leaves the request and mask nibble order unspecified.
		return (u8)((controller.request << 4) | (controller.mask & 0x0f));
	}

	if (reg > 0x14)
		return 0;

	// Each channel exposes its base address and count in little-endian order.
	const int offset = reg - 5;
	const auto& channel = state.channel[ctrlr * 4 + offset / 4];
	const u32 value = (offset & 2) ? channel.base_count : channel.base;
	return (u8)((offset & 1) ? value >> 8 : value);
}

u8 CDMA::read_legacy_port(u16 port)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	int ctrlr, reg;
	if (!legacy_register(port, ctrlr, reg))
	{
		const int num = page_channel(port, dma_low_page_base);
		const u8 data = num < 0 ? 0xff : (u8)state.channel[num].current_page;
#if defined(DEBUG_DMA)
		printf("dma: read port %04x: %02x\n", (unsigned)port, (unsigned)data);
#endif
		return data;
	}

	auto& controller = state.controller[ctrlr];
	u8 data = 0;
	if (reg < 8)
	{
		const auto& channel = state.channel[ctrlr * 4 + reg / 2];
		const u32 value = (reg & 1) ? channel.count : channel.current;
		data = (u8)(value >> (controller.lobyte ? 0 : 8));
		controller.lobyte = !controller.lobyte;
	}
	else if (reg == 0x08)
	{
		data = controller.status | (get_requests(ctrlr) << 4);
		controller.status = 0;
	}
	else if (reg == 0x0f)
		data = controller.mask & 0x0f;

#if defined(DEBUG_DMA)
	printf("dma: read port %04x: %02x\n", (unsigned)port, (unsigned)data);
#endif
	return data;
}

void CDMA::program_address(int num, u16 keep, u16 bits)
{
	release_service(num);
	auto& ch = channel(num);
	ch.base = (ch.base & keep) | bits;
	ch.current = ch.base;
}

void CDMA::program_count(int num, u32 keep, u32 bits)
{
	release_service(num);
	auto& ch = channel(num);
	ch.base_count = (ch.base_count & keep) | bits;
	ch.count = ch.base_count;
}

void CDMA::program_page(int num, u16 keep, u16 bits)
{
	release_service(num);
	auto& ch = channel(num);
	ch.pagebase = (ch.pagebase & keep) | bits;
	ch.current_page = keep == 0x00ff ? (ch.current_page & keep) | bits : ch.pagebase;
}

void CDMA::mask_channel(int num)
{
	state.controller[num / 4].mask |= 1 << (num & 3);
	release_service(num);
}

void CDMA::master_clear_channel(int num)
{
	release_service(num);
}

void CDMA::program_mode(int num, u8 data)
{
	if (DMA_TRACE_CHANNEL(num))
	{
		printf("dma: mode register %d for channel %d written with %x\n", num / 4, num, (unsigned)data);
		printf("    Mode: %s, Address %s, Autoinit %s, Command: %s\n",
			(data & 0x80 ? (data & 0x40 ? "Cascade" : "Block") : (data & 0x40 ? "Single" : "Demand")),
			(data & 0x20 ? "Decrement" : "Increment"),
			(data & 0x10 ? "Enable" : "Disable"),
			(data & 0x08 ? (data & 0x04 ? "Illegal" : "Read") : (data & 0x04 ? "Write" : "Verify")));
	}
	release_service(num);
	channel(num).mode = data;
}

void CDMA::write_legacy_port(u16 port, u8 data)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	int ctrlr, reg;
	if (!legacy_register(port, ctrlr, reg))
	{
		const int num = page_channel(port, dma_low_page_base);
		if (num < 0) return;
		program_page(num, 0xff00, data);
#if defined(DEBUG_DMA)
		printf("dma channel %d pagebase: %04x\n", num, (unsigned)channel(num).pagebase);
#endif
		return;
	}

	auto& controller = state.controller[ctrlr];
	if (reg < 8)
	{
		const int num = ctrlr * 4 + reg / 2;
		const u16 keep = controller.lobyte ? 0xff00 : 0x00ff;
		const u16 bits = controller.lobyte ? data : (u16)(data << 8);
		if (reg & 1)
			program_count(num, keep, bits);
		else
			program_address(num, keep, bits);
		controller.lobyte = !controller.lobyte;
#if defined(DEBUG_DMA)
		printf("dma channel %d %s: %04x\n", num, (reg & 1) ? "count" : "base",
			(unsigned)((reg & 1) ? channel(num).base_count : channel(num).base));
#endif
		return;
	}

	const int local_channel = data & 0x03;
	const int selected_channel = ctrlr * 4 + local_channel;
	switch (reg)
	{
	case 0x08: // command
		if (DMA_TRACE_CONTROLLER(ctrlr))
			printf("dma: command register %d written with %x\n", ctrlr, (unsigned)data);
		if (data & 0x04)
		{
			// Release under the old priority mode before changing command.
			for (int channel = 0; channel < 4; channel++)
				release_service(ctrlr * 4 + channel);
		}
		controller.command = data;
		break;

	case 0x09: // request
		set_request(ctrlr, local_channel, (data & 0x04) >> 2);
		break;

	case 0x0a: // single mask
		if (DMA_TRACE_CHANNEL(selected_channel))
			printf("dma: mask single on %d : %d %s\n", ctrlr, local_channel, data & 0x4 ? "Masked" : "Unmasked");
		if (data & 0x04)
			mask_channel(selected_channel);
		else
			controller.mask &= ~(1 << local_channel);
		if (DMA_TRACE_CHANNEL(selected_channel))
			printf("     Mask status: %x\n", (unsigned)controller.mask);
		do_dma();
		break;

	case 0x0b: // mode register
		program_mode(selected_channel, data);
		break;

	case 0x0c: // clear flipflop(s)
		if (DMA_TRACE_CONTROLLER(ctrlr))
			printf("dma: flipflops cleared for dma %d\n", ctrlr);
		controller.lobyte = true;
		break;

	case 0x0d: // master reset
#if defined(DEBUG_DMA)
		printf("DMA-I-RESET: DMA %d reset.", ctrlr);
#endif
		for (int channel = 0; channel < 4; channel++)
			master_clear_channel(ctrlr * 4 + channel);
		controller.lobyte = true;
		controller.command = 0;
		controller.status = 0;
		controller.request = 0;
		controller.next_priority = 0;
		// External DRQ lines survive a master clear.
		controller.mask = 0x0f;
		break;

	case 0x0e: // master enable
		controller.mask = 0x00;
		do_dma();
		break;

	case 0x0f: // master mask
		controller.mask = data & 0x0f;
		for (int channel = 0; channel < 4; channel++)
		{
			if (data & (1 << channel))
				release_service(ctrlr * 4 + channel);
		}
		do_dma();
		break;
	}
}

u8 CDMA::read_high_page_port(u16 port) const
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const int num = page_channel(port, dma_high_page_base);
	const u8 data = num < 0 ? 0xff : (u8)(state.channel[num].current_page >> 8);
#if defined(DEBUG_DMA)
	printf("dma: read port %04x: %02x\n", (unsigned)port, (unsigned)data);
#endif
	return data;
}

void CDMA::write_high_page_port(u16 port, u8 data)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const int num = page_channel(port, dma_high_page_base);
	if (num < 0)
		return;
	program_page(num, 0x00ff, (u16)(data << 8));
#if defined(DEBUG_DMA)
	printf("dma channel %d pagebase: %04x\n", num, (unsigned)channel(num).pagebase);
#endif
}

void CDMA::write_ali_timing(int controller, u8 data)
{
	const int num = controller * 4 + (data & 3);
	// M1543C 4.2.1: channel select 1:0, timing 5:4; all other bits reserved.
	state.ali.cycle_timing[num] = data & 0x30;
	if (DMA_TRACE_CONTROLLER(controller))
		printf("dma: ALi extended mode channel %d written: %02x (%s timing)\n",
			num, (unsigned)(data & 0x33), state.ali.cycle_timing[num] == 0x30 ? "Type F" : "compatible");
}

u64 CDMA::ReadMem(int index, u64 address, int dsize)
{
	u16 port;
	if (dsize == 8 && dma_mapped_port(index, address, port))
		return index == DMA_IO_HPAGE ? read_high_page_port(port) : read_legacy_port(port);

	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	u64 ret;
	u8  data = 0;
	switch (dsize)
	{
	case 32:
		ret = ReadMem(index, address, 8);
		ret |= ReadMem(index, address + 1, 8) << 8;
		ret |= ReadMem(index, address + 2, 8) << 16;
		ret |= ReadMem(index, address + 3, 8) << 24;
		return ret;

	case 16:
		ret = ReadMem(index, address, 8);
		ret |= ReadMem(index, address + 1, 8) << 8;
		return ret;

	case 8:
		switch (index)
		{
		case DMA0_IO_EXT:
		case DMA1_IO_EXT:
			// M1543C extended-mode ports are write-only. Use the same all-ones
			// value as an unmapped I/O read, without changing controller state.
			data = 0xff;
			break;

		default:
			FAILURE(InvalidArgument, "dma: ReadMem index out of range");
		}

#if defined(DEBUG_DMA)
		printf("dma: read %s,%02" PRIx64 ": %02" PRIx8 ".   \n", DMA_INDEX(index), address, data);
#endif
	}
	return data;
}

void CDMA::WriteMem(int index, u64 address, int dsize, u64 data)
{
	u16 port;
	if (dsize == 8 && dma_mapped_port(index, address, port))
	{
		if (index == DMA_IO_HPAGE)
			write_high_page_port(port, (u8)data);
		else
			write_legacy_port(port, (u8)data);
		return;
	}

	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	switch (dsize)
	{
	case 32:
		WriteMem(index, address + 0, 8, (data >> 0) & 0xff);
		WriteMem(index, address + 1, 8, (data >> 8) & 0xff);
		WriteMem(index, address + 2, 8, (data >> 16) & 0xff);
		WriteMem(index, address + 3, 8, (data >> 24) & 0xff);
		return;

	case 16:
		WriteMem(index, address + 0, 8, (data >> 0) & 0xff);
		WriteMem(index, address + 1, 8, (data >> 8) & 0xff);
		return;

	case 8:
		data &= 0xff;
		switch (index)
		{
		case DMA0_IO_EXT:
		case DMA1_IO_EXT:
			// Each extended register occupies one byte; wider accesses are split
			// above, and the following bytes must not alias this register.
			if (address != 0)
				return;
			write_ali_timing(index == DMA0_IO_EXT ? 0 : 1, (u8)data);
			break;


		default:
			FAILURE(InvalidArgument, "dma: WriteMem index out of range");
		}

		return;
	}
}

static u32  dma_magic1 = 0x65324387;
static u32  dma_magic2 = 0x24092875;

/**
 * Save state to a Virtual Machine State file.
 **/
int CDMA::SaveState(FILE* f)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	long  ss = sizeof(state);

	if (fwrite(&dma_magic1, sizeof(u32), 1, f) != 1 ||
		fwrite(&ss, sizeof(long), 1, f) != 1 ||
		fwrite(&state, sizeof(state), 1, f) != 1 ||
		fwrite(&dma_magic2, sizeof(u32), 1, f) != 1)
	{
		printf("dma: error writing state file!\n");
		return -1;
	}

	printf("dma: %ld bytes saved.\n", ss);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CDMA::RestoreState(FILE* f)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	long    ss = 0;
	u32     m1 = 0;
	u32     m2 = 0;
	size_t  r;
	SDMA_state restored = {};

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("dma: unexpected end of file!\n");
		return -1;
	}

	if (m1 != dma_magic1)
	{
		printf("dma: MAGIC 1 does not match!\n");
		return -1;
	}

	r = fread(&ss, sizeof(long), 1, f);
	if (r != 1)
	{
		printf("dma: unexpected end of file!\n");
		return -1;
	}

	if (ss != sizeof(restored))
	{
		printf("dma: STRUCT SIZE does not match!\n");
		return -1;
	}

	r = fread(&restored, sizeof(restored), 1, f);
	if (r != 1)
	{
		printf("dma: unexpected end of file!\n");
		return -1;
	}

	r = fread(&m2, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("dma: unexpected end of file!\n");
		return -1;
	}

	if (m2 != dma_magic2)
	{
		printf("dma: MAGIC 2 does not match!\n");
		return -1;
	}

	for (auto& channel : restored.channel)
	{
		channel.count &= count_mask;
		channel.base_count &= count_mask;
	}
	state = restored;
	printf("dma: %ld bytes restored.\n", ss);
	return 0;
}

u8 CDMA::software_requests(int ctrlr) const
{
	// A mode write can leave a block request latched but inactive.
	u8 requests = state.controller[ctrlr].request;
	for (int channel = 0; requests && channel < 4; channel++)
	{
		const u8 bit = 1 << channel;
		if ((requests & bit) && !accepts_software_request(ctrlr * 4 + channel))
			requests &= ~bit;
	}
	return requests;
}

/**
 * Set the software request bit for a channel, and initiate DMA
 **/
void CDMA::set_request(int num, int channel, int data) {
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	channel &= 0x03;
	if (!data)
		state.controller[num].request &= ~(1 << channel);
	else if (accepts_software_request(num * 4 + channel))
		state.controller[num].request |= (1 << channel);
	do_dma();
}

/**
 * Set a device's logical DRQ level without changing its software request bit.
 **/
void CDMA::set_drq(int channel, bool asserted)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	// Apply the same device-channel validation as the transfer interfaces.
	validate_device_channel(channel);
	int ctrlr = channel < 4 ? 0 : 1;
	int local_channel = channel & 0x03;
	if (asserted)
		state.controller[ctrlr].drq |= (1 << local_channel);
	else
	{
		state.controller[ctrlr].drq &= ~(1 << local_channel);
		// Even a drop followed by reassertion before the next unit releases demand.
		if (state.controller[ctrlr].demand_active & (1 << local_channel))
			release_service(channel);
	}
	do_dma();
}

/**
 * Read request inputs without changing either controller's stored request bits.
 **/
u8 CDMA::get_requests(int ctrlr)
{
	u8 requests = software_requests(ctrlr) | state.controller[ctrlr].drq;
	// Controller 0's HRQ drives controller 1's channel-4 request input.
	// Software and retained block requests bypass the mask; incoming DRQ does not.
	if (ctrlr == 1 && !(state.controller[0].command & 0x04) &&
		((software_requests(0) | state.controller[0].block_active |
		(state.controller[0].drq & ~state.controller[0].mask)) & 0x0f))
		requests |= 0x01;
	return requests & 0x0f;
}

bool CDMA::cascade_enabled() const
{
	// These gate the grant, not the request input seen in controller 1's status.
	return !(state.controller[1].command & 0x04) &&
		!(state.controller[1].mask & 0x01) &&
		(state.channel[4].mode & 0xc0) == dma_cascade_mode;
}

/**
 * Check explicit or retained requests without consuming a unit.
 **/
bool CDMA::service_requested(int channel) const
{
	validate_device_channel(channel);
	int ctrlr = channel < 4 ? 0 : 1;
	u8 bit = 1 << (channel & 0x03);
	u8 mode = state.channel[channel].mode & 0xc0;
	// A block keeps its accepted request after either request source drops.
	if (mode == 0x80 && (state.controller[ctrlr].block_active & bit))
		return true;
	// Eligible software requests bypass the channel mask.
	if ((mode == 0x40 || mode == 0x80) && (software_requests(ctrlr) & bit))
		return true;
	if (mode == 0xc0) // Cascade does not service device data.
		return false;
	// Demand follows hardware DRQ; a software request cannot keep it running.
	return (state.controller[ctrlr].drq & ~state.controller[ctrlr].mask & bit) != 0;
}

/**
 * Select a global device channel without changing requests or held service.
 **/
int CDMA::select_service_channel(int ctrlr) const
{
	if (state.controller[ctrlr].command & 0x04)
		return -1;

	u8 requests = 0;
	int cascade_channel = -1;
	for (int local_channel = 0; local_channel < 4; local_channel++)
	{
		int channel = ctrlr * 4 + local_channel;
		if (channel == 4)
		{
			// Raw status can include requests for unsupported services. Only an
			// eligible lower winner competes here, not the raw channel-4 input.
			if (cascade_enabled())
				cascade_channel = select_service_channel(0);
			if (cascade_channel >= 0)
				requests |= 0x01;
		}
		else if ((state.channel[channel].mode & 0x0c) != 0x0c &&
			service_requested(channel))
			requests |= 1 << local_channel;
	}

	// Demand/block service is not preempted by a newly arriving request.
	// In particular, an upper owner must win before a new cascade request.
	u8 held = state.controller[ctrlr].block_active | state.controller[ctrlr].demand_active;
	if (ctrlr == 1 && state.cascade_active)
		held |= 0x01;
	held &= requests;
	if (held)
		requests = held;

	int first = state.controller[ctrlr].command & 0x10 ?
		state.controller[ctrlr].next_priority : 0;
	for (int offset = 0; offset < 4; offset++)
	{
		int local_channel = (first + offset) & 0x03;
		if (requests & (1 << local_channel))
			return ctrlr == 1 && local_channel == 0 ?
				cascade_channel : ctrlr * 4 + local_channel;
	}
	return -1;
}

bool CDMA::block_allows_requester(int channel, u32 requester) const
{
	const u8 bit = 1 << (channel & 3);
	return !(state.controller[channel / 4].block_active & bit) ||
		state.channel[channel].block_owner == requester;
}

bool CDMA::service_selected(int channel, u32 requester) const
{
	return service_requested(channel) && block_allows_requester(channel, requester) &&
		select_service_channel(1) == channel;
}

bool CDMA::service_ready(int channel, u8 transfer_type, u32 requester) const
{
	if (!service_selected(channel, requester) || !dma_host.dma_transfer_enabled())
		return false;

	// Direction does not affect arbitration against other pending channels.
	// Verify doesn't move payload bytes.
	u8 type = state.channel[channel].mode & 0x0c;
	return type == 0x00 || type == transfer_type;
}

/**
 * Record a service release without advancing again on repeated notification.
 **/
void CDMA::rotate_priority(int channel)
{
	int ctrlr = channel < 4 ? 0 : 1;
	if (state.controller[ctrlr].command & 0x10)
		state.controller[ctrlr].next_priority = ((channel & 0x03) + 1) & 0x03;
}

/**
 * Release accepted service, without changing requests, counts or TC status.
 **/
void CDMA::release_service(int channel)
{
	int ctrlr = channel < 4 ? 0 : 1;
	u8 bit = 1 << (channel & 0x03);
	bool held = ((state.controller[ctrlr].block_active |
		state.controller[ctrlr].demand_active) & bit) != 0;
	state.controller[ctrlr].block_active &= ~bit;
	state.channel[channel].block_owner = 0;
	state.controller[ctrlr].demand_active &= ~bit;
	if (channel == 4)
	{
		held = held || state.cascade_active;
		state.cascade_active = false;
	}
	if (!held)
		return;
	rotate_priority(channel);
	if (channel < 4 && state.cascade_active)
	{
		state.cascade_active = false;
		rotate_priority(4);
	}
}

/**
 * Retain ongoing demand/block service, or rotate after a successful release.
 **/
void CDMA::finish_service(int channel, const SDMA_result& result, u32 requester)
{
	// Verify services a unit despite transferring zero bytes.
	if (result.blocked)
		return;
	int ctrlr = channel < 4 ? 0 : 1;
	u8 bit = 1 << (channel & 0x03);
	u8 mode = state.channel[channel].mode & 0xc0;
	const bool ended = (result.terminal_count || result.external_eop) && !result.chained;
	if (ended || result.stop_match || mode == 0x40 ||
		(mode == 0x00 && !(state.controller[ctrlr].drq & bit)))
	{
		release_service(channel);
		// Completion may already have recorded a previously held service.
		// These idempotent assignments also cover a first unit ending at TC/EOP
		// and a lower service that has just reacquired its upper cascade grant.
		rotate_priority(channel);
		if (channel < 4)
			rotate_priority(4);
		return;
	}
	if (mode == 0x80)
	{
		state.controller[ctrlr].block_active |= bit;
		state.channel[channel].block_owner = requester;
	}
	else if (mode == 0x00)
		state.controller[ctrlr].demand_active |= bit;
	if (channel < 4)
		state.cascade_active = true;
}

/**
 * Perform a DMA if one is ready.
 *
 * \todo I'm not sure what would actually trigger this, so its mostly just a
 * placeholder.
 **/
void CDMA::do_dma()
{
	for (int ctrlr = 0; ctrlr < 2; ctrlr++)
	{
		if ((state.controller[ctrlr].command & 0x04) == 0 &&
			(ctrlr != 0 || cascade_enabled()))
		{
			for (int chnl = 0; chnl < 4; chnl++)
			{
				if ((state.controller[ctrlr].mask & (1 << chnl)) == 0 ||
					((software_requests(ctrlr) | state.controller[ctrlr].block_active) &
					(1 << chnl)))
				{
					if ((get_requests(ctrlr) | state.controller[ctrlr].block_active) & (1 << chnl))
					{
						// Do it!
					}
				}
			}
		}
	}
}

u8 CDMA::get_mode(int channel)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	validate_device_channel(channel);
	return state.channel[channel].mode;
}

bool CDMA::can_service_send(int channel, u32 requester)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	return service_ready(channel, 0x04, requester); // Device to memory (8237 Write).
}

bool CDMA::can_service_recv(int channel, u32 requester)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	return service_ready(channel, 0x08, requester); // Memory to device (8237 Read).
}

int CDMA::get_count(int channel)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	return state.channel[channel].count;
}

size_t CDMA::get_transfer_size(int channel)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const SDMA_transfer_properties properties = transfer_properties(channel);
	return ((size_t)state.channel[channel].count + 1) * properties.count_unit;
}

size_t CDMA::unit_size(int channel) const
{
	const auto properties = transfer_properties(channel);
	const auto& ch = state.channel[channel];
	const u32 address = dma_address(ch.current_page, ch.current, properties);
	const size_t lanes = properties.data_width - (address & (properties.data_width - 1));
	const size_t remaining = ((size_t)ch.count + 1) * properties.count_unit;
	return lanes < remaining ? lanes : remaining;
}

size_t CDMA::prepare_transfer(int channel, size_t length, const SDMA_transfer_properties& properties,
	bool eop, SDMA_result& result) const
{
	// The last unit is serviced while the count is zero.
	const size_t remaining = ((size_t)state.channel[channel].count + 1) * properties.count_unit;
	if (length == 0 || length > remaining)
		length = remaining;
	if (length % properties.count_unit)
		FAILURE(InvalidArgument, "dma: word-channel transfer length must be even");

	result.external_eop = eop && accepts_external_eop(channel);
	length = limit_transfer(channel, length, remaining, properties, result);
	result.terminal_count = length == remaining;
	result.blocked = false;
	return length;
}

void CDMA::transfer_memory(u32 address, u8* buffer, size_t length, bool decrement,
	bool write, const SDMA_transfer_properties& properties)
{
	// Device buffers are byte streams, even for word channels.
	while (length)
	{
		size_t chunk = increment_chunk_size(address, length, properties);
		if (decrement)
		{
			// Decrement by units, preserving the byte order within each word.
			const size_t lanes = properties.data_width - (address & (properties.data_width - 1));
			if (chunk > lanes) chunk = lanes;
		}
		if (write)
			dma_host.dma_write_memory(address, buffer, chunk);
		else
			dma_host.dma_read_memory(address, buffer, chunk);
		length -= chunk;
		buffer += chunk;
		address = advance_address(address, chunk, decrement, false, properties);
	}
}

/**
 * Account for serviced bytes, including verify.
 **/
void CDMA::advance_transfer(int channel, size_t length,
	const SDMA_transfer_properties& properties, SDMA_result& result)
{
	const size_t count_units = length / properties.count_unit;
	const bool complete = result.terminal_count || result.external_eop;
	auto& ch = state.channel[channel];
	u32 address = dma_address(ch.current_page, ch.current, properties);
	address = advance_address(address, length, (ch.mode & 0x20) != 0, complete, properties);
	ch.current = (u16)(address >> properties.address_shift);
	// Shifted word addressing leaves programmed low-page bit 0 undriven.
	const u16 page_mask = (u16)(0xffffU << properties.address_shift);
	ch.current_page = (ch.current_page & ~page_mask) | ((address >> 16) & page_mask);
	ch.count = (ch.count - (u32)count_units) & count_mask;

	if (complete)
		complete_transfer(channel, result);
	transfer_serviced(channel, result);
}

void CDMA::load_current_from_base(int channel)
{
	auto& ch = state.channel[channel];
	ch.current = ch.base;
	ch.current_page = ch.pagebase;
	ch.count = ch.base_count;
}

/**
 * Complete TC or external EOP without consuming any additional DMA units.
 **/
void CDMA::complete_transfer(int channel, SDMA_result&)
{
	const bool auto_init = (state.channel[channel].mode & 0x10) != 0;

	int ctrlr = channel < 4 ? 0 : 1;
	int local_channel = channel & 0x03;
	state.controller[ctrlr].status |= 1 << local_channel;
	// TC and EOP clear the software request; the device owns its DRQ level.
	state.controller[ctrlr].request &= ~(1 << local_channel);
	release_service(channel);
	if (auto_init)
		load_current_from_base(channel);
	else
		state.controller[ctrlr].mask |= 1 << local_channel;
}

/**
 * Transfer device-buffer bytes to memory, up to the current DMA count.
 **/
CDMA::SDMA_result CDMA::send_data(int channel, void* data, size_t length, bool eop)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const SDMA_transfer_properties properties = transfer_properties(channel);
	int ctrlr = channel < 4 ? 0 : 1;
	int local_channel = channel & 0x03;
	SDMA_result result;
	// Cascade passes bus control to another controller, not buffer data.
	if ((state.channel[channel].mode & 0xc0) == 0xc0)
	{
		if (DMA_TRACE_CHANNEL(channel))
			printf("dma: send on channel %d blocked by cascade mode.\n", channel);
		return result;
	}
	if (channel < 4 && !cascade_enabled())
	{
		if (DMA_TRACE_CHANNEL(channel))
			printf("dma: send on channel %d blocked by channel 4: command %02x, mask %02x, mode %02x.\n",
				channel, state.controller[1].command, state.controller[1].mask,
				state.channel[4].mode);
		return result;
	}

	if (!dma_host.dma_transfer_enabled())
		return result;

	if ((state.controller[ctrlr].command & 0x04) == 0)
	{
		// The mask inhibits incoming DRQ, not software or retained block requests.
		if ((state.controller[ctrlr].mask & (1 << local_channel)) == 0 ||
			((software_requests(ctrlr) | state.controller[ctrlr].block_active) &
			(1 << local_channel)))
		{
			u8 transfer_type = state.channel[channel].mode & 0x0c;
			// 8237 Write means device to memory; Verify has no data direction.
			if (transfer_type != 0x00 && transfer_type != 0x04)
			{
				if (DMA_TRACE_CHANNEL(channel))
					printf("dma: send on channel %d blocked by transfer type %02x.\n",
						channel, transfer_type);
				return result;
			}
			const size_t count = prepare_transfer(channel, length, properties, eop, result);

			if (transfer_type == 0x00)
			{
				if (DMA_TRACE_CHANNEL(channel))
					printf("dma: verify on channel %d: %zx bytes.\n", channel, count);
				advance_transfer(channel, count, properties, result);
				return result;
			}

			u32 addr = dma_address(state.channel[channel].current_page,
				state.channel[channel].current, properties);
			if (DMA_TRACE_CHANNEL(channel))
			{
				printf("DMA send_data:  %zx @ controller address %08" PRIx32 "\n  ", count, addr);
				for (size_t i = 0; i < count; i++)
				{
					printf("%02x ", *((char*)data + i) & 0xff);
					if (i % 16 == 15)
						printf("\n  ");
				}
				printf("\n");
			}

			transfer_memory(addr, (u8*)data, count, (state.channel[channel].mode & 0x20) != 0,
				true, properties);
			advance_transfer(channel, count, properties, result);
			result.transferred = count;
		}
		else if (DMA_TRACE_CHANNEL(channel))
		{
			printf("dma: dma requested by device on channel %d, but it is masked.\n", channel);
		}
	}
	else if (DMA_TRACE_CONTROLLER(ctrlr))
	{
		printf("dma: dma requested by device, but controller %d is disabled.\n", ctrlr);
	}
	return result;
}

CDMA::SDMA_result CDMA::recv_data(int channel, void* data, size_t length, bool eop)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const SDMA_transfer_properties properties = transfer_properties(channel);
	int ctrlr = channel < 4 ? 0 : 1;
	int local_channel = channel & 0x03;
	SDMA_result result;
	if ((state.channel[channel].mode & 0xc0) == 0xc0)
	{
		if (DMA_TRACE_CHANNEL(channel))
			printf("dma: receive on channel %d blocked by cascade mode.\n", channel);
		return result;
	}
	if (channel < 4 && !cascade_enabled())
	{
		if (DMA_TRACE_CHANNEL(channel))
			printf("dma: receive on channel %d blocked by channel 4: command %02x, mask %02x, mode %02x.\n",
				channel, state.controller[1].command, state.controller[1].mask,
				state.channel[4].mode);
		return result;
	}

	if (!dma_host.dma_transfer_enabled())
		return result;

	if ((state.controller[ctrlr].command & 0x04) == 0)
	{
		// The mask inhibits incoming DRQ, not software or retained block requests.
		if ((state.controller[ctrlr].mask & (1 << local_channel)) == 0 ||
			((software_requests(ctrlr) | state.controller[ctrlr].block_active) &
			(1 << local_channel)))
		{
			u8 transfer_type = state.channel[channel].mode & 0x0c;
			// 8237 Read means memory to device.
			if (transfer_type != 0x00 && transfer_type != 0x08)
			{
				if (DMA_TRACE_CHANNEL(channel))
					printf("dma: receive on channel %d blocked by transfer type %02x.\n",
						channel, transfer_type);
				return result;
			}
			const size_t count = prepare_transfer(channel, length, properties, eop, result);

			if (transfer_type == 0x00)
			{
				if (DMA_TRACE_CHANNEL(channel))
					printf("dma: verify on channel %d: %zx bytes.\n", channel, count);
				advance_transfer(channel, count, properties, result);
				return result;
			}

			u32 addr = dma_address(state.channel[channel].current_page,
				state.channel[channel].current, properties);
			if (DMA_TRACE_CHANNEL(channel))
				printf("DMA recv_data:  %zx @ controller address %08" PRIx32 "\n", count, addr);
			transfer_memory(addr, (u8*)data, count, (state.channel[channel].mode & 0x20) != 0,
				false, properties);
			advance_transfer(channel, count, properties, result);
			result.transferred = count;
		}
		else if (DMA_TRACE_CHANNEL(channel))
		{
			printf("dma: dma requested by device on channel %d, but it is masked.\n", channel);
		}
	}
	else if (DMA_TRACE_CONTROLLER(ctrlr))
	{
		printf("dma: dma requested by device, but controller %d is disabled.\n", ctrlr);
	}
	return result;
}

CDMA::SDMA_result CDMA::send_unit(int channel, u32 data, bool eop)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	u32 buffer = endian_32(data);
	return send_data(channel, &buffer, unit_size(channel), eop);
}

template<typename T>
CDMA::SDMA_result CDMA::receive_unit(int channel, T& data, bool eop, bool arbitrate, u32 requester)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const size_t bytes = unit_size(channel);
	const bool needs_data = (state.channel[channel].mode & 0x0c) == 0x08;
	if ((needs_data && bytes > sizeof(T)) || (arbitrate && !service_selected(channel, requester)))
		return {};
	u32 buffer = 0;
	SDMA_result result = recv_data(channel, &buffer, bytes, eop);
	if (result.transferred)
		data = (T)endian_32(buffer);
	if (arbitrate)
		finish_service(channel, result, requester);
	return result;
}

CDMA::SDMA_result CDMA::recv_unit(int channel, u16& data, bool eop)
{
	return receive_unit(channel, data, eop, false, 0);
}

CDMA::SDMA_result CDMA::recv_unit(int channel, u32& data, bool eop)
{
	return receive_unit(channel, data, eop, false, 0);
}

CDMA::SDMA_result CDMA::service_send_unit(int channel, u32 data, bool eop, u32 requester)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	SDMA_result result;
	if (service_selected(channel, requester))
	{
		result = send_unit(channel, data, eop);
		finish_service(channel, result, requester);
	}
	return result;
}

CDMA::SDMA_result CDMA::service_recv_unit(int channel, u16& data, bool eop, u32 requester)
{
	return receive_unit(channel, data, eop, true, requester);
}

CDMA::SDMA_result CDMA::service_recv_unit(int channel, u32& data, bool eop, u32 requester)
{
	return receive_unit(channel, data, eop, true, requester);
}

CDMA::SDMA_result CDMA::service_eop(int channel, u32 requester)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	validate_device_channel(channel);
	SDMA_result result;
	int ctrlr = channel < 4 ? 0 : 1;
	u8 bit = 1 << (channel & 0x03);
	u8 mode = state.channel[channel].mode & 0xc0;
	// EOP cannot acquire service or terminate a channel while it is idle.
	if ((state.controller[ctrlr].command & 0x04) || !accepts_external_eop(channel) ||
		!block_allows_requester(channel, requester))
		return result;
	if (!((mode == 0x00 && (state.controller[ctrlr].demand_active & bit)) ||
		(mode == 0x80 && (state.controller[ctrlr].block_active & bit))))
		return result;
	// A retained lower request may have lost its accepted upper grant.
	if (channel < 4 && (!state.cascade_active || !cascade_enabled()))
		return result;

	// No new memory cycle or arbitration: complete only the accepted service.
	complete_transfer(channel, result);
	result.blocked = false;
	result.external_eop = true;
	transfer_serviced(channel, result);
	return result;
}
