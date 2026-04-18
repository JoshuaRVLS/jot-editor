#include "editor.h"
#include "python_api.h"
#include <algorithm>

Editor::Editor() {
  config.load();
 

  running = true;
  current_pane = 0;
  waiting_for_space_f = false;
  pane_layout_mode = PANE_LAYOUT_SINGLE;
  show_minimap = false;
  minimap_width = 10; // Fixed width for now
  show_integrated_terminal = false;
  current_integrated_terminal = -1;
  integrated_terminal_height = std::clamp(config.get_int("terminal_height", 10), 5, 20);
  show_search = false;
  show_command_palette = false;
  command_palette_selected = 0;
  command_palette_theme_mode = false;
  search_result_index = -1;
  search_case_sensitive = false;
  show_save_prompt = false;
  show_quit_prompt = false;

  popup.visible = false;
  popup.x = 0;
  popup.y = 0;
  popup.w = 0;
  popup.h = 0;
  popup.text = "";

  show_sidebar = false;
  sidebar_width = 30;
  root_dir = ".";
  file_tree_selected = 0;
  file_tree_scroll = 0;
  sidebar_show_hidden = false;
  focus_state = FOCUS_EDITOR;

  status_height = 2;
  tab_height = 1;
  tab_size = config.get_int("tab_size", 2);
  auto_indent = config.get_bool("auto_indent", true);
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
  context_menu_x = 0;
  context_menu_y = 0;
  context_menu_selected = 0;
  lsp_completion_visible = false;
  lsp_completion_manual_request = false;
  lsp_completion_selected = 0;
  lsp_completion_anchor = {0, 0};
  lsp_completion_filepath.clear();
  lsp_completion_items.clear();
  input_prompt_visible = false;

  // Modeless editor behavior: keep an always-insert internal mode.
  mode = MODE_INSERT;
  visual_start = {0, 0};
  visual_line_mode = false;
  has_pending_key = false;
  pending_key = 0;

  // Easter egg
  easter_egg_timer = 0;

  // Default Python-backed theme name.
  current_theme_name = "jot_nvim";

  python_api = new PythonAPI(this);
  python_api->init();

  load_recent_files();

  // Restore saved color scheme now that Python runtime is ready.
  {
    std::string saved = config.get("color_scheme", "jot_nvim");
    apply_theme(saved, false, false);
  }

  terminal.init();
  terminal.set_poll_timeout_ms(std::max(1, 1000 / std::max(render_fps, idle_fps)));
  ui = new UI(&terminal);
  ui->resize(terminal.get_width(), terminal.get_height());

  int h = terminal.get_height();
  int w = terminal.get_width();
  create_pane(0, tab_height, w - minimap_width, h - tab_height - status_height,
              -1);

  FileBuffer fb;
  fb.lines.push_back("");
  fb.cursor = {0, 0};
  fb.preferred_x = 0;
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  buffers.push_back(fb);
  panes[0].buffer_id = 0;
}

Editor::~Editor() {
  save_recent_files();
  stop_all_lsp_clients();

  for (auto &term : integrated_terminals) {
    if (term) {
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
      needs_redraw = true;
      // Continue to check other buffers in case of duplicates
    }
  }
}

void Editor::add_diagnostic(const std::string &filepath,
                            const Diagnostic &diagnostic) {
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
      needs_redraw = true;
      // Continue search
    }
  }
}
