#include "autoclose.h"

bool AutoClose::should_auto_close(char c) {
    return c == '(' || c == '{' || c == '[' || c == '"' || c == '\'';
}

char AutoClose::get_closing_bracket(char c) {
    switch (c) {
        case '(': return ')';
        case '{': return '}';
        case '[': return ']';
        case '"': return '"';
        case '\'': return '\'';
        default: return '\0';
    }
}

bool AutoClose::is_closing_bracket(char c) {
    return c == ')' || c == '}' || c == ']' || c == '"' || c == '\'';
}

bool AutoClose::should_skip_closing(char c, const std::string& line, int pos) {
    if (pos >= (int)line.length()) return false;
    return line[pos] == c;
}
