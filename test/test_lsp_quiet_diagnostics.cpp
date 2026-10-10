// The typing-pause hold on live diagnostics -- the VSCode feel: the squiggle,
// the row band and the inline message land when the typing stops, not under the
// hands on every publish.
//
// The hold is about the *paint*, not the sync. Every edit still reaches the
// server on its short debounce, the slices still take whatever comes back, and
// the merge paints the freshest answer once the pause is over (or at once when
// a save lifts the hold, because Ctrl+S is the user asking for the truth now).
// So a case asserts the buffer's diagnostics -- the list the gutter, the
// squiggles, the band and the message are all drawn from -- and ages the
// keystroke stamp instead of sleeping through the quiet window.
#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_lsp_quiet_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A file on disk the case can open. Names are unique per case: load_file
  // focuses an already-open buffer instead of re-reading it.
  std::string write_source(Editor &e, const std::string &name)
  {
    const std::string path = "/tmp/jot_lsp_quiet_" + name + ".cpp";
    std::ofstream out(path);
    out << "int value = 1;\n";
    out.close();
    e.load_file(path);
    e.set_home_menu_visible(false);
    return path;
  }

  int buffer_index_for(Editor &e, const std::string &path)
  {
    for (int i = 0; i < e.buffer_count_for_test(); i++)
    {
      if (e.buffer_for_test(i).filepath == path)
      {
        return i;
      }
    }
    return -1;
  }
} // namespace

TEST_CASE("LSP diagnostics wait for the typing pause", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  const std::string path = write_source(e, "typing");

  // One keystroke: the stamp the hold measures against, set by the same call
  // every typing path makes on its way to the server.
  e.insert_string_for_test("x");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", path, 0, 1, "the fresh finding");
  e.refresh_lsp_diagnostics_for_test(path);

  // The finding is in the slices, but not in the buffer: nothing paints while
  // the hands are still on the keys.
  REQUIRE(e.lsp_diagnostics_held_for_test(path));
  REQUIRE(e.diagnostics_count_for_test(path) == 0);

  // The pause passes (aged, not slept through), the timer ticks, and the
  // freshest answer paints.
  e.age_lsp_typing_for_test(path, 5000);
  e.paint_held_lsp_diagnostics_for_test();
  REQUIRE_FALSE(e.lsp_diagnostics_held_for_test(path));
  REQUIRE(e.diagnostics_count_for_test(path) == 1);

  // A publish for a file nobody is typing in paints at once: the stamp aged out,
  // and the hold only ever was about the file under the hands.
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", path, 1, 2, "a later finding");
  e.refresh_lsp_diagnostics_for_test(path);
  REQUIRE_FALSE(e.lsp_diagnostics_held_for_test(path));
  REQUIRE(e.diagnostics_count_for_test(path) == 2);
}

TEST_CASE("Saving paints held diagnostics at once", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  const std::string path = write_source(e, "save");
  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);

  e.insert_string_for_test("x");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", path, 0, 1, "the fresh finding");
  e.refresh_lsp_diagnostics_for_test(path);
  REQUIRE(e.lsp_diagnostics_held_for_test(path));
  REQUIRE(e.diagnostics_count_for_test(path) == 0);

  // The save is the user asking for the truth now, so the hold does not make
  // Ctrl+S wait out a typing pause: what was held paints with the save.
  REQUIRE(e.save_buffer_for_test(index, false));
  REQUIRE_FALSE(e.lsp_diagnostics_held_for_test(path));
  REQUIRE(e.diagnostics_count_for_test(path) == 1);

  // The findings the save itself provokes paint too, without a pause in
  // between: the save stamp outranks the keystroke until the next edit.
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", path, 1, 2, "after the save");
  e.refresh_lsp_diagnostics_for_test(path);
  REQUIRE(e.diagnostics_count_for_test(path) == 2);

  // Typing again puts the hold back: the next finding for the edited text waits
  // for the next pause.
  e.insert_string_for_test("y");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", path, 2, 1, "typed again");
  e.refresh_lsp_diagnostics_for_test(path);
  REQUIRE(e.lsp_diagnostics_held_for_test(path));
  REQUIRE(e.diagnostics_count_for_test(path) == 2);
}

TEST_CASE("A quiet time of zero shows findings the moment they arrive", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  const std::string path = write_source(e, "instant");
  e.set_config_for_test("lsp_diagnostics_quiet_ms", "0");
  e.apply_config_live_for_test();

  e.insert_string_for_test("x");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", path, 0, 1, "right away");
  e.refresh_lsp_diagnostics_for_test(path);
  REQUIRE_FALSE(e.lsp_diagnostics_held_for_test(path));
  REQUIRE(e.diagnostics_count_for_test(path) == 1);
}
