#include "config.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

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
  settings["auto_detect_indent"] = "false";
  settings["show_line_numbers"] = "true";
  settings["word_wrap"] = "false";
  settings["cursor_style"] = "block";
  settings["render_fps"] = "120";
  settings["idle_fps"] = "60";
  settings["lsp_change_debounce_ms"] = "120";
  settings["terminal_height"] = "10";
}

void Config::parse_line(const std::string &line) {
  size_t eq = line.find('=');
  if (eq == std::string::npos)
    return;

  std::string key = line.substr(0, eq);
  std::string value = line.substr(eq + 1);

  key.erase(0, key.find_first_not_of(" \t"));
  key.erase(key.find_last_not_of(" \t") + 1);
  value.erase(0, value.find_first_not_of(" \t"));
  value.erase(value.find_last_not_of(" \t") + 1);

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
    if (line.empty() || line[0] == '#')
      continue;
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

bool Config::get_bool(const std::string &key, bool default_val) {
  auto it = settings.find(key);
  if (it == settings.end())
    return default_val;
  std::string val = it->second;
  std::transform(val.begin(), val.end(), val.begin(), ::tolower);
  return val == "true" || val == "1" || val == "yes";
}
