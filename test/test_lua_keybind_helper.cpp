#include "jot/keybind_catalog.h"
#include "jot/lua/api.h"
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

TEST_CASE("Chord naming keeps named keys distinct under Ctrl")
{
  using jot::keybind_detail::chord_name;
  // Ctrl+Enter must stay "Ctrl+Enter" (regression: it used to collapse to
  // "Ctrl+M" via the ^M -> letter translation, breaking Lua keymaps).
  REQUIRE(chord_name(13, true, false, false, 13) == "Ctrl+Enter");
  REQUIRE(chord_name(13, true, true, false, 13) == "Ctrl+Shift+Enter");
  REQUIRE(chord_name('\t', true, false, false, '\t') == "Ctrl+Tab");
  REQUIRE(chord_name(27, true, false, false, 27) == "Ctrl+Esc");
  REQUIRE(chord_name(127, true, false, false, 127) == "Ctrl+Backspace");
  // Plain control letters still map to their letter form.
  REQUIRE(chord_name(19, true, false, false, 19) == "Ctrl+S");
  REQUIRE(chord_name('s', true, true, false, 's') == "Ctrl+Shift+S");
  REQUIRE(chord_name(1008, true, false, false, 1008) == "Ctrl+Up");
  // PageUp/PageDown keep their names so Lua keymaps can bind them
  // (debugger output scrollback uses Ctrl+PageUp / Ctrl+PageDown).
  REQUIRE(chord_name(1015, false, false, false, 1015) == "PageUp");
  REQUIRE(chord_name(1016, true, false, false, 1016) == "Ctrl+PageDown");
}

TEST_CASE("CSI-u decode follows the kitty bitmask+1 modifier convention")
{
  using jot::keybind_detail::decode_csi_u_key;
  // Protocol modifier = raw bitmask + 1: Ctrl=5, Ctrl+Shift=6. A naive
  // "5 = Shift+Ctrl" mapping turns Ctrl+Enter into Ctrl+Shift+Enter, which
  // is exactly the bug that made the VSCode-style line keys misfire.
  constexpr int kCtrl = 0x20000;
  constexpr int kShift = 0x80000;
  REQUIRE((decode_csi_u_key("\x1b[13;5u") & 0xFFFF) == 13);
  REQUIRE((decode_csi_u_key("\x1b[13;5u") & (kCtrl | kShift)) == kCtrl);
  REQUIRE((decode_csi_u_key("\x1b[13;6u") & (kCtrl | kShift)) == (kCtrl | kShift));
  REQUIRE((decode_csi_u_key("\x1b[13;6u") & 0xFFFF) == 13);
  // Plain 13;1u stays unmodified Enter; alt and shift-only stay distinct.
  REQUIRE(decode_csi_u_key("\x1b[13;1u") == 13);
  REQUIRE((decode_csi_u_key("\x1b[13;3u") & 0x40000) != 0);
  REQUIRE((decode_csi_u_key("\x1b[13;2u") & kShift) != 0);
  // Letters keep their case unless Shift is actually held. Uppercasing every
  // letter lost the difference between Ctrl+B and Ctrl+Shift+B, and the global
  // sidebar toggle reads the case to choose between the left explorer and the
  // right dock.
  REQUIRE((decode_csi_u_key("\x1b[115;5u") & 0xFFFF) == 's');
  REQUIRE((decode_csi_u_key("\x1b[98;5u") & 0xFFFF) == 'b');
  REQUIRE((decode_csi_u_key("\x1b[98;5u") & kShift) == 0);
  // Shift+letter is uppercase (the convention the rest of the input path uses),
  // whether the terminal reports the shifted code or the base code plus Shift.
  REQUIRE((decode_csi_u_key("\x1b[98;6u") & 0xFFFF) == 'B');
  REQUIRE((decode_csi_u_key("\x1b[98;6u") & kShift) != 0);
  REQUIRE((decode_csi_u_key("\x1b[66;6u") & 0xFFFF) == 'B');
  REQUIRE((decode_csi_u_key("\x1b[66;6u") & kShift) != 0);
  // The canonical chord name is uppercase either way, so plugin keymaps that
  // register "Ctrl+B" still match the lowercase event. (chord_name takes the
  // already-decoded key, not a raw code with modifier bits.)
  REQUIRE(jot::keybind_detail::chord_name('b', true, false, false, 0) == "Ctrl+B");
  REQUIRE(jot::keybind_detail::chord_name('B', true, true, false, 0) == "Ctrl+Shift+B");
  // Non-CSI-u input and malformed bodies are rejected.
  REQUIRE(decode_csi_u_key("abc") == -1);
  REQUIRE(decode_csi_u_key("\x1b[13") == -1);
  // Empty modifier field means "no modifier" (protocol default of 1).
  REQUIRE(decode_csi_u_key("\x1b[13;u") == 13);
  REQUIRE(decode_csi_u_key("\x1b[abc;5u") == -1);
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
