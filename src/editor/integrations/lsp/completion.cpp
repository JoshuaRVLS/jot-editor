// Editor-side LSP completion: request triggering, client-side filtering and
// ranking, and applying the selected item (including snippet expansion).
#include "core/editor.h"
#include "editor/integrations/lsp/common.h"
#include "lsp/client.h"
#include "lsp/completion_rank.h"
#include "lua/api.h"
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

  char fold_ascii_char(char c)
  {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  }

  // The string the typed word is matched against, in nvim-cmp's order: the text
  // the server asked to be filtered on, else what accepting would insert, else
  // the label.
  const std::string &completion_match_text(const LSPCompletionItem &item)
  {
    if (!item.filter_text.empty())
    {
      return item.filter_text;
    }
    if (!item.insert_text.empty())
    {
      return item.insert_text;
    }
    return item.label;
  }

  // The characters a qualified name is spelled with, for cutting a decoration
  // off a label and a snippet's arguments off a call.
  bool is_qualified_name_char(char c)
  {
    const unsigned char uc = (unsigned char)c;
    return std::isalnum(uc) != 0 || c == '_' || c == ':' || c == '.' || c == '>' || c == '-'
           || c == '~' || c == '!';
  }

  // The name a row is about, without the decoration a server may hang on the
  // label: clangd prefixes every label with a space, or with a bullet for the
  // symbols it would add an include for, and neither belongs in the learned
  // table (a name learned as " quick_scan" would never be recognised again). The
  // text the server matches the typed word against names the symbol, so that is
  // the name; a snippet's `(${1:x})` tail is cut at the first character that
  // cannot be part of a qualified name, which is what leaves `printf` of
  // `printf(${1:format})` and `std::quick_exit` of itself.
  std::string completion_name(const LSPCompletionItem &item)
  {
    const std::string &text = completion_match_text(item);
    std::size_t start = 0;
    while (start < text.size() && !is_qualified_name_char(text[start]))
    {
      // A leading decoration can be multi-byte (the bullet is three), so the
      // scan steps bytes rather than characters.
      start++;
    }
    std::size_t end = start;
    while (end < text.size() && is_qualified_name_char(text[end]))
    {
      end++;
    }
    return text.substr(start, end - start);
  }

  // Whether `text` leads with `prefix`, case-insensitively -- the one test a
  // typed word has to pass for an item to be a completion *of that word*. The
  // list and the preview read it the same way: what is listed is what could be
  // typed to its end, and what is previewed is the rest of it.
  bool text_leads_with(const std::string &text, const std::string &prefix)
  {
    if (prefix.size() > text.size())
    {
      return false;
    }
    for (size_t i = 0; i < prefix.size(); i++)
    {
      if (fold_ascii_char(prefix[i]) != fold_ascii_char(text[i]))
      {
        return false;
      }
    }
    return true;
  }

  // Where a row belongs in the list, in the two parts the ranker compares: the
  // tier the typed word puts it in (an exact hit, a word the typed text leads
  // into, or the open list with nothing typed) and the points the server's own
  // hints carry. The tier is compared first and the learned relevance
  // (lsp/completion_rank.h) second, so a habit beats a shorter name and
  // neither can lift a row over an exact match.
  //
  // The points are in the same units as a label character -- the length penalty
  // the old score carried is kept inside them -- so a list with nothing learned
  // about it comes out in exactly the order it always did.
  struct CompletionScore
  {
    int tier = 0;   // 0 not a completion of the typed word, 1 untyped list, 2 leads, 3 exact
    int points = 0; // preselect / deprecated / kind, minus the label's length
  };

  CompletionScore completion_score(const std::string &query, const LSPCompletionItem &item)
  {
    CompletionScore score;
    const std::string q = lsp_internal::to_lower_copy(query);
    const std::string label = lsp_internal::to_lower_copy(item.label);
    const std::string filter = lsp_internal::to_lower_copy(completion_match_text(item));
    const std::string insert = lsp_internal::to_lower_copy(item.insert_text);

    if (query.empty())
    {
      score.tier = 1; // the list is up before a letter is typed: nothing to match yet
    }
    else if (label == q || filter == q || insert == q)
    {
      score.tier = 3;
    }
    else if (text_leads_with(completion_match_text(item), query))
    {
      score.tier = 2;
      score.points -= (int)item.label.size();
    }
    else
    {
      return score; // tier 0: not a completion of the word being typed
    }

    if (item.preselect)
    {
      score.points += 250;
    }
    if (item.deprecated)
    {
      score.points -= 200;
    }
    switch (item.kind)
    {
    case 2:
    case 3:
    case 5:
    case 6:
    case 10:
      score.points += 40;
      break;
    case 14:
      score.points -= 20;
      break;
    default:
      break;
    }
    return score;
  }

  // Auto-import: the edits a completion item carries are positions in the
  // document as the server saw it -- the request went out before the insert.
  // Text strictly above the insertion point still means what it said; an edit on
  // a later line moved down by however many lines the insert added; an edit that
  // overlaps the text being completed, or starts after it on that same line,
  // cannot be placed honestly from here, so it is dropped rather than written
  // at a guessed column.
  std::vector<LSPTextEdit> remap_additional_edits(const std::vector<LSPTextEdit> &edits,
                                                  int main_line,
                                                  int start_col,
                                                  int added_lines)
  {
    std::vector<LSPTextEdit> out;
    for (LSPTextEdit edit : edits)
    {
      if (edit.end_line < main_line || (edit.end_line == main_line && edit.end_char <= start_col))
      {
        out.push_back(std::move(edit));
        continue;
      }
      if (edit.start_line > main_line)
      {
        edit.start_line += added_lines;
        edit.end_line += added_lines;
        out.push_back(std::move(edit));
      }
    }
    return out;
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

  // A function completion should write the call, not the arguments: clangd
  // answers with `foo(${1:int a}, ${2:int b})`, and expanding that types out
  // parameter names the user never wrote and does not want. A server that sends
  // the same call as plain text is no different, so the rule is about the text,
  // not the format. When the insert text is a bare call -- a callee followed by
  // one argument list that ends the text -- the arguments are dropped and the
  // caret lands inside the empty parens, so the parameters are the user's to
  // type. A declaration-shaped item (`function foo() {}`, or anything after the
  // closing paren) is left alone, as is a function-like item that is not a call
  // at all.
  //
  // `item.kind` is the LSP CompletionItemKind: 2 Method, 3 Function, 4
  // Constructor.
  bool strip_call_arguments(const LSPCompletionItem &item, std::string &text, int &cursor_offset)
  {
    const int kind = item.kind;
    if (kind != 2 && kind != 3 && kind != 4)
    {
      return false;
    }
    const size_t open = text.find('(');
    if (open == std::string::npos || open == 0)
    {
      return false;
    }
    for (size_t i = 0; i < open; i++)
    {
      const unsigned char c = (unsigned char)text[i];
      const bool callee_char = std::isalnum(c) || c == '_' || c == ':' || c == '.' || c == '>'
                               || c == '-' || c == '~' || c == '!';
      if (!callee_char)
      {
        return false; // the text before '(' is not a callee
      }
    }
    const size_t close = text.rfind(')');
    if (close == std::string::npos || close < open)
    {
      return false;
    }
    for (size_t i = close + 1; i < text.size(); i++)
    {
      if (text[i] != ' ' && text[i] != '\t')
      {
        return false; // a body or a trailing `;`: not the call itself
      }
    }
    if (close == open + 1)
    {
      return false; // the argument list is already empty
    }
    text = text.substr(0, open + 1) + ")";
    cursor_offset = (int)open + 1;
    return true;
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

  // nvim-cmp ghost text: the insert text minus the already-typed prefix, so the
  // preview reads as the rest of the word being typed.
  //
  // The list already holds only items the typed word leads into, but the text
  // previewed is the insert text, and the two are not always the same string:
  // an item the server asked us to filter by a *different* name (a snippet whose
  // filter text is the name it matched, say) can be listed and still have no
  // remainder to show. So the preview asks the same question of its own text, and
  // a prefix it does not lead -- or a prefix that is the whole text, leaving no
  // remainder -- previews nothing rather than the whole identifier.
  //
  // Snippets preview as their expanded plain text, and only its first line: a
  // body spanning lines cannot be shown inline at the caret, and the newline's
  // control byte (and every line after it) would paint as garbage on the row.
  std::string ghost_text_for(const LSPCompletionItem &item, const std::string &prefix)
  {
    if (prefix.empty())
    {
      return "";
    }
    std::string text = item.insert_text.empty() ? item.label : item.insert_text;
    // The preview is what accepting writes, so a call whose arguments are
    // dropped previews empty parens too.
    int ignored = -1;
    strip_call_arguments(item, text, ignored);
    if (item.insert_text_format == 2) // snippet: preview the expanded plain text
    {
      text = expand_lsp_snippet(text).text;
    }
    // The same test the list uses, on the text accepting would insert: what is
    // left of the word after the prefix is the remainder -- and a text the prefix
    // does not lead is not the word being typed at all.
    if (!text_leads_with(text, prefix))
    {
      return "";
    }
    text = utf8_drop_prefix(text, utf8_char_count(prefix));
    const size_t first_newline = text.find('\n');
    if (first_newline != std::string::npos)
    {
      text.resize(first_newline);
    }
    return text;
  }
} // namespace

void Editor::hide_lsp_completion()
{
  lsp_completion_visible = false;
  lsp_completion_manual_request = false;
  lsp_completion_selected = 0;
  lsp_completion_box_x = -1;
  lsp_completion_box_y = -1;
  lsp_completion_box_w = 0;
  lsp_completion_box_h = 0;
  lsp_completion_box_start = 0;
  lsp_completion_box_rows = 0;
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
  if (!lsp_completion_filepath.empty()
      && !lsp_internal::same_path(lsp_completion_filepath, buf.filepath))
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
  if (query != lsp_completion_prefix)
  {
    // The word being completed just changed with a keystroke. The preview draws
    // from this clock, so it lands on the pause instead of on every keystroke
    // (see lsp_completion_preview_withheld).
    lsp_completion_typing_ms = lsp_internal::now_ms();
  }
  // The rows are rebuilt here on every frame (the render path re-filters), and
  // the selection has to survive that: a server's overloads share one label --
  // every `std::includes` row is labeled "std::includes" -- so carrying it "by
  // label" would snap a Down press back to the first of the family on the next
  // frame and the list could never be browsed past the first overload. The
  // held rows are kept instead: with the same rows in the same order the index
  // is carried as-is, and only a genuinely new list (a keystroke re-ranked it,
  // or the server answered again) follows the item that was selected, by its
  // label and parameter list first, then by label alone.
  std::vector<LSPCompletionItem> previous_items = std::move(lsp_completion_items);
  const int previous_selected = lsp_completion_selected;
  std::string selected_label;
  std::string selected_detail;
  if (previous_selected >= 0 && previous_selected < (int)previous_items.size())
  {
    selected_label = previous_items[previous_selected].label;
    selected_detail = previous_items[previous_selected].label_detail;
  }
  // What makes two rows the same symbol: the label plus the parameter list a
  // server sends in labelDetails, which is what tells one overload from
  // another.
  const auto same_symbol = [](const LSPCompletionItem &a, const LSPCompletionItem &b)
  {
    return a.label == b.label && a.label_detail == b.label_detail && a.insert_text == b.insert_text;
  };

  struct RankedItem
  {
    CompletionScore score;
    int relevance = 0;
    LSPCompletionItem item;
  };
  std::vector<RankedItem> ranked;
  ranked.reserve(lsp_completion_all_items.size());
  for (const auto &item : lsp_completion_all_items)
  {
    // Only an item the typed word leads into is a completion of it. The server
    // answers fuzzily on its own, and the previous keystroke's rows are still in
    // hand while the next request is in flight, so without this the popup lists
    // identifiers the word being typed has nothing to do with -- the noise that
    // made typing a wrong keyword feel like the editor was guessing.
    if (!query.empty() && !text_leads_with(completion_match_text(item), query))
    {
      continue;
    }
    const CompletionScore score = completion_score(query, item);
    if (score.tier > 0)
    {
      ranked.push_back({score, completion_rank_points(completion_name(item)), item});
    }
  }

  std::stable_sort(ranked.begin(),
                   ranked.end(),
                   [](const RankedItem &a, const RankedItem &b)
                   {
                     if (a.score.tier != b.score.tier)
                     {
                       return a.score.tier > b.score.tier;
                     }
                     // What this user reaches for, and what the file already says,
                     // ahead of the server's own hints: the point of the model is
                     // that a familiar name outranks a stranger, however short.
                     if (a.relevance != b.relevance)
                     {
                       return a.relevance > b.relevance;
                     }
                     if (a.score.points != b.score.points)
                     {
                       return a.score.points > b.score.points;
                     }
                     const std::string &as =
                         a.item.sort_text.empty() ? a.item.label : a.item.sort_text;
                     const std::string &bs =
                         b.item.sort_text.empty() ? b.item.label : b.item.sort_text;
                     return as < bs;
                   });

  const int max_items = 200;
  for (int i = 0; i < (int)ranked.size() && i < max_items; i++)
  {
    lsp_completion_items.push_back(std::move(ranked[i].item));
  }

  lsp_completion_prefix = query;
  lsp_completion_selected = 0;
  bool same_rows_ref = previous_items.size() == lsp_completion_items.size();
  for (int i = 0; same_rows_ref && i < (int)lsp_completion_items.size(); i++)
  {
    if (!same_symbol(previous_items[i], lsp_completion_items[i]))
    {
      same_rows_ref = false;
    }
  }
  if (same_rows_ref && previous_selected >= 0 && !lsp_completion_items.empty())
  {
    // The list is the one that was already on screen, so the index still names
    // the row it named: keep it (Up/Down moved it here, not the filter).
    lsp_completion_selected =
        std::clamp(previous_selected, 0, (int)lsp_completion_items.size() - 1);
  }
  else if (!selected_label.empty())
  {
    // A new list: follow the symbol that was selected. The parameter list
    // narrows it to the same overload when that row is still offered, and the
    // label alone keeps the family when it is not.
    bool followed = false;
    if (!selected_detail.empty())
    {
      for (int i = 0; i < (int)lsp_completion_items.size(); i++)
      {
        if (lsp_completion_items[i].label == selected_label
            && lsp_completion_items[i].label_detail == selected_detail)
        {
          lsp_completion_selected = i;
          followed = true;
          break;
        }
      }
    }
    if (!followed)
    {
      for (int i = 0; i < (int)lsp_completion_items.size(); i++)
      {
        if (lsp_completion_items[i].label == selected_label)
        {
          lsp_completion_selected = i;
          break;
        }
      }
    }
  }
  else
  {
    for (int i = 0; i < (int)lsp_completion_items.size(); i++)
    {
      if (lsp_completion_items[i].preselect)
      {
        lsp_completion_selected = i;
        break;
      }
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

// The preview waits for the typing to pause. A preview is the rest of a word
// being typed, so it is worth reading exactly when the user has stopped to look
// at it, and drawing on every keystroke is the flicker that made it noise. The
// clock is the last keystroke that changed the word (set where the query is
// recomputed), so browsing the list with Up/Down after a pause moves the preview
// at once -- only typing restarts the wait.
int Editor::lsp_completion_preview_delay_ms() const
{
  return std::clamp(config.get_int("lsp_completion_ghost_delay_ms", 150), 0, 5000);
}

bool Editor::lsp_completion_preview_withheld() const
{
  const int delay_ms = lsp_completion_preview_delay_ms();
  if (delay_ms <= 0 || lsp_completion_ghost_text.empty())
  {
    return false;
  }
  return lsp_internal::now_ms() < lsp_completion_typing_ms + delay_ms;
}

// Nothing else asks for the frame that ends the wait, so the frame loop does, and
// this is the query for it: the preview was withheld on an earlier frame and is
// due now. A deadline *window* would not do -- frames do not tick while the
// editor is busy, so the wait can end with the window already behind it, and the
// preview would then stay hidden until the next keystroke. Asking once, on the
// first sight of the clock past the delay, cannot be missed however late that
// sight is; and asking only when the last frame really did withhold it (rather
// than whenever the preview is not on screen) means a preview the painter
// declines for another reason -- the caret is before a bracket, the pane is not
// the focused one -- does not ask for frames forever.
bool Editor::lsp_completion_preview_due_soon()
{
  const bool withheld = lsp_completion_preview_withheld();
  const bool due = !withheld && lsp_completion_preview_was_withheld;
  lsp_completion_preview_was_withheld = withheld;
  return due;
}

bool Editor::completion_rank_enabled()
{
  return config.get_bool("lsp_completion_learn", true);
}

int64_t Editor::completion_half_life_seconds()
{
  const int days = config.get_int("lsp_completion_learn_half_life_days",
                                  (int)(CompletionRank::kDefaultHalfLifeSeconds / 86400));
  // A negative half-life would run a count backwards; 0 is the setting that
  // never forgets, so anything less is read as off rather than as a rate.
  return days > 0 ? (int64_t)days * 86400 : 0;
}

void Editor::load_completion_usage()
{
  if (completion_usage_loaded)
  {
    return;
  }
  // Marked loaded whether or not the file was there: the same missing file is not
  // looked for again on every request of the session.
  completion_usage_loaded = true;
  if (!completion_rank_enabled())
  {
    return;
  }
  completion_usage.set_half_life(completion_half_life_seconds());
  CompletionRank::load_usage(CompletionRank::usage_file_path(), completion_usage);
  // Faded here, once, rather than on every row: what a habit is worth has to be
  // settled before the list is ranked, and the read is where the session begins.
  // The file is only written back if something is accepted, and a count left
  // undecayed on disk reads the same next time -- the age is measured from the
  // stamp, so nothing is lost by not writing it.
  completion_usage.decay(CompletionRank::now_seconds());
}

void Editor::save_completion_usage()
{
  if (!completion_usage_dirty)
  {
    return;
  }
  completion_usage_dirty = false;
  CompletionRank::save_usage(CompletionRank::usage_file_path(), completion_usage);
}

void Editor::rebuild_completion_context()
{
  completion_context.clear();
  if (!completion_rank_enabled() || buffers.empty())
  {
    return;
  }
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  ++completion_context_rebuilds;
  completion_context.rebuild(buf.lines, buf.cursor.y);
}
void Editor::record_completion_use(const std::string &name)
{
  if (!completion_rank_enabled() || name.empty())
  {
    return;
  }
  load_completion_usage();
  completion_usage.record(lsp_internal::detect_lsp_language(get_buffer().filepath),
                          name,
                          CompletionRank::now_seconds());
  completion_usage_dirty = true;
}

int Editor::completion_rank_points(const std::string &name)
{
  if (!completion_rank_enabled() || name.empty())
  {
    return 0;
  }
  load_completion_usage();
  return CompletionRank::relevance(
      completion_usage, completion_context, completion_usage_language, name);
}

void Editor::arm_lsp_completion(const std::string &filepath, bool manual)
{
  auto &buf = get_buffer();
  completion_usage_language = lsp_internal::detect_lsp_language(filepath);
  // The context is the words of the whole buffer, so it is read once per word
  // rather than once per keystroke: another letter of the same word ranks against
  // the text the session opened on, and accepting an item is what closes the
  // session and makes the next word read the buffer the item landed in. The
  // anchor is the start of the word being completed, and it is what says the
  // word changed; gating on lsp_completion_visible instead read the whole window
  // back on every keystroke, because the popup only becomes visible once the
  // server has answered the request that keystroke made. The filepath check
  // comes before the value below is overwritten, so another file always re-reads.
  const Cursor anchor = current_completion_start(buf);
  if (lsp_completion_filepath != filepath || !(anchor == completion_context_anchor))
  {
    rebuild_completion_context();
    completion_context_anchor = anchor;
  }
  lsp_completion_anchor = buf.cursor;
  lsp_completion_replace_start = current_completion_start(buf);
  lsp_completion_filepath = filepath;
  lsp_completion_prefix = completion_prefix_from(buf, lsp_completion_replace_start);
  lsp_completion_manual_request = manual;
  if (!manual)
  {
    // This request went out for a word a keystroke just moved on, and the first
    // response of that word has no other place to say so (refresh_lsp_completion_filter
    // only sees a changed query): the preview waits for the pause from here too,
    // so it never flashes the moment the server answers. A manual request asked
    // for the list on purpose, so its preview is not a flicker.
    lsp_completion_typing_ms = lsp_internal::now_ms();
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

  // Whether the builtin tag list belongs at the caret's site: the word right
  // after a `<`, decided below. A manual request asks for the list on purpose,
  // so it is offered it wherever the caret is.
  bool tag_list_at_site = manual;

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
    // A tag name is the word right after the `<` that opens the tag, and that
    // word is the only thing the builtin tag list is a completion of (see the
    // seeding below): a word anywhere else on the line is an attribute value, a
    // closing tag's name, or text, and a whole element written over it is not
    // what the Tab meant.
    tag_list_at_site = i > 0 && buf.line(buf.cursor.y)[i - 1] == '<';
    bool html_file = lsp_internal::is_html_filepath(buf.filepath);
    bool script_file = lsp_internal::is_script_lsp_filepath(buf.filepath);
    bool style_file = lsp_internal::is_style_lsp_filepath(buf.filepath);
    // A custom property is typed `--name`, and `-` is not an identifier
    // character: without its own trigger the second dash would ask for nothing
    // and the name would quietly never be offered.
    bool punctuation_trigger = trigger_character == '.' || trigger_character == ':'
                               || trigger_character == '>' || trigger_character == '<'
                               || trigger_character == '/'
                               || (style_file && trigger_character == '-');
    int min_prefix = (html_file || script_file || style_file) ? 1 : 2;
    if (!punctuation_trigger && prefix_len < min_prefix)
    {
      return;
    }
  }

  bool has_builtin_html = false;

  // The list a web file starts from, before any server answers: the tags
  // (markup only, and only while a tag name is being typed -- see
  // tag_list_at_site above) plus the workspace's own class names and custom
  // properties. It is seeded only where no server is expected -- a markup or
  // style sheet file -- because for a JavaScript/TypeScript buffer a server does
  // answer and clearing the previous response out from under the popup would
  // blink it empty on every request. When a server does answer for one of these
  // files, the response path appends the index to its items the same way it
  // appends the tags (lifecycle.cpp).
  if (lsp_internal::is_html_filepath(buf.filepath)
      || lsp_internal::is_style_lsp_filepath(buf.filepath))
  {
    lsp_completion_all_items.clear();
    if (lsp_internal::is_html_filepath(buf.filepath) && tag_list_at_site)
    {
      lsp_internal::append_html_builtin_completions(lsp_completion_all_items);
    }
    append_web_index_completions(lsp_completion_all_items);
    arm_lsp_completion(buf.filepath, manual);
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
    arm_lsp_completion(buf.filepath, manual);
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

  // A call completion goes in without the server's argument placeholders, which
  // also leaves a snippet item with no tabstops for the engine to expand.
  const bool stripped_call = strip_call_arguments(item, text, cursor_offset);

  // Keep the raw snippet text (tabstops and all) for the bundled snippet
  // engine, which expands it with real placeholders; `text` is the flattened
  // fallback used when no engine handler is registered.
  const std::string raw_snippet_text = text;
  if (item.insert_text_format == 2 && !stripped_call)
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

  // One acceptance, kept for the next list (lsp/completion_rank.h): the user
  // reaching for this name is the whole signal the model learns from.
  record_completion_use(completion_name(item));

  const std::string &line_ref = buf.line(buf.cursor.y);
  const int cursor = std::clamp(buf.cursor.x, 0, (int)line_ref.size());
  int start = cursor;
  int end = cursor;

  bool site_from_editor = false;
  if (item.has_text_edit_range && item.edit_start_line == buf.cursor.y
      && item.edit_end_line == buf.cursor.y)
  {
    start = std::clamp(item.edit_start_char, 0, (int)line_ref.size());
    end = std::clamp(item.edit_end_char, start, (int)line_ref.size());
  }
  else if (lsp_completion_replace_start.y == buf.cursor.y)
  {
    start = std::clamp(lsp_completion_replace_start.x, 0, cursor);
    site_from_editor = true;
  }
  else
  {
    while (start > 0 && is_identifier_char(line_ref[start - 1]))
    {
      start--;
    }
    site_from_editor = true;
  }

  // A row that brings its own tag takes the tag the author has already opened
  // with it. The tag list is offered for a `<` being typed, but the site above is
  // the word after it -- `<` is not an identifier character -- so accepting a row
  // used to write the opening `<` twice (`<` + Tab wrote `<<a href=""></a>`). A
  // server's own text edit names the range it replaces, so it is left alone.
  if (site_from_editor && !text.empty() && text[0] == '<' && start > 0
      && line_ref[(size_t)start - 1] == '<')
  {
    start--;
  }

  // Where the item is about to be written, and how many lines the document had
  // before it: the auto-import edits below are positions in *that* document, so
  // remapping them needs both.
  const int main_line = buf.cursor.y;
  const int lines_before = (int)buf.lines.size();
  auto apply_additional_edits = [&](int start_col)
  {
    if (item.additional_text_edits.empty() || buf.filepath.empty())
    {
      return;
    }
    const int added_lines = (int)buf.lines.size() - lines_before;
    const auto remapped =
        remap_additional_edits(item.additional_text_edits, main_line, start_col, added_lines);
    apply_lsp_text_edits(buf.filepath, remapped);
  };

  // Snippet items (`insert_text_format = 2`) expand through the bundled
  // snippet engine when it registered a handler: it owns tabstops, choices,
  // mirrors and nested snippets, which a plain-text expansion cannot express.
  // 1-based line/column, end-exclusive, matching jot.buffer.apply_edit.
  if (!stripped_call && item.insert_text_format == 2 && lua_api
      && lua_api->run_lsp_snippet_handler(
          raw_snippet_text, buf.cursor.y + 1, start + 1, buf.cursor.y + 1, end + 1))
  {
    apply_additional_edits(start);
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

  // Last, so the caret above lands where the item was typed and an import the
  // item carries is written after it. Both are one user action; undo takes the
  // import back first (the applier pushes its own step).
  apply_additional_edits(start);

  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  notify_lsp_change(buf.filepath);

  hide_lsp_completion();
  return true;
}