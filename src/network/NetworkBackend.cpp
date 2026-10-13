/* ES40 Emulator.
 * Copyright (C) 2026 by Jan-Benedict Glaw
 * Copyright (C) 2026 by Gary Palter
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

#if defined(HAVE_PCAP) || defined(HAVE_TAP_NET) || defined(HAVE_VMNET) || defined(HAVE_SLIRP)

#include "NetworkBackend.h"
#include "config/Configurator.h"

#if defined(HAVE_PCAP)
#include "NetworkPcap.h"
#endif

#if defined(HAVE_TAP_NET)
#include "NetworkTap.h"
#endif

#if defined(HAVE_VMNET)
#include "NetworkVmnet.h"
#endif

#if defined(HAVE_SLIRP)
#include "NetworkSlirp.h"
#endif


CNetworkBackend* create_network_backend(const char *devid_string,
                                        CConfigurator *cfg)
{
	char *type = cfg->get_text_value("type");

	// pcap is the default when no type is specified.
	if (!type || !strcasecmp(type, "pcap")) {
#if defined(HAVE_PCAP)
		return new CNetworkPcap();
#else
		printf("%s: \"pcap\" support not compiled in.\n", devid_string);
		return nullptr;
#endif
	}

	if (!strcasecmp(type, "tap")) {
#if defined(HAVE_TAP_NET)
		return new CNetworkTap();
#else
		printf("%s: \"tap\" support not compiled in.\n", devid_string);
		return nullptr;
#endif
	}

	if (!strcasecmp(type, "vmnet")) {
#if defined(HAVE_VMNET)
		return new CNetworkVmnet();
#else
		printf("%s: \"vmnet\" support not compiled in.\n", devid_string);
		return nullptr;
#endif
	}

	if (!strcasecmp(type, "slirp")) {
#if defined(HAVE_SLIRP)
		return new CNetworkSlirp();
#else
		printf("%s: \"slirp\" support not compiled in.\n", devid_string);
		return nullptr;
#endif
	}

	printf("%s: Unknown network backend type \"%s\"; expected \"pcap\", "
	       "\"tap\", \"vmnet\", or \"slirp\".\n", devid_string, type);

	return nullptr;

}

#endif /* defined(HAVE_PCAP) || defined(HAVE_TAP_NET) || defined(HAVE_VMNET) || defined(HAVE_SLIRP) */
