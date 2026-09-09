// Shared overlay text helpers: single-line normalization, clipping, and
// syntax-preview colors.
#include "editor.h"
#include "render/overlay_internal.h"
#include "ui/text.h"
#include <cctype>
#include <string>

namespace overlay_internal
{
std::string one_line_text(const std::string &text)
{
  std::string out;
  out.reserve(text.size());
  bool last_space = false;
  for (char c : text)
  {
    unsigned char uc = (unsigned char)c;
    if (c == '\n' || c == '\r' || c == '\t' || std::isspace(uc))
    {
      if (!last_space && !out.empty())
      {
        out.push_back(' ');
        last_space = true;
      }
      continue;
    }
    out.push_back(c);
    last_space = false;
  }
  while (!out.empty() && out.back() == ' ')
  {
    out.pop_back();
  }
  return out;
}

std::string clip_text(const std::string &text, int max_w)
{
  if (max_w <= 0)
  {
    return "";
  }
  if (ui_cell_count(text) <= max_w)
  {
    return text;
  }
  if (max_w <= 3)
  {
    return ui_take_cells(text, max_w);
  }
  return ui_take_cells(text, max_w - 3) + "...";
}

std::string clip_path_left(const std::string &text, int max_w)
{
  if (max_w <= 0)
  {
    return "";
  }
  if (ui_cell_count(text) <= max_w)
  {
    return text;
  }
  if (max_w <= 3)
  {
    return ui_take_cells(text, max_w);
  }
  return "..." + ui_truncate_left_cells(text, max_w - 3);
}

int syntax_preview_color(const Theme &theme, int token)
{
  switch (token)
  {
  case 1:
    return theme.fg_keyword;
  case 2:
    return theme.fg_string;
  case 3:
    return theme.fg_comment;
  case 4:
    return theme.fg_number;
  case 5:
    return theme.fg_type;
  case 6:
    return theme.fg_function;
  default:
    return theme.fg_telescope_preview;
  }
}
} // namespace overlay_internal
