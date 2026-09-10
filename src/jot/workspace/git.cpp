#include "editor.h"
#include "jot/lua/api.h"
#include "jot/workspace/git_run.h"
#include "tools/string_util.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace
{
  namespace fs = std::filesystem;

  std::string normalize_path(const std::string &path)
  {
    if (path.empty())
    {
      return "";
    }
    std::error_code ec;
    fs::path p = fs::absolute(path, ec);
    if (ec)
    {
      p = fs::path(path);
    }
    return p.lexically_normal().string();
  }

  std::string parse_branch_name(const std::string &line)
  {
    if (line.rfind("## ", 0) != 0)
    {
      return "";
    }
    std::string body = line.substr(3);
    size_t dots = body.find("...");
    size_t space = body.find(' ');
    size_t cut = std::string::npos;
    if (dots != std::string::npos)
    {
      cut = dots;
    }
    else if (space != std::string::npos)
    {
      cut = space;
    }
    if (cut != std::string::npos)
    {
      body = body.substr(0, cut);
    }
    if (body == "HEAD" || body.empty())
    {
      return "";
    }
    return body;
  }

  struct GitStatusResult
  {
    std::string root;
    std::string branch;
    int ahead = 0;
    int behind = 0;
    int dirty_count = 0;
    int staged_count = 0;
    int unstaged_count = 0;
    int untracked_count = 0;
    int deleted_count = 0;
    int renamed_count = 0;
    int conflict_count = 0;
    std::unordered_map<std::string, std::string> file_status;
    bool success = false;
  };

  bool is_conflict_status(const std::string &xy)
  {
    return xy == "DD" || xy == "AU" || xy == "UD" || xy == "UA" || xy == "DU" || xy == "AA"
           || xy == "UU" || xy.find('U') != std::string::npos;
  }

  GitStatusResult run_git_commands(const std::string &repo_hint)
  {
    GitStatusResult result;

    const std::string top = jot_git::capture(repo_hint, "rev-parse --show-toplevel");
    if (top.empty())
    {
      return result;
    }

    result.root = normalize_path(top);
    result.success = true;

    result.branch = jot_git::capture(result.root, "symbolic-ref --short HEAD");
    if (result.branch.empty())
    {
      result.branch = jot_git::capture(result.root, "rev-parse --short HEAD");
    }
    if (result.branch.empty())
    {
      result.branch = "(detached)";
    }

    const std::string status_text = jot_git::capture(result.root, "status --porcelain=v1 --branch");
    std::istringstream iss(status_text);
    std::string line;
    while (std::getline(iss, line))
    {
      if (line.empty())
        continue;
      if (line.rfind("## ", 0) == 0)
      {
        std::string from_status = parse_branch_name(line);
        if (!from_status.empty())
          result.branch = from_status;
        // "## main...origin/main [ahead 1, behind 2]"
        const size_t bracket = line.find('[');
        if (bracket != std::string::npos)
        {
          const std::string info = line.substr(bracket);
          const size_t ahead_pos = info.find("ahead ");
          if (ahead_pos != std::string::npos)
          {
            result.ahead = std::atoi(info.c_str() + ahead_pos + 6);
          }
          const size_t behind_pos = info.find("behind ");
          if (behind_pos != std::string::npos)
          {
            result.behind = std::atoi(info.c_str() + behind_pos + 7);
          }
        }
        continue;
      }
      if (line.size() < 3)
        continue;
      const std::string xy = line.substr(0, 2);
      std::string rel_path = line.substr(3);
      size_t arrow = rel_path.find(" -> ");
      if (arrow != std::string::npos)
      {
        rel_path = rel_path.substr(arrow + 4);
      }
      if (!rel_path.empty() && rel_path.front() == '"' && rel_path.back() == '"'
          && rel_path.size() >= 2)
      {
        rel_path = rel_path.substr(1, rel_path.size() - 2);
      }
      std::string abs_path = normalize_path((fs::path(result.root) / fs::path(rel_path)).string());
      if (!abs_path.empty())
      {
        result.file_status[abs_path] = xy;
      }
      if (xy != "  ")
      {
        result.dirty_count++;
      }
      const bool conflict = is_conflict_status(xy);
      const char index_status = xy[0];
      const char worktree_status = xy[1];
      if (xy == "??")
      {
        result.untracked_count++;
      }
      else
      {
        if (conflict)
        {
          result.conflict_count++;
        }
        if (!conflict && index_status != ' ' && index_status != '?')
        {
          result.staged_count++;
        }
        if (!conflict && worktree_status != ' ' && worktree_status != '?')
        {
          result.unstaged_count++;
        }
        if (index_status == 'D' || worktree_status == 'D')
        {
          result.deleted_count++;
        }
        if (index_status == 'R' || worktree_status == 'R')
        {
          result.renamed_count++;
        }
      }
    }

    return result;
  }

} // namespace

void Editor::clear_git_status()
{
  git_root.clear();
  git_branch.clear();
  git_dirty_count = 0;
  git_staged_count = 0;
  git_unstaged_count = 0;
  git_untracked_count = 0;
  git_deleted_count = 0;
  git_renamed_count = 0;
  git_conflict_count = 0;
  git_file_status.clear();
  invalidate_sidebar_git_cache();
}

bool Editor::has_git_repo() const
{
  return !git_root.empty();
}

std::string Editor::run_git_capture(const std::string &args) const
{
  if (git_root.empty())
  {
    return "";
  }
  return jot_git::capture(git_root, args);
}

bool Editor::open_git_diff_panel(const std::string &path, bool staged)
{
  refresh_git_status(true);

  if (!has_git_repo())
  {
    set_message("Git: Not a repo");
    return false;
  }

  std::string target = path;
  if (target.empty() && !buffers.empty())
  {
    const auto &buf = get_buffer();
    if (!buf.filepath.empty())
    {
      target = to_git_relative_path(buf.filepath);
    }
  }

  if (target.empty())
  {
    set_message(staged ? "Usage: :gitdiffstaged [file]" : "Usage :gitdiff [file]");
    return false;
  }

  fs::path input(target);
  std::string rel = input.is_absolute() ? to_git_relative_path(target) : input.generic_string();

  if (rel.empty() || rel[0] == '/' || string_util::starts_with(rel, "../"))
  {
    set_message("Git diff: path outside repo");
    return false;
  }

  std::string args = staged ? "diff --staged -- " : "diff -- ";
  std::string diff = run_git_capture(args + shell_util::shell_quote(rel));

  if (diff.empty())
  {
    set_message(staged ? "Git diff: no staged changes for " + rel
                       : "Git Diff: No unstaged changes for " + rel);
    return false;
  }

  git_diff_panel.visible = true;
  git_diff_panel.staged = staged;
  git_diff_panel.path = rel;
  git_diff_panel.lines.clear();
  git_diff_panel.scroll = 0;

  std::istringstream stream(diff);
  std::string line;

  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    git_diff_panel.lines.push_back(line);
  }

  open_right_panel_tab(RIGHT_PANEL_GIT_DIFF);
  show_debugger_panel = false;
  needs_redraw = true;

  set_message("Git diff opened: " + rel);
  return true;
}

void Editor::close_git_diff_panel()
{
  git_diff_panel = GitDiffPanel();
  if (active_right_panel_tab == RIGHT_PANEL_GIT_DIFF)
  {
    // Coming back from the git panel's per-file diff returns to the panel;
    // otherwise the diff tab closes (the dock stays open if other tabs
    // remain, and hides when the last tab is closed).
    if (git_panel.return_after_diff)
    {
      git_panel.return_after_diff = false;
      open_right_panel_tab(RIGHT_PANEL_GIT);
      focus_state = FOCUS_RIGHT_PANEL;
      git_panel_refresh();
    }
    else
    {
      close_right_panel_tab(RIGHT_PANEL_GIT_DIFF);
    }
  }

  needs_redraw = true;
}

void Editor::scroll_git_diff_panel(int delta)
{
  if (!git_diff_panel.visible)
  {
    return;
  }

  int panel_h = ui ? std::max(1, ui->get_height() - status_height - 1) : 1;
  int body_h = std::max(1, panel_h - 4);
  int max_scroll = std::max(0, (int)git_diff_panel.lines.size() - body_h);
  int next = std::clamp(git_diff_panel.scroll + delta, 0, max_scroll);

  if (next != git_diff_panel.scroll)
  {
    git_diff_panel.scroll = next;
    needs_redraw = true;
  }
}

std::string Editor::to_git_relative_path(const std::string &path) const
{
  if (git_root.empty() || path.empty())
  {
    return "";
  }
  std::error_code ec;
  fs::path abs_path = fs::absolute(path, ec);
  if (ec)
  {
    return path;
  }
  fs::path root = fs::path(git_root);
  fs::path rel = abs_path.lexically_relative(root);
  std::string rel_s = rel.generic_string();
  if (rel_s.empty() || rel_s == "." || string_util::starts_with(rel_s, "../"))
  {
    return abs_path.generic_string();
  }
  return rel_s;
}

bool Editor::git_stage_path(const std::string &path)
{
  if (git_root.empty() || path.empty())
  {
    return false;
  }
  fs::path input(path);
  std::string rel = input.is_absolute() ? to_git_relative_path(path) : input.generic_string();
  if (rel.empty() || rel[0] == '/' || string_util::starts_with(rel, "../"))
  {
    return false;
  }
  jot_git::Captured result =
      jot_git::capture_errors(git_root, "add -A -- " + shell_util::shell_quote(rel));
  if (result.ok())
  {
    refresh_git_status(true);
  }
  return result.ok();
}

bool Editor::git_unstage_path(const std::string &path)
{
  if (git_root.empty() || path.empty())
  {
    return false;
  }
  fs::path input(path);
  std::string rel = input.is_absolute() ? to_git_relative_path(path) : input.generic_string();
  if (rel.empty() || rel[0] == '/' || string_util::starts_with(rel, "../"))
  {
    return false;
  }
  jot_git::Captured result =
      jot_git::capture_errors(git_root, "restore --staged -- " + shell_util::shell_quote(rel));
  if (result.ok())
  {
    refresh_git_status(true);
  }
  return result.ok();
}

bool Editor::git_stage_all()
{
  if (git_root.empty())
  {
    return false;
  }
  jot_git::Captured result = jot_git::capture_errors(git_root, "add -A");
  if (result.ok())
  {
    refresh_git_status(true);
  }
  return result.ok();
}

bool Editor::git_unstage_all()
{
  if (git_root.empty())
  {
    return false;
  }
  jot_git::Captured result = jot_git::capture_errors(git_root, "restore --staged .");
  if (result.ok())
  {
    refresh_git_status(true);
  }
  return result.ok();
}

// Returns an empty string on success, or git's error (last useful line) on
// failure so callers can tell the user why the commit did not land.
std::string Editor::git_commit_message(const std::string &message)
{
  if (git_root.empty())
  {
    return "not a repository";
  }
  if (message.empty())
  {
    return "empty message";
  }
  // Smart commit: when the index is empty, stage everything first (tracked
  // and untracked) so `c` in the git panel works right after editing files —
  // the same flow as `a` (stage all) then commit. A deliberate staged
  // selection is committed as-is.
  jot_git::Captured cached = jot_git::capture_errors(git_root, "diff --cached --quiet");
  if (cached.ok())
  {
    jot_git::capture_errors(git_root, "add -A");
  }

  jot_git::Captured result =
      jot_git::capture_errors(git_root, "commit -m " + shell_util::shell_quote(message));
  if (result.ok())
  {
    refresh_git_status(true);
    return "";
  }
  std::string err = result.output;
  const size_t nl = err.rfind('\n');
  if (nl != std::string::npos)
  {
    err = err.substr(nl + 1);
  }
  return err.empty() ? "commit failed" : err;
}

void Editor::refresh_git_status(bool force)
{
  using namespace std::chrono;
  const long long now_ms =
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();

  if (!force && git_last_refresh_ms > 0 && now_ms - git_last_refresh_ms < 1500)
  {
    return;
  }

  if (git_refresh_pending_)
    return;

  std::string repo_hint;
  if (workspace_session_enabled && !workspace_session_root.empty())
  {
    repo_hint = workspace_session_root;
  }
  else if (!root_dir.empty())
  {
    repo_hint = root_dir;
  }
  else if (!buffers.empty() && !get_buffer().filepath.empty())
  {
    repo_hint = fs::path(get_buffer().filepath).parent_path().string();
  }

  if (repo_hint.empty())
  {
    git_last_refresh_ms = now_ms;
    if (!git_root.empty() || !git_file_status.empty() || git_dirty_count != 0
        || git_staged_count != 0 || git_unstaged_count != 0 || git_untracked_count != 0
        || git_deleted_count != 0 || git_renamed_count != 0 || git_conflict_count != 0
        || !git_branch.empty())
    {
      clear_git_status();
      needs_redraw = true;
    }
    return;
  }

  git_last_refresh_ms = now_ms;
  git_refresh_pending_ = true;

  if (task_queue_)
  {
    task_queue_->submit_val<GitStatusResult>(
        [repo_hint = std::move(repo_hint)]() -> GitStatusResult
        { return run_git_commands(repo_hint); },
        [this](GitStatusResult result)
        {
          git_refresh_pending_ = false;

          // Editor may have shut down while the git command was
          // running on the worker thread. Drop the result rather
          // than touching freed state.
          if (!running)
            return;

          if (!result.success)
          {
            if (!git_root.empty() || !git_file_status.empty() || git_dirty_count != 0
                || git_staged_count != 0 || git_unstaged_count != 0 || git_untracked_count != 0
                || git_deleted_count != 0 || git_renamed_count != 0 || git_conflict_count != 0
                || !git_branch.empty())
            {
              clear_git_status();
              needs_redraw = true;
            }
            return;
          }

          const bool changed = (result.root != git_root) || (result.branch != git_branch)
                               || (result.ahead != git_ahead) || (result.behind != git_behind)
                               || (result.dirty_count != git_dirty_count)
                               || (result.staged_count != git_staged_count)
                               || (result.unstaged_count != git_unstaged_count)
                               || (result.untracked_count != git_untracked_count)
                               || (result.deleted_count != git_deleted_count)
                               || (result.renamed_count != git_renamed_count)
                               || (result.conflict_count != git_conflict_count)
                               || (result.file_status != git_file_status);
          git_root = std::move(result.root);
          git_branch = std::move(result.branch);
          git_ahead = result.ahead;
          git_behind = result.behind;
          git_dirty_count = result.dirty_count;
          git_staged_count = result.staged_count;
          git_unstaged_count = result.unstaged_count;
          git_untracked_count = result.untracked_count;
          git_deleted_count = result.deleted_count;
          git_renamed_count = result.renamed_count;
          git_conflict_count = result.conflict_count;
          git_file_status = std::move(result.file_status);
          if (changed)
          {
            invalidate_sidebar_git_cache();
            needs_redraw = true;
          }
          // Keep an open git panel in step with the async status result.
          if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT)
          {
            git_panel_refresh();
          }
          if (lua_api)
            lua_api->emit_git_refreshed();
        });
  }
  else
  {
    GitStatusResult result = run_git_commands(repo_hint);
    git_refresh_pending_ = false;

    if (!result.success)
    {
      if (!git_root.empty() || !git_file_status.empty() || git_dirty_count != 0
          || git_staged_count != 0 || git_unstaged_count != 0 || git_untracked_count != 0
          || git_deleted_count != 0 || git_renamed_count != 0 || git_conflict_count != 0
          || !git_branch.empty())
      {
        clear_git_status();
        needs_redraw = true;
      }
      return;
    }

    const bool changed =
        (result.root != git_root) || (result.branch != git_branch)
        || (result.ahead != git_ahead) || (result.behind != git_behind)
        || (result.dirty_count != git_dirty_count) || (result.staged_count != git_staged_count)
        || (result.unstaged_count != git_unstaged_count)
        || (result.untracked_count != git_untracked_count)
        || (result.deleted_count != git_deleted_count)
        || (result.renamed_count != git_renamed_count)
        || (result.conflict_count != git_conflict_count) || (result.file_status != git_file_status);
    git_root = std::move(result.root);
    git_branch = std::move(result.branch);
    git_ahead = result.ahead;
    git_behind = result.behind;
    git_dirty_count = result.dirty_count;
    git_staged_count = result.staged_count;
    git_unstaged_count = result.unstaged_count;
    git_untracked_count = result.untracked_count;
    git_deleted_count = result.deleted_count;
    git_renamed_count = result.renamed_count;
    git_conflict_count = result.conflict_count;
    git_file_status = std::move(result.file_status);
    if (changed)
    {
      invalidate_sidebar_git_cache();
      needs_redraw = true;
    }
    // Keep an open git panel in step with the async status result.
    if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT)
    {
      git_panel_refresh();
    }
  }
}
