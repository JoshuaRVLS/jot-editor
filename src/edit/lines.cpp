#include "editor.h"
#include "features/language.h"
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

  bool should_indent_after_line(const FileBuffer &buf, const std::string &line)
  {
    if (Language::is_python_file(buf.filepath))
    {
      return EditorFeatures::should_python_auto_indent(line);
    }
    if (Language::is_lua_file(buf.filepath))
    {
      return EditorFeatures::should_lua_auto_indent(line);
    }
    return EditorFeatures::should_auto_indent(line);
  }

  // Comment markers for a buffer's file type. `block_open`/`block_close`
  // are non-empty when the language has a true block-comment form; a
  // multi-line selection then wraps in one pair instead of commenting each
  // line separately.
  struct CommentStyle
  {
    std::string prefix;        // line comment prefix ("//", "#", "--", "%")
    std::string suffix;        // per-line closer for pseudo-block langs (html "-->")
    std::string block_open;    // block opener, or empty when none exists
    std::string block_close;   // block closer, or empty when none exists
  };
  CommentStyle comment_style_for(const std::string &ext)
  {
    if (ext == ".html" || ext == ".xml" || ext == ".vue" || ext == ".svelte" || ext == ".svg")
    {
      // HTML has no line comments; block-only, and per-line toggles wrap
      // single lines in one <!-- ... --> pair.
      return {"<!--", "-->", "<!--", "-->"};
    }
    if (ext == ".py" || ext == ".rb" || ext == ".lua" || ext == ".sh" || ext == ".bash"
        || ext == ".zsh" || ext == ".fish" || ext == ".yaml" || ext == ".yml"
        || ext == ".toml" || ext == ".ini" || ext == ".cfg" || ext == ".conf"
        || ext == ".dockerfile" || ext == ".cmake" || ext == ".make" || ext == ".r"
        || ext == ".ps1")
    {
      // No block-comment syntax in the language; # per line is the only form.
      return {"#", "", "", ""};
    }
    if (ext == ".sql" || ext == ".hs" || ext == ".ada" || ext == ".vhdl" || ext == ".f"
        || ext == ".f90")
    {
      return {"--", "", "", ""};
    }
    if (ext == ".m" || ext == ".mm" || ext == ".tex")
    {
      return {"%", "", "", ""};
    }
    // .c/.h/.cc/.cpp/.hpp/.java/.js/.jsx/.ts/.tsx/.go/.rs/.swift/.kt/.cs/.php/...
    return {"//", "", "/*", "*/"};
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
  // True when the whole [start,end] range is wrapped in one block comment:
  // the opener sits on the first line and the closer on the last.
  bool range_is_block_commented(const std::vector<std::string> &lines,
                                int start,
                                int end,
                                const CommentStyle &style)
  {
    if (style.block_open.empty() || start > end)
    {
      return false;
    }
    const std::string &first_body = lines[start].substr(line_indent(lines[start]).size());
    const std::string &last_body = lines[end].substr(line_indent(lines[end]).size());
    return first_body.compare(0, style.block_open.size(), style.block_open) == 0
           && last_body.size() >= style.block_close.size()
           && last_body.compare(last_body.size() - style.block_close.size(),
                                style.block_close.size(),
                                style.block_close)
                  == 0;
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

  const bool multi_line = buf.selection.active
                          && buf.selection.start.y != buf.selection.end.y;
  int start_y =
      buf.selection.active ? std::min(buf.selection.start.y, buf.selection.end.y) : buf.cursor.y;
  int end_y =
      buf.selection.active ? std::max(buf.selection.start.y, buf.selection.end.y) : buf.cursor.y;

  // Re-toggle support: a no-selection toggle with the cursor inside the
  // range of the last multi-line toggle applies to that same range again, so
  // select -> Ctrl+/ -> Ctrl+/ still cycles after the selection is cleared.
  const bool used_selection = buf.selection.active;
  const bool used_memory = !used_selection && buf.last_comment_start >= 0
                           && buf.cursor.y >= buf.last_comment_start
                           && buf.cursor.y <= buf.last_comment_end;
  bool block_boundary = used_memory;
  if (used_memory)
  {
    start_y = buf.last_comment_start;
    end_y = buf.last_comment_end;
  }

  // Without a selection, the cursor may still sit on a block boundary (the
  // opener or closer of a /* ... */ or <!-- ... --> pair). Treat the whole
  // enclosing block as the toggle range so a second Ctrl+/ after a block
  // wrap unwraps it from either edge.
  if (!buf.selection.active && !used_memory && !style.block_open.empty()
      && !style.block_close.empty())
  {
    const std::string cur_body =
        buf.lines[start_y].substr(line_indent(buf.lines[start_y]).size());
    if (cur_body.compare(0, style.block_open.size(), style.block_open) == 0)
    {
      // Opener on the cursor line: scan forward for the matching closer.
      for (int i = start_y; i < (int)buf.lines.size(); i++)
      {
        const std::string body = buf.lines[i].substr(line_indent(buf.lines[i]).size());
        if (body.size() >= style.block_close.size()
            && body.compare(body.size() - style.block_close.size(),
                            style.block_close.size(),
                            style.block_close)
                   == 0)
        {
          end_y = i;
          block_boundary = true;
          break;
        }
      }
    }
    else if (cur_body.size() >= style.block_close.size()
             && cur_body.compare(cur_body.size() - style.block_close.size(),
                                 style.block_close.size(),
                                 style.block_close)
                    == 0)
    {
      // Closer on the cursor line: scan backward for the opener.
      for (int i = start_y; i >= 0; i--)
      {
        const std::string body = buf.lines[i].substr(line_indent(buf.lines[i]).size());
        if (body.compare(0, style.block_open.size(), style.block_open) == 0)
        {
          start_y = i;
          block_boundary = true;
          break;
        }
      }
    }
  }

  // Unwrapping takes one of two shapes: the whole range sits in a single
  // block pair (/* ... */, or <!-- ... --> around one line), or every line
  // carries its own line comment (//, #, ...). Block-style languages wrap a
  // clean multi-line selection in one pair; a mixed selection (some lines
  // already line-commented) is finished with per-line markers instead so a
  // block is never nested inside existing comments.
  // A block-only style (html/xml: <!-- -->) is one where every line comment
  // is itself a single-line block wrap.
  const bool block_only_style = !style.block_open.empty() && style.prefix == style.block_open;
  const bool wrapped_in_block =
      !style.block_open.empty()
      && (multi_line || block_only_style || block_boundary)
      && range_is_block_commented(buf.lines, start_y, end_y, style);
  const bool all_line_commented = [&]
  {
    for (int i = start_y; i <= end_y; i++)
    {
      if (!line_is_commented(buf.lines[i], style))
      {
        return false;
      }
    }
    return true;
  }();
  const bool some_line_commented = [&]
  {
    for (int i = start_y; i <= end_y; i++)
    {
      if (line_is_commented(buf.lines[i], style))
      {
        return true;
      }
    }
    return false;
  }();
  const bool do_block_wrap =
      !wrapped_in_block && !all_line_commented && multi_line
      && !style.block_open.empty() && !some_line_commented;
  const bool uncomment = wrapped_in_block || all_line_commented;

  for (int i = start_y; i <= end_y; i++)
  {
    const std::string indent = line_indent(buf.lines[i]);
    const std::string body = buf.lines[i].substr(indent.size());
    const bool line_commented = line_is_commented(buf.lines[i], style);

    // Middle lines of a wrapped block are already commented by the wrapper;
    // only the boundary lines carry the markers to remove.
    if (uncomment && wrapped_in_block && i != start_y && i != end_y)
    {
      continue;
    }
    if (uncomment && i == start_y && wrapped_in_block)
    {
      // Remove the opener on the range's first line (may also carry line
      // comment on the same line, e.g. /*// code).
      std::string rest = body;
      const bool has_open = style.block_open.empty()
                                ? false
                                : rest.compare(0, style.block_open.size(), style.block_open) == 0;
      if (has_open)
      {
        rest = rest.substr(style.block_open.size());
      }
      // Only strip a separate line-comment marker (C: ///* ...). For block
      // styles whose line form is the same token (html: <!--), the opener
      // removal above already consumed it.
      if (has_open && style.prefix != style.block_open && line_commented
          && rest.compare(0, style.prefix.size(), style.prefix) == 0)
      {
        rest = rest.substr(style.prefix.size());
      }
      if (!rest.empty() && rest[0] == ' ')
      {
        rest.erase(0, 1); // undo the "/* " separator
      }
      buf.lines[i] = indent + rest;
    }
    else if (uncomment && i == end_y && wrapped_in_block)
    {
      // Remove the closer on the range's last line.
      std::string rest = body;
      if (!style.block_close.empty() && rest.size() >= style.block_close.size()
          && rest.compare(rest.size() - style.block_close.size(),
                          style.block_close.size(),
                          style.block_close)
                 == 0)
      {
        rest.erase(rest.size() - style.block_close.size());
      }
      if (line_commented && style.prefix != style.block_open
          && rest.compare(0, style.prefix.size(), style.prefix) == 0)
      {
        rest = rest.substr(style.prefix.size());
      }
      size_t end_nonspace = rest.find_last_not_of(' ');
      if (end_nonspace != std::string::npos && end_nonspace + 1 < rest.size())
      {
        rest.erase(end_nonspace + 1); // undo the " */" separator
      }
      buf.lines[i] = indent + rest;
    }
    else if (uncomment && line_commented)
    {
      // Strip this line's own comment marker (prefix and, for per-line
      // block style, its trailing suffix).
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
    else if (do_block_wrap)
    {
      // Clean multi-line selection in a language with real block comments:
      // wrap the whole range in one /* ... */ pair (aligned to the shallowest
      // line) instead of stamping // in front of every line.
      std::string pad = indent;
      for (int j = start_y + 1; j <= end_y; j++)
      {
        if (line_indent(buf.lines[j]).size() < pad.size())
        {
          pad = line_indent(buf.lines[j]);
        }
      }
      if (i == start_y)
      {
        buf.lines[i] = pad + style.block_open + " " + body;
      }
      else if (i == end_y)
      {
        std::string text = indent + body;
        if (!text.empty() && text.back() == ' ')
        {
          text.pop_back();
        }
        buf.lines[i] = text + " " + style.block_close;
      }
    }
    else
    {
      // Single line (or a language with no block form, or a mixed selection
      // being finished with markers): comment the line unless it is already
      // commented. A line already carrying a marker is left alone.
      if (!line_commented)
      {
        buf.lines[i] = indent + style.prefix + body;
        if (!style.suffix.empty())
        {
          buf.lines[i] += style.suffix;
        }
      }
    }
  }

  buf.modified = true;
  needs_redraw = true;
  // The toggle consumes the selection. Leaving it armed is a footgun: the
  // next keystroke (e.g. ':' to save) replaces the freshly wrapped range
  // instead of typing normally. The re-toggle memory above keeps double-tap
  // cycling working without the armed selection.
  buf.selection.active = false;
  if (used_selection || used_memory || block_boundary)
  {
    buf.last_comment_start = start_y;
    buf.last_comment_end = end_y;
  }
  else
  {
    buf.last_comment_start = -1;
    buf.last_comment_end = -1;
  }
  clamp_cursor(get_pane().buffer_id);
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}
