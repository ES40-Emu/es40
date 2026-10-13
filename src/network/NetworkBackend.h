/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2020 Tomáš Glozar
 * Copyright (C) 2026 by Jan-Benedict Glaw
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
 * Abstract interface separating the emulated DEC 21143 NIC from the host
 * networking method used to get packets on and off the wire.
 *
 * Implementations:
 *  - CNetworkPcap  : libpcap / npcap (Windows, and any Unix with libpcap).
 *  - CNetworkTap   : TAP device (Linux, FreeBSD, NetBSD).
 *  - CNetworkSlirp : Userspace NAT ("slirp") implemented in Rust; no host
 *                    privileges or pre-created interface required.
 **/
#if !defined(INCLUDED_NETWORK_BACKEND_H_)
#define INCLUDED_NETWORK_BACKEND_H_

#include "../StdAfx.h"

class CConfigurator;

/**
 * \brief Abstract network backend interface.
 **/
class CNetworkBackend {
public:
	virtual ~CNetworkBackend() {}

	/**
	 * Initialize the backend from configuration. devid_string is used only
	 * for log/error message prefixes. Returns true on success; on failure it
	 * should have already printed a descriptive message.
	 **/
	virtual bool init(const char *devid_string, CConfigurator *cfg) = 0;

	/**
	 * Send a packet to the host network. Returns 0 on success, -1 on error.
	 **/
	virtual int send(const u8 *data, int len) = 0;

	/**
	 * Receive a packet from the host network (non-blocking).
	 * On success, sets *data and *len and returns 1. The buffer pointed to
	 * by *data is only valid until the next call to receive() and must not
	 * be freed by the caller.
	 * If no packet is currently available, returns 0.
	 * On error, returns -1.
	 **/
	virtual int receive(const u8 **data, int *len) = 0;

	/**
	 * Set up MAC-based receive filtering, mirroring the emulated NIC's CSR6
	 * bits:
	 *  - mac_list/num_macs: up to 16 perfect-filter MAC addresses (entries
	 *    that are all-zero should be ignored).
	 *  - promiscuous:     CSR6.PR - accept everything, ignore all other
	 *                      arguments.
	 *  - receive_all:     CSR6.RA - accept everything (like promiscuous, but
	 *                      distinguished for backends that log the reason).
	 *  - pass_multicast:  CSR6.PM - also accept all multicast traffic.
	 *  - inverse:         CSR6.IF - invert the perfect-filter address match.
	 *
	 * Backends that can't express host-side filtering at this granularity
	 * (e.g. a TAP device, which only ever sees frames the kernel already
	 * routed to it) are free to make this a no-op.
	 **/
	virtual void set_filter(u8 mac_list[][6], int num_macs, bool promiscuous,
	                        bool receive_all, bool pass_multicast,
	                        bool inverse) = 0;

	/**
	 * A descriptor that becomes readable when a packet is waiting, or -1
	 * if the backend has no pollable handle.
	 **/
	virtual int get_wait_fd() { return -1; }

	/**
	 * Close the backend and release any resources (file descriptors,
	 * library handles, created interfaces, ...).
	 **/
	virtual void close() = 0;
};

/**
 * Factory: create the network backend selected by the "type" configuration
 * value ("pcap", the default, or "tap"). Returns nullptr (after printing a
 * diagnostic) if the requested backend isn't available in this build or on
 * this platform.
 **/
CNetworkBackend *create_network_backend(const char *devid_string, CConfigurator *cfg);

#endif /* !defined(INCLUDED_NETWORK_BACKEND_H_)  */
