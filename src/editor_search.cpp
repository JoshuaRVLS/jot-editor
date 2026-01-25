#include "editor.h"
#include <algorithm>

void Editor::toggle_search() {
  show_search = !show_search;
  if (show_search) {
    search_query = "";
    search_results.clear();
    search_result_index = -1;
  }
}

void Editor::perform_search() {
  auto &buf = get_buffer();
  search_results.clear();
  search_result_index = -1;

  if (search_query.empty())
    return;

  std::string query = search_query;
  if (!search_case_sensitive) {
    std::transform(query.begin(), query.end(), query.begin(), ::tolower);
  }

  for (size_t i = 0; i < buf.lines.size(); i++) {
    std::string line = buf.lines[i];
    if (!search_case_sensitive) {
      std::transform(line.begin(), line.end(), line.begin(), ::tolower);
    }

    size_t pos = 0;
    while ((pos = line.find(query, pos)) != std::string::npos) {
      search_results.push_back({i, (int)pos});
      pos++;
    }
  }

  if (!search_results.empty()) {
    search_result_index = 0;
    buf.cursor.y = search_results[0].first;
    buf.cursor.x = search_results[0].second;
    clamp_cursor(get_pane().buffer_id);
  }
}

void Editor::find_next() {
  if (search_results.empty()) {
    perform_search();
    return;
  }

  if (search_result_index < (int)search_results.size() - 1) {
    search_result_index++;
  } else {
    search_result_index = 0;
  }

  auto &buf = get_buffer();
  buf.cursor.y = search_results[search_result_index].first;
  buf.cursor.x = search_results[search_result_index].second;
  clamp_cursor(get_pane().buffer_id);
}

void Editor::find_prev() {
  if (search_results.empty())
    return;

  if (search_result_index > 0) {
    search_result_index--;
  } else {
    search_result_index = search_results.size() - 1;
  }

  auto &buf = get_buffer();
  buf.cursor.y = search_results[search_result_index].first;
  buf.cursor.x = search_results[search_result_index].second;
  clamp_cursor(get_pane().buffer_id);
}

void Editor::handle_search_panel(int ch) {
  if (ch == 27) { // Escape
    show_search = false;
    needs_redraw = true;
    set_message("");
    return;
  }

  if (ch == '\n' || ch == 1009) { // Enter or Down -> Next
    find_next();
    needs_redraw = true;
    return;
  }

  if (ch == 1008) { // Up -> Prev
    find_prev();
    needs_redraw = true;
    return;
  }

  if (ch == 127) { // Backspace
    if (!search_query.empty()) {
      search_query.pop_back();
      perform_search();
      needs_redraw = true;
    }
    return;
  }

  if (ch < 32 || ch > 126)
    return; // Ignore non-printable

  search_query += (char)ch;
  perform_search();
  needs_redraw = true;
}
