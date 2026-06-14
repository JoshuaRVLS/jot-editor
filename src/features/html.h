#ifndef HTML_H
#define HTML_H

#include <string>

namespace HtmlFeatures {
bool is_html_extension(const std::string &path);
bool is_jsx_extension(const std::string &path);
bool is_markup_tag_extension(const std::string &path);
bool should_insert_closing_tag(const std::string &line, int cursor_after_gt,
                               std::string &closing_tag);
bool is_between_matching_tags(const std::string &before_cursor,
                              const std::string &after_cursor,
                              std::string &tag_name);
}

#endif
