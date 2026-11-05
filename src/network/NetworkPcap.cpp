/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2020 Tomáš Glozar
 * Copyright (C) 2026 by Jan-Benedict Glaw
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

#include "../StdAfx.h"

#if defined(HAVE_PCAP)

#include "NetworkPcap.h"
#include "config/Configurator.h"
#if defined(_WIN32)
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#ifdef _WIN32
/* Pointers to the real functions, resolved at runtime from wpcap.dll. */
static const char *(*f_pcap_lib_version)(void);
static int (*f_pcap_findalldevs)(pcap_if_t **, char *);
static void (*f_pcap_freealldevs)(void *);
static pcap_t *(*f_pcap_open_live)(const char *, int, int, int, char *);
static int (*f_pcap_compile)(void *, void *, const char *, int, bpf_u_int32);
static int (*f_pcap_setfilter)(void *, void *);
static const unsigned char *(*f_pcap_next)(void *, void *);
static int (*f_pcap_sendpacket)(void *, const unsigned char *, int);
static void (*f_pcap_close)(void *);
static int (*f_pcap_setnonblock)(void *, int, char *);
static int (*f_pcap_set_immediate_mode)(void *, int);
static int (*f_pcap_set_promisc)(void *, int);
static int (*f_pcap_set_snaplen)(void *, int);
static int (*f_pcap_dispatch)(void *, int, pcap_handler callback,
                              unsigned char *user);
static void *(*f_pcap_create)(const char *, char *);
static int (*f_pcap_activate)(void *);
static char *(*f_pcap_geterr)(void *);
static pcap_t *(*f_pcap_open)(const char *source, int snaplen, int flags,
                              int read_timeout, struct pcap_rmtauth *auth,
                              char *errbuf);
static int (*f_pcap_next_ex)(pcap_t *, struct pcap_pkthdr **,
                             const unsigned char **);
static int (*f_pcap_datalink)(pcap_t *);
static const char *(*f_pcap_datalink_val_to_name)(int);
static void (*f_pcap_freecode)(void *);
static bool wpcap_loaded = false;
#endif

CNetworkPcap::CNetworkPcap() : fp(nullptr), opened(false), linktype(-1),
                               filter_active(false),
                               nonether_filter_warned(false) {
	memset(&fcode, 0, sizeof(fcode));

#ifdef _WIN32
	if (!wpcap_loaded) {
		char npcap_dir[512];
		GetSystemDirectoryA(npcap_dir, 480);
		strcat(npcap_dir, "\\Npcap");
		SetDllDirectoryA(npcap_dir);
		auto libhandle = LoadLibraryA("wpcap.dll");
		SetDllDirectoryA(NULL); /* reset the DLL search path */
		if (!libhandle) {
			FAILURE(Runtime, "Failed to load wpcap.dll");
		}

		f_pcap_lib_version = (const char *(*)())GetProcAddress(libhandle, "pcap_lib_version");
		f_pcap_findalldevs = (int (*)(pcap_if_t **, char *))GetProcAddress(libhandle, "pcap_findalldevs");
		f_pcap_freealldevs = (void (*)(void *))GetProcAddress(libhandle, "pcap_freealldevs");
		f_pcap_open_live   = (pcap_t *(*)(const char *, int, int, int, char *))GetProcAddress(libhandle, "pcap_open_live");
		f_pcap_open        = (pcap_t *(*)(const char *, int, int, int, pcap_rmtauth *, char *))GetProcAddress(libhandle, "pcap_open");
		f_pcap_compile     = (int (*)(void *, void *, const char *, int, bpf_u_int32))GetProcAddress(libhandle, "pcap_compile");
		f_pcap_setfilter   = (int (*)(void *, void *))GetProcAddress(libhandle, "pcap_setfilter");
		f_pcap_next        = (const unsigned char *(*)(void *, void *))GetProcAddress(libhandle, "pcap_next");
		f_pcap_sendpacket  = (int (*)(void *, const unsigned char *, int))GetProcAddress(libhandle, "pcap_sendpacket");
		f_pcap_close       = (void (*)(void *))GetProcAddress(libhandle, "pcap_close");
		f_pcap_setnonblock = (int (*)(void *, int, char *))GetProcAddress(libhandle, "pcap_setnonblock");
		f_pcap_set_immediate_mode = (int (*)(void *, int))GetProcAddress(libhandle, "pcap_set_immediate_mode");
		f_pcap_set_promisc = (int (*)(void *, int))GetProcAddress(libhandle, "pcap_set_promisc");
		f_pcap_set_snaplen = (int (*)(void *, int))GetProcAddress(libhandle, "pcap_set_snaplen");
		f_pcap_dispatch    = (int (*)(void *, int, pcap_handler callback, unsigned char *user))GetProcAddress(libhandle, "pcap_dispatch");
		f_pcap_create      = (void *(*)(const char *, char *))GetProcAddress(libhandle, "pcap_create");
		f_pcap_geterr      = (char *(*)(void *))GetProcAddress(libhandle, "pcap_geterr");
		f_pcap_next_ex     = (int (*)(pcap *, pcap_pkthdr **, const unsigned char **))GetProcAddress(libhandle, "pcap_next_ex");
		f_pcap_datalink    = (int (*)(pcap *))GetProcAddress(libhandle, "pcap_datalink");
		f_pcap_datalink_val_to_name = (const char *(*)(int))GetProcAddress(libhandle, "pcap_datalink_val_to_name");
		f_pcap_freecode    = (void (*)(void *))GetProcAddress(libhandle, "pcap_freecode");

#define pcap_findalldevs f_pcap_findalldevs
#define pcap_open f_pcap_open
#define pcap_sendpacket f_pcap_sendpacket
#define pcap_close f_pcap_close
#define pcap_setnonblock f_pcap_setnonblock
#define pcap_setfilter f_pcap_setfilter
#define pcap_compile f_pcap_compile
#define pcap_geterr f_pcap_geterr
#define pcap_next_ex f_pcap_next_ex
#define pcap_datalink f_pcap_datalink
#define pcap_datalink_val_to_name f_pcap_datalink_val_to_name
#define pcap_freecode f_pcap_freecode

#define PCAP_ERROR -1
#define PCAP_OPENFLAG_PROMISCUOUS 0x00000001
#define PCAP_OPENFLAG_NOCAPTURE_LOCAL 0x00000008
#define DLT_EN10MB 1
#define PCAP_IF_LOOPBACK 0x00000001

		wpcap_loaded = true;
	}
#endif
}

CNetworkPcap::~CNetworkPcap()
{
	close();
}

bool CNetworkPcap::init(const char *devid_string, CConfigurator *cfg)
{
	pcap_if_t *alldevs;
	pcap_if_t *d;
	unsigned int inum;
	unsigned int i = 0;
	char errbuf[PCAP_ERRBUF_SIZE];

	char *adapter = cfg->get_text_value("adapter");
	/* The configuration editor writes adapter = "list" to mean "ask". */
	if (adapter && !strcasecmp(adapter, "list"))
		adapter = NULL;
	bool  from_menu = (adapter == NULL);
	if (!adapter) {
		if (!isatty(fileno(stdin))) {
			printf("%s: No adapter configured and no terminal to choose one on "
			       "(started from a GUI or as a service?).\n"
			       "%s: Set adapter = \"<host interface>\" for this NIC, or use "
			       "type = \"slirp\", which needs neither.\n",
			       devid_string, devid_string);
			return false;
		}
		printf("\n%s: Choose a network adapter to connect to:\n", devid_string);
		if (pcap_findalldevs(&alldevs, errbuf) == -1) {
			printf("%s: Error in pcap_findalldevs: %s\n", devid_string, errbuf);
			return false;
		}

		for (d = alldevs; d; d = d->next) {
			printf("%d. %s\n    ", ++i, d->name);
			if (d->description)
				printf(" (%s)", d->description);
			else
				printf(" (No description available)");
			/* Loopback and other IP-only sources carry no ethernet header. */
			if (d->flags & PCAP_IF_LOOPBACK)
				printf(" [loopback - no ethernet frames]");
			printf("\n");
		}

		if (i == 0) {
			printf("%s: No network interfaces found\n", devid_string);
			return false;
		}

		if (i == 1)
			inum = 1;
		else {
			for (;;) {
				char input_buf[64];
				int  parsed;

				printf("%%NIC-Q-NICNO: Enter the interface number (1-%d): ", i);
				fflush(stdout);

				if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) {
					printf("%s: Unexpected end of input while selecting network "
					       "interface\n", devid_string);
					return false;
				}

				if (sscanf(input_buf, "%d", &parsed) != 1
				    || parsed < 1
				    || (unsigned int)parsed > i) {
					printf("%%NIC-W-BADSEL: Invalid selection. Please enter a number "
					       "between 1 and %d.\n", i);
					continue;
				}

				inum = (unsigned int)parsed;
				break;
			}
		}

		for (d = alldevs, i = 0; i < inum - 1; d = d->next, i++)
			;

		adapter = d->name;
	}

#if defined(WIN32)
	// Opening with pcap_open on Windows allows specification of
	// PCAP_OPENFLAG_NOCAPTURE_LOCAL, which stops the pcap device from seeing
	// it's own transmitted packets.
	//
	// This is important because:
	//    1. Real ethernet cards don't reflect packets except while in
	//       loopback mode(s).
	//    2. Reflecting all packets increases inbound packet processing and
	//       host load.
	//    3. DECNET Phase IV will think a reflected packet is from another
	//       node that has the same DECNET Phase IV address
	//       (AA-xx-xx-xx-xx-xx), and will panic on startup and abort.
	//    4. Libpcap doesn't reflect packets, and we want winpcap/libpcap
	//       processing to be identical.
	// Loopback packets are handled via direct entry in the receive queue.
	if ((fp = (pcap_t *)pcap_open(
	         adapter, 65536 /*snaplen: capture entire packets */,
	         PCAP_OPENFLAG_PROMISCUOUS |
	             PCAP_OPENFLAG_NOCAPTURE_LOCAL /*promiscuous */,
	         10 /*packet buffer timeout: [ms] */, 0 /* auth structure */,
	         errbuf)) == NULL) // connect to pcap...
#else
	if ((fp = pcap_open_live(adapter, 65536 /*snaplen: capture entire packets */,
	                         1 /*promiscuous */,
	                         10 /*packet buffer timeout: [ms] */,
	                         errbuf)) == NULL) // connect to pcap...
#endif
	{
		printf("%s: Error opening adapter %s:\n %s\n", devid_string, adapter, errbuf);
#if !defined(WIN32)
		if (strstr(errbuf, "permission") || strstr(errbuf, "Permission"))
			printf("%s: Capturing needs raw-socket rights. Either run the emulator "
			       "as root, or grant them once with:\n"
			       "%s:   sudo setcap cap_net_raw,cap_net_admin+eip $(command -v es40)\n"
			       "%s: (repeat after reinstalling the package), or use type = "
			       "\"tap\" or \"slirp\".\n", devid_string, devid_string, devid_string);
#endif
		return false;
	}

	if (pcap_setnonblock(fp, 1, errbuf) == PCAP_ERROR) {
		printf("%s: Error setting adapter %s non-blocking:\n %s\n", devid_string, adapter, errbuf);
		pcap_close(fp);
		fp = nullptr;
		return false;
	}

	/* Loopback, VPN and other IP-only capture sources carry no ethernet
	   header, so no ether filter can be compiled. */
	linktype = pcap_datalink(fp);
	if (linktype != DLT_EN10MB) {
		const char *lt = pcap_datalink_val_to_name(linktype);
		printf("%s: WARNING: adapter %s is not an ethernet capture source "
		       "(link type %s [%d], not EN10MB).\n", devid_string,
		       adapter, lt ? lt : "unknown", linktype);
		printf("%s: WARNING: captured packets have no ethernet header and "
		       "receive filtering is disabled. Testing use only; pick a "
		       "physical or bridged ethernet adapter for real networking."
		       "\n", devid_string);

		/* Warn on non-ethernet adapter */
		if (from_menu) {
			for (;;) {
				char answer[16];
				printf("%%NIC-Q-NONETH: Use this adapter anyway? (y/n): ");
				fflush(stdout);
				if (fgets(answer, sizeof(answer), stdin) == NULL) {
					printf("%s: Unexpected end of input; not using %s\n",
						devid_string, adapter);
					pcap_close(fp);
					fp = nullptr;
					return false;
				}
				if (answer[0] == 'y' || answer[0] == 'Y')
					break;
				if (answer[0] == 'n' || answer[0] == 'N') {
					printf("%s: Not using %s\n", devid_string, adapter);
					pcap_close(fp);
					fp = nullptr;
					return false;
				}
			}
		}
	}

	opened = true;
	printf("%s: Using pcap adapter %s\n", devid_string, adapter);
	return true;
}

int CNetworkPcap::send(const u8 *data, int len)
{
	if (!fp)
		return -1;
	if (pcap_sendpacket(fp, data, len)) {
		printf("Error sending the packet: %s\n", pcap_geterr(fp));
		return -1;
	}
	return 0;
}

int CNetworkPcap::receive(const u8 **data, int *len)
{
	if (!fp)
		return -1;
	struct pcap_pkthdr   *packet_header;
	const unsigned char  *packet_data = NULL;
	int res = pcap_next_ex(fp, &packet_header, &packet_data);
	if (res > 0) {
		*data = packet_data;
		*len = packet_header->caplen;
		return 1;
	}
	if (res == 0)
		return 0; /* timeout, no packet  */
	return -1;        /* error  */
}

void CNetworkPcap::set_filter(u8 mac_list[][6], int num_macs,
                               bool promiscuous, bool receive_all,
                               bool pass_multicast, bool inverse)
{
	if (!fp)
		return;

	/* Packet filtering only works on ethernet... */
	if (linktype != DLT_EN10MB) {
		if (!nonether_filter_warned) {
			printf("Packet filtering disabled: link type %d is not ethernet.\n",
				linktype);
			nonether_filter_warned = true;
		}
		return;
	}

	char mac_txt[16][20];
	char filter[1000];
	int  numUnique = 0;
	int  unique[16];
	int  count = num_macs > 16 ? 16 : num_macs;

	for (int i = 0; i < count; i++) {
		bool zero = (mac_list[i][0] | mac_list[i][1] | mac_list[i][2] |
		             mac_list[i][3] | mac_list[i][4] | mac_list[i][5]) == 0;
		if (zero)
			continue;
		sprintf(mac_txt[i], "%02x:%02x:%02x:%02x:%02x:%02x",
		        mac_list[i][0], mac_list[i][1], mac_list[i][2],
		        mac_list[i][3], mac_list[i][4], mac_list[i][5]);

		bool u = true;
		for (int j = 0; j < numUnique; j++) {
			if (memcmp(mac_list[i], mac_list[unique[j]], 6) == 0) {
				u = false;
				break;
			}
		}
		if (u) {
			unique[numUnique] = i;
			numUnique++;
		}
	}

	filter[0] = '\0';
	strcpy(filter, "ether broadcast");

	if (!(promiscuous || receive_all)) {
		char list[800];

		if (pass_multicast)
			strcat(filter, " or ether multicast");

		list[0] = '\0';
		if (numUnique == 0) {
			/* No addresses learned yet; there's no "our own MAC"
			   to fall back to at this layer (the caller owns
			   that), so just leave the list empty and rely on the
			   broadcast/multicast terms above.  */
		} else {
			for (int i = 0; i < numUnique; i++) {
				strcat(list, (i == 0) ? "ether dst " : " or ether dst ");
				strcat(list, mac_txt[unique[i]]);
			}
		}

		if (list[0]) {
			if (inverse) {
				strcat(filter, " or (not (");
				strcat(filter, list);
				strcat(filter, "))");
			} else {
				strcat(filter, " or (");
				strcat(filter, list);
				strcat(filter, ")");
			}
		}
	} /* else PR/RA: leave filter as match-all broadcasts + everything */

	/* In promiscuous/receive-all we still compile an empty/very permissive filter. */
	if (promiscuous || receive_all) {
		/* Let the OS accept everything: empty expr compiles to "match all". */
		filter[0] = '\0';
	}

	if (filter_active) {
		pcap_freecode(&fcode);
		filter_active = false;
	}

	if (pcap_compile(fp, &fcode, filter, 1, 0xffffffff) < 0) {
		printf("Unable to compile the packet filter (%s): %s\n", filter,
		       pcap_geterr(fp));
		return;
	}

	filter_active = true;

	if (pcap_setfilter(fp, &fcode) < 0)
		printf("Error setting the filter: %s\n", pcap_geterr(fp));

	return;
}

void CNetworkPcap::close()
{
	if (fp && filter_active) {
		pcap_freecode(&fcode);
		filter_active = false;
	}
	if (fp) {
		pcap_close(fp);
		fp = nullptr;
	}
	opened = false;
}

#endif /* HAVE_PCAP  */
