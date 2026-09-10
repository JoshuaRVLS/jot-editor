// Git panel actions (src/jot/workspace/git_panel.cpp).
//
// The lazygit-style git panel's state transitions and git commands: view
// switching (2 files / 3 branches / 4 commits / 5 stash, matching lazygit's
// panel numbers), selection, staging, diffing, committing, discarding,
// stashing, checkout/merge and push/pull/fetch. Rendering lives in
// src/render/git_panel.cpp; the shared row model in
// src/jot/workspace/git_panel_models.h.

#include "editor.h"

#include "jot/workspace/git_panel_models.h"
#include "jot/workspace/git_run.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace
{
  namespace fs = std::filesystem;

  // Path relative to the repo root for display / git commands.
  std::string relative_to_root(const std::string &abs_path, const std::string &root)
  {
    if (root.empty() || abs_path.size() <= root.size())
    {
      return abs_path;
    }
    if (abs_path.compare(0, root.size(), root) == 0)
    {
      std::string rel = abs_path.substr(root.size());
      while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
      {
        rel.erase(rel.begin());
      }
      return rel;
    }
    return abs_path;
  }

  bool is_untracked(const std::string &xy)
  {
    return xy == "??";
  }
} // namespace

void Editor::toggle_git_panel()
{
  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT)
  {
    close_right_panel_tab(RIGHT_PANEL_GIT);
    git_panel.pending_confirm.clear();
    set_message("Git panel closed");
    return;
  }
  open_right_panel_tab(RIGHT_PANEL_GIT);
  // Keyboard focus follows the panel: arrows / j-k navigate the panel, not
  // the editor or the sidebar (which would otherwise keep the focus it had
  // from an earlier click).
  focus_state = FOCUS_RIGHT_PANEL;
  show_home_menu = false;
  refresh_git_status(true);
  git_panel_refresh();
  set_message("Git panel — 2 files · 3 branches · 4 commits · 5 stash · ? keys");
  needs_redraw = true;
}

void Editor::git_panel_refresh()
{
  git_panel.pending_confirm.clear();
  if (git_root.empty())
  {
    git_panel.files.clear();
    git_panel.branches.clear();
    git_panel.commits.clear();
    git_panel.stashes.clear();
    return;
  }
  const std::string root = git_root;

  // Files: capture `git status --porcelain` synchronously instead of relying
  // on the async status refresh — the panel must show current files the
  // moment it opens, even when that refresh is still in flight. Falls back
  // to the (possibly stale) status map only when the command itself fails;
  // a clean tree (exit 0, empty output) must not look like a failure.
  git_panel.files.clear();
  {
    const jot_git::Captured status = jot_git::capture_ex(root, "status --porcelain=v1");
    const bool captured = status.ok();
    if (captured)
    {
      std::istringstream iss(status.output);
      std::string line;
      while (std::getline(iss, line))
      {
        if (line.size() < 3)
        {
          continue;
        }
        const std::string xy = line.substr(0, 2);
        std::string rel_path = line.substr(3);
        const size_t arrow = rel_path.find(" -> ");
        if (arrow != std::string::npos)
        {
          rel_path = rel_path.substr(arrow + 4);
        }
        if (rel_path.size() >= 2 && rel_path.front() == '"' && rel_path.back() == '"')
        {
          rel_path = rel_path.substr(1, rel_path.size() - 2);
        }
        if (rel_path.empty())
        {
          continue;
        }
        jot_git_panel::FileRow row;
        row.status = xy;
        row.rel_path = rel_path;
        row.abs_path = (fs::path(root) / fs::path(rel_path)).lexically_normal().string();
        git_panel.files.push_back(std::move(row));
      }
    }
    if (!captured)
    {
      git_panel.files.reserve(git_file_status.size());
      for (const auto &entry : git_file_status)
      {
        jot_git_panel::FileRow row;
        row.abs_path = entry.first;
        row.status = entry.second;
        row.rel_path = relative_to_root(entry.first, root);
        git_panel.files.push_back(std::move(row));
      }
    }
  }
  std::sort(git_panel.files.begin(),
            git_panel.files.end(),
            [](const jot_git_panel::FileRow &a, const jot_git_panel::FileRow &b)
            {
              const int ar = jot_git_panel::status_rank(a.status);
              const int br = jot_git_panel::status_rank(b.status);
              if (ar != br)
              {
                return ar < br;
              }
              return a.rel_path < b.rel_path;
            });

  // Keep the panel header / status line counts in step with the captured
  // files: an async status result that landed mid-action (e.g. between
  // staging and committing) must not leave the header disagreeing with the
  // rows below it.
  git_conflict_count = git_staged_count = git_unstaged_count = git_untracked_count = 0;
  for (const auto &f : git_panel.files)
  {
    const std::string section = jot_git_panel::status_section(f.status);
    if (section == "conflict")
    {
      git_conflict_count++;
    }
    else if (section == "staged")
    {
      git_staged_count++;
    }
    else if (section == "untracked")
    {
      git_untracked_count++;
    }
    else
    {
      git_unstaged_count++;
    }
  }

  // Local branches with the current one marked (git branch -vv); the
  // upstream tracking pair "[origin/main]" (with optional ahead/behind)
  // becomes the row's right-aligned detail.
  git_panel.branches.clear();
  {
    std::istringstream iss(jot_git::capture(root, "branch --no-color -vv"));
    std::string line;
    while (std::getline(iss, line))
    {
      if (line.size() < 2)
      {
        continue;
      }
      jot_git_panel::BranchRow row;
      row.current = line[0] == '*';
      std::istringstream ls(line.substr(2));
      std::string name;
      if (!(ls >> name) || name.empty())
      {
        continue;
      }
      row.name = name;
      const size_t bracket = line.find('[');
      const size_t end_bracket = bracket == std::string::npos ? std::string::npos
                                                              : line.find(']', bracket);
      if (bracket != std::string::npos && end_bracket != std::string::npos)
      {
        row.tracking = line.substr(bracket, end_bracket - bracket + 1);
      }
      git_panel.branches.push_back(std::move(row));
    }
  }

  // Recent commits: short hash, date, subject.
  git_panel.commits.clear();
  {
    std::istringstream iss(jot_git::capture(
        root, "log -n 30 --pretty=format:%h%x09%ad%x09%s --date=short"));
    std::string line;
    while (std::getline(iss, line))
    {
      jot_git_panel::CommitRow row;
      std::istringstream ls(line);
      if (!std::getline(ls, row.hash, '\t') || row.hash.empty())
      {
        continue;
      }
      std::getline(ls, row.date, '\t');
      std::getline(ls, row.subject);
      git_panel.commits.push_back(std::move(row));
    }
  }

  // Stash list.
  git_panel.stashes.clear();
  {
    std::istringstream iss(
        jot_git::capture(root, "stash list --pretty=format:%gd%x09%s"));
    std::string line;
    while (std::getline(iss, line))
    {
      jot_git_panel::StashRow row;
      std::istringstream ls(line);
      if (!std::getline(ls, row.ref, '\t') || row.ref.empty())
      {
        continue;
      }
      std::getline(ls, row.subject);
      git_panel.stashes.push_back(std::move(row));
    }
  }

  const size_t count = git_panel.view == jot_git_panel::View::Files
                           ? git_panel.files.size()
                           : (git_panel.view == jot_git_panel::View::Branches
                                  ? git_panel.branches.size()
                                  : (git_panel.view == jot_git_panel::View::Commits
                                         ? git_panel.commits.size()
                                         : git_panel.stashes.size()));
  git_panel.selected = std::clamp(git_panel.selected, 0, std::max(0, (int)count - 1));
  needs_redraw = true;
}

void Editor::git_panel_switch_view(int view_number)
{
  using namespace jot_git_panel;
  switch (view_number)
  {
  case 2:
    git_panel.view = View::Files;
    break;
  case 3:
    git_panel.view = View::Branches;
    break;
  case 4:
    git_panel.view = View::Commits;
    break;
  case 5:
    git_panel.view = View::Stash;
    break;
  default:
    return;
  }
  git_panel.selected = 0;
  git_panel.scroll = 0;
  git_panel_refresh();
}

void Editor::git_panel_move_selection(int delta)
{
  using namespace jot_git_panel;
  const size_t count = git_panel.view == View::Files
                           ? git_panel.files.size()
                           : (git_panel.view == View::Branches
                                  ? git_panel.branches.size()
                                  : (git_panel.view == View::Commits
                                         ? git_panel.commits.size()
                                         : git_panel.stashes.size()));
  if (count == 0)
  {
    return;
  }
  git_panel.pending_confirm.clear();
  git_panel.selected = std::clamp(git_panel.selected + delta, 0, (int)count - 1);
  needs_redraw = true;
}

void Editor::git_panel_page(int delta)
{
  git_panel_move_selection(delta * 12);
}

void Editor::git_panel_jump_to_end(bool bottom)
{
  using namespace jot_git_panel;
  const size_t count = git_panel.view == View::Files
                           ? git_panel.files.size()
                           : (git_panel.view == View::Branches
                                  ? git_panel.branches.size()
                                  : (git_panel.view == View::Commits
                                         ? git_panel.commits.size()
                                         : git_panel.stashes.size()));
  if (count == 0)
  {
    return;
  }
  git_panel.selected = bottom ? (int)count - 1 : 0;
  needs_redraw = true;
}

static const jot_git_panel::FileRow *selected_file(const jot_git_panel::State &state)
{
  if (state.view != jot_git_panel::View::Files)
  {
    return nullptr;
  }
  if (state.selected < 0 || state.selected >= (int)state.files.size())
  {
    return nullptr;
  }
  return &state.files[(size_t)state.selected];
}

void Editor::git_panel_primary()
{
  using namespace jot_git_panel;
  git_panel.pending_confirm.clear();
  const std::string root = git_root;
  if (root.empty())
  {
    set_message("Git: not a repository");
    return;
  }

  switch (git_panel.view)
  {
  case View::Files:
  {
    const FileRow *file = selected_file(git_panel);
    if (!file)
    {
      return;
    }
    if (status_section(file->status) == "staged")
    {
      if (git_unstage_path(file->abs_path))
      {
        set_message("Unstaged: " + file->rel_path);
      }
    }
    else if (git_stage_path(file->abs_path))
    {
      set_message("Staged: " + file->rel_path);
    }
    break;
  }
  case View::Branches:
  {
    if (git_panel.selected < 0 || git_panel.selected >= (int)git_panel.branches.size())
    {
      return;
    }
    const std::string name = git_panel.branches[(size_t)git_panel.selected].name;
    if (jot_git::run_ok(root, "checkout " + shell_util::shell_quote(name)))
    {
      set_message("Checked out: " + name);
    }
    else
    {
      set_message("Checkout failed: " + name);
    }
    break;
  }
  case View::Commits:
  {
    if (git_panel.selected < 0 || git_panel.selected >= (int)git_panel.commits.size())
    {
      return;
    }
    const std::string hash = git_panel.commits[(size_t)git_panel.selected].hash;
    if (jot_git::run_ok(root, "checkout " + shell_util::shell_quote(hash)))
    {
      set_message("Checked out " + hash + " (detached HEAD)");
    }
    else
    {
      set_message("Checkout failed: " + hash);
    }
    break;
  }
  case View::Stash:
  {
    if (git_panel.selected < 0 || git_panel.selected >= (int)git_panel.stashes.size())
    {
      return;
    }
    const std::string ref = git_panel.stashes[(size_t)git_panel.selected].ref;
    if (jot_git::run_ok(root, "stash apply " + shell_util::shell_quote(ref)))
    {
      set_message("Applied " + ref);
    }
    else
    {
      set_message("Stash apply failed: " + ref);
    }
    break;
  }
  }
  refresh_git_status(true);
  git_panel_refresh();
}

void Editor::git_panel_stage_all()
{
  if (git_stage_all())
  {
    set_message("Staged all changes");
  }
  git_panel_refresh();
}

void Editor::git_panel_unstage_all()
{
  if (git_unstage_all())
  {
    set_message("Unstaged all changes");
  }
  git_panel_refresh();
}

void Editor::git_panel_open_diff_selected()
{
  using namespace jot_git_panel;
  const FileRow *file = selected_file(git_panel);
  if (!file)
  {
    return;
  }
  const bool staged = jot_git_panel::status_section(file->status) == "staged";
  git_panel.return_after_diff = true;
  open_git_diff_panel(file->rel_path, staged);
}

void Editor::git_panel_commit_prompt()
{
  if (git_root.empty())
  {
    set_message("Git: not a repository");
    return;
  }
  // Reuse the command palette as the commit-message input: prefilled with
  // ":gitcommit ", typing the message and pressing Enter runs it.
  open_command_palette(":gitcommit ");
}

void Editor::git_panel_discard_or_delete()
{
  using namespace jot_git_panel;
  const std::string root = git_root;
  if (root.empty())
  {
    return;
  }
  std::string confirm_key;
  std::string action_label;
  switch (git_panel.view)
  {
  case View::Files:
    confirm_key = "discard";
    action_label = "discard changes";
    break;
  case View::Branches:
    confirm_key = "delete-branch";
    action_label = "delete branch";
    break;
  case View::Stash:
    confirm_key = "drop-stash";
    action_label = "drop stash";
    break;
  default:
    return;
  }

  if (git_panel.pending_confirm != confirm_key)
  {
    git_panel.pending_confirm = confirm_key;
    set_message("Press d again to " + action_label);
    needs_redraw = true;
    return;
  }
  git_panel.pending_confirm.clear();

  switch (git_panel.view)
  {
  case View::Files:
  {
    const FileRow *file = selected_file(git_panel);
    if (!file)
    {
      return;
    }
    if (is_untracked(file->status))
    {
      set_message("Untracked file — delete it manually (not discarded)");
      return;
    }
    if (jot_git::run_ok(root, "checkout -- " + shell_util::shell_quote(file->rel_path)))
    {
      set_message("Discarded changes: " + file->rel_path);
    }
    else
    {
      set_message("Discard failed: " + file->rel_path);
    }
    break;
  }
  case View::Branches:
  {
    if (git_panel.selected < 0 || git_panel.selected >= (int)git_panel.branches.size())
    {
      return;
    }
    const BranchRow &row = git_panel.branches[(size_t)git_panel.selected];
    if (row.current)
    {
      set_message("Cannot delete the current branch");
      return;
    }
    if (jot_git::run_ok(root, "branch -d " + shell_util::shell_quote(row.name)))
    {
      set_message("Deleted branch: " + row.name);
    }
    else
    {
      set_message("Branch delete failed (unmerged?) — use :gitbranch -D");
    }
    break;
  }
  case View::Stash:
  {
    if (git_panel.selected < 0 || git_panel.selected >= (int)git_panel.stashes.size())
    {
      return;
    }
    const std::string ref = git_panel.stashes[(size_t)git_panel.selected].ref;
    if (jot_git::run_ok(root, "stash drop " + shell_util::shell_quote(ref)))
    {
      set_message("Dropped " + ref);
    }
    else
    {
      set_message("Stash drop failed: " + ref);
    }
    break;
  }
  default:
    return;
  }
  refresh_git_status(true);
  git_panel_refresh();
}

void Editor::git_panel_stash_push()
{
  if (git_root.empty())
  {
    set_message("Git: not a repository");
    return;
  }
  if (jot_git::run_ok(git_root, "stash push -u -m jot"))
  {
    set_message("Stashed all changes (including untracked)");
  }
  else
  {
    set_message("Stash failed");
  }
  refresh_git_status(true);
  git_panel_refresh();
}

void Editor::git_panel_stash_pop()
{
  if (git_root.empty())
  {
    set_message("Git: not a repository");
    return;
  }
  if (git_panel.stashes.empty())
  {
    set_message("Nothing to pop — stash is empty");
    return;
  }
  if (jot_git::run_ok(git_root, "stash pop"))
  {
    set_message("Popped " + git_panel.stashes[0].ref);
  }
  else
  {
    set_message("Stash pop failed (conflicts?)");
  }
  refresh_git_status(true);
  git_panel_refresh();
}

void Editor::git_panel_new_branch_prompt()
{
  if (git_root.empty())
  {
    set_message("Git: not a repository");
    return;
  }
  open_command_palette(":gitcheckout -b ");
}

void Editor::git_panel_merge_prompt()
{
  using namespace jot_git_panel;
  if (git_root.empty())
  {
    set_message("Git: not a repository");
    return;
  }
  std::string prefill = ":gitmerge ";
  if (git_panel.selected >= 0 && git_panel.selected < (int)git_panel.branches.size())
  {
    prefill += git_panel.branches[(size_t)git_panel.selected].name;
  }
  open_command_palette(prefill);
}

void Editor::git_panel_fetch()
{
  if (git_root.empty())
  {
    set_message("Git: not a repository");
    return;
  }
  if (jot_git::run_ok(git_root, "fetch"))
  {
    set_message("Fetched from origin");
  }
  else
  {
    set_message("Fetch failed");
  }
  git_panel_refresh();
}

void Editor::git_panel_push()
{
  if (git_root.empty())
  {
    set_message("Git: not a repository");
    return;
  }
  if (jot_git::run_ok(git_root, "push"))
  {
    set_message("Pushed " + git_branch);
  }
  else
  {
    set_message("Push failed — set an upstream first (git push -u origin " + git_branch
                + " from a terminal)");
  }
  git_panel_refresh();
}

void Editor::git_panel_pull()
{
  if (git_root.empty())
  {
    set_message("Git: not a repository");
    return;
  }
  if (jot_git::run_ok(git_root, "pull --no-edit"))
  {
    set_message("Pulled " + git_branch);
  }
  else
  {
    set_message("Pull failed (conflicts?)");
  }
  refresh_git_status(true);
  git_panel_refresh();
}

void Editor::git_panel_copy()
{
  using namespace jot_git_panel;
  std::string text;
  switch (git_panel.view)
  {
  case View::Files:
  {
    const FileRow *file = selected_file(git_panel);
    if (file)
    {
      text = file->rel_path;
    }
    break;
  }
  case View::Branches:
    if (git_panel.selected >= 0 && git_panel.selected < (int)git_panel.branches.size())
    {
      text = git_panel.branches[(size_t)git_panel.selected].name;
    }
    break;
  case View::Commits:
    if (git_panel.selected >= 0 && git_panel.selected < (int)git_panel.commits.size())
    {
      text = git_panel.commits[(size_t)git_panel.selected].hash;
    }
    break;
  case View::Stash:
    if (git_panel.selected >= 0 && git_panel.selected < (int)git_panel.stashes.size())
    {
      text = git_panel.stashes[(size_t)git_panel.selected].ref;
    }
    break;
  }
  if (text.empty())
  {
    return;
  }
  set_clipboard_text(text);
  set_message("Copied: " + text);
}

bool Editor::handle_git_panel_mouse(int x, int y, bool is_click, bool is_double_click)
{
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_GIT || !ui)
  {
    return false;
  }
  int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return false;
  }
  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = topbar_height();
  const int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  if (x < panel_x || x >= panel_x + panel_w || y < panel_y || y >= panel_y + panel_h)
  {
    // Pointer left the panel: drop the hover highlight.
    if (git_panel_hover_row != -1)
    {
      git_panel_hover_row = -1;
      needs_redraw = true;
    }
    return false;
  }
  // Row area starts below the title + tab strip + header (see render_git_panel).
  const int row_top = panel_y + 4;
  const int row_index = y - row_top;
  using namespace jot_git_panel;
  const std::vector<FlatRow> flat = build_flat_rows(git_panel);
  const int visible = panel_h - 4;
  const int index = git_panel.scroll + row_index;
  int hover_row = -1;
  if (row_index >= 0 && index >= 0 && index < (int)flat.size() && index < git_panel.scroll + visible
      && !flat[(size_t)index].section)
  {
    hover_row = flat[(size_t)index].index;
  }
  if (!is_click)
  {
    // Motion: track the hover highlight without stealing focus.
    if (git_panel_hover_row != hover_row)
    {
      git_panel_hover_row = hover_row;
      needs_redraw = true;
    }
    return true;
  }
  git_panel_hover_row = hover_row;
  focus_state = FOCUS_RIGHT_PANEL;
  needs_redraw = true;
  if (row_index < 0 || index < 0 || index >= (int)flat.size() || index >= git_panel.scroll + visible)
  {
    return true;
  }
  const FlatRow &row = flat[(size_t)index];
  if (!row.section && row.index >= 0)
  {
    // Double-click detection mirrors the sidebar's: two clicks on the
    // same row within 350 ms open the file's diff.
    const long long now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    const bool dbl = is_double_click
                     || (last_git_panel_click_ms > 0 && now_ms - last_git_panel_click_ms <= 350
                         && last_git_panel_click_row == index);
    last_git_panel_click_ms = now_ms;
    last_git_panel_click_row = index;
    git_panel.selected = row.index;
    needs_redraw = true;
    if (dbl && git_panel.view == View::Files)
    {
      git_panel_open_diff_selected();
    }
  }
  return true;
}