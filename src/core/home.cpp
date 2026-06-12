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
  HOME_ACTION_OPEN_PATH_PROMPT = 7,
  HOME_ACTION_RESUME = 8,
  HOME_ACTION_CONTINUE_EDITOR = 9,
  HOME_ACTION_QUIT = 10
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

std::string ellipsize_right(const std::string &text, int max_len) {
  if (max_len <= 0) {
    return "";
  }
  if ((int)text.size() <= max_len) {
    return text;
  }
  if (max_len <= 3) {
    return text.substr(0, (size_t)max_len);
  }
  return text.substr(0, (size_t)(max_len - 3)) + "...";
}

std::string workspace_icon() { return " "; }

std::string display_parent(const std::string &path) {
  std::error_code ec;
  std::filesystem::path p(path);
  std::filesystem::path parent = p.parent_path();
  if (parent.empty()) {
    return path;
  }
  std::filesystem::path normalized = std::filesystem::weakly_canonical(parent, ec);
  return ec ? parent.string() : normalized.string();
}
} // namespace

void Editor::render_home_menu() {
  if (!show_home_menu) {
    return;
  }

  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();
  const int usable_h = std::max(1, screen_h - status_height);

  UIRect full = {0, 0, screen_w, usable_h};
  ui->fill_rect(full, " ", theme.fg_default, theme.bg_default);

  const int content_w = std::max(1, std::min(screen_w - 4, 118));
  const int content_x = std::max(1, (screen_w - content_w) / 2);
  const int content_y = std::max(0, std::min(2, usable_h - 1));
  const int content_h = std::max(1, usable_h - content_y);

  home_menu_panel_x = content_x;
  home_menu_panel_y = content_y;
  home_menu_panel_w = content_w;
  home_menu_panel_h = content_h;

  ui->draw_text(content_x, content_y, "JOT", theme.fg_keyword,
                theme.bg_default, true);
  ui->draw_text(content_x + 5, content_y, "Developer workspace",
                theme.fg_comment, theme.bg_default);

  std::string context = "No recent workspace yet";
  if (!recent_workspaces.empty()) {
    context = "Last folder  " + get_filename(recent_workspaces.front());
  } else if (!recent_files.empty()) {
    context = "Last file  " + get_filename(recent_files.front());
  }
  ui->draw_text(content_x, content_y + 1,
                ellipsize_right(context, std::max(0, content_w - 2)),
                theme.fg_default, theme.bg_default);

  const bool two_column = screen_w >= 88 && content_w >= 78;
  const int gap = two_column ? 4 : 0;
  const int action_w = two_column ? std::max(24, std::min(34, content_w / 3))
                                  : content_w;
  const int recent_w = two_column ? std::max(1, content_w - action_w - gap)
                                  : content_w;
  const int action_x = content_x;
  const int recent_x = two_column ? content_x + action_w + gap : content_x;
  const int section_y = content_y + 4;
  const int row_limit = usable_h - 1;

  home_menu_entries.clear();

  std::vector<HomeMenuRenderItem> items;
  if (!recent_workspaces.empty() || !recent_files.empty()) {
    HomeMenuRenderItem resume;
    resume.action = HOME_ACTION_RESUME;
    if (!recent_workspaces.empty()) {
      resume.recent_workspace_index = 0;
      resume.label = workspace_icon() + std::string("Resume ") +
                     get_filename(recent_workspaces.front());
      resume.secondary = recent_workspaces.front();
    } else {
      resume.recent_index = 0;
      resume.label = icon_for_path(recent_files.front()) + " Resume " +
                     get_filename(recent_files.front());
      resume.secondary = recent_files.front();
    }
    items.push_back(std::move(resume));
  }
  items.push_back(
      {HOME_ACTION_OPEN_PATH_PROMPT, -1, -1, "  Open Folder / File", "open "});
  items.push_back(
      {HOME_ACTION_OPEN_RECENT_PROMPT, -1, -1, "󰱼  Open Recent", "openrecent "});
  items.push_back({HOME_ACTION_NEW_FILE, -1, -1, "  New File", ""});
  items.push_back(
      {HOME_ACTION_COMMAND_PALETTE, -1, -1, "  Command Palette", ""});
  items.push_back({HOME_ACTION_THEME_CHOOSER, -1, -1, "󰔎  Theme", ""});
  items.push_back(
      {HOME_ACTION_CONTINUE_EDITOR, -1, -1, "󰋖  Continue Editing", ""});

  auto draw_section_title = [&](int x, int y, const std::string &title,
                                int width) {
    if (y >= row_limit || width <= 0) {
      return;
    }
    ui->draw_text(x, y, ellipsize_right(title, width),
                  theme.fg_sidebar_directory, theme.bg_default, true);
  };

  auto draw_home_item = [&](const HomeMenuRenderItem &item, int x, int y,
                            int width) -> bool {
    if (y >= row_limit || width <= 0) {
      return false;
    }
    const int entry_index = (int)home_menu_entries.size();
    bool selected = (entry_index == home_menu_selected);
    int fg = selected ? theme.fg_selection : theme.fg_default;
    int bg = selected ? theme.bg_selection : theme.bg_default;

    UIRect row_rect = {x, y, width, 1};
    ui->fill_rect(row_rect, " ", fg, bg);
    std::string label = ellipsize_right(item.label, std::max(0, width - 2));
    ui->draw_text(x + 1, y, label, fg, bg, selected);

    if (!item.secondary.empty() && width > 34) {
      std::string secondary = shorten_tail(item.secondary, width / 2);
      int sx = x + width - (int)secondary.size() - 1;
      if (sx > x + (int)label.size() + 2) {
        int path_fg = selected ? theme.fg_selection : theme.fg_comment;
        ui->draw_text(sx, y, secondary, path_fg, bg);
      }
    }

    home_menu_entries.push_back({item.action, item.recent_index,
                                 item.recent_workspace_index, x, y, width});
    return true;
  };

  int action_y = section_y;
  draw_section_title(action_x, action_y, "Start", action_w);
  action_y += 2;
  for (const auto &item : items) {
    if (draw_home_item(item, action_x, action_y, action_w)) {
      action_y++;
    }
  }

  auto draw_recent_list = [&](int x, int &y, int width,
                              const std::string &title, bool workspaces) {
    draw_section_title(x, y, title, width);
    y++;
    const int max_show = workspaces ? std::min(7, (int)recent_workspaces.size())
                                    : std::min(9, (int)recent_files.size());
    if (max_show == 0) {
      if (y < row_limit) {
        ui->draw_text(x + 1, y, workspaces ? "No recent folders"
                                           : "No recent files",
                      theme.fg_comment, theme.bg_default);
        y++;
      }
      return;
    }
    for (int i = 0; i < max_show && y < row_limit; i++) {
      HomeMenuRenderItem recent;
      if (workspaces) {
        recent.action = HOME_ACTION_OPEN_RECENT_WORKSPACE;
        recent.recent_workspace_index = i;
        recent.label = workspace_icon() + get_filename(recent_workspaces[i]);
        recent.secondary = display_parent(recent_workspaces[i]);
      } else {
        recent.action = HOME_ACTION_OPEN_RECENT;
        recent.recent_index = i;
        recent.label = icon_for_path(recent_files[i]) + get_filename(recent_files[i]);
        recent.secondary = display_parent(recent_files[i]);
      }
      if (draw_home_item(recent, x, y, width)) {
        y++;
      }
    }
  };

  int recent_y = two_column ? section_y : action_y + 1;
  draw_recent_list(recent_x, recent_y, recent_w, "Recent Folders", true);
  if (recent_y < row_limit) {
    recent_y++;
  }
  draw_recent_list(recent_x, recent_y, recent_w, "Recent Files", false);

  if (home_menu_entries.empty()) {
    home_menu_selected = 0;
  } else {
    home_menu_selected =
        std::clamp(home_menu_selected, 0, (int)home_menu_entries.size() - 1);
  }
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
    return buf.filepath.empty() && !buf.modified && buf.line_count() == 1 &&
           buf.line(0).empty();
  };

  auto execute_entry = [&](const HomeMenuEntry &entry) -> bool {
    switch (entry.action) {
    case HOME_ACTION_RESUME:
      if (entry.recent_workspace_index >= 0) {
        return open_workspace_by_index(entry.recent_workspace_index);
      }
      return open_recent_by_index(entry.recent_index);
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
      refresh_command_palette();
      needs_redraw = true;
      return true;
    case HOME_ACTION_OPEN_PATH_PROMPT:
      show_home_menu = false;
      show_command_palette = true;
      command_palette_query = "open ";
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      refresh_command_palette();
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
    HomeMenuEntry e = {HOME_ACTION_OPEN_PATH_PROMPT, -1, -1, 0, 0, 0};
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
