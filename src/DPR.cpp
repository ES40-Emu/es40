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
  * Contains the code for the emulated Dual Port Ram and RMC devices.
  *
  * $Id$
  *
  * X-1.23       Camiel Vanderhoeven                             12-JUN-2008
  *      Implement DPR mechanism for starting secondary cpu's.
  *
  * X-1.22       Brian Wheeler                                   09-APR-2008
  *      Correct RMC ROM versions.
  *
  * X-1.21       Camiel Vanderhoeven                             17-MAR-2008
  *      Always set volatile DPR rom contents.
  *
  * X-1.20       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.19       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.18       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.17       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.16       Brian Wheeler                                   29-FEB-2008
  *      Fix psu and temperature status so Tru64 likes it.
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
#include "DPR.h"
#include "System.h"
#include "Serial.h"
#include <time.h>
#include "AlphaCPU.h"

#define ToBCD(x)  (((x) / 10 << 4) | ((x) % 10))

extern CSerial* srl[2];

/**
 * Constructor.
 **/
CDPR::CDPR(CConfigurator* cfg, CSystem* c) : CSystemComponent(cfg, c)
{
	if (theDPR)
		FAILURE(Configuration, "More than one DPR");
	theDPR = this;

	c->RegisterMemory(this, 0, U64(0x0000080110000000), 0x100000);  // 16KB
}

/**
 * Initialize the DPR device.
 **/
void CDPR::init()
{
	int  i;

	memset(&state, 0, sizeof(state));
	RestoreStateF();

	for (i = 0; i < cSystem->get_cpu_num(); i++)
	{
		state.ram[i * 0x20 + 0x00] = 1; // EV6 BIST
		state.ram[i * 0x20 + 0x01] = (i == 0) ? 0x80 : i; // SROM status
		state.ram[i * 0x20 + 0x02] = 1;     // STR status
		state.ram[i * 0x20 + 0x03] = 1;     // CSC status
		state.ram[i * 0x20 + 0x04] = 1;     // Pchip0 status
		state.ram[i * 0x20 + 0x05] = 1;     // Pchip1 status
		state.ram[i * 0x20 + 0x06] = 1;     // DIMx status
		state.ram[i * 0x20 + 0x07] = 1;     // TIG bus status
		state.ram[i * 0x20 + 0x08] = 0xdd;  // DPR test started
		state.ram[i * 0x20 + 0x09] = 1;     // DPR status
		state.ram[i * 0x20 + 0x0a] = 0xff;  // CPU speed status
		state.ram[i * 0x20 + 0x0b] = (cSystem->get_cpu(i)->get_speed() / 1000000) % 256;  //speed
		state.ram[i * 0x20 + 0x0c] = (cSystem->get_cpu(i)->get_speed() / 1000000) / 256;  //speed

		// powerup time BCD:
		time_t      now = time(NULL);
		bool        faked = false;

		// Optional absolute time override (sys0 "time" config; same UTC parser
		// as AliM1543C). DPR::init runs before AliM1543C::init, so we stay
		// silent on parse failure — bad values trip AliM1543C's FAILURE_1
		// before the guest starts.
		char* faketime = myCfg->get_text_value("time");
		if (faketime)
		{
			struct tm ft = {};
			if (sscanf(faketime, "%d-%d-%d %d:%d:%d",
				&ft.tm_year, &ft.tm_mon, &ft.tm_mday,
				&ft.tm_hour, &ft.tm_min, &ft.tm_sec) >= 3)
			{
				ft.tm_year -= 1900;
				ft.tm_mon -= 1;
#ifdef _WIN32
				time_t set_time = _mkgmtime(&ft);
#else
				time_t set_time = timegm(&ft);
#endif
				if (set_time != (time_t)-1)
				{
					now = set_time;
					faked = true;
				}
			}
		}

		// Display in UTC when faketime is in effect (matches the TOY clock);
		// host local otherwise (preserves existing behavior).
		struct tm* t = faked ? gmtime(&now) : localtime(&now);
		state.ram[i * 0x20 + 0x10] = ToBCD(t->tm_hour);
		state.ram[i * 0x20 + 0x11] = ToBCD(t->tm_min);
		state.ram[i * 0x20 + 0x12] = ToBCD(t->tm_sec);
		state.ram[i * 0x20 + 0x13] = ToBCD(t->tm_mday);
		state.ram[i * 0x20 + 0x14] = ToBCD(t->tm_mon + 1);
		state.ram[i * 0x20 + 0x15] = ToBCD(t->tm_year - 100); // tm_year is based on 1900
#if defined(DEBUG_DPR)
		printf("%%DPR-I-BOOTDATE: %02x-%02x-%02x, %02x:%02x:%02x\n",
			state.ram[i * 0x20 + 21], state.ram[i * 0x20 + 20],
			state.ram[i * 0x20 + 19], state.ram[i * 0x20 + 16],
			state.ram[i * 0x20 + 17], state.ram[i * 0x20 + 18]);
#endif
		state.ram[i * 0x20 + 0x16] = 0;     // no error
		state.ram[i * 0x20 + 0x1e] = 0x80;  // CPU SROM sync moet 0x80 zijn; anders --> cpu0 startup failure
		state.ram[i * 0x20 + 0x1f] = 8;     // cach size in MB
	}

	state.ram[0xda] = 0xaa; // TIG load

	// DIMM config: mirror the modeled topology (CSystem::init_spd_from_config_mb):
	// array a = MMB a, DIMMs J1..J4 (and J5..J8 when twice-split).
	const CSystem::SDimmLayout& lay = cSystem->get_dimm_layout();
	const std::vector<uint8_t>& spd = cSystem->get_dimm_spd();
	for (int a = 0; a < lay.n_arrays; a++)
	{
		// Service guide Table C-1, byte 0x80: <7:4> F = twice-split 8 DIMMs,
		// 4 = non-split lower set only; <3:0> = configured array position.
		state.ram[0x80 + 2 * a] = (u8)(((lay.dimms_per_array == 8) ? 0xf0 : 0x40) | a);
		u8 sz = (u8)(lay.dimm_mb / 64);           // DIMM size in 64MB units
		state.ram[0x81 + 2 * a] = sz ? sz : 1;
	}

	// RMC-cached SPD reads, one 256-byte region per installed DIMM.
	// Region for MMB m DIMM Jd is at 0x100 * (m*8 + d), d = 1..8
	// (service guide Table C-1: 100 = MMB0 J1 ... 2000 = MMB3 J8).
	for (int a = 0; a < lay.n_arrays; a++)
		for (int d = 1; d <= lay.dimms_per_array; d++)
			memcpy(&state.ram[0x100 * (a * 8 + d)], spd.data(), spd.size());

	// powerup failure bits
	state.ram[0x88] = 0;    // each bit is one DIMM on MMB0
	state.ram[0x89] = 0x00; // MMB1
	state.ram[0x8a] = 0x00; // MMB2
	state.ram[0x8b] = 0x00; // MMB3

	// misconfigured DIMM bits
	state.ram[0x8c] = 0;    // each bit is one DIMM on MMB0
	state.ram[0x8d] = 0;    // MMB1
	state.ram[0x8e] = 0;    // MMB2
	state.ram[0x8f] = 0;    // MMB3
	state.ram[0x90] = 0xff; // psu / vterm present
	state.ram[0x91] = 0x00; // psu ok bits
	state.ram[0x92] = 0x07; // ac inputs valid
	state.ram[0x93] = 0x25; // cpu 0 temp in C
	state.ram[0x94] = 0x25; // cpu 1 temp in C
	state.ram[0x95] = 0x25; // cpu 2 temp in C
	state.ram[0x96] = 0x25; // cpu 3 temp in C
	state.ram[0x97] = 0x25; // pci 0 temp in C
	state.ram[0x98] = 0x25; // pci 1 temp in C
	state.ram[0x99] = 0x25; // pci 2 temp in C
	state.ram[0x9a] = 0x8b; // fan 0 speed
	state.ram[0x9b] = 0x8b; // fan 1 speed
	state.ram[0x9c] = 0x8b; // fan 2 speed
	state.ram[0x9d] = 0x8b; // fan 3 speed
	state.ram[0x9e] = 0x8b; // fan 4 speed
	state.ram[0x9f] = 0x8b; // fan 5 speed

	// vector 680 info (various faults)
	for (i = 0xa0; i < 0xaa; i++)
		state.ram[i] = 0;

	state.ram[0xaa] = 0x00; // fans good

	// RMC read failure DIMM bits (bit d-1 = Jd, set = no SPD to read):
	// MMB m carries array m's DIMMs J1..J4 (J1..J8 when twice-split).
	{
		u8 in_set = (lay.dimms_per_array == 8) ? 0xff : 0x0f;
		for (int m = 0; m < 4; m++)
			state.ram[0xab + m] = (u8)((m < lay.n_arrays) ? ~in_set : 0xff);
	}
	switch (cSystem->get_cpu_num())
	{
	case 1: state.ram[0xaf] = 0x0e; // all MMB I2C's read + CPU 0
		break;
	case 2: state.ram[0xaf] = 0x0c; // all MMB I2C's read + CPU 0
		break;
	case 3: state.ram[0xaf] = 0x08; // all MMB I2C's read + CPU 0
		break;
	case 4: state.ram[0xaf] = 0x00; // all MMB I2C's read + CPU 0
		break;
	}

	state.ram[0xb0] = 0x00; // PCI i2c read
	state.ram[0xb1] = 0x00; // mainboard i2c read
	state.ram[0xb2] = 0x00; // psu's and scsi backplanes i2c read
	state.ram[0xba] = 0xba; // i2c finished
	state.ram[0xbb] = 0x00; // rmc error
	state.ram[0xbc] = 0x00; //rmc flash update error status

	// 680 fatal registers
	state.ram[0xbd] = 0x07; // ac inputs valid
	state.ram[0xbe] = 0;    // faults
	state.ram[0xbf] = 0;    // faults
	state.ram[0xda] = 0xaa; // tig load success

	// Power-supplies
	state.ram[0xdb] = 0xf4; // PS0 id
	state.ram[0xdc] = 0x45; // 3.3v current
	state.ram[0xdd] = 0x51; // 5.0v current
	state.ram[0xde] = 0x37; // 12v current
	state.ram[0xdf] = 0x8b; // fan speed
	state.ram[0xe0] = 0xd6; // ac voltage (230v)
	state.ram[0xe1] = 0x49; // internal temp. (56 C)
	state.ram[0xe2] = 0x4b; // inlet temp. (20 C)
	state.ram[0xe4] = 0xf5; // PS1 id
	state.ram[0xe5] = 0x45; // 3.3v current
	state.ram[0xe6] = 0x51; // 5.0v current
	state.ram[0xe7] = 0x37; // 12v current
	state.ram[0xe8] = 0x8b; // fan speed
	state.ram[0xe9] = 0xd6; // ac voltage (230v)
	state.ram[0xea] = 0x49; // internal temp. (56 C)
	state.ram[0xeb] = 0x4b; // inlet temp. (20 C)
	state.ram[0xed] = 0xf6; // PS2 id
	state.ram[0xee] = 0x45; // 3.3v current
	state.ram[0xef] = 0x51; // 5.0v current
	state.ram[0xf0] = 0x37; // 12v current
	state.ram[0xf1] = 0x8b; // fan speed
	state.ram[0xf2] = 0xd6; // ac voltage (230v)
	state.ram[0xf3] = 0x49; // internal temp. (56 C)
	state.ram[0xf4] = 0x4b; // inlet temp. (20 C)

	// EEROMs

	/* Service guide (EK-ES240-SV) Table C-1:
	   100: MMB0 J1 DIMM 1
	   200: MMB0 J2 DIMM 2
	   ...
	   800: MMB0 J8 DIMM 8
	   900: MMB1 J1 DIMM 1
	   ...
	   1000: MMB1 J8 DIMM 8
	   1100: MMB2 J1 DIMM 1
	   ...
	   1800: MMB2 J8 DIMM 8
	   1900: MMB3 J1 DIMM 1
	   ...
	   2000: MMB3 J8 DIMM 8
	   2100: CPU0
	   2200: CPU1
	   2300: CPU2
	   2400: CPU3
	   2500: MMB0
	   2600: MMB1
	   2700: MMB2
	   2800: MMB3
	   2900: CPB (PCI backplane)
	   2a00: CSB (motherboard)
	   3100: PSU0 cont @ 3d00
	   3200: PSU1 cont @ 3e00
	   3300: PSU2 cont @ 3f00
	   3b00: SCSI0 (backplane)
	   3c00: SCSI1

	   2B00:2BFF  RMC Last EV6 Correctable Error
	   ASCII character string that indicates correctable error occurred, type, FRU, and so on.
	   2C00:2CFF  RMC Last Redundant Failure
	   ASCII character string that indicates redundant failure occurred, type, FRU, and so on.
	   2D00:2DFF  RMC Last System Failure
	   ASCII character string that indicates system failure occurred, type, FRU, and so on.
	   2E00:2FFF  RMC Uncorrectable machine logout frame (512 bytes)
	 */

	 //    3000:3008       SROM Version (ASCII string)
	state.ram[0x3000] = 'V';
	state.ram[0x3001] = '2';
	state.ram[0x3002] = '.';
	state.ram[0x3003] = '2';
	state.ram[0x3004] = '2';
	state.ram[0x3005] = 'G';
	state.ram[0x3006] = 0;
	state.ram[0x3007] = 0;
	state.ram[0x3008] = 0;

	//    3009:300B       RMC Rev Level of RMC first byte is letter Rev [x/t/v] second 2 bytes are major/minor.
	//                            This is the rev level of the RMC on-chip code.
	state.ram[0x3009] = 'V';
	state.ram[0x300a] = 0x31;
	state.ram[0x300b] = 0x30;

	//    300C:300E       RMC Rev Level of RMC first byte is letter Rev [x/t/v] second 2 bytes are major/minor.
	//                            This is the rev level of the RMC flash code.
	state.ram[0x300c] = 'V';
	state.ram[0x300d] = 0x31;
	state.ram[0x300e] = 0x30;

	//    300F:3010 300F RMC Revision Field of the DPR Structure
	//    3400 SROM Size of Bcache in MB
	state.ram[0x3400] = 8;

	//3401 SROM Flash SROM is valid flag; 8 = valid,0 = invalid
	state.ram[0x3401] = 8;

	//3402 SROM System's errors determined by SROM
	state.ram[0x3402] = 0;

	for (i = 0; i < cSystem->get_cpu_num(); i++)
	{
		state.ram[0x3418 + 0x10 * i] = 0xff;
		//3410:3417 SROM/SRM Jump to address for CPU0
		//3418 SROM/SRM Waiting to jump to flag for CPU0
		//3419 SROM Shadow of value written to EV6 DC_CTL register.
		//341A:341E SROM Shadow of most recent writes to EV6 CBOX "Write-many" chain.
	}

	//34A0:34A7 SROM Array 0 to DIMM ID translation
	//                                                                            Bits<4:0>
	//            Bits<7:5>
	//            0 = Exists, No Error                    Bits <2:0> =
	//            1 = Expected Missing DIMM                       + 1 (1-8)
	//            2 = Error - Missing DIMM(s)             Bits <4:3> =
	//            4 = Error - Illegal MMB                 (0-3) DIMM(s)
	//            6 = Error - Incompatible DIMM(s)
	//    34A8:34AF SROM Repeat for Array 1 of Array 0 34A0:34A7
	//    34B0:34B7 SROM Repeat for Array 2 of Array 0 34A0:34A7
	//    34B8:34CF SROM Repeat for Array 3 of Array 0 34A0:34A7
	// Entry j of array a: DIMM J(j+1) on MMB a.
	for (int a = 0; a < 4; a++)
		for (int j = 0; j < 8; j++)
		{
			u8  status = (a < lay.n_arrays &&
				j < lay.dimms_per_array) ? 0 : 1; // 1 = expected missing
			state.ram[0x34a0 + a * 8 + j] = (u8)((status << 5) | (a << 3) | j);
		}

	//    34C0:34FF       Used as scratch area for SROM
	//    3500:35FF       Used as the dedicated buffer in which SRM writes OCP or FRU EEROM data.
	//                            Firmware will write this data, RMC will only read this data.
	//    3600:36FF 3600 SRM Reserved
	//    3700:37FF SRM Reserved
	//    3800:3AFF RMC RMC scratch space
	printf("%s: $Id$\n",
		devid_string);
}

/**
 * Destructor.
 **/
CDPR::~CDPR()
{
	FlushIfDirty();
}

void CDPR::FlushIfDirty()
{
	if (!dirty) return;
	SaveStateF(myCfg->get_text_value("rom.dpr", "dpr.rom"), false);
	dirty = false;
}

/**
 * Flush the dirty buffer once writes have been quiet for a couple seconds.
 **/
void CDPR::check_state()
{
	const time_t QUIESCE_SECS = 2;
	if (!dirty) return;
	if (time(nullptr) - last_dirty < QUIESCE_SECS) return;
	FlushIfDirty();
}
u64 CDPR::ReadMem(int index, u64 address, int dsize)
{
	u64 data = 0;
	int a = (int)(address >> 6);

	data = state.ram[a];

#if defined(DEBUG_DPR)
	printf("%%DPR-I-READ: Dual-Port RAM read @ 0x%08x: 0x%02x\n", a,
		(u32)(data & 0xff));
#endif
	return data;
}

void CDPR::WriteMem(int index, u64 address, int dsize, u64 data)
{
	int i;
	int a = (int)(address >> 6);
#if defined(DEBUG_DPR)
	printf("%%DPR-I-WRITE: Dual-Port RAM write 0x%08x 0x%02x:\n", a,
		(u32)(data & 0xff));
#endif

	// FOR COMMANDS:
	//
	// 0xf9:      buffer size
	// 0xfb:fa    qualifier / address
	// 0xfc:      completion code (0 = ok, 80 = error, 81 = invalid code, 82 = invalid qualifier)
	// 0xfd:      rmc command id for response
	// 0xfe:      command code
	// 0xff:      rmc command id for command
	// COMMANDS:
	// 01:        update EEPROM
	// 02:        update baud rate
	// 03:        write to OCP
	// F0:        update RMC flash
	state.ram[a] = (char)data;
	dirty = true;
	last_dirty = time(nullptr);
	switch (a)
	{
	case 0xff:

		// command
		state.ram[0xfd] = state.ram[0xff];
		switch (state.ram[0xfe])
		{
		case 1:

			/* Service guide (EK-ES240-SV) Table C-1:
			   100-800:   MMB0 J1 DIMM 1 ... J8 DIMM 8
			   900-1000:  MMB1 J1 DIMM 1 ... J8 DIMM 8
			   1100-1800: MMB2 J1 DIMM 1 ... J8 DIMM 8
			   1900-2000: MMB3 J1 DIMM 1 ... J8 DIMM 8
			   2100: CPU0
			   2200: CPU1
			   2300: CPU2
			   2400: CPU3
			   2500: MMB0
			   2600: MMB1
			   2700: MMB2
			   2800: MMB3
			   2900: CPB (PCI backplane)
			   2a00: CSB (motherboard)
			   3100: PSU0 cont @ 3d00
			   3200: PSU1 cont @ 3e00
			   3300: PSU2 cont @ 3f00
			   3b00: SCSI0 (backplane)
			   3c00: SCSI1 */

			   // FRU-Write
			switch (state.ram[0xfb])
			{
			case 0x21:
			case 0x22:
			case 0x23:
			case 0x24:
				if ((state.ram[0xfb] - 0x20) > cSystem->get_cpu_num())
				{
					state.ram[0xfc] = 0x80;
					break;
				}

			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
			case 0x09: // MMB1..MMB3 DIMM regions (see map above)
			case 0x0a:
			case 0x0b:
			case 0x0c:
			case 0x0d:
			case 0x0e:
			case 0x0f:
			case 0x10:
			case 0x11:
			case 0x12:
			case 0x13:
			case 0x14:
			case 0x15:
			case 0x16:
			case 0x17:
			case 0x18:
			case 0x19:
			case 0x1a:
			case 0x1b:
			case 0x1c:
			case 0x1d:
			case 0x1e:
			case 0x1f:
			case 0x20:
			case 0x25:
			case 0x26:
			case 0x27:
			case 0x28:
			case 0x29:
			case 0x2a:
			case 0x31:
			case 0x32:
			case 0x33:
			case 0x3b:
			case 0x3c:
			case 0x3d:
			case 0x3e:
			case 0x3f:
				for (i = 0; i < state.ram[0xf9] + 1; i++)
				{
					state.ram[state.ram[0xfb] * 0x100 + state.ram[0xfa] + i] = state.ram[0x3500 + state.ram[0xfa] + i];
#if defined(DEBUG_DPR)
					printf("%%DPR-I-FRU: FRU data %02x @ FRU %02x set to %02x\n",
						state.ram[0xfa] + i, state.ram[0xfb],
						state.ram[0x3500 + state.ram[0xfa] + i]);
#endif
				}

				state.ram[0xfc] = 0;
				break;

			default:
#if defined(DEBUG_DPR)
				printf("%%DPR-I-RMC: RMC Command given: %02x\r\n", state.ram[0xfe]);
				printf("%%DPR-I-RMC: f9:%02x fb-fa:%02x%02x\r\n", state.ram[0xf9],
					state.ram[0xfb], state.ram[0xfa]);
#endif
				state.ram[0xfc] = 0x80;
			}
			break;

		case 2:
			state.ram[0xfc] = 0;
			break;

		case 3:

			// OCP-Write
//#if defined(DEBUG_DPR)
		{
			char buf[17];
			memcpy(buf, &(state.ram[0x3500]), 16);
			buf[16] = 0;
			fprintf(stderr, "%%%%DPR-I-OCP: OCP message: [%s]\n", buf);
		}
//#endif
			state.ram[0xfc] = 0;
			break;

		case 0xf0:
			state.ram[0xfc] = 0;

		default:
#if defined(DEBUG_DPR)
			printf("%%DPR-I-RMC: RMC Command given: %02x\r\n", state.ram[0xfe]);
			printf("%%DPR-I-RMC: f9:%02x fb-fa:%02x%02x\r\n", state.ram[0xf9],
				state.ram[0xfb], state.ram[0xfa]);
#endif
			state.ram[0xfc] = 0x81;
		}
		break;

	case 0xfd:

		// end of command
		state.ram[0xff] = state.ram[0xfd];
		break;

	case 0x3428:
	case 0x3438:
	case 0x3448:
	{
		// start cpu 1..3 (waiting-to-jump flag at 0x3418+0x10*n)
		int n = ((a - 0x3418) >> 4);
		if (cSystem->get_cpu_num() > n)
		{
			CAlphaCPU* c = cSystem->get_cpu(n);

			// Only launch a parked (cold-boot) CPU. 
			// A running CPU is running, a cross-thread set_pc would corrupt it.
			if (c->get_waiting())
			{
				printf("*** DPR *** Starting CPU %d ***\n", n);
				c->set_pc(0x8001); // should come from dpr...
				c->stop_waiting();
			}
			else
				printf("*** DPR *** CPU %d is running, not redirecting ***\n", n);
		}
		break;
	}
	}

	return;
}

/**
 * Save state to a DPR rom file.
 **/
void CDPR::SaveStateF(char* fn, bool verbose)
{
	FILE* ff;
	ff = fopen(fn, "wb");
	if (ff)
	{
		SaveState(ff);
		fclose(ff);
		if (verbose)
			printf("%%DPR-I-SAVEST: DPR state saved to %s\n", fn);
	}
	else
	{
		printf("%%DPR-F-NOSAVE: DPR could not be saved to %s\n", fn);
	}
}

/**
 * Save state to the default DPR rom file.
 **/
void CDPR::SaveStateF()
{
	SaveStateF(myCfg->get_text_value("rom.dpr", "dpr.rom"));
}

/**
 * Restore state from a DPR rom file.
 **/
void CDPR::RestoreStateF(char* fn)
{
	FILE* ff;
	ff = fopen(fn, "rb");
	if (ff)
	{
		RestoreState(ff);
		fclose(ff);
		printf("%%DPR-I-RESTST: DPR state restored from %s\n", fn);
	}
	else
	{
		printf("%%DPR-F-NOREST: DPR could not be restored from %s\n", fn);
	}
}

static u32  dpr_magic1 = 0x18A7B92D;
static u32  dpr_magic2 = 0xD29B7A81;

/**
 * Save state to a Virtual Machine State file.
 **/
int CDPR::SaveState(FILE* f)
{
	long  ss = sizeof(state);

	fwrite(&dpr_magic1, sizeof(u32), 1, f);
	fwrite(&ss, sizeof(long), 1, f);
	fwrite(&state, sizeof(state), 1, f);
	fwrite(&dpr_magic2, sizeof(u32), 1, f);
	printf("dpr: %ld bytes saved.\n", ss);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CDPR::RestoreState(FILE* f)
{
	long    ss;
	u32     m1;
	u32     m2;
	size_t  r;

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", "dpr");
		return -1;
	}

	if (m1 != dpr_magic1)
	{
		printf("%s: MAGIC 1 does not match!\n", "dpr");
		return -1;
	}

	r = fread(&ss, sizeof(long), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", "dpr");
		return -1;
	}

	if (ss != sizeof(state))
	{
		printf("%s: STRUCT SIZE does not match!\n", "dpr");
		return -1;
	}

	r = fread(&state, sizeof(state), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", "dpr");
		return -1;
	}

	r = fread(&m2, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", "dpr");
		return -1;
	}

	if (m2 != dpr_magic2)
	{
		printf("%s: MAGIC 1 does not match!\n", "dpr");
		return -1;
	}

	printf("dpr: %ld bytes restored.\n", ss);
	return 0;
}

/**
 * Restore state from the default DPR rom file.
 **/
void CDPR::RestoreStateF()
{
	RestoreStateF(myCfg->get_text_value("rom.dpr", "dpr.rom"));
}

CDPR* theDPR = 0;
