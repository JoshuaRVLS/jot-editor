// Parse-tree retention (see kMaxCachedSyntaxTrees and
// Editor::evict_stale_syntax_trees).
//
// A tree-sitter CST costs roughly 1.3-3.5 KB per line of C++ (measured: a
// 3000-line file held ~10 MB), and a tree used to sit in memory for as long as
// its buffer was open -- twelve ordinary 3000-line files held ~120 MB of trees
// with one of them on screen. Trees are built by the first paint that needs
// them and released for buffers the renderer has not drawn recently.
//
// Two things are pinned here. The first is that a buffer is not parsed just by
// existing: nothing builds a tree until the paint path asks for one. The second
// is the budget itself -- the sweep keeps the buffers a frame drew (a split
// layout must never evict a pane it is about to redraw) plus the newest
// kMaxCachedSyntaxTrees of the rest, and a buffer dropped this way parses again
// when it is shown again.

#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_syntax_tree_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // Enough token-dense C++ that a parsed tree is clearly a tree, while small
  // enough that the parse stays on the synchronous path (see
  // kAsyncParseThresholdLines), so a headless test can rely on the tree being
  // there right after the init.
  std::vector<std::string> cpp_lines()
  {
    std::vector<std::string> lines;
    for (int i = 0; i < 40; i++)
    {
      lines.push_back("int value_" + std::to_string(i) + " = " + std::to_string(i)
                      + "; // note " + std::to_string(i));
    }
    return lines;
  }

  // Creates a new .cpp buffer holding `cpp_lines()`, so the test owns the
  // buffer instead of a file on disk (load_file would also attach an LSP
  // server). Returns its index, or -1 when no C++ grammar is installed.
  int add_cpp_buffer(Editor &e, int index)
  {
    static int counter = 0;
    e.create_new_buffer_for_test();
    const int id = e.buffer_count_for_test() - 1;
    FileBuffer &buf = e.buffer_for_test(id);
    buf.filepath = "/tmp/jot_syntax_tree_" + std::to_string(counter++) + "_"
                   + std::to_string(index) + ".cpp";
    buf.lines = cpp_lines();
    return id;
  }
} // namespace

TEST_CASE("Syntax trees: a buffer is not parsed until a frame asks for it", "[jot][syntax]")
{
  Editor &e = probe_editor();
  const int id = add_cpp_buffer(e, 0);
  FileBuffer &buf = e.buffer_for_test(id);

  // Nothing drew this buffer, so nothing paid for a parse tree.
  REQUIRE(buf.ts_tree == nullptr);
  REQUIRE(buf.ts_parser == nullptr);

  // Painting it (get_line_syntax_colors -> init_ts_for_buffer) is what parses.
  e.init_syntax_for_buffer_for_test(id);
  if (buf.ts_tree == nullptr)
  {
    SKIP("no installed C++ grammar to parse with");
  }
  REQUIRE(buf.ts_parser != nullptr);
}

TEST_CASE("Syntax trees: the sweep keeps only recently drawn buffers' trees", "[jot][syntax]")
{
  Editor &e = probe_editor();
  std::vector<int> ids;
  for (int i = 0; i < 6; i++)
  {
    const int id = add_cpp_buffer(e, i);
    e.init_syntax_for_buffer_for_test(id);
    if (e.buffer_for_test(id).ts_tree == nullptr)
    {
      SKIP("no installed C++ grammar to parse with");
    }
    ids.push_back(id);
    // One frame per file: the buffer is drawn, then the frame ends and the
    // sweep runs, exactly as the render loop orders it.
    e.mark_syntax_buffer_shown_for_test(id);
    e.run_syntax_tree_sweep_for_test();
  }

  // Six buffers were parsed, but only the budget's worth of trees is held.
  REQUIRE(e.syntax_tree_holder_count_for_test() == (int)kMaxCachedSyntaxTrees);
  // And the newest file -- the one on screen -- is never the one that goes.
  REQUIRE(e.buffer_for_test(ids.back()).ts_tree != nullptr);
  REQUIRE(e.buffer_for_test(ids.front()).ts_tree == nullptr);
  REQUIRE(e.buffer_for_test(ids.front()).ts_parser == nullptr);

  // Showing it again parses it again, through the same lazy path the first
  // paint takes, so the release costs a parse and not correctness.
  e.init_syntax_for_buffer_for_test(ids.front());
  REQUIRE(e.buffer_for_test(ids.front()).ts_tree != nullptr);
}

TEST_CASE("Syntax trees: every buffer a frame draws keeps its tree", "[jot][syntax]")
{
  Editor &e = probe_editor();
  std::vector<int> ids;
  for (int i = 0; i < 5; i++)
  {
    const int id = add_cpp_buffer(e, 10 + i);
    e.init_syntax_for_buffer_for_test(id);
    if (e.buffer_for_test(id).ts_tree == nullptr)
    {
      SKIP("no installed C++ grammar to parse with");
    }
    ids.push_back(id);
  }
  // Five buffers drawn in one frame (a multi-pane layout): the sweep may not
  // drop any of them, however far past the budget the layout is, or every
  // frame would re-parse the panes it is about to paint.
  for (int id : ids)
  {
    e.mark_syntax_buffer_shown_for_test(id);
  }
  e.run_syntax_tree_sweep_for_test();
  for (int id : ids)
  {
    REQUIRE(e.buffer_for_test(id).ts_tree != nullptr);
  }
}
