#include "lua_bridge/api.h"
#include <catch2/catch_test_macros.hpp>

// Regression tests for the which-key style helper: keymaps registered with
// space-separated chord sequences ("Ctrl+T N") must be resolvable into a
// prefix tree that the editor overlays (plugin_keymap_is_prefix /
// plugin_keymap_children / plugin_keymap_group_title).
TEST_CASE("Sequence keymaps resolve into which-key prefix groups")
{
  LuaAPI api(nullptr);
  api.register_keymap("Ctrl+T N", "", "", "New buffer", "global");
  api.register_keymap("Ctrl+T D", "", "", "Delete buffer", "global");
  api.register_keymap("Ctrl+T S P", "", "", "Sub pick", "global");
  // An unrelated single-chord binding must be untouched by the tree logic.
  api.register_keymap("Ctrl+S", "", "", "Save", "global");

  SECTION("prefix detection")
  {
    REQUIRE(api.plugin_keymap_is_prefix("Ctrl+T", "editor"));
    REQUIRE(api.plugin_keymap_is_prefix("Ctrl+T S", "editor"));
    // Single-chord leaves are not prefixes.
    REQUIRE_FALSE(api.plugin_keymap_is_prefix("Ctrl+S", "editor"));
    // Unknown chords never open the helper.
    REQUIRE_FALSE(api.plugin_keymap_is_prefix("Ctrl+N", "editor"));
    REQUIRE_FALSE(api.plugin_keymap_is_prefix("N", "editor"));
  }

  SECTION("root children")
  {
    const auto children = api.plugin_keymap_children("Ctrl+T", "editor");
    REQUIRE(children.size() == 3);
    REQUIRE(children[0].key == "D");
    REQUIRE(children[0].detail == "Delete buffer");
    REQUIRE_FALSE(children[0].group);
    REQUIRE(children[1].key == "N");
    REQUIRE(children[1].detail == "New buffer");
    REQUIRE_FALSE(children[1].group);
    // "S" only leads deeper ("Ctrl+T S P"), so it is a subgroup row.
    REQUIRE(children[2].key == "S");
    REQUIRE(children[2].group);
  }

  SECTION("descend into a subgroup")
  {
    const auto sub = api.plugin_keymap_children("Ctrl+T S", "editor");
    REQUIRE(sub.size() == 1);
    REQUIRE(sub[0].key == "P");
    REQUIRE(sub[0].detail == "Sub pick");
    REQUIRE_FALSE(sub[0].group);
  }

  SECTION("a completed leaf has no further children")
  {
    REQUIRE(api.plugin_keymap_children("Ctrl+T N", "editor").empty());
    REQUIRE(api.plugin_keymap_children("Ctrl+T D", "editor").empty());
    REQUIRE(api.plugin_keymap_children("Ctrl+S", "editor").empty());
  }

  SECTION("modes filter global/editor keymaps")
  {
    LuaAPI api2(nullptr);
    api2.register_keymap("Alt+1 X", "", "", "Mode-bound", "editor");
    REQUIRE(api2.plugin_keymap_is_prefix("Alt+1", "editor"));
    REQUIRE_FALSE(api2.plugin_keymap_is_prefix("Alt+1", "other_mode"));
    REQUIRE(api2.plugin_keymap_children("Alt+1", "other_mode").empty());
  }
}

TEST_CASE("Key registration canonicalizes sequence keys but keeps steps apart")
{
  LuaAPI api(nullptr);
  // Padding around / inside the sequence is trimmed and collapsed to single
  // spaces so the steps stay addressable ("Ctrl+T N"), never merged.
  api.register_keymap("  Ctrl+T   N  ", "", "", "Spaced", "global");
  REQUIRE(api.plugin_keymap_is_prefix("Ctrl+T", "editor"));
  const auto children = api.plugin_keymap_children("Ctrl+T", "editor");
  REQUIRE(children.size() == 1);
  REQUIRE(children[0].key == "N");
}
