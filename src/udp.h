// Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <semaphore.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <vector>

#include <guid.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <vscphelper.h>

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
  bool encode(const vscpEvent &event, std::vector<uint8_t> &packet);
  bool decode(const uint8_t *packet, size_t size, vscpEvent &event);

  std::string m_interface = "udp://255.255.255.255:9999";
  uint32_t m_flags        = 0;
  uint8_t m_encryption    = VSCP_ENCRYPTION_NONE;
  bool m_authenticated    = false;
  std::array<uint8_t, 32> m_key{};
  std::array<uint8_t, 32> m_hmac{};
  vscpEventFilter m_filterIn{};
  vscpEventFilter m_filterOut{};
  std::shared_ptr<spdlog::logger> m_logger;
  int m_socket = -1;
  sockaddr_storage m_destination{};
  socklen_t m_destinationSize = 0;
  std::atomic<bool> m_quit{ true };
  std::thread m_worker;
  std::mutex m_stateMutex;
  std::mutex m_receiveMutex;
  std::list<vscpEvent *> m_receiveList;
  sem_t m_receiveReady;
};
