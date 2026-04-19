#include "editor.h"
#include <algorithm>
#include <cctype>

namespace {
std::string to_lower_ascii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return text;
}

bool is_word_char(unsigned char c) {
  return std::isalnum(c) || c == '_';
}

bool is_whole_word_match(const std::string &line, size_t pos, size_t len) {
  if (len == 0) {
    return false;
  }
  const bool has_prev = pos > 0;
  const bool has_next = (pos + len) < line.size();
  const bool prev_word =
      has_prev && is_word_char((unsigned char)line[pos - 1]);
  const bool next_word =
      has_next && is_word_char((unsigned char)line[pos + len]);
  return !prev_word && !next_word;
}

std::string search_flags(bool case_sensitive, bool whole_word) {
  return std::string(case_sensitive ? "Aa" : "aa") +
         (whole_word ? ",W" : ",w");
}
} // namespace

void Editor::toggle_search() {
  show_search = !show_search;
  if (!show_search) {
    return;
  }

  auto &buf = get_buffer();
  if (buf.selection.active && buf.selection.start.y == buf.selection.end.y) {
    Cursor start = buf.selection.start;
    Cursor end = buf.selection.end;
    if (start.x > end.x) {
      std::swap(start, end);
    }
    if (start.y >= 0 && start.y < (int)buf.lines.size()) {
      const std::string &line = buf.lines[start.y];
      int from = std::clamp(start.x, 0, (int)line.size());
      int to = std::clamp(end.x, 0, (int)line.size());
      if (to > from) {
        search_query = line.substr((size_t)from, (size_t)(to - from));
      }
    }
  }

  if (!search_query.empty()) {
    perform_search();
  } else {
    search_results.clear();
    search_result_index = -1;
  }
}

void Editor::perform_search() {
  auto &buf = get_buffer();
  const int cursor_y = buf.cursor.y;
  const int cursor_x = buf.cursor.x;

  search_results.clear();
  search_result_index = -1;

  if (search_query.empty()) {
    set_message("Search cleared [" +
                search_flags(search_case_sensitive, search_whole_word) + "]");
    return;
  }

  std::string query_cmp = search_case_sensitive ? search_query
                                                : to_lower_ascii(search_query);
  const size_t query_len = search_query.size();

  for (size_t i = 0; i < buf.lines.size(); i++) {
    const std::string &original_line = buf.lines[i];
    std::string line_cmp =
        search_case_sensitive ? original_line : to_lower_ascii(original_line);

    size_t pos = 0;
    while ((pos = line_cmp.find(query_cmp, pos)) != std::string::npos) {
      if (search_whole_word &&
          !is_whole_word_match(original_line, pos, query_len)) {
        pos++;
        continue;
      }
      search_results.push_back({(int)i, (int)pos});
      pos++;
    }
  }

  if (search_results.empty()) {
    set_message("No matches [" +
                search_flags(search_case_sensitive, search_whole_word) + "]");
    needs_redraw = true;
    return;
  }

  auto it = std::lower_bound(search_results.begin(), search_results.end(),
                             std::make_pair(cursor_y, cursor_x));
  search_result_index =
      (it == search_results.end()) ? 0 : (int)(it - search_results.begin());

  buf.cursor.y = search_results[search_result_index].first;
  buf.cursor.x = search_results[search_result_index].second;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();

  set_message(std::to_string(search_results.size()) + " match(es) [" +
              search_flags(search_case_sensitive, search_whole_word) +
              "]  Tab:case Ctrl+W:word");
}

void Editor::find_next() {
  if (search_results.empty()) {
    perform_search();
    return;
  }

  const int prev_index = search_result_index;
  const int count = (int)search_results.size();
  if (search_result_index < 0) {
    search_result_index = 0;
  } else {
    search_result_index = (search_result_index + 1) % count;
  }

  auto &buf = get_buffer();
  buf.cursor.y = search_results[search_result_index].first;
  buf.cursor.x = search_results[search_result_index].second;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();

  const bool wrapped = prev_index >= 0 && search_result_index <= prev_index;
  set_message(std::to_string(search_result_index + 1) + "/" +
              std::to_string(search_results.size()) +
              (wrapped ? " (wrapped)" : ""));
}

void Editor::find_prev() {
  if (search_results.empty()) {
    perform_search();
    return;
  }

  const int prev_index = search_result_index;
  const int count = (int)search_results.size();
  if (search_result_index <= 0) {
    search_result_index = count - 1;
  } else {
    search_result_index--;
  }

  auto &buf = get_buffer();
  buf.cursor.y = search_results[search_result_index].first;
  buf.cursor.x = search_results[search_result_index].second;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();

  const bool wrapped = prev_index >= 0 && search_result_index >= prev_index;
  set_message(std::to_string(search_result_index + 1) + "/" +
              std::to_string(search_results.size()) +
              (wrapped ? " (wrapped)" : ""));
}

void Editor::handle_search_panel(int ch, bool is_ctrl, bool is_shift,
                                 bool /*is_alt*/) {
  if (ch == 27) { // Escape
    show_search = false;
    needs_redraw = true;
    set_message("");
    return;
  }

  if (is_ctrl && (ch == 'f' || ch == 'F')) {
    find_next();
    needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'w' || ch == 'W')) {
    search_whole_word = !search_whole_word;
    perform_search();
    needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'l' || ch == 'L')) {
    search_query.clear();
    search_results.clear();
    search_result_index = -1;
    set_message("Search cleared [" +
                search_flags(search_case_sensitive, search_whole_word) + "]");
    needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13 || ch == 1009) { // Enter or Down -> Next
    if (is_shift && (ch == '\n' || ch == 13)) {
      find_prev();
    } else {
      find_next();
    }
    needs_redraw = true;
    return;
  }

  if (ch == 1008) { // Up -> Prev
    find_prev();
    needs_redraw = true;
    return;
  }

  if (ch == 127 || ch == 8) { // Backspace
    if (!search_query.empty()) {
      search_query.pop_back();
      perform_search();
      needs_redraw = true;
    }
    return;
  }

  if (ch == '\t' || ch == 9) { // Toggle case sensitivity
    search_case_sensitive = !search_case_sensitive;
    perform_search();
    needs_redraw = true;
    return;
  }

  if (ch < 32 || ch > 126) {
    return; // Ignore non-printable
  }

  search_query += (char)ch;
  perform_search();
  needs_redraw = true;
}
