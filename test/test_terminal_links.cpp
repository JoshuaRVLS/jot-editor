// Ctrl+click on a link in the integrated terminal (docked panel and floating
// box).
//
// The rows are a shell's own output, but the link rules are the editor's
// (src/base/url_span.h): a URL under the pointer opens in the desktop's
// handler and the click leaves no selection behind, while Ctrl+hover underlines
// exactly the URL the click would open. The launch is real, so these cases put
// a stub `xdg-open` first on PATH and read the URL from the file it records.
#include "core/editor.h"
#include "core/panels.h" // TerminalBox
#include "terminal/integrated.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_terminal_link_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // setenv/unsetenv around a case, restored on scope exit. PATH is the opener
  // lookup itself, so a case must not leak its scratch bin to the others.
  struct ScopedEnv
  {
    std::string key;
    std::string saved;
    bool had = false;

    ScopedEnv(const std::string &k, const std::string &value) : key(k)
    {
      const char *old = std::getenv(k.c_str());
      had = old != nullptr;
      if (had)
      {
        saved = old;
      }
      setenv(k.c_str(), value.c_str(), 1);
    }
    ~ScopedEnv()
    {
      if (had)
      {
        setenv(key.c_str(), saved.c_str(), 1);
      }
      else
      {
        unsetenv(key.c_str());
      }
    }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;
  };

#ifndef _WIN32
  // A scratch opener: an `xdg-open` script first on PATH that appends the URL
  // it was handed to a file. The opener is backgrounded, so the file can lag
  // the click by a moment and the readers below wait for what they expect.
  struct OpenerStub
  {
    fs::path root;
    fs::path bin;
    fs::path record;

    OpenerStub()
    {
      char dir[] = "/tmp/jot_terminal_link_stub_XXXXXX";
      REQUIRE(mkdtemp(dir) != nullptr);
      root = dir;
      bin = root / "bin";
      record = root / "urls.txt";
      fs::create_directories(bin);
      std::ofstream script(bin / "xdg-open");
      script << "#!/bin/sh\nprintf '%s\\n' \"$1\" >> \"$JOT_LINK_RECORD\"\n";
      script.close();
      fs::permissions(bin / "xdg-open",
                      fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                      fs::perm_options::add);
    }

    std::vector<std::string> read() const
    {
      std::vector<std::string> lines;
      std::ifstream in(record);
      std::string line;
      while (std::getline(in, line))
      {
        lines.push_back(line);
      }
      return lines;
    }

    // The URLs recorded so far, once there are at least `count` of them.
    std::vector<std::string> urls(size_t count, int timeout_ms = 2000) const
    {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
      std::vector<std::string> lines;
      while (std::chrono::steady_clock::now() < deadline)
      {
        lines = read();
        if (lines.size() >= count)
        {
          return lines;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      return lines;
    }

    std::string path_for_child() const
    {
      return bin.string() + ":/usr/bin:/bin";
    }
  };
#endif

  const std::string kOutputLine = "see https://example.com/terminal-link now";
  const std::string kUrl = "https://example.com/terminal-link";

  // The docked panel with a shell-less vterm whose first screen row is `line`.
  // The first frame sizes the vterm to the panel (the renderer is what calls
  // resize), so it runs before the output is fed; the second paints it.
  void seed_docked_terminal(Editor &e, const std::string &line)
  {
    e.set_home_menu_visible(false);
    e.set_terminal_state_for_test(true, false, 12);
    e.add_terminal_for_test();
    e.clear_terminal_selection();
    e.reset_mouse_clicks_for_test();
    e.render_for_test();
    REQUIRE(e.terminal_for_test() != nullptr);
    e.terminal_for_test()->feed_output_for_test(line);
    e.render_for_test();
  }

  int url_at(const std::string &line)
  {
    return (int)line.find("https");
  }
} // namespace

#ifndef _WIN32
TEST_CASE("A plain click on a terminal link still starts a selection", "[jot]")
{
  OpenerStub stub;
  ScopedEnv path("PATH", stub.path_for_child());
  ScopedEnv record("JOT_LINK_RECORD", stub.record.string());

  Editor &e = probe_editor();
  seed_docked_terminal(e, kOutputLine);

  const int content_y = e.bottom_panel_content_y_for_test();
  REQUIRE(e.terminal_mouse_for_test(1 + url_at(kOutputLine) + 5, content_y, true, false, false));
  REQUIRE(e.terminal_sel_active_for_test());
  REQUIRE(e.terminal_sel_dragging_for_test());

  // The release copies the selection; nothing was handed to a browser.
  REQUIRE(e.terminal_mouse_for_test(1 + url_at(kOutputLine) + 8, content_y, false, false, true));
  REQUIRE(e.message_for_test().find("Copied") == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  REQUIRE(stub.read().empty());
}

TEST_CASE("Ctrl+click on a terminal link opens it and skips the selection", "[jot]")
{
  OpenerStub stub;
  ScopedEnv path("PATH", stub.path_for_child());
  ScopedEnv record("JOT_LINK_RECORD", stub.record.string());

  Editor &e = probe_editor();
  seed_docked_terminal(e, kOutputLine);

  const int content_y = e.bottom_panel_content_y_for_test();
  REQUIRE(e.terminal_mouse_for_test(1 + url_at(kOutputLine) + 5, content_y, true, false, false,
                                    /*ctrl=*/true));

  const auto opened = stub.urls(1);
  REQUIRE(opened.size() == 1);
  REQUIRE(opened[0] == kUrl);
  REQUIRE(e.message_for_test() == "Opening " + kUrl);
  // The click was about the link, not about a position in the shell's output.
  REQUIRE_FALSE(e.terminal_sel_active_for_test());
  REQUIRE_FALSE(e.terminal_sel_dragging_for_test());

  // The release is not a second open, and it does not start a selection (the
  // docked handler never consumes a release on its own).
  e.terminal_mouse_for_test(1 + url_at(kOutputLine) + 5, content_y, false, false, true, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  REQUIRE(stub.read().size() == 1);
  REQUIRE_FALSE(e.terminal_sel_active_for_test());
}

TEST_CASE("Ctrl+click off a terminal link still selects", "[jot]")
{
  OpenerStub stub;
  ScopedEnv path("PATH", stub.path_for_child());
  ScopedEnv record("JOT_LINK_RECORD", stub.record.string());

  Editor &e = probe_editor();
  seed_docked_terminal(e, kOutputLine);

  const int content_y = e.bottom_panel_content_y_for_test();
  const int word_at = 0; // the line opens with "see"
  REQUIRE(e.terminal_mouse_for_test(1 + word_at, content_y, true, false, false, /*ctrl=*/true));
  REQUIRE(e.terminal_sel_active_for_test());
  REQUIRE(e.terminal_sel_dragging_for_test());
  REQUIRE(e.terminal_sel_anchor_col_for_test() == word_at);

  REQUIRE(e.terminal_mouse_for_test(1 + word_at + 2, content_y, false, false, true, true));
  REQUIRE(e.message_for_test().find("Copied") == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  REQUIRE(stub.read().empty());
}

#endif // !_WIN32

TEST_CASE("Ctrl+hover underlines the terminal link the click would open", "[jot]")
{
  Editor &e = probe_editor();
  seed_docked_terminal(e, kOutputLine);

  const int content_y = e.bottom_panel_content_y_for_test();
  const int at = url_at(kOutputLine);
  e.mouse_event_for_test(1 + at + 5, content_y, /*bstate=*/32, /*ctrl=*/true);

  int row = -1;
  int start = -1;
  int end = -1;
  REQUIRE(e.terminal_link_hover_span_for_test(row, start, end));
  // The fixture's own row 0: the vterm was sized to the panel, so the fed line
  // sits on its first screen row.
  REQUIRE(row == 0);
  REQUIRE(start == at);
  REQUIRE(end == at + (int)kUrl.size());

  // The frame marks exactly those cells of the output row.
  e.render_for_test();
  for (int x = 0; x < e.ui_width_for_test(); x++)
  {
    const int col = x - 1; // the view's own first cell is screen column 1
    const bool want = col >= at && col < at + (int)kUrl.size();
    INFO("screen column " << x);
    REQUIRE((e.ui_for_test()->cell_at(x, content_y)->underline != 0) == want);
  }

  // Moving off the link drops the underline, and the frame follows.
  e.mouse_event_for_test(1, content_y, 32, true);
  REQUIRE_FALSE(e.terminal_link_hover_span_for_test(row, start, end));
  e.render_for_test();
  for (int x = 0; x < e.ui_width_for_test(); x++)
  {
    REQUIRE(e.ui_for_test()->cell_at(x, content_y)->underline == 0);
  }

  // Releasing Ctrl drops it too, from wherever the pointer is.
  e.mouse_event_for_test(1 + at + 5, content_y, 32, true);
  REQUIRE(e.terminal_link_hover_span_for_test(row, start, end));
  e.mouse_event_for_test(1 + at + 5, content_y, 32, false);
  REQUIRE_FALSE(e.terminal_link_hover_span_for_test(row, start, end));
}

#ifndef _WIN32
TEST_CASE("Ctrl+click in the floating box opens without dismissing it", "[jot]")
{
  OpenerStub stub;
  ScopedEnv path("PATH", stub.path_for_child());
  ScopedEnv record("JOT_LINK_RECORD", stub.record.string());

  Editor &e = probe_editor();
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 40);
  e.set_terminal_state_for_test(false, false, 10);
  e.add_floating_terminal_for_test();
  e.clear_terminal_selection();
  e.render_for_test();
  IntegratedTerminal *term = e.floating_terminal_for_test();
  REQUIRE(term != nullptr);
  term->feed_output_for_test(kOutputLine);
  e.render_for_test();

  const TerminalBox box = e.floating_terminal_rect_for_test();
  const int click_x = box.x + 1 + url_at(kOutputLine) + 5;
  const int click_y = box.y + 1;
  REQUIRE(e.floating_terminal_mouse_for_test(click_x, click_y, true, false, false, true));

  const auto opened = stub.urls(1);
  REQUIRE(opened.size() == 1);
  REQUIRE(opened[0] == kUrl);
  REQUIRE(e.message_for_test() == "Opening " + kUrl);
  REQUIRE(e.floating_terminal_visible_for_test());
  REQUIRE_FALSE(e.terminal_sel_active_for_test());

  // A Ctrl+click away from the box still dismisses it.
  REQUIRE(e.floating_terminal_mouse_for_test(1, 1, true, false, false, true));
  REQUIRE_FALSE(e.floating_terminal_visible_for_test());
}

TEST_CASE("Ctrl+click on a terminal link with no opener reports it", "[jot]")
{
  // The editor is built before PATH is emptied: the constructor looks for the
  // tools the rest of the suite expects it to have.
  Editor &e = probe_editor();
  char dir[] = "/tmp/jot_terminal_link_no_opener_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  ScopedEnv path("PATH", dir); // no xdg-open, no gio, no open

  seed_docked_terminal(e, kOutputLine);
  const int content_y = e.bottom_panel_content_y_for_test();
  REQUIRE(e.terminal_mouse_for_test(1 + url_at(kOutputLine) + 5, content_y, true, false, false,
                                    true));
  REQUIRE(e.message_for_test() == "No browser opener found for " + kUrl);
  REQUIRE_FALSE(e.terminal_sel_active_for_test());
}
#endif // !_WIN32
