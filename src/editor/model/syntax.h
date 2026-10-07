#ifndef JOT_MODEL_SYNTAX_H
#define JOT_MODEL_SYNTAX_H

#include <cstddef>
#include <regex>
#include <utility>
#include <vector>
enum SyntaxEngine
{
  SYNTAX_ENGINE_UNKNOWN,
  SYNTAX_ENGINE_NONE,
  SYNTAX_ENGINE_REGEX,
  SYNTAX_ENGINE_TREESITTER
};

struct SyntaxLineCache
{
  bool valid = false;
  std::size_t line_hash = 0;
  std::size_t line_length = 0;
  // Byte extent for which `colors` is valid ([0, colors_upto)). Highlighting
  // is windowed to the visible area, so huge single-line files don't pay for
  // full-line colorization on every newly-scrolled-into-view line.
  std::size_t colors_upto = 0;
  std::vector<std::pair<int, int>> colors;
  // Bytes this entry is charged against FileBuffer::syntax_cache_bytes, kept so
  // re-highlighting a line can add only the difference rather than double
  // count it.
  std::size_t accounted_bytes = 0;
};

struct SyntaxRule
{
  std::regex pattern;
  int color;
};

#endif
