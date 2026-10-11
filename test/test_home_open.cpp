// Home screen navigation, end to end through the real paint: the two lists, the
// per-row key hints and the typed filter are read off the painted grid, so the
// model and the Lua kit that draws it are pinned together. Opening a row runs the real action, which is why the tests
// assert on where the editor ended up rather than on the model alone.
#include "core/editor.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
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

  // Where the editor keeps what it remembers between sessions.
  fs::path configs()
  {
    return fs::path(getenv("JOT_CONFIG_HOME")) / "configs";
  }

  // A fresh tree per case, with the editor started inside `work`. Nothing on
  // the screen reads the tree any more -- its lists are the recents and the
  // pins -- so the folders and the file are seeded as recents, the way a
  // session that had visited them would have left them. `gamma` carries every
  // marker a scanner could look for and is seeded nowhere: the cases below
  // assert it never reaches the screen.
  struct HomeTree
  {
    std::string base;
    std::string work;
    fs::path previous_cwd;

    explicit HomeTree(Editor &e)
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
      fs::create_directories(base + "/gamma");
      write_file(base + "/alpha/package.json", "{\"name\":\"alpha\"}\n");
      write_file(base + "/beta/go.mod", "module beta\n");
      write_file(base + "/gamma/Cargo.toml", "[package]\nname = \"gamma\"\n");
      write_file(work + "/notes.txt", "hello\n");
      write_file((configs() / "recent_workspaces.txt").string(),
                 base + "/alpha\n" + base + "/beta\n");
      write_file((configs() / "recent_files.txt").string(), work + "/notes.txt\n");
      e.home_reload_recents_for_test();
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
    void strip_gamma_marker()
    {
      std::error_code ec;
      fs::remove(base + "/gamma/Cargo.toml", ec);
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

  // The first painted row holding `needle`, searched from `after_y` down.
  // The last row is the status line, which names the open workspace: a check
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
  HomeTree tree(e);

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
  HomeTree tree(e);

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
  HomeTree tree(e);

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

  // The cells past the band keep the colours they wear on every other row
  // rather than the selection foreground. A row's right edge is its key or --
  // on a row the model gave no key to, the resume entry's path -- its dimmed
  // tail, so the two rows are not necessarily painting the same slot and the
  // cell is bounded to the two colours those slots have everywhere else.
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
  const Theme &th = e.theme_for_test();
  for (const UICell *edge : {ui->cell_at(key_x, y), ui->cell_at(key_x, y + 1)})
  {
    REQUIRE(edge->fg != th.fg_selection);
    REQUIRE((edge->fg == th.fg_keyword || edge->fg == th.fg_comment));
    REQUIRE(edge->bg != band_bg);
  }

  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("Home lists what it remembers, not what it can scan for", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree(e);

  e.set_home_menu_visible(true);
  e.render_for_test();

  // What the editor remembers is listed, and only that: `gamma` sits beside the
  // other two with a project marker of its own, and no scan reaches it.
  REQUIRE(has_path(e, tree.base + "/alpha"));
  REQUIRE(has_path(e, tree.base + "/beta"));
  REQUIRE(has_path(e, tree.work + "/notes.txt"));
  REQUIRE_FALSE(has_path(e, tree.base + "/gamma"));
  REQUIRE(row_containing(e, "gamma") < 0);
  REQUIRE(row_containing(e, "Projects") < 0);

  // The two lists are the sections that paint, each with its own title.
  REQUIRE(row_containing(e, "Recent Folders") > 0);
  REQUIRE(row_containing(e, "Recent Files") > 0);

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
  HomeTree tree(e);

  e.set_home_menu_visible(true);
  e.render_for_test();

  // The folder list, not the whole screen: the first row's Resume entry names a
  // path of its own, and a digit inside it is not this row's key.
  const int list_y = row_containing(e, "Recent Folders");
  REQUIRE(list_y > 0);
  const int alpha_y = row_containing(e, "alpha", list_y);
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
  HomeTree tree(e);

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

TEST_CASE("A pinned folder is listed on home, and unpinning takes it off", "[jot]")
{
  Editor &e = probe_editor();
  HomeTree tree(e);

  // `gamma` is nothing to the editor -- no recent entry, and the marker that
  // would make a scan call it a project is gone -- so the pin is the whole
  // reason it can appear.
  tree.strip_gamma_marker();
  const fs::path pins = configs() / "home_pins.txt";
  {
    std::ofstream out(pins);
    out << tree.base + "/gamma\n";
  }
  e.home_reload_pins_for_test();
  e.set_home_menu_visible(true);
  e.render_for_test();

  const std::string gamma_path = tree.base + "/gamma";
  REQUIRE(has_path(e, gamma_path));
  const int starred_y = row_containing(e, "\u2605");
  REQUIRE(starred_y > 0);
  REQUIRE(row_text(e, starred_y).find("gamma") != std::string::npos);

  // Unpinning from the screen ('f' on the row) takes it off again: nothing else
  // was listing it.
  const std::vector<std::string> paths = e.home_row_paths_for_test();
  for (int i = 0; i < 40 && paths[(std::size_t)e.home_selected_for_test()] != gamma_path; ++i)
  {
    e.home_input_for_test('j');
  }
  REQUIRE(paths[(std::size_t)e.home_selected_for_test()] == gamma_path);
  e.home_input_for_test('f');
  e.render_for_test();
  REQUIRE_FALSE(has_path(e, gamma_path));
  REQUIRE(row_containing(e, "\u2605") < 0);

  // Leave the shared config home the way the other cases expect to find it.
  {
    std::ofstream out(pins);
  }
  e.home_reload_pins_for_test();
  e.set_home_menu_visible(false);
  e.render_for_test();
}

TEST_CASE("Home paints nothing below the lists", "[jot]")
{
  // The panel used to end in a strip: the verse of the day centered over its
  // reference, then a footer with the build date and the credit line. All of it
  // is gone -- the rows under the header belong to the two lists -- so a screen
  // that paints a footer again fails here.
  Editor &e = probe_editor();
  e.apply_resize_for_test(120, 32);
  e.set_home_menu_visible(true);
  e.render_for_test();

  REQUIRE(row_containing(e, "Recent Files") > 0);
  REQUIRE(row_containing(e, "Last updated") < 0);
  REQUIRE(row_containing(e, "Developed by") < 0);

  // The last row the panel owns is empty: the lists stop above it and nothing
  // is centered under them.
  const std::string last = row_text(e, e.ui_height_for_test() - 2);
  REQUIRE(last.find_first_not_of(' ') == std::string::npos);

  e.set_home_menu_visible(false);
  e.render_for_test();
}
