#include "telescope.h"
#include "tools/string_util.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace
{
  constexpr int kMaxDepth = 4;
  constexpr int kMaxResults = 2000;
  constexpr int kMaxCandidates = 20000;
  constexpr int kMaxPreviewLines = 120;
  constexpr int kMaxPreviewLineLength = 240;
  constexpr std::uintmax_t kMaxPreviewFileBytes = 1024 * 1024; // 1MB

  bool should_skip_dir_name(const std::string &name)
  {
    static const std::unordered_set<std::string> kSkipped = {".git",
                                                             ".svn",
                                                             ".hg",
                                                             "node_modules",
                                                             "dist",
                                                             "build",
                                                             ".cache",
                                                             "__pycache__",
                                                             ".venv",
                                                             "target"};
    return kSkipped.find(name) != kSkipped.end();
  }

  bool should_skip_name(const std::string &name)
  {
    return name.empty() || name[0] == '.';
  }

  // True when a filename looks like a generated duplicate / copy that should
  // rank below the clean original: "foo (1).c", "foo - Copy.c", "foo copy.c",
  // "foo_copy.c", "foo (copy).c". Only the basename (no extension) is
  // inspected so a real directory literally named "foo (1)" is not affected.
  bool name_looks_generated_duplicate(const std::string &name)
  {
    std::string s = string_util::lower_copy(name);
    const size_t dot = s.rfind('.');
    if (dot != std::string::npos && dot > 0)
    {
      s = s.substr(0, dot);
    }
    // OS download/collision copies: "name (1)", "name (42)", "name[1]".
    if (s.size() > 3 && s.back() == ')')
    {
      const size_t open = s.rfind(" (");
      if (open != std::string::npos
          && s.find_first_not_of("0123456789", open + 2) == s.size() - 1)
      {
        return true;
      }
    }
    if (s.size() > 3 && s.back() == ']')
    {
      const size_t open = s.rfind('[');
      if (open != std::string::npos
          && s.find_first_not_of("0123456789", open + 1) == s.size() - 1)
      {
        return true;
      }
    }
    // Explicit copy markers.
    return s.find("copy") != std::string::npos || s.find("-dup") != std::string::npos
           || s.find(" duplicate") != std::string::npos;
  }

  // Known source/code extensions that get a small ranking boost so real code
  // surfaces above data/asset/random files on near-ties.
  bool is_source_extension(const std::string &name)
  {
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot + 1 >= name.size())
    {
      return false;
    }
    static const std::unordered_set<std::string> kSourceExts = {
        "c",  "h",    "cpp", "hpp", "cc",  "cxx", "hh", "py", "pyw", "rs",  "go",   "java",
        "kt", "kts",  "js",  "jsx", "ts",  "tsx", "m",  "mm", "lua",  "sh",  "bash", "zsh",
        "rb", "php",  "swift", "cs", "scala", "clj", "ex", "exs", "erl",  "hs",   "ml",
        "fs", "fsx",  "vue", "svelte", "dart", "zig", "nim", "r",   "sql", "toml", "json",
        "yaml", "yml", "cmake", "mk", "proto", "tex", "md", "rst"};
    return kSourceExts.find(string_util::lower_copy(name.substr(dot + 1))) != kSourceExts.end();
  }

  bool file_looks_binary(const std::string &path)
  {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
      return false;
    }
    char chunk[2048];
    file.read(chunk, sizeof(chunk));
    std::streamsize read_n = file.gcount();
    for (std::streamsize i = 0; i < read_n; i++)
    {
      if (chunk[i] == '\0')
      {
        return true;
      }
    }
    return false;
  }

  std::string display_relative_path(const fs::path &path, const fs::path &root)
  {
    std::error_code ec;
    std::string rel = fs::relative(path, root, ec).string();
    if (ec || rel.empty())
    {
      ec.clear();
      rel = path.string();
    }
    return rel;
  }

  std::string parent_display_path(const std::string &relative_path)
  {
    fs::path parent = fs::path(relative_path).parent_path();
    std::string out = parent.string();
    return out.empty() ? "." : out;
  }

  std::string format_size(std::uintmax_t bytes)
  {
    const char *units[] = {"B", "KB", "MB", "GB"};
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 3)
    {
      value /= 1024.0;
      unit++;
    }

    std::ostringstream out;
    if (unit == 0)
    {
      out << bytes << " " << units[unit];
    }
    else if (value >= 10.0)
    {
      out << (int)(value + 0.5) << " " << units[unit];
    }
    else
    {
      out.setf(std::ios::fixed);
      out.precision(1);
      out << value << " " << units[unit];
    }
    return out.str();
  }
} // namespace

TelescopeLayout
telescope_layout_for(int render_width, int screen_height, int top_bound, int bottom_bound)
{
  TelescopeLayout layout;
  const int w = std::max(1, render_width);
  const int h = std::max(1, screen_height);
  const int top = std::clamp(top_bound, 0, std::max(0, h - 1));
  const int bottom = std::clamp(bottom_bound, top + 1, h);
  const int usable_h = std::max(1, bottom - top);
  if (w < 6 || usable_h < 5)
    return layout;

  // The whole picker, centred. The list box owns this width when there is no
  // file view; otherwise the two boxes split it with one column between them.
  const int total_w = std::clamp(w * 9 / 10, std::min(w, 44), w);
  layout.h = std::clamp(usable_h * 5 / 6, std::min(usable_h, 10), usable_h);
  layout.x = std::max(0, (w - total_w) / 2);
  layout.y = top + std::max(0, (usable_h - layout.h) / 2);

  // A file view needs a readable code column next to a readable list, so the
  // split only happens when both fit. Below that the picker is just the list.
  const int kGap = 1;
  const int kMinFileViewW = 30;
  const int left_w = std::max(34, total_w * 45 / 100);
  layout.show_preview =
      total_w >= 76 && layout.h >= 8 && (total_w - left_w - kGap) >= kMinFileViewW;
  layout.w = layout.show_preview ? left_w : total_w;
  layout.region_w = layout.show_preview ? total_w : layout.w;

  layout.inner_x = layout.x + 1;
  layout.inner_y = layout.y + 1;
  layout.inner_w = std::max(1, layout.w - 2);
  layout.inner_h = std::max(1, layout.h - 2);

  // Input row first, then one clear row of air, then the results -- the input
  // reads as a field of its own instead of another row of the list.
  layout.query_x = layout.inner_x + 1;
  layout.query_y = layout.inner_y;
  layout.query_w = std::max(1, layout.inner_w - 2);
  layout.body_y = layout.inner_y + 2;
  layout.footer_y = layout.y + layout.h - 1;
  layout.body_h = std::max(1, layout.footer_y - layout.body_y);
  layout.list_x = layout.inner_x;
  layout.list_y = layout.body_y;
  layout.list_w = layout.inner_w;
  layout.list_h = layout.body_h;

  if (layout.show_preview)
  {
    layout.preview_x = layout.x + layout.w + kGap;
    layout.preview_y = layout.y;
    layout.preview_w = layout.region_w - layout.w - kGap;
    layout.preview_h = layout.h;
    layout.preview_inner_x = layout.preview_x + 1;
    layout.preview_inner_y = layout.preview_y + 1;
    layout.preview_inner_w = std::max(1, layout.preview_w - 2);
    layout.preview_inner_h = std::max(1, layout.preview_h - 2);
    layout.preview_text_y = layout.preview_inner_y;
    layout.preview_status_y = layout.preview_y + layout.preview_h - 1;
  }
  layout.valid = true;
  return layout;
}

Telescope::Telescope()
{
  active = false;
  selected_index = 0;
  list_scroll_offset = 0;
  preview_scroll_offset = 0;
  root_dir = fs::current_path();
}

void Telescope::open(const std::string &root, const std::string &floor)
{
  cancel_scan();
  active = true;
  std::error_code ec;
  if (!root.empty())
  {
    fs::path candidate = fs::absolute(fs::path(root), ec);
    if (!ec && fs::exists(candidate, ec) && fs::is_directory(candidate, ec))
    {
      root_dir = candidate.lexically_normal();
    }
    else
    {
      root_dir = fs::current_path();
    }
  }
  else if (!fs::exists(root_dir, ec) || !fs::is_directory(root_dir, ec))
  {
    root_dir = fs::current_path();
  }
  // The floor is the workspace the find belongs to, but only when the scope
  // being opened really sits inside it: `:find /elsewhere` is its own floor,
  // so nothing can walk from one tree into another.
  floor_dir_ = root_dir;
  if (!floor.empty())
  {
    std::error_code floor_ec;
    const fs::path candidate = fs::weakly_canonical(fs::absolute(fs::path(floor), floor_ec), floor_ec);
    const fs::path root_c = fs::weakly_canonical(root_dir, floor_ec);
    if (!floor_ec && !candidate.empty() && root_c.native().size() >= candidate.native().size()
        && root_c.native().compare(0, candidate.native().size(), candidate.native()) == 0)
    {
      floor_dir_ = candidate;
    }
  }
  query.clear();
  selected_index = 0;
  list_scroll_offset = 0;
  preview_scroll_offset = 0;
  results.clear();
  all_entries_.clear();
  entries_valid_ = false;
  scan_pending_ = false;
  scan_error_.clear();
  focus_ = TelescopeFocus::Query;
  invalidate_preview_cache();
}

void Telescope::close()
{
  cancel_scan();
  active = false;
  query.clear();
  results.clear();
  all_entries_.clear();
  entries_valid_ = false;
  selected_index = 0;
  list_scroll_offset = 0;
  preview_scroll_offset = 0;
  scan_pending_ = false;
  scan_error_.clear();
  invalidate_preview_cache();
}

void Telescope::update_results()
{
  // Synchronous full rescan (no TaskQueue available): walk the tree into the
  // cache, then publish. The cache stays valid so later keystrokes filter
  // instantly instead of re-walking. A failed root walk records scan_error_
  // and leaves the cache invalid so the picker can say so.
  all_entries_.clear();
  entries_valid_ = false;
  scan_directory(root_dir, 0);
  if (scan_error_.empty())
  {
    entries_valid_ = true;
    publish_filtered();
  }
  else
  {
    results.clear();
    selected_index = 0;
    list_scroll_offset = 0;
    preview_scroll_offset = 0;
    invalidate_preview_cache();
  }
}

void Telescope::publish_filtered()
{
  results.clear();
  if (!entries_valid_)
  {
    return;
  }

  const std::string query_lc = string_util::lower_copy(query);
  std::vector<FileMatch> filtered;
  filtered.reserve(all_entries_.size());

  for (auto match : all_entries_)
  {
    // Folders are walked, never listed: the picker's result rows are files,
    // and where a file lives is carried by its (dimmed) path on the row. The
    // walk still descends through them, so a query that names a folder is how
    // you scope to one.
    if (match.is_directory)
    {
      continue;
    }
    if (!query_lc.empty() && !fuzzy_match(match.name, query_lc)
        && !fuzzy_match(match.relative_path, query_lc))
    {
      continue;
    }
    // Highlight the characters the query consumed in the displayed name
    // (only when the name itself matched; a path-only match highlights
    // nothing rather than pointing at the wrong glyphs).
    if (!query_lc.empty() && fuzzy_match(match.name, query_lc))
    {
      match.match = fuzzy_match_positions(match.name, query_lc);
    }
    match.score = rank_score(match.name, match.relative_path, query_lc, match.is_directory);
    filtered.push_back(std::move(match));
  }

  std::sort(filtered.begin(),
            filtered.end(),
            [&](const FileMatch &a, const FileMatch &b)
            {
              if (query_lc.empty() || a.score == b.score)
              {
                return string_util::lower_copy(a.name) < string_util::lower_copy(b.name);
              }
              return a.score > b.score;
            });

  if ((int)filtered.size() > kMaxResults)
  {
    filtered.resize(kMaxResults);
  }
  results = std::move(filtered);

  if (selected_index >= (int)results.size())
  {
    selected_index = std::max(0, (int)results.size() - 1);
  }
  if (selected_index < 0)
  {
    selected_index = 0;
  }
  ensure_selected_visible(std::max(1, (int)results.size()));
  preview_scroll_offset = 0;
  invalidate_preview_cache();
}

void Telescope::scan_directory(const fs::path &dir, int depth)
{
  if (depth > kMaxDepth)
  {
    return;
  }

  std::error_code ec;
  // Fail loudly on the scan root itself: a missing/permission-denied root is
  // a caller bug, and silently caching an empty listing makes the picker
  // report "No files found" for a root that simply does not exist.
  if (depth == 0)
  {
    std::error_code root_ec;
    if (!fs::is_directory(dir, root_ec) || root_ec)
    {
      scan_error_ = "cannot scan " + dir.string();
      return;
    }
    scan_error_.clear();
  }
  std::vector<fs::directory_entry> entries;
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::end(it); it.increment(ec))
  {
    entries.push_back(*it);
  }
  if (ec)
  {
    return;
  }

  std::sort(entries.begin(),
            entries.end(),
            [](const fs::directory_entry &a, const fs::directory_entry &b)
            {
              bool ad = false;
              bool bd = false;
              std::error_code e1, e2;
              ad = a.is_directory(e1);
              bd = b.is_directory(e2);
              if (ad != bd)
              {
                return ad;
              }
              std::string an = string_util::lower_copy(a.path().filename().string());
              std::string bn = string_util::lower_copy(b.path().filename().string());
              return an < bn;
            });

  for (const auto &entry : entries)
  {
    std::string name = entry.path().filename().string();
    if (should_skip_name(name))
    {
      continue;
    }

    bool is_dir = false;
    std::error_code type_ec;
    is_dir = entry.is_directory(type_ec);
    if (type_ec)
    {
      continue;
    }

    if (is_dir && should_skip_dir_name(name))
    {
      continue;
    }

    FileMatch match;
    match.path = entry.path().string();
    match.name = name;
    match.relative_path = display_relative_path(entry.path(), root_dir);
    match.parent_path = parent_display_path(match.relative_path);
    match.is_directory = is_dir;
    match.score = 0;
    all_entries_.push_back(std::move(match));

    if ((int)all_entries_.size() >= kMaxCandidates)
    {
      return;
    }

    if (is_dir && depth < kMaxDepth)
    {
      scan_directory(entry.path(), depth + 1);
      if ((int)all_entries_.size() >= kMaxCandidates)
      {
        return;
      }
    }
  }
}

void Telescope::move_up()
{
  move_by(-1);
}

void Telescope::move_down()
{
  move_by(1);
}

void Telescope::move_by(int delta)
{
  if (results.empty() || delta == 0)
  {
    return;
  }
  int next = std::clamp(selected_index + delta, 0, (int)results.size() - 1);
  if (next != selected_index)
  {
    selected_index = next;
    preview_scroll_offset = 0;
    invalidate_preview_cache();
  }
}

void Telescope::select_index(int index)
{
  if (results.empty())
  {
    selected_index = 0;
    list_scroll_offset = 0;
    preview_scroll_offset = 0;
    invalidate_preview_cache();
    return;
  }
  int next = std::clamp(index, 0, (int)results.size() - 1);
  if (next != selected_index)
  {
    selected_index = next;
    preview_scroll_offset = 0;
    invalidate_preview_cache();
  }
}

void Telescope::ensure_selected_visible(int visible_rows)
{
  visible_rows = std::max(1, visible_rows);
  int max_scroll = std::max(0, (int)results.size() - visible_rows);
  list_scroll_offset = std::clamp(list_scroll_offset, 0, max_scroll);
  if (results.empty())
  {
    list_scroll_offset = 0;
    return;
  }
  if (selected_index < list_scroll_offset)
  {
    list_scroll_offset = selected_index;
  }
  else if (selected_index >= list_scroll_offset + visible_rows)
  {
    list_scroll_offset = selected_index - visible_rows + 1;
  }
  list_scroll_offset = std::clamp(list_scroll_offset, 0, max_scroll);
}

void Telescope::select()
{
  // Every row is a file now, so accepting one is the caller's job (open it and
  // close the picker). Folders are reached by typing their path, not by
  // accepting a row.
}

bool Telescope::can_go_parent() const
{
  if (!root_dir.has_parent_path())
  {
    return false;
  }
  std::error_code ec;
  const fs::path parent = fs::weakly_canonical(root_dir.parent_path(), ec);
  const fs::path floor = ec ? floor_dir_ : fs::weakly_canonical(floor_dir_, ec);
  if (ec)
  {
    return false;
  }
  // Inside the floor ("floor/sub") the parent is still reachable; at the floor
  // it is not.
  return parent.native().size() >= floor.native().size()
         && parent.native().compare(0, floor.native().size(), floor.native()) == 0;
}

void Telescope::go_parent()
{
  if (!can_go_parent())
  {
    return;
  }
  root_dir = root_dir.parent_path();
  query.clear();
  selected_index = 0;
  list_scroll_offset = 0;
  preview_scroll_offset = 0;
  all_entries_.clear();
  entries_valid_ = false;
  invalidate_preview_cache();
}

std::string Telescope::get_relative_root() const
{
  if (floor_dir_.empty())
  {
    return "";
  }
  std::error_code ec;
  fs::path rel = fs::relative(root_dir, floor_dir_, ec);
  if (ec)
  {
    return "";
  }
  const std::string out = rel.generic_string();
  return out == "." ? "" : out;
}

void Telescope::invalidate_cache()
{
  all_entries_.clear();
  entries_valid_ = false;
  results.clear();
  selected_index = 0;
  list_scroll_offset = 0;
  invalidate_preview_cache();
}

void Telescope::scroll_preview(int delta, int visible_rows)
{
  if (delta == 0)
  {
    return;
  }
  TelescopePreview preview = get_selected_preview();
  int max_scroll = std::max(0, (int)preview.lines.size() - std::max(1, visible_rows));
  preview_scroll_offset = std::clamp(preview_scroll_offset + delta, 0, max_scroll);
}

void Telescope::cycle_focus(int delta)
{
  if (delta == 0)
    return;
  int focus = (int)focus_;
  const int count = 3;
  focus = (focus + delta % count + count) % count;
  focus_ = (TelescopeFocus)focus;
}

std::string Telescope::get_selected_path() const
{
  if (selected_index >= 0 && selected_index < (int)results.size())
  {
    return results[selected_index].path;
  }
  return "";
}

std::string Telescope::get_selected_relative_path() const
{
  if (selected_index >= 0 && selected_index < (int)results.size())
  {
    return results[selected_index].relative_path.empty() ? results[selected_index].name
                                                         : results[selected_index].relative_path;
  }
  return "";
}

TelescopePreview Telescope::get_selected_preview() const
{
  if (selected_index < 0 || selected_index >= (int)results.size())
  {
    return {};
  }
  const auto &match = results[selected_index];
  if (preview_cache_valid && preview_cache_path == match.path)
  {
    return preview_cache;
  }
  preview_cache = load_preview(match);
  preview_cache_path = match.path;
  preview_cache_valid = true;
  return preview_cache;
}

std::vector<std::string> Telescope::get_preview_lines() const
{
  return get_selected_preview().lines;
}

void Telescope::invalidate_preview_cache()
{
  preview_cache_valid = false;
  preview_cache_path.clear();
  preview_cache = TelescopePreview();
}

TelescopePreview Telescope::load_preview(const FileMatch &match) const
{
  TelescopePreview preview;
  preview.title = match.relative_path.empty() ? match.name : match.relative_path;
  preview.is_directory = match.is_directory;
  const std::string &path = match.path;

  if (match.is_directory)
  {
    preview.detail = "Directory";
    std::error_code ec;
    int child_count = 0;
    for (auto it = fs::directory_iterator(path, ec); !ec && it != fs::end(it) && child_count < 999;
         it.increment(ec))
    {
      std::string name = it->path().filename().string();
      if (!should_skip_name(name))
      {
        child_count++;
      }
    }
    if (!ec)
    {
      preview.detail += " - " + std::to_string(child_count) + " items";
    }
    preview.lines.push_back("Press Enter to browse this directory.");
    return preview;
  }

  std::error_code ec;
  if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec))
  {
    preview.skipped = true;
    preview.detail = "Not a regular file";
    preview.lines.push_back("Preview unavailable.");
    return preview;
  }

  std::uintmax_t sz = fs::file_size(path, ec);
  if (!ec)
  {
    preview.size_bytes = sz;
    preview.detail = format_size(sz);
  }
  if (!ec && sz > kMaxPreviewFileBytes)
  {
    preview.skipped = true;
    preview.detail += " - preview skipped";
    preview.lines.push_back("File is too large to preview.");
    return preview;
  }

  if (file_looks_binary(path))
  {
    preview.is_binary = true;
    preview.skipped = true;
    if (!preview.detail.empty())
    {
      preview.detail += " - ";
    }
    preview.detail += "binary";
    preview.lines.push_back("Binary file preview is not available.");
    return preview;
  }

  std::ifstream file(path);
  if (!file.is_open())
  {
    preview.skipped = true;
    if (!preview.detail.empty())
    {
      preview.detail += " - ";
    }
    preview.detail += "unreadable";
    preview.lines.push_back("Unable to open file.");
    return preview;
  }

  std::string line;
  int count = 0;
  while (std::getline(file, line) && count < kMaxPreviewLines)
  {
    if ((int)line.length() > kMaxPreviewLineLength)
    {
      line = line.substr(0, kMaxPreviewLineLength) + "...";
    }
    preview.lines.push_back(line);
    count++;
  }
  if (!file.eof())
  {
    preview.truncated = true;
    if (!preview.detail.empty())
      preview.detail += " - ";
    preview.detail += "preview truncated";
  }
  if (preview.lines.empty())
  {
    preview.lines.push_back("[Empty file]");
  }
  return preview;
}

bool Telescope::fuzzy_match(const std::string &text, const std::string &pattern)
{
  if (pattern.empty())
    return true;

  std::string text_lower = string_util::lower_copy(text);
  std::string pattern_lower = string_util::lower_copy(pattern);

  size_t pattern_idx = 0;
  for (size_t i = 0; i < text_lower.length() && pattern_idx < pattern_lower.length(); i++)
  {
    if (text_lower[i] == pattern_lower[pattern_idx])
    {
      pattern_idx++;
    }
  }
  return pattern_idx == pattern_lower.length();
}

std::vector<int> Telescope::fuzzy_match_positions(const std::string &text,
                                                  const std::string &pattern)
{
  std::vector<int> positions;
  if (pattern.empty() || text.empty())
  {
    return positions;
  }
  const std::string text_lower = string_util::lower_copy(text);
  const std::string pattern_lower = string_util::lower_copy(pattern);
  if (text_lower.size() != text.size())
  {
    // Case folding changed the byte length, so offsets into `text` would be
    // wrong; skip highlighting rather than paint the wrong characters.
    return positions;
  }
  size_t pos = 0;
  for (char qc : pattern_lower)
  {
    bool found = false;
    for (; pos < text_lower.size(); pos++)
    {
      if (text_lower[pos] == qc)
      {
        positions.push_back((int)pos);
        pos++;
        found = true;
        break;
      }
    }
    if (!found)
    {
      positions.clear();
      return positions;
    }
  }
  return positions;
}

int Telescope::fuzzy_score(const std::string &text, const std::string &pattern)
{
  if (pattern.empty())
    return 0;

  std::string text_lower = string_util::lower_copy(text);
  std::string pattern_lower = string_util::lower_copy(pattern);
  if (text_lower.empty())
  {
    return 0;
  }

  if (text_lower == pattern_lower)
  {
    return 500;
  }
  if (text_lower.find(pattern_lower) != std::string::npos)
  {
    return 300;
  }

  int score = 0;
  size_t pi = 0;
  int prev = -2;
  for (size_t i = 0; i < text_lower.size() && pi < pattern_lower.size(); i++)
  {
    if (text_lower[i] != pattern_lower[pi])
    {
      continue;
    }
    score += 12;
    if ((int)i == prev + 1)
    {
      score += 10;
    }
    if (i == 0 || text_lower[i - 1] == '/' || text_lower[i - 1] == '_' || text_lower[i - 1] == '-'
        || text_lower[i - 1] == ' ')
    {
      score += 8;
    }
    score += std::max(0, 12 - (int)i);
    prev = (int)i;
    pi++;
  }

  if (pi != pattern_lower.size())
  {
    return 0;
  }
  return score;
}

int Telescope::rank_score(const std::string &name,
                          const std::string &relative_path,
                          const std::string &query_lc,
                          bool is_directory)
{
  int score_name = fuzzy_score(name, query_lc);
  int score_path = fuzzy_score(relative_path, query_lc);
  // No match anywhere: score 0 (callers also filter before ranking, but the
  // method must be self-consistent so boosts never apply to non-matches).
  // An empty query is browse mode: every entry is a candidate, so nothing
  // is zeroed (the empty-query sort ignores scores anyway).
  if (!query_lc.empty() && score_name == 0 && score_path == 0)
  {
    return 0;
  }
  int bonus = 0;
  std::string name_lc = string_util::lower_copy(name);
  std::string rel_lc = string_util::lower_copy(relative_path);
  if (!query_lc.empty() && name_lc.find(query_lc) != std::string::npos)
  {
    bonus += 30;
  }
  if (!query_lc.empty() && rel_lc.find("/" + query_lc) != std::string::npos)
  {
    bonus += 12;
  }
  if (is_directory)
  {
    bonus -= 6;
  }
  else
  {
    // Real code files surface above assets/data/random files on near-ties.
    if (is_source_extension(name))
    {
      bonus += 15;
    }
    // Generated duplicates/copies rank below the clean original.
    if (name_looks_generated_duplicate(name))
    {
      bonus -= 40;
    }
  }
  return score_name * 2 + score_path + bonus;
}

void Telescope::cancel_scan()
{
  scan_id_.fetch_add(1);
  scan_generation_->fetch_add(1);
  scan_pending_ = false;
}

void Telescope::apply_results(std::vector<FileMatch> new_results)
{
  // Injection point (tests / programmatic callers). The given entries also
  // become the candidate cache so a later set_query filters them instantly.
  results = std::move(new_results);
  all_entries_ = results;
  entries_valid_ = true;
  scan_pending_ = false;
  if (selected_index >= (int)results.size())
    selected_index = std::max(0, (int)results.size() - 1);
  if (selected_index < 0)
    selected_index = 0;
  ensure_selected_visible(std::max(1, (int)results.size()));
  preview_scroll_offset = 0;
  invalidate_preview_cache();
}
