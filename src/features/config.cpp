#include "config.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
std::string trim_copy(const std::string &s) {
  size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t");
  return s.substr(start, end - start + 1);
}

std::string strip_inline_comment(const std::string &line) {
  bool in_single = false;
  bool in_double = false;
  for (size_t i = 0; i < line.size(); i++) {
    char c = line[i];
    if (c == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (c == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (!in_single && !in_double && c == '#') {
      return line.substr(0, i);
    }
  }
  return line;
}
} // namespace

Config::Config() {
  const char *home = getenv("HOME");
  if (home) {
    std::string config_root = std::string(home) + "/.config/jot";
    fs::create_directories(config_root + "/configs");
    config_path = config_root + "/configs/settings.conf";
  }
  load_defaults();
}

void Config::load_defaults() {
  settings["explorer_width"] = "25";
  settings["minimap_width"] = "15";
  settings["show_explorer"] = "true";
  settings["show_minimap"] = "true";
  settings["tab_size"] = "2";
  settings["auto_indent"] = "true";
  settings["auto_save"] = "false";
  settings["auto_save_interval_ms"] = "2000";
  settings["prettier_on_save"] = "true";
  settings["clang_format_on_save"] = "true";
  settings["auto_detect_indent"] = "false";
  settings["show_line_numbers"] = "true";
  settings["word_wrap"] = "false";
  settings["cursor_style"] = "block";
  settings["render_fps"] = "120";
  settings["idle_fps"] = "60";
  settings["lsp_change_debounce_ms"] = "120";
  settings["lsp_completion_max_items"] = "8";
  settings["terminal_height"] = "10";
}

void Config::parse_line(const std::string &line) {
  std::string normalized = strip_inline_comment(line);
  normalized = trim_copy(normalized);
  if (normalized.empty()) {
    return;
  }

  size_t eq = normalized.find('=');
  if (eq == std::string::npos)
    return;

  std::string key = trim_copy(normalized.substr(0, eq));
  std::string value = trim_copy(normalized.substr(eq + 1));

  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }

  if (!key.empty()) {
    settings[key] = value;
  }
}

void Config::load() {
  if (config_path.empty())
    return;

  std::ifstream file(config_path);
  if (!file.is_open()) {
    const char *home = getenv("HOME");
    if (home) {
      std::string legacy_path = std::string(home) + "/.config/jot/config";
      file.open(legacy_path);
    }
  }
  if (!file.is_open()) {
    save();
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    parse_line(line);
  }
  file.close();
}

void Config::save() {
  if (config_path.empty())
    return;

  std::ofstream file(config_path);
  if (!file.is_open())
    return;

  file << "# jot configuration file\n\n";

  for (const auto &[key, value] : settings) {
    file << key << "=" << value << "\n";
  }

  file.close();
}

std::string Config::get(const std::string &key,
                        const std::string &default_val) {
  auto it = settings.find(key);
  return (it != settings.end()) ? it->second : default_val;
}

void Config::set(const std::string &key, const std::string &value) {
  settings[key] = value;
}

void Config::set_int(const std::string &key, int value) {
  settings[key] = std::to_string(value);
}

void Config::set_bool(const std::string &key, bool value) {
  settings[key] = value ? "true" : "false";
}

int Config::get_int(const std::string &key, int default_val) {
  auto it = settings.find(key);
  if (it == settings.end())
    return default_val;
  try {
    return std::stoi(it->second);
  } catch (...) {
    return default_val;
  }
}

double Config::get_double(const std::string &key, double default_val) {
  auto it = settings.find(key);
  if (it == settings.end()) {
    return default_val;
  }
  try {
    return std::stod(it->second);
  } catch (...) {
    return default_val;
  }
}

bool Config::get_bool(const std::string &key, bool default_val) {
  auto it = settings.find(key);
  if (it == settings.end())
    return default_val;
  std::string val = it->second;
  std::transform(val.begin(), val.end(), val.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  if (val == "true" || val == "1" || val == "yes" || val == "on") {
    return true;
  }
  if (val == "false" || val == "0" || val == "no" || val == "off") {
    return false;
  }
  return default_val;
}

std::vector<std::string> Config::get_list(const std::string &key,
                                          char delimiter,
                                          bool trim_items) {
  auto it = settings.find(key);
  if (it == settings.end() || it->second.empty()) {
    return {};
  }

  std::vector<std::string> out;
  std::stringstream ss(it->second);
  std::string item;
  while (std::getline(ss, item, delimiter)) {
    if (trim_items) {
      item = trim_copy(item);
    }
    if (!item.empty()) {
      out.push_back(item);
    }
  }
  return out;
}

bool Config::has(const std::string &key) const {
  return settings.find(key) != settings.end();
}

void Config::unset(const std::string &key) {
  settings.erase(key);
}
