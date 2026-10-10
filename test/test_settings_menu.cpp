// Settings menu (:settings): enumerates every config key (defaults +
// Lua-registered), bools toggle on Enter, ints/strings edit through the
// inline input row, and changes flow through config.set + save so they
// survive the session. The menu is cell-based, so the same surface serves
// the terminal and GUI frontends.
#include "core/editor.h"
#include <algorithm>
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

  // Where a key sits in the *filtered* list, which is what the selection
  // walks. With an empty query the two lists line up.
  int filtered_pos(const Editor &e, const std::string &key)
  {
    for (int i = 0; i < e.settings_match_count_for_test(); i++)
    {
      if (e.settings_key_at_for_test(i) == key)
        return i;
    }
    return -1;
  }

  bool filtered_contains(const Editor &e, const std::string &key)
  {
    return filtered_pos(e, key) >= 0;
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

TEST_CASE("Settings menu takes a paste into the row being edited", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);

  const int idx = entry_index(e, "wakatime_api_key");
  REQUIRE(idx >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].type == SettingsEntry::Type::String);
  e.settings_select_for_test(idx);
  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].editing);
  const std::vector<std::string> buffer_before = e.buffer_for_test(-1).lines;

  // A key copied out of a WakaTime account arrives with the newline its copy
  // carried, and that newline must end nothing: the value is what was pasted
  // without it, since nothing can type a newline into the row either.
  e.paste_event_for_test("abc-123\n");
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].edit_input == "abc-123");
  // The buffer behind the panel is not the field the user is looking at.
  REQUIRE(e.buffer_for_test(-1).lines == buffer_before);

  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.config_value_for_test("wakatime_api_key") == "abc-123");
  REQUIRE_FALSE(e.settings_entries_for_test()[(size_t)idx].editing);
}

TEST_CASE("Settings menu keeps a pasted query out of the buffer", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);
  const std::vector<std::string> buffer_before = e.buffer_for_test(-1).lines;

  // No row is being edited, so the paste is the search bar's -- the same field
  // a typed letter goes to -- and the list narrows to what arrived.
  e.paste_event_for_test("wakatime_api");
  REQUIRE(e.settings_query_for_test() == "wakatime_api");
  REQUIRE(filtered_contains(e, "wakatime_api_key"));
  REQUIRE_FALSE(filtered_contains(e, "tab_size"));
  REQUIRE(e.buffer_for_test(-1).lines == buffer_before);
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
TEST_CASE("Settings menu covers Lua-registered and LSP keys", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);

  // C++-read keys that previously fell back to raw names now carry labels.
  const int scheme = entry_index(e, "color_scheme");
  REQUIRE(scheme >= 0);
  // The scheme is one of a set -- the themes that are actually installed --
  // so it is offered as choices rather than as free text.
  REQUIRE(e.settings_entries_for_test()[(size_t)scheme].type
          == SettingsEntry::Type::Enum);
  REQUIRE(e.settings_entries_for_test()[(size_t)scheme].label == "Color scheme");
  const auto &schemes = e.settings_entries_for_test()[(size_t)scheme].options;
  REQUIRE_FALSE(schemes.empty());
  REQUIRE(std::find(schemes.begin(), schemes.end(), e.config_value_for_test("color_scheme"))
          != schemes.end());

  const int inlay = entry_index(e, "lsp_inlay_hints");
  REQUIRE(inlay >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)inlay].type
          == SettingsEntry::Type::Bool);
  REQUIRE(e.settings_entries_for_test()[(size_t)inlay].label == "LSP parameter hints");

  const int inlay_type = entry_index(e, "lsp_inlay_type_hints");
  REQUIRE(inlay_type >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)inlay_type].type
          == SettingsEntry::Type::Bool);

  const int inline_diag = entry_index(e, "decorations_inline_diagnostics");
  REQUIRE(inline_diag >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)inline_diag].type
          == SettingsEntry::Type::Bool);

  const int zen = entry_index(e, "zen_content_width");
  REQUIRE(zen >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)zen].type
          == SettingsEntry::Type::Int);
  REQUIRE(e.settings_entries_for_test()[(size_t)zen].label == "Zen content width");

  // The Lua feature keys registered as defaults are editable ints/bools.
  const int toast_w = entry_index(e, "toast.max_width");
  REQUIRE(toast_w >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)toast_w].type
          == SettingsEntry::Type::Int);
  REQUIRE(e.settings_entries_for_test()[(size_t)toast_w].label == "Toast max width");

  const int toast_dur = entry_index(e, "toast.duration_ms");
  REQUIRE(toast_dur >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)toast_dur].type
          == SettingsEntry::Type::Int);

  const int upd = entry_index(e, "update.check_on_startup");
  REQUIRE(upd >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)upd].type
          == SettingsEntry::Type::Bool);
  REQUIRE(e.settings_entries_for_test()[(size_t)upd].label
          == "Check updates on startup");
}

TEST_CASE("Settings menu disables LSP globally and live", "[jot]")
{
  Editor &e = probe_editor();
  // Start from a known state: the probe editor is shared with the cases above.
  e.set_config_for_test("lsp_enabled", "true");
  e.apply_config_live_for_test();
  open_menu(e);

  const int idx = entry_index(e, "lsp_enabled");
  REQUIRE(idx >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].type == SettingsEntry::Type::Bool);
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].label == "LSP (language servers)");
  // The lsp_ family files it beside the other server keys.
  REQUIRE(e.settings_section_for_test("lsp_enabled") == "Code intelligence");
  REQUIRE(e.lsp_enabled_for_test());

  // Enter flips the row and the editor acts on it: the global switch is down,
  // nothing attaches a server, and the client list is empty.
  e.settings_select_for_test(idx);
  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.config_value_for_test("lsp_enabled") == "false");
  REQUIRE_FALSE(e.lsp_enabled_for_test());
  REQUIRE(e.ensure_lsp_for_file_for_test("/tmp/jot_lsp_off_probe/main.cpp") == nullptr);
  REQUIRE(e.lsp_client_count_for_test() == 0);

  // Left/Right move it back, and the gate opens with it.
  e.settings_select_for_test(idx);
  REQUIRE(e.settings_input_for_test(1010));
  REQUIRE(e.config_value_for_test("lsp_enabled") == "true");
  REQUIRE(e.lsp_enabled_for_test());
}

TEST_CASE("Settings menu edits a toast key and persists it", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);

  const int idx = entry_index(e, "toast.max_width");
  REQUIRE(idx >= 0);
  e.settings_select_for_test(idx);
  REQUIRE(e.settings_input_for_test('\n'));
  for (char c : std::string("40"))
  {
    e.settings_input_for_test(c);
  }
  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.config_int_for_test("toast.max_width") == 40);
  REQUIRE_FALSE(e.settings_entries_for_test()[(size_t)idx].editing);

  // The toast module reads the config live (cfg_num on every show), so the
  // change takes effect without a restart.
  REQUIRE(e.config_value_for_test("toast.max_width") == "40");
}

TEST_CASE("Settings search bar filters the list by label or key", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);
  const int all = e.settings_match_count_for_test();
  REQUIRE(all > 50);
  REQUIRE(filtered_contains(e, "tab_size"));

  // Typing lands in the search bar -- there is no other text field in the
  // panel -- and plain letters are search characters, not navigation.
  for (char c : std::string("autosave"))
  {
    e.settings_input_for_test(c);
  }
  REQUIRE(e.settings_query_for_test() == "autosave");
  REQUIRE(e.settings_match_count_for_test() < all);
  // Punctuation and case in the label are ignored: "autosave" finds
  // "Auto save" and auto_save_interval_ms, and nothing else.
  REQUIRE(filtered_contains(e, "auto_save"));
  REQUIRE(filtered_contains(e, "auto_save_interval_ms"));
  REQUIRE_FALSE(filtered_contains(e, "tab_size"));
  for (int i = 0; i < e.settings_match_count_for_test(); i++)
  {
    REQUIRE(e.settings_key_at_for_test(i).find("auto_save") != std::string::npos);
  }
  // The selection clamps into the shorter list.
  REQUIRE(e.settings_selected_pos_for_test() < e.settings_match_count_for_test());

  // Navigation walks the matches only.
  if (e.settings_match_count_for_test() > 1)
  {
    e.settings_input_for_test(1009); // Down
    REQUIRE(e.settings_selected_pos_for_test() == 1);
    REQUIRE(e.settings_selected_key_for_test().find("auto_save") != std::string::npos);
  }

  // Backspace edits the query; Esc clears it and only then closes the menu.
  e.settings_input_for_test(127);
  REQUIRE(e.settings_query_for_test() == "autosav");
  REQUIRE(e.settings_input_for_test(27));
  REQUIRE(e.settings_query_for_test().empty());
  REQUIRE(e.settings_match_count_for_test() == all);
  REQUIRE(e.settings_menu_open_for_test());
  REQUIRE(e.settings_input_for_test(27));
  REQUIRE_FALSE(e.settings_menu_open_for_test());

  // A fresh menu opens unfiltered rather than on the last visit's query.
  e.toggle_settings_menu_for_test();
  REQUIRE(e.settings_query_for_test().empty());
  REQUIRE(e.settings_match_count_for_test() == all);
  e.close_settings_menu_for_test();
}

TEST_CASE("Settings menu steps ints and clamps them to their range", "[jot]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("tab_size", "7");
  open_menu(e);

  const int pos = filtered_pos(e, "tab_size");
  REQUIRE(pos >= 0);
  e.settings_select_for_test(pos);
  REQUIRE(e.settings_selected_key_for_test() == "tab_size");

  // Right/Left move the number by the row's own step (1010 is the right
  // arrow, 1011 the left).
  REQUIRE(e.settings_input_for_test(1010));
  REQUIRE(e.config_int_for_test("tab_size") == 8);
  REQUIRE(e.settings_input_for_test(1011));
  REQUIRE(e.config_int_for_test("tab_size") == 7);

  // The range is the one the rest of the app already clamps to (1..16), so a
  // stepper or a typed value cannot leave the row with an unusable number.
  for (int i = 0; i < 20; i++)
  {
    e.settings_input_for_test(1010);
  }
  REQUIRE(e.config_int_for_test("tab_size") == 16);
  for (int i = 0; i < 20; i++)
  {
    e.settings_input_for_test(1011);
  }
  REQUIRE(e.config_int_for_test("tab_size") == 1);

  // Enter still opens the inline editor, and a typed value is clamped too.
  const int idx = entry_index(e, "tab_size");
  REQUIRE(idx >= 0);
  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].editing);
  for (char c : std::string("99"))
  {
    e.settings_input_for_test(c);
  }
  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.config_int_for_test("tab_size") == 16);
  REQUIRE_FALSE(e.settings_entries_for_test()[(size_t)idx].editing);

  // Nonsense still cancels rather than corrupting the value.
  REQUIRE(e.settings_input_for_test('\n'));
  e.settings_input_for_test('x');
  REQUIRE(e.settings_input_for_test('\n'));
  REQUIRE(e.config_int_for_test("tab_size") == 16);
}

TEST_CASE("Settings menu cycles enum rows and lists their choices", "[jot]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("image_viewer_backend", "auto");
  open_menu(e);

  const int idx = entry_index(e, "image_viewer_backend");
  REQUIRE(idx >= 0);
  const SettingsEntry &entry = e.settings_entries_for_test()[(size_t)idx];
  REQUIRE(entry.type == SettingsEntry::Type::Enum);
  REQUIRE(entry.options.size() == 5);
  REQUIRE(entry.options.front() == "auto");
  REQUIRE(entry.options.back() == "off");

  e.settings_select_for_test(filtered_pos(e, "image_viewer_backend"));
  REQUIRE(e.settings_selected_key_for_test() == "image_viewer_backend");

  // Right/Left walk the choices and wrap around the ends.
  e.settings_input_for_test(1010);
  REQUIRE(e.config_value_for_test("image_viewer_backend") == "kitty");
  e.settings_input_for_test(1011);
  REQUIRE(e.config_value_for_test("image_viewer_backend") == "auto");
  e.settings_input_for_test(1011);
  REQUIRE(e.config_value_for_test("image_viewer_backend") == "off");

  // Enter opens the list of choices (rather than a text editor) on the value
  // currently in force.
  e.settings_input_for_test('\n');
  REQUIRE(e.settings_dropdown_open_for_test());
  REQUIRE(e.settings_dropdown_index_for_test() == 4); // "off"
  // The painted box has to hold every choice: all five fit, so the list is
  // never scrolled behind the cursor.
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 40);
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(e.settings_dropdown_rows_for_test() == 5);

  // Placement rules the picture has to satisfy: the box lives inside the
  // panel and never covers the row whose value it is offering. The row here
  // sits at the bottom of a one-row list, so this is the case that has to open
  // the box above the row rather than over it.
  int panel_x = 0;
  int panel_y = 0;
  int panel_w = 0;
  int panel_h = 0;
  e.settings_panel_rect_for_test(panel_x, panel_y, panel_w, panel_h);
  int box_x = 0;
  int box_y = 0;
  int box_w = 0;
  int box_h = 0;
  REQUIRE(e.settings_dropdown_rect_for_test(box_x, box_y, box_w, box_h));
  int row_y = 0;
  int value_x = 0;
  int step_down_x = 0;
  int step_up_x = 0;
  REQUIRE(e.settings_row_cells_for_test(
      "image_viewer_backend", row_y, value_x, step_down_x, step_up_x));
  REQUIRE(box_x >= panel_x + 1);
  REQUIRE(box_x + box_w <= panel_x + panel_w - 1);
  REQUIRE(box_y >= panel_y + 1);
  REQUIRE(box_y + box_h <= panel_y + panel_h - 1);
  REQUIRE(box_y + box_h <= row_y); // above the row, never over it
  REQUIRE(box_x == value_x - 1);   // aligned with the value column
  e.settings_input_for_test(1008); // Up
  REQUIRE(e.settings_dropdown_index_for_test() == 3);
  e.settings_input_for_test('\n');
  REQUIRE_FALSE(e.settings_dropdown_open_for_test());
  REQUIRE(e.config_value_for_test("image_viewer_backend") == "cell");

  // Esc puts the list away without taking a choice, and the menu stays up.
  e.settings_input_for_test('\n');
  REQUIRE(e.settings_dropdown_open_for_test());
  e.settings_input_for_test(1009); // Down -> "off"
  REQUIRE(e.settings_input_for_test(27));
  REQUIRE_FALSE(e.settings_dropdown_open_for_test());
  REQUIRE(e.config_value_for_test("image_viewer_backend") == "cell");
  REQUIRE(e.settings_menu_open_for_test());

  // While the list is up it owns the keyboard: a stray letter must not edit
  // the row behind it.
  e.settings_input_for_test('\n');
  REQUIRE(e.settings_dropdown_open_for_test());
  e.settings_input_for_test('x');
  REQUIRE(e.settings_query_for_test().empty());
  REQUIRE(e.settings_dropdown_open_for_test());
  REQUIRE(e.settings_input_for_test(27));

  e.config_set_for_test("image_viewer_backend", "auto");
  e.close_settings_menu_for_test();
}

TEST_CASE("Settings steppers are painted where the mouse can click them", "[jot]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("tab_size", "4");
  open_menu(e);
  // A workspace-less test editor otherwise paints the home screen and never
  // reaches the settings panel at all.
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 40);
  e.settings_select_for_test(filtered_pos(e, "tab_size"));
  e.request_redraw_for_test();
  e.render_for_test();

  int row_y = 0;
  int value_x = 0;
  int down_x = 0;
  int up_x = 0;
  REQUIRE(e.settings_row_cells_for_test("tab_size", row_y, value_x, down_x, up_x));
  REQUIRE(down_x > value_x);
  REQUIRE(up_x == down_x + 2);

  // A press on the −/−+ cells steps the number; the drawn glyphs sit exactly
  // there, so the hit test can never disagree with the picture.
  e.mouse_event_for_test(down_x, row_y, /*bstate=*/1);
  REQUIRE(e.config_int_for_test("tab_size") == 3);
  // The steppers travel with the number, so a value that changes width moves
  // them; the next press reads where the *new* frame put them.
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(e.settings_row_cells_for_test("tab_size", row_y, value_x, down_x, up_x));
  e.mouse_event_for_test(up_x, row_y, /*bstate=*/1);
  REQUIRE(e.config_int_for_test("tab_size") == 4);

  // A press on the number itself keeps the old meaning: start typing it.
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(e.settings_row_cells_for_test("tab_size", row_y, value_x, down_x, up_x));
  e.mouse_event_for_test(value_x, row_y, /*bstate=*/1);
  const int idx = entry_index(e, "tab_size");
  REQUIRE(idx >= 0);
  REQUIRE(e.settings_entries_for_test()[(size_t)idx].editing);
  e.settings_input_for_test(27);

  // Only the row under the cursor wears its steppers; every other visible row
  // shows its number alone, so there is nothing to hit on one.
  e.request_redraw_for_test();
  e.render_for_test();
  const std::string selected_key = e.settings_selected_key_for_test();
  int visible = 0;
  for (int i = 0; i < e.settings_match_count_for_test(); i++)
  {
    const std::string key = e.settings_key_at_for_test(i);
    int ry = 0;
    int vx = 0;
    int dwn = 0;
    int up = 0;
    if (!e.settings_row_cells_for_test(key, ry, vx, dwn, up))
      continue; // scrolled out of the window
    visible++;
    if (key == selected_key)
      continue;
    REQUIRE(dwn < 0);
    REQUIRE(up < 0);
  }
  REQUIRE(visible > 1);
}

TEST_CASE("Settings panel files every key under a section", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);

  // The section tables have to keep up with config.cpp: a default that no
  // section claims would fall into the catch-all, which is for keys the user's
  // own config or a plugin registered. An empty catch-all is what says the
  // tables still cover the defaults.
  std::string unclaimed;
  for (const SettingsEntry &entry : e.settings_entries_for_test())
  {
    if (entry.section == "Other")
      unclaimed += entry.key + " ";
  }
  REQUIRE(unclaimed.empty());

  // A key is filed where its subject says. The families are the interesting
  // part: the colorizer's switches have no section named for them, they hang
  // off "colorizer_" -- and `colorizer` itself is not swallowed by that
  // prefix, because a family has to end in a separator.
  REQUIRE(e.settings_section_for_test("tab_size") == "Editor");
  REQUIRE(e.settings_section_for_test("word_wrap") == "Editor");
  REQUIRE(e.settings_section_for_test("color_scheme") == "Appearance");
  REQUIRE(e.settings_section_for_test("tabline_auto_hide") == "Appearance");
  REQUIRE(e.settings_section_for_test("smooth_scroll_easing") == "Appearance");
  REQUIRE(e.settings_section_for_test("rainbow_brackets") == "Appearance");
  REQUIRE(e.settings_section_for_test("colorizer") == "Color preview");
  REQUIRE(e.settings_section_for_test("colorizer_mode") == "Color preview");
  REQUIRE(e.settings_section_for_test("colorizer_hex_alpha") == "Color preview");
  REQUIRE(e.settings_section_for_test("completion_rich_labels") == "Code intelligence");
  REQUIRE(e.settings_section_for_test("lsp_inlay_hints") == "Code intelligence");
  REQUIRE(e.settings_section_for_test("cpp_dim_inactive") == "Code intelligence");
  REQUIRE(e.settings_section_for_test("treesitter_query_paths") == "Code intelligence");
  REQUIRE(e.settings_section_for_test("diagnostics_virtual_text") == "Code intelligence");
  REQUIRE(e.settings_section_for_test("explorer_width") == "Panes & layout");
  REQUIRE(e.settings_section_for_test("show_explorer") == "Panes & layout");
  REQUIRE(e.settings_section_for_test("discord_rpc") == "Discord presence");
  REQUIRE(e.settings_section_for_test("toast.max_width") == "Toasts");
  REQUIRE(e.settings_section_for_test("update.check_on_startup") == "Updates");
}

TEST_CASE("Settings rows read in groups, header first", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);

  const int rows = e.settings_row_count_for_test();
  REQUIRE(rows > e.settings_match_count_for_test()); // there are headers in it
  // The listing opens on the first group, not on a row with no heading over it.
  REQUIRE(e.settings_row_header_for_test(0) == "Editor");

  // Every group is announced exactly once, on the row before its first member,
  // and no two headings sit together -- a heading with nothing under it would
  // be a group the query emptied, and those are not drawn at all.
  std::vector<std::string> seen;
  std::string current;
  for (int row = 0; row < rows; row++)
  {
    const std::string header = e.settings_row_header_for_test(row);
    if (!header.empty())
    {
      seen.push_back(header);
      REQUIRE(header != current);
      REQUIRE(row + 1 < rows);
      REQUIRE(e.settings_row_header_for_test(row + 1).empty());
      current = header;
      continue;
    }
    const std::string key = e.settings_row_key_for_test(row);
    REQUIRE_FALSE(key.empty());
    // A row is never pulled out of its group: the heading above it is the
    // section the key itself says it belongs to.
    REQUIRE(e.settings_section_for_test(key) == current);
  }
  REQUIRE(seen.size() >= 8);
  // The groups read in the order the panel declares them -- a section that
  // moves in the table moves on screen. Groups whose keys only exist once the
  // Lua features have registered theirs (Snippets, the two previews) are not
  // here at all, so what the list has to be is a subsequence of the order.
  const std::string canonical =
      "Editor Appearance Color preview Code intelligence AI assistant Panes & layout "
      "Markdown preview HTML preview Snippets Discord presence Extensions Toasts Updates";
  size_t at = 0;
  for (const std::string &title : seen)
  {
    const size_t found = canonical.find(title, at);
    REQUIRE(found != std::string::npos);
    at = found + title.size();
  }

  // The painted rows and the selectable list are the same list, in the same
  // order: the search bar, the arrows and the scroll window all speak in
  // entries, and the mouse speaks in rows, so the two must not drift.
  std::string from_rows;
  for (int row = 0; row < rows; row++)
  {
    const std::string key = e.settings_row_key_for_test(row);
    if (!key.empty())
      from_rows += key + " ";
  }
  std::string from_list;
  for (int i = 0; i < e.settings_match_count_for_test(); i++)
  {
    from_list += e.settings_key_at_for_test(i) + " ";
  }
  REQUIRE(from_rows == from_list);
}

TEST_CASE("Settings navigation steps over a section header", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);

  // The last row of one group and the first row of the next name the two keys
  // either side of a heading.
  int boundary = -1;
  for (int row = 1; row < e.settings_row_count_for_test(); row++)
  {
    if (!e.settings_row_header_for_test(row).empty() &&
        e.settings_row_header_for_test(row - 1).empty())
    {
      boundary = row;
      break;
    }
  }
  REQUIRE(boundary > 0);
  const std::string before = e.settings_row_key_for_test(boundary - 1);
  const std::string after = e.settings_row_key_for_test(boundary + 1);
  REQUIRE_FALSE(before.empty());
  REQUIRE_FALSE(after.empty());
  REQUIRE(e.settings_section_for_test(before) != e.settings_section_for_test(after));

  // Down from the group's last row lands on the next group's first row: the
  // heading is a row on screen but not a step in the list.
  e.settings_select_for_test(filtered_pos(e, before));
  REQUIRE(e.settings_selected_key_for_test() == before);
  e.settings_input_for_test(1009); // Down
  REQUIRE(e.settings_selected_key_for_test() == after);
  e.settings_input_for_test(1008); // Up, back over the heading
  REQUIRE(e.settings_selected_key_for_test() == before);
}

TEST_CASE("Settings header rows are not clickable", "[jot]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("tab_size", "4");
  open_menu(e);
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 40);
  e.settings_select_for_test(filtered_pos(e, "tab_size"));
  e.request_redraw_for_test();
  e.render_for_test();

  // A heading is painted on the panel's own row grid, so its y follows from
  // where the list starts (the render pass places both). Aim at the one above
  // the selected row's group rather than at any key's cells.
  int panel_x = 0;
  int panel_y = 0;
  int panel_w = 0;
  int panel_h = 0;
  e.settings_panel_rect_for_test(panel_x, panel_y, panel_w, panel_h);
  int row_y = 0;
  int value_x = 0;
  int down_x = 0;
  int up_x = 0;
  REQUIRE(e.settings_row_cells_for_test("tab_size", row_y, value_x, down_x, up_x));
  const int header_y = panel_y + 3; // the first painted row, which is a heading
  REQUIRE(e.settings_row_header_for_test(0) == "Editor");

  const std::string selected_before = e.settings_selected_key_for_test();
  e.mouse_event_for_test(panel_x + 5, header_y, /*bstate=*/1);
  // Swallowed: no editor opens, no value moves, the panel stays up and the
  // selection stays where it was.
  REQUIRE(e.settings_menu_open_for_test());
  REQUIRE(e.settings_selected_key_for_test() == selected_before);
  REQUIRE(e.config_int_for_test("tab_size") == 4);
  for (const SettingsEntry &entry : e.settings_entries_for_test())
  {
    REQUIRE_FALSE(entry.editing);
  }
  REQUIRE_FALSE(e.settings_dropdown_open_for_test());
}

TEST_CASE("The settings panel fits the screen it is on", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);
  e.set_home_menu_visible(false);

  // A 60-cell floor used to win on anything narrower than 72 columns, so the
  // right border, the divider and every value ran off the edge of the
  // terminal. The panel is the screen's to spend, at any size.
  for (const auto [screen_w, screen_h] :
       {std::pair{120, 40}, {80, 24}, {44, 14}, {30, 10}, {20, 8}})
  {
    e.apply_resize_for_test(screen_w, screen_h);
    e.request_redraw_for_test();
    e.render_for_test();
    int panel_x = 0;
    int panel_y = 0;
    int panel_w = 0;
    int panel_h = 0;
    e.settings_panel_rect_for_test(panel_x, panel_y, panel_w, panel_h);
    INFO("screen " << screen_w << "x" << screen_h << ", panel " << panel_w << "x" << panel_h);
    REQUIRE(panel_x >= 0);
    REQUIRE(panel_y >= 0);
    REQUIRE(panel_w >= 8);
    REQUIRE(panel_h >= 3);
    REQUIRE(panel_x + panel_w <= screen_w);
    REQUIRE(panel_y + panel_h <= screen_h);
  }
}

TEST_CASE("The settings wheel stays inside the matches the search bar kept", "[jot]")
{
  Editor &e = probe_editor();
  open_menu(e);
  for (char c : std::string("tabsize"))
  {
    e.settings_input_for_test(c);
  }
  const int matches = e.settings_match_count_for_test();
  REQUIRE(matches > 0);
  REQUIRE(matches < (int)e.settings_entries_for_test().size());

  // A notch walks three rows and stops at the end of what the query kept; it
  // used to clamp against every entry the panel knows, which could park the
  // selection on a row the search bar was hiding.
  for (int i = 0; i < 10; i++)
  {
    e.wheel_event_for_test(60, 20, /*up=*/false, /*down=*/true);
  }
  REQUIRE(e.settings_selected_pos_for_test() == matches - 1);
  for (int i = 0; i < 10; i++)
  {
    e.wheel_event_for_test(60, 20, /*up=*/true, /*down=*/false);
  }
  REQUIRE(e.settings_selected_pos_for_test() == 0);
}
