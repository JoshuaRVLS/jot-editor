#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>

void Editor::increment_number_at_cursor(int delta) {
  auto &buf = get_buffer();
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size()) {
    set_message("No number at cursor");
    return;
  }

  std::string &line = buf.lines[buf.cursor.y];
  if (line.empty()) {
    set_message("No number at cursor");
    return;
  }

  int x = std::clamp(buf.cursor.x, 0, (int)line.size());
  int i = std::min(x, (int)line.size() - 1);
  if (!std::isdigit((unsigned char)line[i])) {
    if (i > 0 && std::isdigit((unsigned char)line[i - 1])) {
      i--;
    } else {
      set_message("No number at cursor");
      return;
    }
  }

  int start = i;
  while (start > 0 && std::isdigit((unsigned char)line[start - 1])) {
    start--;
  }
  if (start > 0 && (line[start - 1] == '-' || line[start - 1] == '+')) {
    if (start - 1 == 0 || !std::isalnum((unsigned char)line[start - 2])) {
      start--;
    }
  }

  int end = i + 1;
  while (end < (int)line.size() && std::isdigit((unsigned char)line[end])) {
    end++;
  }

  std::string old_num = line.substr((size_t)start, (size_t)(end - start));
  long long value = 0;
  try {
    value = std::stoll(old_num);
  } catch (...) {
    set_message("Invalid number format");
    return;
  }

  save_state();
  long long next = value + delta;
  std::string next_num = std::to_string(next);
  line.replace((size_t)start, (size_t)(end - start), next_num);
  buf.cursor.x = start + (int)next_num.size();
  buf.preferred_x = buf.cursor.x;
  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message(std::string(delta >= 0 ? "Incremented" : "Decremented") +
              " number to " + next_num);
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
