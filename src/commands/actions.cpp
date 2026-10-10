#include "core/editor.h"
#include "text_features.h"
#include <utility>
#include <vector>

void Editor::format_document()
{
  auto &buf = get_buffer();
  if (!buf.filepath.empty())
  {
    // When a language server is attached to the buffer, :format asks the
    // server for a proper textDocument/formatting pass (ts-ls, css/html/json
    // langservers, …) instead of re-indenting. Results arrive asynchronously
    // through the LSP poll and are applied in place.
    if (lsp_format_active_buffer())
    {
      return;
    }
  }
  save_state();
  for (size_t i = 0; i < buf.line_count(); i++)
  {
    EditorFeatures::format_line(buf.line_mut(i), tab_size);
  }
  buf.modified = true;
  needs_redraw = true;
  message = "Formatted document";
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::trim_trailing_whitespace()
{
  auto &buf = get_buffer();

  // What would change is collected before anything is written: a trim that
  // finds nothing is not an edit, and a save_state taken for it would push an
  // undo step that undoes nothing (the next undo would appear to do nothing
  // while it consumed the step).
  std::vector<std::pair<int, std::string>> trimmed_lines;
  for (int i = 0; i < (int)buf.line_count(); i++)
  {
    std::string trimmed = EditorFeatures::trim_right(buf.line(i));
    if (trimmed != buf.line(i))
    {
      trimmed_lines.push_back({i, std::move(trimmed)});
    }
  }

  if (trimmed_lines.empty())
  {
    message = "No trailing whitespace found";
    needs_redraw = true;
    return;
  }

  save_state();
  for (const auto &entry : trimmed_lines)
  {
    buf.line_mut(entry.first) = entry.second;
  }
  // Every trimmed line is shorter than it was, so a caret that sat in the
  // whitespace would be left past the end of its line -- and the insert paths
  // index the line with the caret's column.
  clamp_carets(buf);

  const int changed = (int)trimmed_lines.size();
  buf.modified = true;
  needs_redraw = true;
  message = "Trimmed trailing whitespace on " + std::to_string(changed) + " line(s)";
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::toggle_auto_indent_setting()
{
  auto_indent = !auto_indent;
  config.set("auto_indent", auto_indent ? "true" : "false");
  config.save();
  message = auto_indent ? "Auto-indent: ON" : "Auto-indent: OFF";
  needs_redraw = true;
}

void Editor::change_tab_size(int delta)
{
  int next = tab_size + delta;
  if (next < 1)
    next = 1;
  if (next > 8)
    next = 8;

  if (next == tab_size)
  {
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
