#include "editor.h"
#include "folding.h"

#include <algorithm>
#include <vector>

namespace {
int fold_viewport_height(const SplitPane &pane, int tab_height) {
  return std::max(1, pane.h - tab_height - 1);
}

int anchored_scroll_for_header(const std::vector<FoldRange> &ranges,
                               int header_line, int desired_row,
                               int viewport_h, int line_count) {
  if (desired_row < 0) {
    return Folding::clamp_scroll_offset(ranges, header_line, viewport_h,
                                        line_count);
  }

  int header_visible_index = 0;
  for (int line = 0; line < line_count && line < header_line; line++) {
    if (!Folding::is_line_hidden(ranges, line)) {
      header_visible_index++;
    }
  }

  int target_visible_index = std::max(0, header_visible_index - desired_row);
  int scroll = Folding::buffer_line_for_visible_index(
      ranges, target_visible_index, line_count);
  return Folding::clamp_scroll_offset(ranges, scroll, viewport_h, line_count);
}
} // namespace

void Editor::refresh_folds(FileBuffer &buf) {
  if (buf.is_lazy()) {
    return;
  }
  Folding::refresh_ranges(buf.fold_ranges, buf.lines,
                          get_file_extension(buf.filepath));
  if (Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
    while (buf.cursor.y > 0 &&
           Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
      buf.cursor.y--;
    }
    clamp_cursor(get_pane().buffer_id);
  }
}

bool Editor::toggle_fold_at_line(FileBuffer &buf, int line) {
  refresh_folds(buf);
  int index = Folding::fold_starting_at_line(buf.fold_ranges, line);
  if (index < 0) {
    index = Folding::fold_at_or_before_line(buf.fold_ranges, line);
  }
  if (index < 0) {
    return false;
  }
  const bool will_collapse = !buf.fold_ranges[index].collapsed;
  const int header_line = buf.fold_ranges[index].start_line;
  const int viewport_h = fold_viewport_height(get_pane(), tab_height);
  const int header_row =
      will_collapse
          ? Folding::visible_row_for_line(buf.fold_ranges, buf.scroll_offset,
                                          header_line, viewport_h,
                                          (int)buf.line_count())
          : -1;
  buf.fold_ranges[index].collapsed = !buf.fold_ranges[index].collapsed;
  if (buf.fold_ranges[index].collapsed &&
      Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
    buf.cursor.y = buf.fold_ranges[index].start_line;
    clamp_cursor(get_pane().buffer_id);
  }
  ensure_cursor_visible();
  if (will_collapse) {
    buf.scroll_offset =
        anchored_scroll_for_header(buf.fold_ranges, header_line, header_row,
                                   viewport_h, (int)buf.line_count());
  }
  needs_redraw = true;
  return true;
}

bool Editor::fold_at_cursor() {
  auto &buf = get_buffer();
  refresh_folds(buf);
  int index = Folding::fold_at_or_before_line(buf.fold_ranges, buf.cursor.y);
  if (index < 0) {
    set_message("No foldable block");
    return false;
  }
  const bool will_collapse = !buf.fold_ranges[index].collapsed;
  const int header_line = buf.fold_ranges[index].start_line;
  const int viewport_h = fold_viewport_height(get_pane(), tab_height);
  const int header_row =
      will_collapse
          ? Folding::visible_row_for_line(buf.fold_ranges, buf.scroll_offset,
                                          header_line, viewport_h,
                                          (int)buf.line_count())
          : -1;
  buf.fold_ranges[index].collapsed = true;
  if (Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
    buf.cursor.y = buf.fold_ranges[index].start_line;
    clamp_cursor(get_pane().buffer_id);
  }
  ensure_cursor_visible();
  if (will_collapse) {
    buf.scroll_offset =
        anchored_scroll_for_header(buf.fold_ranges, header_line, header_row,
                                   viewport_h, (int)buf.line_count());
  }
  int hidden = Folding::hidden_line_count_for_header(buf.fold_ranges,
                                                    header_line);
  set_message("Folded block: " + std::to_string(hidden) + " line(s) hidden");
  needs_redraw = true;
  return true;
}

bool Editor::unfold_at_cursor() {
  auto &buf = get_buffer();
  refresh_folds(buf);
  int index = -1;
  if (!Folding::is_line_folded_header(buf.fold_ranges, buf.cursor.y, &index)) {
    index = Folding::fold_at_or_before_line(buf.fold_ranges, buf.cursor.y);
  }
  if (index < 0) {
    set_message("No folded block");
    return false;
  }
  buf.fold_ranges[index].collapsed = false;
  ensure_cursor_visible();
  set_message("Unfolded block");
  needs_redraw = true;
  return true;
}

bool Editor::toggle_fold_at_cursor() {
  auto &buf = get_buffer();
  if (!toggle_fold_at_line(buf, buf.cursor.y)) {
    set_message("No foldable block");
    return false;
  }
  set_message("Toggled fold");
  return true;
}

void Editor::fold_all() {
  auto &buf = get_buffer();
  refresh_folds(buf);
  for (auto &range : buf.fold_ranges) {
    range.collapsed = true;
  }
  if (Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
    while (buf.cursor.y > 0 &&
           Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
      buf.cursor.y--;
    }
    clamp_cursor(get_pane().buffer_id);
  }
  ensure_cursor_visible();
  set_message("Folded all blocks");
  needs_redraw = true;
}

void Editor::unfold_all() {
  auto &buf = get_buffer();
  refresh_folds(buf);
  for (auto &range : buf.fold_ranges) {
    range.collapsed = false;
  }
  ensure_cursor_visible();
  set_message("Unfolded all blocks");
  needs_redraw = true;
}

bool Editor::is_line_hidden_by_fold(const FileBuffer &buf, int line) const {
  return Folding::is_line_hidden(buf.fold_ranges, line);
}

int Editor::buffer_line_for_visible_row(const FileBuffer &buf, int first_line,
                                        int row) const {
  int line = Folding::buffer_line_for_visible_offset(
      buf.fold_ranges, first_line, row, (int)buf.line_count());
  if (line >= 0) {
    return line;
  }
  for (int fallback_row = row - 1; fallback_row >= 0; fallback_row--) {
    line = Folding::buffer_line_for_visible_offset(
        buf.fold_ranges, first_line, fallback_row, (int)buf.line_count());
    if (line >= 0) {
      return line;
    }
  }
  return -1;
}
