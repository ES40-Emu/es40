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

#include "../StdAfx.h"

#if defined(HAVE_SLIRP)

#include "NetworkSlirp.h"
#include "config/Configurator.h"

#include <string.h>

/*
 * C ABI exported by the Rust static library (src/network/slirp-rs). The
 * layout of Es40SlirpConfig must match the #[repr(C)] struct in lib.rs.
 */
extern "C" {
	struct Es40SlirpConfig {
		unsigned char mac[6];
		const char      *network;    /* "10.0.2.0/24", or NULL for the default */
		const char      *gateway;    /* "10.0.2.2", or NULL */
		const char      *dhcp_start; /* or NULL to derive from the network */
		const char      *dhcp_end;   /* or NULL to derive from the network */
		const char      *dns;        /* comma-separated, or NULL for the host's */
		unsigned int     lease_secs; /* 0 for the library default */
	};

	int  es40_slirp_available(void);
	void *es40_slirp_new(const struct Es40SlirpConfig *cfg);
	void es40_slirp_free(void *ctx);
	int  es40_slirp_input(void *ctx, const unsigned char *data, int len);
	int  es40_slirp_output(void *ctx, unsigned char *buf, int cap);
	int  es40_slirp_wait_fd(void *ctx);
}

CNetworkSlirp::CNetworkSlirp():
	ctx(nullptr),
	devid_for_log(nullptr) {

	memset(rx_buf, 0, sizeof(rx_buf));
}

CNetworkSlirp::~CNetworkSlirp() { close(); }

bool CNetworkSlirp::init(const char *devid_string, CConfigurator *cfg)
{
	devid_for_log = devid_string;

	if (es40_slirp_available() == 0) {
		printf("%s: slirp support not available in this build.\n", devid_string);
		return false;
	}

	struct Es40SlirpConfig sc;
	memset(&sc, 0, sizeof(sc));

	/* The guest MAC is assigned by the NIC after the backend is created;
	 * the Rust side does not need it, so leave it zeroed. */
	sc.network = cfg->get_text_value("network");
	sc.gateway = cfg->get_text_value("gateway");
	sc.dhcp_start = cfg->get_text_value("dhcp_start");
	sc.dhcp_end = cfg->get_text_value("dhcp_end");
	sc.dns = cfg->get_text_value("dns");
	sc.lease_secs = (unsigned int)cfg->get_num_value("dhcp_lease", false, 3600);

	ctx = es40_slirp_new(&sc);
	if (!ctx) {
		printf("%s: could not start the slirp backend (see messages above).\n",
		       devid_string);
		return false;
	}

	const char *network = sc.network ? sc.network : "10.0.2.0/24";
	const char *gateway = sc.gateway ? sc.gateway : "10.0.2.2";
	printf("%s: slirp network %s, gateway %s\n", devid_string, network, gateway);

	return true;
}

int CNetworkSlirp::send(const u8 *data, int len)
{
	if (!ctx)
		return -1;
	return es40_slirp_input(ctx, data, len) == 0 ? 0 : -1;
}

int CNetworkSlirp::receive(const u8 **data, int *len)
{
	if (!ctx)
		return -1;
	int n = es40_slirp_output(ctx, rx_buf, (int)sizeof(rx_buf));
	if (n > 0) {
		*data = rx_buf;
		*len = n;
		return 1;
	}
	return n == 0 ? 0 : -1;
}

void CNetworkSlirp::set_filter(u8 mac_list[][6], int num_macs,
                               bool promiscuous, bool receive_all,
                               bool pass_multicast, bool inverse)
{
	/* The backend sits on a private LAN with a single guest (and the DHCP
	 * server), so there is no shared medium to filter: everything the
	 * virtual switch hands us already belongs to this NIC. */
	(void) mac_list;
	(void) num_macs;
	(void) promiscuous;
	(void) receive_all;
	(void) pass_multicast;
	(void) inverse;
}

void CNetworkSlirp::close()
{
	if (ctx) {
		es40_slirp_free(ctx);
		ctx = nullptr;
	}
}

int CNetworkSlirp::get_wait_fd()
{
	if (!ctx)
		return -1;
	return es40_slirp_wait_fd(ctx);
}

#endif /* defined(HAVE_SLIRP)  */
