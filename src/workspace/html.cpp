#include "html.h"
#include "base/string_util.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <string>

namespace
{
  bool name_char(char c)
  {
    return std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':' || c == '.';
  }

  bool void_tag(const std::string &tag)
  {
    static const std::set<std::string> tags = {"area",
                                               "base",
                                               "br",
                                               "col",
                                               "embed",
                                               "hr",
                                               "img",
                                               "input",
                                               "link",
                                               "meta",
                                               "param",
                                               "source",
                                               "track",
                                               "wbr"};
    return tags.count(string_util::lower_copy(tag)) != 0;
  }

  bool can_start_markup_tag(const std::string &line, int lt)
  {
    if (lt <= 0)
    {
      return true;
    }
    int i = lt - 1;
    while (i >= 0 && std::isspace((unsigned char)line[i]))
    {
      i--;
    }
    if (i < 0)
    {
      return true;
    }
    char prev = line[i];
    if (prev == '(' || prev == '[' || prev == '{' || prev == '=' || prev == ':' || prev == ','
        || prev == ';' || prev == '?' || prev == '!' || prev == '|' || prev == '&' || prev == '>')
    {
      return true;
    }
    int word_end = i + 1;
    while (i >= 0 && (std::isalnum((unsigned char)line[i]) || line[i] == '_'))
    {
      i--;
    }
    std::string word = string_util::lower_copy(line.substr(i + 1, word_end - (i + 1)));
    return word == "return";
  }

  // Where the tag name the cursor sits in starts and ends, and whether that tag
  // is a closing one. `col` may be at the name's end (the cursor is what extends
  // it); anywhere else in the tag -- attributes, or the body after `>` -- is not
  // a name.
  bool tag_name_region_at(const std::string &line,
                          int col,
                          bool &closing,
                          int &name_start,
                          int &name_end,
                          std::string &name)
  {
    closing = false;
    name.clear();
    if (col < 0 || col > (int)line.size())
      return false;

    int lt = -1;
    for (int i = col - 1; i >= 0; i--)
    {
      // A `>` first means the cursor is past a tag, in the body.
      if (line[i] == '>')
        return false;
      if (line[i] == '<')
      {
        lt = i;
        break;
      }
    }
    if (lt < 0)
      return false;

    closing = lt + 1 < (int)line.size() && line[lt + 1] == '/';
    name_start = lt + 1 + (closing ? 1 : 0);
    if (name_start >= (int)line.size() || !name_char(line[name_start]))
      return false;

    name_end = name_start;
    while (name_end < (int)line.size() && name_char(line[name_end]))
      name_end++;
    if (col < name_start || col > name_end)
      return false;

    name = line.substr(name_start, name_end - name_start);
    return true;
  }

  // The `>` that ends the tag whose name ends at `name_end`, or -1 when the tag
  // never closes. A `>` inside an attribute value would end the scan early;
  // the pair is then not found, which is a miss rather than a wrong edit.
  int tag_gt(const std::string &text, int name_end)
  {
    for (int i = name_end; i < (int)text.size(); i++)
    {
      if (text[i] == '>')
        return i;
    }
    return -1;
  }

  // Whether the tag spanning [lt, gt] closes itself: `<br>`, `<img ... />`, or a
  // void element that never has a partner.
  bool self_closing_tag(const std::string &text, int lt, int gt, const std::string &name)
  {
    if (void_tag(name))
      return true;
    for (int i = gt - 1; i > lt; i--)
    {
      if (std::isspace((unsigned char)text[i]))
        continue;
      return text[i] == '/';
    }
    return false;
  }

  int name_run_length(const std::string &text, int offset)
  {
    int end = offset;
    while (end < (int)text.size() && name_char(text[end]))
      end++;
    return end - offset;
  }

  int line_start_offset(const std::vector<std::string> &lines, int line)
  {
    int offset = 0;
    for (int i = 0; i < line && i < (int)lines.size(); i++)
      offset += (int)lines[i].size() + 1;
    return offset;
  }

  // The inverse: where an offset in the joined text lands.
  void offset_to_line_col(const std::vector<std::string> &lines, int offset, int &line, int &col)
  {
    line = 0;
    int remaining = offset;
    while (line < (int)lines.size() - 1)
    {
      const int len = (int)lines[line].size();
      if (remaining <= len)
        break;
      remaining -= len + 1;
      line++;
    }
    col = remaining;
  }

  // From just past an opening tag's `>`, the offset of the matching closing
  // tag's name. The walk is structural -- every tag nests or unnests, whatever
  // it is called -- which is what lets the pair survive a name being edited into
  // a different one: `div` -> `spa` -> `span` still finds the same `</...>`.
  // A nameless close (`</>`) is still a close, so the pair holds while a name is
  // being retyped from empty; a bare `<` in body text opens nothing.
  int closing_partner(const std::string &text, int from)
  {
    int depth = 1;
    int i = from;
    while (i < (int)text.size())
    {
      const int lt = (int)text.find('<', i);
      if (lt == (int)std::string::npos)
        return -1;
      int j = lt + 1;
      const bool close = j < (int)text.size() && text[j] == '/';
      if (close)
        j++;
      const int start = j;
      while (j < (int)text.size() && name_char(text[j]))
        j++;
      const std::string other = text.substr(start, j - start);
      i = lt + 1;
      if (close)
      {
        if (--depth == 0)
          return start;
      }
      else if (!other.empty())
      {
        const int gt = tag_gt(text, j);
        if (gt >= 0 && !self_closing_tag(text, lt, gt, other))
          depth++;
      }
    }
    return -1;
  }

  // The mirror: from a closing tag's `<`, the offset of the opening tag's name.
  // Walking backwards, a closing tag deepens the nest and an opening one closes
  // it, so the outermost pair wins for the same reason as above.
  int opening_partner(const std::string &text, int before_lt)
  {
    int depth = 1;
    int i = before_lt;
    while (i > 0)
    {
      const int lt = (int)text.rfind('<', i - 1);
      if (lt == (int)std::string::npos)
        return -1;
      int j = lt + 1;
      const bool close = j < (int)text.size() && text[j] == '/';
      if (close)
        j++;
      const int start = j;
      while (j < (int)text.size() && name_char(text[j]))
        j++;
      const std::string other = text.substr(start, j - start);
      i = lt;
      if (close)
      {
        depth++;
        continue;
      }
      if (other.empty())
        continue; // a bare `<` in body text closes nothing
      const int gt = tag_gt(text, j);
      if (gt >= 0 && !self_closing_tag(text, lt, gt, other) && --depth == 0)
        return start;
    }
    return -1;
  }
} // namespace

namespace HtmlFeatures
{
  bool is_html_extension(const std::string &path)
  {
    std::string p = string_util::lower_copy(path);
    return string_util::ends_with(p, ".html") || string_util::ends_with(p, ".htm");
  }

  bool is_jsx_extension(const std::string &path)
  {
    std::string p = string_util::lower_copy(path);
    return string_util::ends_with(p, ".jsx") || string_util::ends_with(p, ".tsx");
  }

  bool is_markup_tag_extension(const std::string &path)
  {
    return is_html_extension(path) || is_jsx_extension(path);
  }

  bool
  should_insert_closing_tag(const std::string &line, int cursor_after_gt, std::string &closing_tag)
  {
    closing_tag.clear();
    int gt = cursor_after_gt - 1;
    if (gt < 0 || gt >= (int)line.size() || line[gt] != '>')
      return false;

    int lt = (int)line.rfind('<', gt);
    if (lt == (int)std::string::npos)
      return false;
    if (lt + 1 >= gt)
      return false;
    if (!can_start_markup_tag(line, lt))
      return false;

    char next = line[lt + 1];
    if (next == '/' || next == '!' || next == '?')
      return false;
    if (gt > 0 && line[gt - 1] == '/')
      return false;

    int pos = lt + 1;
    while (pos < gt && std::isspace((unsigned char)line[pos]))
      pos++;

    int start = pos;
    while (pos < gt && name_char(line[pos]))
      pos++;
    if (pos == start)
      return false;

    std::string tag = line.substr(start, pos - start);
    if (void_tag(tag))
      return false;

    std::string rest = line.substr(pos, gt - pos);
    if (rest.find("</") != std::string::npos)
      return false;

    closing_tag = "</" + tag + ">";
    return true;
  }

  bool is_between_matching_tags(const std::string &before_cursor,
                                const std::string &after_cursor,
                                std::string &tag_name)
  {
    tag_name.clear();

    size_t lt = before_cursor.rfind('<');
    if (lt == std::string::npos)
      return false;

    size_t gt = before_cursor.find('>', lt);
    if (gt == std::string::npos || gt + 1 != before_cursor.size())
      return false;
    if (!can_start_markup_tag(before_cursor, (int)lt))
      return false;

    if (lt + 1 >= before_cursor.size())
      return false;
    char next = before_cursor[lt + 1];
    if (next == '/' || next == '!' || next == '?')
      return false;

    int pos = (int)lt + 1;
    int end = (int)gt;
    int start = pos;
    while (pos < end && name_char(before_cursor[pos]))
      pos++;
    if (pos == start)
      return false;

    tag_name = before_cursor.substr(start, pos - start);
    if (void_tag(tag_name))
      return false;

    std::string expected = "</" + tag_name + ">";
    return after_cursor.rfind(expected, 0) == 0;
  }

  std::string join_lines(const std::vector<std::string> &lines)
  {
    std::string text;
    size_t total = lines.empty() ? 0 : lines.size() - 1;
    for (const std::string &line : lines)
      total += line.size();
    text.reserve(total);
    for (size_t i = 0; i < lines.size(); i++)
    {
      if (i)
        text += '\n';
      text += lines[i];
    }
    return text;
  }

  bool find_tag_pair(const std::vector<std::string> &lines, int line, int col, MarkupTagPair &pair)
  {
    pair = MarkupTagPair();
    if (line < 0 || line >= (int)lines.size())
      return false;

    bool closing = false;
    int name_start = 0;
    int name_end = 0;
    std::string name;
    if (!tag_name_region_at(lines[line], col, closing, name_start, name_end, name))
      return false;

    const std::string text = join_lines(lines);
    const int edit_offset = line_start_offset(lines, line) + name_start;
    int partner_offset = -1;
    if (!closing)
    {
      const int gt = tag_gt(text, edit_offset + (int)name.size());
      if (gt < 0)
        return false;
      partner_offset = closing_partner(text, gt + 1);
    }
    else
    {
      // `</name`: the `<` sits two columns left of the name.
      partner_offset = opening_partner(text, edit_offset - 2);
    }
    if (partner_offset < 0)
      return false;
    // Zero is allowed: the partner is a `</>` whose name is being retyped, and
    // the caller fills it in.
    const int partner_len = name_run_length(text, partner_offset);

    pair.closing = closing;
    pair.name = name;
    pair.edit_line = line;
    pair.edit_col = name_start;
    pair.partner = text.substr(partner_offset, partner_len);
    offset_to_line_col(lines, partner_offset, pair.partner_line, pair.partner_col);
    return true;
  }
} // namespace HtmlFeatures
