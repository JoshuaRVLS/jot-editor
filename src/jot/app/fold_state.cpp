// Per-file fold-state persistence: save and restore fold ranges across
// sessions via the state-file map.
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

namespace file_internal
{

  std::string file_fold_states_path()
  {
    fs::path root = config_root_path();
    if (root.empty())
    {
      return "";
    }
    fs::path p = root / "configs" / "fold_states.txt";
    return p.string();
  }

  void write_file_fold_state_map(const std::unordered_map<std::string, std::string> &states)
  {
    const std::string path = file_fold_states_path();
    if (path.empty())
    {
      return;
    }

    std::error_code ec;
    fs::path output_path(path);
    fs::create_directories(output_path.parent_path(), ec);
    if (ec)
    {
      return;
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
    {
      return;
    }

    for (const auto &[key, payload] : states)
    {
      if (key.empty() || payload.empty())
      {
        continue;
      }
      file << escape_state_field(key) << '\t' << escape_state_field(payload) << '\n';
    }
  }
} // namespace file_internal
void Editor::save_file_fold_state(FileBuffer &buf)
{
  if (buf.filepath.empty() || buf.is_lazy())
  {
    return;
  }

  Folding::refresh_ranges(buf.fold_ranges, buf.lines, get_file_extension(buf.filepath));
  buf.folds_dirty = false;
  const std::string normalized = normalize_existing_path(buf.filepath);
  if (normalized.empty())
  {
    return;
  }

  auto states = load_file_fold_state_map();
  const std::string payload = Folding::encode_collapsed_ranges(buf.fold_ranges);
  if (payload.empty())
  {
    states.erase(normalized);
  }
  else
  {
    states[normalized] = payload;
  }
  write_file_fold_state_map(states);
}
void Editor::save_file_fold_states()
{
  auto states = load_file_fold_state_map();
  bool changed = false;

  for (auto &buf : buffers)
  {
    if (buf.filepath.empty() || buf.is_lazy())
    {
      continue;
    }
    Folding::refresh_ranges(buf.fold_ranges, buf.lines, get_file_extension(buf.filepath));
    buf.folds_dirty = false;
    const std::string normalized = normalize_existing_path(buf.filepath);
    if (normalized.empty())
    {
      continue;
    }
    const std::string payload = Folding::encode_collapsed_ranges(buf.fold_ranges);
    if (payload.empty())
    {
      changed = states.erase(normalized) > 0 || changed;
    }
    else if (states[normalized] != payload)
    {
      states[normalized] = payload;
      changed = true;
    }
  }

  if (changed)
  {
    write_file_fold_state_map(states);
  }
}
void Editor::restore_file_fold_state(FileBuffer &buf)
{
  if (buf.filepath.empty() || buf.is_lazy())
  {
    return;
  }

  const std::string normalized = normalize_existing_path(buf.filepath);
  if (normalized.empty())
  {
    return;
  }

  auto states = load_file_fold_state_map();
  auto it = states.find(normalized);
  if (it == states.end() || it->second.empty())
  {
    return;
  }

  Folding::refresh_ranges(buf.fold_ranges, buf.lines, get_file_extension(buf.filepath));
  buf.folds_dirty = false;
  Folding::apply_collapsed_ranges(buf.fold_ranges, Folding::decode_collapsed_ranges(it->second));
  while (buf.cursor.y > 0 && Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y))
  {
    buf.cursor.y--;
  }
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
}