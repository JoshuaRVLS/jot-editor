#ifndef QUOTE_TEXT_OBJECT_H
#define QUOTE_TEXT_OBJECT_H

#include <string>

namespace QuoteTextObject {

struct Range {
  bool found = false;
  int open = -1;
  int close = -1;
  int inner_start = -1;
  int inner_end = -1;
};

bool is_supported_quote(char quote);
Range find_inner_range(const std::string &line, int cursor_x, char quote);

} // namespace QuoteTextObject

#endif
