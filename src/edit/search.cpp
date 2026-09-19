#include "editor.h"
#include "jot/editor/search_controller.h"
#include "jot/lua/api.h"
#include <algorithm>
#include <cctype>
#include <regex>

namespace
{
  std::string to_lower_ascii(std::string text)
  {
    std::transform(text.begin(),
                   text.end(),
                   text.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return text;
  }

  bool is_word_char(unsigned char c)
  {
    return std::isalnum(c) || c == '_';
  }

  bool is_whole_word_match(const std::string &line, size_t pos, size_t len)
  {
    if (len == 0)
    {
      return false;
    }
    const bool has_prev = pos > 0;
    const bool has_next = (pos + len) < line.size();
    const bool prev_word = has_prev && is_word_char((unsigned char)line[pos - 1]);
    const bool next_word = has_next && is_word_char((unsigned char)line[pos + len]);
    return !prev_word && !next_word;
  }

  std::string search_flags(bool case_sensitive, bool whole_word, bool regex)
  {
    std::string flags = case_sensitive ? "Aa" : "aa";
    flags += whole_word ? ",W" : ",w";
    if (regex)
    {
      flags += ",.*";
    }
    return flags;
  }

  std::regex make_search_regex(const std::string &pattern, bool case_sensitive)
  {
    auto flags = std::regex::ECMAScript;
    if (!case_sensitive)
    {
      flags |= std::regex::icase;
    }
    return std::regex(pattern, flags);
  }

  bool normalize_non_empty_selection(const FileBuffer &buf, Cursor &start, Cursor &end)
  {
    if (!buf.selection.active)
    {
      return false;
    }
    start = buf.selection.start;
    end = buf.selection.end;
    if (start.y > end.y || (start.y == end.y && start.x > end.x))
    {
      std::swap(start, end);
    }
    if (start.y == end.y && start.x == end.x)
    {
      return false;
    }
    if (buf.line_count() == 0)
    {
      return false;
    }
    start.y = std::clamp(start.y, 0, (int)buf.line_count() - 1);
    end.y = std::clamp(end.y, 0, (int)buf.line_count() - 1);
    start.x = std::clamp(start.x, 0, (int)buf.line(start.y).size());
    end.x = std::clamp(end.x, 0, (int)buf.line(end.y).size());
    return start.y != end.y || start.x < end.x;
  }
} // namespace

void SearchController::clear_scope()
{
  scoped_to_selection_ = false;
  scope_start_ = {0, 0};
  scope_end_ = {0, 0};
}

void SearchController::close()
{
  if (!visible_)
  {
    return;
  }
  visible_ = false;
  clear_scope();
}

void SearchController::clear_results()
{
  results_.clear();
  result_index_ = -1;
}

void SearchController::reset()
{
  // What initialize_state_defaults() used to spell out member by member: the
  // panel is hidden, nothing is selected and every flag is back to its default.
  // The query and the match list are deliberately left alone, the way they were
  // left alone there.
  visible_ = false;
  result_index_ = -1;
  case_sensitive_ = false;
  whole_word_ = false;
  regex_ = false;
  replace_visible_ = false;
  focus_replace_ = false;
  scoped_to_selection_ = false;
  scope_start_ = {0, 0};
  scope_end_ = {0, 0};
}

void SearchController::open()
{
  if (visible_)
  {
    focus_replace_ = false;
    editor_.needs_redraw = true;
    return;
  }
  toggle();
}

void SearchController::toggle()
{
  visible_ = !visible_;
  if (!visible_)
  {
    clear_scope();
    return;
  }

  clear_scope();
  focus_replace_ = false;
  auto &buf = editor_.get_buffer();
  if (buf.selection.active && buf.selection.start.y == buf.selection.end.y)
  {
    Cursor start = buf.selection.start;
    Cursor end = buf.selection.end;
    if (start.x > end.x)
    {
      std::swap(start, end);
    }
    if (start.y >= 0 && start.y < (int)buf.line_count())
    {
      const std::string &line = buf.line(start.y);
      int from = std::clamp(start.x, 0, (int)line.size());
      int to = std::clamp(end.x, 0, (int)line.size());
      if (to > from)
      {
        query_ = line.substr((size_t)from, (size_t)(to - from));
      }
    }
  }

  if (!query_.empty())
  {
    perform();
  }
  else
  {
    results_.clear();
    result_index_ = -1;
  }
  editor_.needs_redraw = true;
}

bool SearchController::open_scoped_replace_from_selection()
{
  auto &buf = editor_.get_buffer();
  Cursor start;
  Cursor end;
  if (!normalize_non_empty_selection(buf, start, end))
  {
    return false;
  }

  scoped_to_selection_ = true;
  scope_start_ = start;
  scope_end_ = end;
  visible_ = true;
  replace_visible_ = true;
  focus_replace_ = false;
  results_.clear();
  result_index_ = -1;

  if (start.y == end.y)
  {
    const std::string &line = buf.line(start.y);
    if (end.x > start.x)
    {
      query_ = line.substr((size_t)start.x, (size_t)(end.x - start.x));
    }
  }
  else
  {
    query_.clear();
  }

  if (!query_.empty())
  {
    perform();
  }
  else
  {
    editor_.set_message("Find/replace in selection");
  }
  editor_.needs_redraw = true;
  return true;
}

void SearchController::perform()
{
  auto &buf = editor_.get_buffer();
  const int cursor_y = buf.cursor.y;
  const int cursor_x = buf.cursor.x;

  auto match_in_scope = [&](int line_idx, int col, int len)
  {
    if (!scoped_to_selection_)
    {
      return true;
    }
    if (line_idx < scope_start_.y || line_idx > scope_end_.y)
    {
      return false;
    }
    const int end_col = col + len;
    if (line_idx == scope_start_.y && col < scope_start_.x)
    {
      return false;
    }
    if (line_idx == scope_end_.y && end_col > scope_end_.x)
    {
      return false;
    }
    return true;
  };

  results_.clear();
  result_index_ = -1;

  if (query_.empty())
  {
    editor_.set_message("Search cleared ["
                + search_flags(case_sensitive_, whole_word_, regex_) + "]");
    editor_.needs_redraw = true;
    return;
  }

  if (regex_)
  {
    std::regex re;
    try
    {
      re = make_search_regex(query_, case_sensitive_);
    }
    catch (const std::regex_error &e)
    {
      editor_.set_message(std::string("Regex error: ") + e.what());
      editor_.needs_redraw = true;
      return;
    }

    for (size_t line_idx = 0; line_idx < buf.line_count(); line_idx++)
    {
      const std::string &line = buf.line(line_idx);
      auto begin = std::sregex_iterator(line.begin(), line.end(), re);
      auto end = std::sregex_iterator();
      for (auto it = begin; it != end; ++it)
      {
        int pos = (int)it->position();
        int len = std::max(1, (int)it->length());
        if (whole_word_ && !is_whole_word_match(line, (size_t)pos, (size_t)len))
        {
          continue;
        }
        if (!match_in_scope((int)line_idx, pos, len))
        {
          continue;
        }
        results_.push_back({(int)line_idx, pos, len});
      }
    }
  }
  else
  {
    std::string query_cmp = case_sensitive_ ? query_ : to_lower_ascii(query_);
    const size_t query_len = query_.size();

    for (size_t i = 0; i < buf.line_count(); i++)
    {
      const std::string &original_line = buf.line(i);
      std::string line_cmp = case_sensitive_ ? original_line : to_lower_ascii(original_line);

      size_t pos = 0;
      while ((pos = line_cmp.find(query_cmp, pos)) != std::string::npos)
      {
        if (whole_word_ && !is_whole_word_match(original_line, pos, query_len))
        {
          pos++;
          continue;
        }
        if (!match_in_scope((int)i, (int)pos, (int)query_len))
        {
          pos += std::max<size_t>(1, query_len);
          continue;
        }
        results_.push_back({(int)i, (int)pos, (int)query_len});
        pos += std::max<size_t>(1, query_len);
      }
    }
  }

  if (results_.empty())
  {
    editor_.set_message("No matches ["
                + search_flags(case_sensitive_, whole_word_, regex_)
                + (scoped_to_selection_ ? ",Sel" : "") + "]");
    editor_.needs_redraw = true;
    return;
  }

  SearchMatch cursor_match{cursor_y, cursor_x, 0};
  auto it = std::lower_bound(results_.begin(), results_.end(), cursor_match);
  result_index_ = (it == results_.end()) ? 0 : (int)(it - results_.begin());

  buf.cursor.y = results_[result_index_].line;
  buf.cursor.x = results_[result_index_].col;
  editor_.clamp_cursor(editor_.get_pane().buffer_id);
  editor_.ensure_cursor_visible();

  editor_.set_message(std::to_string(results_.size()) + " match(es) ["
              + search_flags(case_sensitive_, whole_word_, regex_)
              + (scoped_to_selection_ ? ",Sel" : "") + "]");
  editor_.needs_redraw = true;
}

void SearchController::find_next()
{
  if (results_.empty())
  {
    perform();
    return;
  }

  const int prev_index = result_index_;
  const int count = (int)results_.size();
  if (result_index_ < 0)
  {
    result_index_ = 0;
  }
  else
  {
    result_index_ = (result_index_ + 1) % count;
  }

  auto &buf = editor_.get_buffer();
  buf.cursor.y = results_[result_index_].line;
  buf.cursor.x = results_[result_index_].col;
  editor_.clamp_cursor(editor_.get_pane().buffer_id);
  editor_.ensure_cursor_visible();
  editor_.record_jump();

  const bool wrapped = prev_index >= 0 && result_index_ <= prev_index;
  editor_.set_message(std::to_string(result_index_ + 1) + "/" + std::to_string(results_.size())
              + (wrapped ? " (wrapped)" : ""));
}

void SearchController::find_prev()
{
  if (results_.empty())
  {
    perform();
    return;
  }

  const int prev_index = result_index_;
  const int count = (int)results_.size();
  if (result_index_ <= 0)
  {
    result_index_ = count - 1;
  }
  else
  {
    result_index_--;
  }

  auto &buf = editor_.get_buffer();
  buf.cursor.y = results_[result_index_].line;
  buf.cursor.x = results_[result_index_].col;
  editor_.clamp_cursor(editor_.get_pane().buffer_id);
  editor_.ensure_cursor_visible();
  editor_.record_jump();

  const bool wrapped = prev_index >= 0 && result_index_ >= prev_index;
  editor_.set_message(std::to_string(result_index_ + 1) + "/" + std::to_string(results_.size())
              + (wrapped ? " (wrapped)" : ""));
}

bool SearchController::replace_current()
{
  if (query_.empty() || results_.empty() || result_index_ < 0
      || result_index_ >= (int)results_.size())
  {
    perform();
    return false;
  }

  auto &buf = editor_.get_buffer();
  if (buf.is_lazy())
  {
    buf.materialize();
  }

  SearchMatch match = results_[result_index_];
  if (match.line < 0 || match.line >= (int)buf.lines.size())
  {
    return false;
  }

  std::string replacement = replace_text_;
  if (regex_)
  {
    try
    {
      std::regex re = make_search_regex(query_, case_sensitive_);
      const std::string &matched =
          buf.lines[match.line].substr((size_t)match.col, (size_t)match.len);
      replacement = std::regex_replace(matched, re, replace_text_);
    }
    catch (const std::regex_error &e)
    {
      editor_.set_message(std::string("Regex error: ") + e.what());
      return false;
    }
  }

  editor_.save_state();
  std::string &line = buf.lines[match.line];
  match.col = std::clamp(match.col, 0, (int)line.size());
  match.len = std::clamp(match.len, 0, (int)line.size() - match.col);
  line.replace((size_t)match.col, (size_t)match.len, replacement);
  buf.cursor = {match.col + (int)replacement.size(), match.line};
  buf.preferred_x = buf.cursor.x;
  buf.modified = true;
  buf.selection.active = false;
  if (scoped_to_selection_ && match.line == scope_end_.y)
  {
    scope_end_.x += (int)replacement.size() - match.len;
    scope_end_.x = std::clamp(scope_end_.x, 0, (int)line.size());
  }

  if (editor_.lua_api)
  {
    editor_.lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    editor_.notify_lsp_change(buf.filepath);
  }

  perform();
  find_next();
  editor_.needs_redraw = true;
  return true;
}

bool SearchController::replace_all()
{
  if (query_.empty())
  {
    return false;
  }
  perform();
  if (results_.empty())
  {
    return false;
  }

  auto &buf = editor_.get_buffer();
  if (buf.is_lazy())
  {
    buf.materialize();
  }

  std::regex re;
  if (regex_)
  {
    try
    {
      re = make_search_regex(query_, case_sensitive_);
    }
    catch (const std::regex_error &e)
    {
      editor_.set_message(std::string("Regex error: ") + e.what());
      return false;
    }
  }

  editor_.save_state();
  int total = 0;
  for (int i = (int)results_.size() - 1; i >= 0; i--)
  {
    SearchMatch match = results_[i];
    if (match.line < 0 || match.line >= (int)buf.lines.size())
    {
      continue;
    }
    std::string &line = buf.lines[match.line];
    match.col = std::clamp(match.col, 0, (int)line.size());
    match.len = std::clamp(match.len, 0, (int)line.size() - match.col);
    std::string replacement = replace_text_;
    if (regex_)
    {
      const std::string matched = line.substr((size_t)match.col, (size_t)match.len);
      replacement = std::regex_replace(matched, re, replace_text_);
    }
    line.replace((size_t)match.col, (size_t)match.len, replacement);
    total++;
  }

  if (total <= 0)
  {
    editor_.set_message("No matches found");
    return false;
  }

  buf.modified = true;
  buf.selection.active = false;
  clear_scope();
  editor_.clamp_cursor(editor_.get_pane().buffer_id);
  editor_.ensure_cursor_visible();
  if (editor_.lua_api)
  {
    editor_.lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    editor_.notify_lsp_change(buf.filepath);
  }
  perform();
  editor_.set_message("Replaced " + std::to_string(total) + " occurrence(s)");
  editor_.needs_redraw = true;
  return true;
}

void SearchController::handle_panel_input(int ch, bool is_ctrl, bool is_shift)
{
  if (ch == 27)
  {
    visible_ = false;
    clear_scope();
    editor_.needs_redraw = true;
    editor_.set_message("");
    return;
  }

  if (is_ctrl && (ch == 'f' || ch == 'F'))
  {
    find_next();
    editor_.needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'h' || ch == 'H'))
  {
    replace_visible_ = !replace_visible_;
    focus_replace_ = replace_visible_;
    editor_.needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'r' || ch == 'R'))
  {
    if (is_shift)
    {
      replace_all();
    }
    else
    {
      replace_current();
    }
    editor_.needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'w' || ch == 'W'))
  {
    whole_word_ = !whole_word_;
    perform();
    editor_.needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'e' || ch == 'E'))
  {
    regex_ = !regex_;
    perform();
    editor_.needs_redraw = true;
    return;
  }

  if (is_ctrl && (ch == 'l' || ch == 'L'))
  {
    if (focus_replace_)
    {
      replace_text_.clear();
    }
    else
    {
      query_.clear();
      results_.clear();
      result_index_ = -1;
    }
    editor_.set_message("Search cleared ["
                + search_flags(case_sensitive_, whole_word_, regex_) + "]");
    editor_.needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13 || ch == 1009)
  {
    if (is_shift && (ch == '\n' || ch == 13))
    {
      find_prev();
    }
    else
    {
      find_next();
    }
    editor_.needs_redraw = true;
    return;
  }

  if (ch == 1008)
  {
    find_prev();
    editor_.needs_redraw = true;
    return;
  }

  if (ch == 127 || ch == 8)
  {
    std::string &target = focus_replace_ ? replace_text_ : query_;
    if (!target.empty())
    {
      target.pop_back();
      if (!focus_replace_)
      {
        perform();
      }
      editor_.needs_redraw = true;
    }
    return;
  }

  if (ch == '\t' || ch == 9)
  {
    if (replace_visible_)
    {
      focus_replace_ = !focus_replace_;
    }
    else
    {
      case_sensitive_ = !case_sensitive_;
      perform();
    }
    editor_.needs_redraw = true;
    return;
  }

  if (ch < 32 || ch > 126)
  {
    return;
  }

  std::string &target = focus_replace_ ? replace_text_ : query_;
  target += (char)ch;
  if (!focus_replace_)
  {
    perform();
  }
  editor_.needs_redraw = true;
}

bool SearchController::handle_mouse(int x, int y, bool is_click)
{
  if (!visible_ || !editor_.ui)
  {
    return false;
  }
  int w = std::min(72, std::max(42, editor_.ui->get_render_width() / 2));
  int h = replace_visible_ ? 5 : 4;
  int px = std::max(0, editor_.ui->get_width() - w - 2);
  if (px + w > editor_.ui->get_width())
  {
    w = std::max(20, editor_.ui->get_width() - px);
  }
  const int py = editor_.topbar_height() + editor_.tab_height;
  if (x < px || x >= px + w || y < py || y >= py + h)
  {
    return false;
  }
  if (!is_click)
  {
    return true; // hover over the panel: consume, no action
  }

  const int label_w = 9;
  const int input_x = px + label_w;

  // Chips row (title row): Aa / W / .* / Sel toggles, right-aligned.
  if (y == py)
  {
    std::string chips;
    chips += case_sensitive_ ? " Aa " : " aa ";
    chips += whole_word_ ? " W " : " w ";
    if (regex_)
    {
      chips += " .* ";
    }
    if (scoped_to_selection_)
    {
      chips += " Sel ";
    }
    std::string count = "0/0";
    if (result_index_ >= 0 && !results_.empty())
    {
      count = std::to_string(result_index_ + 1) + "/" + std::to_string(results_.size());
    }
    chips += " " + count + " ";
    int chip_x = std::max(px + 1, px + w - (int)chips.size() - 1);
    if (x >= chip_x && x < chip_x + 4)
    {
      case_sensitive_ = !case_sensitive_;
      perform();
      editor_.needs_redraw = true;
      return true;
    }
    chip_x += 4;
    if (x >= chip_x && x < chip_x + 3)
    {
      whole_word_ = !whole_word_;
      perform();
      editor_.needs_redraw = true;
      return true;
    }
    chip_x += 3;
    if (regex_)
    {
      if (x >= chip_x && x < chip_x + 4)
      {
        regex_ = false;
        perform();
        editor_.needs_redraw = true;
        return true;
      }
      chip_x += 4;
    }
    if (scoped_to_selection_)
    {
      if (x >= chip_x && x < chip_x + 5)
      {
        scoped_to_selection_ = false;
        perform();
        editor_.needs_redraw = true;
        return true;
      }
      chip_x += 5;
    }
    return true;
  }

  // Find / Replace input rows: clicking the input focuses the field.
  if (y == py + 1)
  {
    if (x >= input_x)
    {
      focus_replace_ = false;
      editor_.needs_redraw = true;
    }
    return true;
  }
  if (replace_visible_ && y == py + 2)
  {
    if (x >= input_x)
    {
      focus_replace_ = true;
      editor_.needs_redraw = true;
    }
    return true;
  }
  return true;
}
