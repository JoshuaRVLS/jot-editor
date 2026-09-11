#include "editor.h"
#include "host_api.h"
#include "jot/app/relaunch.h"
#include "jot/lua/api.h"
#include "ui/gui/gui.h"
#include <algorithm>
#include <chrono>
#include <filesystem>

namespace
{
  int severity_rank(int severity)
  {
    switch (severity)
    {
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

  int choose_more_severe(int a, int b)
  {
    return severity_rank(a) >= severity_rank(b) ? a : b;
  }

  int compute_diagnostic_max_severity(const std::vector<Diagnostic> &diagnostics)
  {
    int max_severity = 0;
    for (const auto &d : diagnostics)
    {
      max_severity = choose_more_severe(max_severity, d.severity);
    }
    return max_severity;
  }

  std::string normalize_diagnostic_path(const std::string &path)
  {
    if (path.empty())
    {
      return "";
    }
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(path, ec);
    if (ec)
    {
      p = std::filesystem::path(path);
    }
    return p.lexically_normal().string();
  }
} // namespace

void Editor::load_runtime_config()
{
  config.load();
  image_viewer.configure_backend(config.get("image_viewer_backend", "auto"));
#ifdef JOT_TREESITTER
  ts_manager_.set_runtime_options(config.get_list("treesitter_library_paths"),
                                  config.get_list("treesitter_query_paths"),
                                  config.get_list("treesitter_language_overrides"));
#endif
}

void Editor::apply_config_live()
{
  tab_size = std::clamp(config.get_int("tab_size", 2), 1, 16);
  show_indent_guides = config.get_bool("show_indent_guides", true);
  relative_line_numbers = config.get_bool("relative_line_numbers", false);
  highlight_cursor_line = config.get_bool("highlight_cursor_line", true);
  auto_indent = config.get_bool("auto_indent", true);
  smart_paste_indent = config.get_bool("smart_paste_indent", true);
  set_auto_save(config.get_bool("auto_save", false), false);
  set_auto_save_interval(config.get_int("auto_save_interval_ms", 2000), false);
  render_fps = std::clamp(config.get_int("render_fps", 120), 30, 240);
  idle_fps = std::clamp(config.get_int("idle_fps", 60), 5, 240);
  lsp_change_debounce_ms = std::clamp(config.get_int("lsp_change_debounce_ms", 120), 25, 1000);
  integrated_terminal_height = std::clamp(config.get_int("terminal_height", 10), 5, 20);
  debugger_panel_height = std::clamp(config.get_int("debugger_height", 12), 6, 24);
  right_panel_width = std::clamp(config.get_int("right_panel_width", 42), 28, 80);
  image_viewer.configure_backend(config.get("image_viewer_backend", "auto"));
  // The GUI font family is reconciled here so every path that writes the
  // setting takes effect the same way: the settings menu, :font, a Lua
  // jot.config.set, and :reload. Loading the family already in use is a no-op,
  // so this costs nothing when the setting has not moved.
  if (auto *gui = dynamic_cast<UIGui *>(ui))
  {
    const std::string wanted = config.get("gui_font_family", "");
    if (wanted != gui->font_family() && !gui->apply_font_family(wanted))
    {
      // Worth saying out loud: the GUI is usually launched from a desktop
      // icon, where the warning init_freetype writes to stderr is never seen,
      // and the only other clue is a font that did not change.
      set_message("Font family not found: " + wanted);
    }
  }
#ifdef JOT_TREESITTER
  ts_manager_.set_runtime_options(config.get_list("treesitter_library_paths"),
                                  config.get_list("treesitter_query_paths"),
                                  config.get_list("treesitter_language_overrides"));
#endif
  // Keys that are read on every use (prettier_on_save, clang_format_on_save,
  // auto_detect_indent, lsp_completion_*) need no mirroring: they are live
  // automatically. show_minimap / show_sidebar / widths are toggled by their
  // own commands and intentionally not forced here.
  // 24-bit colour: the terminal advertises support in the environment at init,
  // and this setting can force it on or off (useful when the advertisement is
  // wrong in either direction). "auto" re-detects, so :reload re-reads it too.
  {
    const std::string truecolor = config.get("truecolor", "auto");
    if (truecolor == "on")
    {
      terminal.set_truecolor_supported(true);
    }
    else if (truecolor == "off")
    {
      terminal.set_truecolor_supported(false);
    }
    else
    {
      terminal.set_truecolor_supported(terminal_env_supports_truecolor());
    }
  }
  const std::string scheme = config.get("color_scheme", "");
  if (!scheme.empty() && scheme != current_theme_name)
  {
    apply_theme(scheme, false, false);
  }
  // Right-edge margin (see Terminal::render_margin_): 0 paints the full width.
  // A change alters the paintable width, so the layout has to be rebuilt.
  {
    const int margin = std::clamp(config.get_int("render_margin", 0), 0, 4);
    if (margin != terminal.render_margin())
    {
      terminal.set_render_margin(margin);
      update_pane_layout();
    }
  }
  needs_redraw = true;
}

void Editor::reload_config()
{
  config.load();
  if (lua_api)
  {
    lua_api->load_config_file();
  }
  apply_config_live();
  set_message("Config reloaded");
}

void Editor::initialize_state_defaults()
{
  running = true;
  keyboard_press_count = 0;
  pane_root = -1;
  current_pane = 0;
  pane_layout_mode = PANE_LAYOUT_SINGLE;
  show_minimap = false;
  minimap_width = 10; // Fixed width for now
  show_integrated_terminal = false;
  terminal_zoom_active = false;
  terminal_resize_dragging = false;
  terminal_resize_start_y = 0;
  terminal_resize_start_height = 0;
  current_integrated_terminal = -1;
  last_terminal_task_name.clear();
  integrated_terminal_height = std::clamp(config.get_int("terminal_height", 10), 5, 20);
  show_debugger_panel = false;
  debugger_panel_height = std::clamp(config.get_int("debugger_height", 12), 6, 24);
  show_right_panel = false;
  right_panel_width = std::clamp(config.get_int("right_panel_width", 42), 28, 80);
  active_right_panel_tab = RIGHT_PANEL_DEBUG;
  active_plugin_panel.clear();
  plugin_quick_pick_select_callback.clear();
  show_tree_sitter_status_modal = false;
  tree_sitter_status_scroll = 0;
  show_lsp_status_modal = false;
  lsp_status_scroll = 0;
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
  popup.title.clear();
  popup.lines.clear();
  popup.scroll = 0;
  popup.content_lines = 0;
  popup.presentation = POPUP_MODAL;

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
  show_indent_guides = config.get_bool("show_indent_guides", true);
  relative_line_numbers = config.get_bool("relative_line_numbers", false);
  highlight_cursor_line = config.get_bool("highlight_cursor_line", true);
  tab_scroll_index = 0;
  preview_buffer_index = -1;
  last_sidebar_click_ms = 0;
  last_sidebar_click_row = -1;
  last_tab_click_ms = 0;
  last_tab_clicked_index = -1;
  auto_indent = config.get_bool("auto_indent", true);
  smart_paste_indent = config.get_bool("smart_paste_indent", true);
  auto_save_enabled = config.get_bool("auto_save", false);
  auto_save_interval_ms = std::clamp(config.get_int("auto_save_interval_ms", 2000), 250, 60000);
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
  ctrl_hover_active = false;
  ctrl_hover_buffer = -1;
  ctrl_hover_line = -1;
  ctrl_hover_start = -1;
  ctrl_hover_end = -1;
  pane_resize_dragging = false;
  pane_resize_node = -1;
  pane_resize_vertical = false;
  pane_resize_start_pos = 0;
  pane_resize_start_ratio = 0.5f;
  pane_zoom_active = false;
  pane_zoom_pane = -1;
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
  render_fps = std::clamp(config.get_int("render_fps", 120), 30, 240);
  idle_fps = std::clamp(config.get_int("idle_fps", 60), 5, 240);
  lsp_change_debounce_ms = std::clamp(config.get_int("lsp_change_debounce_ms", 120), 25, 1000);
  last_cursor_shape = -1;
  blink_anchor_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
  blink_suspend_until_ms = 0;
  blink_visible = true;
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
  lsp_completion_ghost_text.clear();
  lsp_completion_all_items.clear();
  lsp_completion_items.clear();
  lsp_signature_visible = false;
  lsp_signature_open_paren_line = -1;
  lsp_signature_open_paren_col = 0;
  lsp_signature_filepath.clear();
  lsp_signature_result = {};
  lsp_jump_stack.clear();
  lsp_definition_jump_pending = false;
  lsp_definition_pending_location = {};
  lsp_back_jump_pending = false;
  lsp_back_pending_location = {};

  // Easter egg
  easter_egg_timer = 0;

  // Default embedded-runtime theme name.
  current_theme_name = "dark";
}

void Editor::initialize_lua_runtime()
{
  lua_api = new LuaAPI(this);
  host_api = std::make_unique<EditorHostAPI>(*this);
  lua_api->init();

  // Record the bundled tree-sitter query sources now (cheap file reads) so
  // the first paint compiles against them instead of falling back to a
  // possibly stale runtime query package; the parser dlopen and query
  // compiles still happen on the background worker started here.
  lua_api->flush_deferred_treesitter_queries();

  load_recent_files();
  load_recent_workspaces();

  // Restore saved color scheme now that embedded runtime is ready.
  {
    std::string saved = config.get("color_scheme", "dark");
    apply_theme(saved, false, false);
  }
}

void Editor::initialize_terminal_ui()
{
  terminal.init();
  // Force a fresh terminal size probe after init. terminal.init() reads the
  // size once and may stick to a fallback (e.g. 80x24) if ioctl/$COLUMNS
  // weren't ready when init ran. Re-probing here ensures the first UI frame
  // uses the real terminal dimensions, not a stale value.
  terminal.refresh_size();
  // Right-edge margin (see Terminal::render_margin_): 0 paints the full width.
  // Read here as well as in apply_config_live() so a configured value is in
  // effect for the first frame, before create_pane() sizes the layout.
  terminal.set_render_margin(config.get_int("render_margin", 0));
  terminal.set_poll_timeout_ms(std::max(1, 1000 / std::max(render_fps, idle_fps)));
  ui = new UI(&terminal);
  ui->resize(terminal.get_width(), terminal.get_height());
  ui->set_default_colors(theme.fg_default, theme.bg_default);
  ui->set_cursor_colors(theme.fg_cursor, theme.bg_cursor);

  int h = terminal.get_height();
  int w = ui->get_render_width();
  create_pane(0, 0, w - minimap_width, h - status_height, -1);
}

void Editor::initialize_placeholder_buffer()
{
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

Editor::Editor(bool gui_mode)
{
  this->gui_mode = gui_mode;
  load_runtime_config();
  initialize_state_defaults();
  if (gui_mode)
  {
    initialize_gui_ui();
  }
  else
  {
    initialize_terminal_ui();
  }
  initialize_placeholder_buffer();
  initialize_lua_runtime();
}

void Editor::initialize_gui_ui()
{
  // The GUI frontend sizes its window from the requested cell grid (80x24
  // starter; the first resize event re-fits the real window). No terminal
  // is touched: raw mode, alternate screen and the ANSI diff renderer are
  // all skipped, and UIGui::render() paints the same cell grid with GL.
  ui = new UIGui(80,
                 24,
                 theme.fg_default,
                 theme.bg_default,
                 std::clamp(config.get_int("gui_font_size", 16), 8, 40),
                 config.get("gui_font_family", ""));
  ui->set_default_colors(theme.fg_default, theme.bg_default);
  ui->set_cursor_colors(theme.fg_cursor, theme.bg_cursor);

  // The GUI frontend paints the same cell grid the terminal backend uses,
  // so no extra wiring is needed here: :settings and Ctrl+, open the same
  // cell-based settings menu in both frontends.
  int h = ui->get_height();
  int w = ui->get_render_width();
  create_pane(0, 0, w - minimap_width, h - status_height, -1);
}

EditorHostAPI &Editor::host()
{
  return *host_api;
}

const EditorHostAPI &Editor::host() const
{
  return *host_api;
}

Editor::~Editor()
{
  save_workspace_session();
  save_file_fold_states();
  save_recent_files();
  save_recent_workspaces();
  stop_all_lsp_clients();

  if (lua_api)
    lua_api->fire_autocmd("Shutdown");

  for (auto &term : integrated_terminals)
  {
    if (term)
    {
      unwatch_integrated_terminal_fd(term.get());
      term->close_shell();
    }
  }
  if (lua_api)
  {
    lua_api->cleanup();
    delete lua_api;
  }
  delete ui;
  terminal.cleanup();
}

void Editor::set_message(const std::string &msg, bool toast)
{
  // DEPRECATED: the statusline message channel is kept for compatibility but
  // is no longer the primary surface — toasts (runtime/lua/features/ui/toast.lua)
  // are the message channel now and this function feeds them below. Once the
  // Lua UI kit owns the status line (it registers a status_line handler), stop
  // populating the statusline text so messages don't linger; the toast
  // delivery below carries the message instead.
  if (transient_message_timer != 0)
  {
    event_loop_.cancel_timer(transient_message_timer);
    transient_message_timer = 0;
  }
  ++message_generation;
  if (!(lua_api && lua_api->has_lua_ui_handler("status_line")))
  {
    message = msg;
    needs_redraw = true;
  }
  // Forward to the toast UI (skip the empty clear message). The bridge
  // no-ops safely when no Lua UI kit is attached.
  if (toast && !msg.empty() && lua_api)
  {
    lua_api->emit_toast_event(msg, 0);
  }
}

void Editor::set_transient_message(const std::string &msg, int duration_ms, bool toast)
{
  // DEPRECATED: same channel as set_message — kept for compatibility, toasts
  // are the message surface now.
  if (transient_message_timer != 0)
  {
    event_loop_.cancel_timer(transient_message_timer);
    transient_message_timer = 0;
  }
  const std::uint64_t generation = ++message_generation;
  if (!(lua_api && lua_api->has_lua_ui_handler("status_line")))
  {
    message = msg;
    needs_redraw = true;
  }
  if (toast && !msg.empty() && lua_api)
  {
    lua_api->emit_toast_event(msg, duration_ms);
  }
  if (duration_ms <= 0 || !event_loop_.is_main_thread())
  {
    return;
  }
  transient_message_timer = event_loop_.set_timeout(
      duration_ms,
      [this, generation]
      {
        if (message_generation != generation)
        {
          return;
        }
        transient_message_timer = 0;
        message.clear();
        needs_redraw = true;
      });
}

void Editor::set_home_menu_visible(bool visible)
{
  show_home_menu = visible;
  if (!show_home_menu)
  {
    home_menu_entries.clear();
    home_menu_panel_x = 0;
    home_menu_panel_y = 0;
    home_menu_panel_w = 0;
    home_menu_panel_h = 0;
  }
  else
  {
    home_menu_selected = 0;
    // A live LSP hover float would sit on top of the menu and, because
    // floats with mouse callbacks are hit-tested before the home handler,
    // swallow the pointer events meant for the menu rows.
    cancel_lsp_mouse_hover(/*hide_popup_now=*/true);
  }
  needs_redraw = true;
}

void Editor::set_diagnostics(const std::string &filepath,
                             const std::vector<Diagnostic> &diagnostics)
{
  const std::string normalized_path = normalize_diagnostic_path(filepath);
  if (!normalized_path.empty())
  {
    const int max_severity = compute_diagnostic_max_severity(diagnostics);
    if (max_severity <= 0)
    {
      workspace_diagnostic_severity.erase(normalized_path);
    }
    else
    {
      workspace_diagnostic_severity[normalized_path] = max_severity;
    }
    invalidate_sidebar_diagnostics_cache();
    needs_redraw = true;
  }

  for (auto &buf : buffers)
  {
    bool match = (buf.filepath == filepath);
    if (!match && !buf.filepath.empty() && !filepath.empty())
    {
      std::error_code ec;
      if (fs::exists(buf.filepath, ec) && fs::exists(filepath, ec)
          && fs::equivalent(buf.filepath, filepath, ec))
      {
        match = true;
      }
    }

    if (match)
    {
      buf.diagnostics = diagnostics;
      buf.diag_severity_dirty = true; // per-line gutter index is stale now
      invalidate_sidebar_diagnostics_cache();
      needs_redraw = true;
      // Continue to check other buffers in case of duplicates
    }
  }

  if (lua_api)
    lua_api->fire_autocmd("DiagnosticChanged", filepath, current_buffer);
  if (lua_api)
    lua_api->emit_diagnostics_changed(filepath, diagnostics);
}

void Editor::add_diagnostic(const std::string &filepath, const Diagnostic &diagnostic)
{
  const std::string normalized_path = normalize_diagnostic_path(filepath);
  if (!normalized_path.empty())
  {
    int &entry = workspace_diagnostic_severity[normalized_path];
    entry = choose_more_severe(entry, diagnostic.severity);
    invalidate_sidebar_diagnostics_cache();
    needs_redraw = true;
  }

  for (auto &buf : buffers)
  {
    bool match = (buf.filepath == filepath);
    if (!match && !buf.filepath.empty() && !filepath.empty())
    {
      std::error_code ec;
      if (fs::exists(buf.filepath, ec) && fs::exists(filepath, ec)
          && fs::equivalent(buf.filepath, filepath, ec))
      {
        match = true;
      }
    }

    if (match)
    {
      buf.diagnostics.push_back(diagnostic);
      buf.diag_severity_dirty = true; // per-line gutter index is stale now
      invalidate_sidebar_diagnostics_cache();
      needs_redraw = true;
      // Continue search
    }
  }
}

bool Editor::restart_editor(bool force)
{
  // The new process boots from disk: any unsaved edits would be lost, so the
  // restart waits until the user saved (or forces past the check).
  for (const auto &b : buffers)
  {
    if (b.modified && !force)
    {
      set_message("Restart skipped: unsaved changes. Save buffers and try again.",
                  false);
      return false;
    }
  }
  if (!relaunch::restart_self())
  {
    set_message("Could not restart jot (failed to relaunch the executable).", false);
    return false;
  }
  // POSIX replaced this process in place and never returned here; on Windows a
  // new instance is running, so stop the event loop and exit cleanly.
  running = false;
  return true;
}
