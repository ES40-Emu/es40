//! Userspace (slirp) network backend for the ES40 emulator.
//!
//! This crate is compiled to a static library and linked into the C++
//! `es40` binary. The emulated DEC 21143 hands it raw Ethernet frames from
//! the guest and pulls frames back out; everything in between — ARP, DHCP,
//! and NATing the guest's TCP/UDP/ICMP to real host sockets — is done here
//! by the pure-Rust [`pktkit`] userspace network stack.
//!
//! Topology of one backend instance:
//!
//! ```text
//!   ES40 (CNetworkSlirp)  <--Ethernet-->  Es40Port
//!                                            |
//!                                         L2Hub  (learning switch)
//!                                         /   \
//!                              DhcpServer     L2Adapter
//!                                                 |
//!                                           slirp::Stack  --> host sockets
//! ```
//!
//! The hub wiring mirrors pktkit's own "virtual LAN with DHCP and NAT"
//! example: the DHCP server auto-configures the guest, and the `L2Adapter`
//! terminates ARP and bridges IP onto the slirp stack, which translates
//! the guest's traffic to real `TcpStream`/`UdpSocket`s.
//!
//! DHCP requests are fed to the server directly (see [`Es40Slirp::input`])
//! rather than through the switch: a renewing client unicasts to the server
//! address, which the gateway also answers, and without this it would be
//! routed to the slirp stack as an ordinary UDP datagram instead.

mod dns;
mod icmp;

use pktkit::dhcp::{Server as DhcpServer, ServerConfig as DhcpConfig};
use pktkit::slirp::Stack;
use pktkit::{
    Frame, IpPrefix, L2Adapter, L2AdapterConfig, L2Device, L2Handler, L2Hub, L2HubHandle, L3Device,
    MacAddr, Result as PktResult,
};
use std::collections::VecDeque;
use std::ffi::{c_char, c_int, CStr};
use std::net::{IpAddr, Ipv4Addr};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

/// Largest frame we will carry, comfortably past any legal Ethernet frame.
const MAX_FRAME: usize = 65536;

/// Ethernet's minimum frame size, excluding the FCS: 64 bytes on the wire
/// less the 4-byte FCS a PHY would append. Frames handed to the guest are
/// padded to this, exactly as a real link (and the TAP/pcap backends) would,
/// so a driver that rejects runts still works.
const MIN_ETHERNET_FRAME: usize = 60;

/// The virtual gateway's MAC: locally administered, fixed so that guest ARP
/// caches stay valid across restarts.
const GATEWAY_MAC: MacAddr = MacAddr([0x02, 0x00, 0x00, 0x00, 0x00, 0x01]);

/// Default DHCP pool when the configuration does not name one: the classic
/// slirp range of host offsets 15 through 30.
const DEFAULT_DHCP_FIRST: u32 = 15;
const DEFAULT_DHCP_LAST: u32 = 30;

// ---------------------------------------------------------------------------
// Wakeup pipe
// ---------------------------------------------------------------------------

/// A self-pipe used to make the backend's readable fd fire when a frame is
/// queued for the guest. On platforms without it the fd is `-1` and the NIC
/// thread polls instead.
struct Wake {
    #[cfg(unix)]
    reader: std::os::unix::net::UnixStream,
    #[cfg(unix)]
    writer: std::os::unix::net::UnixStream,
}

impl Wake {
    fn new() -> std::io::Result<Wake> {
        #[cfg(unix)]
        {
            let (reader, writer) = std::os::unix::net::UnixStream::pair()?;
            reader.set_nonblocking(true)?;
            writer.set_nonblocking(true)?;
            Ok(Wake { reader, writer })
        }
        #[cfg(not(unix))]
        {
            Ok(Wake {})
        }
    }

    /// Make the fd readable. Best-effort: if the pipe is full the reader has
    /// not drained it yet, and a readable fd is already the goal.
    fn signal(&self) {
        #[cfg(unix)]
        {
            use std::io::Write;
            let _ = (&self.writer).write(&[1u8]);
        }
    }

    /// Drain pending wakeups. Called once the output queue is empty so a
    /// level-triggered poll does not spin.
    fn clear(&self) {
        #[cfg(unix)]
        {
            use std::io::Read;
            let mut buf = [0u8; 64];
            loop {
                match (&self.reader).read(&mut buf) {
                    Ok(0) => break,
                    Ok(_) => continue,
                    Err(_) => break,
                }
            }
        }
    }

    fn raw_fd(&self) -> c_int {
        #[cfg(unix)]
        {
            use std::os::unix::io::AsRawFd;
            self.reader.as_raw_fd()
        }
        #[cfg(not(unix))]
        {
            -1
        }
    }
}

// ---------------------------------------------------------------------------
// The guest-side L2 device
// ---------------------------------------------------------------------------

/// The L2 device standing in for the emulated NIC.
///
/// `send` is the network-to-guest direction: the switch delivers a frame to
/// the guest, which we queue for the emulator to collect. `inject` is the
/// guest-to-network direction, driving the handler the switch installed.
struct Es40Port {
    mac: MacAddr,
    handler: Mutex<Option<L2Handler>>,
    out: Arc<Mutex<VecDeque<Vec<u8>>>>,
    wake: Wake,
}

impl Es40Port {
    fn inject(&self, frame: &Frame) -> PktResult<()> {
        // Clone the handler out and drop the lock before calling it: the
        // handler is the switch, which may re-enter us.
        let handler = self.handler.lock().unwrap().clone();
        match handler {
            Some(h) => h(frame),
            None => Ok(()),
        }
    }

    fn enqueue(&self, frame: &Frame) {
        if frame.len() > MAX_FRAME {
            return;
        }
        let mut buf = frame.as_bytes().to_vec();
        // Pad to the Ethernet minimum, as the wire would.
        if buf.len() < MIN_ETHERNET_FRAME {
            buf.resize(MIN_ETHERNET_FRAME, 0);
        }
        self.out.lock().unwrap().push_back(buf);
        self.wake.signal();
    }
}

impl L2Device for Es40Port {
    fn set_handler(&self, h: L2Handler) {
        *self.handler.lock().unwrap() = Some(h);
    }

    fn send(&self, f: &Frame) -> PktResult<()> {
        self.enqueue(f);
        Ok(())
    }

    fn hw_addr(&self) -> MacAddr {
        self.mac
    }

    fn close(&self) -> PktResult<()> {
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Backend context
// ---------------------------------------------------------------------------

/// One initialized slirp backend.
pub struct Es40Slirp {
    /// Kept alive for the backend's lifetime: the devices' handlers and the
    /// port handles only hold weak references to it.
    _hub: Arc<L2Hub>,
    port: Arc<Es40Port>,
    adapter: Arc<L2Adapter>,
    stack: Arc<Stack>,
    dhcp: Arc<DhcpServer>,
    /// Kept alive for the backend's lifetime: dropping one detaches its
    /// device from the switch.
    _handles: Mutex<Vec<L2HubHandle>>,
    /// The gateway address, to tell locally-answered ICMP from external.
    gateway: Ipv4Addr,
    /// The guest subnet, for the same reason.
    network: IpPrefix,
    /// Shared with the ICMP proxy threads so they stop after teardown.
    closed: Arc<AtomicBool>,
}

/// Configuration passed across the FFI boundary.
///
/// All strings are NUL-terminated UTF-8; the pointers only need to be valid
/// for the duration of [`es40_slirp_new`]. Null means "use the default".
#[repr(C)]
pub struct Es40SlirpConfig {
    pub mac: [u8; 6],
    /// Network prefix, e.g. `10.0.2.0/24`.
    pub network: *const c_char,
    /// Address the guest sees as its gateway, e.g. `10.0.2.2`.
    pub gateway: *const c_char,
    /// First DHCP pool address, or null to derive it from the network.
    pub dhcp_start: *const c_char,
    /// Last DHCP pool address, or null to derive it from the network.
    pub dhcp_end: *const c_char,
    /// Comma-separated DNS server addresses, or null to use the host's.
    pub dns: *const c_char,
    /// Lease length in seconds; 0 uses the pktkit default (one hour).
    pub lease_secs: u32,
}

impl Es40Slirp {
    /// Feed an Ethernet frame from the guest into the virtual network.
    fn input(&self, frame: &[u8]) {
        if self.closed.load(Ordering::Acquire) {
            return;
        }
        // DHCP goes to the server directly; see the module docs.
        if is_dhcp_to_server(frame) {
            let _ = self.dhcp.send(Frame::from_slice(frame));
            return;
        }
        // A ping to an off-network host: the stack cannot reach it with
        // ICMP, so replay it from the host and relay the reply (see icmp).
        if let Some(req) = icmp::parse_echo_request(frame)
            && icmp::is_external(req.dst, self.gateway, self.network.addr(), self.network.bits())
        {
            let port = self.port.clone();
            let closed = self.closed.clone();
            let _ = std::thread::Builder::new()
                .name("es40-slirp-icmp".to_string())
                .stack_size(128 * 1024)
                .spawn(move || {
                    if !icmp::host_reachable(req.dst) {
                        return;
                    }
                    if closed.load(Ordering::Acquire) {
                        return;
                    }
                    let reply = icmp::build_echo_reply(&req, GATEWAY_MAC.octets());
                    port.enqueue(Frame::from_slice(&reply));
                });
            return;
        }
        let _ = self.port.inject(Frame::from_slice(frame));
    }
}

/// True for an Ethernet/IPv4/UDP datagram addressed to the DHCP server port
/// (67). Whole datagrams only: a fragment has no UDP header where we look.
fn is_dhcp_to_server(frame: &[u8]) -> bool {
    // Ethernet + IPv4 minimum, then IPv4 header + UDP header.
    if frame.len() < 14 + 20 + 8 {
        return false;
    }
    if frame[12..14] != [0x08, 0x00] {
        return false;
    }
    let ip = &frame[14..];
    let ihl = ((ip[0] & 0x0f) as usize) * 4;
    if ihl < 20 || ip.len() < ihl + 8 {
        return false;
    }
    if ip[9] != 17 {
        return false;
    }
    // A first fragment holding only part of the payload is not a whole
    // datagram, and the bytes where the UDP header would be are data.
    let frag = u16::from_be_bytes([ip[6], ip[7]]);
    if frag & 0x1fff != 0 {
        return false;
    }
    let udp_dst = u16::from_be_bytes([ip[ihl + 2], ip[ihl + 3]]);
    udp_dst == 67
}

// ---------------------------------------------------------------------------
// Construction helpers
// ---------------------------------------------------------------------------

unsafe fn cstr_to_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .ok()
        .map(|s| s.to_string())
}

/// The broadcast address of a prefix whose host bits are set in `addr`.
fn subnet_mask(bits: u8) -> Ipv4Addr {
    let mask: u32 = if bits >= 32 {
        u32::MAX
    } else if bits == 0 {
        0
    } else {
        u32::MAX << (32 - bits)
    };
    Ipv4Addr::from(mask)
}

fn default_pool(network: IpPrefix) -> (Ipv4Addr, Ipv4Addr) {
    let base = match network.addr() {
        IpAddr::V4(a) => u32::from(a),
        IpAddr::V6(_) => 0,
    };
    (
        Ipv4Addr::from(base + DEFAULT_DHCP_FIRST),
        Ipv4Addr::from(base + DEFAULT_DHCP_LAST),
    )
}

struct Settings {
    network: IpPrefix,
    gateway: Ipv4Addr,
    dhcp_start: Ipv4Addr,
    dhcp_end: Ipv4Addr,
    dns: Vec<Ipv4Addr>,
    lease: Duration,
}

/// Parse and validate the C configuration, applying defaults.
unsafe fn parse_config(cfg: &Es40SlirpConfig) -> Result<Settings, String> {
    let network: IpPrefix = unsafe { cstr_to_string(cfg.network) }
        .unwrap_or_else(|| "10.0.2.0/24".to_string())
        .parse()
        .map_err(|_| "invalid network prefix (expected e.g. \"10.0.2.0/24\")".to_string())?;

    if !network.is_valid() || !network.is_v4() {
        return Err("network prefix must be a valid IPv4 prefix".to_string());
    }

    let gateway: Ipv4Addr = unsafe { cstr_to_string(cfg.gateway) }
        .unwrap_or_else(|| "10.0.2.2".to_string())
        .parse()
        .map_err(|_| "invalid gateway address".to_string())?;

    let (default_start, default_end) = default_pool(network);
    let dhcp_start: Ipv4Addr = match unsafe { cstr_to_string(cfg.dhcp_start) } {
        Some(s) => s
            .parse()
            .map_err(|_| "invalid dhcp_start address".to_string())?,
        None => default_start,
    };
    let dhcp_end: Ipv4Addr = match unsafe { cstr_to_string(cfg.dhcp_end) } {
        Some(s) => s
            .parse()
            .map_err(|_| "invalid dhcp_end address".to_string())?,
        None => default_end,
    };
    if u32::from(dhcp_start) > u32::from(dhcp_end) {
        return Err("dhcp_start is after dhcp_end".to_string());
    }

    let configured_dns: Vec<Ipv4Addr> = match unsafe { cstr_to_string(cfg.dns) } {
        Some(s) => s
            .split([',', ' ', ';'])
            .map(str::trim)
            .filter(|t| !t.is_empty())
            .map(|t| {
                t.parse::<Ipv4Addr>()
                    .map_err(|_| format!("invalid DNS address \"{t}\""))
            })
            .collect::<Result<_, _>>()?,
        None => Vec::new(),
    };
    let dns = dns::resolve(&configured_dns);

    let lease = if cfg.lease_secs == 0 {
        Duration::from_secs(3600)
    } else {
        Duration::from_secs(cfg.lease_secs as u64)
    };

    Ok(Settings {
        network,
        gateway,
        dhcp_start,
        dhcp_end,
        dns,
        lease,
    })
}

fn build(cfg: &Es40SlirpConfig) -> Result<Es40Slirp, String> {
    let s = unsafe { parse_config(cfg) }?;

    let stack = Stack::new();
    stack
        .set_addr(IpPrefix::new(IpAddr::V4(s.gateway), s.network.bits()))
        .map_err(|e| format!("could not configure slirp stack: {e}"))?;

    let hub = Arc::new(L2Hub::new());
    let out = Arc::new(Mutex::new(VecDeque::new()));
    let wake = Wake::new().map_err(|e| format!("could not create wakeup pipe: {e}"))?;
    let port = Arc::new(Es40Port {
        mac: MacAddr(cfg.mac),
        handler: Mutex::new(None),
        out,
        wake,
    });

    let adapter = L2Adapter::new_arc(
        stack.clone() as Arc<dyn L3Device>,
        L2AdapterConfig::default().mac(GATEWAY_MAC),
    );

    let dhcp_cfg = DhcpConfig::new(s.gateway, s.dhcp_start, s.dhcp_end)
        .subnet_mask(subnet_mask(s.network.bits()))
        .router(s.gateway)
        .dns(s.dns)
        .lease_time(s.lease);
    let dhcp = Arc::new(DhcpServer::new(dhcp_cfg));

    let handles = vec![
        hub.connect_arc(port.clone() as Arc<dyn L2Device>),
        hub.connect_arc(adapter.clone() as Arc<dyn L2Device>),
        hub.connect_arc(dhcp.clone() as Arc<dyn L2Device>),
    ];

    Ok(Es40Slirp {
        _hub: hub,
        port,
        adapter,
        stack,
        dhcp,
        _handles: Mutex::new(handles),
        gateway: s.gateway,
        network: s.network,
        closed: Arc::new(AtomicBool::new(false)),
    })
}

// ---------------------------------------------------------------------------
// C ABI
// ---------------------------------------------------------------------------

/// Availability probe so the C++ side can report a clear error on a build
/// where the static library was somehow not linked.
#[unsafe(no_mangle)]
pub extern "C" fn es40_slirp_available() -> c_int {
    1
}

/// Create a backend. Returns null on failure after printing a diagnostic.
///
/// # Safety
/// `cfg` must point to a valid [`Es40SlirpConfig`] whose string pointers are
/// valid for the duration of the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn es40_slirp_new(cfg: *const Es40SlirpConfig) -> *mut Es40Slirp {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let Some(cfg) = (unsafe { cfg.as_ref() }) else {
            return Err("null configuration".to_string());
        };
        build(cfg)
    }));

    match result {
        Ok(Ok(ctx)) => Box::into_raw(Box::new(ctx)),
        Ok(Err(msg)) => {
            eprintln!("slirp: {msg}");
            std::ptr::null_mut()
        }
        Err(_) => {
            eprintln!("slirp: panic while initializing the Rust backend");
            std::ptr::null_mut()
        }
    }
}

/// Tear a backend down, closing every flow and stopping its threads.
///
/// # Safety
/// `ctx` must be a pointer returned by [`es40_slirp_new`] and not yet freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn es40_slirp_free(ctx: *mut Es40Slirp) {
    if ctx.is_null() {
        return;
    }
    // If a panic unwinds here we still want to stop; but a panic in Drop is
    // fatal anyway, so keep it simple.
    let ctx = unsafe { Box::from_raw(ctx) };
    ctx.closed.store(true, Ordering::SeqCst);
    // Detach the devices from the switch before shutting the stack down.
    ctx._handles.lock().unwrap().clear();
    let _ = ctx.adapter.close();
    let _ = ctx.stack.shutdown();
}

/// Feed one Ethernet frame from the guest into the network.
///
/// # Safety
/// `ctx` must be a live backend and `data`/`len` a valid frame.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn es40_slirp_input(
    ctx: *mut Es40Slirp,
    data: *const u8,
    len: c_int,
) -> c_int {
    if ctx.is_null() || data.is_null() || len < 0 {
        return -1;
    }
    let Some(ctx) = (unsafe { ctx.as_ref() }) else {
        return -1;
    };
    let frame = unsafe { std::slice::from_raw_parts(data, len as usize) };
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| ctx.input(frame)));
    match r {
        Ok(()) => 0,
        Err(_) => {
            eprintln!("slirp: panic while processing an inbound frame");
            -1
        }
    }
}

/// Copy the next frame for the guest into `buf`.
///
/// Returns the frame length on success, 0 when nothing is queued, and -1 on
/// error. Frames larger than `cap` are dropped.
///
/// # Safety
/// `ctx` must be a live backend and `buf` writable for `cap` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn es40_slirp_output(
    ctx: *mut Es40Slirp,
    buf: *mut u8,
    cap: c_int,
) -> c_int {
    if ctx.is_null() || buf.is_null() || cap <= 0 {
        return -1;
    }
    let Some(ctx) = (unsafe { ctx.as_ref() }) else {
        return -1;
    };
    let cap = cap as usize;

    loop {
        let next = ctx.port.out.lock().unwrap().pop_front();
        match next {
            Some(frame) => {
                if frame.len() > cap {
                    continue; // oversized for the caller's buffer; drop it
                }
                unsafe { std::ptr::copy_nonoverlapping(frame.as_ptr(), buf, frame.len()) };
                return frame.len() as c_int;
            }
            None => {
                // Clear the wakeup before re-checking, so a frame queued
                // between the two cannot be missed behind a drained pipe.
                ctx.port.wake.clear();
                let again = ctx.port.out.lock().unwrap().pop_front();
                match again {
                    Some(frame) => {
                        if frame.len() > cap {
                            continue;
                        }
                        unsafe {
                            std::ptr::copy_nonoverlapping(frame.as_ptr(), buf, frame.len());
                        }
                        return frame.len() as c_int;
                    }
                    None => return 0,
                }
            }
        }
    }
}

/// The descriptor that becomes readable when a frame is waiting, or -1 when
/// the platform has no such descriptor (the NIC thread then polls).
///
/// # Safety
/// `ctx` must be a live backend.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn es40_slirp_wait_fd(ctx: *mut Es40Slirp) -> c_int {
    let Some(ctx) = (unsafe { ctx.as_ref() }) else {
        return -1;
    };
    ctx.port.wake.raw_fd()
}
