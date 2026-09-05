#include "editor.h"
#include "lua_bridge/api.h"
#include "text_features.h"
#include <algorithm>
#include <cctype>

namespace
{
  int count_one_indent_level(const std::string &line, int tab_size)
  {
    if (line.empty())
      return 0;

    if (line[0] == '\t')
    {
      return 1;
    }

    int removed = 0;
    while (removed < tab_size && removed < (int)line.size() && line[removed] == ' ')
    {
      removed++;
    }
    return removed;
  }

  int remove_one_indent_level(std::string &line, int tab_size)
  {
    int removed = count_one_indent_level(line, tab_size);
    if (removed > 0)
    {
      line.erase(0, removed);
    }
    return removed;
  }

  bool has_python_extension(const std::string &path)
  {
    if (path.size() < 3)
      return false;
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
      return false;
    std::string ext = path.substr(dot);
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext == ".py";
  }

  bool should_indent_after_line(const FileBuffer &buf, const std::string &line)
  {
    if (has_python_extension(buf.filepath))
    {
      return EditorFeatures::should_python_auto_indent(line);
    }
    return EditorFeatures::should_auto_indent(line);
  }

  // Comment prefix/suffix for a buffer's file type. Block comments
  // (html/xml) are toggled per line: prefix + suffix around the code.
  struct CommentStyle
  {
    std::string prefix;
    std::string suffix; // empty for line comments
  };
  CommentStyle comment_style_for(const std::string &ext)
  {
    if (ext == ".py" || ext == ".rb" || ext == ".lua" || ext == ".sh" || ext == ".bash"
        || ext == ".zsh" || ext == ".fish" || ext == ".yaml" || ext == ".yml"
        || ext == ".toml" || ext == ".ini" || ext == ".cfg" || ext == ".conf"
        || ext == ".dockerfile" || ext == ".cmake" || ext == ".make" || ext == ".r"
        || ext == ".ps1")
    {
      return {"#", ""};
    }
    if (ext == ".html" || ext == ".xml" || ext == ".vue" || ext == ".svelte" || ext == ".svg")
    {
      return {"<!--", "-->"};
    }
    if (ext == ".sql" || ext == ".hs" || ext == ".ada" || ext == ".vhdl" || ext == ".f"
        || ext == ".f90")
    {
      return {"--", ""};
    }
    if (ext == ".m" || ext == ".mm" || ext == ".tex")
    {
      return {"%", ""};
    }
    // .c/.h/.cc/.cpp/.hpp/.java/.js/.jsx/.ts/.tsx/.go/.rs/.swift/.kt/.cs/.php/...
    return {"//", ""};
  }
  std::string line_indent(const std::string &s)
  {
    size_t n = 0;
    while (n < s.size() && (s[n] == ' ' || s[n] == '\t'))
      n++;
    return s.substr(0, n);
  }
  bool line_is_commented(const std::string &s, const CommentStyle &style)
  {
    const std::string body = s.substr(line_indent(s).size());
    return body.compare(0, style.prefix.size(), style.prefix) == 0;
  }
} // namespace

void Editor::duplicate_line()
{
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, buf.lines[buf.cursor.y]);
  buf.cursor.y++;
  buf.modified = true;
  needs_redraw = true;
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::insert_line_below()
{
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  // Compute indent from current line
  std::string indent_str = "";
  if (auto_indent)
  {
    int indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
    if (should_indent_after_line(buf, buf.lines[buf.cursor.y]))
      indent += tab_size;
    indent_str = EditorFeatures::get_indent_string(indent, tab_size);
  }
  buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, indent_str);
  buf.cursor.y++;
  buf.cursor.x = indent_str.length();
  buf.modified = true;
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::insert_line_above()
{
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  std::string indent_str = "";
  if (auto_indent)
  {
    int indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
    indent_str = EditorFeatures::get_indent_string(indent, tab_size);
  }
  buf.lines.insert(buf.lines.begin() + buf.cursor.y, indent_str);
  buf.cursor.x = indent_str.length();
  buf.modified = true;
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::indent_selection()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (!buf.selection.active)
    return;

  save_state();

  const int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  const int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  const std::string indent(tab_size, ' ');

  for (int y = start_y; y <= end_y; y++)
  {
    buf.lines[y].insert(0, indent);
  }

  buf.selection.start.x += tab_size;
  buf.selection.end.x += tab_size;
  buf.cursor.x += tab_size;

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  buf.modified = true;
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::outdent_selection()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();

  if (!buf.selection.active)
  {
    int y = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
    int removed = count_one_indent_level(buf.lines[y], tab_size);
    if (removed <= 0)
    {
      return;
    }

    save_state();
    buf.lines[y].erase(0, removed);
    buf.cursor.x = std::max(0, buf.cursor.x - removed);

    clamp_cursor(get_pane().buffer_id);
    ensure_cursor_visible();
    buf.modified = true;
    needs_redraw = true;
    if (lua_api)
      lua_api->on_buffer_change(buf.filepath, "");
    if (!buf.filepath.empty())
      notify_lsp_change(buf.filepath);
    return;
  }

  save_state();

  const int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  const int end_y = std::max(buf.selection.start.y, buf.selection.end.y);

  int removed_start = 0;
  int removed_end = 0;
  int removed_cursor = 0;

  for (int y = start_y; y <= end_y; y++)
  {
    int removed = remove_one_indent_level(buf.lines[y], tab_size);
    if (y == buf.selection.start.y)
      removed_start = removed;
    if (y == buf.selection.end.y)
      removed_end = removed;
    if (y == buf.cursor.y)
      removed_cursor = removed;
  }

  buf.selection.start.x = std::max(0, buf.selection.start.x - removed_start);
  buf.selection.end.x = std::max(0, buf.selection.end.x - removed_end);
  buf.cursor.x = std::max(0, buf.cursor.x - removed_cursor);

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  buf.modified = true;
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::toggle_comment()
{
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  const CommentStyle style = comment_style_for(get_file_extension(buf.filepath));

  int start_y =
      buf.selection.active ? std::min(buf.selection.start.y, buf.selection.end.y) : buf.cursor.y;
  int end_y =
      buf.selection.active ? std::max(buf.selection.start.y, buf.selection.end.y) : buf.cursor.y;

  // Comment on the line's own indentation level (right after the leading
  // whitespace), matching what an editor with smart comment toggling does for
  // nested code instead of always anchoring at column 0.
  bool all_commented = true;
  for (int i = start_y; i <= end_y; i++)
  {
    if (!line_is_commented(buf.lines[i], style))
    {
      all_commented = false;
      break;
    }
  }

  for (int i = start_y; i <= end_y; i++)
  {
    const std::string indent = line_indent(buf.lines[i]);
    const std::string body = buf.lines[i].substr(indent.size());
    if (all_commented)
    {
      if (line_is_commented(buf.lines[i], style))
      {
        const size_t after = indent.size() + style.prefix.size();
        std::string rest = buf.lines[i].substr(after);
        if (!style.suffix.empty() && rest.size() >= style.suffix.size()
            && rest.compare(rest.size() - style.suffix.size(), style.suffix.size(), style.suffix)
                   == 0)
        {
          rest.erase(rest.size() - style.suffix.size());
        }
        buf.lines[i] = indent + rest;
      }
    }
    else
    {
      buf.lines[i] = indent + style.prefix + body;
      if (!style.suffix.empty())
      {
        buf.lines[i] += style.suffix;
      }
    }
  }

  buf.modified = true;
  needs_redraw = true;
  clamp_cursor(get_pane().buffer_id);
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}
