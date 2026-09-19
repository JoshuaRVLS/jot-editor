#include "folding.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace
{
  // Naive reference implementations: walk every line against every range. The
  // prepared FoldView exists to avoid exactly this walk, so it is the honest
  // thing to check the view against -- walking the buffer one line at a time
  // is what the view's index has to reproduce.
  bool ref_hidden(const std::vector<FoldRange> &ranges, int line)
  {
    for (const auto &range : ranges)
    {
      if (range.collapsed && line > range.start_line && line <= range.end_line)
      {
        return true;
      }
    }
    return false;
  }

  int ref_visible_count(const std::vector<FoldRange> &ranges, int line_count)
  {
    int visible = 0;
    for (int line = 0; line < line_count; line++)
    {
      if (!ref_hidden(ranges, line))
      {
        visible++;
      }
    }
    return std::max(1, visible);
  }

  int ref_index(const std::vector<FoldRange> &ranges, int index, int line_count)
  {
    int target = std::max(0, index);
    for (int line = 0; line < line_count; line++)
    {
      if (ref_hidden(ranges, line))
      {
        continue;
      }
      if (target == 0)
      {
        return line;
      }
      target--;
    }
    return std::max(0, line_count - 1);
  }

  int ref_offset(const std::vector<FoldRange> &ranges, int first_line, int offset, int line_count)
  {
    if (line_count <= 0)
    {
      return -1;
    }
    int current = std::clamp(first_line, 0, line_count - 1);
    while (current < line_count && ref_hidden(ranges, current))
    {
      current++;
    }
    if (current >= line_count)
    {
      return -1;
    }
    for (int i = 0; i < offset; i++)
    {
      if (current >= line_count - 1)
      {
        return -1;
      }
      int next = std::min(current + 1, line_count - 1);
      while (next < line_count && ref_hidden(ranges, next))
      {
        next++;
      }
      next = std::min(next, line_count - 1);
      if (ref_hidden(ranges, next))
      {
        return -1;
      }
      current = next;
    }
    return current;
  }

  int ref_row_for_line(const std::vector<FoldRange> &ranges,
                       int first_line,
                       int target_line,
                       int visible_rows,
                       int line_count)
  {
    if (line_count <= 0 || visible_rows <= 0 || target_line < 0 || target_line >= line_count
        || ref_hidden(ranges, target_line))
    {
      return -1;
    }
    int current = std::clamp(first_line, 0, line_count - 1);
    while (current < line_count && ref_hidden(ranges, current))
    {
      current++;
    }
    for (int row = 0; row < visible_rows && current < line_count; row++)
    {
      if (current == target_line)
      {
        return row;
      }
      int next = std::min(current + 1, line_count - 1);
      while (next < line_count && ref_hidden(ranges, next))
      {
        next++;
      }
      next = std::min(next, line_count - 1);
      if (next == current)
      {
        break;
      }
      current = next;
    }
    return -1;
  }

  int ref_next_visible(const std::vector<FoldRange> &ranges, int line, int line_count)
  {
    const int last = std::max(0, line_count - 1);
    int next = std::min(line + 1, last);
    while (next < line_count && ref_hidden(ranges, next))
    {
      next++;
    }
    return std::min(next, last);
  }

  int ref_previous_visible(const std::vector<FoldRange> &ranges, int line)
  {
    int prev = std::max(0, line - 1);
    while (prev > 0 && ref_hidden(ranges, prev))
    {
      prev--;
    }
    return prev;
  }

  int ref_clamp_scroll(const std::vector<FoldRange> &ranges,
                       int scroll,
                       int visible_rows,
                       int line_count)
  {
    if (line_count <= 0)
    {
      return 0;
    }
    const int last = std::max(0, line_count - 1);
    int clamped = std::clamp(scroll, 0, last);
    while (clamped > 0 && ref_hidden(ranges, clamped))
    {
      clamped--;
    }
    const int visible_total = ref_visible_count(ranges, line_count);
    const int max_visible_start = std::max(0, visible_total - std::max(1, visible_rows));
    int visible_before_clamped = 0;
    for (int line = 0; line < clamped; line++)
    {
      if (!ref_hidden(ranges, line))
      {
        visible_before_clamped++;
      }
    }
    if (visible_before_clamped <= max_visible_start)
    {
      return clamped;
    }
    // Highest allowed start: the line whose visible rank is max_visible_start.
    int rank = 0;
    for (int line = 0; line < line_count; line++)
    {
      if (ref_hidden(ranges, line))
      {
        continue;
      }
      if (rank == max_visible_start)
      {
        return line;
      }
      rank++;
    }
    return clamped;
  }

  struct FoldScene
  {
    const char *name;
    std::vector<FoldRange> ranges;
    int line_count;
  };

  std::vector<FoldScene> fold_scenes()
  {
    return {
        {"no ranges", {}, 20},
        {"uncollapsed only", {{1, 5, false}, {7, 9, false}}, 20},
        {"one collapsed", {{4, 9, true}}, 20},
        {"nested", {{2, 12, true}, {5, 8, true}}, 20},
        {"overlapping", {{0, 6, true}, {4, 10, true}}, 20},
        {"touching spans", {{1, 3, true}, {4, 6, true}}, 20},
        {"unsorted input", {{9, 12, true}, {1, 3, true}}, 20},
        {"hidden head", {{0, 5, true}}, 20},
        {"hidden tail", {{15, 19, true}}, 20},
        {"whole file folded", {{0, 19, true}}, 20},
        {"degenerate empty range", {{5, 5, true}}, 20},
        {"collapsed at the last line", {{19, 19, true}}, 20},
        // What a real C++ file produces: one range per brace pair, sorted by
        // start line (longest first) with the inner blocks collapsed.
        {"typical cpp", {{0, 11, false}, {1, 5, true}, {2, 4, true}, {6, 10, true}}, 12},
    };
  }
} // namespace

// The prepared view and the free functions must answer the same question.
// The view is what the renderer holds for a whole frame and asks per row, so a
// disagreement here is a visible one: the wrong line, or a row the caret is
// hidden on.
TEST_CASE("Folding view matches the per-line reference", "[jot]")
{
  for (const auto &scene : fold_scenes())
  {
    INFO(scene.name);
    const std::vector<FoldRange> &ranges = scene.ranges;
    const Folding::FoldView view(ranges);
    const int line_count = scene.line_count;

    for (int line = 0; line < line_count; line++)
    {
      INFO("line " << line);
      REQUIRE(view.hidden(line) == ref_hidden(ranges, line));
      REQUIRE(view.folded_header(line) == Folding::is_line_folded_header(ranges, line));
      REQUIRE(view.hidden_count_for_header(line)
              == Folding::hidden_line_count_for_header(ranges, line));
      REQUIRE(view.next_visible_line(line, line_count) == ref_next_visible(ranges, line, line_count));
      REQUIRE(view.previous_visible_line(line) == ref_previous_visible(ranges, line));
      // The two implementations of the step (prepared view, straight scan)
      // have to agree, or the caret and the renderer disagree about which line
      // is next.
      REQUIRE(Folding::next_visible_line(ranges, line, line_count)
              == ref_next_visible(ranges, line, line_count));
      REQUIRE(Folding::previous_visible_line(ranges, line)
              == ref_previous_visible(ranges, line));
    }

    // The walk the arrow keys use: the same answer as one view step per line.
    for (int start = 0; start < line_count; start++)
    {
      int expected = start;
      for (int step = 0; step < 5; step++)
      {
        expected = ref_next_visible(ranges, expected, line_count);
      }
      INFO("walk from " << start);
      REQUIRE(Folding::advance_visible_lines(ranges, start, 5, line_count) == expected);
    }

    REQUIRE(view.visible_line_count(line_count) == ref_visible_count(ranges, line_count));

    for (int first = 0; first <= line_count; first++)
    {
      for (int offset = 0; offset <= line_count; offset++)
      {
        INFO("first " << first << " offset " << offset);
        REQUIRE(view.buffer_line_for_visible_offset(first, offset, line_count)
                == ref_offset(ranges, first, offset, line_count));
        REQUIRE(Folding::buffer_line_for_visible_offset(ranges, first, offset, line_count)
                == ref_offset(ranges, first, offset, line_count));
      }
      for (int index = 0; index <= line_count; index++)
      {
        INFO("index " << index);
        REQUIRE(view.buffer_line_for_visible_index(index, line_count)
                == ref_index(ranges, index, line_count));
        REQUIRE(Folding::buffer_line_for_visible_index(ranges, index, line_count)
                == ref_index(ranges, index, line_count));
      }
      for (int rows : {1, 3, 34})
      {
        for (int target = 0; target < line_count; target++)
        {
          INFO("first " << first << " target " << target << " rows " << rows);
          REQUIRE(view.visible_row_for_line(first, target, rows, line_count)
                  == ref_row_for_line(ranges, first, target, rows, line_count));
        }
      }
      for (int rows : {1, 3, 34})
      {
        INFO("scroll " << first << " rows " << rows);
        REQUIRE(view.clamp_scroll_offset(first, rows, line_count)
                == ref_clamp_scroll(ranges, first, rows, line_count));
        REQUIRE(Folding::clamp_scroll_offset(ranges, first, rows, line_count)
                == ref_clamp_scroll(ranges, first, rows, line_count));
      }
    }
  }
}

// A whole-file fold must not make the walk quadratic: the view answers "the
// next visible line" by jumping to the end of the hiding span, so a file that
// is one collapsed block still steps once per visible line.
TEST_CASE("Folding view walks past a fully folded file", "[jot]")
{
  // The block hides 1..99998, so exactly two lines are visible and the step
  // between them is one jump over the collapsed span.
  std::vector<FoldRange> ranges = {{0, 99998, true}};
  const Folding::FoldView view(ranges);
  REQUIRE(view.visible_line_count(100000) == 2);
  REQUIRE_FALSE(view.hidden(0));
  REQUIRE(view.hidden(50000));
  REQUIRE(view.next_visible_line(0, 100000) == 99999);
  REQUIRE(view.buffer_line_for_visible_index(0, 100000) == 0);
  REQUIRE(view.buffer_line_for_visible_index(1, 100000) == 99999);
  REQUIRE(view.buffer_line_for_visible_offset(0, 1, 100000) == 99999);
  REQUIRE(view.buffer_line_for_visible_offset(0, 2, 100000) == -1);

  // A file folded to its last line has a single visible line, and asking for
  // the one after it declines instead of walking the hidden tail.
  std::vector<FoldRange> whole = {{0, 99999, true}};
  const Folding::FoldView whole_view(whole);
  REQUIRE(whole_view.visible_line_count(100000) == 1);
  REQUIRE(whole_view.buffer_line_for_visible_offset(0, 1, 100000) == -1);
  REQUIRE(whole_view.buffer_line_for_visible_index(1, 100000) == 99999);
}

TEST_CASE("Folding Detects C++ Block", "[jot]")
{
  std::vector<std::string> lines = {
      "int main() {",
      "  if (ok) {",
      "    return 1;",
      "  }",
      "}",
  };
  auto ranges = Folding::detect_ranges(lines, ".cpp");
  REQUIRE_FALSE(ranges.empty());
  REQUIRE(ranges[0].start_line == 0);
  REQUIRE(ranges[0].end_line == 4);
}

TEST_CASE("Folding Detects Python Indent Block", "[jot]")
{
  std::vector<std::string> lines = {
      "def f():",
      "    x = 1",
      "    return x",
      "print(f())",
  };
  auto ranges = Folding::detect_ranges(lines, ".py");
  REQUIRE((int)ranges.size() == 1);
  REQUIRE(ranges[0].start_line == 0);
  REQUIRE(ranges[0].end_line == 2);
}

TEST_CASE("Folding Visible Line Mapping", "[jot]")
{
  std::vector<FoldRange> ranges = {{0, 4, true}, {1, 3, true}};
  REQUIRE_FALSE(Folding::is_line_hidden(ranges, 0));
  REQUIRE(Folding::is_line_hidden(ranges, 2));
  REQUIRE(Folding::next_visible_line(ranges, 0, 6) == 5);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 1, 6) == 5);
  REQUIRE(Folding::visible_line_count(ranges, 6) == 2);
}

TEST_CASE("Folding Visible Line Mapping Past End Returns Sentinel", "[jot]")
{
  std::vector<FoldRange> ranges;
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 0, 3) == 0);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 2, 3) == 2);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 3, 3) == -1);
}

TEST_CASE("Folding Visible Line Mapping Past Folded End Returns Sentinel", "[jot]")
{
  std::vector<FoldRange> ranges = {{0, 4, true}, {1, 3, true}};
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 0, 6) == 0);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 1, 6) == 5);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 2, 6) == -1);
}

TEST_CASE("Folding Visible Line Mapping Hidden Start Finds Next Visible Line", "[jot]")
{
  std::vector<FoldRange> ranges = {{0, 2, true}};
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 1, 0, 4) == 3);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 1, 1, 4) == -1);
}

TEST_CASE("Folding Visible Row For Line", "[jot]")
{
  std::vector<FoldRange> ranges = {{2, 4, true}};
  REQUIRE(Folding::visible_row_for_line(ranges, 0, 0, 6, 8) == 0);
  REQUIRE(Folding::visible_row_for_line(ranges, 0, 2, 6, 8) == 2);
  REQUIRE(Folding::visible_row_for_line(ranges, 0, 5, 6, 8) == 3);
  REQUIRE(Folding::visible_row_for_line(ranges, 0, 3, 6, 8) == -1);
}

TEST_CASE("Folding Buffer Line For Visible Index Skips Hidden Lines", "[jot]")
{
  std::vector<FoldRange> ranges = {{1, 3, true}, {6, 7, true}};
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 0, 9) == 0);
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 1, 9) == 1);
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 2, 9) == 4);
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 4, 9) == 6);
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 5, 9) == 8);
}

TEST_CASE("Folding Encode Decode Collapsed Ranges", "[jot]")
{
  std::vector<FoldRange> ranges = {{0, 4, true}, {6, 8, false}, {10, 12, true}};
  std::string encoded = Folding::encode_collapsed_ranges(ranges);
  REQUIRE(encoded == "0-4,10-12");

  auto decoded = Folding::decode_collapsed_ranges(encoded);
  REQUIRE((int)decoded.size() == 2);
  REQUIRE(decoded[0].start_line == 0);
  REQUIRE(decoded[0].end_line == 4);
  REQUIRE(decoded[0].collapsed);
  REQUIRE(decoded[1].start_line == 10);
  REQUIRE(decoded[1].end_line == 12);
}

TEST_CASE("Folding Decode Ignores Malformed Ranges", "[jot]")
{
  auto decoded = Folding::decode_collapsed_ranges("bad,4-x,7-6,2-5");
  REQUIRE((int)decoded.size() == 1);
  REQUIRE(decoded[0].start_line == 2);
  REQUIRE(decoded[0].end_line == 5);
}

namespace
{
  // What a freshly built index answers, bypassing the store: the cached index
  // has to give the same answers as this, or it is describing ranges the
  // buffer does not have any more.
  int fresh_visible_count(const FoldRanges &folds, int line_count)
  {
    return Folding::FoldView(folds.ranges()).visible_line_count(line_count);
  }
} // namespace

// The prepared index is what every fold question in the editor is answered
// from, so it must be impossible for it to describe anything but the ranges it
// was handed. The first two cases pin the two halves of that: one index per
// revision (which is the point of caching it), retired by every write. The
// third pins the half the revision cannot cover on its own -- a change no
// writer made -- which is what the checksum is for.
TEST_CASE("The prepared fold index is reused until a write retires it", "[jot]")
{
  FoldRanges folds;
  folds.assign({{0, 4, false}, {10, 14, false}});

  const auto first = Folding::view_of(folds);
  const auto second = Folding::view_of(folds);
  REQUIRE(first.get() == second.get());
  REQUIRE(first->visible_line_count(20) == 20);
  REQUIRE_FALSE(folds.index_needs_rebuild());

  // A write retires it, and the next lookup rebuilds from the new state -- the
  // folded block's four hidden lines are gone from the visible count.
  folds.set_collapsed(0, true);
  const auto folded = Folding::view_of(folds);
  REQUIRE(folded.get() != first.get());
  REQUIRE(folded->visible_line_count(20) == 16);
  REQUIRE(folded->hidden(2));
  REQUIRE(folded.get() == Folding::view_of(folds).get());
  REQUIRE_FALSE(folds.index_needs_rebuild());

  // The block itself is still a foldable header, at its own position in the
  // range vector -- the index records that position, so it is what invalidates
  // when a range in front of it appears or disappears.
  int header_index = -1;
  REQUIRE(folded->folded_header(0, &header_index));
  REQUIRE(header_index == 0);
}

TEST_CASE("Every fold-range writer invalidates the index", "[jot]")
{
  FoldRanges folds;

  const auto expect_current = [&folds](int line_count) {
    // Whatever asked first prepares the index; the store's own verification is
    // then revision plus a checksum of the contents, and the answer has to
    // match a view built from scratch right now.
    const auto view = Folding::view_of(folds);
    REQUIRE_FALSE(folds.index_needs_rebuild());
    REQUIRE(view->visible_line_count(line_count) == fresh_visible_count(folds, line_count));
  };

  // assign(), and the assignment operator built on it.
  folds.assign({{0, 4, true}, {10, 14, false}});
  expect_current(20);
  std::uint64_t revision = folds.revision();
  folds = std::vector<FoldRange>{{0, 4, false}};
  REQUIRE(folds.revision() > revision);
  expect_current(20);

  // set_collapsed(): both directions, and a write of the value already there is
  // not a change and must not retire the index.
  revision = folds.revision();
  folds.set_collapsed(0, true);
  REQUIRE(folds.revision() > revision);
  expect_current(20);
  revision = folds.revision();
  folds.set_collapsed(0, true);
  REQUIRE(folds.revision() == revision);
  folds.set_collapsed(0, false);
  REQUIRE(folds.revision() > revision);
  expect_current(20);

  // set_collapsed() on an index past the end is a no-op, not a stray write.
  revision = folds.revision();
  folds.set_collapsed(99, true);
  REQUIRE(folds.revision() == revision);

  // set_all_collapsed(): the fold-all / unfold-all commands, including the
  // no-op case that must leave the index standing.
  folds.set_all_collapsed(true);
  expect_current(20);
  REQUIRE(folds[0].collapsed);
  revision = folds.revision();
  folds.set_all_collapsed(true);
  REQUIRE(folds.revision() == revision);
  folds.set_all_collapsed(false);
  REQUIRE(folds.revision() > revision);
  expect_current(20);
  REQUIRE_FALSE(folds[0].collapsed);

  // clear().
  folds.clear();
  REQUIRE(folds.empty());
  expect_current(20);

  // The module entry points that rewrite the ranges from outside: a
  // re-detection that keeps the folded set, and a restore of a saved one.
  std::vector<std::string> lines = {"int f() {", "  if (a) {", "    g();", "  }", "}"};
  Folding::refresh_ranges(folds, lines, ".cpp");
  REQUIRE(folds.size() == 2);
  REQUIRE(folds[0].start_line == 0);
  REQUIRE(folds[0].end_line == 4);
  folds.set_collapsed(0, true);
  expect_current(5);

  // Re-detecting the same text keeps the fold -- the pair still exists -- and
  // the write happens once, so the index retires once.
  Folding::refresh_ranges(folds, lines, ".cpp");
  REQUIRE(folds[0].collapsed);
  expect_current(5);

  // A restore of a saved set (the session's collapsed ranges) replaces the
  // whole collapsed state in one write.
  Folding::apply_collapsed_ranges(folds, {{0, 4, false}, {1, 3, true}});
  REQUIRE_FALSE(folds[0].collapsed);
  REQUIRE(folds[1].collapsed);
  expect_current(5);

  Folding::apply_collapsed_ranges(folds, {{0, 4, true}, {1, 3, false}});
  REQUIRE(folds[0].collapsed);
  REQUIRE_FALSE(folds[1].collapsed);
  expect_current(5);
}

TEST_CASE("A change no writer made is caught by the checksum", "[jot]")
{
  FoldRanges folds;
  folds.assign({{0, 4, false}, {10, 14, false}});
  const auto cached = Folding::view_of(folds);
  REQUIRE(cached->visible_line_count(20) == 20);

  // The threat model the checksum exists for: a writer that never went through
  // the store, so no revision changed -- an alias held across a mutation, or a
  // future writer added beside this class. Written through a const_cast here
  // because reaching these fields past the store is exactly that bug.
  const std::uint64_t revision = folds.revision();
  const std::vector<FoldRange> &alias = folds.ranges();
  const_cast<FoldRange &>(alias[0]).collapsed = true;
  REQUIRE(folds.revision() == revision);
  REQUIRE(cached->visible_line_count(20) == 20); // the stale index still says so

  // The verification sees it: the index was built from different contents than
  // the ranges hold.
  REQUIRE(folds.index_needs_rebuild());

  // And view_of() runs that verification on its own cadence, so a caller that
  // only ever asks for the index is re-indexed within kVerifyEveryAccesses
  // lookups rather than drawn from the old shape indefinitely.
  std::shared_ptr<const Folding::FoldView> view;
  for (unsigned i = 0; i < FoldRanges::kVerifyEveryAccesses; i++)
  {
    view = Folding::view_of(folds);
  }
  REQUIRE(view->visible_line_count(20) == 16);
  REQUIRE(view->hidden(2));
  REQUIRE_FALSE(folds.index_needs_rebuild());
  REQUIRE(view.get() != cached.get());
}

TEST_CASE("A copied fold store carries a valid index", "[jot]")
{
  // The closed-buffer snapshot copies the store (the restore in buffers.cpp),
  // so a copy must neither resurrect a stale index nor throw away a good one:
  // the index is immutable and its content is a function of the ranges, which
  // the copy carries identically.
  FoldRanges folds;
  folds.assign({{0, 4, true}, {10, 14, false}});
  const auto prepared = Folding::view_of(folds);

  FoldRanges copy = folds;
  REQUIRE(copy.revision() == folds.revision());
  REQUIRE_FALSE(copy.index_needs_rebuild());
  REQUIRE(Folding::view_of(copy).get() == prepared.get());

  // A write in the copy retires only the copy's index. The original's ranges
  // did not change, so its index still stands -- including for a copy taken
  // before that write.
  copy.set_collapsed(1, true);
  REQUIRE(Folding::view_of(copy).get() != prepared.get());
  REQUIRE(Folding::is_line_hidden(copy, 12));
  REQUIRE_FALSE(Folding::is_line_hidden(folds, 12));
  REQUIRE(Folding::view_of(folds).get() == prepared.get());

  // Assignment (the restore path) is the same: the restored store answers from
  // the copied index instead of rebuilding one.
  FoldRanges restored;
  restored.assign({{7, 9, false}});
  Folding::view_of(restored);
  restored = copy;
  REQUIRE_FALSE(restored.index_needs_rebuild());
  REQUIRE(Folding::is_line_hidden(restored, 12));
  REQUIRE(Folding::view_of(restored).get() == Folding::view_of(copy).get());
}

TEST_CASE("Buffer call sites answer from the prepared index", "[jot]")
{
  FoldRanges folds;
  folds.assign({{0, 4, false}, {10, 14, false}});
  REQUIRE(folds.prepared_index() == nullptr);

  // Passing the store (which is what `buf.fold_ranges` is) prepares the index
  // once and answers every later question from it, and the answers match the
  // one-shot forms the tests and standalone helpers use.
  REQUIRE_FALSE(Folding::is_line_hidden(folds, 12));
  REQUIRE(folds.prepared_index() != nullptr);
  folds.set_collapsed(1, true);
  REQUIRE(Folding::is_line_hidden(folds, 12));
  REQUIRE(Folding::is_line_hidden(folds, 12));
  REQUIRE(Folding::hidden_line_count_for_header(folds, 10) == 4);
  REQUIRE(Folding::visible_line_count(folds, 20) == 16);
  // Offset 16 is the 17th visible row of a 16-line view: past the end, which
  // the mouse path handles as "no line" (it walks back up for a real one).
  REQUIRE(Folding::buffer_line_for_visible_offset(folds, 0, 16, 20) == -1);
  REQUIRE(Folding::buffer_line_for_visible_offset(folds, 0, 15, 20) == 19);
  REQUIRE(Folding::clamp_scroll_offset(folds, 12, 5, 20) == 10);
  REQUIRE(Folding::advance_visible_lines(folds, 9, 2, 20) == 15);
  REQUIRE(Folding::previous_visible_line(folds, 12) == 10);
  REQUIRE(Folding::next_visible_line(folds, 9, 20) == 10);
  // The header itself steps past the block it folds.
  REQUIRE(Folding::next_visible_line(folds, 10, 20) == 15);
  REQUIRE_FALSE(Folding::is_line_folded_header(folds, 9));
  REQUIRE(Folding::is_line_folded_header(folds, 10));
  REQUIRE(Folding::visible_row_for_line(folds, 0, 15, 20, 20) == 11);
  REQUIRE(Folding::buffer_line_for_visible_index(folds, 11, 20) == 15);

  // The same answers, asked of a bare vector.
  const std::vector<FoldRange> &raw = folds.ranges();
  REQUIRE(Folding::is_line_hidden(raw, 12));
  REQUIRE(Folding::hidden_line_count_for_header(raw, 10) == 4);
  REQUIRE(Folding::visible_line_count(raw, 20) == 16);
  REQUIRE(Folding::clamp_scroll_offset(raw, 12, 5, 20) == 10);
}

TEST_CASE("Folding Apply Collapsed Ranges Requires Exact Match", "[jot]")
{
  FoldRanges folds;
  folds.assign({{0, 4, false}, {5, 9, false}});
  std::vector<FoldRange> collapsed = {{0, 4, true}, {7, 9, true}};
  Folding::apply_collapsed_ranges(folds, collapsed);
  REQUIRE(folds[0].collapsed);
  REQUIRE_FALSE(folds[1].collapsed);
}
