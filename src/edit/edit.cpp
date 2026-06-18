#include "autoclose.h"
#include "editor.h"
#include "html.h"
#include "text_features.h"
#include "python_bridge/api.h"
#include <algorithm>
#include <cctype>

namespace {
int tab_advance(int visual_col, int tab_size) {
  const int ts = std::max(1, tab_size);
  const int rem = visual_col % ts;
  return rem == 0 ? ts : (ts - rem);
}

int compute_visual_column(const std::string &line, int logical_col,
                          int tab_size) {
  int clamped = std::clamp(logical_col, 0, (int)line.size());
  int visual = 0;
  for (int i = 0; i < clamped; i++) {
    visual += (line[i] == '\t') ? tab_advance(visual, tab_size) : 1;
  }
  return visual;
}
} // namespace

bool Editor::insert_char(char c) {
  save_state();
  auto &buf = get_buffer();

  if (buf.selection.active && AutoClose::should_auto_close(c)) {
    Cursor s = buf.selection.start;
    Cursor e = buf.selection.end;
    if (s.y > e.y || (s.y == e.y && s.x > e.x)) {
      std::swap(s, e);
    }

    if (s.y >= 0 && s.y < (int)buf.line_count() && e.y >= 0 &&
        e.y < (int)buf.line_count()) {
      char closing = AutoClose::get_closing_bracket(c);
      if (closing != '\0') {
        if (s.y == e.y) {
          auto &line = buf.line_mut(s.y);
          s.x = std::max(0, std::min(s.x, (int)line.length()));
          e.x = std::max(0, std::min(e.x, (int)line.length()));
          line.insert(e.x, 1, closing);
          line.insert(s.x, 1, c);
        } else {
          auto &end_line = buf.line_mut(e.y);
          auto &start_line = buf.line_mut(s.y);
          e.x = std::max(0, std::min(e.x, (int)end_line.length()));
          s.x = std::max(0, std::min(s.x, (int)start_line.length()));
          end_line.insert(e.x, 1, closing);
          start_line.insert(s.x, 1, c);
        }

        buf.selection.start = {s.x + 1, s.y};
        buf.selection.end = {e.x + 1, e.y};
        buf.selection.active = true;
        buf.cursor = buf.selection.end;
        buf.preferred_x = buf.cursor.x;
        buf.modified = true;
        buf.is_placeholder = false;
        ensure_cursor_visible();
        needs_redraw = true;
        if (python_api)
          python_api->on_buffer_change(buf.filepath, "");
        if (!buf.filepath.empty())
          notify_lsp_change(buf.filepath);
        return false;
      }
    }
  }

  if (buf.selection.active) {
    delete_selection();
  }

  bool inserted_html_closing_tag = false;

  if (c == '\t') {
    const std::string &line = buf.line(buf.cursor.y);
    int visual_col = compute_visual_column(line, buf.cursor.x, tab_size);
    int spaces_to_insert = tab_advance(visual_col, tab_size);
    std::string spaces(spaces_to_insert, ' ');
    buf.line_mut(buf.cursor.y).insert(buf.cursor.x, spaces);
    buf.cursor.x += spaces_to_insert;
  } else {
    // Check if we should skip closing bracket
    if (AutoClose::is_closing_bracket(c) &&
        AutoClose::should_skip_closing(c, buf.line_mut(buf.cursor.y),
                                       buf.cursor.x)) {
      buf.cursor.x++;
      needs_redraw = true;
      return false;
    }

    buf.line_mut(buf.cursor.y).insert(buf.cursor.x, 1, c);
    buf.cursor.x++;


    if (c == '>' && HtmlFeatures::is_markup_tag_extension(buf.filepath)) {
      std::string closing;
      std::string &line = buf.line_mut(buf.cursor.y);
      if (HtmlFeatures::should_insert_closing_tag(line, buf.cursor.x, closing)) {
        line.insert(buf.cursor.x, closing);
        inserted_html_closing_tag = true;
      }
    }

    if (auto_indent && (c == '}' || c == ']' || c == ')')) {
      if (EditorFeatures::should_dedent(buf.line_mut(buf.cursor.y))) {
        int current_indent =
            EditorFeatures::get_indent_level(buf.line_mut(buf.cursor.y));
        if (current_indent >= tab_size) {
          // Simply reduce by one tab stop
          int new_indent = current_indent - tab_size;
          std::string trimmed = buf.line_mut(buf.cursor.y);
          size_t start = trimmed.find_first_not_of(" \t");
          if (start != std::string::npos) {
            trimmed.erase(0, start);
            buf.line_mut(buf.cursor.y) =
                EditorFeatures::get_indent_string(new_indent, tab_size) +
                trimmed;
            buf.cursor.x = std::max(0, buf.cursor.x - tab_size);
          }
        }
      }
    }
    
    if (AutoClose::should_auto_close(c)) {
      char closing = AutoClose::get_closing_bracket(c);
      if (closing != '\0') {
        buf.line_mut(buf.cursor.y).insert(buf.cursor.x, 1, closing);
      }
    }
  }

  buf.modified = true;
  buf.is_placeholder = false;
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);

  return inserted_html_closing_tag;

}

void Editor::insert_string(const std::string &str) {
  save_state();
  auto &buf = get_buffer();
  if (buf.selection.active) {
    delete_selection();
  }
  buf.line_mut(buf.cursor.y).insert(buf.cursor.x, str);
  buf.cursor.x += str.length();
  buf.modified = true;
  buf.is_placeholder = false;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_char(bool forward) {
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  if (buf.selection.active) {
    delete_selection();
    needs_redraw = true;
    return;
  }

  if (forward) {
    if (buf.cursor.x < (int)buf.line_mut(buf.cursor.y).length()) {
      buf.line_mut(buf.cursor.y).erase(buf.cursor.x, 1);
      buf.modified = true;
    } else if (buf.cursor.y < (int)buf.line_count() - 1) {
      buf.line_mut(buf.cursor.y) += buf.line_mut(buf.cursor.y + 1);
      buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
      buf.modified = true;
    }
  } else {
    if (buf.cursor.x > 0) {
      auto &line = buf.line_mut(buf.cursor.y);
      if (buf.cursor.x < (int)line.length()) {
        char left = line[buf.cursor.x - 1];
        char right = line[buf.cursor.x];
        char expected = AutoClose::get_closing_bracket(left);
        if (expected != '\0' && right == expected) {
          line.erase(buf.cursor.x, 1);
          buf.cursor.x--;
          line.erase(buf.cursor.x, 1);
          buf.modified = true;
        } else {
          buf.cursor.x--;
          line.erase(buf.cursor.x, 1);
          buf.modified = true;
        }
      } else {
        buf.cursor.x--;
        line.erase(buf.cursor.x, 1);
        buf.modified = true;
      }
    } else if (buf.cursor.y > 0) {
      buf.cursor.y--;
      buf.cursor.x = buf.line_mut(buf.cursor.y).length();
      buf.line_mut(buf.cursor.y) += buf.line_mut(buf.cursor.y + 1);
      buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
      buf.modified = true;
    }
  }
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_word_backward() {
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  if (buf.selection.active) {
    delete_selection();
    return;
  }

  if (buf.cursor.x == 0 && buf.cursor.y == 0)
    return;

  save_state();

  if (buf.cursor.x == 0 && buf.cursor.y > 0) {
    buf.cursor.y--;
    buf.cursor.x = (int)buf.line_mut(buf.cursor.y).length();
    buf.line_mut(buf.cursor.y) += buf.line_mut(buf.cursor.y + 1);
    buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
    buf.modified = true;
  } else {
    auto &line = buf.line_mut(buf.cursor.y);
    int start = buf.cursor.x;

    while (start > 0 &&
           std::isspace(static_cast<unsigned char>(line[start - 1]))) {
      start--;
    }
    while (start > 0 &&
           (std::isalnum(static_cast<unsigned char>(line[start - 1])) ||
            line[start - 1] == '_')) {
      start--;
    }
    if (start == buf.cursor.x) {
      start = std::max(0, buf.cursor.x - 1);
    }

    line.erase(start, buf.cursor.x - start);
    buf.cursor.x = start;
    buf.modified = true;
  }

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_word_forward() {
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  if (buf.selection.active) {
    delete_selection();
    return;
  }

  if (buf.cursor.y == (int)buf.line_count() - 1 &&
      buf.cursor.x == (int)buf.line_mut(buf.cursor.y).length())
    return;

  save_state();

  auto &line = buf.line_mut(buf.cursor.y);
  if (buf.cursor.x >= (int)line.length() &&
      buf.cursor.y < (int)buf.line_count() - 1) {
    buf.line_mut(buf.cursor.y) += buf.line_mut(buf.cursor.y + 1);
    buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
    buf.modified = true;
  } else {
    int end = buf.cursor.x;

    while (end < (int)line.length() &&
           std::isspace(static_cast<unsigned char>(line[end]))) {
      end++;
    }
    while (end < (int)line.length() &&
           (std::isalnum(static_cast<unsigned char>(line[end])) ||
            line[end] == '_')) {
      end++;
    }
    if (end == buf.cursor.x) {
      end = std::min((int)line.length(), buf.cursor.x + 1);
    }

    line.erase(buf.cursor.x, end - buf.cursor.x);
    buf.modified = true;
  }

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_selection() {
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  if (!buf.selection.active)
    return;

  int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  int start_x =
      buf.selection.start.y < buf.selection.end.y
          ? buf.selection.start.x
          : (buf.selection.start.y == buf.selection.end.y
                 ? std::min(buf.selection.start.x, buf.selection.end.x)
                 : buf.selection.end.x);
  int end_x = buf.selection.start.y < buf.selection.end.y
                  ? buf.selection.end.x
                  : (buf.selection.start.y == buf.selection.end.y
                         ? std::max(buf.selection.start.x, buf.selection.end.x)
                         : buf.selection.start.x);

  if (start_y == end_y) {
    buf.line_mut(start_y).erase(start_x, end_x - start_x);
    buf.cursor.y = start_y;
    buf.cursor.x = start_x;
  } else {
    buf.line_mut(start_y) =
        buf.line_mut(start_y).substr(0, start_x) + buf.line_mut(end_y).substr(end_x);
    buf.lines.erase(buf.lines.begin() + start_y + 1,
                    buf.lines.begin() + end_y + 1);
    buf.cursor.y = start_y;
    buf.cursor.x = start_x;
  }

  buf.selection.active = false;
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_line() {
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  if (buf.line_count() == 1) {
    clipboard = buf.line_mut(0);
    buf.line_mut(0) = "";
  } else {
    clipboard = buf.line_mut(buf.cursor.y);
    buf.lines.erase(buf.lines.begin() + buf.cursor.y);
    if (buf.cursor.y >= (int)buf.line_count())
      buf.cursor.y = buf.line_count() - 1;
  }
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::new_line() {
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  std::string current_line = buf.line_mut(buf.cursor.y);
  std::string remaining = current_line.substr(buf.cursor.x);
  buf.line_mut(buf.cursor.y) = current_line.substr(0, buf.cursor.x);

  std::string new_line_str = "";
  bool split_closing_bracket_line = false;
  std::string closing_line_str = remaining;
  bool preserve_remaining_as_is = false;
  if (auto_indent && buf.cursor.y >= 0) {
    int indent = EditorFeatures::get_indent_level(buf.line_mut(buf.cursor.y));

    std::string html_tag;
    if (HtmlFeatures::is_markup_tag_extension(buf.filepath) &&
        HtmlFeatures::is_between_matching_tags(buf.line_mut(buf.cursor.y), remaining, html_tag)) {
          int closing_indent = indent;
          int inner_indent = indent + tab_size;

          new_line_str = EditorFeatures::get_indent_string(inner_indent, tab_size);
          closing_line_str = EditorFeatures::get_indent_string(closing_indent, tab_size) + remaining;
          split_closing_bracket_line = true; 
        } else if (EditorFeatures::should_auto_indent(buf.line_mut(buf.cursor.y))) {
          indent += tab_size;
        }

    if (!split_closing_bracket_line) {
      new_line_str = EditorFeatures::get_indent_string(indent, tab_size);
    }

    if (EditorFeatures::should_dedent(remaining)) {
      size_t content_start = remaining.find_first_not_of(" \t");
      std::string trimmed_remaining =
          content_start == std::string::npos ? "" : remaining.substr(content_start);
      int closing_indent = EditorFeatures::get_indent_level(buf.line_mut(buf.cursor.y));
      closing_line_str =
          EditorFeatures::get_indent_string(closing_indent, tab_size) +
          trimmed_remaining;
      split_closing_bracket_line = true;
    } else if (!remaining.empty() && buf.cursor.x > 0) {
      const char left = current_line[buf.cursor.x - 1];
      const bool has_non_space_remaining =
          remaining.find_first_not_of(" \t") != std::string::npos;
      if (!std::isspace(static_cast<unsigned char>(left)) &&
          has_non_space_remaining) {
        // When splitting in the middle of content, keep the moved text as-is
        // to avoid adding unexpected extra indentation.
        preserve_remaining_as_is = true;
      }
    }
  }

  if (split_closing_bracket_line) {
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, new_line_str);
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 2, closing_line_str);
    buf.cursor.y++;
    buf.cursor.x = new_line_str.length();
  } else if (preserve_remaining_as_is) {
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, remaining);
    buf.cursor.y++;
    buf.cursor.x = 0;
  } else {
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1,
                     new_line_str + remaining);
    buf.cursor.y++;
    buf.cursor.x = new_line_str.length();
  }
  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}
