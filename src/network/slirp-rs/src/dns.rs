//! Host DNS server discovery for the slirp backend.
//!
//! The guest gets DNS servers from the built-in DHCP server. When the user
//! does not configure any, we try the host's own resolvers so that names that
//! resolve on the host also resolve in the guest. The queries themselves are
//! ordinary NATed UDP datagrams, so the guest reaches whatever address we
//! hand it exactly as the host would.

use std::net::Ipv4Addr;

/// Public fallbacks used when neither the user nor the host offers a resolver.
const FALLBACK: [Ipv4Addr; 2] = [
    Ipv4Addr::new(1, 1, 1, 1),
    Ipv4Addr::new(8, 8, 8, 8),
];

/// Resolve the DNS servers to hand to the guest.
///
/// * `configured` — addresses named in the ES40 config, if any.
/// * otherwise the host's resolvers are used, and failing that the public
///   fallbacks above.
pub fn resolve(configured: &[Ipv4Addr]) -> Vec<Ipv4Addr> {
    if !configured.is_empty() {
        return configured.to_vec();
    }

    let host = host_servers();
    if !host.is_empty() {
        return host;
    }

    FALLBACK.to_vec()
}

/// The host's own IPv4 DNS servers, as best we can discover them.
fn host_servers() -> Vec<Ipv4Addr> {
    #[cfg(unix)]
    {
        resolv_conf()
    }
    #[cfg(windows)]
    {
        windows_servers()
    }
    #[cfg(not(any(unix, windows)))]
    {
        Vec::new()
    }
}

/// Parse the `nameserver` lines of `/etc/resolv.conf`.
#[cfg(unix)]
fn resolv_conf() -> Vec<Ipv4Addr> {
    let Ok(text) = std::fs::read_to_string("/etc/resolv.conf") else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        let Some(rest) = line.strip_prefix("nameserver") else {
            continue;
        };
        let token = rest.split_whitespace().next().unwrap_or("");
        if let Ok(ip) = token.parse::<Ipv4Addr>()
            && !out.contains(&ip)
        {
            out.push(ip);
        }
    }
    out
}

/// DNS servers of the active adapters, read through `GetNetworkParams`.
#[cfg(windows)]
fn windows_servers() -> Vec<Ipv4Addr> {
    // IP_ADDR_STRING: a singly linked list node; IpAddress is a 16-byte
    // dotted-decimal string, not a binary address.
    #[repr(C)]
    struct IpAddrString {
        next: *mut IpAddrString,
        ip_address: [u8; 16],
        ip_mask: [u8; 16],
        context: u32,
    }

    // FIXED_INFO, laid out per iptypes.h. String sizes include the NUL.
    #[repr(C)]
    struct FixedInfo {
        host_name: [u8; 132],
        domain_name: [u8; 132],
        current_dns_server: *mut IpAddrString,
        dns_server_list: IpAddrString,
        node_type: u32,
        scope_id: [u8; 260],
        enable_routing: u32,
        enable_proxy: u32,
        enable_dns: u32,
    }

    #[link(name = "iphlpapi")]
    unsafe extern "system" {
        fn GetNetworkParams(info: *mut u8, size: *mut u32) -> u32;
    }

    let mut out: Vec<Ipv4Addr> = Vec::new();
    unsafe {
        let mut size = 0u32;
        // First call sizes the buffer (returns ERROR_BUFFER_OVERFLOW).
        let _ = GetNetworkParams(std::ptr::null_mut(), &mut size);
        if size == 0 {
            return out;
        }
        let mut buf = vec![0u8; size as usize];
        if GetNetworkParams(buf.as_mut_ptr(), &mut size) != 0 {
            return out;
        }
        let info = &*(buf.as_ptr() as *const FixedInfo);
        let mut node: *const IpAddrString = &info.dns_server_list as *const IpAddrString;
        let mut guard = 0;
        while !node.is_null() && guard < 64 {
            guard += 1;
            let entry = &*node;
            let bytes = &entry.ip_address;
            let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
            if let Ok(s) = std::str::from_utf8(&bytes[..end])
                && let Ok(ip) = s.trim().parse::<Ipv4Addr>()
                && !ip.is_unspecified()
                && !out.contains(&ip)
            {
                out.push(ip);
            }
            node = entry.next;
        }
    }
    out
}
