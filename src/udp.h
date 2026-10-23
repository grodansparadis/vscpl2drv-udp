// Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <guid.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <vscphelper.h>

struct SecureKeyDeleter {
  void operator()(uint8_t *key) const noexcept;
};

using SecureKey = std::unique_ptr<uint8_t, SecureKeyDeleter>;

#ifdef _WIN32
using udp_socket_t = std::uintptr_t;
#else
using udp_socket_t = int;
#endif

class CUdp {
public:
  CUdp();
  ~CUdp();
  bool open(const std::string &path, const cguid &guid);
  void close();
  int write(const vscpEvent *event, unsigned long timeout);
  int read(vscpEvent *event, unsigned long timeout);

private:
  bool loadConfig(const std::string &path);
  bool setupSocket();
  void worker();
  void stopReceiver();
  bool encode(const vscpEvent &event, std::vector<uint8_t> &packet);
  bool decode(const uint8_t *packet, size_t size, vscpEvent &event);
  void clearKeys() noexcept;

  std::string m_interface = "udp://255.255.255.255:33333";
  uint32_t m_flags        = 0;
  uint8_t m_encryption    = VSCP_ENCRYPTION_NONE;
  bool m_authenticated    = false;
  SecureKey m_key;
  SecureKey m_hmac;
  vscpEventFilter m_filterIn{};
  vscpEventFilter m_filterOut{};
  std::shared_ptr<spdlog::logger> m_logger;
  udp_socket_t m_socket;
  std::array<uint8_t, 128> m_destination{};
  int m_destinationSize = 0;
  std::atomic<bool> m_quit{ true };
  std::thread m_worker;
  std::mutex m_stateMutex;
  std::mutex m_receiveMutex;
  std::list<vscpEvent *> m_receiveList;
  std::condition_variable m_receiveReady;
};
