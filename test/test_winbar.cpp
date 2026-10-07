// The breadcrumb winbar (src/features/winbar.*, src/render/winbar.cpp).
//
// Two halves. The model cases are pure: a chain is the workspace root, the
// folders down to the file, the file, and then the symbol ancestors of the
// cursor line, with the nesting recovered from the symbols' own columns (the
// document-symbol index is flat). The editor cases pin what a pane actually
// pays and paints -- the row comes out of the pane's own height, the file's
// first line starts below it -- and that a crumb press opens a menu whose rows
// land where the native geometry says they do.
#include "editor.h"
#include "editor/model/panes.h"
#include "render/winbar_model.h"
#include "syntax/index.h"
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
  void seed_config_home()
  {
    char home[] = "/tmp/jot_winbar_test_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A throwaway workspace whose *name* is known: the chain shows the root's own
  // folder name, so it cannot be random.
  std::string write_workspace_root()
  {
    char parent[] = "/tmp/jot_winbar_parent_XXXXXX";
    REQUIRE(mkdtemp(parent) != nullptr);
    const fs::path root = fs::path(parent) / "jot_winbar_ws";
    const fs::path dir = root / "src" / "render";
    fs::create_directories(dir);
    std::ofstream out(dir / "tabs.cpp");
    out << "// strip\n"        // 0
           "class Editor\n"    // 1
           "{\n"               // 2
           "  void render_tabline()\n" // 3
           "  {\n"             // 4
           "    int alpha = 1;\n" // 5
           "  }\n"             // 6
           "};\n"              // 7
           "int free_function()\n" // 8
           "{\n"               // 9
           "  return 2;\n"     // 10
           "}\n";              // 11
    out.close();
    std::ofstream other(dir / "buffer.cpp");
    other << "int other()\n{\n  return 0;\n}\n";
    other.close();
    // One file next to `render/`, so the `src` crumb's menu holds a folder row
    // *and* a file row: the cascade's follow-the-selection rules need a level
    // where both kinds of row exist.
    std::ofstream scratch(root / "src" / "scratch.cpp");
    scratch << "int scratch()\n{\n  return 1;\n}\n";
    scratch.close();
    return root.string();
  }

  std::vector<std::string> workspace_source_lines()
  {
    return {"// strip",
            "class Editor",
            "{",
            "  void render_tabline()",
            "  {",
            "    int alpha = 1;",
            "  }",
            "};",
            "int free_function()",
            "{",
            "  return 2;",
            "}"};
  }

  SymbolMatch symbol(const std::string &name, const std::string &kind, int line, int column)
  {
    SymbolMatch match;
    match.name = name;
    match.kind = kind;
    match.line = line;
    match.column = column;
    return match;
  }

  std::vector<std::string> labels(const std::vector<Winbar::Crumb> &crumbs)
  {
    std::vector<std::string> out;
    for (const Winbar::Crumb &crumb : crumbs)
    {
      out.push_back(crumb.label);
    }
    return out;
  }

  std::vector<std::string> entry_labels(const std::vector<Winbar::Entry> &entries)
  {
    std::vector<std::string> out;
    for (const Winbar::Entry &entry : entries)
    {
      out.push_back(entry.label);
    }
    return out;
  }

  std::vector<std::string> expect(std::initializer_list<const char *> list)
  {
    return std::vector<std::string>(list.begin(), list.end());
  }

  // The whole grid row as text (the recording cell grid), so a case can check
  // what a row shows instead of trusting a coordinate.
  std::string row_text(Editor &e, int y)
  {
    UI *ui = e.ui_for_test();
    std::string row;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      row += cell ? cell->ch : " ";
    }
    return row;
  }

  int row_of_text(Editor &e, const std::string &needle)
  {
    for (int y = 0; y < e.ui_height_for_test(); y++)
    {
      if (row_text(e, y).find(needle) != std::string::npos)
      {
        return y;
      }
    }
    return -1;
  }

  // One pane's slice of a grid row: what *that* pane painted there, not what
  // any pane painted on the same screen row.
  std::string pane_row_text(Editor &e, const SplitPane &pane, int y)
  {
    UI *ui = e.ui_for_test();
    std::string row;
    for (int x = pane.x; x < pane.x + std::max(1, pane.w); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      row += cell ? cell->ch : " ";
    }
    return row;
  }
} // namespace

TEST_CASE("A chain is the folders down to the file, then the cursor's symbols", "[jot][winbar]")
{
  const std::string root = write_workspace_root();
  const std::string file = (fs::path(root) / "src" / "render" / "tabs.cpp").string();
  const std::vector<SymbolMatch> symbols =
      SymbolIndex::extract_document_symbols(workspace_source_lines(), "tabs.cpp");
  // The C++ patterns: `class Editor` is a type, the two functions are functions.
  REQUIRE(symbols.size() == 3);
  REQUIRE(symbols[0].name == "Editor");
  REQUIRE(symbols[0].kind == "type");
  REQUIRE(symbols[1].name == "render_tabline");
  REQUIRE(symbols[2].name == "free_function");

  // Inside the class's method: the chain is root / src / render / tabs.cpp /
  // Editor / render_tabline, and the innermost crumb is the current one.
  const std::vector<Winbar::Crumb> inside = Winbar::build(file, root, symbols, 5);
  REQUIRE(labels(inside) == expect({"jot_winbar_ws", "src", "render", "tabs.cpp", "Editor",
                                    "render_tabline"}));
  REQUIRE(inside.back().current);
  REQUIRE_FALSE(inside.front().current);
  REQUIRE(inside.back().kind == "symbol");
  REQUIRE(inside.back().symbol_kind == "function");
  REQUIRE(inside[4].symbol_kind == "type");

  // At the free function there is no class crumb: the chain follows the scope
  // the line is actually in, not every symbol above it.
  const std::vector<Winbar::Crumb> free_fn = Winbar::build(file, root, symbols, 9);
  REQUIRE(labels(free_fn)
          == expect({"jot_winbar_ws", "src", "render", "tabs.cpp", "free_function"}));

  // Above every symbol (the file's first line) the chain stops at the file.
  const std::vector<Winbar::Crumb> header = Winbar::build(file, root, symbols, 0);
  REQUIRE(labels(header) == expect({"jot_winbar_ws", "src", "render", "tabs.cpp"}));
  REQUIRE(header.back().kind == "file");
}

TEST_CASE("A symbol's parent is the innermost symbol that starts further left", "[jot][winbar]")
{
  // Columns carry the nesting: a flat index plus the names' indents is enough
  // to rebuild the tree the outline shows.
  const std::vector<SymbolMatch> symbols = {
      symbol("Outer", "type", 0, 0),
      symbol("first", "function", 2, 2),
      symbol("second", "function", 4, 2),
      symbol("deep", "variable", 5, 4),
      symbol("Solo", "function", 9, 0),
  };
  const std::vector<int> parents = Winbar::symbol_parents(symbols);
  REQUIRE(parents[0] == -1);
  REQUIRE(parents[1] == 0);
  REQUIRE(parents[2] == 0);
  REQUIRE(parents[3] == 2);
  REQUIRE(parents[4] == -1);

  // The chain at `deep` is Outer -> second -> deep, and the siblings of `first`
  // are the other methods of Outer.
  REQUIRE(Winbar::symbol_chain(symbols, 5) == std::vector<int>({0, 2, 3}));
  REQUIRE(Winbar::symbol_chain(symbols, 2) == std::vector<int>({0, 1}));
  REQUIRE(Winbar::symbol_siblings(symbols, parents, 1) == std::vector<int>({1, 2}));
  // `Solo` shares the top level with `Outer`.
  REQUIRE(Winbar::symbol_siblings(symbols, parents, 4) == std::vector<int>({0, 4}));
}

TEST_CASE("A file crumb's menu is its folder, a folder crumb's its children", "[jot][winbar]")
{
  const std::string root = write_workspace_root();
  const std::string file = (fs::path(root) / "src" / "render" / "tabs.cpp").string();
  const std::vector<Winbar::Crumb> chain = Winbar::build(file, root, {}, 0);
  REQUIRE(chain.size() == 4);
  REQUIRE(labels(chain) == expect({"jot_winbar_ws", "src", "render", "tabs.cpp"}));

  // The file crumb offers its siblings, with the file the chain is on marked
  // current.
  const std::vector<Winbar::Entry> siblings = Winbar::menu_entries(chain, 3, {});
  REQUIRE(entry_labels(siblings) == expect({"buffer.cpp", "tabs.cpp"}));
  REQUIRE_FALSE(siblings[0].is_dir);
  REQUIRE_FALSE(siblings[0].current);
  REQUIRE(siblings[1].current);
  REQUIRE_FALSE(siblings[1].is_dir);

  // The `render` crumb lists its own children; the `src` crumb what is inside
  // src, folders first (the folder row is what opens a level of its own).
  REQUIRE(entry_labels(Winbar::menu_entries(chain, 2, {})) == expect({"buffer.cpp", "tabs.cpp"}));
  const std::vector<Winbar::Entry> src_dir = Winbar::menu_entries(chain, 1, {});
  REQUIRE(entry_labels(src_dir) == expect({"render", "scratch.cpp"}));
  REQUIRE(src_dir[0].is_dir);
  REQUIRE_FALSE(src_dir[1].is_dir);

  // The root crumb lists the root's own children, never its parents.
  REQUIRE(entry_labels(Winbar::menu_entries(chain, 0, {})) == expect({"src"}));

  // An index outside the chain offers nothing rather than guessing.
  REQUIRE(Winbar::menu_entries(chain, 9, {}).empty());
}

TEST_CASE("A file outside the workspace keeps its own folder, not a fake chain", "[jot][winbar]")
{
  const std::string root = write_workspace_root();
  const std::string outside = "/tmp/jot_winbar_elsewhere/other_file.py";
  const std::vector<Winbar::Crumb> chain = Winbar::build(outside, root, {}, 0);
  // The root crumb never appears for a file it does not contain.
  REQUIRE(labels(chain) == expect({"jot_winbar_elsewhere", "other_file.py"}));

  // No workspace at all behaves the same way (the file's own folder).
  REQUIRE(labels(Winbar::build(outside, "", {}, 0))
          == expect({"jot_winbar_elsewhere", "other_file.py"}));

  // And a path with no folder has no crumbs beyond the file itself.
  REQUIRE(labels(Winbar::build("", root, {}, 0)).empty());
}

TEST_CASE("Symbol crumbs offer their siblings and jump targets", "[jot][winbar]")
{
  const std::vector<SymbolMatch> symbols = {
      symbol("Outer", "type", 0, 0),
      symbol("first", "function", 2, 2),
      symbol("deep", "variable", 3, 4),
      symbol("second", "function", 6, 2),
  };
  const std::vector<Winbar::Crumb> chain = Winbar::build("/w/one.cpp", "/w", symbols, 3);
  REQUIRE(labels(chain) == expect({"w", "one.cpp", "Outer", "first", "deep"}));

  // `deep` is alone in its scope; the methods share the class's.
  const std::vector<Winbar::Entry> deep_siblings = Winbar::menu_entries(chain, 4, symbols);
  REQUIRE(entry_labels(deep_siblings) == expect({"deep"}));
  REQUIRE(deep_siblings[0].current);
  REQUIRE(deep_siblings[0].line == 3);
  REQUIRE(deep_siblings[0].col == 4);

  const std::vector<Winbar::Entry> method_siblings = Winbar::menu_entries(chain, 3, symbols);
  REQUIRE(entry_labels(method_siblings) == expect({"first", "second"}));
  REQUIRE(method_siblings[0].current);
  REQUIRE(method_siblings[0].line == 2);
  REQUIRE(method_siblings[1].line == 6);
  REQUIRE_FALSE(method_siblings[1].current);

  // The class crumb lists the classes at its own level -- here, itself.
  const std::vector<Winbar::Entry> class_siblings = Winbar::menu_entries(chain, 2, symbols);
  REQUIRE(entry_labels(class_siblings) == expect({"Outer"}));
  REQUIRE(class_siblings[0].line == 0);
}

TEST_CASE("Symbol icons and colors are stable per kind", "[jot][winbar]")
{
  const std::string function_icon = Winbar::symbol_icon("function");
  REQUIRE_FALSE(function_icon.empty());
  REQUIRE(Winbar::symbol_icon("method") == function_icon);
  REQUIRE(Winbar::symbol_icon("class") != function_icon);
  REQUIRE(Winbar::symbol_icon("Struct") == Winbar::symbol_icon("struct"));

  Theme theme;
  REQUIRE(Winbar::symbol_color(theme, "function") == theme.fg_function);
  REQUIRE(Winbar::symbol_color(theme, "class") == theme.fg_type);
  REQUIRE(Winbar::symbol_color(theme, "namespace") == theme.fg_namespace);
  REQUIRE(Winbar::symbol_color(theme, "variable") == theme.fg_variable);
  REQUIRE(Winbar::symbol_color(theme, "constant") == theme.fg_constant);
}

TEST_CASE("A code pane pays a breadcrumb row and paints the chain on it", "[jot][winbar]")
{
  seed_config_home();
  const std::string root = write_workspace_root();
  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root, true);
  e.load_file((fs::path(root) / "src" / "render" / "tabs.cpp").string());
  e.render_for_test();

  REQUIRE(e.pane_has_winbar_for_test());
  REQUIRE(e.winbar_height_for_test() == 1);
  const SplitPane &pane = e.pane_for_test();
  REQUIRE(pane.header_height == 1);
  REQUIRE(pane_viewport_h(pane) == pane.h - 1);

  // The chain is painted on the pane's own first row, with its separator, and
  // the file's text starts on the row below it.
  const Winbar::WinbarLayout layout = e.winbar_layout_for_test();
  REQUIRE(layout.y == pane.y);
  REQUIRE(layout.crumbs.size() == 4);
  REQUIRE(layout.segments.size() == 4);
  const std::string winbar_row = row_text(e, pane.y);
  INFO("row " << pane.y << ": [" << winbar_row << "]");
  REQUIRE(winbar_row.find("tabs.cpp") != std::string::npos);
  REQUIRE(winbar_row.find("\u203A") != std::string::npos); // the crumb separator
  REQUIRE(winbar_row.find("render") != std::string::npos);
  REQUIRE(row_of_text(e, "// strip") == pane_content_top(pane));
}

TEST_CASE("Every pane of a split paints its own breadcrumb row", "[jot][winbar]")
{
  // The rows are one surface for the whole split (render/winbar.cpp hands the
  // Lua painter every pane's row in one payload, frame.cpp emits it once).
  // Keyed on a single row it only ever painted the pane that emitted last, and
  // the earlier panes still paid for a breadcrumb row nothing drew into.
  seed_config_home();
  const std::string root = write_workspace_root();
  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root, true);
  e.load_file((fs::path(root) / "src" / "render" / "tabs.cpp").string());
  e.split_pane_for_test(false); // stacked: the layout the split chord makes
  e.render_for_test();

  const std::vector<SplitPane> &panes = e.panes_for_test();
  REQUIRE(panes.size() == 2);
  REQUIRE(panes[0].y != panes[1].y);
  for (const SplitPane &pane : panes)
  {
    REQUIRE(pane.header_height == 1);
    REQUIRE(pane_viewport_h(pane) == pane.h - 1);
    // The chain on this pane's own row, and the file's first line below it.
    const std::string crumb_row = pane_row_text(e, pane, pane.y);
    INFO("pane at y " << pane.y << ": [" << crumb_row << "]");
    REQUIRE(crumb_row.find("tabs.cpp") != std::string::npos);
    REQUIRE(crumb_row.find("\u203A") != std::string::npos);
    REQUIRE(pane_row_text(e, pane, pane_content_top(pane)).find("// strip")
            != std::string::npos);
  }
}

TEST_CASE("A crumb press opens its menu where the layout says, and a row picks it",
          "[jot][winbar]")
{
  seed_config_home();
  const std::string root = write_workspace_root();
  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root, true);
  e.load_file((fs::path(root) / "src" / "render" / "tabs.cpp").string());
  e.render_for_test();

  const Winbar::WinbarLayout layout = e.winbar_layout_for_test();
  REQUIRE(layout.segments.size() == 4);
  // The file crumb is the last one: press it.
  const Winbar::WinbarSegment &file_segment = layout.segments.back();
  REQUIRE(e.winbar_press_for_test(file_segment.x, layout.y));
  REQUIRE(e.winbar_menu_open_for_test());
  REQUIRE(e.winbar_menu_title_for_test() == "tabs.cpp");
  REQUIRE(e.winbar_menu_labels_for_test() == expect({"buffer.cpp", "tabs.cpp"}));
  // The menu hangs directly under the row, at the crumb's own column, and opens
  // on the row the chain is already on (tabs.cpp, the second entry).
  REQUIRE(e.winbar_menu_y_for_test() == layout.y + 1);
  REQUIRE(e.winbar_menu_x_for_test() == file_segment.x);
  REQUIRE(e.winbar_menu_h_for_test() == 4);
  REQUIRE(e.winbar_menu_selected_for_test() == 1);

  // A motion over the first content row selects it; the row above the border
  // selects nothing (and a row below the last entry is not a row at all).
  const int menu_x = e.winbar_menu_x_for_test() + 2;
  REQUIRE(e.winbar_move_for_test(menu_x, e.winbar_menu_y_for_test() + 1));
  REQUIRE(e.winbar_menu_selected_for_test() == 0);
  REQUIRE(e.winbar_move_for_test(menu_x, e.winbar_menu_y_for_test() + 2));
  REQUIRE(e.winbar_menu_selected_for_test() == 1);
  // Esc puts it away (the same key path the dispatcher uses).
  REQUIRE(e.winbar_menu_key_for_test(27));
  REQUIRE_FALSE(e.winbar_menu_open_for_test());

  // A press on the row but not on a crumb (the padding cell before the first
  // crumb) is still the row's: it is consumed and opens nothing.
  REQUIRE(e.winbar_press_for_test(layout.x, layout.y));
  REQUIRE_FALSE(e.winbar_menu_open_for_test());
  // A press below the row is not the winbar's at all.
  REQUIRE_FALSE(e.winbar_press_for_test(layout.x, layout.y + 1));
}

TEST_CASE("A folder row opens its own panel beside the level that offered it", "[jot][winbar]")
{
  seed_config_home();
  const std::string root = write_workspace_root();
  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root, true);
  e.load_file((fs::path(root) / "src" / "render" / "tabs.cpp").string());
  e.render_for_test();

  // The chain is root / src / render / tabs.cpp. The `src` crumb's own level
  // lists that folder: the `render` directory and the sibling file beside it.
  const Winbar::WinbarLayout layout = e.winbar_layout_for_test();
  REQUIRE(layout.segments.size() == 4);
  const Winbar::WinbarSegment &src_segment = layout.segments[1];
  REQUIRE(e.winbar_press_for_test(src_segment.x, layout.y));
  REQUIRE(e.winbar_menu_labels_for_test() == expect({"render", "scratch.cpp"}));
  REQUIRE(e.winbar_menu_dirs_for_test() == std::vector<bool>{true, false});
  REQUIRE(e.winbar_menu_level_count_for_test() == 1);

  // A motion onto the folder row opens its listing as a *second* level, beside
  // the first and anchored on the row that offered it -- the parent stays up.
  const int row_x = e.winbar_menu_x_for_test() + 2;
  REQUIRE(e.winbar_move_for_test(row_x, e.winbar_menu_y_for_test() + 1));
  REQUIRE(e.winbar_menu_level_count_for_test() == 2);
  REQUIRE(e.winbar_menu_title_for_test(1) == "render");
  REQUIRE(e.winbar_menu_labels_for_test(1) == expect({"buffer.cpp", "tabs.cpp"}));
  REQUIRE(e.winbar_menu_y_for_test(1) == e.winbar_menu_y_for_test() + 1);
  REQUIRE(e.winbar_menu_x_for_test(1) >= e.winbar_menu_x_for_test() + e.winbar_menu_w_for_test());
  // Both panels are painted where their rects say: the parent's title on its own
  // top row, the child's beside it on the row the folder sits on.
  e.render_for_test();
  const std::string parent_row = row_text(e, e.winbar_menu_y_for_test());
  const std::string child_row = row_text(e, e.winbar_menu_y_for_test(1));
  INFO("parent: [" << parent_row << "] child: [" << child_row << "]");
  REQUIRE(parent_row.find("src") != std::string::npos);
  REQUIRE(child_row.find("render") != std::string::npos);
  REQUIRE(child_row.find("render") > parent_row.find("src"));
  REQUIRE(row_text(e, e.winbar_menu_y_for_test(1) + 1).find("buffer.cpp") != std::string::npos);

  // The cascade follows the selection: a motion onto the file row below drops
  // the folder's level again (its rows no longer answer to the pointer).
  REQUIRE(e.winbar_move_for_test(row_x, e.winbar_menu_y_for_test() + 2));
  REQUIRE(e.winbar_menu_level_count_for_test() == 1);

  // A click there opens the file and puts the whole cascade away, so nothing is
  // left painted over the pane with no state behind it.
  REQUIRE(e.winbar_press_for_test(row_x, e.winbar_menu_y_for_test() + 2));
  REQUIRE_FALSE(e.winbar_menu_open_for_test());
  REQUIRE(e.buffer_for_test().filepath == (fs::path(root) / "src" / "scratch.cpp").string());
}

TEST_CASE("A row in a folder's own panel is picked where the panel is drawn", "[jot][winbar]")
{
  seed_config_home();
  const std::string root = write_workspace_root();
  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root, true);
  e.load_file((fs::path(root) / "src" / "render" / "tabs.cpp").string());
  e.render_for_test();

  const Winbar::WinbarLayout layout = e.winbar_layout_for_test();
  const Winbar::WinbarSegment &src_segment = layout.segments[1];
  REQUIRE(e.winbar_press_for_test(src_segment.x, layout.y));
  const int row_x = e.winbar_menu_x_for_test() + 2;
  REQUIRE(e.winbar_move_for_test(row_x, e.winbar_menu_y_for_test() + 1));
  REQUIRE(e.winbar_menu_level_count_for_test() == 2);

  // A press on the child's first row opens *that* file: the click hit-tests the
  // panel it landed on, not the level that happens to own the selection.
  const int child_file_x = e.winbar_menu_x_for_test(1) + 2;
  const int child_file_y = e.winbar_menu_y_for_test(1) + 1;
  REQUIRE(e.winbar_press_for_test(child_file_x, child_file_y));
  REQUIRE_FALSE(e.winbar_menu_open_for_test());
  REQUIRE(e.buffer_for_test().filepath == (fs::path(root) / "src" / "render" / "buffer.cpp").string());

  // Reopened, h steps back one level instead of closing everything, and Esc
  // then puts the rest away.
  REQUIRE(e.winbar_press_for_test(src_segment.x, layout.y));
  REQUIRE(e.winbar_move_for_test(row_x, e.winbar_menu_y_for_test() + 1));
  REQUIRE(e.winbar_menu_level_count_for_test() == 2);
  REQUIRE(e.winbar_menu_key_for_test('h'));
  REQUIRE(e.winbar_menu_level_count_for_test() == 1);
  REQUIRE(e.winbar_menu_title_for_test() == "src");
  REQUIRE(e.winbar_menu_key_for_test(27));
  REQUIRE_FALSE(e.winbar_menu_open_for_test());
}

TEST_CASE("The winbar setting turns the row off, and auto keeps it for code only", "[jot][winbar]")
{
  seed_config_home();
  const std::string root = write_workspace_root();
  {
    std::ofstream out(fs::path(root) / "src" / "render" / "notes.txt");
    out << "PLAIN TEXT\n";
  }

  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root, true);

  // `on`: every named file pays the row, prose included.
  e.set_winbar_mode_for_test(Winbar::WINBAR_MODE_ON);
  e.load_file((fs::path(root) / "src" / "render" / "notes.txt").string());
  e.render_for_test();
  REQUIRE(e.pane_has_winbar_for_test());

  // `auto`: prose does not (the row is for files with symbols to show).
  e.set_winbar_mode_for_test(Winbar::WINBAR_MODE_AUTO);
  e.render_for_test();
  REQUIRE_FALSE(e.pane_has_winbar_for_test());
  REQUIRE(e.pane_for_test().header_height == 0);

  // `off`: not even code.
  e.load_file((fs::path(root) / "src" / "render" / "buffer.cpp").string());
  e.render_for_test();
  REQUIRE(e.pane_has_winbar_for_test());
  e.set_winbar_mode_for_test(Winbar::WINBAR_MODE_OFF);
  e.render_for_test();
  REQUIRE_FALSE(e.pane_has_winbar_for_test());
  REQUIRE(e.pane_for_test().header_height == 0);
}
