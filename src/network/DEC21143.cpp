/* ES40 emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2020 Martin Vorländer
 * Copyright (C) 2023, 2025-2026 by gdwnldsKSC
 * Copyright (C) 2026 by Aleksander Czajczyński
 * Copyright (C) 2026 by Jan-Benedict Glaw
 * Copyright (C) 2026 by Scott Klein
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
 * Parts of this file based upon GXemul, which is Copyright (C) 2004-2007
 * Anders Gavare.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

 /**
  * \file
  * Contains the code for the emulated DEC 21143 NIC device.
  *
  * $Id$
  *
  * X-1.38       gdwnldsKSC                                      17-OCT-2025
  *		Hopefully resolved long standing issue with packet handling
  *		and improved overall stability.
  *
  * X-1.37       gdwnldsKSC                                      17-OCT-2025
  *		Various improvements to accuracy and function, including
  *		preventing OpenBSD kernel panics (doesn't work yet though).
  *
  * X-1.36       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.35       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.34       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.33       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.32       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.31       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.30       Camiel Vanderhoeven                             02-MAR-2008
  *      Natural way to specify large numeric values ("10M") in the config file.
  *
  * X-1.29       Brian Wheeler                                   29-FEB-2008
  *      Compute SROM checksum. Tru64 needs this.
  *
  * X-1.28       David Hittner                                   26-FEB-2008
  *      Major rewrite. Real internal loopback support, ring queue for
  *      incoming packets, and various other improvements.
  *
  * X-1.27       Camiel Vanderhoeven                             24-JAN-2008
  *      Use new CPCIDevice::do_pci_read and CPCIDevice::do_pci_write.
  *
  * X-1.26       Fang Zhe                                        08-JAN-2008
  *      Avoid compiler warning.
  *
  * X-1.25       Fausto Saporito                                 05-JAN-2008
  *      Fixed typo (\m instead of \n).
  *
  * X-1.24       David Hittner                                   04-JAN-2008
  *      MAC address configurable.
  *
  * X-1.23       Camiel Vanderhoeven                             02-JAN-2008
  *      Ignore OPMODE_OM (loopback mode) bits.
  *
  * X-1.22       Camiel Vanderhoeven                             02-JAN-2008
  *      Replaced USE_NETWORK with HAVE_PCAP.
  *
  * X-1.21       Camiel Vanderhoeven                             30-DEC-2007
  *      Print file id on initialization.
  *
  * X-1.20       Camiel Vanderhoeven                             29-DEC-2007
  *      Compileable with older compilers (VC 6.0). Avoid referencing
  *      uninitialized data. Fixed memory-leak.
  *
  * X-1.19       Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.18       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.17       Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.16       Brian Wheeler                                   10-DEC-2007
  *      Added pthread.h
  *
  * X-1.15       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.14       Camiel Vanderhoeven                             6-DEC-2007
  *      Identifies itself as DE-500BA.
  *
  * X-1.13       Camiel Vanderhoeven                             2-DEC-2007
  *      Receive network data in a separate thread.
  *
  * X-1.12       Camiel Vanderhoeven                             1-DEC-2007
  *      Moved inclusion of StdAfx.h outside conditional block; necessary
  *      for using precompiled headers in Visual C++.
  *
  * X-1.11       Camiel Vanderhoeven                             17-NOV-2007
  *      File should end in a newline.
  *
  * X-1.10       Camiel Vanderhoeven                             17-NOV-2007
  *      Use the standard pcap functions (not the extended windows ones), we
  *      want to be compatible.
  *
  * X-1.9        Camiel Vanderhoeven                             17-NOV-2007
  *      Corrected a small "oops" error in getting the DECnet address.
  *
  * X-1.8        Camiel Vanderhoeven                             17-NOV-2007
  *      Get the adapter and DECnet address to use from the configuration
  *      file.
  *
  * X-1.7        Camiel Vanderhoeven                             17-NOV-2007
  *      Changed the MAC address into the DigitalE-range.
  *
  * X-1.6        Camiel Vanderhoeven                             16-NOV-2007
  *      Change the packet filter less often (only when required).
  *
  * X-1.5        Camiel Vanderhoeven                             16-NOV-2007
  *      Removed some debug messages, and corrected readout of CSR 12.
  *
  * X-1.4        Camiel Vanderhoeven                             16-NOV-2007
  *      BPF filter used for perfect filtering; more correct behaviour of
  *      registers.
  *
  * X-1.3        Camiel Vanderhoeven                             15-NOV-2007
  *      Use pcap for network access.
  *
  * X-1.2        Camiel Vanderhoeven                             14-NOV-2007
  *      Removed some debug messages.
  *
  * X-1.1        Camiel Vanderhoeven                             14-NOV-2007
  *      Initial version for ES40 emulator.
  **/
#include "StdAfx.h"

#if defined(HAVE_PCAP) || defined(HAVE_TAP_NET) || defined(HAVE_SLIRP)
#include "DEC21143.h"
#include "system/System.h"
#include <string.h>

// Shared by allocation, DMA bounds checks, and saved-state validation.
static const int nic_tx_capacity = 2048;

#if defined(DEBUG_NIC)
#define DEBUG_NIC_FILTER
#define DEBUG_NIC_SROM
#endif

//#define DEBUG_NIC_IRQ

static void pkt_format_mac(char* out, const u8* mac)
{
	sprintf(out, "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void pkt_format_ip(char* out, const u8* ip)
{
	sprintf(out, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static const char* dhcp_type_name(int msg_type)
{
	switch (msg_type)
	{
	case 1: return "discover";
	case 2: return "offer";
	case 3: return "request";
	case 4: return "decline";
	case 5: return "ack";
	case 6: return "nak";
	case 7: return "release";
	case 8: return "inform";
	default: return "unknown";
	}
}

static void append_text(char* out, int out_len, int* pos, const char* text)
{
	if (*pos >= out_len - 1)
		return;
	int left = out_len - *pos;
	int wrote = snprintf(out + *pos, left, "%s", text);
	if (wrote < 0)
		return;
	if (wrote >= left)
		*pos = out_len - 1;
	else
		*pos += wrote;
}

static void append_dhcp_ip(char* out, int out_len, int* pos, const char* name,
	const u8* ip)
{
	char addr[16];
	char text[40];

	pkt_format_ip(addr, ip);
	snprintf(text, sizeof(text), " %s=%s", name, addr);
	append_text(out, out_len, pos, text);
}

static void describe_dhcp(const u8* bootp, int len, char* out, int out_len)
{
	int pos = 0;
	int msg_type = -1;
	const u8* subnet = NULL;
	const u8* router = NULL;
	const u8* server = NULL;
	const u8* requested = NULL;
	char text[48];
	char yiaddr[16];

	out[0] = '\0';
	if (len < 240)
		return;
	if (bootp[236] != 0x63 || bootp[237] != 0x82 ||
		bootp[238] != 0x53 || bootp[239] != 0x63)
		return;

	for (int off = 240; off < len;)
	{
		int opt = bootp[off++];
		if (opt == 0)
			continue;
		if (opt == 255)
			break;
		if (off >= len)
			break;

		int opt_len = bootp[off++];
		if (opt_len < 0 || off + opt_len > len)
			break;

		switch (opt)
		{
		case 1:
			if (opt_len >= 4)
				subnet = &bootp[off];
			break;
		case 3:
			if (opt_len >= 4)
				router = &bootp[off];
			break;
		case 50:
			if (opt_len >= 4)
				requested = &bootp[off];
			break;
		case 53:
			if (opt_len >= 1)
				msg_type = bootp[off];
			break;
		case 54:
			if (opt_len >= 4)
				server = &bootp[off];
			break;
		default:
			break;
		}
		off += opt_len;
	}

	if (msg_type >= 0)
	{
		snprintf(text, sizeof(text), " DHCP=%s(%d)",
			dhcp_type_name(msg_type), msg_type);
		append_text(out, out_len, &pos, text);
	}
	else
		append_text(out, out_len, &pos, " DHCP");

	if (bootp[16] || bootp[17] || bootp[18] || bootp[19])
	{
		pkt_format_ip(yiaddr, &bootp[16]);
		snprintf(text, sizeof(text), " yiaddr=%s", yiaddr);
		append_text(out, out_len, &pos, text);
	}
	if (subnet)
		append_dhcp_ip(out, out_len, &pos, "subnet", subnet);
	if (router)
		append_dhcp_ip(out, out_len, &pos, "router", router);
	if (server)
		append_dhcp_ip(out, out_len, &pos, "server", server);
	if (requested)
		append_dhcp_ip(out, out_len, &pos, "request", requested);
}

  /*  Internal states during MII data stream decode:  */
#define MII_STATE_RESET                 0
#define MII_STATE_START_WAIT            1
#define MII_STATE_READ_OP               2
#define MII_STATE_READ_PHYADDR_REGADDR  3
#define MII_STATE_A                     4
#define MII_STATE_D                     5
#define MII_STATE_IDLE                  6

/**
 * Thread entry point.
 **/
void CDEC21143::run()
{
	try
	{
		for (;;)
		{
			if (StopThread)
				return;

			/* Deferred SIA autoneg: complete outside the guest's CSR write. */
			if (autoneg_pending.load(std::memory_order_acquire) &&
				std::chrono::steady_clock::now() >= autoneg_complete_at)
			{
				autoneg_pending = false;
				complete_sia_autoneg();
			}

			receive_process();

			bool  asserted;

			if ((state.reg[CSR_OPMODE / 8] & OPMODE_ST))
				while (dec21143_tx());

			/* Normal and Abnormal interrupt summary (align with 21143/QEMU). */
			{
				u32 ie = state.reg[CSR_STATUS / 8] & state.reg[CSR_INTEN / 8];
				state.reg[CSR_STATUS / 8] &= ~(STATUS_NIS | STATUS_AIS);
				/* NIS if any normal event is enabled + set */
				if (ie & (STATUS_TI | STATUS_TU | STATUS_RI | STATUS_TM | STATUS_ER))
					state.reg[CSR_STATUS / 8] |= STATUS_NIS;
				/* AIS if any abnormal event is enabled + set */
				if (ie & (STATUS_LC | STATUS_GPPI | STATUS_SE | STATUS_LNF | STATUS_ETI |
					STATUS_RWT | STATUS_RPS | STATUS_RU | STATUS_UNF | STATUS_LNPANC |
					STATUS_TJT | STATUS_TPS))
					state.reg[CSR_STATUS / 8] |= STATUS_AIS;
				asserted = (state.reg[CSR_STATUS / 8] & state.reg[CSR_INTEN / 8] &
					(STATUS_AIS | STATUS_NIS)) != 0;
			}

			if (asserted != state.irq_was_asserted)
			{
#if defined(DEBUG_NIC_IRQ)
				printf("21143: IRQ %s  CSR5=%08x CSR7=%08x (bg)\n",
					asserted ? "ASSERT" : "DEASSERT",
					state.reg[CSR_STATUS / 8], state.reg[CSR_INTEN / 8]);
#endif
				if (do_pci_interrupt(0, asserted))
					state.irq_was_asserted = asserted;
			}

			mySemaphore.tryWait(10);
		}
	}

	catch (CException& e)
	{
		printf("Exception in NIC thread: %s.\n", e.displayText().c_str());

		// Let the thread die...
	}
}

u32             dec21143_cfg_data[64] = {
	/*00*/ 0x00191011,  // CFID: vendor + device
	/*04*/ 0x02800000,  // CFCS: command + status
	/*08*/ 0x02000030,  // CFRV: class + revision   //dth:was 41
	/*0c*/ 0x00000000,  // CFLT: latency timer + cache line size
	/*10*/ 0x00000001,  // BAR0: CBIO
	/*14*/ 0x00000000,  // BAR1: CBMA
	/*18*/ 0x00000000,  // BAR2:
	/*1c*/ 0x00000000,  // BAR3:
	/*20*/ 0x00000000,  // BAR4:
	/*24*/ 0x00000000,  // BAR5:
	/*28*/ 0x00000000,  // CCIC: CardBus
	/*2c*/ 0x500b1011,  // CSID: subsystem + vendor
	/*30*/ 0x00000000,  // BAR6: expansion rom base
	/*34*/ 0x00000000,  // CCAP: capabilities pointer
	/*38*/ 0x00000000,
	/*3c*/ 0x281401ff,  // CFIT: interrupt configuration
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

u32             dec21143_cfg_mask[64] = {
	/*00*/ 0x00000000,  // CFID: vendor + device
	/*04*/ 0x0000ffff,  // CFCS: command + status
	/*08*/ 0x00000000,  // CFRV: class + revision
	/*0c*/ 0x0000ffff,  // CFLT: latency timer + cache line size
	/*10*/ 0xffffff00,  // BAR0
	/*14*/ 0xffffff00,  // BAR1: CBMA
	/*18*/ 0x00000000,  // BAR2:
	/*1c*/ 0x00000000,  // BAR3:
	/*20*/ 0x00000000,  // BAR4:
	/*24*/ 0x00000000,  // BAR5:
	/*28*/ 0x00000000,  // CCIC: CardBus
	/*2c*/ 0x00000000,  // CSID: subsystem + vendor
	/*30*/ 0x00000000,  // BAR6: expansion rom base
	/*34*/ 0x00000000,  // CCAP: capabilities pointer
	/*38*/ 0x00000000,
	/*3c*/ 0x000000ff,  // CFIT: interrupt configuration
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

int CDEC21143::nic_num = 0;

/**
 * Constructor.
 **/
CDEC21143::CDEC21143(CConfigurator* confg, CSystem* c, int pcibus, int pcidev) : CPCIDevice(confg, c, pcibus, pcidev), mySemaphore(0, 1)
{
	net_backend = nullptr;
}

/**
 * Initialize the network device.
 **/
void CDEC21143::init()
{
	char* cfg;

	add_function(0, dec21143_cfg_data, dec21143_cfg_mask);

	net_backend = create_network_backend(devid_string, myCfg);
	if (!net_backend)
		FAILURE(Runtime, "Could not create a network backend (see messages above)");

	if (!net_backend->init(devid_string, myCfg))
	{
		delete net_backend;
		net_backend = nullptr;
		FAILURE(Runtime, "Could not initialize the network backend (see messages above)");
	}

	// set default mac = Digital ethernet prefix: 08-00-2B + hexified "ES40" + nic number
	state.mac[0] = 0x08;
	state.mac[1] = 0x00;
	state.mac[2] = 0x2B;
	state.mac[3] = 0xE5;
	state.mac[4] = 0x40;
	state.mac[5] = nic_num++;

	// set assigned mac
	cfg = myCfg->get_text_value("mac");
	if (cfg)
	{
		const char* mac_chars = "0123456789abcdefABCDEF-.:";
		const char* hex_chars = "0123456789abcdefABCDEF";
		const char* hex_scanf = "%hx";
		bool          mac_replaced = false;
		if ((strlen(cfg) == 17) && (strspn(cfg, mac_chars) == 17))
		{
			char  newmac[18];
			strcpy(newmac, cfg);
			newmac[2] = newmac[5] = newmac[8] = newmac[11] = newmac[14] = 0;
			if ((strspn(&newmac[0], hex_chars) == 2)
				&& (strspn(&newmac[3], hex_chars) == 2)
				&& (strspn(&newmac[6], hex_chars) == 2)
				&& (strspn(&newmac[9], hex_chars) == 2)
				&& (strspn(&newmac[12], hex_chars) == 2)
				&& (strspn(&newmac[15], hex_chars) == 2))
			{
				short unsigned int  num;
				sscanf(&newmac[0], hex_scanf, &num);
				state.mac[0] = num & 0xff;
				sscanf(&newmac[3], hex_scanf, &num);
				state.mac[1] = num & 0xff;
				sscanf(&newmac[6], hex_scanf, &num);
				state.mac[2] = num & 0xff;
				sscanf(&newmac[9], hex_scanf, &num);
				state.mac[3] = num & 0xff;
				sscanf(&newmac[12], hex_scanf, &num);
				state.mac[4] = num & 0xff;
				sscanf(&newmac[15], hex_scanf, &num);
				state.mac[5] = num & 0xff;
				mac_replaced = true;
			}
		}

		if (mac_replaced)
		{
			printf("%s: MAC set to %s\n", devid_string, cfg);
		}
		else
		{
			FAILURE_1(Configuration,
				"MAC address (%s) should have xx-xx-xx-xx-xx-xx format", cfg);
		}
	}
	else
	{
		char  mac[18];
		sprintf(mac, "%02X-%02X-%02X-%02X-%02X-%02X", state.mac[0], state.mac[1],
			state.mac[2], state.mac[3], state.mac[4], state.mac[5]);
		printf("%s: MAC defaulted to %s\n", devid_string, mac);
	}

	rx_queue = new CPacketQueue("rx_queue",
		(int)myCfg->get_num_value("queue", false, 1024));
	calc_crc = myCfg->get_bool_value("crc", false);
	trace_packets = myCfg->get_bool_value("trace_packets", false);
	/* Hidden option: defer SIA autoneg completion ~50ms. */
	autoneg_delay_enabled = myCfg->get_bool_value("autonegotiate_delay", false);

	state.rx.cur_buf = NULL;
	/* Use a 2KB TX scratch buffer like QEMU's tulip (tx_frame[2048]) to avoid overflows. */
	state.tx.cur_buf = (unsigned char*)malloc(nic_tx_capacity);
	state.irq_was_asserted = false;

	ResetPCI();
	ResetNIC();   // explicit one-shot internal reset; previously implicit via ResetPCI

	myThread = 0;

	printf("%s: $Id$\n",
		devid_string);
}

void CDEC21143::start_threads()
{
	if (!myThread)
	{
		myThread = new CThread("nic");
		printf(" %s", myThread->getName().c_str());
		StopThread = false;
		myThread->start(*this);
#if !defined(_WIN32)
		/* Wake the service thread on packet arrival; otherwise inbound
		 * packets wait out the 10 ms tryWait window (~5 ms avg added). */
		if (net_backend && net_backend->get_wait_fd() >= 0 &&
			!rx_wake_thread.joinable())
			rx_wake_thread = std::thread(&CDEC21143::rx_wake_loop, this);
#endif
	}
}

#if !defined(_WIN32)
#include <poll.h>
#include <unistd.h>

void CDEC21143::rx_wake_loop()
{
	struct pollfd pfd;
	pfd.fd = net_backend->get_wait_fd();
	pfd.events = POLLIN;

	while (!StopThread)
	{
		int r = poll(&pfd, 1, 250);
		if (StopThread)
			break;
		if (r > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP)))
		{
			mySemaphore.tryWait(0);
			mySemaphore.set();

			/* Let the service thread drain the fd; avoids a
			 * level-triggered spin. */
			usleep(500);
		}
	}
}
#endif

void CDEC21143::stop_threads()
{
	StopThread = true;
	if (myThread)
	{
		mySemaphore.tryWait(0);
		mySemaphore.set();
		printf(" %s", myThread->getName().c_str());
		myThread->join();
		delete myThread;
		myThread = 0;
	}
#if !defined(_WIN32)
	if (rx_wake_thread.joinable())
		rx_wake_thread.join();
#endif
}

/**
 * Destructor.
 **/
CDEC21143::~CDEC21143()
{
	stop_threads();

	if (net_backend != nullptr) {
		net_backend->close();
		delete net_backend;
		net_backend = nullptr;
	}
	delete rx_queue;
	/* Free TX scratch on device teardown (not on ResetNIC, which expects it alive). */
	if (state.tx.cur_buf) {
		free(state.tx.cur_buf);
		state.tx.cur_buf = nullptr;
	}
}

u32 CDEC21143::ReadMem_Bar(int func, int bar, u32 address, int dsize)
{
	switch (bar)
	{
	case 0: // CBIO
	case 1: // CBMA
		return nic_read(address, dsize);
	}

	printf("21143: ReadMem_Bar: unsupported bar %d\n", bar);
	return 0;
}

void CDEC21143::WriteMem_Bar(int func, int bar, u32 address, int dsize, u32 data)
{
	switch (bar)
	{
	case 0: // CBIO
	case 1: // CBMA
		nic_write(address, dsize, (u32)endian_bits(data, dsize));
		return;
	}

	printf("21143: WriteMem_Bar: unsupported bar %d\n", bar);
}

/**
 * Check if threads are still running.
 **/
void CDEC21143::check_state()
{
	if (myThread && !myThread->isRunning())
		FAILURE(Thread, "NIC thread has died");
}

void CDEC21143::receive_process()
{
	const u8* packet_data = NULL;
	int       packet_len = 0;

	// if receive process active
	if (state.reg[CSR_OPMODE / 8] & OPMODE_SR)
	{

		// get packets from host nic if not in internal loopback mode
		if (!(state.reg[CSR_OPMODE / 8] & OPMODE_OM_INTLOOP))
		{
			while (net_backend->receive(&packet_data, &packet_len) > 0)
			{
				if (trace_packets)
					trace_packet("RX", packet_data, packet_len);
				bool  resl = rx_queue->add_tail(packet_data, packet_len,
					calc_crc, true);
				state.reg[CSR_SIASTAT / 8] |= SIASTAT_TRA;  //set 10bT activity
			}
		}

		// process a receive descriptor until we run out of
		// descriptors or packets to process
		while (dec21143_rx());
	}
}

void CDEC21143::trace_packet(const char* dir, const u8* frame, int len)
{
	char dst[18];
	char src[18];
	u16 ether_type;

	if (len < 14)
	{
		printf("%s: %s len=%d short ethernet frame\n", devid_string, dir, len);
		return;
	}

	pkt_format_mac(dst, &frame[0]);
	pkt_format_mac(src, &frame[6]);
	ether_type = get_be16(&frame[12]);

	if (ether_type == 0x0806 && len >= 42)
	{
		char sha[18];
		char tha[18];
		char spa[16];
		char tpa[16];
		u16 op = get_be16(&frame[20]);

		pkt_format_mac(sha, &frame[22]);
		pkt_format_ip(spa, &frame[28]);
		pkt_format_mac(tha, &frame[32]);
		pkt_format_ip(tpa, &frame[38]);
		printf("%s: %s len=%d %s -> %s ARP op=%u sha=%s spa=%s tha=%s tpa=%s\n",
			devid_string, dir, len, src, dst, op, sha, spa, tha, tpa);
		return;
	}

	if (ether_type == 0x0800 && len >= 34)
	{
		const u8* ip = &frame[14];
		int ihl = (ip[0] & 0x0f) * 4;
		char src_ip[16];
		char dst_ip[16];
		u8 proto;

		if (ihl < 20 || len < 14 + ihl)
		{
			printf("%s: %s len=%d %s -> %s IPv4 malformed\n",
				devid_string, dir, len, src, dst);
			return;
		}

		proto = ip[9];
		pkt_format_ip(src_ip, &ip[12]);
		pkt_format_ip(dst_ip, &ip[16]);

		if (proto == 17 && len >= 14 + ihl + 8)
		{
			const u8* udp = ip + ihl;
			int udp_payload_len = len - 14 - ihl - 8;
			u16 sport = get_be16(&udp[0]);
			u16 dport = get_be16(&udp[2]);
			char dhcp[220];

			dhcp[0] = '\0';
			if ((sport == 67 || sport == 68 || dport == 67 || dport == 68) &&
				udp_payload_len > 0)
				describe_dhcp(udp + 8, udp_payload_len, dhcp, sizeof(dhcp));

			printf("%s: %s len=%d %s -> %s IPv4 %s:%u -> %s:%u UDP%s\n",
				devid_string, dir, len, src, dst, src_ip, sport,
				dst_ip, dport, dhcp);
			return;
		}

		if (proto == 1 && len >= 14 + ihl + 2)
		{
			const u8* icmp = ip + ihl;
			printf("%s: %s len=%d %s -> %s IPv4 %s -> %s ICMP type=%u code=%u\n",
				devid_string, dir, len, src, dst, src_ip, dst_ip,
				icmp[0], icmp[1]);
			return;
		}

		printf("%s: %s len=%d %s -> %s IPv4 %s -> %s proto=%u\n",
			devid_string, dir, len, src, dst, src_ip, dst_ip, proto);
		return;
	}

	printf("%s: %s len=%d %s -> %s ethertype=0x%04x\n",
		devid_string, dir, len, src, dst, ether_type);
}

/**
 * Read from the NIC registers.
 **/
u32 CDEC21143::nic_read(u32 address, int dsize)
{
	u32 data = 0;

	u32 oldreg = 0;
	int regnr = (int)(address >> 3);

	if ((address & 7) == 0 && regnr < 32)
	{
		data = state.reg[regnr];
	}
	else
		printf("dec21143: WARNING! unaligned access (0x%x) \n", (int)address);
#if defined(DEBUG_NIC)
	printf("21143: nic_read - CSR(%d), value: %08x\n", regnr, data);
#endif
	return data;
}

/**
 * Write to the NIC registers.
 **/
void CDEC21143::nic_write(u32 address, int dsize, u32 data)
{
	uint32_t  oldreg = 0;

	int       regnr = (int)(address >> 3);

#if defined(DEBUG_NIC)
	printf("21143: nic_write - CSR(%d), value: %08x\n", regnr, data);
#endif

	//printf("21143: rx_queue->name= %s\n", rx_queue->name);
	if ((address & 7) == 0 && regnr < 32)
	{
		oldreg = state.reg[regnr];
		switch (regnr)
		{
		case CSR_STATUS / 8:  /*  CSR5: Write-1-to-clear */
			/* Clear all write-1-to-clear events like QEMU. */
			state.reg[regnr] &=
				~((u32)data & (STATUS_TI | STATUS_TPS | STATUS_TU | STATUS_TJT |
					STATUS_LNPANC | STATUS_UNF | STATUS_RI | STATUS_RU |
					STATUS_RPS | STATUS_RWT | STATUS_ETI | STATUS_LNF |
					STATUS_SE | STATUS_ER | STATUS_AIS | STATUS_NIS |
					STATUS_GPPI | STATUS_LC));
#if defined(DEBUG_NIC_IRQ)
			printf("21143: CSR5 W1C=%08x -> CSR5=%08x CSR7=%08x\n", data, state.reg[CSR_STATUS / 8], state.reg[CSR_INTEN / 8]);
#endif
			/* Drop INTx immediately if that cleared the cause(s). */
			update_irq();
			break;

		case CSR_MISSED / 8:  /*  Read only  */
			break;

		default:
			state.reg[regnr] = (u32)data;
		}
	}
	else
		printf("[ dec21143: WARNING! unaligned access (0x%x) ]\n", (int)address);

	switch (address)
	{
	case CSR_BUSMODE:       /*  csr0  */
		if (data & BUSMODE_SWR)
		{
			ResetNIC();
			data &= ~BUSMODE_SWR;
		}

		// calculate descriptor skip length in bytes
		state.descr_skip = ((data & BUSMODE_DSL) >> 2) * 4;
		break;

	case CSR_TXPOLL:        /*  csr1  */
		/* CaVa interpretation... */
		state.reg[CSR_STATUS / 8] &= ~STATUS_TU;
		state.tx.suspend = false;
		mySemaphore.tryWait(0);
		mySemaphore.set();
		break;

	case CSR_RXPOLL:        /*  csr2  */
		mySemaphore.tryWait(0);
		mySemaphore.set();
		break;

	case CSR_RXLIST:        /*  csr3  */
		/* debug("[ dec21143: setting RXLIST to 0x%x ]\n", (int)data); */
		if (data & 0x3)
			printf("[ dec21143: WARNING! RXLIST not aligned? (0x%x) ]\n", data);
		data &= ~0x3;
		state.rx.cur_addr = data;
		break;

	case CSR_TXLIST:        /*  csr4  */
		/* debug("[ dec21143: setting TXLIST to 0x%x ]\n", (int)data); */
		if (data & 0x3)
			printf("[ dec21143: WARNING! TXLIST not aligned? (0x%x) ]\n", data);
		data &= ~0x3;
		state.tx.cur_addr = data;
		break;

	case CSR_STATUS:        /* csr5: handled above; just ensure IRQ is updated */
		update_irq();
		break;

	case CSR_INTEN:         /* csr7: interrupt mask */
		/* NOTE: state.reg[CSR_INTEN/8] was already written via default case above.
				 We just need to update the line level right now. */
#if defined(DEBUG_NIC_IRQ)
		printf("21143: CSR7 write %08x (mask)\n", state.reg[CSR_INTEN / 8]);
#endif
		update_irq();
		break;

	case CSR_OPMODE:        /*  csr6:  */
#if defined(DEBUG_NIC_IRQ)
		if ((data ^ oldreg) & (OPMODE_SR | OPMODE_ST))
			printf("21143: CSR6 SR %d->%d ST %d->%d (CSR5=%08x)\n",
				!!(oldreg & OPMODE_SR), !!(data & OPMODE_SR),
				!!(oldreg & OPMODE_ST), !!(data & OPMODE_ST),
				state.reg[CSR_STATUS / 8]);
#endif
		/* MBO (bit 25) is hardware-driven on real silicon and always reads
		 * back as 1, regardless of what the driver wrote (HRM 3.2.2.6,
		 * Table 3-42). Force it set in the stored value so a write/read-back
		 * verify by the driver (e.g. AlphaBIOS adapter self-test) succeeds
		 * even when the write omitted MBO. */
		state.reg[regnr] |= 0x02000000;
		if (data & 0x02000000)
		{

			/*  A must-be-one bit.  */
			data &= ~0x02000000;
		}

		if (data & OPMODE_ST)
		{

			//data &= ~OPMODE_ST;
		}
		else
		{

			/*  Turned off TX? Then idle:  */

			//      state.reg[CSR_STATUS/8] |= STATUS_TPS;
		}

		if (data & OPMODE_SR)
		{

			//data &= ~OPMODE_SR;
		}
		else
		{

			/*  Turned off RX? Then go to stopped state:  */

			//state.reg[CSR_STATUS/8] &= ~STATUS_RS;
		}

		// Did Start/Stop Transmission change state ?
		if ((data ^ oldreg) & OPMODE_ST)
		{
			if (data & OPMODE_ST)
			{ // ST went high
				set_tx_state(STATUS_TS_SUSPENDED);
				/* transmitter running -> clear 'process stopped' */
				state.reg[CSR_STATUS / 8] &= ~STATUS_TPS;
				mySemaphore.tryWait(0);
				mySemaphore.set();
			}
			else
			{ // ST went low
				set_tx_state(STATUS_TS_STOPPED);
				/* transmitter stopped -> advertise idle */
				state.reg[CSR_STATUS / 8] |= STATUS_TPS;
			}
		}

		// Did Start/Stop Receive change state ?
		if ((data ^ oldreg) & OPMODE_SR)
		{
			if (data & OPMODE_SR)
			{ // SR went high
				set_rx_state(STATUS_RS_WAIT);
				state.reg[CSR_STATUS / 8] &= ~STATUS_RPS;
				mySemaphore.tryWait(0);
				mySemaphore.set();
			}
			else
			{ // SR went low
				/* BUGFIX: this must change the RX state, not TX */
				set_rx_state(STATUS_RS_STOPPED);
				state.reg[CSR_STATUS / 8] |= STATUS_RPS;
				/* When receiver is stopped, drop any queued frames to avoid growth while SR=0. */
				if (rx_queue) {
					rx_queue->flush();
				}
			}
			/* Mode & status changed → recompute IRQ level now. */
			update_irq();
		}

		/* If mode bits affecting reception/filtering changed, rebuild BPF. */
		if ((data ^ oldreg) & (OPMODE_PR | OPMODE_IF | OPMODE_PM | OPMODE_RA)) {
			SetupFilter();
		}
		break;

	case CSR_MISSED:  /*  csr8  */
		break;

	case CSR_MIIROM:  /*  csr9  */
		if (data & MIIROM_MDC)
			mii_access(oldreg, (u32)data);
		else
			srom_access(oldreg, (u32)data);
		break;

	case CSR_SIASTAT: /*  csr12  */
		state.reg[CSR_SIASTAT / 8] = oldreg;  /* status register: ignore written data */
		if (((data & SIASTAT_ANS) == SIASTAT_ANS_START)
			&& (state.reg[CSR_SIATXRX / 8] & SIATXRX_ANE))
		{
			start_sia_autoneg();
		}
		break;

	case CSR_SIATXRX: /*  csr14  */
		if ((data & SIATXRX_ANE) && (state.reg[CSR_SIACONN / 8] & SIACONN_SRL))
			start_sia_autoneg();
		break;

	case CSR_SIACONN: /*  csr13  */
		if ((data & SIACONN_SRL) && (state.reg[CSR_SIATXRX / 8] & SIATXRX_ANE))
		{
			start_sia_autoneg();
		}
		break;

	case CSR_SIAGEN:  /*  csr15  */
		break;

	default:
		printf("[ dec21143: write to unimplemented 0x%02x: 0x%02x ]\n", (int)address,
			(int)data);
	}
}

void CDEC21143::set_tx_state(int tx_state)
{
	state.reg[CSR_STATUS / 8] &= ~STATUS_TS;
	state.reg[CSR_STATUS / 8] |= (tx_state & STATUS_TS);
}

void CDEC21143::set_rx_state(int rx_state)
{
	state.reg[CSR_STATUS / 8] &= ~STATUS_RS;
	state.reg[CSR_STATUS / 8] |= (rx_state & STATUS_RS);
}

/**
 *  This function handles accesses to the MII. Data streams seem to be of the
 *  following format:
 *
 *      vv---- starting delimiter
 *  ... 01 xx yyyyy zzzzz a[a] dddddddddddddddd
 *         ^---- I am starting with mii_bit = 0 here
 *
 *  where x = opcode (10 = read, 01 = write)
 *        y = PHY address
 *        z = register address
 *        a = on Reads: ACK bit (returned, should be 0)
 *            on Writes: _TWO_ dummy bits (10)
 *        d = 16 bits of data (MSB first)
 **/
void CDEC21143::mii_access(uint32_t oldreg, uint32_t idata)
{
	int       obit;

	int       ibit = 0;
	uint16_t  tmp;

	/*  Only care about data during clock cycles:  */
	if (!(idata & MIIROM_MDC))
		return;

	if (idata & MIIROM_MDC && oldreg & MIIROM_MDC)
		return;

	/*  printf("[ mii_access(): 0x%08x ]\n", (int)idata);  */
	if (idata & MIIROM_BR)
	{
		printf("[ mii_access(): MIIROM_BR: TODO ]\n");
		return;
	}

	obit = idata & MIIROM_MDO ? 1 : 0;

	if (state.mii.state >= MII_STATE_START_WAIT
		&& state.mii.state <= MII_STATE_READ_PHYADDR_REGADDR && idata & MIIROM_MIIDIR) printf("[ mii_access(): bad dir? ]\n");

	switch (state.mii.state)
	{
	case MII_STATE_RESET:

		/*  Wait for a starting delimiter (0 followed by 1).  */
		if (obit)
			return;
		if (idata & MIIROM_MIIDIR)
			return;

		/*  printf("[ mii_access(): got a 0 delimiter ]\n");  */
		state.mii.state = MII_STATE_START_WAIT;
		state.mii.opcode = 0;
		state.mii.phyaddr = 0;
		state.mii.regaddr = 0;
		break;

	case MII_STATE_START_WAIT:

		/*  Wait for a starting delimiter (0 followed by 1).  */
		if (!obit)
			return;
		if (idata & MIIROM_MIIDIR)
		{
			state.mii.state = MII_STATE_RESET;
			return;
		}

		/*  printf("[ mii_access(): got a 1 delimiter ]\n");  */
		state.mii.state = MII_STATE_READ_OP;
		state.mii.bit = 0;
		break;

	case MII_STATE_READ_OP:
		if (state.mii.bit == 0)
		{
			state.mii.opcode = obit << 1;

			/*  printf("[ mii_access(): got first opcode bit (%i) ]\n", obit);  */
		}
		else
		{
			state.mii.opcode |= obit;

			/*  printf("[ mii_access(): got opcode = %i ]\n", state.mii.opcode);  */
			state.mii.state = MII_STATE_READ_PHYADDR_REGADDR;
		}

		state.mii.bit++;
		break;

	case MII_STATE_READ_PHYADDR_REGADDR:

		/*  printf("[ mii_access(): got phy/reg addr bit nr %i (%i)"
		   " ]\n", state.mii.bit - 2, obit);  */
		if (state.mii.bit <= 6)
			state.mii.phyaddr |= obit << (6 - state.mii.bit);
		else
			state.mii.regaddr |= obit << (11 - state.mii.bit);
		state.mii.bit++;
		if (state.mii.bit >= 12)
		{

			/*  printf("[ mii_access(): phyaddr=0x%x regaddr=0x"
			   "%x ]\n", state.mii.phyaddr, state.mii.regaddr);  */
			state.mii.state = MII_STATE_A;
		}
		break;

	case MII_STATE_A:
		switch (state.mii.opcode)
		{
		case MII_COMMAND_WRITE:
			if (state.mii.bit >= 13)
				state.mii.state = MII_STATE_D;
			break;

		case MII_COMMAND_READ:
			ibit = 0;
			state.mii.state = MII_STATE_D;
			break;

		case 3: /* Some stacks tickle the line so we see '11b' treat as READ */
			ibit = 0;
			state.mii.state = MII_STATE_D;
			state.mii.opcode = MII_COMMAND_READ;
			break;

		default:
			printf("[ mii_access(): UNIMPLEMENTED MII opcode %i (probably just a bug in GXemul's MII data stream handling) ]\n",
				state.mii.opcode);
			state.mii.state = MII_STATE_RESET;
		}

		state.mii.bit++;
		break;

	case MII_STATE_D:
		switch (state.mii.opcode)
		{
		case MII_COMMAND_WRITE:
			if (idata & MIIROM_MIIDIR)
				printf("[ mii_access(): write: bad dir? ]\n");
			obit = obit ? (0x8000 >> (state.mii.bit - 14)) : 0;
			tmp = state.mii.phy_reg[(state.mii.phyaddr << 5) + state.mii.regaddr] | obit;
			if (state.mii.bit >= 29)
			{
				state.mii.state = MII_STATE_IDLE;

				//                              debug("[ mii_access(): WRITE to phyaddr=0x%x regaddr=0x%x: 0x%04x ]\n", state.mii.phyaddr, state.mii.regaddr, tmp);
			}
			break;

		case MII_COMMAND_READ:
			if (!(idata & MIIROM_MIIDIR))
				break;
			tmp = state.mii.phy_reg[(state.mii.phyaddr << 5) + state.mii.regaddr];

			//                      if (state.mii.bit == 13)
			//                              debug("[ mii_access(): READ phyaddr=0x%x regaddr=0x%x: 0x%04x ]\n", state.mii.phyaddr, state.mii.regaddr, tmp);
			ibit = tmp & (0x8000 >> (state.mii.bit - 13));
			if (state.mii.bit >= 28)
				state.mii.state = MII_STATE_IDLE;
			break;
		}

		state.mii.bit++;
		break;

	case MII_STATE_IDLE:
		state.mii.bit++;
		if (state.mii.bit >= 31)
			state.mii.state = MII_STATE_RESET;
		break;
	}

	state.reg[CSR_MIIROM / 8] &= ~MIIROM_MDI;
	if (ibit)
		state.reg[CSR_MIIROM / 8] |= MIIROM_MDI;
}

/* deferred autoneg instead of finishing it inside the CSR write */
void CDEC21143::start_sia_autoneg()
{
	if (!autoneg_delay_enabled)
	{
		/* Default: legacy instant completion. */
		complete_sia_autoneg();
		return;
	}

	/* Negotiating: ANS = ability detect, link down, no partner seen yet. */
	state.reg[CSR_SIASTAT / 8] &= ~(SIASTAT_ANS | SIASTAT_LPN);
	state.reg[CSR_SIASTAT / 8] |= SIASTAT_ANS_ABD | SIASTAT_LS100 | SIASTAT_LS10;

	autoneg_complete_at = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(50);
	autoneg_pending.store(true, std::memory_order_release);
}

void CDEC21143::complete_sia_autoneg()
{
	const u32 link_partner =
		((u32)(ANLPAR_ACK | ANLPAR_TX_FD | ANLPAR_TX |
			ANLPAR_10_FD | ANLPAR_10 | ANLPAR_CSMA) << 16);

	/* Autonegotiation completes immediately against the emulated link partner.
	   Report a stable 100baseTX full-duplex link without remote-fault bits. */
	state.reg[CSR_SIASTAT / 8] &= ~(SIASTAT_ANS | SIASTAT_LPC |
		SIASTAT_LS100 | SIASTAT_LS10 | SIASTAT_NSN | SIASTAT_TRF);
	state.reg[CSR_SIASTAT / 8] |= SIASTAT_ANS_FLPGOOD |
		SIASTAT_LPN | link_partner;

	state.reg[CSR_STATUS / 8] &= ~STATUS_LNF;
	state.reg[CSR_STATUS / 8] |= STATUS_LNPANC;

	state.reg[CSR_OPMODE / 8] &= ~OPMODE_TTM;
	state.reg[CSR_OPMODE / 8] |= OPMODE_PS | OPMODE_PCS |
		OPMODE_SCR | OPMODE_FD | OPMODE_HBD;

	state.reg[CSR_SIATXRX / 8] &= ~(SIATXRX_TH | SIATXRX_THX | SIATXRX_T4);
	state.reg[CSR_SIATXRX / 8] |= SIATXRX_TXF;

	update_irq();
}

/**
 *  This function handles reads from the Ethernet Address ROM. This is not a
 *  100% correct implementation, as it was reverse-engineered from OpenBSD
 *  sources; it seems to work with OpenBSD, NetBSD, and Linux, though.
 *
 *  Each transfer (if I understood this correctly) is of the following format:
 *
 *	1xx yyyyyy zzzzzzzzzzzzzzzz
 *
 *  where 1xx    = operation (6 means a Read),
 *        yyyyyy = ROM address
 *        zz...z = data
 *
 *  y and z are _both_ read and written to at the same time; this enables the
 *  operating system to sense the number of bits in y (when reading, all y bits
 *  are 1 except the last one).
 **/
void CDEC21143::srom_access(uint32_t oldreg, uint32_t idata)
{
	int obit;

	int ibit;

	/*  debug("CSR9 WRITE! 0x%08x\n", (int)idata);  */

	/*  New selection? Then reset internal state.  */
	if (idata & MIIROM_SR && !(oldreg & MIIROM_SR))
	{
		state.srom.curbit = 0;
		state.srom.opcode = 0;
		state.srom.opcode_has_started = 0;
		state.srom.addr = 0;
	}

	/*  Only care about data during clock cycles:  */
	if (!(idata & MIIROM_SROMSK))
		return;

	obit = 0;
	ibit = idata & MIIROM_SROMDI ? 1 : 0;

	/*  debug("CLOCK CYCLE! (bit %i): ", state.srom.curbit);  */

	/*
	 *  Linux sends more zeroes before starting the actual opcode, than
	 *  OpenBSD and NetBSD. Hopefully this is correct. (I'm just guessing
	 *  that all opcodes should start with a 1, perhaps that's not really
	 *  the case.)
	 */
	if (!ibit && !state.srom.opcode_has_started)
		return;

	if (state.srom.curbit < 3)
	{
		state.srom.opcode_has_started = 1;
		state.srom.opcode <<= 1;
		state.srom.opcode |= ibit;

		/*  debug("opcode input '%i'\n", ibit);  */
	}
	else
	{
		switch (state.srom.opcode)
		{
		case TULIP_SROM_OPC_READ:
			if (state.srom.curbit < 6 + 3)
			{
				obit = state.srom.curbit < 6 + 2;
				state.srom.addr <<= 1;
				state.srom.addr |= ibit;
			}
			else
			{
				uint16_t  romword = state.srom.data[state.srom.addr * 2] +
					(state.srom.data[state.srom.addr * 2 + 1] << 8);

				//                              if (state.srom.curbit == 6 + 3)
				//                                      debug("[ dec21143: ROM read from offset 0x%03x: 0x%04x ]\n", state.srom.addr, romword);
				obit = romword & (0x8000 >> (state.srom.curbit - 6 - 3)) ? 1 : 0;
#if defined(DEBUG_NIC_SROM)
				printf("%%NIC-I-SROMREAD: Read %04x from %04x\n", romword,
					state.srom.addr);
#endif
			}
			break;

		default:
			printf("[ dec21243: unimplemented SROM/EEPROM opcode %i ]\n",
				state.srom.opcode);
		}

		state.reg[CSR_MIIROM / 8] &= ~MIIROM_SROMDO;
		if (obit)
			state.reg[CSR_MIIROM / 8] |= MIIROM_SROMDO;

		/*  debug("input '%i', output '%i'\n", ibit, obit);  */
	}

	state.srom.curbit++;

	/*
	 *  Done opcode + addr + data? Then restart. (At least NetBSD does
	 *  sequential reads without turning selection off and then on.)
	 */
	if (state.srom.curbit >= 3 + 6 + 16)
	{
		state.srom.curbit = 0;
		state.srom.opcode = 0;
		state.srom.opcode_has_started = 0;
		state.srom.addr = 0;
	}
}

/*
bool CDEC21143:acquire_rx_descriptor(u32 status_true, u32 status_false) {
	// get current receive descriptor
	do_pci_read(state.rx.cur_addr, descr, 4, 4);

	if (rdes0 & TDSTAT_OWN) {	// 21143 owns descriptor
		state.reg[CSR_STATUS/8] &= ~STATUS_RU;	// clear buffers unavailable
		if (state.reg[CSR_OPMODE/8] & OPMODE_SR) {	// if receive start
			state.reg[CSR_STATUS/8] = (state.reg[CSR_STATUS/8] & ~STATUS_RS) | STATUS_RS_WAIT;
		} else {
		}
		return true;		// indicate nothing was processed
	} else {
		state.reg[CSR_STATUS/8] = (state.reg[CSR_STATUS/8] & ~(STATUS_RS | STATUS_RU)) | STATUS_R
		return true;
	}
}
*/

/**
 *  Receive a packet. (If there is no current packet, then check for newly
 *  arrived ones. If the current packet couldn't be fully transfered the
 *  last time, then continue on that packet.)
 **/
int CDEC21143::dec21143_rx()
{
	static u32    descr[4];
	static u32& rdes0 = descr[0];
	static u32& rdes1 = descr[1];
	static u32& rdes2 = descr[2];
	static u32& rdes3 = descr[3];

	u32           addr = state.rx.cur_addr;
	u32           bufaddr;

	//unsigned char descr[16];
	//u32 rdes0, rdes1, rdes2, rdes3;
	int           bufsize;

	//unsigned char descr[16];
	int           buf1_size;

	//unsigned char descr[16];
	int           buf2_size;

	//unsigned char descr[16];
	//int           writeback_len = 4;

	//unsigned char descr[16];
	int           to_xfer;

	//struct pcap_pkthdr * packet_header;
	//const u_char * packet_data = NULL;

	/*  Is current packet finished? Then check for new ones.  */
	if (state.rx.current.used >= state.rx.current.len)
	{

		/*  Nothing available? Then abort.  */
		if (rx_queue->count() == 0)
			return 0;           // indicate nothing was processed

		//      printf("pcap recv: %d bytes (%d captured) for %02x:%02x:%02x:%02x:%02x:%02x  \n",packet_header->len, packet_header->caplen,packet_data[0],packet_data[1],packet_data[2],packet_data[3],packet_data[4],packet_data[5]);
		// get next packet from receive queue
		rx_queue->get_head(state.rx.current);

		/*  Append a 4 byte CRC:  */

		//state.rx.cur_buf_len += 4;
		//CHECK_REALLOCATION(state.rx.cur_buf, realloc(state.rx.cur_buf, state.rx.cur_buf_len), unsigned char);

		/*  Get the next packet into our buffer:  */

		//memcpy(state.rx.cur_buf, packet_data, state.rx.cur_buf_len);

		/*  Well... the CRC is just zeros, for now.  */

		//memset(state.rx.cur_buf + state.rx.cur_buf_len - 4, 0, 4);
		//state.rx.cur_offset = 0;
	}

	// Fatal bus error (SE) halts both DMA engines until a reset 
	// (HRM: "a software or hardware reset is required")
	// frames arriving meanwhile are discarded and counted. 
	// Re-enabling BME doesn't revive the engines. 
	if (state.reg[CSR_STATUS / 8] & STATUS_SE)
	{
		state.rx.current.used = state.rx.current.len;   // discard
		u32 missed = state.reg[CSR_MISSED / 8];
		if ((missed & 0xffff) == 0xffff) missed |= 0x10000; else missed++;
		state.reg[CSR_MISSED / 8] = missed;
		return 0;
	}

	// Master access with PCI Command.BME off = master abort = fatal bus error:
	// latch SE, stop both processes.
	if (!(config_read(0, 0x04, 16) & 0x4))
	{
		state.reg[CSR_STATUS / 8] |= STATUS_SE;
		set_rx_state(STATUS_RS_STOPPED);
		set_tx_state(STATUS_TS_STOPPED);
		state.tx.suspend = true;
		state.rx.current.used = state.rx.current.len;   // discard
		u32 missed = state.reg[CSR_MISSED / 8];
		if ((missed & 0xffff) == 0xffff) missed |= 0x10000; else missed++;
		state.reg[CSR_MISSED / 8] = missed;
		return 0;
	}

	// read current descriptor
	do_pci_read(state.rx.cur_addr, descr, 4, 4);
	// respect DBO bit - swap descriptor byte order if needed
	if (state.reg[CSR_BUSMODE / 8] & BUSMODE_DBO) {
		for (int i = 0; i < 4; i++) descr[i] = swap_32(descr[i]);
	}

	rdes0 = descr[0];
	rdes1 = descr[1];
	rdes2 = descr[2];
	rdes3 = descr[3];

	/*  Only use descriptors owned by the 21143:  */
	if (!(rdes0 & TDSTAT_OWN))
	{
		// 21143 DISCARDS the arriving frame with missed count in CSR8
		// latches RU only on the running to suspended change.
		// Incriment counter during suspension. 
		const bool was_suspended =
			(state.reg[CSR_STATUS / 8] & STATUS_RS) == STATUS_RS_SUSPENDED;
		if (state.rx.current.used < state.rx.current.len)
		{
#if defined(DEBUG_NIC_IRQ)
			printf("21143: RX no-descriptor, frame dropped (CSR5=%08x)\n",
				state.reg[CSR_STATUS / 8]);
#endif
			state.rx.current.used = state.rx.current.len;   // drop the frame
			u32 missed = state.reg[CSR_MISSED / 8];
			if ((missed & 0xffff) == 0xffff)
				missed |= 0x10000;                          // MFO: counter overflow
			else
				missed++;
			state.reg[CSR_MISSED / 8] = missed;
		}

		// set recive buffers unavailable and receive state to suspended
		state.reg[CSR_STATUS / 8] = (state.reg[CSR_STATUS / 8] & ~STATUS_RS) |
			(was_suspended ? 0 : STATUS_RU) |
			STATUS_RS_SUSPENDED;
		return 0;             // indicate nothing was processed
	}

	buf1_size = rdes1 & TDCTL_SIZE1;
	buf2_size = (rdes1 & TDCTL_SIZE2) >> TDCTL_SIZE2_SHIFT;
	bufaddr = buf1_size ? rdes2 : rdes3;
	bufsize = buf1_size ? buf1_size : buf2_size;

	//state.reg[CSR_STATUS/8] &= ~STATUS_RS; // dth: wrong, this is receive state stopped
	//  printf("{ dec21143_rx: base = 0x%08x }\n", addr);
	//      debug("{ RX (%08x): 0x%08x 0x%08x 0x%x 0x%x: buf %d bytes at 0x%x }\n",
	//          addr, rdes0, rdes1, rdes2, rdes3, bufsize, (int)bufaddr);
	// Turn off all status bits, and give up ownership
	rdes0 = 0x00000000;

	//  Is this the first buffer of the frame?
	if (state.rx.current.used == 0)
		rdes0 |= TDSTAT_Rx_FS;

	// use buffer 1, if length is non-zero
	if (buf1_size > 0)
	{
		to_xfer = state.rx.current.len - state.rx.current.used;
		if (to_xfer > buf1_size)
			to_xfer = buf1_size;

		// DMA bytes from the packet into buffer 1
		do_pci_write(rdes2, &state.rx.current.frame[state.rx.current.used], 1,
			to_xfer);

		// update used
		state.rx.current.used += to_xfer;
	}

	// use buffer 2, if length is non-zero and not a chain buffer
	if ((buf2_size > 0) && (!(rdes1 & TDCTL_CH)))
	{
		to_xfer = state.rx.current.len - state.rx.current.used;
		if (to_xfer > buf2_size)
			to_xfer = buf2_size;

		// DMA bytes from the packet into buffer 2
		do_pci_write(rdes3, state.rx.current.frame + state.rx.current.used, 1,
			to_xfer);

		// update used
		state.rx.current.used += to_xfer;
	}

	//  Frame completed?
	if (state.rx.current.used >= state.rx.current.len)
	{

		//        debug("frame complete.\n");
		rdes0 |= TDSTAT_Rx_LS;

		/*  Set the frame length: */
		rdes0 |= ((state.rx.current.len) << 16) & TDSTAT_Rx_FL; /* include CRC like HW/QEMU */

		/* Set multicast / filter-fail flags. */
		const u8* dst = state.rx.current.frame;
		bool is_multicast = (dst[0] & 1) != 0;
		if (is_multicast) {
			rdes0 |= TDSTAT_Rx_MF;

		}
		/* Check perfect filter list programmed by setup-frame; fall back to our MAC. */
		bool perfect = false;
		for (int k = 0; k < 16; k++) {
			u8* ent = &state.setup_filter[k * 12];
			u8 pa[6] = { ent[0], ent[1], ent[4], ent[5], ent[8], ent[9] };
			if ((pa[0] | pa[1] | pa[2] | pa[3] | pa[4] | pa[5]) == 0)
				continue;
			if (memcmp(dst, pa, 6) == 0) { perfect = true; break; }
		}
		if (!perfect && memcmp(dst, state.mac, 6) == 0) {
			perfect = true;

		}
		if (!perfect) {
			if (state.reg[CSR_OPMODE / 8] & (OPMODE_PR | OPMODE_RA)) {
				rdes0 |= TDSTAT_Rx_FF;

			}
		}

		/* Data type per OM (normal / internal / external loopback). */
		if (state.reg[CSR_OPMODE / 8] & OPMODE_OM_INTLOOP) {
			rdes0 |= TDSTAT_Rx_DT_IL;
		}
		else if (state.reg[CSR_OPMODE / 8] & OPMODE_OM_EXTLOOP) {
			rdes0 |= TDSTAT_Rx_DT_EL;
		}
		else {
			rdes0 |= TDSTAT_Rx_DT_SR;
		}

		/*  Frame too long? (1518 is max ethernet frame length)  */
		if (state.rx.current.len > 1518)
			rdes0 |= TDSTAT_Rx_TL;
		// ^^ this is quite not possible in current code path...?

		// set receive interrupt and receive state to waiting-for-packet
		state.reg[CSR_STATUS / 8] = (state.reg[CSR_STATUS / 8] & ~STATUS_RS) |
			STATUS_RI |
			STATUS_RS_WAIT;
	}

	// Writeback rdes0, others are read-only
	state.reg[CSR_STATUS / 8] = (state.reg[CSR_STATUS / 8] & ~STATUS_RS) | STATUS_RS_CLOSE;
	// respect DBO bit - swap descriptor byte order if needed
	{
		u32 w0 = rdes0;
		if (state.reg[CSR_BUSMODE / 8] & BUSMODE_DBO) w0 = swap_32(w0);
		do_pci_write(state.rx.cur_addr, &w0, 4, 1);
	}

	// move to next descriptor
	if (rdes1 & TDCTL_ER)    // end-of-ring, return to base
		state.rx.cur_addr = state.reg[CSR_RXLIST / 8];
	else
	{
		if (rdes1 & TDCTL_CH)  // explicit chain, use chain address
			state.rx.cur_addr = rdes3;
		else
			// implicit chain
			state.rx.cur_addr += (4 * sizeof(uint32_t)) + state.descr_skip;
	}

	return 1; // indicate processing has occurred
}

/**
 *  Transmit a packet, if the guest OS has marked a descriptor as containing
 *  data to transmit.
 **/
int CDEC21143::dec21143_tx()
{
	u32           addr = state.tx.cur_addr;

	u32           bufaddr;
	u32			  descr[4];
	u32           tdes0;
	u32           tdes1;
	u32           tdes2;
	u32           tdes3;
	int           bufsize;
	int           buf1_size;
	int           buf2_size;

	if (state.tx.suspend)
		return 0;

	// Fatal bus error halts both engines until reset; a master access with
	// Command.BME off latches it (see dec21143_rx).
	if (state.reg[CSR_STATUS / 8] & STATUS_SE)
	{
		state.tx.suspend = true;
		return 0;
	}
	if (!(config_read(0, 0x04, 16) & 0x4))
	{
		state.reg[CSR_STATUS / 8] |= STATUS_SE;
		set_rx_state(STATUS_RS_STOPPED);
		set_tx_state(STATUS_TS_STOPPED);
		state.tx.suspend = true;
		return 0;
	}

	set_tx_state(STATUS_TS_FETCH);
	do_pci_read(addr, descr, 4, 4);

	if (state.reg[CSR_BUSMODE / 8] & BUSMODE_DBO) {
		for (int i = 0; i < 4; i++) descr[i] = swap_32(descr[i]);
	}
	tdes0 = descr[0];
	tdes1 = descr[1];
	tdes2 = descr[2];
	tdes3 = descr[3];

	/*  printf("{ dec21143_tx: base=0x%08x, tdes0=0x%08x }\n", (int)addr, (int)tdes0);  */

	/*  Only process packets owned by the 21143:  */
	if (!(tdes0 & TDSTAT_OWN))
	{
		/* HRM 4.3.7.1: on fetching an unowned descriptor, the chip raises
		 * STATUS_TU and moves to suspended state. Do this immediately, no
		 * idle threshold. */
		state.reg[CSR_STATUS / 8] |= STATUS_TU;
		set_tx_state(STATUS_TS_SUSPENDED);
		state.tx.suspend = true;
		update_irq();
		return 0;
	}

	buf1_size = tdes1 & TDCTL_SIZE1;
	buf2_size = (tdes1 & TDCTL_SIZE2) >> TDCTL_SIZE2_SHIFT;
	bufaddr = buf1_size ? tdes2 : tdes3;
	bufsize = buf1_size ? buf1_size : buf2_size;

	if (tdes1 & TDCTL_ER)    // end-of-ring, return to base
		state.tx.cur_addr = state.reg[CSR_TXLIST / 8];
	else
	{
		if (tdes1 & TDCTL_CH)  // explicit chain, use chain address
			state.tx.cur_addr = tdes3;
		else
			// implicit chain
			state.tx.cur_addr += (4 * sizeof(uint32_t)) + state.descr_skip;
	}

	/*
	   printf("{ TX (%llx): 0x%08x 0x%08x 0x%x 0x%x: buf %i bytes at 0x%x }\n",
	   (long long)addr, tdes0, tdes1, tdes2, tdes3, bufsize, (int)bufaddr);
	 */

	 /*  Assume no error:  */
	tdes0 &= ~
		(
			TDSTAT_Tx_UF |
			TDSTAT_Tx_EC |
			TDSTAT_Tx_LC |
			TDSTAT_Tx_NC |
			TDSTAT_Tx_LO |
			TDSTAT_Tx_TO |
			TDSTAT_ES
			);

	if (tdes1 & TDCTL_Tx_SET)
	{
		set_tx_state(STATUS_TS_SETUP);

		/*
		 *  Setup Packet.
		 *
		 *  TODO. For now, just ignore it, and pretend it worked.
		 */

		 //              printf("{ TX: setup packet }\n");
		if (bufsize != 192)
			printf("[ dec21143: setup packet len = %i, should be 192! ]\n",
				(int)bufsize);
		do_pci_read(bufaddr, state.setup_filter, 1, 192);
		SetupFilter();

		if (tdes1 & TDCTL_Tx_IC)
			state.reg[CSR_STATUS / 8] |= STATUS_TI;

		/*  Setup frame complete (HRM 4.2.3): clear OWN, set all other
		    TDES0 bits to 1. TDES1/2/3 are driver-owned -- do not touch
		    (matches QEMU tulip.c and 86Box net_tulip.c). */
		tdes0 = 0x7fffffff;
	}
	else
	{
		set_tx_state(STATUS_TS_READING);

		/*
		 *  Data Packet.
		 */

		 //              printf("{ TX: data packet: ");
		if (tdes1 & TDCTL_Tx_FS)
		{

			/*  First segment. Let's allocate a new buffer:  */

			/*  printf("new frame }\n");  */

			//CHECK_ALLOCATION(state.tx.cur_buf = (unsigned char *)malloc(bufsize));
			state.tx.cur_buf_len = 0;
		}
		else
		{

			/*  Not first segment. Increase the length of the current buffer:  */

			/*  printf("continuing last frame }\n");  */
			if (state.tx.cur_buf == NULL)
				printf("[ dec21143: WARNING! tx: middle segment, but no first segment?! ]\n");

			//CHECK_REALLOCATION(state.tx.cur_buf, realloc(state.tx.cur_buf, state.tx.cur_buf_len + bufsize), unsigned char);
		}

		/* Safely DMA data from guest memory, without exceeding TX scratch capacity. */
		{
			/* 2KB scratch (matches allocation in init()), large enough for legal ethernet frames */
			const int tx_cap = nic_tx_capacity;
			/* If guest tried to exceed scratch capacity, we will mark error at LS. */

			/* Buffer 1 */
			if (buf1_size > 0)
			{
				int avail = tx_cap - state.tx.cur_buf_len;
				int copy = (buf1_size < avail) ? buf1_size : (avail > 0 ? avail : 0);
				if (copy > 0)
				{
					do_pci_read(tdes2, state.tx.cur_buf + state.tx.cur_buf_len, 1, copy);
					state.tx.cur_buf_len += copy;
				}
			}

			/* Buffer 2 is valid unless the second address is chained.
			   TER only controls descriptor wraparound after this descriptor. */
			if (buf2_size > 0 && !(tdes1 & TDCTL_CH))
			{
				int avail = tx_cap - state.tx.cur_buf_len;
				int copy = (buf2_size < avail) ? buf2_size : (avail > 0 ? avail : 0);
				if (copy > 0)
				{
					do_pci_read(tdes3, state.tx.cur_buf + state.tx.cur_buf_len, 1, copy);
					state.tx.cur_buf_len += copy;
				}
			}
		}

		/*  Last segment? Then actually transmit it:  */
		if (tdes1 & TDCTL_Tx_LS)
		{

			/*  printf("{ TX: data frame complete. }\n");  */

			/* Enforce ethernet max frame length for delivery (without CRC = 1514 bytes). */
			bool frame_too_long = (state.tx.cur_buf_len > ETH_MAX_PACKET_RAW);

			/* Only transmit to wire if no loopback is active and frame size is legal. */
			if (!frame_too_long && !(state.reg[CSR_OPMODE / 8] & OPMODE_OM))
			{
				// printf("send: %d bytes   \n", state.tx.cur_buf_len);
				if (trace_packets)
					trace_packet("TX", state.tx.cur_buf, state.tx.cur_buf_len);
				net_backend->send(state.tx.cur_buf, state.tx.cur_buf_len);
			}

			/* In internal or external loopback, inject into RX queue iff RX is running and size ok. */
			if (!frame_too_long && (state.reg[CSR_OPMODE / 8] & OPMODE_OM) && (state.reg[CSR_OPMODE / 8] & OPMODE_SR))
			{
				bool  crc = !(tdes1 & TDCTL_Tx_AC);

				//printf("21143: %s packet, AC: %d\n", (state.reg[CSR_OPMODE / 8] & OPMODE_OM_INTLOOP) ? "IL" : "EL", !crc);
				//printf("21143: TX enabled: %d, RX enabled: %d\n", state.reg[CSR_OPMODE /8] & OPMODE_ST, state.reg[CSR_OPMODE /8] & OPMODE_SR);
				//printf("21143: tx(), len=%d, addr=%08x, mode=%s\n", state.tx.cur_buf_len, (int) state.tx.cur_buf, (state.reg[CSR_OPMODE / 8] & OPMODE_OM_INTLOOP) ? "IL" : "EL");
				//printf("21143: tx(), data=|");
				//unsigned char* aptr = state.tx.cur_buf;
				//for(int i=0; i<state.tx.cur_buf_len; i++) {
				//      printf("%02x-",*aptr++);
				//}
				//printf("|\n");
				(void)rx_queue->add_tail(state.tx.cur_buf, state.tx.cur_buf_len, calc_crc, crc);
			}

			/* Oversize frames: signal jabber and error summary, and do not deliver. */
			if (frame_too_long) {
				tdes0 |= TDSTAT_Tx_TO;   /* transmit jabber timeout */
				tdes0 |= TDSTAT_ES;      /* error summary */
			}

			//free(state.tx.cur_buf);
			//state.tx.cur_buf = NULL;
			state.tx.cur_buf_len = 0;

			/*  Interrupt, if Tx_IC is set:  */
			if (tdes1 & TDCTL_Tx_IC)
				state.reg[CSR_STATUS / 8] |= STATUS_TI;
		}

		/*  We are done with this segment.  */
		tdes0 &= ~TDSTAT_OWN;
	}

	/*  Error summary:  */
	if (tdes0 & (TDSTAT_Tx_UF | TDSTAT_Tx_EC | TDSTAT_Tx_LC | TDSTAT_Tx_NC |
		TDSTAT_Tx_LO | TDSTAT_Tx_TO))
		tdes0 |= TDSTAT_ES;

	set_tx_state(STATUS_TS_CLOSE);

	/*  Descriptor writeback:
	    only write back tdes0 - the status word
	    tdes1/tdes2/tdes3 are read-only from NIC's perspective */

	descr[0] = tdes0;
	if (state.reg[CSR_BUSMODE / 8] & BUSMODE_DBO)
	    descr[0] = swap_32(descr[0]);
	do_pci_write(addr, descr, 1, 4);

	return 1;
}

void CDEC21143::SetupFilter()
{
	u8    mac[16][6];
	char  mac_txt[16][20];
	int   i;
	int   j;
	int   numUnique;
	int   unique[16];
	bool  u;
#if defined(DEBUG_NIC_FILTER)
	printf("Building a filter...\n");
#endif
	for (i = 0; i < 16; i++)
	{
		mac[i][0] = state.setup_filter[i * 12];
		mac[i][1] = state.setup_filter[i * 12 + 1];
		mac[i][2] = state.setup_filter[i * 12 + 4];
		mac[i][3] = state.setup_filter[i * 12 + 5];
		mac[i][4] = state.setup_filter[i * 12 + 8];
		mac[i][5] = state.setup_filter[i * 12 + 9];
		sprintf(mac_txt[i], "%02x:%02x:%02x:%02x:%02x:%02x", mac[i][0], mac[i][1],
			mac[i][2], mac[i][3], mac[i][4], mac[i][5]);
#if defined(DEBUG_NIC_FILTER)
		printf("MAC[%d] = %s. \n", i, mac_txt[i]);
#endif
	}

#if defined(DEBUG_NIC_FILTER)
	printf("Filter mode: ");
	if (state.reg[CSR_OPMODE / 8] & OPMODE_PR)
		printf("promiscuous.\n");
	else
	{
		if (state.reg[CSR_OPMODE / 8] & OPMODE_IF)
			printf("inverse ");
		if (state.reg[CSR_OPMODE / 8] & OPMODE_HP)
		{
			printf("hash ");
			if (state.reg[CSR_OPMODE / 8] & OPMODE_HO)
				printf("only ");
		}
		else
			printf("perfect ");
		printf("filtering.\n");
	}
#endif
	numUnique = 0;
	for (i = 0; i < 16; i++)
	{
		if ((mac[i][0] | mac[i][1] | mac[i][2] | mac[i][3] | mac[i][4] | mac[i][5]) == 0)
			continue;

		u = true;
		for (j = 0; j < numUnique; j++)
		{
			if (mac[i][0] == mac[unique[j]][0] && mac[i][1] == mac[unique[j]][1]
				&& mac[i][2] == mac[unique[j]][2] && mac[i][3] == mac[unique[j]][3]
				&& mac[i][4] == mac[unique[j]][4] && mac[i][5] == mac[unique[j]][5])
			{
				u = false;
				break;
			}
		}

		if (u)
		{
			unique[numUnique] = i;
			numUnique++;
		}
	}

#if defined(DEBUG_NIC_FILTER)
	for (i = 0; i < numUnique; i++)
		printf("Unique MAC[%d] = %s. \n", i, mac_txt[unique[i]]);
#endif

	/* No setup-frame received yet: fall back to filtering on our own MAC,
	 * same as before this was split out into the network backend. */
	if (numUnique == 0)
	{
		memcpy(mac[0], state.mac, 6);
		numUnique = 1;
	}
	else
	{
		/* Compact the unique addresses down to the front of the array,
		 * since that's what the backend interface expects. */
		u8 compact[16][6];
		for (i = 0; i < numUnique; i++)
			memcpy(compact[i], mac[unique[i]], 6);
		memcpy(mac, compact, sizeof(u8) * 6 * numUnique);
	}

	/* Filtering semantics per CSR6[PR,PM,IF,RA]; the backend decides how
	   (or whether) to actually apply this on the host side.  */
	bool promisc = (state.reg[CSR_OPMODE / 8] & OPMODE_PR) != 0;
	bool receive_all = (state.reg[CSR_OPMODE / 8] & OPMODE_RA) != 0;
	bool pass_multi = (state.reg[CSR_OPMODE / 8] & OPMODE_PM) != 0;
	bool inverse = (state.reg[CSR_OPMODE / 8] & OPMODE_IF) != 0;

	net_backend->set_filter(mac, numUnique, promisc, receive_all, pass_multi, inverse);
}

/**
 * Reset the network interface internals to their condition immediately
 * after power-up, including the PCI configuration.
 **/
void CDEC21143::ResetPCI()
{
	CPCIDevice::ResetPCI();

	// Do NOT call ResetNIC() here. PCI bus reset (pchip 0x800, fired by LFU)
	// must preserve the chip's internal CSRs to match qemu's tulip behavior.
	// SRM does a partial NIC config (CSR0/13/6/7) immediately before triggering
	// the LFU self-IPI and expects that config to survive the bus reset, so it
	// skips re-init on the LFU restart path. Internal reset still happens via:
	//   - explicit ResetNIC() in init() (one-shot at construction)
	//   - BUSMODE_SWR (CSR0=1), which calls ResetNIC() inline in nic_write()
}

/**
 * Reset the network interface internals to their condition immediately
 * after power-up. This does not affect the PCI configuration.
 *
 * Code for computing the SROM checksum is taken from [T64].
 **/
void CDEC21143::ResetNIC()
{
	int leaf;

#if defined(DEBUG_NIC_IRQ)
	printf("21143: RESET (CSR5 was %08x)\n", state.reg[CSR_STATUS / 8]);
#endif

	// Drop any queued inbound frames; a real 21143 loses its RX FIFO on reset.
	if (rx_queue)
		rx_queue->flush();

	// Clear any previously programmed perfect-filter setup frame.
	memset(state.setup_filter, 0, sizeof(state.setup_filter));

	// Reset derived/internal soft state that is not covered by the CSR array.
	state.descr_skip = 0;
	state.rx.current.len = 0;
	state.rx.current.used = 0;
	state.rx.cur_offset = 0;
	state.rx.cur_buf_len = 0;
	state.tx.cur_buf_len = 0;
	state.tx.suspend = false;

	// Drop any partially assembled RX buffer.
	if (state.rx.cur_buf != NULL)
		free(state.rx.cur_buf);

	//  if (state.tx.cur_buf != NULL)
	//        free(state.tx.cur_buf);
	state.rx.cur_buf = /*state.tx.cur_buf = */ NULL;

	memset(state.reg, 0, sizeof(uint32_t) * 32);

	// Reset the whole SROM/MII state machines (not just their data).
	memset(&state.srom, 0, sizeof(state.srom));
	memset(&state.mii, 0, sizeof(state.mii));

	/*  Register values at reset, per HRM tables 3-27/41/47/49/51/57/59/61/63/66:  */
	state.reg[CSR_BUSMODE / 8] = 0xFE000000;  /* csr0  */
	state.reg[CSR_TXPOLL / 8] = 0xFFFFFFFF;  /* csr1  */
	state.reg[CSR_RXPOLL / 8] = 0xFFFFFFFF;  /* csr2  */
	state.reg[CSR_STATUS / 8] = 0xF0000000;  /* csr5  */
	state.reg[CSR_OPMODE / 8] = 0x32000040;  /* csr6  - includes MBO */
	state.reg[CSR_INTEN / 8] = 0xF3FE0000;  /* csr7  */
	state.reg[CSR_MISSED / 8] = 0xE0000000;  /* csr8  */
	state.reg[CSR_MIIROM / 8] = 0xFFF483FF;  /* csr9  */
	state.reg[CSR_GPT / 8] = 0xFFFE0000;  /* csr11 */
	state.reg[CSR_SIASTAT / 8] = 0x000000C6;  /* csr12 - link‑fail until autoneg */
	state.reg[CSR_SIACONN / 8] = 0xFFFF0000;  /* csr13 */
	state.reg[CSR_SIATXRX / 8] = 0xFFFFFFFF;  /* csr14 */
	state.reg[CSR_SIAGEN / 8] = 0x8FF00000;  /* csr15 */

	state.rx.cur_addr = state.tx.cur_addr = 0;

	/* SROM v3 build per Digital "21X4 Serial ROM Format" 4.05.
	 * v3 is the lowest version that defines extended-format info blocks
	 * and 21143 block types  */

	/* ID Block (bytes 0..17) — single-function format 5 */
	const uint16_t subsysVid = 0x1011;   /* DEC                 */
	const uint16_t subsysId  = 0x500B;   /* DE-500BA            */
	state.srom.data[ 0] = subsysVid & 0xff;
	state.srom.data[ 1] = (subsysVid >> 8) & 0xff;
	state.srom.data[ 2] = subsysId  & 0xff;
	state.srom.data[ 3] = (subsysId  >> 8) & 0xff;
	/* bytes 4..14 = 0  (CIS pointers, ID_Reserved1) — left zero */
	state.srom.data[15] = 0x00;     /* MiscHwOptions   - no PME/STSCHG       */
	/* byte 16 = ID_BLOCK_CRC (Appendix B: low byte of word 8), filled below */
	state.srom.data[17] = 0x00;     /* Func0_HwOptions - no BootROM          */

	/* Board info header (bytes 18..29) */
	state.srom.data[TULIP_ROM_SROM_FORMAT_VERION] = 3;
	state.srom.data[TULIP_ROM_CHIP_COUNT] = 1;

	/*  Set the MAC address:  */
	memcpy(state.srom.data + TULIP_ROM_IEEE_NETWORK_ADDRESS, state.mac, 6);

	leaf = 30;
	state.srom.data[TULIP_ROM_CHIPn_DEVICE_NUMBER(0)] = 0;
	state.srom.data[TULIP_ROM_CHIPn_INFO_LEAF_OFFSET(0)] = leaf & 255;
	state.srom.data[TULIP_ROM_CHIPn_INFO_LEAF_OFFSET(0) + 1] = leaf >> 8;

	/* Controller info leaf (offset 30) — 21143 7.5.1 */
	state.srom.data[leaf + 0] = 0x00;   /* Selected Conn Type LSB         */
	state.srom.data[leaf + 1] = 0x08;   /* MSB -> 0x0800 Powerup+Dynamic   */
	state.srom.data[leaf + 2] = 2;      /* Block Count = 2                */
	leaf += 3;

	/* 21143 SYM 100BaseTX-FDX (type 4, extended) 7.5.2.1.3
	 * The DE-500BA uses the chip's internal SYM scrambler/PCS for 100TX
	 * - there is NO external MII PHY on this board.  */
	state.srom.data[leaf++] = 0x80 | 8;       /* F=1, length=8                       */
	state.srom.data[leaf++] = TULIP_ROM_MB_21143_SYM;
	state.srom.data[leaf++] = TULIP_ROM_MB_MEDIA_100TX_FDX;  /* 0x05 = 100BaseTX FDX */
	state.srom.data[leaf++] = 0x00;           /* GPP Control LSB - no GPP needed     */
	state.srom.data[leaf++] = 0x00;           /* GPP Control MSB                     */
	state.srom.data[leaf++] = 0x00;           /* GPP Data LSB                        */
	state.srom.data[leaf++] = 0x00;           /* GPP Data MSB                        */
	state.srom.data[leaf++] = 0x61;           /* Command LSB: PS|PCS|SCR, no TTM     */
	state.srom.data[leaf++] = 0x80;           /* Command MSB: no media sense pin      */

	/* 21142/3 SIA 10BaseT (type 2, extended, EXT=0) 7.4.2.1.1 */
	state.srom.data[leaf++] = 0x80 | 6;       /* F=1, length=6 (no Media Specific Data) */
	state.srom.data[leaf++] = TULIP_ROM_MB_21142_SIA;
	state.srom.data[leaf++] = 0x00;           /* EXT=0, MediaCode=0 (10BaseT) */
	state.srom.data[leaf++] = 0x00;           /* GPP Control LSB          */
	state.srom.data[leaf++] = 0x00;           /* GPP Control MSB          */
	state.srom.data[leaf++] = 0x00;           /* GPP Data LSB             */
	state.srom.data[leaf++] = 0x00;           /* GPP Data MSB             */

	/* ID_BLOCK_CRC (Appendix B): 8-bit CRC, MSB-first, poly 0x06, init 0xFF.
	 * Walks bits of the first 9 words MSB-first, stopping at word 8 bit 7.
	 * Per the algorithm in the spec, the CRC result lands in the LOW byte
	 * of word 8 (= byte 16). Byte 17 is the high byte of word 8 and is
	 * INPUT to the walk (Func0_HwOptions in our layout, value 0). */
	{
		unsigned char crc8 = 0xFF;
		for (int word = 0; word < 9; word++) {
			uint16_t w = state.srom.data[word*2] | (state.srom.data[word*2 + 1] << 8);
			for (int bit = 15; bit >= 0; bit--) {
				if (word == 8 && bit == 7) break;
				unsigned char bv = ((w >> bit) & 1) ^ ((crc8 >> 7) & 1);
				crc8 <<= 1;
				if (bv) { crc8 ^= 0x06; crc8 |= 0x01; }
			}
		}
		state.srom.data[16] = crc8;
	}

	/*  MII Management decoder initial state:  */
	state.mii.state = MII_STATE_RESET;

	autoneg_pending = false;

	state.tx.suspend = false;

	// compute the CRC for the SROM data.  This code is from the
	// tu sample driver from HP in if_tu.c, which was apparently
	// derived from the 21140 Hardware Specification
	unsigned int  POLY = 0x04c11db6;
	unsigned int  FlippedCrc = 0;
	unsigned int  Crc = 0xffffffff;
	unsigned char i;
	unsigned char CurrentByte;
	unsigned char Bit;
	unsigned char Msb;
	unsigned char chksm_1;
	unsigned char chksm_2;

	for (i = 0; i < 126; i++)
	{
		CurrentByte = state.srom.data[i];

		for (Bit = 0; Bit < 8; Bit++)
		{
			Msb = (Crc >> 31) & 1;
			Crc <<= 1;

			if (Msb ^ (CurrentByte & 1))
			{
				Crc ^= POLY;
				Crc |= 1;
			}

			CurrentByte >>= 1;
		}
	}

	for (i = 0; i < 32; i++)
	{
		FlippedCrc <<= 1;
		Bit = Crc & 1;
		Crc >>= 1;
		FlippedCrc += Bit;
	}

	Crc = FlippedCrc ^ 0xffffffff;

	chksm_1 = Crc & 0xff;
	chksm_2 = Crc >> 8;

	state.srom.data[126] = chksm_1;
	state.srom.data[127] = chksm_2;
#if defined(DEBUG_NIC_SROM)
	printf("%%NIC-I-CKSUM: SROM checksum bytes are %02x, %02x\n",
		state.srom.data[126], state.srom.data[127]);
#endif

	// Make sure the host-side capture filter matches the reset state (SRM relies on this).
	SetupFilter();

	// Real reset deasserts the interrupt line.
	(void)do_pci_interrupt(0, false);
	state.irq_was_asserted = false;
}

static u32  nic_magic1 = 0xDEC21143;
static u32  nic_magic2 = 0x21143DEC;

/**
 * Save state to a Virtual Machine State file.
 **/
int CDEC21143::SaveState(FILE* f)
{
	long  ss = sizeof(state);
	int   res;

	// Keep a partially assembled TX frame: the next descriptor may be its
	// middle or last segment, so discarding this prefix corrupts the packet.
	if (state.tx.cur_buf_len < 0 || state.tx.cur_buf_len > nic_tx_capacity ||
		(state.tx.cur_buf_len != 0 && !state.tx.cur_buf) ||
		state.rx.current.len < 0 || state.rx.current.len > ETH_MAX_PACKET_CRC ||
		state.rx.current.used < 0 || state.rx.current.used > state.rx.current.len)
	{
		printf("%s: invalid NIC packet state!\n", devid_string);
		return -1;
	}

	SNIC_state saved = state;
	saved.tx.cur_buf = nullptr;
	saved.rx.cur_buf = nullptr;
	saved.rx.cur_buf_len = 0;
	saved.rx.cur_offset = 0;

	if ((res = CPCIDevice::SaveState(f)))
		return res;

	if (fwrite(&nic_magic1, sizeof(u32), 1, f) != 1 ||
		fwrite(&ss, sizeof(long), 1, f) != 1 ||
		fwrite(&saved, sizeof(saved), 1, f) != 1 ||
		(saved.tx.cur_buf_len != 0 &&
			fwrite(state.tx.cur_buf, 1, saved.tx.cur_buf_len, f) !=
				(size_t)saved.tx.cur_buf_len) ||
		fwrite(&nic_magic2, sizeof(u32), 1, f) != 1)
	{
		printf("%s: failed to write NIC state!\n", devid_string);
		return -1;
	}
	printf("%s: %li bytes saved.\n", devid_string, ss + saved.tx.cur_buf_len);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CDEC21143::RestoreState(FILE* f)
{
	long    ss;
	u32     m1;
	u32     m2;
	int     res;
	SNIC_state restored;
	u8      tx_data[nic_tx_capacity];

	if ((res = CPCIDevice::RestoreState(f)))
		return res;

	if (fread(&m1, sizeof(u32), 1, f) != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m1 != nic_magic1)
	{
		printf("%s: MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	if (fread(&ss, sizeof(long), 1, f) != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (ss != sizeof(state))
	{
		printf("%s: STRUCT SIZE does not match!\n", devid_string);
		return -1;
	}

	if (fread(&restored, sizeof(restored), 1, f) != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	// Never install addresses from a file, even if the pointer slots were
	// damaged. Validate lengths before reading or copying packet payloads.
	restored.tx.cur_buf = nullptr;
	restored.rx.cur_buf = nullptr;
	restored.rx.cur_buf_len = 0;
	restored.rx.cur_offset = 0;
	if (restored.tx.cur_buf_len < 0 || restored.tx.cur_buf_len > nic_tx_capacity ||
		restored.rx.current.len < 0 || restored.rx.current.len > ETH_MAX_PACKET_CRC ||
		restored.rx.current.used < 0 ||
		restored.rx.current.used > restored.rx.current.len)
	{
		printf("%s: invalid NIC packet state!\n", devid_string);
		return -1;
	}

	if ((restored.tx.cur_buf_len != 0 &&
		fread(tx_data, 1, restored.tx.cur_buf_len, f) !=
			(size_t)restored.tx.cur_buf_len) ||
		fread(&m2, sizeof(u32), 1, f) != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m2 != nic_magic2)
	{
		printf("%s: MAGIC 2 does not match!\n", devid_string);
		return -1;
	}

	// Commit only after the entire NIC section has been checked.
	restored.tx.cur_buf = state.tx.cur_buf;
	if (!restored.tx.cur_buf)
	{
		restored.tx.cur_buf = (unsigned char*)malloc(nic_tx_capacity);
		if (!restored.tx.cur_buf)
		{
			printf("%s: unable to allocate NIC TX buffer!\n", devid_string);
			return -1;
		}
	}
	if (restored.tx.cur_buf_len != 0)
		memcpy(restored.tx.cur_buf, tx_data, restored.tx.cur_buf_len);
	free(state.rx.cur_buf);
	state = restored;

	// These are host-side resources, not guest DMA state. Discard packets.
	if (rx_queue)
		rx_queue->flush();
	if (net_backend)
		SetupFilter();

	printf("%s: %li bytes restored.\n", devid_string, ss + state.tx.cur_buf_len);
	return 0;
}

// Helper functions

/* ----- IRQ recompute: set/clear INTx instantly (level-triggered) ----- */
void CDEC21143::update_irq()
{
	/* Rebuild NIS/AIS like the background loop does, but do it now. */
	const u32 csr5_before = state.reg[CSR_STATUS / 8];
	const u32 csr7 = state.reg[CSR_INTEN / 8];

	/* Summary recompute follows QEMU logic: only enabled events contribute. */
	u32 ie = csr5_before & csr7;
	u32 csr5 = csr5_before & ~(STATUS_NIS | STATUS_AIS);

	const u32 normal = (STATUS_TI | STATUS_TU | STATUS_RI | STATUS_TM | STATUS_ER);
	const u32 abnormal = (STATUS_LC | STATUS_GPPI | STATUS_SE | STATUS_LNF | STATUS_ETI |
		STATUS_RWT | STATUS_RPS | STATUS_RU | STATUS_UNF | STATUS_LNPANC |
		STATUS_TJT | STATUS_TPS);
	if (ie & normal)   csr5 |= STATUS_NIS;
	if (ie & abnormal) csr5 |= STATUS_AIS;

	state.reg[CSR_STATUS / 8] = csr5;

	const bool asserted = (csr5 & csr7 & (STATUS_AIS | STATUS_NIS)) != 0;
	if (asserted != state.irq_was_asserted) {
#if defined(DEBUG_NIC_IRQ)
		printf("21143: IRQ %s  CSR5=%08x CSR7=%08x pendN=%08x pendA=%08x\n",
			asserted ? "ASSERT" : "DEASSERT",
			csr5, csr7, (ie & normal), (ie & abnormal));
#endif
		if (do_pci_interrupt(0, asserted))
			state.irq_was_asserted = asserted;
	}
}


#endif //defined(HAVE_PCAP) || defined(HAVE_TAP_NET) || defined(HAVE_SLIRP)
