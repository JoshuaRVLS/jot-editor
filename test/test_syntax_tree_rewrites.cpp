// The parse tree under a rewrite that does not go through the edit hooks
// (Editor::invalidate_syntax_cache, src/syntax/syntax_editor.cpp).
//
// Every text command funnels through save_state(), which snapshots the text the
// tree is about to lose (ts_begin_edit) before the lines move. The paths that
// rewrite a buffer from outside that funnel -- prettier/clang-format on save,
// an LSP format, a Lua buffer write -- replace the lines and call
// invalidate_syntax_cache instead. That call used to drop only the per-line
// colour cache: the tree was left describing the replaced bytes while
// ts_tree_in_sync stayed true, so the next paint skipped the reparse and
// queried the old tree against the new text. Every highlight past the rewrite
// came from the wrong offset, and nothing marked the tree out of sync again --
// the symptom reported from a real project was that saving a TypeScript file
// with the on-save prettier left the file mis-coloured.
//
// The case drives the rewrite on a real buffer and compares the colours it
// produces with a fresh parse of the same bytes.

#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_syntax_tree_rewrites_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }
} // namespace

TEST_CASE("An external rewrite reparses the tree instead of querying the replaced bytes",
          "[jot][syntax]")
{
  Editor &e = probe_editor();
  const std::vector<std::string> unformatted = {
      "function probe(): number {",
      "    const value = 1;",
      "    return value;",
      "}",
  };
  const std::vector<std::string> formatted = {
      "function probe(): number {",
      "  const value = 1;",
      "  return value;",
      "}",
  };

  e.create_new_buffer_for_test();
  const int id = e.buffer_count_for_test() - 1;
  FileBuffer &buf = e.buffer_for_test();
  buf.filepath = "/tmp/jot_syntax_tree_rewrites.ts";
  buf.lines = unformatted;
  e.init_syntax_for_buffer_for_test(id);
  if (buf.ts_tree == nullptr)
  {
    SKIP("no installed TypeScript grammar to parse with");
  }
  REQUIRE(buf.ts_tree_in_sync);

  // What an external formatter's callback does: the lines are replaced and the
  // syntax cache is invalidated, with no edit hook snapshotting the old text.
  buf.lines = formatted;
  e.invalidate_syntax_cache_for_test();

  // The tree in hand describes bytes that are gone, and the invalidation has to
  // say so; otherwise the next paint trusts it and never reparses.
  REQUIRE_FALSE(buf.ts_tree_in_sync);
  e.init_syntax_for_buffer_for_test(id);
  REQUIRE(buf.ts_tree_in_sync);
  std::vector<std::vector<std::pair<int, int>>> after;
  for (int line = 0; line < (int)formatted.size(); line++)
  {
    after.push_back(e.line_syntax_colors_for_test(line));
  }

  // ...and those colours are the ones a tree parsed from the new bytes gives.
  // Releasing the tree forces that fresh parse, with the per-line cache dropped
  // so the answer cannot come from the entries the rewrite left behind.
  buf.release_syntax();
  e.invalidate_syntax_cache_for_test();
  e.init_syntax_for_buffer_for_test(id);
  REQUIRE(buf.ts_tree != nullptr);
  for (int line = 0; line < (int)formatted.size(); line++)
  {
    REQUIRE(e.line_syntax_colors_for_test(line) == after[line]);
  }
}
