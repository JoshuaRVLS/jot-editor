// The keymap grammar (runtime/lua/features/keymaps.lua): operators, selection,
// next/previous.
//
// The tables are data, and both failure modes are silent at runtime: a child
// that is never registered means the key does nothing, and a command name that
// does not exist gets swallowed as "Unknown command" in the statusline. So this
// loads the real file against a stub `jot` (the same trick the UI-kit test uses),
// asserts the tree which-key will render, runs the operator actions to see which
// commands they invoke, and then checks every one of those names against a real
// editor.
#include "editor.h"
#include "input/commands/utils.h"
#include "jot/lua/api.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

// The Lua C headers are wrapped the way the engine wraps them (api_internal.h):
// included bare, the declarations come out C++-mangled and nothing links.
extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  struct KeymapRecord
  {
    std::string chords;
    bool has_function = false;
    std::string command; // for string actions (":" prefix stripped)
    std::string detail;
    int function_ref = LUA_NOREF; // registry ref, for calling the action
  };

  struct Grammar
  {
    std::vector<KeymapRecord> entries;
    std::vector<std::string> commands_run; // recorded by the stub jot.command
  };

  Grammar g;
  std::string g_last_lua_error; // kept outside the helper so REQUIRE can report it

  const KeymapRecord *find(const std::string &chords)
  {
    for (const auto &entry : g.entries)
    {
      if (entry.chords == chords)
      {
        return &entry;
      }
    }
    return nullptr;
  }

  int stub_command(lua_State *L)
  {
    const char *text = luaL_checkstring(L, 1);
    g.commands_run.push_back(text ? text : "");
    return 0;
  }

  int stub_keymap_set(lua_State *L)
  {
    // jot.keymap.set(chords, action, detail)
    KeymapRecord record;
    record.chords = luaL_checkstring(L, 1);
    if (lua_isfunction(L, 2))
    {
      record.has_function = true;
      lua_pushvalue(L, 2);
      record.function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    else if (lua_isstring(L, 2))
    {
      std::string command = lua_tostring(L, 2);
      if (!command.empty() && command[0] == ':')
      {
        command.erase(0, 1);
      }
      record.command = command;
    }
    if (lua_isstring(L, 3))
    {
      record.detail = lua_tostring(L, 3);
    }
    g.entries.push_back(record);
    return 0;
  }

  int stub_noop(lua_State *)
  {
    return 0;
  }

  // The shipped file also binds keys to functions that call the editor APIs
  // directly (the debugger keys), so the stub has to answer for those too -- the
  // point is to be able to call every action that is registered.
  int stub_returns_empty_string(lua_State *L)
  {
    lua_pushstring(L, "");
    return 1;
  }

  int stub_returns_one(lua_State *L)
  {
    lua_pushinteger(L, 1);
    return 1;
  }

  void push_stub_jot(lua_State *L)
  {
    lua_newtable(L); // jot
    lua_newtable(L); // jot.keymap
    lua_pushcfunction(L, stub_keymap_set);
    lua_setfield(L, -2, "set");
    lua_setfield(L, -2, "keymap");
    lua_pushcfunction(L, stub_command);
    lua_setfield(L, -2, "command");
    lua_newtable(L); // jot.buffer
    lua_pushcfunction(L, stub_returns_empty_string);
    lua_setfield(L, -2, "current_file");
    lua_pushcfunction(L, stub_returns_one);
    lua_setfield(L, -2, "cursor");
    lua_setfield(L, -2, "buffer");
    lua_newtable(L); // jot.debugger
    for (const char *name : {"toggle_breakpoint", "scroll_output", "cycle_thread", "cycle_frame"})
    {
      lua_pushcfunction(L, stub_noop);
      lua_setfield(L, -2, name);
    }
    lua_setfield(L, -2, "debugger");
    lua_setglobal(L, "jot");
  }

  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_keymap_grammar_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // Loads the shipped keymap tables against the stub. Returns false when the
  // file cannot be loaded (the caller fails, rather than skipping: this test
  // needs no grammar and no display).
  // Calls a keymap action, reporting the Lua error rather than just the pcall
  // code (a failure here means the action itself threw).
  bool call_action(lua_State *L, int ref)
  {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
      const char *message = lua_tostring(L, -1);
      g_last_lua_error = message ? message : "(non-string lua error)";
      lua_pop(L, 1);
      return false;
    }
    return true;
  }

  bool load_grammar(lua_State *L)
  {
    g = Grammar{};
    luaL_openlibs(L);
    push_stub_jot(L);
    const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/keymaps.lua";
    if (luaL_loadfile(L, path.c_str()) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
      return false;
    }
    return true;
  }
} // namespace

TEST_CASE("The keymap grammar registers the menus which-key renders", "[jot][keymap]")
{
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  REQUIRE(load_grammar(L));

  // Prefixes exist as group titles (no action, a label).
  for (const char *prefix : {"Alt+D", "Alt+Y", "Alt+V", "Alt+]", "Alt+["})
  {
    const KeymapRecord *entry = find(prefix);
    REQUIRE(entry != nullptr);
    REQUIRE_FALSE(entry->has_function);
    REQUIRE(entry->command.empty());
    REQUIRE_FALSE(entry->detail.empty());
  }

  // The operator grammar: verb, then inside/around, then the object.
  const KeymapRecord *del_inside = find("Alt+D i");
  REQUIRE(del_inside != nullptr);
  REQUIRE(del_inside->detail.find("inside") != std::string::npos);
  for (const char *chords : {"Alt+D i f",
                             "Alt+D a f",
                             "Alt+D i c",
                             "Alt+D a c",
                             "Alt+D i a",
                             "Alt+D a a",
                             "Alt+D w",
                             "Alt+D l"})
  {
    REQUIRE(find(chords) != nullptr);
  }
  // ...and the yank operator mirrors it, so the grammar is learnt once.
  for (const char *chords : {"Alt+Y i f", "Alt+Y a f", "Alt+Y w", "Alt+Y l"})
  {
    REQUIRE(find(chords) != nullptr);
  }

  // Selection and next/previous.
  for (const char *chords : {"Alt+V e",
                             "Alt+V c",
                             "Alt+V k",
                             "Alt+V r",
                             "Alt+V b",
                             "Alt+V a",
                             "Alt+V l",
                             "Alt+V m",
                             "Alt+V s f"})
  {
    REQUIRE(find(chords) != nullptr);
  }
  for (const char *chords : {"Alt+] f", "Alt+] c", "Alt+] d", "Alt+[ f", "Alt+[ c", "Alt+[ d"})
  {
    REQUIRE(find(chords) != nullptr);
  }

  lua_close(L);
}

TEST_CASE("Every command a keymap names exists", "[jot][keymap]")
{
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  REQUIRE(load_grammar(L));

  // The chords this grammar owns. The shipped file also binds the debugger keys,
  // and those are not what is under test here.
  const auto in_grammar = [](const std::string &chords)
  {
    for (const char *prefix : {"Alt+D", "Alt+Y", "Alt+V", "Alt+]", "Alt+["})
    {
      if (chords.rfind(prefix, 0) == 0)
      {
        return true;
      }
    }
    return false;
  };

  // Names are resolved against the ex-command list rather than by running them:
  // :yankselection and :deleteselection write the system clipboard, which spawns
  // a helper that outlives the test and holds its stdout open.
  const auto &known = CommandLineUtils::ex_commands();
  const auto is_known = [&known](const std::string &name)
  { return std::find(known.begin(), known.end(), name) != known.end(); };

  std::vector<std::string> named;
  for (const auto &entry : g.entries)
  {
    if (!in_grammar(entry.chords))
    {
      continue;
    }
    if (!entry.command.empty())
    {
      named.push_back(entry.command);
    }
  }
  REQUIRE_FALSE(named.empty());

  // Function actions (the operators) run against a stubbed jot.command, so their
  // sequence is recorded rather than executed.
  for (const auto &entry : g.entries)
  {
    if (!entry.has_function || !in_grammar(entry.chords))
    {
      continue;
    }
    REQUIRE(call_action(L, entry.function_ref));
  }
  REQUIRE_FALSE(g.commands_run.empty());
  for (const auto &command : g.commands_run)
  {
    named.push_back(command);
  }

  for (const auto &command : named)
  {
    // A typo here is a key that silently does nothing, and the list is what the
    // palette and :help complete from.
    // The grammar writes arguments after the verb ("textobject inside function"),
    // and the list holds verbs.
    const std::string verb = command.substr(0, command.find(' '));
    INFO("keymap command: [" << command << "] verb: " << verb);
    REQUIRE(is_known(verb));
  }

  lua_close(L);
}

TEST_CASE("The selection commands the grammar calls work end to end", "[jot][keymap]")
{
  // The names above are checked statically; this drives the ones that do not
  // touch the clipboard through a real editor, so the plumbing is not assumed.
  Editor &e = probe_editor();
  e.host().core.set_buffer_content("alpha beta gamma\nsecond line\n");
  e.host().core.set_cursor(0, 0);

  e.run_ex_for_test(":selectword");
  REQUIRE(e.host().core.selected_text() == "alpha");

  e.host().core.set_cursor(1, 3);
  e.run_ex_for_test(":selectline");
  REQUIRE(e.host().core.selected_text() == "second line");

  // A textobject command on a buffer with no file is handled, not fatal: there is
  // no extension to pick a grammar with, so it reports and changes nothing. (The
  // statusline text is not assertable here: set_message deliberately stops
  // populating it once the Lua statusline owns the line, so behaviour is what
  // this checks.)
  const std::string before = e.host().core.buffer_content();
  e.run_ex_for_test(":textobject around function");
  REQUIRE(e.host().core.buffer_content() == before);
}

TEST_CASE("Operator leaves name the object and the verb", "[jot][keymap]")
{
  // Leaf actions are command strings, not closures: the keymap API is built for
  // those (every documented example is one), they keep the tables pure data, and
  // a function action in a *multi-chord* leaf does not fire at all -- see the
  // note in the commit. So the spelling of each command is the contract.
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  REQUIRE(load_grammar(L));

  const KeymapRecord *leaf = find("Alt+D a f");
  REQUIRE(leaf != nullptr);
  REQUIRE_FALSE(leaf->has_function);
  REQUIRE(leaf->command == "deleteobject around function");

  leaf = find("Alt+D i c");
  REQUIRE(leaf != nullptr);
  REQUIRE(leaf->command == "deleteobject inside class");

  leaf = find("Alt+Y w");
  REQUIRE(leaf != nullptr);
  REQUIRE(leaf->command == "yankobject word");

  leaf = find("Alt+V m");
  REQUIRE(leaf != nullptr);
  REQUIRE(leaf->command == "selectoccurrences");

  lua_close(L);
}

TEST_CASE("The grammar resolves through the which-key path", "[jot][keymap]")
{
  // What which-key does: register the tables into the real keymap store and ask
  // for the children of a prefix. This is the data the panel renders *and* the
  // path its input handler walks, so a gap here is a key that opens a menu and
  // then does nothing.
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  REQUIRE(load_grammar(L));

  LuaAPI api(nullptr);
  for (const auto &entry : g.entries)
  {
    api.register_keymap(entry.chords, "", "", entry.detail, "global");
  }

  REQUIRE(api.plugin_keymap_is_prefix("Alt+D", "editor"));
  const auto top = api.plugin_keymap_children("Alt+D", "editor");
  std::vector<std::string> keys;
  for (const auto &child : top)
  {
    keys.push_back(child.key);
  }
  // Lowercase, because that is what the user types and what the panel should
  // show; the lookup is case-insensitive on the pressing side.
  for (const char *expected : {"i", "a", "w", "l"})
  {
    INFO("children of Alt+D: " << keys.size());
    REQUIRE(std::find(keys.begin(), keys.end(), expected) != keys.end());
  }

  // Three levels deep: verb -> inside/around -> object, and the leaf is a real
  // registration rather than another group.
  REQUIRE(api.plugin_keymap_children("Alt+D a", "editor").size() == 3);
  REQUIRE(api.plugin_keymap_children("Alt+D a f", "editor").empty());

  // The code-navigation family, which is the same shape (prefix, then a letter
  // per lookup). Each child has to reach a real ex command: a typo here is a key
  // that opens the group and then does nothing.
  REQUIRE(api.plugin_keymap_is_prefix("Alt+C", "editor"));
  const std::map<std::string, std::string> code_leaves = {
      {"d", "gd"},
      {"c", "lspdecl"},
      {"t", "lsptypedef"},
      {"i", "lspimpl"},
      {"h", "switchheader"},
      {"r", "lsprefs"},
      {"n", "lsprename"},
      {"a", "lspactions"},
      {"s", "symbols"},
      {"w", "wsymbols"},
      {"k", "hover"},
  };
  std::vector<std::string> code_keys;
  for (const auto &child : api.plugin_keymap_children("Alt+C", "editor"))
  {
    code_keys.push_back(child.key);
  }
  for (const auto &leaf : code_leaves)
  {
    INFO("leaf Alt+C " << leaf.first << " -> :" << leaf.second);
    REQUIRE(std::find(code_keys.begin(), code_keys.end(), leaf.first) != code_keys.end());
    const KeymapRecord *record = find("Alt+C " + leaf.first);
    REQUIRE(record != nullptr);
    REQUIRE(record->command == leaf.second);
  }
  REQUIRE(code_keys.size() == code_leaves.size());

  lua_close(L);
}
