/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2020 Martin Vorländer
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
  * Contains the code for the emulated Ali M1543C chipset devices.
  *
  * $Id$
  *
  * X-1.66       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.65       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.64       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.63       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.62       Camiel Vanderhoeven                             11-MAR-2008
  *      Named, debuggable mutexes.
  *
  * X-1.61       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.60       Brian Wheeler                                   27-FEB-2008
  *      Avoid compiler warnings.
  *
  * X-1.59       Camiel Vanderhoeven                             26-FEB-2008
  *      Moved DMA code into it's own class (CDMA)
  *
  * X-1.58       Camiel Vanderhoeven                             12-FEB-2008
  *      Moved keyboard code into it's own class (CKeyboard)
  *
  * X-1.57       Camiel Vanderhoeven                             08-FEB-2008
  *      Add more keyboard debugging.
  *
  * X-1.56       Camiel Vanderhoeven                             08-FEB-2008
  *      Set default keyboard translation to scanset 3 (PS/2).
  *
  * X-1.55       Camiel Vanderhoeven                             07-FEB-2008
  *      Add more keyboard debugging.
  *
  * X-1.54       Camiel Vanderhoeven                             07-FEB-2008
  *      Don't define DEBUG_KBD by default.
  *
  * X-1.53       Camiel Vanderhoeven                             07-FEB-2008
  *      Comments.
  *
  * X-1.52       Brian Wheeler                                   02-FEB-2008
  *      Completed LPT support so it works with FreeBSD as a guest OS.
  *
  * X-1.51       Brian wheeler                                   15-JAN-2008
  *      When a keyboard self-test command is received, and the queue is
  *      not empty, the queue is cleared so the 0x55 that's sent back
  *      will be the first thing in line. Makes the keyboard initialize
  *      a little better with SRM.
  *
  * X-1.50       Camiel Vanderhoeven                             08-JAN-2008
  *      Comments.
  *
  * X-1.49       Camiel Vanderhoeven                             02-JAN-2008
  *      Comments; moved keyboard status register bits to a "status" struct.
  *
  * X-1.48       Camiel Vanderhoeven                             30-DEC-2007
  *      Print file id on initialization.
  *
  * X-1.47       Camiel Vanderhoeven                             30-DEC-2007
  *      Comments.
  *
  * X-1.46       Camiel Vanderhoeven                             29-DEC-2007
  *      Avoid referencing uninitialized data.
  *
  * X-1.45       Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.44       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.43        Camiel Vanderhoeven                             19-DEC-2007
  *      Commented out message on PIC de-assertion.
  *
  * X-1.42        Brian wheeler                                   17-DEC-2007
  *      Better DMA support.
  *
  * X-1.41        Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.40       Brian Wheeler                                   11-DEC-2007
  *      Improved timer logic (again).
  *
  * X-1.39       Brian Wheeler                                   10-DEC-2007
  *      Improved timer logic.
  *
  * X-1.38       Camiel Vanderhoeven                             10-DEC-2007
  *      Added config item for vga_console.
  *
  * X-1.37       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator; move IDE and USB to their own classes.
  *
  * X-1.36       Camiel Vanderhoeven                             7-DEC-2007
  *      Made keyboard messages conditional; add busmaster_status; add
  *      pic_edge_level.
  *
  * X-1.35       Camiel Vanderhoeven                             7-DEC-2007
  *      Generate keyboard interrupts when needed.
  *
  * X-1.34       Camiel Vanderhoeven                             6-DEC-2007
  *      Corrected bug regarding make/break key settings.
  *
  * X-1.33       Camiel Vanderhoeven                             6-DEC-2007
  *      Changed keyboard implementation (with thanks to the Bochs project!!)
  *
  * X-1.32       Brian Wheeler                                   2-DEC-2007
  *      Timing / floppy tweak for Linux/BSD guests.
  *
  * X-1.31       Brian Wheeler                                   1-DEC-2007
  *      Added console support (using SDL library), corrected timer
  *      behavior for Linux/BSD as a guest OS.
  *
  * X-1.30       Camiel Vanderhoeven                             1-DEC-2007
  *      Use correct interrupt for secondary IDE controller.
  *
  * X-1.29       Camiel Vanderhoeven                             17-NOV-2007
  *      Use CHECK_ALLOCATION.
  *
  * X-1.28       Eduardo Marcelo Serrat                          31-OCT-2007
  *      Corrected IDE interface revision level.
  *
  * X-1.27       Camiel Vanderhoeven                             18-APR-2007
  *      On a big-endian system, the LBA address for a read or write action
  *      was byte-swapped. Fixed this.
  *
  * X-1.26       Camiel Vanderhoeven                             17-APR-2007
  *      Removed debugging messages.
  *
  * X-1.25       Camiel Vanderhoeven                             16-APR-2007
  *      Added ResetPCI()
  *
  * X-1.24       Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.23	    Camiel Vanderhoeven				                3-APR-2007
  *	    Fixed wrong IDE configuration mask (address ranges masked were too
  *	    short, leading to overlapping memory regions.)
  *
  * X-1.22	    Camiel Vanderhoeven				                1-APR-2007
  *	    Uncommented the IDE debugging statements.
  *
  * X-1.21       Camiel Vanderhoeven                             31-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.20	    Camiel Vanderhoeven				                30-MAR-2007
  *	    Unintentional CVS commit / version number increase.
  *
  * X-1.19	    Camiel Vanderhoeven				                27-MAR-2007
  *       a) When DEBUG_PIC is defined, generate more debugging messages.
  *       b)	When an interrupt originates from the cascaded interrupt controller,
  *	    the interrupt vector from the cascaded controller is returned.
  *       c)	When interrupts are ended on the cascaded controller, and no
  *	    interrupts are left on that controller, the cascade interrupt (2)
  *	    on the primary controller is ended as well. I'M NOT COMPLETELY SURE
  *	    IF THIS IS CORRECT, but what goes on in OpenVMS seems to imply this.
  *       d) When the system state is saved to a vms file, and then restored, the
  *	    ide_status may be 0xb9, this bug has not been found yet, but as a
  *	    workaround, we detect the value 0xb9, and replace it with 0x40.
  *       e) Changed the values for cylinders/heads/sectors on the IDE identify
  *	    command, because it looks like OpenVMS' DQDRIVER doesn't like it if
  *	    the number of sectors is greater than 63.
  *       f) All IDE commands generate an interrupt upon completion.
  *       g) IDE command SET TRANSLATION (0x91) is recognized, but has no effect.
  *	    This is allright, as long as OpenVMS NEVER DOES CHS-mode access to
  *	    the disk.
  *
  * X-1.18	    Camiel Vanderhoeven				                26-MAR-2007
  *       a) Specific-EOI's (end-of-interrupt) now only end the interrupt they
  *	    are meant for.
  *   b) When DEBUG_PIC is defined, debugging messages for the interrupt
  *	controllers are output to the console, same with DEBUG_IDE and the
  *	IDE controller.
  *   c) If IDE registers for a non-existing drive are read, 0xff is returned.
  *   d) Generate an interrupt when a sector is read or written from a disk.
  *
  * X-1.17	Camiel Vanderhoeven				1-MAR-2007
  *   a) Accesses to IDE-configuration space are byte-swapped on a big-endian
  *	architecture. This is done through the endian_bits macro.
  *   b) Access to the IDE databuffers (16-bit transfers) are byte-swapped on
  *	a big-endian architecture. This is done through the endian_16 macro.
  *
  * X-1.16	Camiel Vanderhoeven				20-FEB-2007
  *	Write sectors to disk when the IDE WRITE command (0x30) is executed.
  *
  * X-1.15	Brian Wheeler					20-FEB-2007
  *	Information about IDE disks is now kept in the ide_info structure.
  *
  * X-1.14	Camiel Vanderhoeven				16-FEB-2007
  *   a) This is now a slow-clocked device.
  *   b) Removed ifdef _WIN32 from printf statements.
  *
  * X-1.13	Brian Wheeler					13-FEB-2007
  *      Corrected some typecasts in printf statements.
  *
  * X-1.12	Camiel Vanderhoeven				12-FEB-2007
  *	Added comments.
  *
  * X-1.11       Camiel Vanderhoeven                             9-FEB-2007
  *      Replaced f_ variables with ide_ members.
  *
  * X-1.10       Camiel Vanderhoeven                             9-FEB-2007
  *      Only open an IDE disk image, if there is a filename.
  *
  * X-1.9 	Brian Wheeler					7-FEB-2007
  *	Load disk images according to the configuration file.
  *
  * X-1.8	Camiel Vanderhoeven				7-FEB-2007
  *   a)	Removed a lot of pointless messages.
  *   b)	Calls to trace_dev now use the TRC_DEVx macro's.
  *
  * X-1.7	Camiel Vanderhoeven				3-FEB-2007
  *      Removed last conditional for supporting another system than an ES40
  *      (ifdef DS15)
  *
  * X-1.6        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.5	Brian Wheeler					3-FEB-2007
  *	Fixed some problems with sprintf statements.
  *
  * X-1.4	Brian Wheeler					3-FEB-2007
  *	Space for 4 disks in f_img.
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      Scanf, printf and 64-bit literals made compatible with
  *	Linux/GCC/glibc.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Includes are now case-correct (necessary on Linux)
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  **/
#include "StdAfx.h"
#include "AliM1543C.h"
#include "AliM1543C_pmu.h"
#include "System.h"
#include "VGA.h"

#ifdef DEBUG_PIC
bool  pic_messages = false;
#endif

/* Timer Calibration: Instructions per Microsecond (assuming 1 clock = 1 instruction) */
#define IPus  847
#define REFRESH_TOGGLE_NS 15085ULL
#define PIT_CLOCK_HZ      1193182ULL
#define PIT_LATCH_VALID     0x00010000U
#define PIT_LATCH_HIGH_NEXT 0x00020000U

u32   ali_cfg_data[64] = {
	/*00*/ 0x153310b9,  // CFID: vendor + device
	/*04*/ 0x0200000f,  // CFCS: command + status
	/*08*/ 0x060100c3,  // CFRV: class + revision
	/*0c*/ 0x00000000,  // CFLT: latency timer + cache line size
	/*10*/ 0x00000000,  // BAR0:
	/*14*/ 0x00000000,  // BAR1:
	/*18*/ 0x00000000,  // BAR2:
	/*1c*/ 0x00000000,  // BAR3:
	/*20*/ 0x00000000,  // BAR4:
	/*24*/ 0x00000000,  // BAR5:
	/*28*/ 0x00000000,  // CCIC: CardBus
	/*2c*/ 0x00000000,  // CSID: subsystem + vendor
	/*30*/ 0x00000000,  // BAR6: expansion rom base
	/*34*/ 0x00000000,  // CCAP: capabilities pointer
	/*38*/ 0x00000000,
	/*3c*/ 0x00000000,  // CFIT: interrupt configuration
	/*40*/ 0x00000000,
	// 0x44 byte = IDENRI (IDE channel 0 IRQ route).  0x0d → IRQ 14.
	/*44*/ 0x0000000d,
	/*48*/ 0x00000000,  // PIRT[A:D] PCI INTx route — programmed by firmware
	/*4c*/ 0x00000000,
	/*50*/ 0x00000000,
	/*54*/ 0x00000200,
	/*58*/ 0x00000000,
	/*5c*/ 0x00000000, /*60*/ 0x00000000, /*64*/ 0x00000000,
	/*68*/ 0x00000000, /*6c*/ 0x00000000, /*70*/ 0x00000000,
	// 0x74 byte = USBIR (USB IRQ route),       0x03 → IRQ 10
	// 0x75 byte = IDENRII (IDE chan 1 route),  0x0f → IRQ 15
	// 0x76 byte = SCIIR (SCI route),           0x00 → disabled at reset
	// 0x77 byte = SMBIR (SMBus / SCI route),   0x01 → IRQ  9
	/*74*/ 0x01000f03,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

u32   ali_cfg_mask[64] = {
	/*00*/ 0x00000000,  // CFID: vendor + device
	/*04*/ 0x00000000,  // CFCS: command + status
	/*08*/ 0x00000000,  // CFRV: class + revision
	/*0c*/ 0x00000000,  // CFLT: latency timer + cache line size
	/*10*/ 0x00000000,  // BAR0
	/*14*/ 0x00000000,  // BAR1: CBMA
	/*18*/ 0x00000000,  // BAR2:
	/*1c*/ 0x00000000,  // BAR3:
	/*20*/ 0x00000000,  // BAR4:
	/*24*/ 0x00000000,  // BAR5:
	/*28*/ 0x00000000,  // CCIC: CardBus
	/*2c*/ 0x00000000,  // CSID: subsystem + vendor
	/*30*/ 0x00000000,  // BAR6: expansion rom base
	/*34*/ 0x00000000,  // CCAP: capabilities pointer
	/*38*/ 0x00000000,
	/*3c*/ 0x00000000,  // CFIT: interrupt configuration
	// Chipset-config wmask.  Byte-granular bits set here mark each PCI-config
	// byte as OS-writable; bits left clear are read-only.  Without writable
	// bits in 0x5c-0x77, AlphaBIOS's writes to USB / IDE-1 / SCI / SMBus IRQ
	// route bytes are silently dropped and the table-build path produces
	// wrong IRQ-routing data.
	/*40*/ 0xffcfff7f,
	/*44*/ 0xff00cbdf,
	/*48*/ 0xffffffff, // PIRT[A:D]
	/*4c*/ 0x000000ff,
	/*50*/ 0xcfff8fff,
	/*54*/ 0xe0ffff00,
	/*58*/ 0x020f0d7f,
	/*5c*/ 0xffe0027f,
	/*60*/ 0x00000000, /*64*/ 0x00000000, /*68*/ 0x00000000,
	/*6c*/ 0x00ffbf00,
	/*70*/ 0xffefefff,
	/*74*/ 0x1fcf1fdf, // USBIR / IDENRII / SCIIR / SMBIR
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/**
 * Constructor.
 **/
CAliM1543C::CAliM1543C(CConfigurator* cfg, CSystem* c, int pcibus, int pcidev) : CPCIDevice(cfg, c, pcibus, pcidev)
{
	if (theAli != 0)
		FAILURE(Configuration, "More than one Ali");
	theAli = this;
}

/**
 * Initialize the Ali device.
 **/
void CAliM1543C::init()
{
	add_function(0, ali_cfg_data, ali_cfg_mask);

	int     i;
	char* filename;

	add_legacy_io(1, 0x61, 1);

	state.reg_61 = 0;

	add_legacy_io(2, 0x70, 4);
	cSystem->RegisterMemory(this, 2, U64(0x00000801fc000070), 4);
	for (i = 0; i < 4; i++)
		state.toy_access_ports[i] = 0;
	for (i = 0; i < 256; i++)
		state.toy_stored_data[i] = 0;
	state.toy_offset = 0;

	// restore cmos/mc146818 state from TOY backing file. 
	restore_toy_nvram();

	// The MC146818 is battery-backed: register A keeps its divider/rate across power
	// cycles, so on real hardware the 1024Hz periodic tick runs from power-on. SRM's
	// boot-CPU speed calibration (at "lowering IPL", BEFORE it programs the RTC at
	// "create timer") depends on that: with no ticks it times out after ~0.7s and
	// falls back to a hardcoded 2800ps cycle time = the mystery 357 MHz readout.
	state.toy_stored_data[0x0a] = 0x26;   // divider on, periodic rate 1024Hz

	arc_year_compat = myCfg->get_myParent()->get_bool_value("arc_year_compat", false);

	// Optional absolute time override from sys0:
	//   time = "YYYY-MM-DD" or "YYYY-MM-DD HH:MM:SS"
	// Interpreted as UTC to match the runtime ARC/SRM TOY edit path
	// (see toy_write SET_TIME below). Sets the initial toy_offset; subsequent
	// guest TOY writes still take precedence and overwrite this offset.
	char* faketime = myCfg->get_myParent()->get_text_value("time");
	if (faketime)
	{
		struct tm ft = {};
		int n = sscanf(faketime, "%d-%d-%d %d:%d:%d",
			&ft.tm_year, &ft.tm_mon, &ft.tm_mday,
			&ft.tm_hour, &ft.tm_min, &ft.tm_sec);
		if (n < 3)
			FAILURE_1(Configuration,
				"Invalid time format: %s (use YYYY-MM-DD or "
				"YYYY-MM-DD HH:MM:SS)",
				faketime);
		ft.tm_year -= 1900;
		ft.tm_mon -= 1;
#ifdef _WIN32
		time_t set_time = _mkgmtime(&ft);
#else
		time_t set_time = timegm(&ft);
#endif
		if (set_time == (time_t)-1)
			FAILURE_1(Configuration, "Invalid time value: %s", faketime);
		time_t host_now;
		time(&host_now);
		state.toy_offset = (long)(set_time - host_now);
	}

	state.toy_stored_data[0x17] = myCfg->get_bool_value("vga_console") ? 1 : 0;

	if (state.toy_stored_data[0x17] && !cSystem->has_vga_device())
	{
		printf("! CONFIGURATION WARNING ! vga_console set to true, but no VGA card installed.\n");
		state.toy_stored_data[0x17] = 0;
	}

	ResetPCI();

	// PIT Setup
	add_legacy_io(6, 0x40, 4);
	for (i = 0; i < 3; i++)
		state.pit_status[i] = 0x40; // invalid/null counter
	for (i = 0; i < 9; i++)
		state.pit_counter[i] = 0;
	m_pit_last = std::chrono::steady_clock::now();
	for (i = 0; i < 3; i++)
		m_pit_epoch[i] = m_pit_last;
	m_pit_acc = 0;

	add_legacy_io(7, 0x20, 2);
	add_legacy_io(8, 0xa0, 2);
	add_legacy_io(30, 0x4d0, 2);
	add_legacy_io(9, 0x22, 2); // PIC backdoor control
	state.pic_control_index = 0;

	// odd one, byte read in PCI IACK (interrupt acknowledge) cycle. Interrupt vector.
	cSystem->RegisterMemory(this, 20, U64(0x00000801f8000000), 1);

	for (i = 0; i < 2; i++)
	{
		state.pic_elcr[i] = 0;
		state.pic_ltim[i] = 0;
		pic_init_reset(i);
	}

	// Initialize parallel port - IO registration is SuperIO driven, so not here, just output handling
	filename = myCfg->get_text_value("lpt.outfile");
	if (filename)
	{
		lpt = fopen(filename, "ab");
	}
	else
	{
		lpt = NULL;
	}

	lpt_reset();

	// SuperIO
	add_legacy_io(40, 0x370, 2);

	// Populate the SuperIO chip-level / LDN register banks 
	// power on defaults
	superio_reset();

	// Docking windows may forward to ISA even when no modeled dev exists.
	add_legacy_io(60, 0, 0x10000);

	// ISA Plug-and-Play address (0x279) and write-data (0xA79) ports.
	// The OS-selectable READ_DATA port (id 52) is registered dynamically
	// when the OS programs it via PnP register 0x00.
	add_legacy_io(50, 0x279, 1);
	add_legacy_io(51, 0xa79, 1);
	state.isapnp_state    = PNP_WAIT_FOR_KEY;
	state.isapnp_key_pos  = 0;
	state.isapnp_reg      = 0;
	state.isapnp_rd_port  = 0;
	state.isapnp_wake_csn = 0;

	myRegLock = new CMutex("ali-reg");

	myThread = 0;

	printf("%s: $Id$\n",
		devid_string);
}

void CAliM1543C::start_threads()
{
	if (!myThread)
	{
		myThread = new CThread("ali");
		printf(" %s", myThread->getName().c_str());
		StopThread = false;
		myThread->start(*this);
	}
}

void CAliM1543C::stop_threads()
{
	StopThread = true;
	if (myThread)
	{
		printf(" %s", myThread->getName().c_str());
		myThread->join();
		delete myThread;
		myThread = 0;
	}
}

/**
 * Destructor.
 **/
CAliM1543C::~CAliM1543C()
{
	stop_threads();

	if (lpt)
		fclose(lpt);
}

bool CAliM1543C::uses_subtractive_decode(int index, u64, int, bool write) const noexcept
{
	// M1543C 3.5(g/h): NMI/RTC and external ISA are normally subtractive.
	switch (index)
	{
	case 1: case 2:
	case 9: case 27: case 40: case 50: case 51: case 52:
		return !is_docking_mode();
	}

	// M1543C 3.5(b/e/h): 44h<6> controls PIT/PIC and IACK reads.
	if (index == 6 || index == 7 || index == 8 || index == 30 ||
		(index == 20 && !write))
		return programmable_io_is_subtractive();
	return false;
}

bool CAliM1543C::is_docking_mode() const noexcept
{
	return (endian_32(pci_state.config_data[0][0x5c / 4]) & 1) != 0;
}

bool CAliM1543C::is_pmu_hidden() const noexcept
{
	return (endian_32(pci_state.config_data[0][0x5c / 4]) & 0x04000000U) != 0;
}

u32 CAliM1543C::config_read_custom(int func, u32 address, int dsize, u32 data)
{
	// M1543C 5Fh<2>, p.71: hidden M7101 registers 7Ch-FFh alias here.
	if (func != 0 || !is_pmu_hidden() || !docking_pmu ||
		(dsize != 8 && dsize != 16 && dsize != 32))
		return data;
	for (u32 lane = 0; lane < (u32)dsize / 8; ++lane)
		if (address + lane >= 0x7c && address + lane < 0x100)
		{
			const u32 value = docking_pmu->config_read(0, address + lane, 8);
			data = (data & ~(0xffU << (lane * 8))) | (value << (lane * 8));
		}
	return data;
}

void CAliM1543C::config_write_custom(int func, u32 address, int dsize,
	u32, u32, u32 data)
{
	if (func != 0 || !is_pmu_hidden() || !docking_pmu ||
		(dsize != 8 && dsize != 16 && dsize != 32))
		return;
	// The generic write hook receives data in stored byte order.
	if (dsize == 16)
		data = endian_16((u16)data);
	else if (dsize == 32)
		data = endian_32(data);
	for (u32 lane = 0; lane < (u32)dsize / 8; ++lane)
		if (address + lane >= 0x7c && address + lane < 0x100)
			// Use the PMU's own mask and register handling for every byte lane.
			docking_pmu->config_write(0, address + lane, 8,
				(data >> (lane * 8)) & 0xff);
}

bool CAliM1543C::programmable_io_is_subtractive() const noexcept
{
	return (endian_32(pci_state.config_data[0][0x44 / 4]) & 0x40) &&
		!is_docking_mode();
}

CAliM1543C::IsaIoDecode CAliM1543C::isa_io_decode(u32 port) const noexcept
{
	if (!is_docking_mode())
		return IsaIoDecode::Subtractive;

	// PCI positive I/O decode compares every address bit; no high-bit alias.
	if (port > 0xffff)
		return IsaIoDecode::None;

	// DMDC 5D:5C (M1543C p.69) enables the UNION of address windows.
	// Selectors belong to M7101.
	const u32 dmdc = endian_32(pci_state.config_data[0][0x5c / 4]);
	if ((dmdc & 0x04) && port >= 0x3b0 && port <= 0x3df)
		return IsaIoDecode::Positive;
	if ((dmdc & 0x20) && (port == 0x60 || port == 0x64))
		return IsaIoDecode::Positive;

	if (dmdc & 0x08)
	{
		const u32 selector = docking_pmu ? docking_pmu->docking_config(0x68) : 0;
		const u32 base = (selector & 1) ? 0x370 : 0x3f0;
		if (port >= base && port < base + 8)
			return IsaIoDecode::Positive;
	}

	if (dmdc & 0x02)
	{
		const u32 selector = docking_pmu ? docking_pmu->docking_config(0x6c) : 0;
		// M7101 6D:6C<15:2>, p.140. The book explicitly prints 338h for ADLIB.
		static const u16 bases[] = { 0x200, 0x338, 0x300, 0x310, 0x320, 0x330,
			0x220, 0x240, 0x260, 0x280, 0x530, 0x604, 0xe80, 0xf40 };
		static const u8 lengths[] = { 8, 4, 4, 4, 4, 4, 20, 20, 20, 20, 8, 8, 8, 8 };
		for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i)
			if ((selector & (1U << (i + 2))) &&
				port >= bases[i] && port < (u32)bases[i] + lengths[i])
				return IsaIoDecode::Positive;
	}

	if (dmdc & 0x50)
	{
		const u32 selector = docking_pmu ? docking_pmu->docking_config(0x70) : 0;
		if (dmdc & 0x10)
		{
			static const u16 bases[] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8,
				0x220, 0x228, 0x238, 0x338 };
			for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i)
				if ((selector & (1U << i)) && port >= bases[i] && port < (u32)bases[i] + 8)
					return IsaIoDecode::Positive;
		}
		if (dmdc & 0x40)
		{
			// 71h<2:0> are address selectors; DRQ/event enables are not ranges.
			if (((selector & 0x100) && port >= 0x378 && port <= 0x37f) ||
				((selector & 0x200) && port >= 0x278 && port <= 0x27f) ||
				((selector & 0x400) && port >= 0x3bc && port <= 0x3be))
				return IsaIoDecode::Positive;
		}
	}

	if (dmdc & 0x200)
	{
		// A5:A4<1:0> mask address bits 3:2; A1:0 are always ignored (p.156).
		const u32 selector = docking_pmu ? docking_pmu->docking_config(0xa4) : 0;
		const u32 mask = 0xfffcU & ~((selector & 3) << 2);
		if (((port ^ selector) & mask) == 0)
			return IsaIoDecode::Positive;
	}
	return IsaIoDecode::None;
}

bool CAliM1543C::superio_claims_port(u32 port, bool write) const noexcept
{
	// The strapped 370h index is write-only; 371h is valid in config mode.
	return ((port == 0x370 && write) ||
		(port == 0x371 && state.superio_config_mode)) &&
		isa_io_decode(port) != IsaIoDecode::None;
}

bool CAliM1543C::decodes_memory_access(int index, u64 address, int dsize,
	bool write) const noexcept
{
	u32 base;
	u32 length;
	switch (index)
	{
	case 9: base = 0x22; length = 2; break;
	case 27:
		base = (state.superio_ldn_regs[3][0x60] << 8) | state.superio_ldn_regs[3][0x61];
		if (!(state.superio_ldn_regs[3][0x30] & 1) || base == 0)
			return false;
		base &= 0xfffc;
		length = 4;
		break;
	case 40:
		return address < 2 && superio_claims_port(0x370 + (u32)address, write);
	case 50: base = 0x279; length = 1; break;
	case 51: base = 0xa79; length = 1; break;
	case 52:
		base = state.isapnp_rd_port;
		if (base == 0)
			return false;
		length = 1;
		break;
	case 60:
		return is_docking_mode() && address <= 0xffff &&
			isa_io_decode((u32)address) == IsaIoDecode::Positive;
	default:
		return CPCIDevice::decodes_memory_access(index, address, dsize, write);
	}
	return address < length && isa_io_decode(base + (u32)address) != IsaIoDecode::None;
}

bool CAliM1543C::memory_decode_fallback(int index) const noexcept
{
	// READ_DATA models an empty ISA PnP bus, not another device.
	// Let an actual endpoint behind this bridge supply occupied-port data.
	return index == 52 || index == 60;
}

/**
 * Read (byte,word,longword) from one of the legacy ranges. Only byte-accesses are supported.
 *
 * Ranges are:
 *  - 1. I/O port 61h
 *  - 2. I/O ports 70h-73h (time-of-year clock)
 *  - 6. I/O ports 40h-43h (programmable interrupt timer)
 *  - 7. I/O ports 20h-21h (primary programmable interrupt controller)
 *  - 9. I/O ports 22h-23h (PIC8259 control register index/data backdoor)
 *  - 8. I/O ports a0h-a1h (secondary (cascaded) programmable interrupt controller)
 *  - 20. PCI IACK address (interrupt vector)
 *  - 40. I/O ports 370h-371h (SuperIO)
 *  - 27. I/O ports 3bch-3bfh (parallel port)
 *  - 30. I/O ports 4d0h-4d1h (edge/level register of programmable interrupt controller)
 *  .
 **/
u32 CAliM1543C::ReadMem_Legacy(int index, u32 address, int dsize)
{
	if (dsize != 8 && index != 20 && index != 60) // IACK and ISA forwarding admit wider accesses.
	{
		FAILURE_4(InvalidArgument,
			"%s: DSize %d reading from legacy memory range # %d at address %02x\n",
			devid_string, dsize, index, address);
	}

	int channel = 0;
	switch (index)
	{
	case 1:   
		return reg_61_read();

	case 2:   
		return toy_read(address);

	case 6:   
		return pit_read(address);

	case 7:   
		return pic_read(0, address);

	case 8:   
		return pic_read(1, address);

	case 9:   
		return pic_control_read(address);

	case 20:  
		return pic_read_vector();

	case 30:
		return pic_read_edge_level(address);

	case 40:
		return superio_read(address);

	case 27:
		return lpt_read(address);

	case 50:  // 0x279 ADDRESS — write-only on real cards
	case 51:  // 0xA79 WRITE_DATA — write-only on real cards
		return 0xff;

	case 52:  // OS-programmed READ_DATA port — no cards => 0xff
		return isapnp_data_read(address);

	case 60:  // Docking window forwarded to an unpopulated ISA address.
		return dsize == 8 ? 0xffU : dsize == 16 ? 0xffffU : 0xffffffffU;
	}

	return 0;
}

/**
 * Write (byte,word,longword) to one of the legacy ranges. Only byte-accesses are supported.
 *
 * Ranges are:
 *  - 1. I/O port 61h
 *  - 2. I/O ports 70h-73h (time-of-year clock)
 *  - 6. I/O ports 40h-43h (programmable interrupt timer)
 *  - 7. I/O ports 20h-21h (primary programmable interrupt controller)
 *  - 9. I/O ports 22h-23h (PIC8259 control register index/data backdoor)
 *  - 8. I/O ports a0h-a1h (secondary (cascaded) programmable interrupt controller)
 *  - 12. I/O ports 00h-0fh (primary DMA controller)
 *  - 13. I/O ports c0h-dfh (secondary DMA controller)
 *  - 20. PCI IACK address (interrupt vector)
 *  - 40. I/O ports 370h-371h (SuperIO)
 *  - 27. I/O ports 3bch-3bfh (parallel port)
 *  - 30. I/O ports 4d0h-4d1h (edge/level register of programmable interrupt controller)
 *  - 33. I/O ports 80h-8fh (DMA controller memory base low page register)
 *  - 34. I/O ports 480h-48fh (DMA controller memory base high page register)
 *  .
 **/
void CAliM1543C::WriteMem_Legacy(int index, u32 address, int dsize, u32 data)
{
	if (dsize != 8 && index != 40 && index != 60) // SuperIO and ISA forwarding
	{
		FAILURE_4(InvalidArgument,
			"%s: DSize %d writing to legacy memory range # %d at address %02x\n",
			devid_string, dsize, index, address);
	}

	int channel = 0;
	switch (index)
	{
	case 1:   
		reg_61_write((u8)data); 
		return;

	case 2:   
		toy_write(address, (u8)data); 
		return;

	case 6:   
		pit_write(address, (u8)data); 
		return;

	case 7:
		pic_write(0, address, (u8)data);
		return;

	case 8:   
		pic_write(1, address, (u8)data);
		return;

	case 9:
		pic_control_write(address, (u8)data);
		return;

	case 30:
		pic_write_edge_level(address, (u8)data);
		return;

	case 40:
		for (int shift = 0, byte_offset = 0; shift < dsize; shift += 8, byte_offset++)
			superio_write(address + byte_offset, (u8)(data >> shift));
		return;

	case 27:
		lpt_write(address, (u8)data);
		return;

	case 50:  // 0x279 ADDRESS
		isapnp_addr_write((u8)data);
		return;

	case 51:  // 0xA79 WRITE_DATA
		isapnp_data_write((u8)data);
		return;

	case 52:  // OS-programmed READ_DATA port — read-only on real cards
	case 60:  // No local ISA handler at this forwarded address.
		return;
	}
}

/** Read port 61h: bit 4 is refresh and bit 5 is the PIT 2 output. */
u8 CAliM1543C::reg_61_read()
{
	const u64 refresh_ns = (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();

	state.reg_61 &= ~0x30;
	if ((refresh_ns / REFRESH_TOGGLE_NS) & 1U)
		state.reg_61 |= 0x10;
	if (pit_out(2))
		state.reg_61 |= 0x20;
	return state.reg_61;
}

/**
 * Write port 61h (speaker/ miscellaneous).
 **/
void CAliM1543C::reg_61_write(u8 data)
{
	const bool gate_was_high = (state.reg_61 & 0x01) != 0;
	const bool gate_is_high = (data & 0x01) != 0;
	state.reg_61 = (state.reg_61 & 0xf0) | (((u8)data) & 0x0f);
	const int mode = (state.pit_status[2] & 0x0e) >> 1;
	if (gate_was_high && !gate_is_high && mode != 0)
		state.pit_status[2] |= 0x80;
	if (!gate_was_high && gate_is_high)
	{
		const auto now = std::chrono::steady_clock::now();
		if (mode == 2 || mode == 3)
		{
			state.pit_counter[2] = state.pit_counter[2 + PIT_OFFSET_MAX];
			state.pit_status[2] |= 0x80;
			m_pit_epoch[2] = now;
		}
		else if (mode == 0 && state.pit_counter[2] != 0)
		{
			const u32 reload = state.pit_counter[2 + PIT_OFFSET_MAX];
			const u32 elapsed = reload > state.pit_counter[2] ?
				reload - state.pit_counter[2] : 0;
			m_pit_epoch[2] = now - std::chrono::microseconds(
				(u64)elapsed * 1000000ULL / PIT_CLOCK_HZ);
		}
	}
}

void CAliM1543C::superio_reset()
{
	memset(state.superio_chip_regs, 0, sizeof(state.superio_chip_regs));
	memset(state.superio_ldn_regs, 0, sizeof(state.superio_ldn_regs));

	state.superio_config_mode = false;
	state.superio_unlock_state = 0;
	state.superio_index = 0;
	state.superio_ldn = 0;

	state.superio_chip_regs[0x20] = 0x43;
	state.superio_chip_regs[0x21] = 0x15;
	state.superio_chip_regs[0x22] = 0x00;
	state.superio_chip_regs[0x23] = 0x00;
	state.superio_chip_regs[0x26] = 0x80; // 0x80 = M1543_DEV_STATUS_KBC
	state.superio_chip_regs[0x2d] = 0x20;
	state.superio_chip_regs[0x2e] = 0x20;

	// The FDC logical device is fixed system hardware. CMOS byte 0x10
	// describes attached drives, not whether the controller itself exists.
	state.superio_ldn_regs[0][0x30] = 0x01;
	state.superio_ldn_regs[0][0x60] = 0x03;
	state.superio_ldn_regs[0][0x61] = 0xf0;
	state.superio_ldn_regs[0][0x70] = 0x06;
	state.superio_ldn_regs[0][0x74] = 0x02;
	state.superio_ldn_regs[0][0xf0] = 0x08;
	state.superio_ldn_regs[0][0xf1] = 0x00;
	state.superio_ldn_regs[0][0xf2] = 0xff;
	state.superio_ldn_regs[0][0xf4] = 0x00;

	// LDN 3 — Parallel port.  Reset values mirror QEMU's M1543 emulation
	// (hw/isa/m1543.c) which is the known-good reference for booting
	// Windows NT 5.x on this chipset; the high bit in CFG0/CFG1 selects
	// the parallel-port mode bits the OS expects to round-trip.
	state.superio_ldn_regs[3][0x30] = 0x00;
	state.superio_ldn_regs[3][0x60] = 0x03;
	state.superio_ldn_regs[3][0x61] = 0x78;
	state.superio_ldn_regs[3][0x70] = 0x05;
	state.superio_ldn_regs[3][0x74] = 0x04;
	state.superio_ldn_regs[3][0xf0] = 0x8c;
	state.superio_ldn_regs[3][0xf1] = 0x85;

	// LDN 4 — UART1 (COM1).
	state.superio_ldn_regs[4][0x30] = 0x00;
	state.superio_ldn_regs[4][0x60] = 0x03;
	state.superio_ldn_regs[4][0x61] = 0xf8;
	state.superio_ldn_regs[4][0x70] = 0x04;
	state.superio_ldn_regs[4][0xf0] = 0x00;
	state.superio_ldn_regs[4][0xf1] = 0x00;
	state.superio_ldn_regs[4][0xf2] = 0x0c;

	// LDN 5 — UART2 (COM2).
	state.superio_ldn_regs[5][0x30] = 0x00;
	state.superio_ldn_regs[5][0x60] = 0x02;
	state.superio_ldn_regs[5][0x61] = 0xf8;
	state.superio_ldn_regs[5][0x70] = 0x03;
	state.superio_ldn_regs[5][0xf0] = 0x00;
	state.superio_ldn_regs[5][0xf1] = 0x00;
	state.superio_ldn_regs[5][0xf2] = 0x0c;

	// LDN 7 — Keyboard controller.  CFG0 bit 6 (0x40) is the KBC speed/
	// translate enable bit.
	state.superio_ldn_regs[7][0x30] = 0x01; // active
	state.superio_ldn_regs[7][0x60] = 0x00; // base high (informational)
	state.superio_ldn_regs[7][0x61] = 0x60; // base low — KBC at 0x60/0x64
	state.superio_ldn_regs[7][0x70] = 0x01; // keyboard IRQ1
	state.superio_ldn_regs[7][0x72] = 0x0c; // AUX IRQ12
	state.superio_ldn_regs[7][0xf0] = 0x40; // KBC vendor config

	// LDN 8 — AUX (mouse).  M1543C exposes mouse via the LDN 7 KBC
	// (the IRQ12 wire is on KBD's reg 0x72), but NT may also probe LDN 8
	// as a separate "logical" mouse device — keep it advertised so the
	// PnP enumerator doesn't mark the port driver as "no mouse".
	state.superio_ldn_regs[8][0x30] = 0x01;
	state.superio_ldn_regs[8][0x70] = 0x0c;
	state.superio_ldn_regs[8][0xf0] = 0x00;

	// LDN 0xC — Hotkey controller. Whistler's
	// PnP enumerator reads these to determine whether a hotkey logical
	// device exists.  Reporting consistent values keeps the enumeration
	// from looping on this LDN.
	state.superio_ldn_regs[0xc][0xf0] = 0x35;
	state.superio_ldn_regs[0xc][0xf1] = 0x14;
	state.superio_ldn_regs[0xc][0xf2] = 0x11;
	state.superio_ldn_regs[0xc][0xf3] = 0x71;
	state.superio_ldn_regs[0xc][0xf4] = 0x42;
	superio_apply_ldn(3);
}

u8 CAliM1543C::superio_current_reg() const
{
	if (state.superio_index == 0x07)
		return state.superio_ldn;

	if (state.superio_index < 0x30)
		return state.superio_chip_regs[state.superio_index];

	return state.superio_ldn_regs[state.superio_ldn & 0x0f][state.superio_index];
}

u8 CAliM1543C::superio_read(u32 address)
{
	if (!state.superio_config_mode)
		return 0xff;

	if (address == 0)
		return state.superio_index;

	const u8 value = superio_current_reg();
#ifdef DEBUG_SUPERIO
	static int superio_read_log_count = 0;
	printf("*** SUPERIO READ[%d]: port=%03x index=%02x ldn=%02x value=%02x\n",
		superio_read_log_count++, address + 0x370, state.superio_index, state.superio_ldn, value);
#endif
	return value;
}

void CAliM1543C::superio_write(u32 address, u8 data)
{
#ifdef DEBUG_SUPERIO
	static int superio_write_log_count = 0;
	printf("*** SUPERIO WRITE[%d]: port=%03x data=%02x cfg=%d index=%02x ldn=%02x \n",
		superio_write_log_count++, address + 0x370, data, state.superio_config_mode ? 1 : 0,
		state.superio_index, state.superio_ldn);
#endif

	if (address == 0)
	{
		if (!state.superio_config_mode)
		{
			if ((state.superio_unlock_state == 0 && data == 0x51) ||
				(state.superio_unlock_state == 1 && data == 0x23))
			{
				state.superio_unlock_state++;
				if (state.superio_unlock_state == 2)
				{
					state.superio_config_mode = true;
					state.superio_unlock_state = 0;
#ifdef DEBUG_SUPERIO
					printf("*** SUPERIO: entered configuration mode on port 370h\n");
#endif
				}
				return;
			}
			state.superio_unlock_state = (data == 0x51) ? 1 : 0;
			return;
		}

		if (data == 0xbb)
		{
			state.superio_config_mode = false;
			state.superio_unlock_state = 0;
#ifdef DEBUG_SUPERIO
			printf("*** SUPERIO: exited configuration mode on port 370h\n");
#endif
			return;
		}

		state.superio_index = data;
		return;
	}

	if (!state.superio_config_mode)
		return;

	if (state.superio_index == 0x02)
	{
		if (data & 0x01)
		{
			// Reset the configuration registers within the current session.
			superio_reset();
			state.superio_config_mode = true;
		}
		return;
	}

	if (state.superio_index == 0x07)
	{
		state.superio_ldn = data & 0x0f;
		return;
	}

	if (state.superio_index == 0x20 || state.superio_index == 0x21 || state.superio_index == 0x22)
		return;

	if (state.superio_index < 0x30)
	{
		state.superio_chip_regs[state.superio_index] = data;
		return;
	}

	state.superio_ldn_regs[state.superio_ldn & 0x0f][state.superio_index] = data;

	if (state.superio_index == 0x30 ||
		state.superio_index == 0x60 ||
		state.superio_index == 0x61)
	{
		superio_apply_ldn(state.superio_ldn & 0x0f);
	}
}

void CAliM1543C::superio_apply_ldn(int ldn)
{
	const u8 activate = state.superio_ldn_regs[ldn][0x30] & 0x01;
	const u16 base = (u16)((state.superio_ldn_regs[ldn][0x60] << 8) |
		state.superio_ldn_regs[ldn][0x61]);
	const u8 irq1 = state.superio_ldn_regs[ldn][0x70];
	const u8 irq2 = state.superio_ldn_regs[ldn][0x72];

	switch (ldn) {
	case 0:  // FDC.  CFloppyController binds 0x3f0-0x3f7 statically;
		// honoring an OS-driven relocation here would require a hook into
		// that class.  For now just acknowledge the activation so PnP
		// enumeration sees a coherent picture.
#ifdef DEBUG_SUPERIO
		printf("%s: FDC %s (base=%#06x irq=%u)\n", devid_string,
			activate ? "activated" : "deactivated", base, irq1);
#endif
		break;

	case 3:  // LPT — only LDN with dynamic binding implemented.
		add_legacy_io(27, base & 0xfffc, activate && base != 0 ? 4 : 0);
		if (activate && base != 0) {
#ifdef DEBUG_SUPERIO
			printf("%s: LPT activated at %#06x\n", devid_string, base & 0xfffc);
#endif
		}
#ifdef DEBUG_SUPERIO
		else {
			printf("%s: LPT deactivated (activate=%u base=%#06x)\n",
				devid_string, activate, base);
		}
#endif
		break;

	case 4:  // UART1 (COM1).  CSerial binds its ports per-instance from
		// the configurator.
	case 5:  // UART2 (COM2).
#ifdef DEBUG_SUPERIO
		printf("%s: COM%d %s (base=%#06x irq=%u)\n", devid_string, ldn - 3,
			activate ? "activated" : "deactivated", base, irq1);
#endif
		break;

	case 7:  // Keyboard controller.  CKeyboard binds 0x60/0x64 statically;
		// the LDN advertises the same so the OS sees a self-consistent
		// SuperIO view.
#ifdef DEBUG_SUPERIO
		printf("%s: KBC %s (base=%#06x kbd_irq=%u aux_irq=%u)\n",
			devid_string, activate ? "activated" : "deactivated",
			base, irq1, irq2);
#endif
		(void)irq2;
		break;

	case 8:  // AUX (mouse) — IRQ wire lives on the KBC; nothing to bind.
#ifdef DEBUG_SUPERIO
		printf("%s: AUX %s (irq=%u)\n", devid_string,
			activate ? "activated" : "deactivated", irq1);
#endif
		break;

	default:
#ifdef DEBUG_SUPERIO
		printf("%s: LDN %#x apply (activate=%u base=%#06x)\n",
			devid_string, ldn, activate, base);
#endif
		break;
	}
}

/**
 * \brief ISA Plug-and-Play protocol handlers.
 *
 * The OS drives ISA PnP card discovery via three I/O ports:
 *   - 0x279 (ADDRESS, write-only): selects the next register to access
 *     and is also the channel for the 32-byte LFSR initiation key that
 *     transitions cards from Wait-For-Key into Sleep state.
 *   - 0xA79 (WRITE_DATA, write-only): writes the value of the register
 *     previously selected via 0x279.
 *   - READ_DATA (read-only): an OS-selectable address in 0x203-0x3FF,
 *     programmed by writing PnP register 0x00.  Used for serial
 *     isolation reads and per-card resource descriptor reads.
 *
 * The es40 platform exposes no ISA PnP cards.  The handlers below run
 * the protocol state machine just enough to swallow the OS's enumeration
 * cleanly and report "no cards found": isolation reads return 0xff so
 * the OS sees a failed alternating 0x55/0xAA pattern and gives up after
 * the standard timeout, instead of generating a flood of "unknown IO
 * port" warnings every boot.
 *
 * Reference: Plug and Play ISA Specification, version 1.0a (1994),
 * Microsoft / Intel, section 4 (auto-configuration ports).
 **/

const u8 CAliM1543C::isapnp_init_key[32] = {
	0x6A, 0xB5, 0xDA, 0xED, 0xF6, 0xFB, 0x7D, 0xBE,
	0xDF, 0x6F, 0x37, 0x1B, 0x0D, 0x86, 0xC3, 0x61,
	0xB0, 0x58, 0x2C, 0x16, 0x8B, 0x45, 0xA2, 0xD1,
	0xE8, 0x74, 0x3A, 0x9D, 0xCE, 0xE7, 0x73, 0x39
};

void CAliM1543C::isapnp_addr_write(u8 data)
{
#ifdef DEBUG_ISAPNP
	printf("%%PNP-I-ADDR: write 0x%02x to 0x279 (state=%d, key_pos=%d)\n",
		data, state.isapnp_state, state.isapnp_key_pos);
#endif

	if (state.isapnp_state == PNP_WAIT_FOR_KEY)
	{
		// Per spec 4.3.2: the LFSR is reset by writing 0x00 to ADDRESS
		// twice in succession, after which the 32-byte initiation key
		// must arrive byte-by-byte without interruption.  Any byte that
		// fails to match the expected position resets our match counter.
		if (data == 0x00 && state.isapnp_key_pos < 2)
		{
			state.isapnp_key_pos = 0;
			return;
		}

		if (data == isapnp_init_key[state.isapnp_key_pos])
		{
			state.isapnp_key_pos++;
			if (state.isapnp_key_pos >= 32)
			{
#ifdef DEBUG_ISAPNP
				printf("%%PNP-I-KEY: initiation key accepted, entering SLEEP\n");
#endif
				state.isapnp_state   = PNP_SLEEP;
				state.isapnp_key_pos = 0;
			}
		}
		else
		{
			state.isapnp_key_pos = 0;
		}
		return;
	}

	// Past the key — every ADDRESS write selects the register that the
	// next 0xA79 / READ_DATA access will operate on.
	state.isapnp_reg = data;
}

void CAliM1543C::isapnp_data_write(u8 data)
{
#ifdef DEBUG_ISAPNP
	printf("%%PNP-I-DATA: write 0x%02x to reg 0x%02x (state=%d)\n",
		data, state.isapnp_reg, state.isapnp_state);
#endif

	if (state.isapnp_state == PNP_WAIT_FOR_KEY)
		return;

	switch (state.isapnp_reg)
	{
	case 0x00:  // Set RD_DATA Port — actual port = (data << 2) | 0x0003
	{
		u16 new_port = ((u16)data << 2) | 0x0003;
		state.isapnp_rd_port = new_port;
		// Re-register our READ_DATA hook at the OS-chosen port.  The
		// system memory map updates id 52 in place, so successive
		// programmings just relocate the same registration.
		add_legacy_io(52, new_port, 1);
#ifdef DEBUG_ISAPNP
		printf("%%PNP-I-RDPORT: READ_DATA port set to 0x%03x\n", new_port);
#endif
		break;
	}

	case 0x02:  // Config Control
		// bit 0: Reset (drives RESET_DRV — all cards back to Wait-For-Key)
		// bit 1: Wait For Key (return to Wait-For-Key)
		// bit 2: Reset CSN (clear all CSNs to zero)
		if (data & 0x03)
		{
			state.isapnp_state   = PNP_WAIT_FOR_KEY;
			state.isapnp_key_pos = 0;
		}
		break;

	case 0x03:  // Wake[CSN]
		state.isapnp_wake_csn = data;
		// CSN==0 with a card in Sleep starts isolation; non-zero wakes
		// the card with that CSN into Config.  We have no cards either
		// way, but tracking the state lets future reads of register 0x05
		// (Status) return something coherent if asked.
		if (state.isapnp_state == PNP_SLEEP)
			state.isapnp_state = (data == 0) ? PNP_ISOLATION : PNP_CONFIG;
		break;

	case 0x06:  // Set CSN — assign CSN to the card that won isolation.
		// No card won, no-op.
		break;

	default:
		// Logical-device select (0x07), card-level resource registers
		// (0x20-0x2F) and per-LDN resource registers (0x30-0xFF) are
		// per-card.  No cards, nothing to record.
		break;
	}
}

u8 CAliM1543C::isapnp_data_read(u32 /*address*/)
{
	// With no PnP cards driving the bus, isolation reads see floating
	// pull-ups (0xff).  The OS interprets a {0xff, 0xff} pair as a "0"
	// bit; after 72 such reads it computes a checksum that won't match,
	// concludes no card responded, and ends the isolation sequence.
	return 0xff;
}

/**
 * Restore CMOS/TOY NVRAM contents from the file named by the "rom.toy"
 * setting (default "toy.rom", like rom.dpr / rom.flash). The file is the
 * raw 256-byte CMOS image.
 **/
void CAliM1543C::restore_toy_nvram()
{
	char* fn = myCfg->get_myParent()->get_text_value("rom.toy", "toy.rom");

	FILE* f = fopen(fn, "rb");
	if (!f)
	{
		printf("%%ALI-I-NOTOYFILE: %s not found; starting with blank CMOS (created on exit).\n", fn);
		return;
	}

	size_t n = fread(state.toy_stored_data, 1, 256, f);
	fclose(f);
	if (n != 256)
	{
		printf("%%ALI-W-BADTOYFILE: %s is not a 256-byte CMOS image; ignored.\n", fn);
		memset(state.toy_stored_data, 0, 256);
		return;
	}

	printf("%%ALI-I-TOYRESTORED: CMOS contents restored from %s.\n", fn);
}

/**
 * Save CMOS/TOY NVRAM contents to the rom.toy file (default "toy.rom").
 **/
void CAliM1543C::save_toy_nvram(bool verbose)
{
	static bool warned = false;
	char* fn = myCfg->get_myParent()->get_text_value("rom.toy", "toy.rom");

	FILE* f = fopen(fn, "wb");
	if (!f)
	{
		if (!warned || verbose)
			printf("%%ALI-W-TOYSAVEFAIL: can't write CMOS contents to %s.\n", fn);
		warned = true;
		return;
	}

	fwrite(state.toy_stored_data, 1, 256, f);
	fclose(f);
	if (verbose)
		printf("%%ALI-I-TOYSAVED: CMOS contents saved to %s.\n", fn);
}

/**
 * Read time-of-year clock ports (70h-73h).
 **/
u8 CAliM1543C::toy_read(u32 address)
{

	//printf("%%ALI-I-READTOY: read port %02x: 0x%02x\n", (u32)(0x70 + address), state.toy_access_ports[address]);
	return(u8)state.toy_access_ports[address];
}

/**
 * Write time-of-year clock ports (70h-73h). On a write to port 0, recalculate clock values.
 **/
void CAliM1543C::toy_write(u32 address, u8 data)
{
	time_t      ltime;
	struct tm   stime;
	static long read_count = 0;
	static long hold_count = 0;

	//printf("%%ALI-I-WRITETOY: write port %02x: 0x%02x\n", (u32)(0x70 + address), data);
	state.toy_access_ports[address] = (u8)data;

	switch (address)
	{
	case 0:
		if ((data & 0x7f) < 14 && !(state.toy_stored_data[0x0b] & 0x80))
		{
			state.toy_stored_data[0x0d] = 0x80;   // data is geldig!

			// update clock.......
			time(&ltime);
			ltime += state.toy_offset;
			gmtime_s(&stime, &ltime);
			if (state.toy_stored_data[0x0b] & 4)
			{

				// binary
				state.toy_stored_data[0] = (u8)(stime.tm_sec);
				state.toy_stored_data[2] = (u8)(stime.tm_min);
				if (state.toy_stored_data[0x0b] & 2) // 24-hour
					state.toy_stored_data[4] = (u8)(stime.tm_hour);
				else
					// 12-hour
					state.toy_stored_data[4] = (u8)(((stime.tm_hour / 12) ? 0x80 : 0) | (stime.tm_hour % 12));
				state.toy_stored_data[6] = (u8)(stime.tm_wday + 1);
				state.toy_stored_data[7] = (u8)(stime.tm_mday);
				state.toy_stored_data[8] = (u8)(stime.tm_mon + 1);
				state.toy_stored_data[9] = (u8)(arc_year_compat ? (stime.tm_year - 80) : (stime.tm_year % 100));
			}
			else
			{

				// BCD
				state.toy_stored_data[0] = (u8)(((stime.tm_sec / 10) << 4) | (stime.tm_sec % 10));
				state.toy_stored_data[2] = (u8)(((stime.tm_min / 10) << 4) | (stime.tm_min % 10));
				if (state.toy_stored_data[0x0b] & 2) // 24-hour
					state.toy_stored_data[4] = (u8)(((stime.tm_hour / 10) << 4) | (stime.tm_hour % 10));
				else
				{ // 12-hour
					state.toy_stored_data[4] = (u8)
						(
							((stime.tm_hour / 12) ? 0x80 : 0) |
							(((stime.tm_hour % 12) / 10) << 4) | ((stime.tm_hour % 12) % 10)
							);
				}

				state.toy_stored_data[6] = (u8)(stime.tm_wday + 1);
				state.toy_stored_data[7] = (u8)(((stime.tm_mday / 10) << 4) | (stime.tm_mday % 10));
				state.toy_stored_data[8] = (u8)((((stime.tm_mon + 1) / 10) << 4) | ((stime.tm_mon + 1) % 10));
				{
					int yy_dec = arc_year_compat ? (stime.tm_year - 80) : (stime.tm_year % 100);
					state.toy_stored_data[9] = (u8)(((yy_dec / 10) << 4) | (yy_dec % 10));
				}
			}

			// Debian Linux wants something out of 0x0a.  It gets initialized
			// with 0x26, by the SRM
			// Ah, here's something from the linux kernel:
			//# /********************************************************
			//# * register details
			//# ********************************************************/
			//# #define RTC_FREQ_SELECT RTC_REG_A
			//#
			//# /* update-in-progress - set to "1" 244 microsecs before RTC goes off the bus,
			//# * reset after update (may take 1.984ms @ 32768Hz RefClock) is complete,
			//# * totalling to a max high interval of 2.228 ms.
			//# */
			//# # define RTC_UIP 0x80
			//# # define RTC_DIV_CTL 0x70
			//# /* divider control: refclock values 4.194 / 1.049 MHz / 32.768 kHz */
			//# # define RTC_REF_CLCK_4MHZ 0x00
			//# # define RTC_REF_CLCK_1MHZ 0x10
			//# # define RTC_REF_CLCK_32KHZ 0x20
			//# /* 2 values for divider stage reset, others for "testing purposes only" */
			//# # define RTC_DIV_RESET1 0x60
			//# # define RTC_DIV_RESET2 0x70
			//# /* Periodic intr. / Square wave rate select. 0=none, 1=32.8kHz,... 15=2Hz */
			//# # define RTC_RATE_SELECT 0x0F
			//#
			// The SRM-init value of 0x26 means:
			//  xtal speed 32.768KHz  (standard)
			//  periodic interrupt rate divisor of 32 = interrupt every 976.562 µs (1024Hz clock)
			 
			 
			
			// MC146818 UIP (Update-In-Progress) bit at register A bit 7.
			// Real hardware pulses UIP high for ~244 µs every 1 second of
			// wall-clock when the internal RTC is about to advance, then
			// holds it low for the remainder of the second.  
			{
				using clk = std::chrono::steady_clock;
				static auto uip_epoch = clk::now();
				const auto now = clk::now();
				const u64 elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
					now - uip_epoch).count();
				const u64 sub_sec_us = elapsed_us % 1000000ull;
				// UIP is high during the last 244 µs of each second.
				if (sub_sec_us >= (1000000ull - 244ull))
					state.toy_stored_data[0x0a] |= 0x80;
				else
					state.toy_stored_data[0x0a] &= ~0x80;
			}
			(void)read_count; (void)hold_count;

			//# /****************************************************/
			//# #define RTC_CONTROL RTC_REG_B
			//# # define RTC_SET 0x80 /* disable updates for clock setting */
			//# # define RTC_PIE 0x40 /* periodic interrupt enable */
			//# # define RTC_AIE 0x20 /* alarm interrupt enable */
			//# # define RTC_UIE 0x10 /* update-finished interrupt enable */
			//# # define RTC_SQWE 0x08 /* enable square-wave output */
			//# # define RTC_DM_BINARY 0x04 /* all time/date values are BCD if clear */
			//# # define RTC_24H 0x02 /* 24 hour mode - else hours bit 7 means pm */
			//# # define RTC_DST_EN 0x01 /* auto switch DST - works f. USA only */
			//#
			// this is set (by the srm?) to 0x0e = SQWE | DM_BINARY | 24H
			// linux sets the PIE bit later.
			//# /***********************************************************/
			//# #define RTC_INTR_FLAGS RTC_REG_C
			//# /* caution - cleared by read */
			//# # define RTC_IRQF 0x80 /* any of the following 3 is active */
			//# # define RTC_PF 0x40
			//# # define RTC_AF 0x20
			//# # define RTC_UF 0x10
			//#
		}

		{
			/*
			  See sys/dev/ic/mc146818reg.h and sys/arch/alpha/alpha/mcclock.c in NetBSD
			*/
#define MC_BASE_32_KHz  0x20
#define RTC_PF 0x40
			// Wall-exact, count-preserving periodic flag: PF sets exactly once per elapsed
			// rate period, from an analytic fires-due count against a fixed epoch, so a fast
			// poller sees the true cadence (976.5625us at the SRM-programmed 1024Hz). 
			const int rate_pow = state.toy_stored_data[0x0a] & 0x0f;
			u64 pf_freq = rate_pow ? (65536ull >> rate_pow) : 0;
			if (state.toy_stored_data[0x0a] & MC_BASE_32_KHz)
			{
				if (rate_pow == 0x1)
					pf_freq = 256;
				else if (rate_pow == 0x2)
					pf_freq = 128;
			}
			if (pf_freq)
			{
				const auto now = std::chrono::steady_clock::now();
				if (pf_freq != m_toy_pf_freq)
				{
					// Rate change (or first use): restart the divider chain from now.
					m_toy_pf_freq = pf_freq;
					m_toy_pf_epoch = now;
					m_toy_pf_count = 0;
				}
				const u64 elapsed_ns = (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(
					now - m_toy_pf_epoch).count();
				const u64 due = elapsed_ns * pf_freq / 1000000000ull;
				if (due != m_toy_pf_count)
				{
					// One or more periods elapsed since the last consumed tick; PF is a
					// flag, so a slow poller correctly coalesces missed periods into one.
					state.toy_stored_data[0x0c] |= RTC_PF;
					m_toy_pf_count = due;
				}
			}
		}

		state.toy_access_ports[1] = state.toy_stored_data[data & 0x7f];

		// register C is cleared after a read, and we don't care if its a write
		if (data == 0x0c)
			state.toy_stored_data[data & 0x7f] = 0;
		break;

	case 1:
		if (state.toy_access_ports[0] == 0x0b && data & 0x040) // If we're writing to register B, we make register C look like it fired.
			state.toy_stored_data[0x0c] = 0xf0;
		// On REG_B SET 1->0 transition, snapshot the just-written time/date
		// registers and compute an offset from host time so the value sticks
		// after SET goes low.  Without this, the regen path overwrites the
		// user's writes on the next port-70h poke.
		if (state.toy_access_ports[0] == 0x0b
			&& (state.toy_stored_data[0x0b] & 0x80)
			&& !(data & 0x80))
		{
			bool binary = (data & 0x04) != 0;
			bool h24    = (data & 0x02) != 0;

			u8 ss  = state.toy_stored_data[0];
			u8 mm  = state.toy_stored_data[2];
			u8 hh  = state.toy_stored_data[4];
			u8 dd  = state.toy_stored_data[7];
			u8 mo  = state.toy_stored_data[8];
			u8 yy  = state.toy_stored_data[9];

			bool pm = false;
			if (!h24) { pm = (hh & 0x80) != 0; hh &= 0x7f; }

			auto unbcd = [](u8 v) -> int { return ((v >> 4) & 0x0f) * 10 + (v & 0x0f); };

			struct tm st = {};
			st.tm_sec  = binary ? ss : unbcd(ss);
			st.tm_min  = binary ? mm : unbcd(mm);
			st.tm_hour = binary ? hh : unbcd(hh);
			st.tm_mday = binary ? dd : unbcd(dd);
			st.tm_mon  = (binary ? mo : unbcd(mo)) - 1;
			if (arc_year_compat)
			{
				st.tm_year = (binary ? yy : unbcd(yy)) + 80;   // 1980-base
			}
			else
			{
				st.tm_year = binary ? yy : unbcd(yy);
				if (st.tm_year < 70) st.tm_year += 100;   // 20XX pivot
			}

			if (!h24 &&  pm && st.tm_hour < 12) st.tm_hour += 12;
			if (!h24 && !pm && st.tm_hour == 12) st.tm_hour = 0;
#ifdef _WIN32
			time_t set_time = _mkgmtime(&st);
#else
			time_t set_time = timegm(&st);
#endif
			time_t host_now;
			time(&host_now);
			if (set_time != (time_t)-1)
				state.toy_offset = (long)(set_time - host_now);
		}
		state.toy_stored_data[state.toy_access_ports[0] & 0x7f] = (u8)data;
		save_toy_nvram();
		break;

	case 2:
		state.toy_access_ports[3] = state.toy_stored_data[0x80 + (data & 0x7f)];
		break;

	case 3:
		state.toy_stored_data[0x80 + (state.toy_access_ports[2] & 0x7f)] = (u8)data;
		save_toy_nvram();
		break;
	}
}

/**
 * Read from the programmable interrupt timer ports (40h-43h)
 *
 * BDW:
 * Here's the PIT Traffic during SRM and Linux Startup:
 *
 * SRM
 * PIT Write:  3, 36  = counter 0, load lsb + msb, mode 3
 * PIT Write:  0, 00
 * PIT Write:  0, 00  = 65536 = 18.2 Hz = timer interrupt.
 * PIT Write:  3, 54  = counter 1, msb only, mode 2
 * PIT Write:  1, 12  = 0x1200 = memory refresh?
 * PIT Write:  3, b6  = counter 2, load lsb + msb, mode 3
 * PIT Write:  3, 00
 * PIT Write:  0, 00
 * PIT Write:  0, 00
 *
 * Linux Startup
 * PIT Write:  3, b0  = counter 2, load lsb+msb, mode 0
 * PIT Write:  2, a4
 * PIT Write:  2, ec  = eca4
 * PIT Write:  3, 36  = counter 0, load lsb+msb, mode 3
 * PIT Write:  0, 00
 * PIT Write:  0, 00  = 65536
 * PIT Write:  3, b6  = counter 2, load lsb+msb, mode 3
 * PIT Write:  2, 31
 * PIT Write:  2, 13  = 1331
 **/
u8 CAliM1543C::pit_read(u32 address)
{
	if (address >= 3)
		return 0;

	u32& latch = state.pit_counter[address + PIT_OFFSET_LATCH];
	const bool latched = (latch & PIT_LATCH_VALID) != 0;
	const u16 count = (u16)(latched ? latch : state.pit_counter[address]);
	const int access = (state.pit_status[address] & 0x30) >> 4;

	switch (access)
	{
	case 1:                         // low byte only
		if (latched)
			latch = 0;
		return (u8)count;

	case 2:                         // high byte only
		if (latched)
			latch = 0;
		return (u8)(count >> 8);

	case 3:                         // low byte followed by high byte
		if ((latch & PIT_LATCH_HIGH_NEXT) == 0)
		{
			/* Hold the count until its high byte is read. */
			latch = (u32)count | PIT_LATCH_VALID | PIT_LATCH_HIGH_NEXT;
			return (u8)count;
		}
		latch = 0;
		return (u8)(count >> 8);
	}

	return 0;
}

/**
 * Write to the programmable interrupt timer ports (40h-43h)
 **/
void CAliM1543C::pit_write(u32 address, u8 data)
{
	if (address == 3)
	{
		const int counter = (data >> 6) & 3;
		if (counter == 3)
		{
			state.pit_status[3] = data;
			return;
		}

		const int access = (data >> 4) & 3;
		if (access == 0)
		{
			/* Do not replace an unread latch. */
			u32& latch = state.pit_counter[counter + PIT_OFFSET_LATCH];
			if ((latch & PIT_LATCH_VALID) == 0)
				latch = (state.pit_counter[counter] & 0xFFFFU) | PIT_LATCH_VALID;
			return;
		}

		int mode = (data >> 1) & 7;
		if (mode >= 6)
			mode -= 4;
		state.pit_status[counter] = (u8)((data & 0x31) | (mode << 1) | 0x40);
		if (mode != 0)
			state.pit_status[counter] |= 0x80;
		state.pit_mode[counter] = (u8)access;
		state.pit_counter[counter + PIT_OFFSET_LATCH] = 0;
		return;
	}

	if (address < 3)
	{
		const int access = (state.pit_status[address] & 0x30) >> 4;
		u32 count = 0;
		switch (access)
		{
		case 1:                       // low byte only
			count = data;
			break;

		case 2:                       // high byte only
			count = (u32)data << 8;
			break;

		case 3:                       // low byte followed by high byte
			if (state.pit_mode[address] == 3)
			{
				state.pit_counter[address + PIT_OFFSET_LATCH] = data;
				state.pit_mode[address] = 2;
				return;
			}
			count = (state.pit_counter[address + PIT_OFFSET_LATCH] & 0xFFU) |
				((u32)data << 8);
			state.pit_mode[address] = 3;
			break;

		default:
			return;
		}

		if (count == 0)
			count = 65536;
		state.pit_counter[address] = count;
		state.pit_counter[address + PIT_OFFSET_MAX] = count;
		state.pit_counter[address + PIT_OFFSET_LATCH] = 0;
		state.pit_status[address] &= ~0x40;
		if (((state.pit_status[address] & 0x0e) >> 1) == 0)
			state.pit_status[address] &= ~0x80;
		else
			state.pit_status[address] |= 0x80;
		m_pit_epoch[address] = std::chrono::steady_clock::now();
	}
}

/**
 * Derive a counter's output pin from wall-clock phase since its last load.
 * Analytic on purpose: a poller (port 61h bit 5) sees exact edges instead of
 * edges quantized to the Ali thread's wakeup cadence.
 **/
bool CAliM1543C::pit_out(int c)
{
	const int mode = (state.pit_status[c] & 0x0e) >> 1;
	if (state.pit_status[c] & 0x40)
		return mode != 0;   // after a control word, mode 0 is low; others are high
	if (c == 2 && (state.reg_61 & 0x01) == 0)
	{
		// GATE2 low inhibits counter 2.
		return mode == 0 ? (state.pit_status[c] & 0x80) != 0 : true;
	}
	const u32 n = state.pit_counter[c + PIT_OFFSET_MAX];
	if (!n)
		return true;
	// microsecond granularity: ns * PIT_CLOCK_HZ would overflow u64 after ~4h uptime
	const u64 clocks = (u64)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - m_pit_epoch[c]).count() * PIT_CLOCK_HZ / 1000000ull;
	switch (mode)
	{
	case 0:   // interrupt on terminal count: low while counting, high at terminal
		return clocks >= n;

	case 3:   // square wave: high for the first half of each period
		return ((clocks % n) * 2) < n;

	default:  // unmodelled mode: fall back to the decrement machinery's view
		return (state.pit_status[c] & 0x80) != 0;
	}
}

/**
 * Advance the PIT counters by elapsed wall-clock time.
 *
 *  - counter 0 is the 18.2Hz time counter.
 *  - counter 1 is the ram refresh, we don't care.
 *  - counter 2 is the speaker and/or generic timer
 *  .
 **/
void CAliM1543C::pit_clock()
{
	// Convert elapsed host time to 1.193182 MHz input clocks. The pacing must
	// be wall-clock: SRM calibrates the boot CPU's speed against this timer,
	// and the guest RPCC it compares against is wall-clock true.
	const auto now = std::chrono::steady_clock::now();
	u64 elapsed_ns = (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_pit_last).count();
	m_pit_last = now;
	if (elapsed_ns > 30000000ull)  // stall/restore catch-up cap: under one ch0 half-period,
		elapsed_ns = 30000000ull;  // so toggles can never bunch into one call
	m_pit_acc += elapsed_ns * PIT_CLOCK_HZ;
	const u64 clocks = m_pit_acc / 1000000000ull;
	m_pit_acc %= 1000000000ull;
	if (!clocks)
		return;

	for (int i = 0; i < 3; i++)
	{
		if (state.pit_status[i] & 0x40)
			continue;
		if (i == 2 && (state.reg_61 & 0x01) == 0)
			continue;
		switch ((state.pit_status[i] & 0x0e) >> 1)
		{
		case 0: // interrupt at terminal
			if (state.pit_counter[i] <= clocks)
			{
				state.pit_counter[i] = 0;
				// OUT high only: NULL COUNT (0x40) would make pit_out() read
				// mode 0 as low, hanging port-61h pollers that miss the edge.
				state.pit_status[i] |= 0x80;
			}
			else
				state.pit_counter[i] -= (u32)clocks;
			break;

		case 3: // square wave generator: counts down by two per input clock,
			// output toggles at terminal, so one output period = one reload value
			{
				u64 dec = clocks * 2;
				const u32 reload = state.pit_counter[i + PIT_OFFSET_MAX];
				while (dec >= state.pit_counter[i])
				{
					if (!reload)
					{ // half-programmed counter: don't spin on a zero reload
						state.pit_counter[i] = 0;
						dec = 0;
						break;
					}
					dec -= state.pit_counter[i];
					state.pit_counter[i] = reload;
					if (state.pit_status[i] & 0x80)
					{
						state.pit_status[i] &= ~0x80; // lower output;
					}
					else
					{
						state.pit_status[i] |= 0x80;  // raise output
						if (i == 0)
							pic_interrupt(0, 0);        // counter 0 is tied to irq 0.
					}
				}
				state.pit_counter[i] -= (u32)dec;
			}
			break;

		default:
			break;  // we don't care to handle it.
		}
	}
}

/**
 * Thread entry point.
 **/
void CAliM1543C::run()
{
	try
	{
		for (;;)
		{
			CThread::sleep(1);
			if (StopThread)
				return;
			do_pit_clock();
		}
	}

	catch (CException& e)
	{
		printf("Exception in Ali thread: %s.\n", e.displayText().c_str());

		// Let the thread die...
	}
}

/**
 * Handle all events that need to be handled on a clock-driven basis.
 *
 * This is a slow-clocked device, which means this DoClock isn't called as often as the CPU's DoClock.
 * Do the following:
 *  - Handle PIT clock (wall-clock paced, so the call cadence only sets granularity).
 *  .
 **/
void CAliM1543C::do_pit_clock()
{
	pit_clock();
}

u64 CAliM1543C::get_interval_period_ns() const
{
	const u8 reg_a = state.toy_stored_data[0x0a];
	const int rate_pow = reg_a & 0x0f;
	if (rate_pow == 0)
		return 0;

	if (reg_a & 0x20)
	{
		if (rate_pow == 0x1) return 1000000000ull / 256;
		if (rate_pow == 0x2) return 1000000000ull / 128;
	}
	return ((1ull << rate_pow) * 1000000000ull) / 65536ull;
}

// 8259 PIC

void CAliM1543C::pic_control_write(u32 address, u8 data)
{
	if (address == 0) {            // port 0x22 — index latch (write-only)
		state.pic_control_index = data;
		return;
	}

	// port 0x23 — data; dispatch by latched index
	switch (state.pic_control_index) {
	case 0x03:  pic_write_edge_level(0, data); return;  // ELCR_1
	case 0x04:  pic_write_edge_level(1, data); return;  // ELCR_2
	case 0x01: case 0x02: case 0x05:
		// PIC CFG_1/CFG_2/RTC_CFG — not modelled, ignore
		return;
	default:
		printf("%s: unknown PIC control index %02x = %02x\n",
			devid_string, state.pic_control_index, data);
		return;
	}
}

u8 CAliM1543C::pic_control_read(u32 address)
{
	if (address == 0) {
		// 0x22 is write-only per the header; return last index as harmless readback
		return state.pic_control_index;
	}

	switch (state.pic_control_index) {
	case 0x03:  return pic_read_edge_level(0);
	case 0x04:  return pic_read_edge_level(1);
	case 0x01: case 0x02: case 0x05:
		return 0;
	default:
		return 0xff;
	}
}

/**
 * Reset the PIC to its post-ICW1 state.  ELCR/LTIM are intentionally not reset
 * Caller must hold picLock.
 **/
void CAliM1543C::pic_init_reset(int index)
{
	state.pic_last_irr[index] = 0;
	state.pic_irr[index] &= state.pic_elcr[index];
	state.pic_imr[index] = 0;
	state.pic_isr[index] = 0;
	state.pic_priority_add[index] = 0;
	state.pic_irq_base[index] = 0;
	state.pic_read_reg_select[index] = 0;
	state.pic_poll[index] = 0;
	state.pic_special_mask[index] = 0;
	state.pic_init_state[index] = 0;
	state.pic_auto_eoi[index] = 0;
	state.pic_rotate_on_aeoi[index] = 0;
	state.pic_special_fnm[index] = 0;
	state.pic_init4[index] = 0;
	state.pic_single_mode[index] = 0;
	pic_update_output(index);
}

/* Highest-priority bit in mask, considering rotation.  Returns 8 if mask==0. */
static int pic_priority_of(u8 mask, u8 priority_add)
{
	if (mask == 0)
		return 8;
	int priority = 0;
	while ((mask & (1 << ((priority + priority_add) & 7))) == 0)
		priority++;
	return priority;
}

/**
 * Determine which IRQ (if any) the controller wants to deliver right now.
 * Returns -1 if nothing pending above the in-service priority threshold.
 * Caller must hold picLock.
 **/
int CAliM1543C::pic_get_irq(int index)
{
	int mask = state.pic_irr[index] & ~state.pic_imr[index];
	int priority = pic_priority_of((u8)mask, state.pic_priority_add[index]);
	if (priority == 8)
		return -1;

	// Compute current in-service priority.  In SFNM on the master, slave-
	// originated IRQs (cascade IRQ2) do not block other slave IRQs.
	int isr_mask = state.pic_isr[index];
	if (state.pic_special_mask[index])
		isr_mask &= ~state.pic_imr[index];
	if (state.pic_special_fnm[index] && index == 0)
		isr_mask &= ~(1 << 2);

	int cur_priority = pic_priority_of((u8)isr_mask, state.pic_priority_add[index]);
	if (priority < cur_priority)
		return (priority + state.pic_priority_add[index]) & 7;
	return -1;
}

/**
 * Recompute and drive the controller's output line.  For the master, this is
 * the wired-OR of all 8259 outputs that goes to Tsunami DRIR55 and from there
 * to the CPU's external IRQ1.  For the slave, this propagates into the
 * master's IRQ2 (cascade) and recurses to the master.  Caller must hold
 * picLock.
 **/
void CAliM1543C::pic_update_output(int index)
{
	bool line_high = (pic_get_irq(index) >= 0);

	if (index == 1)
	{
		// Slave output drives master IRQ2.  Cascade is conventionally edge-
		// triggered; honor ELCR/LTIM in case the OS programs it as level.
		const u8 cascade_mask = (1 << 2);
		const bool is_level =
			(state.pic_elcr[0] & cascade_mask) || state.pic_ltim[0];

		if (line_high)
		{
			if (is_level)
			{
				state.pic_irr[0] |= cascade_mask;
			}
			else
			{
				if ((state.pic_last_irr[0] & cascade_mask) == 0)
					state.pic_irr[0] |= cascade_mask;
			}
			state.pic_last_irr[0] |= cascade_mask;
		}
		else
		{
			// The cascade request is derived from the slave output, so it
			// must disappear with that output.  Keeping a master IRQ2 after
			// the slave request was canceled produces a phantom IRQ15 and
			// can leave master ISR2 permanently in service.
			state.pic_irr[0] &= ~cascade_mask;
			state.pic_last_irr[0] &= ~cascade_mask;
		}
		pic_update_output(0);
	}
	else
	{
		cSystem->interrupt(55, line_high);
	}
}

/**
 * Acknowledge an IRQ during the INTA cycle.  Sets ISR (unless AEOI), clears
 * IRR for edge-triggered lines, and re-evaluates the output. 
 * Caller must hold picLock.
 **/
void CAliM1543C::pic_intack(int index, int irq)
{
	if (state.pic_auto_eoi[index])
	{
		if (state.pic_rotate_on_aeoi[index])
			state.pic_priority_add[index] = (irq + 1) & 7;
	}
	else
	{
		state.pic_isr[index] |= (1 << irq);
	}

	// Level-sensitive lines keep their IRR bit set as long as the source
	// holds the line high; only edge-sensitive bits clear at INTA.
	const bool is_level =
		(state.pic_elcr[index] & (1 << irq)) || state.pic_ltim[index];
	if (!is_level)
		state.pic_irr[index] &= ~(1 << irq);

	pic_update_output(index);
}

/**
 * Read a byte from one of the programmable interrupt controller's registers.
 **/
u8 CAliM1543C::pic_read(int index, u32 address)
{
	CFastMutex::ScopedLock guard(picLock);
	u8 data;

	if (state.pic_poll[index])
	{
		int irq = pic_get_irq(index);
		if (irq >= 0)
		{
			pic_intack(index, irq);
			data = (u8)(irq | 0x80);
		}
		else
		{
			data = 0;
		}
		state.pic_poll[index] = 0;
	}
	else
	{
		if (address == 0)
		{
			data = state.pic_read_reg_select[index]
				? state.pic_isr[index]
				: state.pic_irr[index];
		}
		else
		{
			data = state.pic_imr[index];
		}
	}

#ifdef DEBUG_PIC
	if (pic_messages)
		printf("%%PIC-I-READ: read %02x from port %" PRId64 " on PIC %d\n", data,
			address, index);
#endif
	return data;
}

/**
 * Read a byte from the edge/level register of one of the programmable interrupt controllers.
 **/
u8 CAliM1543C::pic_read_edge_level(int index)
{
	CFastMutex::ScopedLock guard(picLock);
	return state.pic_elcr[index];
}

/**
 * Read the interrupt vector during a PCI IACK cycle.  This is the hook the
 * Tsunami chipset uses on INTACK; it must mirror what the master 8259 would
 * place on the data bus, including the cascade hand-off to the slave for
 * IRQs 8-15. 
 **/
u8 CAliM1543C::pic_read_vector()
{
	CFastMutex::ScopedLock guard(picLock);

	int irq = pic_get_irq(0);
	int intno;

	if (irq >= 0)
	{
		if (irq == 2)
		{
			// Cascade: fetch vector from slave.
			int irq2 = pic_get_irq(1);
			if (irq2 >= 0)
			{
				pic_intack(1, irq2);
			}
			else
			{
				irq2 = 7;   // spurious slave IRQ
			}
			intno = state.pic_irq_base[1] + irq2;
			pic_intack(0, irq);
		}
		else
		{
			intno = state.pic_irq_base[0] + irq;
			pic_intack(0, irq);
		}
	}
	else
	{
		// Spurious master IRQ.
		intno = state.pic_irq_base[0] + 7;
	}

	return (u8)intno;
}

/**
 * Write a byte to one of the programmable interrupt controller's registers.
 * Implements the ICW1/ICW2/ICW3/ICW4 init sequence and the OCW1/OCW2/OCW3
 * operational commands. 
 **/
void CAliM1543C::pic_write(int index, u32 address, u8 data)
{
	CFastMutex::ScopedLock guard(picLock);

#ifdef DEBUG_PIC
	if (pic_messages)
		printf("%%PIC-I-WRITE: write %02x to port %" PRId64 " on PIC %d\n", data,
			address, index);
#endif

	if (address == 0)
	{
		if (data & 0x10)
		{
			// ICW1: start init sequence.
			pic_init_reset(index);
			state.pic_init_state[index] = 1;
			state.pic_init4[index]      = (data & 0x01) ? 1 : 0;
			state.pic_single_mode[index] = (data & 0x02) ? 1 : 0;
			state.pic_ltim[index]       = (data & 0x08) ? 1 : 0;
		}
		else if (data & 0x08)
		{
			// OCW3
			if (data & 0x04)
				state.pic_poll[index] = 1;
			if (data & 0x02)
				state.pic_read_reg_select[index] = data & 0x01;
			if (data & 0x40)
				state.pic_special_mask[index] = (data >> 5) & 0x01;
		}
		else
		{
			// OCW2
			const int cmd   = (data >> 5) & 7;
			const int level = data & 7;
			int eoi_irq;
			int priority;

			switch (cmd)
			{
			case 0: // rotate in auto-EOI mode (clear)
			case 4: // rotate in auto-EOI mode (set)
				state.pic_rotate_on_aeoi[index] = (u8)(cmd >> 2);
				break;

			case 1: // non-specific EOI
			case 5: // rotate on non-specific EOI
				priority = pic_priority_of(state.pic_isr[index],
					state.pic_priority_add[index]);
				if (priority != 8)
				{
					eoi_irq = (priority + state.pic_priority_add[index]) & 7;
					state.pic_isr[index] &= ~(1 << eoi_irq);
					if (cmd == 5)
						state.pic_priority_add[index] = (u8)((eoi_irq + 1) & 7);
					pic_update_output(index);
				}
#ifdef DEBUG_PIC
				pic_messages = false;
#endif
				break;

			case 3: // specific EOI
				state.pic_isr[index] &= ~(1 << level);
				pic_update_output(index);
#ifdef DEBUG_PIC
				pic_messages = false;
#endif
				break;

			case 6: // set priority command
				state.pic_priority_add[index] = (u8)((level + 1) & 7);
				pic_update_output(index);
				break;

			case 7: // rotate on specific EOI
				state.pic_isr[index] &= ~(1 << level);
				state.pic_priority_add[index] = (u8)((level + 1) & 7);
				pic_update_output(index);
#ifdef DEBUG_PIC
				pic_messages = false;
#endif
				break;

			default:
				// no operation
				break;
			}
		}
		return;
	}

	// address == 1: ICW2/3/4 during init, IMR (OCW1) otherwise.
	switch (state.pic_init_state[index])
	{
	case 0:
		// OCW1: write IMR.  Masking only gates delivery; IRR and ISR are
		// unaffected — re-evaluating the output picks up any newly-unmasked
		// pending IRR bit.
		state.pic_imr[index] = data;
		pic_update_output(index);
		break;

	case 1:
		// ICW2: vector base.
		state.pic_irq_base[index] = data & 0xf8;
		state.pic_init_state[index] = state.pic_single_mode[index]
			? (state.pic_init4[index] ? 3 : 0)
			: 2;
		break;

	case 2:
		// ICW3: cascade configuration (we hard-wire master IRQ2 ↔ slave).
		state.pic_init_state[index] = state.pic_init4[index] ? 3 : 0;
		break;

	case 3:
		// ICW4
		state.pic_special_fnm[index] = (data >> 4) & 0x01;
		state.pic_auto_eoi[index]    = (data >> 1) & 0x01;
		state.pic_init_state[index]  = 0;
		break;
	}
}

/**
 * Write a byte to the edge/level register of one of the programmable interrupt controllers.
 **/
void CAliM1543C::pic_write_edge_level(int index, u8 data)
{
	CFastMutex::ScopedLock guard(picLock);
	state.pic_elcr[index] = data;
	// Changing trigger mode can affect the wanted-IRQ calculation if any
	// previously-edge-latched bit is now considered level.
	pic_update_output(index);
}

#define DEBUG_EXPR  (index != 0 || (intno != 0 && intno > 4))

/**
 * Assert an interrupt — inner implementation, no lock.
 * Caller MUST hold picLock.
 *
 * Each call latches a fresh edge in IRR regardless of prior line state.  This
 * preserves the historical es40 API where pic_interrupt() is treated as a
 * single-shot "this device fired" event by KBD/PIT/serial/IDE.  Level-mode
 * pins (ELCR or LTIM) additionally have IRR follow the line until pic_deassert.
 **/
void CAliM1543C::pic_interrupt_inner(int index, int intno)
{
#ifdef DEBUG_PIC
	if (DEBUG_EXPR)
	{
		printf("%%PIC-I-INCOMING: Interrupt %d incomming on PIC %d", intno, index);
		pic_messages = true;
	}
#endif

	const u8 mask = (u8)(1 << intno);

	// Latch a fresh edge: drop last_irr first, then raise.
	state.pic_last_irr[index] &= ~mask;
	state.pic_irr[index]      |= mask;
	state.pic_last_irr[index] |= mask;

#ifdef DEBUG_PIC
	if (DEBUG_EXPR)
	{
		if (state.pic_imr[index] & mask)
			printf(" (masked, latched)\n");
		else
			printf("\n");
	}
#endif

	pic_update_output(index);
}

/**
 * De-assert an interrupt — inner implementation, no lock.
 * Caller MUST hold picLock.
 *
 * For level-mode pins, this clears IRR (the line dropped low).  For edge-
 * mode pins, only last_irr drops; any IRR bit already latched stays pending
 * until INTA, which matches real-hardware edge semantics.
 **/
void CAliM1543C::pic_deassert_inner(int index, int intno)
{
	const u8 mask = (u8)(1 << intno);
	const bool is_level =
		(state.pic_elcr[index] & mask) || state.pic_ltim[index];

	if (is_level)
		state.pic_irr[index] &= ~mask;
	state.pic_last_irr[index] &= ~mask;

	pic_update_output(index);
}

/**
 * Assert an interrupt on one of the programmable interrupt controllers.
 **/
void CAliM1543C::pic_interrupt(int index, int intno)
{
	CFastMutex::ScopedLock guard(picLock);
	pic_interrupt_inner(index, intno);
}

/**
 * De-assert an interrupt on one of the programmable interrupt controllers.
 **/
void CAliM1543C::pic_deassert(int index, int intno)
{
	CFastMutex::ScopedLock guard(picLock);
	pic_deassert_inner(index, intno);
}

/**
 * Drive a device IRQ input line to a level (active/inactive) — the model for
 * sources whose "interrupt" is a level the device holds until the guest clears
 * the cause.
 *
 * Because the source is a level, IRR FOLLOWS the line: a rising edge latches
 * IRR (debounced via last_irr so a held line yields one interrupt), and a
 * falling line RETRACTS IRR.  This intentionally differs from a stock 8259
 * which keeps an edge latched until INTA: a real 8259 has a wire whose source
 * holds it, but here the "wire" IS the emulated device cause and once that
 * cause disappears there is nothing left to deliver.
 * 
 * caller must hold picLock 
 **/
void CAliM1543C::pic_set_line_inner(int index, int intno, bool active)
{
	const u8 mask = (u8)(1 << intno);

	if ((state.pic_elcr[index] & mask) || state.pic_ltim[index])
	{
		// Level triggered: IRR follows the input line.
		if (active)
		{
			state.pic_irr[index]      |= mask;
			state.pic_last_irr[index] |= mask;
		}
		else
		{
			state.pic_irr[index]      &= ~mask;
			state.pic_last_irr[index] &= ~mask;
		}
	}
	else
	{
		// Edge triggered: latch IRR only on the rising edge.  A held line
		// produces no duplicate edge; INTA consumes it unless the emulated
		// cause falls first.
		if (active)
		{
			if ((state.pic_last_irr[index] & mask) == 0)
				state.pic_irr[index] |= mask;
			state.pic_last_irr[index] |= mask;
		}
		else
		{
			// The emulated cause disappeared before INTA.  Retract the
			// request and rearm edge detection.
			state.pic_irr[index]      &= ~mask;
			state.pic_last_irr[index] &= ~mask;
		}
	}

	pic_update_output(index);
}

/**
 * Drive a device IRQ input line — locked wrapper, see pic_set_line_inner.
 **/
void CAliM1543C::pic_set_line(int index, int intno, bool active)
{
	CFastMutex::ScopedLock guard(picLock);
	pic_set_line_inner(index, intno, active);
}

static u32  ali_magic1 = 0xA111543C;
static u32  ali_magic2 = 0xC345111A;

/**
 * Save state to a Virtual Machine State file.
 **/
int CAliM1543C::SaveState(FILE* f)
{
	long  ss = sizeof(state);
	int   res;

	if ((res = CPCIDevice::SaveState(f)))
		return res;

	fwrite(&ali_magic1, sizeof(u32), 1, f);
	fwrite(&ss, sizeof(long), 1, f);
	fwrite(&state, sizeof(state), 1, f);
	fwrite(&ali_magic2, sizeof(u32), 1, f);
	printf("%s: %d bytes saved.\n", devid_string, (int)ss);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CAliM1543C::RestoreState(FILE* f)
{
	long    ss;
	u32     m1;
	u32     m2;
	int     res;
	size_t  r;

	if ((res = CPCIDevice::RestoreState(f)))
		return res;

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m1 != ali_magic1)
	{
		printf("%s: MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	fread(&ss, sizeof(long), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (ss != sizeof(state))
	{
		printf("%s: STRUCT SIZE does not match!\n", devid_string);
		return -1;
	}

	fread(&state, sizeof(state), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	r = fread(&m2, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m2 != ali_magic2)
	{
		printf("%s: MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	// Rebuild dynamic ISA mappings from the restored register state.
	superio_apply_ldn(3);
	add_legacy_io(52, state.isapnp_rd_port, state.isapnp_rd_port ? 1 : 0);

	printf("%s: %d bytes restored.\n", devid_string, (int)ss);
	return 0;
}

/**
 * Parallel Port information:
 * address 0 (R/W):  data pins.  On read, the last byte written is returned.
 *
 *
 * address 1 (R): status register
 * \code
 *   1 0 0 0 0 0 00 <-- default
 *   ^ ^ ^ ^ ^ ^ ^
 *   | | | | | | +- Undefined
 *   | | | | | +--- IRQ (undefined?)
 *   | | | | +----- printer has error condition
 *   | | | +------- printer is not selected.
 *   | | +--------- printer has paper (online)
 *   | +----------- printer is asserting 'ack'
 *   +------------- printer busy (active low).
 * \endcode
 *
 * address 2 (R/W): control register.
 * \code
 *   00 0 0 1 0 1 1  <-- default
 *   ^  ^ ^ ^ ^ ^ ^
 *   |  | | | | | +-- Strobe (active low)
 *   |  | | | | +---- Auto feed (active low)
 *   |  | | | +------ Initialize
 *   |  | | +-------- Select (active low)
 *   |  | +---------- Interrupt Control
 *   |  +------------ Bidirectional control (unimplemented)
 *   +--------------- Unused
 * \endcode
 **/
void CAliM1543C::lpt_reset()
{
	state.lpt_data = '\xff';
	state.lpt_status = 0xd8;  // busy, ack, online, error
	state.lpt_control = 0x0c; // select, init
	state.lpt_init = false;
}

/**
 * Read a byte from one of the parallel port controller's registers.
 **/
u8 CAliM1543C::lpt_read(u32 address)
{
	if (!(state.superio_ldn_regs[3][0x30] & 0x01)) return 0xff;
	u8  data = 0;
	switch (address)
	{
	case 0:
		data = state.lpt_data;
		break;

	case 1:
		data = state.lpt_status;
		if ((state.lpt_status & 0x80) == 0 && (state.lpt_control & 0x01) == 0)
		{
			if (state.lpt_status & 0x40)
			{ // test ack
				state.lpt_status &= ~0x40;  // turn off ack
			}
			else
			{
				state.lpt_status |= 0x40;   // set ack.
				state.lpt_status |= 0x80;   // set (not) busy.
			}
		}
		break;

	case 2:
		data = state.lpt_control;
	}

#ifdef DEBUG_LPT
	printf("%%LPT-I-READ: port %d = %x\n", address, data);
#endif
	return data;
}

/**
 * Write a byte to one of the parallel port controller's registers.
 **/
void CAliM1543C::lpt_write(u32 address, u8 data)
{
	if (!(state.superio_ldn_regs[3][0x30] & 0x01)) return;
#ifdef DEBUG_LPT
	printf("%%LPT-I-WRITE: port %d = %x\n", address, data);
#endif
	switch (address)
	{
	case 0:
		state.lpt_data = data;
		break;

	case 1:
		break;

	case 2:
		if ((data & 0x04) == 0)
		{
			state.lpt_init = true;
			state.lpt_status = 0xd8;
		}
		else
		{
			if (data & 0x08)
			{   // select bit
				if (data & 0x01)
				{ // strobe?
					state.lpt_status &= ~0x80;  // we're busy

					// do the write!
					if (lpt && state.lpt_init)
						fputc(state.lpt_data, lpt);
					if (state.lpt_control & 0x10)
					{
						pic_interrupt(0, 7);
					}
				}
				else
				{

					// ?
				}
			}
		}

		state.lpt_control = data;
	}
}

void CAliM1543C::set_floppy_presence(bool driveA, bool driveB)
{
    u8 cmos = 0;
    if (driveA) cmos |= 0x40; // Drive A: 1.44MB 3.5"
    if (driveB) cmos |= 0x04; // Drive B: 1.44MB 3.5"
    state.toy_stored_data[0x10] = cmos;
    state.superio_ldn_regs[0][0x30] = 0x01;
}

/**
 * Check if threads are still running.
 **/
void CAliM1543C::check_state()
{
	if (myThread && !myThread->isRunning())
		FAILURE(Thread, "ALi thread has died");

	// HACK HACK HACK HACK ALERT
	// ENABLES BSD SYSTEMS TO BOOT WITHOUT panic: pci_display_console: no device at 255/255/0
	// SRM builds the HWPRB / CTB Fully! BUT! Does not properly set the CTB turboslot
	// Theory: TGA/TGA2 would set properly, but SRM does not 'know' about others to set. 
	// Investigate possibly how to make SRM happy. This appears to be an issue on real hardware
	// as well
	static bool ctb_fixed = false;

	// Without a firmware card identity, repair is unambiguous only with one VGA.
	const CVGA* console = !ctb_fixed ? cSystem->get_sole_vga_device() : nullptr;
	if (console)
	{
		const u64 HWRPB_BASE = U64(0x2000);
		const u64 HWRPB_MAGIC = U64(0x0000004250525748); // "HWRPB\0\0\0" LE
		const u64 CTB_OFF_OFF = U64(0xB8);  // offset of rpb_ctb_off in HWRPB
		const u64 CTB_TYPE_OFF = U64(0x00);   // offset of ctb_type in CTB
		const u64 CTB_TERM_TYPE_OFF = U64(0x38); // terminal type within CTB type 4
		const u64 CTB_TS_OFF = U64(0xF0);   // offset of ctb_turboslot in CTB
		const u64 CTB_MULTIPURPOSE = U64(4);
		const u64 CTB_TERM_GRAPHICS = U64(3);

		// Step 1: Check if HWRPB is built (magic signature present)
		u64 magic = cSystem->ReadMem(HWRPB_BASE + U64(0x08), 64, this);
		// Temporary: dump HWRPB fields to find ctb_off
		if (magic != HWRPB_MAGIC)
			return;  // SRM hasn't built the HWRPB yet

		// Step 2: Read CTB offset from HWRPB
		u64 ctb_off = cSystem->ReadMem(HWRPB_BASE + CTB_OFF_OFF, 64, this);
		if (ctb_off == 0 || ctb_off > U64(0x100000))
			return;  // CTB offset not valid (yet)

		// After step 2, before step 3, add a one-shot CTB dump:
		u64 ctb_phys = HWRPB_BASE + ctb_off;

#ifdef DEBUG_HWRPB_TURBOSLOT
		static bool ctb_dumped = false;
		if (!ctb_dumped) {
			printf("%s: CTB dump at phys 0x%" PRIx64 " (ctb_off=0x%" PRIx64 "):\n",
				devid_string, ctb_phys, ctb_off);
			for (int i = 0; i < 0x100; i += 8) {
				u64 val = cSystem->ReadMem(ctb_phys + i, 64, this);
				printf("  CTB+0x%03X (0x%05" PRIx64 "): 0x%016" PRIx64 "\n",
					i, ctb_phys + i, val);
			}
			ctb_dumped = true;
		}
#endif

		// Step 3: CTB type 4 is multipurpose; its terminal type identifies graphics.
		u64 ctb_type = cSystem->ReadMem(ctb_phys + CTB_TYPE_OFF, 64, this);
		if (ctb_type == 0)
			return;  // CTB not populated yet — try again later
		if (ctb_type != CTB_MULTIPURPOSE)
		{
			// This workaround only handles the multipurpose CTB layout.
			ctb_fixed = true;
			return;
		}
		if (cSystem->ReadMem(ctb_phys + CTB_TERM_TYPE_OFF, 64, this) != CTB_TERM_GRAPHICS)
			return; // Leave serial and not-yet-populated terminal metadata alone.

		// Step 4: Read current turboslot value
		u64 turboslot = cSystem->ReadMem(ctb_phys + CTB_TS_OFF, 64, this);
		// pci_bus() selects a Tsunami hose; directly attached cards are on PCI bus 0.
		const int vga_hose = console->pci_bus() & 0xFF;
		const int vga_dev = console->pci_dev() & 0xFF;
		const u64 ts = (((u64)vga_hose) << 24) | (U64(3) << 16) | ((u64)vga_dev);

		// Step 5: Check if it needs fixing
		// SRM leaves this as all-FF for non-TGA adapters
		bool needs_fix = ((turboslot & U64(0xFFFF)) == U64(0xFFFF));

		if (needs_fix)
		{
			cSystem->WriteMem(ctb_phys + CTB_TS_OFF, 64, ts, this);
			static bool printed = false;
#ifdef DEBUG_HWRPB_TURBOSLOT
			if (!printed) {
				printf("%s: fixed CTB turboslot at phys 0x%" PRIx64
					" from 0x%" PRIx64 " to 0x%08" PRIx64
					" (PCI hose=%d bus=0 dev=%d)\n",
					devid_string, ctb_phys + CTB_TS_OFF,
					turboslot, ts, vga_hose, vga_dev);
			}
#endif
		}
		else if (turboslot == ts)
		{
			// Our value is there and SRM didn't overwrite it — we're done
			ctb_fixed = true;
		}
	}
}

CAliM1543C* theAli = 0;
