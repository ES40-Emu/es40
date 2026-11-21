/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
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

/**
 * Constructor.
 **/
CDMA::CDMA(CConfigurator* cfg, CSystem* c, CDMAHost& host) :
	CSystemComponent(cfg, c), dma_host(host)
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

static int dma_page_channel(u64 address)
{
	static const int channelmap[] = { 2, 3, 1, 0xff, 0xff, 0xff, 0,
				 0xff, 6, 7, 5 };

	if (address >= (sizeof(channelmap) / sizeof(channelmap[0])) ||
		channelmap[address] == 0xff)
		return -1;

	return channelmap[address];
}

static int dma_low_page_channel(u16 port)
{
	return port >= dma_low_page_base ? dma_page_channel(port - dma_low_page_base) : -1;
}

static bool dma_mapped_legacy_port(int index, u64 offset, u16& port)
{
	switch (index)
	{
	case DMA0_IO_CHANNEL: port = 0x00; break;
	case DMA0_IO_MAIN:    port = 0x08; break;
	case DMA_IO_LPAGE:    port = dma_low_page_base; break;
	case DMA1_IO_CHANNEL: port = 0xc0; break;
	case DMA1_IO_MAIN:    port = 0xd0; break;
	default: return false;
	}
	// Keep an out-of-bank offset from wrapping into a valid port.
	port = offset < 0x20 ? (u16)(port + offset) : 0xffff;
	return true;
}

static bool dma_legacy_register(u16 port, int& ctrlr, int& reg)
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
	// ALi's ISA channels count bytes or shifted words. Timing selection does
	// not change data width, count units or addressing on this controller.
	if (channel < 4)
		return { 1, 1, 1, 0, 0xffff };
	return { 2, 2, 1, 1, 0xffff };
}

u32 CDMA::dma_address(u16 pagebase, u32 current,
	const SDMA_transfer_properties& properties)
{
	// Shifted addressing drives A16 from current, not low-page bit 0.
	pagebase &= (u16)(0xffffU << properties.address_shift);

	return ((u32)pagebase << 16) | (current << properties.address_shift);
}

u32 CDMA::step_address(u32 current, size_t units, bool decrement,
	const SDMA_transfer_properties& properties)
{
	u32 delta = (u32)(units * properties.address_step);
	return (decrement ? current - delta : current + delta) & properties.address_mask;
}

size_t CDMA::increment_chunk_size(u32 current, size_t length,
	const SDMA_transfer_properties& properties)
{
	// Current ISA geometry is contiguous and wraps only the current register.
	size_t units_to_wrap = ((size_t)properties.address_mask + 1 - current) /
		properties.address_step;
	size_t bytes_to_wrap = units_to_wrap * properties.data_width;
	return length < bytes_to_wrap ? length : bytes_to_wrap;
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
	const u16 value = (offset & 2) ? channel.base_count : channel.base;
	return (u8)((offset & 1) ? value >> 8 : value);
}

u8 CDMA::read_legacy_port(u16 port)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	int ctrlr, reg;
	if (!dma_legacy_register(port, ctrlr, reg))
	{
		const int num = dma_low_page_channel(port);
		const u8 data = num < 0 ? 0xff : (u8)state.channel[num].pagebase;
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
		const u16 value = (reg & 1) ? channel.count : channel.current;
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

void CDMA::write_legacy_port(u16 port, u8 data)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	int ctrlr, reg;
	if (!dma_legacy_register(port, ctrlr, reg))
	{
		const int num = dma_low_page_channel(port);
		if (num < 0) return;
		release_service(num);
		u16& page = state.channel[num].pagebase;
		page = (page & 0xff00) | data;
#if defined(DEBUG_DMA)
		printf("dma channel %d pagebase: %04x\n", num, (unsigned)page);
#endif
		return;
	}

	auto& controller = state.controller[ctrlr];
	if (reg < 8)
	{
		const int num = ctrlr * 4 + reg / 2;
		release_service(num);
		auto& channel = state.channel[num];
		u16& base = (reg & 1) ? channel.base_count : channel.base;
		if (controller.lobyte)
			base = (base & 0xff00) | data;
		else
			base = (base & 0x00ff) | (data << 8);
		if (reg & 1)
			channel.count = base;
		else
			channel.current = base;
		controller.lobyte = !controller.lobyte;
#if defined(DEBUG_DMA)
		printf("dma channel %d %s: %04x\n", num, (reg & 1) ? "count" : "base", (unsigned)base);
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
		{
			controller.mask |= 1 << local_channel;
			release_service(selected_channel);
		}
		else
			controller.mask &= ~(1 << local_channel);
		if (DMA_TRACE_CHANNEL(selected_channel))
			printf("     Mask status: %x\n", (unsigned)controller.mask);
		do_dma();
		break;

	case 0x0b: // mode register
		if (DMA_TRACE_CHANNEL(selected_channel))
		{
			printf("dma: mode register %d for channel %d written with %x\n", ctrlr, selected_channel, (unsigned)data);
			printf("    Mode: %s, Address %s, Autoinit %s, Command: %s\n",
				(data & 0x80 ? (data & 0x40 ? "Cascade" : "Block") : (data & 0x40 ? "Single" : "Demand")),
				(data & 0x20 ? "Decrement" : "Increment"),
				(data & 0x10 ? "Enable" : "Disable"),
				(data & 0x08 ? (data & 0x04 ? "Illegal" : "Read") : (data & 0x04 ? "Write" : "Verify")));
		}

		release_service(selected_channel);
		state.channel[selected_channel].mode = data;
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
			release_service(ctrlr * 4 + channel);
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

u64 CDMA::ReadMem(int index, u64 address, int dsize)
{
	u16 port;
	if (dsize == 8 && dma_mapped_legacy_port(index, address, port))
		return read_legacy_port(port);

	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	u64 ret;
	u8  data = 0;
	int num;
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
		case DMA_IO_HPAGE:
			num = dma_page_channel(address);
			if (num < 0)
			{
				data = 0xff;
				break;
			}
			data = (state.channel[num].pagebase >> 8) & 0xff;
			break;

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
	if (dsize == 8 && dma_mapped_legacy_port(index, address, port))
	{
		write_legacy_port(port, (u8)data);
		return;
	}

	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	int num = 0;
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
		case DMA_IO_HPAGE:
			num = dma_page_channel(address);
			if (num < 0)
				return;
			release_service(num);
			state.channel[num].pagebase = (state.channel[num].pagebase & 0xff) | (data << 8);

#if defined(DEBUG_DMA)
			printf("dma channel %d pagebase: %04x\n", num, state.channel[num].pagebase);
#endif      

			break;


		case DMA0_IO_EXT:
		case DMA1_IO_EXT:
			// Each extended register occupies one byte; wider accesses are split
			// above, and the following bytes must not alias this register.
			if (address != 0)
				return;
			// M1543C 4.2.1: channel select 1:0, timing 5:4; all other bits reserved.
			data &= 0x33;
			num = (index == DMA1_IO_EXT ? 4 : 0) + (int)(data & 0x03);
			state.ali.cycle_timing[num] = (u8)(data & 0x30);
			if (DMA_TRACE_CONTROLLER(index - DMA0_IO_EXT))
				printf("dma: ALi extended mode channel %d written: %02" PRIx64 " (%s timing)\n",
					num, data, state.ali.cycle_timing[num] == 0x30 ? "Type F" : "compatible");
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

	state = restored;
	printf("dma: %ld bytes restored.\n", ss);
	return 0;
}

/**
 * Set the software request bit for a channel, and initiate DMA
 **/
void CDMA::set_request(int num, int channel, int data) {
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	channel &= 0x03;
	if (data)
		state.controller[num].request |= (1 << channel);
	else
		state.controller[num].request &= ~(1 << channel);
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
	u8 requests = state.controller[ctrlr].request | state.controller[ctrlr].drq;
	// Controller 0's HRQ drives controller 1's channel-4 request input.
	// Software and retained block requests bypass the mask; incoming DRQ does not.
	if (ctrlr == 1 && !(state.controller[0].command & 0x04) &&
		((state.controller[0].request | state.controller[0].block_active |
		(state.controller[0].drq & ~state.controller[0].mask)) & 0x0f))
		requests |= 0x01;
	return requests & 0x0f;
}

bool CDMA::cascade_enabled() const
{
	// These gate the grant, not the request input seen in controller 1's status.
	return !(state.controller[1].command & 0x04) &&
		!(state.controller[1].mask & 0x01) &&
		(state.channel[4].mode & 0xc0) == 0xc0;
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
	// Single and block service accept software requests even while masked.
	if ((mode == 0x40 || mode == 0x80) && (state.controller[ctrlr].request & bit))
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

bool CDMA::service_selected(int channel) const
{
	return service_requested(channel) && select_service_channel(1) == channel;
}

bool CDMA::service_ready(int channel, u8 transfer_type) const
{
	if (!service_selected(channel) || !dma_host.dma_transfer_enabled())
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
void CDMA::finish_service(int channel, const SDMA_result& result)
{
	// Verify services a unit despite transferring zero bytes.
	if (result.blocked)
		return;
	int ctrlr = channel < 4 ? 0 : 1;
	u8 bit = 1 << (channel & 0x03);
	u8 mode = state.channel[channel].mode & 0xc0;
	if (result.terminal_count || result.external_eop || mode == 0x40 ||
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
		state.controller[ctrlr].block_active |= bit;
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
					((state.controller[ctrlr].request | state.controller[ctrlr].block_active) &
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

bool CDMA::can_service_send(int channel)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	return service_ready(channel, 0x04); // Device to memory (8237 Write).
}

bool CDMA::can_service_recv(int channel)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	return service_ready(channel, 0x08); // Memory to device (8237 Read).
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

/**
 * Account for serviced bytes, including verify, and report terminal count.
 **/
bool CDMA::advance_transfer(int channel, size_t length,
	const SDMA_transfer_properties& properties, bool eop)
{
	size_t units = length / properties.data_width;
	size_t count_units = length / properties.count_unit;
	// Check before narrowing: a full transfer can contain 65536 units.
	bool terminal_count = count_units == (size_t)state.channel[channel].count + 1;

	state.channel[channel].current = (u16)step_address(state.channel[channel].current,
		units, (state.channel[channel].mode & 0x20) != 0, properties);
	state.channel[channel].count -= (u16)count_units;

	if (terminal_count || eop)
		complete_transfer(channel);
	return terminal_count;
}

/**
 * Complete TC or external EOP without consuming any additional DMA units.
 **/
void CDMA::complete_transfer(int channel)
{
	int ctrlr = channel < 4 ? 0 : 1;
	int local_channel = channel & 0x03;
	state.controller[ctrlr].status |= 1 << local_channel;
	// TC and EOP clear the software request; the device owns its DRQ level.
	state.controller[ctrlr].request &= ~(1 << local_channel);
	release_service(channel);
	if (state.channel[channel].mode & 0x10)
	{
		state.channel[channel].current = state.channel[channel].base;
		state.channel[channel].count = state.channel[channel].base_count;
	}
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
	SDMA_result result = { 0, true, false, false };
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
			((state.controller[ctrlr].request | state.controller[ctrlr].block_active) &
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
			size_t count = get_transfer_size(channel);
			if (length > 0 && length < count) count = length;
			if (count % properties.data_width)
				FAILURE(InvalidArgument, "dma: word-channel transfer length must be even");

			if (transfer_type == 0x00)
			{
				if (DMA_TRACE_CHANNEL(channel))
					printf("dma: verify on channel %d: %zx bytes.\n", channel, count);
				result.terminal_count = advance_transfer(channel, count, properties, eop);
				result.external_eop = eop;
				result.blocked = false;
				return result;
			}

			u32 addr = dma_address(state.channel[channel].pagebase,
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

			// Device buffers are byte streams, even for word channels.
			if (state.channel[channel].mode & 0x20)
			{
				u32 current = state.channel[channel].current;
				// Decrement by units, preserving the byte order within each word.
				for (size_t offset = 0; offset < count; offset += properties.data_width)
				{
					addr = dma_address(state.channel[channel].pagebase, current, properties);
					dma_host.dma_write_memory(addr, (u8*)data + offset, properties.data_width);
					current = step_address(current, 1, true, properties);
				}
			}
			else
			{
				size_t first_count = increment_chunk_size(state.channel[channel].current,
					count, properties);
				dma_host.dma_write_memory(addr, data, first_count);
				// A programmed transfer contains at most 65536 units, so only one wrap is possible.
				if (first_count < count)
				{
					u32 wrap_addr = dma_address(state.channel[channel].pagebase, 0, properties);
					dma_host.dma_write_memory(wrap_addr, (u8*)data + first_count,
						count - first_count);
				}
			}
			result.terminal_count = advance_transfer(channel, count, properties, eop);
			result.external_eop = eop;
			result.transferred = count;
			result.blocked = false;
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
	SDMA_result result = { 0, true, false, false };
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
			((state.controller[ctrlr].request | state.controller[ctrlr].block_active) &
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
			size_t count = get_transfer_size(channel);
			if (length > 0 && length < count) count = length;
			if (count % properties.data_width)
				FAILURE(InvalidArgument, "dma: word-channel transfer length must be even");

			if (transfer_type == 0x00)
			{
				if (DMA_TRACE_CHANNEL(channel))
					printf("dma: verify on channel %d: %zx bytes.\n", channel, count);
				result.terminal_count = advance_transfer(channel, count, properties, eop);
				result.external_eop = eop;
				result.blocked = false;
				return result;
			}

			u32 addr = dma_address(state.channel[channel].pagebase,
				state.channel[channel].current, properties);
			if (DMA_TRACE_CHANNEL(channel))
				printf("DMA recv_data:  %zx @ controller address %08" PRIx32 "\n", count, addr);
			if (state.channel[channel].mode & 0x20)
			{
				u32 current = state.channel[channel].current;
				for (size_t offset = 0; offset < count; offset += properties.data_width)
				{
					addr = dma_address(state.channel[channel].pagebase, current, properties);
					dma_host.dma_read_memory(addr, (u8*)data + offset, properties.data_width);
					current = step_address(current, 1, true, properties);
				}
			}
			else
			{
				size_t first_count = increment_chunk_size(state.channel[channel].current,
					count, properties);
				dma_host.dma_read_memory(addr, data, first_count);
				if (first_count < count)
				{
					u32 wrap_addr = dma_address(state.channel[channel].pagebase, 0, properties);
					dma_host.dma_read_memory(wrap_addr, (u8*)data + first_count,
						count - first_count);
				}
			}
			result.terminal_count = advance_transfer(channel, count, properties, eop);
			result.external_eop = eop;
			result.transferred = count;
			result.blocked = false;
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

CDMA::SDMA_result CDMA::send_unit(int channel, u16 data, bool eop)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const SDMA_transfer_properties properties = transfer_properties(channel);
	u8 buffer[2] = { (u8)data, (u8)(data >> 8) };
	return send_data(channel, buffer, properties.data_width, eop);
}

CDMA::SDMA_result CDMA::recv_unit(int channel, u16& data, bool eop)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	const SDMA_transfer_properties properties = transfer_properties(channel);
	u8 buffer[2] = { 0, 0 };
	SDMA_result result = recv_data(channel, buffer, properties.data_width, eop);
	if (result.transferred == properties.data_width)
		data = (u16)(buffer[0] | ((u16)buffer[1] << 8));
	return result;
}

CDMA::SDMA_result CDMA::service_send_unit(int channel, u16 data, bool eop)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	SDMA_result result = { 0, true, false, false };
	if (service_selected(channel))
	{
		result = send_unit(channel, data, eop);
		finish_service(channel, result);
	}
	return result;
}

CDMA::SDMA_result CDMA::service_recv_unit(int channel, u16& data, bool eop)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	SDMA_result result = { 0, true, false, false };
	if (service_selected(channel))
	{
		result = recv_unit(channel, data, eop);
		finish_service(channel, result);
	}
	return result;
}

CDMA::SDMA_result CDMA::service_eop(int channel)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	validate_device_channel(channel);
	SDMA_result result = { 0, true, false, false };
	int ctrlr = channel < 4 ? 0 : 1;
	u8 bit = 1 << (channel & 0x03);
	u8 mode = state.channel[channel].mode & 0xc0;
	// EOP cannot acquire service or terminate a channel while it is idle.
	if (state.controller[ctrlr].command & 0x04)
		return result;
	if (!((mode == 0x00 && (state.controller[ctrlr].demand_active & bit)) ||
		(mode == 0x80 && (state.controller[ctrlr].block_active & bit))))
		return result;
	// A retained lower request may have lost its accepted upper grant.
	if (channel < 4 && (!state.cascade_active || !cascade_enabled()))
		return result;

	// No new memory cycle or arbitration: complete only the accepted service.
	complete_transfer(channel);
	result.blocked = false;
	result.external_eop = true;
	return result;
}
