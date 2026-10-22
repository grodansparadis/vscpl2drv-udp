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

#include <arpa/inet.h>
#include <canal.h>
#include <crc.h>
#include <netdb.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <poll.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unistd.h>

namespace {
constexpr size_t maxPacket = 38 + VSCP_LEVEL2_MAXDATA + 48 + 3;
constexpr size_t maxQueue  = 32000;

void
deleteEvent(vscpEvent *event)
{
  vscp_deleteEvent_v2(&event);
}

void
readKey(const std::string &path, uint8_t *key, size_t bytes)
{
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Cannot open key file: " + path);
  }
  std::string text((std::istreambuf_iterator<char>(input)), {});
  text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); }), text.end());
  if (text.size() != bytes * 2 ||
      !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c); })) {
    throw std::runtime_error("Invalid hexadecimal key length or content in: " + path);
  }
  for (size_t i = 0; i < bytes; ++i) {
    key[i] = static_cast<uint8_t>(std::stoul(text.substr(i * 2, 2), nullptr, 16));
  }
  OPENSSL_cleanse(text.data(), text.size());
}

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
}

CUdp::CUdp()
{
  if (sem_init(&m_receiveReady, 0, 0)) {
    throw std::system_error(errno, std::generic_category(), "sem_init");
  }
  m_logger = spdlog::default_logger();
}

CUdp::~CUdp()
{
  close();
  for (auto *event : m_receiveList)
    deleteEvent(event);
  sem_destroy(&m_receiveReady);
  OPENSSL_cleanse(m_key.data(), m_key.size());
  OPENSSL_cleanse(m_hmac.data(), m_hmac.size());
}

bool
CUdp::loadConfig(const std::string &path)
{
  try {
    std::ifstream input(path);
    if (!input)
      throw std::runtime_error("Cannot open configuration: " + path);
    auto config = nlohmann::json::parse(input);
    if (!config.is_object())
      throw std::runtime_error("Configuration must be an object");
    m_interface = config.value("interface", m_interface);
    if (config.contains("flags") && !config.at("flags").is_number_integer()) {
      throw std::runtime_error("flags must be an integer");
    }
    const auto flags = config.value("flags", int64_t{ 0 });
    if (flags < 0 || flags > 3)
      throw std::runtime_error("flags must be in range 0..3");
    m_flags = static_cast<uint32_t>(flags);
    config.value("write", false); // Reserved for future runtime configuration support.
    const bool debug             = config.value("debug", false);
    const std::string encryption = config.value("encryption", std::string{});
    if (encryption.empty() || encryption == "none")
      m_encryption = VSCP_ENCRYPTION_NONE;
    else if (encryption == "aes128")
      m_encryption = VSCP_ENCRYPTION_AES128;
    else if (encryption == "aes192")
      m_encryption = VSCP_ENCRYPTION_AES192;
    else if (encryption == "aes256")
      m_encryption = VSCP_ENCRYPTION_AES256;
    else
      throw std::runtime_error("Unknown encryption: " + encryption);

    const std::string hmacPath = config.value("hmac", std::string{});
    m_authenticated            = !hmacPath.empty();
    if (m_authenticated && !m_encryption) {
      throw std::runtime_error("HMAC requires AES encryption");
    }
    if (m_encryption) {
      const size_t bytes = m_encryption == VSCP_ENCRYPTION_AES128   ? 16
                           : m_encryption == VSCP_ENCRYPTION_AES192 ? 24
                                                                    : 32;
      readKey(config.at("key").get<std::string>(), m_key.data(), bytes);
      const EVP_CIPHER *cipher = m_encryption == VSCP_ENCRYPTION_AES128   ? EVP_aes_128_cbc()
                                 : m_encryption == VSCP_ENCRYPTION_AES192 ? EVP_aes_192_cbc()
                                                                          : EVP_aes_256_cbc();
      std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
      std::array<uint8_t, 16> iv{};
      if (!ctx || EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, m_key.data(), iv.data()) != 1) {
        throw std::runtime_error("OpenSSL AES-CBC is unavailable");
      }
    }
    if (m_authenticated)
      readKey(hmacPath, m_hmac.data(), m_hmac.size());

    vscp_clearVSCPFilter(&m_filterIn);
    vscp_clearVSCPFilter(&m_filterOut);
    if (config.contains("filter")) {
      const auto &filter = config.at("filter");
      if (!filter.is_object())
        throw std::runtime_error("filter must be an object");
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

    const auto logging = config.value("logging", nlohmann::json::object());
    if (!logging.is_object())
      throw std::runtime_error("logging must be an object");
    std::vector<spdlog::sink_ptr> sinks;
    if (logging.value("console-enable", true)) {
      auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      sink->set_level(logLevel(logging.value("console-level", std::string(debug ? "debug" : "info"))));
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
    m_logger = std::make_shared<spdlog::logger>("vscpl2drv-udp", sinks.begin(), sinks.end());
    m_logger->set_level(spdlog::level::trace);
    m_logger->flush_on(spdlog::level::err);
    return true;
  }
  catch (const std::exception &error) {
    m_logger->error("UDP configuration failed: {}", error.what());
    return false;
  }
}

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
  for (auto *address = addresses; address; address = address->ai_next) {
    m_socket = socket(address->ai_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_socket < 0)
      continue;
    int enabled = 1;
    bool ok =
      address->ai_family != AF_INET || setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) == 0;
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
      memcpy(&m_destination, address->ai_addr, address->ai_addrlen);
      m_destinationSize = address->ai_addrlen;
      return true;
    }
    const int error = errno;
    ::close(m_socket);
    m_socket = -1;
    m_logger->error("Cannot configure/bind UDP socket: {}", strerror(error));
  }
  m_logger->error("Cannot open UDP endpoint {}", m_interface);
  return false;
}

bool
CUdp::open(const std::string &path, const cguid &)
{
  std::lock_guard<std::mutex> lock(m_stateMutex);
  if (m_socket >= 0) {
    m_logger->error("UDP driver is already open");
    return false;
  }
  if (!loadConfig(path) || !setupSocket())
    return false;
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    crcInit();
    vscp_setFrameEncryptionUseOpenSSL(true);
  });
  m_quit = false;
  try {
    if (!(m_flags & 2))
      m_worker = std::thread(&CUdp::worker, this);
  }
  catch (const std::system_error &error) {
    m_logger->error("Cannot start UDP receiver: {}", error.what());
    m_quit = true;
    ::close(m_socket);
    m_socket = -1;
    return false;
  }
  m_logger->debug(
    "UDP driver opened on {} (flags={}, encryption={}, HMAC={})", m_interface, m_flags, m_encryption, m_authenticated);
  return true;
}

void
CUdp::close()
{
  std::lock_guard<std::mutex> lock(m_stateMutex);
  m_quit = true;
  if (m_worker.joinable())
    m_worker.join();
  if (m_socket >= 0)
    ::close(m_socket);
  m_socket = -1;
}

bool
CUdp::encode(const vscpEvent &event, std::vector<uint8_t> &packet)
{
  std::array<uint8_t, maxPacket> plain{}, encrypted{};
  const size_t size = 38 + event.sizeData;
  if (!vscp_writeEventToFrame(plain.data(), plain.size(), 0, &event))
    return false;
  const size_t wireSize =
    m_authenticated
      ? vscp_encryptFrameV1(
          encrypted.data(), encrypted.size(), plain.data(), size, m_key.data(), m_hmac.data(), nullptr, m_encryption)
      : vscp_encryptFrame(encrypted.data(), plain.data(), size, m_key.data(), nullptr, m_encryption);
  if (!wireSize)
    return false;
  if (m_authenticated) {
    const uint16_t prefix = 0x2000 | static_cast<uint16_t>(wireSize);
    packet                = { 0x55, static_cast<uint8_t>(prefix >> 8), static_cast<uint8_t>(prefix) };
  }
  packet.insert(packet.end(), encrypted.begin(), encrypted.begin() + wireSize);
  return true;
}

bool
CUdp::decode(const uint8_t *packet, size_t size, vscpEvent &event)
{
  std::array<uint8_t, maxPacket> plain{}, input{};
  if (size < 1 || size > maxPacket)
    return false;
  if (m_authenticated) {
    if (size < 4 || packet[0] != 0x55)
      return false;
    const unsigned prefix = (packet[1] << 8) | packet[2];
    if ((prefix >> 13) != 1 || (prefix & 0x1fff) != size - 3)
      return false;
    packet += 3;
    size -= 3;
  }
  if ((packet[0] & 0x0f) != m_encryption || (packet[0] >> 4) > 1)
    return false;
  size_t plainSize = size;
  if (m_authenticated) {
    plainSize = vscp_decryptFrameV1(plain.data(), plain.size(), packet, size, m_key.data(), m_hmac.data());
    if (!plainSize)
      return false;
  }
  else {
    memcpy(input.data(), packet, size);
    if (!vscp_decryptFrame(plain.data(), input.data(), size, m_key.data(), nullptr, m_encryption))
      return false;
  }
  if (plainSize < 38)
    return false;
  const size_t dataSize = (plain[34] << 8) | plain[35];
  if (dataSize > VSCP_LEVEL2_MAXDATA)
    return false;
  const size_t frameSize = 38 + dataSize;
  if (m_authenticated || !m_encryption) {
    if (frameSize != plainSize)
      return false;
  }
  else if (size != 1 + ((frameSize - 1) / 16 + 1) * 16 + 16)
    return false;
  return vscp_getEventFromFrame(&event, plain.data(), frameSize);
}

int
CUdp::write(const vscpEvent *event, unsigned long timeout)
{
  if (!event || event->sizeData > VSCP_LEVEL2_MAXDATA || (event->sizeData && !event->pdata)) {
    m_logger->error("Invalid event supplied to UDP write");
    return CANAL_ERROR_PARAMETER;
  }
  std::lock_guard<std::mutex> lock(m_stateMutex);
  if (m_quit || m_socket < 0)
    return CANAL_ERROR_NOT_OPEN;
  if (m_flags & 1)
    return CANAL_ERROR_NOT_SUPPORTED;
  if (!vscp_doLevel2Filter(event, &m_filterOut))
    return CANAL_ERROR_SUCCESS;
  std::vector<uint8_t> packet;
  if (!encode(*event, packet)) {
    m_logger->error("Cannot encode/encrypt UDP event");
    return CANAL_ERROR_INTERNAL;
  }
  pollfd descriptor{ m_socket, POLLOUT, 0 };
  const int ready = poll(&descriptor, 1, static_cast<int>(std::min<unsigned long>(timeout, INT_MAX)));
  if (!ready)
    return CANAL_ERROR_TIMEOUT;
  if (ready < 0 || !(descriptor.revents & POLLOUT)) {
    m_logger->error("UDP send poll failed: {}", ready < 0 ? strerror(errno) : "socket error");
    return CANAL_ERROR_COMMUNICATION;
  }
  if (sendto(m_socket,
             packet.data(),
             packet.size(),
             0,
             reinterpret_cast<const sockaddr *>(&m_destination),
             m_destinationSize) != static_cast<ssize_t>(packet.size())) {
    m_logger->error("UDP send failed: {}", strerror(errno));
    return CANAL_ERROR_COMMUNICATION;
  }
  return CANAL_ERROR_SUCCESS;
}

int
CUdp::read(vscpEvent *event, unsigned long timeout)
{
  if (!event) {
    m_logger->error("Invalid event supplied to UDP read");
    return CANAL_ERROR_PARAMETER;
  }
  if (m_quit)
    return CANAL_ERROR_NOT_OPEN;
  if (m_flags & 2)
    return CANAL_ERROR_NOT_SUPPORTED;
  const auto start = std::chrono::steady_clock::now();
  while (true) {
    if (m_quit)
      return CANAL_ERROR_NOT_OPEN;
    const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    const auto remaining = elapsed >= timeout ? 0 : timeout - static_cast<unsigned long>(elapsed);
    if (vscp_sem_wait(&m_receiveReady, std::min<unsigned long>(remaining, 20)) == 0)
      break;
    if (errno == ETIMEDOUT || errno == EAGAIN) {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >=
          timeout)
        return CANAL_ERROR_TIMEOUT;
      continue;
    }
    m_logger->error("UDP receive wait failed: {}", strerror(errno));
    return CANAL_ERROR_INTERNAL;
  }
  std::lock_guard<std::mutex> lock(m_receiveMutex);
  if (m_receiveList.empty()) {
    m_logger->error("UDP receive queue/semaphore mismatch");
    return CANAL_ERROR_INTERNAL;
  }
  auto *received = m_receiveList.front();
  m_receiveList.pop_front();
  *event          = *received;
  received->pdata = nullptr;
  deleteEvent(received);
  return CANAL_ERROR_SUCCESS;
}

void
CUdp::worker()
{
  try {
    while (!m_quit) {
      pollfd descriptor{ m_socket, POLLIN, 0 };
      const int ready = poll(&descriptor, 1, 20);
      if (ready < 0 && errno == EINTR)
        continue;
      if (ready < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        m_logger->error("UDP receiver socket failed: {}", ready < 0 ? strerror(errno) : "socket error");
        m_quit = true;
        break;
      }
      if (!ready)
        continue;
      std::array<uint8_t, maxPacket + 1> packet{};
      const ssize_t size = recvfrom(m_socket, packet.data(), packet.size(), 0, nullptr, nullptr);
      if (size < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
          continue;
        m_logger->error("UDP receive failed: {}", strerror(errno));
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
      sem_post(&m_receiveReady);
    }
  }
  catch (const std::exception &error) {
    m_logger->error("UDP receiver stopped: {}", error.what());
    m_quit = true;
  }
}
