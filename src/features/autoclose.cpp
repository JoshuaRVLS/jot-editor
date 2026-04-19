#include "autoclose.h"
#include "python_api.h"

bool AutoClose::should_auto_close(char c) {
    if (auto *py = PythonAPI::active()) {
        return py->feature_should_auto_close(c);
    }
    return c == '(' || c == '{' || c == '[' || c == '"' || c == '\'';
}

char AutoClose::get_closing_bracket(char c) {
    if (auto *py = PythonAPI::active()) {
        char out = py->feature_get_closing_bracket(c);
        if (out != '\0') {
            return out;
        }
    }
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
    if (auto *py = PythonAPI::active()) {
        return py->feature_is_closing_bracket(c);
    }
    return c == ')' || c == '}' || c == ']' || c == '"' || c == '\'';
}

bool AutoClose::should_skip_closing(char c, const std::string& line, int pos) {
    if (auto *py = PythonAPI::active()) {
        return py->feature_should_skip_closing(c, line, pos);
    }
    if (pos >= (int)line.length()) return false;
    return line[pos] == c;
}
