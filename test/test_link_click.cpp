// Ctrl+click on a link in the text (src/input/mouse/dispatcher.cpp).
//
// A URL under the pointer opens in the desktop's own handler and the caret
// stays put, while everything else still asks about the element under the
// pointer. The launch is real, so these cases put a stub `xdg-open` first on
// PATH and read the URL from the file it records, then put nothing on PATH to
// pin the no-opener report.
#include "core/editor.h"
#include "core/panes.h" // pane_content_top
#include "render/gutter.h"

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
      char cfgdir[] = "/tmp/jot_link_click_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void load_lines(Editor &e, const std::vector<std::string> &lines, const std::string &name)
  {
    const std::string path = "/tmp/jot_link_probe_" + name + ".txt";
    std::ofstream out(path);
    for (const auto &line : lines)
    {
      out << line << "\n";
    }
    out.close();
    e.load_file(path);
  }

  int code_col(Editor &e, int col)
  {
    return e.pane_for_test().x + 1 + gutter::width(e.buffer_for_test().line_count()) + col;
  }

  // The screen row that renders buffer line 0, found by clicking: the pane's
  // chrome (a winbar) is the renderer's business, so the row is discovered
  // rather than assumed. The caret is parked on line 1 first, so "the click
  // left it on line 0" proves the row is line 0's.
  int first_code_row(Editor &e)
  {
    e.scroll_cursor_to_for_test(1, 0);
    const int top = pane_content_top(e.pane_for_test());
    for (int row = top; row < top + 8; row++)
    {
      e.reset_mouse_clicks_for_test();
      e.mouse_event_for_test(code_col(e, 0), row, /*bstate=*/1);
      e.mouse_event_for_test(code_col(e, 0), row, /*bstate=*/2);
      if (e.buffer_for_test().cursor.y == 0)
      {
        return row;
      }
      e.scroll_cursor_to_for_test(1, 0);
    }
    return top;
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
      char dir[] = "/tmp/jot_link_stub_XXXXXX";
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

  // A click at a buffer position through the real mouse path: press and
  // release, one reset click each so the double-click window cannot turn them
  // into a word selection.
  void ctrl_click_at(Editor &e, int col, int row)
  {
    e.reset_mouse_clicks_for_test();
    e.mouse_event_for_test(code_col(e, col), row, /*bstate=*/1, /*ctrl=*/true);
    e.mouse_event_for_test(code_col(e, col), row, /*bstate=*/2, /*ctrl=*/true);
  }
#endif
} // namespace

#ifndef _WIN32
TEST_CASE("Ctrl+click on a link opens it and leaves the caret alone", "[jot]")
{
  OpenerStub stub;
  ScopedEnv path("PATH", stub.path_for_child());
  ScopedEnv record("JOT_LINK_RECORD", stub.record.string());

  Editor &e = probe_editor();
  load_lines(e, {"int alpha = 1;", "// see https://example.com/page. for docs"}, "link_open");
  e.apply_resize_for_test(100, 30);
  e.buffer_for_test().scroll_offset = 0;
  e.scroll_cursor_to_for_test(0, 3);
  e.render_for_test();

  const int row = first_code_row(e) + 1; // line 1, the one with the link
  e.scroll_cursor_to_for_test(0, 3);     // parked where the link click must leave it
  const int link_col = (int)e.buffer_for_test().line(1).find("https") + 5;
  ctrl_click_at(e, link_col, row);

  // The URL, and not the sentence's dot: the tail belongs to the prose.
  const auto opened = stub.urls(1);
  REQUIRE(opened.size() == 1);
  REQUIRE(opened[0] == "https://example.com/page");
  REQUIRE(e.message_for_test() == "Opening https://example.com/page");

  // The caret is where it was, and no drag selection was left behind: the
  // click was about the link, not about a position in the text.
  REQUIRE(e.buffer_for_test().cursor.y == 0);
  REQUIRE(e.buffer_for_test().cursor.x == 3);
  REQUIRE_FALSE(e.mouse_selecting_for_test());

  // The release is not a second open.
  REQUIRE(stub.read().size() == 1);
}

TEST_CASE("A period after a link is prose, not an opener", "[jot]")
{
  OpenerStub stub;
  ScopedEnv path("PATH", stub.path_for_child());
  ScopedEnv record("JOT_LINK_RECORD", stub.record.string());

  Editor &e = probe_editor();
  load_lines(e, {"int alpha = 1;", "// see https://example.com/page."}, "link_prose_tail");
  e.apply_resize_for_test(100, 30);
  e.buffer_for_test().scroll_offset = 0;
  e.scroll_cursor_to_for_test(0, 0);
  e.render_for_test();

  const int row = first_code_row(e) + 1;
  const int dot_col = (int)e.buffer_for_test().line(1).size() - 1;
  ctrl_click_at(e, dot_col, row);

  // The definition path may answer about the run's first name; either way the
  // browser must not open for a punctuation mark.
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  REQUIRE(stub.read().empty());
}

TEST_CASE("Ctrl+click off a link still asks about the symbol", "[jot]")
{
  OpenerStub stub;
  ScopedEnv path("PATH", stub.path_for_child());
  ScopedEnv record("JOT_LINK_RECORD", stub.record.string());

  Editor &e = probe_editor();
  load_lines(e, {"int alpha = 1;", "// see https://example.com/page for docs"}, "link_symbol");
  e.apply_resize_for_test(100, 30);
  e.buffer_for_test().scroll_offset = 0;
  e.scroll_cursor_to_for_test(0, 0);
  e.render_for_test();

  const int row = first_code_row(e);
  const int alpha_col = (int)e.buffer_for_test().line(0).find("alpha");
  ctrl_click_at(e, alpha_col, row);

  // The definition path put the caret on the element it would ask about.
  REQUIRE(e.buffer_for_test().cursor.y == 0);
  REQUIRE(e.buffer_for_test().cursor.x == alpha_col);
  // And nothing was handed to a browser: a name is not a link.
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  REQUIRE(stub.read().empty());
}

TEST_CASE("Ctrl+click on a link with no opener reports it instead", "[jot]")
{
  // The editor is built before PATH is emptied: the constructor looks for the
  // tools the rest of the suite expects it to have.
  Editor &e = probe_editor();
  char dir[] = "/tmp/jot_link_no_opener_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  ScopedEnv path("PATH", dir); // no xdg-open, no gio, no open

  load_lines(e, {"int alpha = 1;", "// see https://example.com/page now"}, "link_no_opener");
  e.apply_resize_for_test(100, 30);
  e.buffer_for_test().scroll_offset = 0;
  e.scroll_cursor_to_for_test(0, 3);
  e.render_for_test();

  const int row = first_code_row(e) + 1;
  e.scroll_cursor_to_for_test(0, 3);
  const int link_col = (int)e.buffer_for_test().line(1).find("https") + 5;
  ctrl_click_at(e, link_col, row);

  REQUIRE(e.message_for_test() == "No browser opener found for https://example.com/page");
  // Still no caret move: a link the machine cannot open is not a symbol.
  REQUIRE(e.buffer_for_test().cursor.y == 0);
  REQUIRE(e.buffer_for_test().cursor.x == 3);
}
#endif // !_WIN32

TEST_CASE("Ctrl+hover underlines the whole link", "[jot]")
{
  Editor &e = probe_editor();
  const std::string line = "// see https://example.com/a?b=1 now";
  load_lines(e, {"int alpha = 1;", line}, "link_underline");
  e.apply_resize_for_test(100, 30);
  e.buffer_for_test().scroll_offset = 0;
  e.scroll_cursor_to_for_test(0, 0);
  e.render_for_test();

  const int row = first_code_row(e) + 1;
  const std::string url = "https://example.com/a?b=1";
  const int url_at = (int)line.find(url);
  // The affordance covers what the click will open, however many cells in the
  // pointer is: the host, the path, the query and the scheme all answer.
  for (int offset : {0, 7, (int)url.size() - 1})
  {
    e.mouse_event_for_test(code_col(e, url_at + offset), row, /*bstate=*/32, /*ctrl=*/true);
    int start = -1;
    int end = -1;
    INFO("offset " << offset);
    REQUIRE(e.ctrl_hover_span_for_test(start, end));
    REQUIRE(start == url_at);
    REQUIRE(end == url_at + (int)url.size());
  }

  // Off the link the affordance follows the token path instead.
  e.mouse_event_for_test(code_col(e, (int)line.find("see")), row, /*bstate=*/32, /*ctrl=*/true);
  int start = -1;
  int end = -1;
  REQUIRE(e.ctrl_hover_span_for_test(start, end));
  REQUIRE(start == (int)line.find("see"));
  REQUIRE(end == (int)line.find("see") + 3);
}
