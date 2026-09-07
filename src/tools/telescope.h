#ifndef TELESCOPE_H
#define TELESCOPE_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct FileMatch
{
  std::string path;
  std::string name;
  std::string relative_path;
  std::string parent_path;
  int score;
  bool is_directory;
};

struct TelescopePreview
{
  std::vector<std::string> lines;
  std::string title;
  std::string detail;
  std::uintmax_t size_bytes = 0;
  bool is_directory = false;
  bool is_binary = false;
  bool skipped = false;
  bool truncated = false;
};

enum class TelescopeFocus
{
  Query,
  Results,
  Preview,
};

struct TelescopeLayout
{
  bool valid = false;
  bool show_preview = false;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int inner_x = 0;
  int inner_y = 0;
  int inner_w = 0;
  int inner_h = 0;
  int query_x = 0;
  int query_y = 0;
  int query_w = 0;
  int body_y = 0;
  int body_h = 0;
  int list_x = 0;
  int list_y = 0;
  int list_w = 0;
  int list_h = 0;
  int preview_x = 0;
  int preview_y = 0;
  int preview_w = 0;
  int preview_h = 0;
  int footer_y = 0;
};

TelescopeLayout
telescope_layout_for(int render_width, int screen_height, int top_bound, int bottom_bound);

class TaskQueue;

class Telescope
{
public:
  Telescope();

  void open(const std::string &root = "");
  void close();
  bool is_active() const
  {
    return active;
  }

  void
  set_query(const std::string &q, TaskQueue *tq = nullptr, std::function<void()> on_update = {});
  void update_results();

  void scan_async(TaskQueue *tq, std::function<void()> on_update = {});
  void cancel_scan();
  void apply_results(std::vector<FileMatch> new_results);
  // Drops the cached candidate list; the next set_query / scan_async re-walks
  // the directory tree so newly created files show up (Ctrl+R inside the
  // picker). Filtering between cache drops is always instant.
  void invalidate_cache();

  void move_up();
  void move_down();
  void move_by(int delta);
  void select_index(int index);
  void ensure_selected_visible(int visible_rows);
  void select();
  void go_parent();
  void scroll_preview(int delta, int visible_rows);
  void cycle_focus(int delta);
  void set_focus(TelescopeFocus value)
  {
    focus_ = value;
  }

  std::string get_selected_path() const;
  std::string get_selected_relative_path() const;
  TelescopePreview get_selected_preview() const;
  std::vector<std::string> get_preview_lines() const;

  const std::vector<FileMatch> &get_results() const
  {
    return results;
  }
  int get_result_count() const
  {
    return (int)results.size();
  }
  int get_selected_index() const
  {
    return selected_index;
  }
  int get_list_scroll_offset() const
  {
    return list_scroll_offset;
  }
  int get_preview_scroll_offset() const
  {
    return preview_scroll_offset;
  }
  std::string get_query() const
  {
    return query;
  }
  std::string get_root_dir() const
  {
    return root_dir.string();
  }
  int current_scan_id() const
  {
    return scan_id_.load();
  }
  bool scan_pending() const
  {
    return scan_pending_;
  }
  bool can_accept_selection() const
  {
    return !scan_pending_ && !results.empty();
  }
  TelescopeFocus focus() const
  {
    return focus_;
  }

  static bool fuzzy_match(const std::string &text, const std::string &pattern);
  static int fuzzy_score(const std::string &text, const std::string &pattern);
  // Composite ranking used by both the sync and async result builders:
  // name*2 + path + bonuses, minus a penalty for duplicate/generated-looking
  // names ("foo (1).c", "foo - Copy.c") and plus a boost for real source
  // extensions so clean code files outrank numbered copies and assets.
  static int rank_score(const std::string &name,
                        const std::string &relative_path,
                        const std::string &query_lc,
                        bool is_directory);

private:
  bool active;
  std::string query;
  std::vector<FileMatch> results;
  int selected_index;
  int list_scroll_offset = 0;
  int preview_scroll_offset = 0;
  bool scan_pending_ = false;
  TelescopeFocus focus_ = TelescopeFocus::Query;
  fs::path root_dir;

  std::atomic<int> scan_id_{0};
  std::shared_ptr<std::atomic<int>> scan_generation_ = std::make_shared<std::atomic<int>>(0);
  mutable bool preview_cache_valid = false;
  mutable std::string preview_cache_path;
  mutable TelescopePreview preview_cache;

  // Raw file listing collected once per open / root change. Query edits never
  // re-walk the tree: they run publish_filtered() over this cache, which is
  // what makes typing feel instant (fzf-style) on large projects.
  std::vector<FileMatch> all_entries_;
  bool entries_valid_ = false;

  void scan_directory(const fs::path &dir, int depth = 0);
  // results <- fuzzy-filter(all_entries_, query), sorted + capped. No I/O.
  void publish_filtered();
  void invalidate_preview_cache();
  TelescopePreview load_preview(const FileMatch &match) const;
};

#endif
