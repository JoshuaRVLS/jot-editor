// The file timeline (src/jot/workspace/timeline.cpp).
//
// The sidebar's timeline section: the commits touching the buffer on screen,
// loaded on the git task queue so a big repository never blocks a frame, and
// cached per file. The parser and the age arithmetic live in timeline.h
// (header-only) so unit tests can drive them without a repo.
#include "jot/workspace/timeline.h"
#include "editor.h"
#include "tools/shell_util.h"
#include "tools/string_util.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace
{
  // Newest commits the section ever holds; the window scrolls the rest.
  constexpr int kTimelineCommits = 20;
  // The same file is reloaded at most this often. The section repaints every
  // frame, and this cache is what keeps that from spawning git every frame.
  constexpr long long kTimelineTtlMs = 3000;
} // namespace

std::string Editor::timeline_current_path() const
{
  if (current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return "";
  }
  return buffers[(size_t)current_buffer].filepath;
}

void Editor::refresh_timeline(bool force)
{
  if (!show_sidebar)
  {
    return;
  }

  const std::string path = timeline_current_path();
  const long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();

  auto clear = [&]()
  {
    timeline_commits.clear();
    timeline_path.clear();
    timeline_loading = false;
    timeline_dirty = false;
    timeline_loaded_ms = now_ms;
    sidebar_timeline_selected = 0;
    sidebar_timeline_scroll = 0;
  };

  if (path.empty() || !has_git_repo())
  {
    if (!timeline_commits.empty() || !timeline_path.empty() || timeline_dirty || timeline_loading)
    {
      clear();
      needs_redraw = true;
    }
    return;
  }

  if (timeline_loading)
  {
    return;
  }
  if (!force && timeline_path == path && !timeline_dirty && timeline_loaded_ms > 0
      && now_ms - timeline_loaded_ms < kTimelineTtlMs)
  {
    return;
  }

  const std::string rel = to_git_relative_path(path);
  if (rel.empty() || rel[0] == '/' || string_util::starts_with(rel, "../"))
  {
    if (!timeline_commits.empty() || timeline_path != path)
    {
      clear();
      needs_redraw = true;
    }
    return;
  }
  if (!task_queue_)
  {
    return;
  }

  timeline_loading = true;
  timeline_loaded_ms = now_ms;
  timeline_dirty = false;
  const std::string root = git_root;
  const std::string args = "log -n " + std::to_string(kTimelineCommits)
                           + " --pretty=format:%h%x09%at%x09%s -- " + shell_util::shell_quote(rel);
  task_queue_->submit_val<std::string>([root, args]() { return jot_git::capture(root, args); },
                                       [this, path](std::string output)
                                       {
                                         timeline_loading = false;
                                         if (!running)
                                         {
                                           return;
                                         }
                                         // The buffer moved on while the log ran: drop the result
                                         // and let the next frame ask for the new file.
                                         if (timeline_current_path() != path)
                                         {
                                           timeline_dirty = true;
                                           return;
                                         }
                                         timeline_commits = jot_timeline::parse_log(output);
                                         timeline_path = path;
                                         const int last =
                                             std::max(0, (int)timeline_commits.size() - 1);
                                         sidebar_timeline_selected =
                                             std::clamp(sidebar_timeline_selected, 0, last);
                                         sidebar_timeline_scroll = 0;
                                         needs_redraw = true;
                                       });
}

void Editor::sidebar_open_timeline_commit(int index)
{
  if (index < 0 || index >= (int)timeline_commits.size())
  {
    return;
  }
  sidebar_timeline_selected = index;
  const std::string path = timeline_current_path();
  const jot_timeline::Commit &commit = timeline_commits[(size_t)index];
  if (path.empty() || !open_commit_diff_panel(commit.hash, path))
  {
    set_message("Timeline: " + commit.hash + "  " + commit.subject);
  }
  needs_redraw = true;
}
