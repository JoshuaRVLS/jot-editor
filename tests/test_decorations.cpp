// Anchored decorations (extmark-style): the anchoring transform math and the
// sorted-vector invariants maintained by the decoration helpers. The Lua API
// surface is exercised end-to-end by tests/runtime_smoke.lua (real editor);
// here the pure pieces run without one.
#include "core/app/decorations.h"
#include "core/types.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
  Decoration deco(std::uint64_t id, int row, int col, int width = 0, int priority = 50)
  {
    Decoration d;
    d.id = id;
    d.row = row;
    d.col = col;
    d.width = width;
    d.priority = priority;
    return d;
  }

  bool sorted_ok(const std::vector<Decoration> &v)
  {
    for (size_t i = 1; i < v.size(); i++)
    {
      const Decoration &a = v[i - 1];
      const Decoration &b = v[i];
      if (a.row != b.row)
      {
        if (a.row > b.row)
          return false;
        continue;
      }
      if (a.col != b.col)
      {
        if (a.col > b.col)
          return false;
        continue;
      }
      if (a.priority != b.priority)
      {
        if (a.priority < b.priority)
          return false; // descending within the same position
        continue;
      }
      if (a.id >= b.id)
        return false;
    }
    return true;
  }
} // namespace

TEST_CASE("Decoration anchoring shifts marks through an insertion")
{
  // Insert "brave " (6 bytes) at byte 6 of line 0.
  const std::string base = "hello world\nsecond line\n";
  const std::string current = "hello brave world\nsecond line\n";
  std::vector<Decoration> decos = {
      deco(1, 0, 2),  // strictly before the edit: unchanged
      deco(2, 0, 6),  // right gravity at the window start: pushed after the insert
      deco(3, 0, 6, 0, 50), // left gravity at the window start: stays
      deco(4, 0, 11), // at the (unchanged) window end: shifts by +6
      deco(5, 1, 2),  // later line: col preserved, row preserved (suffix identical)
  };
  decos[2].right_gravity = false;
  decoration_anchor_transform(decos, base, current);
  REQUIRE(decos[0].row == 0);
  REQUIRE(decos[0].col == 2);
  REQUIRE(decos[1].row == 0);
  REQUIRE(decos[1].col == 12); // right after the inserted "brave "
  REQUIRE(decos[2].row == 0);
  REQUIRE(decos[2].col == 6); // left gravity keeps it before the insert
  REQUIRE(decos[3].row == 0);
  REQUIRE(decos[3].col == 17); // the line's newline moved with the line
  REQUIRE(decos[4].row == 1);
  REQUIRE(decos[4].col == 2);
}

TEST_CASE("Decoration anchoring collapses through deletions")
{
  // Delete "X" at byte 1.
  const std::string base = "aXbc";
  const std::string current = "abc";
  std::vector<Decoration> decos = {
      deco(1, 0, 1, 0, 50), // right gravity on the deleted byte: collapses to the start
      deco(2, 0, 1, 0, 50), // left gravity on the deleted byte: collapses to the start
      deco(3, 0, 2),        // after the edit: shifts by -1
      deco(4, 0, 4),        // past the end of the line: shifts, clamps to line end
  };
  decos[1].right_gravity = false;
  decoration_anchor_transform(decos, base, current);
  REQUIRE(decos[0].col == 1);
  REQUIRE(decos[1].col == 1);
  REQUIRE(decos[2].col == 1);
  REQUIRE(decos[3].row == 0);
  REQUIRE(decos[3].col == 3);
}

TEST_CASE("Decoration anchoring handles multi-line edits")
{
  // Insert two lines after line 0.
  const std::string base = "a\nb\n";
  const std::string current = "a\nX\nY\nb\n";
  std::vector<Decoration> decos = {
      deco(1, 1, 0), // right gravity at the window start: lands after the inserted lines
      deco(2, 1, 0, 0, 50), // left gravity: stays on the inserted text
      deco(3, 1, 1), // inside the replaced window: collapses by gravity
  };
  decos[1].right_gravity = false;
  decoration_anchor_transform(decos, base, current);
  REQUIRE(decos[0].row == 3);
  REQUIRE(decos[0].col == 0);
  REQUIRE(decos[1].row == 1);
  REQUIRE(decos[1].col == 0);
  REQUIRE(decos[2].row == 3);
  REQUIRE(decos[2].col == 1); // the line's trailing newline moved with the line
}

TEST_CASE("Decoration anchoring is a no-op on identical text")
{
  const std::string text = "same text\n";
  std::vector<Decoration> decos = {deco(1, 0, 1), deco(2, 1, 0)};
  const auto snapshot = decos;
  decoration_anchor_transform(decos, text, text);
  REQUIRE(decos.size() == snapshot.size());
  for (size_t i = 0; i < decos.size(); i++)
  {
    REQUIRE(decos[i].id == snapshot[i].id);
    REQUIRE(decos[i].row == snapshot[i].row);
    REQUIRE(decos[i].col == snapshot[i].col);
  }
}

TEST_CASE("Decoration insert keeps the vector sorted and erase removes by id")
{
  FileBuffer buf;
  decoration_insert(buf, deco(7, 1, 5, 2, 50));
  decoration_insert(buf, deco(1, 0, 3, 1, 50));
  decoration_insert(buf, deco(3, 1, 5, 1, 90)); // same position, higher priority first
  decoration_insert(buf, deco(5, 1, 5, 1, 50)); // same position/priority, id order
  decoration_insert(buf, deco(2, 2, 0, 0, 50));
  REQUIRE(buf.decorations.size() == 5);
  REQUIRE(sorted_ok(buf.decorations));
  REQUIRE(buf.decorations.front().id == 1);

  REQUIRE(decoration_erase(buf, 3));
  REQUIRE_FALSE(decoration_erase(buf, 3));
  REQUIRE(buf.decorations.size() == 4);
  REQUIRE(sorted_ok(buf.decorations));
  for (const auto &d : buf.decorations)
  {
    REQUIRE(d.id != 3);
  }
}