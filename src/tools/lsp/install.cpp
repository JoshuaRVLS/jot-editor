#include "tools/lsp/install.h"
#include "tools/shell_util.h"
#include "tools/string_util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{

  fs::path data_root()
  {
#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    if (local && *local)
      return fs::path(local) / "jot" / "lsp";
    const char *app_data = getenv("APPDATA");
    if (app_data && *app_data)
      return fs::path(app_data) / "jot" / "lsp";
    const char *home = getenv("USERPROFILE");
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
      return fs::path(xdg) / "jot" / "lsp";
    const char *home = getenv("HOME");
#endif
    return home ? fs::path(home) / ".local" / "share" / "jot" / "lsp" : fs::path();
  }

  // Directory the running executable lives in, or empty when it cannot be
  // resolved. A release tarball can be unpacked anywhere, so the payload tree
  // is looked up relative to the binary (`<exe>/../share/jot/payload`) rather
  // than trusting the prefix compiled into JOT_DEFAULT_DATA_DIR.
  fs::path exe_dir()
  {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
    {
      return fs::path(buf).parent_path();
    }
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
    {
      return fs::path(buf).parent_path();
    }
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
    {
      buf[n] = '\0';
      return fs::path(buf).parent_path();
    }
#endif
    return {};
  }

} // namespace

namespace LspInstall
{

  std::string install_root()
  {
    return data_root().string();
  }

  std::string bin_dir()
  {
    return (data_root() / "bin").string();
  }

  std::string platform_tag()
  {
#ifdef _WIN32
    return "win";
#elif defined(__APPLE__)
    return "mac";
#else
    return "linux";
#endif
  }

  std::string bundled_payload_dir(const std::string &bin_name)
  {
    if (bin_name.empty())
    {
      return "";
    }
    // Payload tree roots. JOT_LSP_PAYLOAD_DIR is authoritative when set (that
    // is what lets a packager or a test say "no payload here" instead of
    // falling through to the install tree); otherwise a relocated tarball and
    // a normal cmake --install prefix are tried.
    std::vector<fs::path> roots;
    if (const char *env = getenv("JOT_LSP_PAYLOAD_DIR"); env && *env)
    {
      roots.emplace_back(env);
    }
    else
    {
      const fs::path exe = exe_dir();
      if (!exe.empty())
      {
        roots.push_back(exe.parent_path() / "share" / "jot" / "payload");
      }
#ifdef JOT_DEFAULT_DATA_DIR
      roots.emplace_back(fs::path(JOT_DEFAULT_DATA_DIR) / "payload");
#endif
    }
    std::error_code ec;
    for (const auto &root : roots)
    {
      const fs::path candidate = root / bin_name;
      if (fs::is_directory(candidate, ec))
      {
        return candidate.string();
      }
    }
    return "";
  }

  std::string resolve_managed_bin(const std::string &bin_name)
  {
    fs::path candidate = data_root() / "bin" / bin_name;
    std::error_code ec;
    if (fs::is_regular_file(candidate, ec) || fs::is_symlink(candidate, ec))
    {
      return candidate.string();
    }
    return "";
  }

  bool is_installed(const std::string &id)
  {
    if (id.empty())
      return false;
    std::error_code ec;
    return fs::is_regular_file(data_root() / id / "receipt", ec);
  }

  std::vector<std::string> installed_ids()
  {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path root = data_root();
    if (!fs::is_directory(root, ec))
    {
      return out;
    }
    for (const auto &entry : fs::directory_iterator(root, ec))
    {
      if (!entry.is_directory(ec))
      {
        continue;
      }
      const std::string id = entry.path().filename().string();
      if (!id.empty() && is_installed(id))
      {
        out.push_back(id);
      }
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  std::string wrap_script(const std::string &server, const std::string &body)
  {
    // The body may carry its own `set -e` (fail-fast installs): run it in a
    // subshell so a failed step can never swallow the completion marker.
    const std::string script = "printf '[jot:lsp] start " + server + "\\n'; ( "
                               + body + " ); rc=$?; if [ \"$rc\" -eq 0 ]; then "
                                 "printf '[jot:lsp] success "
                               + server
                               + " exit=%s\\n' \"$rc\"; "
                                 "else printf '[jot:lsp] failed "
                               + server + " exit=%s\\n' \"$rc\"; fi";
    return "/bin/sh -lc " + shell_util::shell_quote(script);
  }

  bool parse_marker(const std::string &line, Marker &marker)
  {
    const std::string prefix = "[jot:lsp] ";
    const size_t marker_pos = line.find(prefix);
    if (marker_pos == std::string::npos)
    {
      return false;
    }

    const std::string rest = string_util::trim_copy(line.substr(marker_pos + prefix.size()));
    const size_t first_space = rest.find(' ');
    if (first_space == std::string::npos)
    {
      return false;
    }
    marker.phase = rest.substr(0, first_space);
    const std::string details = string_util::trim_copy(rest.substr(first_space + 1));
    const size_t exit_pos = details.find(" exit=");
    marker.server = exit_pos == std::string::npos ? details : details.substr(0, exit_pos);
    marker.exit_code = -1;
    if (exit_pos != std::string::npos)
    {
      const std::string exit_value = string_util::trim_copy(details.substr(exit_pos + 6));
      if (!exit_value.empty())
      {
        marker.exit_code = std::atoi(exit_value.c_str());
      }
    }
    return !marker.phase.empty() && !marker.server.empty();
  }

} // namespace LspInstall