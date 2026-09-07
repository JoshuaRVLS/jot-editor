#include "task_queue.h"
#include "telescope.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace
{
  constexpr int kMaxDepth = 4;
  constexpr int kMaxCandidates = 20000;

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
} // namespace

void Telescope::set_query(const std::string &q, TaskQueue *tq, std::function<void()> on_update)
{
  query = q;
  selected_index = 0;
  list_scroll_offset = 0;
  preview_scroll_offset = 0;
  invalidate_preview_cache();

  // Instant path: the tree was already scanned (cache valid), so typing
  // filters purely in memory — no directory walk, no task hop. This is what
  // makes the picker feel like fzf on large projects.
  if (entries_valid_)
  {
    publish_filtered();
    if (on_update)
      on_update();
    return;
  }

  // No cache yet (first open, scan still running). Never re-walk per key: make
  // sure exactly one scan is in flight; when it lands it filters against the
  // latest query.
  if (scan_pending_)
  {
    return;
  }
  if (tq)
  {
    scan_async(tq, std::move(on_update));
  }
  else
  {
    update_results(); // sync walk, fills the cache
    if (on_update)
      on_update();
  }
}

void Telescope::scan_async(TaskQueue *tq, std::function<void()> on_update)
{
  if (!tq || !active)
  {
    return;
  }

  scan_pending_ = true;
  int scan_id = scan_id_.fetch_add(1) + 1;
  const auto generation = scan_generation_;
  const int scan_generation = generation->fetch_add(1) + 1;
  fs::path scan_root = root_dir;

  tq->submit_val<std::vector<FileMatch>>(
      [scan_root = std::move(scan_root), generation, scan_generation]() -> std::vector<FileMatch>
      {
        std::vector<FileMatch> raw;

        std::function<void(const fs::path &, int)> scan_dir;
        scan_dir = [&raw, &scan_dir, &scan_root, generation, scan_generation](const fs::path &dir,
                                                                              int depth)
        {
          if (depth > kMaxDepth || (int)raw.size() >= kMaxCandidates
              || generation->load() != scan_generation)
          {
            return;
          }

          std::error_code ec;
          for (auto it = fs::directory_iterator(dir, ec);
               !ec && it != fs::end(it) && (int)raw.size() < kMaxCandidates
               && generation->load() == scan_generation;
               it.increment(ec))
          {
            std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.')
            {
              continue;
            }

            bool is_dir = false;
            std::error_code type_ec;
            is_dir = it->is_directory(type_ec);
            if (type_ec)
            {
              continue;
            }

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

            if (is_dir && kSkipped.find(name) != kSkipped.end())
            {
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

            if (is_dir && depth < kMaxDepth)
            {
              scan_dir(it->path(), depth + 1);
            }
          }
        };

        return raw;
      },
      [this, scan_id, generation, scan_generation, on_update = std::move(on_update)](
          std::vector<FileMatch> raw)
      {
        if (!active || scan_id != scan_id_.load() || generation->load() != scan_generation)
        {
          return;
        }
        // Scan landed: cache the raw listing once, then filter against the
        // latest query. Future keystrokes never re-walk the directory tree.
        scan_pending_ = false;
        all_entries_ = std::move(raw);
        entries_valid_ = true;
        publish_filtered();
        if (on_update)
          on_update();
      });
}
