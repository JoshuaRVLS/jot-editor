#ifndef JOT_MODEL_SYNTAX_H
#define JOT_MODEL_SYNTAX_H

#include <cstddef>
#include <regex>
#include <utility>
#include <vector>

// How many buffers a painted frame may keep a live tree-sitter parse tree for,
// beyond the ones it just drew (see Editor::evict_stale_syntax_trees). A CST
// costs roughly 1.3-3.5 KB per line of C++ (measured: a 3000-line file held
// ~10 MB), so every open source file used to keep megabytes alive even while
// it sat in the background -- twelve ordinary files held ~120 MB of trees.
// The buffers a frame draws always keep their trees; this many others (the
// most recently drawn) are kept too, so switching among a few files does not
// re-parse on every tab change. Anything past that is released and parsed
// again on demand, through the same lazy path a large file already takes on
// open: synchronous when the file is small, on the background worker when it
// is not.
inline constexpr std::size_t kMaxCachedSyntaxTrees = 3;

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
