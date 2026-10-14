// Undo tree memory (src/buffers/undo.cpp).
//
// A snapshot used to hold a copy of every line of the buffer and the history
// keeps 500 of them, so one keystroke in a 2000-line file put ~190 KB in the
// history and it grew to ~94 MB before the cap stopped it. Snapshots now share
// the lines they did not change with the state behind them: a keystroke holds
// the line it changed plus one pointer per line.
//
// Three things are pinned here. The first is the sharing itself, measured as
// the bytes the tree holds with each shared line counted once. The second is
// that sharing never aliases live text: a line an early edit changed still has
// to restore correctly after many later states, and the windowed path (files
// past the full-snapshot line count) still has to restore its lines. The third
// is the byte budget that bounds the tree when the line pointers themselves are
// the cost: 500 states of a 4000-line buffer is ~32 MB of pointers no matter
// how well the lines are shared, so states are dropped by accounted bytes as
// well as by count.

#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <set>
#include <string>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_undo_memory_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // Enough lines and long enough lines that a per-keystroke copy of the buffer
  // is an order of magnitude past what the sharing costs.
  constexpr int kLines = 2000;
  constexpr int kLineWidth = 80;

  std::string many_lines(int count)
  {
    std::string text;
    for (int i = 0; i < count; i++)
    {
      std::string line = "line " + std::to_string(i) + ":";
      while ((int)line.size() < kLineWidth)
      {
        line += " filler";
      }
      text += line;
      text += "\n";
    }
    return text;
  }

  // Bytes the tree holds, counting a line shared by several states once: the
  // number the sharing moves, and the one a copy-per-state would inflate. The
  // state in front holds the live buffer rather than a snapshot, so it is the
  // one node with nothing to count.
  std::size_t history_bytes(const FileBuffer &buf)
  {
    std::set<const std::string *> seen;
    std::size_t total = 0;
    for (const FileBuffer::UndoNode &node : buf.undo_nodes)
    {
      if (!node.state_known)
      {
        continue;
      }
      for (const SnapshotLine &line : node.state.old_lines)
      {
        if (line && seen.insert(line.get()).second)
        {
          total += line->size() + sizeof(std::string);
        }
      }
    }
    return total;
  }

  // Bytes the tree really holds, arrays included: every state's line-pointer
  // array plus each line's text counted once however many states share it. The
  // number the byte budget has to be an upper bound of.
  std::size_t history_bytes_with_arrays(const FileBuffer &buf)
  {
    std::size_t total = 0;
    std::set<const std::string *> seen;
    for (const FileBuffer::UndoNode &node : buf.undo_nodes)
    {
      if (!node.state_known)
      {
        continue;
      }
      total += node.state.old_lines.size() * sizeof(SnapshotLine);
      for (const SnapshotLine &line : node.state.old_lines)
      {
        if (line && seen.insert(line.get()).second)
        {
          total += line->size() + sizeof(std::string);
        }
      }
    }
    return total;
  }

  // How many undos the state in front is from the oldest one kept.
  int depth_to_root(const FileBuffer &buf)
  {
    int depth = 0;
    for (int n = buf.undo_current; n >= 0; n = buf.undo_nodes[(size_t)n].parent)
    {
      depth++;
    }
    return depth;
  }

  std::size_t text_bytes(const FileBuffer &buf)
  {
    std::size_t total = 0;
    for (int i = 0; i < (int)buf.line_count(); i++)
    {
      total += buf.line(i).size() + sizeof(std::string);
    }
    return total;
  }

  void seed(Editor &e, int lines)
  {
    e.set_home_menu_visible(false);
    auto &core = e.host().core;
    core.set_buffer_content(many_lines(lines));
    core.clear_extra_carets();
    core.set_cursor(0, 0);
  }

  constexpr int kCtrl = 0x20000;
  constexpr int kUndo = 'z' | kCtrl;
} // namespace

TEST_CASE("Undo history: keystrokes on a 2000-line file do not copy the file", "[jot][undo]")
{
  Editor &e = probe_editor();
  seed(e, kLines);
  auto &core = e.host().core;
  FileBuffer &buf = e.buffer_for_test();

  const std::size_t text = text_bytes(buf);
  for (int i = 0; i < 20; i++)
  {
    core.insert_char_at_carets('x');
  }

  // Twenty states of the file as it stood before each keystroke. They share
  // every line the typing did not touch, so what the tree holds is the twenty
  // arrays of line pointers plus a line each, not twenty copies of the file.
  // What the tree holds is the text once plus one array of line pointers per
  // state, which is the floor this design accepts; a copy per state would be
  // twenty copies of the text on top of that.
  const std::size_t arrays = 20 * (std::size_t)kLines * sizeof(SnapshotLine);
  const std::size_t held = history_bytes_with_arrays(buf);
  REQUIRE(held > text / 2);
  REQUIRE(held < text * 2 + arrays);
  // The same count without the pointer arrays: the file's text once, not once
  // per state.
  REQUIRE(history_bytes(buf) < text * 2);
}

TEST_CASE("Undo history: an early edit still restores after many later snapshots", "[jot][undo]")
{
  Editor &e = probe_editor();
  seed(e, kLines);
  auto &core = e.host().core;

  core.insert_char_at_carets('A');
  REQUIRE(core.buffer_content().rfind("Aline 0:", 0) == 0);

  // Twenty edits far away, so the state holding the first line sits twenty
  // snapshots down and every one of them had to have kept its own copy of it.
  core.set_cursor(1500, 0);
  for (int i = 0; i < 20; i++)
  {
    core.insert_char_at_carets('z');
  }
  REQUIRE(core.buffer_content().find("zzzzzzzzzzzzzzzzzzzz") != std::string::npos);

  for (int i = 0; i < 20; i++)
  {
    e.raw_key_for_test(kUndo);
  }
  REQUIRE(core.buffer_content().find("zzzzzzzzzzzzzzzzzzzz") == std::string::npos);
  REQUIRE(core.buffer_content().rfind("Aline 0:", 0) == 0);

  e.raw_key_for_test(kUndo);
  REQUIRE(core.buffer_content().rfind("line 0:", 0) == 0);
}

TEST_CASE("Undo history: the byte budget bounds a big buffer's history", "[jot][undo]")
{
  // 4000 lines is under kMaxFullSnapshotLines, so every state here is a full
  // snapshot: what it adds is one line pointer per line (~64 KB) plus the one
  // line its command changed. 300 keystrokes are ~19 MB of pointers alone, past
  // the byte budget, which therefore has to trim long before the 500-state cap
  // would -- and that is the memory the RSS soak was climbing by.
  Editor &e = probe_editor();
  seed(e, 4000);
  auto &core = e.host().core;
  FileBuffer &buf = e.buffer_for_test();
  buf.clear_undo_history();

  for (int i = 0; i < 300; i++)
  {
    core.set_cursor((i * 7) % 4000, 0);
    core.insert_char_at_carets('k');
  }

  REQUIRE(buf.undo_nodes.size() < kMaxUndoHistory);
  REQUIRE(buf.undo_nodes.size() >= kMinUndoStates);
  REQUIRE(buf.undo_tree_bytes <= kUndoHistoryBytes);
  // The accounting may overstate what is held (a line still shared by a newer
  // state keeps its creator's charge) but never understate it.
  REQUIRE(buf.undo_tree_bytes >= history_bytes_with_arrays(buf));

  // Undo still reaches the states the trim kept. The oldest states -- the ones
  // holding the seeded text -- were dropped, so walking the retained history
  // ends at the oldest state kept, not at the seed; what has to hold is that
  // the newest edits come back and the walk never over-runs the tree.
  const int depth = depth_to_root(buf);
  REQUIRE(depth > 1);
  for (int i = 0; i < depth; i++)
  {
    e.raw_key_for_test(kUndo);
  }
  REQUIRE(buf.undo_nodes[(size_t)buf.undo_current].parent == -1);
  // The newest keystroke was on line 299 * 7 % 4000 = 2093, well inside what
  // the budget keeps.
  REQUIRE(core.buffer_content().find("kline 2093:") == std::string::npos);
  REQUIRE(core.buffer_content().find("line 2093:") != std::string::npos);
}

TEST_CASE("Undo history: a windowed snapshot past the full-snapshot size still restores",
          "[jot][undo]")
{
  // Past kMaxFullSnapshotLines a snapshot holds only the lines around the
  // caret, so that path needs its own case: it is the one that copies a window
  // rather than sharing a whole file.
  Editor &e = probe_editor();
  seed(e, 6000);
  auto &core = e.host().core;

  core.set_cursor(3000, 0);
  core.insert_char_at_carets('Q');
  REQUIRE(core.buffer_content().find("Qline 3000:") != std::string::npos);

  e.raw_key_for_test(kUndo);
  REQUIRE(core.buffer_content().find("Qline 3000:") == std::string::npos);
  REQUIRE(core.buffer_content().find("line 3000:") != std::string::npos);
}
