/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2019 by Ingo Albrecht
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2026 by BlueRain-debug
 * Copyright (C) 2026 by WNT50
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
  * Contains the code for the emulated Floppy Controller devices.
  *
  * $Id$
  *
  * X-1.16       Camiel Vanderhoeven                             29-APR-2008
  *      Make floppy disk use CDisk images.
  *
  * X-1.15       Brian Wheeler                                   29-APR-2008
  *      Fixed floppy disk implementation.
  *
  * X-1.14       Brian Wheeler                                   29-APR-2008
  *      Floppy disk implementation.
  *
  * X-1.13       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.12       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.11       Camiel Vanderhoeven                             30-DEC-2007
  *      Print file id on initialization.
  *
  * X-1.10       Camiel Vanderhoeven                             11-DEC-2007
  *      Don't claim IO port 3f6 as this is in use by the IDE controller.
  *
  * X-1.9        Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.8        Camiel Vanderhoeven                             31-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.7	Brian Wheeler					13-FEB-2007
  *	Formatting.
  *
  * X-1.6 	Camiel Vanderhoeven				12-FEB-2007
  *	Added comments.
  *
  * X-1.5        Camiel Vanderhoeven                             9-FEB-2007
  *      Added comments.
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
#include "chipset/AliM1543C.h"
#include "FloppyController.h"
#include "system/System.h"
#include "chipset/DMA.h"
#include "Disk.h"

#if defined(DEBUG_FDC)
#define FDC_DEBUG(...) printf(__VA_ARGS__)
#else
#define FDC_DEBUG(...) ((void)0)
#endif

  /**
   * Constructor.
   **/
CFloppyController::CFloppyController(CConfigurator* cfg, CSystem* c, int id) :
	CSystemComponent(cfg, c), CDiskController(1, 2),
	isa_io_base(0x3f0u - 0x80u * (u32)id)
{
	c->RegisterMemory(this, 1536, U64(0x00000801fc0003f0) - (0x80 * id), 6);
	c->RegisterMemory(this, 1537, U64(0x00000801fc0003f7) - (0x80 * id), 1);

	memset(&state, 0, sizeof(state));
	state.status.rqm = true;
	state.dma = true;
	state.dor = 0x0C;

	printf("%s: $Id$\n",
		devid_string);
}

/**
 * Destructor.
 **/
CFloppyController::~CFloppyController()
{
}

const CSystemComponent* CFloppyController::memory_decode_owner() const noexcept
{
	return isa_bridge ? static_cast<const CSystemComponent*>(isa_bridge) : this;
}

bool CFloppyController::decodes_memory_access(int index, u64 address, int, bool write) const noexcept
{
	if (!((index == 1536 && address < 6) || (index == 1537 && address == 0)))
		return false;
	if (!isa_bridge)
		return true;
	const u32 port = isa_io_base + (index == 1537 ? 7u : 0u) + (u32)address;
	if (isa_bridge->superio_claims_port(port, write))
		return false;
	return isa_bridge->isa_io_decode(port) != CAliM1543C::IsaIoDecode::None;
}

bool CFloppyController::uses_subtractive_decode(int index, u64 address, int dsize,
	bool write) const noexcept
{
	if (!isa_bridge || !decodes_memory_access(index, address, dsize, write))
		return false;
	const u32 port = isa_io_base + (index == 1537 ? 7u : 0u) + (u32)address;
	return isa_bridge->isa_io_decode(port) == CAliM1543C::IsaIoDecode::Subtractive;
}

void CFloppyController::service_pending_media_actions_if_idle()
{
	if (state.pio.active || state.cmd_parms_ptr != 0)
		return;

	// DMA requests complete inside command dispatch, and the controller mutex
	// excludes that path here. A pending result phase no longer accesses the
	// image and therefore does not need to delay an operator media change.
	for (int drive = 0; drive < 2; drive++)
		if (FDISK(drive) != NULL)
		{
			try
			{
				FDISK(drive)->service_pending_media_actions();
			}
			catch (...)
			{
				printf("FDC: Could not service a pending media action for drive %d.\n",
					drive);
			}
		}
}

void CFloppyController::check_state()
{
	// Drives are registered before emulation starts. Only the atomic mailbox
	// flag is inspected here; controller state still requires both locks below.
	bool pending = false;
	for (int drive = 0; drive < 2; drive++)
		if (FDISK(drive) != NULL && FDISK(drive)->has_pending_media_actions())
		{
			pending = true;
			break;
		}
	if (!pending)
		return;

	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	std::lock_guard<std::recursive_mutex> lock(controller_mutex);
	service_pending_media_actions_if_idle();
}


std::string datarate_name[] = {
	"500 Kb/S MFM",
	"300 Kb/S MFM",
	"250 Kb/S MFM",
	"1 Mb/S MFM"
};

struct cmdinfo_t {
	u8 command;
	u8 parms;
	u8 returns;
	std::string name;
}
cmdinfo[] = {
  { 0, 0, 0, ""},
  { 0, 0, 0, ""},
  { 2, 9, 7, "Read Track"},
  { 3, 3, 0, "Specify"},
  { 4, 2, 1, "Sense Drive Status"},
  { 5, 9, 7, "Write Data"},
  { 6, 9, 7, "Read Data"},
  { 7, 2, 0, "Recalibrate"},
  { 8, 1, 2, "Sense Interrupt Status"},
  { 9, 9, 7, "Write Deleted Data"},
  {10, 2, 7, "Read ID"},
  {11, 0, 0, ""},
  {12, 9, 7, "Read Deleted"},
  {13, 6, 7, "Format Track"},
  {14, 1, 10, "DumpReg"},
  {15, 3, 0, "Seek"},
  {16, 1, 1, "Version"},
  {17, 9, 7, "Scan Equal"},
  {18, 2, 0, "Perpendicular Mode"},
  {19, 4, 0, "Configure"},
  {20, 1, 1, "Lock"},
  {21, 0, 0, ""},
  {22, 9, 7, "Verify"},
  {23, 0, 0, ""},
  {24, 0, 0, ""},
  {25, 9, 7, "Scan Low or Equal"},
  {26, 0, 0, ""},
  {27, 0, 0, ""},
  {28, 0, 0, ""},
  {29, 9, 7, "Scan High or Equal"},
  {30, 0, 0, ""},
  {31, 0, 0, ""},
};

void CFloppyController::reset_controller(bool raise_irq)
{
	memset(&state.pio, 0, sizeof(state.pio));
	memset(state.cmd_parms, 0, sizeof(state.cmd_parms));
	memset(state.cmd_res, 0, sizeof(state.cmd_res));
	state.cmd_parms_ptr = 0;
	state.cmd_res_ptr = 0;
	state.cmd_res_max = 0;
	state.status.rqm = true;
	state.status.dio = false;
	state.status.nondma = false;
	state.status.busy = false;
	state.status.seeking[0] = false;
	state.status.seeking[1] = false;
	state.drive[0].seeking = 0;
	state.drive[1].seeking = 0;
	state.dma = true;
	state.reset_sense_cnt = raise_irq ? 4 : 0;
	clear_interrupt();
	if (raise_irq)
		do_interrupt();
}

bool CFloppyController::get_geometry(int drive, SFloppyGeometry* geometry)
{
	struct SGeometryEntry {
		int cylinders;
		int heads;
		int sectors;
		u8 data_rate;
		off_t_large byte_size;
	};

	static const SGeometryEntry geometries[] = {
		{ 40, 1,  8, 2, 40 * 1 *  8 * 512 }, // 160K,  250 Kb/s
		{ 40, 1,  9, 2, 40 * 1 *  9 * 512 }, // 180K,  250 Kb/s
		{ 40, 2,  8, 2, 40 * 2 *  8 * 512 }, // 320K,  250 Kb/s
		{ 40, 2,  9, 2, 40 * 2 *  9 * 512 }, // 360K,  250 Kb/s
		{ 40, 2, 10, 2, 40 * 2 * 10 * 512 }, // 400K,  250 Kb/s
		{ 80, 2,  8, 2, 80 * 2 *  8 * 512 }, // 640K,  250 Kb/s
		{ 80, 2,  9, 2, 80 * 2 *  9 * 512 }, // 720K,  250 Kb/s
		{ 80, 2, 10, 2, 80 * 2 * 10 * 512 }, // 800K,  250 Kb/s
		{ 80, 2, 15, 0, 80 * 2 * 15 * 512 }, // 1.2M,  500 Kb/s
		{ 80, 2, 18, 0, 80 * 2 * 18 * 512 }, // 1.44M, 500 Kb/s
		{ 80, 2, 21, 0, 80 * 2 * 21 * 512 }, // 1.68M, 500 Kb/s (DMF)
		{ 80, 2, 36, 3, 80 * 2 * 36 * 512 }, // 2.88M, 1 Mb/s
	};

	CDisk* disk = FDISK(drive);
	if (disk == NULL || !disk->media_present() || geometry == NULL)
		return false;

	off_t_large media_size = disk->get_byte_size();
	const int geometry_count = (int)(sizeof(geometries) / sizeof(geometries[0]));
	int best = 0;
	off_t_large best_delta = media_size >= geometries[0].byte_size ?
		media_size - geometries[0].byte_size : geometries[0].byte_size - media_size;

	for (int i = 1; i < geometry_count; i++) {
		off_t_large delta = media_size >= geometries[i].byte_size ?
			media_size - geometries[i].byte_size : geometries[i].byte_size - media_size;
		if (delta < best_delta) {
			best = i;
			best_delta = delta;
		}
	}

	// Raw images have no track metadata.  Exact listed sizes win first.  
	// Otherwise, prefer a 40/80-cylinder track layout that packs the 
	// into complete cylinders, the cylinder count itself need not be
	// standard because real drives can step beyond tracks 40/80 
	// if you're lucky.
	bool exact_nominal = false;
	int extended_cylinders = 0;
	for (int i = 0; i < geometry_count; i++) {
		if (media_size == geometries[i].byte_size) {
			best = i;
			exact_nominal = true;
			break;
		}
	}

	if (!exact_nominal) {
		int extended_best = -1;
		int least_extra = 0;
		for (int i = 0; i < geometry_count; i++) {
			off_t_large cylinder_size =
				(off_t_large)geometries[i].heads * geometries[i].sectors * 512;
			if (media_size % cylinder_size != 0)
				continue;

			off_t_large cylinders = media_size / cylinder_size;
			if (cylinders <= geometries[i].cylinders || cylinders > 256)
				continue;

			int extra = (int)cylinders - geometries[i].cylinders;
			if (extended_best < 0 || extra < least_extra) {
				extended_best = i;
				extended_cylinders = (int)cylinders;
				least_extra = extra;
			}
		}

		if (extended_best >= 0)
			best = extended_best;
	}

	geometry->heads = geometries[best].heads;
	geometry->sectors = geometries[best].sectors;
	geometry->data_rate = geometries[best].data_rate;
	off_t_large cylinder_size =
		(off_t_large)geometry->heads * geometry->sectors * 512;
	off_t_large cylinders = extended_cylinders > 0 ? extended_cylinders :
		geometries[best].cylinders;
	if (cylinders < 1)
		cylinders = 1;
	if (cylinders > 256)
		cylinders = 256;
	geometry->cylinders = (int)cylinders;
	geometry->byte_size = cylinders * cylinder_size;
	return true;
}

void CFloppyController::prepare_rw_result(int drive, int head, int eot,
	const SFloppyGeometry& geometry, bool multi_track,
	bool result_is_next, size_t count)
{
	int sectors = (int)(count / 512);
	bool ended_in_partial_sector = (count % 512) != 0;
	int address_advances = sectors;
	if (!result_is_next && !ended_in_partial_sector && address_advances > 0)
		address_advances--;
	int last_head = head;

	for (int i = 0; i < address_advances; i++) {
		last_head = state.cmd_parms[3];
		state.cmd_parms[4]++;
		if (state.cmd_parms[4] > eot) {
			state.cmd_parms[4] = 1;
			if (multi_track) {
				state.cmd_parms[3]++;
				if (state.cmd_parms[3] >= geometry.heads) {
					state.cmd_parms[3] = 0;
					state.cmd_parms[2]++;
				}
			} else {
				// Table 5-6: MT=0 advances C but leaves H unchanged.
				state.cmd_parms[2]++;
			}
		}
	}
	if (!result_is_next || ended_in_partial_sector)
		last_head = state.cmd_parms[3];

	state.cmd_res[0] = drive | (last_head << 2);
	state.cmd_res[1] = 0;
	state.cmd_res[2] = 0;
	state.cmd_res[3] = state.cmd_parms[2];
	state.cmd_res[4] = state.cmd_parms[3];
	state.cmd_res[5] = state.cmd_parms[4];
	state.cmd_res[6] = state.cmd_parms[5];
	state.drive[drive].seeking = 1;
}

bool CFloppyController::format_track(int drive, int head, u8 sector_size,
	u8 sector_count, u8 fill, const u8* sector_ids, size_t id_bytes)
{
	u8 result_cylinder = drive >= 0 && drive < 2 ?
		(u8)state.drive[drive].cylinder : 0;
	auto set_result = [&](u8 st0, u8 st1, const u8* id) {
		state.cmd_res[0] = st0;
		state.cmd_res[1] = st1;
		state.cmd_res[2] = 0;
		state.cmd_res[3] = id ? id[0] : result_cylinder;
		state.cmd_res[4] = id ? id[1] : (u8)head;
		state.cmd_res[5] = id ? id[2] : 1;
		state.cmd_res[6] = id ? id[3] : sector_size;
	};

	CDisk* disk = drive >= 0 && drive < 2 ? FDISK(drive) : NULL;
	if (disk == NULL || !disk->is_ready()) {
		set_result(0x40 | ST0_NR | (head << 2) | drive, 0, NULL);
		return false;
	}
	if (disk->ro()) {
		set_result(0x40 | (head << 2) | drive, ST1_WP, NULL);
		return false;
	}

	SFloppyGeometry geometry;
	if (!get_geometry(drive, &geometry) || sector_size != 2 ||
		sector_count == 0 || sector_count > geometry.sectors ||
		sector_ids == NULL ||
		id_bytes < (size_t)sector_count * 4 ||
		state.drive[drive].cylinder < 0 ||
		state.drive[drive].cylinder >= geometry.cylinders ||
		head < 0 || head >= geometry.heads) {
		set_result(0x40 | (head << 2) | drive, ST1_ND, NULL);
		return false;
	}

	u8 sector_data[512];
	memset(sector_data, fill, sizeof(sector_data));
	const u8* last_id = NULL;
	for (unsigned i = 0; i < sector_count; i++) {
		const u8* id = sector_ids + i * 4;
		last_id = id;

		// Raw images cannot preserve deliberately unusual C/H ID fields or
		// physical interleave.  R still identifies the logical sector to fill;
		// C/H are retained in the result packet, matching normal formatters.
		if (id[2] < 1 || id[2] > geometry.sectors || id[3] != 2) {
			set_result(0x40 | (head << 2) | drive, ST1_ND, id);
			return false;
		}

		off_t_large sector_index =
			((off_t_large)state.drive[drive].cylinder * geometry.heads + head) *
			geometry.sectors + id[2] - 1;
		off_t_large offset = sector_index * 512;
		if (offset < 0 || offset > geometry.byte_size - 512 ||
			offset > disk->get_byte_size() - 512 ||
			!disk->seek_byte(offset) ||
			disk->write_bytes(sector_data, sizeof(sector_data)) != sizeof(sector_data)) {
			const bool not_ready = !disk->is_ready();
			set_result(0x40 | (not_ready ? ST0_NR : 0) | (head << 2) | drive,
				not_ready ? 0 : ST1_ND, id);
			return false;
		}
	}

	disk->flush();
	set_result((head << 2) | drive, 0, last_id);
	return true;
}

void CFloppyController::finish_pio_transfer(bool ok)
{
	CDisk* disk = FDISK(state.pio.drive);
	bool was_write = state.pio.write;
	bool was_format = state.pio.format;
	u32 transferred = state.pio.pos;
	u32 requested = state.pio.size;
	bool not_ready = disk == NULL || !disk->is_ready();
	if (not_ready) {
		ok = false;
		if (was_format) {
			state.cmd_res[0] = state.pio.drive | (state.pio.head << 2);
			state.cmd_res[3] = state.drive[state.pio.drive].cylinder;
			state.cmd_res[4] = state.pio.head;
			state.cmd_res[5] = 1;
			state.cmd_res[6] = state.pio.format_n;
		}
	}

	if (ok && was_format) {
		ok = format_track(state.pio.drive, state.pio.head,
			state.pio.format_n, state.pio.format_sc, state.pio.format_fill,
			state.pio.data, state.pio.size);
	} else if (ok && was_write) {
		u32 first_size = state.pio.first_size <= state.pio.size ?
			state.pio.first_size : state.pio.size;
		u32 second_size = state.pio.size - first_size;
		ok = disk != NULL && first_size > 0 && disk->seek_byte(state.pio.offset) &&
			disk->write_bytes(state.pio.data, first_size) == first_size;
		if (ok && second_size > 0) {
			ok = disk->seek_byte(state.pio.second_offset) &&
				disk->write_bytes(state.pio.data + first_size, second_size) == second_size;
		}
		if (ok)
			disk->flush();
	}
	not_ready = disk == NULL || !disk->is_ready();
	if (not_ready)
		ok = false;

	state.pio.active = false;
	state.pio.write = false;
	state.pio.format = false;
	state.pio.format_n = 0;
	state.pio.format_sc = 0;
	state.pio.format_fill = 0;
	state.pio.offset = 0;
	state.pio.second_offset = 0;
	state.pio.size = 0;
	state.pio.first_size = 0;
	state.pio.pos = 0;
	state.status.nondma = false;
	state.status.rqm = true;
	state.status.dio = true;
	state.cmd_res_ptr = 0;
	state.cmd_res_max = 7;

	if (not_ready) {
		state.cmd_res[0] = (state.cmd_res[0] & (ST0_DS | ST0_HA)) | 0x40 | ST0_NR;
		state.cmd_res[1] = 0;
		state.cmd_res[2] = 0;
	} else if (!ok && !was_format) {
		state.cmd_res[0] = (state.cmd_res[0] & (ST0_DS | ST0_HA)) | 0x40;
		state.cmd_res[1] = ST1_ND;
		state.cmd_res[2] = 0;
	}

	FDC_DEBUG("FDC [PIO]: %s %s (%u/%u bytes)\n",
		was_format ? "format" : (was_write ? "write" : "read"),
		ok ? "complete" : "failed",
		transferred, requested);

	clear_interrupt();
	do_interrupt();
}





void CFloppyController::WriteMem(int index, u64 address, int dsize, u64 data)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	std::lock_guard<std::recursive_mutex> lock(controller_mutex);

	if (index == 1537)
		address += 7;

	//printf("FDC: Write port %d, value: %x\n", address, data);

	switch (address)
	{
	case FDC_REG_STATUS_A:
	case FDC_REG_STATUS_B:
		FDC_DEBUG("FDC: Read only register %" PRId64 " written.\n", address);
		break;

	case FDC_REG_DOR:
	{
		u8 old_dor = state.dor;
		state.dor = (u8)data;
		// bit 4 = drive 0 motor, bit 5 = drive 1 motor
		// bit 3 = IRQ/DMA output gate (PS/2 reserved?)
		// bit 2 = 1: fdc enable (reset), 0: hold at reset
		// bits 1-0:  drive select 0: a, 1: b, I assume 2 & 3 are reserved.

		state.drive[0].motor = (data & 0x10) >> 4;
		state.drive[1].motor = (data & 0x20) >> 5;
		state.drive_select = data & 0x03;

		FDC_DEBUG("FDC:  motor a: %s, motor b: %s, dma/irq gate: %s, drive: %s\n",
			state.drive[0].motor ? "on" : "off",
			state.drive[1].motor ? "on" : "off",
			(data & 0x08) ? "on" : "off",
			state.drive_select == 0 ? "A" : "B");

		if ((data & 0x04) == 0) {
			reset_controller(false);
		} else if ((old_dor & 0x04) == 0) {
			reset_controller(true);
		} else if (((old_dor ^ data) & 0x08) && state.interrupt && theAli) {
			// DOR bit 3 gates the external IRQ pin; it does not select the
			// transfer mode, which comes from SPECIFY's ND bit.
			if (data & 0x08)
				theAli->pic_interrupt(0, 6);
			else
				theAli->pic_deassert(0, 6);
		}
		break;
	}
	case FDC_REG_TAPE:
		FDC_DEBUG("FDC: Tape register written with %" PRIx64 "\n", data);
		break;

	case FDC_REG_STATUS:  // write = data rate selector
		// bit 7 = software reset (self clearing)
		// bit 6 = power down
		// bit 5 = reserved (0)
		// bit 4-2 = write precomp (000 = default)
		// bit 1-0 = data rate select

		state.datarate = data & 0x03;
		state.write_precomp = (data & 0x1c) >> 2;
		FDC_DEBUG("FDC: data rate %s, precomp: %d\n", datarate_name[state.datarate].c_str(), state.write_precomp);

		if (data & 0x80)
			reset_controller(true);
		break;

	case FDC_REG_COMMAND:
		// the excitement happens here.
		if (dsize > 8) {
			int bytes = dsize / 8;
			if (bytes > 8)
				bytes = 8;
			for (int i = 0; i < bytes; i++)
				WriteMem(index, address, 8, (data >> (i * 8)) & 0xff);
			break;
		}

		service_pending_media_actions_if_idle();

		if (state.pio.active) {
			CDisk* disk = FDISK(state.pio.drive);
			if (!disk || !disk->is_ready()) {
				finish_pio_transfer(false);
				break;
			}
			if (!state.pio.write) {
				FDC_DEBUG("FDC: write to data register during non-DMA read phase.\n");
				break;
			}

			state.pio.data[state.pio.pos++] = (u8)data;
			// A non-DMA execution request is acknowledged by each FIFO access.
			// The next ready byte generates another request; the last byte instead
			// transitions to the independently-signalled result phase.
			state.status.rqm = false;
			clear_interrupt();
			if (state.pio.pos >= state.pio.size) {
				finish_pio_transfer(true);
			} else {
				state.status.rqm = true;
				do_interrupt();
			}
			break;
		}

		if (state.status.dio) {
			FDC_DEBUG("Unrequested data byte to command port.  Throwing away.\n");
			break;
		}
		else
		{
			state.cmd_parms[state.cmd_parms_ptr++] = data;
			int cmd = state.cmd_parms[0] & 0x1F;
			state.cmd_res_max = cmdinfo[cmd].returns;
			//printf("FDC: parm_ptr: %d, parms: %d\n", state.cmd_parms_ptr, cmdinfo[cmd].parms);
			if (cmdinfo[cmd].parms == 0 ||
				state.cmd_parms_ptr == cmdinfo[cmd].parms)
			{
				FDC_DEBUG("FDC: command %s(", cmdinfo[cmd].name.c_str());
				for (int i = 1; i < state.cmd_parms_ptr; i++)
				{
					FDC_DEBUG("%x ", state.cmd_parms[i]);
				}
				FDC_DEBUG(")\n");

				state.cmd_res_max = cmdinfo[cmd].returns;
				state.cmd_res_ptr = 0;
				state.status.rqm = 0;
				switch (cmd) {
				case 3: // specify
					// set up some hardware parameters.  We really don't care about
					// the times (step rate time, head unload time, head load time}, but
					// the ND bit selects the execution data path.
					state.dma = (state.cmd_parms[2] & 0x01) == 0;
					break;

				case 4: // Sense Drive Status
				{
					int drive_idx = state.cmd_parms[1] & 3;
					int head = (state.cmd_parms[1] >> 2) & 1;
					CDisk* disk = drive_idx < 2 ? FDISK(drive_idx) : NULL;
					// The M1543C defines ST3 bit 5 as fixed one and bit 3 as
					// fixed zero.  OpenVMS treats bit 5 as the legacy READY bit;
					// returning it clear makes an otherwise present disk appear
					// offline.
					u8 st3 = 0x20;
					if (disk && disk->is_offline())
						st3 &= ~0x20; // Inject a legacy not-ready drive; keep reserved bits clear.
					if (drive_idx < 2 && state.drive[drive_idx].cylinder == 0)
						st3 |= 0x10; // Track 0
					st3 |= (head << 2);
					st3 |= drive_idx;
					if (disk && disk->media_present() && disk->ro()) st3 |= 0x40;
					state.cmd_res[0] = st3;
					break;
				}

				case 5: // write data
				case 6: // read data
					// args:
					// 0: bit 7 = MT (multitrack), 6 = MFM, 5 = SK (skip flag)
					// 1: bit 2 = HDS (head), 1 = DS1, 0 = DS0 
					// 2: C = cyl
					// 3: H = head address
					// 4: R = sector
					// 5: N = sector size, 2 = 512b
					// 6: EOT = last sector number on the track (18 for 1.44MB)
					// 7: GPL = gap length 
					// 8: DTL = sector size (if N = 0)
				{
					int drive_idx = state.cmd_parms[1] & 0x03;
					int head = (state.cmd_parms[1] >> 2) & 1;
					CDisk* disk = FDISK(drive_idx);
					auto fail_rw = [&](u8 st1) {
						state.cmd_res[0] = 0x40 | (head << 2) | drive_idx;
						state.cmd_res[1] = st1;
						state.cmd_res[2] = 0;
						state.cmd_res[3] = state.cmd_parms[2];
						state.cmd_res[4] = state.cmd_parms[3];
						state.cmd_res[5] = state.cmd_parms[4];
						state.cmd_res[6] = state.cmd_parms[5];
						do_interrupt();
					};

					// The FDC and a physical drive can exist without inserted media.
					// A data command to an absent drive or empty drive terminates
					// abnormally instead of trying to access a backing image.
					// ST0 IC=01 (abnormal termination) with the Not Ready bit set.
					if (disk == NULL || !disk->is_ready()) {
						FDC_DEBUG("FDC [CMD %02x]: drive %d not ready - aborting\n", cmd, drive_idx);
						state.cmd_res[0] = 0x40 | ST0_NR | (head << 2) | drive_idx; // ST0
						state.cmd_res[1] = 0;                   // ST1
						state.cmd_res[2] = 0;                   // ST2
						state.cmd_res[3] = state.cmd_parms[2];  // C (cylinder)
						state.cmd_res[4] = state.cmd_parms[3];  // H (head)
						state.cmd_res[5] = state.cmd_parms[4];  // R (sector)
						state.cmd_res[6] = state.cmd_parms[5];  // N (sector size)
						do_interrupt();
						break;  // -> switch(cmd) tail arms the result phase (rqm=1, dio=1)
					}

					int cyl = state.cmd_parms[2];
					int sector = state.cmd_parms[4];
					int eot = state.cmd_parms[6];
					SFloppyGeometry geometry;
					get_geometry(drive_idx, &geometry);
					off_t_large media_size = disk->get_byte_size();
					if (state.datarate != geometry.data_rate) {
						FDC_DEBUG("FDC [%s]: data-rate mismatch: controller=%s, media=%s\n",
							state.dma ? "DMA" : "PIO",
							datarate_name[state.datarate].c_str(),
							datarate_name[geometry.data_rate].c_str());
						// At the wrong clock rate no valid ID address mark can be
						// decoded from the medium. Density probes depend on this
						// failure before trying the next supported data rate.
						fail_rw(ST1_MAM);
						break;
					}
					if (eot == 0)
						eot = geometry.sectors;

					int pos =
						(cyl * geometry.heads + head) * geometry.sectors + sector - 1;

					bool mt = (state.cmd_parms[0] & 0x80) ? true : false;
					int sectors_to_read = 0;
					if (mt && head == 0 && geometry.heads > 1) {
						sectors_to_read = (eot - sector + 1) + eot;
					} else {
						sectors_to_read = eot - sector + 1;
					}
					if (sectors_to_read <= 0) sectors_to_read = 1;

					size_t fdc_count = sectors_to_read * 512;
					size_t count;
					if (state.dma) {
						if (!isa_dma) {
							fail_rw(ST1_OR);
							break;
						}
						size_t dma_count = isa_dma->get_count(2) + 1;
						count = (fdc_count < dma_count) ? fdc_count : dma_count;
					} else {
						count = fdc_count;
					}

					FDC_DEBUG("FDC [CMD %02x]: CHS=(%d/%d/%d) EOT=%d MT=%d. "
						"Drive=%d geometry=%d/%d/%d media=%" PRId64 "\n",
						cmd, cyl, head, sector, eot, mt, drive_idx,
						geometry.cylinders, geometry.heads, geometry.sectors, media_size);
					FDC_DEBUG("FDC [%s]: Transfer size requested = %zu bytes (%zu sectors)\n",
						state.dma ? "DMA" : "PIO", count, count/512);

					bool sector_valid = sector >= 1 && sector <= geometry.sectors &&
						eot >= sector && eot <= geometry.sectors;

					off_t_large byte_offset = (off_t_large)pos * 512;
					size_t first_count = count;
					off_t_large second_offset = 0;
					if (sector_valid && mt && head == 0 &&
						geometry.heads > 1 && eot < geometry.sectors) {
						size_t first_track_count = (size_t)(eot - sector + 1) * 512;
						if (first_track_count < count) {
							first_count = first_track_count;
							second_offset = (off_t_large)(cyl * geometry.heads + 1) *
								geometry.sectors * 512;
						}
					}
					size_t second_count = count - first_count;
					auto range_fits = [&](off_t_large offset, size_t length) {
						return offset >= 0 && offset <= geometry.byte_size &&
							offset <= media_size &&
							(off_t_large)length <= geometry.byte_size - offset &&
							(off_t_large)length <= media_size - offset;
					};
					bool range_valid = range_fits(byte_offset, first_count) &&
						(second_count == 0 || range_fits(second_offset, second_count));
					if (state.cmd_parms[5] != 2 || !sector_valid ||
						cyl >= geometry.cylinders || head >= geometry.heads ||
						state.cmd_parms[3] != head || !range_valid ||
						(!state.dma && count > sizeof(state.pio.data))) {
						FDC_DEBUG("FDC [%s]: unsupported request C/H/R/N/EOT="
							"%d/%d/%d/%d/%d, media=%" PRId64 " bytes\n",
							state.dma ? "DMA" : "PIO", cyl, head, sector,
							state.cmd_parms[5], eot, media_size);
						fail_rw(ST1_ND);
						break;
					}

					if (cmd == 5 && disk->ro()) {
						fail_rw(ST1_WP);
						break;
					}

					if (!state.dma) {
						// With no TC input, reaching EOT is the controller's implicit
						// terminator.  The M1543 reports abnormal termination/End of
						// Track and advances result CHRN to the next logical sector.
						prepare_rw_result(drive_idx, head, eot, geometry,
							mt, true, count);
						state.cmd_res[0] =
							(state.cmd_res[0] & (ST0_DS | ST0_HA)) | 0x40;
						state.cmd_res[1] = ST1_EOC;
						state.pio.active = true;
						state.pio.write = cmd == 5;
						state.pio.format = false;
						state.pio.drive = (u8)drive_idx;
						state.pio.head = (u8)head;
						state.pio.offset = byte_offset;
						state.pio.second_offset = second_offset;
						state.pio.size = (u32)count;
						state.pio.first_size = (u32)first_count;
						state.pio.pos = 0;
						state.cmd_res_ptr = 0;
						state.cmd_res_max = 0;
						state.status.rqm = true;
						state.status.dio = !state.pio.write;
						state.status.nondma = true;

						if (!state.pio.write) {
							memset(state.pio.data, 0, count);
							bool ok = disk->seek_byte(state.pio.offset) &&
								disk->read_bytes(state.pio.data, first_count) == first_count;
							if (ok && second_count > 0) {
								ok = disk->seek_byte(state.pio.second_offset) &&
									disk->read_bytes(state.pio.data + first_count, second_count) ==
									second_count;
							}
							if (!ok) {
								finish_pio_transfer(false);
								break;
							}

#if defined(DEBUG_FDC)
							u32 fingerprint = 2166136261U;
							for (size_t i = 0; i < count; i++)
								fingerprint = (fingerprint ^ state.pio.data[i]) * 16777619U;
							printf("FDC [PIO]: staged read data fnv1a=%08x first=",
								(unsigned)fingerprint);
							for (size_t i = 0; i < count && i < 16; i++)
								printf("%02x%s", (unsigned)state.pio.data[i],
									i + 1 < count && i < 15 ? " " : "");
							if (count >= 2)
								printf(" tail=%02x %02x", (unsigned)state.pio.data[count - 2],
									(unsigned)state.pio.data[count - 1]);
							printf("\n");
#endif
						}

						// In non-DMA mode INT and RQM signal that execution data is ready.
						do_interrupt();
						break;
					}

					u8* buffer = new u8[count];
					memset(buffer, 0, count);

					FDC_DEBUG("FDC [LBA]: Calculated LBA = %d (offset 0x%x)\n", pos, pos * 512);

					bool io_ok = false;
					if (cmd == 6) {
						io_ok = disk->seek_byte(byte_offset) &&
							disk->read_bytes(buffer, first_count) == first_count;
						if (io_ok && second_count > 0) {
							io_ok = disk->seek_byte(second_offset) &&
								disk->read_bytes(buffer + first_count, second_count) ==
								second_count;
						}
#if defined(DEBUG_FDC)
						printf("FDC: read data:  %zx @ %x\n  ", count, pos * 512);
						for (int i = 0; i < count; i++)
						{
							printf("%02x ", *((char*)buffer + i) & 0xff);
							if (i % 16 == 15)
								printf("\n  ");
						}
						printf("\n");
#endif
						if (io_ok)
							isa_dma->send_data(2, buffer, count);
					} else {
						isa_dma->recv_data(2, buffer, count);
#if defined(DEBUG_FDC)
						printf("FDC: write data:  %zx @ %x\n  ", count, pos * 512);
						for (int i = 0; i < count; i++)
						{
							printf("%02x ", *((char*)buffer + i) & 0xff);
							if (i % 16 == 15)
								printf("\n  ");
						}
						printf("\n");
#endif
						io_ok = disk->seek_byte(byte_offset) &&
							disk->write_bytes(buffer, first_count) == first_count;
						if (io_ok && second_count > 0) {
							io_ok = disk->seek_byte(second_offset) &&
								disk->write_bytes(buffer + first_count, second_count) ==
								second_count;
						}
						if (io_ok)
							disk->flush();
					}
					delete[] buffer;

					if (io_ok) {
						prepare_rw_result(drive_idx, head, eot, geometry,
							mt, true, count);
						do_interrupt();
					} else {
						const bool not_ready = !disk->is_ready();
						fail_rw(not_ready ? 0 : ST1_ND);
						if (not_ready)
							state.cmd_res[0] |= ST0_NR;
					}
				}
				break;

				case 13: // Format Track
				{
					// Command parameters are HDS/DS, N, SC, GPL and the fill byte.
					// The execution phase then supplies SC four-byte C/H/R/N IDs.
					int drive_idx = state.cmd_parms[1] & 0x03;
					int head = (state.cmd_parms[1] >> 2) & 1;
					u8 sector_size = state.cmd_parms[2];
					u8 sector_count = state.cmd_parms[3];
					u8 fill = state.cmd_parms[5];
					CDisk* disk = drive_idx < 2 ? FDISK(drive_idx) : NULL;

					auto fail_format = [&](u8 st0, u8 st1) {
						state.cmd_res[0] = st0;
						state.cmd_res[1] = st1;
						state.cmd_res[2] = 0;
						state.cmd_res[3] = drive_idx < 2 ?
							(u8)state.drive[drive_idx].cylinder : 0;
						state.cmd_res[4] = (u8)head;
						state.cmd_res[5] = 1;
						state.cmd_res[6] = sector_size;
						do_interrupt();
					};

					if (disk == NULL || !disk->is_ready()) {
						fail_format(0x40 | ST0_NR | (head << 2) | drive_idx, 0);
						break;
					}
					if (disk->ro()) {
						fail_format(0x40 | (head << 2) | drive_idx, ST1_WP);
						break;
					}
					if (sector_size != 2 || sector_count == 0) {
						fail_format(0x40 | (head << 2) | drive_idx, ST1_ND);
						break;
					}

					size_t id_bytes = (size_t)sector_count * 4;
					if (state.dma) {
						if (!isa_dma || isa_dma->get_transfer_size(2) < id_bytes) {
							fail_format(0x40 | (head << 2) | drive_idx, ST1_OR);
							break;
						}

						u8 sector_ids[256 * 4];
						memset(sector_ids, 0, sizeof(sector_ids));
						isa_dma->recv_data(2, sector_ids, id_bytes);
						format_track(drive_idx, head, sector_size, sector_count,
							fill, sector_ids, id_bytes);
						do_interrupt();
					} else {
						state.pio.active = true;
						state.pio.write = true;
						state.pio.format = true;
						state.pio.drive = (u8)drive_idx;
						state.pio.head = (u8)head;
						state.pio.format_n = sector_size;
						state.pio.format_sc = sector_count;
						state.pio.format_fill = fill;
						state.pio.size = (u32)id_bytes;
						state.pio.first_size = 0;
						state.pio.pos = 0;
						state.cmd_res_ptr = 0;
						state.cmd_res_max = 0;
						state.status.rqm = true;
						state.status.dio = false;
						state.status.nondma = true;
						do_interrupt();
					}
					break;
				}

				case 7: // recalibrate
				{
					int drive_idx = state.cmd_parms[1] & 3;
					if (drive_idx < 2) {
						state.drive[drive_idx].seeking = 3; // wait for 3 status reads to finish seek.
						state.drive[drive_idx].cylinder = 0;
						if (FDISK(drive_idx) != NULL)
							FDISK(drive_idx)->acknowledge_media_change();
					}
					do_interrupt();
				}
				break;

				case 8: // sense interrupt status
					if (!state.interrupt){
						state.cmd_res[0] = 0x80;
						state.cmd_res[1] = 0;
					} else {
						int drive_idx = state.drive_select & 3;
						state.cmd_res[0] = 0x20 | drive_idx; // Seek End
						clear_interrupt();
						state.cmd_res[1] = state.drive[drive_idx].cylinder; // present cylinder number
					}
					break;

				case 10: // Read ID
				{
					int drive_idx = state.cmd_parms[1] & 3;
					int head = (state.cmd_parms[1] >> 2) & 1;
					CDisk* disk = drive_idx < 2 ? FDISK(drive_idx) : NULL;
					if (!disk || !disk->is_ready())
					{
						state.cmd_res[0] = 0x40 | ST0_NR | (head << 2) | drive_idx;
						state.cmd_res[1] = ST1_MAM;
						state.cmd_res[2] = 0;
						state.cmd_res[3] = drive_idx < 2 ?
							state.drive[drive_idx].cylinder : 0;
						state.cmd_res[4] = head;
						state.cmd_res[5] = 1;
						state.cmd_res[6] = 2;
						do_interrupt();
						break;
					}
					SFloppyGeometry geometry;
					get_geometry(drive_idx, &geometry);
					if (state.datarate != geometry.data_rate)
					{
						state.cmd_res[0] = 0x40 | (head << 2) | drive_idx;
						state.cmd_res[1] = ST1_MAM;
						state.cmd_res[2] = 0;
						state.cmd_res[3] = state.drive[drive_idx].cylinder;
						state.cmd_res[4] = head;
						state.cmd_res[5] = 1;
						state.cmd_res[6] = 2;
						do_interrupt();
						break;
					}
					state.cmd_res[0] = drive_idx | (head << 2);
					state.cmd_res[1] = 0;
					state.cmd_res[2] = 0;
					state.cmd_res[3] = state.drive[drive_idx].cylinder;
					state.cmd_res[4] = head;
					state.cmd_res[5] = 1;
					state.cmd_res[6] = 2; // 512 bytes
					do_interrupt();
					break;
				}

				case 14: // DumpReg
					// we're software, we don't care (I think)
					break;

				case 15: // seek
				{
					// args:
					// 0: opcode
					// 1: bit 2 = HDS (head), 1 = DS1, 0 = DS0 
					// 2: NCN = new cylinder number
					int drive_idx = state.cmd_parms[1] & 3;
					if (drive_idx < 2) {
						state.drive[drive_idx].seeking = 3; // wait 3 status reads to finish seek.
						state.drive[drive_idx].cylinder = state.cmd_parms[2];
						if (FDISK(drive_idx) != NULL)
							FDISK(drive_idx)->acknowledge_media_change();
					}
					do_interrupt();
					break;
				}

				case 16: // Version
					state.cmd_res[0] = 0x90; // 82077 compatible
					break;

				case 18: // perpendicular mode
					// We really don't care, somehow
					break;

				case 19: // configure
					// we're software, we don't care (I think)
					break;

				case 20: // Lock
					state.cmd_res[0] = (state.cmd_parms[0] >> 3) & 0x10; // per the datasheet
					break;

				default:
					FDC_DEBUG("FDC: unsupported command %d = %s\n",
						cmd, cmdinfo[cmd].name.c_str());
					// An unsupported opcode terminates in the result phase with
					// ST0's invalid-command indication; guest input must never
					// terminate the emulator process.
					state.cmd_res[0] = 0x80;
					state.cmd_res_max = 1;
					break;
				}


				state.status.rqm = 1;
				if (state.cmd_res_max > 0 && !state.pio.active) {
					state.status.dio = 1;
				}
				state.cmd_parms_ptr = 0;
			}
			else {
				//printf("FDC: command parameter byte %d = %x, expecting %d bytes for %s\n", state.cmd_parms_ptr-1, data, cmdinfo[state.cmd_parms[0] & 0x1f].parms, cmdinfo[state.cmd_parms[0] &0x1f].name.c_str());
			}
		}


		break;

	case FDC_REG_DIR:
		// PC/AT, PS/2
		//    bits 7-2 = reserved
		//    bit 0-1 = MFM data rate
		state.datarate = data & 0x03;
		FDC_DEBUG("FDC: data rate %s\n", datarate_name[state.datarate].c_str());

		break;
	}
}

u64 CFloppyController::ReadMem(int index, u64 address, int dsize)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	std::lock_guard<std::recursive_mutex> lock(controller_mutex);

	u64 data = 0;
	bool log_read = true;

	if (index == 1537)
		address += 7;

	switch (address)
	{
	case FDC_REG_STATUS_A:
		// bit 7 = interrupt pending
		// bit 6 = -DRV2  (second drive installed)
		// bit 5 = step
		// bit 4 = -track0
		// bit 3 = head1 select
		// bit 2 = -index
		// bit 1 = -write protect
		// bit 0 = +direction


		break;

	case FDC_REG_STATUS_B:
		// bit 7-6 reserved (1)
		// bit 5 = drive select
		// bit 4 = write data
		// bit 3 = read data
		// bit 2 = write enable
		// bit 1 = motor 1 enable
		// bit 0 = motor 0 enable



		break;

	case FDC_REG_DOR:
	case FDC_REG_TAPE:
		FDC_DEBUG("FDC: Write only register %" PRId64 " read.", address);
		break;

	case FDC_REG_STATUS:
		data = get_status();
		break;

	case FDC_REG_COMMAND:
	{
		// The data comes back from here.
		int bytes = dsize / 8;
		if (bytes < 1)
			bytes = 1;
		if (bytes > 8)
			bytes = 8;

		for (int i = 0; i < bytes; i++) {
			u8 value = 0;
			if (state.pio.active) {
				CDisk* disk = FDISK(state.pio.drive);
				if (!disk || !disk->is_ready())
					finish_pio_transfer(false);
			}
			if (state.pio.active) {
				if (state.pio.write) {
					FDC_DEBUG("FDC: read from data register during non-DMA write phase.\n");
					break;
				}

				value = state.pio.data[state.pio.pos++];
				data |= ((u64)value) << (i * 8);
				log_read = false;
				state.status.rqm = false;
				clear_interrupt();
				if (state.pio.pos >= state.pio.size) {
					finish_pio_transfer(true);
					break;
				}
				state.status.rqm = true;
				do_interrupt();
				continue;
			}

			if (!state.status.dio || state.cmd_res_max == 0)
				break;

			bool first_result_byte = state.cmd_res_ptr == 0;
			value = state.cmd_res[state.cmd_res_ptr++];
			data |= ((u64)value) << (i * 8);
			state.status.rqm = false;
			// Result-phase INT is acknowledged by the first result-byte read,
			// not by draining the entire seven-byte result packet.
			if (first_result_byte)
				clear_interrupt();
			if (state.cmd_res_ptr >= state.cmd_res_max) {
				state.status.rqm = true;
				state.status.dio = false;
				state.cmd_res_ptr = 0;
				state.cmd_res_max = 0;
				break;
			}
			state.status.rqm = true;
		}
		break;
	}

	case FDC_REG_DIR:
		// PS/2 mode:
		//    bit 7 = diskette change
		//    bits 6-3 = 1
		//    bit 2 = datarate select 1
		//    bit 1 = datarate select 0
		//    bit 0 = high density select

		service_pending_media_actions_if_idle();
		int drive_idx = state.drive_select & 3;
		CDisk* disk = drive_idx < 2 ? FDISK(drive_idx) : NULL;
		if (disk != NULL && disk->media_present()) {
			data = disk->media_change_pending() ? 0x80 : 0x00;
		} else {
			data = 0x80;
		}
		break;
	}

	if (log_read)
		FDC_DEBUG("FDC: Read register %" PRId64 ", value: %" PRIx64 "\n", address, data);

	return data;
}


static u32 fdc_magic1 = 0x0fdc0fdc;
static u32 fdc_magic2 = 0xfdc0fdc0;

int CFloppyController::SaveState(FILE* f) {
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	std::lock_guard<std::recursive_mutex> lock(controller_mutex);
	long  ss = sizeof(state);

	fwrite(&fdc_magic1, sizeof(u32), 1, f);
	fwrite(&ss, sizeof(long), 1, f);
	fwrite(&state, sizeof(state), 1, f);
	fwrite(&fdc_magic2, sizeof(u32), 1, f);
	FDC_DEBUG("fdc: %ld bytes saved.\n", ss);
	return 0;
}

int CFloppyController::RestoreState(FILE* f)
{
	std::lock_guard<std::recursive_mutex> bus_lock(cSystem->get_device_bus_mutex());
	std::lock_guard<std::recursive_mutex> lock(controller_mutex);
	long    ss;
	u32     m1;
	u32     m2;
	size_t  r;

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("fdc: unexpected end of file!\n");
		return -1;
	}

	if (m1 != fdc_magic1)
	{
		printf("fdc: MAGIC 1 does not match!\n");
		return -1;
	}

	r = fread(&ss, sizeof(long), 1, f);
	if (r != 1)
	{
		printf("fdc: unexpected end of file!\n");
		return -1;
	}

	if (ss != sizeof(state))
	{
		printf("fdc: STRUCT SIZE does not match!\n");
		return -1;
	}

	r = fread(&state, sizeof(state), 1, f);
	if (r != 1)
	{
		printf("fdc: unexpected end of file!\n");
		return -1;
	}

	r = fread(&m2, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("fdc: unexpected end of file!\n");
		return -1;
	}

	if (m2 != fdc_magic2)
	{
		printf("fdc: MAGIC 2 does not match!\n");
		return -1;
	}

	FDC_DEBUG("fdc: %ld bytes restored.\n", ss);
	return 0;
}

void CFloppyController::init()
{
    if (theAli)
    {
        bool hasA = (FDISK(0) != NULL);
        bool hasB = (FDISK(1) != NULL);
        theAli->set_floppy_presence(hasA, hasB);
    }
}

void CFloppyController::do_interrupt() {
	state.interrupt = true;
	if (theAli && (state.dor & 0x08))
		theAli->pic_interrupt(0, 6);
}

void CFloppyController::clear_interrupt() {
	state.interrupt = false;
	if (theAli)
		theAli->pic_deassert(0, 6);
}


u8 CFloppyController::get_status() {
	// bit 7 = RQM data register is ready (0: no access is permitted)
	// bit 6 = 1: transfer from controller to system, 0: sys to controller
	// bit 5 = non dma mode
	// bit 4 = diskette controller is busy
	// bit 3-2 reserved
	// bit 1 = drive 1 is busy (seeking)
	// bit 0 = drive 0 is busy (seeking)

	for (int i = 0; i < 2; i++) {
		if (state.drive[i].seeking > 0)
			state.drive[i].seeking--;
		if (state.drive[i].seeking == 0)
			state.status.seeking[i] = false;
		else
			state.status.seeking[i] = true;
	}

	// CMD BUSY is separate from the per-drive seek/recalibrate busy bits.  For
	// commands without a result phase it clears after the last command byte.
	state.status.nondma = state.pio.active;
	if (state.pio.active || (state.status.dio && state.status.rqm) ||
		(state.cmd_parms_ptr > 0))
		state.status.busy = true;
	else
		state.status.busy = false;


	FDC_DEBUG("FDC Status: %s, %s, %s, %s, %s, %s\n",
		state.status.rqm ? "Data Register Ready" : "No Access",
		state.status.dio ? "C->S" : "S->C",
		state.status.nondma ? "No DMA" : "DMA",
		state.status.busy ? "BUSY" : "not busy",
		state.status.seeking[0] ? "Disk 1 Seeking" : "Disk 1 Idle",
		state.status.seeking[1] ? "Disk 0 Seeking" : "Disk 0 Idle");

	u8 data = (state.status.rqm ? 0x80 : 0x00) |
		(state.status.dio ? 0x40 : 0x00) |
		(state.status.nondma ? 0x20 : 0x00) |
		(state.status.busy ? 0x10 : 0x00) |
		(state.status.seeking[1] ? 0x02 : 0x00) |
		(state.status.seeking[0] ? 0x01 : 0x00);
	return data;
}
