// Undo after a command that changed nothing (Editor::undo in
// src/editor/app/undo.cpp).
//
// Every command takes its snapshot before it runs, because save_state cannot
// know the outcome: a command that ends up changing nothing - "Format Document"
// on a document with no tabs, :trim on clean lines, :uniquelines with no
// duplicates, :unsurround with nothing around the caret - still pushed a state
// that describes the buffer exactly as it already is. Undo applied that state,
// which changed nothing at all: the keypress looked dead. undo() now drops
// those states, so the press reaches the edit the user meant.
//
// The cases pin both halves: the no-op state is skipped, and a state that
// restores different text (or only a different view) is still a step.

#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_undo_noop_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // A fresh buffer with the caret at the start: setting the content goes
  // through save_state, so no case inherits history from the previous one.
  void seed(const std::string &text)
  {
    Editor &e = probe_editor();
    e.set_home_menu_visible(false);
    auto &core = e.host().core;
    core.set_buffer_content(text);
    core.clear_extra_carets();
    core.set_cursor(0, 0);
  }

  std::string content()
  {
    return probe_editor().host().core.buffer_content();
  }

  void type(char c)
  {
    probe_editor().host().core.insert_char_at_carets(c);
  }
} // namespace

TEST_CASE("Undo: a format that changes nothing does not consume the step", "[jot][undo]")
{
  Editor &e = probe_editor();
  seed("alpha\nbeta\n");
  type('x');
  REQUIRE(content() == "xalpha\nbeta\n");

  // No tab to expand anywhere, so this is not an edit.
  e.format_document_for_test();
  REQUIRE(content() == "xalpha\nbeta\n");

  e.host().core.undo();
  REQUIRE(content() == "alpha\nbeta\n");
}

TEST_CASE("Undo: a format that does change the text is still one step", "[jot][undo]")
{
  Editor &e = probe_editor();
  seed("a\tb\n");
  type('x');
  REQUIRE(content() == "xa\tb\n");

  e.format_document_for_test();
  REQUIRE(content() != "xa\tb\n"); // the tab became spaces

  // One press lands on the text as it was before the format, not before the
  // typed character: the format is a real edit and keeps its step.
  e.host().core.undo();
  REQUIRE(content() == "xa\tb\n");

  e.host().core.undo();
  REQUIRE(content() == "a\tb\n");
}

TEST_CASE("Undo: skipping a no-op state still redoes into the edit it hid", "[jot][undo]")
{
  Editor &e = probe_editor();
  seed("alpha\n");
  type('x');
  REQUIRE(content() == "xalpha\n");

  e.format_document_for_test(); // no-op, dropped by undo
  e.host().core.undo();
  REQUIRE(content() == "alpha\n");

  // The redo step is the state undo captured before it applied the real one,
  // so it restores the typed character and nothing of the dropped state.
  e.host().core.redo();
  REQUIRE(content() == "xalpha\n");
}
