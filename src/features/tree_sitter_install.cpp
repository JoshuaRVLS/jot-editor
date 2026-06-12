#include "tree_sitter_install.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {
std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return value;
}

std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::string package_for_language(const std::string &language) {
  if (language == "cpp") {
    return "tree-sitter-cpp";
  }
  return "tree-sitter-" + language;
}
} // namespace

namespace TreeSitterInstall {
const std::vector<std::string> &supported_languages() {
  static const std::vector<std::string> languages = {
      "c",    "cpp",  "python", "javascript", "typescript",
      "rust", "go",   "json",   "html",       "css",
      "bash", "lua",  "markdown", "toml",     "yaml"};
  return languages;
}

bool is_supported_language(const std::string &language) {
  std::string normalized = lower_copy(language);
  const auto &languages = supported_languages();
  return std::find(languages.begin(), languages.end(), normalized) !=
         languages.end();
}

TreeSitterInstallCommand command_for_language(const std::string &language) {
  TreeSitterInstallCommand result;
  result.language = lower_copy(language);
  if (!is_supported_language(result.language)) {
    result.message = "Unsupported Tree-sitter language: " + language;
    return result;
  }

  const std::string pkg = package_for_language(result.language);
  const std::string quoted_pkg = shell_quote(pkg);
  std::ostringstream cmd;
  cmd << "set -e; ";
  cmd << "echo '[jot:treesitter] Installing " << pkg << "'; ";
  cmd << "if command -v pacman >/dev/null 2>&1; then ";
  cmd << "sudo pacman -Sy --noconfirm " << quoted_pkg << "; ";
  cmd << "elif command -v apt-get >/dev/null 2>&1; then ";
  cmd << "sudo apt-get update && sudo apt-get install -y libtree-sitter-dev; ";
  cmd << "elif command -v dnf >/dev/null 2>&1; then ";
  cmd << "sudo dnf install -y tree-sitter-devel; ";
  cmd << "elif command -v yum >/dev/null 2>&1; then ";
  cmd << "sudo yum install -y tree-sitter-devel; ";
  cmd << "elif command -v zypper >/dev/null 2>&1; then ";
  cmd << "sudo zypper --non-interactive install tree-sitter-devel; ";
  cmd << "elif command -v brew >/dev/null 2>&1; then ";
  cmd << "brew install tree-sitter; ";
  cmd << "else ";
  cmd << "echo '[jot:treesitter] No supported package manager found.'; ";
  cmd << "exit 1; ";
  cmd << "fi; ";
  cmd << "echo '[jot:treesitter] Install finished. Rebuild and restart jot to "
         "enable newly installed grammars.'";

  result.supported = true;
  result.command = cmd.str();
  result.message =
      "Installing Tree-sitter " + result.language + " in terminal";
  return result;
}
} // namespace TreeSitterInstall
