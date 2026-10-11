#ifndef JOT_STATE_WORKSPACE_STATE_H
#define JOT_STATE_WORKSPACE_STATE_H

#include "core/buffer.h"           // GlobalMark
#include "core/explorer.h"         // FileNode, SidebarView, SidebarRenderCache
#include "core/session.h"          // ClosedBufferSnapshot
#include "git/git_panel_models.h" // jot_git_panel::State
#include "git/git_run.h"          // jot_git::StatusSnapshot
#include <atomic>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// The workspace around the buffers: the sidebar (file explorer + git lists),
// the workspace session that restores a root, the git repository summary the
// status line and Discord read, the lazygit-style git panel, and the
// session-scoped lists (recent files/workspaces, closed buffers, global marks).
//
// Split out of editor_state.h (which is now the umbrella over src/core/);
// the members and their comments moved verbatim.
struct WorkspaceState
{
  bool show_sidebar = false;
  // Activity bar: the icon rail down the left edge that would switch which view
  // the primary sidebar shows. It is off, so the sidebar is a plain file
  // explorer -- no rail, the view pinned to the explorer, and Tab inert. The
  // git panel lives in the secondary sidebar (the right dock) instead.
  bool show_activity_bar = false;
  // True when the *renderer* hid the sidebar because the window was too narrow
  // for it. Growing the window back re-shows it; a sidebar the user closed
  // themselves stays closed.
  bool sidebar_hidden_for_width_ = false;
  // Zen focus mode: sidebar, right dock, tab strip and status line hidden, the
  // pane area across the full width. Sidebar / panel / status-height values are
  // remembered on entry so leaving zen restores the layout it was entered from,
  // and `zen_mode` is persisted so a start lands in the same place.
  bool zen_mode = false;
  bool zen_saved_sidebar_ = true;
  bool zen_saved_panel_ = false;
  int zen_saved_status_height_ = 1;
  SidebarView active_sidebar_view = SIDEBAR_VIEW_EXPLORER;
  // The explorer section: whether it is expanded (the header row toggles it),
  // and the tree's cursor and window, which live in the display space that
  // starts at the pinned root row.
  bool sidebar_explorer_open = true;
  // The row under the pointer (-1 = none): the hover highlight and its display
  // row inside the section (-1 = the header row). Motion sets it; the render
  // paints it and never lets it outrank a cursor.
  int sidebar_hover_index = -2;
  int sidebar_width = 0;
  std::string root_dir;
  bool workspace_session_enabled = false;
  std::string workspace_session_root;
  std::vector<FileNode> file_tree;
  int file_tree_selected = 0;
  int file_tree_scroll = 0;
  int git_sidebar_selected = 0;
  int git_sidebar_scroll = 0;
  bool sidebar_show_hidden = false;
  std::string file_tree_watch_signature_;
  bool file_tree_watch_ready_ = false;
  std::string file_tree_event_watch_root_;
  SidebarRenderCache sidebar_render_cache_;

  // Session lists: the buffers closed this session (restorable), the recent
  // files and workspaces the home screen and :recent show, and the workspace
  // severity map the explorer colours rows from.
  std::vector<ClosedBufferSnapshot> closed_buffer_history;
  std::vector<std::string> recent_files;
  std::vector<std::string> recent_workspaces;
  // Home screen pins: kept at the top of their section and shown even when the
  // project scan or the recent lists do not carry them any more.
  std::vector<std::string> home_pinned;
  std::unordered_map<std::string, int> workspace_diagnostic_severity;
  std::map<char, GlobalMark> global_marks; // global marks ('A'-'Z'), cross-file

  // The repository the current workspace belongs to, refreshed by the git
  // poll: root/branch, the ahead/behind and status counts the status line
  // shows, and the per-file status the explorer decorates with.
  std::string git_root;
  std::string git_branch;
  int git_ahead = 0;  // commits ahead of the upstream branch
  int git_behind = 0; // commits behind the upstream branch
  int git_dirty_count = 0;
  int git_staged_count = 0;
  int git_unstaged_count = 0;
  int git_untracked_count = 0;
  int git_deleted_count = 0;
  int git_renamed_count = 0;
  int git_conflict_count = 0;
  std::atomic<bool> git_refresh_pending_{false};
  std::unordered_map<std::string, std::string> git_file_status;
  // The porcelain output `git_file_status` was parsed from. A poll that digests
  // to the same output skips the parse entirely: the map is the biggest
  // allocation in the process on a repo with tens of thousands of entries (the
  // home directory can be one), and rebuilding it for an unchanged repo was
  // what made memory climb poll after poll.
  jot_git::StatusSnapshot git_status_snapshot;
  // Parses applied to the map, for the headless test that pins the skip.
  long long git_status_parse_count = 0;
  long long git_last_refresh_ms;
  // Git panel (right dock): lazygit-style files / branches / commits / stash
  // views. State and row lists live here so render + input share one model.
  jot_git_panel::State git_panel;
};

#endif
