// Per-pane tab strips (VSCode editor groups): a pane draws its own strip once
// the pane area is split, clicking a tab switches that pane, and a tab can be
// dragged into another pane's strip (a move), onto one of its edges (a split)
// or back into its own list (a reorder).
//
// The workspace strip on row 0 stays for the whole pane area and yields it
// while the groups draw their own lists (so the files are never inked twice);
// these cases pin that handover, the pane-local strip, its row budget, and the
// drag session (src/render/pane_tabs.cpp, src/input/mouse/pane_tabs.cpp,
// src/editor/app/panes_tabs.cpp).
#include "editor.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{
  // The input path's Alt modifier bit (see src/ui/terminal.h).
  const int ALT = 0x40000;

  void seed_config_home()
  {
    char home[] = "/tmp/jot_pane_tabs_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // The same scratch home with a settings.conf already in it, for the cases
  // that pin what the editor reads at construction (`tabline` is only read
  // there).
  void seed_config_home_with(const std::string &settings)
  {
    char home[] = "/tmp/jot_pane_tabs_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
    const std::string root(home);
    std::filesystem::create_directories(root + "/configs");
    std::ofstream(root + "/configs/settings.conf") << settings;
  }

  std::string write_file(const std::string &name, const std::string &text)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_pane_tabs_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + "_" + name;
    std::ofstream out(path);
    out << text;
    out.close();
    return path;
  }

  // An editor sized so the panes have rows, with no winbar row in play: these
  // cases assert the strip's own row budget.
  void prepare(Editor &e)
  {
    e.set_home_menu_visible(false);
    e.set_winbar_mode_for_test(Winbar::WINBAR_MODE_OFF);
    e.apply_resize_for_test(110, 30);
  }

  int buffer_index_of(Editor &e, const std::string &path)
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

  int tab_index_of(Editor &e, int pane, int buffer_id)
  {
    const PaneTabLayout layout = e.pane_tab_layout_for_test(pane);
    for (const PaneTabSegment &tab : layout.segments)
    {
      if (tab.buffer_id == buffer_id)
      {
        return tab.tab_index;
      }
    }
    return -1;
  }

  // A tab's centre cell in screen coordinates (the click that switches it).
  int tab_center_x(Editor &e, int pane, int tab_index)
  {
    const PaneTabLayout layout = e.pane_tab_layout_for_test(pane);
    for (const PaneTabSegment &tab : layout.segments)
    {
      if (tab.tab_index == tab_index)
      {
        return layout.x + (tab.x + tab.end_x) / 2;
      }
    }
    return -1;
  }

  // The strip's tail, past every tab: a drop there appends.
  int strip_tail_x(Editor &e, int pane)
  {
    const PaneTabLayout layout = e.pane_tab_layout_for_test(pane);
    return layout.x + std::max(1, layout.w - 2);
  }
} // namespace

TEST_CASE("A lone pane leaves the tab row to the workspace strip", "[jot][panetabs]")
{
  seed_config_home();
  Editor e;
  prepare(e);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  e.load_file(a);
  e.load_file(b);
  e.render_for_test();

  // Two buffers, one pane: the workspace strip on row 0 already lists both, so
  // the pane must not spend a second row on the same list. Its text starts on
  // its first row as it did before the strip existed.
  REQUIRE(e.panes_for_test().size() == 1);
  REQUIRE_FALSE(e.pane_shows_tab_strip_for_test(0));
  REQUIRE(e.pane_for_test().header_height == 0);
  REQUIRE(e.pane_tab_layout_for_test(0).segments.empty());
  REQUIRE(pane_content_top(e.pane_for_test()) == e.pane_for_test().y);
  REQUIRE(pane_viewport_h(e.pane_for_test()) == e.pane_for_test().h);

  // A split is where the row is earned: the workspace strip acts on the
  // focused pane alone, so each group gets its own list.
  e.split_pane_for_test(true);
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 2);
  REQUIRE(e.pane_shows_tab_strip_for_test(0));
  REQUIRE(e.pane_shows_tab_strip_for_test(1));
  const PaneTabLayout layout = e.pane_tab_layout_for_test(1);
  REQUIRE(layout.segments.size() == 2);
  REQUIRE(layout.segments[0].buffer_id != layout.segments[1].buffer_id);
  REQUIRE(layout.segments[1].active); // the file just opened is in front
  const SplitPane &pane = e.pane_for_test();
  REQUIRE(pane.header_height == 1);
  REQUIRE(pane_viewport_h(pane) == pane.h - 1);
  REQUIRE(pane_content_top(pane) == pane.y + 1);

  // The switch is the `pane_tabs` setting's live face: `off` takes the row away
  // again, and `always` gives it even to a pane with a single buffer.
  e.set_pane_tabs_mode_for_test(PANE_TABS_OFF);
  REQUIRE_FALSE(e.pane_shows_tab_strip_for_test(0));
  REQUIRE(e.pane_for_test().header_height == 0);
  e.set_pane_tabs_mode_for_test(PANE_TABS_ALWAYS);
  REQUIRE(e.pane_shows_tab_strip_for_test(0));
}

TEST_CASE("A split gives every group a strip and the workspace strip yields",
          "[jot][panetabs]")
{
  // The pair of rules that keeps one row of tabs on screen: the workspace strip
  // owns row 0 while the pane area is whole, and steps aside the moment the
  // groups draw their own lists -- drawing both would be the same files inked
  // twice, one row above the other. Every group then draws a strip, a group
  // holding a single tab included, because that strip is where its file is
  // named and where a dragged tab lands to join it.
  seed_config_home();
  Editor e;
  prepare(e);
  const std::string a = write_file("a.txt", "alpha\n");
  e.load_file(a);
  e.render_for_test();

  // Lone pane: the workspace strip is the whole tab UI, and the pane spends no
  // row of its own on the same list.
  REQUIRE(e.panes_for_test().size() == 1);
  REQUIRE(e.topbar_height_for_test() == 1); // the strip owns row 0
  REQUIRE_FALSE(e.pane_shows_tab_strip_for_test(0));
  REQUIRE(e.pane_for_test().header_height == 0);
  REQUIRE(e.pane_for_test().y == 1);

  // Split: the workspace strip gives the row to the groups (the topbar is the
  // strip's height here -- the retired menu bar costs nothing -- so 0 is what
  // "the strip stepped aside" looks like), and both panes start on row 0.
  e.split_pane_for_test(true);
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 2);
  REQUIRE(e.topbar_height_for_test() == 0);
  REQUIRE(e.panes_for_test()[0].y == 0);

  // Both groups pay their strip row -- the second one holds a single tab, and
  // still has to be the place its file is named and a dragged tab is dropped.
  REQUIRE(e.pane_shows_tab_strip_for_test(0));
  REQUIRE(e.pane_shows_tab_strip_for_test(1));
  REQUIRE(e.panes_for_test()[1].tab_buffer_ids.size() == 1);
  REQUIRE(e.pane_tab_layout_for_test(1).segments.size() == 1);
  REQUIRE(e.pane_tab_layout_for_test(1).segments[0].active);
  REQUIRE(e.panes_for_test()[1].header_height == 1);
  REQUIRE(pane_content_top(e.panes_for_test()[1]) == e.panes_for_test()[1].y + 1);
  REQUIRE(pane_viewport_h(e.panes_for_test()[1]) == e.panes_for_test()[1].h - 1);

  // `pane_tabs = off` hands the row back to the workspace strip: the panes draw
  // none of their own, so it is the only list left.
  e.set_pane_tabs_mode_for_test(PANE_TABS_OFF);
  e.render_for_test();
  REQUIRE(e.topbar_height_for_test() == 1);
  REQUIRE_FALSE(e.pane_shows_tab_strip_for_test(0));
  REQUIRE_FALSE(e.pane_shows_tab_strip_for_test(1));
  REQUIRE(e.panes_for_test()[1].header_height == 0);
}

TEST_CASE("In a split the tab keys drive the focused group's own list", "[jot][panetabs]")
{
  // The workspace strip hands row 0 to the groups, but its keys are not part of
  // that handover: Alt+1..9 stays live and addresses the strip the user can
  // see -- the focused group's own list, exactly where a click on it lands.
  seed_config_home();
  Editor e;
  prepare(e);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  const std::string c = write_file("c.txt", "gamma\n");
  e.load_file(a);
  e.load_file(b);
  e.split_pane_for_test(true);
  e.load_file(c); // a third file lands in the focused group only
  e.render_for_test();

  REQUIRE(e.panes_for_test().size() == 2);
  REQUIRE(e.topbar_height_for_test() == 0); // the strip yielded the row
  REQUIRE(e.panes_for_test()[1].tab_buffer_ids.size() == 3);
  const std::vector<int> tabs = e.panes_for_test()[1].tab_buffer_ids;

  // The position is on the focused group's strip: Alt+2 and Alt+0 walk it.
  e.raw_key_for_test('2' | ALT);
  REQUIRE(e.pane_for_test().buffer_id == tabs[1]);
  e.raw_key_for_test('0' | ALT);
  REQUIRE(e.pane_for_test().buffer_id == tabs[2]);

  // Focus the first group (two tabs) and ask for a third position: the key
  // names its own list, so it is inert -- the workspace order has three tabs,
  // and reaching across would have switched this pane instead.
  const int x = tab_center_x(e, 0, 0);
  const int y = e.panes_for_test()[0].y;
  REQUIRE(e.pane_tab_mouse_for_test(x, y, true /*click*/));
  e.pane_tab_mouse_for_test(x, y, false, true /*release*/);
  REQUIRE(e.panes_for_test()[0].tab_buffer_ids.size() == 2);
  const int held = e.panes_for_test()[0].buffer_id;
  e.raw_key_for_test('3' | ALT);
  REQUIRE(e.pane_for_test().buffer_id == held);

  // Alt+R arms jump mode here too, and the group's strip is where the letters
  // appear (the row-0 strip that used to carry them is not on screen).
  e.raw_key_for_test('r' | ALT);
  e.render_for_test();
  const PaneTabLayout layout = e.pane_tab_layout_for_test(0);
  REQUIRE(layout.segments.size() == 2);
  for (const PaneTabSegment &tab : layout.segments)
  {
    REQUIRE(tab.jump_letter != 0);
  }
  const PaneTabSegment &pick = layout.segments[1];
  e.raw_key_for_test(pick.jump_letter);
  REQUIRE(e.pane_for_test().buffer_id == pick.buffer_id);
}

TEST_CASE("With the workspace strip off, the pane's own row carries the tabs", "[jot][panetabs]")
{
  // The other half of the auto rule: `tabline = false` leaves the pane's list
  // as the only place the tabs can live, so even a lone pane gets its row back.
  seed_config_home_with("tabline = false\n");
  Editor e;
  prepare(e);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  e.load_file(a);
  e.load_file(b);
  e.render_for_test();

  REQUIRE(e.panes_for_test().size() == 1);
  REQUIRE(e.pane_for_test().y == 0); // nothing above the pane's own row
  REQUIRE(e.pane_shows_tab_strip_for_test(0));
  REQUIRE(e.pane_for_test().header_height == 1);
  REQUIRE(e.pane_tab_layout_for_test(0).segments.size() == 2);
}

TEST_CASE("Clicking a tab in another pane focuses it and switches its buffer", "[jot][panetabs]")
{
  seed_config_home();
  Editor e;
  prepare(e);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  e.load_file(a);
  e.load_file(b);
  e.split_pane_for_test(true); // side by side; both panes inherit the pair
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 2);
  REQUIRE(e.pane_shows_tab_strip_for_test(1));

  const int first_tab = e.pane_tab_layout_for_test(1).segments.front().buffer_id;
  const int x = tab_center_x(e, 1, 0);
  const int y = e.panes_for_test()[1].y;
  REQUIRE(x > 0);
  REQUIRE(e.pane_tab_mouse_for_test(x, y, true /*click*/));
  e.pane_tab_mouse_for_test(x, y, false, true /*release*/);

  // The click landed on the first tab of the second pane: it owns the focus,
  // the buffer, and its strip marks that tab active.
  REQUIRE(e.pane_for_test().active);
  REQUIRE(e.pane_for_test().buffer_id == first_tab);
  REQUIRE(e.buffer_for_test().filepath == (first_tab == buffer_index_of(e, a) ? a : b));
  REQUIRE(e.pane_tab_layout_for_test(1).segments.front().active);
}

TEST_CASE("Dragging a tab onto another pane's strip moves it", "[jot][panetabs]")
{
  seed_config_home();
  Editor e;
  prepare(e);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  const std::string c = write_file("c.txt", "gamma\n");
  e.load_file(a);
  e.load_file(b);
  e.split_pane_for_test(true);
  // The split copies the pair into the new pane; a third file lands there only,
  // so the two panes now hold different windows on the same buffers.
  e.load_file(c);
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 2);
  const int moved = buffer_index_of(e, a);
  const int from_tab = tab_index_of(e, 0, moved);
  REQUIRE(from_tab >= 0);

  const int from_x = tab_center_x(e, 0, from_tab);
  const int from_y = e.panes_for_test()[0].y;
  const SplitPane &target = e.panes_for_test()[1];
  const int to_x = strip_tail_x(e, 1);
  REQUIRE(e.pane_tab_mouse_for_test(from_x, from_y, true /*click*/));
  REQUIRE(e.pane_tab_drag_active_for_test());

  REQUIRE(e.pane_tab_mouse_for_test(to_x, target.y, false, false, false, true /*motion*/));
  REQUIRE(e.pane_tab_drop_kind_for_test() == (int)PANE_TAB_DROP_INSERT);
  REQUIRE(e.pane_tab_drop_pane_for_test() == 1);
  REQUIRE(e.pane_tab_drop_on_strip_for_test());

  REQUIRE(e.pane_tab_mouse_for_test(to_x, target.y, false, true /*release*/));
  REQUIRE_FALSE(e.pane_tab_drag_active_for_test());

  // It left the source's list, landed at the end of the target's, and the
  // target shows it.
  const std::vector<SplitPane> &panes = e.panes_for_test();
  REQUIRE(std::find(panes[0].tab_buffer_ids.begin(), panes[0].tab_buffer_ids.end(), moved)
          == panes[0].tab_buffer_ids.end());
  REQUIRE(panes[1].tab_buffer_ids.back() == moved);
  REQUIRE(e.pane_for_test().buffer_id == moved);
  REQUIRE(e.buffer_for_test().filepath == a);
}

TEST_CASE("Dropping a tab on a pane's edge splits and plants it there", "[jot][panetabs]")
{
  seed_config_home();
  Editor e;
  prepare(e);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  const std::string c = write_file("c.txt", "gamma\n");
  e.load_file(a);
  e.load_file(b);
  e.split_pane_for_test(true);
  e.load_file(c);
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 2);
  const int moved = buffer_index_of(e, a);
  const int from_tab = tab_index_of(e, 0, moved);
  REQUIRE(from_tab >= 0);

  const int from_x = tab_center_x(e, 0, from_tab);
  const SplitPane &target = e.panes_for_test()[1];
  REQUIRE(e.pane_tab_mouse_for_test(from_x, e.panes_for_test()[0].y, true /*click*/));
  REQUIRE(e.pane_tab_mouse_for_test(
      target.x + target.w - 1, target.y + target.h / 2, false, false, false, true /*motion*/));
  REQUIRE(e.pane_tab_drop_kind_for_test() == (int)PANE_TAB_DROP_EDGE);
  REQUIRE(e.pane_tab_drop_pane_for_test() == 1);
  REQUIRE(e.pane_tab_drop_edge_for_test() == 2); // the right band splits right

  REQUIRE(e.pane_tab_mouse_for_test(
      target.x + target.w - 1, target.y + target.h / 2, false, true /*release*/));
  const std::vector<SplitPane> &panes = e.panes_for_test();
  REQUIRE(panes.size() == 3);
  // The new group sits to the right of the target and holds only the tab it was
  // dropped with; the source lost it.
  REQUIRE(panes[2].x > panes[1].x);
  REQUIRE(panes[2].tab_buffer_ids == std::vector<int>{moved});
  REQUIRE(panes[2].buffer_id == moved);
  REQUIRE(e.pane_for_test().buffer_id == moved);
  REQUIRE(e.buffer_for_test().filepath == a);
  REQUIRE(std::find(panes[0].tab_buffer_ids.begin(), panes[0].tab_buffer_ids.end(), moved)
          == panes[0].tab_buffer_ids.end());
}

TEST_CASE("A pane whose last tab leaves closes, the VSCode merge", "[jot][panetabs]")
{
  seed_config_home();
  Editor e;
  prepare(e);
  // `always` so the one-tab pane still shows a strip to grab (that is the shape
  // the mode is for).
  e.set_pane_tabs_mode_for_test(PANE_TABS_ALWAYS);
  const std::string a = write_file("a.txt", "alpha\n");
  e.load_file(a);
  e.split_pane_for_test(true);
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 2);
  REQUIRE(e.pane_shows_tab_strip_for_test(1));
  REQUIRE(e.panes_for_test()[1].tab_buffer_ids.size() == 1);

  const int moved = e.panes_for_test()[1].tab_buffer_ids.front();
  const int x = tab_center_x(e, 1, 0);
  const SplitPane &target = e.panes_for_test()[0];
  REQUIRE(e.pane_tab_mouse_for_test(x, e.panes_for_test()[1].y, true /*click*/));
  // Onto the first pane's strip (not its body: a body drop near the edge would
  // split instead, which is the case below).
  const int to_x = strip_tail_x(e, 0);
  REQUIRE(e.pane_tab_mouse_for_test(to_x, target.y, false, false, false, true /*motion*/));
  REQUIRE(e.pane_tab_drop_kind_for_test() == (int)PANE_TAB_DROP_INSERT);
  REQUIRE(e.pane_tab_drop_pane_for_test() == 0);
  REQUIRE(e.pane_tab_mouse_for_test(to_x, target.y, false, true /*release*/));

  REQUIRE(e.panes_for_test().size() == 1);
  REQUIRE(e.pane_for_test().buffer_id == moved);
  REQUIRE(e.buffer_for_test().filepath == a);
}

TEST_CASE("A tab's close control closes its buffer", "[jot][panetabs]")
{
  seed_config_home();
  Editor e;
  prepare(e);
  // `always` so the lone pane keeps its strip after the close leaves one tab
  // (the auto rule hands the row back to the workspace strip there).
  e.set_pane_tabs_mode_for_test(PANE_TABS_ALWAYS);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  e.load_file(a);
  e.load_file(b);
  e.render_for_test();

  const PaneTabLayout layout = e.pane_tab_layout_for_test(0);
  const int inactive = buffer_index_of(e, a);
  const PaneTabSegment *victim = nullptr;
  for (const PaneTabSegment &tab : layout.segments)
  {
    if (tab.buffer_id == inactive)
    {
      victim = &tab;
    }
  }
  REQUIRE(victim != nullptr);
  REQUIRE_FALSE(victim->hidden_close);
  const int before = e.buffer_count_for_test();
  REQUIRE(
      e.pane_tab_mouse_for_test(layout.x + victim->close_x, e.pane_for_test().y, true /*click*/));

  REQUIRE(e.buffer_count_for_test() == before - 1);
  REQUIRE(buffer_index_of(e, a) < 0);
  REQUIRE(e.buffer_for_test().filepath == b);
  REQUIRE(e.pane_tab_layout_for_test(0).segments.size() == 1);
}

TEST_CASE("A drop inside its own pane reorders the tab", "[jot][panetabs]")
{
  seed_config_home();
  Editor e;
  prepare(e);
  // Reordering is a pane-local move; `always` gives the lone pane the row the
  // drag needs (auto would leave it to the workspace strip).
  e.set_pane_tabs_mode_for_test(PANE_TABS_ALWAYS);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  const std::string c = write_file("c.txt", "gamma\n");
  e.load_file(a);
  e.load_file(b);
  e.load_file(c);
  e.render_for_test();

  const int moved = buffer_index_of(e, c);
  const int from_tab = tab_index_of(e, 0, moved);
  REQUIRE(from_tab == 2);
  REQUIRE(e.panes_for_test()[0].tab_buffer_ids.front() != moved);
  // The leading edge of the first tab: the drop lands in front of it.
  const PaneTabLayout layout = e.pane_tab_layout_for_test(0);
  const int to_x = layout.x + layout.segments.front().x;
  REQUIRE(e.pane_tab_mouse_for_test(tab_center_x(e, 0, from_tab), e.pane_for_test().y, true));
  REQUIRE(e.pane_tab_mouse_for_test(to_x, e.pane_for_test().y, false, false, false, true));
  REQUIRE(e.pane_tab_mouse_for_test(to_x, e.pane_for_test().y, false, true));

  REQUIRE(e.panes_for_test()[0].tab_buffer_ids.front() == moved);
  REQUIRE(e.panes_for_test()[0].buffer_id == moved);
  REQUIRE(e.buffer_for_test().filepath == c);
}

TEST_CASE("The wheel over a pane's strip scrolls its tab window", "[jot][panetabs]")
{
  seed_config_home();
  Editor e;
  prepare(e);
  // One pane, twelve tabs: `always` is how this shape gets a strip at all.
  e.set_pane_tabs_mode_for_test(PANE_TABS_ALWAYS);
  for (int i = 0; i < 12; i++)
  {
    e.load_file(write_file("f" + std::to_string(i) + ".txt", "line\n"));
  }
  e.render_for_test();
  const PaneTabLayout layout = e.pane_tab_layout_for_test(0);
  REQUIRE(layout.hidden_after > 0);
  const int scroll_before = e.buffer_for_test().scroll_offset;
  REQUIRE(e.pane_tab_wheel_for_test(e.pane_for_test().x + 2, e.pane_for_test().y, false, true));
  REQUIRE(e.pane_tab_scroll_index_for_test(0) == 1);
  REQUIRE(e.buffer_for_test().scroll_offset == scroll_before);
}

TEST_CASE("A strip row on a shared pane border belongs to the strip", "[jot][panetabs]")
{
  // Two stacked panes share one border row, and the lower pane's strip sits
  // exactly on it. The press must reach the tab, not the resize grab (the
  // dispatcher orders the strip handler first for this).
  seed_config_home();
  Editor e;
  prepare(e);
  const std::string a = write_file("a.txt", "alpha\n");
  const std::string b = write_file("b.txt", "beta\n");
  e.load_file(a);
  e.load_file(b);
  e.split_pane_for_test(false); // stacked
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 2);
  const SplitPane &bottom = e.panes_for_test()[1];
  REQUIRE(bottom.y == e.panes_for_test()[0].y + e.panes_for_test()[0].h);

  const int x = tab_center_x(e, 1, 0);
  e.mouse_event_for_test(x, bottom.y, 1 /*left press*/);
  REQUIRE(e.pane_tab_drag_active_for_test());
  REQUIRE(e.buffer_for_test().filepath == a);
  e.mouse_event_for_test(x, bottom.y, 2 /*release*/);
  REQUIRE_FALSE(e.pane_tab_drag_active_for_test());
  REQUIRE(e.pane_for_test().active);
}
