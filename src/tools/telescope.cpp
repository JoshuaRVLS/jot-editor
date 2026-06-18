#include "telescope.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {
constexpr int kMaxDepth = 4;
constexpr int kMaxResults = 2000;
constexpr int kMaxPreviewLines = 120;
constexpr int kMaxPreviewLineLength = 240;
constexpr std::uintmax_t kMaxPreviewFileBytes = 1024 * 1024; // 1MB

std::string lower_copy(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return out;
}

bool should_skip_dir_name(const std::string &name) {
  static const std::unordered_set<std::string> kSkipped = {
      ".git",       ".svn",      ".hg",       "node_modules", "dist",
      "build",      ".cache",    "__pycache__", ".venv",      "target"};
  return kSkipped.find(name) != kSkipped.end();
}

bool should_skip_name(const std::string &name) {
  return name.empty() || name[0] == '.';
}

bool file_looks_binary(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  char chunk[2048];
  file.read(chunk, sizeof(chunk));
  std::streamsize read_n = file.gcount();
  for (std::streamsize i = 0; i < read_n; i++) {
    if (chunk[i] == '\0') {
      return true;
    }
  }
  return false;
}

std::string display_relative_path(const fs::path &path, const fs::path &root) {
  std::error_code ec;
  std::string rel = fs::relative(path, root, ec).string();
  if (ec || rel.empty()) {
    ec.clear();
    rel = path.string();
  }
  return rel;
}

std::string parent_display_path(const std::string &relative_path) {
  fs::path parent = fs::path(relative_path).parent_path();
  std::string out = parent.string();
  return out.empty() ? "." : out;
}

std::string format_size(std::uintmax_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB"};
  double value = (double)bytes;
  int unit = 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    unit++;
  }

  std::ostringstream out;
  if (unit == 0) {
    out << bytes << " " << units[unit];
  } else if (value >= 10.0) {
    out << (int)(value + 0.5) << " " << units[unit];
  } else {
    out.setf(std::ios::fixed);
    out.precision(1);
    out << value << " " << units[unit];
  }
  return out.str();
}
} // namespace

Telescope::Telescope() {
  active = false;
  selected_index = 0;
  list_scroll_offset = 0;
  preview_scroll_offset = 0;
  root_dir = fs::current_path();
}

void Telescope::open(const std::string &root) {
  active = true;
  std::error_code ec;
  if (!root.empty()) {
    fs::path candidate = fs::absolute(fs::path(root), ec);
    if (!ec && fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
      root_dir = candidate.lexically_normal();
    } else {
      root_dir = fs::current_path();
    }
  } else if (!fs::exists(root_dir, ec) || !fs::is_directory(root_dir, ec)) {
    root_dir = fs::current_path();
  }
  query.clear();
  selected_index = 0;
  list_scroll_offset = 0;
  preview_scroll_offset = 0;
  results.clear();
  invalidate_preview_cache();
  update_results();
}

void Telescope::close() {
  active = false;
  query.clear();
  results.clear();
  selected_index = 0;
  list_scroll_offset = 0;
  preview_scroll_offset = 0;
  invalidate_preview_cache();
}

void Telescope::update_results() {
  results.clear();
  scan_directory(root_dir, 0);

  const std::string query_lc = lower_copy(query);
  std::vector<FileMatch> filtered;
  filtered.reserve(results.size());

  for (auto match : results) {
    fs::path p(match.path);
    std::string rel = display_relative_path(p, root_dir);
    match.relative_path = rel;
    match.parent_path = parent_display_path(rel);
    if (!query_lc.empty() &&
        !fuzzy_match(match.name, query_lc) && !fuzzy_match(rel, query_lc)) {
      continue;
    }

    int score_name = fuzzy_score(match.name, query_lc);
    int score_path = fuzzy_score(rel, query_lc);
    int bonus = 0;
    std::string name_lc = lower_copy(match.name);
    std::string rel_lc = lower_copy(rel);
    if (!query_lc.empty() && name_lc.find(query_lc) != std::string::npos) {
      bonus += 30;
    }
    if (!query_lc.empty() && rel_lc.find("/" + query_lc) != std::string::npos) {
      bonus += 12;
    }
    if (match.is_directory) {
      bonus -= 6;
    }

    match.score = score_name * 2 + score_path + bonus;
    filtered.push_back(std::move(match));
  }

  std::sort(filtered.begin(), filtered.end(),
            [&](const FileMatch &a, const FileMatch &b) {
              if (query_lc.empty()) {
                if (a.is_directory != b.is_directory) {
                  return a.is_directory;
                }
                return lower_copy(a.name) < lower_copy(b.name);
              }
              if (a.score != b.score) {
                return a.score > b.score;
              }
              if (a.is_directory != b.is_directory) {
                return !a.is_directory;
              }
              return lower_copy(a.name) < lower_copy(b.name);
            });

  if ((int)filtered.size() > kMaxResults) {
    filtered.resize(kMaxResults);
  }
  results = std::move(filtered);

  if (selected_index >= (int)results.size()) {
    selected_index = std::max(0, (int)results.size() - 1);
  }
  if (selected_index < 0) {
    selected_index = 0;
  }
  ensure_selected_visible(std::max(1, (int)results.size()));
  preview_scroll_offset = 0;
  invalidate_preview_cache();
}

void Telescope::scan_directory(const fs::path &dir, int depth) {
  if (depth > kMaxDepth) {
    return;
  }

  std::error_code ec;
  std::vector<fs::directory_entry> entries;
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::end(it);
       it.increment(ec)) {
    entries.push_back(*it);
  }
  if (ec) {
    return;
  }

  std::sort(entries.begin(), entries.end(),
            [](const fs::directory_entry &a, const fs::directory_entry &b) {
              bool ad = false;
              bool bd = false;
              std::error_code e1, e2;
              ad = a.is_directory(e1);
              bd = b.is_directory(e2);
              if (ad != bd) {
                return ad;
              }
              std::string an = lower_copy(a.path().filename().string());
              std::string bn = lower_copy(b.path().filename().string());
              return an < bn;
            });

  for (const auto &entry : entries) {
    std::string name = entry.path().filename().string();
    if (should_skip_name(name)) {
      continue;
    }

    bool is_dir = false;
    std::error_code type_ec;
    is_dir = entry.is_directory(type_ec);
    if (type_ec) {
      continue;
    }

    if (is_dir && should_skip_dir_name(name)) {
      continue;
    }

    FileMatch match;
    match.path = entry.path().string();
    match.name = name;
    match.relative_path = display_relative_path(entry.path(), root_dir);
    match.parent_path = parent_display_path(match.relative_path);
    match.is_directory = is_dir;
    match.score = 0;
    results.push_back(std::move(match));

    if ((int)results.size() >= kMaxResults) {
      return;
    }

    if (is_dir && depth < kMaxDepth) {
      scan_directory(entry.path(), depth + 1);
      if ((int)results.size() >= kMaxResults) {
        return;
      }
    }
  }
}

void Telescope::move_up() {
  move_by(-1);
}

void Telescope::move_down() {
  move_by(1);
}

void Telescope::move_by(int delta) {
  if (results.empty() || delta == 0) {
    return;
  }
  int next = std::clamp(selected_index + delta, 0, (int)results.size() - 1);
  if (next != selected_index) {
    selected_index = next;
    preview_scroll_offset = 0;
    invalidate_preview_cache();
  }
}

void Telescope::select_index(int index) {
  if (results.empty()) {
    selected_index = 0;
    list_scroll_offset = 0;
    preview_scroll_offset = 0;
    invalidate_preview_cache();
    return;
  }
  int next = std::clamp(index, 0, (int)results.size() - 1);
  if (next != selected_index) {
    selected_index = next;
    preview_scroll_offset = 0;
    invalidate_preview_cache();
  }
}

void Telescope::ensure_selected_visible(int visible_rows) {
  visible_rows = std::max(1, visible_rows);
  int max_scroll = std::max(0, (int)results.size() - visible_rows);
  list_scroll_offset = std::clamp(list_scroll_offset, 0, max_scroll);
  if (results.empty()) {
    list_scroll_offset = 0;
    return;
  }
  if (selected_index < list_scroll_offset) {
    list_scroll_offset = selected_index;
  } else if (selected_index >= list_scroll_offset + visible_rows) {
    list_scroll_offset = selected_index - visible_rows + 1;
  }
  list_scroll_offset = std::clamp(list_scroll_offset, 0, max_scroll);
}

void Telescope::select() {
  if (selected_index >= 0 && selected_index < (int)results.size()) {
    if (results[selected_index].is_directory) {
      root_dir = fs::path(results[selected_index].path);
      query.clear();
      selected_index = 0;
      list_scroll_offset = 0;
      preview_scroll_offset = 0;
      invalidate_preview_cache();
      update_results();
    }
  }
}

void Telescope::go_parent() {
  if (root_dir.has_parent_path()) {
    root_dir = root_dir.parent_path();
    query.clear();
    selected_index = 0;
    list_scroll_offset = 0;
    preview_scroll_offset = 0;
    invalidate_preview_cache();
    update_results();
  }
}

void Telescope::scroll_preview(int delta, int visible_rows) {
  if (delta == 0) {
    return;
  }
  TelescopePreview preview = get_selected_preview();
  int max_scroll = std::max(0, (int)preview.lines.size() - std::max(1, visible_rows));
  preview_scroll_offset =
      std::clamp(preview_scroll_offset + delta, 0, max_scroll);
}

std::string Telescope::get_selected_path() const {
  if (selected_index >= 0 && selected_index < (int)results.size()) {
    return results[selected_index].path;
  }
  return "";
}

std::string Telescope::get_selected_relative_path() const {
  if (selected_index >= 0 && selected_index < (int)results.size()) {
    return results[selected_index].relative_path.empty()
               ? results[selected_index].name
               : results[selected_index].relative_path;
  }
  return "";
}

TelescopePreview Telescope::get_selected_preview() const {
  if (selected_index < 0 || selected_index >= (int)results.size()) {
    return {};
  }
  const auto &match = results[selected_index];
  if (preview_cache_valid && preview_cache_path == match.path) {
    return preview_cache;
  }
  preview_cache = load_preview(match);
  preview_cache_path = match.path;
  preview_cache_valid = true;
  return preview_cache;
}

std::vector<std::string> Telescope::get_preview_lines() const {
  return get_selected_preview().lines;
}

void Telescope::invalidate_preview_cache() {
  preview_cache_valid = false;
  preview_cache_path.clear();
  preview_cache = TelescopePreview();
}

TelescopePreview Telescope::load_preview(const FileMatch &match) const {
  TelescopePreview preview;
  preview.title = match.relative_path.empty() ? match.name : match.relative_path;
  preview.is_directory = match.is_directory;
  const std::string &path = match.path;

  if (match.is_directory) {
    preview.detail = "Directory";
    std::error_code ec;
    int child_count = 0;
    for (auto it = fs::directory_iterator(path, ec);
         !ec && it != fs::end(it) && child_count < 999; it.increment(ec)) {
      std::string name = it->path().filename().string();
      if (!should_skip_name(name)) {
        child_count++;
      }
    }
    if (!ec) {
      preview.detail += " - " + std::to_string(child_count) + " items";
    }
    preview.lines.push_back("Press Enter to browse this directory.");
    return preview;
  }

  std::error_code ec;
  if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) {
    preview.skipped = true;
    preview.detail = "Not a regular file";
    preview.lines.push_back("Preview unavailable.");
    return preview;
  }

  std::uintmax_t sz = fs::file_size(path, ec);
  if (!ec) {
    preview.size_bytes = sz;
    preview.detail = format_size(sz);
  }
  if (!ec && sz > kMaxPreviewFileBytes) {
    preview.skipped = true;
    preview.detail += " - preview skipped";
    preview.lines.push_back("File is too large to preview.");
    return preview;
  }

  if (file_looks_binary(path)) {
    preview.is_binary = true;
    preview.skipped = true;
    if (!preview.detail.empty()) {
      preview.detail += " - ";
    }
    preview.detail += "binary";
    preview.lines.push_back("Binary file preview is not available.");
    return preview;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    preview.skipped = true;
    if (!preview.detail.empty()) {
      preview.detail += " - ";
    }
    preview.detail += "unreadable";
    preview.lines.push_back("Unable to open file.");
    return preview;
  }

  std::string line;
  int count = 0;
  while (std::getline(file, line) && count < kMaxPreviewLines) {
    if ((int)line.length() > kMaxPreviewLineLength) {
      line = line.substr(0, kMaxPreviewLineLength) + "...";
    }
    preview.lines.push_back(line);
    count++;
  }
  if (preview.lines.empty()) {
    preview.lines.push_back("[Empty file]");
  }
  return preview;
}

bool Telescope::fuzzy_match(const std::string &text, const std::string &pattern) {
  if (pattern.empty())
    return true;

  std::string text_lower = lower_copy(text);
  std::string pattern_lower = lower_copy(pattern);

  size_t pattern_idx = 0;
  for (size_t i = 0; i < text_lower.length() && pattern_idx < pattern_lower.length();
       i++) {
    if (text_lower[i] == pattern_lower[pattern_idx]) {
      pattern_idx++;
    }
  }
  return pattern_idx == pattern_lower.length();
}

int Telescope::fuzzy_score(const std::string &text, const std::string &pattern) {
  if (pattern.empty())
    return 0;

  std::string text_lower = lower_copy(text);
  std::string pattern_lower = lower_copy(pattern);
  if (text_lower.empty()) {
    return 0;
  }

  if (text_lower == pattern_lower) {
    return 500;
  }
  if (text_lower.find(pattern_lower) != std::string::npos) {
    return 300;
  }

  int score = 0;
  size_t pi = 0;
  int prev = -2;
  for (size_t i = 0; i < text_lower.size() && pi < pattern_lower.size(); i++) {
    if (text_lower[i] != pattern_lower[pi]) {
      continue;
    }
    score += 12;
    if ((int)i == prev + 1) {
      score += 10;
    }
    if (i == 0 || text_lower[i - 1] == '/' || text_lower[i - 1] == '_' ||
        text_lower[i - 1] == '-' || text_lower[i - 1] == ' ') {
      score += 8;
    }
    score += std::max(0, 12 - (int)i);
    prev = (int)i;
    pi++;
  }

  if (pi != pattern_lower.size()) {
    return 0;
  }
  return score;
}

void Telescope::cancel_scan() { scan_id_.fetch_add(1); }

void Telescope::apply_results(std::vector<FileMatch> new_results) {
  results = std::move(new_results);
  if (selected_index >= (int)results.size())
    selected_index = std::max(0, (int)results.size() - 1);
  if (selected_index < 0)
    selected_index = 0;
  ensure_selected_visible(std::max(1, (int)results.size()));
  preview_scroll_offset = 0;
  invalidate_preview_cache();
}
