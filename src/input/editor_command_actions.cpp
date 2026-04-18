#include "editor.h"
#include "editor_features.h"
#include "python_api.h"

void Editor::format_document() {
  auto &buf = get_buffer();
  save_state();
  for (auto &line : buf.lines) {
    EditorFeatures::format_line(line, tab_size);
  }
  buf.modified = true;
  needs_redraw = true;
  message = "Formatted document";
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::trim_trailing_whitespace() {
  auto &buf = get_buffer();
  save_state();

  int changed = 0;
  for (auto &line : buf.lines) {
    std::string trimmed = EditorFeatures::trim_right(line);
    if (trimmed != line) {
      line = trimmed;
      changed++;
    }
  }

  if (changed > 0) {
    buf.modified = true;
    message = "Trimmed trailing whitespace on " + std::to_string(changed) +
              " line(s)";
  } else {
    message = "No trailing whitespace found";
  }
  needs_redraw = true;
  if (changed > 0 && !buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::toggle_auto_indent_setting() {
  auto_indent = !auto_indent;
  config.set("auto_indent", auto_indent ? "true" : "false");
  config.save();
  message = auto_indent ? "Auto-indent: ON" : "Auto-indent: OFF";
  needs_redraw = true;
}

void Editor::change_tab_size(int delta) {
  int next = tab_size + delta;
  if (next < 1)
    next = 1;
  if (next > 8)
    next = 8;

  if (next == tab_size) {
    message = "Tab size unchanged (" + std::to_string(tab_size) + ")";
    needs_redraw = true;
    return;
  }

  tab_size = next;
  config.set("tab_size", std::to_string(tab_size));
  config.save();
  message = "Tab size set to " + std::to_string(tab_size);
  needs_redraw = true;
}

void Editor::register_command(const std::string &name,
                              const std::string &callback) {
  for (auto &command : custom_commands) {
    if (command.name == name) {
      command.callback = callback;
      return;
    }
  }
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
  if (ch == 27) {
    hide_input_prompt();
  } else if (ch == '\n' || ch == 13) {
    hide_input_prompt();
    if (python_api) {
      python_api->invoke_callback(input_prompt_callback, input_prompt_buffer);
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
