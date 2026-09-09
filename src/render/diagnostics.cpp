// Diagnostic decoration helpers for buffer rendering: severity colors,
// per-line lookup, dense-span coalescing, and compact label text.
#include "column_utils.h"
#include "editor.h"
#include "render/buffer_internal.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace buffer_internal
{
int diagnostic_severity_color(const Theme &theme, int severity)
{
  switch (severity)
  {
  case 1:
    return theme.fg_diagnostic_error;
  case 2:
    return theme.fg_diagnostic_warning;
  case 3:
    return theme.fg_diagnostic_info;
  case 4:
    return theme.fg_diagnostic_hint;
  default:
    return theme.fg_comment;
  }
}
std::string diagnostic_severity_label(int severity)
{
  switch (severity)
  {
  case 1:
    return "Error";
  case 2:
    return "Warning";
  case 3:
    return "Info";
  case 4:
    return "Hint";
  default:
    return "Diagnostic";
  }
}
bool diagnostic_covers_line(const Diagnostic &diag, int line)
{
  return line >= diag.line && line <= diag.end_line;
}
const Diagnostic *find_line_diagnostic(const FileBuffer &buf, int line, int cursor_col)
{
  const Diagnostic *best = nullptr;
  for (const auto &diag : buf.diagnostics)
  {
    if (!diagnostic_covers_line(diag, line))
    {
      continue;
    }

    bool contains_cursor = false;
    if (line == diag.line && line == diag.end_line)
    {
      contains_cursor = cursor_col >= diag.col && cursor_col <= diag.end_col;
    }
    else if (line == diag.line)
    {
      contains_cursor = cursor_col >= diag.col;
    }
    else if (line == diag.end_line)
    {
      contains_cursor = cursor_col <= diag.end_col;
    }
    else
    {
      contains_cursor = true;
    }

    if (contains_cursor)
    {
      if (!best || diag.severity < best->severity)
      {
        best = &diag;
      }
      continue;
    }

    if (!best)
    {
      best = &diag;
    }
  }
  return best;
}
void extend_diag_severity(FileBuffer &buf, int to_line)
{
  if (buf.diag_severity_dirty)
  {
    buf.diag_severity_by_line.clear();
    buf.diag_severity_built_upto = -1;
    buf.diag_wide_spans.clear();
    for (int i = 0; i < (int)buf.diagnostics.size(); i++)
    {
      const Diagnostic &d = buf.diagnostics[i];
      if (d.end_line - d.line + 1 > kDiagDenseSpanLimit)
      {
        buf.diag_wide_spans.push_back(i);
      }
    }
    buf.diag_severity_dirty = false;
  }
  if (to_line <= buf.diag_severity_built_upto)
  {
    return;
  }
  const int built = buf.diag_severity_built_upto;
  if ((int)buf.diag_severity_by_line.size() <= to_line)
  {
    buf.diag_severity_by_line.resize((size_t)to_line + 1, 0);
  }
  for (const auto &diag : buf.diagnostics)
  {
    const int span = diag.end_line - diag.line + 1;
    if (span > kDiagDenseSpanLimit || span <= 0)
    {
      continue; // wide spans are answered on lookup; malformed ones ignored
    }
    const int from = std::max(diag.line, built + 1);
    const int to = std::min(diag.end_line, to_line);
    if (from > to)
    {
      continue;
    }
    const unsigned char sev = (unsigned char)std::clamp(diag.severity, 1, 4);
    for (int line = from; line <= to; line++)
    {
      unsigned char &cell = buf.diag_severity_by_line[(size_t)line];
      if (cell == 0 || sev < cell)
      {
        cell = sev;
      }
    }
  }
  buf.diag_severity_built_upto = to_line;
}
int line_diagnostic_severity(FileBuffer &buf, int line)
{
  if (line < 0)
  {
    return 0;
  }
  extend_diag_severity(buf, line);
  int best = (line < (int)buf.diag_severity_by_line.size())
                 ? buf.diag_severity_by_line[(size_t)line]
                 : 0;
  for (int wide : buf.diag_wide_spans)
  {
    const Diagnostic &d = buf.diagnostics[(size_t)wide];
    if (d.line <= line && line <= d.end_line)
    {
      if (best == 0 || d.severity < best)
      {
        best = d.severity;
      }
    }
  }
  return best;
}
std::string compact_diagnostic_text(const std::string &text)
{
  std::string out;
  out.reserve(text.size());
  bool last_space = false;
  for (char ch : text)
  {
    const bool is_space = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
    if (is_space)
    {
      if (!last_space)
      {
        out.push_back(' ');
        last_space = true;
      }
      continue;
    }
    out.push_back(ch);
    last_space = false;
  }

  while (!out.empty() && out.front() == ' ')
  {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == ' ')
  {
    out.pop_back();
  }
  return out;
}
} // namespace buffer_internal
