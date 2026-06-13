#include "html.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool name_char(char c) {
    return std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':'; 
}

bool void_tag(const std::string &tag) {
    static const std::set<std::string> tags = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    return tags.count(lower(tag)) != 0;
}
}

namespace HtmlFeatures {
bool is_html_extension(const std::string &path) {
    std::string p = lower(path);
    return (p.size() >= 5 && p.substr(p.size() - 5) == ".html") || 
           (p.size() >= 4 && p.substr(p.size() - 5) == ".htm");
}

bool should_insert_closing_tag(const std::string &line, int cursor_after_gt, std::string &closing_tag) {
    closing_tag.clear();
    int gt = cursor_after_gt - 1;
    if (gt < 0 || gt >= (int)line.size() || line[gt] != '>') return false;

    int lt = line.rfind('<', gt);
    if (lt == (int)std::string::npos) return false;
    if (lt + 1 >= gt) return false;

    char next = line[lt + 1];
    if (next == '/' || next == '!' || next == '?') return false;
    if (gt > 0 && line[gt - 1] == '/') return false;

    int pos = lt + 1;
    while (pos < gt && std::isspace((unsigned char)line[pos])) pos++;

    int start = pos;
    while (pos < gt && name_char(line[pos])) pos++;
    if (pos == start) return false;

    std::string tag = line.substr(start, pos - start);
    if (void_tag(tag)) return false;

    std::string rest = line.substr(pos, gt - pos);
    if (rest.find("</") != std::string::npos) return false;

    closing_tag = "</" + tag + ">";
    return true;
}

bool is_between_matching_tags(const std::string &before_cursor, const std::string &after_cursor, std::string &tag_name) {
    tag_name.clear();

    size_t lt = before_cursor.rfind('<');
    if (lt == std::string::npos) return false;

    size_t gt = before_cursor.find('>', lt);
    if (gt == std::string::npos || gt + 1 != before_cursor.size()) return false;

    if (lt + 1 >= before_cursor.size()) return false;
    char next = before_cursor[lt + 1];
    if (next == '/' || next == '!' || next == '?') return false;

    int pos = (int)lt + 1;
    int end = (int)gt;
    int start = pos;
    while (pos < end && name_char(before_cursor[pos])) pos++;
    if (pos == start) return false;

    tag_name = before_cursor.substr(start, pos - start);
    if (void_tag(tag_name)) return false;

    std::string expected = "</" + tag_name + ">";
    return after_cursor.rfind(expected, 0) == 0;
}
}