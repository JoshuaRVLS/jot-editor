#include "editor.h"
#include "python_api.h"
#include "text_features.h"
#include <algorithm>
#include <sstream>
#include <vector>

namespace {
bool has_newline(const std::string &text) {
  return text.find('\n') != std::string::npos ||
         text.find('\r') != std::string::npos;
}

std::string trim_left_copy(const std::string &line) {
  const size_t start = line.find_first_not_of(" \t");
  return start == std::string::npos ? "" : line.substr(start);
}

std::string leading_indent(const std::string &line) {
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
    pos++;
  }
  return line.substr(0, pos);
}

int indent_columns(const std::string &indent, int tab_size) {
  int cols = 0;
  const int step = std::max(1, tab_size);
  for (char c : indent) {
    cols += (c == '\t') ? step : 1;
  }
  return cols;
}

std::string indent_from_columns(int columns, int tab_size, bool prefer_tabs) {
  columns = std::max(0, columns);
  const int step = std::max(1, tab_size);
  if (!prefer_tabs) {
    return std::string(columns, ' ');
  }
  const int tabs = columns / step;
  const int spaces = columns % step;
  return std::string(tabs, '\t') + std::string(spaces, ' ');
}

std::vector<std::string> split_paste_lines(const std::string &text) {
  std::vector<std::string> lines;
  std::string current;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (c == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') {
        i++;
      }
      lines.push_back(current);
      current.clear();
    } else if (c == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  lines.push_back(current);
  return lines;
}

bool starts_with_indent(const std::string &line, const std::string &indent) {
  return indent.empty() ||
         (line.size() >= indent.size() &&
          line.compare(0, indent.size(), indent) == 0);
}

std::string common_paste_indent(const std::vector<std::string> &lines) {
  std::string common;
  bool initialized = false;
  for (const auto &line : lines) {
    if (trim_left_copy(line).empty()) {
      continue;
    }
    const std::string indent = leading_indent(line);
    if (!initialized) {
      common = indent;
      initialized = true;
      continue;
    }
    size_t n = 0;
    while (n < common.size() && n < indent.size() && common[n] == indent[n]) {
      n++;
    }
    common.resize(n);
  }
  return common;
}

std::string join_paste_lines(const std::vector<std::string> &lines) {
  std::string out;
  for (size_t i = 0; i < lines.size(); i++) {
    if (i > 0) {
      out.push_back('\n');
    }
    out += lines[i];
  }
  return out;
}

std::string smart_indent_paste_text(const std::string &text,
                                    const std::string &target_line,
                                    int cursor_x, int tab_size) {
  if (!has_newline(text)) {
    return text;
  }

  std::vector<std::string> lines = split_paste_lines(text);
  if (lines.size() <= 1) {
    return text;
  }

  const std::string base_indent = leading_indent(target_line);
  const std::string before_cursor =
      target_line.substr(0, std::clamp(cursor_x, 0, (int)target_line.size()));
  const bool prefer_tabs = base_indent.find('\t') != std::string::npos;
  int base_columns = indent_columns(base_indent, tab_size);

  if (EditorFeatures::should_auto_indent(before_cursor)) {
    base_columns += std::max(1, tab_size);
  }

  std::string first_meaningful;
  for (const auto &line : lines) {
    first_meaningful = trim_left_copy(line);
    if (!first_meaningful.empty()) {
      break;
    }
  }
  if (!first_meaningful.empty() &&
      (first_meaningful[0] == '}' || first_meaningful[0] == ')' ||
       first_meaningful[0] == ']')) {
    base_columns -= std::max(1, tab_size);
  }

  const std::string common_indent = common_paste_indent(lines);
  const std::string rebased_indent =
      indent_from_columns(base_columns, tab_size, prefer_tabs);

  for (auto &line : lines) {
    if (trim_left_copy(line).empty()) {
      line.clear();
      continue;
    }
    if (starts_with_indent(line, common_indent)) {
      line.erase(0, common_indent.size());
    }
    line = rebased_indent + line;
  }

  return join_paste_lines(lines);
}
} // namespace

void Editor::copy() {
  auto &buf = get_buffer();
  if (!buf.selection.active) {
    clipboard = buf.line(buf.cursor.y);
    return;
  }

  clipboard = "";
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

  for (int i = start_y; i <= end_y; i++) {
    if (i == start_y && i == end_y) {
      clipboard += buf.line(i).substr(start_x, end_x - start_x);
    } else if (i == start_y) {
      clipboard += buf.line(i).substr(start_x) + "\n";
    } else if (i == end_y) {
      clipboard += buf.line(i).substr(0, end_x);
    } else {
      clipboard += buf.line(i) + "\n";
    }
  }
}

void Editor::cut() {
  copy();
  delete_selection();
}

void Editor::paste() {
  if (clipboard.empty())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  if (buf.selection.active) {
    delete_selection();
  }

  const std::string paste_text =
      (auto_indent && smart_paste_indent)
          ? smart_indent_paste_text(clipboard, buf.line(buf.cursor.y),
                                    buf.cursor.x, tab_size)
          : clipboard;

  std::vector<std::string> paste_lines = split_paste_lines(paste_text);
  if (paste_lines.size() == 1) {
    std::string &line_ref = buf.line_mut(buf.cursor.y);
    const int insert_x = std::clamp(buf.cursor.x, 0, (int)line_ref.size());
    line_ref.insert(insert_x, paste_lines[0]);
    buf.cursor.x = insert_x + (int)paste_lines[0].length();
  } else {
    std::string &line_ref = buf.line_mut(buf.cursor.y);
    const int insert_x = std::clamp(buf.cursor.x, 0, (int)line_ref.size());
    const std::string suffix = line_ref.substr(insert_x);
    line_ref.erase(insert_x);
    line_ref += paste_lines.front();

    int insert_y = buf.cursor.y + 1;
    for (size_t i = 1; i < paste_lines.size(); i++) {
      std::string next = paste_lines[i];
      if (i + 1 == paste_lines.size()) {
        next += suffix;
      }
      buf.lines.insert(buf.lines.begin() + insert_y, next);
      insert_y++;
    }

    buf.cursor.y = insert_y - 1;
    buf.cursor.x = (int)paste_lines.back().length();
  }

  buf.modified = true;
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::move_line_up() {
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  int start_y = buf.cursor.y;
  int end_y = buf.cursor.y;
  if (buf.selection.active) {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  }

  if (start_y <= 0)
    return;

  // Move the whole selected line block up by one row.
  std::rotate(buf.lines.begin() + start_y - 1, buf.lines.begin() + start_y,
              buf.lines.begin() + end_y + 1);

  buf.cursor.y = std::max(0, buf.cursor.y - 1);
  if (buf.selection.active) {
    buf.selection.start.y -= 1;
    buf.selection.end.y -= 1;
  }

  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::move_line_down() {
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy()) buf.materialize();
  int start_y = buf.cursor.y;
  int end_y = buf.cursor.y;
  if (buf.selection.active) {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  }

  if (end_y >= (int)buf.line_count() - 1)
    return;

  // Move the whole selected line block down by one row.
  std::rotate(buf.lines.begin() + start_y, buf.lines.begin() + end_y + 1,
              buf.lines.begin() + end_y + 2);

  buf.cursor.y = std::min((int)buf.line_count() - 1, buf.cursor.y + 1);
  if (buf.selection.active) {
    buf.selection.start.y += 1;
    buf.selection.end.y += 1;
  }

  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}
