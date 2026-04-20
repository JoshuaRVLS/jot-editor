#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>
#include <regex>

namespace {
bool is_word_char(char c) {
  unsigned char uc = (unsigned char)c;
  return std::isalnum(uc) || c == '_';
}

std::string to_lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

int replace_in_line(std::string &line, const std::string &needle,
                    const std::string &replacement, bool case_sensitive,
                    bool whole_word) {
  if (needle.empty()) {
    return 0;
  }

  int count = 0;
  size_t pos = 0;
  const std::string needle_cmp = case_sensitive ? needle : to_lower_copy(needle);
  while (pos <= line.size()) {
    size_t found = std::string::npos;
    if (case_sensitive) {
      found = line.find(needle, pos);
    } else {
      std::string line_lc = to_lower_copy(line);
      found = line_lc.find(needle_cmp, pos);
    }
    if (found == std::string::npos) {
      break;
    }

    if (whole_word) {
      bool left_ok = (found == 0) || !is_word_char(line[found - 1]);
      size_t right_pos = found + needle.size();
      bool right_ok = (right_pos >= line.size()) || !is_word_char(line[right_pos]);
      if (!(left_ok && right_ok)) {
        pos = found + 1;
        continue;
      }
    }

    line.replace(found, needle.size(), replacement);
    pos = found + replacement.size();
    count++;
  }
  return count;
}
} // namespace

void Editor::replace_all_text(const std::string &needle,
                              const std::string &replacement,
                              bool case_sensitive, bool whole_word) {
  if (needle.empty()) {
    set_message("Usage: needle cannot be empty");
    return;
  }

  auto &buf = get_buffer();
  save_state();
  int total = 0;
  for (auto &line : buf.lines) {
    total += replace_in_line(line, needle, replacement, case_sensitive, whole_word);
  }

  if (total <= 0) {
    set_message("No matches found");
    return;
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Replaced " + std::to_string(total) + " occurrence(s)");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::replace_all_regex(const std::string &pattern,
                               const std::string &replacement) {
  if (pattern.empty()) {
    set_message("Usage: regex pattern cannot be empty");
    return;
  }

  std::regex re;
  try {
    re = std::regex(pattern, std::regex::ECMAScript);
  } catch (const std::regex_error &e) {
    set_message(std::string("Regex error: ") + e.what());
    return;
  }

  auto &buf = get_buffer();
  save_state();
  int changed_lines = 0;
  for (auto &line : buf.lines) {
    if (!std::regex_search(line, re)) {
      continue;
    }
    line = std::regex_replace(line, re, replacement);
    changed_lines++;
  }

  if (changed_lines <= 0) {
    set_message("No regex matches found");
    return;
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Regex replaced in " + std::to_string(changed_lines) + " line(s)");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
