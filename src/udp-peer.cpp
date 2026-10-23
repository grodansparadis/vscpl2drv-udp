// Copyright (c) 2000-2026 Ake Hedman and contributors, the VSCP Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "udp.h"
#include "vscpl2drv-udp.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <canal.h>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace {
void
usage()
{
  std::cout << "Usage: vscp-udp-peer send|receive --config FILE [options]\n"
               "\n"
               "Standalone VSCP UDP peer; uses the same JSON configuration as the driver.\n"
               "  --driver FILE    Load a driver DLL/.so/.dylib and use its Level II API\n"
               "                   instead of the built-in UDP transport (optional)\n"
               "  --count N        Number of events to send/receive (default 1)\n"
               "  --timeout MS     Per-event timeout, 0..3600000 (default 5000)\n"
               "Send options:\n"
               "  --class N        VSCP class, 0..65535 (default 10)\n"
               "  --type N         VSCP type, 0..65535 (default 6)\n"
               "  --guid GUID      Colon-separated event GUID (default all zeros)\n"
               "  --data HEX       Comma-separated hexadecimal bytes, e.g. 01,02,ff\n"
               "  --interval MS    Delay between sends, 0..3600000 (default 1000)\n"
               "\n"
               "Received events are printed as JSON, one per line. Set logging.console-enable\n"
               "to false in the config for clean JSON output. Peer diagnostics go to stderr.\n"
               "Use flags=2 in a sending config and flags=1 in a receiving config.\n"
               "AES and optional HMAC use security.encryption, security.key and security.hmac.\n";
}

unsigned long
number(const std::string &value, unsigned long maximum, const std::string &name, int base = 10)
{
  unsigned long result = 0;
  const auto parsed    = std::from_chars(value.data(), value.data() + value.size(), result, base);
  if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result > maximum) {
    throw std::runtime_error("Invalid " + name + ": " + value);
  }
  return result;
}

struct Options {
  bool send = false;
  std::string config;
  std::string driver;
  unsigned long count    = 1;
  unsigned long timeout  = 5000;
  unsigned long interval = 1000;
  uint16_t vscpClass     = 10;
  uint16_t vscpType      = 6;
  std::array<uint8_t, 16> guid{};
  std::vector<uint8_t> data;
};

Options
parse(int argc, char **argv)
{
  Options options;
  const std::string command = argv[1];
  if (command != "send" && command != "receive")
    throw std::runtime_error("Expected send or receive command");
  options.send = command == "send";
  std::set<std::string> seen;
  for (int i = 2; i < argc; i += 2) {
    const std::string name = argv[i];
    if (i + 1 == argc)
      throw std::runtime_error("Missing value for " + name);
    if (!seen.insert(name).second)
      throw std::runtime_error("Repeated option: " + name);
    const std::string value = argv[i + 1];
    if (name == "--config") {
      options.config = value;
    }
    else if (name == "--driver") {
      if (value.empty())
        throw std::runtime_error("--driver requires a library path");
      options.driver = value;
    }
    else if (name == "--count") {
      options.count = number(value, 1000000, name);
      if (!options.count)
        throw std::runtime_error("--count must be at least 1");
    }
    else if (name == "--timeout") {
      options.timeout = number(value, 3600000, name);
    }
    else if (!options.send) {
      throw std::runtime_error("Unknown receive option: " + name);
    }
    else if (name == "--interval") {
      options.interval = number(value, 3600000, name);
    }
    else if (name == "--class") {
      options.vscpClass = static_cast<uint16_t>(number(value, 65535, name));
    }
    else if (name == "--type") {
      options.vscpType = static_cast<uint16_t>(number(value, 65535, name));
    }
    else if (name == "--guid") {
      if (value.size() != 47)
        throw std::runtime_error("--guid must contain 16 colon-separated hexadecimal bytes");
      for (size_t byte = 0; byte < options.guid.size(); ++byte) {
        if (byte && value[byte * 3 - 1] != ':')
          throw std::runtime_error("Invalid GUID separator");
        options.guid[byte] = static_cast<uint8_t>(number(value.substr(byte * 3, 2), 255, name, 16));
      }
    }
    else if (name == "--data") {
      if (value.empty())
        continue;
      size_t start = 0;
      do {
        const auto comma = value.find(',', start);
        const auto byte  = value.substr(start, comma == std::string::npos ? comma : comma - start);
        if (byte.size() != 2)
          throw std::runtime_error("--data must contain comma-separated two-digit hexadecimal bytes");
        options.data.push_back(static_cast<uint8_t>(number(byte, 255, name, 16)));
        if (options.data.size() > VSCP_LEVEL2_MAXDATA)
          throw std::runtime_error("--data exceeds the VSCP maximum payload");
        if (comma == std::string::npos)
          break;
        start = comma + 1;
      } while (true);
    }
    else {
      throw std::runtime_error("Unknown option: " + name);
    }
  }
  if (options.config.empty())
    throw std::runtime_error("--config is required");
  return options;
}

class DriverLibrary {
public:
  explicit DriverLibrary(const std::string &path)
  {
#ifdef _WIN32
    const auto wide = std::filesystem::u8path(path).wstring();
    m_library       = LoadLibraryW(wide.c_str());
    if (!m_library)
      throw std::runtime_error("Cannot load driver " + path + ": " + std::system_category().message(GetLastError()));
#else
    m_library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_library)
      throw std::runtime_error("Cannot load driver " + path + ": " + dlerror());
#endif
  }

  ~DriverLibrary()
  {
#ifdef _WIN32
    if (!FreeLibrary(m_library))
      std::cerr << "Cannot unload driver: " << std::system_category().message(GetLastError()) << '\n';
#else
    if (dlclose(m_library))
      std::cerr << "Cannot unload driver: " << dlerror() << '\n';
#endif
  }

  DriverLibrary(const DriverLibrary &)            = delete;
  DriverLibrary &operator=(const DriverLibrary &) = delete;

  template<typename Function>
  Function symbol(const char *name)
  {
#ifdef _WIN32
    auto address = GetProcAddress(m_library, name);
    if (!address)
      throw std::runtime_error(std::string("Missing driver export ") + name + ": " +
                               std::system_category().message(GetLastError()));
#else
    dlerror();
    auto address      = dlsym(m_library, name);
    const char *error = dlerror();
    if (error || !address)
      throw std::runtime_error(std::string("Missing driver export ") + name + ": " + (error ? error : "null address"));
#endif
    // The OS loader returns an untyped address; convert to the exact API type.
    return reinterpret_cast<Function>(address);
  }

private:
#ifdef _WIN32
  HMODULE m_library = nullptr;
#else
  void *m_library = nullptr;
#endif
};

class DriverSession {
public:
  DriverSession(const std::string &library, const std::string &config)
    : m_library(library)
    , m_close(m_library.symbol<decltype(&VSCPClose)>("VSCPClose"))
    , m_write(m_library.symbol<decltype(&VSCPWrite)>("VSCPWrite"))
    , m_read(m_library.symbol<decltype(&VSCPRead)>("VSCPRead"))
  {
    const auto open    = m_library.symbol<decltype(&VSCPOpen)>("VSCPOpen");
    const auto version = m_library.symbol<decltype(&VSCPGetVersion)>("VSCPGetVersion");
    std::cerr << "Loaded driver version " << version() << '\n';
    m_handle = open(config.c_str(), "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00");
    if (!m_handle)
      throw std::runtime_error("VSCPOpen failed; check driver configuration and bind port");
  }

  ~DriverSession()
  {
    if (m_handle) {
      const int result = m_close(m_handle);
      if (result != CANAL_ERROR_SUCCESS)
        std::cerr << "VSCPClose failed, CANAL error " << result << '\n';
    }
  }

  DriverSession(const DriverSession &)            = delete;
  DriverSession &operator=(const DriverSession &) = delete;

  int write(const vscpEvent *event, unsigned long timeout) { return m_write(m_handle, event, timeout); }
  int read(vscpEvent *event, unsigned long timeout) { return m_read(m_handle, event, timeout); }

  void close()
  {
    const auto handle = m_handle;
    m_handle          = 0;
    const int result  = m_close(handle);
    if (result != CANAL_ERROR_SUCCESS)
      throw std::runtime_error("VSCPClose failed, CANAL error " + std::to_string(result));
  }

private:
  // The library outlives the handle, including during exception unwinding.
  DriverLibrary m_library;
  decltype(&VSCPClose) m_close;
  decltype(&VSCPWrite) m_write;
  decltype(&VSCPRead) m_read;
  long m_handle = 0;
};

void
printEvent(const vscpEvent &event)
{
  nlohmann::json data = nlohmann::json::array();
  for (size_t i = 0; i < event.sizeData; ++i)
    data.push_back(event.pdata[i]);
  cguid guid(event.GUID);
  std::string guidText;
  guid.toString(guidText);
  std::cout << nlohmann::json{
    { "head", event.head }, { "class", event.vscp_class },          { "type", event.vscp_type },
    { "guid", guidText },   { "timestamp_ns", event.timestamp_ns }, { "data", data }
  }.dump() << std::endl;
}
}

int
main(int argc, char **argv)
{
  if (argc == 2 && std::string(argv[1]) == "--help") {
    usage();
    return 0;
  }
  if (argc < 2) {
    usage();
    return 1;
  }
  try {
    const auto options = parse(argc, argv);
    // Configuration errors before the configured logger is ready go to stderr.
    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("udp-peer", console));
    std::unique_ptr<CUdp> peer;
    std::unique_ptr<DriverSession> driver;
    if (options.driver.empty()) {
      peer = std::make_unique<CUdp>();
      if (!peer->open(options.config, cguid{}))
        throw std::runtime_error("Cannot open UDP peer; check configuration and bind port");
    }
    else {
      driver = std::make_unique<DriverSession>(options.driver, options.config);
    }
    std::cerr << (driver ? "Driver" : "UDP peer") << " ready (" << (options.send ? "send" : "receive") << ")\n";
    for (unsigned long i = 0; i < options.count; ++i) {
      if (options.send) {
        vscpEvent event{};
        event.head         = VSCP_HEADER16_FRAME_VERSION_UNIX_NS;
        event.timestamp_ns = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
        event.vscp_class = options.vscpClass;
        event.vscp_type  = options.vscpType;
        std::copy(options.guid.begin(), options.guid.end(), event.GUID);
        event.sizeData = static_cast<uint16_t>(options.data.size());
        // write() does not take ownership or modify the payload.
        auto data        = options.data;
        event.pdata      = data.empty() ? nullptr : data.data();
        const int result = driver ? driver->write(&event, options.timeout) : peer->write(&event, options.timeout);
        if (result != CANAL_ERROR_SUCCESS)
          throw std::runtime_error("Send failed, CANAL error " + std::to_string(result));
        std::cerr << "Sent event " << i + 1 << '/' << options.count << '\n';
        if (i + 1 < options.count)
          std::this_thread::sleep_for(std::chrono::milliseconds(options.interval));
      }
      else {
        auto deleter = [](vscpEvent *event) { vscp_deleteEvent_v2(&event); };
        std::unique_ptr<vscpEvent, decltype(deleter)> event(new vscpEvent{}, deleter);
        const int result =
          driver ? driver->read(event.get(), options.timeout) : peer->read(event.get(), options.timeout);
        if (result != CANAL_ERROR_SUCCESS)
          throw std::runtime_error("Receive failed (timeout or closed), CANAL error " + std::to_string(result));
        printEvent(*event);
      }
    }
    if (driver)
      driver->close();
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << "vscp-udp-peer: " << error.what() << '\n';
    return 1;
  }
}
