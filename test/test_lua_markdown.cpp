// Headless tests for the bundled markdown preview renderer
// (runtime/lua/features/markdown/*). The module tree is loaded into a raw Lua
// state whose jot.config API is stubbed by a small table store, so no Editor,
// no terminal and no HTTP server is needed to check the HTML the preview page
// receives.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  // Builds the `jot` stub and pre-loads every markdown module under its
  // require() name, exactly like the native loader does.
  const char *kPrelude = R"LUA(
    local dir = ...
    store = {}
    jot = {
      config = {
        get = function(k, d) local v = store[k]; if v == nil then return d end; return v end,
        get_bool = function(k, d) local v = store[k]; if v == nil then return d end; return v == true end,
        get_number = function(k, d) local v = store[k]; if v == nil then return d end; return tonumber(v) end,
        set = function(k, v) store[k] = v end,
      },
    }
    local order = { "config", "inline", "toc", "block", "assets", "template", "render" }
    for _, name in ipairs(order) do
      package.loaded["jot_md." .. name] = assert(loadfile(dir .. name .. ".lua"))()
    end
    local render_mod = package.loaded["jot_md.render"]
    md = {
      render = render_mod.render,
      config = package.loaded["jot_md.config"],
    }
  )LUA";

  struct MdState
  {
    lua_State *L = nullptr;

    MdState()
    {
      L = luaL_newstate();
      REQUIRE(L != nullptr);
      luaL_openlibs(L);
      const std::string dir = std::string(JOT_LUA_SOURCE_DIR) + "/features/markdown/";
      REQUIRE(luaL_loadstring(L, kPrelude) == LUA_OK);
      lua_pushstring(L, dir.c_str());
      REQUIRE(lua_pcall(L, 1, 0, 0) == LUA_OK);
    }

    ~MdState()
    {
      if (L)
        lua_close(L);
    }

    MdState(const MdState &) = delete;
    MdState &operator=(const MdState &) = delete;

    // Enables a preview option ("mermaid", "emoji", ...) for this state.
    void enable_option(const std::string &name)
    {
      lua_getglobal(L, "jot");
      lua_getfield(L, -1, "config");
      lua_getfield(L, -1, "set");
      lua_pushfstring(L, "markdown_preview_option_%s", name.c_str());
      lua_pushboolean(L, 1);
      REQUIRE(lua_pcall(L, 2, 0, 0) == LUA_OK);
      lua_pop(L, 2);
    }

    // `field` picks a member of the result table: "body" (the rendered
    // document), "toc", "title" or "html" (the whole page shell).
    std::string render(const std::string &text, const std::string &field = "body")
    {
      lua_getglobal(L, "md");
      lua_getfield(L, -1, "render");
      lua_pushlstring(L, text.data(), text.size());
      lua_newtable(L);
      lua_pushstring(L, "Doc.md");
      lua_setfield(L, -2, "path");
      lua_pushstring(L, "Doc.md");
      lua_setfield(L, -2, "name");
      if (lua_pcall(L, 2, 1, 0) != LUA_OK)
      {
        const char *error = lua_tostring(L, -1);
        FAIL("markdown render failed: " << (error ? error : "unknown"));
      }
      REQUIRE(lua_istable(L, -1));
      lua_getfield(L, -1, field.c_str());
      REQUIRE(lua_isstring(L, -1));
      std::string out = lua_tostring(L, -1);
      lua_pop(L, 3); // value, result table, md table
      return out;
    }
  };

  bool has(const std::string &haystack, const std::string &needle)
  {
    return haystack.find(needle) != std::string::npos;
  }
} // namespace

TEST_CASE("Markdown headings get slug ids, source anchors and a nested TOC", "[markdown][lua]")
{
  MdState state;
  const std::string body = state.render("# Title\n\n## Sub One\n\n### Deep\n\n## Sub Two\n");
  REQUIRE(has(body, "<h1 id=\"title\" data-line=\"1\">Title</h1>"));
  REQUIRE(has(body, "<h2 id=\"sub-one\" data-line=\"3\">Sub One</h2>"));
  REQUIRE(has(body, "<h3 id=\"deep\" data-line=\"5\">Deep</h3>"));

  const std::string toc = state.render("# Title\n\n## Sub One\n\n### Deep\n\n## Sub Two\n", "toc");
  REQUIRE(has(toc, "<a href=\"#title\">Title</a>"));
  REQUIRE(has(toc, "<a href=\"#deep\">Deep</a>"));
  REQUIRE(has(toc, "<a href=\"#sub-two\">Sub Two</a>"));
  // The deep heading nests inside "Sub One", so its closing tag follows it.
  REQUIRE(toc.find("#deep") < toc.find("</ul></li></ul>"));
}

TEST_CASE("Markdown inline marks render and text is escaped", "[markdown][lua]")
{
  MdState state;
  const std::string body = state.render(
      "Some *em*, **strong**, ***both***, `code`, ~~gone~~, H~2~O, E=mc^2^, ==hi==, ++new++. "
      "Ampersand & and 5 < 6 > 4.");
  REQUIRE(has(body, "<em>em</em>"));
  REQUIRE(has(body, "<strong>strong</strong>"));
  REQUIRE(has(body, "<strong><em>both</em></strong>"));
  REQUIRE(has(body, "<code>code</code>"));
  REQUIRE(has(body, "<del>gone</del>"));
  REQUIRE(has(body, "H<sub>2</sub>O"));
  REQUIRE(has(body, "E=mc<sup>2</sup>"));
  REQUIRE(has(body, "<mark>hi</mark>"));
  REQUIRE(has(body, "<ins>new</ins>"));
  REQUIRE(has(body, "Ampersand &amp; and 5 &lt; 6 &gt; 4."));
}

TEST_CASE("Markdown links and images validate targets and sizes", "[markdown][lua]")
{
  MdState state;
  const std::string body = state.render(
      "A [link](https://example.com \"home\") and ![shot](img.png =320x200) "
      "and [bad](javascript:alert(1)) and <https://auto.example>.");
  REQUIRE(has(body, "<a href=\"https://example.com\" title=\"home\""));
  REQUIRE(has(body, "target=\"_blank\""));
  REQUIRE(has(body, "<img src=\"img.png\" alt=\"shot\" width=\"320\" height=\"200\">"));
  // javascript: targets are dropped instead of becoming an href; the label
  // survives as plain text.
  REQUIRE_FALSE(has(body, "href=\"javascript:"));
  REQUIRE(has(body, "[bad]"));
  REQUIRE(has(body, "<a href=\"https://auto.example\""));
}

TEST_CASE("Markdown lists nest, ordered lists keep their start, tasks get checkboxes", "[markdown][lua]")
{
  MdState state;
  const std::string body = state.render("- [x] done\n- [ ] todo\n");
  REQUIRE(has(body, "<input type=\"checkbox\" disabled checked> done"));
  REQUIRE(has(body, "<input type=\"checkbox\" disabled> todo"));

  // A nested child makes the parent list loose, so the task paragraph wraps.
  const std::string nested = state.render("- [ ] parent\n  - child\n");
  REQUIRE(has(nested, "<ul data-line=\"2\">"));
  REQUIRE(has(nested, "<li>child"));

  const std::string ordered = state.render("3. three\n4. four\n");
  REQUIRE(has(ordered, "<ol start=\"3\""));
}

TEST_CASE("Markdown tables honour alignment", "[markdown][lua]")
{
  MdState state;
  const std::string body = state.render(
      "| Left | Center | Right |\n"
      "|:-----|:------:|------:|\n"
      "| a | b | c |\n");
  REQUIRE(has(body, "<table>"));
  REQUIRE(has(body, "<th style=\"text-align:left\">Left</th>"));
  REQUIRE(has(body, "<th style=\"text-align:center\">Center</th>"));
  REQUIRE(has(body, "<th style=\"text-align:right\">Right</th>"));
  REQUIRE(has(body, "<td style=\"text-align:right\">c</td>"));
}

TEST_CASE("Fenced code renders with language classes, copy buttons and diagrams", "[markdown][lua]")
{
  MdState state;
  const std::string inline_default =
      state.render("```cpp\nint x = 1;\n```\n");
  REQUIRE(has(inline_default, "<code class=\"language-cpp\">int x = 1;</code>"));
  REQUIRE(has(inline_default, "class=\"copy-code\""));

  // mermaid is off by default: the fence stays an ordinary code block.
  const std::string mermaid_off = state.render("```mermaid\ngraph TD; A-->B;\n```\n");
  REQUIRE(has(mermaid_off, "<code class=\"language-mermaid\">"));

  state.enable_option("mermaid");
  const std::string mermaid_on = state.render("```mermaid\ngraph TD; A-->B;\n```\n");
  REQUIRE(has(mermaid_on, "<div class=\"mermaid\" data-line=\"1\">"));
  REQUIRE(has(mermaid_on, "A--&gt;B;"));
}

TEST_CASE("Markdown footnotes and reference links resolve", "[markdown][lua]")
{
  MdState state;
  const std::string body = state.render(
      "Text with a note[^a] and a [reference][ref].\n"
      "\n"
      "[ref]: https://ref.example \"Ref\"\n"
      "[^a]: The footnote body.\n");
  REQUIRE(has(body, "<sup class=\"footnote-ref\"><a href=\"#fn-a\" id=\"fnref-a\">1</a></sup>"));
  REQUIRE(has(body, "<a href=\"https://ref.example\" title=\"Ref\""));
  REQUIRE(has(body, "<li id=\"fn-a\" data-line=\"4\">"));
  REQUIRE(has(body, "The footnote body."));
  // The definition lines themselves never render as paragraphs.
  REQUIRE_FALSE(has(body, "[^a]: The footnote body."));
}

TEST_CASE("Markdown blockquotes, alerts, rules and raw HTML", "[markdown][lua]")
{
  MdState state;
  const std::string body = state.render(
      "> quoted\n"
      "\n"
      "> [!NOTE]\n"
      "> heads up\n"
      "\n"
      "---\n"
      "\n"
      "<div class=\"raw\">kept</div>\n");
  REQUIRE(has(body, "<blockquote data-line=\"1\">"));
  REQUIRE(has(body, "quoted"));
  REQUIRE(has(body, "<blockquote class=\"alert alert-note\""));
  REQUIRE(has(body, "<p class=\"alert-title\">Note</p>"));
  REQUIRE(has(body, "<hr data-line=\"6\">"));
  REQUIRE(has(body, "<div class=\"raw\">kept</div>"));
}

TEST_CASE("The preview page embeds the body and the live client", "[markdown][lua]")
{
  MdState state;
  const std::string page = state.render("# Hello\n", "html");
  REQUIRE(has(page, "<!DOCTYPE html>"));
  REQUIRE(has(page, "<div id=\"content\"><h1 id=\"hello\""));
  REQUIRE(has(page, "EventSource('/events')"));
  REQUIRE(has(page, "fetch('/sync'"));
  REQUIRE(has(page, "<title>Hello</title>"));
}
