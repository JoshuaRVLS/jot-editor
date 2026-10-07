// Undo and redo chords (src/input/modes/modeless.cpp).
//
// Ctrl+Z undoes, Ctrl+Y redoes, and Ctrl+Shift+Z has to redo as well. That last
// one is why this file exists: the Ctrl block reads a bare 'z' as undo, so the
// shifted chord can only work if it is claimed before that block is reached.
// The gestures go through the frontend's decode path (raw_key_for_test) rather
// than a hand-built key event, so what is asserted is the chord as the terminal
// delivers it -- and Ctrl+Z and Ctrl+Shift+Z are only told apart at all by
// terminals that report modifiers distinctly (kitty protocol), which is the
// report the shift case is driven with.

#include "editor.h"
#include "editor/keybind_catalog.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_undo_redo_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // The raw modifier bits the backends decode into ctrl/shift (see
  // src/ui/terminal.h).
  constexpr int kCtrl = 0x20000;
  constexpr int kShift = 0x80000;

  // A fresh buffer with the cursor at the start. Setting the content goes
  // through save_state, which drops the redo tail, so no case can inherit
  // history from the one before it.
  void seed(const std::string &text)
  {
    Editor &e = probe_editor();
    e.set_home_menu_visible(false);
    auto &core = e.host().core;
    core.set_buffer_content(text);
    core.clear_extra_carets();
    core.set_cursor(0, 0);
  }

  void type(char c)
  {
    probe_editor().host().core.insert_char_at_carets(c);
  }

  std::string content()
  {
    return probe_editor().host().core.buffer_content();
  }

  // Exactly what a kitty-protocol terminal reports for the chord: 122 = 'z',
  // modifier 5 = Ctrl, 6 = Ctrl+Shift (bitmask + 1).
  int ctrl_chord()
  {
    using jot::keybind_detail::decode_csi_u_key;
    const int raw = decode_csi_u_key("\x1b[122;5u");
    REQUIRE((raw & kCtrl) != 0);
    REQUIRE((raw & kShift) == 0);
    return raw;
  }

  int ctrl_shift_chord()
  {
    using jot::keybind_detail::decode_csi_u_key;
    const int raw = decode_csi_u_key("\x1b[122;6u");
    REQUIRE((raw & kCtrl) != 0);
    REQUIRE((raw & kShift) != 0);
    return raw;
  }
} // namespace

TEST_CASE("Ctrl+Shift+Z redoes what Ctrl+Z undid", "[jot][undo]")
{
  Editor &e = probe_editor();
  seed("alpha");
  type('X');
  type('Y');
  REQUIRE(content() == "XYalpha");

  // Ctrl+Z walks back one edit at a time.
  e.raw_key_for_test(ctrl_chord());
  REQUIRE(content() == "Xalpha");
  e.raw_key_for_test(ctrl_chord());
  REQUIRE(content() == "alpha");

  // Ctrl+Shift+Z goes forward again -- the same step Ctrl+Y would take, not
  // another undo.
  e.raw_key_for_test(ctrl_shift_chord());
  REQUIRE(content() == "Xalpha");

  e.raw_key_for_test(ctrl_shift_chord());
  REQUIRE(content() == "XYalpha");

  e.raw_key_for_test(ctrl_shift_chord());
  REQUIRE(content() == "XYalpha"); // nothing left to redo
}

TEST_CASE("Ctrl+Y still redoes", "[jot][undo]")
{
  Editor &e = probe_editor();
  seed("alpha");
  type('X');
  REQUIRE(content() == "Xalpha");

  e.raw_key_for_test(ctrl_chord());
  REQUIRE(content() == "alpha");

  e.raw_key_for_test('y' | kCtrl);
  REQUIRE(content() == "Xalpha");
}

TEST_CASE("Ctrl+Shift+Z does nothing when the redo tail is empty", "[jot][undo]")
{
  Editor &e = probe_editor();
  seed("alpha");
  type('X'); // a fresh edit, so the redo tail is empty
  REQUIRE(content() == "Xalpha");

  e.raw_key_for_test(ctrl_shift_chord());

  // Neither undone (Ctrl+Shift+Z must not reach the Ctrl block's undo) nor
  // typed as text.
  REQUIRE(content() == "Xalpha");
}
