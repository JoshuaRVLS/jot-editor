// Home screen navigation, end to end through the real paint: the project scan,
// the launch-folder listing, the per-row key hints and the typed filter are
// read off the painted grid, so the model and the Lua kit that draws it are
// pinned together. Opening a row runs the real action, which is why the tests
// assert on where the editor ended up rather than on the model alone.
#include "editor.h"
#include "extras/bible.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_home_open_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void write_file(const std::string &path, const std::string &text)
  {
    std::ofstream out(path);
    out << text;
  }

  // A fresh tree per case, with the editor started inside `work` so the scan
  // reaches the launch folder and its parent only.
  struct HomeTree
  {
    std::string base;
    std::string work;
    fs::path previous_cwd;

    HomeTree()
    {
      std::error_code ec;
      previous_cwd = fs::current_path(ec);
      char tmpl[] = "/tmp/jot_home_tree_XXXXXX";
      REQUIRE(mkdtemp(tmpl) != nullptr);
      base = tmpl;
      work = base + "/work";
      fs::create_directories(work);
      fs::create_directories(base + "/alpha");
      fs::create_directories(base + "/beta");
      write_file(base + "/alpha/package.json", "{\"name\":\"alpha\"}\n");
      write_file(base + "/beta/go.mod", "module beta\n");
      write_file(work + "/notes.txt", "hello\n");
      fs::current_path(work, ec);
      REQUIRE(!ec);
    }

    ~HomeTree()
    {
      std::error_code ec;
      fs::current_path(previous_cwd, ec);
      fs::remove_all(base, ec);
    }

    // A pinned folder is listed without any marker to find it by.
    void unpin_alpha()
    {
      std::error_code ec;
      fs::remove(base + "/alpha/package.json", ec);
    }
  };

  std::string row_text(Editor &e, int y)
  {
    const UI *ui = e.ui_for_test();
    std::string text;
    for (int x = 0; x < e.ui_width_for_test(); ++x)
    {
      const UICell *cell = ui->cell_at(x, y);
      text += cell ? cell->ch : " ";
    }
    return text;
  }

  // The first painted row holding `needle`, with the rows above it as well: a
  // section can list the same name twice (here and in Projects).
  // The last row is the status line, which names the open workspace: a scan
  // that must not see a filtered-out row stops above it.
  int row_containing(Editor &e, const std::string &needle, int after_y = -1)
  {
    for (int y = after_y + 1; y < e.ui_height_for_test() - 1; ++y)
    {
      if (row_text(e, y).find(needle) != std::string::npos)
      {
        return y;
      }
    }
    return -1;
  }

  // The digit the row prints at its right edge -- the key the model hands out
  // in layout order.
  char row_key(Editor &e, int y)
  {
    const UI *ui = e.ui_for_test();
    for (int x = e.ui_width_for_test() - 1; x >= 0; --x)
    {
      const UICell *cell = ui->cell_at(x, y);
      if (cell && cell->ch.size() == 1 && cell->ch[0] >= '1' && cell->ch[0] <= '9')
      {
        return cell->ch[0];
      }
    }
    return 0;
  }

  // The panel's own geometry, as test_home_menu computes it: items start two
  // rows under the header that sits four rows under the top of the panel. The
  // probe grid is 80x24, too short for the big wordmark, so the header is the
  // compact one-line wordmark (the big art is asserted on a taller grid).
  struct HomeProbe
  {
    int content_x = 0;
    int content_w = 0;
    int first_item_y = 0;

    explicit HomeProbe(Editor &e)
    {
      const int screen_w = e.ui_width_for_test();
      const int usable_h = std::max(1, e.ui_height_for_test() - 1);
      const int content_w = std::max(1, std::min(screen_w - 4, 118));
      this->content_w = content_w;
      content_x = std::max(1, (screen_w - content_w) / 2);
      const int content_y = std::max(0, std::min(2, usable_h - 1));
      first_item_y = content_y + 6;
    }
  };

  bool has_path(const Editor &e, const std::string &path)
  {
    for (const std::string &row : e.home_row_paths_for_test())
    {
      if (row == path)
      {
        return true;
      }
    }
    return false;
  }
} // namespace

TEST_CASE("A keystroke that outruns the frame acts on the rows on screen", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree;

  e.home_scan_cache_clear_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();

  // A paste, or a hand faster than the frame, delivers the query and its Enter
  // in one read: no render rebuilds the rows in between, so the handler has to.
  for (char c : std::string("beta"))
  {
    e.home_input_for_test(c);
  }
  e.home_input_for_test('\r');
  e.render_for_test();
  REQUIRE_FALSE(e.home_visible_for_test());
  REQUIRE(e.lua_float_count_for_test("sidebar") == 1);
  REQUIRE(row_containing(e, "go.mod") > 0); // the project the query left, not the first row

  // The other half: a query nothing matches leaves nothing to open, so Enter
  // must open nothing -- not the first row of the list from before the query.
  e.set_home_menu_visible(true);
  e.render_for_test();
  for (char c : std::string("zzz"))
  {
    e.home_input_for_test(c);
  }
  e.home_input_for_test('\r');
  e.render_for_test();
  REQUIRE(e.home_visible_for_test());

  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("Reopening home starts on the lists, not on the last query", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree;

  e.home_scan_cache_clear_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();
  for (char c : std::string("zzz"))
  {
    e.home_input_for_test(c);
  }
  e.render_for_test();
  REQUIRE(e.home_filter_for_test() == "zzz");
  REQUIRE_FALSE(has_path(e, tree.base + "/alpha"));

  // The query is part of the visit: hiding the screen and raising it again
  // (the menu bar, `:home`, the next startup) starts on the lists.
  e.set_home_menu_visible(false);
  e.render_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();
  REQUIRE(e.home_filter_for_test().empty());
  REQUIRE(has_path(e, tree.base + "/alpha"));
  REQUIRE(has_path(e, tree.base + "/beta"));

  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("The home selection band hugs the label instead of the row", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree;

  e.home_scan_cache_clear_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();

  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);
  const HomeProbe probe(e);
  const int y = probe.first_item_y;
  const int band_bg = ui->cell_at(probe.content_x + 1, y)->bg;
  const int plain_bg = ui->cell_at(probe.content_x + 1, y + 1)->bg;
  REQUIRE(band_bg != plain_bg); // the first entry is the selected one

  int first = -1;
  int last = -1;
  for (int x = probe.content_x; x < probe.content_x + 70; ++x)
  {
    const UICell *cell = ui->cell_at(x, y);
    if (cell && cell->bg == band_bg)
    {
      if (first < 0)
      {
        first = x;
      }
      last = x;
    }
  }
  REQUIRE(first == probe.content_x + 1); // on the label, not on the row's edge

  // The band covers the name and stops: nothing behind it, no bar to the edge.
  std::string banded;
  for (int x = first; x <= last; ++x)
  {
    banded += ui->cell_at(x, y)->ch;
  }
  // The first entry is Resume when the file has opened something already, the
  // open prompt otherwise; either way the band is on a label, not on padding.
  REQUIRE((banded.find("Resume") != std::string::npos
           || banded.find("Open Folder / File") != std::string::npos));
  REQUIRE(banded.front() != ' ');
  REQUIRE(banded.back() != ' ');
  const UICell *after = ui->cell_at(last + 1, y);
  REQUIRE(after != nullptr);
  REQUIRE(after->bg != band_bg);
  REQUIRE(last - first < 40); // the row is much wider than its label

  // The key and the dimmed tail no longer sit on the band, so they keep the
  // colours they have on every other row instead of the selection foreground.
  int key_x = -1;
  for (int x = probe.content_x + probe.content_w - 1; x >= probe.content_x; --x)
  {
    const UICell *cell = ui->cell_at(x, y);
    if (cell && !cell->ch.empty() && cell->ch != " ")
    {
      key_x = x;
      break;
    }
  }
  REQUIRE(key_x > last);
  REQUIRE(ui->cell_at(key_x, y)->fg == ui->cell_at(key_x, y + 1)->fg);

  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("Home lists the projects around the launch folder and its own files", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree;

  e.home_scan_cache_clear_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();

  // The scan finds both checkouts above the launch folder, and "Here" lists the
  // one file next to it. No recent list is involved: this is the tree on disk.
  REQUIRE(has_path(e, tree.base + "/alpha"));
  REQUIRE(has_path(e, tree.base + "/beta"));
  REQUIRE(has_path(e, tree.work + "/notes.txt"));

  const std::string projects_row = row_text(e, row_containing(e, "Projects"));
  REQUIRE(projects_row.find("Projects") != std::string::npos);
  REQUIRE(row_containing(e, "notes.txt") > 0);

  // The command rows carry letters and the list rows digits, handed out in
  // layout order so the Nth list row is always 'N'.
  const std::vector<char> keys = e.home_row_keys_for_test();
  std::vector<char> digits;
  for (char key : keys)
  {
    if (key >= '1' && key <= '9')
    {
      digits.push_back(key);
    }
  }
  REQUIRE(digits.size() >= 3);
  for (std::size_t i = 0; i < digits.size(); ++i)
  {
    REQUIRE(digits[i] == (char)('1' + (int)i));
  }

  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("Home prints the key each row opens on", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree;

  e.home_scan_cache_clear_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();

  const int alpha_y = row_containing(e, "alpha");
  REQUIRE(alpha_y > 0);
  const char printed = row_key(e, alpha_y);
  REQUIRE(printed != 0);

  // The digit the row shows is the key the model handed it, so the picture and
  // the shortcut cannot drift apart.
  const std::vector<std::string> paths = e.home_row_paths_for_test();
  const std::vector<char> keys = e.home_row_keys_for_test();
  REQUIRE(paths.size() == keys.size());
  const std::string alpha_path = tree.base + "/alpha";
  std::size_t alpha_index = paths.size();
  for (std::size_t i = 0; i < paths.size(); ++i)
  {
    if (paths[i] == alpha_path)
    {
      alpha_index = i;
    }
  }
  REQUIRE(alpha_index < paths.size());
  REQUIRE(keys[alpha_index] == printed);

  // Pressing it opens that project: the menu closes and the explorer for the
  // new workspace takes the frame.
  e.home_input_for_test(printed);
  e.render_for_test();
  REQUIRE_FALSE(e.home_visible_for_test());
  REQUIRE(e.lua_float_count_for_test("home_screen") == 0);
  REQUIRE(e.lua_float_count_for_test("sidebar") == 1);

  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("Typing on home filters the rows instead of dismissing the screen", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree;

  e.home_scan_cache_clear_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();
  REQUIRE(has_path(e, tree.base + "/alpha"));
  REQUIRE(has_path(e, tree.base + "/beta"));

  // "beta" keeps one project and drops the other, and the screen stays up.
  for (char c : std::string("beta"))
  {
    e.home_input_for_test(c);
  }
  e.render_for_test();
  REQUIRE(e.home_visible_for_test());
  REQUIRE(e.home_filter_for_test() == "beta");
  REQUIRE_FALSE(has_path(e, tree.base + "/alpha"));
  REQUIRE(has_path(e, tree.base + "/beta"));
  REQUIRE_FALSE(has_path(e, tree.work + "/notes.txt"));
  REQUIRE(row_containing(e, "beta") > 0);
  REQUIRE(row_containing(e, "alpha") < 0);

  // The query line says what the rows are filtered by, and the legend changes
  // to the keys that edit it.
  REQUIRE(row_containing(e, "Filter  beta") > 0);

  // The first Esc takes the query back, the screen stays.
  e.home_input_for_test(27);
  e.render_for_test();
  REQUIRE(e.home_visible_for_test());
  REQUIRE(e.home_filter_for_test().empty());
  REQUIRE(has_path(e, tree.base + "/alpha"));

  // Once a query is started every printable key is part of it, so a letter that
  // is also a shortcut -- 't' opens the theme chooser -- types instead of
  // firing, and the screen stays up.
  e.home_input_for_test('b');
  e.render_for_test();
  REQUIRE(e.home_visible_for_test());
  REQUIRE(e.home_filter_for_test() == "b");
  e.home_input_for_test('t');
  e.render_for_test();
  REQUIRE(e.home_visible_for_test());
  REQUIRE(e.home_filter_for_test() == "bt");

  // '/' arms the query for a letter that would otherwise fire, so a shortcut
  // letter can start one.
  e.home_input_for_test(27);
  e.home_input_for_test('/');
  REQUIRE(e.home_filter_for_test().empty());
  e.home_input_for_test('t');
  REQUIRE(e.home_filter_for_test() == "t");

  // Esc (with the armed query cleared first) closes the screen on the second
  // press, and the query goes with it.
  e.home_input_for_test(27);
  e.home_input_for_test(27);
  e.render_for_test();
  REQUIRE_FALSE(e.home_visible_for_test());
  REQUIRE(e.home_filter_for_test().empty());
  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("A pinned folder stays on home after its project marker is gone", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree;

  e.home_scan_cache_clear_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();
  const int alpha_y = row_containing(e, "alpha");
  REQUIRE(alpha_y > 0);

  // Walk the selection down to the project row and pin it with 'f'.
  const std::string alpha_path = tree.base + "/alpha";
  const std::vector<std::string> paths = e.home_row_paths_for_test();
  for (int i = 0; i < 40 && paths[(std::size_t)e.home_selected_for_test()] != alpha_path; ++i)
  {
    e.home_input_for_test('j');
  }
  REQUIRE(paths[(std::size_t)e.home_selected_for_test()] == alpha_path);
  e.home_input_for_test('f');
  e.render_for_test();
  REQUIRE(e.home_visible_for_test());

  // The pin is a star on the row, and the folder is listed above the scan now.
  const int starred_y = row_containing(e, "\u2605");
  REQUIRE(starred_y > 0);
  REQUIRE(row_text(e, starred_y).find("alpha") != std::string::npos);

  // Drop what made alpha look like a project: the pin is the only reason it is
  // still on the screen.
  tree.unpin_alpha();
  e.home_scan_cache_clear_for_test();
  e.render_for_test();
  REQUIRE(has_path(e, tree.base + "/alpha"));
  REQUIRE(row_containing(e, "\u2605") > 0);

  // Unpinning with 'f' again takes it off (the scan has nothing to find).
  const std::vector<std::string> pinned_paths = e.home_row_paths_for_test();
  for (int i = 0; i < 40 && pinned_paths[(std::size_t)e.home_selected_for_test()] != alpha_path;
       ++i)
  {
    e.home_input_for_test('j');
  }
  REQUIRE(pinned_paths[(std::size_t)e.home_selected_for_test()] == alpha_path);
  e.home_input_for_test('f');
  e.home_scan_cache_clear_for_test();
  e.render_for_test();
  REQUIRE_FALSE(has_path(e, tree.base + "/alpha"));

  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("Home centers the verse of the day on the bottom strip", "[jot]")
{
  // Its own config and cache home: the day's chapter is pre-seeded in the cache
  // the Bible reader reads, so the model gets the verse synchronously and the
  // strip lands on the first painted frame.
  char home[] = "/tmp/jot_home_votd_XXXXXX";
  REQUIRE(mkdtemp(home) != nullptr);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);

  const long long day = (long long)(std::time(nullptr) / 86400);
  const Bible::Reference chapter = Bible::chapter_for_day(day);
  const std::string url =
      Bible::chapter_url("https://bible.helloao.org/api", "BSB", chapter.book->id, chapter.chapter);
  const fs::path cache_dir = fs::path(home) / "bible";
  fs::create_directories(cache_dir);
  {
    std::ofstream out(cache_dir / Bible::cache_file_name(url));
    out << "{\"translation\":{\"id\":\"BSB\",\"englishName\":\"Berean Standard Bible\"},"
           "\"book\":{\"id\":\"JHN\",\"commonName\":\"John\"},"
           "\"chapter\":{\"number\":1,\"content\":[{\"type\":\"verse\",\"number\":1,"
           "\"text\":\"For God so loved the world that he gave his one and only Son, that "
           "whoever believes in him shall not perish but have eternal life.\"}]}}";
  }

  Editor e;
  e.apply_resize_for_test(120, 32);
  e.set_home_menu_visible(true);
  e.render_for_test();

  // The verse wraps to two centered lines on the bottom strip, the reference sits
  // under them, and the footer row carries the build date and the credit.
  const int first_row = row_containing(e, "For God so loved");
  REQUIRE(first_row > 0);
  REQUIRE(first_row > e.ui_height_for_test() * 6 / 10);
  const std::string line = row_text(e, first_row);
  const size_t left = line.find_first_not_of(' ');
  const size_t right = line.find_last_not_of(' ');
  REQUIRE(left != std::string::npos);
  REQUIRE(std::abs((int)(left + right + 1) - e.ui_width_for_test()) <= 12);
  REQUIRE(row_containing(e, "eternal life") == first_row + 1);

  Bible::Reference shown = chapter;
  shown.has_verse = true;
  shown.verse_first = Bible::verse_for_day(day, 1);
  shown.verse_last = shown.verse_first;
  REQUIRE(row_containing(e, Bible::reference_label(shown)) == first_row + 2);
  REQUIRE(row_containing(e, "Last updated") > 0);
  REQUIRE(row_containing(e, "Developed by Joshua Ravael") > 0);

  e.set_home_menu_visible(false);
  e.render_for_test();
}
