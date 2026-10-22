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
: Destination URL, default udp://255.255.255.255:9999. IPv6 URLs use brackets,
  for example udp://[::1]:9999. The receiver binds the wildcard address of
  the resolved family on the same port. Client-only sockets use an ephemeral
  source port. Multicast group membership is not implemented.

**flags**
: 0 enables sending and receiving, 1 receives only, 2 sends only,
  and 3 disables both.

**encryption**
: none (default), aes128, aes192 or aes256. AES uses CBC with PKCS#7 padding
  and a fresh random 16-byte IV. The receiver enforces the configured algorithm.

**key**
: Path to a hex-encoded AES key containing exactly 16, 24 or 32 bytes,
  according to the selected AES algorithm. Whitespace is permitted.

**hmac**
: Optional path to an independent hex-encoded 32-byte HMAC key. If defined,
  AES encryption is required and both directions use authenticated version-1
  packets. HMAC-SHA256 is truncated to its leftmost 16 bytes and covers
  the prefix, type, ciphertext and IV. It is verified before decryption.
  Without this key, legacy UDP packets without a prefix are used.

**filter**
: in-filter, in-mask, out-filter and out-mask use VSCP priority,class,type,GUID
  strings. Defaults accept all events.

**logging**
: Console and rotating-file logging are supported. See the sample for options.

**write**
: Reserved. Runtime configuration changes are not currently implemented.

Invalid configuration, missing/invalid required keys or bind failures prevent
the driver from opening. Keep configuration and key files private to the
driver user. Encryption without HMAC provides confidentiality only.
UDP has no delivery, ordering or replay guarantees.

# TESTING

Use udp://127.0.0.1:9999 with flags 0 to receive events written by the same
driver instance. For separate local sender and receiver processes use flags
2 and 1 respectively at the same destination port.

    cmake --build build --target udp-test
    ctest --test-dir build --output-on-failure

# SEE ALSO

The VSCP specification: <https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_over_binary>
