/* ES40 emulator.
 * Copyright (C) 2026 by Cacodemon345
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Derived from the formerly stubbed MPU-401 emulation of VSBHDA
 * (https://github.com/Baron-von-Riedesel/VSBHDA), a fork of SBEMU, both
 * distributed under the GNU General Public License version 2.
 */

#include "MPU401.h"

#ifdef _WIN32
#include "chipset/ISABus.h"
#include "config/ISASlot.h"
#pragma comment(lib, "winmm.lib")

CMPU401::CMPU401(CConfigurator* cfg, CSystem* system, CISABus& bus, u16 io_port)
	: CSystemComponent(cfg, system), bus(bus), io_port(io_port)
{
	bus.register_isa_io(*this, 0, io_port, mpu401_io_ports.count);
}

CMPU401::~CMPU401()
{
	if (hmo) midiOutClose(hmo);
}

void CMPU401::init()
{
	const UINT device = static_cast<UINT>(myCfg->get_num_value("midi_out", true, 0));
	if (midiOutOpen(&hmo, device, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
		FAILURE_1(Configuration, "MPU-401: Failed to open MIDI output device %u.", device);

	UINT id;
	MIDIOUTCAPSA caps;
	if (midiOutGetID(hmo, &id) == MMSYSERR_NOERROR &&
		midiOutGetDevCapsA(id, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
		printf("MPU-401: Opened MIDI device \"%s\".\n", caps.szPname);
	else
		printf("MPU-401: Opened MIDI device.\n");
}

std::string CMPU401::snapshot_identity() const
{
	return CSystemComponent::snapshot_identity() + ":" + std::to_string(io_port);
}

const CSystemComponent* CMPU401::memory_decode_owner() const noexcept
{
	return bus.isa_decode_owner();
}

bool CMPU401::decodes_memory_access(int index, u64 address, int, bool) const noexcept
{
	return index == 0 && address < mpu401_io_ports.count &&
		bus.isa_io_decode(io_port + (u32)address) != CISABus::IsaIoDecode::None;
}

bool CMPU401::uses_subtractive_decode(int index, u64 address, int, bool) const noexcept
{
	return index == 0 && address < mpu401_io_ports.count &&
		bus.isa_io_decode(io_port + (u32)address) == CISABus::IsaIoDecode::Subtractive;
}

u64 CMPU401::ReadMem(int index, u64 address, int dsize)
{
	if (dsize == 16)
	{
		const u64 low = ReadMem(index, address, 8);
		return low | (ReadMem(index, address + 1, 8) << 8);
	}
	if (dsize > 16) return 0;
	if (!decodes_memory_access(index, address, 8, false)) return 0xff;
	if (address == 1) return midi_mpu_status;
	midi_mpu_status |= 0x80;
	bReset = false;
	return 0xfe; // Command acknowledgement or Active Sensing.
}

void CMPU401::WriteMem(int index, u64 address, int dsize, u64 data)
{
	if (dsize == 16)
	{
		WriteMem(index, address, 8, (data >> 0) & 0xff);
		WriteMem(index, address + 1, 8, (data >> 8) & 0xff);
		return;
	}
	if (dsize > 16 || !decodes_memory_access(index, address, 8, true)) return;
	// Byte I/O ignores upper bits of the CPU register supplied by the caller.
	const u8 byte = static_cast<u8>(data);
	if (address == 1)
	{
		if (byte == 0x3f)
		{
			midi_mpu_status &= ~0x80;
		}
		if (byte == 0xff)
		{
			bReset = true;
			midi_ptr = 0;
			midi_mpu_status &= ~0x80;
		}
	}
	else
	{
		if (!bReset)
		{
			if (midi_status_byte == 0xF0 && byte != 0xF7)
			{
				sysex_data.push_back(byte);
				return;
			}
			if (midi_status_byte == 0xF0 && byte == 0xF7)
			{
				sysex_data.push_back(byte);
				hdr.dwBufferLength = sysex_data.size();
				hdr.lpData = (LPSTR)sysex_data.data();
				midiOutPrepareHeader(hmo, &hdr, sizeof(hdr));
				midiOutLongMsg(hmo, &hdr, sizeof(hdr));
				midi_status_byte = 0x80;
				midi_buffer[0] = 0x80;
				midi_ptr = 0;
				return;
			}
			if ((byte & 0xF0) < 0x80 && midi_ptr == 0)
			{
				midi_buffer[0] = midi_status_byte;
				midi_ptr = 1;
			}

			midi_buffer[midi_ptr++] = byte;
			midi_status_byte = midi_buffer[0];

			if (midi_buffer[0] == 0xF0)
			{
				sysex_data.clear();
				sysex_data.push_back(0xF0);
				return;
			}

			if (midi_ptr >= midi_lengths[(midi_buffer[0] >> 4) - 0x8])
			{
				midiOutShortMsg(hmo, *(DWORD*)(&midi_buffer[0]));
				midi_ptr = 0;
			}
		}
	}
	return;
}

#endif
