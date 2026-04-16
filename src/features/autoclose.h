#ifndef AUTOCLOSE_H
#define AUTOCLOSE_H

#include <string>
#include <vector>

class AutoClose {
public:
    static bool should_auto_close(char c);
    static char get_closing_bracket(char c);
    static bool is_closing_bracket(char c);
    static bool should_skip_closing(char c, const std::string& line, int pos);
};

#endif

