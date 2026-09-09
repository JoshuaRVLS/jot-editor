// Internal helpers shared between the buffer rendering modules: bracket-pair
// matching (bracket.cpp) and diagnostic decoration (diagnostics.cpp), both
// consumed by the main render pass in buffer.cpp.
#pragma once

#include "editor.h"

namespace buffer_internal
{
inline constexpr int kBracketDepthScanLimitLines = 500;
inline constexpr int kBracketMatchSearchLimitLines = 5000;
inline constexpr int kDiagDenseSpanLimit = 64;

struct ActiveBracketGuide
{
  bool active = false;
  int visual_column = 0;
  int start_line = 0;
  int end_line = 0;
};

struct BracketPairMatch
{
  bool found = false;
  int open_line = -1;
  int open_col = -1;
  int close_line = -1;
  int close_col = -1;
};

// buffer.cpp
int leading_indent_visual_column(const std::string &line, int tab_size);

// bracket.cpp
bool is_open_bracket(char c);
bool is_close_bracket(char c);
int rainbow_bracket_color(const Theme &theme, int depth);
void apply_bracket_depth_delta(char c, int &depth);
bool bracket_chars(char c, char &open, char &close, bool &is_open);
BracketPairMatch find_pair_at(const FileBuffer &buf, int line, int col);
ActiveBracketGuide build_active_bracket_guide(const FileBuffer &buf, int tab_size);

// diagnostics.cpp
int diagnostic_severity_color(const Theme &theme, int severity);
std::string diagnostic_severity_label(int severity);
bool diagnostic_covers_line(const Diagnostic &diag, int line);
const Diagnostic *find_line_diagnostic(const FileBuffer &buf, int line, int cursor_col);
void extend_diag_severity(FileBuffer &buf, int to_line);
int line_diagnostic_severity(FileBuffer &buf, int line);
std::string compact_diagnostic_text(const std::string &text);
} // namespace buffer_internal