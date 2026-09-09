// Recent files / workspaces tracking: state-file load, save, and the
// :openrecent picker.
#include "editor.h"
#include "folding.h"
#include "jot/app/file_internal.h"
#include "jot/lua/api.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

using namespace file_internal;

namespace
{

  std::string recent_files_path()
  {
    fs::path root = config_root_path();
    if (root.empty())
    {
      return "";
    }
    fs::path p = root / "configs" / "recent_files.txt";
    return p.string();
  }

  std::string recent_workspaces_path()
  {
    fs::path root = config_root_path();
    if (root.empty())
    {
      return "";
    }
    fs::path p = root / "configs" / "recent_workspaces.txt";
    return p.string();
  }
} // namespace
void Editor::track_recent_file(const std::string &path)
{
  const std::string normalized = normalize_existing_path(path);
  if (normalized.empty())
  {
    return;
  }

  recent_files.erase(std::remove(recent_files.begin(), recent_files.end(), normalized),
                     recent_files.end());
  recent_files.insert(recent_files.begin(), normalized);
  if ((int)recent_files.size() > kMaxRecentFiles)
  {
    recent_files.resize(kMaxRecentFiles);
  }
}
void Editor::track_recent_workspace(const std::string &path)
{
  const std::string normalized = normalize_existing_path(path);
  if (normalized.empty())
  {
    return;
  }

  std::error_code ec;
  if (!fs::exists(normalized, ec) || ec || !fs::is_directory(normalized, ec))
  {
    return;
  }

  recent_workspaces.erase(
      std::remove(recent_workspaces.begin(), recent_workspaces.end(), normalized),
      recent_workspaces.end());
  recent_workspaces.insert(recent_workspaces.begin(), normalized);
  if ((int)recent_workspaces.size() > kMaxRecentWorkspaces)
  {
    recent_workspaces.resize(kMaxRecentWorkspaces);
  }
}
void Editor::load_recent_files()
{
  recent_files.clear();
  const std::string path = recent_files_path();
  if (path.empty())
  {
    return;
  }

  std::ifstream file(path);
  if (!file.is_open())
  {
    return;
  }

  std::string line;
  std::set<std::string> seen;
  while (std::getline(file, line))
  {
    if (line.empty())
    {
      continue;
    }
    const std::string normalized = normalize_existing_path(line);
    if (normalized.empty())
    {
      continue;
    }
    std::error_code ec;
    if (!fs::exists(normalized, ec) || ec)
    {
      continue;
    }
    if (seen.find(normalized) != seen.end())
    {
      continue;
    }
    seen.insert(normalized);
    recent_files.push_back(normalized);
    if ((int)recent_files.size() >= kMaxRecentFiles)
    {
      break;
    }
  }
}
void Editor::load_recent_workspaces()
{
  recent_workspaces.clear();
  const std::string path = recent_workspaces_path();
  if (path.empty())
  {
    return;
  }

  std::ifstream file(path);
  if (!file.is_open())
  {
    return;
  }

  std::string line;
  std::set<std::string> seen;
  while (std::getline(file, line))
  {
    if (line.empty())
    {
      continue;
    }
    const std::string normalized = normalize_existing_path(line);
    if (normalized.empty())
    {
      continue;
    }
    std::error_code ec;
    if (!fs::exists(normalized, ec) || ec || !fs::is_directory(normalized, ec))
    {
      continue;
    }
    if (seen.find(normalized) != seen.end())
    {
      continue;
    }
    seen.insert(normalized);
    recent_workspaces.push_back(normalized);
    if ((int)recent_workspaces.size() >= kMaxRecentWorkspaces)
    {
      break;
    }
  }
}
void Editor::save_recent_files()
{
  const std::string path = recent_files_path();
  if (path.empty())
  {
    return;
  }

  std::error_code ec;
  fs::path output_path(path);
  fs::create_directories(output_path.parent_path(), ec);

  std::ofstream file(path);
  if (!file.is_open())
  {
    return;
  }
  for (const auto &entry : recent_files)
  {
    file << entry << '\n';
  }
}
void Editor::save_recent_workspaces()
{
  const std::string path = recent_workspaces_path();
  if (path.empty())
  {
    return;
  }

  std::error_code ec;
  fs::path output_path(path);
  fs::create_directories(output_path.parent_path(), ec);

  std::ofstream file(path);
  if (!file.is_open())
  {
    return;
  }
  for (const auto &entry : recent_workspaces)
  {
    file << entry << '\n';
  }
}
void Editor::open_recent_file(const std::string &query)
{
  if (recent_files.empty())
  {
    set_message("Recent files list is empty");
    return;
  }

  auto open_path = [&](const std::string &path)
  {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
      set_message("Recent file missing: " + path);
      recent_files.erase(std::remove(recent_files.begin(), recent_files.end(), path),
                         recent_files.end());
      return;
    }
    open_file(path);
    set_message("Opened recent: " + get_filename(path));
  };

  if (query.empty())
  {
    open_path(recent_files.front());
    return;
  }

  std::string query_trimmed = query;
  query_trimmed.erase(query_trimmed.begin(),
                      std::find_if(query_trimmed.begin(),
                                   query_trimmed.end(),
                                   [](unsigned char ch) { return !std::isspace(ch); }));
  query_trimmed.erase(std::find_if(query_trimmed.rbegin(),
                                   query_trimmed.rend(),
                                   [](unsigned char ch) { return !std::isspace(ch); })
                          .base(),
                      query_trimmed.end());

  bool numeric = !query_trimmed.empty();
  for (char c : query_trimmed)
  {
    if (!std::isdigit((unsigned char)c))
    {
      numeric = false;
      break;
    }
  }
  if (numeric)
  {
    try
    {
      long long idx = std::stoll(query_trimmed);
      if (idx >= 1 && idx <= (long long)recent_files.size())
      {
        open_path(recent_files[(size_t)idx - 1]);
        return;
      }
      set_message("Recent index out of range: " + query_trimmed);
      return;
    }
    catch (...)
    {
      set_message("Invalid recent index: " + query_trimmed);
      return;
    }
  }

  std::string needle = query_trimmed.empty() ? query : query_trimmed;
  std::transform(needle.begin(),
                 needle.end(),
                 needle.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  for (const auto &path : recent_files)
  {
    std::string haystack = path;
    std::transform(haystack.begin(),
                   haystack.end(),
                   haystack.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (haystack.find(needle) != std::string::npos)
    {
      open_path(path);
      return;
    }
  }

  set_message("No recent file matched: " + query);
}