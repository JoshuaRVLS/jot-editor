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
  if (!active)
  {
    return;
  }

  scan_pending_ = true;
  int scan_id = scan_id_.fetch_add(1) + 1;
  const auto generation = scan_generation_;
  const int scan_generation = generation->fetch_add(1) + 1;
  fs::path scan_root = root_dir;

  // Shared completion: land the scan into the cache and filter against the
  // latest query. Used by the async completion so neither path can wedge.
  auto land_scan = [this, scan_id, generation, scan_generation,
                    on_redraw = std::move(on_update)](std::vector<FileMatch> raw) mutable
  {
    if (!active || scan_id != scan_id_.load() || generation->load() != scan_generation)
    {
      // A newer scan is in flight — it owns scan_pending_ now.
      return;
    }
    // Scan landed: cache the raw listing once, then filter against the
    // latest query. Future keystrokes never re-walk the directory tree.
    scan_pending_ = false;
    scan_error_.clear();
    all_entries_ = std::move(raw);
    entries_valid_ = true;
    publish_filtered();
    if (on_redraw)
      on_redraw();
  };

  if (!tq)
  {
    // No worker available: walk synchronously on this thread, then land the
    // result through the shared completion. update_results() already
    // published (or recorded the scan error), so bypass land_scan's
    // generation check and just clear the pending flag with a redraw.
    update_results();
    scan_pending_ = false;
    if (on_update)
      on_update();
    return;
  }

  auto scan_work = [scan_root = std::move(scan_root), generation, scan_generation]()
                     -> std::pair<std::vector<FileMatch>, std::string>
  {
    std::vector<FileMatch> raw;
    std::string root_error;

    std::function<void(const fs::path &, int)> scan_dir;
    scan_dir = [&raw, &root_error, &scan_dir, &scan_root, generation, scan_generation](
                   const fs::path &dir, int depth)
    {
      if (depth > kMaxDepth || (int)raw.size() >= kMaxCandidates
          || generation->load() != scan_generation)
      {
        return;
      }

      std::error_code ec;
      if (depth == 0)
      {
        // Fail loudly on the scan root itself (mirror of the sync walk): a
        // missing root would otherwise cache as a valid empty listing.
        std::error_code root_ec;
        if (!fs::is_directory(dir, root_ec) || root_ec)
        {
          root_error = std::string("cannot scan ") + dir.string();
          return;
        }
      }
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

    scan_dir(scan_root, 0);
    return {std::move(raw), std::move(root_error)};
  };

  auto scan_done = [this, scan_id, generation, scan_generation,
                      on_redraw = std::move(land_scan)](std::pair<std::vector<FileMatch>, std::string>
                                                            out) mutable
  {
    if (!active || scan_id != scan_id_.load() || generation->load() != scan_generation)
    {
      return;
    }
    if (!out.second.empty())
    {
      // Root unreadable: record the failure and leave the cache invalid
      // instead of caching an empty listing as valid. scan_pending_ is
      // cleared so the picker shows the error instead of wedging on
      // "Scanning...". (Do not route through land_scan here: it marks the
      // cache valid, which is exactly what an unreadable root must not do.)
      scan_error_ = std::move(out.second);
      scan_pending_ = false;
      all_entries_.clear();
      results.clear();
      entries_valid_ = false;
      selected_index = 0;
      list_scroll_offset = 0;
      preview_scroll_offset = 0;
      invalidate_preview_cache();
      return;
    }
    scan_error_.clear();
    on_redraw(std::move(out.first));
  };

  if (!tq->submit_val<std::pair<std::vector<FileMatch>, std::string>>(std::move(scan_work),
                                                                       std::move(scan_done)))
  {
    // Submit refused (queue shut down): run the sync walk below instead of
    // leaving the cache empty with a scan marked in flight.
  }
  else
  {
    return;
  }

  update_results();
  land_scan(std::move(all_entries_));
}
