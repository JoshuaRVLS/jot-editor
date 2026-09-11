// Home screen, end to end: the Lua float lifecycle (one float reused across
// frames, so hover repaints stay cheap) and the frame ownership rule (the
// menu owns the frame while it is up, so the explorer/dock floats from the
// last workspace frame are torn down with it).
#include "editor.h"
#include "ui/ui.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_home_test_XXXXXX";
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

  // Mirrors the native home layout for the probe grid: the panel is centered,
  // the section header sits four rows below its top, and items start two rows
  // under the header.
  struct HomeProbe
  {
    int content_x = 0;
    int content_y = 0;
    int first_item_y = 0;

    explicit HomeProbe(const Editor &e)
    {
      const int screen_w = e.ui_width_for_test();
      const int usable_h = std::max(1, e.ui_height_for_test() - 2); // status line
      const int content_w = std::max(1, std::min(screen_w - 4, 118));
      content_x = std::max(1, (screen_w - content_w) / 2);
      content_y = std::max(0, std::min(2, usable_h - 1));
      first_item_y = content_y + 6;
    }
  };
} // namespace

TEST_CASE("Home surface keeps a single float across frames", "[jot]")
{
  Editor &e = probe_editor();
  e.set_home_menu_visible(true);
  e.render_for_test();
  const int after_first = e.lua_float_count_for_test("home_screen");
  INFO("home floats after first frame: " << after_first);
  REQUIRE(after_first == 1);

  // The same float must stay in the GUI's overlay list: UIGui keys its float
  // animations by surface name, and a fresh handle each frame re-ran the
  // entrance animation and left a float behind for render_floats() to repaint.
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);
  const auto home_overlay = [&]() -> const FloatOverlay *
  {
    for (const FloatOverlay &ov : ui->float_overlays)
    {
      if (ov.surface == "home_screen")
      {
        return &ov;
      }
    }
    return nullptr;
  };
  const FloatOverlay *first = home_overlay();
  REQUIRE(first != nullptr);
  const int stable_handle = first->handle;

  for (int i = 0; i < 4; i++)
  {
    // Re-arm the redraw the way a hover does (selection change).
    e.set_home_menu_visible(true);
    e.render_for_test();
  }
  const int after_many = e.lua_float_count_for_test("home_screen");
  INFO("home floats after 5 frames: " << after_many);
  REQUIRE(after_many == 1);
  const FloatOverlay *again = home_overlay();
  REQUIRE(again != nullptr);
  REQUIRE(again->handle == stable_handle);
  int home_overlays = 0;
  for (const FloatOverlay &ov : ui->float_overlays)
  {
    home_overlays += ov.surface == "home_screen" ? 1 : 0;
  }
  REQUIRE(home_overlays == 1);
}

TEST_CASE("Hovering a home row moves the highlight to it", "[jot]")
{
  Editor &e = probe_editor();
  e.set_home_menu_visible(true);
  e.render_for_test();
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);

  const HomeProbe probe(e);
  const int x = probe.content_x + 2;
  const int first_y = probe.first_item_y;
  const int second_y = first_y + 1;
  REQUIRE(ui->cell_at(x, first_y) != nullptr);

  // The first entry is selected after opening; the second row is not.
  const int sel_bg = ui->cell_at(x, first_y)->bg;
  const int plain_bg = ui->cell_at(x, second_y)->bg;
  REQUIRE(sel_bg != plain_bg);

  // Motion (bstate 32) over the second row must select it and clear the first.
  e.mouse_event_for_test(x, second_y, 32);
  e.render_for_test();
  REQUIRE(ui->cell_at(x, second_y)->bg == sel_bg);
  REQUIRE(ui->cell_at(x, first_y)->bg == plain_bg);

  // The same motion again changes nothing and stays cheap (no repaint churn:
  // the surface keeps its float).
  e.mouse_event_for_test(x, second_y, 32);
  e.render_for_test();
  REQUIRE(ui->cell_at(x, second_y)->bg == sel_bg);
  REQUIRE(e.lua_float_count_for_test("home_screen") == 1);
}

TEST_CASE("Home screen owns the frame: the explorer float is torn down", "[jot]")
{
  Editor &e = probe_editor();
  char tmpl[] = "/tmp/jot_home_ws_XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  const std::string root = tmpl;
  fs::create_directory(root + "/src");
  write_file(root + "/src/a.cpp", "int a;");
  write_file(root + "/README.md", "# hi");

  // A workspace frame: the explorer float is up and painting its separator on
  // the only edge it shares with the editor. The screen-edge left column is
  // left unpainted -- borders only go where two regions meet (pane_edges.h).
  e.host().io.open_workspace(root);
  e.render_for_test();
  REQUIRE(e.lua_float_count_for_test("sidebar") == 1);
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);
  REQUIRE(ui->cell_at(0, 1) != nullptr);
  REQUIRE(ui->cell_at(0, 1)->ch != "│");
  int separator_x = -1;
  for (int x = 1; x < 200; ++x)
  {
    const UICell *cell = ui->cell_at(x, 1);
    if (cell && cell->ch == "│")
    {
      separator_x = x;
      break;
    }
  }
  REQUIRE(separator_x > 0);

  // Opening home tears the explorer float down instead of letting it paint
  // over the menu (the duplicated left pane).
  e.set_home_menu_visible(true);
  e.render_for_test();
  REQUIRE(e.lua_float_count_for_test("sidebar") == 0);
  REQUIRE(e.lua_float_count_for_test("home_screen") == 1);
  REQUIRE(ui->cell_at(separator_x, 1)->ch != "│");

  // Closing home brings the explorer back.
  e.set_home_menu_visible(false);
  e.render_for_test();
  REQUIRE(e.lua_float_count_for_test("sidebar") == 1);
  REQUIRE(ui->cell_at(separator_x, 1)->ch == "│");
}
