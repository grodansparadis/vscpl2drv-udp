# Creating a VSCP Level II multicast driver

## Status and purpose

This is an implementation guide for creating a separate multicast driver from
the current UDP driver. It describes proposed behavior, not features currently
implemented by this repository. In particular, the current receiver does not
join multicast groups.

Multicast is still UDP. It changes the destination addressing and receiver
membership, not the VSCP event encoding or encryption.

The suggested new project/library name is `vscpl2drv-multicast`. Keep the existing
UDP driver working independently; do not silently change its socket-sharing or
receive behavior when creating the multicast driver.

## Existing code to reuse

| Source | Responsibility | Multicast changes |
|--------|----------------|-------------------|
| [src/udp.h](../src/udp.h) | Transport state, filters, receive queue, synchronization | Add multicast configuration and membership/interface state; rename the class if creating a new driver. |
| [src/udp.cpp](../src/udp.cpp), `CUdp::loadConfig` | Configuration, keys, logging and filters | Parse and validate multicast options. |
| [src/udp.cpp](../src/udp.cpp), `CUdp::setupSocket` | Endpoint resolution, socket configuration and bind | Main implementation changes: multicast detection, interface selection, reuse, sender options and group join. |
| [src/udp.cpp](../src/udp.cpp), `CUdp::write` | Encode and send one datagram | Keep `sendto`; destination becomes the selected group. |
| [src/udp.cpp](../src/udp.cpp), `CUdp::worker` | Receive, validate, filter and queue events | Basic flow stays; consider group isolation and oversized-datagram handling below. |
| [src/udp.cpp](../src/udp.cpp), `CUdp::close` | Stop worker and close socket | Release membership and close reliably. |
| [src/vscpl2drv-udp.cpp](../src/vscpl2drv-udp.cpp) | Exported Level II API and handle management | Preserve the API under the new library name. |
| [src/udp-peer.cpp](../src/udp-peer.cpp) | Standalone peer and runtime driver host | Extend standalone mode to use multicast; retain `--driver` for testing the new library. |
| [src/test.cpp](../src/test.cpp) | Wire-format, crypto and integration tests | Reuse existing tests and add multicast-specific coverage. |
| [CMakeLists.txt](../CMakeLists.txt) | Build, tests, installation and packaging | Add/rename targets, install paths, package names and platform dependencies. |

Reuse existing VSCP framing and crypto helpers. Avoid maintaining a second copy
of the wire format merely because the destination is multicast.

## What does not change

- Exported functions: `VSCPOpen`, `VSCPClose`, `VSCPWrite`, `VSCPRead` and
  `VSCPGetVersion`.
- Event class, type, GUID, timestamp and payload handling.
- Binary frame formats and CRC validation.
- AES-128/192/256-CBC and optional HMAC-SHA256 packet format.
- Mandatory matching encryption/authentication modes at both endpoints.
- Input/output VSCP event filters.
- Receive queue, locking, read timeouts and close interrupting a pending read.
- Existing flag meanings:
  - `0`: send and receive.
  - `1`: receive only.
  - `2`: send only.
  - `3`: neither direction.
- Console, file, syslog and UDP **logging** settings. UDP logging is a separate
  transport from VSCP event multicast and must not be redirected automatically.

Multicast adds no acknowledgements, ordering, replay protection or guaranteed
delivery.

## Proposed configuration

Keep the existing `udp://` URL syntax: multicast is a UDP addressing mode, so a
new URL scheme is unnecessary. For a multicast-only driver, require the resolved
destination to be a multicast group rather than accepting unicast/broadcast.

IPv4 example:

```json
{
  "interface": "udp://239.192.0.1:33333",
  "flags": 0,
  "multicast": {
    "interface-address": "192.168.1.20",
    "ttl": 1,
    "loopback": true,
    "reuse-address": true
  },
  "security": {
    "encryption": "none",
    "key": "",
    "hmac": ""
  },
  "logging": {
    "console-enable": true,
    "console-level": "info"
  }
}
```

The interface address is the **local NIC address**, not the multicast group or
the remote sender. Replace it with an address actually assigned to the host.

Proposed option contract:

| Option | Meaning / validation |
|--------|----------------------|
| `interface` | Multicast group and port. Validate multicast after address resolution; port must be 1-65535. Prefer numeric group addresses for deterministic configuration. |
| `multicast.interface-address` | IPv4 local interface address; optional. Omission uses the OS-selected default interface. Use the same selection for joining and sending. |
| `multicast.interface-index` | IPv6 local interface index; optional except when scope/interface selection requires it. Must refer to a valid interface. |
| `multicast.ttl` | IPv4 TTL or IPv6 hop limit, integer 0-255; default 1. Reject floating-point values and out-of-range integers. |
| `multicast.loopback` | Boolean, default true. Controls multicast delivery to local sockets. Platform semantics differ; do not treat it as portable self-event suppression. |
| `multicast.reuse-address` | Boolean, proposed default true for multicast receive sockets, allowing multiple local subscribers on one port where supported. |

Use `interface-address` for IPv4 and `interface-index` for IPv6. Reject conflicting
or wrong-family selections rather than silently ignoring them. An interface-name
option can be added later, but needs explicit cross-platform name-to-index/address
mapping; do not assume POSIX names such as `eth0` exist on Windows.

For IPv6, a corresponding configuration could use:

```json
{
  "interface": "udp://[ff12::1234]:33333",
  "flags": 0,
  "multicast": {
    "interface-index": 2,
    "ttl": 1,
    "loopback": true,
    "reuse-address": true
  },
  "security": { "encryption": "none" }
}
```

The group is illustrative; select a suitable group and scope for the deployment.
For interface-/link-local scoped destinations, set the destination's
`sin6_scope_id` consistently with the membership and outbound interface.
If scoped URLs such as `[ff12::1234%eth0]` are supported, document their syntax
and reject conflicts between the URL scope and configured interface index.

Group allocation, IPv4 administrative scope and IPv6 scope must be considered.
Do not choose reserved control groups or arbitrary groups already used by other
applications on the network.

## Socket setup sequence

Implement this in the multicast equivalent of `CUdp::setupSocket`.

1. Parse configuration and initialize Winsock on Windows.
2. Resolve the endpoint, then validate the family and multicast group:
   - IPv4: `IN_MULTICAST(ntohl(address.s_addr))`, or an equivalent check for
     `224.0.0.0/4`.
   - IPv6: `IN6_IS_ADDR_MULTICAST`.
3. Resolve/validate the selected local interface.
4. Create the UDP socket and configure nonblocking mode and close-on-exec where
   supported, using the existing platform helpers.
5. Set IPv6 sockets to IPv6-only where appropriate to keep IPv4/IPv6 behavior
   explicit rather than relying on platform dual-stack defaults.
6. If receiving is enabled, configure the chosen address/port reuse policy
   **before binding**.
7. If sending is enabled, configure the outbound multicast interface,
   TTL/hop limit and loopback option.
8. If receiving is enabled:
   - Bind the wildcard address for the selected family and configured port.
   - Join the selected group on the chosen interface.
   - Apply the selected group-isolation policy.
9. Save the group destination and start the existing receive worker.

Binding to a wildcard address/port alone does **not** subscribe to multicast.
An explicit membership join is required.

Do not enable `SO_BROADCAST` for multicast-only sockets. It is unrelated to
multicast operation.

Send-only sockets do not need to join a group or bind the receive port.
For `flags: 3`, preserve the current disabled-operation results and document
whether startup still validates/creates a socket; do not join a receive group.

Every required `setsockopt`, bind and join failure must prevent startup with an
explicit diagnostic. Include the operation, group, interface and platform error
text, but never log keys. Close partial sockets on every failure path.

### IPv4 socket options

| Purpose | Option | Typical argument |
|---------|--------|------------------|
| Join a group | `IP_ADD_MEMBERSHIP` at `IPPROTO_IP` | `ip_mreq` containing group address and local interface address |
| Leave a group | `IP_DROP_MEMBERSHIP` | Same group/interface selection |
| Select sending NIC | `IP_MULTICAST_IF` | Local IPv4 interface address |
| Set reach | `IP_MULTICAST_TTL` | Platform-appropriate integer/byte TTL |
| Configure local delivery | `IP_MULTICAST_LOOP` | Platform-appropriate integer/byte boolean |

Use portable `ip_mreq` for an address-based configuration. Linux's `ip_mreqn`
offers an interface index but is not portable to all target platforms.

### IPv6 socket options

| Purpose | Option | Typical argument |
|---------|--------|------------------|
| Join a group | `IPV6_JOIN_GROUP` at `IPPROTO_IPV6` | `ipv6_mreq` containing group and interface index |
| Leave a group | `IPV6_LEAVE_GROUP` | Same group/interface selection |
| Select sending NIC | `IPV6_MULTICAST_IF` | Interface index |
| Set reach | `IPV6_MULTICAST_HOPS` | Integer hop limit |
| Configure local delivery | `IPV6_MULTICAST_LOOP` | Platform-appropriate boolean/integer |

Use argument types and lengths expected by each platform, not a single assumed
`sizeof(int)` for every IPv4/IPv6 option. Winsock option pointers also require
the existing `const char *` adaptation.

## Port sharing and receive isolation

The expected multicast use case is one sender and multiple subscribers. Test
multiple receivers on the same host, group and port, not just on separate hosts.

- Linux normally uses `SO_REUSEADDR` for multicast port sharing.
- macOS/BSD may also need `SO_REUSEPORT` for the intended sharing behavior.
- Windows has different reuse/exclusive-binding semantics. Do not combine
  `SO_EXCLUSIVEADDRUSE` with a design that requires shared multicast bindings.
- Never enable a platform-specific option unconditionally on other platforms.
- If extending the UDP driver rather than creating a separate one, keep reuse
  opt-in for multicast; preserve existing unicast bind-collision behavior.

Reusing a wildcard port can also allow traffic other than the subscribed group's
traffic to reach a socket. A successful group join is not a portable guarantee
of strict group isolation:

- On Linux consider `IP_MULTICAST_ALL=0` for IPv4, so reception is limited to the
  socket's own memberships rather than memberships of other sockets on the host.
- If strict destination/group/interface validation is required across platforms,
  receive destination metadata with `recvmsg`/packet-info options on POSIX and
  `WSARecvMsg` with packet-info options on Windows. Validate the destination
  group and incoming interface before decoding the VSCP event.
- Joining only one group does not by itself exclude unicast packets sent to the
  bound wildcard port. Define whether those packets are accepted or rejected.

Document and test the chosen policy. VSCP event filters inspect event fields,
not the destination IP address, and cannot provide multicast group isolation.

## Loopback and self-reception

With multicast loopback enabled, a bidirectional instance may receive its own
events. Local delivery behavior and the endpoint on which the loopback option
takes effect vary between Winsock and POSIX.

Keep loopback enabled by default for two-process local tests. Do not silently
discard an event based only on its GUID or source port: several legitimate
senders may share those values.

If self-suppression is later required, define it as a separate feature with a
reliable identifier and tests. Disabling multicast loopback is not a substitute:
it can affect other local subscribers as well.

## Shutdown and errors

Preserve the current shutdown order: mark stopped, wake blocked readers, join
the worker, then close the socket.

Closing a socket releases its multicast memberships automatically. Explicit
leave calls are optional for a close-only lifecycle; if implemented, retain the
exact family/group/interface used to join, issue the matching leave operation,
report failures and still close the socket.

Do not change membership while the worker is receiving without an explicit
synchronization design. Runtime interface changes, rejoining after network
changes and reconnect policies are separate features; the simplest first
implementation requires closing and reopening the driver.

Preserve rejection of malformed VSCP packets. Oversized UDP datagrams need
special care on Windows, where `recvfrom` can return `WSAEMSGSIZE` rather than a
truncated positive result. Log/drop them and keep the receive loop alive.

## Encryption and group security

Use the existing AES and HMAC configuration under the `security` object unchanged:

- Match AES size and key material at all participating endpoints.
- If HMAC is enabled, configure the matching HMAC key at every endpoint.
- HMAC with `encryption: "none"` must continue to fail startup.
- Keep fresh IV generation, authentication-before-decryption and CRC checks.
- Do not embed shared keys in the group address or discovery messages.

Every group member holding the HMAC key can generate authenticated packets.
HMAC authenticates possession of the group key, not a unique sender identity.
There is no replay protection, sender authorization, key distribution or
automatic rekeying in the current protocol implementation. Membership changes
may therefore require operational key rotation.

Multicast group membership is not a security boundary. Use encryption with HMAC,
appropriate host/network firewall rules and a trusted deployment network where
needed.

## CLI and documentation updates

Extend the standalone transport used by `vscp-udp-peer` to support the new group
configuration. Adding `--driver` alone tests only the loaded endpoint; the
standalone endpoint must also join the group when receiving.

An intended two-terminal test after implementation:

1. Create matching multicast configurations:
   - `receive.json`: selected group/port, receive-only `flags: 1`.
   - `send.json`: same group/port, send-only `flags: 2`.
   - Select local interfaces as appropriate for each host.
2. Start the new driver through the CLI:

   ```bash
   ./build/vscp-udp-peer receive \
     --driver ./build/libvscpl2drv-multicast.so \
     --config receive.json --count 5 --timeout 60000
   ```

3. Wait for the receiver-ready message, then send from the multicast-capable
   standalone transport:

   ```bash
   ./build/vscp-udp-peer send --config send.json \
     --count 5 --class 10 --type 6 --data 01,02,ff
   ```

4. Repeat with `--driver` on the sender and a standalone receiver.
5. Start two receivers simultaneously and verify both obtain every test event.

These commands describe the intended future multicast implementation. The new
library does not currently exist and the existing standalone receiver does not
join multicast groups.

Update README, sample JSON, man page, CLI help, daemon configuration examples,
CMake/CPack names and CI artifacts for the new driver. Keep the log collector's
UDP port separate from the VSCP event port.

## Validation and acceptance criteria

Run platform-native tests on Linux amd64, Raspberry Pi armhf/arm64, Windows and
macOS. CI may restrict multicast, and emulation may have different networking
behavior: do not silently treat a networking failure as a successful test.
If multicast tests require a dedicated network-enabled job, expose the result
separately and document any explicit skip.

| Test | Expected result |
|------|-----------------|
| IPv4 one sender, two local receivers | Both subscribed sockets receive each event with matching metadata/payload. |
| Two hosts on the same LAN | Both endpoints use the intended NIC; firewall permits group/port traffic. |
| IPv6 membership and scope | Correct interface index/scope delivers events; invalid selection fails startup. |
| Send-only / receive-only / disabled flags | Existing operation results preserved; no unnecessary group joins. |
| Shared group/port | Multiple receivers bind and join on each supported OS. |
| Different groups on the same port | Behavior matches the documented isolation policy; test unicast traffic too. |
| Loopback | Verify enabled/disabled behavior per OS, including a bidirectional instance. |
| Interface selection | Verify on a multi-NIC host; invalid/missing interfaces produce clear errors. |
| TTL/hops | Validate 0, 1 and 255 plus rejected negative, fractional and >255 inputs; routed delivery requires a multicast router. |
| Plain, AES-128/192/256, each AES with HMAC | Preserve all seven existing modes and event round trips. |
| Wrong keys, mismatched mode, tampering, truncation, oversized packet | Reject/drop without crashing or stopping subsequent valid reception. |
| Filters and maximum payload | Existing event filtering and payload limits remain intact. |
| CLI against loaded multicast driver, both directions | Multiple events cross the real Level II API boundary and UDP socket. |
| Pending read during close | Read wakes promptly; worker exits; port and membership can be reused on reopen. |
| Join/bind/option failure | Startup fails, partial sockets are cleaned up and no secrets are logged. |
| Existing UDP driver regression | Unicast/broadcast behavior, exclusive bind tests, framing and crypto remain unchanged. |

A TTL of 1 is a sensible local-network default, not a promise of cross-subnet
delivery. Routers, IGMP/MLD snooping, Wi-Fi access points, containers, VPNs and
firewalls can all affect multicast. Successful `sendto` means only that the
local socket accepted a datagram.

## Suggested implementation order

1. Create the separate target/library identity while retaining the Level II API.
2. Add strict multicast configuration parsing and address/interface validation.
3. Implement IPv4 joins, outbound selection, TTL, loopback and shared receive.
4. Add IPv4 multi-receiver and shutdown tests before expanding the feature set.
5. Implement IPv6 interface/scope handling and native platform differences.
6. Implement and test the chosen group-isolation policy.
7. Wire the standalone CLI and test loaded-driver communication in both directions.
8. Run existing crypto/filter tests plus the multicast acceptance matrix.
9. Finish platform documentation, sample configurations and CI packaging.

## API references

- [Linux ip(7)](https://man7.org/linux/man-pages/man7/ip.7.html)
- [Linux ipv6(7)](https://man7.org/linux/man-pages/man7/ipv6.7.html)
- [Winsock IPPROTO_IP socket options](https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options)
- [Winsock IPPROTO_IPV6 socket options](https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ipv6-socket-options)
- [Winsock SO_REUSEADDR and SO_EXCLUSIVEADDRUSE](https://learn.microsoft.com/en-us/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse)

Verify the target operating system's current API documentation and behavior when
implementing socket-option argument types, port sharing and loopback semantics.
