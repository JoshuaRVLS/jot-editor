#include "editor.h"
#include "column_utils.h"
#include "folding.h"
#include <cctype>

void Editor::move_cursor(int dx, int dy, bool extend_selection) {
  auto &buf = get_buffer();

  if (!extend_selection && buf.selection.active) {
    clear_selection();
  }

  if (extend_selection && !buf.selection.active) {
    buf.selection.start = buf.cursor;
    buf.selection.active = true;
  }

  int max_y = buf.line_count() > 0 ? (int)buf.line_count() - 1 : 0;

  if (dy != 0 && dx == 0) {
    int target_y = Folding::advance_visible_lines(buf.fold_ranges, buf.cursor.y,
                                                  dy, (int)buf.line_count());
    int desired_x = std::max(buf.cursor.x, std::max(0, buf.preferred_x));
    buf.cursor.y = target_y;
    int line_len = (int)buf.line(buf.cursor.y).length();
    buf.cursor.x = std::min(desired_x, line_len);
  } else {
    buf.cursor.y =
        std::max(0, std::min(max_y, buf.cursor.y + dy));
    buf.cursor.x = std::max(0, buf.cursor.x + dx);
    // Horizontal movement (or mixed dx/dy) sets new preferred column.
    buf.preferred_x = buf.cursor.x;
  }
  clamp_cursor(get_pane().buffer_id);
  if (dy == 0 && dx == 0) {
    buf.preferred_x = buf.cursor.x;
  }
  ensure_cursor_visible();

  if (extend_selection) {
    buf.selection.end = buf.cursor;
  }
  needs_redraw = true;
} 

void Editor::clamp_cursor(int buffer_id) {
  auto &buf = get_buffer(buffer_id);
  if (buf.line_count() == 0) {
    buf.cursor.y = 0;
    buf.cursor.x = 0;
    return;
  }
  if (buf.cursor.y >= (int)buf.line_count())
    buf.cursor.y = buf.line_count() - 1;
  if (buf.cursor.y < 0)
    buf.cursor.y = 0;
  while (buf.cursor.y > 0 && Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
    buf.cursor.y--;
  }
  int line_len = buf.line(buf.cursor.y).length();
  buf.cursor.x = std::max(0, std::min(line_len, buf.cursor.x));
}

void Editor::ensure_cursor_visible(bool adjust_horizontal) {
  if (panes.empty())
    return;
  auto &pane = get_pane();
  auto &buf = get_buffer(pane.buffer_id);

  int viewport_h = pane.h - tab_height - 1;
  if (viewport_h < 1)
    viewport_h = 1;

  buf.scroll_offset = Folding::clamp_scroll_offset(
      buf.fold_ranges, buf.scroll_offset, viewport_h, (int)buf.line_count());

  if (buf.cursor.y < buf.scroll_offset) {
    buf.scroll_offset = buf.cursor.y;
  } else {
    int last_visible =
        Folding::buffer_line_for_visible_offset(buf.fold_ranges,
                                                buf.scroll_offset,
                                                viewport_h - 1,
                                                (int)buf.line_count());
    if (buf.cursor.y > last_visible) {
      buf.scroll_offset = buf.cursor.y;
      for (int i = 1; i < viewport_h; i++) {
        buf.scroll_offset =
            Folding::previous_visible_line(buf.fold_ranges, buf.scroll_offset);
      }
    }
  }

  if (adjust_horizontal) {
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
      draw_w = std::max(1, draw_w - minimap_width);
    int viewport_w = draw_w - 9;
    if (viewport_w < 1)
      viewport_w = 1;

    if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.line_count()) {
      const std::string &line = buf.line(buf.cursor.y);
      int cursor_visual =
          compute_visual_column(line, buf.cursor.x, tab_size);
      int scroll_visual =
          compute_visual_column(line, buf.scroll_x, tab_size);
      if (cursor_visual < scroll_visual) {
        buf.scroll_x = buf.cursor.x;
      } else if (cursor_visual >= scroll_visual + viewport_w) {
        int cur = buf.scroll_x;
        int cur_visual = scroll_visual;
        while (cur < (int)line.size()) {
          int next_visual =
              cur_visual + ((line[cur] == '\t')
                                ? tab_advance(cur_visual, tab_size)
                                : 1);
          if (next_visual > cursor_visual - viewport_w + 1)
            break;
          cur_visual = next_visual;
          cur++;
        }
        buf.scroll_x = cur;
      }
    } else {
      if (buf.cursor.x < buf.scroll_x) {
        buf.scroll_x = buf.cursor.x;
      } else if (buf.cursor.x >= buf.scroll_x + viewport_w) {
        buf.scroll_x = buf.cursor.x - viewport_w + 1;
      }
    }
  }

  if (buf.scroll_x < 0)
    buf.scroll_x = 0;
  if (buf.scroll_offset < 0)
    buf.scroll_offset = 0;
}

void Editor::move_word_forward(bool extend_selection) {
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  int len = buf.line(buf.cursor.y).length();
  while (buf.cursor.x < len &&
         !std::isalnum(buf.line(buf.cursor.y)[buf.cursor.x]))
    buf.cursor.x++;
  while (buf.cursor.x < len &&
         std::isalnum(buf.line(buf.cursor.y)[buf.cursor.x]))
    buf.cursor.x++;
  clamp_cursor(get_pane().buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection) {
    if (!buf.selection.active) {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_word_backward(bool extend_selection) {
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  buf.cursor.x--;
  if (buf.cursor.x < 0) {
    if (buf.cursor.y > 0) {
      buf.cursor.y--;
      buf.cursor.x = buf.line(buf.cursor.y).length();
    } else {
      buf.cursor.x = 0;
    }
  } else {
    while (buf.cursor.x > 0 &&
           !std::isalnum(buf.line(buf.cursor.y)[buf.cursor.x]))
      buf.cursor.x--;
    while (buf.cursor.x > 0 &&
           std::isalnum(buf.line(buf.cursor.y)[buf.cursor.x - 1]))
      buf.cursor.x--;
  }
  clamp_cursor(get_pane().buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection) {
    if (!buf.selection.active) {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_line_smart_start(bool extend_selection) {
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;

  const std::string &line = buf.line(buf.cursor.y);
  int first_non_ws = 0;
  while (first_non_ws < (int)line.size() &&
         (line[first_non_ws] == ' ' || line[first_non_ws] == '\t')) {
    first_non_ws++;
  }
  if (first_non_ws >= (int)line.size()) {
    first_non_ws = 0;
  }

  int target_x = (buf.cursor.x == first_non_ws) ? 0 : first_non_ws;
  buf.cursor.x = target_x;
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();

  if (extend_selection) {
    if (!buf.selection.active) {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_line_start(bool extend_selection) {
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  buf.cursor.x = 0;
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection) {
    if (!buf.selection.active) {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_line_end(bool extend_selection) {
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  buf.cursor.x = buf.line(buf.cursor.y).length();
  clamp_cursor(get_pane().buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection) {
    if (!buf.selection.active) {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_file_start(bool extend_selection) {
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  buf.cursor.y = 0;
  buf.cursor.x = 0;
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection) {
    if (!buf.selection.active) {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_file_end(bool extend_selection) {
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  if (buf.line_count() == 0) {
    buf.cursor.y = 0;
    buf.cursor.x = 0;
  } else {
    buf.cursor.y = buf.line_count() - 1;
    buf.cursor.x = buf.line(buf.cursor.y).length();
  }
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection) {
    if (!buf.selection.active) {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}
