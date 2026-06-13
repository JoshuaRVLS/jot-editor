#include "tools/workspace/search.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string trim_line_preview(std::string line) {
  for (char &c : line) {
    if (c == '\t' || c == '\r' || c == '\n') {
      c = ' ';
    }
  }
  size_t start = line.find_first_not_of(' ');
  if (start == std::string::npos) {
    return "";
  }
  line.erase(0, start);
  if (line.size() > 180) {
    line = line.substr(0, 177) + "...";
  }
  return line;
}

std::string relative_display(const fs::path &path, const fs::path &root) {
  std::error_code ec;
  std::string rel = fs::relative(path, root, ec).generic_string();
  if (ec || rel.empty()) {
    return path.generic_string();
  }
  return rel;
}
} // namespace

namespace WorkspaceSearch {
bool should_skip_path_component(const std::string &name) {
  static const std::unordered_set<std::string> skipped = {
      ".git",       ".svn",       ".hg",    "node_modules", "dist",
      "build",      ".cache",     "target", "__pycache__",  ".venv",
      "venv",       ".mypy_cache"};
  return name.empty() || skipped.find(name) != skipped.end();
}

bool text_looks_binary(const std::string &sample) {
  return sample.find('\0') != std::string::npos;
}

std::vector<WorkspaceSearchResult>
search(const std::string &root, const std::string &query,
       std::size_t max_file_bytes, int max_results) {
  std::vector<WorkspaceSearchResult> out;
  std::string needle = lower_copy(query);
  if (needle.empty() || max_results <= 0) {
    return out;
  }

  std::error_code ec;
  fs::path root_path = root.empty() ? fs::current_path(ec) : fs::path(root);
  root_path = fs::absolute(root_path, ec).lexically_normal();
  if (ec || !fs::exists(root_path, ec)) {
    return out;
  }

  fs::recursive_directory_iterator it(
      root_path, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;
  for (; !ec && it != end && (int)out.size() < max_results; it.increment(ec)) {
    const fs::directory_entry &entry = *it;
    std::string name = entry.path().filename().string();
    if (entry.is_directory(ec)) {
      if (should_skip_path_component(name)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!entry.is_regular_file(ec) || should_skip_path_component(name)) {
      continue;
    }

    std::uintmax_t size = entry.file_size(ec);
    if (ec || size > max_file_bytes) {
      ec.clear();
      continue;
    }

    std::ifstream file(entry.path(), std::ios::binary);
    if (!file.is_open()) {
      continue;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    if (text_looks_binary(content)) {
      continue;
    }

    std::istringstream lines(content);
    std::string line;
    int line_no = 0;
    std::string rel = relative_display(entry.path(), root_path);
    std::string rel_lower = lower_copy(rel);
    while (std::getline(lines, line) && (int)out.size() < max_results) {
      line_no++;
      std::string lower = lower_copy(line);
      size_t pos = lower.find(needle);
      if (pos == std::string::npos) {
        continue;
      }
      WorkspaceSearchResult result;
      result.path = entry.path().string();
      result.relative_path = rel;
      result.line_text = trim_line_preview(line);
      result.line = line_no - 1;
      result.column = (int)pos;
      result.score = 1000 - (int)pos;
      if (rel_lower.find(needle) != std::string::npos) {
        result.score += 100;
      }
      out.push_back(std::move(result));
    }
  }

  std::stable_sort(out.begin(), out.end(),
                   [](const WorkspaceSearchResult &a,
                      const WorkspaceSearchResult &b) {
                     if (a.score != b.score) {
                       return a.score > b.score;
                     }
                     if (a.relative_path != b.relative_path) {
                       return a.relative_path < b.relative_path;
                     }
                     if (a.line != b.line) {
                       return a.line < b.line;
                     }
                     return a.column < b.column;
                   });
  return out;
}
} // namespace WorkspaceSearch
