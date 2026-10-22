// Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project
// SPDX-License-Identifier: LGPL-2.0-or-later
#include "vscpl2drv-udp.h"
#include <canal.h>
#include <crc.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <vscphelper.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
const char *guid = "FF:FF:FF:FF:FF:FF:FF:F5:02:00:00:00:00:00:00:03";

void
check(bool condition, const std::string &message)
{
  if (!condition)
    throw std::runtime_error(message);
}

#ifdef _WIN32
using socket_t                = SOCKET;
const socket_t invalidSocket = INVALID_SOCKET;

void
closeSocket(socket_t fd)
{
  closesocket(fd);
}

int
pollIn(socket_t fd, int timeout)
{
  WSAPOLLFD descriptor{};
  descriptor.fd     = fd;
  descriptor.events = POLLIN;
  return WSAPoll(&descriptor, 1, timeout);
}
#else
using socket_t                = int;
const socket_t invalidSocket = -1;

void
closeSocket(socket_t fd)
{
  ::close(fd);
}

int
pollIn(socket_t fd, int timeout)
{
  pollfd descriptor{ fd, POLLIN, 0 };
  return poll(&descriptor, 1, timeout);
}
#endif

struct Socket {
  socket_t fd = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in address{};
  Socket()
  {
    check(fd != invalidSocket, "socket");
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "bind");
    socklen_t length = sizeof(address);
    check(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) == 0, "getsockname");
  }
  ~Socket()
  {
    if (fd != invalidSocket)
      closeSocket(fd);
  }
  unsigned port() const { return ntohs(address.sin_port); }
  std::vector<uint8_t> receive()
  {
    check(pollIn(fd, 1000) == 1, "outbound packet missing");
    std::vector<uint8_t> packet(2048);
    const auto size = recv(fd, reinterpret_cast<char *>(packet.data()), static_cast<int>(packet.size()), 0);
    check(size > 0, "recv");
    packet.resize(size);
    return packet;
  }
  void send(const std::vector<uint8_t> &packet, unsigned port)
  {
    auto destination     = address;
    destination.sin_port = htons(port);
    check(
      sendto(fd,
             reinterpret_cast<const char *>(packet.data()),
             static_cast<int>(packet.size()),
             0,
             reinterpret_cast<sockaddr *>(&destination),
             sizeof(destination)) == static_cast<int>(packet.size()),
      "sendto");
  }
};

struct Handle {
  long value = 0;
  ~Handle()
  {
    if (value)
      VSCPClose(value);
  }
};

struct Files {
  std::filesystem::path dir;
  Files()
  {
    std::random_device random;
    for (int attempt = 0; attempt < 100 && dir.empty(); ++attempt) {
      auto candidate = std::filesystem::temp_directory_path() / ("vscp-udp-test-" + std::to_string(random()));
      if (std::filesystem::create_directory(candidate))
        dir = candidate;
    }
    check(!dir.empty(), "create temporary directory");
  }
  ~Files() { std::filesystem::remove_all(dir); }
  std::string put(const std::string &name, const std::string &text)
  {
    auto path = dir / name;
    std::ofstream output(path);
    output << text;
    check(output.good(), "write fixture");
    return path.string();
  }
};

void
verifyWire(const std::vector<uint8_t> &packet, const vscpEvent &event, int encryption, bool authenticated)
{
  const size_t offset = authenticated ? 3 : 0;
  check(packet.size() > offset && packet[offset] == (0x10 | encryption), "packet type/encryption");
  std::array<uint8_t, 32> key{};
  std::array<uint8_t, 32> hmac{};
  key.fill(0x11);
  hmac.fill(0x22);
  size_t end = packet.size();
  if (authenticated) {
    check(packet[0] == 0x55, "HMAC prefix");
    const unsigned word = (packet[1] << 8) | packet[2];
    check((word >> 13) == 1 && (word & 0x1fff) == packet.size() - 3, "version/length");
    unsigned digestSize = 0;
    std::array<uint8_t, 32> digest{};
    check(HMAC(EVP_sha256(), hmac.data(), hmac.size(), packet.data(), packet.size() - 16, digest.data(), &digestSize) &&
            digestSize == 32,
          "independent HMAC");
    check(memcmp(digest.data(), packet.data() + packet.size() - 16, 16) == 0, "HMAC tag mismatch");
    end -= 16;
  }
  std::vector<uint8_t> plain;
  if (encryption) {
    check(end >= offset + 33 && (end - offset - 17) % 16 == 0, "AES length");
    const EVP_CIPHER *cipher = encryption == 1   ? EVP_aes_128_cbc()
                               : encryption == 2 ? EVP_aes_192_cbc()
                                                 : EVP_aes_256_cbc();
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    check(context && EVP_DecryptInit_ex(context.get(), cipher, nullptr, key.data(), packet.data() + end - 16) == 1,
          "independent AES init");
    plain.resize(packet.size());
    int bytes = 0, tail = 0;
    check(EVP_DecryptUpdate(context.get(), plain.data(), &bytes, packet.data() + offset + 1, end - offset - 17) == 1 &&
            EVP_DecryptFinal_ex(context.get(), plain.data() + bytes, &tail) == 1,
          "AES decrypt/padding");
    plain.resize(bytes + tail);
  }
  else
    plain.assign(packet.begin() + offset + 1, packet.end());
  check(plain.size() == 37 + event.sizeData, "plain frame length");
  check(plain[13] == (event.vscp_class >> 8) && plain[14] == (event.vscp_class & 0xff) &&
          plain[15] == (event.vscp_type >> 8) && plain[16] == (event.vscp_type & 0xff),
        "class/type");
  check(memcmp(plain.data() + 17, event.GUID, 16) == 0, "wire GUID");
  check(((plain[33] << 8) | plain[34]) == event.sizeData, "wire data length");
  if (event.sizeData)
    check(memcmp(plain.data() + 35, event.pdata, event.sizeData) == 0, "wire data");
  check(crcFast(plain.data(), plain.size()) == 0, "wire CRC");
}

void
roundTrip(Files &files, const std::string &mode, int encryption, bool authenticated)
{
  Socket capture;
  const std::string key(encryption == 1 ? 32 : encryption == 2 ? 48 : 64, '1');
  auto config = nlohmann::json{ { "interface", "udp://127.0.0.1:" + std::to_string(capture.port()) },
                                { "flags", 2 },
                                { "encryption", mode },
                                { "key", files.put("aes.key", " \n" + key + "\n") },
                                { "logging", { { "console-enable", false }, { "file-enable", false } } } };
  if (authenticated)
    config["hmac"] = files.put("hmac.key", std::string(64, '2'));
  Handle client{ VSCPOpen(files.put("client.json", config.dump()).c_str(), guid) };
  check(client.value, "client open " + mode);
  vscpEvent received{};
  check(VSCPRead(client.value, &received, 1) == CANAL_ERROR_NOT_SUPPORTED, "client read disabled");

  Socket reservation;
  const unsigned serverPort = reservation.port();
  closeSocket(reservation.fd);
  reservation.fd = invalidSocket;
  config["interface"] = "udp://127.0.0.1:" + std::to_string(serverPort);
  config["flags"]     = 1;
  Handle server{ VSCPOpen(files.put("server.json", config.dump()).c_str(), guid) };
  check(server.value, "server open");
  check(VSCPOpen(files.put("collision.json", config.dump()).c_str(), guid) == 0, "bind collision must fail");

  std::array<uint8_t, VSCP_LEVEL2_MAXDATA> data{};
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<uint8_t>(i);
  vscpEvent event{};
  event.head         = VSCP_HEADER16_FRAME_VERSION_UNIX_NS;
  event.timestamp_ns = 1792680000123456789ULL;
  event.vscp_class   = 1026;
  event.vscp_type    = 65500;
  for (size_t i = 0; i < 16; ++i)
    event.GUID[i] = static_cast<uint8_t>(i + 16);
  event.pdata = data.data();
  check(VSCPWrite(server.value, &event, 100) == CANAL_ERROR_NOT_SUPPORTED, "server write disabled");
  std::vector<uint8_t> last;
  for (unsigned length : { 0, 1, 10, 11, 26, 27, 255, VSCP_LEVEL2_MAXDATA }) {
    event.sizeData = length;
    check(VSCPWrite(client.value, &event, 100) == CANAL_ERROR_SUCCESS, "write");
    last = capture.receive();
    verifyWire(last, event, encryption, authenticated);
    capture.send(last, serverPort);
    check(VSCPRead(server.value, &received, 1000) == CANAL_ERROR_SUCCESS, "read round trip");
    check(received.sizeData == length && received.vscp_class == event.vscp_class &&
            received.vscp_type == event.vscp_type && received.timestamp_ns == event.timestamp_ns &&
            memcmp(received.GUID, event.GUID, 16) == 0,
          "event metadata");
    if (length)
      check(memcmp(received.pdata, data.data(), length) == 0, "event payload");
    delete[] received.pdata;
    received.pdata = nullptr;
  }
  if (encryption) {
    check(VSCPWrite(client.value, &event, 100) == CANAL_ERROR_SUCCESS, "repeat write");
    const auto repeated   = capture.receive();
    const size_t ivOffset = last.size() - (authenticated ? 32 : 16);
    check(memcmp(last.data() + ivOffset, repeated.data() + ivOffset, 16) != 0, "IV must be fresh");
  }
  std::vector<uint8_t> plain(38 + event.sizeData);
  check(vscp_writeEventToFrame(plain.data(), plain.size(), 0, &event), "plain fixture");
  if (encryption)
    capture.send(plain, serverPort);
  if (authenticated) {
    capture.send(std::vector<uint8_t>(last.begin() + 3, last.end()), serverPort);
    std::array<uint8_t, 32> aes{}, hmac{};
    aes.fill(0x44);
    hmac.fill(0x22);
    for (int wrong = 0; wrong < 2; ++wrong) {
      if (wrong) {
        aes.fill(0x11);
        hmac.fill(0x44);
      }
      std::vector<uint8_t> wire(plain.size() + 48);
      const auto length = vscp_encryptFrameV1(
        wire.data(), wire.size(), plain.data(), plain.size(), aes.data(), hmac.data(), nullptr, encryption);
      check(length, "wrong-key fixture");
      wire.resize(length);
      wire.insert(wire.begin(), { 0x55, static_cast<uint8_t>(0x20 | (length >> 8)), static_cast<uint8_t>(length) });
      capture.send(wire, serverPort);
    }
  }
  // Every authenticated byte, including prefix, IV and type, must be protected.
  for (size_t i = 0; i < last.size(); ++i) {
    if (!authenticated && i != last.size() - 1)
      continue;
    auto corrupted = last;
    corrupted[i] ^= 1;
    capture.send(corrupted, serverPort);
  }
  for (size_t length : { size_t{ 0 }, size_t{ 1 }, size_t{ 3 }, size_t{ 35 }, last.size() - 1 }) {
    capture.send(std::vector<uint8_t>(last.begin(), last.begin() + length), serverPort);
  }
  auto extra = last;
  extra.push_back(0);
  capture.send(extra, serverPort);
  capture.send(std::vector<uint8_t>(2000, 0), serverPort);
  check(VSCPRead(server.value, &received, 200) == CANAL_ERROR_TIMEOUT, "corrupt packets accepted");
  capture.send(last, serverPort);
  check(VSCPRead(server.value, &received, 1000) == CANAL_ERROR_SUCCESS, "receiver failed after bad packets");
  delete[] received.pdata;
  // Build an original timestamp-format event independently of the serializer.
  std::vector<uint8_t> legacy(39, 0);
  legacy[0]  = encryption;
  legacy[6]  = 123;
  legacy[7]  = 0x07;
  legacy[8]  = 0xea; // 2026
  legacy[9]  = 10;
  legacy[10] = 22;
  legacy[14] = 4;
  legacy[15] = 2;
  legacy[17] = 7;
  memcpy(legacy.data() + 18, event.GUID, 16);
  legacy[35]          = 1;
  legacy[36]          = 0xab;
  const auto checksum = crcFast(legacy.data() + 1, 36);
  legacy[37]          = static_cast<uint8_t>(checksum >> 8);
  legacy[38]          = static_cast<uint8_t>(checksum);
  std::array<uint8_t, 32> aes{}, hmac{};
  aes.fill(0x11);
  hmac.fill(0x22);
  std::vector<uint8_t> wire(legacy.size() + 48);
  const auto length =
    authenticated
      ? vscp_encryptFrameV1(
          wire.data(), wire.size(), legacy.data(), legacy.size(), aes.data(), hmac.data(), nullptr, encryption)
      : vscp_encryptFrame(wire.data(), legacy.data(), legacy.size(), aes.data(), nullptr, encryption);
  check(length, "legacy fixture");
  wire.resize(length);
  if (authenticated) {
    wire.insert(wire.begin(), { 0x55, static_cast<uint8_t>(0x20 | (length >> 8)), static_cast<uint8_t>(length) });
  }
  capture.send(wire, serverPort);
  received.pdata = nullptr;
  check(VSCPRead(server.value, &received, 1000) == CANAL_ERROR_SUCCESS, "legacy event read");
  check(received.year == 2026 && received.month == 10 && received.day == 22 && received.timestamp == 123 &&
          received.vscp_class == 1026 && received.vscp_type == 7 && received.sizeData == 1 && received.pdata[0] == 0xab,
        "legacy event metadata");
  delete[] received.pdata;
  event.sizeData = VSCP_LEVEL2_MAXDATA + 1;
  check(VSCPWrite(client.value, &event, 10) == CANAL_ERROR_PARAMETER, "oversize event");
  check(VSCPWrite(client.value, nullptr, 10) == CANAL_ERROR_PARAMETER, "null event");
  std::cout << "Passed " << mode << (authenticated ? " + HMAC" : "") << '\n';
}

void
configurationTests(Files &files)
{
  const auto aes  = files.put("valid.key", std::string(32, '1'));
  const auto hmac = files.put("valid-hmac.key", std::string(64, '2'));
  const auto bad  = files.put("bad.key", "not-hex");
  for (auto config :
       std::vector<nlohmann::json>{ { { "encryption", "unknown" } },
                                    { { "interface", "vcan0" } },
                                    { { "interface", "udp://localhost:0" } },
                                    { { "interface", "udp://localhost:65536" } },
                                    { { "flags", -1 } },
                                    { { "flags", 4 } },
                                    { { "flags", 1.5 } },
                                    { { "encryption", "none" }, { "hmac", hmac } },
                                    { { "encryption", "aes128" } },
                                    { { "encryption", "aes128" }, { "key", bad } },
                                    { { "encryption", "aes192" }, { "key", aes } },
                                    { { "encryption", "aes128" }, { "key", aes }, { "hmac", bad } },
                                    { { "encryption", "aes128" }, { "key", "/nonexistent/vscp.key" } } }) {
    check(VSCPOpen(files.put("invalid.json", config.dump()).c_str(), guid) == 0, "invalid config accepted");
  }
  check(VSCPOpen(files.put("invalid.json", "{").c_str(), guid) == 0, "invalid JSON");
  check(VSCPOpen(nullptr, guid) == 0, "null path");
  check(VSCPOpen("/nonexistent/udp.json", guid) == 0, "missing config");
  check(VSCPClose(123) == CANAL_ERROR_NOT_OPEN, "invalid close");

  Socket reservation;
  auto config =
    nlohmann::json{ { "interface", "udp://127.0.0.1:" + std::to_string(reservation.port()) }, { "flags", 0 } };
  closeSocket(reservation.fd);
  reservation.fd = invalidSocket;
  Handle duplex{ VSCPOpen(files.put("duplex.json", config.dump()).c_str(), guid) };
  check(duplex.value, "duplex open");
  vscpEvent event{}, received{};
  event.head         = VSCP_HEADER16_FRAME_VERSION_UNIX_NS;
  event.vscp_class   = 2000;
  event.timestamp_ns = 1792680000123456789ULL;
  check(VSCPWrite(duplex.value, &event, 100) == CANAL_ERROR_SUCCESS, "duplex write");
  check(VSCPRead(duplex.value, &received, 1000) == CANAL_ERROR_SUCCESS, "duplex loopback");
  check(received.vscp_class == 2000, "duplex class");
  delete[] received.pdata;
  check(VSCPClose(duplex.value) == CANAL_ERROR_SUCCESS, "close");
  check(VSCPWrite(duplex.value, &event, 10) == CANAL_ERROR_NOT_OPEN, "stale handle");
  duplex.value = 0;

  // Exercise both directional filters with exact class masks.
  config["filter"] = { { "out-filter", "0,10,0,-" },
                       { "out-mask", "0,65535,0,-" },
                       { "in-filter", "0,20,0,-" },
                       { "in-mask", "0,65535,0,-" } };
  Handle filtered{ VSCPOpen(files.put("filtered.json", config.dump()).c_str(), guid) };
  check(filtered.value, "filtered open");
  event.vscp_class = 20;
  check(VSCPWrite(filtered.value, &event, 100) == CANAL_ERROR_SUCCESS, "out-filter write");
  check(VSCPRead(filtered.value, &received, 50) == CANAL_ERROR_TIMEOUT, "out-filter");
  event.vscp_class = 10;
  check(VSCPWrite(filtered.value, &event, 100) == CANAL_ERROR_SUCCESS, "in-filter write");
  check(VSCPRead(filtered.value, &received, 50) == CANAL_ERROR_TIMEOUT, "in-filter");

  config.erase("filter");
  config["flags"] = 3;
  Handle disabled{ VSCPOpen(files.put("disabled.json", config.dump()).c_str(), guid) };
  check(disabled.value, "both directions disabled open");
  check(VSCPRead(disabled.value, &received, 0) == CANAL_ERROR_NOT_SUPPORTED, "both disabled read");
  check(VSCPWrite(disabled.value, &event, 0) == CANAL_ERROR_NOT_SUPPORTED, "both disabled write");

  auto waiting = std::async(std::launch::async, [&] {
    vscpEvent pending{};
    return VSCPRead(filtered.value, &pending, 10000);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  check(VSCPClose(filtered.value) == CANAL_ERROR_SUCCESS, "close while reading");
  check(waiting.wait_for(std::chrono::seconds(1)) == std::future_status::ready && waiting.get() == CANAL_ERROR_NOT_OPEN,
        "close must interrupt read");
  filtered.value = 0;
}

void
ipv6AndDnsTests(Files &files)
{
  const socket_t fd = socket(AF_INET6, SOCK_DGRAM, 0);
  check(fd != invalidSocket, "IPv6 socket");
  sockaddr_in6 address{};
  address.sin6_family = AF_INET6;
  address.sin6_addr   = in6addr_loopback;
  check(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "IPv6 bind");
  socklen_t size = sizeof(address);
  check(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) == 0, "IPv6 getsockname");
  closeSocket(fd);
  for (const std::string &host : { "[::1]", "localhost" }) {
    auto config = nlohmann::json{ { "interface", "udp://" + host + ":" + std::to_string(ntohs(address.sin6_port)) },
                                  { "flags", 0 },
                                  { "logging", { { "console-enable", false } } } };
    Handle driver{ VSCPOpen(files.put("endpoint.json", config.dump()).c_str(), guid) };
    check(driver.value, "IPv6/DNS open");
    vscpEvent event{}, received{};
    event.head         = VSCP_HEADER16_FRAME_VERSION_UNIX_NS;
    event.timestamp_ns = 1792680000123456789ULL;
    event.vscp_class   = 6000;
    check(VSCPWrite(driver.value, &event, 100) == CANAL_ERROR_SUCCESS, "IPv6/DNS write");
    check(VSCPRead(driver.value, &received, 1000) == CANAL_ERROR_SUCCESS && received.vscp_class == 6000,
          "IPv6/DNS roundtrip");
    delete[] received.pdata;
  }
}
}

int
main()
{
#ifdef _WIN32
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::cerr << "UDP test failure: WSAStartup\n";
    return 1;
  }
#endif
  crcInit();
  try {
    Files files;
    configurationTests(files);
    ipv6AndDnsTests(files);
    roundTrip(files, "none", 0, false);
    for (int encryption = 1; encryption <= 3; ++encryption) {
      const std::string mode = encryption == 1 ? "aes128" : encryption == 2 ? "aes192" : "aes256";
      roundTrip(files, mode, encryption, false);
      roundTrip(files, mode, encryption, true);
    }
    std::cout << "UDP tests passed\n";
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << "UDP test failure: " << error.what() << '\n';
    return 1;
  }
}
