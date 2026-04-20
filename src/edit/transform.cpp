#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>

namespace {
struct Range {
  int start_y = 0;
  int end_y = 0;
  int start_x = 0;
  int end_x = 0;
};

Range normalized_selection_range(const FileBuffer &buf) {
  Range r;
  r.start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  r.end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  r.start_x =
      buf.selection.start.y < buf.selection.end.y
          ? buf.selection.start.x
          : (buf.selection.start.y == buf.selection.end.y
                 ? std::min(buf.selection.start.x, buf.selection.end.x)
                 : buf.selection.end.x);
  r.end_x = buf.selection.start.y < buf.selection.end.y
                ? buf.selection.end.x
                : (buf.selection.start.y == buf.selection.end.y
                       ? std::max(buf.selection.start.x, buf.selection.end.x)
                       : buf.selection.start.x);
  return r;
}

bool is_word_char(char c) {
  const unsigned char uc = (unsigned char)c;
  return std::isalnum(uc) || c == '_';
}

char apply_case_char(char c, bool upper) {
  const unsigned char uc = (unsigned char)c;
  return (char)(upper ? std::toupper(uc) : std::tolower(uc));
}
} // namespace

void Editor::transform_selection_uppercase() {
  auto &buf = get_buffer();
  save_state();

  bool changed = false;
  if (buf.selection.active) {
    Range r = normalized_selection_range(buf);
    for (int y = r.start_y; y <= r.end_y; y++) {
      if (y < 0 || y >= (int)buf.lines.size()) {
        continue;
      }
      std::string &line = buf.lines[y];
      int from = 0;
      int to = (int)line.size();
      if (y == r.start_y) {
        from = std::clamp(r.start_x, 0, (int)line.size());
      }
      if (y == r.end_y) {
        to = std::clamp(r.end_x, 0, (int)line.size());
      }
      if (to < from) {
        std::swap(to, from);
      }
      for (int i = from; i < to; i++) {
        char next = apply_case_char(line[i], true);
        if (next != line[i]) {
          line[i] = next;
          changed = true;
        }
      }
    }
  } else if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.lines.size()) {
    std::string &line = buf.lines[buf.cursor.y];
    if (!line.empty()) {
      int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
      int start = cursor;
      int end = cursor;
      while (start > 0 && is_word_char(line[start - 1])) {
        start--;
      }
      while (end < (int)line.size() && is_word_char(line[end])) {
        end++;
      }
      if (start < end) {
        for (int i = start; i < end; i++) {
          char next = apply_case_char(line[i], true);
          if (next != line[i]) {
            line[i] = next;
            changed = true;
          }
        }
      }
    }
  }

  if (!changed) {
    set_message("Nothing to uppercase (select text or place cursor on a word)");
    return;
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Uppercase applied");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::transform_selection_lowercase() {
  auto &buf = get_buffer();
  save_state();

  bool changed = false;
  if (buf.selection.active) {
    Range r = normalized_selection_range(buf);
    for (int y = r.start_y; y <= r.end_y; y++) {
      if (y < 0 || y >= (int)buf.lines.size()) {
        continue;
      }
      std::string &line = buf.lines[y];
      int from = 0;
      int to = (int)line.size();
      if (y == r.start_y) {
        from = std::clamp(r.start_x, 0, (int)line.size());
      }
      if (y == r.end_y) {
        to = std::clamp(r.end_x, 0, (int)line.size());
      }
      if (to < from) {
        std::swap(to, from);
      }
      for (int i = from; i < to; i++) {
        char next = apply_case_char(line[i], false);
        if (next != line[i]) {
          line[i] = next;
          changed = true;
        }
      }
    }
  } else if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.lines.size()) {
    std::string &line = buf.lines[buf.cursor.y];
    if (!line.empty()) {
      int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
      int start = cursor;
      int end = cursor;
      while (start > 0 && is_word_char(line[start - 1])) {
        start--;
      }
      while (end < (int)line.size() && is_word_char(line[end])) {
        end++;
      }
      if (start < end) {
        for (int i = start; i < end; i++) {
          char next = apply_case_char(line[i], false);
          if (next != line[i]) {
            line[i] = next;
            changed = true;
          }
        }
      }
    }
  }

  if (!changed) {
    set_message("Nothing to lowercase (select text or place cursor on a word)");
    return;
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Lowercase applied");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::sort_selected_lines() {
  auto &buf = get_buffer();
  if (buf.lines.empty()) {
    set_message("Nothing to sort");
    return;
  }

  int start_y = 0;
  int end_y = 0;
  if (buf.selection.active) {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  } else {
    set_message("Select lines first to sort");
    return;
  }

  start_y = std::clamp(start_y, 0, (int)buf.lines.size() - 1);
  end_y = std::clamp(end_y, 0, (int)buf.lines.size() - 1);
  if (start_y > end_y) {
    std::swap(start_y, end_y);
  }
  if (start_y == end_y) {
    set_message("Select multiple lines to sort");
    return;
  }

  save_state();
  std::stable_sort(buf.lines.begin() + start_y, buf.lines.begin() + end_y + 1,
                   [](const std::string &a, const std::string &b) {
                     std::string la = a;
                     std::string lb = b;
                     std::transform(la.begin(), la.end(), la.begin(),
                                    [](unsigned char c) {
                                      return (char)std::tolower(c);
                                    });
                     std::transform(lb.begin(), lb.end(), lb.begin(),
                                    [](unsigned char c) {
                                      return (char)std::tolower(c);
                                    });
                     return la < lb;
                   });

  buf.modified = true;
  buf.cursor.y = start_y;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[start_y].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Sorted selected lines");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
