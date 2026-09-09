#include "editor.h"
#include "features/column_utils.h"
#include "jot/lua/api.h"
#include "lsp/client.h"
extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <utility>

// Regression tests for the inlay-hint coordinate helpers shared by the
// buffer renderer, the hardware caret placement, and the mouse click
// mapping. These helpers must count exactly the hints the renderer draws:
// hints on OTHER lines must never leak into a line's shift (that made
// carets render far right of the clicked character and clicks land
// off-target), and widths must match the drawn labels so click mapping and
// caret placement agree with what is on screen.
namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_inlay_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // Seeds the per-file inlay-hint cache through the public test hook.
  std::string seed_cache(const std::vector<LSPInlayHint> &hints,
                         const std::string &filepath = "/tmp/inlay_probe.cpp")
  {
    Editor &e = probe_editor();
    e.set_inlay_hints_for_test(filepath, hints);
    return filepath;
  }

  void set_config_bool(Editor &e, const char *key, bool value)
  {
    LuaAPI api(&e);
    lua_State *L = luaL_newstate();
    lua_pushstring(L, key);
    lua_pushboolean(L, value ? 1 : 0);
    api.config_set_from_lua(L);
    lua_close(L);
  }

  LSPInlayHint hint(int line, int character, std::string label, int kind = 2)
  {
    LSPInlayHint h;
    h.line = line;
    h.character = character;
    h.label = std::move(label);
    h.kind = kind;
    return h;
  }

  int hint_width(const LSPInlayHint &h)
  {
    int w = ui_cell_count(h.label);
    if (h.padding_left)
      w += 1;
    if (h.padding_right)
      w += 1;
    return w;
  }
} // namespace

TEST_CASE("Inlay hint helpers only count hints on the queried line", "[jot][lsp]")
{
  Editor &e = probe_editor();
  set_config_bool(e, "lsp_inlay_hints", true);
  set_config_bool(e, "lsp_inlay_type_hints", true);

  const std::string file = seed_cache(
      {hint(0, 5, "x:"), hint(0, 9, "f:"), hint(2, 8, "c:"), hint(2, 12, "d:")});

  const std::string line0 = "auto x = f(a, b);";
  const std::string line2 = "int y = g(c, d);";

  // Line 2's hints shift to byte-visual + earlier hints on the same line:
  // byte 8 -> 8, byte 12 -> 12 + 2 = 14. Line 0's hints must not appear.
  const auto vis = e.lsp_inlay_hints_visual(file, 2, line2, 4);
  REQUIRE(vis.size() == 2);
  REQUIRE(vis[0] == std::make_pair(8, 2));
  REQUIRE(vis[1] == std::make_pair(14, 2));

  // Cells before a byte on line 2 count only line 2's hints: the hint AT
  // byte 8 shifts the glyph there, so it counts for byte_col >= 8.
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 2, 7, line2) == 0);
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 2, 8, line2) == 2);
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 2, 13, line2) == 4);

  // Line 0's own hints still shift line 0.
  REQUIRE(e.lsp_inlay_hints_visual(file, 0, line0, 4).size() == 2);
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 0, 6, line0) == 2);
}

TEST_CASE("Inlay hint width and kind gating match the renderer", "[jot][lsp]")
{
  Editor &e = probe_editor();
  set_config_bool(e, "lsp_inlay_hints", true);
  set_config_bool(e, "lsp_inlay_type_hints", true);

  // A type hint (kind 1) and a parameter hint (kind 2) share byte 5; a
  // third hint sits at byte 9.
  const std::string file =
      seed_cache({hint(0, 5, "int", 1), hint(0, 5, "x:", 2), hint(0, 9, "y:", 2)},
                 "/tmp/inlay_probe2.cpp");

  const std::string line = "auto x = f(y);";

  // Both hints at byte 5 shift it (a hint counts for byte_col >= its
  // position, so the caret at byte 5 sits right of both labels); byte 9's
  // hint adds its own width. Widths: "int" = 3, "x:" = 2, "y:" = 2.
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 0, 4, line) == 0);
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 0, 5, line) == 5);
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 0, 10, line) == 7);

  // Visual positions: 5 + 0, 5 + 3, 9 + 5.
  const auto vis = e.lsp_inlay_hints_visual(file, 0, line, 4);
  REQUIRE(vis.size() == 3);
  REQUIRE(vis[0] == std::make_pair(5, 3));
  REQUIRE(vis[1] == std::make_pair(8, 2));
  REQUIRE(vis[2] == std::make_pair(14, 2));

  // Disabling type hints drops kind 1 from every helper ("x:" = 2).
  set_config_bool(e, "lsp_inlay_type_hints", false);
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 0, 6, line) == 2);
  REQUIRE(e.lsp_inlay_hints_visual(file, 0, line, 4).size() == 2);

  // Master switch disables everything.
  set_config_bool(e, "lsp_inlay_hints", false);
  REQUIRE(e.lsp_inlay_hint_cells_before(file, 0, 6, line) == 0);
  REQUIRE(e.lsp_inlay_hints_visual(file, 0, line, 4).empty());
}

TEST_CASE("Click mapping lands on the drawn character with hints", "[jot][lsp]")
{
  Editor &e = probe_editor();
  set_config_bool(e, "lsp_inlay_hints", true);
  set_config_bool(e, "lsp_inlay_type_hints", true);

  // Hints on several lines, including the queried one: a stale pre-fix
  // implementation counted all lines' hints and shoved clicks and carets
  // right by the other lines' widths.
  const std::vector<LSPInlayHint> seeded = {hint(0, 5, "x:"), hint(0, 9, "f:"),
                                            hint(0, 15, "p:"), hint(2, 8, "c:"),
                                            hint(2, 12, "d:"), hint(2, 16, "e:")};
  const std::string file = seed_cache(seeded);

  const std::string line2 = "int y = g(c, d, e);";
  const int tab = 4;

  // Replays the dispatcher: screen column = start_visual + rel_x, then
  // subtract the hints whose shifted column sits at/before the click.
  auto click_to_byte = [&](int rel_x, int scroll_x)
  {
    int start_visual = compute_visual_column(line2, scroll_x, tab);
    int click_visual = start_visual + std::max(0, rel_x);
    const int raw = click_visual;
    for (const auto &hw : e.lsp_inlay_hints_visual(file, 2, line2, tab))
    {
      if (hw.first <= raw)
      {
        click_visual -= hw.second;
      }
    }
    return visual_to_logical_column(line2, click_visual, tab);
  };

  // The renderer draws byte `b` at visual(b) + every same-line hint at or
  // before b (the hint at b shifts the glyph right of its label).
  auto drawn_column = [&](int b)
  {
    int col = compute_visual_column(line2, b, tab);
    for (const auto &h : seeded)
    {
      if (h.line == 2 && h.character <= b)
      {
        col += hint_width(h);
      }
    }
    return col;
  };

  for (int scroll_x : {0, 5})
  {
    const int start_visual = compute_visual_column(line2, scroll_x, tab);
    for (int byte = 0; byte <= (int)line2.size(); byte++)
    {
      const int drawn = drawn_column(byte);
      if (drawn < start_visual)
      {
        continue; // scrolled off the left edge; not clickable
      }
      const int rel_x = drawn - start_visual;

      // A click exactly on the glyph's first cell maps back to the byte.
      REQUIRE(click_to_byte(rel_x, scroll_x) == byte);

      // The hardware caret is drawn at the same column the glyph occupies.
      const int caret = compute_visual_column(line2, byte, tab) - start_visual
                        + e.lsp_inlay_hint_cells_before(file, 2, byte, line2);
      REQUIRE(caret == rel_x);
    }
  }
}