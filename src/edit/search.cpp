#include "editor.h"
#include "python_bridge/api.h"
#include <algorithm>
#include <cctype>
#include <regex>

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

std::string search_flags(bool case_sensitive, bool whole_word, bool regex) {
  std::string flags = case_sensitive ? "Aa" : "aa";
  flags += whole_word ? ",W" : ",w";
  if (regex) {
    flags += ",.*";
  }
  return flags;
}

std::regex make_search_regex(const std::string &pattern, bool case_sensitive) {
  auto flags = std::regex::ECMAScript;
  if (!case_sensitive) {
    flags |= std::regex::icase;
  }
  return std::regex(pattern, flags);
}

bool normalize_non_empty_selection(const FileBuffer &buf, Cursor &start,
                                   Cursor &end) {
  if (!buf.selection.active) {
    return false;
  }
  start = buf.selection.start;
  end = buf.selection.end;
  if (start.y > end.y || (start.y == end.y && start.x > end.x)) {
    std::swap(start, end);
  }
  if (start.y == end.y && start.x == end.x) {
    return false;
  }
  if (buf.line_count() == 0) {
    return false;
  }
  start.y = std::clamp(start.y, 0, (int)buf.line_count() - 1);
  end.y = std::clamp(end.y, 0, (int)buf.line_count() - 1);
  start.x = std::clamp(start.x, 0, (int)buf.line(start.y).size());
  end.x = std::clamp(end.x, 0, (int)buf.line(end.y).size());
  return start.y != end.y || start.x < end.x;
}
} // namespace

void Editor::clear_search_scope() {
  search_scoped_to_selection = false;
  search_scope_start = {0, 0};
  search_scope_end = {0, 0};
}

void Editor::open_search() {
  if (show_search) {
    search_focus_replace = false;
    needs_redraw = true;
    return;
  }
  toggle_search();
}

void Editor::toggle_search() {
  show_search = !show_search;
  if (!show_search) {
    clear_search_scope();
    return;
  }

  clear_search_scope();
  search_focus_replace = false;
  auto &buf = get_buffer();
  if (buf.selection.active && buf.selection.start.y == buf.selection.end.y) {
    Cursor start = buf.selection.start;
    Cursor end = buf.selection.end;
    if (start.x > end.x) {
      std::swap(start, end);
    }
    if (start.y >= 0 && start.y < (int)buf.line_count()) {
      const std::string &line = buf.line(start.y);
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
  needs_redraw = true;
}

bool Editor::open_scoped_replace_from_selection() {
  auto &buf = get_buffer();
  Cursor start;
  Cursor end;
  if (!normalize_non_empty_selection(buf, start, end)) {
    return false;
  }

  search_scoped_to_selection = true;
  search_scope_start = start;
  search_scope_end = end;
  show_search = true;
  search_replace_visible = true;
  search_focus_replace = false;
  search_results.clear();
  search_result_index = -1;

  if (start.y == end.y) {
    const std::string &line = buf.line(start.y);
    if (end.x > start.x) {
      search_query = line.substr((size_t)start.x,
                                 (size_t)(end.x - start.x));
    }
  } else {
    search_query.clear();
  }

  if (!search_query.empty()) {
    perform_search();
  } else {
    set_message("Find/replace in selection");
  }
  needs_redraw = true;
  return true;
}

void Editor::perform_search() {
  auto &buf = get_buffer();
  const int cursor_y = buf.cursor.y;
  const int cursor_x = buf.cursor.x;

  auto match_in_scope = [&](int line_idx, int col, int len) {
    if (!search_scoped_to_selection) {
      return true;
    }
    if (line_idx < search_scope_start.y || line_idx > search_scope_end.y) {
      return false;
    }
    const int end_col = col + len;
    if (line_idx == search_scope_start.y && col < search_scope_start.x) {
      return false;
    }
    if (line_idx == search_scope_end.y && end_col > search_scope_end.x) {
      return false;
    }
    return true;
  };

  search_results.clear();
  search_result_index = -1;

  if (search_query.empty()) {
    set_message("Search cleared [" +
                search_flags(search_case_sensitive, search_whole_word,
                             search_regex) +
                "]");
    needs_redraw = true;
    return;
  }

  if (search_regex) {
    std::regex re;
    try {
      re = make_search_regex(search_query, search_case_sensitive);
    } catch (const std::regex_error &e) {
      set_message(std::string("Regex error: ") + e.what());
      needs_redraw = true;
      return;
    }

    for (size_t line_idx = 0; line_idx < buf.line_count(); line_idx++) {
      const std::string &line = buf.line(line_idx);
      auto begin = std::sregex_iterator(line.begin(), line.end(), re);
      auto end = std::sregex_iterator();
      for (auto it = begin; it != end; ++it) {
        int pos = (int)it->position();
        int len = std::max(1, (int)it->length());
        if (search_whole_word &&
            !is_whole_word_match(line, (size_t)pos, (size_t)len)) {
          continue;
        }
        if (!match_in_scope((int)line_idx, pos, len)) {
          continue;
        }
        search_results.push_back({(int)line_idx, pos, len});
      }
    }
  } else {
    std::string query_cmp = search_case_sensitive ? search_query
                                                  : to_lower_ascii(search_query);
    const size_t query_len = search_query.size();

    for (size_t i = 0; i < buf.line_count(); i++) {
      const std::string &original_line = buf.line(i);
      std::string line_cmp =
          search_case_sensitive ? original_line : to_lower_ascii(original_line);

      size_t pos = 0;
      while ((pos = line_cmp.find(query_cmp, pos)) != std::string::npos) {
        if (search_whole_word &&
            !is_whole_word_match(original_line, pos, query_len)) {
          pos++;
          continue;
        }
        if (!match_in_scope((int)i, (int)pos, (int)query_len)) {
          pos += std::max<size_t>(1, query_len);
          continue;
        }
        search_results.push_back({(int)i, (int)pos, (int)query_len});
        pos += std::max<size_t>(1, query_len);
      }
    }
  }

  if (search_results.empty()) {
    set_message("No matches [" +
                search_flags(search_case_sensitive, search_whole_word,
                             search_regex) +
                (search_scoped_to_selection ? ",Sel" : "") +
                "]");
    needs_redraw = true;
    return;
  }

  SearchMatch cursor_match{cursor_y, cursor_x, 0};
  auto it = std::lower_bound(search_results.begin(), search_results.end(),
                             cursor_match);
  search_result_index =
      (it == search_results.end()) ? 0 : (int)(it - search_results.begin());

  buf.cursor.y = search_results[search_result_index].line;
  buf.cursor.x = search_results[search_result_index].col;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();

  set_message(std::to_string(search_results.size()) + " match(es) [" +
              search_flags(search_case_sensitive, search_whole_word,
                           search_regex) +
              (search_scoped_to_selection ? ",Sel" : "") +
              "]");
  needs_redraw = true;
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
  buf.cursor.y = search_results[search_result_index].line;
  buf.cursor.x = search_results[search_result_index].col;
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
  buf.cursor.y = search_results[search_result_index].line;
  buf.cursor.x = search_results[search_result_index].col;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();

  const bool wrapped = prev_index >= 0 && search_result_index >= prev_index;
  set_message(std::to_string(search_result_index + 1) + "/" +
              std::to_string(search_results.size()) +
              (wrapped ? " (wrapped)" : ""));
}

bool Editor::replace_current_search_match() {
  if (search_query.empty() || search_results.empty() ||
      search_result_index < 0 ||
      search_result_index >= (int)search_results.size()) {
    perform_search();
    return false;
  }

  auto &buf = get_buffer();
  if (buf.is_lazy()) {
    buf.materialize();
  }

  SearchMatch match = search_results[search_result_index];
  if (match.line < 0 || match.line >= (int)buf.lines.size()) {
    return false;
  }

  std::string replacement = search_replace_text;
  if (search_regex) {
    try {
      std::regex re = make_search_regex(search_query, search_case_sensitive);
      const std::string &matched =
          buf.lines[match.line].substr((size_t)match.col, (size_t)match.len);
      replacement = std::regex_replace(matched, re, search_replace_text);
    } catch (const std::regex_error &e) {
      set_message(std::string("Regex error: ") + e.what());
      return false;
    }
  }

  save_state();
  std::string &line = buf.lines[match.line];
  match.col = std::clamp(match.col, 0, (int)line.size());
  match.len = std::clamp(match.len, 0, (int)line.size() - match.col);
  line.replace((size_t)match.col, (size_t)match.len, replacement);
  buf.cursor = {match.col + (int)replacement.size(), match.line};
  buf.preferred_x = buf.cursor.x;
  buf.modified = true;
  buf.selection.active = false;
  if (search_scoped_to_selection && match.line == search_scope_end.y) {
    search_scope_end.x += (int)replacement.size() - match.len;
    search_scope_end.x =
        std::clamp(search_scope_end.x, 0, (int)line.size());
  }

  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }

  perform_search();
  find_next();
  needs_redraw = true;
  return true;
}

bool Editor::replace_all_search_matches() {
  if (search_query.empty()) {
    return false;
  }
  perform_search();
  if (search_results.empty()) {
    return false;
  }

  auto &buf = get_buffer();
  if (buf.is_lazy()) {
    buf.materialize();
  }

  std::regex re;
  if (search_regex) {
    try {
      re = make_search_regex(search_query, search_case_sensitive);
    } catch (const std::regex_error &e) {
      set_message(std::string("Regex error: ") + e.what());
      return false;
    }
  }

  save_state();
  int total = 0;
  for (int i = (int)search_results.size() - 1; i >= 0; i--) {
    SearchMatch match = search_results[i];
    if (match.line < 0 || match.line >= (int)buf.lines.size()) {
      continue;
    }
    std::string &line = buf.lines[match.line];
    match.col = std::clamp(match.col, 0, (int)line.size());
    match.len = std::clamp(match.len, 0, (int)line.size() - match.col);
    std::string replacement = search_replace_text;
    if (search_regex) {
      const std::string matched =
          line.substr((size_t)match.col, (size_t)match.len);
      replacement = std::regex_replace(matched, re, search_replace_text);
    }
    line.replace((size_t)match.col, (size_t)match.len, replacement);
    total++;
  }

  if (total <= 0) {
    set_message("No matches found");
    return false;
  }

  buf.modified = true;
  buf.selection.active = false;
  clear_search_scope();
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
  perform_search();
  set_message("Replaced " + std::to_string(total) + " occurrence(s)");
  needs_redraw = true;
  return true;
}

void Editor::handle_search_panel(int ch, bool is_ctrl, bool is_shift,
                                 bool /*is_alt*/) {
  if (ch == 27) {
    show_search = false;
    clear_search_scope();
    needs_redraw = true;
    set_message("");
    return;
  }

  if (is_ctrl && (ch == 'f' || ch == 'F')) {
    find_next();
    needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'h' || ch == 'H')) {
    search_replace_visible = !search_replace_visible;
    search_focus_replace = search_replace_visible;
    needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'r' || ch == 'R')) {
    if (is_shift) {
      replace_all_search_matches();
    } else {
      replace_current_search_match();
    }
    needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'w' || ch == 'W')) {
    search_whole_word = !search_whole_word;
    perform_search();
    needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'e' || ch == 'E')) {
    search_regex = !search_regex;
    perform_search();
    needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'l' || ch == 'L')) {
    if (search_focus_replace) {
      search_replace_text.clear();
    } else {
      search_query.clear();
      search_results.clear();
      search_result_index = -1;
    }
    set_message("Search cleared [" +
                search_flags(search_case_sensitive, search_whole_word,
                             search_regex) +
                "]");
    needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13 || ch == 1009) {
    if (is_shift && (ch == '\n' || ch == 13)) {
      find_prev();
    } else {
      find_next();
    }
    needs_redraw = true;
    return;
  }

  if (ch == 1008) {
    find_prev();
    needs_redraw = true;
    return;
  }

  if (ch == 127 || ch == 8) {
    std::string &target =
        search_focus_replace ? search_replace_text : search_query;
    if (!target.empty()) {
      target.pop_back();
      if (!search_focus_replace) {
        perform_search();
      }
      needs_redraw = true;
    }
    return;
  }

  if (ch == '\t' || ch == 9) {
    if (search_replace_visible) {
      search_focus_replace = !search_focus_replace;
    } else {
      search_case_sensitive = !search_case_sensitive;
      perform_search();
    }
    needs_redraw = true;
    return;
  }

  if (ch < 32 || ch > 126) {
    return;
  }

  std::string &target =
      search_focus_replace ? search_replace_text : search_query;
  target += (char)ch;
  if (!search_focus_replace) {
    perform_search();
  }
  needs_redraw = true;
}
