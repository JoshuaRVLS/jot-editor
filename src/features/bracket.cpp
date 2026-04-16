
#include "bracket.h"
#include <stack>
#include <map>

bool BracketHighlighter::is_bracket(char c) {
    return c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']';
}

bool BracketHighlighter::is_opening_bracket(char c) {
    return c == '(' || c == '{' || c == '[';
}

bool BracketHighlighter::is_closing_bracket(char c) {
    return c == ')' || c == '}' || c == ']';
}

char BracketHighlighter::get_matching_bracket(char c) {
    switch(c) {
        case '(': return ')';
        case ')': return '(';
        case '{': return '}';
        case '}': return '{';
        case '[': return ']';
        case ']': return '[';
        default: return '\0';
    }
}

std::pair<int, int> BracketHighlighter::find_matching_bracket(
    const std::vector<std::string>& lines,
    int line, int col
) {
    if (line < 0 || line >= (int)lines.size()) return {-1, -1};
    if (col < 0 || col >= (int)lines[line].length()) return {-1, -1};
    
    char bracket = lines[line][col];
    if (!is_bracket(bracket)) return {-1, -1};
    
    bool is_opening = is_opening_bracket(bracket);
    char target = get_matching_bracket(bracket);
    int direction = is_opening ? 1 : -1;
    
    std::stack<char> stack;
    int current_line = line;
    int current_col = col + (is_opening ? 1 : -1);
    
    while (current_line >= 0 && current_line < (int)lines.size()) {
        const std::string& current_line_str = lines[current_line];
        
        while (current_col >= 0 && current_col < (int)current_line_str.length()) {
            char ch = current_line_str[current_col];
            
            if (is_bracket(ch)) {
                if (is_opening_bracket(ch)) {
                    stack.push(ch);
                } else {
                    if (stack.empty()) {
                        if (ch == target) {
                            return {current_line, current_col};
                        }
                        return {-1, -1};
                    }
                    char top = stack.top();
                    stack.pop();
                    if (get_matching_bracket(top) != ch) {
                        return {-1, -1};
                    }
                }
            }
            
            if (stack.empty() && ch == target) {
                return {current_line, current_col};
            }
            
            current_col += direction;
        }
        
        if (stack.empty()) {
            return {-1, -1};
        }
        
        current_line += direction;
        if (current_line >= 0 && current_line < (int)lines.size()) {
            current_col = is_opening ? 0 : (int)lines[current_line].length() - 1;
        } else {
            break;
        }
    }
    
    return {-1, -1};
}

std::vector<BracketPair> BracketHighlighter::find_all_pairs(
    const std::vector<std::string>& lines
) {
    std::vector<BracketPair> pairs;
    std::stack<std::pair<BracketPos, int>> stack;
    int pair_index = 0;
    
    for (size_t i = 0; i < lines.size(); i++) {
        for (size_t j = 0; j < lines[i].length(); j++) {
            char ch = lines[i][j];
            
            if (is_opening_bracket(ch)) {
                BracketPos pos = {(int)j, (int)i};
                stack.push({pos, pair_index++});
            } else if (is_closing_bracket(ch)) {
                if (!stack.empty()) {
                    char opening = get_matching_bracket(ch);
                    auto& top = stack.top();
                    BracketPos open_pos = top.first;
                    
                    if (open_pos.y >= 0 && open_pos.y < (int)lines.size() &&
                        open_pos.x >= 0 && open_pos.x < (int)lines[open_pos.y].length() &&
                        lines[open_pos.y][open_pos.x] == opening) {
                        BracketPair pair;
                        pair.open = open_pos;
                        pair.close = {(int)j, (int)i};
                        pair.color_index = top.second;
                        pairs.push_back(pair);
                        stack.pop();
                    }
                }
            }
        }
    }
    
    return pairs;
}

int BracketHighlighter::get_bracket_color(int pair_index) {
    int colors[] = {1, 2, 3, 4, 5, 6};
    return colors[pair_index % 6];
}

int BracketHighlighter::get_bracket_color_at(
    const std::vector<std::string>& lines,
    int line, int col,
    const std::vector<BracketPair>& pairs
) {
    if (line < 0 || line >= (int)lines.size()) return 0;
    if (col < 0 || col >= (int)lines[line].length()) return 0;
    
    char ch = lines[line][col];
    if (!is_bracket(ch)) return 0;
    
    for (const auto& pair : pairs) {
        if ((pair.open.y == line && pair.open.x == col) ||
            (pair.close.y == line && pair.close.x == col)) {
            return get_bracket_color(pair.color_index);
        }
    }
    
    return 0;
}

std::pair<int, int> BracketHighlighter::find_matching_bracket_near_cursor(
    const std::vector<std::string>& lines,
    int cursor_line, int cursor_col
) {
    if (cursor_line < 0 || cursor_line >= (int)lines.size()) return {-1, -1};
    
    int check_positions[][2] = {
        {cursor_col, cursor_line},
        {cursor_col - 1, cursor_line},
        {cursor_col + 1, cursor_line}
    };
    
    for (int i = 0; i < 3; i++) {
        int col = check_positions[i][0];
        int line = check_positions[i][1];
        
        if (line >= 0 && line < (int)lines.size() &&
            col >= 0 && col < (int)lines[line].length()) {
            auto match = find_matching_bracket(lines, line, col);
            if (match.first != -1) {
                return match;
            }
        }
    }
    
    return {-1, -1};
}



