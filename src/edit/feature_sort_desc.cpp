#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>

void Editor::sort_selected_lines_desc() {
  auto &buf = get_buffer();
  if (!buf.selection.active) {
    set_message("Select lines first: :sortdesc");
    return;
  }

  int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  start_y = std::clamp(start_y, 0, (int)buf.lines.size() - 1);
  end_y = std::clamp(end_y, 0, (int)buf.lines.size() - 1);
  if (start_y >= end_y) {
    set_message("Select multiple lines to sort descending");
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
                     return la > lb;
                   });

  buf.modified = true;
  buf.cursor.y = start_y;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[start_y].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Sorted selected lines (desc)");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
