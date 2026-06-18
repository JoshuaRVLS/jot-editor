#include "editor.h"
#include "host_api.h"
#include "python_bridge/api.h"
#include <algorithm>
#include <filesystem>

namespace {
int severity_rank(int severity) {
  switch (severity) {
  case 1:
    return 4; // error
  case 2:
    return 3; // warning
  case 3:
    return 2; // info
  case 4:
    return 1; // hint
  default:
    return 0;
  }
}

int choose_more_severe(int a, int b) {
  return severity_rank(a) >= severity_rank(b) ? a : b;
}

int compute_diagnostic_max_severity(
    const std::vector<Diagnostic> &diagnostics) {
  int max_severity = 0;
  for (const auto &d : diagnostics) {
    max_severity = choose_more_severe(max_severity, d.severity);
  }
  return max_severity;
}

std::string normalize_diagnostic_path(const std::string &path) {
  if (path.empty()) {
    return "";
  }
  std::error_code ec;
  std::filesystem::path p = std::filesystem::absolute(path, ec);
  if (ec) {
    p = std::filesystem::path(path);
  }
  return p.lexically_normal().string();
}
} // namespace

Editor::Editor() {
  config.load();
  image_viewer.configure_backend(config.get("image_viewer_backend", "auto"));
#ifdef JOT_TREESITTER
  ts_manager_.set_runtime_options(
      config.get_list("treesitter_library_paths"),
      config.get_list("treesitter_query_paths"),
      config.get_list("treesitter_language_overrides"));
#endif

  running = true;
  keyboard_press_count = 0;
  pane_root = -1;
  current_pane = 0;
  pane_layout_mode = PANE_LAYOUT_SINGLE;
  show_minimap = false;
  minimap_width = 10; // Fixed width for now
  show_integrated_terminal = false;
  current_integrated_terminal = -1;
  last_terminal_task_name.clear();
  integrated_terminal_height =
      std::clamp(config.get_int("terminal_height", 10), 5, 20);
  show_debugger_panel = false;
  debugger_panel_height =
      std::clamp(config.get_int("debugger_height", 12), 6, 24);
  show_right_panel = false;
  right_panel_width = std::clamp(config.get_int("right_panel_width", 42), 28, 80);
  active_right_panel_tab = RIGHT_PANEL_DEBUG;
  show_tree_sitter_status_modal = false;
  tree_sitter_status_scroll = 0;
  tree_sitter_install_jobs.clear();
  current_debugger_session = -1;
  debugger_breakpoint_hover_visible = false;
  debugger_breakpoint_hover_pane = -1;
  debugger_breakpoint_hover_buffer = -1;
  debugger_breakpoint_hover_line = -1;
  show_search = false;
  quick_pick_kind = QUICK_PICK_NONE;
  show_quick_pick = false;
  quick_pick_title.clear();
  quick_pick_query.clear();
  quick_pick_all_items.clear();
  quick_pick_items.clear();
  quick_pick_selected = 0;
  show_command_palette = false;
  command_palette_selected = 0;
  command_palette_theme_mode = false;
  show_menu_bar_dropdown = false;
  menu_bar_active = -1;
  menu_bar_selected = 0;
  menu_bar_segments.clear();
  search_result_index = -1;
  search_case_sensitive = false;
  search_whole_word = false;
  search_regex = false;
  search_replace_visible = false;
  search_focus_replace = false;
  search_scoped_to_selection = false;
  search_scope_start = {0, 0};
  search_scope_end = {0, 0};
  show_save_prompt = false;
  show_quit_prompt = false;

  popup.visible = false;
  popup.x = 0;
  popup.y = 0;
  popup.w = 0;
  popup.h = 0;
  popup.text = "";

  show_sidebar = false;
  active_sidebar_view = SIDEBAR_VIEW_EXPLORER;
  sidebar_width = 30;
  root_dir = ".";
  workspace_session_enabled = false;
  workspace_session_root.clear();
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
  git_last_refresh_ms = 0;
  file_tree_selected = 0;
  file_tree_scroll = 0;
  git_sidebar_selected = 0;
  git_sidebar_scroll = 0;
  sidebar_show_hidden = false;
  file_tree_watch_signature_.clear();
  file_tree_watch_ready_ = false;
  focus_state = FOCUS_EDITOR;

  status_height = 2;
  tab_height = 1;
  tab_size = config.get_int("tab_size", 2);
  show_indent_guides = config.get_bool("show_indent_guides", false);
  relative_line_numbers = config.get_bool("relative_line_numbers", false);
  tab_scroll_index = 0;
  preview_buffer_index = -1;
  last_sidebar_click_ms = 0;
  last_sidebar_click_row = -1;
  last_tab_click_ms = 0;
  last_tab_clicked_index = -1;
  auto_indent = config.get_bool("auto_indent", true);
  smart_paste_indent = config.get_bool("smart_paste_indent", true);
  auto_save_enabled = config.get_bool("auto_save", false);
  auto_save_interval_ms =
      std::clamp(config.get_int("auto_save_interval_ms", 2000), 250, 60000);
  last_auto_save_ms = 0;
  show_home_menu = true;
  home_menu_selected = 0;
  home_menu_panel_x = 0;
  home_menu_panel_y = 0;
  home_menu_panel_w = 0;
  home_menu_panel_h = 0;
  home_menu_entries.clear();
  needs_redraw = true;
  mouse_selecting = false;
  mouse_selection_mode = MOUSE_SELECT_CHAR;
  mouse_anchor_end = {0, 0};
  mouse_press_screen_x = -1;
  mouse_press_screen_y = -1;
  mouse_press_buf_x = -1;
  mouse_press_buf_y = -1;
  mouse_drag_started = false;
  lsp_mouse_hover_enabled = false;
  lsp_mouse_hover_pending = false;
  lsp_mouse_hover_visible = false;
  lsp_mouse_hover_deadline_ms = 0;
  lsp_mouse_hover_pane = -1;
  lsp_mouse_hover_buffer = -1;
  lsp_mouse_hover_line = -1;
  lsp_mouse_hover_col = -1;
  lsp_mouse_hover_token_start = -1;
  lsp_mouse_hover_token_end = -1;
  lsp_mouse_hover_screen_x = -1;
  lsp_mouse_hover_screen_y = -1;
  lsp_mouse_hover_filepath.clear();
  pane_resize_dragging = false;
  pane_resize_node = -1;
  pane_resize_vertical = false;
  pane_resize_start_pos = 0;
  pane_resize_start_ratio = 0.5f;
  sidebar_resize_dragging = false;
  sidebar_resize_opening = false;
  sidebar_resize_start_x = 0;
  sidebar_resize_start_width = sidebar_width;
  right_panel_resize_dragging = false;
  right_panel_resize_start_x = 0;
  right_panel_resize_start_width = right_panel_width;
  scrollbar_dragging = false;
  scrollbar_drag_pane = -1;
  scrollbar_drag_start_y = 0;
  scrollbar_drag_start_scroll = 0;
  scrollbar_drag_track_y = 0;
  scrollbar_drag_track_h = 0;
  scrollbar_drag_thumb_h = 0;
  scrollbar_drag_max_scroll = 0;
  last_left_click_ms = 0;
  last_left_click_pos = {-1, -1};
  last_left_click_count = 0;
  idle_frame_count = 0;
  cursor_blink_frame = 0;
  cursor_visible = true;
  render_fps = std::clamp(config.get_int("render_fps", 120), 30, 240);
  idle_fps = std::clamp(config.get_int("idle_fps", 60), 5, 240);
  lsp_change_debounce_ms =
      std::clamp(config.get_int("lsp_change_debounce_ms", 120), 25, 1000);
  last_cursor_shape = -1;
  show_context_menu = false;
  context_menu_surface = CONTEXT_MENU_NONE;
  context_menu_items.clear();
  context_menu_x = 0;
  context_menu_y = 0;
  context_menu_w = 0;
  context_menu_h = 0;
  context_menu_selected = 0;
  context_menu_target_buffer = -1;
  context_menu_target_pane = -1;
  context_menu_target_terminal = -1;
  context_menu_target_line = -1;
  context_menu_target_path.clear();
  context_menu_target_is_dir = false;
  lsp_completion_visible = false;
  lsp_completion_manual_request = false;
  lsp_completion_selected = 0;
  lsp_completion_anchor = {0, 0};
  lsp_completion_replace_start = {0, 0};
  lsp_completion_filepath.clear();
  lsp_completion_prefix.clear();
  lsp_completion_all_items.clear();
  lsp_completion_items.clear();
  lsp_jump_stack.clear();
  lsp_definition_jump_pending = false;
  lsp_definition_pending_location = {};
  lsp_back_jump_pending = false;
  lsp_back_pending_location = {};

  // Easter egg
  easter_egg_timer = 0;

  // Default Python-backed theme name.
  current_theme_name = "dark";

  python_api = new PythonAPI(this);
  python_api->init();
  host_api = std::make_unique<EditorHostAPI>(*this);

  load_recent_files();
  load_recent_workspaces();

  // Restore saved color scheme now that Python runtime is ready.
  {
    std::string saved = config.get("color_scheme", "dark");
    apply_theme(saved, false, false);
  }

  terminal.init();
  // Force a fresh terminal size probe after init. terminal.init() reads the
  // size once and may stick to a fallback (e.g. 80x24) if ioctl/$COLUMNS
  // weren't ready when init ran. Re-probing here ensures the first UI frame
  // uses the real terminal dimensions, not a stale value.
  terminal.refresh_size();
  terminal.set_poll_timeout_ms(
      std::max(1, 1000 / std::max(render_fps, idle_fps)));
  ui = new UI(&terminal);
  ui->resize(terminal.get_width(), terminal.get_height());
  ui->set_default_colors(theme.fg_default, theme.bg_default);

  int h = terminal.get_height();
  int w = ui->get_render_width();
  create_pane(0, 0, w - minimap_width, h - status_height, -1);

  current_buffer = 0;

  FileBuffer fb;
  fb.lines.push_back("");
  fb.cursor = {0, 0};
  fb.preferred_x = 0;
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  fb.is_placeholder = true;
  buffers.push_back(std::move(fb));
  panes[0].buffer_id = 0;
}

EditorHostAPI &Editor::host() { return *host_api; }

const EditorHostAPI &Editor::host() const { return *host_api; }

Editor::~Editor() {
  save_workspace_session();
  save_file_fold_states();
  save_recent_files();
  save_recent_workspaces();
  stop_all_lsp_clients();

  for (auto &term : integrated_terminals) {
    if (term) {
      unwatch_integrated_terminal_fd(term.get());
      term->close_shell();
    }
  }
  if (python_api) {
    python_api->cleanup();
    delete python_api;
  }
  delete ui;
  terminal.cleanup();
}

void Editor::set_message(const std::string &msg) {
  message = msg;
  needs_redraw = true;
}

void Editor::set_home_menu_visible(bool visible) {
  show_home_menu = visible;
  if (!show_home_menu) {
    home_menu_entries.clear();
    home_menu_panel_x = 0;
    home_menu_panel_y = 0;
    home_menu_panel_w = 0;
    home_menu_panel_h = 0;
  } else {
    home_menu_selected = 0;
  }
  needs_redraw = true;
}

void Editor::set_diagnostics(const std::string &filepath,
                             const std::vector<Diagnostic> &diagnostics) {
  const std::string normalized_path = normalize_diagnostic_path(filepath);
  if (!normalized_path.empty()) {
    const int max_severity = compute_diagnostic_max_severity(diagnostics);
    if (max_severity <= 0) {
      workspace_diagnostic_severity.erase(normalized_path);
    } else {
      workspace_diagnostic_severity[normalized_path] = max_severity;
    }
    invalidate_sidebar_diagnostics_cache();
    needs_redraw = true;
  }

  for (auto &buf : buffers) {
    bool match = (buf.filepath == filepath);
    if (!match && !buf.filepath.empty() && !filepath.empty()) {
      std::error_code ec;
      if (fs::exists(buf.filepath, ec) && fs::exists(filepath, ec) &&
          fs::equivalent(buf.filepath, filepath, ec)) {
        match = true;
      }
    }

    if (match) {
      buf.diagnostics = diagnostics;
      invalidate_sidebar_diagnostics_cache();
      needs_redraw = true;
      // Continue to check other buffers in case of duplicates
    }
  }
}

void Editor::add_diagnostic(const std::string &filepath,
                            const Diagnostic &diagnostic) {
  const std::string normalized_path = normalize_diagnostic_path(filepath);
  if (!normalized_path.empty()) {
    int &entry = workspace_diagnostic_severity[normalized_path];
    entry = choose_more_severe(entry, diagnostic.severity);
    invalidate_sidebar_diagnostics_cache();
    needs_redraw = true;
  }

  for (auto &buf : buffers) {
    bool match = (buf.filepath == filepath);
    if (!match && !buf.filepath.empty() && !filepath.empty()) {
      std::error_code ec;
      if (fs::exists(buf.filepath, ec) && fs::exists(filepath, ec) &&
          fs::equivalent(buf.filepath, filepath, ec)) {
        match = true;
      }
    }

    if (match) {
      buf.diagnostics.push_back(diagnostic);
      invalidate_sidebar_diagnostics_cache();
      needs_redraw = true;
      // Continue search
    }
  }
}

void Editor::poll_discord_rpc(long long now_ms) {
  if (!config.get_bool("discord_rpc", true)) {
    if (discord_rpc.is_connected()) {
      discord_rpc.disconnect();
    } 
    return;
  }

  discord_rpc.poll(now_ms);

  std::string project = "No Workspace";
  if (!root_dir.empty() && root_dir != ".") {
    project = fs::path(root_dir).filename().string();
  }
  
  std::string details = "Working on " + project;
  
  if (has_git_repo() && !git_branch.empty()) {
    details += " . " + git_branch;
    if (git_dirty_count > 0) {
      details += " (" + std::to_string(git_dirty_count) + " changes)";
    }
  }
  
  std::string state = "Browsing workspace";
  
  if (!buffers.empty() && current_buffer >= 0 && !buffers[current_buffer].filepath.empty()) {
    
    const std::string path = buffers[current_buffer].filepath;
    const std::string filename = fs::path(path).filename().string();
    std::string ext = get_file_extension(path);
    if (!ext.empty() && ext[0] == '.') {
      ext.erase(0, 1);
    }
    
    if (!ext.empty()) {
      state = "Editing " + filename + " . " + ext;
    } else {
      state = "Editing " + filename;
    }
  }

  if (details != discord_rpc_last_details || state != discord_rpc_last_state) {
    discord_rpc_last_details = details;
    discord_rpc_last_state = state;
    discord_rpc.update_presence(details, state);
  }
}
