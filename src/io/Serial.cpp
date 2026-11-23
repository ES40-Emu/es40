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
#include "SerialListenAddress.h"
#include "system/System.h"
#include "chipset/AliM1543C.h"

#include "network/lockstep.h"
#include <algorithm>
#include <chrono>
#if defined(__VMS)
#include <ioctl.h>
#elif !defined(_WIN32)
#include <sys/ioctl.h>
#endif

#define UART_BASE_CLOCK  1843200
#define CYCLE_TIME_MS    20

#define RECV_TICKS  10

int iCounter = 0;

#define FIFO_SIZE 1024

//#define DEBUG_SERIAL 1

/**
 * Constructor.
 **/
CSerial::CSerial(CConfigurator* cfg, CSystem* c, u16 number)
	: CSystemComponent(cfg, c), uart(Uart::Model::Legacy16550), port_number(number),
	terminal_config(cfg), irq_output([this](bool level) {
		if (isa_bridge)
			isa_bridge->pic_set_line(0, 4 - port_number, level);
	})
{
}

CSerial::CSerial(CConfigurator* cfg, CSystem* c, u16 number, std::function<void(bool)> irq)
	: CSystemComponent(cfg, c), uart(Uart::Model::VL16C450B), port_number(number),
	irq_output(std::move(irq))
{
}

void CSerial::configure_terminal(CConfigurator* cfg)
{
	if (terminal_config)
		FAILURE(Configuration, "Duplicate serial terminal configuration");
	terminal_config = cfg;
}

std::string CSerial::snapshot_identity() const
{
	auto identity = CSystemComponent::snapshot_identity();
	if (uart.is_vl16c450())
		identity += "/com" + std::to_string(port_number + 1);
	return identity;
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
	const u32 port = 0x3f8u - 0x100u * (u32)port_number + (u32)address;
	return isa_bridge->isa_io_decode(port) != CAliM1543C::IsaIoDecode::None;
}

bool CSerial::uses_subtractive_decode(int index, u64 address, int dsize,
	bool write) const noexcept
{
	if (!isa_bridge || !decodes_memory_access(index, address, dsize, write))
		return false;
	const u32 port = 0x3f8u - 0x100u * (u32)port_number + (u32)address;
	return isa_bridge->isa_io_decode(port) == CAliM1543C::IsaIoDecode::Subtractive;
}

/**
 * Initialize the Serial port device.
 **/
void CSerial::init()
{
	const bool onboard = uart.is_vl16c450();
	const bool host_disabled = terminal_config && terminal_config->get_bool_value("disabled");
	disabled = !onboard && host_disabled;
	null_attach = !terminal_config || host_disabled || terminal_config->get_bool_value("null_attach");
	if (!onboard)
		cSystem->RegisterMemory(this, 0, U64(0x00000801fc0003f8) - (0x100 * port_number), 8);

	if (disabled)
	{
		// The port is claimed, but scratchpad writes don't stick and MSR/LSR/IIR read 0xff.
		// KDCOM and SERIAL.SYS recognize the missing UART and skip further probing.
		printf("%s: disabled - guest will see no UART at this address.\n", devid_string);
		return;
	}

	if (null_attach)
	{
		// This bit-bucket UART discards guest TX and has no socket or BREAK menu.
		// The legacy 16550 needs no worker and keeps THRE/TSRE and CTS/DSR set.
		// Register reads, MCR.LOOP self-tests and TX-empty interrupts still work.
		printf("%s: COM%d has no host terminal.\n", devid_string, port_number + 1);
		return;
	}

	raw_mode = terminal_config->get_bool_value("raw_mode");
	wait_for_connection = terminal_config->get_bool_value("wait_for_connection", !onboard);
	listenPort = (int)terminal_config->get_num_value("port", false, 8000 + port_number);

	std::vector<std::string> configured_addresses;
	std::string address_error;
	if (!serial_parse_listen_addresses(terminal_config->get_text_value("listen_address", ""),
		configured_addresses, address_error))
		FAILURE(Configuration, std::string(devid_string) + ": listen_address: " + address_error);

	addrinfo* address = NULL;
	try
	{
#if defined(_WIN32)
		WSADATA wsa;
		const int startup_error = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (startup_error != 0)
			FAILURE_3(Configuration, "%s: serial port %d WSAStartup failed (error %d)",
				devid_string, listenPort, startup_error);
		winsock_started = true;
#endif
		const bool all_ipv4 = std::find(configured_addresses.begin(), configured_addresses.end(),
			"0.0.0.0") != configured_addresses.end();
		const bool all_ipv6 = std::find(configured_addresses.begin(), configured_addresses.end(),
			"::") != configured_addresses.end();
		const std::string port = std::to_string(listenPort);
		listenSockets.reserve(configured_addresses.size());
		listenAddresses.reserve(configured_addresses.size());
		for (const std::string& host : configured_addresses)
		{
			const bool ipv6 = host.find(':') != std::string::npos;
			if ((ipv6 && all_ipv6 && host != "::") || (!ipv6 && all_ipv4 && host != "0.0.0.0"))
				continue;
#if defined(_WIN32)
			if (listenSockets.size() == FD_SETSIZE)
				FAILURE(Configuration, "Too many serial listen addresses");
#endif
			addrinfo hints = {};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			hints.ai_flags = AI_NUMERICHOST;
			const int error = getaddrinfo(host.c_str(), port.c_str(), &hints, &address);
			if (error != 0)
				FAILURE(Configuration, std::string(devid_string) + ": cannot resolve serial address " +
					host + " (error " + std::to_string(error) + ")");

			const auto listener_failure = [this, &host](const char* operation)
			{
#if defined(_WIN32)
				const int error = WSAGetLastError();
				char text[256] = {};
				FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL, error, 0, text, sizeof(text), NULL);
				text[strcspn(text, "\r\n")] = '\0';
#else
				const int error = errno;
				const char* text = strerror(error);
#endif
				FAILURE(Configuration, std::string(devid_string) + ": serial address " + host +
					" port " + std::to_string(listenPort) + " " + operation + " failed: " + text +
					" (error " + std::to_string(error) + ")");
			};
			const int64_t listener = socket(address->ai_family, SOCK_STREAM, IPPROTO_TCP);
			if (listener == INVALID_SOCKET)
				listener_failure("socket");
			listenSockets.push_back(listener);
			listenAddresses.push_back(host);
#if !defined(_WIN32)
			if (listener >= FD_SETSIZE)
				FAILURE(Configuration, "Serial listener exceeds FD_SETSIZE");
#endif
#if defined(_WIN32)
			u_long nonblocking = 1;
			if (ioctlsocket(listener, FIONBIO, &nonblocking) != 0)
#else
			int nonblocking = 1;
			if (ioctl(listener, FIONBIO, &nonblocking) != 0)
#endif
				listener_failure("nonblocking mode");
			int optval = 1;
#if defined(_WIN32)
			const int reuse_option = SO_EXCLUSIVEADDRUSE;
#else
			const int reuse_option = SO_REUSEADDR;
#endif
			if (setsockopt(listener, SOL_SOCKET, reuse_option, (char*)&optval, sizeof(optval)) != 0)
				listener_failure("setsockopt");
			if (address->ai_family == AF_INET6 &&
				setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&optval, sizeof(optval)) != 0)
				listener_failure("setsockopt(IPV6_V6ONLY)");
			if (bind(listener, address->ai_addr, (socklen_t)address->ai_addrlen) != 0)
				listener_failure("bind");
			if (listen(listener, 8) != 0)
				listener_failure("listen");
			freeaddrinfo(address);
			address = NULL;
			printf("%s: Listening on [%s]:%d.\n", devid_string, host.c_str(), listenPort);
		}
		start_connection_action();
		if (wait_for_connection)
		{
			printf("%s: Waiting for connection on port %d.\n", devid_string, listenPort);
			WaitForConnection();
		}
		else
			printf("%s: Continuing without a client on port %d; disconnected output is discarded.\n",
				devid_string, listenPort);
	}
	catch (...)
	{
		if (address)
			freeaddrinfo(address);
		close_sockets();
		throw;
	}

#if defined(IDB) && defined(LS_MASTER)
	struct sockaddr_in  dest_addr;
	int                 result = -1;

	throughSocket = socket(AF_INET, SOCK_STREAM, 0);

	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons((u16)(listenPort + port_number));
	dest_addr.sin_addr.s_addr = inet_addr(ls_IP);

	printf("%s: Waiting to initiate remote connection to %s.\n", devid_string,
		ls_IP);

	while (result == -1)
	{
		result = connect(throughSocket, (struct sockaddr*)&dest_addr,
			sizeof(struct sockaddr));
	}
#endif
	printf("%s: $Id$\n",
		devid_string);
}

void CSerial::start_threads()
{
	char  buffer[5];
	if (disabled || (null_attach && !uart.is_vl16c450()))
		return;
	if (!myThread)
	{
		sprintf(buffer, "srl%d", port_number);
		myThread = new CThread(buffer);
		printf(" %s", myThread->getName().c_str());
		StopThread = false;
		myThread->start(*this);
	}
}

void CSerial::stop_threads()
{
	{
		std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
		StopThread = true;
		uart_wakeup.notify_all();
	}
	if (myThread)
	{
		printf(" %s", myThread->getName().c_str());
		myThread->join();
		delete myThread;
		myThread = 0;
	}
	prepare_snapshot();
}

void CSerial::prepare_snapshot()
{
	std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
	if (uart.is_vl16c450() && uart_sample != Clock::time_point{})
	{
		advance_uart(cSystem->device_access_time());
		publish_irq();
		uart_sample = {};
	}
}

/**
 * Destructor.
 **/
CSerial::~CSerial()
{
	if (myThread)
		stop_threads();
	close_sockets();
}

void CSerial::close_connection()
{
	std::lock_guard<std::mutex> lock(m_connection_mutex);
	tx_head = tx_size = 0;
	tx_failed = false;
	if (connectSocket == INVALID_SOCKET)
		return;
#if defined(_WIN32)
	closesocket(connectSocket);
#else
	close(connectSocket);
#endif
	connectSocket = INVALID_SOCKET;
	connection_action_started = false;
}

void CSerial::close_sockets()
{
	close_connection();
	for (int64_t listener : listenSockets)
	{
#if defined(_WIN32)
		closesocket(listener);
#else
		close(listener);
#endif
	}
	listenSockets.clear();
	listenAddresses.clear();
#if defined(IDB) && defined(LS_MASTER)
	if (throughSocket != INVALID_SOCKET)
	{
#if defined(_WIN32)
		closesocket(throughSocket);
#else
		close(throughSocket);
#endif
	}
	throughSocket = INVALID_SOCKET;
#endif
#if defined(_WIN32)
	if (winsock_started)
	{
		WSACleanup();
		winsock_started = false;
	}
#endif
}

u64 CSerial::ReadMem(int index, u64 address, int dsize)
{
	if (disabled)
		return 0xffu;  // missing-UART signature

	std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
	if (uart.is_vl16c450())
		advance_uart(cSystem->device_access_time());
	const bool had_data = uart.data_ready();
	const u8 value = uart.read((unsigned)address);
	if (address == 0 && !(uart.regs.lcr & 0x80))
	{
		if (had_data)
		{
			TRC_DEV4("Read character %02x (%c) on serial port %d\n", value, printable(value), port_number);
#if defined(DEBUG_SERIAL)
			printf("Read character %02x (%c) on serial port %d\n", value, printable(value), port_number);
#endif
		}
		else
		{
			TRC_DEV2("Read past FIFO on serial port %d\n", port_number);
#if defined(DEBUG_SERIAL)
			printf("Read past FIFO on serial port %d\n", port_number);
#endif
		}
	}
	publish_irq();
	return value;
}

void CSerial::WriteMem(int index, u64 address, int dsize, u64 data)
{
	if (disabled)
		return;

	std::unique_lock<std::recursive_mutex> lock(m_register_mutex);
	if (uart.is_vl16c450())
		advance_uart(cSystem->device_access_time());
	const u8 previous_mcr = uart.regs.mcr;
	const auto output = uart.write((unsigned)address, (u8)data);
	if (address == 0 && !(uart.regs.lcr & 0x80))
	{
		const u8 d = (u8)data;
		TRC_DEV4("Write character %02x (%c) on serial port %d\n", d, printable(d), port_number);
#if defined(DEBUG_SERIAL)
		printf("Write character %02x (%c) on serial port %d\n", d, printable(d), port_number);
#endif
	}
	if (!uart.is_vl16c450() && previous_mcr != uart.regs.mcr)
	{
		const u8 d = uart.regs.mcr;
		printf("SRL%d: MCR %02x -> %02x (LOOP=%d DTR=%d RTS=%d OUT1=%d OUT2=%d), MSR now %02x\n",
			port_number, previous_mcr, d, (d >> 4) & 1, d & 1, (d >> 1) & 1,
			(d >> 2) & 1, (d >> 3) & 1, uart.regs.msr);
	}
	publish_irq();
	if (uart.is_vl16c450() && uart.next_event_ns())
		uart_wakeup.notify_one();
	lock.unlock();  // Legacy THR writes can release the UART lock before sending.
	if (output)
		write((const char*)&*output, 1);
}

void CSerial::publish_irq(bool force)
{
	const bool level = uart.interrupt_pending();
	if (force || level != irq_level)
	{
		irq_level = level;
		irq_output(level);
	}
}

void CSerial::eval_interrupts()
{
	if (!disabled)
	{
		std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
		publish_irq(!uart.is_vl16c450());
	}
}

void CSerial::ResetPCI()
{
	if (uart.is_vl16c450())
	{
		std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
		uart.reset();
		uart_sample = {};
		uart_wakeup.notify_one();
		publish_irq(true);
	}
}

void CSerial::finalize_restore() noexcept
{
	if (!disabled)
	{
		std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
		publish_irq(true);
	}
}

static bool serial_io_retryable()
{
#if defined(_WIN32)
	const int error = WSAGetLastError();
	return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

// Caller holds m_connection_mutex. Overflow ends this client's session.
void CSerial::queue_output(const char* data, size_t size)
{
	if (tx_failed)
		return;
	if (size > tx_buffer.size() - tx_size)
	{
		tx_failed = true;
		return;
	}
	const size_t tail = (tx_head + tx_size) % tx_buffer.size();
	const size_t first = (std::min)(size, tx_buffer.size() - tail);
	memcpy(tx_buffer.data() + tail, data, first);
	memcpy(tx_buffer.data(), data + first, size - first);
	tx_size += size;
}

// Nonblocking, with at most two sends per call. Caller holds the TX lock.
void CSerial::flush_output_locked(int64_t connection)
{
	if (connection == INVALID_SOCKET || tx_failed)
		return;
	for (int attempt = 0; attempt < 2 && tx_size != 0; ++attempt)
	{
		int flags = 0;
#if defined(MSG_NOSIGNAL)
		flags |= MSG_NOSIGNAL;
#endif
		const size_t size = (std::min)(tx_size, tx_buffer.size() - tx_head);
		const int sent = send(connection, tx_buffer.data() + tx_head, (int)size, flags);
		if (sent < 0)
		{
			if (!serial_io_retryable())
				tx_failed = true;
			return;
		}
		if (sent == 0)
		{
			tx_failed = true;
			return;
		}
		tx_head = (tx_head + sent) % tx_buffer.size();
		tx_size -= sent;
	}
}

bool CSerial::flush_output()
{
	std::lock_guard<std::mutex> lock(m_connection_mutex);
	flush_output_locked(connectSocket);
	return !tx_failed;
}

void CSerial::write(const char* s, int dsize)
{
	if (disabled || null_attach || dsize <= 0)
		return;
	std::lock_guard<std::mutex> lock(m_connection_mutex);
	if (connectSocket == INVALID_SOCKET || tx_failed)
		return;
	flush_output_locked(connectSocket);
	queue_output(s, (size_t)dsize);
	flush_output_locked(connectSocket);
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
	std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
	int consumed = 0;

	while (consumed < dsize && uart.receive((u8)data[consumed]))
		consumed++;
	publish_irq();
	return consumed;
}

/**
 * drain_staging  — Move data from the staging buffer into the FIFO,
 *                  limited to max_bytes.  Compacts after partial drain.
 **/
void CSerial::drain_staging()
{
	if (uart.is_vl16c450() || stageLen == 0)
		return;

	std::lock_guard<std::recursive_mutex> lock(m_register_mutex);

	// Only deliver when the guest has consumed the previous character.
	// This self-clocks to the emulated CPU speed, just like a real
	// UART where the next character doesn't arrive until the baud
	// rate shifts it in — and the CPU is always fast enough to read
	// it before then.
	if (uart.data_ready())
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
		if (uart.is_vl16c450())
		{
			run_onboard();
			return;
		}
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

void CSerial::advance_uart(Clock::time_point now)
{
	if (now <= uart_sample)
		return;
	if (uart_sample != Clock::time_point{} && uart.next_event_ns())
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - uart_sample);
		const auto output = uart.advance(elapsed.count());
		if (output.size)
			write((const char*)output.bytes, output.size);
	}
	uart_sample = now;
}

void CSerial::stage_uart_input(const char* data, int size)
{
	std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
	advance_uart(Clock::now());
	uart.stage_input(data, size);
	publish_irq();
	uart_wakeup.notify_one();
}

void CSerial::run_onboard()
{
	std::unique_lock<std::recursive_mutex> lock(m_register_mutex);
	auto next_poll = Clock::now();
	while (!StopThread)
	{
		const auto now = Clock::now();
		advance_uart(now);
		publish_irq();
		if (!null_attach && now >= next_poll)
		{
			next_poll = now + std::chrono::milliseconds(CYCLE_TIME_MS);
			lock.unlock();
			execute();
			lock.lock();
			continue;
		}
		auto wake = null_attach ? (Clock::time_point::max)() : next_poll;
		if (uart.next_event_ns())
			wake = (std::min)(wake, now + std::chrono::ceil<Clock::duration>(std::chrono::nanoseconds(uart.next_event_ns())));
		if (wake == (Clock::time_point::max)())
			uart_wakeup.wait(lock);
		else
			uart_wakeup.wait_until(lock, wake);
	}
}

/**
 * Check if threads are still running.
 *
 * Enter serial port menu if <break> was received.
 **/
void CSerial::check_state()
{
	if (disabled || (null_attach && !uart.is_vl16c450()))
		return;
	if (breakHit)
		serial_menu();

	if (myThread && !myThread->isRunning())
		FAILURE(Thread, "Serial thread has died");
}

void CSerial::serial_menu()
{
	fd_set          readset;
	fd_set          writeset;
	unsigned char   buffer[FIFO_SIZE + 1];
	int             size;
	struct timeval  tv;
	bool            exitLoop = false;
	bool            waitForChoice = false;

	cSystem->stop_threads();
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

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
		// The worker is stopped; the menu must drain its own output.
		if (connectSocket == INVALID_SOCKET || !flush_output())
		{
			close_connection();
			printf("%%SRL-W-DISCONNECT: Serial menu connection unavailable. Continuing emulation.\n");
			break;
		}
		const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
			deadline - std::chrono::steady_clock::now()).count();
		if (!waitForChoice && remaining <= 0)
		{
			write_cstr("%SRL-I-TIMEOUT: no timely answer received. Continuing emulation.\r\n");
			break;
		}
		FD_ZERO(&readset);
		FD_ZERO(&writeset);
		FD_SET(connectSocket, &readset);
		if (tx_size != 0)
			FD_SET(connectSocket, &writeset);
		tv.tv_sec = waitForChoice ? 0 : (long)(remaining / 1000000);
		tv.tv_usec = waitForChoice ? 0 : (long)(remaining % 1000000);
#if defined(_WIN32)
		int ready = select(0, &readset, &writeset, NULL,
#else
		int ready = select((int)connectSocket + 1, &readset, &writeset, NULL,
#endif
			waitForChoice ? NULL : &tv);
		if (ready < 0)
		{
			if (serial_io_retryable())
				continue;
			close_connection();
			printf("%%SRL-W-DISCONNECT: Serial menu connection failed. Continuing emulation.\n");
			break;
		}
		if (ready == 0 || !FD_ISSET(connectSocket, &readset))
			continue;

#if defined(_WIN32) || defined(__VMS)
		size = recv(connectSocket, (char*)buffer, FIFO_SIZE, 0);
#else
		size = read(connectSocket, &buffer, FIFO_SIZE);
#endif
		if (size < 0 && serial_io_retryable())
			continue;
		if (size <= 0)
		{
			close_connection();
			printf("%%SRL-W-DISCONNECT: Serial menu connection closed or failed. Continuing emulation.\n");
			break;
		}
		deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
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
	if (!flush_output())
	{
		printf("%%SRL-W-DISCONNECT: Serial output failed or queue full on port %d.\n", port_number);
		close_connection();
	}
	// Poll for clients without suspending UART processing.
	if (connectSocket == INVALID_SOCKET)
	{
		start_connection_action();
		accept_connection(0);
		eval_interrupts();
		return;
	}
	fd_set          readset;
	unsigned char   raw[FIFO_SIZE + 16];
	unsigned char   cbuffer[FIFO_SIZE + 1];
	unsigned char*  b;
	unsigned char*  c;
	unsigned char*  end;
	int             size;
	struct timeval  tv;

	drain_staging();

	// ------------------------------------------------------------------
	// Every RECV_TICKS cycles: read from the socket and cook telnet out
	// ------------------------------------------------------------------
	serial_cycles++;
	if (serial_cycles >= RECV_TICKS)
	{
		// If staging buffer still has data, don't pull more off the socket.
		// TCP backpressure holds the rest at the OS level.
		{
			std::lock_guard<std::recursive_mutex> lock(m_register_mutex);
			if (stageLen > 0 || (uart.is_vl16c450() && uart.input_staged()))
			{
				serial_cycles = 0;
				publish_irq();
				return;
			}
		}

		FD_ZERO(&readset);
		FD_SET(connectSocket, &readset);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
#if defined(_WIN32)
		if (select(0, &readset, NULL, NULL, &tv) > 0)
#else
		if (select((int)connectSocket + 1, &readset, NULL, NULL, &tv) > 0)
#endif
		{
			unsigned char* recv_buf = raw + iac_carry_len;

#if defined(_WIN32) || defined(__VMS)
			size = recv(connectSocket, (char*)recv_buf, FIFO_SIZE, 0);
#else
			size = read(connectSocket, recv_buf, FIFO_SIZE);
#endif

			if (size < 0 && serial_io_retryable())
			{
				serial_cycles = 0;
				eval_interrupts();
				return;
			}
			extern int  got_sigint;
			if (size <= 0 && !got_sigint)
			{
				printf("%%SRL-W-DISCONNECT: Write socket closed on other end for serial port %d.\n",
					port_number);
				printf("-SRL-I-WAITFOR: Waiting for a new connection on port %d.\n",
					listenPort);
				close_connection();
				return;
			}

			if (size <= 0)
			{
				serial_cycles = 0;
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

			// raw_mode skips telnet IAC cooking and, on the legacy UART, the staging buffer's
			// drip-feed.  The byte stream must stay 8-bit clean and flow at line
			// rate for windbg/kgdb, which sends arbitrary binary (including 0xff)
			// in KD packets and times out if delivery is throttled.
			if (raw_mode)
			{
				if (uart.is_vl16c450())
				{
					// Pace a raw socket burst into the single-byte RBR (PB22H-KB, p. 20-17).
					stage_uart_input((const char*)raw, size);
					serial_cycles = 0;
					return;
				}
				int delivered = receive((const char*)raw, (int)size);
				if (delivered < (int)size)
					printf("%%SRL-W-RAWFULL: raw_mode FIFO full on port %d, dropped %d byte(s)\n",
						port_number, (int)size - delivered);
				serial_cycles = 0;
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

				if (uart.is_vl16c450())
					stage_uart_input((const char*)cbuffer, cooked_len);
				else if (cooked_len <= (int)(STAGE_SIZE - stageLen))
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
						"dropped %d bytes\n", port_number, cooked_len - space);
#endif
				}
			}
		}

		serial_cycles = 0;
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
	const SSrl_state state{uart.regs, serial_cycles, uart.queue, port_number, uart.thre_pending};
	const u8 routing = u8(uart.tx.loopback) | (u8(uart.tx.suppress) << 1);
	const long ss = sizeof(state) + (uart.is_vl16c450() ? 2 * sizeof(u64) + 2 : 0);
	if (!fwrite(&srl_magic1, sizeof(u32), 1, f) || !fwrite(&ss, sizeof(long), 1, f)
		|| !fwrite(&state, sizeof(state), 1, f))
		return -1;
	if (uart.is_vl16c450() && (!fwrite(&uart.tx.remaining_ns, sizeof(u64), 1, f)
		|| !fwrite(&uart.tx.shift, 1, 1, f) || !fwrite(&routing, 1, 1, f)
		|| !fwrite(&uart.rx_remaining_ns, sizeof(u64), 1, f)))
		return -1;
	if (!fwrite(&srl_magic2, sizeof(u32), 1, f))
		return -1;
	printf("%s: %d bytes saved.\n", devid_string, (int)ss);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CSerial::RestoreState(FILE* f)
{
	SSrl_state state{};
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

	if (ss != sizeof(state) + (uart.is_vl16c450() ? 2 * sizeof(u64) + 2 : 0))
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

	Uart::Transmitter tx{};
	u64 rx_remaining_ns = 0;
	if (uart.is_vl16c450())
	{
		u8 routing;
		if (!fread(&tx.remaining_ns, sizeof(u64), 1, f) || !fread(&tx.shift, 1, 1, f)
			|| !fread(&routing, 1, 1, f) || !fread(&rx_remaining_ns, sizeof(u64), 1, f)
			|| routing > 3 || tx.remaining_ns > 10000000000ULL || rx_remaining_ns > 10000000000ULL)
			return -1;
		tx.loopback = (routing & 1) != 0;
		tx.suppress = (routing & 2) != 0;
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

	const unsigned queue_limit = sizeof(state.queue.bytes) - (uart.is_vl16c450() ? 0 : 1);
	if (state.iNumber != port_number || (unsigned)state.queue.read > queue_limit
		|| (unsigned)state.queue.write > queue_limit)
		return -1;
	if (uart.is_vl16c450() && (state.queue.read > state.queue.write
		|| (rx_remaining_ns && state.queue.read == state.queue.write)))
		return -1;
	uart.regs = state.regs;
	uart.queue = state.queue;
	uart.thre_pending = state.thre_pending != 0;
	uart.tx = tx;
	uart.rx_remaining_ns = rx_remaining_ns;
	uart_sample = {};
	serial_cycles = state.serial_cycles;
	printf("%s: %d bytes restored.\n", devid_string, (int)ss);
	return 0;
}

void CSerial::WaitForConnection()
{
	// Only startup waits; preserve any existing connection.
	if (listenSockets.empty())
		return;
	while (!StopThread && connectSocket == INVALID_SOCKET)
	{
		start_connection_action();
		accept_connection(100000);
	}
}

void CSerial::start_connection_action()
{
	if (connection_action_started || StopThread || listenSockets.empty())
		return;
	connection_action_started = true;
#if !defined(LS_SLAVE)
	char                s[1000];
	char* nargv = s;
	int                 i = 0;
	char                s2[200];
	char* argv[20];

	strncpy(s, terminal_config->get_text_value("action", ""), 999);
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
}

bool CSerial::accept_connection(int timeout_us)
{
	if (connectSocket != INVALID_SOCKET)
		return true;
	if (listenSockets.empty() || StopThread)
		return false;
	fd_set          readset;
	struct timeval  tv;
	FD_ZERO(&readset);
#if !defined(_WIN32)
	int highest_socket = 0;
#endif
	for (int64_t listener : listenSockets)
	{
		FD_SET(listener, &readset);
#if !defined(_WIN32)
		if (listener > highest_socket)
			highest_socket = (int)listener;
#endif
	}
	tv.tv_sec = 0;
	tv.tv_usec = timeout_us;
#if defined(_WIN32)
	const int ready = select(0, &readset, NULL, NULL, &tv);
#else
	const int ready = select(highest_socket + 1, &readset, NULL, NULL, &tv);
#endif
	if (ready < 0)
	{
#if defined(_WIN32)
		const int error = WSAGetLastError();
		if (error == WSAEINTR)
#else
		const int error = errno;
		if (error == EINTR)
#endif
			return false;
		FAILURE_2(Runtime, "%s: serial listener select failed (error %d)", devid_string, error);
	}
	if (ready == 0 || StopThread)
		return false;
	for (size_t i = 0; i < listenSockets.size(); ++i)
	{
		if (!FD_ISSET(listenSockets[i], &readset))
			continue;
		const int64_t connection = accept(listenSockets[i], NULL, NULL);
		if (connection == INVALID_SOCKET)
		{
#if defined(_WIN32)
			const int error = WSAGetLastError();
			if (error == WSAEINTR || error == WSAEWOULDBLOCK ||
				error == WSAECONNRESET || error == WSAECONNABORTED)
				continue;
#else
			const int error = errno;
			if (error == EINTR || error == EAGAIN || error == EWOULDBLOCK || error == ECONNABORTED)
				continue;
#if defined(__linux__)
			if (error == ENETDOWN || error == EPROTO || error == ENOPROTOOPT ||
				error == EHOSTDOWN || error == ENONET || error == EHOSTUNREACH ||
				error == EOPNOTSUPP || error == ENETUNREACH)
				continue;
#endif
#endif
			FAILURE(Runtime, std::string(devid_string) + ": serial address " + listenAddresses[i] +
				" accept failed (error " + std::to_string(error) + ")");
		}
		// Queue telnet setup ahead of guest TX before publishing the socket.
		const auto close_pending = [connection]()
		{
#if defined(_WIN32)
			closesocket(connection);
#else
			close(connection);
#endif
		};
		try
		{
#if !defined(_WIN32)
			if (connection >= FD_SETSIZE)
				FAILURE(Runtime, "Accepted serial socket exceeds FD_SETSIZE");
#endif
#if defined(_WIN32)
			u_long nonblocking = 1;
			const int mode_error = ioctlsocket(connection, FIONBIO, &nonblocking);
#else
			int nonblocking = 1;
			const int mode_error = ioctl(connection, FIONBIO, &nonblocking);
#endif
			if (mode_error != 0)
				FAILURE(Runtime, "Cannot set nonblocking mode on serial connection");
#if defined(SO_NOSIGPIPE)
			const int no_sigpipe = 1;
			if (setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
				(const char*)&no_sigpipe, sizeof(no_sigpipe)) != 0)
				FAILURE(Runtime, "Cannot disable SIGPIPE on serial connection");
#endif
			std::lock_guard<std::mutex> lock(m_connection_mutex);
			if (StopThread || !initialize_connection(connection))
			{
				close_pending();
				continue;
			}
			iac_carry_len = 0;
			in_subneg = false;
			stageLen = 0;
			serial_cycles = 0;
			connectSocket = connection;
			return true;
		}
		catch (...)
		{
			close_pending();
			throw;
		}
	}
	return false;
}

bool CSerial::initialize_connection(int64_t connection)
{
	tx_head = tx_size = 0;
	tx_failed = false;
	if (port_number != 1 && !raw_mode) // skip for kgdb-default port and any raw-mode port
	{
		// Use explicit lengths: TELOPT_BINARY is zero.
		const unsigned char options[] = {
			IAC, DO, TELOPT_ECHO, IAC, DO, TELOPT_NAWS, IAC, DO, TELOPT_LFLOW,
			IAC, WILL, TELOPT_ECHO, IAC, WILL, TELOPT_SGA,
			IAC, DO, TELOPT_BINARY, IAC, WILL, TELOPT_BINARY
		};
		char banner[100];
		snprintf(banner, sizeof(banner), "This is serial port #%d on ES40 Emulator\r\n", port_number);
		queue_output((const char*)options, sizeof(options));
		queue_output(banner, strlen(banner));
		flush_output_locked(connection);
	}
	return !tx_failed;
}
