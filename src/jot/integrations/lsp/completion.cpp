// Editor-side LSP completion: request triggering, client-side filtering and
// ranking, and applying the selected item (including snippet expansion).
#include "editor.h"
#include "jot/lua/api.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{
  bool is_identifier_char(char c)
  {
    unsigned char uc = (unsigned char)c;
    return std::isalnum(uc) || c == '_';
  }

  Cursor current_completion_start(const FileBuffer &buf)
  {
    if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
    {
      return {0, 0};
    }
    const std::string &line = buf.line(buf.cursor.y);
    int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
    int start = cursor;
    while (start > 0 && is_identifier_char(line[start - 1]))
    {
      start--;
    }
    return {start, buf.cursor.y};
  }

  std::string completion_prefix_from(const FileBuffer &buf, const Cursor &start)
  {
    if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count() || start.y != buf.cursor.y)
    {
      return "";
    }
    const std::string &line = buf.line(buf.cursor.y);
    int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
    int prefix_start = std::clamp(start.x, 0, cursor);
    return line.substr((size_t)prefix_start, (size_t)(cursor - prefix_start));
  }

  bool is_subsequence_case_insensitive(const std::string &needle, const std::string &haystack)
  {
    if (needle.empty())
    {
      return true;
    }
    size_t j = 0;
    for (size_t i = 0; i < haystack.size() && j < needle.size(); i++)
    {
      if (std::tolower((unsigned char)haystack[i]) == std::tolower((unsigned char)needle[j]))
      {
        j++;
      }
    }
    return j == needle.size();
  }

  int completion_match_score(const std::string &query, const LSPCompletionItem &item)
  {
    int score = query.empty() ? 50 : 0;

    const std::string q = lsp_internal::to_lower_copy(query);
    const std::string label = lsp_internal::to_lower_copy(item.label);
    const std::string filter =
        lsp_internal::to_lower_copy(item.filter_text.empty() ? item.label : item.filter_text);
    const std::string insert = lsp_internal::to_lower_copy(item.insert_text);

    if (!query.empty())
    {
      if (label == q || filter == q || insert == q)
      {
        score = 10000;
      }
      else if (label.rfind(q, 0) == 0 || filter.rfind(q, 0) == 0 || insert.rfind(q, 0) == 0)
      {
        score = 7000 - (int)label.size();
      }
      else
      {
        size_t label_pos = label.find(q);
        size_t filter_pos = filter.find(q);
        size_t insert_pos = insert.find(q);
        size_t best_pos = std::min(label_pos, std::min(filter_pos, insert_pos));
        if (best_pos != std::string::npos)
        {
          score = 4000 - (int)best_pos;
        }
        else if (is_subsequence_case_insensitive(q, label)
                 || is_subsequence_case_insensitive(q, filter))
        {
          score = 1500;
        }
      }
    }
    if (score <= 0)
    {
      return 0;
    }
    if (item.preselect)
    {
      score += 250;
    }
    if (item.deprecated)
    {
      score -= 200;
    }
    switch (item.kind)
    {
    case 2:
    case 3:
    case 5:
    case 6:
    case 10:
      score += 40;
      break;
    case 14:
      score -= 20;
      break;
    default:
      break;
    }
    return score;
  }

  struct SnippetExpansion
  {
    std::string text;
    int cursor_offset = -1;
  };

  SnippetExpansion expand_lsp_snippet(const std::string &snippet)
  {
    SnippetExpansion expansion;
    std::string &out = expansion.text;
    out.reserve(snippet.size());

    for (size_t i = 0; i < snippet.size(); i++)
    {
      char c = snippet[i];
      if (c == '\\')
      {
        if (i + 1 < snippet.size())
        {
          out.push_back(snippet[i + 1]);
          i++;
        }
        continue;
      }
      if (c != '$')
      {
        out.push_back(c);
        continue;
      }

      if (i + 1 >= snippet.size())
      {
        out.push_back(c);
        continue;
      }
      if (std::isdigit((unsigned char)snippet[i + 1]))
      {
        size_t start = i + 1;
        while (i + 1 < snippet.size() && std::isdigit((unsigned char)snippet[i + 1]))
        {
          i++;
        }
        int tabstop = std::atoi(snippet.substr(start, i - start + 1).c_str());
        if (tabstop == 0 || expansion.cursor_offset < 0)
        {
          expansion.cursor_offset = (int)out.size();
        }
        continue;
      }
      if (snippet[i + 1] == '{')
      {
        size_t j = i + 2;
        std::string inner;
        while (j < snippet.size() && snippet[j] != '}')
        {
          inner.push_back(snippet[j]);
          j++;
        }
        if (j < snippet.size() && snippet[j] == '}')
        {
          i = j;
        }
        else
        {
          i = snippet.size();
        }

        size_t pos = 0;
        while (pos < inner.size() && std::isdigit((unsigned char)inner[pos]))
        {
          pos++;
        }
        int tabstop = pos > 0 ? std::atoi(inner.substr(0, pos).c_str()) : -1;
        std::string value;
        if (pos < inner.size() && inner[pos] == ':')
        {
          value = inner.substr(pos + 1);
        }
        else if (pos < inner.size() && inner[pos] == '|')
        {
          size_t end = inner.find('|', pos + 1);
          std::string choices = end == std::string::npos ? inner.substr(pos + 1)
                                                         : inner.substr(pos + 1, end - pos - 1);
          size_t comma = choices.find(',');
          value = choices.substr(0, comma);
        }
        else if (pos == 0)
        {
          size_t colon = inner.find(':');
          value = colon == std::string::npos ? "" : inner.substr(colon + 1);
        }

        if (tabstop == 0 || (tabstop > 0 && expansion.cursor_offset < 0))
        {
          expansion.cursor_offset = (int)out.size();
        }
        out.append(value);
        continue;
      }
      out.push_back(c);
    }

    if (expansion.cursor_offset < 0)
    {
      expansion.cursor_offset = (int)out.size();
    }
    return expansion;
  }

  char fold_ascii_char(char c)
  {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  }

  size_t utf8_char_count(const std::string &s)
  {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++)
    {
      if (((unsigned char)s[i] & 0xC0) != 0x80)
      {
        n++;
      }
    }
    return n;
  }

  // Drops the first `count` UTF-8 characters from `s`.
  std::string utf8_drop_prefix(const std::string &s, size_t count)
  {
    size_t i = 0;
    while (i < s.size() && count > 0)
    {
      if (((unsigned char)s[i] & 0xC0) != 0x80)
      {
        count--;
      }
      i++;
    }
    return s.substr(i);
  }

  // nvim-cmp ghost text: the insert text minus the already-typed prefix.
  // The prefix is dropped only when it actually leads the text (case-
  // insensitive), so mismatched prefixes keep the full preview.
  std::string ghost_text_for(const LSPCompletionItem &item, const std::string &prefix)
  {
    std::string text = item.insert_text.empty() ? item.label : item.insert_text;
    if (item.insert_text_format == 2) // snippet: preview the expanded plain text
    {
      text = expand_lsp_snippet(text).text;
    }
    if (text.empty() || prefix.empty())
    {
      return text;
    }
    size_t i = 0;
    while (i < prefix.size() && i < text.size()
           && fold_ascii_char(prefix[i]) == fold_ascii_char(text[i]))
    {
      i++;
    }
    if (i == prefix.size())
    {
      text = utf8_drop_prefix(text, utf8_char_count(prefix));
    }
    return text;
  }
} // namespace

void Editor::hide_lsp_completion()
{
  lsp_completion_visible = false;
  lsp_completion_manual_request = false;
  lsp_completion_selected = 0;
  lsp_completion_replace_start = {0, 0};
  lsp_completion_items.clear();
  lsp_completion_all_items.clear();
  lsp_completion_filepath.clear();
  lsp_completion_prefix.clear();
  lsp_completion_ghost_text.clear();
}

bool Editor::refresh_lsp_completion_filter()
{
  if (lsp_completion_all_items.empty() || buffers.empty() || panes.empty())
  {
    lsp_completion_visible = false;
    lsp_completion_items.clear();
    return false;
  }

  auto &buf = get_buffer();
  if (!lsp_completion_filepath.empty() && !lsp_internal::same_path(lsp_completion_filepath, buf.filepath))
  {
    hide_lsp_completion();
    return false;
  }
  if (buf.cursor.y != lsp_completion_replace_start.y
      || buf.cursor.x < lsp_completion_replace_start.x)
  {
    hide_lsp_completion();
    return false;
  }

  std::string query = completion_prefix_from(buf, lsp_completion_replace_start);
  std::string selected_label;
  if (lsp_completion_selected >= 0 && lsp_completion_selected < (int)lsp_completion_items.size())
  {
    selected_label = lsp_completion_items[lsp_completion_selected].label;
  }

  std::vector<std::pair<int, LSPCompletionItem>> ranked;
  ranked.reserve(lsp_completion_all_items.size());
  for (const auto &item : lsp_completion_all_items)
  {
    int score = completion_match_score(query, item);
    if (query.empty() || score > 0)
    {
      ranked.push_back({score, item});
    }
  }

  std::stable_sort(ranked.begin(),
                   ranked.end(),
                   [](const auto &a, const auto &b)
                   {
                     if (a.first != b.first)
                     {
                       return a.first > b.first;
                     }
                     const std::string &as =
                         a.second.sort_text.empty() ? a.second.label : a.second.sort_text;
                     const std::string &bs =
                         b.second.sort_text.empty() ? b.second.label : b.second.sort_text;
                     return as < bs;
                   });

  lsp_completion_items.clear();
  const int max_items = 200;
  for (int i = 0; i < (int)ranked.size() && i < max_items; i++)
  {
    lsp_completion_items.push_back(std::move(ranked[i].second));
  }

  lsp_completion_prefix = query;
  lsp_completion_selected = 0;
  for (int i = 0; i < (int)lsp_completion_items.size(); i++)
  {
    if (!selected_label.empty() && lsp_completion_items[i].label == selected_label)
    {
      lsp_completion_selected = i;
      break;
    }
    if (selected_label.empty() && lsp_completion_items[i].preselect)
    {
      lsp_completion_selected = i;
      break;
    }
  }
  lsp_completion_visible = !lsp_completion_items.empty();
  update_lsp_completion_ghost();
  return lsp_completion_visible;
}

// nvim-cmp ghost text: the selected item's insert text minus the typed
// prefix, drawn dimmed at the cursor while the popup stays open. Recomputed
// on every filter refresh AND on direct selection moves (Up/Down), so the
// preview never lags a frame behind the highlighted row.
void Editor::update_lsp_completion_ghost()
{
  lsp_completion_ghost_text.clear();
  if (lsp_completion_selected >= 0 && lsp_completion_selected < (int)lsp_completion_items.size())
  {
    lsp_completion_ghost_text =
        ghost_text_for(lsp_completion_items[lsp_completion_selected], lsp_completion_prefix);
  }
}

void Editor::request_lsp_completion(bool manual, char trigger_character)
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  if (buf.filepath.empty())
  {
    if (manual)
    {
      set_message("Save file first to use LSP completion");
    }
    return;
  }

  if (!manual)
  {
    if (!(std::isalnum((unsigned char)trigger_character) || trigger_character == '_'
          || trigger_character == '.' || trigger_character == ':' || trigger_character == '>'
          || trigger_character == '<' || trigger_character == '/'))
    {
      return;
    }

    int prefix_len = 0;
    int i = std::min(buf.cursor.x, (int)buf.line(buf.cursor.y).size());
    while (i > 0 && is_identifier_char(buf.line(buf.cursor.y)[i - 1]))
    {
      prefix_len++;
      i--;
    }
    bool html_file = lsp_internal::is_html_filepath(buf.filepath);
    bool script_file = lsp_internal::is_script_lsp_filepath(buf.filepath);
    bool punctuation_trigger = trigger_character == '.' || trigger_character == ':'
                               || trigger_character == '>' || trigger_character == '<'
                               || trigger_character == '/';
    int min_prefix = (html_file || script_file) ? 1 : 2;
    if (!punctuation_trigger && prefix_len < min_prefix)
    {
      return;
    }
  }

  Cursor replace_start = current_completion_start(buf);
  bool has_builtin_html = false;

  if (lsp_internal::is_html_filepath(buf.filepath))
  {
    lsp_completion_all_items.clear();
    lsp_internal::append_html_builtin_completions(lsp_completion_all_items);
    lsp_completion_anchor = buf.cursor;
    lsp_completion_replace_start = replace_start;
    lsp_completion_filepath = buf.filepath;
    lsp_completion_prefix = completion_prefix_from(buf, replace_start);
    lsp_completion_manual_request = manual;
    has_builtin_html = refresh_lsp_completion_filter();
    if (has_builtin_html)
    {
      needs_redraw = true;
    }
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    if (manual)
    {
      set_message("No LSP server for this file");
    }
    return;
  }

  // Completion must use current text state, not debounced change state.
  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));

  char trigger = '\0';
  if (trigger_character == '.' || trigger_character == ':' || trigger_character == '>'
      || trigger_character == '<' || trigger_character == '/')
  {
    trigger = trigger_character;
  }

  if (!client->request_completion(buf.filepath, buf.cursor.y, buf.cursor.x, trigger))
  {
    if (manual)
    {
      set_message("LSP completion request failed");
    }
    return;
  }

  if (!has_builtin_html)
  {
    lsp_completion_anchor = buf.cursor;
    lsp_completion_replace_start = replace_start;
    lsp_completion_filepath = buf.filepath;
    lsp_completion_prefix = completion_prefix_from(buf, replace_start);
    lsp_completion_manual_request = manual;
  }
}

bool Editor::apply_selected_lsp_completion()
{
  if (!lsp_completion_visible || lsp_completion_items.empty())
  {
    return false;
  }

  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    buf.materialize();
  }
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size())
  {
    hide_lsp_completion();
    return false;
  }

  int idx = std::clamp(lsp_completion_selected, 0, (int)lsp_completion_items.size() - 1);
  const auto &item = lsp_completion_items[idx];
  std::string text = item.insert_text.empty() ? item.label : item.insert_text;

  int cursor_offset = -1;
  size_t marker_pos = text.find('|');
  if (marker_pos != std::string::npos)
  {
    cursor_offset = (int)marker_pos;
    text.erase(marker_pos, 1);
  }

  // Keep the raw snippet text (tabstops and all) for the bundled snippet
  // engine, which expands it with real placeholders; `text` is the flattened
  // fallback used when no engine handler is registered.
  const std::string raw_snippet_text = text;
  if (item.insert_text_format == 2)
  {
    SnippetExpansion expansion = expand_lsp_snippet(text);
    text = std::move(expansion.text);
    cursor_offset = expansion.cursor_offset;
  }
  if (text.empty())
  {
    hide_lsp_completion();
    return false;
  }

  const std::string &line_ref = buf.line(buf.cursor.y);
  const int cursor = std::clamp(buf.cursor.x, 0, (int)line_ref.size());
  int start = cursor;
  int end = cursor;

  if (item.has_text_edit_range && item.edit_start_line == buf.cursor.y
      && item.edit_end_line == buf.cursor.y)
  {
    start = std::clamp(item.edit_start_char, 0, (int)line_ref.size());
    end = std::clamp(item.edit_end_char, start, (int)line_ref.size());
  }
  else if (lsp_completion_replace_start.y == buf.cursor.y)
  {
    start = std::clamp(lsp_completion_replace_start.x, 0, cursor);
  }
  else
  {
    while (start > 0 && is_identifier_char(line_ref[start - 1]))
    {
      start--;
    }
  }

  // Snippet items (`insert_text_format = 2`) expand through the bundled
  // snippet engine when it registered a handler: it owns tabstops, choices,
  // mirrors and nested snippets, which a plain-text expansion cannot express.
  // 1-based line/column, end-exclusive, matching jot.buffer.apply_edit.
  if (item.insert_text_format == 2 && lua_api
      && lua_api->run_lsp_snippet_handler(raw_snippet_text,
                                          buf.cursor.y + 1,
                                          start + 1,
                                          buf.cursor.y + 1,
                                          end + 1))
  {
    hide_lsp_completion();
    needs_redraw = true;
    return true;
  }

  save_state();

  std::string &line = buf.line_mut(buf.cursor.y);
  int insert_at = cursor;

  if (start < end)
  {
    line.erase(start, end - start);
    insert_at = start;
  }
  else if (start < cursor)
  {
    line.erase(start, cursor - start);
    insert_at = start;
  }
  std::string tail = line.substr(insert_at);
  line.erase(insert_at);

  size_t segment_start = 0;
  std::vector<std::string> inserted_lines;
  while (true)
  {
    size_t nl = text.find('\n', segment_start);
    if (nl == std::string::npos)
    {
      inserted_lines.push_back(text.substr(segment_start));
      break;
    }
    inserted_lines.push_back(text.substr(segment_start, nl - segment_start));
    segment_start = nl + 1;
  }
  if (inserted_lines.empty())
  {
    inserted_lines.push_back("");
  }

  line.insert(insert_at, inserted_lines.front());
  int insert_line = buf.cursor.y;
  for (size_t i = 1; i < inserted_lines.size(); i++)
  {
    buf.lines.insert(buf.lines.begin() + insert_line + (int)i, inserted_lines[i]);
  }
  buf.line_mut(insert_line + (int)inserted_lines.size() - 1) += tail;

  int target_offset = cursor_offset >= 0 ? cursor_offset : (int)text.size();
  int target_line_delta = 0;
  int target_col = insert_at;
  for (int i = 0; i < target_offset && i < (int)text.size(); i++)
  {
    if (text[i] == '\n')
    {
      target_line_delta++;
      target_col = 0;
    }
    else
    {
      target_col++;
    }
  }
  buf.cursor.y = insert_line + target_line_delta;
  buf.cursor.x = target_col;
  buf.preferred_x = buf.cursor.x;
  buf.modified = true;
  buf.selection.active = false;
  ensure_cursor_visible();
  needs_redraw = true;

  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  notify_lsp_change(buf.filepath);

  hide_lsp_completion();
  return true;
}