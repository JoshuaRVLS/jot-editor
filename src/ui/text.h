#ifndef UI_TEXT_H
#define UI_TEXT_H

#include <string>

int ui_utf8_char_len(const std::string &text, int i);
bool ui_is_valid_utf8_sequence(const std::string &text);
std::string ui_sanitized_cell_text(const std::string &text);

int ui_cell_count(const std::string &text);
std::string ui_take_cells(const std::string &text, int max_cells);
std::string ui_truncate_cells(const std::string &text, int max_cells);
std::string ui_truncate_left_cells(const std::string &text, int max_cells);
std::string ui_one_line(std::string text);
int ui_clamp_to_utf8_boundary(const std::string &text, int byte_index);
int ui_next_grapheme_boundary(const std::string &text, int byte_index);
int ui_prev_grapheme_boundary(const std::string &text, int byte_index);
std::string ui_normalize_nfc(const std::string &text);
std::string ui_normalize_nfd(const std::string &text);

#endif
