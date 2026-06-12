#include "editor.h"
#include "folding.h"

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
  buf.fold_ranges[index].collapsed = !buf.fold_ranges[index].collapsed;
  if (buf.fold_ranges[index].collapsed &&
      Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
    buf.cursor.y = buf.fold_ranges[index].start_line;
    clamp_cursor(get_pane().buffer_id);
  }
  ensure_cursor_visible();
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
  buf.fold_ranges[index].collapsed = true;
  if (Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
    buf.cursor.y = buf.fold_ranges[index].start_line;
    clamp_cursor(get_pane().buffer_id);
  }
  ensure_cursor_visible();
  set_message("Folded block");
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
  return Folding::buffer_line_for_visible_offset(
      buf.fold_ranges, first_line, row, (int)buf.line_count());
}
