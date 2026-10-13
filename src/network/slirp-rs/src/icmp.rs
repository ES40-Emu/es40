//! External ICMP echo proxying.
//!
//! The userspace stack answers echo requests addressed to the virtual
//! gateway itself, but it has no way to reach the outside world with ICMP:
//! it only speaks TCP and UDP over host sockets. This module fills that gap
//! by replaying the guest's echo request as a real ping from the host and
//! synthesizing the matching echo reply back to the guest.
//!
//! Only external destinations are handled here; anything addressed to the
//! gateway or inside the virtual network is left to the stack, which already
//! answers it.

use std::net::{IpAddr, Ipv4Addr};

/// A parsed IPv4 ICMP echo request from the guest.
pub struct EchoRequest {
    pub src: Ipv4Addr,
    pub dst: Ipv4Addr,
    /// Ethernet source of the request, i.e. the guest's MAC.
    pub src_mac: [u8; 6],
    /// The whole ICMP message (header + payload).
    pub icmp: Vec<u8>,
}

/// Parse `frame` as an Ethernet/IPv4/ICMP echo request, if it is one.
pub fn parse_echo_request(frame: &[u8]) -> Option<EchoRequest> {
    // Ethernet + minimum IPv4 + minimum ICMP.
    if frame.len() < 14 + 20 + 8 {
        return None;
    }
    if frame[12..14] != [0x08, 0x00] {
        return None;
    }
    let ip = &frame[14..];
    let ihl = ((ip[0] & 0x0f) as usize) * 4;
    if ihl < 20 || ip.len() < ihl + 8 {
        return None;
    }
    if ip[9] != 1 {
        return None; // not ICMP
    }
    // Only whole datagrams: a fragment has no usable ICMP header here.
    let frag = u16::from_be_bytes([ip[6], ip[7]]);
    if frag & 0x1fff != 0 || frag & 0x2000 != 0 {
        return None;
    }
    let total = u16::from_be_bytes([ip[2], ip[3]]) as usize;
    if total < ihl + 8 || ip.len() < total {
        return None;
    }
    let icmp = &ip[ihl..total];
    if icmp[0] != 8 {
        return None; // not an echo request
    }

    let mut src = [0u8; 4];
    let mut dst = [0u8; 4];
    let mut src_mac = [0u8; 6];
    src.copy_from_slice(&ip[12..16]);
    dst.copy_from_slice(&ip[16..20]);
    src_mac.copy_from_slice(&frame[6..12]);

    Some(EchoRequest {
        src: Ipv4Addr::from(src),
        dst: Ipv4Addr::from(dst),
        src_mac,
        icmp: icmp.to_vec(),
    })
}

/// True for a destination that must go out through the host rather than be
/// answered locally.
pub fn is_external(dst: Ipv4Addr, gateway: Ipv4Addr, network: IpAddr, bits: u8) -> bool {
    if dst == gateway || dst.is_multicast() || dst.is_broadcast() || dst.is_unspecified() {
        return false;
    }
    let net = match network {
        IpAddr::V4(a) => a,
        IpAddr::V6(_) => return true,
    };
    let mask: u32 = if bits >= 32 {
        u32::MAX
    } else if bits == 0 {
        0
    } else {
        u32::MAX << (32 - bits)
    };
    (u32::from(dst) & mask) != (u32::from(net) & mask)
}

/// Build the Ethernet frame for an echo reply to `req`, as the host would
/// have answered it. `router_mac` is the MAC the reply carries on the wire
/// (the gateway's), since the destination IP is the off-link host.
pub fn build_echo_reply(req: &EchoRequest, router_mac: [u8; 6]) -> Vec<u8> {
    let mut icmp = req.icmp.clone();
    if icmp.len() >= 8 {
        icmp[0] = 0; // echo reply
        icmp[1] = 0; // code
    }
    let c = checksum(&icmp);
    if icmp.len() >= 4 {
        icmp[2] = (c >> 8) as u8;
        icmp[3] = c as u8;
    }

    let total = 20 + icmp.len();
    let mut ip = vec![0u8; 20];
    ip[0] = 0x45;
    ip[2..4].copy_from_slice(&(total as u16).to_be_bytes());
    ip[8] = 64; // TTL
    ip[9] = 1; // ICMP
    ip[12..16].copy_from_slice(&req.dst.octets()); // source: the pinged host
    ip[16..20].copy_from_slice(&req.src.octets()); // destination: the guest
    let cs = checksum(&ip);
    ip[10..12].copy_from_slice(&cs.to_be_bytes());

    let mut frame = Vec::with_capacity(14 + total);
    frame.extend_from_slice(&req.src_mac);
    frame.extend_from_slice(&router_mac);
    frame.extend_from_slice(&[0x08, 0x00]);
    frame.extend_from_slice(&ip);
    frame.extend_from_slice(&icmp);
    if frame.len() < 60 {
        frame.resize(60, 0); // Ethernet minimum, as the wire would pad
    }
    frame
}

/// The Internet checksum (RFC 1071) of `data`.
fn checksum(data: &[u8]) -> u16 {
    let mut sum: u32 = 0;
    let mut i = 0;
    while i + 1 < data.len() {
        sum += u32::from(u16::from_be_bytes([data[i], data[i + 1]]));
        i += 2;
    }
    if i < data.len() {
        sum += u32::from(data[i]) << 8;
    }
    while sum >> 16 != 0 {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    !(sum as u16)
}

/// Ask the host whether `dst` answers ICMP, by pinging it the way the host
/// itself would.
pub fn host_reachable(dst: Ipv4Addr) -> bool {
    #[cfg(windows)]
    {
        use std::ffi::c_void;

        #[link(name = "iphlpapi")]
        unsafe extern "system" {
            fn IcmpCreateFile() -> *mut c_void;
            fn IcmpCloseHandle(handle: *mut c_void) -> i32;
            fn IcmpSendEcho(
                handle: *mut c_void,
                destination: u32,
                request: *mut c_void,
                request_size: u16,
                options: *mut c_void,
                reply: *mut c_void,
                reply_size: u32,
                timeout_ms: u32,
            ) -> u32;
        }

        unsafe {
            let handle = IcmpCreateFile();
            // INVALID_HANDLE_VALUE is (HANDLE)-1.
            if handle.is_null() || handle as isize == -1 {
                return false;
            }
            let mut request = [0u8; 8];
            let mut reply = [0u8; 128];
            // IPAddr is a ULONG in network byte order.
            let addr = u32::from_ne_bytes(dst.octets());
            let n = IcmpSendEcho(
                handle,
                addr,
                request.as_mut_ptr() as *mut c_void,
                request.len() as u16,
                std::ptr::null_mut(),
                reply.as_mut_ptr() as *mut c_void,
                reply.len() as u32,
                1200,
            );
            IcmpCloseHandle(handle);
            n != 0
        }
    }
    #[cfg(unix)]
    {
        // A non-root process cannot make an ICMP socket portably, so use the
        // host's own ping (which carries the needed privileges/capabilities).
        std::process::Command::new("ping")
            .args(["-c", "1", "-n", &dst.to_string()])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
    }
    #[cfg(not(any(windows, unix)))]
    {
        let _ = dst;
        false
    }
}
