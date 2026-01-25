#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>

void Editor::toggle_command_palette() {
  show_command_palette = !show_command_palette;
  if (show_command_palette) {
    command_palette_query = "";
    refresh_command_palette();
  }
}

void Editor::refresh_command_palette() {
  command_palette_results.clear();
  std::string query = command_palette_query;
  std::transform(query.begin(), query.end(), query.begin(), ::tolower);

  std::vector<std::string> commands = {
      "New File",      "Open File",        "Save",
      "Save As",       "Close File",       "Toggle Minimap",
      "Toggle Search", "Split Horizontal", "Split Vertical",
      "Next Pane",     "Previous Pane",    "Previous Pane",
      "Quit"};

  for (const auto &cmd : commands) {
    std::string cmd_lower = cmd;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(),
                   ::tolower);
    if (query.empty() || cmd_lower.find(query) != std::string::npos) {
      command_palette_results.push_back(cmd);
    }
  }

  for (const auto &cmd : custom_commands) {
    std::string cmd_lower = cmd.name;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(),
                   ::tolower);
    if (query.empty() || cmd_lower.find(query) != std::string::npos) {
      command_palette_results.push_back(cmd.name);
    }
  }

  if (command_palette_selected >= (int)command_palette_results.size()) {
    command_palette_selected = 0;
  }
}

void Editor::execute_command(const std::string &cmd) {
  if (cmd == "New File") {
    create_new_buffer();
  } else if (cmd == "Open File") {
    message = "Press Space+F to open file finder";
  } else if (cmd == "Save") {
    save_file();
  } else if (cmd == "Save As") {
    save_file_as();
  } else if (cmd == "Close File") {
    close_buffer();
  } else if (cmd == "Toggle Minimap") {
    toggle_minimap();
  } else if (cmd == "Toggle Search") {
    toggle_search();
  } else if (cmd == "Split Horizontal") {
    split_pane_horizontal();
  } else if (cmd == "Split Vertical") {
    split_pane_vertical();
  } else if (cmd == "Close Pane") {
    close_pane();
  } else if (cmd == "Next Pane") {
    next_pane();
  } else if (cmd == "Previous Pane") {
    prev_pane();
  } else if (cmd == "Previous Pane") {
    prev_pane();
  } else if (cmd == "Quit") {
    running = false;
  }
}

void Editor::handle_command_palette(int ch) {
  if (ch == 27) {
    show_command_palette = false;
    command_palette_query.clear();
    needs_redraw = true;
  } else if (ch == '\n' || ch == 13) {
    if (!command_palette_results.empty()) {
      execute_command(command_palette_results[command_palette_selected]);
      show_command_palette = false;
      command_palette_query.clear();
      needs_redraw = true;
    }
  } else if (ch == 1008 || ch == 'k') {
    if (command_palette_selected > 0)
      command_palette_selected--;
    needs_redraw = true;
  } else if (ch == 1009 || ch == 'j') {
    if (command_palette_selected < (int)command_palette_results.size() - 1)
      command_palette_selected++;
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    if (!command_palette_query.empty()) {
      command_palette_query.pop_back();
      refresh_command_palette();
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    command_palette_query += ch;
    refresh_command_palette();
    needs_redraw = true;
  }
}

void Editor::handle_telescope(int ch) {
  if (ch == 27) {
    telescope.close();
    waiting_for_space_f = false;
    needs_redraw = true;
  } else if (ch == '\n' || ch == 13) {
    std::string path = telescope.get_selected_path();
    if (!path.empty()) {
      std::string ext = get_file_extension(path);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

      if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
          ext == ".bmp" || ext == ".svg" || ext == ".webp" || ext == ".ico") {
        image_viewer.open(path);
      } else {
        open_file(path);
      }
      telescope.close();
      waiting_for_space_f = false;
      needs_redraw = true;
    }
  } else if (ch == 1008 || ch == 'k') {
    telescope.move_up();
    needs_redraw = true;
  } else if (ch == 1009 || ch == 'j') {
    telescope.move_down();
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    std::string q = telescope.get_query();
    if (!q.empty()) {
      q.pop_back();
      telescope.set_query(q);
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    std::string q = telescope.get_query();
    q += ch;
    telescope.set_query(q);
    needs_redraw = true;
  }
}

void Editor::toggle_minimap() { show_minimap = !show_minimap; }

void Editor::jump_to_matching_bracket() {
  auto &buf = get_buffer();
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size())
    return;
  if (buf.cursor.x < 0 || buf.cursor.x >= (int)buf.lines[buf.cursor.y].length())
    return;

  char ch = buf.lines[buf.cursor.y][buf.cursor.x];
  int match = -1;

  if (ch == '(')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '(', ')');
  else if (ch == ')')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '(', ')');
  else if (ch == '{')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '{', '}');
  else if (ch == '}')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '{', '}');
  else if (ch == '[')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '[', ']');
  else if (ch == ']')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '[', ']');

  if (match >= 0) {
    buf.cursor.y = match / 10000;
    buf.cursor.x = match % 10000;
    clamp_cursor(get_pane().buffer_id);
    needs_redraw = true;
    message = "Jumped to matching bracket";
  }
}

void Editor::format_document() {
  auto &buf = get_buffer();
  save_state();
  for (auto &line : buf.lines) {
    EditorFeatures::format_line(line, tab_size);
  }
  buf.modified = true;
  needs_redraw = true;
  message = "Formatted document";
}

void Editor::register_command(const std::string &name,
                              const std::string &callback) {
  custom_commands.push_back({name, callback});
}

void Editor::show_input_prompt(const std::string &message,
                               const std::string &callback) {
  input_prompt_visible = true;
  input_prompt_message = message;
  input_prompt_callback = callback;
  input_prompt_buffer = "";
  needs_redraw = true;
}

void Editor::hide_input_prompt() {
  input_prompt_visible = false;
  needs_redraw = true;
}

void Editor::handle_input_prompt(int ch) {
  if (ch == 27) { // Escape
    hide_input_prompt();
  } else if (ch == '\n' || ch == 13) {
    hide_input_prompt();
    if (python_api) {
      // Call the callback via Python API
      std::string safe_input = input_prompt_buffer;
      // Escape quotes to prevent syntax errors
      size_t pos = 0;
      while ((pos = safe_input.find("'", pos)) != std::string::npos) {
        safe_input.replace(pos, 1, "\\'");
        pos += 2;
      }

      std::string code =
          "import jcode_api; jcode_api._internal_call_callback('" +
          input_prompt_callback + "', '" + safe_input + "')";
      python_api->execute_code(code);
    }
  } else if (ch == 127 || ch == 8) {
    if (!input_prompt_buffer.empty())
      input_prompt_buffer.pop_back();
    needs_redraw = true;
  } else if (ch >= 32 && ch < 127) {
    input_prompt_buffer += (char)ch;
    needs_redraw = true;
  }
}
