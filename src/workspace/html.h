#ifndef HTML_H
#define HTML_H

#include <string>
#include <vector>

namespace HtmlFeatures
{
  // A tag name under the cursor and the partner it has to keep in step with.
  struct MarkupTagPair
  {
    bool closing = false; // the tag under the cursor is a closing one (`</div`)
    std::string name;     // the name under the cursor, as the buffer spells it
    int edit_line = -1;   // and where that name is
    int edit_col = -1;
    std::string partner;   // the partner tag's name, as the buffer spells it
    int partner_line = -1; // and where it is
    int partner_col = -1;
  };

  bool is_html_extension(const std::string &path);
  bool is_jsx_extension(const std::string &path);
  bool is_markup_tag_extension(const std::string &path);
  bool
  should_insert_closing_tag(const std::string &line, int cursor_after_gt, std::string &closing_tag);
  bool is_between_matching_tags(const std::string &before_cursor,
                                const std::string &after_cursor,
                                std::string &tag_name);

  // The buffer's lines as one text: the scan below reads it as a stream rather
  // than reasoning about lines.
  std::string join_lines(const std::vector<std::string> &lines);

  // The tag-name pair around the cursor, for keeping an opening tag and its
  // closing tag spelled the same while one of them is edited. `line`/`col` is
  // the cursor; it has to sit inside a tag *name* (not the attributes, and not
  // the body), and the partner has to exist, for a pair to come back. The names
  // are allowed to differ by a prefix either way, which is what makes typing or
  // backspacing a name one character at a time carry the partner along.
  bool find_tag_pair(const std::vector<std::string> &lines, int line, int col, MarkupTagPair &pair);
} // namespace HtmlFeatures

#endif
