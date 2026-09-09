// Internal helpers shared by the file modules: path normalization, the
// state-file root, and the tab-escaped field encoding used by the recent
// files/workspaces lists and the per-file fold-state map.
#pragma once

#include "jot/editor_models.h"
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

inline std::string escape_state_field(const std::string &input)
{
  std::string out;
  out.reserve(input.size());
  for (char c : input)
  {
    if (c == '\\')
    {
      out += "\\\\";
    }
    else if (c == '\t')
    {
      out += "\\t";
    }
    else if (c == '\n')
    {
      out += "\\n";
    }
    else
    {
      out.push_back(c);
    }
  }
  return out;
}

inline std::string unescape_state_field(const std::string &input)
{
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); i++)
  {
    if (input[i] == '\\' && i + 1 < input.size())
    {
      char n = input[i + 1];
      if (n == 't')
      {
        out.push_back('\t');
        i++;
        continue;
      }
      if (n == 'n')
      {
        out.push_back('\n');
        i++;
        continue;
      }
      if (n == '\\')
      {
        out.push_back('\\');
        i++;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

inline std::vector<std::string> split_state_tab(const std::string &line)
{
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= line.size())
  {
    size_t pos = line.find('\t', start);
    if (pos == std::string::npos)
    {
      parts.push_back(line.substr(start));
      break;
    }
    parts.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
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
    std::vector<std::string> parts = split_state_tab(line);
    if (parts.size() < 2)
    {
      continue;
    }
    const std::string key = normalize_existing_path(unescape_state_field(parts[0]));
    const std::string payload = unescape_state_field(parts[1]);
    if (!key.empty())
    {
      states[key] = payload;
    }
  }
  return states;
}
} // namespace file_internal
