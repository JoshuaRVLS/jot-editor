#include "quote_text_object.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace QuoteTextObject {
namespace {
bool is_escaped(const std::string &line, int index) {
  int slash_count = 0;
  for (int i = index - 1; i >= 0 && line[i] == '\\'; i--) {
    slash_count++;
  }
  return (slash_count % 2) == 1;
}
} // namespace

bool is_supported_quote(char quote) {
  return quote == '"' || quote == '\'' || quote == '`';
}

Range find_inner_range(const std::string &line, int cursor_x, char quote) {
  Range result;
  if (!is_supported_quote(quote) || line.size() < 2) {
    return result;
  }

  std::vector<int> quotes;
  for (int i = 0; i < (int)line.size(); i++) {
    if (line[i] == quote && !is_escaped(line, i)) {
      quotes.push_back(i);
    }
  }
  if (quotes.size() < 2) {
    return result;
  }

  cursor_x = std::clamp(cursor_x, 0, (int)line.size());
  int best_open = -1;
  int best_close = -1;
  int best_distance = -1;

  for (size_t i = 0; i + 1 < quotes.size(); i += 2) {
    const int open = quotes[i];
    const int close = quotes[i + 1];
    const bool contains_cursor = cursor_x >= open && cursor_x <= close;
    const int distance = contains_cursor
                             ? -1
                             : std::min(std::abs(cursor_x - open),
                                        std::abs(cursor_x - close));
    if (best_open < 0 || distance < best_distance) {
      best_open = open;
      best_close = close;
      best_distance = distance;
    }
    if (contains_cursor) {
      break;
    }
  }

  if (best_open < 0 || best_close <= best_open) {
    return result;
  }

  result.found = true;
  result.open = best_open;
  result.close = best_close;
  result.inner_start = best_open + 1;
  result.inner_end = best_close;
  return result;
}

} // namespace QuoteTextObject
