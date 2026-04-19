#include "telescope.h"
#include <algorithm>
#include <cctype>
#include <fstream>
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
} // namespace

Telescope::Telescope() {
  active = false;
  selected_index = 0;
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
  results.clear();
  update_results();
}

void Telescope::close() {
  active = false;
  query.clear();
  results.clear();
  selected_index = 0;
}

void Telescope::set_query(const std::string &q) {
  query = q;
  update_results();
}

void Telescope::update_results() {
  results.clear();
  scan_directory(root_dir, 0);

  std::error_code ec;
  const std::string query_lc = lower_copy(query);
  std::vector<FileMatch> filtered;
  filtered.reserve(results.size());

  for (auto match : results) {
    fs::path p(match.path);
    std::string rel = fs::relative(p, root_dir, ec).string();
    if (ec || rel.empty()) {
      rel = p.string();
      ec.clear();
    }
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
  if (selected_index > 0)
    selected_index--;
}

void Telescope::move_down() {
  if (selected_index < (int)results.size() - 1)
    selected_index++;
}

void Telescope::select() {
  if (selected_index >= 0 && selected_index < (int)results.size()) {
    if (results[selected_index].is_directory) {
      root_dir = fs::path(results[selected_index].path);
      query.clear();
      selected_index = 0;
      update_results();
    }
  }
}

void Telescope::go_parent() {
  if (root_dir.has_parent_path()) {
    root_dir = root_dir.parent_path();
    query.clear();
    selected_index = 0;
    update_results();
  }
}

std::string Telescope::get_selected_path() const {
  if (selected_index >= 0 && selected_index < (int)results.size()) {
    return results[selected_index].path;
  }
  return "";
}

std::vector<std::string> Telescope::get_preview_lines() const {
  if (selected_index < 0 || selected_index >= (int)results.size()) {
    return {};
  }
  std::string path = get_selected_path();
  if (path.empty() || results[selected_index].is_directory) {
    return {};
  }
  return load_preview(path);
}

std::vector<std::string> Telescope::load_preview(const std::string &path) const {
  std::vector<std::string> lines;
  std::error_code ec;
  if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) {
    lines.push_back("[Not a regular file]");
    return lines;
  }

  std::uintmax_t sz = fs::file_size(path, ec);
  if (!ec && sz > kMaxPreviewFileBytes) {
    lines.push_back("[Preview skipped: file too large]");
    return lines;
  }

  if (file_looks_binary(path)) {
    lines.push_back("[Preview skipped: binary file]");
    return lines;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    lines.push_back("[Unable to open file]");
    return lines;
  }

  std::string line;
  int count = 0;
  while (std::getline(file, line) && count < kMaxPreviewLines) {
    if ((int)line.length() > kMaxPreviewLineLength) {
      line = line.substr(0, kMaxPreviewLineLength) + "...";
    }
    lines.push_back(line);
    count++;
  }
  return lines;
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
