#ifndef EDITOR_COMMAND_UTILS_H
#define EDITOR_COMMAND_UTILS_H

#include <string>
#include <vector>

namespace CommandLineUtils {

std::string to_lower_copy(std::string s);
std::string shell_quote(const std::string &value);
std::string first_line_copy(const std::string &text);
std::string limit_lines(const std::string &text, int max_lines);
const std::vector<std::string> &ex_commands();
bool starts_with_icase(const std::string &value, const std::string &prefix);
bool command_takes_argument(const std::string &cmd);
bool parse_line_col(const std::string &s, int &line_out, int &col_out);
std::string trim_copy(const std::string &s);
std::vector<std::string> complete_path_argument(const std::string &arg);

} // namespace CommandLineUtils

#endif
