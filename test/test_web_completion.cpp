// The workspace's own CSS vocabulary (src/features/web_completion.*), and the
// completions the editor builds out of it.
//
// Two halves, for the two ways this can be wrong. The extractors are pure text
// scans, and every rule in them exists because the obvious scan gets something
// wrong: a class name in a comment, `a.b` read as a class, a decimal point read
// as a class, a custom property inside a string. The editor cases then pin where
// the names are *offered* -- inside `class="..."` and `var(--)` and nowhere
// else, since a class list in a `printf` line is not a class list.
#include "core/editor.h"
#include "workspace/web_completion.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  bool has(const std::vector<std::string> &list, const std::string &value)
  {
    return std::find(list.begin(), list.end(), value) != list.end();
  }

  std::string joined(const std::vector<std::string> &list)
  {
    std::string out;
    for (const std::string &value : list)
    {
      out += value + " ";
    }
    return out;
  }

  void seed_config_home()
  {
    char home[] = "/tmp/jot_web_completion_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A file whose content is `text`, opened with the caret at `line`:`col`. The
  // suffix is what decides the file type, and the file type is half of what the
  // cases below are about -- a real path beats a faked one.
  void
  open_with_caret(Editor &e, const std::string &suffix, const std::string &text, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_web_completion_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + suffix;
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(line, col);
  }

  // A scratch workspace of one file per call, so a walk has something to read.
  class ScratchWorkspace
  {
  public:
    ScratchWorkspace()
    {
      char dir[] = "/tmp/jot_web_index_test_XXXXXX";
      REQUIRE(mkdtemp(dir) != nullptr);
      root_ = dir;
      fs::create_directories(root_);
    }
    ~ScratchWorkspace()
    {
      fs::remove_all(root_);
    }
    ScratchWorkspace(const ScratchWorkspace &) = delete;
    ScratchWorkspace &operator=(const ScratchWorkspace &) = delete;

    const std::string &root() const
    {
      return root_;
    }

    void write(const std::string &relative, const std::string &text)
    {
      const fs::path path = fs::path(root_) / relative;
      fs::create_directories(path.parent_path());
      std::ofstream out(path);
      out << text;
    }

  private:
    std::string root_;
  };
} // namespace

TEST_CASE("Web completion: class names come out of the markup that uses them", "[jot][web]")
{
  const std::string text = "<div class=\"card card--wide\" id=\"main\">\n"
                           "  <span class='tag'>x</span>\n"
                           "  <a className=\"link active\">y</a>\n"
                           "  <em class={dynamic}>z</em>\n"
                           "  <!-- class=\"not-real\" -->\n"
                           "  <li class=\"item--{{ loop.index }}\">q</li>\n"
                           "  <li class=\"base extra\">r</li>\n"
                           "</div>\n";

  WebCompletion::Index index;
  WebCompletion::extract_from_markup(text, index);
  index.sort_unique();

  REQUIRE(has(index.classes, "card"));
  REQUIRE(has(index.classes, "card--wide"));
  REQUIRE(has(index.classes, "tag"));
  REQUIRE(has(index.classes, "link"));
  REQUIRE(has(index.classes, "active"));
  REQUIRE(has(index.classes, "base"));
  REQUIRE(has(index.classes, "extra"));
  // What must not be a name: a commented-out attribute, a JSX expression, and a
  // token with a template marker in it. The last one takes the whole token --
  // `item--{{ loop.index }}` is one word, and half of a templated name is not
  // the name.
  REQUIRE_FALSE(has(index.classes, "not-real"));
  REQUIRE_FALSE(has(index.classes, "dynamic"));
  REQUIRE_FALSE(has(index.classes, "loop.index"));
  REQUIRE_FALSE(has(index.classes, "item--"));
  REQUIRE(index.css_vars.empty());
}

TEST_CASE("Web completion: custom properties come out of declarations only", "[jot][web]")
{
  const std::string text = ":root {\n"
                           "  --mint: #0e8a6f;\n"
                           "  --sakura-400 : pink;\n"
                           "  /* --ghost: not-a-declaration */\n"
                           "}\n"
                           ".card { color: var(--mint); }\n"
                           ".other::after { content: \"--string-only\"; }\n"
                           "a { --not-a-prop }  /* no colon, so not a declaration either */\n";

  WebCompletion::Index index;
  WebCompletion::extract_from_css(text, index);
  index.sort_unique();

  REQUIRE(has(index.css_vars, "mint"));
  REQUIRE(has(index.css_vars, "sakura-400"));
  REQUIRE_FALSE(has(index.css_vars, "ghost"));
  REQUIRE_FALSE(has(index.css_vars, "string-only"));
  REQUIRE_FALSE(has(index.css_vars, "not-a-prop"));

  // The same text as a selector scan: `.card` and `.other` are classes, and the
  // `::after` pseudo-element must not be mistaken for one.
  WebCompletion::Index selectors;
  WebCompletion::extract_selectors(text, selectors);
  REQUIRE(has(selectors.classes, "card"));
  REQUIRE(has(selectors.classes, "other"));
  REQUIRE_FALSE(has(selectors.classes, "after"));
}

TEST_CASE("Web completion: a dot that is not a selector does not name a class", "[jot][web]")
{
  const std::string text = "@media (min-width: 40.5em) { .narrow { width: 1.5rem; } }\n"
                           "js.thing { }\n"
                           "#id .cls, div > .child + .sibling { }\n"
                           "url(icon.svg) el.classList.add(\"from-js\");\n";

  WebCompletion::Index index;
  WebCompletion::extract_selectors(text, index);
  index.sort_unique();

  INFO(joined(index.classes));
  REQUIRE(has(index.classes, "narrow"));
  REQUIRE(has(index.classes, "cls"));
  REQUIRE(has(index.classes, "child"));
  REQUIRE(has(index.classes, "sibling"));
  // `40.5`, `1.5` are decimals, `js.thing` is a member access, and
  // `from-js` is a string argument rather than a selector.
  REQUIRE_FALSE(has(index.classes, "5em"));
  REQUIRE_FALSE(has(index.classes, "5rem"));
  REQUIRE_FALSE(has(index.classes, "thing"));
  REQUIRE_FALSE(has(index.classes, "svg"));
  REQUIRE_FALSE(has(index.classes, "add"));
  REQUIRE_FALSE(has(index.classes, "from-js"));
  // The one trade-off worth naming: a dot right after an identifier is skipped,
  // so `#id.cls` -- an id immediately followed by a class -- is not read as a
  // class here. That spelling is rare, and the class is nearly always declared
  // as `.cls { ... }` somewhere in the same file, which is where the scan picks
  // it up. The alternative (accepting every `ident.ident`) buys that case back
  // at the price of reading `icon.svg` and every `a.b` as class names.
  REQUIRE(has(index.classes, "cls"));
}

TEST_CASE("Web completion: the caret's context decides which names apply", "[jot][web]")
{
  using WebCompletion::Context;
  using WebCompletion::context_at;

  // Markup: the class attribute, and only while its quote is open.
  REQUIRE(context_at("  <div class=\"ca", 16, false) == Context::ClassName);
  REQUIRE(context_at("  <div className=\"ca", 20, false) == Context::ClassName);
  REQUIRE(context_at("  <div class=\"card\" id=\"x", 23, false) == Context::None);
  REQUIRE(context_at("  <div id=\"card", 15, false) == Context::None);
  REQUIRE(context_at("  <div class=\"card\">", 20, false) == Context::None);
  REQUIRE(context_at("  cla", 5, false) == Context::None);

  // A style sheet: `var(--` and a property being declared, and neither
  // anywhere else.
  REQUIRE(context_at("  color: var(--mi", 16, true) == Context::CssVar);
  REQUIRE(context_at("  --mi", 6, true) == Context::CssVar);
  // The caret inside the call is in it; past the closing paren it is not.
  REQUIRE(context_at("  color: var(--mint)", 19, true) == Context::CssVar);
  REQUIRE(context_at("  color: var(--mint)", 20, true) == Context::None);
  REQUIRE(context_at("  margin: 10px", 13, true) == Context::None);
  REQUIRE(context_at("  -webkit-mask: none", 20, true) == Context::None);

  // The markup test is not applied to a style sheet (or the reverse): the two
  // languages spell their contexts differently.
  REQUIRE(context_at("  <div class=\"ca", 16, true) == Context::None);
}

TEST_CASE("Web completion: the walk reads the workspace's web files and skips noise", "[jot][web]")
{
  ScratchWorkspace workspace;
  workspace.write("index.html", "<div class=\"hero hero--tall\"></div>\n");
  workspace.write("styles/site.css", ":root { --brand: #d92a76; }\n.card { color: red; }\n");
  workspace.write("components/button.jsx", "<button className=\"btn btn--primary\">go</button>\n");
  workspace.write("notes.txt", "class=\"not-scanned\"\n");
  workspace.write("node_modules/pkg/index.css", ".from-a-dependency { }\n");
  workspace.write("build/index.html", "<i class=\"from-a-build\"></i>\n");

  const WebCompletion::Index index = WebCompletion::scan_workspace(workspace.root());

  INFO(joined(index.classes) << "|" << joined(index.css_vars));
  REQUIRE(has(index.classes, "hero"));
  REQUIRE(has(index.classes, "hero--tall"));
  REQUIRE(has(index.classes, "card"));
  REQUIRE(has(index.classes, "btn"));
  REQUIRE(has(index.classes, "btn--primary"));
  REQUIRE(has(index.css_vars, "brand"));
  REQUIRE_FALSE(has(index.classes, "not-scanned"));
  REQUIRE_FALSE(has(index.classes, "from-a-dependency"));
  REQUIRE_FALSE(has(index.classes, "from-a-build"));
  // The lists are sorted and deduplicated, so the completion order is stable.
  REQUIRE(std::is_sorted(index.classes.begin(), index.classes.end()));
  REQUIRE(std::is_sorted(index.css_vars.begin(), index.css_vars.end()));
  REQUIRE(std::adjacent_find(index.classes.begin(), index.classes.end()) == index.classes.end());

  // A root that is not there yields an empty index rather than an error.
  const WebCompletion::Index missing =
      WebCompletion::scan_workspace("/tmp/jot_web_index_test_absent_root");
  REQUIRE(missing.empty());
}

TEST_CASE("Web completion: the editor offers the workspace names where they belong", "[jot][web]")
{
  seed_config_home();
  Editor e;
  WebCompletion::Index index;
  index.classes = {"card", "hero"};
  index.css_vars = {"brand", "mint"};
  index.sort_unique();
  e.set_web_index_for_test(index);

  // Inside a class attribute: the class names, not the custom properties.
  open_with_caret(e, ".html", "<div class=\"ca\">x</div>\n", 0, 14);
  {
    const auto labels = e.web_index_completions_for_test();
    INFO(joined(labels));
    REQUIRE(has(labels, "card"));
    REQUIRE(has(labels, "hero"));
    REQUIRE_FALSE(has(labels, "brand"));
  }

  // Inside var(--): the custom properties, not the class names.
  open_with_caret(e, ".css", "  color: var(--br\n", 0, 17);
  {
    const auto labels = e.web_index_completions_for_test();
    INFO(joined(labels));
    REQUIRE(has(labels, "brand"));
    REQUIRE(has(labels, "mint"));
    REQUIRE_FALSE(has(labels, "card"));
  }

  // Anywhere else: nothing, so the list stays whatever the server said. The
  // first of these is the case that matters -- the same `class="` text in a
  // language that has no class attribute must not be read as one, or every
  // C++ string that starts a class attribute would offer CSS classes.
  open_with_caret(e, ".cpp", "  printf(\"class=\\\"ca\n", 0, 18);
  REQUIRE(e.web_index_completions_for_test().empty());
  open_with_caret(e, ".css", "  margin: 10px\n", 0, 13);
  REQUIRE(e.web_index_completions_for_test().empty());
  open_with_caret(e, ".html", "<div id=\"card\n", 0, 14);
  REQUIRE(e.web_index_completions_for_test().empty());

  // An empty index adds nothing, whatever the context.
  e.set_web_index_for_test(WebCompletion::Index{});
  open_with_caret(e, ".html", "<div class=\"ca\">x</div>\n", 0, 14);
  REQUIRE(e.web_index_completions_for_test().empty());
}
