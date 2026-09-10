// Settings menu (:settings): enumerates every config key (defaults +
// Lua-registered), bools toggle on Enter, ints/strings edit through the
// inline input row, and changes flow through config.set + save so they
// survive the session. The menu is cell-based, so the same surface serves
// the terminal and GUI frontends.
#include "editor.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_settings_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // Opens the menu from a known-closed state.
  void open_menu(Editor &e)
  {
    e.close_settings_menu_for_test();
    e.toggle_settings_menu_for_test();
    REQUIRE(e.settings_menu_open_for_test());
  }

  int entry_index(const Editor &e, const std::string &key)
  {
    for (int i = 0; i < (int)e.settings_entries_for_test().size(); i++)
    {
      if (e.settings_entries_for_test()[(size_t)i].key == key)
        return i;
    }
    return -1;
  }
} // namespace

TEST_CASE("Settings menu enumerates config keys with typed entries", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);
  REQUIRE_FALSE(e.settings_entries_for_test().empty());

  // Known bool key is typed as Bool; known int as Int.
  const int auto_save = entry_index(e, "auto_save");
  REQUIRE(auto_save >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)auto_save].type
          == SettingsEntry::Type::Bool);
  REQUIRE(e.settings_entries_for_test()[(size_t)auto_save].label == "Auto save");

  const int tab_size = entry_index(e, "tab_size");
  REQUIRE(tab_size >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)tab_size].type
          == SettingsEntry::Type::Int);
  REQUIRE(e.settings_entries_for_test()[(size_t)tab_size].label == "Tab size");
}

TEST_CASE("Settings menu toggles bools and saves the change", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);

  const std::string before = e.config_value_for_test("auto_save");
  const int idx = entry_index(e, "auto_save");
  REQUIRE(idx >= 0);

  // Select the row and toggle it; the value must flip and persist.
  e.settings_select_for_test(idx);
  REQUIRE(e.settings_input_for_test('\n'));
  const std::string after = e.config_value_for_test("auto_save");
  REQUIRE(after != before);
  REQUIRE((after == "true" || after == "false"));

  // Left/Right toggles too.
  e.settings_select_for_test(idx);
  const std::string before_lr = e.config_value_for_test("auto_save");
  REQUIRE(e.settings_input_for_test(1010));
  REQUIRE(e.config_value_for_test("auto_save") != before_lr);
}

TEST_CASE("Settings menu edits ints inline with validation", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);

  const int idx = entry_index(e, "tab_size");
  REQUIRE(idx >= 0);
  e.settings_select_for_test(idx);

  // Enter opens the inline edit with a fresh input buffer.
  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].editing);
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].edit_input.empty());

  // Type a new value and apply it.
  for (char c : std::string("8"))
  {
    e.settings_input_for_test(c);
  }
  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.config_int_for_test("tab_size") == 8);
  REQUIRE_FALSE(e.settings_entries_for_test()[(size_t)idx].editing);

  // Non-numeric input cancels the edit instead of corrupting the config.
  e.settings_select_for_test(idx);
  e.settings_input_for_test('\n');
  for (char c : std::string("abc"))
  {
    e.settings_input_for_test(c);
  }
  e.settings_input_for_test('\n');
  REQUIRE(e.config_int_for_test("tab_size") == 8);
  REQUIRE_FALSE(e.settings_entries_for_test()[(size_t)idx].editing);

  // Esc cancels the edit without applying anything.
  e.settings_select_for_test(idx);
  e.settings_input_for_test('\n');
  e.settings_input_for_test('9');
  REQUIRE(e.settings_input_for_test(27));
  REQUIRE_FALSE(e.settings_entries_for_test()[(size_t)idx].editing);
  REQUIRE(e.config_int_for_test("tab_size") == 8);
}

TEST_CASE("Settings menu closes on Esc", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);
  REQUIRE(e.settings_input_for_test(27));
  REQUIRE_FALSE(e.settings_menu_open_for_test());

  // Reopening rebuilds the list (menu is a fresh view over config.keys()).
  e.toggle_settings_menu_for_test();
  REQUIRE(e.settings_menu_open_for_test());
  REQUIRE(e.settings_input_for_test(27));
  REQUIRE_FALSE(e.settings_menu_open_for_test());
}