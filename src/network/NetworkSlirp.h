/* ES40 Emulator.
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
 * Userspace (slirp) network backend.
 *
 * This backend needs no host privileges and no pre-created interface: the
 * emulated NIC is placed on a private virtual LAN whose gateway is a
 * user-space NAT implemented in Rust (see src/network/slirp-rs). The guest
 * gets its address over DHCP from the backend, and its TCP/UDP/ICMP traffic
 * is translated to ordinary host sockets on the process's behalf.
 *
 * The heavy lifting lives in a Rust static library linked into es40; this
 * class is only the thin C++ side of the C ABI exposed by that library.
 **/
#ifndef INCLUDED_NETWORK_SLIRP_H_
#define INCLUDED_NETWORK_SLIRP_H_

#include "NetworkBackend.h"

#if defined(HAVE_SLIRP)

/** Largest frame we will hand to or accept from the Rust backend. */
#define ES40_SLIRP_BUF 65536

class CNetworkSlirp: public CNetworkBackend {
public:
	CNetworkSlirp();
	virtual ~CNetworkSlirp();

	virtual bool init(const char *devid_string, CConfigurator *cfg);
	virtual int  send(const u8 *data, int len);
	virtual int  receive(const u8 **data, int *len);
	virtual void set_filter(u8 mac_list[][6], int num_macs,
	                        bool promiscuous, bool receive_all,
	                        bool pass_multicast, bool inverse);
	virtual void close();
	virtual int  get_wait_fd();

private:
	void *ctx; /* opaque Es40Slirp* owned by the Rust backend */
	unsigned char rx_buf[ES40_SLIRP_BUF];

	const char *devid_for_log;
};

#endif /* defined(HAVE_SLIRP)  */

#endif /* !defined(INCLUDED_NETWORK_SLIRP_H_)  */
