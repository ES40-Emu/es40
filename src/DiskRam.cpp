/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by Cacodemon345
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
  * Contains code to use a RAM disk.
  *
  * $Id$
  *
  * X-1.17       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.16       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.15       Camiel Vanderhoeven                             02-MAR-2008
  *      Natural way to specify large numeric values ("10M") in the config file.
  *
  * X-1.14       Camiel Vanderhoeven                             13-JAN-2008
  *      Use determine_layout in stead of calc_cylinders.
  *
  * X-1.12       Camiel Vanderhoeven                             09-JAN-2008
  *      Save disk state to state file.
  *
  * X-1.11       Camiel Vanderhoeven                             06-JAN-2008
  *      Set default blocksize to 2048 for cd-rom devices.
  *
  * X-1.10       Camiel Vanderhoeven                             06-JAN-2008
  *      Support changing the block size (required for SCSI, ATAPI).
  *
  * X-1.9        Camiel Vanderhoeven                             04-JAN-2008
  *      64-bit file I/O.
  *
  * X-1.8        Camiel Vanderhoeven                             02-JAN-2008
  *      Cleanup.
  *
  * X-1.7        Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.6        Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.5        Camiel Vanderhoeven                             18-DEC-2007
  *      Byte-sized transfers for SCSI controller.
  *
  * X-1.4        Brian wheeler                                   17-DEC-2007
  *      Changed last cylinder number.
  *
  * X-1.3        Brian Wheeler                                   16-DEC-2007
  *      Fixed case of StdAfx.h.
  *
  * X-1.2        Camiel Vanderhoeven                             14-DEC-2007
  *      Free memory malloc'ed at destruction.
  *
  * X-1.1        Camiel Vanderhoeven                             12-DEC-2007
  *      Initial version in CVS.
  **/
#include "StdAfx.h"
#include "DiskRam.h"
#include <memory>

static const u32 ramdisk_magic1 = 0x52414d31;
static const u32 ramdisk_magic2 = 0x52414d32;

CDiskRam::CDiskRam(CConfigurator* cfg, CSystem* sys, CDiskController* c,
	int idebus, int idedev) : CDisk(cfg, sys, c, idebus, idedev)
{
	byte_size = myCfg->get_num_value("size", false, 512 * 1024 * 1024);

	CHECK_ALLOCATION(ramdisk = calloc((size_t)byte_size, 1));
	auto filename = myCfg->get_text_value("file");
	model_number = nullptr;
	if (filename)
	{
		FILE* handle = NULL;
		if (read_only)
			handle = fopen(filename, "rb");
		else
			handle = fopen_large(filename, "rb+");
		if (handle)
		{
			fseek_large(handle, 0, SEEK_END);
			auto file_byte_size = ftell_large(handle);
			fseek_large(handle, 0, SEEK_SET);
			auto original_ramdisk = ramdisk;
			CHECK_REALLOCATION(ramdisk, realloc(ramdisk, (size_t)file_byte_size), char);
			byte_size = file_byte_size;
			auto r = fread(ramdisk, 1, (size_t)file_byte_size, handle);
			if (r != (size_t)file_byte_size)
				FAILURE_1(IO, "%s: unexpected end of file!\n", filename);
			fclose(handle);

			model_number = myCfg->get_text_value("model_number", filename);

			// skip to the filename portion of the path.
			char* p = model_number;
#if defined(_WIN32)
			char    x = '\\';
#elif defined(__VMS)
			char    x = ']';
#else
			char    x = '/';
#endif
			while (*p)
			{
				if (*p == x)
					model_number = p + 1;
				p++;
			}
		}
	}

	state.byte_pos = 0;

	sectors = 32;
	heads = 8;

	//calc_cylinders();
	determine_layout();

	if (!model_number)
		model_number = myCfg->get_text_value("model_number", "ES40RAMDISK");

	printf("%s: Mounted RAMDISK, %" PRId64 " %zu-byte blocks, %" PRId64 "/%ld/%ld.\n",
		devid_string, byte_size / state.block_size, state.block_size,
		cylinders, heads, sectors);
}

CDiskRam::~CDiskRam(void)
{
	if (ramdisk)
	{
		printf("%s: RAMDISK freed.\n", devid_string);
		free(ramdisk);
		ramdisk = 0;
	}
}

std::string CDiskRam::snapshot_identity() const
{
	return CSystemComponent::snapshot_identity() + "|ram-bytes=" +
		std::to_string(byte_size) + "|read-only=" + (read_only ? "1" : "0") +
		"|cdrom=" + (is_cdrom ? "1" : "0");
}

int CDiskRam::SaveState(FILE* f)
{
	if (byte_size < 0 || (u64)byte_size > SIZE_MAX || (byte_size && !ramdisk))
		return -1;
	const u64 size = (u64)byte_size;
	// payload before record so restorestate can verify first
	if (fwrite(&ramdisk_magic1, sizeof(ramdisk_magic1), 1, f) != 1 ||
		fwrite(&size, sizeof(size), 1, f) != 1 ||
		(size && fwrite(ramdisk, 1, (size_t)size, f) != (size_t)size) ||
		fwrite(&ramdisk_magic2, sizeof(ramdisk_magic2), 1, f) != 1)
		return -1;
	return CDisk::SaveState(f);
}

int CDiskRam::RestoreState(FILE* f)
{
	u32 magic = 0;
	u64 size = 0;
	if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != ramdisk_magic1 ||
		fread(&size, sizeof(size), 1, f) != 1 || byte_size < 0 ||
		size != (u64)byte_size || size > SIZE_MAX || (size && !ramdisk))
		return -1;

	// the file cannot enlarge the live allocation. 
	// if either this payload or the following disk protocol record is invalid.
	std::unique_ptr<void, decltype(&free)> restored(size ? malloc((size_t)size) : nullptr, &free);
	if (size && (!restored || fread(restored.get(), 1, (size_t)size, f) != (size_t)size))
		return -1;
	if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != ramdisk_magic2 ||
		CDisk::RestoreState(f))
		return -1;
	if (size)
		memcpy(ramdisk, restored.get(), (size_t)size);
	return 0;
}

bool CDiskRam::seek_byte(off_t_large byte)
{
	if (byte < 0 || byte >= byte_size)
	{
		FAILURE_1(InvalidArgument, "%s: Seek outside disk image!\n", devid_string);
	}

	state.byte_pos = byte;
	return true;
}

size_t CDiskRam::read_bytes(void* dest, size_t bytes)
{
	if (state.byte_pos >= byte_size)
		return 0;

	off_t_large available = byte_size - state.byte_pos;
	if ((off_t_large)bytes > available)
		bytes = (size_t)available;

	memcpy(dest, &(((char*)ramdisk)[state.byte_pos]), bytes);
	state.byte_pos += (unsigned long)bytes;
	return bytes;
}

size_t CDiskRam::write_bytes(void* src, size_t bytes)
{
	if (state.byte_pos >= byte_size)
		return 0;

	off_t_large available = byte_size - state.byte_pos;
	if ((off_t_large)bytes > available)
		bytes = (size_t)available;

	memcpy(&(((char*)ramdisk)[state.byte_pos]), src, bytes);
	state.byte_pos += (unsigned long)bytes;
	return bytes;
}
