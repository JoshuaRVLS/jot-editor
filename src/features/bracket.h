#ifndef BRACKET_H
#define BRACKET_H

#include <vector>
#include <string>
#include <utility>

struct BracketPos {
    int x, y;
};

struct BracketPair {
    BracketPos open;
    BracketPos close;
    int color_index;
};

class BracketHighlighter {
public:
    static bool is_bracket(char c);
    static bool is_opening_bracket(char c);
    static bool is_closing_bracket(char c);
    static char get_matching_bracket(char c);
    
    static std::pair<int, int> find_matching_bracket(
        const std::vector<std::string>& lines,
        int line, int col
    );
    
    static std::vector<BracketPair> find_all_pairs(
        const std::vector<std::string>& lines
    );
    
    static int get_bracket_color(int pair_index);
    static int get_bracket_color_at(
        const std::vector<std::string>& lines,
        int line, int col,
        const std::vector<BracketPair>& pairs
    );
    
    static std::pair<int, int> find_matching_bracket_near_cursor(
        const std::vector<std::string>& lines,
        int cursor_line, int cursor_col
    );
};

#endif

