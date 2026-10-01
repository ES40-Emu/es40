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
  * Contains the code for the emulated Flash ROM devices.
  *
  * $Id$
  *
  * X-1.19       Camiel Vanderhoeven                             24-MAR-2008
  *      Comments.
  *
  * X-1.18       Camiel Vanderhoeven                             17-MAR-2008
  *      Always set volatile DPR rom contents.
  *
  * X-1.17       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.16       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.15       Camiel Vanderhoeven                             30-DEC-2007
  *      Print file id on initialization.
  *
  * X-1.14       Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.13       Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.12       Camiel Vanderhoeven                             10-DEC-2007
  *      Changes to make the TraceEngine work again after recent changes.
  *
  * X-1.11       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.10       Camiel Vanderhoeven                             31-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.9        Camiel Vanderhoeven                             16-FEB-2007
  *      Added functions SaveStateF and RestoreStateF.
  *
  * X-1.8	Brian Wheeler					13-FEB-2007
  *	Formatting.
  *
  * X-1.7 	Camiel Vanderhoeven				12-FEB-2007
  *	Added comments.
  *
  * X-1.6        Camiel Vanderhoeven                             9-FEB-2007
  *      Added comments.
  *
  * X-1.5	Camiel Vanderhoeven				7-FEB-2007
  *	Calls to trace_dev now use the TRC_DEVx macro's.
  *
  * X-1.4        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      64-bit literals made compatible with Linux/GCC/glibc.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Includes are now case-correct (necessary on Linux)
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  *
  * \author Camiel Vanderhoeven
  **/
#include "StdAfx.h"
#include "Flash.h"
#include "System.h"
#include "AlphaCPU.h"
#include <memory>

  // These are the modes for our flash-state-machine.
#define MODE_READ         0
#define MODE_STEP1        1
#define MODE_STEP2        2
#define MODE_AUTOSEL      3
#define MODE_PROGRAM      4
#define MODE_ERASE_STEP3  5
#define MODE_ERASE_STEP4  6
#define MODE_ERASE_STEP5  7
#define MODE_CONFIRM_0    8
#define MODE_CONFIRM_1    9

// Magic from the obsolete wrapped ES40 flash state-file format. Retained so we
// can recognize and warn about those files when loading flash.rom.
static const u32 flash_magic1 = 0xFF3E3FF3;
static const u32 flash_snapshot_magic1 = 0x464c5331;
static const u32 flash_snapshot_magic2 = 0x464c5332;

// SRM partition CPQ signature offsets within a real ES40 flash image.
static const u32 srm_partition_offset = 0x00010000;
static const u32 cpq_sig_offset = 0x14;

extern CAlphaCPU* cpu[4];

/**
 * Constructor.
 **/
CFlash::CFlash(CConfigurator* cfg, CSystem* c) : CSystemComponent(cfg, c)
{
	if (theSROM)
		FAILURE(Configuration, "More than one Flash");
	theSROM = this;
	c->RegisterMemory(this, 0, U64(0x0000080100000000), 0x8000000); // 2MB
	memset(&state, 0, sizeof(state));
	memset(state.Flash, 0xff, sizeof(state.Flash));
	state.mode = MODE_READ;
	RestoreStateF();
	dirty = false;
	state.mode = MODE_READ; // always start in read mode after load

	printf("%s: $Id$\n",
		devid_string);
}

/**
 * Destructor.
 **/
CFlash::~CFlash()
{
	try
	{
		FlushIfDirty();
	}
	catch (const CException& e)
	{
		printf("%%FLS-F-NOSAVE: %s: %s\n", e.what(), e.message().c_str());
	}
	catch (...)
	{
		printf("%%FLS-F-NOSAVE: Could not save flash during shutdown.\n");
	}
	if (theSROM == this)
		theSROM = nullptr;
}

bool CFlash::HasBootFirmware() const
{
	// A real ES40 flash carries a partitioned layout (TIG/SRM/ABIOS/SROM) with
	// a CPQ header at the start of the SRM partition. Treat that signature as
	// the sole marker for "this flash holds bootable firmware".
	const u8* const p = state.Flash + srm_partition_offset + cpq_sig_offset;
	return p[0] == 'C' && p[1] == 'P' && p[2] == 'Q' && p[3] == 0;
}

const u8* CFlash::GetFlashBytes() const
{
	return state.Flash;
}

void CFlash::FlushIfDirty()
{
	if (!dirty) return;
	SaveStateF();
	dirty = false;
}

/**
 * Flush the dirty buffer once writes have been quiet for a couple seconds.
 **/
void CFlash::check_state()
{
	const time_t QUIESCE_SECS = 2;
	if (!dirty) return;
	if (time(nullptr) - last_dirty < QUIESCE_SECS) return;
	FlushIfDirty();
}

/**
 * Read a byte from flashmemory.
 * Normally, this returns one byte from flash, however, after some commands
 * sent to the flash-rom, this returns identification or status information.
 **/
u64 CFlash::ReadMem(int index, u64 address, int dsize)
{
	u64 data = 0;
	int a = (int)(address >> 6);

	// TIGbus flash is an 8-bit device on a wider bus. Only the low 32-bit lane is wired.
	// Ignore reads from the upper lane (addr & 0x4) to match real ES40 behavior and QEMU.
	if (address & 0x4)
		return 0;

	// Out-of-range reads behave like open bus / erased flash.
	if ((unsigned)a >= (unsigned)sizeof(state.Flash))
		return 0xFF;

	switch (state.mode)
	{
	case MODE_AUTOSEL:
		// AM29F016 autoselect IDs alias within each 64 KiB sector.
		// ARC probes sector-local offsets, not only absolute 0/1.
		switch (a & 0xff)
		{
		case 0:   
			data = 1;     // manufacturer
			break;

		case 1:   
			data = 0xad;  // device
			break;

		case 2:
			data = 0;           // sector is not protected
			break;

		default:  
			data = 0;
		}
		break;

	case MODE_CONFIRM_0:
		data = 0x80;
		state.mode = MODE_READ;
		break;

	case MODE_CONFIRM_1:
		data = 0x80;
		state.mode = MODE_CONFIRM_0;
		break;

	default:
		data = state.Flash[a];
		break;
	}

	return data;
}

/**
 * Write command or programming data to flash-rom.
 *
 * The state machine for this looks like this:
 * \code
 *         |
 *         v
 *     MODE_READ <---------------------------+
 *         | write 0x5555:0xaa               |
 *         v                                 |
 *     MODE_STEP1 ---------------------------+
 *         | write 0x2aaa:0x55               |
 *         v                                 |
 *     ==MODE_STEP2= ------------------------+
 * 0x80| 0xa0| 0x90| write 0x5555            |
 *     |     |     v                         |
 *     |     | MODE_AUTOSEL (read device id)-+
 *     |     v                               |
 *     | MODE_PROGRAM                        |
 *     |     | write data byte               |
 *     |     +-------------------------------+
 *     v                                     |
 *  MODE_ERASE_STEP3 ------------------------+
 *     | write 0x5555:0xaa                   |
 *     v                                     |
 *  MODE_ERASE_STEP4 ------------------------+
 *     | write 0x2aaa:0x55                   |
 *     v                                     |
 *  MODE_ERASE_STEP4 ------------------------+
 *   | write 0x30  | write 0x5555:0x10       |
 *   | anywhere    v                         |
 *   v           ERASE ENTIRE FLASH          |
 * ERASE BLOCK     |                         |
 *       |         |                         |
 *       v         v                         |
 *      MODE_CONFIRM1                        |
 *          | read 0x80                      |
 *          v                                |
 *      MODE_CONFIRM2                        |
 *          | read 0x80                      |
 *          +--------------------------------+
 * \endcode
 **/
void CFlash::WriteMem(int index, u64 address, int dsize, u64 data)
{
	// Flash is mapped with 64-byte spacing, so byte index is address >> 6.
	const int a = (int)(address >> 6);
	const int ad = a & 0xffff;
	const u8 byte = (u8)data;

	// Upper 32-bit TIGbus lane is not connected to the flash.
	if (address & 0x4)
		return;

	// sanity check... are we supposed to be here?
	if ((unsigned)a >= (unsigned)sizeof(state.Flash))
	{
		state.mode = MODE_READ;
		return;
	}


	switch (state.mode)
	{
	case MODE_PROGRAM:
	{
		// More realistic flash behavior: programming can only change 1 -> 0.
		// Any attempt to set bits back to 1 requires an erase.
		const u8 oldv = state.Flash[a];
		const u8 newv = (u8)(oldv & byte);

		if (newv != oldv)
		{
			state.Flash[a] = newv;
			dirty = true;
			last_dirty = time(nullptr);
			printf("%%SRM-I-FLASH: Wrote data: 0x%02X to sector address: 0x%04X\n", byte, a);
		}

		state.mode = MODE_READ;
		return;
	}

	case MODE_READ:
	case MODE_AUTOSEL:
		// formerly we bailed as if confirm_0 or confirm_1 here, now
		// explicitly handle those modes for future-proofing.
	case MODE_CONFIRM_0:
	case MODE_CONFIRM_1:
		// AMD-style flashes support "reset/read-array" with 0xF0 or 0xFF.
		// For firmware/software to abort a command sequence.
		if (byte == 0xF0 || byte == 0xFF)
		{
			state.mode = MODE_READ;
			return;
		}

		if ((ad == 0x5555) && (byte == 0xaa))
		{
			state.mode = MODE_STEP1;
			return;
		}

		state.mode = MODE_READ;
		return;

	case MODE_STEP1:
		if (byte == 0xF0 || byte == 0xFF)
		{
			state.mode = MODE_READ;
			return;
		}

		if ((ad == 0x2aaa) && (byte == 0x55))
		{
			state.mode = MODE_STEP2;
			return;
		}

		state.mode = MODE_READ;
		return;

	case MODE_STEP2:
		if (byte == 0xF0 || byte == 0xFF)
		{
			state.mode = MODE_READ;
			return;
		}

		if (ad != 0x5555)
		{
			state.mode = MODE_READ;
			return;
		}

		switch (byte)
		{
		case 0x90:
			state.mode = MODE_AUTOSEL;
			return;

		case 0xa0:
			state.mode = MODE_PROGRAM;
			return;

		case 0x80:
			state.mode = MODE_ERASE_STEP3;
			return;

		default:
			state.mode = MODE_READ;
			return;
		}

	case MODE_ERASE_STEP3:
		if (byte == 0xF0 || byte == 0xFF)
		{
			state.mode = MODE_READ;
			return;
		}

		if ((ad == 0x5555) && (byte == 0xaa))
		{
			state.mode = MODE_ERASE_STEP4;
			return;
		}

		state.mode = MODE_READ;
		return;

	case MODE_ERASE_STEP4:
		if (byte == 0xF0 || byte == 0xFF)
		{
			state.mode = MODE_READ;
			return;
		}

		if ((ad == 0x2aaa) && (byte == 0x55))
		{
			state.mode = MODE_ERASE_STEP5;
			return;
		}

		state.mode = MODE_READ;
		return;

	case MODE_ERASE_STEP5:
		if (byte == 0xF0 || byte == 0xFF)
		{
			state.mode = MODE_READ;
			return;
		}

		// Chip erase: AA/55/80/AA/55/10 at 5555
		if ((ad == 0x5555) && (byte == 0x10))
		{
			printf("%%SRM-I-FLASH: Erasing flash chip\n");
			memset(state.Flash, 0xff, sizeof(state.Flash));
			dirty = true;
			last_dirty = time(nullptr);
			state.mode = MODE_CONFIRM_1;
			return;
		}

		// Sector erase: AA/55/80/AA/55 then 0x30 written "anywhere" in sector
		if (byte == 0x30)
		{
			printf("%%SRM-I-FLASH: Erasing flash sector\n");
			const int sector_start = a & ~0xFFFF;
			memset(&state.Flash[sector_start], 0xFF, 0x10000);
			dirty = true;
			last_dirty = time(nullptr);
			state.mode = MODE_CONFIRM_1;
			return;
		}

		state.mode = MODE_READ;
		return;

	default:
		// unknown mode, reset to read
		state.mode = MODE_READ;
		return;
	}
}

/**
 * Save flash contents as a raw 2 MiB image.
 **/
void CFlash::save_raw_image(const char* fn)
{
	FILE* ff = fopen(fn, "wb");
	if (!ff)
		FAILURE_1(Runtime, "Flash could not be saved to %s", fn);

	const size_t n = fwrite(state.Flash, 1, sizeof(state.Flash), ff);
	const int close_result = fclose(ff);

	if (n != sizeof(state.Flash))
		FAILURE_3(Runtime, "Short flash write (%zu of %zu bytes) to %s",
			n, sizeof(state.Flash), fn);
	if (close_result != 0)
		FAILURE_1(Runtime, "Could not finish writing flash image %s", fn);
	printf("%%FLS-I-SAVEST: Flash saved to %s\n", fn);
}

void CFlash::SaveStateF(char* fn)
{
	save_raw_image(fn);
}

void CFlash::SaveStateF()
{
	SaveStateF(myCfg->get_text_value("rom.flash", "flash.rom"));
}

/**
 * Restore flash contents from a raw 2 MiB image.
 *
 * If the file is missing, a blank 2 MiB (all-0xFF) image is created so SRM
 * always has a persistent place to store console variables regardless of the
 * boot path. A file with the obsolete wrapped ES40 state-file magic at offset
 * 0, or any size other than 2 MiB, aborts startup so the user can move or
 * delete the bad file rather than have it silently overwritten.
 **/
void CFlash::RestoreStateF(char* fn)
{
	FILE* ff = fopen(fn, "rb");
	if (!ff)
	{
		// First boot: create a blank 2 MiB flash so firmware writes have
		// somewhere to land.
		FILE* wf = fopen(fn, "wb");
		if (!wf)
		{
			printf("%%FLS-F-NOCREATE: Flash could not be created at %s\n", fn);
			return;
		}
		const size_t n = fwrite(state.Flash, 1, sizeof(state.Flash), wf);
		const int close_result = fclose(wf);
		if (n != sizeof(state.Flash))
			printf("%%FLS-F-NOCREATE: Short write (%zu of %zu bytes) creating %s\n",
				n, sizeof(state.Flash), fn);
		else if (close_result != 0)
			printf("%%FLS-F-NOCREATE: Could not finish creating flash image %s\n", fn);
		else
			printf("%%FLS-I-CREATE: Blank 2 MiB flash image created at %s\n", fn);
		return;
	}

	fseek(ff, 0, SEEK_END);
	const long file_size = ftell(ff);
	fseek(ff, 0, SEEK_SET);

	if (file_size == (long)sizeof(state.Flash))
	{
		const size_t n = fread(state.Flash, 1, sizeof(state.Flash), ff);
		fclose(ff);
		if (n != sizeof(state.Flash))
		{
			printf("%%FLS-F-SHORTRD: Short read (%zu of %zu bytes) from %s\n",
				n, sizeof(state.Flash), fn);
			FAILURE(Runtime, "Short read loading flash image");
		}
		printf("%%FLS-I-RESTST: Flash restored from %s\n", fn);
		return;
	}

	u32 header = 0;
	auto r = fread(&header, sizeof(header), 1, ff);
	if (r != 1)
		FAILURE_1(IO, "%s: unexpected end of file!\n", fn);
	fclose(ff);

	if (header == flash_magic1)
	{
		printf("%%FLS-F-LEGACYFMT: %s uses the obsolete wrapped ES40 flash format; "
			"rename or delete it, then restart.\n", fn);
		FAILURE(Runtime, "Flash file uses obsolete wrapped format");
	}

	printf("%%FLS-F-BADSIZE: %s is %ld bytes, expected %zu; "
		"rename or delete it, then restart.\n",
		fn, file_size, sizeof(state.Flash));
	FAILURE(Runtime, "Flash file has invalid size");
}

void CFlash::RestoreStateF()
{
	RestoreStateF(myCfg->get_text_value("rom.flash", "flash.rom"));
}

/**
 * Save state to a Virtual Machine State file.
 **/
int CFlash::SaveState(FILE* f)
{
	if (state.mode < MODE_READ || state.mode > MODE_CONFIRM_1)
		return -1;
	const u32 size = (u32)sizeof(state.Flash);
	const u32 mode = (u32)state.mode;
	if (fwrite(&flash_snapshot_magic1, sizeof(flash_snapshot_magic1), 1, f) != 1 ||
		fwrite(&size, sizeof(size), 1, f) != 1 ||
		fwrite(&mode, sizeof(mode), 1, f) != 1 ||
		fwrite(state.Flash, 1, sizeof(state.Flash), f) != sizeof(state.Flash) ||
		fwrite(&flash_snapshot_magic2, sizeof(flash_snapshot_magic2), 1, f) != 1)
		return -1;
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CFlash::RestoreState(FILE* f)
{
	u32 magic = 0, size = 0, mode = 0;
	if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != flash_snapshot_magic1 ||
		fread(&size, sizeof(size), 1, f) != 1 || size != sizeof(state.Flash) ||
		fread(&mode, sizeof(mode), 1, f) != 1 || mode > MODE_CONFIRM_1)
		return -1;
	std::unique_ptr<void, decltype(&free)> restored(malloc(sizeof(state.Flash)), &free);
	if (!restored || fread(restored.get(), 1, sizeof(state.Flash), f) != sizeof(state.Flash) ||
		fread(&magic, sizeof(magic), 1, f) != 1 || magic != flash_snapshot_magic2)
		return -1;
	memcpy(state.Flash, restored.get(), sizeof(state.Flash));
	state.mode = (int)mode;
	// It may still fail a restore at a later component, so let's not write, even if save state was dirty.
	dirty = false;
	return 0;
}

void CFlash::finalize_restore() noexcept
{
	dirty = true;
	last_dirty = time(nullptr);
}

CFlash* theSROM = 0;
