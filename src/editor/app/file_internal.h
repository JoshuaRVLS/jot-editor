// Internal helpers shared by the file modules: path normalization, the
// state-file root, and the tab-separated field encoding used by the recent
// files/workspaces lists and the per-file fold-state map.
#pragma once

#include "base/string_util.h"
#include "editor/editor_models.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace file_internal
{
  // Capacity limits for the recent-files / recent-workspaces / closed-buffer lists.
  inline constexpr int kMaxRecentFiles = 50;
  inline constexpr int kMaxRecentWorkspaces = 30;
  inline constexpr int kMaxClosedBufferHistory = 20;
  inline constexpr int kMaxHomePins = 12;

  // Path to the per-file fold-state map (defined in fold_state.cpp).
  std::string file_fold_states_path();

  inline std::string normalize_existing_path(const std::string &path)
  {
    if (path.empty())
    {
      return "";
    }
    std::error_code ec;
    fs::path p(path);
    fs::path absolute = fs::absolute(p, ec);
    if (ec)
    {
      return path;
    }
    fs::path canonical = fs::weakly_canonical(absolute, ec);
    if (!ec)
    {
      return canonical.string();
    }
    return absolute.string();
  }

  inline fs::path config_root_path()
  {
    const char *override_home = std::getenv("JOT_CONFIG_HOME");
    if (override_home && *override_home)
    {
      return fs::path(override_home);
    }
#ifdef _WIN32
    const char *app_data = std::getenv("APPDATA");
    if (app_data && *app_data)
    {
      return fs::path(app_data) / "jot";
    }
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    if (!home || !*home)
    {
      return {};
    }
    return fs::path(home) / ".config" / "jot";
  }

  inline std::unordered_map<std::string, std::string> load_file_fold_state_map()
  {
    std::unordered_map<std::string, std::string> states;
    const std::string path = file_fold_states_path();
    if (path.empty())
    {
      return states;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
      return states;
    }

    std::string line;
    while (std::getline(file, line))
    {
      if (line.empty())
      {
        continue;
      }
      std::vector<std::string_view> parts = string_util::split(line, '\t');
      if (parts.size() < 2)
      {
        continue;
      }
      const std::string key = normalize_existing_path(string_util::unescape_field(parts[0]));
      const std::string payload = string_util::unescape_field(parts[1]);
      if (!key.empty())
      {
        states[key] = payload;
      }
    }
    return states;
  }
} // namespace file_internal
