#include "editor.h"

#include <algorithm>

void Editor::close_menu_bar() {
  show_menu_bar_dropdown = false;
  menu_bar_active = -1;
  menu_bar_selected = 0;
  needs_redraw = true;
}

void Editor::open_menu_bar(int index) {
  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  if (menus.empty()) {
    close_menu_bar();
    return;
  }
  menu_bar_active = std::clamp(index, 0, (int)menus.size() - 1);
  show_menu_bar_dropdown = true;
  menu_bar_selected = 0;
  for (int i = 0; i < (int)menus[menu_bar_active].items.size(); i++) {
    if (menus[menu_bar_active].items[i].enabled) {
      menu_bar_selected = i;
      break;
    }
  }
  close_context_menu();
  hide_popup();
  needs_redraw = true;
}

void Editor::execute_menu_bar_item(int menu_index, int item_index) {
  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  if (menu_index < 0 || menu_index >= (int)menus.size()) {
    return;
  }
  const auto &items = menus[menu_index].items;
  if (item_index < 0 || item_index >= (int)items.size() ||
      !items[item_index].enabled) {
    return;
  }

  MenuBarItem item = items[item_index];
  close_menu_bar();

  switch (item.action) {
  case MENU_ACTION_COMMAND:
    if (!item.command.empty() && item.command.back() == ' ') {
      show_command_palette = true;
      command_palette_query = item.command;
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      refresh_command_palette();
      needs_redraw = true;
    } else {
      execute_command(item.command);
    }
    break;
  case MENU_ACTION_NEW_FILE:
    create_new_buffer();
    break;
  case MENU_ACTION_OPEN_FINDER:
    execute_command(":find");
    break;
  case MENU_ACTION_SAVE:
    save_file();
    break;
  case MENU_ACTION_SAVE_AS:
    save_file_as();
    break;
  case MENU_ACTION_CLOSE_FILE:
    close_buffer();
    break;
  case MENU_ACTION_QUIT:
    execute_command("Quit");
    break;
  case MENU_ACTION_UNDO:
    undo();
    break;
  case MENU_ACTION_REDO:
    redo();
    break;
  case MENU_ACTION_CUT:
    cut();
    break;
  case MENU_ACTION_COPY:
    copy();
    set_message("Copied");
    break;
  case MENU_ACTION_PASTE:
    paste();
    break;
  case MENU_ACTION_SELECT_ALL:
    select_all();
    break;
  case MENU_ACTION_SELECT_LINE:
    select_current_line();
    break;
  case MENU_ACTION_DUPLICATE_LINE:
    duplicate_line();
    break;
  case MENU_ACTION_MOVE_LINE_UP:
    move_line_up();
    break;
  case MENU_ACTION_MOVE_LINE_DOWN:
    move_line_down();
    break;
  case MENU_ACTION_TOGGLE_COMMENT:
    toggle_comment();
    break;
  case MENU_ACTION_COMMAND_PALETTE:
    toggle_command_palette();
    break;
  case MENU_ACTION_TOGGLE_SIDEBAR:
    toggle_sidebar();
    break;
  case MENU_ACTION_TOGGLE_MINIMAP:
    toggle_minimap();
    break;
  case MENU_ACTION_THEME:
    open_theme_chooser();
    break;
  case MENU_ACTION_HOME:
    set_home_menu_visible(true);
    break;
  case MENU_ACTION_TOGGLE_TERMINAL:
    toggle_integrated_terminal();
    break;
  case MENU_ACTION_NEW_TERMINAL:
    create_integrated_terminal();
    break;
  case MENU_ACTION_TASKS:
    execute_command(":task");
    break;
  case MENU_ACTION_RERUN_TASK:
    rerun_last_terminal_task();
    break;
  case MENU_ACTION_TOGGLE_DEBUG_PANEL:
    toggle_debugger_panel();
    break;
  case MENU_ACTION_DEBUG_STOP:
    stop_debugger_session();
    break;
  case MENU_ACTION_DEBUG_CONTINUE:
    continue_debugger_session();
    break;
  case MENU_ACTION_DEBUG_PAUSE:
    pause_debugger_session();
    break;
  case MENU_ACTION_DEBUG_STEP_IN:
    step_debugger_in();
    break;
  case MENU_ACTION_DEBUG_STEP_OVER:
    step_debugger_next();
    break;
  case MENU_ACTION_DEBUG_STEP_OUT:
    step_debugger_out();
    break;
  case MENU_ACTION_LSP_DEFINITION:
    request_lsp_definition();
    break;
  case MENU_ACTION_LSP_BACK:
    return_from_lsp_definition();
    break;
  case MENU_ACTION_HELP:
    execute_command(":help");
    break;
  case MENU_ACTION_NONE:
    break;
  }
  needs_redraw = true;
}

bool Editor::handle_menu_bar_input(int ch) {
  if (!show_menu_bar_dropdown) {
    return false;
  }

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  if (menus.empty()) {
    close_menu_bar();
    return true;
  }

  auto move_menu = [&](int delta) {
    int n = (int)menus.size();
    menu_bar_active = (menu_bar_active + delta + n) % n;
    menu_bar_selected = 0;
    for (int i = 0; i < (int)menus[menu_bar_active].items.size(); i++) {
      if (menus[menu_bar_active].items[i].enabled) {
        menu_bar_selected = i;
        break;
      }
    }
    needs_redraw = true;
  };

  auto move_item = [&](int delta) {
    if (menu_bar_active < 0 || menu_bar_active >= (int)menus.size() ||
        menus[menu_bar_active].items.empty()) {
      return;
    }
    int n = (int)menus[menu_bar_active].items.size();
    for (int step = 0; step < n; step++) {
      menu_bar_selected = (menu_bar_selected + delta + n) % n;
      if (menus[menu_bar_active].items[menu_bar_selected].enabled) {
        break;
      }
    }
    needs_redraw = true;
  };

  if (ch == 27) {
    close_menu_bar();
    return true;
  }
  if (ch == 1011 || ch == 'h' || ch == 'H') {
    move_menu(-1);
    return true;
  }
  if (ch == 1010 || ch == 'l' || ch == 'L') {
    move_menu(1);
    return true;
  }
  if (ch == 1008 || ch == 'k' || ch == 'K') {
    move_item(-1);
    return true;
  }
  if (ch == 1009 || ch == 'j' || ch == 'J') {
    move_item(1);
    return true;
  }
  if (ch == '\n' || ch == 13 || ch == ' ') {
    execute_menu_bar_item(menu_bar_active, menu_bar_selected);
    return true;
  }
  return true;
}
