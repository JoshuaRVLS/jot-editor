#include "lsp/install.h"
#include "base/exe_path.h"
#include "base/shell_util.h"
#include "base/string_util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{

  // Server ids are registry slugs, but they reach a filename here, so anything
  // that could escape the scripts dir or trip cmd's parser becomes a dash.
  std::string script_file_stem(const std::string &server)
  {
    std::string out;
    out.reserve(server.size());
    for (char c : server)
    {
      const bool safe = std::isalnum((unsigned char)c) || c == '-' || c == '_';
      out.push_back(safe ? c : '-');
    }
    return out.empty() ? std::string("server") : out;
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

  // Directory the generated Windows batch files live in, beside the install
  // tree this module already owns. The temp dir is the fallback for an
  // environment with no data root at all (no LOCALAPPDATA/APPDATA/USERPROFILE).
  fs::path script_dir()
  {
    const fs::path root = data_root();
    fs::path dir = root.empty() ? fs::temp_directory_path() : root / "scripts";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return ec ? fs::temp_directory_path() : dir;
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
    return shell_util::install_platform();
  }

  ScriptShell default_shell()
  {
    return platform_tag() == "win" ? ScriptShell::Cmd : ScriptShell::Sh;
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
      // A release tarball can be unpacked anywhere, so the tree the binary
      // itself sits in is tried before the compiled-in prefix
      // (base/exe_path.h).
      const fs::path data = exe_path::data_dir();
      if (!data.empty())
      {
        roots.push_back(data / "payload");
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
    if (bin_name.empty())
    {
      return "";
    }
    const fs::path dir = data_root() / "bin";
    std::vector<fs::path> candidates = {dir / bin_name};
    if (platform_tag() == "win")
    {
      // A managed bin is never the bare POSIX name on Windows: clangd comes as
      // clangd.exe and npm publishes <name>.cmd. The extension is what the
      // spawner needs, so it is resolved here rather than guessed later.
      for (const char *ext : {".exe", ".cmd", ".bat"})
      {
        candidates.push_back(dir / (bin_name + ext));
      }
    }
    std::error_code ec;
    for (const auto &candidate : candidates)
    {
      if (fs::is_regular_file(candidate, ec) || fs::is_symlink(candidate, ec))
      {
        return candidate.string();
      }
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
    return wrap_script(server, body, default_shell());
  }

  std::string wrap_script(const std::string &server, const std::string &body, ScriptShell shell)
  {
    if (shell == ScriptShell::Cmd)
    {
      // cmd.exe has no `set -e`, so a script cannot be one command line and
      // still fail loudly. A batch file can `exit /b` on the first bad step,
      // which `cmd /c <file>` reports, making the marker below honest.
      static unsigned long long counter = 0;
      counter++;
      const std::string stem = script_file_stem(server);
      const fs::path file =
          script_dir() / ("install_" + stem + "_" + std::to_string(counter) + ".cmd");
      std::ofstream out(file, std::ios::binary | std::ios::trunc);
      if (!out)
      {
        return "echo [jot:lsp] failed " + server + " exit=1";
      }
      // CRLF: cmd's parser is line oriented, and a bare LF misparses some
      // builtins. The body arrives with LF endings from Lua.
      for (char c : body)
      {
        if (c == '\n')
        {
          out << "\r\n";
        }
        else if (c != '\r')
        {
          out << c;
        }
      }
      out << "\r\n";
      out.close();
      if (!out)
      {
        return "echo [jot:lsp] failed " + server + " exit=1";
      }
      return "echo [jot:lsp] start " + server + " && cmd /c "
             + shell_util::shell_quote(file.string()) + " && echo [jot:lsp] success " + server
             + " exit=0 || echo [jot:lsp] failed " + server + " exit=1";
    }
    // The body may carry its own `set -e` (fail-fast installs): run it in a
    // subshell so a failed step can never swallow the completion marker.
    const std::string script = "printf '[jot:lsp] start " + server + "\\n'; ( " + body
                               + " ); rc=$?; if [ \"$rc\" -eq 0 ]; then "
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