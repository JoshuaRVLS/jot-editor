// Process spawn helpers for the DAP client: non-blocking fd setup, argv
// quoting, and the debug log file used to trace adapter traffic.
#include "tools/debugger/client.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
using ssize_t = intptr_t;
#define read _read
#define write _write
#define close _close
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace dbg_internal
{
  bool set_non_blocking(int fd)
  {
#ifdef _WIN32
    (void)fd;
    return true;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
      return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
  }

#ifdef _WIN32
  std::string windows_error_message(DWORD code)
  {
    char *text = nullptr;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                   | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr,
                               code,
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<LPSTR>(&text),
                               0,
                               nullptr);
    std::string message =
        len && text ? std::string(text, len) : "Windows error " + std::to_string(code);
    if (text)
    {
      LocalFree(text);
    }
    while (!message.empty()
           && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
    {
      message.pop_back();
    }
    return message;
  }

  std::string quote_windows_arg(const std::string &arg)
  {
    if (arg.empty())
    {
      return "\"\"";
    }
    bool needs_quotes = false;
    for (char c : arg)
    {
      if (std::isspace((unsigned char)c) || c == '"')
      {
        needs_quotes = true;
        break;
      }
    }
    if (!needs_quotes)
    {
      return arg;
    }
    std::string out = "\"";
    int backslashes = 0;
    for (char c : arg)
    {
      if (c == '\\')
      {
        backslashes++;
        continue;
      }
      if (c == '"')
      {
        out.append((size_t)(backslashes * 2 + 1), '\\');
        out.push_back(c);
        backslashes = 0;
        continue;
      }
      out.append((size_t)backslashes, '\\');
      backslashes = 0;
      out.push_back(c);
    }
    out.append((size_t)(backslashes * 2), '\\');
    out.push_back('"');
    return out;
  }

  std::string windows_command_line(const std::vector<std::string> &argv)
  {
    std::string out;
    for (size_t i = 0; i < argv.size(); i++)
    {
      if (i > 0)
      {
        out.push_back(' ');
      }
      out += quote_windows_arg(argv[i]);
    }
    return out;
  }

  bool fd_has_data(int fd)
  {
    intptr_t os_handle = _get_osfhandle(fd);
    if (os_handle == -1)
    {
      return false;
    }
    DWORD available = 0;
    if (!PeekNamedPipe(
            reinterpret_cast<HANDLE>(os_handle), nullptr, 0, nullptr, &available, nullptr))
    {
      return false;
    }
    return available > 0;
  }
#else
  bool fd_has_data(int)
  {
    return true;
  }
#endif

  std::string debug_log_path(const std::string &name)
  {
    const char *override_home = getenv("JOT_CONFIG_HOME");
    if (override_home && *override_home)
    {
      fs::path base = fs::path(override_home) / "logs";
      std::error_code ec;
      fs::create_directories(base, ec);
      std::string safe = name.empty() ? "session" : name;
      for (char &c : safe)
      {
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_')
        {
          c = '_';
        }
      }
      return (base / ("debug_" + safe + ".log")).string();
    }
#ifdef _WIN32
    const char *app_data = getenv("APPDATA");
    fs::path base = app_data && *app_data ? fs::path(app_data) / "jot" / "logs"
                                          : fs::temp_directory_path() / "jot-logs";
#else
    const char *home = getenv("HOME");
    fs::path base =
        home ? fs::path(home) / ".config" / "jot" / "logs" : fs::temp_directory_path() / "jot-logs";
#endif
    std::error_code ec;
    fs::create_directories(base, ec);
    std::string safe = name.empty() ? "session" : name;
    for (char &c : safe)
    {
      if (!std::isalnum((unsigned char)c) && c != '-' && c != '_')
      {
        c = '_';
      }
    }
    return (base / ("debug_" + safe + ".log")).string();
  }

  void append_log(const std::string &name, const std::string &prefix, const std::string &line)
  {
    std::ofstream log(debug_log_path(name), std::ios::app);
    if (!log.is_open())
    {
      return;
    }
    log << prefix << line << "\n";
  }
} // namespace dbg_internal
