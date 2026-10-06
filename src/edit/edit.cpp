#include "autoclose.h"
#include "editor.h"
#include "features/language.h"
#include "html.h"
#include "jot/lua/api.h"
#include "text_features.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <vector>

namespace
{
  int tab_advance(int visual_col, int tab_size)
  {
    const int ts = std::max(1, tab_size);
    const int rem = visual_col % ts;
    return rem == 0 ? ts : (ts - rem);
  }

  int compute_visual_column(const std::string &line, int logical_col, int tab_size)
  {
    int clamped = ui_clamp_to_utf8_boundary(line, std::clamp(logical_col, 0, (int)line.size()));
    int visual = 0;
    for (int i = 0; i < clamped;)
    {
      int next = ui_next_grapheme_boundary(line, i);
      if (next <= i)
        next = i + 1;
      visual += (line[i] == '\t') ? tab_advance(visual, tab_size)
                                  : std::max(1, ui_range_cell_count(line, i, next));
      i = next;
    }
    return visual;
  }

  bool ascii_space_at(const std::string &line, int pos)
  {
    if (pos < 0 || pos >= (int)line.size())
      return false;
    unsigned char c = static_cast<unsigned char>(line[pos]);
    return c < 0x80 && std::isspace(c);
  }

  bool word_grapheme_at(const std::string &line, int pos)
  {
    if (pos < 0 || pos >= (int)line.size())
      return false;
    unsigned char c = static_cast<unsigned char>(line[pos]);
    if (c >= 0x80)
      return true;
    return std::isalnum(c) || c == '_';
  }

  bool is_python_buffer(const FileBuffer &buf)
  {
    return Language::is_python_file(buf.filepath);
  }

  bool is_lua_buffer(const FileBuffer &buf)
  {
    return Language::is_lua_file(buf.filepath);
  }

  bool is_c_family_buffer(const FileBuffer &buf)
  {
    return Language::is_c_family_file(buf.filepath);
  }

  void dedent_current_line_one_level(FileBuffer &buf, int tab_size)
  {
    std::string &line = buf.line_mut(buf.cursor.y);
    const int current_indent = EditorFeatures::get_indent_level(line);
    if (current_indent < tab_size)
      return;

    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
      return;

    const int new_indent = current_indent - tab_size;
    const std::string new_indent_str = EditorFeatures::get_indent_string(new_indent, tab_size);
    const std::string trimmed = line.substr(start);
    line = new_indent_str + trimmed;
    buf.cursor.x = std::max(0, buf.cursor.x - (int)start + (int)new_indent_str.size());
  }

  // The preprocessor takes the whole line to column 0, which is one level out
  // of wherever the caret happened to be.
  void dedent_current_line_to_column_zero(FileBuffer &buf)
  {
    std::string &line = buf.line_mut(buf.cursor.y);
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
      return;

    line.erase(0, start);
    buf.cursor.x = std::max(0, buf.cursor.x - (int)start);
  }
} // namespace

void Editor::sync_markup_tag_name()
{
  auto &buf = get_buffer();
  if (!HtmlFeatures::is_markup_tag_extension(buf.filepath))
    return;
  if (buf.is_lazy())
    buf.materialize();

  HtmlFeatures::MarkupTagPair pair;
  if (!HtmlFeatures::find_tag_pair(buf.lines, buf.cursor.y, buf.cursor.x, pair))
    return;
  if (pair.partner == pair.name)
    return;
  if (pair.partner_line < 0 || pair.partner_line >= (int)buf.lines.size())
    return;

  std::string &partner_line = buf.line_mut(pair.partner_line);
  if (pair.partner_col < 0
      || pair.partner_col + (int)pair.partner.size() > (int)partner_line.size())
    return;

  const int delta = (int)pair.name.size() - (int)pair.partner.size();
  partner_line.replace(pair.partner_col, pair.partner.size(), pair.name);

  // The partner is ahead of the cursor except when the cursor is in the closing
  // tag of a pair on one line, and then the caret has to follow the text that
  // moved under it. On another line only that line's length changed, so the
  // column is already right.
  if (pair.partner_line == buf.cursor.y && pair.partner_col < buf.cursor.x)
    buf.cursor.x = std::max(0, buf.cursor.x + delta);

  buf.modified = true;
  needs_redraw = true;
}

bool Editor::edit_allowed()
{
  if (!get_buffer().read_only)
  {
    return true;
  }
  // The refusal has to reach the screen, and in a real session the status line
  // is the Lua UI kit's surface: a plain message would be set and never
  // painted. It rides the toast instead, once per burst -- while the notice is
  // still up another keystroke in a rendered view stays quiet rather than
  // stacking one toast per character of a held key.
  if (transient_message_timer == 0)
  {
    set_transient_message("This view is read-only", 2000, true);
  }
  return false;
}

bool Editor::insert_char(char c)
{
  if (!edit_allowed())
    return false;
  save_state();
  auto &buf = get_buffer();

  if (buf.selection.active && AutoClose::should_auto_close(c))
  {
    Cursor s = buf.selection.start;
    Cursor e = buf.selection.end;
    if (s.y > e.y || (s.y == e.y && s.x > e.x))
    {
      std::swap(s, e);
    }

    if (s.y >= 0 && s.y < (int)buf.line_count() && e.y >= 0 && e.y < (int)buf.line_count())
    {
      char closing = AutoClose::get_closing_bracket(c);
      if (closing != '\0')
      {
        if (s.y == e.y)
        {
          auto &line = buf.line_mut(s.y);
          s.x = std::max(0, std::min(s.x, (int)line.length()));
          e.x = std::max(0, std::min(e.x, (int)line.length()));
          line.insert(e.x, 1, closing);
          line.insert(s.x, 1, c);
        }
        else
        {
          auto &end_line = buf.line_mut(e.y);
          auto &start_line = buf.line_mut(s.y);
          e.x = std::max(0, std::min(e.x, (int)end_line.length()));
          s.x = std::max(0, std::min(s.x, (int)start_line.length()));
          end_line.insert(e.x, 1, closing);
          start_line.insert(s.x, 1, c);
        }

        buf.selection.start = {s.x + 1, s.y};
        buf.selection.end = {e.x + 1, e.y};
        buf.selection.active = true;
        buf.cursor = buf.selection.end;
        buf.preferred_x = buf.cursor.x;
        buf.modified = true;
        buf.is_placeholder = false;
        ensure_cursor_visible();
        needs_redraw = true;
        if (lua_api)
          lua_api->on_buffer_change(buf.filepath, "");
        if (!buf.filepath.empty())
          notify_lsp_change(buf.filepath);
        return false;
      }
    }
  }

  if (buf.selection.active || !buf.extra_carets.empty())
  {
    delete_selection();
  }

  bool has_extras = !buf.extra_carets.empty();
  if (has_extras)
  {
    std::string text(1, c);
    if (c == '\t')
    {
      const std::string &line = buf.line(buf.cursor.y);
      text = std::string(tab_advance(compute_visual_column(line, buf.cursor.x, tab_size), tab_size),
                         ' ');
    }
    struct CaretEdit
    {
      size_t extra_idx;
      bool is_primary;
      int y;
      int x;
    };
    std::vector<CaretEdit> edits;
    edits.push_back({0, true, buf.cursor.y, buf.cursor.x});
    for (size_t i = 0; i < buf.extra_carets.size(); i++)
      edits.push_back({i, false, buf.extra_carets[i].end.y, buf.extra_carets[i].end.x});
    std::sort(edits.begin(), edits.end(), [](const CaretEdit &a, const CaretEdit &b) {
      return a.y > b.y || (a.y == b.y && a.x > b.x);
    });
    // A closing character that already sits at a caret is stepped over rather
    // than doubled, and a pair character inserts its partner at every caret, so
    // the pairs a lone caret gets are the pairs a column gets.
    auto move_caret = [&](const CaretEdit &edit, int x) {
      if (edit.is_primary)
        buf.cursor.x = ui_clamp_to_utf8_boundary(buf.line(edit.y), x);
      else
      {
        auto &caret = buf.extra_carets[edit.extra_idx];
        caret.end.x = ui_clamp_to_utf8_boundary(buf.line(edit.y), x);
        caret.start = caret.end;
      }
    };
    const bool closing_domain = AutoClose::is_closing_bracket(c);
    auto caret_x = [&](const CaretEdit &edit) {
      return edit.is_primary ? buf.cursor.x : buf.extra_carets[edit.extra_idx].end.x;
    };

    for (std::size_t i = 0; i < edits.size(); i++)
    {
      const CaretEdit &e = edits[i];
      const std::string &line = buf.line(e.y);
      const int ix = ui_clamp_to_utf8_boundary(line, std::clamp(e.x, 0, (int)line.size()));

      if (closing_domain && AutoClose::should_skip_closing(c, line, ix))
      {
        move_caret(e, e.x + 1);
        continue;
      }

      std::string insert = text;
      if (AutoClose::should_insert_pair(c, line, ix))
        insert.push_back(AutoClose::get_closing_bracket(c));

      buf.line_mut(e.y).insert(ix, insert);
      // One character forward, so a caret that opened a pair lands inside it.
      move_caret(e, e.x + (int)text.size());

      // Carets are reached right to left, so what is already placed on this
      // line sits to the right of the text that just went in and moves with
      // it. Without that, a caret that opened its pair earlier in the loop no
      // longer sits inside it.
      for (std::size_t j = 0; j < i; j++)
      {
        if (edits[j].y != e.y)
          continue;
        const int x = caret_x(edits[j]);
        if (x >= ix)
          move_caret(edits[j], x + (int)insert.size());
      }
    }
    buf.modified = true;
    buf.is_placeholder = false;
    ensure_cursor_visible();
    needs_redraw = true;
    if (lua_api)
      lua_api->on_buffer_change(buf.filepath, "");
    if (!buf.filepath.empty())
      notify_lsp_change(buf.filepath);
    return false;
  }

  // The caret's column can outlive the line under it (a trim, an LSP edit or a
  // plugin may have shortened the line since) and insert() is out of range for
  // a position past the end, so the column comes back onto the line first.
  clamp_carets(buf);

  bool inserted_html_closing_tag = false;

  if (c == '\t')
  {
    const std::string &line = buf.line(buf.cursor.y);
    int visual_col = compute_visual_column(line, buf.cursor.x, tab_size);
    int spaces_to_insert = tab_advance(visual_col, tab_size);
    std::string spaces(spaces_to_insert, ' ');
    buf.line_mut(buf.cursor.y).insert(buf.cursor.x, spaces);
    buf.cursor.x += spaces_to_insert;
  }
  else
  {
    // Check if we should skip closing bracket
    if (AutoClose::is_closing_bracket(c)
        && AutoClose::should_skip_closing(c, buf.line_mut(buf.cursor.y), buf.cursor.x))
    {
      buf.cursor.x++;
      needs_redraw = true;
      return false;
    }

    // The partner is decided before the character goes in: afterwards the
    // neighbour before the caret is the character just typed, not the one the
    // rules are about.
    char partner = '\0';
    {
      const std::string &line = buf.line(buf.cursor.y);
      const int at = std::clamp(buf.cursor.x, 0, (int)line.size());
      if (AutoClose::should_insert_pair(c, line, at))
        partner = AutoClose::get_closing_bracket(c);
    }

    buf.line_mut(buf.cursor.y).insert(buf.cursor.x, 1, c);
    buf.cursor.x++;

    if (c == '>' && HtmlFeatures::is_markup_tag_extension(buf.filepath))
    {
      std::string closing;
      std::string &line = buf.line_mut(buf.cursor.y);
      if (HtmlFeatures::should_insert_closing_tag(line, buf.cursor.x, closing))
      {
        line.insert(buf.cursor.x, closing);
        inserted_html_closing_tag = true;
      }
    }

    if (auto_indent && (c == '}' || c == ']' || c == ')'))
    {
      if (EditorFeatures::should_dedent(buf.line_mut(buf.cursor.y)))
      {
        dedent_current_line_one_level(buf, tab_size);
      }
    }

    if (auto_indent && c == ':' && is_python_buffer(buf))
    {
      if (EditorFeatures::should_python_dedent(buf.line_mut(buf.cursor.y)))
      {
        dedent_current_line_one_level(buf, tab_size);
      }
    }

    // A C++ label belongs one level out of the body it introduces (`case`,
    // `default`, an access specifier), and a preprocessor directive goes all the
    // way back to column 0 -- both what the project's clang-format produces.
    if (auto_indent && is_c_family_buffer(buf) && (c == ':' || c == '#'))
    {
      if (EditorFeatures::should_cpp_dedent(buf.line_mut(buf.cursor.y)))
      {
        dedent_current_line_one_level(buf, tab_size);
      }
      else if (EditorFeatures::is_preprocessor_directive_start(buf.line_mut(buf.cursor.y)))
      {
        dedent_current_line_to_column_zero(buf);
      }
    }

    // Lua blocks close with a word (`end`, `else`, `elseif`, `until`) instead
    // of a bracket, so dedent the moment the typed word completes at the
    // start of the line. Reserved words cannot be identifiers, so the match
    // can only be the block closer itself.
    if (auto_indent && is_lua_buffer(buf) && std::isalnum(static_cast<unsigned char>(c)))
    {
      if (EditorFeatures::should_lua_dedent(buf.line_mut(buf.cursor.y)))
      {
        dedent_current_line_one_level(buf, tab_size);
      }
    }

    if (partner != '\0')
    {
      buf.line_mut(buf.cursor.y).insert(buf.cursor.x, 1, partner);
    }

    sync_markup_tag_name();
  }

  buf.modified = true;
  buf.is_placeholder = false;
  // A typed character can push the caret past the pane's right edge, and on a
  // line longer than the pane it would keep typing off screen without this.
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);

  return inserted_html_closing_tag;
}

void Editor::insert_string(const std::string &str)
{
  if (!edit_allowed())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.selection.active || !buf.extra_carets.empty())
  {
    delete_selection();
  }
  std::string text = ui_normalize_nfc(str);
  if (buf.extra_carets.empty())
  {
    // See insert_char: a column past the end of the caret's line would make
    // insert() throw.
    clamp_carets(buf);
    buf.line_mut(buf.cursor.y).insert(buf.cursor.x, text);
    buf.cursor.x += text.length();
  }
  else
  {
    struct CaretPoint
    {
      size_t extra_idx;
      bool is_primary;
      int y;
      int x;
    };
    std::vector<CaretPoint> points;
    points.push_back({0, true, buf.cursor.y, buf.cursor.x});
    for (size_t i = 0; i < buf.extra_carets.size(); i++)
      points.push_back({i, false, buf.extra_carets[i].end.y, buf.extra_carets[i].end.x});
    std::sort(points.begin(), points.end(), [](const CaretPoint &a, const CaretPoint &b) {
      return a.y > b.y || (a.y == b.y && a.x > b.x);
    });
    for (const auto &p : points)
    {
      int ix = std::clamp(p.x, 0, (int)buf.line(p.y).size());
      buf.line_mut(p.y).insert(ix, text);
      if (p.is_primary)
        buf.cursor.x = ix + (int)text.size();
      else
      {
        buf.extra_carets[p.extra_idx].end.x = ix + (int)text.size();
        buf.extra_carets[p.extra_idx].start = buf.extra_carets[p.extra_idx].end;
      }
    }
  }
  buf.modified = true;
  buf.is_placeholder = false;
  // An inserted block can be taller or wider than the pane; the caret it lands
  // on has to be on screen, as it is after every other edit.
  ensure_cursor_visible();
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_char(bool forward)
{
  if (!edit_allowed())
    return;
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.selection.active || !buf.extra_carets.empty())
  {
    // Every caret deletes: active spans are erased wholesale, point carets
    // (Alt+click) erase one grapheme around themselves, and the main cursor
    // participates even when it shares its cell with a caret.
    delete_at_all_carets(forward);
    needs_redraw = true;
    return;
  }
  save_state();

  if (forward)
  {
    if (buf.cursor.x < (int)buf.line_mut(buf.cursor.y).length())
    {
      auto &line = buf.line_mut(buf.cursor.y);
      const int end = ui_next_grapheme_boundary(line, buf.cursor.x);
      line.erase(buf.cursor.x, end - buf.cursor.x);
      buf.modified = true;
    }
    else if (buf.cursor.y < (int)buf.line_count() - 1)
    {
      buf.line_mut(buf.cursor.y) += buf.line_mut(buf.cursor.y + 1);
      buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
      buf.modified = true;
    }
  }
  else
  {
    if (buf.cursor.x > 0)
    {
      auto &line = buf.line_mut(buf.cursor.y);
      if (buf.cursor.x < (int)line.length())
      {
        const int left_start = ui_prev_grapheme_boundary(line, buf.cursor.x);
        char left = line[left_start];
        char right = line[buf.cursor.x];
        char expected = AutoClose::get_closing_bracket(left);
        if (expected != '\0' && right == expected)
        {
          line.erase(buf.cursor.x, 1);
          buf.cursor.x = left_start;
          line.erase(left_start, ui_next_grapheme_boundary(line, left_start) - left_start);
          buf.modified = true;
        }
        else
        {
          buf.cursor.x = left_start;
          line.erase(left_start, ui_next_grapheme_boundary(line, left_start) - left_start);
          buf.modified = true;
        }
      }
      else
      {
        const int start = ui_prev_grapheme_boundary(line, buf.cursor.x);
        line.erase(start, buf.cursor.x - start);
        buf.cursor.x = start;
        buf.modified = true;
      }
    }
    else if (buf.cursor.y > 0)
    {
      buf.cursor.y--;
      buf.cursor.x = buf.line_mut(buf.cursor.y).length();
      buf.line_mut(buf.cursor.y) += buf.line_mut(buf.cursor.y + 1);
      buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
      buf.modified = true;
    }

    // Backspacing a tag name shrinks the partner with it.
    sync_markup_tag_name();
  }
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_word_backward()
{
  if (!edit_allowed())
    return;
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.selection.active)
  {
    delete_selection();
    return;
  }

  if (buf.cursor.x == 0 && buf.cursor.y == 0)
    return;

  save_state();

  if (buf.cursor.x == 0 && buf.cursor.y > 0)
  {
    buf.cursor.y--;
    buf.cursor.x = (int)buf.line_mut(buf.cursor.y).length();
    buf.line_mut(buf.cursor.y) += buf.line_mut(buf.cursor.y + 1);
    buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
    buf.modified = true;
  }
  else
  {
    auto &line = buf.line_mut(buf.cursor.y);
    int start = ui_clamp_to_utf8_boundary(line, buf.cursor.x);

    while (start > 0)
    {
      int prev = ui_prev_grapheme_boundary(line, start);
      if (!ascii_space_at(line, prev))
        break;
      start = prev;
    }
    while (start > 0)
    {
      int prev = ui_prev_grapheme_boundary(line, start);
      if (!word_grapheme_at(line, prev))
        break;
      start = prev;
    }
    if (start == buf.cursor.x)
    {
      start = ui_prev_grapheme_boundary(line, buf.cursor.x);
    }

    line.erase(start, buf.cursor.x - start);
    buf.cursor.x = start;
    buf.modified = true;
  }

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_word_forward()
{
  if (!edit_allowed())
    return;
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.selection.active)
  {
    delete_selection();
    return;
  }

  if (buf.cursor.y == (int)buf.line_count() - 1
      && buf.cursor.x == (int)buf.line_mut(buf.cursor.y).length())
    return;

  save_state();

  auto &line = buf.line_mut(buf.cursor.y);
  if (buf.cursor.x >= (int)line.length() && buf.cursor.y < (int)buf.line_count() - 1)
  {
    buf.line_mut(buf.cursor.y) += buf.line_mut(buf.cursor.y + 1);
    buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
    buf.modified = true;
  }
  else
  {
    int end = buf.cursor.x;

    while (end < (int)line.length() && ascii_space_at(line, end))
    {
      end = ui_next_grapheme_boundary(line, end);
    }
    while (end < (int)line.length() && word_grapheme_at(line, end))
    {
      end = ui_next_grapheme_boundary(line, end);
    }
    if (end == buf.cursor.x)
    {
      end = ui_next_grapheme_boundary(line, buf.cursor.x);
    }

    line.erase(buf.cursor.x, end - buf.cursor.x);
    buf.modified = true;
  }

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::delete_selection()
{
  if (!edit_allowed())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();

  if (!buf.selection.active && buf.extra_carets.empty())
    return;

  if (buf.extra_carets.empty())
  {
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

    if (start_y == end_y)
    {
      start_x = ui_clamp_to_utf8_boundary(buf.line(start_y), start_x);
      end_x = ui_clamp_to_utf8_boundary(buf.line(start_y), end_x);
      buf.line_mut(start_y).erase(start_x, end_x - start_x);
      buf.cursor.y = start_y;
      buf.cursor.x = start_x;
    }
    else
    {
      start_x = ui_clamp_to_utf8_boundary(buf.line(start_y), start_x);
      end_x = ui_clamp_to_utf8_boundary(buf.line(end_y), end_x);
      buf.line_mut(start_y) =
          buf.line_mut(start_y).substr(0, start_x) + buf.line_mut(end_y).substr(end_x);
      buf.lines.erase(buf.lines.begin() + start_y + 1, buf.lines.begin() + end_y + 1);
      buf.cursor.y = start_y;
      buf.cursor.x = start_x;
    }

    buf.selection.active = false;
    buf.modified = true;
    clamp_cursor(get_pane().buffer_id);
    ensure_cursor_visible();
    needs_redraw = true;
    if (lua_api)
      lua_api->on_buffer_change(buf.filepath, "");
    if (!buf.filepath.empty())
      notify_lsp_change(buf.filepath);
    return;
  }

  {
    struct Span
    {
      int start_y, end_y, start_x, end_x;
    };
    auto normalize = [&](const Selection &sel, Span &out) {
    out.start_y = std::min(sel.start.y, sel.end.y);
    out.end_y = std::max(sel.start.y, sel.end.y);
    out.start_x = sel.start.y < sel.end.y
                      ? sel.start.x
                      : (sel.start.y == sel.end.y ? std::min(sel.start.x, sel.end.x)
                                                 : sel.end.x);
    out.end_x = sel.start.y < sel.end.y
                    ? sel.end.x
                    : (sel.start.y == sel.end.y ? std::max(sel.start.x, sel.end.x)
                                               : sel.start.x);
  };
  std::vector<std::pair<Span, bool>> spans;
  if (buf.selection.active)
  {
    Span s{};
    normalize(buf.selection, s);
    spans.push_back({s, true});
  }
  for (size_t i = 0; i < buf.extra_carets.size(); i++)
  {
    if (!buf.extra_carets[i].active)
      continue;
    Span s{};
    normalize(buf.extra_carets[i], s);
    spans.push_back({s, false});
  }
  if (spans.empty())
    return;
  struct SpanEdit
  {
    Span span;
    bool is_primary;
    size_t extra_idx;
  };
  std::vector<SpanEdit> ordered;
  {
    size_t extra_seen = 0;
    for (size_t si = 0; si < spans.size(); si++)
    {
      if (spans[si].second)
      {
        ordered.push_back({spans[si].first, true, 0});
      }
      else
      {
        while (extra_seen < buf.extra_carets.size() && !buf.extra_carets[extra_seen].active)
          extra_seen++;
        if (extra_seen >= buf.extra_carets.size())
          continue;
        ordered.push_back({spans[si].first, false, extra_seen});
        extra_seen++;
      }
    }
  }
  if (ordered.empty())
    return;

  std::sort(ordered.begin(), ordered.end(), [](const SpanEdit &a, const SpanEdit &b) {
    return a.span.start_y > b.span.start_y
           || (a.span.start_y == b.span.start_y && a.span.start_x > b.span.start_x);
  });
  for (const auto &item : ordered)
  {
    const Span &s = item.span;
    bool is_primary = item.is_primary;
    int start_x = ui_clamp_to_utf8_boundary(buf.line(s.start_y), s.start_x);
    int end_x = ui_clamp_to_utf8_boundary(buf.line(s.end_y), s.end_x);
    if (s.start_y == s.end_y)
    {
      buf.line_mut(s.start_y).erase(start_x, end_x - start_x);
      if (is_primary)
      {
        buf.cursor.y = s.start_y;
        buf.cursor.x = start_x;
      }
      else
      {
        buf.extra_carets[item.extra_idx].start = {start_x, s.start_y};
        buf.extra_carets[item.extra_idx].end = {start_x, s.start_y};
      }
    }
    else
    {
      buf.line_mut(s.start_y) = buf.line_mut(s.start_y).substr(0, start_x)
                                + buf.line_mut(s.end_y).substr(end_x);
      buf.lines.erase(buf.lines.begin() + s.start_y + 1, buf.lines.begin() + s.end_y + 1);
      if (is_primary)
      {
        buf.cursor.y = s.start_y;
        buf.cursor.x = start_x;
      }
      else
      {
        buf.extra_carets[item.extra_idx].start = {start_x, s.start_y};
        buf.extra_carets[item.extra_idx].end = {start_x, s.start_y};
      }
    }
  }

  buf.selection.active = false;
  for (auto &c : buf.extra_carets)
  {
    if (c.active)
    {
      c.end = c.start;
      c.active = false;
    }
  }
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
  }
}

void Editor::delete_line()
{
  if (!edit_allowed())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.line_count() == 1)
  {
    clipboard = buf.line_mut(0);
    buf.line_mut(0) = "";
  }
  else
  {
    clipboard = buf.line_mut(buf.cursor.y);
    buf.lines.erase(buf.lines.begin() + buf.cursor.y);
    if (buf.cursor.y >= (int)buf.line_count())
      buf.cursor.y = buf.line_count() - 1;
  }
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::new_line()
{
  if (!edit_allowed())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  std::string current_line = buf.line_mut(buf.cursor.y);
  buf.cursor.x = ui_clamp_to_utf8_boundary(current_line, buf.cursor.x);
  std::string remaining = current_line.substr(buf.cursor.x);
  buf.line_mut(buf.cursor.y) = current_line.substr(0, buf.cursor.x);

  std::string new_line_str = "";
  bool split_closing_bracket_line = false;
  std::string closing_line_str = remaining;
  bool preserve_remaining_as_is = false;
  if (auto_indent && buf.cursor.y >= 0)
  {
    int indent = EditorFeatures::get_indent_level(buf.line_mut(buf.cursor.y));

    std::string html_tag;
    if (HtmlFeatures::is_markup_tag_extension(buf.filepath)
        && HtmlFeatures::is_between_matching_tags(buf.line_mut(buf.cursor.y), remaining, html_tag))
    {
      int closing_indent = indent;
      int inner_indent = indent + tab_size;

      new_line_str = EditorFeatures::get_indent_string(inner_indent, tab_size);
      closing_line_str = EditorFeatures::get_indent_string(closing_indent, tab_size) + remaining;
      split_closing_bracket_line = true;
    }
    else
    {
      indent = EditorFeatures::indent_for_new_line(
          buf.filepath, buf.line_mut(buf.cursor.y), buf.cursor.x, tab_size);
    }

    if (!split_closing_bracket_line)
    {
      new_line_str = EditorFeatures::get_indent_string(indent, tab_size);
    }

    if (EditorFeatures::should_dedent(remaining)
        || (is_lua_buffer(buf) && EditorFeatures::should_lua_dedent(remaining)))
    {
      size_t content_start = remaining.find_first_not_of(" \t");
      std::string trimmed_remaining =
          content_start == std::string::npos ? "" : remaining.substr(content_start);
      int closing_indent = EditorFeatures::get_indent_level(buf.line_mut(buf.cursor.y));
      closing_line_str =
          EditorFeatures::get_indent_string(closing_indent, tab_size) + trimmed_remaining;
      split_closing_bracket_line = true;
    }
    else if (!remaining.empty() && buf.cursor.x > 0)
    {
      const char left = current_line[buf.cursor.x - 1];
      const bool has_non_space_remaining = remaining.find_first_not_of(" \t") != std::string::npos;
      if (!std::isspace(static_cast<unsigned char>(left)) && has_non_space_remaining)
      {
        // When splitting in the middle of content, keep the moved text as-is
        // to avoid adding unexpected extra indentation.
        preserve_remaining_as_is = true;
      }
    }
  }

  if (split_closing_bracket_line)
  {
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, new_line_str);
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 2, closing_line_str);
    buf.cursor.y++;
    buf.cursor.x = new_line_str.length();
  }
  else if (preserve_remaining_as_is)
  {
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, remaining);
    buf.cursor.y++;
    buf.cursor.x = 0;
  }
  else
  {
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, new_line_str + remaining);
    buf.cursor.y++;
    buf.cursor.x = new_line_str.length();
  }
  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}
