#include "tools/lsp/install.h"

#include <cctype>
#include <cstdlib>

namespace {

std::string shell_quote(const std::string &value) {
  std::string quoted = "'";
  for (char ch : value) {
    if (ch == '\'') {
      quoted += "'\"'\"'";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string trim_copy(const std::string &value) {
  size_t start = 0;
  while (start < value.size() && std::isspace((unsigned char)value[start])) {
    start++;
  }
  size_t end = value.size();
  while (end > start && std::isspace((unsigned char)value[end - 1])) {
    end--;
  }
  return value.substr(start, end - start);
}

} // namespace

namespace LspInstall {

Command command_for_server(const std::string &server) {
  Command install;
  install.server = server;
  if (server == "python") {
    install.command = "python3 -m pip install --user -U python-lsp-server";
  } else if (server == "typescript") {
    install.command = "npm install -g typescript typescript-language-server";
  } else if (server == "bash") {
    install.command = "npm install -g bash-language-server";
  } else if (server == "html") {
    install.command = "npm install -g vscode-langservers-extracted";
  }
  install.supported = !install.command.empty();
  if (install.supported) {
    install.message = "LSP install started: " + server;
  }
  return install;
}

std::string terminal_command(const Command &install) {
  if (!install.supported) {
    return "";
  }
  const std::string script =
      "printf '[jot:lsp] start " + install.server + "\\n'; " +
      install.command +
      "; rc=$?; if [ \"$rc\" -eq 0 ]; then "
      "printf '[jot:lsp] success " + install.server + " exit=%s\\n' \"$rc\"; "
      "else printf '[jot:lsp] failed " + install.server +
      " exit=%s\\n' \"$rc\"; fi";
  return "/bin/sh -lc " + shell_quote(script);
}

bool parse_marker(const std::string &line, Marker &marker) {
  const std::string prefix = "[jot:lsp] ";
  const size_t marker_pos = line.find(prefix);
  if (marker_pos == std::string::npos) {
    return false;
  }

  const std::string rest = trim_copy(line.substr(marker_pos + prefix.size()));
  const size_t first_space = rest.find(' ');
  if (first_space == std::string::npos) {
    return false;
  }
  marker.phase = rest.substr(0, first_space);
  const std::string details = trim_copy(rest.substr(first_space + 1));
  const size_t exit_pos = details.find(" exit=");
  marker.server = exit_pos == std::string::npos ? details
                                                  : details.substr(0, exit_pos);
  marker.exit_code = -1;
  if (exit_pos != std::string::npos) {
    const std::string exit_value = trim_copy(details.substr(exit_pos + 6));
    if (!exit_value.empty()) {
      marker.exit_code = std::atoi(exit_value.c_str());
    }
  }
  return !marker.phase.empty() && !marker.server.empty();
}

} // namespace LspInstall
