#include "tools/lsp/install.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{

  std::string shell_quote(const std::string &value)
  {
    std::string quoted = "'";
    for (char ch : value)
    {
      if (ch == '\'')
      {
        quoted += "'\"'\"'";
      }
      else
      {
        quoted.push_back(ch);
      }
    }
    quoted.push_back('\'');
    return quoted;
  }

  std::string trim_copy(const std::string &value)
  {
    size_t start = 0;
    while (start < value.size() && std::isspace((unsigned char)value[start]))
    {
      start++;
    }
    size_t end = value.size();
    while (end > start && std::isspace((unsigned char)value[end - 1]))
    {
      end--;
    }
    return value.substr(start, end - start);
  }

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
    return "/bin/sh -lc " + shell_quote(script);
  }

  bool parse_marker(const std::string &line, Marker &marker)
  {
    const std::string prefix = "[jot:lsp] ";
    const size_t marker_pos = line.find(prefix);
    if (marker_pos == std::string::npos)
    {
      return false;
    }

    const std::string rest = trim_copy(line.substr(marker_pos + prefix.size()));
    const size_t first_space = rest.find(' ');
    if (first_space == std::string::npos)
    {
      return false;
    }
    marker.phase = rest.substr(0, first_space);
    const std::string details = trim_copy(rest.substr(first_space + 1));
    const size_t exit_pos = details.find(" exit=");
    marker.server = exit_pos == std::string::npos ? details : details.substr(0, exit_pos);
    marker.exit_code = -1;
    if (exit_pos != std::string::npos)
    {
      const std::string exit_value = trim_copy(details.substr(exit_pos + 6));
      if (!exit_value.empty())
      {
        marker.exit_code = std::atoi(exit_value.c_str());
      }
    }
    return !marker.phase.empty() && !marker.server.empty();
  }

} // namespace LspInstall