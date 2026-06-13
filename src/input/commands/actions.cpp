#include "editor.h"
#include "text_features.h"

void Editor::format_document() {
  auto &buf = get_buffer();
  save_state();
  for (size_t i = 0; i < buf.line_count(); i++) {
    EditorFeatures::format_line(buf.line_mut(i), tab_size);
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
  for (size_t i = 0; i < buf.line_count(); i++) {
    auto &line = buf.line_mut(i);
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
