#include "editor.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace {
enum HomeAction {
  HOME_ACTION_OPEN_RECENT = 1,
  HOME_ACTION_OPEN_RECENT_WORKSPACE = 2,
  HOME_ACTION_NEW_FILE = 3,
  HOME_ACTION_OPEN_RECENT_PROMPT = 4,
  HOME_ACTION_COMMAND_PALETTE = 5,
  HOME_ACTION_THEME_CHOOSER = 6,
  HOME_ACTION_TOGGLE_SIDEBAR = 7,
  HOME_ACTION_CONTINUE_EDITOR = 8,
  HOME_ACTION_QUIT = 9
};

struct HomeMenuRenderItem {
  int action = 0;
  int recent_index = -1;
  int recent_workspace_index = -1;
  std::string label;
  std::string secondary;
};

std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string icon_for_path(const std::string &path) {
  static const std::unordered_map<std::string, std::string> ext_icons = {
      {".cpp", " "}, {".cc", " "},   {".cxx", " "}, {".c", " "},
      {".h", " "},   {".hpp", " "},  {".py", " "},  {".js", " "},
      {".ts", " "},  {".jsx", " "},  {".tsx", " "}, {".json", " "},
      {".md", " "},  {".toml", " "}, {".yaml", " "}, {".yml", " "},
      {".html", " "}, {".css", " "},  {".scss", " "}, {".sh", " "},
      {".go", " "},  {".rs", " "},   {".java", " "}, {".php", " "},
      {".rb", " "},  {".xml", "󰗀 "},  {".txt", "󰈙 "}, {".lock", "󰌾 "}};

  std::string name = lower_copy(path);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    std::string ext = name.substr(dot);
    auto it = ext_icons.find(ext);
    if (it != ext_icons.end()) {
      return it->second;
    }
  }
  return "󰈔 ";
}

std::string shorten_tail(const std::string &text, int max_len) {
  if (max_len <= 0) {
    return "";
  }
  if ((int)text.size() <= max_len) {
    return text;
  }
  if (max_len <= 3) {
    return text.substr(0, max_len);
  }
  return "..." + text.substr(text.size() - (size_t)(max_len - 3));
}
} // namespace

void Editor::render_home_menu() {
  if (!show_home_menu) {
    return;
  }

  const int screen_w = ui->get_width();
  const int screen_h = ui->get_height();
  const int usable_h = std::max(1, screen_h - status_height);

  UIRect full = {0, 0, screen_w, usable_h};
  ui->fill_rect(full, " ", theme.fg_default, theme.bg_default);

  const int panel_w = std::max(48, std::min(screen_w - 4, 96));
  const int panel_h = std::max(18, std::min(usable_h - 2, 28));
  const int panel_x = std::max(1, (screen_w - panel_w) / 2);
  const int panel_y = std::max(0, (usable_h - panel_h) / 2);

  home_menu_panel_x = panel_x;
  home_menu_panel_y = panel_y;
  home_menu_panel_w = panel_w;
  home_menu_panel_h = panel_h;

  UIRect panel = {panel_x, panel_y, panel_w, panel_h};
  ui->fill_rect(panel, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_command);

  std::vector<std::string> banner = {
      "      ██╗ ██████╗ ████████╗",
      "      ██║██╔═══██╗╚══██╔══╝",
      "      ██║██║   ██║   ██║   ",
      " ██   ██║██║   ██║   ██║   ",
      " ╚█████╔╝╚██████╔╝   ██║   ",
      "  ╚════╝  ╚═════╝    ╚═╝   "};

  std::vector<int> banner_colors = {theme.fg_keyword, theme.fg_function,
                                    theme.fg_type,    theme.fg_string,
                                    theme.fg_number,  theme.fg_keyword};

  int banner_y = panel_y + 1;
  for (int i = 0; i < (int)banner.size(); i++) {
    int bx = panel_x + std::max(1, (panel_w - (int)banner[i].size()) / 2);
    ui->draw_text(bx, banner_y + i, banner[i], banner_colors[i % banner_colors.size()],
                  theme.bg_command, true);
  }

  std::string title = "󰚩  JOT Home";
  int title_x = panel_x + std::max(1, (panel_w - (int)title.size()) / 2);
  ui->draw_text(title_x, banner_y + (int)banner.size(), title, theme.fg_status,
                theme.bg_command, true);

  int row_x = panel_x + 2;
  int row_w = panel_w - 4;
  int row_y = banner_y + (int)banner.size() + 2;
  int row_limit = panel_y + panel_h - 2;

  ui->draw_text(row_x, row_y, "󰈔  Recent Files", theme.fg_sidebar_directory,
                theme.bg_command, true);
  row_y++;

  std::vector<HomeMenuRenderItem> items;
  int recent_show = std::min(8, (int)recent_files.size());
  for (int i = 0; i < recent_show; i++) {
    HomeMenuRenderItem item;
    item.action = HOME_ACTION_OPEN_RECENT;
    item.recent_index = i;
    item.label = icon_for_path(recent_files[i]) + " " + std::to_string(i + 1) +
                 ". " + get_filename(recent_files[i]);
    item.secondary = recent_files[i];
    items.push_back(std::move(item));
  }

  if (row_y < row_limit) {
    row_y++;
  }
  if (row_y < row_limit) {
    ui->draw_text(row_x, row_y, "  Recent Workspaces",
                  theme.fg_sidebar_directory, theme.bg_command, true);
    row_y++;
  }

  int workspace_show = std::min(6, (int)recent_workspaces.size());
  for (int i = 0; i < workspace_show; i++) {
    HomeMenuRenderItem item;
    item.action = HOME_ACTION_OPEN_RECENT_WORKSPACE;
    item.recent_workspace_index = i;
    item.label = "  " + std::to_string(i + 1) + ". " +
                 get_filename(recent_workspaces[i]);
    item.secondary = recent_workspaces[i];
    items.push_back(std::move(item));
  }

  items.push_back({HOME_ACTION_NEW_FILE, -1, -1, "  New File", ""});
  items.push_back(
      {HOME_ACTION_OPEN_RECENT_PROMPT, -1, -1, "󰱼  Open Recent Prompt", ""});
  items.push_back(
      {HOME_ACTION_COMMAND_PALETTE, -1, -1, "  Command Palette", ""});
  items.push_back(
      {HOME_ACTION_THEME_CHOOSER, -1, -1, "󰔎  Theme Chooser", ""});
  items.push_back(
      {HOME_ACTION_TOGGLE_SIDEBAR, -1, -1, "  Toggle Sidebar", ""});
  items.push_back(
      {HOME_ACTION_CONTINUE_EDITOR, -1, -1, "󰋖  Continue to Editor", ""});
  items.push_back({HOME_ACTION_QUIT, -1, -1, "󰩈  Quit JOT", ""});

  if (items.empty()) {
    home_menu_selected = 0;
  } else {
    home_menu_selected = std::clamp(home_menu_selected, 0, (int)items.size() - 1);
  }

  home_menu_entries.clear();

  if (recent_show == 0) {
    ui->draw_text(row_x + 1, row_y, "󰞋  No recent files yet", theme.fg_comment,
                  theme.bg_command);
    row_y++;
  }

  for (int i = 0; i < (int)items.size() && row_y < row_limit; i++) {
    bool selected = (i == home_menu_selected);
    int fg = selected ? theme.fg_selection : theme.fg_default;
    int bg = selected ? theme.bg_selection : theme.bg_command;

    UIRect row_rect = {row_x, row_y, row_w, 1};
    ui->fill_rect(row_rect, " ", fg, bg);
    std::string label = items[i].label;
    if ((int)label.size() > row_w - 2) {
      label = label.substr(0, std::max(0, row_w - 5)) + "...";
    }
    ui->draw_text(row_x + 1, row_y, label, fg, bg, selected);

    if (!items[i].secondary.empty() && row_w > 24) {
      std::string secondary = shorten_tail(items[i].secondary, row_w / 2);
      int sx = row_x + row_w - (int)secondary.size() - 1;
      if (sx > row_x + (int)label.size() + 2) {
        int path_fg = selected ? theme.fg_selection : theme.fg_comment;
        ui->draw_text(sx, row_y, secondary, path_fg, bg);
      }
    }

    home_menu_entries.push_back({items[i].action, items[i].recent_index,
                                 items[i].recent_workspace_index, row_x, row_y,
                                 row_w});
    row_y++;
  }

  std::string hint =
      "↑/↓ navigate  •  Enter open  •  1-9 quick recent  •  Esc hide home";
  if ((int)hint.size() > panel_w - 4) {
    hint = "Enter open  •  1-9 quick recent  •  Esc hide";
  }
  ui->draw_text(panel_x + 2, panel_y + panel_h - 2, hint, theme.fg_comment,
                theme.bg_command);
}

bool Editor::handle_home_menu_input(int ch, bool is_ctrl, bool is_shift,
                                    bool is_alt) {
  (void)is_shift;
  (void)is_alt;
  if (!show_home_menu) {
    return false;
  }

  auto open_recent_by_index = [&](int idx) -> bool {
    if (idx < 0 || idx >= (int)recent_files.size()) {
      return false;
    }
    show_home_menu = false;
    open_recent_file(std::to_string(idx + 1));
    needs_redraw = true;
    return true;
  };

  auto open_workspace_by_index = [&](int idx) -> bool {
    if (idx < 0 || idx >= (int)recent_workspaces.size()) {
      return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(recent_workspaces[idx], ec) || ec ||
        !std::filesystem::is_directory(recent_workspaces[idx], ec)) {
      recent_workspaces.erase(recent_workspaces.begin() + idx);
      set_message("Workspace not found");
      needs_redraw = true;
      return true;
    }
    show_home_menu = false;
    open_workspace(recent_workspaces[idx], true);
    needs_redraw = true;
    return true;
  };

  auto is_startup_pristine = [&]() {
    if (buffers.size() != 1) {
      return false;
    }
    const auto &buf = buffers[0];
    return buf.filepath.empty() && !buf.modified && buf.lines.size() == 1 &&
           buf.lines[0].empty();
  };

  auto execute_entry = [&](const HomeMenuEntry &entry) -> bool {
    switch (entry.action) {
    case HOME_ACTION_OPEN_RECENT:
      return open_recent_by_index(entry.recent_index);
    case HOME_ACTION_OPEN_RECENT_WORKSPACE:
      return open_workspace_by_index(entry.recent_workspace_index);
    case HOME_ACTION_NEW_FILE:
      show_home_menu = false;
      if (!is_startup_pristine()) {
        create_new_buffer();
      }
      set_message("Ready: new file");
      needs_redraw = true;
      return true;
    case HOME_ACTION_OPEN_RECENT_PROMPT:
      show_home_menu = false;
      show_command_palette = true;
      command_palette_query = "openrecent ";
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      needs_redraw = true;
      return true;
    case HOME_ACTION_COMMAND_PALETTE:
      show_home_menu = false;
      toggle_command_palette();
      needs_redraw = true;
      return true;
    case HOME_ACTION_THEME_CHOOSER:
      show_home_menu = false;
      open_theme_chooser();
      needs_redraw = true;
      return true;
    case HOME_ACTION_TOGGLE_SIDEBAR:
      show_home_menu = false;
      toggle_sidebar();
      needs_redraw = true;
      return true;
    case HOME_ACTION_CONTINUE_EDITOR:
      show_home_menu = false;
      set_message("Home hidden");
      needs_redraw = true;
      return true;
    case HOME_ACTION_QUIT:
      running = false;
      return true;
    default:
      return false;
    }
  };

  if (is_ctrl && (ch == 'q' || ch == 'Q')) {
    running = false;
    return true;
  }

  if (ch == 27) {
    show_home_menu = false;
    set_message("Home hidden");
    needs_redraw = true;
    return true;
  }

  if (ch >= '1' && ch <= '9') {
    int idx = ch - '1';
    if (open_recent_by_index(idx) || open_workspace_by_index(idx)) {
      return true;
    }
  }

  const int count = (int)home_menu_entries.size();
  if (count > 0) {
    if (ch == 1008 || ch == 'k' || ch == 'K') {
      home_menu_selected = (home_menu_selected - 1 + count) % count;
      needs_redraw = true;
      return true;
    }
    if (ch == 1009 || ch == 'j' || ch == 'J') {
      home_menu_selected = (home_menu_selected + 1) % count;
      needs_redraw = true;
      return true;
    }
    if (ch == 1012) {
      home_menu_selected = 0;
      needs_redraw = true;
      return true;
    }
    if (ch == 1013) {
      home_menu_selected = count - 1;
      needs_redraw = true;
      return true;
    }
    if (ch == '\n' || ch == 13) {
      return execute_entry(home_menu_entries[home_menu_selected]);
    }
  }

  if (ch == 'n' || ch == 'N') {
    HomeMenuEntry e = {HOME_ACTION_NEW_FILE, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 'p' || ch == 'P') {
    HomeMenuEntry e = {HOME_ACTION_COMMAND_PALETTE, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 't' || ch == 'T') {
    HomeMenuEntry e = {HOME_ACTION_THEME_CHOOSER, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 's' || ch == 'S') {
    HomeMenuEntry e = {HOME_ACTION_TOGGLE_SIDEBAR, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 'r' || ch == 'R') {
    HomeMenuEntry e = {HOME_ACTION_OPEN_RECENT_PROMPT, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 'w' || ch == 'W') {
    if (open_workspace_by_index(0)) {
      return true;
    }
  }
  if (ch == 'e' || ch == 'E') {
    HomeMenuEntry e = {HOME_ACTION_CONTINUE_EDITOR, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 'q' || ch == 'Q') {
    HomeMenuEntry e = {HOME_ACTION_QUIT, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }

  if (ch >= 32 && ch < 127) {
    // Any other printable key dismisses home and continues normal editing.
    show_home_menu = false;
    needs_redraw = true;
    return false;
  }

  return true;
}

bool Editor::handle_home_menu_mouse(int x, int y, bool is_click) {
  if (!show_home_menu || !is_click) {
    return false;
  }

  for (int i = 0; i < (int)home_menu_entries.size(); i++) {
    const auto &entry = home_menu_entries[i];
    if (y == entry.y && x >= entry.x && x < entry.x + entry.w) {
      home_menu_selected = i;
      needs_redraw = true;
      return handle_home_menu_input('\n', false, false, false);
    }
  }

  bool inside_panel = x >= home_menu_panel_x &&
                      x < home_menu_panel_x + home_menu_panel_w &&
                      y >= home_menu_panel_y &&
                      y < home_menu_panel_y + home_menu_panel_h;
  if (inside_panel) {
    return true;
  }

  show_home_menu = false;
  needs_redraw = true;
  return false;
}
