#include "command_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace CommandLineUtils {
namespace fs = std::filesystem;

std::string to_lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string shell_quote(const std::string &value) {
  std::string out = "'";
  out.reserve(value.size() + 8);
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

std::string first_line_copy(const std::string &text) {
  size_t end = text.find_first_of("\r\n");
  if (end == std::string::npos) {
    return text;
  }
  return text.substr(0, end);
}

std::string limit_lines(const std::string &text, int max_lines) {
  if (max_lines <= 0) {
    return "";
  }
  std::istringstream iss(text);
  std::string out;
  std::string line;
  int count = 0;
  while (count < max_lines && std::getline(iss, line)) {
    out += line;
    out.push_back('\n');
    count++;
  }
  if (iss.good()) {
    out += "...";
  } else if (!out.empty() && out.back() == '\n') {
    out.pop_back();
  }
  return out;
}

const std::vector<std::string> &ex_commands() {
  static const std::vector<std::string> commands = {
      "q",      "quit",     "q!",       "quit!",   "w",      "write",
      "wq",     "x",        "xit",      "e",       "edit",   "open",
      "new",    "enew",     "bd",       "bdelete", "close",  "sp",
      "split",  "splith",   "vsp",      "splitv", "splitleft", "splitright",
      "splitup", "splitdown", "spleft", "spright", "spup", "spdown",
      "bn",     "nextpane", "bp",       "prevpane", "theme", "colorscheme", "colo", "minimap",
      "term",   "terminal", "termnew",  "terminalnew", "search",
      "find",   "ff",       "mkfile",   "mkdir",   "rename", "rm",
      "format", "trim",     "upper",    "lower",  "sortlines", "sortdesc",
      "reverselines", "uniquelines", "shufflelines", "joinlines", "dupe",
      "trimblank", "copypath", "copyname", "datetime", "stats", "replace",
      "replacei", "replaceword", "replacere", "surround", "unsurround",
      "incnum", "decnum",
      "line", "goto",        "resizeleft",
      "resizeright", "resizeup", "resizedown", "lspstart", "lspstatus",
      "lspstop", "lsprestart", "lspinstall", "lspremove", "lspmanager",
      "gitstatus", "gitdiff", "gitblame",
      "gitrefresh", "recent", "openrecent", "reopen",
      "reopenlast", "autosave", "help", "h"};
  return commands;
}

bool starts_with_icase(const std::string &value, const std::string &prefix) {
  if (prefix.size() > value.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); i++) {
    if (std::tolower((unsigned char)value[i]) !=
        std::tolower((unsigned char)prefix[i])) {
      return false;
    }
  }
  return true;
}

bool command_takes_argument(const std::string &cmd) {
  const std::string lc = to_lower_copy(cmd);
  return lc == "e" || lc == "edit" || lc == "open" || lc == "w" ||
         lc == "write" || lc == "wq" || lc == "x" || lc == "xit" ||
         lc == "theme" || lc == "colorscheme" ||
         lc == "colo" || lc == "line" || lc == "goto" ||
         lc == "openrecent" || lc == "autosave" || lc == "help" ||
         lc == "h" || lc == "gitdiff" || lc == "find" || lc == "ff" ||
         lc == "mkfile" || lc == "mkdir" || lc == "rename" || lc == "rm" ||
         lc == "lspinstall" || lc == "lspremove" || lc == "replace" ||
         lc == "replacei" || lc == "replaceword" || lc == "replacere" ||
         lc == "surround";
}

bool parse_line_col(const std::string &s, int &line_out, int &col_out) {
  if (s.empty())
    return false;

  size_t colon = s.find(':');
  std::string line_part = (colon == std::string::npos) ? s : s.substr(0, colon);
  std::string col_part =
      (colon == std::string::npos) ? "" : s.substr(colon + 1);

  if (line_part.empty())
    return false;
  for (char c : line_part) {
    if (!std::isdigit((unsigned char)c))
      return false;
  }
  if (!col_part.empty()) {
    for (char c : col_part) {
      if (!std::isdigit((unsigned char)c))
        return false;
    }
  }

  line_out = std::max(1, std::stoi(line_part));
  col_out = col_part.empty() ? 1 : std::max(1, std::stoi(col_part));
  return true;
}

std::string trim_copy(const std::string &s) {
  const size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos)
    return "";
  const size_t end = s.find_last_not_of(" \t");
  return s.substr(start, end - start + 1);
}

std::vector<std::string> complete_path_argument(const std::string &arg) {
  std::vector<std::string> out;

  std::string dir_part;
  std::string name_prefix = arg;
  size_t slash = arg.find_last_of("/\\");
  if (slash != std::string::npos) {
    dir_part = arg.substr(0, slash + 1);
    name_prefix = arg.substr(slash + 1);
  }

  std::string list_dir = dir_part.empty() ? "." : dir_part;
  std::error_code ec;
  fs::path base(list_dir);
  if (!fs::exists(base, ec) || !fs::is_directory(base, ec)) {
    return out;
  }

  for (const auto &entry : fs::directory_iterator(base, ec)) {
    if (ec) {
      break;
    }

    std::string name = entry.path().filename().string();
    if (name.empty()) {
      continue;
    }
    if (!name_prefix.empty() && !starts_with_icase(name, name_prefix)) {
      continue;
    }

    std::string suggestion = dir_part + name;
    if (entry.is_directory(ec)) {
      suggestion += "/";
    }
    out.push_back(suggestion);
  }

  std::sort(out.begin(), out.end(),
            [](const std::string &a, const std::string &b) {
              return to_lower_copy(a) < to_lower_copy(b);
            });

  if (out.size() > 64) {
    out.resize(64);
  }
  return out;
}

} // namespace CommandLineUtils
