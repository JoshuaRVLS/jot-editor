#include "base/shell_util.h"
#include "core/editor.h"
#include "lua/api.h"
#include "text_features.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef _WIN32
bool wayland_session()
{
  const char *display = std::getenv("WAYLAND_DISPLAY");
  return display != nullptr && display[0] != '\0';
}

bool write_wl_clipboard(const std::string &text)
{
  if (!shell_util::command_exists("wl-copy"))
    return false;
  FILE *pipe = shell_util::open_command_pipe("wl-copy" + shell_util::detached_redirect(), "w");
  if (!pipe)
    return false;

  fwrite(text.data(), 1, text.size(), pipe);
  return shell_util::close_command_pipe(pipe) == 0;
}

bool write_xclip_selection(const std::string &text)
{
  if (!shell_util::command_exists("xclip"))
    return false;
  FILE *pipe = shell_util::open_command_pipe(
      "xclip -selection clipboard -in" + shell_util::detached_redirect(), "w");
  if (!pipe)
    return false;
  fwrite(text.data(), 1, text.size(), pipe);
  return shell_util::close_command_pipe(pipe) == 0;
}

bool read_wl_clipboard(std::string &candidate)
{
  if (!shell_util::command_exists("wl-paste"))
    return false;
  FILE *pipe = shell_util::open_command_pipe("wl-paste --no-newline 2>/dev/null", "r");
  if (!pipe)
    return false;
  std::array<char, 4096> buffer{};
  candidate.clear();

  while (size_t n = fread(buffer.data(), 1, buffer.size(), pipe))
  {
    candidate.append(buffer.data(), n);
  }

  return shell_util::close_command_pipe(pipe) == 0 && !candidate.empty();
}

bool read_xclip_selection(std::string &candidate)
{
  if (!shell_util::command_exists("xclip"))
    return false;
  FILE *pipe = shell_util::open_command_pipe("xclip -selection clipboard -out 2>/dev/null", "r");
  if (!pipe)
    return false;
  std::array<char, 4096> buffer{};
  candidate.clear();

  while (size_t n = fread(buffer.data(), 1, buffer.size(), pipe))
  {
    candidate.append(buffer.data(), n);
  }

  return shell_util::close_command_pipe(pipe) == 0 && !candidate.empty();
}
#endif

bool write_xclip_clipboard(const std::string &text)
{
#ifdef _WIN32
  if (!OpenClipboard(nullptr))
    return false;
  EmptyClipboard();

  int wide_len = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
  if (wide_len <= 0)
  {
    CloseClipboard();
    return false;
  }
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (wide_len + 1) * sizeof(wchar_t));
  if (!mem)
  {
    CloseClipboard();
    return false;
  }
  wchar_t *wide = static_cast<wchar_t *>(GlobalLock(mem));
  MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide, wide_len);
  wide[wide_len] = L'\0';
  GlobalUnlock(mem);
  bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
  if (!ok)
  {
    GlobalFree(mem);
  }
  CloseClipboard();
  return ok;
#else
  if (wayland_session())
  {
    if (write_wl_clipboard(text))
      return true;
    return write_xclip_selection(text);
  }
  if (write_xclip_selection(text))
    return true;
  return write_wl_clipboard(text);
#endif
}

bool read_xclip_clipboard(std::string &out)
{
#ifdef _WIN32
  if (!OpenClipboard(nullptr))
    return false;
  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (!handle)
  {
    CloseClipboard();
    return false;
  }
  const wchar_t *wide = static_cast<const wchar_t *>(GlobalLock(handle));
  if (!wide)
  {
    CloseClipboard();
    return false;
  }
  int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 1)
  {
    GlobalUnlock(handle);
    CloseClipboard();
    return false;
  }
  std::string result((size_t)len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
  if (!result.empty() && result.back() == '\0')
  {
    result.pop_back();
  }
  GlobalUnlock(handle);
  CloseClipboard();
  out = std::move(result);
  return !out.empty();
#else
  std::string candidate;
  if (wayland_session())
  {
    if (read_wl_clipboard(candidate) || read_xclip_selection(candidate))
    {
      out = std::move(candidate);
      return true;
    }
    return false;
  }
  if (read_xclip_selection(candidate) || read_wl_clipboard(candidate))
  {
    out = std::move(candidate);
    return true;
  }
  return false;
#endif
}

// The external tools stay first where the session has them: they hand the
// selection to a helper process that keeps serving it after jot exits, which a
// frontend owning the selection itself cannot. The frontend's own connection is
// the fallback, so a GUI on a machine with neither xclip nor wl-copy still
// yanks somewhere the rest of the desktop can paste from.
void Editor::write_system_clipboard(const std::string &text)
{
  if (write_xclip_clipboard(text))
  {
    return;
  }
  if (ui)
  {
    ui->set_clipboard_text(text);
  }
}

namespace
{
  bool has_newline(const std::string &text)
  {
    return text.find('\n') != std::string::npos || text.find('\r') != std::string::npos;
  }

  std::string trim_left_copy(const std::string &line)
  {
    const size_t start = line.find_first_not_of(" \t");
    return start == std::string::npos ? "" : line.substr(start);
  }

  std::string leading_indent(const std::string &line)
  {
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
    {
      pos++;
    }
    return line.substr(0, pos);
  }

  int indent_columns(const std::string &indent, int tab_size)
  {
    int cols = 0;
    const int step = std::max(1, tab_size);
    for (char c : indent)
    {
      cols += (c == '\t') ? step : 1;
    }
    return cols;
  }

  std::string indent_from_columns(int columns, int tab_size, bool prefer_tabs)
  {
    columns = std::max(0, columns);
    const int step = std::max(1, tab_size);
    if (!prefer_tabs)
    {
      return std::string(columns, ' ');
    }
    const int tabs = columns / step;
    const int spaces = columns % step;
    return std::string(tabs, '\t') + std::string(spaces, ' ');
  }

  std::vector<std::string> split_paste_lines(const std::string &text)
  {
    std::vector<std::string> lines;
    std::string current;
    for (size_t i = 0; i < text.size(); i++)
    {
      char c = text[i];
      if (c == '\r')
      {
        if (i + 1 < text.size() && text[i + 1] == '\n')
        {
          i++;
        }
        lines.push_back(current);
        current.clear();
      }
      else if (c == '\n')
      {
        lines.push_back(current);
        current.clear();
      }
      else
      {
        current.push_back(c);
      }
    }
    lines.push_back(current);
    return lines;
  }

  bool starts_with_indent(const std::string &line, const std::string &indent)
  {
    return indent.empty()
           || (line.size() >= indent.size() && line.compare(0, indent.size(), indent) == 0);
  }

  std::string common_paste_indent(const std::vector<std::string> &lines)
  {
    std::string common;
    bool initialized = false;
    for (const auto &line : lines)
    {
      if (trim_left_copy(line).empty())
      {
        continue;
      }
      const std::string indent = leading_indent(line);
      if (!initialized)
      {
        common = indent;
        initialized = true;
        continue;
      }
      size_t n = 0;
      while (n < common.size() && n < indent.size() && common[n] == indent[n])
      {
        n++;
      }
      common.resize(n);
    }
    return common;
  }

  std::string join_paste_lines(const std::vector<std::string> &lines)
  {
    std::string out;
    for (size_t i = 0; i < lines.size(); i++)
    {
      if (i > 0)
      {
        out.push_back('\n');
      }
      out += lines[i];
    }
    return out;
  }

  // Rebases a pasted block onto the caret's own context. Where it lands is
  // where Enter would open a line (EditorFeatures::indent_for_new_line), so
  // paste and Enter agree about bodies, argument lists and each language's
  // brace style; the block's own common indent is what is rebased, so the
  // relative indentation inside it survives.
  std::string smart_indent_paste_text(const std::string &text,
                                      const std::string &path,
                                      const std::string &target_line,
                                      int cursor_x,
                                      int tab_size)
  {
    if (!has_newline(text))
    {
      return text;
    }

    std::vector<std::string> lines = split_paste_lines(text);
    if (lines.size() <= 1)
    {
      return text;
    }

    const int at = std::clamp(cursor_x, 0, (int)target_line.size());
    const std::string before_cursor = target_line.substr(0, at);
    const std::string after_cursor = target_line.substr(at);
    const std::string base_indent = leading_indent(target_line);
    const bool prefer_tabs = base_indent.find('\t') != std::string::npos;
    const int step = std::max(1, tab_size);

    const int context_columns =
        EditorFeatures::indent_for_new_line(path, target_line, at, tab_size);
    // A block pasted where Enter would step in starts on a line of its own:
    // pasting after `{` or `if x:` belongs inside the body, not glued to the
    // line the caret is on. Everywhere else the first line continues that line.
    const bool starts_new_line = trim_left_copy(after_cursor).empty()
                                 && context_columns > EditorFeatures::get_indent_level(target_line);

    std::string first_meaningful;
    for (const auto &line : lines)
    {
      first_meaningful = trim_left_copy(line);
      if (!first_meaningful.empty())
      {
        break;
      }
    }

    int base_columns = context_columns;
    // A block that closes the body it is pasted into belongs one level out.
    if (!first_meaningful.empty()
        && (first_meaningful[0] == '}' || first_meaningful[0] == ')' || first_meaningful[0] == ']'))
    {
      base_columns -= step;
    }

    // The first pasted line continues the caret's own line, so it only takes
    // the indent the caret has not already passed; after other text it takes
    // none at all.
    int first_columns = 0;
    if (!starts_new_line && trim_left_copy(before_cursor).empty())
    {
      first_columns = std::max(0, base_columns - indent_columns(before_cursor, tab_size));
    }

    const std::string common_indent = common_paste_indent(lines);
    const std::string block_indent = indent_from_columns(base_columns, tab_size, prefer_tabs);
    const std::string first_indent = indent_from_columns(first_columns, tab_size, prefer_tabs);

    if (starts_new_line)
    {
      lines.insert(lines.begin(), std::string());
    }

    for (size_t i = 0; i < lines.size(); i++)
    {
      if (trim_left_copy(lines[i]).empty())
      {
        lines[i].clear();
        continue;
      }
      if (starts_with_indent(lines[i], common_indent))
      {
        lines[i].erase(0, common_indent.size());
      }
      lines[i] = (i == 0 ? first_indent : block_indent) + lines[i];
    }

    // The caret lands on the last line, and a block copied with its trailing
    // newline ends it blank: it keeps the block's indent, the way a line
    // opened with Enter does.
    if (!lines.empty() && lines.back().empty())
    {
      lines.back() = block_indent;
    }

    return join_paste_lines(lines);
  }
} // namespace

void Editor::copy()
{
  auto &buf = get_buffer();
  if (!buf.selection.active)
  {
    clipboard = buf.line(buf.cursor.y);
    return;
  }

  clipboard = "";
  int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  int start_x = buf.selection.start.y < buf.selection.end.y
                    ? buf.selection.start.x
                    : (buf.selection.start.y == buf.selection.end.y
                           ? std::min(buf.selection.start.x, buf.selection.end.x)
                           : buf.selection.end.x);
  int end_x = buf.selection.start.y < buf.selection.end.y
                  ? buf.selection.end.x
                  : (buf.selection.start.y == buf.selection.end.y
                         ? std::max(buf.selection.start.x, buf.selection.end.x)
                         : buf.selection.start.x);

  for (int i = start_y; i <= end_y; i++)
  {
    if (i == start_y && i == end_y)
    {
      clipboard += buf.line(i).substr(start_x, end_x - start_x);
    }
    else if (i == start_y)
    {
      clipboard += buf.line(i).substr(start_x) + "\n";
    }
    else if (i == end_y)
    {
      clipboard += buf.line(i).substr(0, end_x);
    }
    else
    {
      clipboard += buf.line(i) + "\n";
    }
  }
  write_system_clipboard(clipboard);
}

void Editor::set_clipboard_text(const std::string &text)
{
  clipboard = text;
  // The register and the system clipboard both carry it; see
  // write_system_clipboard for which one owns the selection.
  write_system_clipboard(text);
}

void Editor::cut()
{
  auto &buf = get_buffer();
  if (!buf.selection.active)
  {
    delete_line();
    return;
  }
  copy();
  delete_selection();
}

void Editor::paste()
{
  // The register first, then whatever the OS clipboard has: pressing paste with
  // neither an event payload nor anything newer must still paste the register.
  std::string source_clipboard = clipboard;
  read_xclip_clipboard(source_clipboard);
  paste_text(source_clipboard);
}

void Editor::paste_text(const std::string &text)
{
  if (!edit_allowed())
    return;
  // An image tab's buffer is an empty placeholder: pasting into it would fill
  // that placeholder, and a later save would write it over the picture.
  if (image_viewer.is_image_file(get_buffer().filepath))
  {
    return;
  }

  if (text.empty())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.selection.active || !buf.extra_carets.empty())
  {
    delete_selection();
  }

  const std::string paste_text =
      (auto_indent && smart_paste_indent)
          ? smart_indent_paste_text(
                text, buf.filepath, buf.line(buf.cursor.y), buf.cursor.x, tab_size)
          : text;

  std::vector<std::string> paste_lines = split_paste_lines(paste_text);
  if (!buf.extra_carets.empty() && paste_lines.size() == 1)
  {
    std::vector<Cursor> points = {buf.cursor};
    for (const auto &c : buf.extra_carets)
      points.push_back(c.end);
    std::sort(points.begin(),
              points.end(),
              [](const Cursor &a, const Cursor &b)
              { return a.y > b.y || (a.y == b.y && a.x > b.x); });
    for (const auto &p : points)
    {
      std::string &line_ref = buf.line_mut(p.y);
      const int insert_x = std::clamp(p.x, 0, (int)line_ref.size());
      line_ref.insert(insert_x, paste_lines[0]);
    }
    buf.cursor.x += (int)paste_lines[0].length();
    for (auto &c : buf.extra_carets)
    {
      c.end.x += (int)paste_lines[0].length();
      c.start = c.end;
    }
    buf.modified = true;
    ensure_cursor_visible();
    needs_redraw = true;
    if (lua_api)
      lua_api->on_buffer_change(buf.filepath, "");
    if (!buf.filepath.empty())
      notify_lsp_change(buf.filepath);
    return;
  }

  if (paste_lines.size() == 1)
  {
    std::string &line_ref = buf.line_mut(buf.cursor.y);
    const int insert_x = std::clamp(buf.cursor.x, 0, (int)line_ref.size());
    line_ref.insert(insert_x, paste_lines[0]);
    buf.cursor.x = insert_x + (int)paste_lines[0].length();
  }
  else
  {
    std::string &line_ref = buf.line_mut(buf.cursor.y);
    const int insert_x = std::clamp(buf.cursor.x, 0, (int)line_ref.size());
    const std::string suffix = line_ref.substr(insert_x);
    line_ref.erase(insert_x);
    line_ref += paste_lines.front();

    int insert_y = buf.cursor.y + 1;
    for (size_t i = 1; i < paste_lines.size(); i++)
    {
      std::string next = paste_lines[i];
      if (i + 1 == paste_lines.size())
      {
        next += suffix;
      }
      buf.lines.insert(buf.lines.begin() + insert_y, next);
      insert_y++;
    }

    buf.cursor.y = insert_y - 1;
    buf.cursor.x = (int)paste_lines.back().length();
  }

  buf.modified = true;
  // A pasted block is routinely taller than the pane, so the viewport has to
  // follow the caret to the end of it rather than leave it below the fold.
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::move_line_up()
{
  if (!edit_allowed())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  int start_y = buf.cursor.y;
  int end_y = buf.cursor.y;
  if (buf.selection.active)
  {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  }

  if (start_y <= 0)
    return;

  // Move the whole selected line block up by one row.
  std::rotate(
      buf.lines.begin() + start_y - 1, buf.lines.begin() + start_y, buf.lines.begin() + end_y + 1);

  buf.cursor.y = std::max(0, buf.cursor.y - 1);
  if (buf.selection.active)
  {
    buf.selection.start.y -= 1;
    buf.selection.end.y -= 1;
  }

  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::move_line_down()
{
  if (!edit_allowed())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  int start_y = buf.cursor.y;
  int end_y = buf.cursor.y;
  if (buf.selection.active)
  {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  }

  if (end_y >= (int)buf.line_count() - 1)
    return;

  // Move the whole selected line block down by one row.
  std::rotate(
      buf.lines.begin() + start_y, buf.lines.begin() + end_y + 1, buf.lines.begin() + end_y + 2);

  buf.cursor.y = std::min((int)buf.line_count() - 1, buf.cursor.y + 1);
  if (buf.selection.active)
  {
    buf.selection.start.y += 1;
    buf.selection.end.y += 1;
  }

  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}
