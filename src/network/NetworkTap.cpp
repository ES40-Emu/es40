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

#if defined(HAVE_TAP_NET)

#include "NetworkTap.h"
#include "config/Configurator.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/if_tun.h>
#include <linux/sockios.h>

// Bridge ioctls may not be defined in every libc's headers.
#ifndef SIOCBRADDIF
#define SIOCBRADDIF 0x89a2
#endif
#ifndef SIOCBRADDBR
#define SIOCBRADDBR 0x89a0
#endif

#elif defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/sockio.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

CNetworkTap::CNetworkTap():
	tap_fd(-1),
	devid_for_log(nullptr) {

	memset(ifname, 0, sizeof(ifname));
}

CNetworkTap::~CNetworkTap() { close(); }

// ---------------------------------------------------------------------
// Device open.
//
// Linux: a single shared clone device (/dev/net/tun) is opened, and the
// TUNSETIFF ioctl both selects (attaches to) an existing persistent tap
// interface and creates a new one if it doesn't exist yet - the kernel
// decides which based on whether the name is already taken and whether
// the caller has CAP_NET_ADMIN.
//
// FreeBSD/NetBSD: there is no separate clone-and-bind step. A TAP device
// *is* a character device node, normally /dev/tapN, and using it is just
// open("/dev/tapN", O_RDWR) - exactly like any other file. If the
// interface doesn't exist yet (ENOENT/ENXIO), it can be instantiated with
// the generic BSD "interface cloning" ioctl SIOCIFCREATE, which is the
// same mechanism "ifconfig tapN create" uses; afterwards the device node
// can be opened normally.
// ---------------------------------------------------------------------
bool CNetworkTap::tap_open(const char *devid_string, const char *name) {
#if defined(__linux__)
	struct ifreq ifr;

	tap_fd = open("/dev/net/tun", O_RDWR);
	if (tap_fd < 0) {
		const int error = errno;
		printf("%s: Cannot open /dev/net/tun: %s\n", devid_string, strerror(error));
		if (error == ENOENT || error == ENODEV || error == ENXIO)
			printf("%s: Check host TUN/TAP support and availability of /dev/net/tun.\n",
			       devid_string);
		else if (error == EACCES || error == EPERM)
			printf("%s: Check read/write permissions for /dev/net/tun and host access policy.\n",
			       devid_string);
		return false;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	if (name && name[0])
		strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

	if (ioctl(tap_fd, TUNSETIFF, &ifr) < 0) {
		const int error = errno;
		printf("%s: Cannot create or attach TAP interface %s (TUNSETIFF): %s\n",
		       devid_string, name && name[0] ? name : "(automatic)", strerror(error));
		if (error == EACCES || error == EPERM) {
			printf("%s: Check owner/group permissions for an existing TAP; "
			       "creating an interface requires CAP_NET_ADMIN.\n", devid_string);
			if (!name || !name[0])
				printf("%s: Set adapter to an existing TAP interface to avoid automatic creation.\n",
				       devid_string);
		} else if (error == EBUSY)
			printf("%s: The TAP interface may already be attached to another process.\n",
			       devid_string);
		else if (error == EINVAL)
			printf("%s: Check the interface name, TAP device type and compatible flags.\n",
			       devid_string);
		::close(tap_fd);
		tap_fd = -1;
		return false;
	}

	return true;

#elif defined(__FreeBSD__) || defined(__NetBSD__)
	tap_fd = open(name, O_RDWR);
	if (tap_fd < 0) {
		const int error = errno;
		printf("%s: Cannot open %s: %s\n", devid_string,
		       name && name[0] ? name : "(not configured)", strerror(error));
		if (!name || !name[0])
			printf("%s: Set adapter to a TAP device path, such as /dev/tap0.\n",
			       devid_string);
		else if (error == ENOENT || error == ENODEV || error == ENXIO)
			printf("%s: Check the TAP device path and host TAP support.\n", devid_string);
		else if (error == EACCES || error == EPERM)
			printf("%s: Check TAP device read/write permissions and host TAP access policy.\n",
			       devid_string);
		else if (error == EBUSY)
			printf("%s: The TAP device may already be open in another process.\n", devid_string);
		return false;
	}

	return true;
#else
#error "HAVE_TAP_NET defined on an unsupported platform"
#endif
}

bool CNetworkTap::init(const char *devid_string, CConfigurator *cfg)
{
	devid_for_log = devid_string;
	char *adapter = cfg->get_text_value("adapter");

	if (!tap_open(devid_string, adapter))
		return false;

	int flags = fcntl(tap_fd, F_GETFL, 0);
	if (flags < 0 || fcntl(tap_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		const int error = errno;
		printf("%s: Cannot set TAP fd %d non-blocking: %s\n", devid_string, tap_fd, strerror(error));
		close();
		return false;
	}

	return true;
}

int CNetworkTap::send(const u8 *data, int len) {
	if (tap_fd < 0)
		return -1;
	ssize_t n = write(tap_fd, data, len);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0; /* Host-side queue full; drop the packet like a real wire would.  */
		printf("TAP: Error sending packet: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

int CNetworkTap::receive(const u8 **data, int *len)
{
	if (tap_fd < 0)
		return -1;
	ssize_t n = read(tap_fd, rx_buf, sizeof(rx_buf));

	if (n > 0) {
		*data = rx_buf;
		*len = (int)n;
		return 1;
	}

	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0; // no packet available
	if (n == 0)
		return 0;

	return -1;
}

void CNetworkTap::set_filter(u8 mac_list[][6], int num_macs,
                              bool promiscuous, bool receive_all,
                              bool pass_multicast, bool inverse)
{
	// TAP devices only ever deliver frames the kernel has already routed to
	// this interface, so there's no host-side capture filter to program
	// (unlike libpcap on a shared physical link). The emulated NIC's own
	// setup-frame handling still does perfect/hash filtering in software
	// for anything delivered here.
	(void) mac_list;
	(void) num_macs;
	(void) promiscuous;
	(void) receive_all;
	(void) pass_multicast;
	(void) inverse;

	return;
}

void CNetworkTap::close()
{
	if (tap_fd >= 0) {
		::close(tap_fd);
		tap_fd = -1;
	}

	return;
}

#endif /* defined(HAVE_TAP_NET)  */
