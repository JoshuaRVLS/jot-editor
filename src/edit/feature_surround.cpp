#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>

namespace {
bool is_word_char(char c) {
  const unsigned char uc = (unsigned char)c;
  return std::isalnum(uc) || c == '_';
}

bool pair_match(char left, char right) {
  return (left == '(' && right == ')') || (left == '[' && right == ']') ||
         (left == '{' && right == '}') || (left == '"' && right == '"') ||
         (left == '\'' && right == '\'') || (left == '`' && right == '`');
}
} // namespace

bool Editor::surround_selection_or_word(const std::string &left,
                                        const std::string &right) {
  if (left.empty() || right.empty()) {
    set_message("Usage: :surround <left> <right>");
    return false;
  }

  auto &buf = get_buffer();
  save_state();

  if (buf.selection.active) {
    Cursor s = buf.selection.start;
    Cursor e = buf.selection.end;
    if (s.y > e.y || (s.y == e.y && s.x > e.x)) {
      std::swap(s, e);
    }

    s.y = std::clamp(s.y, 0, (int)buf.lines.size() - 1);
    e.y = std::clamp(e.y, 0, (int)buf.lines.size() - 1);
    s.x = std::clamp(s.x, 0, (int)buf.lines[s.y].size());
    e.x = std::clamp(e.x, 0, (int)buf.lines[e.y].size());

    buf.lines[e.y].insert((size_t)e.x, right);
    buf.lines[s.y].insert((size_t)s.x, left);
    buf.selection.start = {s.x + (int)left.size(), s.y};
    buf.selection.end = {e.x + (int)left.size(), e.y};
    buf.cursor = buf.selection.end;
  } else {
    int y = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
    std::string &line = buf.lines[y];
    int x = std::clamp(buf.cursor.x, 0, (int)line.size());
    int start = x;
    int end = x;
    while (start > 0 && is_word_char(line[start - 1])) {
      start--;
    }
    while (end < (int)line.size() && is_word_char(line[end])) {
      end++;
    }
    if (start == end) {
      set_message("No selection/word to surround");
      return false;
    }
    line.insert((size_t)end, right);
    line.insert((size_t)start, left);
    buf.cursor.y = y;
    buf.cursor.x = end + (int)left.size() + (int)right.size();
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Surrounded selection/word");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
  return true;
}

bool Editor::unsurround_selection_or_cursor() {
  auto &buf = get_buffer();
  if (buf.lines.empty()) {
    set_message("Nothing to unsurround");
    return false;
  }

  save_state();
  int y = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
  std::string &line = buf.lines[y];
  if (line.size() < 2) {
    set_message("Nothing to unsurround");
    return false;
  }

  int x = std::clamp(buf.cursor.x, 0, (int)line.size());
  int left_idx = std::clamp(x - 1, 0, (int)line.size() - 1);
  int right_idx = std::clamp(x, 0, (int)line.size() - 1);

  bool done = false;
  if (left_idx >= 0 && right_idx >= 0 && left_idx < (int)line.size() &&
      right_idx < (int)line.size() &&
      pair_match(line[left_idx], line[right_idx])) {
    line.erase((size_t)right_idx, 1);
    line.erase((size_t)left_idx, 1);
    buf.cursor.x = left_idx;
    done = true;
  } else {
    int start = x;
    int end = x;
    while (start > 0 && is_word_char(line[start - 1])) {
      start--;
    }
    while (end < (int)line.size() && is_word_char(line[end])) {
      end++;
    }
    if (start > 0 && end < (int)line.size() &&
        pair_match(line[start - 1], line[end])) {
      line.erase((size_t)end, 1);
      line.erase((size_t)(start - 1), 1);
      buf.cursor.x = start - 1;
      done = true;
    }
  }

  if (!done) {
    set_message("No surrounding pair found");
    return false;
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Removed surrounding pair");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
  return true;
}
