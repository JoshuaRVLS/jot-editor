#ifndef LSP_INSTALL_H
#define LSP_INSTALL_H

#include <string>

namespace LspInstall
{

  struct Marker
  {
    std::string phase;
    std::string server;
    int exit_code = -1;
  };

  // Host half of the Lua-driven installer (src/lua/lsp/install.lua). The Lua
  // side owns the package registry and the per-manager install scripts; this
  // side owns the install root on disk, the [jot:lsp] marker protocol that
  // the background-job poll loop parses, and receipt/binary lookups.
  std::string install_root();   // <data>/lsp
  std::string bin_dir();        // <data>/lsp/bin
  std::string platform_tag();   // "linux" | "mac" | "win"
  // Absolute path of a managed binary when installed there, else "".
  std::string resolve_managed_bin(const std::string &bin_name);
  // True when the package dir carries a receipt (a completed install).
  bool is_installed(const std::string &id);
  // Wraps a raw install/remove script body with the start/success/failed
  // marker protocol and returns the full shell command line to run.
  std::string wrap_script(const std::string &server, const std::string &body);
  bool parse_marker(const std::string &line, Marker &marker);

} // namespace LspInstall

#endif