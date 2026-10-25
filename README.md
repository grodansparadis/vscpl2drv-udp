# vscpl2drv-udp

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Linux Build](https://github.com/grodansparadis/vscpl2drv-udp/actions/workflows/build.yml/badge.svg) 
[![Project Status: Active – The project has reached a stable, usable state and is being actively developed.](https://www.repostatus.org/badges/latest/active.svg)](https://www.repostatus.org/#active)

Available for

  * Linux including Raspberry Pi (32-bit and 64 bit)
  * Windows
  * MacOS (x86_64 and arm64)

## VSCP level II UDP driver

- **Available for:** Linux (Debian,Raspberry Pi 64-bit (ARM64) and 32-bit (ARMHF))
- **Driver Linux**: libvscpl2drv-udp.so

This driver can act as a UDP client, a UDP server or both (the default).
It transports full Level II events using the [VSCP general binary protocol (VSCP-GBP)](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_over_binary).
OpenSSL provides AES-128/192/256-CBC encryption and optional HMAC-SHA256 authentication.

## Install the driver
You can find binary installation files [here](https://github.com/grodansparadis/vscpl2drv-udp/releases)

You install the driver using the debian package with

> sudo apt install ./vscpl2drv-udp_2026.10.2_amd64.deb

the driver will be installed to the folder _/var/lib/vscp/drivers/level2/_. 

After installing the driver you need to add configuration information for it to the vscpd.conf file (_/etc/vscp/vscpd.json_). Se the *configuration* section below.

You also need to set up the configuration file for the driver. If you don't need to dynamically edit the content of this file a good and safe location for it is in the */etc/vscp/* folder alongside the VSCP daemon configuration file.

If you need to do dynamic configuration (**write** enabled) we recommend that you create the file in the _/var/lib/vscp/_ folder or any location you find to be convenient.

A sample configuration file is available in _/usr/share/vscpl2drv-udp_ folder after installation. The sample configuration file is named udp.json. Copy this file to the folder you want it to be in (/etc/vscp) and edit it to suite your needs and the point tot the file from the driver setup in your master software (vscp-mqtt-daemon, vscp-tcpip-daemon or other.)


## How to build the driver

You need _build-essentials_ and _git_ installed on your system

```bash
sudo apt update && sudo apt -y upgrade
sudo apt install build-essential git cmake libssl-dev libexpat1-dev
```

To build this driver you to clone the driver source

```bash
git clone --recurse-submodules -j8 https://github.com/grodansparadis/vscpl2drv-udp.git
```

The build used **pandoc** for man-page generation so you want the man pages you should install it first with

```
sudo apt install pandoc
```

If you skip it, the build will warn and omit the man page.

Now go into the repository and build the driver

```
cd vscpl2drv-udp
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/
make
ctest --output-on-failure
sudo make install
```

You can add _-DCMAKE-DEBUG-TYPE=debug__ to the cmake line above if you want to build a version with ebug information.

Skip _"-DCMAKE_INSTALL_PREFIX=/"_ if you want the _"local"_ prefix to all install paths except for the driver.

If you want to generate binary packages issue

```bash
sudo cpack
```

Default install folder when you build from source is */var/lib/vscp/drivers/level2*. You can change this with the --prefix option in the make install step. For example 

```
make DESTDIR==/usr/local install
```

to install to */usr/local/var/lib/vscp/drivers/level2*.

### Build on macOS

```bash
brew install cmake openssl@3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" -DOPENSSL_USE_STATIC_LIBS=TRUE
cmake --build build
ctest --test-dir build --output-on-failure
```

### Build on Windows

Use Visual Studio 2022 and [vcpkg](https://vcpkg.io). Dependencies are listed in _vcpkg.json_ and are installed automatically during configuration. The static triplet gives a self-contained driver DLL.

```powershell
cmake -S . -B build -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix stage
```

The driver is installed as _drivers/level2/vscpl2drv-udp.dll_ below the selected prefix.

### Continuous integration artifacts

The GitHub Actions workflow builds, tests and packages the driver for every push and pull request:

| Platform | Artifact |
|----------|----------|
| Linux amd64 | `.deb` and `.tar.gz` |
| Raspberry Pi 32-bit (armhf, Debian/Raspberry Pi OS bookworm) | `.deb` and `.tar.gz` |
| Raspberry Pi 64-bit (arm64, Debian/Raspberry Pi OS bookworm) | `.deb` and `.tar.gz` |
| macOS arm64 and x86_64 | `.tar.gz` |
| Windows x64 | `.zip` |

Download them from the _Artifacts_ section of a workflow run.

## Configuration

#### VSCP tcp/ip daemon (vscp-tcpip-daemon) driver config

```json
{
  "enable": true,
  "name": "udp",
  "path-driver": "/path/to/driver/libvscpl2drv-udp.so",
  "path-config": "/etc/vscp/udp.json",
  "guid": "FF:FF:FF:FF:FF:FF:FF:F5:02:00:00:00:00:00:00:03"
}
```

##### enable
Set enable to "true" if the driver should be loaded by the VSCP daemon.

##### name
This is the name of the driver. Used when referring to it in different interfaces.

##### path-driver
This is the path to the driver. If you install from a Debian package this will be */var/lib/vscp/drivers/level2/libvscpl2drv-udp.so*.

##### path-config
This is the path to the driver configuration file (see below). This file determines the functionality of the driver. A good place for this file is in _/etc/vscp/udp.json_ It should be readable only by the user the VSCP daemon is run under (normally _vscp_) as it holds credentials to log in to a remote VSCP tcp/ip link interface. Never make it writable at this location.

##### guid
All level II drivers must have a unique GUID. There is many ways to obtain this GUID, Read more [here](https://grodansparadis.gitbooks.io/the-vscp-specification/vscp_globally_unique_identifiers.html). The tool [vscp_eth_to_guid](https://grodansparadis.github.io/vscp/#/configuring_the_vscp_daemon?id=think-before-guid) is a useful tool that is shipped with the VSCP daemon that will get you a unique GUID if you are working on a machine with an Ethernet interface.

#### VSCP MQTT daemon (vscp-mqtt-daemon) driver config

The VSCP MQTT daemon configuration file is (normally) located at */etc/vscp/vscpd.json* (For the curious: [VSCP daemon full sample config file for Linux](https://github.com/grodansparadis/vscp/blob/master/resources/linux/vscpd.json)). To use the vscpl2drv-udp.so driver there must be an entry in the drivers level2 section

```json
"drivers" : {
  "level2" : [
  ]
}
```

with the following format

```json
{
  "enable" : true,
  "name" : "udp",
  "path-driver" : "/path/do/driver/libvscpl2drv-udp.so",
  "path-config" : "/etc/vscp/udp.json",
  "guid" : "FF:FF:FF:FF:FF:FF:FF:F5:33:33:00:00:00:00:00:00",

  "mqtt": {
    "bind": "",
    "host": "test.mosquitto.org",
    "port": 1883,
    "mqtt-options": {
      "tcp-nodelay": true,
      "protocol-version": 311,
      "receive-maximum": 20,
      "send-maximum": 20,
      "ssl-ctx-with-defaults": 0,
      "tls-ocsp-required": 0,
      "tls-use-os-certs": 0
    },
    "user": "vscp",
    "password": "secret",
    "clientid": "the-vscp-daemon",
    "publish-format": "json",
    "subscribe-format": "auto",
    "qos": 1,
    "bcleansession": false,
    "bretain": false,
    "keepalive": 60,
    "bjsonmeasurementblock": true,
    "topic-daemon-base": "vscp-daemon/{{guid}}/",
    "topic-drivers": "drivers",
    "topic-discovery": "discovery",
    "reconnect": {
      "delay": 2,
      "delay-max": 10,
      "exponential-backoff": false
    },
    "tls": {
      "cafile": "",
      "capath": "",
      "certfile": "",
      "keyfile": "",
      "pwkeyfile": "",
      "no-hostname-checking": true,
      "cert-reqs": 0,
      "version": "",
      "ciphers": "",
      "psk": "",
      "psk-identity": ""
    },
    "will": {
      "topic": "vscp-daemon/{{srvguid}}/will",
      "qos": 1,
      "retain": true,
      "payload": "VSCP Daemon is down"
    },
    "subscribe" : [
      {
        "topic": "remote-vscp/{{guid}}/#",
        "qos": 0,
        "v5-options": 0,
        "format": "auto"
      }
    ],
    "publish" : [
      {
        "topic": "vscp/{{guid}}/{{class}}/{{type}}/{{nodeid}}",
        "qos": 1,
        "retain": false,
        "format": "json"
      }
    ]
  }
}
```

##### enable
Set enable to "true" if the driver should be loaded by the VSCP daemon.

##### name
This is the name of the driver. Used when referring to it in different interfaces.

##### path-driver
This is the path to the driver. If you install from a Debian package this will be */var/lib/vscp/drivers/level2/libvscpl2drv-udp.so*.

##### path-config
This is the path to the driver configuration file (see below). This file determines the functionality of the driver. A good place for this file is in _/etc/vscp/udp.json_ It should be readable only by the user the VSCP daemon is run under (normally _vscp_) as it holds credentials to log in to a remote VSCP tcp/ip link interface. Never make it writable at this location.

##### guid
All level II drivers must have a unique GUID. There is many ways to obtain this GUID, Read more [here](https://grodansparadis.gitbooks.io/the-vscp-specification/vscp_globally_unique_identifiers.html). The tool [vscp_eth_to_guid](https://grodansparadis.github.io/vscp/#/configuring_the_vscp_daemon?id=think-before-guid) is a useful tool that is shipped with the VSCP daemon that will get you a unique GUID if you are working on a machine with an Ethernet interface.

##### mqtt
See the [VSCP configuration documentation](https://grodansparadis.github.io/vscp/#/configuring_the_vscp_daemon?id=config-mqtt) for info about this section. It is common for all drivers.



## Configuring the driver

On startup the configuration is read from the path set in the driver
configuration of the VSCP daemon, usually `/etc/vscp/udp.json`. The file should
be readable by the driver user and protected against unauthorized modification.

The configuration file have the following format

```json
{
  "write" : false,
  "interface": "udp://255.255.255.255:33333",
  "flags": 0,

  "security" : {
    "key": "",
    "hmac": "",
    "encryption" : "none"
  },

  "logging": {
    "console-enable": true,
    "console-level": "trace",
    "console-pattern": "[vcpl2drv-udp %c] [%^%l%$] %v",
    "file-enable": true,
    "file-log-level": "debug",
    "file-log-path" : "/var/log/vscp/vscpl2drv-udp.log",
    "file-log-pattern": "[vcpl2drv-udp %c] [%^%l%$] %v",
    "file-log-max-size": 50000,
    "file-log-max-files": 7,
    "syslog-enable-log" : true,
    "syslog-log-level" : "info",
    "syslog-ident" : "vscpudp",
    "udp-enable-log": true,
    "udp-log-level" : "info",
    "udp-pattern" : "[vscpudp] [%^%l%$] %v",
    "udp-host" : "127.0.0.1",
    "udp-port" : 9999
  }, 
  
  "filter" : {
    "in-filter" : "0,0,0,-",
    "in-mask" : "0,0,0,-",
    "out-filter" : "0,0,0,-",
    "out-mask" : "0,0,0,-"
  }
}
```

### write
Reserved for runtime configuration support. Configuration is currently read only at
startup; changing the JSON file requires closing and reopening the driver.
The installed sample is `/usr/share/vscpl2drv-udp/udp.json`.

### guid
All level II drivers must have a unique GUID. There is many ways to obtain this GUID, Read more [here](https://grodansparadis.gitbooks.io/the-vscp-specification/vscp_globally_unique_identifiers.html).

### Interface

The destination URL, default `udp://255.255.255.255:33333`. IPv4, DNS names
and bracketed IPv6 addresses such as `udp://[::1]:33333` are supported.
The client sends to this address and port. The server listens on the wildcard
address of the resolved family at the same port. Client-only sockets
use an ephemeral source port. IPv4 broadcast is enabled; multicast group
membership is not implemented. No CAN interface or remote variable is used.
Only one server can bind a given local address/port.

### Flags
This is configuration flags for the UDP interface.

| Bit | Description |
| --- | ----------- |
| 0 | If set disable UDP client functionality |
| 1 | If set disable UDP server functionality |

Thus `0` enables both directions, `1` receives only, `2` sends only and `3`
disables both. Reads/writes in a disabled direction return
`CANAL_ERROR_NOT_SUPPORTED`. Writes send synchronously and do not take ownership
of the supplied event. The receive queue is bounded to 32,000 events.

### security

Encryption and authentication settings are grouped in the `security` object:

```json
{
  "security": {
    "key": "",
    "hmac": "",
    "encryption": "none"
  }
}
```

The object may be omitted or empty for plaintext operation. Legacy top-level
`key`, `hmac` and `encryption` settings are still accepted when `security` is
absent. Do not mix the two forms: the driver rejects configurations containing
both rather than silently overriding security settings. Restart/reopen the
driver after migrating a configuration.

#### security.key
Path to a file containing exactly 16/24/32 hexadecimal-encoded bytes for
AES-128/192/256 respectively (32/48/64 hex digits). Whitespace is allowed.
Required when encryption is enabled; there is no default key.

#### security.hmac
Path to a file containing a separate 32-byte HMAC-SHA256 key (64 hex digits).
An absent or empty path disables HMAC. When defined, authenticated version-1
packets are mandatory in both directions; unauthenticated packets are rejected.
HMAC with `encryption: "none"` fails startup. Missing, malformed or incorrectly
sized key files also fail startup. Keys are never logged.
When encryption is enabled, decoded keys are held in OpenSSL secure memory and
cleansed when the driver closes or releases them. Startup fails if secure memory
cannot be initialized; keys are not kept in ordinary heap memory as a fallback.

#### security.encryption
Encryption used.

| Value | Description |
| ----- | ----------- |
| none or empty | No encryption. |
| aes128 | AES-128 encryption. |
| aes192 | AES-192 encryption. |
| aes256 | AES-256 encryption. |

AES uses CBC mode, PKCS#7 padding and a fresh random 16-byte IV per packet.
OpenSSL supports all required algorithms, so libsodium is not needed.
Receivers enforce the configured encryption mode rather than trusting the
received packet's encryption nibble.



### Logging

Options for driver logging is set here.

#### console-enable 
Set to _true_ to log to the console.

#### console-level
Logging level for console log. Set to one of "off | critical | error | warn | info | debug | trace". 

#### console-pattern
The logging pattern for the console. The default is

```
"[vcpl2drv-udp] [%^%l%$] %v"
```
Patterns are described [here](https://spdlog.docsforge.com/v1.x/3.custom-formatting/#pattern-flags).

#### file-enable 
Set to _true_ to log to a rotating file. Disabled by default.

#### file-level
Set to one of "off | critical | error | warn | info | debug | trace" to set log level.

#### file-path
This is a writable path to a file that will get log information written to it. This can be a valuable to have if things does not behave as expected.

#### file-pattern
The logging pattern for the console. The default is

```
"[vcpl2drv-udp] [%^%l%$] %v"
```
Patterns are described [here](https://spdlog.docsforge.com/v1.x/3.custom-formatting/#pattern-flags).

#### file-max-size
Max size for log file before it will rotate and a new file is created. Default is 5 Mb.

#### file-max-files
Max number of log files to keep. Default is 7

The `file-log-level`, `file-log-path`, `file-log-pattern`, `file-log-max-size`
and `file-log-max-files` names in the example are also accepted and take
precedence over the corresponding `file-*` names.

#### syslog-enable-log
Set to _true_ to log to the system log (syslog). Disabled by default. Syslog is
not available on Windows; there the option is ignored and a warning is logged.

#### syslog-log-level
Set to one of "off | critical | error | warn | info | debug | trace" to set log level. Default is "info".

#### syslog-ident
The identity that log entries are tagged with in the system log. Default is
"vscpl2drv-udp". Syslog takes the timestamp and level from the system, so only
the message text is sent.

#### udp-enable-log
Set to _true_ to send each log message as a UDP datagram to a log collector.
Disabled by default.

#### udp-log-level
Set to one of "off | critical | error | warn | info | debug | trace" to set log level. Default is "info".

#### udp-pattern
The logging pattern for UDP log messages. The default is

```
"[vscpl2drv-udp] [%l] %v"
```
Patterns are described [here](https://spdlog.docsforge.com/v1.x/3.custom-formatting/#pattern-flags).

#### udp-host
IPv4 address or host name of the log collector. Host names are resolved once
when the driver is opened. Default is "127.0.0.1".

#### udp-port
UDP port of the log collector (1-65535). Default is 9999. Use a different port
than the one in _interface_ so log messages are not mixed with VSCP events.


### Filter
Main filter for incoming and outgoing events. Default is to send and receive all events. The truth table for VSCP filter/masks is described [here](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_decision_matrix?id=truth-table-for-filtermask).

Filter and mask is a way to select which events is received by the driver. A filter have the following format

> priority,vscpclass,vscptype,guid

All values can be give in decimal or hexadecimal (preceded number with '0x'). GUID is always given i hexadecimal (without preceded '0x').

**Default**: setting is

> 0,0,0,00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00

Read the [vscpd manual](http://grodansparadis.github.io/vscp/#/) for more information about how filter/masks work.

The default filter/mask pair means that all events are received by the driver.

#### mask
Filter and mask is a way to select which events is received by the driver. A mask have the following format

> priority,vscpclass,vscptype,guid

All values can be give in decimal or hexadecimal (preceded number with '0x'). GUID is always given i hexadecimal (without preceded '0x').

The mask have a binary one ('1') in the but position of the filter that should have a specific value and zero ('0') for a don't care bit.

Default setting is

> 0,0,0,00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00

Read the vscpd manual for more information about how filter/masks work.

The default filter/mask pair means that all events are received by the driver.

#### in-filter
Incoming filter on string form. The string filter has the form _"filter-priority, filter-class, 
    filter-type, filter-GUID"_

#### in-mask
Incoming mask on string form. The string mask has the form _"mask-priority, 
    mask-class, mask-type, mask-GUID”_.

#### out-filter
Outgoing filter on string form. The string filter has the form _"filter-priority, filter-class, 
    filter-type, filter-GUID"_

#### out-mask
Outgoing mask on string form. The string mask has the form _"mask-priority, 
    mask-class, mask-type, mask-GUID”_.


## Using the vscpl2drv-udp driver

For a local loopback test, configure `interface` as `udp://127.0.0.1:33333`,
`flags` as `0`, and open the driver through its Level II API. Events submitted
with `VSCPWrite` can then be received with `VSCPRead`.

For two processes on the same host, set the sender's `flags` to `2` and the
receiver's to `1`, both using the same destination port. For two hosts, use the
remote host's address as the sender destination and allow the UDP port through
the receiver's firewall.

To enable authenticated AES-256, create independent keys in a private directory:

```bash
umask 077
openssl rand -hex 32 > vscp.key
openssl rand -hex 32 > hmac.key
```

Set `security.encryption` to `aes256` and set `security.key` and `security.hmac` to the absolute paths of
these files. Both peers must use the same encryption and HMAC keys. For AES-128
or AES-192 use `openssl rand -hex 16` or `openssl rand -hex 24` for the AES key;
the HMAC key is always 32 bytes. Protect key files so only the driver user can
read them.

The automated loopback tests need no root privileges or CAN hardware:

```bash
cmake --build build --target udp-test
ctest --test-dir build --output-on-failure
```

### Wire format

One binary event is carried per UDP datagram. Outgoing events use binary event
format 1 (nanosecond timestamps); incoming event formats 0 and 1 are supported.
The GUID, class, type and full event payload are preserved without CAN's
eight-byte or Level I class restrictions.

Without HMAC the legacy UDP layout is `[type][payload + CRC]`, or with AES,
`[type][ciphertext][IV:16]`, with no stream prefix.

With HMAC the layout is
`[0x55][version:3 | length:13, big-endian][type][ciphertext][IV:16][tag:16]`.
Version is 1 and length includes type, ciphertext, IV and tag, not the prefix.
The tag is the leftmost 16 bytes of
`HMAC-SHA256(prefix || type || ciphertext || IV)`. Authentication is checked
in constant time before decryption; padding and the plaintext CRC are then
validated. Packets with incorrect lengths, CRCs, tags or encryption settings
are discarded and logged.

AES without HMAC provides confidentiality only. UDP provides no delivery,
ordering or replay guarantees, including when HMAC is enabled. Use application
sequence numbers and acknowledgements where required.

### Sending encrypted or unencrypted events

As a driver user, you select the wire format in the **driver's JSON
configuration's `security` object**, not in individual events. Your application or VSCP daemon sends
normal VSCP events; the driver handles serialization, CRC, encryption, padding,
IV generation and optional HMAC automatically. Do not pass pre-encrypted frames
as event data.

#### Unencrypted events

Save this complete configuration as `send-plain.json`:

```json
{
  "interface": "udp://127.0.0.1:33333",
  "flags": 2,
  "security": {
    "encryption": "none",
    "key": "",
    "hmac": ""
  },
  "logging": { "console-enable": false }
}
```

Select its absolute path in the daemon's `path-config` and restart/reopen the
driver. Send events exactly as in the encrypted case. No key files are needed.
The CRC remains present, but the payload is readable on the network and is not
cryptographically authenticated. **A nonempty `hmac` with `encryption: "none"`
is invalid and prevents startup.**

#### AES encryption with HMAC authentication

For authenticated AES-256, generate two independent key files. On Linux/macOS:

```bash
umask 077
mkdir -p private
openssl rand -hex 32 > private/vscp.key
openssl rand -hex 32 > private/hmac.key
```

These are hexadecimal text files, each containing 64 hex digits. Store them in
a protected location readable by the account running the driver. Securely
provision identical key material on the receiver; do not generate different
keys there.

Save this complete configuration as `send-auth.json`, replacing the key paths
with actual absolute paths:

```json
{
  "interface": "udp://127.0.0.1:33333",
  "flags": 2,
  "security": {
    "encryption": "aes256",
    "key": "/absolute/path/private/vscp.key",
    "hmac": "/absolute/path/private/hmac.key"
  },
  "logging": { "console-enable": false }
}
```

`flags: 2` selects send-only operation. Replace `127.0.0.1` with the receiver's
address for another host. Use `flags: 0` if the instance must also receive;
that instance will bind the configured local port.

In the VSCP daemon's driver entry, set `path-driver` to the installed UDP driver
library and `path-config` to the absolute path of `send-auth.json`. Enable the
entry and restart/reopen the driver. Publish ordinary VSCP events using the
daemon's event interface, ensuring they are routed to this driver. For a custom
Level II host, open this configuration through `VSCPOpen`, submit events with
`VSCPWrite`, and close with `VSCPClose`.

For AES-128 or AES-192, set `security.encryption` to `aes128` or `aes192` and generate the
AES key with `openssl rand -hex 16` or `openssl rand -hex 24`, respectively.
The **separate HMAC key is always 32 bytes**. AES without HMAC is supported by
omitting `security.hmac` or setting it to an empty string, but provides confidentiality
only, not cryptographic authentication.



#### Verify either mode without a daemon

Build the CLI and driver using the instructions below. For the authenticated
case, copy `send-auth.json` to `receive-auth.json` and change only `flags` to `1`.
Start the standalone receiver in terminal 1:

```bash
./build/vscp-udp-peer receive --config receive-auth.json --count 1 --timeout 60000
```

To keep waiting without a deadline, replace `--timeout 60000` with
`--timeout infinite`. This still exits after receiving one event; press Ctrl+C
to stop before an event arrives.

Wait for `UDP peer ready (receive)`, then send through the actual driver in
terminal 2:

```bash
./build/vscp-udp-peer send --driver ./build/libvscpl2drv-udp.so \
  --config send-auth.json --class 10 --type 6 --data 01,02,ff --count 1
```

For the plaintext test, copy `send-plain.json` to `receive-plain.json`, change
`flags` to `1`, and repeat those commands with the corresponding plaintext
filenames. Close the previous receiver/driver before starting another receiver
on the same local port. The receiver prints a JSON event with class 10, type 6
and data `[1,2,255]`; both commands exit after one event.

On macOS use `./build/libvscpl2drv-udp.dylib`. On Windows use
`.\build\Release\vscp-udp-peer.exe` and
`--driver .\build\Release\vscpl2drv-udp.dll`, with each command on one line.
Use forward slashes in Windows JSON key paths, for example `C:/private/vscp.key`.

Both endpoints must agree on encryption mode and HMAC use, and have identical
key contents when enabled. Configuration and keys are read at startup: close
and reopen the driver after changing either. Sending successfully does not
acknowledge delivery; check the receiver and allow UDP port 33333 through its
firewall.

For the example's three data bytes, the driver emits:

| Mode | UDP payload | Bytes |
|------|-------------|-------|
| Unencrypted | `[type=0x10][event body including CRC:40]` | 41 |
| AES-256 with HMAC | `[0x55][0x20][0x51][type=0x13][ciphertext:48][IV:16][tag:16]` | 84 |

The authenticated length word includes the clear type, ciphertext, IV and tag;
the driver authenticates the prefix as well. Plaintext packets have no prefix,
padding, IV or HMAC. See [Wire format](#wire-format) for the general layout and
the [binary protocol specification](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_over_binary)
for byte-by-byte offsets.

### Standalone UDP test peer

`vscp-udp-peer` has two modes. By default it sends and receives UDP events without
loading the driver shared library. With `--driver FILE` it loads a driver at
runtime and sends/receives through its exported Level II API instead. You can
run one instance in each mode to test the actual driver without a VSCP daemon.
The standalone transport shares the driver's transport and crypto implementation,
so it is an interoperability/exercising tool, not an independent protocol
implementation. IPv4, IPv6, AES-128/192/256 and optional HMAC are
supported, using the same configuration format and key files as the driver.

#### Modes and command-line reference

```text
vscp-udp-peer send|receive --config FILE [--driver LIBRARY] [options]
```

| Mode | Selection | Event path |
|------|-----------|------------|
| Standalone UDP peer | Omit `--driver` | CLI uses its built-in UDP transport. No driver library is opened. |
| Driver host | Add `--driver LIBRARY` | CLI loads the selected library, calls `VSCPOpen`, then `VSCPWrite` or `VSCPRead`, and finally `VSCPClose`. |

Use one process in each mode to test the library against the standalone peer.
Both modes use the same options and configuration files; `--driver` selects a
local shared library, **not** a network address or configuration file.

| Option | Applies to | Default / accepted values |
|--------|------------|---------------------------|
| `--config FILE` | Both commands/modes | Required; driver-format JSON configuration. |
| `--driver LIBRARY` | Both commands | Optional path to a `.so`, `.dylib` or `.dll`; omission selects standalone UDP. |
| `--count N` | Both commands/modes | 1; range 1-1000000. |
| `--timeout MS` | Both commands/modes | 5000; range 0-3600000 milliseconds per operation, or `infinite` to wait without a deadline. |
| `--interval MS` | Send only | 1000; range 0-3600000 milliseconds between events. |
| `--class N` | Send only | 10; range 0-65535. |
| `--type N` | Send only | 6; range 0-65535. |
| `--guid GUID` | Send only | All zeros; 16 colon-separated two-digit hexadecimal bytes. |
| `--data HEX` | Send only | Empty payload; comma-separated two-digit hexadecimal bytes, e.g. `01,02,ff`. |
| `--help` | Used alone | Print usage and exit successfully. |

Options take a separate value, e.g. `--count 5`, not `--count=5`. Repeated
options and send-only options passed to `receive` are rejected. Counts are
finite: the process exits after the requested operations or the first error.
CLI diagnostics (including the loaded driver's version) go to stderr;
received events go to stdout as JSON lines. The driver's configured logging
sinks remain active, so disable console logging for clean JSON output.

Use `--timeout infinite` to wait indefinitely for each event, in either mode:

```bash
./build/vscp-udp-peer receive --config receive.json --count 5 --timeout infinite
```

The same option works when receiving through the driver library:

```bash
./build/vscp-udp-peer receive --driver ./build/libvscpl2drv-udp.so \
  --config receive.json --count 5 --timeout infinite
```

This retries timed-out operations but still exits on other errors and after
`--count` events. Stop the process with Ctrl+C if no events arrive.
Numeric `--timeout 0` still means an immediate attempt, not an infinite wait.

You can of course also use [vscp-works](https://github.com/grodansparadis/vscp-works-qt) for this.

#### 1. Build or install the application

It is built by default, installed in `bin`, and included in CI packages.
From the repository root, configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target vscp-udp-peer
./build/vscp-udp-peer --help
```

Install the dependencies described in [How to build the driver](#how-to-build-the-driver)
first. On macOS, use the OpenSSL options in that section when configuring.
On Windows, configure with the vcpkg toolchain as described above, then run:

```powershell
cmake --build build --config Release --target vscp-udp-peer
.\build\Release\vscp-udp-peer.exe --help
```

The commands below assume a Linux/macOS build and are run from the repository
root. On Windows substitute `.\build\Release\vscp-udp-peer.exe` for
`./build/vscp-udp-peer`; enter multi-line commands on one line or use PowerShell
backticks instead of Bash backslashes. If installed, use `vscp-udp-peer` directly
(on Windows, add the package's `bin` directory to PATH or use its full path).
To omit the application from a build, configure with `-DBUILD_UDP_PEER=OFF`.

#### 2. Create sender and receiver configurations

Save the following as `receive.json` in your working directory:

```json
{
  "interface": "udp://127.0.0.1:33333",
  "flags": 1,
  "security": { "encryption": "none" },
  "logging": { "console-enable": false }
}
```

Save this as `send.json` in the same directory:

```json
{
  "interface": "udp://127.0.0.1:33333",
  "flags": 2,
  "security": { "encryption": "none" },
  "logging": { "console-enable": false }
}
```

These configurations work for both the peer and the driver. `flags: 1` disables
sending, while `flags: 2` disables receiving. The receiver binds port 33333; the
sender uses an ephemeral source port. No keys are needed for this initial test.

#### 3. Check the setup using two terminals

Before connecting the driver, check the peer and local networking independently
of the daemon. Stop any driver instance already listening on port 33333.
In terminal 1 start the receiver and wait for `UDP peer ready (receive)`:

```bash
./build/vscp-udp-peer receive --config receive.json --count 5 --timeout 60000
```

In terminal 2 send five events:

```bash
./build/vscp-udp-peer send --config send.json --count 5 --interval 1000 \
  --class 10 --type 6 --data 01,02,ff \
  --guid FF:FF:FF:FF:FF:FF:FF:F5:02:00:00:00:00:00:00:03
```

Terminal 1 prints five JSON lines and exits successfully. Each line contains
`"class":10`, `"type":6`, `"data":[1,2,255]`, the supplied GUID and a fresh
`timestamp_ns`. Terminal 2 reports the five sends on stderr.
`--data` accepts comma-separated two-digit hexadecimal bytes (omit it for an
empty payload). The peer assigns a fresh nanosecond timestamp to every event.

#### 4. Test the driver in both directions

##### Without a daemon: load the driver from the CLI

Use the `send.json` and `receive.json` files from step 2. Build both the library
and the CLI first:

```bash
cmake --build build --target vscpl2drv-udp vscp-udp-peer
```

**Peer to loaded driver:** in terminal 1, load the actual driver as the receiver:

```bash
./build/vscp-udp-peer receive --driver ./build/libvscpl2drv-udp.so \
  --config receive.json --count 5 --timeout 60000
```

Wait for `Driver ready (receive)`. In terminal 2, send from the standalone UDP
transport (no `--driver`):

```bash
./build/vscp-udp-peer send --config send.json --count 5 \
  --class 10 --type 6 --data 01,02,ff
```

Terminal 1 prints the events obtained through the driver's `VSCPRead`.
Both processes exit after five events.

**Loaded driver to peer:** start the standalone receiver in terminal 1:

```bash
./build/vscp-udp-peer receive --config receive.json --count 5 --timeout 60000
```

Wait for `UDP peer ready (receive)`. In terminal 2, send through the driver:

```bash
./build/vscp-udp-peer send --driver ./build/libvscpl2drv-udp.so \
  --config send.json --count 5 --class 10 --type 6 --data 01,02,ff
```

This calls `VSCPWrite` on the loaded library; the other process receives and
prints the resulting UDP events. No daemon is needed for either test.

On macOS replace the library path with `./build/libvscpl2drv-udp.dylib`.
On Windows use `.\build\Release\vscp-udp-peer.exe` and
`--driver .\build\Release\vscpl2drv-udp.dll`. For installed packages, pass the
full path to the installed driver. Use a trusted library built for the same
architecture and compatible VSCP event ABI as the CLI.

The loader checks `VSCPOpen`, `VSCPClose`, `VSCPRead`, `VSCPWrite` and
`VSCPGetVersion`, reports the version, opens the selected config, and closes the
driver before unloading it. It passes an all-zero interface GUID to `VSCPOpen`;
the send-only `--guid` option sets the **event's** GUID, as in standalone mode.
Load failures, missing exports, open/read/write/close failures and timeouts
produce diagnostics on stderr and a nonzero exit status. There is no fallback
to the standalone transport if loading fails.

##### Through a VSCP daemon or another host application

**Peer to driver:** stop the peer receiver from the previous step. In the VSCP
daemon's driver entry, set `path-config` to the absolute path of `receive.json`
and `path-driver` to the installed driver library. Enable the entry and restart
the daemon (or close/reopen the driver in your host application). See the daemon
configuration examples above. Run the sender command from step 3. Read the five
events through the host application's Level II `VSCPRead` API or the daemon's
event monitoring client. Check the class, type, GUID and payload.

**Driver to peer:** stop or close that driver instance first so it releases the
receive port. Start the peer receiver:

```bash
./build/vscp-udp-peer receive --config receive.json --count 5 --timeout 60000
```

Then change the daemon's `path-config` to the absolute path of `send.json` and
restart/reopen the driver. Submit five events through the host application's
Level II `VSCPWrite` API or the daemon's event publishing client, ensuring the
daemon routes them to this driver.
The peer prints one JSON object per received event, containing `head`, `class`,
`type`, `guid`, `timestamp_ns` and decimal-byte `data`. Disable console logging
in the peer configuration to keep stdout as JSON only. Application diagnostics
go to stderr. The timeout is in milliseconds **per event**, or `infinite` for
no deadline; with a numeric timeout, expiration produces a nonzero exit code. Disabled
direction, invalid arguments or configuration errors produce a nonzero exit code.
A successful send means the datagram was submitted, not acknowledged by the
remote driver. Configured input/output filters still apply.

For network tests, use the receiving machine's address in the sender's
`interface` and allow UDP port 33333 through its firewall. Only one process may
bind a given local port; use separate hosts or the receive-only/send-only flag
settings above rather than two bidirectional sockets on the same host.

#### 5. Enable encryption and authentication

For AES testing, set `security.encryption` and `security.key` identically in both configurations.
Set `security.hmac` to the same separate key file on both sides to test authentication;
omit it on both sides to test unauthenticated AES. Key generation is described
above. For example, merge these fields into **both** configurations, replacing
the paths with absolute paths to your own key files:

```json
{
  "security": {
    "encryption": "aes256",
    "key": "/absolute/private/path/vscp.key",
    "hmac": "/absolute/private/path/hmac.key"
  }
}
```

This fragment is not a complete configuration: keep the `interface`, `flags`
and `logging` fields from step 2. On Windows use forward slashes in JSON paths,
for example `C:/private/vscp.key`. For two hosts, securely copy identical key
material to both machines; their local file paths may differ. Restart both
endpoints after changing configuration or key files. Do not enable `hmac` with
`encryption: "none"`.

The automated loopback suite also launches standalone and driver-loading CLI
processes together in both directions for all seven encryption/authentication
modes, and checks loader failures and receive timeouts.

#### Troubleshooting

| Symptom | Check |
|---------|-------|
| Configuration cannot be opened | Run from the directory containing the JSON files, or pass an absolute path. The daemon also needs permission to read its config and keys. |
| Driver cannot be loaded / missing export | Pass the full path to the driver library, not the CLI. Check its architecture, dependencies and Level II API exports. |
| Bind failure / address already in use | Only one receiver may bind port 33333 on the same address. Stop the other receiver or use a different port at both endpoints. |
| Receive timeout | Start the receiver first, check the sender's destination address/port and firewall, and verify the host routes events to/from the driver. Increase `--timeout` or use `--timeout infinite` to wait without a deadline. |
| Disabled-direction error | Use `flags: 2` for sending and `flags: 1` for receiving. |
| Invalid frame / authentication / CRC warnings | Match encryption modes and key contents on both endpoints; either both must configure HMAC or neither must. |
| Missing events | Check driver input/output filters. UDP delivery is not guaranteed. A successful send is not a delivery acknowledgement. |
| Extra text mixed with JSON | Set `logging.console-enable` to `false` in the peer config; application diagnostics remain on stderr. |

To inspect discarded-packet warnings, temporarily enable console logging on the
receiving endpoint. To save JSON results while keeping diagnostics visible:

```bash
./build/vscp-udp-peer receive --config receive.json --count 5 --timeout 60000 > events.jsonl
```


---

## Other sources with information

  * [Multicast driver implementation guide](docs/multicast-driver-design.md) -
    proposed configuration, socket changes, platform differences and acceptance
    tests for creating a separate multicast driver from this UDP driver.
  * The VSCP site - https://www.vscp.org
  * The VSCP document site - https://docs.vscp.org/
  * VSCP discussions - https://github.com/grodansparadis/vscp/discussions

---

## Copyright
Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project - MIT license.
