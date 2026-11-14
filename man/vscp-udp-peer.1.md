% VSCP-UDP-PEER(1) VSCP UDP peer
% Åke Hedman, Grodans Paradis AB
% November 2026

# NAME

vscp-udp-peer - send or receive VSCP events with the vscpl2drv-udp transport

# SYNOPSIS

**vscp-udp-peer** **send** **\-\-config** *FILE* [*options*]

**vscp-udp-peer** **receive** **\-\-config** *FILE* [*options*]

# DESCRIPTION

**vscp-udp-peer** is a small stand-alone program that uses the UDP code of
the **vscpl2drv-udp** driver. It sends or receives VSCP events with the same
JSON configuration file as the driver, including AES encryption and HMAC
authentication. It is used to test a driver setup, or another VSCP UDP
node, without a VSCP daemon.

With **\-\-driver** it loads a VSCP Level II driver (for example the
vscpl2drv-udp library itself) and uses its Level II API instead of the built-in
transport.

Received events are printed on standard output as JSON, one event per line.
Diagnostics go to standard error. Set **logging.console-enable** to false in
the configuration file for clean JSON output.

# COMMANDS

**send**
:   Send **\-\-count** events (default one), **\-\-interval** milliseconds apart.

**receive**
:   Wait for **\-\-count** events (default one) and print them.

# OPTIONS

**\-\-config** *FILE*
:   The driver's JSON configuration file. Use **flags** 2 in a sending
    configuration and **flags** 1 in a receiving one. AES and the optional
    HMAC are set with **security.encryption**, **security.key** and
    **security.hmac**.

**\-\-driver** *FILE*
:   Load this driver (.so, .dylib or .dll) and use its Level II API instead of
    the built-in UDP transport.

**\-\-count** *N*
:   Number of events to send or receive (default 1).

**\-\-timeout** *MS*
:   Time to wait for each event, 0 to 3600000 milliseconds or **infinite**
    (default 5000).

**\-\-class** *N*
:   VSCP class of sent events, 0 to 65535 (default 10).

**\-\-type** *N*
:   VSCP type of sent events, 0 to 65535 (default 6).

**\-\-guid** *GUID*
:   GUID of sent events, 16 colon separated hex bytes (default all zero).

**\-\-data** *HEX*
:   Data of sent events as comma separated hex bytes, for example 89,02,00,e6.

**\-\-interval** *MS*
:   Delay between sent events, 0 to 3600000 milliseconds (default 1000).

**-h**, **\-\-help**
:   Show the options.

# EXIT STATUS

0 when all events were sent or received, non-zero on errors or when an event
did not arrive in time.

# EXAMPLES

Wait for one event:

    vscp-udp-peer receive --config receive.json --timeout 10000

Send three temperature measurements, one second apart:

    vscp-udp-peer send --config send.json --count 3 --class 10 --type 6 --data 89,82,00,e6

Use the driver library instead of the built-in transport:

    vscp-udp-peer receive --config receive.json --driver /var/lib/vscp/drivers/level2/libvscpl2drv-udp.so

# SEE ALSO

**vscpl2drv-udp**(1), **vscp-dump**(1), **vscp-send**(1), **vscpd**(8)

The source code and the full documentation:
<https://github.com/grodansparadis/vscpl2drv-udp>
