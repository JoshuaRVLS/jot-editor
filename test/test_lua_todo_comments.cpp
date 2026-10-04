// Headless test of the bundled TODO-comment feature
// (runtime/lua/features/todo_comments.lua), the port of folke/todo-comments.nvim.
// The module is loaded into a raw Lua state whose jot.* API is stubbed with
// recording functions, so no Editor or terminal is needed.
//
// Pins the contract: the keyword has to sit on a word boundary and the longest
// keyword wins (FIXME over FIX), the chip only appears when the syntax
// highlighter calls the hit a comment, it is the keyword alone and never the
// `//`, the space before the word or the colon, the colon is hidden by painting
// it in the theme's own background ink while the character stays real, the text
// after it takes the family colour and the colour run carries into the
// following comment lines, and the chip's ink is whichever of the theme's
// normal fg/bg contrasts most with it (upstream's maximize_contrast).
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  struct Span
  {
    int start = 0;
    int len = 0;
    std::string kind;
  };

  struct DecoSpec
  {
    int row = 0;
    int col = 0;
    int width = 0;
    int priority = 0;
    std::string fg;
    std::string bg;
  };

  struct SearchHit
  {
    std::string path;
    std::string relative;
    std::string text;
    int line = 1;
  };

  struct StubState
  {
    std::map<std::string, int> handlers; // event -> registry ref
    std::vector<std::string> commands;
    std::vector<std::pair<std::string, std::string>> keymaps; // chords -> action
    std::vector<DecoSpec> specs;
    std::map<std::string, std::string> config_overrides;
    std::map<std::string, std::string> theme; // group -> fg
    std::string normal_fg = "#c5c0d4";
    std::string normal_bg = "#07060e";
    std::map<std::string, std::vector<Span>> spans; // line -> fixed spans
    bool no_rules = false;                          // highlighter has no rules
    int current_buffer = 1;
    std::string path = "/tmp/probe.lua";
    std::string text;
    int cursor_line = 1;
    int cursor_col = 1;
    std::vector<std::string> notices;
    std::vector<SearchHit> search_hits;
    int get_text_calls = 0;
    int get_line_calls = 0;
  };

  StubState g;

  int stub_config_get(lua_State *L)
  {
    // jot.config.get(key, default): an override when the test set one, else
    // the default (the real bridge behaves the same for a defaulted key).
    const char *key = luaL_checkstring(L, 1);
    auto it = g.config_overrides.find(key);
    if (it != g.config_overrides.end())
      lua_pushstring(L, it->second.c_str());
    else
      lua_pushvalue(L, 2);
    return 1;
  }

  int stub_autocmd(lua_State *L)
  {
    const char *event = luaL_checkstring(L, 1);
    REQUIRE(lua_isfunction(L, 2));
    lua_pushvalue(L, 2);
    auto it = g.handlers.find(event);
    if (it != g.handlers.end() && it->second != LUA_NOREF)
      luaL_unref(L, LUA_REGISTRYINDEX, it->second);
    g.handlers[event] = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
  }

  int stub_command(lua_State *L)
  {
    g.commands.push_back(luaL_checkstring(L, 1));
    REQUIRE(lua_isfunction(L, 2));
    return 0;
  }

  int stub_notify(lua_State *L)
  {
    g.notices.push_back(luaL_checkstring(L, 1));
    return 0;
  }

  int stub_keymap_set(lua_State *L)
  {
    g.keymaps.emplace_back(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
    return 0;
  }

  // The regex fallback's shape for these tests: a comment span from the first
  // `//` or `--` through the end of the line. A test overrides g.spans by exact
  // line text when it needs strings or block comments distinguished.
  std::vector<Span> default_spans(const std::string &line)
  {
    size_t at = line.find("//");
    size_t dash = line.find("--");
    if (dash != std::string::npos && (at == std::string::npos || dash < at))
      at = dash;
    if (at == std::string::npos)
      return {};
    return {{(int)at, (int)(line.size() - at), "comment"}};
  }

  int stub_syntax_highlight(lua_State *L)
  {
    luaL_checkstring(L, 1);
    const char *line = luaL_checkstring(L, 2);
    lua_newtable(L);
    lua_pushboolean(L, g.no_rules ? 0 : 1);
    lua_setfield(L, -2, "rules");
    auto it = g.spans.find(line);
    const std::vector<Span> spans = it == g.spans.end() ? default_spans(line) : it->second;
    for (size_t i = 0; i < spans.size(); i++)
    {
      lua_newtable(L);
      lua_pushinteger(L, spans[i].start);
      lua_setfield(L, -2, "start");
      lua_pushinteger(L, spans[i].len);
      lua_setfield(L, -2, "len");
      lua_pushstring(L, spans[i].kind.c_str());
      lua_setfield(L, -2, "kind");
      lua_rawseti(L, -2, (int)i + 1);
    }
    return 1;
  }

  int stub_theme_get(lua_State *L)
  {
    const char *name = luaL_checkstring(L, 1);
    if (std::string(name) == "normal")
    {
      lua_newtable(L);
      lua_pushstring(L, g.normal_fg.c_str());
      lua_setfield(L, -2, "fg");
      lua_pushstring(L, g.normal_bg.c_str());
      lua_setfield(L, -2, "bg");
      return 1;
    }
    auto it = g.theme.find(name);
    if (it == g.theme.end())
    {
      lua_pushnil(L);
      return 1;
    }
    lua_newtable(L);
    lua_pushstring(L, it->second.c_str());
    lua_setfield(L, -2, "fg");
    return 1;
  }

  int stub_decoration_delete(lua_State *)
  {
    return 0;
  }

  int stub_decoration_set(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    DecoSpec spec;
    auto int_of = [&](const char *key)
    {
      lua_getfield(L, 2, key);
      const int value = (int)lua_tointeger(L, -1);
      lua_pop(L, 1);
      return value;
    };
    auto text_of = [&](const char *key)
    {
      lua_getfield(L, 2, key);
      const std::string value = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
      lua_pop(L, 1);
      return value;
    };
    spec.row = int_of("row");
    spec.col = int_of("col");
    spec.width = int_of("width");
    spec.priority = int_of("priority");
    spec.fg = text_of("fg");
    spec.bg = text_of("bg");
    g.specs.push_back(std::move(spec));
    lua_pushinteger(L, (lua_Integer)g.specs.size());
    return 1;
  }

  int stub_buffer_current(lua_State *L)
  {
    lua_pushinteger(L, g.current_buffer);
    return 1;
  }

  int stub_buffer_current_file(lua_State *L)
  {
    lua_pushstring(L, g.path.c_str());
    return 1;
  }

  int stub_buffer_get_text(lua_State *L)
  {
    g.get_text_calls++;
    lua_pushstring(L, g.text.c_str());
    return 1;
  }

  int stub_buffer_get_line(lua_State *L)
  {
    g.get_line_calls++;
    const lua_Integer index = luaL_checkinteger(L, 1);
    // The buffer's own split: a trailing newline ends the last line and does
    // not leave an empty one behind (the reader uses std::getline).
    std::vector<std::string> lines;
    size_t at = 0;
    while (true)
    {
      const size_t end = g.text.find('\n', at);
      const std::string line =
          g.text.substr(at, end == std::string::npos ? std::string::npos : end - at);
      lines.push_back(line);
      if (end == std::string::npos)
        break;
      at = end + 1;
      if (at >= g.text.size())
        break;
    }
    if (index < 1 || (size_t)index > lines.size())
      lua_pushnil(L);
    else
      lua_pushstring(L, lines[(size_t)index - 1].c_str());
    return 1;
  }

  int stub_cursor_get(lua_State *L)
  {
    lua_pushinteger(L, g.cursor_line);
    return 1;
  }

  int stub_cursor_set(lua_State *L)
  {
    g.cursor_line = (int)luaL_checkinteger(L, 1);
    g.cursor_col = (int)luaL_checkinteger(L, 2);
    return 0;
  }

  int stub_set_timeout(lua_State *)
  {
    return 0;
  }

  int stub_file_read(lua_State *L)
  {
    lua_pushnil(L);
    return 1;
  }

  int stub_file_open(lua_State *)
  {
    return 0;
  }

  int stub_workspace_search(lua_State *L)
  {
    const char *query = luaL_checkstring(L, 1);
    lua_newtable(L);
    int count = 0;
    for (const SearchHit &hit : g.search_hits)
    {
      if (hit.text.find(query) == std::string::npos)
        continue;
      lua_newtable(L);
      lua_pushstring(L, hit.path.c_str());
      lua_setfield(L, -2, "path");
      lua_pushstring(L, hit.relative.c_str());
      lua_setfield(L, -2, "relative_path");
      lua_pushstring(L, hit.text.c_str());
      lua_setfield(L, -2, "text");
      lua_pushinteger(L, hit.line);
      lua_setfield(L, -2, "line");
      lua_pushinteger(L, 1);
      lua_setfield(L, -2, "column");
      lua_rawseti(L, -2, ++count);
    }
    return 1;
  }

  void push_stub_jot(lua_State *L)
  {
    lua_newtable(L); // jot
    lua_newtable(L);
    lua_pushcfunction(L, stub_config_get);
    lua_setfield(L, -2, "get");
    lua_setfield(L, -2, "config");
    lua_pushcfunction(L, stub_autocmd);
    lua_setfield(L, -2, "autocmd");
    lua_pushcfunction(L, stub_command);
    lua_setfield(L, -2, "command");
    lua_pushcfunction(L, stub_notify);
    lua_setfield(L, -2, "notify");
    lua_newtable(L);
    lua_pushcfunction(L, stub_keymap_set);
    lua_setfield(L, -2, "set");
    lua_setfield(L, -2, "keymap");
    lua_newtable(L);
    lua_pushcfunction(L, stub_syntax_highlight);
    lua_setfield(L, -2, "highlight");
    lua_setfield(L, -2, "syntax");
    lua_newtable(L);
    lua_pushcfunction(L, stub_theme_get);
    lua_setfield(L, -2, "get");
    lua_setfield(L, -2, "theme");
    lua_newtable(L);
    lua_pushcfunction(L, stub_decoration_set);
    lua_setfield(L, -2, "set");
    lua_pushcfunction(L, stub_decoration_delete);
    lua_setfield(L, -2, "delete");
    lua_setfield(L, -2, "decoration");
    lua_newtable(L);
    lua_pushcfunction(L, stub_buffer_current);
    lua_setfield(L, -2, "current");
    lua_pushcfunction(L, stub_buffer_current_file);
    lua_setfield(L, -2, "current_file");
    lua_pushcfunction(L, stub_buffer_get_text);
    lua_setfield(L, -2, "get_text");
    lua_pushcfunction(L, stub_buffer_get_line);
    lua_setfield(L, -2, "get_line");
    lua_setfield(L, -2, "buffer");
    lua_newtable(L);
    lua_pushcfunction(L, stub_cursor_get);
    lua_setfield(L, -2, "get");
    lua_pushcfunction(L, stub_cursor_set);
    lua_setfield(L, -2, "set");
    lua_setfield(L, -2, "cursor");
    lua_pushcfunction(L, stub_set_timeout);
    lua_setfield(L, -2, "set_timeout");
    lua_newtable(L);
    lua_pushcfunction(L, stub_file_open);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, stub_file_read);
    lua_setfield(L, -2, "read");
    lua_setfield(L, -2, "file");
    lua_newtable(L);
    lua_pushcfunction(L, stub_workspace_search);
    lua_setfield(L, -2, "search");
    lua_setfield(L, -2, "workspace");
    lua_setglobal(L, "jot");
  }

  // Loads the feature into a fresh state (the stub `jot` is installed first).
  // The module table is left at stack index 1 for the rest of the test.
  lua_State *load_module()
  {
    lua_State *L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    push_stub_jot(L);
    const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/todo_comments.lua";
    REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
    REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
    REQUIRE(lua_istable(L, -1));
    return L;
  }

  void push_module_fn(lua_State *L, const char *name)
  {
    lua_getfield(L, 1, name);
    REQUIRE(lua_isfunction(L, -1));
  }

  void invoke_event(lua_State *L, const std::string &event)
  {
    auto it = g.handlers.find(event);
    REQUIRE(it != g.handlers.end());
    REQUIRE(it->second != LUA_NOREF);
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
    REQUIRE(lua_pcall(L, 0, 0, 0) == LUA_OK);
  }

  std::string field_text(lua_State *L, int index, const char *key)
  {
    lua_getfield(L, index, key);
    const std::string value = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    return value;
  }

  int field_int(lua_State *L, int index, const char *key)
  {
    lua_getfield(L, index, key);
    const int value = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return value;
  }

  std::vector<DecoSpec> read_specs(lua_State *L, int index)
  {
    std::vector<DecoSpec> out;
    REQUIRE(lua_istable(L, index));
    const int count = (int)lua_rawlen(L, index);
    for (int i = 1; i <= count; i++)
    {
      lua_rawgeti(L, index, i);
      DecoSpec spec;
      spec.row = field_int(L, -1, "row");
      spec.col = field_int(L, -1, "col");
      spec.width = field_int(L, -1, "width");
      spec.priority = field_int(L, -1, "priority");
      spec.fg = field_text(L, -1, "fg");
      spec.bg = field_text(L, -1, "bg");
      out.push_back(std::move(spec));
      lua_pop(L, 1);
    }
    return out;
  }

  int push_keywords_and_matcher(lua_State *L, int &matcher)
  {
    // Leaves the keyword map below the matcher; returns the keyword map index.
    push_module_fn(L, "configured_keywords");
    REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
    const int keywords = lua_gettop(L);
    push_module_fn(L, "compile_matcher");
    lua_pushvalue(L, keywords);
    REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
    matcher = lua_gettop(L);
    return keywords;
  }

  struct MatchResult
  {
    bool hit = false;
    std::string keyword;
    std::string family;
    int start0 = 0; // keyword start, 0-based
    int colon0 = 0; // the colon's offset, 0-based
  };

  MatchResult call_match_line(lua_State *L, const std::string &line)
  {
    MatchResult out;
    const int base = lua_gettop(L);
    int matcher = 0;
    const int keywords = push_keywords_and_matcher(L, matcher);
    push_module_fn(L, "match_line");
    lua_pushstring(L, line.c_str());
    lua_pushvalue(L, matcher);
    lua_pushvalue(L, keywords);
    REQUIRE(lua_pcall(L, 3, 4, 0) == LUA_OK);
    if (lua_isstring(L, -4))
    {
      out.hit = true;
      out.keyword = lua_tostring(L, -4);
      out.family = lua_tostring(L, -3);
      out.start0 = (int)lua_tointeger(L, -2);
      out.colon0 = (int)lua_tointeger(L, -1);
    }
    lua_settop(L, base);
    return out;
  }

  using Accents = std::map<std::string, std::pair<std::string, std::string>>; // fg, ink

  // `conceal` is the ink the colon is painted in; the default is the dark
  // normal background a family accent resolves to in these tests. An empty
  // string sends nil, which is the no-conceal fallback.
  // A closure over an index upvalue, the shape the editor's own scan uses in
  // place of a table of every line.
  int iter_lines(lua_State *L)
  {
    const int at = (int)lua_tointeger(L, lua_upvalueindex(1));
    const auto *lines = (const std::vector<std::string> *)lua_touserdata(L, lua_upvalueindex(2));
    if (at >= (int)lines->size())
    {
      lua_pushnil(L);
      return 1;
    }
    lua_pushinteger(L, at + 1);
    lua_replace(L, lua_upvalueindex(1));
    lua_pushinteger(L, at + 1);
    lua_pushstring(L, (*lines)[(size_t)at].c_str());
    return 2;
  }

  void push_lines_iter(lua_State *L, const std::vector<std::string> &lines)
  {
    lua_pushinteger(L, 0);
    lua_pushlightuserdata(L, (void *)&lines);
    lua_pushcclosure(L, iter_lines, 2);
  }

  std::vector<DecoSpec> call_scan_lines(lua_State *L,
                                        const std::vector<std::string> &lines,
                                        const std::string &ext,
                                        const Accents &accents,
                                        const std::string &conceal = "#07060e",
                                        bool as_iterator = false)
  {
    const int base = lua_gettop(L);
    int matcher = 0;
    const int keywords = push_keywords_and_matcher(L, matcher);
    push_module_fn(L, "scan_lines");
    if (as_iterator)
    {
      push_lines_iter(L, lines);
    }
    else
    {
      lua_newtable(L);
      for (size_t i = 0; i < lines.size(); i++)
      {
        lua_pushstring(L, lines[i].c_str());
        lua_rawseti(L, -2, (int)i + 1);
      }
    }
    lua_pushstring(L, ext.c_str());
    lua_pushvalue(L, keywords);
    lua_pushvalue(L, matcher);
    lua_newtable(L);
    for (const auto &entry : accents)
    {
      lua_newtable(L);
      lua_pushstring(L, entry.second.first.c_str());
      lua_setfield(L, -2, "fg");
      lua_pushstring(L, entry.second.second.c_str());
      lua_setfield(L, -2, "ink");
      lua_setfield(L, -2, entry.first.c_str());
    }
    if (conceal.empty())
      lua_pushnil(L);
    else
      lua_pushstring(L, conceal.c_str());
    REQUIRE(lua_pcall(L, 6, 1, 0) == LUA_OK);
    std::vector<DecoSpec> specs = read_specs(L, -1);
    lua_settop(L, base);
    return specs;
  }
} // namespace

TEST_CASE("Bundled todo comments prefers the longest keyword at a word boundary")
{
  g = StubState{};
  lua_State *L = load_module();

  // The default keyword map comes from upstream: each family's keyword and its
  // aliases land on the family's colour name.
  push_module_fn(L, "configured_keywords");
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(field_text(L, -1, "FIX") == "error");
  REQUIRE(field_text(L, -1, "FIXME") == "error");
  REQUIRE(field_text(L, -1, "BUG") == "error");
  REQUIRE(field_text(L, -1, "TODO") == "info");
  REQUIRE(field_text(L, -1, "HACK") == "warning");
  REQUIRE(field_text(L, -1, "WARN") == "warning");
  REQUIRE(field_text(L, -1, "WARNING") == "warning");
  REQUIRE(field_text(L, -1, "PERF") == "default");
  REQUIRE(field_text(L, -1, "OPTIM") == "default");
  REQUIRE(field_text(L, -1, "NOTE") == "hint");
  REQUIRE(field_text(L, -1, "INFO") == "hint");
  REQUIRE(field_text(L, -1, "TEST") == "test");
  lua_pop(L, 1);

  // A custom keyword joins the default family (upstream merge_keywords).
  g.config_overrides["todo_comments_keywords"] = "SECURITY,REVIEW";
  push_module_fn(L, "configured_keywords");
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(field_text(L, -1, "SECURITY") == "default");
  REQUIRE(field_text(L, -1, "REVIEW") == "default");
  lua_pop(L, 1);
  g.config_overrides.erase("todo_comments_keywords");

  // FIXME is preferred over FIX when both match; the boundary keeps a word
  // like `mytodo:` from being a hit.
  const MatchResult fixme = call_match_line(L, "// FIXME: broken");
  REQUIRE(fixme.hit);
  REQUIRE(fixme.keyword == "FIXME");
  REQUIRE(fixme.family == "error");
  REQUIRE(call_match_line(L, "// FIX: done").keyword == "FIX");
  REQUIRE_FALSE(call_match_line(L, "mytodo: not a keyword").hit);
  REQUIRE_FALSE(call_match_line(L, "// NOTODO: nope").hit);
  REQUIRE_FALSE(call_match_line(L, "// TODO without a colon").hit);
  REQUIRE_FALSE(call_match_line(L, "").hit);

  // The match reports the keyword start and the colon's offset, both 0-based:
  // the band is drawn around the keyword, and the colon is left out of it.
  const MatchResult band = call_match_line(L, "// TODO: fix");
  REQUIRE(band.hit);
  REQUIRE(band.start0 == 3);
  REQUIRE(band.colon0 == 7);
  const MatchResult unicode = call_match_line(L, "// TODO:\xc3\xa9");
  REQUIRE(unicode.hit);
  REQUIRE(unicode.colon0 == unicode.start0 + 4);

  lua_close(L);
}

TEST_CASE("Bundled todo comments only bands inside comments")
{
  g = StubState{};
  lua_State *L = load_module();

  const Accents accents = {
      {"error", {"#cc7f86", "#07060e"}},
      {"info", {"#7b98c2", "#07060e"}},
  };

  // The chip is the keyword alone: the `//` and the space before the word are
  // untouched, the colon is hidden in the theme's background ink, and the text
  // after it takes the family fg.
  const std::string code = "int x = 1; // TODO: fix it";
  g.spans[code] = {{11, (int)code.size() - 11, "comment"}};
  std::vector<DecoSpec> specs = call_scan_lines(L, {code}, ".c", accents);
  REQUIRE(specs.size() == 3);
  REQUIRE(specs[0].row == 1);
  REQUIRE(specs[0].col == 15);
  REQUIRE(specs[0].width == 4); // "TODO", the pad and the colon excluded
  REQUIRE(specs[0].bg == "#7b98c2");
  REQUIRE(specs[0].fg == "#07060e");
  REQUIRE(specs[0].priority == 8);
  // The colon stays a real character - it is painted, not removed, so it can be
  // backspaced and the paint goes away with it.
  REQUIRE(specs[1].col == 19);
  REQUIRE(specs[1].width == 1);
  REQUIRE(specs[1].bg.empty());
  REQUIRE(specs[1].fg == "#07060e");
  REQUIRE(specs[2].col == 20);
  REQUIRE(specs[2].width == 7); // " fix it", colon excluded
  REQUIRE(specs[2].bg.empty());
  REQUIRE(specs[2].fg == "#7b98c2");
  REQUIRE(specs[2].priority == 7);

  // A theme whose normal group carries no background leaves the colon as the
  // comment paints it, rather than hiding it in an invented colour.
  const std::vector<DecoSpec> unconcealed = call_scan_lines(L, {code}, ".c", accents, "");
  REQUIRE(unconcealed.size() == 2);
  REQUIRE(unconcealed[0].bg == "#7b98c2");
  REQUIRE(unconcealed[1].col == 20);

  // A keyword inside a string is not a comment: the spans name the string and
  // no comment covers the hit, so nothing is painted. Nothing is looked for
  // past it either (the matcher lands on the first hit only), which is the
  // same line upstream keeps with its tree-sitter comment gate.
  const std::string quoted = "char *s = \"// FIXME: no\";";
  g.spans[quoted] = {{10, (int)quoted.size() - 10, "string"}};
  REQUIRE(call_scan_lines(L, {quoted}, ".c", accents).empty());

  // A real comment later on the same line is banded when it is the hit the
  // matcher lands on, and the chip is the keyword, not the string or the
  // comment marker.
  const std::string mixed = "char *s = \"plain\"; // TODO: yes";
  g.spans[mixed] = {
      {10, 7, "string"},
      {19, (int)mixed.size() - 19, "comment"},
  };
  specs = call_scan_lines(L, {mixed}, ".c", accents);
  REQUIRE(specs.size() == 3);
  REQUIRE(specs[0].col == 23); // the keyword, not the comment
  REQUIRE(specs[0].width == 4);
  REQUIRE(specs[0].bg == "#7b98c2");
  REQUIRE(specs[1].col == 27); // the hidden colon
  REQUIRE(specs[1].width == 1);
  REQUIRE(specs[2].col == 28); // "yes" after the colon
  REQUIRE(specs[2].width == 4);

  // The colour run carries into the following comment lines (upstream's
  // multiline) and dies with the comment.
  const std::vector<std::string> block = {
      "-- TODO: first",
      "--   still the same comment",
      "local x = 1",
  };
  g.spans.erase(block[0]);
  specs = call_scan_lines(L, block, ".lua", accents);
  REQUIRE(specs.size() == 4);
  REQUIRE(specs[0].bg == "#7b98c2");
  REQUIRE(specs[0].col == 4); // the keyword, not the comment
  REQUIRE(specs[0].width == 4);
  REQUIRE(specs[1].col == 8); // the hidden colon
  REQUIRE(specs[1].width == 1);
  REQUIRE(specs[2].row == 1);
  REQUIRE(specs[2].col == 9); // "first" after "-- TODO: "
  REQUIRE(specs[2].width == 6);
  REQUIRE(specs[2].fg == "#7b98c2");
  REQUIRE(specs[2].bg.empty());
  REQUIRE(specs[3].row == 2);
  REQUIRE(specs[3].col == 1);
  REQUIRE(specs[3].width == (int)block[1].size());
  REQUIRE(specs[3].fg == "#7b98c2");
  REQUIRE(specs[3].bg.empty());

  // An extension the syntax fallback has no rules for fails open: the keyword
  // is banded anyway, as upstream does without a parser.
  g.no_rules = true;
  const std::string plain = "plain text TODO: anywhere";
  specs = call_scan_lines(L, {plain}, ".md", accents);
  REQUIRE(specs.size() == 3);
  REQUIRE(specs[0].col == 12); // the keyword
  REQUIRE(specs[0].width == 4);
  REQUIRE(specs[0].bg == "#7b98c2");
  REQUIRE(specs[1].col == 16); // the hidden colon
  REQUIRE(specs[1].width == 1);
  REQUIRE(specs[2].col == 17);
  REQUIRE(specs[2].width == 9); // " anywhere"

  // The fail-open band stays on its own line. Nothing here says the line below
  // belongs to the comment, so the carry stops: otherwise every TODO in a file
  // type the fallback does not know re-inks the code under it in the family
  // colour, which reads as the whole next line being highlighted.
  const std::vector<std::string> plain_block = {
      "TODO: one",
      "this line keeps its own ink",
  };
  specs = call_scan_lines(L, plain_block, ".txt", accents);
  REQUIRE(specs.size() == 3);
  for (const DecoSpec &spec : specs)
    REQUIRE(spec.row == 1);
  g.no_rules = false;

  lua_close(L);
}

TEST_CASE("Bundled todo comments resolves family colours from the theme and paints")
{
  g = StubState{};
  g.text = "int x = 1; // TODO: fix it\nstatic int y; // FIXME: broken\n";
  g.path = "/tmp/probe.c";
  g.theme = {
      {"diagnostic_error", "#cc7f86"},
      {"diagnostic_warning", "#b8a06a"},
      {"diagnostic_info", "#7b98c2"},
      {"diagnostic_hint", "#6fa886"},
      {"type", "#9689c4"},
      {"keyword", "#c96f9c"},
  };
  lua_State *L = load_module();

  REQUIRE(g.commands == std::vector<std::string>{"TodoNext", "TodoPrev", "Todo"});
  // The jump chord joins the Alt+]/Alt+[ family, but is registered here because
  // the commands are this feature's own (see the comment in keymaps.lua).
  REQUIRE(g.keymaps
          == std::vector<std::pair<std::string, std::string>>{
              {"Alt+] t", ":TodoNext"},
              {"Alt+[ t", ":TodoPrev"},
          });
  REQUIRE(g.handlers.count("CursorMoved") == 1);
  REQUIRE(g.handlers.count("BufChange") == 1);
  REQUIRE(g.handlers.count("BufOpen") == 1);

  invoke_event(L, "BufOpen");

  // Two comments, each a chip, a hidden colon and the text after it. The
  // band's ink is the theme's dark normal background on these mid-light
  // diagnostic colours (maximize_contrast), and the family fg is the theme's
  // group colour.
  REQUIRE(g.specs.size() == 6);
  REQUIRE(g.specs[0].row == 1);
  REQUIRE(g.specs[0].col == 15);
  REQUIRE(g.specs[0].width == 4);
  REQUIRE(g.specs[0].bg == "#7b98c2"); // DiagnosticInfo
  REQUIRE(g.specs[0].fg == "#07060e");
  REQUIRE(g.specs[1].row == 1);
  REQUIRE(g.specs[1].col == 19); // the hidden colon
  REQUIRE(g.specs[1].width == 1);
  REQUIRE(g.specs[1].fg == "#07060e");
  REQUIRE(g.specs[2].row == 1);
  REQUIRE(g.specs[2].col == 20);
  REQUIRE(g.specs[2].fg == "#7b98c2");
  REQUIRE(g.specs[2].bg.empty());
  REQUIRE(g.specs[3].row == 2);
  REQUIRE(g.specs[3].col == 18);
  REQUIRE(g.specs[3].width == 5);
  REQUIRE(g.specs[3].bg == "#cc7f86"); // DiagnosticError (FIXME)
  REQUIRE(g.specs[3].fg == "#07060e");
  REQUIRE(g.specs[4].row == 2);
  REQUIRE(g.specs[4].col == 23); // the hidden colon
  REQUIRE(g.specs[4].width == 1);
  REQUIRE(g.specs[4].fg == "#07060e");
  REQUIRE(g.specs[5].row == 2);
  REQUIRE(g.specs[5].col == 24);
  REQUIRE(g.specs[5].fg == "#cc7f86");

  // A group the theme does not name is left unpainted rather than invented.
  g.specs.clear();
  g.theme.erase("diagnostic_info");
  g.text = "// TODO: no colour\n";
  lua_close(L);
  L = load_module();
  invoke_event(L, "BufOpen");
  REQUIRE(g.specs.empty());

  lua_close(L);
}

TEST_CASE("Bundled todo comments scans a buffer line by line, never as one table")
{
  // The scan used to build a Lua table holding every line of the buffer (one
  // string per line) on every open and every edit, so a 200k-line buffer cost
  // tens of MB. It reads lines one at a time now, and this pins the contract:
  // no whole-buffer crossing, one read per line, and the same specs whether
  // scan_lines is handed an array or the editor's iterator.
  g = StubState{};
  g.text = "local a = 1\n-- TODO: later\nlocal b = 2\n";
  g.path = "/tmp/probe.lua";
  g.theme = {{"diagnostic_info", "#7b98c2"}};
  lua_State *L = load_module();

  invoke_event(L, "BufOpen");

  REQUIRE(g.get_text_calls == 0);
  REQUIRE(g.get_line_calls == 4); // line-1 probe, lines 2 and 3, the nil end
  REQUIRE(g.specs.size() == 3);

  const Accents accents = {{"info", {"#7b98c2", "#07060e"}}};
  const std::vector<std::string> lines = {"// TODO: same", "local x = 1"};
  const std::vector<DecoSpec> from_table = call_scan_lines(L, lines, ".lua", accents);
  const std::vector<DecoSpec> from_iter =
      call_scan_lines(L, lines, ".lua", accents, "#07060e", true);
  REQUIRE(from_iter.size() == from_table.size());
  REQUIRE(from_iter.size() == 3);
  for (size_t i = 0; i < from_iter.size(); i++)
  {
    REQUIRE(from_iter[i].row == from_table[i].row);
    REQUIRE(from_iter[i].col == from_table[i].col);
    REQUIRE(from_iter[i].width == from_table[i].width);
    REQUIRE(from_iter[i].fg == from_table[i].fg);
    REQUIRE(from_iter[i].bg == from_table[i].bg);
  }

  lua_close(L);
}

TEST_CASE("Bundled todo comments readable ink maximizes contrast")
{
  g = StubState{};
  lua_State *L = load_module();

  auto call = [&](const std::string &band)
  {
    push_module_fn(L, "readable_ink");
    lua_pushstring(L, band.c_str());
    lua_newtable(L);
    lua_pushstring(L, g.normal_fg.c_str());
    lua_setfield(L, -2, "fg");
    lua_pushstring(L, g.normal_bg.c_str());
    lua_setfield(L, -2, "bg");
    REQUIRE(lua_pcall(L, 2, 1, 0) == LUA_OK);
    const std::string ink = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    return ink;
  };

  // A pale band takes the dark background ink, a near-black one the light fg.
  REQUIRE(call("#f5e0ff") == "#07060e");
  REQUIRE(call("#181826") == "#c5c0d4");
  // Not a colour: no ink to hand back rather than a guess.
  REQUIRE(call("not-a-colour").empty());

  lua_close(L);
}

TEST_CASE("Bundled todo comments walks the workspace and formats picker rows")
{
  g = StubState{};
  g.theme = {
      {"diagnostic_error", "#cc7f86"},
      {"diagnostic_info", "#7b98c2"},
  };
  lua_State *L = load_module();

  const std::string dup = "int x; // TODO: one";
  const std::string quoted = "char *s = \"// TODO: no\";";
  g.search_hits = {
      {"src/a.c", "src/a.c", dup, 2},
      {"src/a.c", "src/a.c", dup, 2}, // a second keyword's query returns it again
      {"src/a.c", "src/a.c", quoted, 3},
      {"src/b.lua", "src/b.lua", "-- FIXME: broken", 5},
  };
  g.spans[quoted] = {{10, (int)quoted.size() - 10, "string"}};

  push_module_fn(L, "workspace_items");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE((int)lua_rawlen(L, -1) == 2); // deduped, string hit rejected
  lua_rawgeti(L, -1, 1);
  REQUIRE(field_text(L, -1, "relative") == "src/a.c");
  REQUIRE(field_int(L, -1, "line") == 2);
  REQUIRE(field_int(L, -1, "column") == 11);
  REQUIRE(field_text(L, -1, "text") == dup);
  REQUIRE(field_text(L, -1, "family") == "info"); // the row colour comes from here
  lua_pop(L, 1);
  lua_rawgeti(L, -1, 2);
  REQUIRE(field_text(L, -1, "relative") == "src/b.lua");
  REQUIRE(field_int(L, -1, "line") == 5);
  REQUIRE(field_int(L, -1, "column") == 4);
  REQUIRE(field_text(L, -1, "family") == "error");
  lua_pop(L, 1);
  lua_pop(L, 1);

  // `keywords=TODO` keeps only the TODO rows (upstream's TodoTelescope).
  push_module_fn(L, "keyword_filter");
  lua_pushstring(L, "keywords=TODO");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, -1));
  REQUIRE_FALSE(lua_isnil(L, -1));
  const int filter = lua_gettop(L);
  push_module_fn(L, "workspace_items");
  lua_pushvalue(L, filter);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE((int)lua_rawlen(L, -1) == 1);
  lua_rawgeti(L, -1, 1);
  REQUIRE(field_text(L, -1, "relative") == "src/a.c");
  lua_pop(L, 1);
  lua_pop(L, 2); // items, filter

  // The picker row carries the readable line in label and the location in
  // value, so the callback never parses its own display text.
  push_module_fn(L, "workspace_items");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  push_module_fn(L, "picker_rows");
  lua_insert(L, -2); // items under picker_rows
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE((int)lua_rawlen(L, -1) == 2);
  lua_rawgeti(L, -1, 1);
  REQUIRE(field_text(L, -1, "label") == "src/a.c:2  int x; // TODO: one");
  REQUIRE(field_text(L, -1, "value") == "src/a.c\t2\t11\tsrc/a.c");
  // The row wears its family colour and repeats nothing: no detail text on
  // every row and no label again in the footer.
  REQUIRE(field_text(L, -1, "fg") == "#7b98c2");
  REQUIRE(field_text(L, -1, "detail").empty());
  REQUIRE(field_text(L, -1, "preview").empty());
  lua_pop(L, 1);
  lua_rawgeti(L, -1, 2);
  REQUIRE(field_text(L, -1, "fg") == "#cc7f86");
  lua_pop(L, 2);

  // An empty result is reported, not shown as an empty picker.
  g.search_hits.clear();
  push_module_fn(L, "workspace_items");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE((int)lua_rawlen(L, -1) == 0);
  lua_settop(L, 1);

  lua_close(L);
}

TEST_CASE("Bundled todo comments jumps between comments and reports the end")
{
  g = StubState{};
  g.text = "local a = 1\n-- TODO: later\nlocal b = 2\n";
  g.path = "/tmp/probe.lua";
  g.cursor_line = 1;
  lua_State *L = load_module();

  push_module_fn(L, "jump");
  lua_pushinteger(L, 1);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1) == 1);
  lua_pop(L, 1);
  REQUIRE(g.cursor_line == 2);
  REQUIRE(g.cursor_col == 4); // the keyword, not the comment start

  push_module_fn(L, "jump");
  lua_pushinteger(L, 1);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1) == 0); // past the last one
  lua_pop(L, 1);
  REQUIRE(g.notices == std::vector<std::string>{"No more todo comments to jump to"});

  // Backwards from the top wraps nothing: there is no previous comment.
  g.cursor_line = 1;
  g.notices.clear();
  push_module_fn(L, "jump");
  lua_pushinteger(L, -1);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1) == 0);
  lua_pop(L, 1);
  REQUIRE(g.notices.size() == 1);

  lua_close(L);
}
