#ifndef LSP_INSTALL_H
#define LSP_INSTALL_H

#include <string>

namespace LspInstall {

struct Command {
  std::string server;
  std::string command;
  std::string message;
  bool supported = false;
};

struct Marker {
  std::string phase;
  std::string server;
  int exit_code = -1;
};

Command command_for_server(const std::string &server);
std::string terminal_command(const Command &install);
bool parse_marker(const std::string &line, Marker &marker);

} // namespace LspInstall

#endif
