// The severity band: a line that holds an error or a warning is painted edge to
// edge in the theme's error/warning background -- the gutter, the code, and the
// empty space past the end of the text -- so a problem is findable while
// scrolling instead of only where the squiggle sits.
//
// Two rules keep the band from becoming a wash over everything. It is a base
// rather than an overlay, so selection, search hits and anchored decorations
// still paint on top of it (the occurrence highlight is the one that does not:
// it is ambient, and a tint clipping a corner out of a banded row reads as a
// rendering fault); and it exists only when the theme asks for one, so
// a theme that never mentions a band gets plain rows instead of the editor
// inventing a tint out of the severity colour. Info and hint rows carry no band
// at all.
#include "core/editor.h"
#include "core/decorations_rebase.h"
#include "core/panes.h" // pane_content_top
#include "render/gutter.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  // A fresh config home per case: the band is decided by the theme and the
  // buffer's diagnostics, and a shared home would carry another case's theme.
  void seed_config_home()
  {
    char home[] = "/tmp/jot_diag_band_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  void write_theme(const std::string &name, const std::string &body)
  {
    const std::filesystem::path dir =
        std::filesystem::path(getenv("JOT_CONFIG_HOME")) / "configs" / "colors";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / (name + ".json"));
    out << body;
  }

  void load_lines(Editor &e, const std::vector<std::string> &lines, const std::string &name)
  {
    const std::string path = "/tmp/jot_diag_band_" + name + ".cpp";
    std::ofstream out(path);
    for (const auto &line : lines)
    {
      out << line << "\n";
    }
    out.close();
    e.load_file(path);
  }

  Diagnostic diagnostic(int line, int severity, const std::string &message)
  {
    Diagnostic d;
    d.line = line;
    d.col = 0;
    d.end_line = line;
    d.end_col = 1;
    d.severity = severity;
    d.message = message;
    return d;
  }

  // Repaints on demand: a diagnostic seeded straight into the buffer does not
  // mark the frame dirty, and render() leaves an undirtied frame as it is.
  void repaint(Editor &e)
  {
    e.request_redraw_for_test();
    e.render_for_test();
  }

  // The background of one cell of a visible pane row, read off the frame that
  // was just rendered.
  int row_bg(Editor &e, int line, int screen_col)
  {
    const SplitPane &pane = e.pane_for_test();
    const UICell *cell = e.ui_for_test()->cell_at(screen_col, pane_content_top(pane) + line);
    REQUIRE(cell != nullptr);
    return cell->bg;
  }

  // The screen column of the first code cell: the pane's own left edge, the
  // gutter the pane charges for (border space included), then the text.
  int code_x(Editor &e)
  {
    return e.pane_for_test().x + 1 + gutter::width(e.buffer_for_test().line_count());
  }
} // namespace

TEST_CASE("Diagnostic band: error and warning rows are painted edge to edge", "[jot]")
{
  seed_config_home();
  write_theme("bandprobe",
              "{\n"
              "  \"extends\": \"jot-dark\",\n"
              "  \"DiagnosticError\": {\"bg\": 52},\n"
              "  \"DiagnosticWarn\": {\"bg\": 58}\n"
              "}\n");
  Editor e;
  e.set_home_menu_visible(false);
  load_lines(e, {"int broken = 1;", "int fine = 2;", "int warned = 3;", "int hinted = 4;"},
             "rows");
  e.apply_resize_for_test(100, 30);
  REQUIRE(e.apply_theme_for_test("bandprobe"));
  const Theme &t = e.theme_for_test();
  REQUIRE(t.bg_diagnostic_error == 52);
  REQUIRE(t.bg_diagnostic_warning == 58);

  FileBuffer &buf = e.buffer_for_test();
  buf.diagnostics = {diagnostic(0, 1, "error here"), diagnostic(2, 2, "warning here"),
                     diagnostic(3, 3, "info here")};
  buf.diag_severity_dirty = true;
  // Park the caret on `fine`, which these rows name once. Every row here starts
  // with `int`, so a caret left on it would tint those cells and pull the clean
  // row off the pane background this case asserts.
  e.scroll_cursor_to_for_test(1, 4);
  repaint(e);

  const SplitPane &pane = e.pane_for_test();
  const int code = code_x(e);
  const int left = pane.x;               // the row's first cell: the gutter's edge
  const int right = pane.x + pane.w - 2; // the last cell the content area owns
  const int past_text = code + 20;       // clear of every short line's text
  REQUIRE(past_text < right);

  // The error row wears the error band from its left edge out past the text to
  // the pane's right edge; the number's own cell is on that band too.
  REQUIRE(row_bg(e, 0, left) == 52);
  REQUIRE(row_bg(e, 0, pane.x + 1) == 52);
  REQUIRE(row_bg(e, 0, code) == 52);
  REQUIRE(row_bg(e, 0, past_text) == 52);
  REQUIRE(row_bg(e, 0, right) == 52);

  // The warning row wears the warning band, not the error's.
  REQUIRE(row_bg(e, 2, left) == 58);
  REQUIRE(row_bg(e, 2, code) == 58);
  REQUIRE(row_bg(e, 2, past_text) == 58);
  REQUIRE(row_bg(e, 2, right) == 58);

  // Info is neither an error nor a warning, and a clean row is clean: both keep
  // the pane background right out to the row's end.
  REQUIRE(row_bg(e, 3, left) == t.bg_default);
  REQUIRE(row_bg(e, 3, past_text) == t.bg_default);
  REQUIRE(row_bg(e, 1, code) == t.bg_default);
  REQUIRE(row_bg(e, 1, right) == t.bg_default);
}

TEST_CASE("Diagnostic band: the band is a base, so the overlays still win", "[jot]")
{
  seed_config_home();
  write_theme("bandprobe",
              "{\n"
              "  \"extends\": \"jot-dark\",\n"
              "  \"DiagnosticError\": {\"bg\": 52},\n"
              "  \"DiagnosticWarn\": {\"bg\": 58}\n"
              "}\n");
  Editor e;
  e.set_home_menu_visible(false);
  load_lines(e, {"int broken = 1;", "int fine = 2;", "int warned = 3;"}, "layers");
  e.apply_resize_for_test(100, 30);
  REQUIRE(e.apply_theme_for_test("bandprobe"));
  const Theme &t = e.theme_for_test();
  const int code = code_x(e);

  FileBuffer &buf = e.buffer_for_test();
  buf.diagnostics = {diagnostic(0, 1, "error here"), diagnostic(2, 2, "warning here")};
  buf.diag_severity_dirty = true;

  // A selection over the first half of the error row: the selected cells take
  // the selection background, and the rest of the row keeps its band.
  e.scroll_cursor_to_for_test(1, 0);
  buf.selection = {{0, 0}, {5, 0}, true}; // first five cells of the error row
  repaint(e);
  REQUIRE(row_bg(e, 0, code) == t.bg_selection);
  REQUIRE(row_bg(e, 0, code + 3) == t.bg_selection);
  REQUIRE(row_bg(e, 0, code + 10) == 52);
  REQUIRE(row_bg(e, 0, code + 20) == 52);

  // An anchored decoration with a background of its own paints over the band,
  // the same layering a search hit obeys.
  buf.selection = {{0, 0}, {0, 0}, false};
  Decoration deco;
  deco.row = 0;
  deco.col = 6;
  deco.width = 3;
  deco.priority = 10;
  deco.bg = 11;
  decoration_insert(buf, deco);
  repaint(e);
  REQUIRE(row_bg(e, 0, code + 6) == 11);
  REQUIRE(row_bg(e, 0, code + 10) == 52);

  // The occurrence highlight is the one overlay the band beats. It is ambient
  // (the caret lands on a word by itself), and a tint that clipped a corner out
  // of a banded row reads as a rendering fault, so the whole row stays banded.
  e.scroll_cursor_to_for_test(2, 4);
  repaint(e);
  REQUIRE(t.bg_word_highlight_strong != 58);
  REQUIRE(row_bg(e, 2, code) == 58);
  REQUIRE(row_bg(e, 2, code + 4) == 58);
  REQUIRE(row_bg(e, 2, code + 9) == 58);
  REQUIRE(row_bg(e, 2, code + 20) == 58);
  REQUIRE(row_bg(e, 1, code) == t.bg_default);

  // The cursor row keeps its band: severity is the louder signal, and the
  // number's own colour already says where the caret is.
  e.scroll_cursor_to_for_test(2, 0);
  repaint(e);
  REQUIRE(t.bg_cursor_line != 58);
  REQUIRE(row_bg(e, 2, e.pane_for_test().x) == 58);
  REQUIRE(row_bg(e, 2, e.pane_for_test().x + 1) == 58);
  REQUIRE(row_bg(e, 2, code + 20) == 58);
}

TEST_CASE("Diagnostic band: a theme that sets no band keeps the rows plain", "[jot]")
{
  seed_config_home();
  write_theme("plainprobe", "{\n  \"Normal\": {\"fg\": 7, \"bg\": 0}\n}\n");
  Editor e;
  e.set_home_menu_visible(false);
  load_lines(e, {"int broken = 1;", "int fine = 2;"}, "plain");
  e.apply_resize_for_test(100, 30);
  REQUIRE(e.apply_theme_for_test("plainprobe"));
  const Theme &t = e.theme_for_test();
  // A theme that never names a band leaves both slots at the "no band"
  // sentinel. The row must stay on the pane background: painting the sentinel
  // itself (or a colour derived from the severity) is the failure this guards.
  REQUIRE(t.bg_diagnostic_error == -1);
  REQUIRE(t.bg_diagnostic_warning == -1);

  FileBuffer &buf = e.buffer_for_test();
  buf.diagnostics = {diagnostic(0, 1, "error here")};
  buf.diag_severity_dirty = true;
  repaint(e);

  const SplitPane &pane = e.pane_for_test();
  REQUIRE(row_bg(e, 0, pane.x) == t.bg_default);
  REQUIRE(row_bg(e, 0, code_x(e) + 20) == t.bg_default);
  REQUIRE(row_bg(e, 0, pane.x + pane.w - 2) == t.bg_default);
  REQUIRE(row_bg(e, 1, pane.x) == t.bg_default);
}
