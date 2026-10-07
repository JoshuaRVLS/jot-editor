#ifndef LSP_INSTALL_H
#define LSP_INSTALL_H

#include <string>
#include <vector>

namespace LspInstall
{

  struct Marker
  {
    std::string phase;
    std::string server;
    int exit_code = -1;
  };

  // Which shell the installer scripts are written for. Windows has no POSIX
  // shell (and no /bin/sh), so its scripts are cmd.exe batch instead.
  enum class ScriptShell
  {
    Sh,
    Cmd,
  };

  // Host half of the Lua-driven installer (runtime/lua/lsp/install.lua). The Lua
  // side owns the package registry and the per-manager install scripts; this
  // side owns the install root on disk, the [jot:lsp] marker protocol that
  // the background-job poll loop parses, and receipt/binary lookups.
  std::string install_root(); // <data>/lsp
  std::string bin_dir();      // <data>/lsp/bin
  std::string platform_tag(); // "linux" | "mac" | "win"
  // ScriptShell the current platform's install scripts use.
  ScriptShell default_shell();
  // Absolute path of a managed binary when installed there, else "".
  std::string resolve_managed_bin(const std::string &bin_name);
  // Directory a packaged copy of this binary ships in
  // (share/jot/payload/<bin_name>), or "" when the package carries none. A
  // release vendors clangd this way, so the installer can link it instead of
  // downloading it. $JOT_LSP_PAYLOAD_DIR is authoritative when set; without it
  // the tree beside the executable and then the compiled-in prefix are tried.
  std::string bundled_payload_dir(const std::string &bin_name);
  // True when the package dir carries a receipt (a completed install).
  bool is_installed(const std::string &id);
  // Ids of every server with a receipt under the install root, sorted.
  std::vector<std::string> installed_ids();
  // Wraps a raw install/remove script body with the start/success/failed marker
  // protocol and returns the full command line to run. For ScriptShell::Cmd the
  // body is first written to a batch file beside the install tree (see the
  // implementation for why cmd.exe needs one) and the returned line runs it.
  std::string wrap_script(const std::string &server, const std::string &body);
  std::string wrap_script(const std::string &server, const std::string &body, ScriptShell shell);
  bool parse_marker(const std::string &line, Marker &marker);

} // namespace LspInstall

#endif