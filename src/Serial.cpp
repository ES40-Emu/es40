/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023-2026 by gdwnldsKSC
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
  * Contains the code for the emulated Serial Port devices.
  *
  * $Id$
  *
  * X-1.51       Camiel Vanderhoeven                             03-JUN-2008
  *      Fixed misplaced semicolon.
  *
  * X-1.50       Camiel Vanderhoeven                             01-JUN-2008
  *      Error message if execution of 'action' fails on Windows.
  *
  * X-1.49       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.47       Camiel Vanderhoeven                             31-MAR-2008
  *      Compileable on OpenVMS.
  *
  * X-1.46       Camiel Vanderhoeven                             19-MAR-2008
  *      Initialize breakHit.
  *
  * X-1.45       Camiel Vanderhoeven                             13-MAR-2008
  *      Fixed FAILURE macro's for Unix.
  *
  * X-1.44       Camiel Vanderhoeven                             13-MAR-2008
  *      Formatting.
  *
  * X-1.42       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.41       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.40       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.39       Camiel Vanderhoeven                             02-MAR-2008
  *      Natural way to specify large numeric values ("10M") in the config file.
  *
  * X-1.38       Brian Wheeler                                   29-FEB-2008
  *      Restart serial port connection if lost.
  *
  * X-1.37       Brian Wheeler                                   27-FEB-2008
  *      Avoid compiler warnings.
  *
  * X-1.36       Camiel Vanderhoeven                             06-JAN-2008
  *      Proper interrupt handling.
  *
  * X-1.35       Camiel Vanderhoeven                             02-JAN-2008
  *      Cleanup.
  *
  * X-1.34       Camiel Vanderhoeven                             30-DEC-2007
  *      Print file id on initialization.
  *
  * X-1.33       Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.32       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.31       Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.30       Camiel Vanderhoeven                             16-DEC-2007
  *      Added menu option to load state.
  *
  * X-1.29       Brian Wheeler                                   10-DEC-2007
  *      Better exec handling.
  *
  * X-1.28       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.27		Camiel Vanderhoeven								10-NOV-2007
  *	    Add possibility to save system state when not in debug mode.
  *
  * X-1.26		Camiel Vanderhoeven								09-NOV-2007
  *	    Drop LF when received; OpenVMS expects to receive a CR only on its
  *      console. This allows entering the password during the OpenVMS 8.3
  *      installation procedure.
  *
  * X-1.25       Camiel Vanderhoeven                             17-APR-2007
  *      Allow a telnet client program to be in a directory containing
  *      spaces on Windows ("c:\program files\putty\putty.exe")
  *
  * X-1.24       Camiel Vanderhoeven                             17-APR-2007
  *      Only include process.h on Windows.
  *
  * X-1.23       Camiel Vanderhoeven                             16-APR-2007
  *      Never start a telnet client when running as lockstep slave, because
  *      we want to receive a connection from the master instead.
  *
  * X-1.22       Camiel Vanderhoeven                             16-APR-2007
  *      Added possibility to start a Telnet client automatically.
  *
  * X-1.21       Camiel Vanderhoeven                             31-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.20	    Camiel Vanderhoeven				                26-MAR-2007
  *	Unintentional CVS commit / version number increase.
  *
  * X-1.19	    Camiel Vanderhoeven				                27-FEB-2007
  *   a) Moved tons of defines to telnet.h
  *   b) When this emulator is the lockstep-master, connect to the slave's
  *	    serial port.
  *
  * X-1.18	    Camiel Vanderhoeven				                27-FEB-2007
  *	    Define socklen_t as unsigned int on OpenVMS.
  *
  * X-1.17	    Camiel Vanderhoeven				                27-FEB-2007
  *	    Add support for OpenVMS.
  *
  * X-1.16	    Camiel Vanderhoeven				                20-FEB-2007
  *	    Use small menu to determine what to do when a <BREAK> is received.
  *
  * X-1.15	    Camiel Vanderhoeven				                16-FEB-2007
  *	    Directly use the winsock functions, don't use the CTelnet class
  *	    any more. windows and Linux code are more alike now.
  *
  * X-1.14	    Brian Wheeler					                13-FEB-2007
  *	    Formatting.
  *
  * X-1.13	    Camiel Vanderhoeven				                12-FEB-2007
  *	    Added comments.
  *
  * X-1.12       Camiel Vanderhoeven                             9-FEB-2007
  *      Added comments.
  *
  * X-1.11	    Camiel Vanderhoeven				                9-FEB-2007
  *	    Enable eating of first characters (needed for now for WINDOWS).
  *
  * X-1.10	    Brian Wheeler					                7-FEB-2007
  *	    Disable eating of first characters. Treat Telnet commands properly
  *	    for Linux.
  *
  * X-1.9	    Camiel Vanderhoeven				                7-FEB-2007
  * 	    Calls to trace_dev now use the TRC_DEVx macro's.
  *
  * X-1.8	    Camiel Vanderhoeven				                3-FEB-2007
  *   a)	Restructure Linux/Windows code mixing to make more sense.
  *   b)	Eat first incoming characters (so we don't burden the SRM with
  *	    weird incoming characters.
  *
  * X-1.7	    Camiel Vanderhoeven				                3-FEB-2007
  * 	    No longer start PuTTy. We might just want to do something wild like
  *	    connecting from a different machine!
  *
  * X-1.6        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.5	    Brian Wheeler					                3-FEB-2007
  *	    Get the Telnet port number from the configuration file.
  *
  * X-1.4	    Brian Wheeler					                3-FEB-2007
  *	    Add support for Linux.
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

  // =============================================================================
  // Design:
  //
  //   socket --> recv() --> raw buf --> cook IAC --> staging buffer --> FIFO
  //                                                      |              |
  //                                                      |              v
  //                                                 every execute()   guest reads
  //                                                   (~20 ms)       via ReadMem
  //
  //   - recv() and IAC cooking happen every RECV_TICKS cycles (~200ms).
  //     Cooked data goes into the staging buffer.
  //   - drain_staging() runs every execute() cycle (~20ms) but only
  //     delivers one character when the guest has consumed the previous
  //     one (FIFO empty).  This self-clocks to the emulated CPU speed,
  //     matching how a real UART works — the next character doesn't
  //     arrive until the line shifts it in, and the CPU is always fast
  //     enough to read it before then.
  //   - If staging still has data, recv() is skipped — TCP provides
  //     backpressure at the OS level.
  // =============================================================================

#include "StdAfx.h"
#include "Serial.h"
#include "System.h"
#include "AliM1543C.h"

#include "network/lockstep.h"

#define UART_BASE_CLOCK  1843200
#define CYCLE_TIME_MS    20

#define RECV_TICKS  10

int iCounter = 0;

#define FIFO_SIZE 1024

//#define DEBUG_SERIAL 1

/**
 * Constructor.
 **/
CSerial::CSerial(CConfigurator* cfg, CSystem* c, u16 number) : CSystemComponent(cfg, c)
{
	state.iNumber = number;
}

const CSystemComponent* CSerial::memory_decode_owner() const noexcept
{
	return isa_bridge ? static_cast<const CSystemComponent*>(isa_bridge) : this;
}

bool CSerial::decodes_memory_access(int index, u64 address, int, bool) const noexcept
{
	if (!(index == 0 && address < 8))
		return false;
	if (!isa_bridge)
		return true;
	const u32 port = 0x3f8u - 0x100u * (u32)state.iNumber + (u32)address;
	return isa_bridge->isa_io_decode(port) != CAliM1543C::IsaIoDecode::None;
}

bool CSerial::uses_subtractive_decode(int index, u64 address, int dsize,
	bool write) const noexcept
{
	if (!isa_bridge || !decodes_memory_access(index, address, dsize, write))
		return false;
	const u32 port = 0x3f8u - 0x100u * (u32)state.iNumber + (u32)address;
	return isa_bridge->isa_io_decode(port) == CAliM1543C::IsaIoDecode::Subtractive;
}

/**
 * Initialize the Serial port device.
 **/
void CSerial::init()
{
	disabled = myCfg->get_bool_value("disabled");
	raw_mode = myCfg->get_bool_value("raw_mode");
	null_attach = myCfg->get_bool_value("null_attach");
	listenPort = (int)myCfg->get_num_value("port", false, 8000 + state.iNumber);
	listenAddress = myCfg->get_text_value("listen_address", "");

	char    s[1000];
	char* nargv = s;
	int     i = 0;

	cSystem->RegisterMemory(this, 0,
		U64(0x00000801fc0003f8) - (0x100 * state.iNumber), 8);

	if (disabled)
	{
		// Port is claimed in the address map but presents itself as a missing UART:
		// scratchpad writes don't stick, MSR/LSR read 0xff, IIR=0xff. KDCOM and
		// SERIAL.SYS conclude "no UART here" and skip further probing/polling.
		state.rcvW = 0;
		state.rcvR = 0;
		listenSocket = INVALID_SOCKET;
		connectSocket = INVALID_SOCKET;
		printf("%s: disabled - guest will see no UART at this address.\n", devid_string);
		return;
	}

	if (null_attach)
	{
		// Bit-bucket UART: port exists in the address map and presents itself as a
		// healthy idle 16550 — TX shift register always empty (THRE/TSRE), modem
		// signals up (CTS/DSR), RX FIFO permanently empty. TX bytes from the guest
		// are silently discarded; nothing ever arrives at RX. No socket is opened,
		// no I/O thread runs, no <BREAK> menu. ReadMem/WriteMem stay on the normal
		// path so register reads return live values (e.g. LSR=0x60 because rcvR==rcvW),
		// MCR.LOOP self-test still works (loopback writes go to rcvBuffer, never the
		// socket), and TX-empty interrupts still fire when the guest enables IER.bit1.
		state.rcvW = 0;
		state.rcvR = 0;
		state.bTHR = 0x00;
		state.bRDR = 0x00;
		state.bBRB_LSB = 0x00;
		state.bBRB_MSB = 0x00;
		state.bIER = 0x00;
		state.bIIR = 0x01;  // no interrupt pending
		state.bFCR = 0x00;
		state.bLCR = 0x00;
		state.bMCR = 0x00;
		state.bLSR = 0x60;  // THRE, TSRE
		state.bMSR = 0x30;  // CTS, DSR
		state.bSPR = 0x00;
		state.serial_cycles = 0;
		state.thre_pending = false;
		iac_carry_len = 0;
		in_subneg = false;
		stageLen = 0;
		myThread = 0;
		listenSocket = INVALID_SOCKET;
		connectSocket = INVALID_SOCKET;
		printf("%s: null_attach - TX discarded, RX always empty.\n", devid_string);
		return;
	}

	// Start Telnet server
	listenSocket = INVALID_SOCKET;
	connectSocket = INVALID_SOCKET;
#if defined(_WIN32)

  // Windows Sockets only work after calling WSAStartup.
	WSADATA wsa;
	const int startup_error = WSAStartup(0x0101, &wsa);
	if (startup_error != 0)
		FAILURE_3(Configuration,
			"%s: serial port %d WSAStartup failed (Winsock error %d)",
			devid_string, listenPort, startup_error);
#endif // defined (_WIN32)
	const auto listener_failure = [this](const char* operation)
	{
		// Capture the original error before cleanup changes it.
#if defined(_WIN32)
		const int error = WSAGetLastError();
		if (listenSocket != INVALID_SOCKET)
			closesocket(listenSocket);
		listenSocket = INVALID_SOCKET;
		WSACleanup();
		char error_text[256] = {};
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, error, 0, error_text, sizeof(error_text), nullptr);
		error_text[strcspn(error_text, "\r\n")] = '\0';
		FAILURE_5(Configuration,
			"%s: serial port %d %s failed: %s (Winsock error %d)",
			devid_string, listenPort, operation, error_text, error);
#else
		const int error = errno;
		if (listenSocket != INVALID_SOCKET)
			close(listenSocket);
		listenSocket = INVALID_SOCKET;
		FAILURE_5(Configuration,
			"%s: serial port %d %s failed: %s (errno %d)",
			devid_string, listenPort, operation, strerror(error), error);
#endif
	};

	listenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSocket == INVALID_SOCKET)
		listener_failure("socket");

	struct sockaddr_in Address = {};
	Address.sin_addr.s_addr = INADDR_ANY;
	Address.sin_port = htons((u16)(listenPort));
	Address.sin_family = AF_INET;

	// Listen on one address only, if the config asks for it.
	if (listenAddress[0])
	{
		Address.sin_addr.s_addr = inet_addr(listenAddress);
		if (Address.sin_addr.s_addr == INADDR_NONE)
			FAILURE_2(Configuration, "%s: listen_address \"%s\" is not an IPv4 address",
				devid_string, listenAddress);
	}

	int optval = 1;
#if defined(_WIN32)
	// Windows SO_REUSEADDR can share an occupied port with another listener.
	const int reuse_option = SO_EXCLUSIVEADDRUSE;
#else
	const int reuse_option = SO_REUSEADDR;
#endif
	if (setsockopt(listenSocket, SOL_SOCKET, reuse_option, (char*)&optval,
		sizeof(optval)) != 0)
		listener_failure("setsockopt");
	// Do not let listen() auto-bind an ephemeral port after a failed bind().
	if (bind(listenSocket, (struct sockaddr*)&Address, sizeof(Address)) != 0)
		listener_failure("bind");
	if (listen(listenSocket, 8) != 0)
		listener_failure("listen");

	printf("%s: Waiting for connection on port %d.\n", devid_string, listenPort);

	WaitForConnection();

#if defined(IDB) && defined(LS_MASTER)
	struct sockaddr_in  dest_addr;
	int                 result = -1;

	throughSocket = socket(AF_INET, SOCK_STREAM, 0);

	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons((u16)(listenPort + state.iNumber));
	dest_addr.sin_addr.s_addr = inet_addr(ls_IP);

	printf("%s: Waiting to initiate remote connection to %s.\n", devid_string,
		ls_IP);

	while (result == -1)
	{
		result = connect(throughSocket, (struct sockaddr*)&dest_addr,
			sizeof(struct sockaddr));
	}
#endif
	state.rcvW = 0;
	state.rcvR = 0;

	state.bTHR = 0x00;
	state.bRDR = 0x00;
	state.bBRB_LSB = 0x00;
	state.bBRB_MSB = 0x00;
	state.bIER = 0x00;
	state.bIIR = 0x01;  // no interrupt
	state.bFCR = 0x00;
	state.bLCR = 0x00;
	state.bMCR = 0x00;  // Important: reads of MCR.LOOP gate the case-0 LOOP-mode write path
	state.bLSR = 0x60;  // THRE, TSRE
	state.bMSR = 0x30;  // CTS, DSR
	state.bSPR = 0x00;
	state.serial_cycles = 0;
	state.thre_pending = false;
	iac_carry_len = 0;
	in_subneg = false;
	stageLen = 0;
	myThread = 0;

	printf("%s: $Id$\n",
		devid_string);
}

void CSerial::start_threads()
{
	char  buffer[5];
	if (disabled || null_attach)
		return;
	if (!myThread)
	{
		sprintf(buffer, "srl%d", state.iNumber);
		myThread = new CThread(buffer);
		printf(" %s", myThread->getName().c_str());
		StopThread = false;
		myThread->start(*this);
	}
}

void CSerial::stop_threads()
{
	if (disabled || null_attach)
		return;
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
CSerial::~CSerial()
{
	stop_threads();
}

u64 CSerial::ReadMem(int index, u64 address, int dsize)
{
	u8  d;

	if (disabled)
		return 0xffu;  // missing-UART signature

	switch (address)
	{
	case 0: // data buffer
		if (state.bLCR & 0x80)
		{
			return state.bBRB_LSB;
		}
		else
		{
			if (state.rcvR != state.rcvW)
			{
				state.bRDR = state.rcvBuffer[state.rcvR];
				state.rcvR++;
				if (state.rcvR == FIFO_SIZE)
					state.rcvR = 0;
				TRC_DEV4("Read character %02x (%c) on serial port %d\n", state.bRDR,
					printable(state.bRDR), state.iNumber);
#if defined(DEBUG_SERIAL)
				printf("Read character %02x (%c) on serial port %d\n", state.bRDR,
					printable(state.bRDR), state.iNumber);
#endif
			}
			else
			{
				TRC_DEV2("Read past FIFO on serial port %d\n", state.iNumber);
#if defined(DEBUG_SERIAL)
				printf("Read past FIFO on serial port %d\n", state.iNumber);
#endif
			}

			// Received-data interrupt follows the FIFO — it may have just emptied.
			eval_interrupts();
			return state.bRDR;
		}

	case 1:
		if (state.bLCR & 0x80)
		{
			return state.bBRB_MSB;
		}
		else
		{
			return state.bIER;
		}

	case 2: //interrupt cause
		d = state.bIIR;
		if (d == 0x02)
			state.thre_pending = false;  // reading IIR acknowledges THRE
		eval_interrupts();               // recompute IIR and re-drive the IRQ line
		return d;

	case 3:
		return state.bLCR;

	case 4:
		return state.bMCR;

	case 5: //serialization state
		if (state.rcvR != state.rcvW)
			state.bLSR = 0x61;    // THRE, TSRE, RxRD
		else
			state.bLSR = 0x60;    // THRE, TSRE
		return state.bLSR;

	case 6:
		return state.bMSR;

	default:
		return state.bSPR;
	}
}

void CSerial::WriteMem(int index, u64 address, int dsize, u64 data)
{
	u8    d;
	d = (u8)data;

	if (disabled)
		return;  // ignore guest writes to a disabled port

	switch (address)
	{
	case 0:
		if (state.bLCR & 0x80)   // divisor latch access bit set
		{

			// LSB of divisor latch
			state.bBRB_LSB = d;
		}
		else
		{
			if (state.bMCR & 0x10)
			{
				// MCR.LOOP set: deliver TX byte to our own RX FIFO instead of the wire.
				// Windows 2000 UART detection writes a sentinel here and expects to read it back via LSR.DR.
				int nextW = state.rcvW + 1;
				if (nextW == FIFO_SIZE)
					nextW = 0;
				if (nextW != state.rcvR)
				{
					state.rcvBuffer[state.rcvW] = (char)d;
					state.rcvW = nextW;
				}
			}
			else
			{
				// Transmit Hold Register
				write((const char*)&d, 1);
			}
			TRC_DEV4("Write character %02x (%c) on serial port %d\n", d, printable(d),
				state.iNumber);
#if defined(DEBUG_SERIAL)
			printf("Write character %02x (%c) on serial port %d\n", d, printable(d),
				state.iNumber);
#endif
			// TX is instantaneous: the holding register is empty again, so
			// re-arm THRE (delivered only if the THRE interrupt is enabled).
			state.thre_pending = true;
			eval_interrupts();
		}
		break;

	case 1:
		if (state.bLCR & 0x80)   // divisor latch access bit set
		{

			// MSB of divisor latch
			state.bBRB_MSB = d;
		}
		else
		{
			// Interrupt Enable Register.  Enabling the THRE interrupt while the
			// (instantly-drained) holding register is empty arms one THRE edge.
			u8 prev_ier = state.bIER;
			state.bIER = d;
			if ((d & 0x02) && !(prev_ier & 0x02))
				state.thre_pending = true;
			eval_interrupts();
		}
		break;

	case 2:
		state.bFCR = d;
		break;

	case 3:
		state.bLCR = d;
		break;

	case 4:
	{
		u8 prev_mcr = state.bMCR;
		state.bMCR = d;
		if (d & 0x10)
		{
			// MCR.LOOP set: mirror modem-control output bits into MSR status bits.
			// PC16550D loopback: DTR->DSR, RTS->CTS, OUT1->RI, OUT2->DCD.
			// Windows 2000 SERIAL.SYS uses this to confirm a 16550 by toggling MCR.OUT1
			// and watching MSR.RI track it.
			u8 status = 0;
			if (d & 0x01) status |= 0x20;  // DTR  -> DSR
			if (d & 0x02) status |= 0x10;  // RTS  -> CTS
			if (d & 0x04) status |= 0x40;  // OUT1 -> RI
			if (d & 0x08) status |= 0x80;  // OUT2 -> DCD
			state.bMSR = status | (state.bMSR & 0x0f);
		}
		else
		{
			// Loopback released: present the default "modem connected" status the rest of the code assumes.
			state.bMSR = (state.bMSR & 0x0f) | 0x30;
		}
		if (prev_mcr != d)
			printf("SRL%d: MCR %02x -> %02x (LOOP=%d DTR=%d RTS=%d OUT1=%d OUT2=%d), MSR now %02x\n",
				state.iNumber, prev_mcr, d,
				(d >> 4) & 1, d & 1, (d >> 1) & 1, (d >> 2) & 1, (d >> 3) & 1,
				state.bMSR);
		break;
	}

	default:
		state.bSPR = d;
	}
}

void CSerial::eval_interrupts()
{
	if (disabled)
		return;

	// RX-available is a level (follows the receive FIFO); THR-empty is edge-
	// latched in thre_pending (set on THR write / THRE enable, cleared on IIR
	// read) because es40's instant TX would make a plain THRE level storm.
	state.bIIR = 0x01;        // no interrupt
	if ((state.bIER & 0x01) && (state.rcvR != state.rcvW))
		state.bIIR = 0x04;    // received data available
	else if ((state.bIER & 0x02) && state.thre_pending)
		state.bIIR = 0x02;    // transmitter holding register empty

	// Drive IRQ4 (serial0) / IRQ3 (serial1) as a level following the cause;
	// pic_set_line() makes one edge per rising transition, retracting on fall.
	theAli->pic_set_line(0, 4 - state.iNumber, state.bIIR > 0x01);
}

void CSerial::write(const char* s, int dsize)
{
	if (disabled || null_attach)
		return;  // null_attach: drop TX silently; no socket to send on
	int val = send(connectSocket, s, dsize, 0);
}

void CSerial::write_cstr(const char* s)
{
	write(s, (int)strlen(s));
}

/**
 * receive  — Push data into the FIFO.  Returns number of bytes consumed.
 *            Stops when FIFO is full.
 **/

int CSerial::receive(const char* data, int dsize)
{
	int consumed = 0;

	while (dsize)
	{
		int nextW = state.rcvW + 1;
		if (nextW == FIFO_SIZE)
			nextW = 0;

		if (nextW == state.rcvR)
			break;  // FIFO full

		state.rcvBuffer[state.rcvW] = *data;
		state.rcvW = nextW;
		data++;
		dsize--;
		consumed++;
		eval_interrupts();
	}

	return consumed;
}

/**
 * drain_staging  — Move data from the staging buffer into the FIFO,
 *                  limited to max_bytes.  Compacts after partial drain.
 **/
void CSerial::drain_staging()
{
	if (stageLen == 0)
		return;

	// Only deliver when the guest has consumed the previous character.
	// This self-clocks to the emulated CPU speed, just like a real
	// UART where the next character doesn't arrive until the baud
	// rate shifts it in — and the CPU is always fast enough to read
	// it before then.
	if (state.rcvR != state.rcvW)
		return;  // previous character not yet read

	// Deliver exactly one character
	receive(stageBuf, 1);
	stageLen--;
	if (stageLen > 0)
		memmove(stageBuf, stageBuf + 1, stageLen);
}


/**
 * Thread entry point.
 **/
void CSerial::run()
{
	if (disabled)
		return;
	try
	{
		for (;;)
		{
			if (StopThread)
				return;
			execute();
			CThread::sleep(20);
		}
	}

	catch (CException& e)
	{
		printf("Exception in Serial thread: %s.\n", e.displayText().c_str());

		// Let the thread die...
	}
}

/**
 * Check if threads are still running.
 *
 * Enter serial port menu if <break> was received.
 **/
void CSerial::check_state()
{
	if (disabled || null_attach)
		return;
	if (breakHit)
		serial_menu();

	if (myThread && !myThread->isRunning())
		FAILURE(Thread, "Serial thread has died");
}

void CSerial::serial_menu()
{
	fd_set          readset;
	unsigned char   buffer[FIFO_SIZE + 1];
	int             size;
	struct timeval  tv;
	bool            exitLoop = false;
	bool            waitForChoice = false;

	cSystem->stop_threads();

	write_cstr("\r\n<BREAK> received. What do you want to do?\r\n");
	write_cstr("     0. Continue\r\n");
#if defined(IDB)
	write_cstr("     1. End run\r\n");
#else
	write_cstr("     1. Exit emulator gracefully\r\n");
	write_cstr("     2. Abort emulator (no changes saved)\r\n");
	write_cstr("     3. Save state to autosave.axp and continue\r\n");
	write_cstr("     4. Load state from autosave.axp and continue\r\n");
	write_cstr("     5. Save state to autosave.axp and exit\r\n");
#endif
	while (!exitLoop)
	{
		FD_ZERO(&readset);
		FD_SET(connectSocket, &readset);
		tv.tv_sec = 60;
		tv.tv_usec = 0;
		int ready = select(connectSocket + 1, &readset, NULL, NULL,
			waitForChoice ? NULL : &tv);
		if (ready <= 0)
		{
			if (ready < 0)
				printf("%%SRL-W-DISCONNECT: Serial menu connection failed. Continuing emulation.\n");
			else
				write_cstr("%SRL-I-TIMEOUT: no timely answer received. Continuing emulation.\r\n");
			break;  // leave loop
		}

#if defined(_WIN32) || defined(__VMS)
		size = recv(connectSocket, (char*)buffer, FIFO_SIZE, 0);
#else
		size = read(connectSocket, &buffer, FIFO_SIZE);
#endif
		if (size <= 0)
		{
			printf("%%SRL-W-DISCONNECT: Serial menu connection closed or failed. Continuing emulation.\n");
			break;
		}
		switch (buffer[0])
		{
		case '0':
			write_cstr("%SRL-I-CONTINUE: continuing emulation.\r\n");
			exitLoop = true;
			break;

		case '1':
			write_cstr("%SRL-I-EXIT: exiting emulation gracefully.\r\n");
			FAILURE(Graceful, "Graceful exit");
			break;

		case '2':
			write_cstr("%SRL-I-ABORT: aborting emulation.\r\n");
			FAILURE(Abort, "Aborting");
			break;

		case '3':
			write_cstr("%SRL-I-SAVESTATE: Saving state to autosave.axp.\r\n");
			try
			{
				cSystem->SaveState("autosave.axp");
			}
			catch (const CException& e)
			{
				// Saving does not replace live guest state, so a failed write can safely resume. 
				write_cstr("%SRL-E-SAVESTATE: Could not save state: ");
				write_cstr(e.displayText().c_str());
				write_cstr("\r\n");
			}
			write_cstr("%SRL-I-CONTINUE: continuing emulation.\r\n");
			exitLoop = true;
			break;

		case '4':
			write_cstr("%SRL-I-LOADSTATE: Loading state from autosave.axp.\r\n");
			if (!cSystem->RestoreState("autosave.axp"))
				write_cstr("%SRL-W-LOADSTATE: Restore skipped; current state is unchanged.\r\n");
			write_cstr("%SRL-I-CONTINUE: continuing emulation.\r\n");
			exitLoop = true;
			break;

		case '5':
			write_cstr("%SRL-I-SAVESTATE: Saving state to autosave.axp.\r\n");
			try
			{
				cSystem->SaveState("autosave.axp");
			}
			catch (const CException& e)
			{
				write_cstr("%SRL-E-SAVESTATE: Could not save state: ");
				write_cstr(e.displayText().c_str());
				write_cstr("\r\n%SRL-I-STOPPED: Emulation remains stopped. Choose 5 to retry, 0 to continue, or 1 to exit.\r\n");
				waitForChoice = true;
				break;
			}
			write_cstr("%SRL-I-EXIT: State saved; exiting emulation gracefully.\r\n");
			FAILURE(Graceful, "Graceful exit");
			break;

		default:
			write_cstr("%SRL-W-INVALID: Not a valid answer.\r\n");
			break;
		}
	}

	breakHit = false;
	cSystem->start_threads();
}

// ---------------------------------------------------------------------------
// execute  — Main loop body, called every ~20ms from the serial thread.
// ---------------------------------------------------------------------------
void CSerial::execute()
{
	fd_set          readset;
	unsigned char   raw[FIFO_SIZE + 16];
	unsigned char   cbuffer[FIFO_SIZE + 1];
	unsigned char*  b;
	unsigned char*  c;
	unsigned char*  end;
	ssize_t         size;
	struct timeval  tv;

	drain_staging();

	// ------------------------------------------------------------------
	// Every RECV_TICKS cycles: read from the socket and cook telnet out
	// ------------------------------------------------------------------
	state.serial_cycles++;
	if (state.serial_cycles >= RECV_TICKS)
	{
		// If staging buffer still has data, don't pull more off the socket.
		// TCP backpressure holds the rest at the OS level.
		if (stageLen > 0)
		{
			state.serial_cycles = 0;
			eval_interrupts();
			return;
		}

		FD_ZERO(&readset);
		FD_SET(connectSocket, &readset);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		if (select(connectSocket + 1, &readset, NULL, NULL, &tv) > 0)
		{
			unsigned char* recv_buf = raw + iac_carry_len;

#if defined(_WIN32) || defined(__VMS)
			size = recv(connectSocket, (char*)recv_buf, FIFO_SIZE, 0);
#else
			size = read(connectSocket, recv_buf, FIFO_SIZE);
#endif

			extern int  got_sigint;
			if (size <= 0 && !got_sigint)
			{
				printf("%%SRL-W-DISCONNECT: Write socket closed on other end for serial port %d.\n",
					state.iNumber);
				printf("-SRL-I-WAITFOR: Waiting for a new connection on port %d.\n",
					listenPort);
				WaitForConnection();
				return;
			}

			if (size <= 0)
			{
				state.serial_cycles = 0;
				eval_interrupts();
				return;
			}

			// Prepend carryover from partial telnet sequence
			if (iac_carry_len > 0)
			{
				memcpy(raw, iac_carry, iac_carry_len);
				size += iac_carry_len;
				iac_carry_len = 0;
			}

			// raw_mode: skip telnet IAC cooking AND the staging buffer's baud-rate
			// drip-feed.  The byte stream must stay 8-bit clean and flow at line
			// rate for windbg/kgdb, which sends arbitrary binary (including 0xff)
			// in KD packets and times out if delivery is throttled.
			if (raw_mode)
			{
				int delivered = receive((const char*)raw, (int)size);
				if (delivered < (int)size)
					printf("%%SRL-W-RAWFULL: raw_mode FIFO full on port %d, dropped %d byte(s)\n",
						state.iNumber, (int)size - delivered);
				state.serial_cycles = 0;
				eval_interrupts();
				return;
			}

			b = raw;
			c = cbuffer;
			end = raw + size;

			while (b < end)
			{
				// ----- Subnegotiation scan (SB ... IAC SE) -----
				if (in_subneg)
				{
					while (b < end)
					{
						if (*b == IAC)
						{
							if (b + 1 >= end)
							{
								iac_carry[0] = IAC;
								iac_carry_len = 1;
								b = end;
								break;
							}
							if (*(b + 1) == SE)
							{
								b += 2;
								in_subneg = false;
								break;
							}
							else if (*(b + 1) == IAC)
							{
								b += 2;
							}
							else
							{
								b += 2;
							}
						}
						else
						{
							b++;
						}
					}
					continue;
				}

				// ----- Telnet IAC with bounds-safe lookahead -----
				if (*b == IAC)
				{
					ssize_t remaining = end - b;

					if (remaining < 2)
					{
						iac_carry[0] = IAC;
						iac_carry_len = 1;
						b = end;
						break;
					}

					unsigned char cmd = *(b + 1);

					if (cmd == IAC)
					{
						*c++ = 0xFF;
						b += 2;
						continue;
					}
					else if (cmd >= WILL && cmd <= DONT)
					{
						if (remaining < 3)
						{
							for (ssize_t i = 0; i < remaining; i++)
								iac_carry[i] = b[i];
							iac_carry_len = (int)remaining;
							b = end;
							break;
						}
						b += 3;
						continue;
					}
					else if (cmd == SB)
					{
						in_subneg = true;
						b += 2;
						continue;
					}
					else if (cmd == BREAK)
					{
						breakHit = true;
						b += 2;
						continue;
					}
					else if (cmd == AYT)
					{
						b += 2;
						continue;
					}
					else
					{
						b += 2;
						continue;
					}
				}

				// ----- Normal data byte -----
				*c = *b;
				c++;
				b++;
			}

			// Move cooked data into the staging buffer.
			// drain_staging() will drip-feed it into the FIFO at baud rate
			// on subsequent execute() cycles.
			if (c > cbuffer)
			{
				int cooked_len = (int)(c - cbuffer);

				if (cooked_len <= (int)(STAGE_SIZE - stageLen))
				{
					memcpy(stageBuf + stageLen, cbuffer, cooked_len);
					stageLen += cooked_len;
				}
				else
				{
					// Staging buffer full — this means the guest is severely
					// behind.  Copy what fits; the rest is lost, but this
					// should only happen under extreme conditions.
					int space = STAGE_SIZE - stageLen;
					if (space > 0)
					{
						memcpy(stageBuf + stageLen, cbuffer, space);
						stageLen += space;
					}
#if defined(DEBUG_SERIAL)
					printf("%%SRL-W-STAGEDROP: staging buffer full on port %d, "
						"dropped %d bytes\n", state.iNumber, cooked_len - space);
#endif
				}
			}
		}

		state.serial_cycles = 0;
	}

	eval_interrupts();
}


static u32  srl_magic1 = 0x5A15A15A;
static u32  srl_magic2 = 0x1A51A51A;

/**
 * Save state to a Virtual Machine State file.
 **/
int CSerial::SaveState(FILE* f)
{
	long  ss = sizeof(state);

	fwrite(&srl_magic1, sizeof(u32), 1, f);
	fwrite(&ss, sizeof(long), 1, f);
	fwrite(&state, sizeof(state), 1, f);
	fwrite(&srl_magic2, sizeof(u32), 1, f);
	printf("%s: %d bytes saved.\n", devid_string, (int)ss);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CSerial::RestoreState(FILE* f)
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

	if (m1 != srl_magic1)
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

	if (ss != sizeof(state))
	{
		printf("%s: STRUCT SIZE does not match!\n", devid_string);
		return -1;
	}

	r = fread(&state, sizeof(state), 1, f);
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

	if (m2 != srl_magic2)
	{
		printf("%s: MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	printf("%s: %d bytes restored.\n", devid_string, (int)ss);
	return 0;
}

void CSerial::WaitForConnection()
{
	struct sockaddr_in  Address;
	socklen_t           nAddressSize = sizeof(struct sockaddr_in);
	const char* telnet_options = "%c%c%c";
	char                buffer[8];
	char                s[1000];
	char* nargv = s;
	int                 i = 0;

#if !defined(LS_SLAVE)
	char                s2[200];
	char* argv[20];

	strncpy(s, myCfg->get_text_value("action", ""), 999);
	s[999] = '\0';

	//printf("%s: Specified : %s\n",devid_string,s);
	if (strcmp(s, ""))
	{

		// spawn external program (telnet client)...
		while (*nargv)
		{
			argv[i] = nargv;
			if (nargv[0] == '\"')
				nargv = strchr(nargv + 1, '\"');
			if (nargv)
				nargv = strchr(nargv, ' ');
			if (!nargv)
				break;
			*nargv++ = '\0';
			i++;
			argv[i] = NULL;
		}

		argv[i + 1] = NULL;
		strcpy(s2, argv[0]);
		nargv = s2;
		if (nargv[0] == '\"')
		{
			nargv++;
			*(strchr(nargv, '\"')) = '\0';
		}

		//printf("%s: Starting %s\n", devid_string,nargv);
#if defined(_WIN32)
		if (_spawnvp(_P_NOWAIT, nargv, argv) < 0)
			FAILURE_1(Runtime, "Exec of '%s' has failed.\n", argv[0]);
#elif !defined(__VMS)
		pid_t child;
		int   status;
		if (!(child = fork()))
		{
			execvp(argv[0], argv);
			FAILURE_1(Runtime, "Exec of '%s' failed.\n", argv[0]);
		}
		else
		{
			sleep(1); // give it a chance to start up.
			waitpid(child, &status, WNOHANG); // reap it, if needed.
			if (kill(child, 0) < 0)
			{ // uh oh, no kiddo.
				FAILURE_1(Runtime, "Exec of '%s' has failed.\n", argv[0]);
			}
		}
#endif
	}
#endif
	Address.sin_addr.s_addr = INADDR_ANY;
	Address.sin_port = htons((u16)listenPort);
	Address.sin_family = AF_INET;

	//  Wait until we have a connection. Poll instead of blocking in accept()
	//  so stop_threads() can interrupt us; otherwise shutdown hangs in join()
	//  until a client connects (issue #158).
	fd_set          readset;
	struct timeval  tv;
	connectSocket = INVALID_SOCKET;
	while (connectSocket == INVALID_SOCKET)
	{
		if (StopThread)
			return;
		FD_ZERO(&readset);
		FD_SET(listenSocket, &readset);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		if (select(listenSocket + 1, &readset, NULL, NULL, &tv) > 0)
			connectSocket = accept(listenSocket, (struct sockaddr*)&Address,
				&nAddressSize);
	}

	iac_carry_len = 0;
	in_subneg = false;
	stageLen = 0;

	state.serial_cycles = 0;


	if (state.iNumber != 1 && !raw_mode) // skip for kgdb-default port and any raw-mode port
	{
		// Send some control characters to the telnet client to handle
		// character-at-a-time mode.
		sprintf(buffer, telnet_options, IAC, DO, TELOPT_ECHO);
		write_cstr(buffer);

		sprintf(buffer, telnet_options, IAC, DO, TELOPT_NAWS);
		write_cstr(buffer);

		sprintf(buffer, telnet_options, IAC, DO, TELOPT_LFLOW);
		write_cstr(buffer);

		sprintf(buffer, telnet_options, IAC, WILL, TELOPT_ECHO);
		write_cstr(buffer);

		sprintf(buffer, telnet_options, IAC, WILL, TELOPT_SGA);
		write_cstr(buffer);

		// we do these two to avoid a weird character spam in the client
		unsigned char opt_do_binary[3] = { IAC, DO,   TELOPT_BINARY }; // tell client to send raw
		unsigned char opt_will_binary[3] = { IAC, WILL, TELOPT_BINARY }; // tell client to receive raw
		write((const char*)opt_do_binary, 3);
		write((const char*)opt_will_binary, 3);

		sprintf(s, "This is serial port #%d on ES40 Emulator\r\n", state.iNumber);
		write_cstr(s);
	}
}
