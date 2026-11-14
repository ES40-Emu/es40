/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by Cacodemon345
 * Copyright (C) 2023, 2025-2026 by gdwnldsKSC
 * Copyright (C) 2026 by GenSayer
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
 * Parts of this file are based upon Bochs, which is Copyright (C) 2002
 * MandrakeSoft S.A., originally distributed under the GNU Lesser General
 * Public License, version 2 or later.
 */

 /**
  * \file
  * Contains code for the disk base class.
  *
  * $Id$
  *
  * X-1.31       Camiel Vanderhoeven                             02-APR-2008
  *      Fixed compiler warnings.
  *
  * X-1.30       Camiel Vanderhoeven                             16-MAR-2008
  *      Always reset dati.read when setting dati.available.
  *
  * X-1.29       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.28       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.27       Brian Wheeler                                   27-FEB-2008
  *      Avoid compiler warnings.
  *
  * X-1.26       David Leonard                                   20-FEB-2008
  *      Return SYSTEM RESOURCE FAILURE sense if dato/dati buffer size is
  *      exceeded.
  *
  * X-1.25       Brian Wheeler                                   20-FEB-2008
  *      Support MSF in READ TOC scsi command.
  *
  * X-1.24       Brian Wheeler                                   18-FEB-2008
  *      Added vital product data page 0 (Required for Tru64).
  *
  * X-1.23       Camiel Vanderhoeven                             18-FEB-2008
  *      Removed support for CD-R/RW specific SCSI commands.
  *
  * X-1.22       Camiel Vanderhoeven                             18-FEB-2008
  *      READ CAPACITY returns the number of the last LBA (n-1); not the
  *      number of LBA's (n).
  *
  * X-1.20       Camiel Vanderhoeven                             17-FEB-2008
  *      Set up sense data when error occurs.
  *
  * X-1.19       Camiel Vanderhoeven                             17-FEB-2008
  *      Added REQUEST_SENSE scsi command.
  *
  * X-1.18       Camiel Vanderhoeven                             16-FEB-2008
  *      Added READ_LONG scsi command, and support for MODE_SENSE changeable
  *      parameter pages.
  *
  * X-1.17       Camiel Vanderhoeven                             21-JAN-2008
  *      OpenVMs doesn't like max 255 sectors.
  *
  * X-1.16       Camiel Vanderhoeven                             16-JAN-2008
  *      Less messages without debugging enabled.
  *
  * X-1.15       Brian Wheeler                                   14-JAN-2008
  *      Corrected output from read track info and read toc.
  *
  * X-1.14       Brian Wheeler/Camiel Vanderhoeven               14-JAN-2008
  *      Added read_track_info command, completed read_toc command.
  *
  * X-1.13       Camiel Vanderhoeven                             13-JAN-2008
  *      Determine best-fitting C/H/S lay-out.
  *
  * X-1.11       Brian Wheeler                                   13-JAN-2008
  *      More CD-ROM commands supported.
  *
  * X-1.10       Camiel Vanderhoeven                             12-JAN-2008
  *      Include SCSI engine, because this is common to both SCSI and ATAPI
  *      devices.
  *
  * X-1.9        Camiel Vanderhoeven                             09-JAN-2008
  *      Save disk state to state file.
  *
  * X-1.8        Camiel Vanderhoeven                             06-JAN-2008
  *      Set default blocksize to 2048 for cd-rom devices.
  *
  * X-1.7        Camiel Vanderhoeven                             06-JAN-2008
  *      Support changing the block size (required for SCSI, ATAPI).
  *
  * X-1.6        Camiel Vanderhoeven                             02-JAN-2008
  *      Cleanup.
  *
  * X-1.5        Camiel Vanderhoeven                             29-DEC-2007
  *      Fix memory-leak.
  *
  * X-1.4        Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.3        Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.2        Brian Wheeler                                   16-DEC-2007
  *      Fixed case of StdAfx.h.
  *
  * X-1.1        Camiel Vanderhoeven                             12-DEC-2007
  *      Initial version in CVS.
  **/
#include "StdAfx.h"
#include "Disk.h"
#include "DiskFile.h"

#include <memory>

#define SCSI_MEDIA_STATE_STABLE             0
#define SCSI_MEDIA_STATE_REMOVED            1
#define SCSI_MEDIA_STATE_CHANGED           -1
#define SCSI_MEDIA_STATE_RESET              2
#define SCSI_MEDIA_STATE_RESET_REMOVED      3
#define SCSI_MEDIA_STATE_RESET_CHANGED     -2

  /**
   * \brief Constructor.
   **/
CDisk::CDisk(CConfigurator* cfg, CSystem* sys, CDiskController* ctrl,
	int idebus, int idedev) : CSystemComponent(cfg, sys)
{
	char* a;
	char* b;
	char* c;
	char* d;

	myCfg = cfg;
	myCtrl = ctrl;
	myBus = idebus;
	myDev = idedev;
	atapi_mode = false;

	a = myCfg->get_myName();
	b = myCfg->get_myValue();
	c = myCfg->get_myParent()->get_myName();
	d = myCfg->get_myParent()->get_myValue();

	free(devid_string); // we override the default to include the controller.
	CHECK_ALLOCATION(devid_string = (char*)malloc(
		strlen(a) + strlen(b) + strlen(c) + strlen(d) + 6));
	sprintf(devid_string, "%s(%s).%s(%s)", c, d, a, b);

	// Accept both spellings: es40-cfg and the sample config have always
	// emitted the long forms, while the code historically read the short ones.
	serial_number = myCfg->get_text_value("serial_number",
		myCfg->get_text_value("serial_num", "ES40EM00000"));
	revision_number = myCfg->get_text_value("rev_number",
		myCfg->get_text_value("rev_num", "0.0"));
	is_cdrom = myCfg->get_bool_value("cdrom");
	read_only = myCfg->get_bool_value("read_only", is_cdrom);

	byte_size = 0;
	cylinders = 0;
	heads = 0;
	sectors = 0;
	state.block_size = is_cdrom ? 2048 : 512;
	state.byte_pos = 0;
	state.scsi.sense.available = false;
	state.scsi.locked = false;
	state.scsi.media_changed = SCSI_MEDIA_STATE_STABLE;
	fault_control = std::make_shared<CDiskFaultControl>(devid_string,
		myCfg->get_bool_value("offline", false));
	register_disk_fault_control(fault_control);

	myCtrl->register_disk(this, myBus, myDev);
}

/**
 * \brief Destructor.
 **/
CDisk::~CDisk(void)
{
	fault_control->deactivate();
	free(devid_string);
	devid_string = nullptr;
}

/**
 * \Calculate the number of cylinders to report.
 **/
void CDisk::calc_cylinders()
{
	cylinders = byte_size / state.block_size / sectors / heads;

	off_t_large chs_size = sectors * cylinders * heads * state.block_size;
	if (chs_size < byte_size)
		cylinders++;
}

/**
 * \brief Called when this device is selected.
 *
 * Set status fields up to begin a new SCSI command sequence
 * and set the SCSI bus phase to Message Out.
 **/
void CDisk::scsi_select_me(int bus)
{
	// Selection begins a new SCSI/ATAPI transaction.
	// That's where changing media is safe
	try
	{
		service_pending_media_actions();
	}
	catch (...)
	{
		printf("%s: Could not service a pending media action.\n",
			devid_string);
	}

	state.scsi.msgo.written = 0;
	state.scsi.msgi.available = 0;
	state.scsi.msgi.read = 0;
	state.scsi.cmd.written = 0;
	state.scsi.dati.available = 0;
	state.scsi.dati.read = 0;
	state.scsi.dato.expected = 0;
	state.scsi.dato.written = 0;
	state.scsi.stat.available = 0;
	state.scsi.stat.read = 0;
	state.scsi.lun_selected = false;

	//state.scsi.disconnect_priv = false;
	//state.scsi.will_disconnect = false;
	//state.scsi.disconnected = false;
	if (atapi_mode)
		scsi_set_phase(bus, SCSI_PHASE_COMMAND);
	else
		scsi_set_phase(bus, SCSI_PHASE_MSG_OUT);
}

/** Reset the target protocol state without changing the media. **/
void CDisk::scsi_reset()
{
	const bool media_removed = state.scsi.media_changed == SCSI_MEDIA_STATE_REMOVED ||
		state.scsi.media_changed == SCSI_MEDIA_STATE_RESET_REMOVED;
	const bool media_changed = state.scsi.media_changed == SCSI_MEDIA_STATE_CHANGED ||
		state.scsi.media_changed == SCSI_MEDIA_STATE_RESET_CHANGED;
	memset(&state.scsi, 0, sizeof(state.scsi));
	state.scsi.media_changed = media_removed ? SCSI_MEDIA_STATE_RESET_REMOVED :
		(media_changed ? SCSI_MEDIA_STATE_RESET_CHANGED : SCSI_MEDIA_STATE_RESET);
	media_lock_changed(false);
}

static u32  disk_magic1 = 0xD15D15D1;
static u32  disk_magic2 = 0x15D15D15;

/**
 * Save state to a Virtual Machine State file.
 **/
int CDisk::SaveState(FILE* f)
{
	const u8 offline = is_offline() ? 1 : 0;
	long  ss = sizeof(state) + sizeof(offline);

	if (fwrite(&disk_magic1, sizeof(u32), 1, f) != 1 ||
		fwrite(&ss, sizeof(long), 1, f) != 1 ||
		fwrite(&state, sizeof(state), 1, f) != 1 ||
		fwrite(&offline, sizeof(offline), 1, f) != 1 ||
		fwrite(&disk_magic2, sizeof(u32), 1, f) != 1)
		return -1;
	printf("%s: %d bytes saved.\n", devid_string, (int)ss);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CDisk::RestoreState(FILE* f)
{
	long    ss;
	u32     m1;
	u32     m2;
	size_t  r;

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m1 != disk_magic1)
	{
		printf("%s: MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	r = fread(&ss, sizeof(long), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (ss != sizeof(state) && ss != sizeof(state) + sizeof(u8))
	{
		printf("%s: STRUCT SIZE does not match!\n", devid_string);
		return -1;
	}

	// Do not replace the live protocol state unless the entire record is valid.
	std::unique_ptr<SDisk_state> restored_state(new SDisk_state);
	r = fread(restored_state.get(), sizeof(state), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	// Older disk records predate failure injection and restore as online.
	u8 offline = 0;
	if (ss != sizeof(state) &&
		(fread(&offline, sizeof(offline), 1, f) != 1 || offline > 1))
	{
		printf("%s: Invalid disk offline state.\n", devid_string);
		return -1;
	}
	r = fread(&m2, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m2 != disk_magic2)
	{
		printf("%s: MAGIC 2 does not match!\n", devid_string);
		return -1;
	}

	const auto& s = *restored_state;
	if (s.byte_pos < 0 || s.byte_pos > byte_size ||
		(byte_size > 0 && (!s.block_size || s.block_size > (u64)byte_size)) ||
		s.scsi.msgi.available > sizeof(s.scsi.msgi.data) ||
		s.scsi.msgi.read > s.scsi.msgi.available ||
		s.scsi.msgo.written > sizeof(s.scsi.msgo.data) ||
		s.scsi.cmd.written > sizeof(s.scsi.cmd.data) ||
		s.scsi.dati.available > sizeof(s.scsi.dati.data) ||
		s.scsi.dati.read > s.scsi.dati.available ||
		s.scsi.dato.expected > sizeof(s.scsi.dato.data) ||
		s.scsi.dato.written > s.scsi.dato.expected ||
		s.scsi.stat.available > sizeof(s.scsi.stat.data) ||
		s.scsi.stat.read > s.scsi.stat.available ||
		s.scsi.sense.available > sizeof(s.scsi.sense.data))
	{
		printf("%s: Invalid disk position or protocol buffer bounds.\n", devid_string);
		return -1;
	}
	memcpy(&state, restored_state.get(), sizeof(state));
	set_offline(offline != 0);
	//calc_cylinders(); // state.block_size may have changed.
	determine_layout();
	media_lock_changed(state.scsi.locked);

	printf("%s: %d bytes restored.\n", devid_string, (int)ss);
	return 0;
}

/**
 * \brief Return the number of bytes expected or available.
 *
 * Return the number of bytes we still expect to receive
 * from the initiator, or still have available for the
 * initiator, in the current SCSI phase.
 *
 * For an overview of data transfer during a SCSI bus phase,
 * see SCSIDevice::scsi_xfer_ptr.
 **/
size_t CDisk::scsi_expected_xfer_me(int bus)
{
	switch (scsi_get_phase(0))
	{
	case SCSI_PHASE_DATA_OUT:
		return state.scsi.dato.expected - state.scsi.dato.written;

	case SCSI_PHASE_DATA_IN:
		return state.scsi.dati.available - state.scsi.dati.read;

	case SCSI_PHASE_COMMAND:
		return 256 - state.scsi.cmd.written;

	case SCSI_PHASE_STATUS:
		return state.scsi.stat.available - state.scsi.stat.read;

	case SCSI_PHASE_MSG_OUT:
		return 256 - state.scsi.msgo.written;

	case SCSI_PHASE_MSG_IN:
		return state.scsi.msgi.available - state.scsi.msgi.read;

	default:
		FAILURE_2(IllegalState, "%s: transfer requested in phase %d\n",
			devid_string, scsi_get_phase(0));
	}
}

/**
 * \brief Return a pointer where the initiator can read or write data.
 *
 * Return a pointer to where the initiator can read or write
 * (the remainder of) our data in the current SCSI phase.
 *
 * For an overview of data transfer during a SCSI bus phase,
 * see SCSIDevice::scsi_xfer_ptr.
 **/
void* CDisk::scsi_xfer_ptr_me(int bus, size_t bytes)
{
	void* res = 0;

	switch (scsi_get_phase(0))
	{
	case SCSI_PHASE_DATA_OUT:
		res = &(state.scsi.dato.data[state.scsi.dato.written]);
		state.scsi.dato.written += bytes;
		break;

	case SCSI_PHASE_DATA_IN:
		res = &(state.scsi.dati.data[state.scsi.dati.read]);
		state.scsi.dati.read += bytes;
		break;

	case SCSI_PHASE_COMMAND:
		res = &(state.scsi.cmd.data[state.scsi.cmd.written]);
		state.scsi.cmd.written += bytes;
		break;

	case SCSI_PHASE_STATUS:
		res = &(state.scsi.stat.data[state.scsi.stat.read]);
		state.scsi.stat.read += bytes;
		break;

	case SCSI_PHASE_MSG_OUT:
		res = &(state.scsi.msgo.data[state.scsi.msgo.written]);
		state.scsi.msgo.written += bytes;
		break;

	case SCSI_PHASE_MSG_IN:

		//if (PT.reselected)
		//{
		//  retval = 0x80; // identify
		//  break;
		//}
		//if (PT.disconnected)
		//{
		//  if (!PT.dati_ptr)
		//    retval = 0x04; // disconnect
		//  else
		//  {
		//    if (state.scsi.msgi.read==0)
		//    {
		//      retval = 0x02; // save data pointer
		//      state.scsi.msgi.read=1;
		//    }
		//    else if (state.scsi.msgi.read==1)
		//    {
		//      retval = 0x04; // disconnect
		//      state.scsi.msgi.read=0;
		//    }
		//  }
		//  break;
		//}
		res = &(state.scsi.msgi.data[state.scsi.msgi.read]);
		state.scsi.msgi.read += bytes;
		break;

	default:
		FAILURE_2(IllegalState, "%s: transfer requested in phase %d\n",
			devid_string, scsi_get_phase(0));
	}

	return res;
}

/**
 * \brief Process data written or read.
 *
 * Determine what action (if any) should be taken after a
 * transfer, and what the next SCSI bus phase should be.
 *
 * For an overview of data transfer during a SCSI bus phase,
 * see SCSIDevice::scsi_xfer_ptr.
 *
 * \todo Handle disconnect/reconnect properly.
 **/
void CDisk::scsi_xfer_done_me(int bus)
{
	int res;
	int newphase = scsi_get_phase(0);

	switch (scsi_get_phase(0))
	{
	case SCSI_PHASE_DATA_OUT:
		if (state.scsi.dato.written < state.scsi.dato.expected)
			break;

		res = do_scsi_command();
		if (res == 2)
			FAILURE(IllegalState, "do_command returned 2 after DATA OUT phase");

		if (state.scsi.dati.available)
			newphase = SCSI_PHASE_DATA_IN;
		else
			newphase = SCSI_PHASE_STATUS;
		break;

	case SCSI_PHASE_DATA_IN:
		if (state.scsi.dati.read < state.scsi.dati.available)
			break;

		newphase = SCSI_PHASE_STATUS;
		break;

	case SCSI_PHASE_COMMAND:
		res = do_scsi_command();
		if (res == 2)
			newphase = SCSI_PHASE_DATA_OUT;
		else if (state.scsi.dati.available)
			newphase = SCSI_PHASE_DATA_IN;
		else
			newphase = SCSI_PHASE_STATUS;
		break;

	case SCSI_PHASE_STATUS:
		if (state.scsi.stat.read < state.scsi.stat.available)
			break;

		if (atapi_mode)
		{
			scsi_free(0);
			return;
		}

		newphase = SCSI_PHASE_MSG_IN;
		break;

	case SCSI_PHASE_MSG_OUT:
		newphase = do_scsi_message();       // command
		break;

	case SCSI_PHASE_MSG_IN:

		//if (state.scsi.reselected)
		//{
		//  state.scsi.reselected = false;
		//  newphase = state.scsi.disconnect_phase;
		//}
		//else if (state.scsi.disconnected)
		//{
		//  if (!state.scsi.msgi.read)
		//    newphase = -1;
		//}
		if (state.scsi.msgi.read < state.scsi.msgi.available)
			break;

		if (state.scsi.cmd.written)
		{
			scsi_free(0);
			return;
		}
		else
			newphase = SCSI_PHASE_COMMAND;
		break;

	default:
		FAILURE_2(IllegalState, "%s: transfer requested in phase %d\n",
			devid_string, scsi_get_phase(0));
	}

	// if data in and can disconnect...
	//if (state.phase!=7 && newphase==1 && PT.will_disconnect && !PT.disconnected)
	//{
	//  printf("%s: Disconnecting now...\n",devid_string);
	//  PT.disconnected = true;
	//  PT.disconnect_phase = newphase;
	//  newphase = 7; // msg in
	//}
	if (newphase != scsi_get_phase(0))
	{

		//    if (newphase==-1)
		//    {
		//      printf("%s: Disconnect. Timer started!\n",devid_string);
		//      // disconnect. generate interrupt?
		//      state.disconnected = 20;
		//    }
		scsi_set_phase(0, newphase);
	}

	//getchar();
}

// SCSI commands:
#define SCSICMD_TEST_UNIT_READY       0x00
#define SCSICMD_REQUEST_SENSE         0x03
#define SCSICMD_INQUIRY               0x12

#define SCSICMD_READ                  0x08
#define SCSICMD_READ_10               0x28
#define SCSICMD_READ_12               0xA8
#define SCSICMD_READ_16               0x88
#define SCSICMD_READ_32               0x7F
#define SCSICMD_READ_LONG             0x3E
#define SCSICMD_READ_CD               0xBE

#define SCSICMD_VERIFY_10             0x2F

#define SCSICMD_WRITE                 0x0A
#define SCSICMD_WRITE_10              0x2A
#define SCSICMD_WRITE_12              0xAA
#define SCSICMD_WRITE_LONG            0x3F

#define SCSICMD_MODE_SELECT           0x15
#define SCSICMD_MODE_SENSE            0x1a
#define SCSICMD_START_STOP_UNIT       0x1b
#define SCSICMD_PREVENT_ALLOW_REMOVE  0x1e
#define SCSICMD_READ_SUBCHANNEL       0x42
#define SCSICMD_MODE_SELECT_10        0x55
#define SCSICMD_MODE_SENSE_10         0x5a
#define SCSICMD_MAINTENANCE_IN        0xA3

#define SCSICMD_SYNCHRONIZE_CACHE     0x35

#define SCSICMD_GET_EVENT_STATUS_NOTIFICATION 0x4a

//  SCSI block device commands:
#define SCSIBLOCKCMD_READ_CAPACITY  0x25
#define SCSIBLOCKCMD_SEEK           0x2B

//  SCSI CD-ROM commands:
#define SCSICDROM_READ_SUBCHANNEL  0x42
#define SCSICDROM_READ_TOC         0x43
#define SCSICDROM_MECHANISM_STATUS 0xBD

// SCSI CD-R/RW commands:
#define SCSICDRRW_FORMAT          0x04
#define SCSICDRRW_READ_DISC_INFO  0x51
#define SCSICDRRW_READ_TRACK_INFO 0x52
#define SCSICDRRW_RESERVE_TRACK   0x53
#define SCSICDRRW_SEND_OPC_INFO   0x54
#define SCSICDRRW_REPAIR_TRACK    0x58
#define SCSICDRRW_READ_MASTER_CUE 0x59
#define SCSICDRRW_CLOSE_TRACK     0x5b
#define SCSICDRRW_READ_BUFFER_CAP 0x5c
#define SCSICDRRW_SEND_CUE_SHEET  0x5d
#define SCSICDRRW_BLANK           0xa1
//  SCSI tape commands:
#define SCSICMD_REWIND            0x01
#define SCSICMD_READ_BLOCK_LIMITS 0x05
#define SCSICMD_SPACE             0x11

// SCSI mode pages:
#define SCSIMP_VENDOR               0x00
#define SCSIMP_READ_WRITE_ERRREC    0x01
#define SCSIMP_DISCONNECT_RECONNECT 0x02
#define SCSIMP_FORMAT_PARAMS        0x03
#define SCSIMP_RIGID_GEOMETRY       0x04
#define SCSIMP_FLEX_PARAMS          0x05
#define SCSIMP_CACHING              0x08
#define SCSIMP_INFO_EXCEPTIONS      0x1C
#define SCSIMP_CDROM_CAP            0x2A

#define SCSI_OK                     0
#define SCSI_ILL_CMD                - 1 /* illegal command */
#define SCSI_LBA_RANGE              - 2 /* LBA out of range */
#define SCSI_TOO_BIG                - 3 /* Too big for buffer */
// Per SPC: for malformed RSOC CDB (e.g., wrong reporting option vs opcode type)
#define SCSI_INVALID_FIELD			- 4 /* Invalid field in CDB */
#define SCSI_MEDIA_CHANGE           - 5 /* Media changed */
#define SCSI_MEDIA_REMOVED          - 6 /* Media removed */
#define SCSI_INVALID_LUN            - 7 /* Invalid LUN */
#define SCSI_RESET                  - 8 /* Power-on or bus/device reset */
#define SCSI_MEDIA_LOCKED           - 9 /* Medium removal prevented */
#define SCSI_HARDWARE_ERROR        - 10 /* Backing-media operation failed */
#define SCSI_WRITE_PROTECTED       - 11 /* Medium is read-only */
#define SCSI_OFFLINE               - 12 /* Attached drive requires intervention */
#define SCSI_PARAM_LENGTH          - 13 /* Truncated mode parameter list */
#define SCSI_INVALID_PARAM         - 14 /* Invalid field in parameter list */

void CDisk::do_scsi_error(int errcode, int info)
{
	state.scsi.stat.available = 1;
	state.scsi.stat.data[0] = 0;
	state.scsi.stat.read = 0;
	state.scsi.msgi.available = 1;
	state.scsi.msgi.data[0] = 0;
	state.scsi.msgi.read = 0;

	if (errcode == SCSI_OK)
	{
#if defined(DEBUG_SCSI)
		printf("%s: Command returns OK status.\n", devid_string);
#endif
		return;
	}

	state.scsi.stat.data[0] = 0x02;       // check sense
	state.scsi.sense.data[0] = 0xf0;      // error code
	state.scsi.sense.data[1] = 0x00;      // segment number
	state.scsi.sense.data[3] = info >> 24;      // info
	state.scsi.sense.data[4] = info >> 16;
	state.scsi.sense.data[5] = info >> 8;
	state.scsi.sense.data[6] = info & 0xFF;
	state.scsi.sense.data[7] = 10;        // additional sense length
	state.scsi.sense.data[8] = 0x00;      // command specific
	state.scsi.sense.data[9] = 0x00;
	state.scsi.sense.data[10] = 0x00;
	state.scsi.sense.data[11] = 0x00;
	state.scsi.sense.data[14] = 0x00;     // FRU code
	state.scsi.sense.data[15] = 0x00;     // sense key specific
	state.scsi.sense.data[16] = 0x00;
	state.scsi.sense.data[17] = 0x00;
	state.scsi.sense.available = 18;

	switch (errcode)
	{
	case SCSI_ILL_CMD:
		state.scsi.sense.data[2] = 0x05;    // illegal request
		state.scsi.sense.data[12] = 0x20;   // invalid command
		state.scsi.sense.data[13] = 0x00;
#if defined(DEBUG_SCSI)
		printf("%s: Command returns check sense status (sense: ILLEGAL COMMAND).\n",
			devid_string);
#endif
		break;

	case SCSI_INVALID_FIELD:
		state.scsi.sense.data[2] = 0x05;   // ILLEGAL REQUEST
		state.scsi.sense.data[12] = 0x24;   // INVALID FIELD IN CDB
		state.scsi.sense.data[13] = 0x00;
#if defined(DEBUG_SCSI)
		printf("%s: Check sense: INVALID FIELD IN CDB.\n", devid_string);
#endif
		break;

	case SCSI_INVALID_LUN:
		state.scsi.sense.data[2] = 0x05;   // ILLEGAL REQUEST
		state.scsi.sense.data[12] = 0x25;   // INVALID LUN
		state.scsi.sense.data[13] = 0x00;
#if defined(DEBUG_SCSI)
		printf("%s: Check sense: INVALID LUN.\n", devid_string);
#endif
		break;

	case SCSI_PARAM_LENGTH:
		state.scsi.sense.data[2] = 0x05;   // ILLEGAL REQUEST
		state.scsi.sense.data[12] = 0x1a;  // PARAMETER LIST LENGTH ERROR
		state.scsi.sense.data[13] = 0x00;
		break;

	case SCSI_INVALID_PARAM:
		state.scsi.sense.data[2] = 0x05;   // ILLEGAL REQUEST
		state.scsi.sense.data[12] = 0x26;  // INVALID FIELD IN PARAMETER LIST
		state.scsi.sense.data[13] = 0x00;
		break;

	case SCSI_LBA_RANGE:
		state.scsi.sense.data[2] = 0x05;    // illegal request
		state.scsi.sense.data[12] = 0x21;   // LBA out of range
		state.scsi.sense.data[13] = 0x00;
#if defined(DEBUG_SCSI)
		printf("%s: Command returns check sense status (sense: LBA OUT OF RANGE).\n",
			devid_string);
#endif
		break;

	case SCSI_MEDIA_CHANGE:
		state.scsi.sense.data[2] = 0x06;    // unit attention
		state.scsi.sense.data[12] = 0x28;   // media changed
		state.scsi.sense.data[13] = 0x00;
		break;

	case SCSI_MEDIA_REMOVED:
		state.scsi.sense.data[2] = 0x02;    // not ready
		state.scsi.sense.data[12] = 0x3a;   // media not present
		state.scsi.sense.data[13] = 0x00;
		break;

	case SCSI_OFFLINE:
		state.scsi.sense.data[2] = 0x02;    // NOT READY
		state.scsi.sense.data[12] = 0x04;   // logical unit not ready
		state.scsi.sense.data[13] = 0x03;   // manual intervention required
		break;

	case SCSI_MEDIA_LOCKED:
		state.scsi.sense.data[2] = 0x05;    // illegal request
		state.scsi.sense.data[12] = 0x53;   // media removal prevented
		state.scsi.sense.data[13] = 0x02;
		break;

	case SCSI_HARDWARE_ERROR:
		state.scsi.sense.data[2] = 0x04;    // hardware error
		state.scsi.sense.data[12] = 0x44;   // internal target failure
		state.scsi.sense.data[13] = 0x00;
		break;

	case SCSI_WRITE_PROTECTED:
		state.scsi.sense.data[2] = 0x07;    // data protect
		state.scsi.sense.data[12] = 0x27;   // write protected
		state.scsi.sense.data[13] = 0x00;
		break;

	case SCSI_TOO_BIG:
		state.scsi.sense.data[2] = 0x05;    // illegal request
		state.scsi.sense.data[12] = 0x55;   // system resource failure
		state.scsi.sense.data[13] = 0x00;
#if defined(DEBUG_SCSI)
		printf("%s: Command returns check sense status (sense: SYSTEM RESOURCE FAILURE).\n",
			devid_string);
#endif
		break;

	case SCSI_RESET:
		state.scsi.sense.data[2] = 0x06;   // unit attention
		state.scsi.sense.data[12] = 0x29;  // power on, reset, or bus-device reset
		state.scsi.sense.data[13] = 0x00;
#if defined(DEBUG_SCSI)
		printf("%s: Check sense: POWER ON OR RESET.\n", devid_string);
#endif
		break;
	}
}

/**
 * Basic algorithm taken from libcdio for converting lba to msf
 **/
static u32 lba2msf(off_t_large lba)
{
#define PREGAP_SECTORS    150
#define CD_FRAMES_PER_SEC 75
#define CD_MAX_LSN        450150

#define bin2bcd(x)        ((x / 10) << 4) | (x % 10)
	int m;

	int s;

	int f;
	lba -= PREGAP_SECTORS;
	if (lba >= -PREGAP_SECTORS)
	{
		m = (lba + PREGAP_SECTORS) / (CD_FRAMES_PER_SEC * 60);
		lba -= m * (CD_FRAMES_PER_SEC * 60);
		s = (lba + PREGAP_SECTORS) / CD_FRAMES_PER_SEC;
		lba -= s * CD_FRAMES_PER_SEC;
		f = lba + PREGAP_SECTORS;
	}
	else
	{
		m = (lba + CD_MAX_LSN) / (CD_FRAMES_PER_SEC * 60);
		lba -= m * (CD_FRAMES_PER_SEC * 60);
		s = (lba + CD_MAX_LSN) / CD_FRAMES_PER_SEC;
		lba -= s * CD_FRAMES_PER_SEC;
		f = lba + CD_MAX_LSN;
	}

	if (m > 99)
		m = 99;

	//printf("m=%d, s=%d, f=%d == m=%x, s=%x, f=%x\n", m,s,f,bin2bcd(m),bin2bcd(s),bin2bcd(f));
	return bin2bcd(m) << 16 | bin2bcd(s) << 8 | bin2bcd(f);
}

static inline bool opcode_has_service_actions(u8 op)
{
	// Common SA-coded opcodes (SPC/MMC/SBC families)
	switch (op) {
	case 0x7F: // VARIABLE LENGTH CDB
	case 0x9E: // SERVICE ACTION IN(16)
	case 0x9F: // SERVICE ACTION OUT(16)
	case 0xA3: // MAINTENANCE IN (this command)
	case 0xA4: // MAINTENANCE OUT
		return true;
	default:   return false;
	}
}

static inline int cdb_len_for_opcode(u8 op)
{
	// Return canonical CDB sizes for commands this emulator already handles.
	switch (op) {
		// 6-byte CDBs
	case 0x00: /* TEST UNIT READY   */ return 6;
	case 0x03: /* REQUEST SENSE     */ return 6;
	case 0x08: /* READ (6)          */ return 6;
	case 0x0A: /* WRITE (6)         */ return 6;
	case 0x12: /* INQUIRY           */ return 6;
	case 0x1A: /* MODE SENSE (6)    */ return 6;
	case 0x1B: /* START STOP UNIT   */ return 6;
	case 0x1E: /* PREVENT/ALLOW     */ return 6;
	case 0x3E: /* READ LONG         */ return 6;

		// 10-byte CDBs
	case 0x25: /* READ CAPACITY(10) */ return 10;
	case 0x28: /* READ (10)         */ return 10;
	case 0x2A: /* WRITE (10)        */ return 10;
	case 0x2F: /* VERIFY (10)       */ return 10;
	case 0x35: /* SYNCHRONIZE CACHE */ return 10;
	case 0x5A: /* MODE SENSE (10)   */ return 10;

		// 12-byte CDBs (supported by this tree)
	case 0xA8: /* READ (12)         */ return 12;
	case 0xAA: /* WRITE (12)        */ return 12;
	}
	return -1; // unknown/unsupported
}

// some packet handling macros
#define EXTRACT_FIELD(arr,byte,start,num_bits) (((arr)[(byte)] >> (start)) & ((1 << (num_bits)) - 1))
#define get_packet_field(arr,b,s,n) (EXTRACT_FIELD((arr),(b),(s),(n)))
#define get_packet_byte(arr,b) (arr[(b)])
#define get_packet_word(arr,b) (((uint16_t)arr[(b)] << 8) | arr[(b)+1])

// This, too, is straight out of Bochs.
int read_sub_channel(uint8_t* buf, bool sub_q, bool msf, int start_track, int format, int alloc_length) {
	int ret_len = 4;

	buf[0] = 0;
	buf[1] = 0; // audio status not supported
	buf[2] = 0;
	buf[3] = 4;

	if (sub_q) { // !sub_q == header only
		if (format == 1) {
			buf[2] = 0;  // length (MSB -> LSB)
			buf[3] = 12; // 

			buf[4] = 1;  // format code = 1
			buf[5] = (0 << 4) | (0 << 0); // ADR | CONTROL
			buf[6] = 1;  // track number
			buf[7] = 1;  // index number
			if (msf) {
				// in TIME format
				uint32_t lba = /*BX_SELECTED_DRIVE(channel).cdrom.curr_lba*/ 0 + (2 * 75); // 150 = 2 second lead-in from start
				int mins = (lba / 75) / 60;
				int secs = (lba / 75) % 60;
				int frames = lba % 75;
				buf[8] = 0;  // Absolute CD Address (H = 0)
				buf[9] = mins;  // M field (0 -> 99)
				buf[10] = secs;  // S field (0 -> 59)
				buf[11] = frames;  // F field (0 -> 74)
				buf[12] = 0;  // Track Relative CD Address (H = 0)
				buf[13] = mins;  // M field (0 -> 99)
				buf[14] = secs;  // S field (0 -> 59)
				buf[15] = frames;  // F field (0 -> 74)
			}
			else {
				// LBA = (((M * 60) + S) * 75) + F - 150
				// in LBA format
				buf[8] = 0;  // Absolute CD Address (MSB -> LSB)
				buf[9] = 0;  //
				buf[10] = 0;  //
				buf[11] = 1;  //
				buf[12] = 0;  // Track Relative CD Address (MSB -> LSB)
				buf[13] = 0;  //
				buf[14] = 0;  //
				buf[15] = 1;  //
			}
			ret_len = 16;

		}
		else if (format == 2) {
			buf[3] = 20;
			buf[4] = 2;
			buf[8] = 0; // no MCN
			memset(&buf[9], 0, 13);
			buf[22] = 0; // zero
			buf[23] = 0; // AFRAME (0 -> 4Ah)
			ret_len = 24;

		}
		else if (format == 3) {
			buf[3] = 20;
			buf[4] = 3;
			buf[5] = (1 << 4) | (4 << 0); // 0x14
			buf[6] = 1;
			buf[7] = 0; // reserved
			buf[8] = 0; // no ISRC
			memset(&buf[9], 0, 12);
			buf[21] = 0; // zero
			buf[22] = 0; // AFRAME (0 -> 4Ah)
			buf[23] = 0; // reserved
			ret_len = 24;
		}
		else {
			ret_len = 0;
		}
	}

	return ret_len;
}

/**
 * \brief Emit one MODE SENSE mode page at offset q, returning the offset just
 *        past it.
 *
 * Used for both single-page and page 0x3F ("return all supported pages")
 * requests. The complete page is initialized independently of allocation
 * length, which may only allow the initiator to read the mode header.
 **/
int CDisk::mode_sense_page(int page, bool changeable, int q)
{
	u8* d = state.scsi.dati.data;
	int len;

	switch (page)
	{
	case SCSIMP_READ_WRITE_ERRREC: len = 10;   break;
	case SCSIMP_FORMAT_PARAMS:     len = 22;   break;
	case SCSIMP_RIGID_GEOMETRY:    len = 22;   break;
	case SCSIMP_FLEX_PARAMS:       len = 0x1e; break;
	case SCSIMP_CACHING:           len = 0x12; break;
	case SCSIMP_INFO_EXCEPTIONS:   len = 0x0a; break;
	case SCSIMP_CDROM_CAP:         len = 0x14; break;
	default:                       return q;   // unknown page: emit nothing
	}

	for (int i = 0; i < len + 2; i++)
		d[q + i] = 0;
	d[q + 0] = (u8)page;
	d[q + 1] = (u8)len;

	if (!changeable)
	{
		switch (page)
		{
		case SCSIMP_FORMAT_PARAMS:
			d[q + 11] = (u8)get_sectors();
			d[q + 12] = (u8)(get_block_size() >> 8) & 255;
			d[q + 13] = (u8)(get_block_size() >> 0) & 255;
			break;

		case SCSIMP_RIGID_GEOMETRY:
			d[q + 2] = (u8)(get_cylinders() >> 16) & 255;
			d[q + 3] = (u8)(get_cylinders() >> 8) & 255;
			d[q + 4] = (u8)get_cylinders() & 255;
			d[q + 5] = (u8)get_heads();
			d[q + 20] = (7200 >> 8) & 255;
			d[q + 21] = 7200 & 255;
			break;

		case SCSIMP_CACHING:
			d[q + 2] = 0x0a;
			break;

		case SCSIMP_FLEX_PARAMS:
			d[q + 2] = (5000 >> 8) & 255;
			d[q + 3] = 5000 & 255;
			d[q + 4] = (u8)get_heads();
			d[q + 5] = (u8)get_sectors();
			d[q + 6] = (u8)(get_block_size() >> 8) & 255;
			d[q + 7] = (u8)get_block_size();
			d[q + 8] = (u8)(get_cylinders() >> 8) & 255;
			d[q + 9] = (u8)get_cylinders();
			d[q + 28] = (7200 >> 8) & 255;
			d[q + 29] = 7200 & 255;
			break;

		case SCSIMP_CDROM_CAP:
			d[q + 2] = 0x03;
			d[q + 6] = state.scsi.locked ? 0x23 : 0x21;
			d[q + 8] = (u8)(2800 >> 8);
			d[q + 9] = (u8)(2800 >> 0);
			d[q + 12] = (u8)(64 >> 8);
			d[q + 13] = (u8)(64 >> 0);
			d[q + 14] = (u8)(2800 >> 8);
			d[q + 15] = (u8)(2800 >> 0);
			break;

		default:
			break;
		}
	}

	return q + len + 2;
}

/**
 * \brief Handle a SCSI command.
 *
 * Called when a SCSI command has been received. We parse
 * the command, and set up the state for the data in or
 * data out phases.
 *
 * If a data out phase is required, we return the value 2
 * to indicate this. In that case, do_scsi_command will be
 * called again once the data out has been received from
 * the initiator.
 **/
int CDisk::do_scsi_command()
{
	unsigned int  retlen = 0;
	int           q;
	int           pagecode;
	u32           ofs = 0;

#if defined(DEBUG_SCSI)
	printf("%s: %d-byte command ", devid_string, state.scsi.cmd.written);
	for (unsigned int x = 0; x < state.scsi.cmd.written; x++)
		printf("%02x ", state.scsi.cmd.data[x]);
	printf("\n");
#endif
	if (state.scsi.cmd.written < 1)
		return 0;

	/* INQUIRY and REQUEST SENSE do not consume a pending reset notification. */
	if ((state.scsi.media_changed == SCSI_MEDIA_STATE_RESET ||
		state.scsi.media_changed == SCSI_MEDIA_STATE_RESET_REMOVED ||
		state.scsi.media_changed == SCSI_MEDIA_STATE_RESET_CHANGED) &&
		state.scsi.cmd.data[0] != SCSICMD_INQUIRY &&
		state.scsi.cmd.data[0] != SCSICMD_REQUEST_SENSE)
	{
		if (state.scsi.media_changed == SCSI_MEDIA_STATE_RESET_REMOVED)
			state.scsi.media_changed = SCSI_MEDIA_STATE_REMOVED;
		else if (state.scsi.media_changed == SCSI_MEDIA_STATE_RESET_CHANGED)
			state.scsi.media_changed = SCSI_MEDIA_STATE_CHANGED;
		else
			state.scsi.media_changed = SCSI_MEDIA_STATE_STABLE;
		do_scsi_error(SCSI_RESET);
		return 0;
	}

	if (cdrom() && state.scsi.media_changed == SCSI_MEDIA_STATE_CHANGED &&
		state.scsi.cmd.data[0] != SCSICMD_INQUIRY &&
		state.scsi.cmd.data[0] != SCSICMD_REQUEST_SENSE)
	{
		state.scsi.media_changed = SCSI_MEDIA_STATE_STABLE;
		do_scsi_error(SCSI_MEDIA_CHANGE);
		return 0;
	}

	if (state.scsi.cmd.data[1] & 0xe0)
	{
#if defined(DEBUG_SCSI)
		printf("%s: LUN selected...\n", devid_string);
#endif
		state.scsi.lun_selected = true;
	}

	if (state.scsi.lun_selected && state.scsi.cmd.data[0] != SCSICMD_INQUIRY
		&& state.scsi.cmd.data[0] != SCSICMD_REQUEST_SENSE)
	{
		do_scsi_error(SCSI_INVALID_FIELD, state.scsi.cmd.data[1] >> 5);
		return 0;
	}

	// Keep discovery and diagnostics available while the drive has failed.
	// This is also checked when WRITE returns from its DATA OUT phase, so
	// injection during a transfer cannot commit that command to the image.
	if (is_offline() && state.scsi.cmd.data[0] != SCSICMD_INQUIRY &&
		state.scsi.cmd.data[0] != SCSICMD_REQUEST_SENSE)
	{
		state.scsi.dati.available = state.scsi.dati.read = 0;
		do_scsi_error(SCSI_OFFLINE);
		return 0;
	}

	switch (state.scsi.cmd.data[0])
	{
	case SCSICMD_TEST_UNIT_READY:
#if defined(DEBUG_SCSI)
		printf("%s: TEST UNIT READY.\n", devid_string);
#endif

		// The offline gate above applies to fixed and removable drives.
		if (cdrom()) {
			if (!media_present()) {
				do_scsi_error(SCSI_MEDIA_REMOVED);
				break;
			}
			if (state.scsi.media_changed == SCSI_MEDIA_STATE_REMOVED) {
				do_scsi_error(SCSI_MEDIA_REMOVED);
				state.scsi.media_changed = SCSI_MEDIA_STATE_CHANGED;
				break;
			}
			else if (state.scsi.media_changed == SCSI_MEDIA_STATE_CHANGED) {
				do_scsi_error(SCSI_MEDIA_CHANGE);
				state.scsi.media_changed = SCSI_MEDIA_STATE_STABLE;
				break;
			}
		}
		do_scsi_error(SCSI_OK);
		break;
	
	case SCSICMD_READ_SUBCHANNEL:
	{
		if (cdrom() && !media_present()) {
			do_scsi_error(SCSI_MEDIA_REMOVED);
			break;
		}
		bool msf = get_packet_field(state.scsi.cmd.data, 1, 1, 1);
		bool sub_q = get_packet_field(state.scsi.cmd.data, 2, 6, 1);
		uint8_t data_format = get_packet_byte(state.scsi.cmd.data, 3);
		uint8_t track_number = get_packet_byte(state.scsi.cmd.data, 6);
		uint16_t alloc_length = get_packet_word(state.scsi.cmd.data, 7);
		{
			int ret_len = read_sub_channel(state.scsi.dati.data, sub_q, msf, track_number, data_format, alloc_length);
			if (ret_len == 0)
			{
				do_scsi_error(SCSI_ILL_CMD);
				break;
			}
			state.scsi.dati.available = ret_len;
			state.scsi.dati.read = 0;
			do_scsi_error(SCSI_OK);
		}
		break;
	}

	case SCSICMD_GET_EVENT_STATUS_NOTIFICATION:
	{
		if (!cdrom()) {
			do_scsi_error(SCSI_ILL_CMD);
			break;
		}
		// Straight copied out of Bochs.
		bool polled = (state.scsi.cmd.data[1] & (1 << 0)) > 0;
		int event_length, request = state.scsi.cmd.data[4];
		uint16_t alloc_length = get_be16(state.scsi.cmd.data + 7);
		bool inserted = media_present();
		if (polled) {
			// we currently only support the MEDIA event (bit 4)
			if (request == (1 << 4)) {
				state.scsi.dati.data[0] = 0;
				state.scsi.dati.data[1] = 4;  // MEDIA event is 4 bytes long
				state.scsi.dati.data[2] = (0 << 7) | 4;  // 4 = MEDIA event
				state.scsi.dati.data[3] = (1 << 4);  // we only support the MEDIA event (bit 4)
				state.scsi.dati.data[4] =
					(!state.scsi.media_changed) ? 0 : // Event code: 0 = no change
					(inserted) ? 4 : 3;      // Event code: 4 = media changed, 3 = removed
				state.scsi.dati.data[5] =
					(inserted) ? (1 << 1) : 0; // Media Status (bit 1 = Media Present)
				state.scsi.dati.data[6] = 0;
				state.scsi.dati.data[7] = 0;
				event_length = (alloc_length <= 4) ? 4 : 8;
			}
			else {
				state.scsi.dati.data[0] = 0;
				state.scsi.dati.data[1] = 0;
				state.scsi.dati.data[2] = (1 << 7) | (uint8_t)request;
				state.scsi.dati.data[3] = (1 << 4);  // we only support the MEDIA event (bit 4)
				event_length = 4;
			}
			state.scsi.dati.available = event_length;
			state.scsi.dati.read = 0;
			do_scsi_error(SCSI_OK);
		} else {
			do_scsi_error(SCSI_INVALID_FIELD);
		}
		break;
	}

	case SCSICMD_REQUEST_SENSE:
#if defined(DEBUG_SCSI)
		printf("%s: REQUEST SENSE.\n", devid_string);
#endif
		retlen = state.scsi.cmd.data[4];

		//    FAILURE("Sense requested");
		if (!state.scsi.sense.available)
		{
#if defined(DEBUG_SCSI)
			printf("%s: NO SENSE.\n", devid_string);
#endif
			state.scsi.sense.data[0] = 0xf0;    // error code
			state.scsi.sense.data[1] = 0x00;    // segment number
			state.scsi.sense.data[2] = 0x00;    // sense key: no sense
			state.scsi.sense.data[3] = 0x00;    // info
			state.scsi.sense.data[4] = 0x00;
			state.scsi.sense.data[5] = 0x00;
			state.scsi.sense.data[6] = 0x00;
			state.scsi.sense.data[7] = 10;      // additional sense length
			state.scsi.sense.data[8] = 0x00;    // command specific
			state.scsi.sense.data[9] = 0x00;
			state.scsi.sense.data[10] = 0x00;
			state.scsi.sense.data[11] = 0x00;
			state.scsi.sense.data[12] = 0x00;   // additional sense code: no additional sense
			state.scsi.sense.data[13] = 0x00;   // additional qualifier
			state.scsi.sense.data[14] = 0x00;   // FRU code
			state.scsi.sense.data[15] = 0x00;   // sense key specific
			state.scsi.sense.data[16] = 0x00;
			state.scsi.sense.data[17] = 0x00;
			state.scsi.sense.available = 18;
		}

#if defined(DEBUG_SCSI)
		printf("%s: Returning data: ", devid_string);
		for (unsigned int x1 = 0; x1 < state.scsi.sense.available; x1++)
			printf("%02x ", state.scsi.sense.data[x1]);
		printf("\n");
#endif
		state.scsi.dati.read = 0;
		state.scsi.dati.available = retlen;
		memcpy(state.scsi.dati.data, state.scsi.sense.data,
			state.scsi.sense.available);
		for (unsigned int x2 = state.scsi.sense.available; x2 < retlen; x2++)
			state.scsi.dati.data[x2] = 0;

		if (state.scsi.sense.data[2] == 0x06) {
			state.scsi.sense.data[2] = 0x00;
		}

		do_scsi_error(SCSI_OK);
		break;

	case SCSICMD_INQUIRY:
	{
#if defined(DEBUG_SCSI)
		printf("%s: INQUIRY.\n", devid_string);
#endif
		if ((state.scsi.cmd.data[1] & 0x1e) != 0x00)
		{
			FAILURE_2(NotImplemented,
				"%s: Don't know how to handle INQUIRY with cmd[1]=0x%02x.\n",
				devid_string, state.scsi.cmd.data[1]);
			break;
		}

		u8  qual_dev = state.scsi.lun_selected ? 0x7F : (cdrom() ? 0x05 : 0x00);

		retlen = state.scsi.cmd.data[4];
		state.scsi.dati.data[0] = qual_dev; // device type
		if (state.scsi.cmd.data[1] & 0x01)
		{

			// Vital Product Data
			switch (state.scsi.cmd.data[2])
			{
			case 0x00:

				// Page 0 is basically a list of page codes supported, so if
				// any others are added, make sure to insert them in the proper
				// place and increase the page length.
				state.scsi.dati.data[1] = 0x00; // page code 0
				state.scsi.dati.data[2] = 0x00; // reserved
				state.scsi.dati.data[3] = 0x02; // page length
				state.scsi.dati.data[4] = 0x00; // page 0 is supported.
				state.scsi.dati.data[5] = 0x80; // page 0x80 is supported.
				break;

			case 0x80:
				char serial_number2[20];
				sprintf(serial_number2, "SRL%04x", scsi_initiator_id[0] * 0x0101);

				// unit serial number page
				state.scsi.dati.data[1] = 0x80; // page code: 0x80
				state.scsi.dati.data[2] = 0x00; // reserved
				state.scsi.dati.data[3] = (u8)strlen(serial_number2);
				memcpy(&state.scsi.dati.data[4], serial_number2, strlen(serial_number2));
				break;

			default:
#if 1
				FAILURE_1(NotImplemented,
					"Don't know format for vital product data page %02x!!\n", state.scsi.cmd.data[2]);
#else
				state.scsi.dati.data[1] = state.scsi.cmd.data[2]; // page code
				state.scsi.dati.data[2] = 0x00; // reserved
#endif
			}
		}
		else
		{

			//  Return values:
			if (retlen < 36)
			{
				printf("%s: SCSI inquiry len=%i, <36!\n", devid_string, retlen);
				retlen = 36;
			}

			state.scsi.dati.data[1] = 0;      // not removable;
			state.scsi.dati.data[2] = 0x02;   // ANSI scsi 2
			state.scsi.dati.data[3] = 0x02;   // response format
			state.scsi.dati.data[4] = 32;     // additional length
			state.scsi.dati.data[5] = 0;      // reserved
			state.scsi.dati.data[6] = 0x04;   // reserved
			state.scsi.dati.data[7] = 0x60;   // capabilities

			//                        vendor  model           rev.
			memcpy(&(state.scsi.dati.data[8]), "DEC     RZ58     (C) DEC2000", 28);

			//  Some data is different for CD-ROM drives:
			if (cdrom())
			{
				state.scsi.dati.data[1] = 0x80; //  0x80 = removable

				//                           vendor  model           rev.
				memcpy(&(state.scsi.dati.data[8]), "DEC     RRD42   (C) DEC 4.5d", 28);
			}
		}

		state.scsi.dati.read = 0;
		state.scsi.dati.available = retlen;

#if defined(DEBUG_SCSI)
		printf("%s: Returning data: ", devid_string);
		for (unsigned int x1 = 0; x1 < 36; x1++)
			printf("%02x ", state.scsi.dati.data[x1]);
		printf("\n");
#endif
		do_scsi_error(SCSI_OK);
	}
	break;

	// Also from Bochs.
	case 0x46: // get configuration (mmc4r05a.pdf, page 286) (pages are physical pdf pages, not page numbers listed on specific page)
	{
		//                Bit8u rt = (state.scsi.dati.data[1] & (3<<0));
		if (!cdrom())
		{
			do_scsi_error(SCSI_ILL_CMD);
			break;
		}
		uint16_t start_feature = get_be16(state.scsi.cmd.data + 2);
		uint16_t alloc_length = get_be16(state.scsi.cmd.data + 7);
		uint8_t *feature_ptr = state.scsi.dati.data;
		bool inserted = media_present();

		// The controller buffer is guaranteed to be at least 2048 bytes.
		// The largest return for this command is guaranteed to be less than 1024 bytes.
		// Therefore, we just build the return as if requested all bytes,
		//  then only return up to 'alloc_length' bytes.

		if (alloc_length >= 8)
		{
			// create a header (page 287)
			// state.scsi.dati.data[0] = 0;  // length is calculated below
			// state.scsi.dati.data[1] = 0;
			// state.scsi.dati.data[2] = 0;
			// state.scsi.dati.data[3] = 0;
			state.scsi.dati.data[4] = 0; // reserved
			state.scsi.dati.data[5] = 0; // reserved
			state.scsi.dati.data[6] = 0;
			state.scsi.dati.data[7] = inserted ? 8 : 0; // current profile
			feature_ptr += 8;

			// page: 238
			// Profile 8 requires feature numbers, 0x0, 0x1, 0x2, 0x3, 0x10, 0x1E, 0x100, 0x105

			// profile list (feature 0x0000) (mmc4r05a.pdf, page 174)
			if (start_feature == 0x0000)
			{
				feature_ptr[0] = 0x00; // Feature Code 0x000
				feature_ptr[1] = 0x00;
				feature_ptr[2] = (0 << 6) | (0 << 2) | (1 << 1) | (inserted << 0); // version 1, persistent = 1, current = 1 if inserted
				feature_ptr[3] = 4;												   // additional length = (1 * 4)
				feature_ptr[4] = 0x00;											   // profile 0x0008
				feature_ptr[5] = 0x08;											   //
				feature_ptr[6] = (0 << 1) | (inserted << 0);					   // reserved, Current = 1 if inserted
				feature_ptr[7] = 0;												   // reserved
				feature_ptr += 8;
			}

			// core feature (feature 0x0001) (mmc4r05a.pdf, page 174)
			if (start_feature <= 0x0001)
			{
				feature_ptr[0] = 0x00; // Feature Code 0x001
				feature_ptr[1] = 0x01;
				feature_ptr[2] = (0 << 6) | (1 << 2) | (1 << 1) | (1 << 0); // version 1, persistent = 1, current = 1
				feature_ptr[3] = 8;											// additional length = 8
				feature_ptr[4] = 0;											// physical interface standard:
				feature_ptr[5] = 0;											//   2 = ATAPI
				feature_ptr[6] = 0;											//
				feature_ptr[7] = 2;											//
				feature_ptr[8] = (0 << 1) | (0 << 0);						// reserved, DBE = 0
				feature_ptr[9] = 0;											//
				feature_ptr[10] = 0;										//
				feature_ptr[11] = 0;										//
				feature_ptr += 12;
			}

			// morphing feature (feature 0x0002) (mmc4r05a.pdf, page 178)
			if (start_feature <= 0x0002)
			{
				feature_ptr[0] = 0x00; // Feature Code 0x002
				feature_ptr[1] = 0x02;
				feature_ptr[2] = (0 << 6) | (1 << 2) | (1 << 1) | (1 << 0); // version 1, persistent = 1, current = 1
				feature_ptr[3] = 4;											// additional length = 4
				feature_ptr[4] = (0 << 1) | (0 << 0);						// OCEvent = 0 (see page 178), ASYNC = 0 (0 = polling of EVENT STATUS NOTIFICATION)
				feature_ptr[5] = 0;											//
				feature_ptr[6] = 0;											//
				feature_ptr[7] = 0;											//
				feature_ptr += 8;
			}

			// Removable Medium feature (feature 0x0003) (mmc4r05a.pdf, page 179)
			if (start_feature <= 0x0003)
			{
				feature_ptr[0] = 0x00; // Feature Code 0x003
				feature_ptr[1] = 0x03;
				feature_ptr[2] = (0 << 6) | (0 << 2) | (1 << 1) | (1 << 0); // version 0, persistent = 1, current = 1
				feature_ptr[3] = 4;											// additional length = 4
				feature_ptr[4] = (1 << 5)									// Loading mechanism: tray
								 | (1 << 3)									// Eject mechanism present
								 | (1 << 2)									// No Pvnt Jumper
								 | (1 << 0);								// Locking supported
				feature_ptr[5] = 0;											//
				feature_ptr[6] = 0;											//
				feature_ptr[7] = 0;											//
				feature_ptr += 8;
			}

			// Random Readable feature (feature 0x0010) (mmc4r05a.pdf, page 182)
			if (start_feature <= 0x0010)
			{
				feature_ptr[0] = 0x00; // Feature Code 0x010
				feature_ptr[1] = 0x10;
				feature_ptr[2] = (0 << 6) | (0 << 2) | (1 << 1) |
					(inserted << 0); // version 0, persistent = 1
				feature_ptr[3] = 8;											// additional length = 8
				feature_ptr[4] = 0x00;										// Logical Block Size:
				feature_ptr[5] = 0x00;										//   2048 (0x800)
				feature_ptr[6] = 0x08;										//
				feature_ptr[7] = 0x00;										//
				feature_ptr[8] = (16 >> 8);				// blocking
				feature_ptr[9] = (16 & 0xFF);
				feature_ptr[10] = (0 << 0); // PP = 0
				feature_ptr[11] = 0;
				feature_ptr += 12;
			}

			// CD Read feature (feature 0x001E) (mmc4r05a.pdf, page 185)
			if (start_feature <= 0x001E)
			{
				feature_ptr[0] = 0x00; // Feature Code 0x01E
				feature_ptr[1] = 0x1E;
				feature_ptr[2] = (0 << 6) | (2 << 2) | (1 << 1) |
					(inserted << 0); // version 2, persistent = 1
				feature_ptr[3] = 4;											// additional length = 4
				feature_ptr[4] = (0 << 7) | (0 << 1) | (0 << 0);			// DAP = 0, C2 Flags = 0, CD-Text = 0
				feature_ptr[5] = 0;
				feature_ptr[6] = 0;
				feature_ptr[7] = 0;
				feature_ptr += 8;
			}

			// Timeout feature (feature 0x0105) (mmc4r05a.pdf, page 222)
			if (start_feature <= 0x0105)
			{
				feature_ptr[0] = 0x01; // Feature Code 0x105
				feature_ptr[1] = 0x05;
				feature_ptr[2] = (0 << 6) | (1 << 2) | (1 << 1) | (1 << 0); // version 1, persistent = 1, current = 1
				feature_ptr[3] = 4;											// additional length = 4
				feature_ptr[4] = (0 << 0);									// Group 3 = 0
				feature_ptr[5] = 0;
				feature_ptr[6] = 0;
				feature_ptr[7] = 0;
				feature_ptr += 8;
			}

			// update the return length
			// The Data Length field indicates the amount of data available given a sufficient allocation length following this field.
			// This length shall not be truncated due to an insufficient Allocation Length.
			uint16_t return_length = (uint16_t)(feature_ptr - state.scsi.dati.data) - 4;
			state.scsi.dati.data[0] = 0;
			state.scsi.dati.data[1] = 0;
			state.scsi.dati.data[2] = (return_length >> 8);
			state.scsi.dati.data[3] = (return_length & 0xFF);

			/* Bochs used this because of ReactOS boot problems, but this should be in fact correct. */
			state.scsi.dati.available = return_length + 4;
			state.scsi.dati.read = 0;
		}
		else
		{
			do_scsi_error(SCSI_INVALID_FIELD);
		}
	}
	break;

	case SCSICMD_MODE_SENSE:
	case SCSICMD_MODE_SENSE_10:
#if defined(DEBUG_SCSI)
		printf("%s: MODE SENSE.\n", devid_string);
#endif
		{
			const int num_blk_desc = (state.scsi.cmd.data[1] & 0x08) ? 0 : 1;

			if (state.scsi.cmd.data[0] == SCSICMD_MODE_SENSE)
			{
				q = 4;
				retlen = state.scsi.cmd.data[4];
				state.scsi.dati.data[0] = 0; // filled with the complete response length below
				state.scsi.dati.data[1] = cdrom() ? 0x01 : 0x00;  // medium type (120 mm data for CD-ROM)
				state.scsi.dati.data[2] = 0x00; // device specific parameter
				state.scsi.dati.data[3] = 8 * num_blk_desc;       // block descriptor length: 1 page (?)
			}
			else
			{
				q = 8;
				retlen = state.scsi.cmd.data[7] * 256 + state.scsi.cmd.data[8];
				state.scsi.dati.data[0] = 0; // mode data length, filled below
				state.scsi.dati.data[1] = 0;
				state.scsi.dati.data[2] = cdrom() ? 0x01 : 0x00;  // medium type (120 mm data for CD-ROM)
				state.scsi.dati.data[3] = 0x00; // device specific parameter
				state.scsi.dati.data[4] = 0x00; // reserved
				state.scsi.dati.data[5] = 0x00; // reserved
				state.scsi.dati.data[6] = (u8)((8 * num_blk_desc) >> 8); //  block descriptor length: 1 page (?)
				state.scsi.dati.data[7] = (u8)(8 * num_blk_desc);
			}

			if ((state.scsi.cmd.data[2] & 0xc0) > 0x40)
			{
				FAILURE_2(NotImplemented, "%s: mode sense, cmd[2] = 0x%02x.\n",
					devid_string, state.scsi.cmd.data[2]);
			}

			bool  changeable = ((state.scsi.cmd.data[2] & 0xc0) == 0x40);

			//  Return data:
			if (retlen > DATI_BUFSZ)
			{
				printf("%s: read too big (%d)\n", devid_string, retlen);
				do_scsi_error(SCSI_TOO_BIG);
				break;
			}

			state.scsi.dati.read = 0;
			state.scsi.dati.available = 0;
			pagecode = state.scsi.cmd.data[2] & 0x3f;

			if (num_blk_desc)
			{
				state.scsi.dati.data[q++] = 0x00;   // density code
				state.scsi.dati.data[q++] = 0;      // all remaining blocks
				state.scsi.dati.data[q++] = 0;
				state.scsi.dati.data[q++] = 0;
				state.scsi.dati.data[q++] = 0x00;   // reserved
				state.scsi.dati.data[q++] = (u8)(get_block_size() >> 16) & 255;
				state.scsi.dati.data[q++] = (u8)(get_block_size() >> 8) & 255;
				state.scsi.dati.data[q++] = (u8)(get_block_size() >> 0) & 255;
			}

			// Page 0x3F = return every mode page the device supports (SPC).
			// Concatenate the implemented pages after the header + block
			// descriptor, using the same length/transfer handling as one page.
			if (pagecode == 0x3f)
			{
				q = mode_sense_page(SCSIMP_READ_WRITE_ERRREC, changeable, q);
				if (!cdrom())
				{
					q = mode_sense_page(SCSIMP_FORMAT_PARAMS, changeable, q);
					q = mode_sense_page(SCSIMP_RIGID_GEOMETRY, changeable, q);
				}
				q = mode_sense_page(SCSIMP_CACHING, changeable, q);
				q = mode_sense_page(SCSIMP_INFO_EXCEPTIONS, changeable, q);
				if (cdrom())
					q = mode_sense_page(SCSIMP_CDROM_CAP, changeable, q);
			}
			else if (pagecode != SCSIMP_VENDOR)
			{
				if (pagecode == SCSIMP_FLEX_PARAMS && cdrom())
				{
					FAILURE_1(NotImplemented,
						"%s: CD-ROM write parameter page not implemented.\n", devid_string);
				}
				const int end = mode_sense_page(pagecode, changeable, q);
				if (end == q)
				{
					// Optional pages are probed by several guest drivers.
					printf("%s: MODE_SENSE page 0x%02x unsupported -> INVALID FIELD IN CDB\n",
						devid_string, pagecode);
					do_scsi_error(SCSI_INVALID_FIELD);
					return 0;
				}
				q = end;
			}

			// Mode data length describes the full response, excluding its own
			// one- or two-byte field. Allocation length only limits the transfer.
			// In particular, allocation 255 must not turn a 24-byte response
			// into length 255 (256 total), which breaks MODE SELECT(6) callers.
			if (state.scsi.cmd.data[0] == SCSICMD_MODE_SENSE)
				state.scsi.dati.data[0] = (u8)(q - 1);
			else
			{
				state.scsi.dati.data[0] = (u8)((q - 2) >> 8);
				state.scsi.dati.data[1] = (u8)(q - 2);
			}
			state.scsi.dati.available = retlen < (unsigned int)q ? retlen : (unsigned int)q;
			do_scsi_error(SCSI_OK);

#if defined(DEBUG_SCSI)
			printf("%s: Returning data: ", devid_string);
			for (unsigned int x1 = 0; x1 < state.scsi.dati.available; x1++)
				printf("%02x ", state.scsi.dati.data[x1]);
			printf("\n");
#endif
		}
		break;

		// NetBSD wants this.... who else wants it I wonder? We'll give it to 'em!
	case SCSICMD_MAINTENANCE_IN:  // REPORT SUPPORTED OPERATION CODES lives here (SA=0x0C)
	{
#if defined(DEBUG_SCSI)
		printf("%s: MAINTENANCE IN.\n", devid_string);
#endif
		const u8  sa = state.scsi.cmd.data[1];
		const u8  rctd = (state.scsi.cmd.data[2] >> 7) & 1; // request Command Timeouts Desc
		const u8  ropt = (state.scsi.cmd.data[2] & 0x07);   // reporting options
		const u8  req_op = state.scsi.cmd.data[3];          // requested opcode
		const u16 req_sa = (u16(state.scsi.cmd.data[4]) << 8) | state.scsi.cmd.data[5];
		const u32 alloc = (u32(state.scsi.cmd.data[6]) << 24) |
			(u32(state.scsi.cmd.data[7]) << 16) |
			(u32(state.scsi.cmd.data[8]) << 8) |
			(u32(state.scsi.cmd.data[9]) << 0);

		if (sa != 0x0C) {                 // Only RSOC is implemented
			do_scsi_error(SCSI_ILL_CMD);  // invalid operation code (service action)
			break;

		}

		// Convenience shorthands
		u8* p = state.scsi.dati.data;
		auto finish_ok = [&](u32 nbytes) {
			state.scsi.dati.read = 0;
			state.scsi.dati.available = (alloc < nbytes) ? alloc : nbytes;
			do_scsi_error(SCSI_OK);
			};

		// ---- Reporting options ----
		if (ropt == 0x00) {
			// ---- all_commands ----
				// Small, accurate list of commands we actually implement.
			struct Desc { u8 op; u16 cdb_len; u16 svc; };
			static const Desc kCmds[] = {
			{0x00,  6, 0}, // TEST UNIT READY
			{0x03,  6, 0}, // REQUEST SENSE
			{0x12,  6, 0}, // INQUIRY
			{0x1A,  6, 0}, // MODE SENSE(6)
			{0x1B,  6, 0}, // START STOP UNIT
			{0x1E,  6, 0}, // PREVENT ALLOW MEDIUM REMOVAL
			{0x25, 10, 0}, // READ CAPACITY(10)
			{0x28, 10, 0}, // READ(10)
			{0x2A, 10, 0}, // WRITE(10)
			{0x2F, 10, 0}, // VERIFY(10)
			{0x35, 10, 0}, // SYNCHRONIZE CACHE(10)
			{0x5A, 10, 0}, // MODE SENSE(10)
			{0xA8, 12, 0}, // READ(12)
			{0xAA, 12, 0}, // WRITE(12)
			{0x3E,  6, 0}, // READ LONG
			};
			const u32 n = (u32)(sizeof(kCmds) / sizeof(kCmds[0]));
			const u32 desc_len = rctd ? (8 + 12) : 8;   // Table 166 (+ Table 171 when RCTD=1)
			const u32 payload = n * desc_len;

			// Header: 4-byte COMMAND DATA LENGTH (size of descriptors only)
			put_be32(p, payload); p += 4;

			for (u32 i = 0; i < n; ++i) {
				const Desc& d = kCmds[i];
				p[0] = d.op;  p[1] = 0;                 // opcode, reserved
				put_be16(p + 2, d.svc);                 // service action (0 for non-SA)
				p[4] = 0;
				// flags: [5]=reserved [4]=RWCDLP [3]=MLU [2]=CDLP [1]=CTDP [0]=SERVACTV
				p[5] = rctd ? 0x02 : 0x00;              // set CTDP when including descriptor
				put_be16(p + 6, d.cdb_len);             // CDB length
				p += 8;
				if (rctd) {
					// Command Timeouts descriptor (Table 171) - all zeros is fine
					put_be16(p + 0, 0x000A); // descriptor length (10h bytes after the length)
					p[2] = 0x00;             // reserved
					p[3] = 0x00;             // command specific
					memset(p + 4, 0x00, 8);  // nominal + recommended timeouts
					p += 12;

				}

			}
			finish_ok((u32)(p - state.scsi.dati.data));
			break;
		}

		if (ropt == 0x01) {
			// ---- one_command (non-SA only) ----
			if (opcode_has_service_actions(req_op)) {
				do_scsi_error(SCSI_INVALID_FIELD);  // spec mandates INVALID FIELD IN CDB here
				break;
			}
			const int cdb_len = cdb_len_for_opcode(req_op);

			// Byte 0..1: flags (RWCDLP=0) and CTDP/MLU/CDLP/SUPPORT
			p[0] = 0x00;              // RWCDLP=0
			p[1] = 0x00;              // CTDP=0, MLU=0, CDLP=0
			p[1] |= 0x03;             // SUPPORT=011b ("supported") by default
			if (cdb_len < 0) {
				p[1] = (p[1] & ~0x07) | 0x01; // SUPPORT=001b ("not supported")
				finish_ok(2);                 // bytes after 1 are undefined when not supported
				break;

			}
			put_be16(p + 2, (u16)cdb_len);    // CDB SIZE
			// CDB USAGE DATA: set opcode, everything else 0 (conservative)
			memset(p + 4, 0x00, (size_t)cdb_len);
			p[4] = req_op;                    // first byte is the opcode
			p += 4 + cdb_len;

			// (Optional) include a zeroed timeouts descriptor when requested
			if (rctd) {
				p[-(int)0] = p[-(int)0]; // no-op to keep style
				put_be16(p + 0, 0x000A);
				p[2] = 0x00; p[3] = 0x00;
				memset(p + 4, 0x00, 8);
				p += 12;
				// Reflect presence via CTDP bit
				state.scsi.dati.data[1] |= 0x80; // CTDP=1

			}
			finish_ok((u32)(p - state.scsi.dati.data));
			break;
		}

		if (ropt == 0x02) {
			// ---- one_service_action (opcode MUST have SA) ----
			if (!opcode_has_service_actions(req_op)) {
				do_scsi_error(SCSI_INVALID_FIELD);  // opcode without SAs -> INVALID FIELD
				break;
			}
			// We don’t actually implement any SA-coded commands -> say "not supported"
			p[0] = 0x00;
			p[1] = (0x01);              // SUPPORT=001b (not supported)
			finish_ok(2);
			break;
		}

		if (ropt == 0x03) {
			// ---- either: SA if present, otherwise no-SA with SA==0 ----
			if (opcode_has_service_actions(req_op)) {
				// We don't support any SA variants -> NOT SUPPORTED (no CHECK CONDITION here)
				p[0] = 0x00; p[1] = 0x01;     // SUPPORT=001b
				finish_ok(2);
				break;
			}
			else {
				// Treat as one_command for a non-SA opcode; REQUESTED SA must be 0
				if (req_sa != 0) {
					p[0] = 0x00; p[1] = 0x01; // NOT SUPPORTED (per 3.37.3 for 011b)
					finish_ok(2);
					break;
				}
				const int cdb_len = cdb_len_for_opcode(req_op);
				p[0] = 0x00; p[1] = 0x03;    // SUPPORT=011b (supported)
				if (cdb_len < 0) {
					p[1] = 0x01;             // not supported
					finish_ok(2);
					break;
				}
				put_be16(p + 2, (u16)cdb_len);
				memset(p + 4, 0x00, (size_t)cdb_len);
				p[4] = req_op;
				p += 4 + cdb_len;
				if (rctd) {
					put_be16(p + 0, 0x000A);
					p[2] = 0x00; p[3] = 0x00;
					memset(p + 4, 0x00, 8);
					p += 12;
					state.scsi.dati.data[1] |= 0x80; // CTDP=1
				}
				finish_ok((u32)(p - state.scsi.dati.data));
				break;
			}
		}

		// All other reporting options are reserved
		do_scsi_error(SCSI_INVALID_FIELD);
		break;
	}

	case SCSICMD_PREVENT_ALLOW_REMOVE:
		if (state.scsi.cmd.data[4] & 1)
		{
			state.scsi.locked = true;
			media_lock_changed(true);
#if defined(DEBUG_SCSI)
			printf("%s: PREVENT MEDIA REMOVAL.\n", devid_string);
#endif
		}
		else
		{
			state.scsi.locked = false;
			media_lock_changed(false);
#if defined(DEBUG_SCSI)
			printf("%s: ALLOW MEDIA REMOVAL.\n", devid_string);
#endif
		}

		do_scsi_error(SCSI_OK);
		break;

	case SCSICMD_START_STOP_UNIT:
	{
		const bool load_eject = (state.scsi.cmd.data[4] & 0x02) != 0;
		const bool start = (state.scsi.cmd.data[4] & 0x01) != 0;

		if (cdrom() && load_eject && !start)
		{
			if (state.scsi.locked)
			{
				do_scsi_error(SCSI_MEDIA_LOCKED);
				break;
			}
			if (!eject_media())
			{
				do_scsi_error(SCSI_HARDWARE_ERROR);
				break;
			}
			printf("%s: CD-ROM media eject complete (guest request).\n",
				devid_string);
		}

		do_scsi_error(SCSI_OK);
		break;
	}

	case SCSICMD_MODE_SELECT:
	case SCSICMD_MODE_SELECT_10:
	{
		const bool mode_select_10 = (state.scsi.cmd.data[0] == SCSICMD_MODE_SELECT_10);
		state.scsi.dato.expected = mode_select_10 ?
			((state.scsi.cmd.data[7] << 8) | state.scsi.cmd.data[8]) :
			state.scsi.cmd.data[4];

		if (state.scsi.dato.expected > DATO_BUFSZ)
		{
			printf("%s: mode select too big (%d)\n", devid_string, state.scsi.dato.expected);
			do_scsi_error(SCSI_TOO_BIG);
			break;
		}

		// get data out first...
		if (state.scsi.dato.written < state.scsi.dato.expected)
			return 2;

#if defined(DEBUG_SCSI)
		printf("%s: MODE SELECT%s.\n", devid_string, mode_select_10 ? " (10)" : "");
		printf("Data: ");
		for (unsigned int x = 0; x < state.scsi.dato.written; x++)
			printf("%02x ", state.scsi.dato.data[x]);
		printf("\n");
#endif

		// A zero-length parameter list is a successful no-op.
		const unsigned int length = state.scsi.dato.expected;
		if (length == 0)
		{
			do_scsi_error(SCSI_OK);
			break;
		}

		const u8* params = state.scsi.dato.data;
		const unsigned int header_size = mode_select_10 ? 8 : 4;
		if (length < header_size)
		{
			do_scsi_error(SCSI_PARAM_LENGTH);
			break;
		}
		const unsigned int descriptor_length = mode_select_10 ?
			((params[6] << 8) | params[7]) : params[3];
		if (descriptor_length > length - header_size)
		{
			do_scsi_error(SCSI_PARAM_LENGTH);
			break;
		}
		// Only a single short block descriptor is supported. Medium type is
		// ignored, as before; mode data length and reserved fields must be zero.
		if ((descriptor_length != 0 && descriptor_length != 8) ||
			params[0] != 0 || params[mode_select_10 ? 3 : 2] != 0 ||
			(mode_select_10 && (params[1] != 0 || params[4] != 0 || params[5] != 0)))
		{
			do_scsi_error(SCSI_INVALID_PARAM);
			break;
		}

		// MODE SELECT may carry pages after the block descriptor (e.g. a
		// 24-byte MODE SENSE(6) page 1 response). Validate the entire list
		// before changing block size, so a truncated page cannot partly apply it.
		// PF=0 uses a vendor-specific payload, not SCSI-2 mode pages.
		unsigned int pos = (state.scsi.cmd.data[1] & 0x10) ?
			header_size + descriptor_length : length;
		while (pos < length)
		{
			const unsigned int page_header = (params[pos] & 0x40) ? 4 : 2;
			if (length - pos < page_header)
				break;
			const unsigned int page_length = page_header == 4 ?
				((params[pos + 2] << 8) | params[pos + 3]) : params[pos + 1];
			if (page_length > length - pos - page_header)
				break;
			pos += page_header + page_length;
		}
		if (pos != length)
		{
			do_scsi_error(SCSI_PARAM_LENGTH);
			break;
		}

		bool block_size_updated = false;
		if (descriptor_length == 8)
		{
			const u8* descriptor = params + header_size;
			const unsigned int block_size = (descriptor[5] << 16) |
				(descriptor[6] << 8) | descriptor[7];
			if (descriptor[0] != 0 || descriptor[1] != 0 ||
				descriptor[2] != 0 || descriptor[3] != 0 || descriptor[4] != 0 ||
				block_size > DATI_BUFSZ || block_size > DATO_BUFSZ ||
				(byte_size > 0 && block_size > (u64)byte_size))
			{
				do_scsi_error(SCSI_INVALID_PARAM);
				break;
			}
			// A zero block length retains the current logical block size.
			if (block_size != 0)
			{
				set_block_size(block_size);
				block_size_updated = true;
			}
		}

#if defined(DEBUG_SCSI)
		if (block_size_updated)
			printf("%s: Block size set to %d.\n", devid_string, get_block_size());
#endif
		if (!block_size_updated)
		{
			unsigned int x;
			printf("%s: MODE SELECT%s ignored.\nCommand: ", devid_string,
				mode_select_10 ? " (10)" : "");
			for (x = 0; x < state.scsi.cmd.written; x++)
				printf("%02x ", state.scsi.cmd.data[x]);
			printf("\nData: ");
			for (x = 0; x < state.scsi.dato.written; x++)
				printf("%02x ", state.scsi.dato.data[x]);
			printf("\n");
		}

		// Mode-page settings are still ignored; the block descriptor is applied above.
		do_scsi_error(SCSI_OK);
		break;
	}

	case SCSIBLOCKCMD_SEEK:
	{
		if (cdrom() && !media_present()) {
			do_scsi_error(SCSI_MEDIA_REMOVED);
			break;
		}
		auto ofs = (state.scsi.cmd.data[2] << 24) + (state.scsi.cmd.data[3] << 16) + (state.scsi.cmd.data[4] << 8) + state.scsi.cmd.data[5];
		if (ofs >= get_lba_size()) {
			do_scsi_error(SCSI_LBA_RANGE);
			break;
		}
		if (!seek_block(ofs))
			do_scsi_error(is_offline() ? SCSI_OFFLINE : SCSI_HARDWARE_ERROR);
		else
			do_scsi_error(SCSI_OK);
		break;
	}

	case SCSIBLOCKCMD_READ_CAPACITY:
#if defined(DEBUG_SCSI)
		printf("%s: READ CAPACITY.\n", devid_string);
#endif
		if (cdrom() && !media_present())
		{
			do_scsi_error(SCSI_MEDIA_REMOVED);
			break;
		}
		if (state.scsi.cmd.data[8] & 1)
		{
			FAILURE_1(NotImplemented,
				"%s: Don't know how to handle READ CAPACITY with PMI bit set.\n",
				devid_string);
			break;
		}

		// READ CAPACITY returns the number of the last LBA (n-1);
		// not the number of LBA's (n)
		state.scsi.dati.data[0] = (u8)((get_lba_size() - 1) >> 24) & 255;
		state.scsi.dati.data[1] = (u8)((get_lba_size() - 1) >> 16) & 255;
		state.scsi.dati.data[2] = (u8)((get_lba_size() - 1) >> 8) & 255;
		state.scsi.dati.data[3] = (u8)((get_lba_size() - 1) >> 0) & 255;

		state.scsi.dati.data[4] = (u8)(get_block_size() >> 24) & 255;
		state.scsi.dati.data[5] = (u8)(get_block_size() >> 16) & 255;
		state.scsi.dati.data[6] = (u8)(get_block_size() >> 8) & 255;
		state.scsi.dati.data[7] = (u8)(get_block_size() >> 0) & 255;

		state.scsi.dati.read = 0;
		state.scsi.dati.available = 8;

#if defined(DEBUG_SCSI)
		printf("%s: Returning data: ", devid_string);
		for (unsigned int x1 = 0; x1 < 8; x1++)
			printf("%02x ", state.scsi.dati.data[x1]);
		printf("\n");
#endif
		do_scsi_error(SCSI_OK);
		break;

	case SCSICMD_VERIFY_10:
	{
#if defined(DEBUG_SCSI)
		printf("%s: VERIFY(10).\n", devid_string);
#endif
		if (cdrom() && !media_present())
		{
			do_scsi_error(SCSI_MEDIA_REMOVED);
			break;
		}
		// BYTCHK requests a DATA OUT compare buffer. The emulator can verify
		// media readability, but does not implement host-data comparison.
		if (state.scsi.cmd.data[1] & 0x02)
		{
			do_scsi_error(SCSI_INVALID_FIELD);
			break;
		}

		off_t_large verify_ofs =
			((off_t_large)state.scsi.cmd.data[2] << 24) |
			((off_t_large)state.scsi.cmd.data[3] << 16) |
			((off_t_large)state.scsi.cmd.data[4] << 8) |
			((off_t_large)state.scsi.cmd.data[5] << 0);
		retlen = (state.scsi.cmd.data[7] << 8) | state.scsi.cmd.data[8];

		if ((verify_ofs + retlen) > get_lba_size())
		{
			do_scsi_error(SCSI_LBA_RANGE);
			break;
		}

		do_scsi_error(SCSI_OK);
		break;
	}

	case SCSICMD_READ:
	case SCSICMD_READ_10:
	case SCSICMD_READ_12:
	case SCSICMD_READ_CD:
#if defined(DEBUG_SCSI)
		printf("%s: READ.\n", devid_string);
#endif
		if (cdrom() && !media_present())
		{
			do_scsi_error(SCSI_MEDIA_REMOVED);
			break;
		}

		//if (state.scsi.disconnect_priv)
		//{
		//  //printf("%s: Will disconnect before returning read data.\n", devid_string);
		//  state.scsi.will_disconnect = true;
		//}
		if (state.scsi.cmd.data[0] == SCSICMD_READ)
		{

			//  bits 4..0 of cmd[1], and cmd[2] and cmd[3]
			//  hold the logical block address.
			//
			//  cmd[4] holds the number of logical blocks
			//  to transfer. (Special case if the value is
			//  0, actually means 256.)
			ofs = ((state.scsi.cmd.data[1] & 0x1f) << 16) + (state.scsi.cmd.data[2] << 8) + state.scsi.cmd.data[3];
			retlen = state.scsi.cmd.data[4];
			if (retlen == 0)
				retlen = 256;
		}
		else if (state.scsi.cmd.data[0] == SCSICMD_READ_10)
		{

			//  cmd[2..5] hold the logical block address.
			//  cmd[7..8] holds the number of logical
			ofs = get_be32(state.scsi.cmd.data + 2);
			retlen = (state.scsi.cmd.data[7] << 8) + state.scsi.cmd.data[8];
		}
		else if (state.scsi.cmd.data[0] == SCSICMD_READ_12)
		{

			//  cmd[2..5] hold the logical block address.
			//  cmd[6..9] holds the number of logical
			ofs = get_be32(state.scsi.cmd.data + 2);
			retlen = get_be32(state.scsi.cmd.data + 6);
		}
		else if (state.scsi.cmd.data[0] == SCSICMD_READ_CD)
		{
			// MMC READ CD (0xBE) byte 9 selects which sector subfields to return:
			//   bit 7    SYNC          (12-byte sync)
			//   bits 6:5 HEADER CODE   (00=none, 01=Hdr, 10=SubHdr, 11=both)
			//   bit 4    USER DATA     (2048 bytes of user data)
			//   bit 3    EDC/ECC       (288 bytes EDC+ECC)
			//   bits 2:1 ERROR FIELD   (C2 error info)
			//   bit 0    reserved
			// Win2K's cdrom.sys sometimes issues this with byte 9 == 0x00 (no
			// fields explicitly requested) — most real drives treat that as the
			// default data-only read. We handle 0x00 (default) and 0x10 (user
			// data only) identically: a plain 2048-byte-per-block read. Anything
			// requesting sync/header/ECC subfields is rejected with INVALID
			// FIELD IN CDB so the driver can fall back to a basic READ.
			const u8 sub = state.scsi.cmd.data[9];
			if (sub != 0x00 && sub != 0x10)
			{
				printf("%s: READ CD subfield byte 0x%02x unsupported -> INVALID FIELD IN CDB\n",
					devid_string, sub);
				do_scsi_error(SCSI_INVALID_FIELD);
				return 0;
			}

			//  cmd[2..5] hold the logical block address.
			//  cmd[6..8] holds the number of logical blocks to transfer.
			ofs = get_be32(state.scsi.cmd.data + 2);
			retlen = (state.scsi.cmd.data[6] << 16) +
				(state.scsi.cmd.data[7] << 8) +
				state.scsi.cmd.data[8];
		}

		// Within bounds?
		if ((u64(ofs) + retlen) > u64(get_lba_size()))
		{
			do_scsi_error(SCSI_LBA_RANGE);
			break;
		}

		// Would exceed buffer?
		if (retlen > DATI_BUFSZ / get_block_size())
		{
			printf("%s: read too big (%d)\n", devid_string, retlen);
			do_scsi_error(SCSI_TOO_BIG);
			break;
		}

		//  Return data:
		state.scsi.dati.read = 0;
		state.scsi.dati.available = 0;
		try
		{
			if (retlen && (!seek_block(ofs) ||
				read_blocks(state.scsi.dati.data, retlen) != retlen))
			{
				do_scsi_error(is_offline() ? SCSI_OFFLINE : SCSI_HARDWARE_ERROR);
				break;
			}
		}
		catch (const CIOException& error)
		{
			printf("%s: Read failed: %s\n", devid_string, error.message().c_str());
			do_scsi_error(SCSI_HARDWARE_ERROR);
			break;
		}
		state.scsi.dati.available = retlen * get_block_size();

#if defined(DEBUG_SCSI)
		printf("%s: READ  ofs=%d size=%d\n", devid_string, ofs, retlen);
#endif
		do_scsi_error(SCSI_OK);
		break;

	case SCSICMD_READ_LONG:
#if defined(DEBUG_SCSI)
		printf("%s: READ_LONG.\n", devid_string);
#endif
		if (cdrom() && !media_present())
		{
			do_scsi_error(SCSI_MEDIA_REMOVED);
			break;
		}

		// The read long command is used to read one block of disk data, including
		// ECC data. OpenVMS uses read long / write long to do host-based shadowing.
		// During driver initialization, OpenVMS will check each disk to see if it
		// supports host-based shadowing, by trying to find the right size for read
		// long commands. The emulated scsi disk sets the size for read long / write
		// long commands to 514 bytes (the first value OpenVMS tries).
		//  cmd[2..5] hold the logical block address.
		//  cmd[7..8] holds the number of bytes to transfer
		ofs = get_be32(state.scsi.cmd.data + 2);
		retlen = (state.scsi.cmd.data[7] << 8) + state.scsi.cmd.data[8];

		state.scsi.stat.available = 1;
		state.scsi.stat.data[0] = 0;
		state.scsi.stat.read = 0;
		state.scsi.msgi.available = 1;
		state.scsi.msgi.data[0] = 0;
		state.scsi.msgi.read = 0;

		// If the requested size is not 514 bytes, don't accept it.
		if (retlen != 514)
		{
			do_scsi_error(SCSI_ILL_CMD);
			break;
		}

		// Within bounds?
		if ((u64(ofs) + 1) > u64(get_lba_size()))
		{
			do_scsi_error(SCSI_LBA_RANGE);
			break;
		}

		// Would exceed buffer?
		if (retlen > DATI_BUFSZ || get_block_size() > DATI_BUFSZ)
		{
			printf("%s: read too big (%d)\n", devid_string, retlen);
			do_scsi_error(SCSI_TOO_BIG);
			break;
		}

		//  Return data:
		state.scsi.dati.read = 0;
		state.scsi.dati.available = 0;
		try
		{
			if (!seek_block(ofs) || read_blocks(state.scsi.dati.data, 1) != 1)
			{
				do_scsi_error(is_offline() ? SCSI_OFFLINE : SCSI_HARDWARE_ERROR);
				break;
			}
		}
		catch (const CIOException& error)
		{
			printf("%s: Read failed: %s\n", devid_string, error.message().c_str());
			do_scsi_error(SCSI_HARDWARE_ERROR);
			break;
		}
		for (unsigned int x1 = get_block_size(); x1 < retlen; x1++)
			state.scsi.dati.data[x1] = 0;             // set ECC bytes to 0.
		state.scsi.dati.read = 0;
		state.scsi.dati.available = retlen;
		do_scsi_error(SCSI_OK);
		break;

	case SCSICMD_WRITE:
	case SCSICMD_WRITE_10:
	case SCSICMD_WRITE_12:
#if defined(DEBUG_SCSI)
		printf("%s: WRITE.\n", devid_string);
#endif
		if (cdrom() && !media_present())
		{
			do_scsi_error(SCSI_MEDIA_REMOVED);
			break;
		}
		if (read_only)
		{
			do_scsi_error(SCSI_WRITE_PROTECTED);
			break;
		}
		if (state.scsi.cmd.data[0] == SCSICMD_WRITE)
		{

			//  bits 4..0 of cmd[1], and cmd[2] and cmd[3]
			//  hold the logical block address.
			//
			//  cmd[4] holds the number of logical blocks
			//  to transfer. (Special case if the value is
			//  0, actually means 256.)
			ofs = ((state.scsi.cmd.data[1] & 0x1f) << 16) + (state.scsi.cmd.data[2] << 8) + state.scsi.cmd.data[3];
			retlen = state.scsi.cmd.data[4];
			if (retlen == 0)
				retlen = 256;
		}
		else if (state.scsi.cmd.data[0] == SCSICMD_WRITE_10)
		{

			//  cmd[2..5] hold the logical block address.
			//  cmd[7..8] holds the number of logical blocks
			//  to transfer.
			ofs = get_be32(state.scsi.cmd.data + 2);
			retlen = (state.scsi.cmd.data[7] << 8) + state.scsi.cmd.data[8];
		}
		else
		{
			//  WRITE_12:
			//  cmd[2..5] hold the logical block address.
			//  cmd[6..9] holds the number of logical blocks
			//  to transfer.
			ofs = get_be32(state.scsi.cmd.data + 2);
			retlen = get_be32(state.scsi.cmd.data + 6);
		}

		// Within bounds?
		if ((u64(ofs) + retlen) > u64(get_lba_size()))
		{
			do_scsi_error(SCSI_LBA_RANGE);
			break;
		}

		// Would exceed buffer?
		if (retlen > DATO_BUFSZ / get_block_size())
		{
			printf("%s: write too big (%u blocks)\n", devid_string, retlen);
			do_scsi_error(SCSI_TOO_BIG);
			break;
		}

		state.scsi.dato.expected = retlen * get_block_size();

		if (state.scsi.dato.written < state.scsi.dato.expected)
			return 2;

		//  Write data
		try
		{
			if (retlen && (!seek_block(ofs) ||
				write_blocks(state.scsi.dato.data, retlen) != retlen))
			{
				do_scsi_error(is_offline() ? SCSI_OFFLINE : SCSI_HARDWARE_ERROR);
				break;
			}
		}
		catch (const CIOException& error)
		{
			printf("%s: Write failed: %s\n", devid_string, error.message().c_str());
			do_scsi_error(SCSI_HARDWARE_ERROR);
			break;
		}

#if defined(DEBUG_SCSI)
		printf("%s: WRITE  ofs=%d size=%d\n", devid_string, ofs, retlen);
#endif
		do_scsi_error(SCSI_OK);
		break;

	case SCSICMD_SYNCHRONIZE_CACHE:
#if defined(DEBUG_SCSI)
		printf("%s: SYNCHRONIZE CACHE.\n", devid_string);
#endif
		flush();
		do_scsi_error(SCSI_OK);
		break;

    case SCSICDROM_READ_TOC:
    {
#if defined(DEBUG_SCSI)
        printf("%s: CDROM READ TOC.\n", devid_string);
#endif
        if (!media_present())
        {
            do_scsi_error(SCSI_MEDIA_REMOVED);
            break;
        }
        // We support format field == 0 only (standard TOC).
        if (state.scsi.cmd.data[2] & 0x0f)
        {
            FAILURE_2(NotImplemented,
                "%s: I don't understand READ TOC/PMA/ATIP with format %01x.\n",
                devid_string, state.scsi.cmd.data[2] & 0x0f);
        }

        const bool msf_flag       = (state.scsi.cmd.data[1] & 0x02) != 0;
        const u8   start_track_req = state.scsi.cmd.data[6];

        retlen = (unsigned int)state.scsi.cmd.data[7] * 256
               + state.scsi.cmd.data[8];

        state.scsi.dati.available = retlen;
        state.scsi.dati.read      = 0;

        // Clear response buffer so we never return stale data.
        if (retlen <= DATI_BUFSZ)
            memset(state.scsi.dati.data, 0, retlen);

        // -----------------------------------------------------------------
        // Try to get multi-track info from a bin/cue image.
        // We use ONLY the public interface of CDiskFile so that this code
        // compiles correctly from CDisk's scope.
        // For plain ISO images get_track_count() returns 0 and we fall
        // through to the original single-track response which OpenVMS relies
        // on - completely unchanged.
        // -----------------------------------------------------------------
        CDiskFile* disk_file    = dynamic_cast<CDiskFile*>(this);
        int        bincue_tracks = disk_file ? disk_file->get_track_count() : 0;

        if (bincue_tracks > 0)
        {
            // ---- Multi-track BIN/CUE response -------------------------
            // All track data accessed exclusively through get_track(idx).
            const CueTrack* first = disk_file->get_track(0);
            const CueTrack* last  = disk_file->get_track(bincue_tracks - 1);

            // Sanity check: both must be valid.
            if (!first || !last)
            {
                printf("%s: BIN/CUE track pointers invalid, "
                       "falling back to single-track TOC.\n", devid_string);
                bincue_tracks = 0;
                goto single_track_toc;
            }

            int q2 = 2;
            state.scsi.dati.data[q2++] = (u8)first->number;  // first track
            state.scsi.dati.data[q2++] = (u8)last->number;   // last track

            // Emit one descriptor per track that satisfies start_track_req.
            for (int ti = 0; ti < bincue_tracks; ti++)
            {
                const CueTrack* trk = disk_file->get_track(ti);
                if (!trk) continue;

                // Skip tracks before the requested starting track.
                // start_track_req == 0 or 1 means "all tracks".
                // start_track_req == 0xAA means "lead-out only" - handled
                // after this loop.
                if (start_track_req > 1 &&
                    start_track_req != 0xAA &&
                    trk->number < (int)start_track_req)
                    continue;

                // If only the lead-out was requested, skip data tracks.
                if (start_track_req == 0xAA)
                    continue;

                // ADR/Control byte:
                //   bits 7:4 = ADR  (1 = Q sub-channel encodes position)
                //   bits 3:0 = Control
                //     0x04 = data track
                //     0x00 = audio track (2-channel, no pre-emphasis)
                u8 adr_ctrl = (trk->mode == TRACK_MODE_AUDIO) ? 0x10 : 0x14;

                state.scsi.dati.data[q2++] = 0;         // reserved
                state.scsi.dati.data[q2++] = adr_ctrl;
                state.scsi.dati.data[q2++] = (u8)trk->number;
                state.scsi.dati.data[q2++] = 0;         // reserved

                if (msf_flag)
                {
                    u32 x = lba2msf(trk->startLBA);
                    state.scsi.dati.data[q2++] = 0;
                    state.scsi.dati.data[q2++] = (x >> 16) & 0xff;
                    state.scsi.dati.data[q2++] = (x >>  8) & 0xff;
                    state.scsi.dati.data[q2++] =  x        & 0xff;
                }
                else
                {
                    long lba = trk->startLBA;
                    state.scsi.dati.data[q2++] = (u8)(lba >> 24);
                    state.scsi.dati.data[q2++] = (u8)(lba >> 16);
                    state.scsi.dati.data[q2++] = (u8)(lba >>  8);
                    state.scsi.dati.data[q2++] = (u8) lba;
                }
            }

            // ---- Lead-out descriptor (track 0xAA) --------------------
            // Always emitted regardless of start_track_req: many OS TOC
            // parsers (including OpenVMS) require the lead-out entry.
            {
                long leadout_lba = last->endLBA;

                state.scsi.dati.data[q2++] = 0;      // reserved
                state.scsi.dati.data[q2++] = 0x16;   // ADR/Control: data, copy
                state.scsi.dati.data[q2++] = 0xAA;   // track number: lead-out
                state.scsi.dati.data[q2++] = 0;      // reserved

                if (msf_flag)
                {
                    u32 x = lba2msf(leadout_lba);
                    state.scsi.dati.data[q2++] = 0;
                    state.scsi.dati.data[q2++] = (x >> 16) & 0xff;
                    state.scsi.dati.data[q2++] = (x >>  8) & 0xff;
                    state.scsi.dati.data[q2++] =  x        & 0xff;
                }
                else
                {
                    state.scsi.dati.data[q2++] = (u8)(leadout_lba >> 24);
                    state.scsi.dati.data[q2++] = (u8)(leadout_lba >> 16);
                    state.scsi.dati.data[q2++] = (u8)(leadout_lba >>  8);
                    state.scsi.dati.data[q2++] = (u8) leadout_lba;
                }
            }

            // TOC data length = bytes following the 2-byte length field.
            state.scsi.dati.data[0] = (u8)((q2 - 2) >> 8);
            state.scsi.dati.data[1] = (u8) (q2 - 2);

#if defined(DEBUG_SCSI)
            printf("%s: BIN/CUE READ TOC: %d track(s), %d response bytes\n",
                   devid_string, bincue_tracks, q2);
            for (int x1 = 0; x1 < q2; x1++)
                printf("%02x ", state.scsi.dati.data[x1]);
            printf("\n");
#endif
            do_scsi_error(SCSI_OK);
            break;
        }

        // -----------------------------------------------------------------
        // Original single-track response for plain ISO / raw images.
        // This code is IDENTICAL to the original; OpenVMS relies on it.
        // -----------------------------------------------------------------
        single_track_toc:

        if (start_track_req > 1 && start_track_req != 0xAA)
        {
            FAILURE_2(InvalidArgument,
                "%s: I don't know CD-ROM track 0x%02x.\n",
                devid_string, start_track_req);
        }

        {
            int q2 = 2;
            state.scsi.dati.data[q2++] = 1;    // first track
            state.scsi.dati.data[q2++] = 1;    // last track

            if (start_track_req <= 1)
            {
                state.scsi.dati.data[q2++] = 0;      // reserved
                state.scsi.dati.data[q2++] = 0x14;   // ADR/Control: data
                state.scsi.dati.data[q2++] = 1;      // track number
                state.scsi.dati.data[q2++] = 0;      // reserved

                if (msf_flag)
                {
                    u32 x = lba2msf(0);
                    state.scsi.dati.data[q2++] = 0;
                    state.scsi.dati.data[q2++] = (x & 0xff0000) >> 16;
                    state.scsi.dati.data[q2++] = (x & 0x00ff00) >>  8;
                    state.scsi.dati.data[q2++] =  x & 0x0000ff;
                }
                else
                {
                    state.scsi.dati.data[q2++] = 0;
                    state.scsi.dati.data[q2++] = 0;
                    state.scsi.dati.data[q2++] = 0;
                    state.scsi.dati.data[q2++] = 0;
                }
            }

            // Lead-out descriptor.
            state.scsi.dati.data[q2++] = 0;      // reserved
            state.scsi.dati.data[q2++] = 0x16;   // ADR/Control: data, copy
            state.scsi.dati.data[q2++] = 0xAA;   // lead-out track
            state.scsi.dati.data[q2++] = 0;      // reserved

            if (msf_flag)
            {
                u32 x = lba2msf(get_lba_size());
                state.scsi.dati.data[q2++] = 0;
                state.scsi.dati.data[q2++] = (x & 0xff0000) >> 16;
                state.scsi.dati.data[q2++] = (x & 0x00ff00) >>  8;
                state.scsi.dati.data[q2++] =  x & 0x0000ff;
            }
            else
            {
                state.scsi.dati.data[q2++] = (u8)(get_lba_size() >> 24);
                state.scsi.dati.data[q2++] = (u8)(get_lba_size() >> 16);
                state.scsi.dati.data[q2++] = (u8)(get_lba_size() >>  8);
                state.scsi.dati.data[q2++] = (u8) get_lba_size();
            }

            state.scsi.dati.data[0] = (u8)((q2 - 2) >> 8);
            state.scsi.dati.data[1] = (u8) (q2 - 2);

#if defined(DEBUG_SCSI)
            printf("%s: Single-track READ TOC: %d bytes\n", devid_string, q2);
            for (unsigned int x1 = 0; x1 < (unsigned)q2; x1++)
                printf("%02x ", state.scsi.dati.data[x1]);
            printf("\n");
#endif
            do_scsi_error(SCSI_OK);
        }
    }
    break;

	case SCSICDROM_MECHANISM_STATUS:
	{
#if defined(DEBUG_SCSI)
		printf("%s: CDROM MECHANISM STATUS.\n", devid_string);
#endif
		const u16 alloc = (u16(state.scsi.cmd.data[8]) << 8) | state.scsi.cmd.data[9];
		const u16 payload_len = 8;

		// Report a simple non-changer drive, matching the QEMU ATAPI reply.
		put_be16(state.scsi.dati.data + 0, 0);
		state.scsi.dati.data[2] = 0;  // no current LBA / mechanism state info
		state.scsi.dati.data[3] = 0;
		state.scsi.dati.data[4] = 0;
		state.scsi.dati.data[5] = 1;  // one slot present
		put_be16(state.scsi.dati.data + 6, 0);

		state.scsi.dati.read = 0;
		state.scsi.dati.available = (alloc < payload_len) ? alloc : payload_len;
		do_scsi_error(SCSI_OK);
	}
	break;

	case SCSICDRRW_FORMAT:
	case SCSICDRRW_READ_DISC_INFO:
	case SCSICDRRW_READ_TRACK_INFO:
	case SCSICDRRW_RESERVE_TRACK:
	case SCSICDRRW_SEND_OPC_INFO:
	case SCSICDRRW_REPAIR_TRACK:
	case SCSICDRRW_READ_MASTER_CUE:
	case SCSICDRRW_CLOSE_TRACK:
	case SCSICDRRW_READ_BUFFER_CAP:
	case SCSICDRRW_SEND_CUE_SHEET:
	case SCSICDRRW_BLANK:

		// These are CD-R/RW specific commands; we pretend to be a simple
		// CD-ROM player, so no support for these commands.
#if defined(DEBUG_SCSI)
		printf("%s: CD-R/RW specific.\n", devid_string);
#endif
		do_scsi_error(SCSI_ILL_CMD);
		break;

	default:
		FAILURE_2(NotImplemented, "%s: Unknown SCSI command 0x%02x.\n",
			devid_string, state.scsi.cmd.data[0]);
	}

	return 0;
}

/**
 * \brief Handle a (series of) SCSI message(s).
 *
 * Called when one or more SCSI messages have been received.
 * We parse the message(s) and return what the next SCSI bus
 * phase should be.
 **/
int CDisk::do_scsi_message()
{
	unsigned int  msg;
	unsigned int  msglen;

	msg = 0;
	while (msg < state.scsi.msgo.written)
	{
		if (state.scsi.msgo.data[msg] & 0x80)
		{

			// identify
#if defined(DEBUG_SCSI)
			printf("%s: MSG: identify", devid_string);
#endif
			if (state.scsi.msgo.data[msg] & 0x40)
			{
#if defined(DEBUG_SCSI)
				printf(" w/disconnect priv");
#endif

				//        state.scsi.disconnect_priv = true;
			}

			if (state.scsi.msgo.data[msg] & 0x07)
			{

				// LUN...
#if defined(DEBUG_SCSI)
				printf(" for lun %d%", state.scsi.msgo.data[msg] & 0x07);
#endif
				state.scsi.lun_selected = true;
			}

#if defined(DEBUG_SCSI)
			printf("\n");
#endif
			msg++;
		}
		else
		{
			switch (state.scsi.msgo.data[msg])
			{
			case 0x01:
#if defined(DEBUG_SCSI)
				printf("%s: MSG: extended: ", devid_string);
#endif
				msglen = state.scsi.msgo.data[msg + 1];
				msg += 2;
				switch (state.scsi.msgo.data[msg])
				{
				case 0x01:
				{
#if defined(DEBUG_SCSI)
					printf("SDTR.\n");
#endif
					state.scsi.msgi.available = msglen + 2;
					state.scsi.msgi.data[0] = 0x01;
					state.scsi.msgi.data[1] = msglen;
					for (unsigned int x = 0; x < msglen; x++)
						state.scsi.msgi.data[2 + x] = state.scsi.msgo.data[msg + x];
				}
				break;

				case 0x03:
				{
#if defined(DEBUG_SCSI)
					printf("WDTR.\n");
#endif
					state.scsi.msgi.available = msglen + 2;
					state.scsi.msgi.data[0] = 0x01;
					state.scsi.msgi.data[1] = msglen;
					for (unsigned int x = 0; x < msglen; x++)
						state.scsi.msgi.data[2 + x] = state.scsi.msgo.data[msg + x];
				}
				break;

				default:
					FAILURE_2(NotImplemented,
						"%s: MSG: don't understand extended message %02x.\n", devid_string,
						state.scsi.msgo.data[msg]);
				}

				msg += msglen;
				break;

			default:
				FAILURE_2(NotImplemented, "%s: MSG: don't understand message %02x.\n",
					devid_string, state.scsi.msgo.data[msg]);
			}
		}
	}

	// return next phase
	if (state.scsi.msgi.available)
		return SCSI_PHASE_MSG_IN;
	else
		return SCSI_PHASE_COMMAND;
}

static int  primes_54[54] = { 2,  3,    5,   7,  11,  13,  17,  19,  23,  29,
  31,  37,  41,  43,  47,  53,  59,  61,  67,  71,
  73,  79,  83,  89,  97, 101, 103, 107, 109, 113,
  127, 131, 137, 139, 149, 151, 157, 163, 167, 173,
  179, 181, 191, 193, 197, 199, 211, 223, 227, 229,
  233, 239, 241, 251 };

//  2 3 5 7 11 13
static int  pri16[16][6] = { {4,0,0,0, 0, 0}, //16
  {0,1,1,0, 0, 0},  //15
  {1,0,0,1, 0, 0},  //14
  {0,0,0,0, 0, 1},  //13
  {2,1,0,0, 0, 0},  //12
  {0,0,0,0, 1, 0},  //11
  {1,0,1,0, 0, 0},  //10
  {0,2,0,0, 0, 0},  //9
  {3,0,0,0, 0, 0},  //8
  {0,0,0,1, 0, 0},  //7
  {1,1,0,0, 0, 0},  //6
  {0,0,1,0, 0, 0},  //5
  {2,0,0,0, 0, 0},  //4
  {0,1,0,0, 0, 0},  //3
  {1,0,0,0, 0, 0},  //2
  {0,0,0,0, 0, 0} }; //1
static off_t_large get_primes(off_t_large value, int pri[54])
{
	int i;
	// Zero is divisible by every prime and would never leave the loop below.
	if (value == 0)
	{
		memset(pri, 0, 54 * sizeof(int));
		return 0;
	}

	for (i = 0; i < 54; i++)
	{
		pri[i] = 0;
		while (!(value % primes_54[i]))
		{
			pri[i]++;
			value /= primes_54[i];
		}
	}

	return value;
}

/**
 * Calculate optimal disk layout...
 **/
#define MAX_HD  16
#define MAX_SEC 50

void CDisk::determine_layout()
{
	int   disk_primes[54];
	int   compare_primes[54];

	long  heads_sectors = 0;
	long  c_heads = 0;
	bool  b;
	int   prime;

	// Empty removable media (including a restored empty drive) has no CHS
	// geometry. Avoid both division by zero and trying to factor zero sectors.
	if (state.block_size == 0 || get_lba_size() == 0)
	{
		cylinders = heads = sectors = 0;
		return;
	}

	get_primes(get_lba_size(), disk_primes);

	for (heads_sectors = MAX_SEC * MAX_HD; heads_sectors > 0; heads_sectors--)
	{
		if (get_primes(heads_sectors, compare_primes) > 1)
			continue;

		for (c_heads = MAX_HD; c_heads > 0; c_heads--)
		{
			b = true;
			for (prime = 0; prime < 6; prime++)
			{
				if (pri16[16 - c_heads][prime] > compare_primes[prime])
				{
					b = false;
					break;
				}
			}

			if (b)
				break;
		}

		if (heads_sectors / c_heads > MAX_SEC)
			continue;

		b = true;
		for (prime = 0; prime < 54; prime++)
		{
			if (compare_primes[prime] > disk_primes[prime])
			{
				b = false;
				break;
			}
		}

		if (b)
			break;
	}

	heads = c_heads;
	sectors = heads_sectors / c_heads;

	//  sectors = 32;
	//  heads = 8;
	cylinders = get_lba_size() / heads / sectors;
}
