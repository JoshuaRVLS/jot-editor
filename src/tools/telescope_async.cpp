#include "telescope.h"
#include "task_queue.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace {
constexpr int kMaxDepth = 4;
constexpr int kMaxResults = 2000;

std::string lower_copy(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return out;
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
} // namespace

void Telescope::scan_async(TaskQueue *tq) {
  if (!tq || !active) {
    return;
  }

  int scan_id = scan_id_.fetch_add(1) + 1;
  std::string scan_query = query;
  fs::path scan_root = root_dir;

  tq->submit_val<std::vector<FileMatch>>(
      [scan_root = std::move(scan_root),
       scan_query = std::move(scan_query)]() -> std::vector<FileMatch> {
        std::vector<FileMatch> raw;

        std::function<void(const fs::path &, int)> scan_dir;
        scan_dir = [&raw, &scan_dir, &scan_root](const fs::path &dir,
                                                 int depth) {
          if (depth > kMaxDepth || (int)raw.size() >= kMaxResults) {
            return;
          }

          std::error_code ec;
          for (auto it = fs::directory_iterator(dir, ec);
               !ec && it != fs::end(it) && (int)raw.size() < kMaxResults;
               it.increment(ec)) {
            std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.') {
              continue;
            }

            bool is_dir = false;
            std::error_code type_ec;
            is_dir = it->is_directory(type_ec);
            if (type_ec) {
              continue;
            }

            static const std::unordered_set<std::string> kSkipped = {
                ".git",  ".svn",  ".hg",        "node_modules", "dist",
                "build", ".cache", "__pycache__", ".venv",      "target"};

            if (is_dir && kSkipped.find(name) != kSkipped.end()) {
              continue;
            }

            FileMatch match;
            match.path = it->path().string();
            match.name = std::move(name);
            match.relative_path = display_relative_path(it->path(), scan_root);
            match.parent_path = parent_display_path(match.relative_path);
            match.is_directory = is_dir;
            match.score = 0;
            raw.push_back(std::move(match));

            if (is_dir && depth < kMaxDepth) {
              scan_dir(it->path(), depth + 1);
            }
          }
        };

        scan_dir(scan_root, 0);

        const std::string query_lc = lower_copy(scan_query);
        std::vector<FileMatch> filtered;
        filtered.reserve(raw.size());

        for (auto &match : raw) {
          fs::path p(match.path);
          std::string rel = match.relative_path.empty()
                                ? display_relative_path(p, scan_root)
                                : match.relative_path;
          match.relative_path = rel;
          match.parent_path = parent_display_path(rel);
          if (!query_lc.empty() && !fuzzy_match(match.name, query_lc) &&
              !fuzzy_match(rel, query_lc)) {
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
          if (!query_lc.empty() &&
              rel_lc.find("/" + query_lc) != std::string::npos) {
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

        return filtered;
      },
      [this, scan_id](std::vector<FileMatch> filtered) {
        if (scan_id != scan_id_.load()) {
          return;
        }
        apply_results(std::move(filtered));
      });
}
