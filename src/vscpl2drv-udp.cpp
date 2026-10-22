// Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project
// SPDX-License-Identifier: LGPL-2.0-or-later
#include "vscpl2drv-udp.h"
#include "udp.h"
#include "version.h"
#include <canal.h>
#include <limits>
#include <map>
#include <mutex>

namespace {
std::mutex handlesMutex;
std::map<long, std::shared_ptr<CUdp>> handles;
long nextHandle = 1681;

std::shared_ptr<CUdp>
getDriver(long handle)
{
  std::lock_guard<std::mutex> lock(handlesMutex);
  auto found = handles.find(handle);
  return found == handles.end() ? nullptr : found->second;
}
}

extern "C" long
VSCPOpen(const char *path, const char *guid)
{
  if (!path || !*path || !guid) {
    spdlog::error("VSCPOpen requires a configuration path and GUID");
    return 0;
  }
  try {
    auto driver = std::make_shared<CUdp>();
    if (!driver->open(path, cguid(guid)))
      return 0;
    std::lock_guard<std::mutex> lock(handlesMutex);
    if (nextHandle == std::numeric_limits<long>::max()) {
      spdlog::error("UDP driver handle space exhausted");
      return 0;
    }
    const long handle = nextHandle++;
    handles.emplace(handle, driver);
    return handle;
  }
  catch (const std::exception &error) {
    spdlog::error("VSCPOpen failed: {}", error.what());
    return 0;
  }
}

extern "C" int
VSCPClose(long handle)
{
  std::shared_ptr<CUdp> driver;
  {
    std::lock_guard<std::mutex> lock(handlesMutex);
    auto found = handles.find(handle);
    if (found == handles.end())
      return CANAL_ERROR_NOT_OPEN;
    driver = found->second;
    handles.erase(found);
  }
  driver->close();
  return CANAL_ERROR_SUCCESS;
}

extern "C" int
VSCPWrite(long handle, const vscpEvent *event, unsigned long timeout)
{
  try {
    auto driver = getDriver(handle);
    return driver ? driver->write(event, timeout) : CANAL_ERROR_NOT_OPEN;
  }
  catch (const std::exception &error) {
    spdlog::error("VSCPWrite failed: {}", error.what());
    return CANAL_ERROR_INTERNAL;
  }
}

extern "C" int
VSCPRead(long handle, vscpEvent *event, unsigned long timeout)
{
  try {
    auto driver = getDriver(handle);
    return driver ? driver->read(event, timeout) : CANAL_ERROR_NOT_OPEN;
  }
  catch (const std::exception &error) {
    spdlog::error("VSCPRead failed: {}", error.what());
    return CANAL_ERROR_INTERNAL;
  }
}

extern "C" unsigned long
VSCPGetVersion()
{
  return (static_cast<unsigned long>(MAJOR_VERSION & 0xfff) << 20) |
         (static_cast<unsigned long>(MINOR_VERSION & 0xf) << 16) | static_cast<unsigned long>(PATCH_VERSION & 0xffff);
}
