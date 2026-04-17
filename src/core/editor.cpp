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
  tab_size = config.get_int("tab_size", 4);
  auto_indent = config.get_bool("auto_indent", true);
  needs_redraw = true;
  mouse_selecting = false;
  last_left_click_ms = 0;
  last_left_click_pos = {-1, -1};
  idle_frame_count = 0;
  cursor_blink_frame = 0;
  cursor_visible = true;
  render_fps = std::clamp(config.get_int("render_fps", 120), 30, 240);
  idle_fps = std::clamp(config.get_int("idle_fps", 60), 5, 240);
  last_cursor_shape = -1;
  show_context_menu = false;
  context_menu_x = 0;
  context_menu_y = 0;
  context_menu_selected = 0;
  input_prompt_visible = false;

  // Modeless editor behavior: keep an always-insert internal mode.
  mode = MODE_INSERT;
  visual_start = {0, 0};
  visual_line_mode = false;
  has_pending_key = false;
  pending_key = 0;

  // Easter egg
  easter_egg_timer = 0;

  // Default theme
  current_theme_name = "Dark";

  // Restore saved color scheme
  {
    std::string saved = config.get("color_scheme", "Dark");
    apply_theme(saved);
  }

  python_api = new PythonAPI(this);
  python_api->init();

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
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  buffers.push_back(fb);
  panes[0].buffer_id = 0;
}

Editor::~Editor() {
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
