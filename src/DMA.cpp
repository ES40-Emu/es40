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
  * \author Camiel Vanderhoeven (camiel@camicom.com / http://www.camicom.com)
  **/
#include "StdAfx.h"
#include "System.h"
#include "DMA.h"
#include "AliM1543C.h"
#include "PCIDevice.h"

CDMA* theDMA = 0;

/**
 * Constructor.
 **/
CDMA::CDMA(CConfigurator* cfg, CSystem* c) : CSystemComponent(cfg, c)
{
	// DMA Setup
#define LEGACY_IO(id,port,size) c->RegisterMemory(this, id, U64(0x00000801fc000000) + port, size)
	LEGACY_IO(DMA0_IO_CHANNEL, 0x00, 8);
	LEGACY_IO(DMA0_IO_MAIN, 0x08, 8);
	LEGACY_IO(DMA_IO_LPAGE, 0x81, 11);
	LEGACY_IO(DMA1_IO_CHANNEL, 0xc0, 16);
	LEGACY_IO(DMA1_IO_MAIN, 0xd0, 16);
	LEGACY_IO(DMA_IO_HPAGE, 0x481, 11);
	LEGACY_IO(DMA0_IO_EXT, 0x040b, 1);
	LEGACY_IO(DMA1_IO_EXT, 0x04D6, 1);

	memset(&state, 0, sizeof(state));
	for (int controller = 0; controller < 2; controller++)
	{
		state.controller[controller].mask = 0x0f;
		state.controller[controller].lobyte = true;
	}

	theDMA = this;
	printf("dma: $Id$\n");
}

/**
 * Destructor.
 **/
CDMA::~CDMA()
{
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

static size_t dma_transfer_width(int channel)
{
	if (channel < 0 || channel >= 8 || channel == 4)
		FAILURE(InvalidArgument, "dma: invalid device channel (channel 4 is cascade)");

	return channel < 4 ? 1 : 2;
}

static u64 dma_address(u16 pagebase, u16 current, size_t width)
{
	if (!theAli || !(theAli->config_read(0, 0x42, 8) & 0x40))
		pagebase &= 0x00ff;

	// Word channels drive A16 from the current address, not low-page bit 0.
	if (width == 2)
		pagebase &= 0xfffe;

	return ((u64)pagebase << 16) | ((u64)current * width);
}

static size_t dma_increment_chunk_size(u16 current, size_t width, size_t count)
{
	// The 16-bit current register wraps without carrying into the page registers.
	size_t bytes_to_wrap = ((size_t)0x10000 - current) * width;
	return count < bytes_to_wrap ? count : bytes_to_wrap;
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


u64 CDMA::ReadMem(int index, u64 address, int dsize)
{
	u64 ret;
	u8  data = 0;
	int ctrlr;
	int num;
	//printf("dma: Readmem %s, %" PRIx64 ", %x\n",DMA_INDEX(index),address, dsize);
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




		if (index == DMA1_IO_CHANNEL || index == DMA1_IO_MAIN)
			address >>= 1;



		switch (index)
		{
		case DMA0_IO_CHANNEL:
		case DMA1_IO_CHANNEL:
			ctrlr = (index == DMA1_IO_CHANNEL) ? 1 : 0;
			num = ((address & 0x0e) >> 1) + (ctrlr * 4);
			if (address & 1)
			{
				// word count registers
				data = (state.channel[num].count >>
					(state.controller[ctrlr].lobyte ? 0 : 8)) & 0xff;
			}
			else
			{
				// current address
				data = (state.channel[num].current >>
					(state.controller[ctrlr].lobyte ? 0 : 8)) & 0xff;
			}
			state.controller[ctrlr].lobyte =
				!state.controller[ctrlr].lobyte;
			break;

		case DMA0_IO_MAIN:
		case DMA1_IO_MAIN:
			ctrlr = (index == DMA1_IO_MAIN) ? 1 : 0;
			if (address == 0)
			{
				// Pending requests are visible even when masked or disabled.
				u8 requests = state.controller[ctrlr].request | state.controller[ctrlr].drq;
				data = state.controller[ctrlr].status | ((requests & 0x0f) << 4);
				state.controller[ctrlr].status = 0;
			}
			else if (address == 7)
			{
				data = state.controller[ctrlr].mask & 0x0f;
			}
			break;

		case DMA_IO_LPAGE:
		case DMA_IO_HPAGE:
			num = dma_page_channel(address);
			if (num < 0)
			{
				data = 0xff;
				break;
			}
			if (index == DMA_IO_LPAGE)
				data = state.channel[num].pagebase & 0xff;
			else
				data = (state.channel[num].pagebase >> 8) & 0xff;
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
		if (index == DMA1_IO_CHANNEL || index == DMA1_IO_MAIN)
			address >>= 1;

#if defined(DEBUG_DMA)
		//printf("dma: write %s, %02x: %02x.   \n", DMA_INDEX(index), (u32)address, data);
#endif
		switch (index)
		{
		case DMA0_IO_CHANNEL:
		case DMA1_IO_CHANNEL:
		{
			int ctrlr = (index == DMA1_IO_CHANNEL) ? 1 : 0;
			num = ((address & 0x0e) >> 1) + (ctrlr * 4);
			if (address & 1)
			{
				if (state.controller[ctrlr].lobyte)
					state.channel[num].base_count =
						(state.channel[num].base_count & 0xff00) | data;
				else
					state.channel[num].base_count =
						(state.channel[num].base_count & 0x00ff) | (data << 8);
				state.channel[num].count = state.channel[num].base_count;
#if defined(DEBUG_DMA)
				printf("dma channel %d count: %04x\n", num, state.channel[num].count);
#endif	
			}
			else {
				if (state.controller[ctrlr].lobyte)
					state.channel[num].base = (state.channel[num].base & 0xff00) | data;
				else
					state.channel[num].base = (state.channel[num].base & 0x00ff) | (data << 8);
				state.channel[num].current = state.channel[num].base;
#if defined(DEBUG_DMA)
				printf("dma channel %d base: %04x\n", num, state.channel[num].base);
#endif	
			}
			state.controller[ctrlr].lobyte =
				!state.controller[ctrlr].lobyte;
			break;
		}

		case DMA1_IO_MAIN:
		case DMA0_IO_MAIN:
		{
			int ctrlr = (index == DMA1_IO_MAIN) ? 1 : 0;
			num = ctrlr;
			switch (address) {
			case 0: // command
				if (DMA_TRACE_CONTROLLER(num))
					printf("dma: command register %d written with %" PRIx64 "\n", num, data);
				state.controller[num].command = data;
				break;

			case 1: // request
				set_request(num, data & 0x03, (data & 0x04) >> 2);
				break;

			case 2: // single mask
				if (DMA_TRACE_CHANNEL((num * 4) + (data & 0x03)))
					printf("dma: mask single on %d : %" PRId64 " %s\n", num, data & 0x03, data & 0x4 ? "Masked" : "Unmasked");
				state.controller[num].mask = (state.controller[num].mask & ~(1 << (data & 0x03))) | (((data & 0x04) >> 2) << (data & 0x03));
				if (DMA_TRACE_CHANNEL((num * 4) + (data & 0x03)))
					printf("     Mask status: %x\n", state.controller[num].mask);
				do_dma();
				break;

			case 3: // mode register
				if (DMA_TRACE_CHANNEL((num * 4) + (data & 0x03)))
				{
					printf("dma: mode register %d for channel %" PRId64 " written with %" PRIx64 "\n", num, (num * 4) + (data & 0x03), data);
					printf("    Mode: %s, Address %s, Autoinit %s, Command: %s\n",
						(data & 0x80 ? (data & 0x40 ? "Cascade" : "Block") : (data & 0x40 ? "Single" : "Demand")),
						(data & 0x20 ? "Decrement" : "Increment"),
						(data & 0x10 ? "Enable" : "Disable"),
						(data & 0x08 ? (data & 0x04 ? "Illegal" : "Read") : (data & 0x04 ? "Write" : "Verify")));
				}


				state.channel[(num * 4) + (data & 0x03)].mode = data;
				break;

			case 4: // clear flipflop(s)
				if (DMA_TRACE_CONTROLLER(num))
					printf("dma: flipflops cleared for dma %d\n", num);
				state.controller[num].lobyte = true;
				break;

			case 5: // master reset
#if defined(DEBUG_DMA)
				printf("DMA-I-RESET: DMA %d reset.", index - DMA_IO_BASE);
#endif
				state.controller[num].lobyte = true;
				state.controller[num].command = 0;
				state.controller[num].status = 0;
				state.controller[num].request = 0;
				// Reset the controller, not the request lines driven by devices.
				state.controller[num].mask = 0x0f;
				break;

			case 6: // master enable
				state.controller[num].mask = 0x00;
				do_dma();
				break;


			case 7: // master mask
				state.controller[num].mask = data & 0x0f;
				do_dma();
				break;
			}
			break;
		}

		case DMA_IO_LPAGE:
		case DMA_IO_HPAGE:
			num = dma_page_channel(address);
			if (num < 0)
				return;
			if (index == DMA_IO_LPAGE)
				state.channel[num].pagebase = (state.channel[num].pagebase & 0xff00) | data;
			else
				state.channel[num].pagebase = (state.channel[num].pagebase & 0xff) | (data << 8);

#if defined(DEBUG_DMA)
			printf("dma channel %d pagebase: %04x\n", num, state.channel[num].pagebase);
#endif      

			break;


		case DMA0_IO_EXT:
		case DMA1_IO_EXT:
			if (DMA_TRACE_CONTROLLER(index - DMA0_IO_EXT))
				printf("dma: extended mode register %d written: %02" PRIx64 "\n", index - DMA0_IO_EXT, data);
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
	// Apply the same device-channel validation as the transfer interfaces.
	dma_transfer_width(channel);
	int ctrlr = channel < 4 ? 0 : 1;
	int local_channel = channel & 0x03;
	if (asserted)
		state.controller[ctrlr].drq |= (1 << local_channel);
	else
		state.controller[ctrlr].drq &= ~(1 << local_channel);
	do_dma();
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
		if ((state.controller[ctrlr].command & 0x04) == 0) // controller not disabled.
		{
			for (int chnl = 0; chnl < 4; chnl++)
			{
				if ((state.controller[ctrlr].mask & (1 << chnl)) == 0) // channel not masked
				{
					if ((state.controller[ctrlr].request | state.controller[ctrlr].drq) &
						(1 << chnl)) // channel has a software or device request
					{
						// Do it!
					}
				}
			}
		}
	}
}

size_t CDMA::get_transfer_size(int channel)
{
	size_t width = dma_transfer_width(channel);
	return ((size_t)state.channel[channel].count + 1) * width;
}

/**
 * Advance the current registers by transferred byte/word units and report TC.
 **/
bool CDMA::advance_transfer(int channel, size_t units, bool eop)
{
	// Check before narrowing: a full transfer can contain 65536 units.
	bool terminal_count = units == (size_t)state.channel[channel].count + 1;

	if (state.channel[channel].mode & 0x20)
		state.channel[channel].current -= (u16)units;
	else
		state.channel[channel].current += (u16)units;
	state.channel[channel].count -= (u16)units;

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
	size_t width = dma_transfer_width(channel);
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

	// The PCI helpers otherwise silently skip DMA when bus mastering is disabled.
	if (!theAli || !(theAli->config_read(0, 0x04, 16) & 0x04))
		return result;

	if ((state.controller[ctrlr].command & 0x04) == 0)
	{
		if ((state.controller[ctrlr].mask & (1 << local_channel)) == 0)
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
			if (count % width)
				FAILURE(InvalidArgument, "dma: word-channel transfer length must be even");
			size_t units = count / width;

			if (transfer_type == 0x00)
			{
				if (DMA_TRACE_CHANNEL(channel))
					printf("dma: verify on channel %d: %zx bytes.\n", channel, count);
				result.terminal_count = advance_transfer(channel, units, eop);
				result.external_eop = eop;
				result.blocked = false;
				return result;
			}

			u64 addr = dma_address(state.channel[channel].pagebase,
				state.channel[channel].current, width);
			if (DMA_TRACE_CHANNEL(channel))
			{
				printf("DMA send_data:  %zx @ %16" PRIx64 "\n  ", count, addr);
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
				u16 current = state.channel[channel].current;
				// Decrement by units, preserving the byte order within each word.
				for (size_t offset = 0; offset < count; offset += width)
				{
					addr = dma_address(state.channel[channel].pagebase, current, width);
					theAli->do_pci_write((u32)addr, (u8*)data + offset, 1, width);
					current--;
				}
			}
			else
			{
				size_t first_count = dma_increment_chunk_size(state.channel[channel].current,
					width, count);
				theAli->do_pci_write((u32)addr, data, 1, first_count);
				// A programmed transfer contains at most 65536 units, so only one wrap is possible.
				if (first_count < count)
				{
					u64 wrap_addr = dma_address(state.channel[channel].pagebase, 0, width);
					theAli->do_pci_write((u32)wrap_addr, (u8*)data + first_count, 1,
						count - first_count);
				}
			}
			result.terminal_count = advance_transfer(channel, units, eop);
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
	size_t width = dma_transfer_width(channel);
	int ctrlr = channel < 4 ? 0 : 1;
	int local_channel = channel & 0x03;
	SDMA_result result = { 0, true, false, false };
	if ((state.channel[channel].mode & 0xc0) == 0xc0)
	{
		if (DMA_TRACE_CHANNEL(channel))
			printf("dma: receive on channel %d blocked by cascade mode.\n", channel);
		return result;
	}

	// Avoid zero-filling the device buffer and advancing a blocked transfer.
	if (!theAli || !(theAli->config_read(0, 0x04, 16) & 0x04))
		return result;

	if ((state.controller[ctrlr].command & 0x04) == 0)
	{
		if ((state.controller[ctrlr].mask & (1 << local_channel)) == 0)
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
			if (count % width)
				FAILURE(InvalidArgument, "dma: word-channel transfer length must be even");
			size_t units = count / width;

			if (transfer_type == 0x00)
			{
				if (DMA_TRACE_CHANNEL(channel))
					printf("dma: verify on channel %d: %zx bytes.\n", channel, count);
				result.terminal_count = advance_transfer(channel, units, eop);
				result.external_eop = eop;
				result.blocked = false;
				return result;
			}

			u64 addr = dma_address(state.channel[channel].pagebase,
				state.channel[channel].current, width);
			if (DMA_TRACE_CHANNEL(channel))
				printf("DMA recv_data:  %zx @ %16" PRIx64 "\n", count, addr);
			if (state.channel[channel].mode & 0x20)
			{
				u16 current = state.channel[channel].current;
				for (size_t offset = 0; offset < count; offset += width)
				{
					addr = dma_address(state.channel[channel].pagebase, current, width);
					theAli->do_pci_read((u32)addr, (u8*)data + offset, 1, width);
					current--;
				}
			}
			else
			{
				size_t first_count = dma_increment_chunk_size(state.channel[channel].current,
					width, count);
				theAli->do_pci_read((u32)addr, data, 1, first_count);
				if (first_count < count)
				{
					u64 wrap_addr = dma_address(state.channel[channel].pagebase, 0, width);
					theAli->do_pci_read((u32)wrap_addr, (u8*)data + first_count, 1,
						count - first_count);
				}
			}
			result.terminal_count = advance_transfer(channel, units, eop);
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
	size_t width = dma_transfer_width(channel);
	u8 buffer[2] = { (u8)data, (u8)(data >> 8) };
	return send_data(channel, buffer, width, eop);
}

CDMA::SDMA_result CDMA::recv_unit(int channel, u16& data, bool eop)
{
	size_t width = dma_transfer_width(channel);
	u8 buffer[2] = { 0, 0 };
	SDMA_result result = recv_data(channel, buffer, width, eop);
	if (result.transferred == width)
		data = (u16)(buffer[0] | ((u16)buffer[1] << 8));
	return result;
}
