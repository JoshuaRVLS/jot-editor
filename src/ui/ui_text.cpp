#include "ui_text.h"

#include <cctype>

int ui_utf8_char_len(const std::string &text, int i) {
  if (i < 0 || i >= (int)text.size())
    return 0;
  const unsigned char c = (unsigned char)text[(size_t)i];
  if ((c & 0x80) == 0)
    return 1;
  if ((c & 0xE0) == 0xC0)
    return 2;
  if ((c & 0xF0) == 0xE0)
    return 3;
  if ((c & 0xF8) == 0xF0)
    return 4;
  return 0;
}

bool ui_is_valid_utf8_sequence(const std::string &text) {
  if (text.empty())
    return false;

  const unsigned char *p = (const unsigned char *)text.data();
  int n = (int)text.size();
  if (n == 1) {
    return (p[0] & 0x80) == 0;
  }
  if (n == 2) {
    if ((p[0] & 0xE0) != 0xC0)
      return false;
    return (p[1] & 0xC0) == 0x80;
  }
  if (n == 3) {
    if ((p[0] & 0xF0) != 0xE0)
      return false;
    return (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80;
  }
  if (n == 4) {
    if ((p[0] & 0xF8) != 0xF0)
      return false;
    return (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
           (p[3] & 0xC0) == 0x80;
  }
  return false;
}

std::string ui_sanitized_cell_text(const std::string &text) {
  if (text.empty())
    return " ";
  if (ui_is_valid_utf8_sequence(text))
    return text;
  return "?";
}

int ui_cell_count(const std::string &text) {
  int cells = 0;
  int i = 0;
  while (i < (int)text.size()) {
    int len = ui_utf8_char_len(text, i);
    if (len <= 0 || i + len > (int)text.size()) {
      i++;
    } else {
      i += len;
    }
    cells++;
  }
  return cells;
}

std::string ui_take_cells(const std::string &text, int max_cells) {
  if (max_cells <= 0)
    return "";

  std::string out;
  int cells = 0;
  int i = 0;
  while (i < (int)text.size() && cells < max_cells) {
    int len = ui_utf8_char_len(text, i);
    if (len <= 0 || i + len > (int)text.size()) {
      out += "?";
      i++;
    } else {
      out.append(text, (size_t)i, (size_t)len);
      i += len;
    }
    cells++;
  }
  return out;
}

std::string ui_truncate_cells(const std::string &text, int max_cells) {
  if (max_cells <= 0)
    return "";
  if (ui_cell_count(text) <= max_cells)
    return text;
  if (max_cells <= 2)
    return ui_take_cells(text, max_cells);
  return ui_take_cells(text, max_cells - 2) + "..";
}

std::string ui_truncate_left_cells(const std::string &text, int max_cells) {
  if (max_cells <= 0)
    return "";
  if (ui_cell_count(text) <= max_cells)
    return text;
  if (max_cells <= 2)
    return ui_take_cells(text, max_cells);

  int keep = max_cells - 2;
  int total = ui_cell_count(text);
  int skip = total - keep;
  int cells = 0;
  int i = 0;
  while (i < (int)text.size() && cells < skip) {
    int len = ui_utf8_char_len(text, i);
    if (len <= 0 || i + len > (int)text.size()) {
      i++;
    } else {
      i += len;
    }
    cells++;
  }
  return ".." + ui_take_cells(text.substr((size_t)i), keep);
}

std::string ui_one_line(std::string text) {
  std::string out;
  out.reserve(text.size());
  bool last_space = false;
  for (char c : text) {
    unsigned char uc = (unsigned char)c;
    if (c == '\n' || c == '\r' || c == '\t' || std::isspace(uc)) {
      if (!last_space && !out.empty()) {
        out.push_back(' ');
        last_space = true;
      }
      continue;
    }
    out.push_back(c);
    last_space = false;
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}
