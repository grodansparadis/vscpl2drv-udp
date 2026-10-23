% vscpl2drv-udp(1) VSCP Level II UDP Driver
% Ake Hedman and contributors, the VSCP Project
% October 2026

# NAME

vscpl2drv-udp - VSCP Level II UDP binary protocol driver

# DESCRIPTION

The shared library libvscpl2drv-udp.so exports the VSCP Level II driver API.
It sends and receives full VSCP binary events over UDP using IPv4 or IPv6.
No SocketCAN interface or CAN hardware is required.

# CONFIGURATION

Pass the JSON configuration path and driver GUID to VSCPOpen.
The installed sample is /usr/share/vscpl2drv-udp/udp.json.

**interface**
: Destination URL, default udp://255.255.255.255:33333. IPv6 URLs use brackets,
  for example udp://[::1]:33333. The receiver binds the wildcard address of
  the resolved family on the same port. Client-only sockets use an ephemeral
  source port. Multicast group membership is not implemented.

**flags**
: 0 enables sending and receiving, 1 receives only, 2 sends only,
  and 3 disables both.

**security**
: Object containing encryption, key and hmac. Omitted/empty means plaintext.
  Legacy top-level settings are accepted only when security is absent;
  mixing the two forms is rejected.

**security.encryption**
: none (default), aes128, aes192 or aes256. AES uses CBC with PKCS#7 padding
  and a fresh random 16-byte IV. The receiver enforces the configured algorithm.

**security.key**
: Path to a hex-encoded AES key containing exactly 16, 24 or 32 bytes,
  according to the selected AES algorithm. Whitespace is permitted.

**security.hmac**
: Optional path to an independent hex-encoded 32-byte HMAC key. If defined,
  AES encryption is required and both directions use authenticated version-1
  packets. HMAC-SHA256 is truncated to its leftmost 16 bytes and covers
  the prefix, type, ciphertext and IV. It is verified before decryption.
  Without this key, legacy UDP packets without a prefix are used.

**filter**
: in-filter, in-mask, out-filter and out-mask use VSCP priority,class,type,GUID
  strings. Defaults accept all events.

**logging**
: Console, rotating-file, syslog (not on Windows) and UDP logging are
  supported. See the sample for options.

**write**
: Reserved. Runtime configuration changes are not currently implemented.

Invalid configuration, missing/invalid required keys or bind failures prevent
the driver from opening. Keep configuration and key files private to the
driver user. Encryption without HMAC provides confidentiality only.
UDP has no delivery, ordering or replay guarantees.

# TESTING

Use udp://127.0.0.1:33333 with flags 0 to receive events written by the same
driver instance. For separate local sender and receiver processes use flags
2 and 1 respectively at the same destination port.

    cmake --build build --target udp-test
    ctest --test-dir build --output-on-failure

## Standalone test peer

The **vscp-udp-peer** executable sends and receives events using its built-in UDP
transport, or loads the driver at runtime with **--driver** and uses its Level II
API instead. Build it and the library with:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target vscp-udp-peer vscpl2drv-udp
    ./build/vscp-udp-peer --help

### CLI options

    vscp-udp-peer send|receive --config FILE [--driver LIBRARY] [options]

**--config FILE**
: Required JSON configuration in the driver's format.

**--driver LIBRARY**
: Optional local driver library path. Omit it to use the standalone UDP
  transport; specify it to call the loaded driver's Level II API.

**--count N**
: Number of events, 1-1000000. Default 1.

**--timeout MS**
: Timeout per operation, 0-3600000 milliseconds. Default 5000.

**--interval MS**
: Send only. Delay between events, 0-3600000 milliseconds. Default 1000.

**--class N**, **--type N**
: Send only. Class and type, each 0-65535. Defaults 10 and 6 respectively.

**--guid GUID**
: Send only. Sixteen colon-separated hexadecimal bytes. Default all zeros.

**--data HEX**
: Send only. Comma-separated two-digit hexadecimal bytes. Default empty.

**--help**
: Used alone, prints usage and exits successfully.

Options require separate values; repeated options are rejected. Diagnostics
go to stderr, received events to stdout as JSON lines. Disable console logging
in the configuration to avoid mixing driver log messages with JSON output.

### Setup and use

Create receive.json:

    {
      "interface": "udp://127.0.0.1:33333",
      "flags": 1,
      "security": { "encryption": "none" },
      "logging": { "console-enable": false }
    }

Copy it to send.json and change flags to 2. Start the receiver in one terminal:

    ./build/vscp-udp-peer receive --config receive.json --count 5 --timeout 60000

In another terminal send five events:

    ./build/vscp-udp-peer send --config send.json --count 5 --class 10 --type 6 --data 01,02,ff

The receiver prints one JSON event per line. Timeout is in milliseconds per
event. Errors produce a nonzero exit status.

To test the loaded driver without a daemon, start a driver-loading receiver:

    ./build/vscp-udp-peer receive --driver ./build/libvscpl2drv-udp.so --config receive.json --count 5 --timeout 60000

Then run the standalone sender command above. For the opposite direction, start
the standalone receiver and run:

    ./build/vscp-udp-peer send --driver ./build/libvscpl2drv-udp.so --config send.json --count 5 --data 01,02,ff

On macOS use libvscpl2drv-udp.dylib. On Windows use
build/Release/vscp-udp-peer.exe with --driver build/Release/vscpl2drv-udp.dll.
Only load trusted libraries matching the CLI architecture and VSCP event ABI.
The driver is closed before unloading. Missing libraries, missing exports and
driver API failures produce a nonzero exit status; there is no transport fallback.

To test peer-to-driver communication, stop the peer receiver and open the driver
with receive.json, then run the peer sender and read events through VSCPRead.
To test driver-to-peer communication, close the driver, start the peer receiver,
reopen the driver with send.json and send events through VSCPWrite. The daemon's
path-config must name the selected JSON file using an absolute path.
Only one process can bind the receive port on a given local address.

For network tests, change the sender's interface host to the receiving machine
and allow UDP port 33333 through its firewall. For encryption, configure the same
encryption mode and key material on both endpoints; set hmac on both or neither.
See the README for full setup, key generation and troubleshooting instructions.

# SEE ALSO

The VSCP specification: <https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_over_binary>
