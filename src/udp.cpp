// Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "udp.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <canal.h>
#include <crc.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/udp_sink.h>
#ifndef _WIN32
#include <spdlog/sinks/syslog_sink.h>
#include <syslog.h>
#endif

namespace {
// Largest datagram handled: frame header + CRC (38) + max data, plus worst-case
// AES padding/IV/HMAC tag (48) and the 3-byte authenticated packet prefix.
constexpr size_t maxPacket = 38 + VSCP_LEVEL2_MAXDATA + 48 + 3;

// Max number of received events buffered before new ones are dropped.
constexpr size_t maxQueue = 32000;

// Keep the process-wide OpenSSL secure heap small enough for common
// RLIMIT_MEMLOCK defaults while allowing many driver instances.
constexpr size_t secureHeapSize = 16 * 1024;

bool
initializeSecureHeap()
{
  static std::once_flag initialized;
  static bool available = false;
  std::call_once(initialized, [] {
    available = CRYPTO_secure_malloc_initialized() || CRYPTO_secure_malloc_init(secureHeapSize, 32) == 1;
  });
  return available || CRYPTO_secure_malloc_initialized();
}

SecureKey
allocateSecureKey()
{
  auto *key = static_cast<uint8_t *>(OPENSSL_secure_malloc(32));
  if (!key || !CRYPTO_secure_allocated(key)) {
    if (key) {
      OPENSSL_clear_free(key, 32);
    }
    throw std::runtime_error("Cannot allocate key in OpenSSL secure memory");
  }
  std::memset(key, 0, 32);
  return SecureKey(key);
}

// Thin platform layer so the driver code below is identical for Winsock and
// BSD sockets.

#ifdef _WIN32
constexpr udp_socket_t invalidSocket = INVALID_SOCKET;
using pollDescriptor                 = WSAPOLLFD;

///////////////////////////////////////////////////////////////////////////////
// socketErrorCode
//
// Return the last Winsock error for the calling thread.
//

int
socketErrorCode()
{
  return WSAGetLastError();
}

///////////////////////////////////////////////////////////////////////////////
// wouldBlock
//
// Return true for transient errors where the operation should be retried.
// WSAECONNRESET is included because Windows reports an ICMP "port
// unreachable" caused by an earlier send on the next receive call, which
// must not stop the receiver.
//

bool
wouldBlock(int error)
{
  return error == WSAEWOULDBLOCK || error == WSAEINTR || error == WSAECONNRESET;
}

///////////////////////////////////////////////////////////////////////////////
// socketErrorText
//
// Return a readable description of a Winsock error code.
//

std::string
socketErrorText(int error)
{
  return std::system_category().message(error);
}

///////////////////////////////////////////////////////////////////////////////
// pollSocket
//
// Wait up to timeout ms for the events requested in descriptor.
// Returns >0 when ready, 0 on timeout and <0 on error.
//

int
pollSocket(pollDescriptor &descriptor, int timeout)
{
  return WSAPoll(&descriptor, 1, timeout);
}

///////////////////////////////////////////////////////////////////////////////
// closeSocket
//
// Close a socket handle.
//

void
closeSocket(udp_socket_t socket)
{
  closesocket(socket);
}

///////////////////////////////////////////////////////////////////////////////
// initSockets
//
// Start Winsock 2.2. Done once per process; the result is cached so later
// calls are cheap. Returns false if Winsock is unavailable.
//

bool
initSockets()
{
  static const bool initialized = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return initialized;
}

///////////////////////////////////////////////////////////////////////////////
// configureSocket
//
// Put the socket in non-blocking mode so send/receive never stall the
// caller; waiting is done explicitly with pollSocket().
//

bool
configureSocket(udp_socket_t socket)
{
  u_long enabled = 1;
  return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
#else
constexpr udp_socket_t invalidSocket = -1;
using pollDescriptor                 = pollfd;

///////////////////////////////////////////////////////////////////////////////
// socketErrorCode
//
// Return the last socket error (errno) for the calling thread.
//

int
socketErrorCode()
{
  return errno;
}

///////////////////////////////////////////////////////////////////////////////
// wouldBlock
//
// Return true for transient errors where the operation should be retried.
//

bool
wouldBlock(int error)
{
  return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

///////////////////////////////////////////////////////////////////////////////
// socketErrorText
//
// Return a readable description of an errno value.
//

std::string
socketErrorText(int error)
{
  return std::generic_category().message(error);
}

///////////////////////////////////////////////////////////////////////////////
// pollSocket
//
// Wait up to timeout ms for the events requested in descriptor.
// Returns >0 when ready, 0 on timeout and <0 on error.
//

int
pollSocket(pollDescriptor &descriptor, int timeout)
{
  return poll(&descriptor, 1, timeout);
}

///////////////////////////////////////////////////////////////////////////////
// closeSocket
//
// Close a socket descriptor.
//

void
closeSocket(udp_socket_t socket)
{
  ::close(socket);
}

///////////////////////////////////////////////////////////////////////////////
// initSockets
//
// No socket library initialization is needed on POSIX systems.
//

bool
initSockets()
{
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// configureSocket
//
// Make the socket non-blocking and close-on-exec so it is not inherited by
// child processes started by the host application.
//

bool
configureSocket(udp_socket_t socket)
{
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0 && fcntl(socket, F_SETFD, FD_CLOEXEC) == 0;
}
#endif

///////////////////////////////////////////////////////////////////////////////
// deleteEvent
//
// Free an event allocated with new together with its data buffer.
// vscp_deleteEvent() only frees the data, so the _v2 variant is used.
//

void
deleteEvent(vscpEvent *event)
{
  vscp_deleteEvent_v2(&event);
}

///////////////////////////////////////////////////////////////////////////////
// readKey
//
// Read a key stored as hexadecimal text from path into key. Whitespace and
// line breaks are ignored, but the file must contain exactly bytes * 2 hex
// digits. Hexadecimal characters are decoded directly into secure memory.
// Throws std::runtime_error if the file is missing or malformed.
//

void
readKey(const std::string &path, uint8_t *key, size_t bytes)
{
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Cannot open key file: " + path);
  }
  size_t digits      = 0;
  uint8_t highNibble = 0;
  char character     = 0;
  while (input.get(character)) {
    const auto value = static_cast<unsigned char>(character);
    if (!std::isspace(value)) {
      if (!std::isxdigit(value) || digits >= bytes * 2) {
        OPENSSL_cleanse(&character, sizeof(character));
        OPENSSL_cleanse(key, bytes);
        throw std::runtime_error("Invalid hexadecimal key length or content in: " + path);
      }
      const uint8_t nibble = static_cast<uint8_t>(std::isdigit(value) ? value - '0' : std::tolower(value) - 'a' + 10);
      if ((digits & 1) == 0) {
        highNibble = nibble;
      }
      else {
        key[digits / 2] = static_cast<uint8_t>((highNibble << 4) | nibble);
      }
      ++digits;
    }
    OPENSSL_cleanse(&character, sizeof(character));
  }
  OPENSSL_cleanse(&highNibble, sizeof(highNibble));
  if (input.bad() || digits != bytes * 2) {
    OPENSSL_cleanse(key, bytes);
    throw std::runtime_error("Invalid hexadecimal key length or content in: " + path);
  }
}

///////////////////////////////////////////////////////////////////////////////
// logLevel
//
// Convert a level name from the configuration (off, critical, error, warn,
// info, debug, trace) to an spdlog level. "error" is accepted as an alias
// for spdlog's "err". Throws std::runtime_error for unknown names.
//

spdlog::level::level_enum
logLevel(const std::string &name)
{
  if (name == "error")
    return spdlog::level::err;
  for (auto level : { spdlog::level::off,
                      spdlog::level::critical,
                      spdlog::level::err,
                      spdlog::level::warn,
                      spdlog::level::info,
                      spdlog::level::debug,
                      spdlog::level::trace }) {
    if (name == spdlog::level::to_string_view(level))
      return level;
  }
  throw std::runtime_error("Invalid logging level: " + name);
}

///////////////////////////////////////////////////////////////////////////////
// resolveIPv4
//
// Resolve a host name or address to a numeric IPv4 address string. The
// spdlog UDP sink only accepts numeric IPv4 addresses, so DNS names are
// resolved here. Throws std::runtime_error if the host can't be resolved.
//

std::string
resolveIPv4(const std::string &host)
{
  if (!initSockets()) {
    throw std::runtime_error("Cannot initialize socket library");
  }
  addrinfo hints{};
  hints.ai_family     = AF_INET;
  hints.ai_socktype   = SOCK_DGRAM;
  addrinfo *addresses = nullptr;
  const int result    = getaddrinfo(host.c_str(), nullptr, &hints, &addresses);
  if (result || !addresses) {
    throw std::runtime_error("Cannot resolve udp-host " + host + ": " + gai_strerror(result));
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> guard(addresses, freeaddrinfo);
  char text[INET_ADDRSTRLEN] = {};
  if (!inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(addresses->ai_addr)->sin_addr, text, sizeof(text))) {
    throw std::runtime_error("Cannot convert udp-host address: " + host);
  }
  return text;
}
} // namespace

void
SecureKeyDeleter::operator()(uint8_t *key) const noexcept
{
  if (key) {
    OPENSSL_secure_clear_free(key, 32);
  }
}

///////////////////////////////////////////////////////////////////////////////
// CUdp constructor
//
// Log to the default logger until a configuration with its own logging
// setup has been loaded.
//

CUdp::CUdp()
  : m_socket(invalidSocket)
{
  m_logger = spdlog::default_logger();
}

///////////////////////////////////////////////////////////////////////////////
// CUdp destructor
//
// Stop the receiver, free events nobody has read and wipe the keys.
//

CUdp::~CUdp()
{
  close();
  for (auto *event : m_receiveList) {
    deleteEvent(event);
  }
  clearKeys();
}

void
CUdp::clearKeys() noexcept
{
  m_key.reset();
  m_hmac.reset();
  m_encryption    = VSCP_ENCRYPTION_NONE;
  m_authenticated = false;
}

///////////////////////////////////////////////////////////////////////////////
// loadConfig
//
// Load and validate the JSON configuration file at path and set up the
// keys, filters and logging it describes. See README.md for the format.
// Returns false (and logs the reason) if anything is invalid.
//

bool
CUdp::loadConfig(const std::string &path)
{
  try {
    std::ifstream input(path);
    if (!input) {
      throw std::runtime_error("Cannot open configuration: " + path);
    }

    // Parse JSON configuration file
    auto config = nlohmann::json::parse(input);
    if (!config.is_object()) {
      throw std::runtime_error("Configuration must be an object");
    }

    // Interface (address + port)
    m_interface = config.value("interface", m_interface);

    // Driver flags: bit 0 = disable sending (client), bit 1 = disable
    // receiving (server)
    if (config.contains("flags") && !config.at("flags").is_number_integer()) {
      throw std::runtime_error("flags must be an integer");
    }
    const auto flags = config.value("flags", int64_t{ 0 });
    if (flags < 0 || flags > 3) {
      throw std::runtime_error("flags must be in range 0..3");
    }
    m_flags = static_cast<uint32_t>(flags);

    // Writable flag (reserved for future use)
    config.value("write", false); // Reserved for future runtime configuration support.

    const bool legacySecurity = config.contains("key") || config.contains("hmac") || config.contains("encryption");
    if (config.contains("security") && legacySecurity) {
      throw std::runtime_error("Do not mix security settings with top-level key, hmac or encryption");
    }
    const auto &security = config.contains("security") ? config.at("security") : config;
    if (!security.is_object()) {
      throw std::runtime_error("security must be an object");
    }

    // Accept legacy top-level settings, but never silently override them.
    std::string encryption = security.value("encryption", std::string{});
    // Trim and check encryption string
    encryption.erase(0, encryption.find_first_not_of(" \t\n\r"));
    encryption.erase(encryption.find_last_not_of(" \t\n\r") + 1);
    if (encryption.empty() || encryption == "none") {
      m_encryption = VSCP_ENCRYPTION_NONE;
    }
    else if (encryption == "aes128") {
      m_encryption = VSCP_ENCRYPTION_AES128;
    }
    else if (encryption == "aes192") {
      m_encryption = VSCP_ENCRYPTION_AES192;
    }
    else if (encryption == "aes256") {
      m_encryption = VSCP_ENCRYPTION_AES256;
    }
    else {
      throw std::runtime_error("Unknown encryption: " + encryption);
    }

    // HMAC authentication
    const std::string hmacPath = security.value("hmac", std::string{});
    m_authenticated            = !hmacPath.empty();
    if (m_authenticated && !m_encryption) {
      throw std::runtime_error("HMAC requires AES encryption");
    }
    // Read encryption key if encryption is enabled
    if (m_encryption) {
      if (!initializeSecureHeap()) {
        throw std::runtime_error("Cannot initialize OpenSSL secure memory for key storage");
      }
      m_key              = allocateSecureKey();
      const size_t bytes = m_encryption == VSCP_ENCRYPTION_AES128   ? 16
                           : m_encryption == VSCP_ENCRYPTION_AES192 ? 24
                                                                    : 32;

      // Read the encryption key from the file named in the configuration
      readKey(security.at("key").get<std::string>(), m_key.get(), bytes);

      // Fail early if this OpenSSL build cannot do the selected cipher,
      // rather than on the first send or receive
      const EVP_CIPHER *cipher = m_encryption == VSCP_ENCRYPTION_AES128   ? EVP_aes_128_cbc()
                                 : m_encryption == VSCP_ENCRYPTION_AES192 ? EVP_aes_192_cbc()
                                                                          : EVP_aes_256_cbc();

      std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
      std::array<uint8_t, 16> iv{};
      if (!ctx || EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, m_key.get(), iv.data()) != 1) {
        throw std::runtime_error("OpenSSL AES-CBC is unavailable");
      }
    }
    // HMAC-SHA256 key is always 256 bits
    if (m_authenticated) {
      m_hmac = allocateSecureKey();
      readKey(hmacPath, m_hmac.get(), 32);
    }

    // Filters. Cleared filters let every event through.
    vscp_clearVSCPFilter(&m_filterIn);
    vscp_clearVSCPFilter(&m_filterOut);
    if (config.contains("filter")) {
      const auto &filter = config.at("filter");
      if (!filter.is_object()) {
        throw std::runtime_error("filter must be an object");
      }
      for (const auto &entry : { std::make_pair("in-filter", &m_filterIn), { "out-filter", &m_filterOut } }) {
        if (filter.contains(entry.first) &&
            !vscp_readFilterFromString(entry.second, filter.at(entry.first).get<std::string>())) {
          throw std::runtime_error(std::string("Invalid ") + entry.first);
        }
      }
      for (const auto &entry : { std::make_pair("in-mask", &m_filterIn), { "out-mask", &m_filterOut } }) {
        if (filter.contains(entry.first) &&
            !vscp_readMaskFromString(entry.second, filter.at(entry.first).get<std::string>())) {
          throw std::runtime_error(std::string("Invalid ") + entry.first);
        }
      }
    }

    // Logging to console, rotating file, syslog and UDP. Both the
    // "file-log-*" and the shorter "file-*" key names are accepted for file
    // logging options.
    const auto logging = config.value("logging", nlohmann::json::object());
    if (!logging.is_object()) {
      throw std::runtime_error("logging must be an object");
    }
    std::vector<spdlog::sink_ptr> sinks;
    if (logging.value("console-enable", true)) {
      auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      sink->set_level(logLevel(logging.value("console-level", std::string("info"))));
      sink->set_pattern(logging.value("console-pattern", std::string("[vscpl2drv-udp] [%^%l%$] %v")));
      sinks.push_back(sink);
    }
    if (logging.value("file-enable", false)) {
      auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logging.value("file-log-path", logging.value("file-path", std::string("/var/log/vscp/vscpl2drv-udp.log"))),
        logging.value("file-log-max-size", logging.value("file-max-size", size_t{ 5242880 })),
        logging.value("file-log-max-files", logging.value("file-max-files", size_t{ 7 })));
      sink->set_level(logLevel(logging.value("file-log-level", logging.value("file-level", std::string("info")))));
      sink->set_pattern(
        logging.value("file-log-pattern", logging.value("file-pattern", std::string("[vscpl2drv-udp] [%l] %v"))));
      sinks.push_back(sink);
    }

    // Syslog. Not available on Windows, where the option is ignored.
    bool syslogIgnored = false;
    if (logging.value("syslog-enable-log", false)) {
      const auto level = logLevel(logging.value("syslog-log-level", std::string("info")));
#ifdef _WIN32
      syslogIgnored = true;
#else
      auto sink =
        std::make_shared<spdlog::sinks::syslog_sink_mt>(logging.value("syslog-ident", std::string("vscpl2drv-udp")),
                                                        LOG_PID,
                                                        LOG_USER,
                                                        false);
      sink->set_level(level);
      sinks.push_back(sink);
#endif
    }

    // Log messages sent as UDP datagrams to a remote log collector
    if (logging.value("udp-enable-log", false)) {
      if (logging.contains("udp-port") && !logging.at("udp-port").is_number_integer()) {
        throw std::runtime_error("udp-port must be an integer");
      }
      const auto port = logging.value("udp-port", int64_t{ 9999 });
      if (port < 1 || port > 65535) {
        throw std::runtime_error("udp-port must be in range 1..65535");
      }
      auto sink = std::make_shared<spdlog::sinks::udp_sink_mt>(
        spdlog::sinks::udp_sink_config(resolveIPv4(logging.value("udp-host", std::string("127.0.0.1"))),
                                       static_cast<uint16_t>(port)));
      sink->set_level(logLevel(logging.value("udp-log-level", std::string("info"))));
      sink->set_pattern(logging.value("udp-pattern", std::string("[vscpl2drv-udp] [%l] %v")));
      sinks.push_back(sink);
    }

    m_logger = std::make_shared<spdlog::logger>("vscpl2drv-udp", sinks.begin(), sinks.end());
    // Let each sink do its own level filtering
    m_logger->set_level(spdlog::level::trace);
    m_logger->flush_on(spdlog::level::err);
    if (syslogIgnored) {
      m_logger->warn("syslog logging is not supported on Windows; syslog-enable-log ignored");
    }
    return true;
  }
  catch (const std::exception &error) {
    clearKeys();
    m_logger->error("UDP configuration failed: {}", error.what());
    return false;
  }
}

///////////////////////////////////////////////////////////////////////////////
// setupSocket
//
// Parse the interface string (udp://host:port, IPv6 hosts in brackets),
// resolve it and create the socket. The resolved address is the destination
// for sent events. Unless receiving is disabled (flags bit 1) the socket is
// also bound to the same port on the wildcard address so that it receives
// datagrams from any sender. Broadcast is enabled for IPv4.
// Returns false (and logs the reason) on failure.
//

bool
CUdp::setupSocket()
{
  if (m_interface.compare(0, 6, "udp://") != 0) {
    m_logger->error("interface must be udp://host:port");
    return false;
  }
  std::string host, port;
  const auto endpoint = m_interface.substr(6);
  const auto colon    = endpoint.rfind(':');
  if (colon == std::string::npos) {
    m_logger->error("UDP interface requires a port");
    return false;
  }
  // Split on the last colon so IPv6 addresses like [::1]:33333 work
  host = endpoint.substr(0, colon);
  port = endpoint.substr(colon + 1);
  if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
    host = host.substr(1, host.size() - 2);
  }
  if (host.empty() || port.empty() || port.size() > 5 ||
      !std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); }) ||
      std::stoul(port) == 0 || std::stoul(port) > 65535) {
    m_logger->error("Invalid UDP host or port: {}", m_interface);
    return false;
  }
  if (!initSockets()) {
    m_logger->error("Cannot initialize socket library");
    return false;
  }
  addrinfo hints{};
  hints.ai_family     = AF_UNSPEC;
  hints.ai_socktype   = SOCK_DGRAM;
  hints.ai_flags      = AI_NUMERICSERV;
  addrinfo *addresses = nullptr;
  const int result    = getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
  if (result) {
    m_logger->error("Cannot resolve UDP endpoint: {}", gai_strerror(result));
    return false;
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> guard(addresses, freeaddrinfo);

  // Use the first resolved address that a socket can be set up for
  for (auto *address = addresses; address; address = address->ai_next) {
    if (address->ai_addrlen > m_destination.size())
      continue;
    m_socket = socket(address->ai_family, SOCK_DGRAM, 0);
    if (m_socket == invalidSocket)
      continue;
    int enabled = 1;
    bool ok =
      configureSocket(m_socket) &&
      (address->ai_family != AF_INET ||
       setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&enabled), sizeof(enabled)) == 0);
    if (!(m_flags & 2)) {
      if (address->ai_family == AF_INET) {
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port   = reinterpret_cast<sockaddr_in *>(address->ai_addr)->sin_port;
        ok               = ok && bind(m_socket, reinterpret_cast<sockaddr *>(&local), sizeof(local)) == 0;
      }
      else {
        sockaddr_in6 local{};
        local.sin6_family = AF_INET6;
        local.sin6_port   = reinterpret_cast<sockaddr_in6 *>(address->ai_addr)->sin6_port;
        ok                = ok && bind(m_socket, reinterpret_cast<sockaddr *>(&local), sizeof(local)) == 0;
      }
    }
    if (ok) {
      memcpy(m_destination.data(), address->ai_addr, address->ai_addrlen);
      m_destinationSize = static_cast<int>(address->ai_addrlen);
      return true;
    }
    const int error = socketErrorCode();
    closeSocket(m_socket);
    m_socket = invalidSocket;
    m_logger->error("Cannot configure/bind UDP socket: {}", socketErrorText(error));
  }
  m_logger->error("Cannot open UDP endpoint {}", m_interface);
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// open
//
// Load the configuration at path, open the socket and start the receive
// thread (unless receiving is disabled). Returns false on failure.
//

bool
CUdp::open(const std::string &path, const cguid &)
{
  std::lock_guard<std::mutex> lock(m_stateMutex);
  if (m_socket != invalidSocket) {
    m_logger->error("UDP driver is already open");
    return false;
  }

  if (!loadConfig(path)) {
    return false;
  }
  if (!setupSocket()) {
    clearKeys();
    return false;
  }

  // Process wide initialization of the CRC table and frame crypto backend
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    crcInit();
    vscp_setFrameEncryptionUseOpenSSL(true);
  });

  m_quit = false;
  try {
    if (!(m_flags & 2)) {
      m_worker = std::thread(&CUdp::worker, this);
    }
  }
  catch (const std::system_error &error) {
    m_logger->error("Cannot start UDP receiver: {}", error.what());
    m_quit = true;
    closeSocket(m_socket);
    m_socket = invalidSocket;
    clearKeys();
    return false;
  }

  m_logger->debug("UDP driver opened on {} (flags={}, encryption={}, HMAC={})",
                  m_interface,
                  m_flags,
                  m_encryption,
                  m_authenticated);

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// close
//
// Stop the receive thread and close the socket. Wakes any read() that is
// waiting. Safe to call more than once.
//

void
CUdp::close()
{
  std::lock_guard<std::mutex> lock(m_stateMutex);
  stopReceiver();

  if (m_worker.joinable()) {
    m_worker.join();
  }

  if (m_socket != invalidSocket) {
    closeSocket(m_socket);
  }

  m_socket = invalidSocket;
  clearKeys();
}

///////////////////////////////////////////////////////////////////////////////
// encode
//
// Serialize event into a datagram ready to send. Always uses binary frame
// format 1 (nanosecond timestamp).
//
// Without HMAC:  [type][frame + CRC]            (no encryption)
//                [type][ciphertext][IV:16]       (AES)
// With HMAC:     [0x55][ver:3|len:13][type][ciphertext][IV:16][tag:16]
//
// Returns false if the event cannot be serialized or encrypted.
//

bool
CUdp::encode(const vscpEvent &event, std::vector<uint8_t> &packet)
{
  std::array<uint8_t, maxPacket> plain{}, encrypted{};
  const size_t size = 38 + event.sizeData;
  if (!vscp_writeEventToFrame(plain.data(), plain.size(), 0, &event)) {
    return false;
  }

  const size_t wireSize =
    m_authenticated ? vscp_encryptFrameV1(encrypted.data(),
                                          encrypted.size(),
                                          plain.data(),
                                          size,
                                          m_key.get(),
                                          m_hmac.get(),
                                          nullptr,
                                          m_encryption)
                    : vscp_encryptFrame(encrypted.data(), plain.data(), size, m_key.get(), nullptr, m_encryption);

  if (!wireSize) {
    return false;
  }

  // Authenticated packets start with 0x55 followed by a big-endian word
  // holding version 1 in the top 3 bits and the remaining length below.
  if (m_authenticated) {
    const uint16_t prefix = 0x2000 | static_cast<uint16_t>(wireSize);
    packet                = { 0x55, static_cast<uint8_t>(prefix >> 8), static_cast<uint8_t>(prefix) };
  }

  packet.insert(packet.end(), encrypted.begin(), encrypted.begin() + wireSize);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// decode
//
// Validate, decrypt and parse a received datagram into event. Packets that
// do not match the configured encryption/HMAC mode exactly are rejected, as
// are packets with a bad length, tag or CRC. Frame formats 0 and 1 are
// accepted. Returns false if the packet is rejected.
//

bool
CUdp::decode(const uint8_t *packet, size_t size, vscpEvent &event)
{
  std::array<uint8_t, maxPacket> plain{}, input{};
  if (size < 1 || size > maxPacket) {
    return false;
  }

  if (m_authenticated) {
    if (size < 4 || packet[0] != 0x55) {
      return false;
    }
    const unsigned prefix = (packet[1] << 8) | packet[2];
    if ((prefix >> 13) != 1 || (prefix & 0x1fff) != size - 3) {
      return false;
    }
    packet += 3;
    size -= 3;
  }

  // Low nibble of the type byte is the encryption, high nibble the frame
  // format. Only the configured encryption and formats 0/1 are accepted.
  if ((packet[0] & 0x0f) != m_encryption || (packet[0] >> 4) > 1) {
    return false;
  }

  size_t plainSize = size;
  if (m_authenticated) {
    plainSize = vscp_decryptFrameV1(plain.data(), plain.size(), packet, size, m_key.get(), m_hmac.get());
    if (!plainSize)
      return false;
  }
  else {
    // vscp_decryptFrame() takes a non-const buffer
    memcpy(input.data(), packet, size);
    if (!vscp_decryptFrame(plain.data(), input.data(), size, m_key.get(), nullptr, m_encryption))
      return false;
  }

  if (plainSize < 38) {
    return false;
  }

  // Data size is stored big-endian at offset 34 in the frame
  const size_t dataSize = (plain[34] << 8) | plain[35];
  if (dataSize > VSCP_LEVEL2_MAXDATA) {
    return false;
  }

  // Make sure the packet is exactly as long as the frame says. Plain AES
  // frames are zero padded to whole blocks and followed by the IV.
  const size_t frameSize = 38 + dataSize;
  if (m_authenticated || !m_encryption) {
    if (frameSize != plainSize)
      return false;
  }
  else if (size != 1 + ((frameSize - 1) / 16 + 1) * 16 + 16) {
    return false;
  }

  return vscp_getEventFromFrame(&event, plain.data(), frameSize);
}

///////////////////////////////////////////////////////////////////////////////
// write
//
// Send event as one UDP datagram. Events rejected by the out filter are
// silently dropped and reported as success. Waits at most timeout ms for
// the socket to accept the datagram. Returns a CANAL_ERROR_* code.
//

int
CUdp::write(const vscpEvent *event, unsigned long timeout)
{
  if (!event || event->sizeData > VSCP_LEVEL2_MAXDATA || (event->sizeData && !event->pdata)) {
    m_logger->error("Invalid event supplied to UDP write");
    return CANAL_ERROR_PARAMETER;
  }

  std::lock_guard<std::mutex> lock(m_stateMutex);
  if (m_quit || m_socket == invalidSocket) {
    return CANAL_ERROR_NOT_OPEN;
  }

  if (m_flags & 1) {
    return CANAL_ERROR_NOT_SUPPORTED;
  }

  if (!vscp_doLevel2Filter(event, &m_filterOut)) {
    return CANAL_ERROR_SUCCESS;
  }

  std::vector<uint8_t> packet;
  if (!encode(*event, packet)) {
    m_logger->error("Cannot encode/encrypt UDP event");
    return CANAL_ERROR_INTERNAL;
  }

  // Try to send directly; only wait for the socket if it would block
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
  while (true) {
    const auto sent = sendto(m_socket,
                             reinterpret_cast<const char *>(packet.data()),
                             static_cast<int>(packet.size()),
                             0,
                             reinterpret_cast<const sockaddr *>(m_destination.data()),
                             m_destinationSize);
    if (sent == static_cast<decltype(sent)>(packet.size())) {
      break;
    }
    const int error = socketErrorCode();
    if (sent >= 0 || !wouldBlock(error)) {
      m_logger->error("UDP send failed: {}", sent < 0 ? socketErrorText(error) : "short write");
      return CANAL_ERROR_COMMUNICATION;
    }

    const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) {
      return CANAL_ERROR_TIMEOUT;
    }
    pollDescriptor descriptor{};
    descriptor.fd     = m_socket;
    descriptor.events = POLLOUT;
    if (pollSocket(descriptor, static_cast<int>(std::min<long long>(remaining, INT_MAX))) < 0 &&
        !wouldBlock(socketErrorCode())) {
      m_logger->error("UDP send poll failed: {}", socketErrorText(socketErrorCode()));
      return CANAL_ERROR_COMMUNICATION;
    }
  }
  return CANAL_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// read
//
// Fetch the oldest received event, waiting at most timeout ms for one to
// arrive. Ownership of the event data (pdata) is passed to the caller.
// Returns CANAL_ERROR_NOT_OPEN if the driver is closed while waiting.
//

int
CUdp::read(vscpEvent *event, unsigned long timeout)
{
  if (!event) {
    m_logger->error("Invalid event supplied to UDP read");
    return CANAL_ERROR_PARAMETER;
  }

  if (m_quit) {
    return CANAL_ERROR_NOT_OPEN;
  }

  if (m_flags & 2) {
    return CANAL_ERROR_NOT_SUPPORTED;
  }

  std::unique_lock<std::mutex> lock(m_receiveMutex);
  if (!m_receiveReady.wait_for(lock, std::chrono::milliseconds(timeout), [this] {
        return m_quit || !m_receiveList.empty();
      })) {
    return CANAL_ERROR_TIMEOUT;
  }

  // Woken with an empty queue means the driver is closing
  if (m_receiveList.empty()) {
    return CANAL_ERROR_NOT_OPEN;
  }

  auto *received = m_receiveList.front();
  m_receiveList.pop_front();
  // Hand the data buffer over to the caller before freeing the event
  *event          = *received;
  received->pdata = nullptr;
  deleteEvent(received);
  return CANAL_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// stopReceiver
//
// Flag the driver as stopped and wake all threads waiting in read(). The
// flag is set under the receive mutex so a waiting reader cannot miss it.
//

void
CUdp::stopReceiver()
{
  {
    std::lock_guard<std::mutex> lock(m_receiveMutex);
    m_quit = true;
  }
  m_receiveReady.notify_all();
}

///////////////////////////////////////////////////////////////////////////////
// worker
//
// Receive thread. Polls the socket in 20 ms slices so it notices quit
// requests quickly, decodes and filters incoming datagrams, and queues the
// resulting events for read(). Invalid packets are logged and dropped.
//

void
CUdp::worker()
{
  try {
    while (!m_quit) {
      pollDescriptor descriptor{};
      descriptor.fd     = m_socket;
      descriptor.events = POLLIN;
      const int ready   = pollSocket(descriptor, 20);
      if (ready < 0 && wouldBlock(socketErrorCode())) {
        continue;
      }
      if (ready < 0 || (descriptor.revents & POLLNVAL)) {
        m_logger->error("UDP receiver socket failed: {}",
                        ready < 0 ? socketErrorText(socketErrorCode()) : "invalid socket");
        stopReceiver();
        break;
      }
      if (!ready) {
        continue;
      }

      // One byte larger than the largest valid packet so oversized
      // datagrams are detected instead of silently truncated
      std::array<uint8_t, maxPacket + 1> packet{};
      const auto size = recvfrom(m_socket,
                                 reinterpret_cast<char *>(packet.data()),
                                 static_cast<int>(packet.size()),
                                 0,
                                 nullptr,
                                 nullptr);
      if (size < 0) {
        const int error = socketErrorCode();
        if (!wouldBlock(error)) {
          m_logger->error("UDP receive failed: {}", socketErrorText(error));
        }
        continue;
      }

      std::unique_ptr<vscpEvent, decltype(&deleteEvent)> event(new vscpEvent{}, deleteEvent);
      if (!decode(packet.data(), static_cast<size_t>(size), *event)) {
        m_logger->warn("Discarding invalid UDP frame (length, encryption, authentication or CRC)");
        continue;
      }

      if (!vscp_doLevel2Filter(event.get(), &m_filterIn)) {
        continue;
      }

      std::lock_guard<std::mutex> lock(m_receiveMutex);
      if (m_receiveList.size() >= maxQueue) {
        m_logger->warn("UDP receive queue full; discarding event");
        continue;
      }

      m_receiveList.push_back(event.get());
      event.release();
      m_receiveReady.notify_one();
    }
  }
  catch (const std::exception &error) {
    m_logger->error("UDP receiver stopped: {}", error.what());
    stopReceiver();
  }
}
