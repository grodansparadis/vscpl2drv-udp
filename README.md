# vscpl2drv-udp

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Linux Build](https://github.com/grodansparadis/vscpl2drv-udp/actions/workflows/build.yml/badge.svg) 
[![Project Status: Active – The project has reached a stable, usable state and is being actively developed.](https://www.repostatus.org/badges/latest/active.svg)](https://www.repostatus.org/#active)

VSCP UDP level II driver

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
  "debug" : true,
  "write" : false,
  "interface": "udp://255.255.255.255:9999",
  "flags": 0,
  "key": "",
  "hmac": "",
  "encryption" : "none",
  "logging": {
    "console-enable": true,
    "console-level": "trace",
    "console-pattern": "[vcpl2drv-udp %c] [%^%l%$] %v",
    "file-enable": true,
    "file-log-level": "debug",
    "file-log-path" : "/var/log/vscp/vscpl2drv-udp.log",
    "file-log-pattern": "[vcpl2drv-udp %c] [%^%l%$] %v",
    "file-log-max-size": 50000,
    "file-log-max-files": 7
  }, 
  "filter" : {
    "in-filter" : "0,0,0,-",
    "in-mask" : "0,0,0,-",
    "out-filter" : "0,0,0,-",
    "out-mask" : "0,0,0,-"
  }
}
```

### debug
Set debug to _true_ to get debug information written to the log file. This can be a valuable help if things does not behave as expected.

### write
Reserved for runtime configuration support. Configuration is currently read only at
startup; changing the JSON file requires closing and reopening the driver.
The installed sample is `/usr/share/vscpl2drv-udp/udp.json`.

### guid
All level II drivers must have a unique GUID. There is many ways to obtain this GUID, Read more [here](https://grodansparadis.gitbooks.io/the-vscp-specification/vscp_globally_unique_identifiers.html).

### Interface

The destination URL, default `udp://255.255.255.255:9999`. IPv4, DNS names
and bracketed IPv6 addresses such as `udp://[::1]:9999` are supported.
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

### key
Path to a file containing exactly 16/24/32 hexadecimal-encoded bytes for
AES-128/192/256 respectively (32/48/64 hex digits). Whitespace is allowed.
Required when encryption is enabled; there is no default key.

### hmac
Path to a file containing a separate 32-byte HMAC-SHA256 key (64 hex digits).
An absent or empty path disables HMAC. When defined, authenticated version-1
packets are mandatory in both directions; unauthenticated packets are rejected.
HMAC with `encryption: "none"` fails startup. Missing, malformed or incorrectly
sized key files also fail startup. Keys are never logged.

### encryption
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

#### Wire format

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
precedence over the corresponding `file-*` names. Console and rotating-file
logging are supported; syslog and UDP logging are not implemented.


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

For a local loopback test, configure `interface` as `udp://127.0.0.1:9999`,
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

Set `encryption` to `aes256` and set `key` and `hmac` to the absolute paths of
these files. Both peers must use the same encryption and HMAC keys. For AES-128
or AES-192 use `openssl rand -hex 16` or `openssl rand -hex 24` for the AES key;
the HMAC key is always 32 bytes. Protect key files so only the driver user can
read them.

The automated loopback tests need no root privileges or CAN hardware:

```bash
cmake --build build --target udp-test
ctest --test-dir build --output-on-failure
```


---

## Other sources with information

  * The VSCP site - https://www.vscp.org
  * The VSCP document site - https://docs.vscp.org/
  * VSCP discussions - https://github.com/grodansparadis/vscp/discussions

---

## Copyright
Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project - MIT license.
