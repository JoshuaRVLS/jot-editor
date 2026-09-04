// Lua git payloads + commands: repository info/status pushers (with the
// xy-flag decoder and absolute-path helpers), stage/unstage/commit/refresh
// commands and the git-diff panel opener. Split out of api_core.cpp.
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"

#include <filesystem>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace fs = std::filesystem;

static std::string lua_abs_path(const std::string &path)
{
  std::error_code ec;
  fs::path p = path;
  if (!p.is_absolute())
  {
    p = fs::absolute(p, ec);
  }
  return p.lexically_normal().string();
}

static void lua_git_xy_flags(const std::string &xy,
                             bool &staged,
                             bool &unstaged,
                             bool &untracked,
                             bool &deleted,
                             bool &renamed,
                             bool &conflict)
{
  conflict = xy == "DD" || xy == "AU" || xy == "UD" || xy == "UA" || xy == "DU" || xy == "AA"
             || xy == "UU" || xy.find('U') != std::string::npos;
  untracked = (xy == "??");
  const char a = xy.size() > 0 ? xy[0] : ' ';
  const char b = xy.size() > 1 ? xy[1] : ' ';
  staged = !conflict && !untracked && a != ' ' && a != '?';
  unstaged = !conflict && !untracked && b != ' ' && b != '?';
  deleted = (a == 'D' || b == 'D');
  renamed = (a == 'R' || b == 'R');
}

static void lua_push_git_entry(lua_State *L,
                               const std::string &root,
                               const std::string &abs_path,
                               const std::string &xy)
{
  lua_newtable(L);
  lua_push_str_field(L, "path", abs_path);
  std::string rel = abs_path;
  if (abs_path.size() > root.size() && abs_path.compare(0, root.size(), root) == 0
      && abs_path[root.size()] == '/')
  {
    rel = abs_path.substr(root.size() + 1);
  }
  else if (abs_path == root)
  {
    rel = "";
  }
  lua_push_str_field(L, "rel", rel);
  lua_push_str_field(L, "code", xy);
  bool staged = false, unstaged = false, untracked = false;
  bool deleted = false, renamed = false, conflict = false;
  lua_git_xy_flags(xy, staged, unstaged, untracked, deleted, renamed, conflict);
  lua_push_bool_field(L, "staged", staged);
  lua_push_bool_field(L, "unstaged", unstaged);
  lua_push_bool_field(L, "untracked", untracked);
  lua_push_bool_field(L, "deleted", deleted);
  lua_push_bool_field(L, "renamed", renamed);
  lua_push_bool_field(L, "conflict", conflict);
}

void LuaAPI::push_git_info(lua_State *L)
{
  if (!editor || editor->git_root.empty())
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  lua_push_str_field(L, "root", editor->git_root);
  lua_push_str_field(L, "branch", editor->git_branch);
  lua_push_int_field(L, "dirty", editor->git_dirty_count);
  lua_push_int_field(L, "staged", editor->git_staged_count);
  lua_push_int_field(L, "unstaged", editor->git_unstaged_count);
  lua_push_int_field(L, "untracked", editor->git_untracked_count);
  lua_push_int_field(L, "deleted", editor->git_deleted_count);
  lua_push_int_field(L, "renamed", editor->git_renamed_count);
  lua_push_int_field(L, "conflict", editor->git_conflict_count);
  lua_push_int_field(L, "files", (long long)editor->git_file_status.size());
}

void LuaAPI::push_git_status(lua_State *L)
{
  if (!editor || editor->git_root.empty())
  {
    lua_pushnil(L);
    return;
  }
  // Single entry lookup when a path argument is given.
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1) && lua_isstring(L, 1))
  {
    const std::string key = lua_abs_path(lua_tostring(L, 1));
    auto it = editor->git_file_status.find(key);
    if (it == editor->git_file_status.end())
    {
      lua_pushnil(L);
      return;
    }
    lua_push_git_entry(L, editor->git_root, it->first, it->second);
    return;
  }
  lua_newtable(L);
  std::vector<std::pair<std::string, std::string>> entries(editor->git_file_status.begin(),
                                                           editor->git_file_status.end());
  std::sort(entries.begin(),
            entries.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  int n = 1;
  for (const auto &entry : entries)
  {
    lua_push_git_entry(L, editor->git_root, entry.first, entry.second);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::git_stage_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_stage_path(luaL_optstring(L, 1, "")));
}

void LuaAPI::git_unstage_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_unstage_path(luaL_optstring(L, 1, "")));
}

void LuaAPI::git_stage_all_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_stage_all());
}

void LuaAPI::git_unstage_all_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_unstage_all());
}

void LuaAPI::git_commit_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_commit_message(luaL_optstring(L, 1, "")));
}

void LuaAPI::git_refresh_from_lua(lua_State *L)
{
  if (editor)
    editor->refresh_git_status(true);
}

void LuaAPI::git_diff_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string path = luaL_checkstring(L, 1);
  const bool staged = lua_toboolean(L, 2);
  lua_pushboolean(L, editor->open_git_diff_panel(path, staged));
}
