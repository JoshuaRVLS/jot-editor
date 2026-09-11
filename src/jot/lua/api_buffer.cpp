// LuaAPI buffer surface: open/change/save hooks, edit-delta computation,
// cursor/selection accessors, buffer vars, diagnostics, folds,
// bookmarks, syntax tokens, and per-line text queries.
#include "editor.h"
#include "host_api.h"
#include "jot/lua/api.h"
#include "jot/lua/api_internal.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace fs = std::filesystem;

#include "features/syntax_highlighter.h"
#include "ui/components.h"
#include <algorithm>
#include <string>
#include <vector>

void LuaAPI::on_buffer_open(const std::string &f)
{
  if (editor)
    editor->notify_lsp_open(f);
  fire_autocmd("BufOpen", f, -1);
  emit_buffer_event("buffer.open", f);
}
static void lua_split_edit_lines(const std::string &text, std::vector<std::string> &out)
{
  size_t start = 0;
  for (size_t i = 0; i < text.size(); i++)
  {
    if (text[i] == '\n')
    {
      out.push_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  out.push_back(text.substr(start));
}

static size_t lua_common_prefix_bytes(const std::string &a, const std::string &b)
{
  const size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  while (i < n && a[i] == b[i])
    i++;
  return i;
}

static size_t lua_common_suffix_bytes(const std::string &a, const std::string &b)
{
  const size_t na = a.size(), nb = b.size();
  size_t i = 0;
  while (i < na && i < nb && a[na - 1 - i] == b[nb - 1 - i])
    i++;
  return i;
}

// Joins lines[first..last] (inclusive). The first line loses its leading
// cut_left bytes; the last line is truncated to its first end_col bytes.
static std::string lua_join_edit_range(const std::vector<std::string> &lines,
                                       size_t first,
                                       size_t last,
                                       size_t cut_left,
                                       size_t end_col)
{
  std::string out;
  if (first > last || first >= lines.size())
    return out;
  for (size_t i = first; i <= last && i < lines.size(); i++)
  {
    const std::string &line = lines[i];
    size_t from = 0;
    size_t to = line.size();
    if (i == first)
      from = std::min(cut_left, line.size());
    if (i == last)
    {
      to = std::min(end_col, line.size());
      if (i == first && to < from)
        to = from;
    }
    if (i > first)
      out += '\n';
    if (to > from)
      out.append(line, from, to - from);
  }
  return out;
}

// Computes a best-effort line/column delta between two versions of a buffer
// using common-prefix/common-suffix line matching. Exact for single-line
// typing/paste/delete edits and block inserts; approximations suffice for
// anything more exotic (undo spans, merges).
static LuaEditDelta compute_edit_delta(const std::string &prev, const std::string &now)
{
  LuaEditDelta d;
  if (prev == now)
    return d;
  std::vector<std::string> a, b;
  lua_split_edit_lines(prev, a);
  lua_split_edit_lines(now, b);
  size_t ai = 0, bi = 0;
  while (ai < a.size() && bi < b.size() && a[ai] == b[bi])
  {
    ai++;
    bi++;
  }
  size_t aend = a.size(), bend = b.size();
  while (aend > ai && bend > bi && a[aend - 1] == b[bend - 1])
  {
    aend--;
    bend--;
  }
  if (ai == aend && bi == bend)
    return d;

  const std::string &old_first = ai < a.size() ? a[ai] : std::string();
  const std::string &new_first = bi < b.size() ? b[bi] : std::string();
  const size_t p = lua_common_prefix_bytes(old_first, new_first);
  const bool old_has = aend > ai;
  const bool new_has = bend > bi;
  size_t suffix = 0;
  if (old_has && new_has)
  {
    suffix = lua_common_suffix_bytes(a[aend - 1], b[bend - 1]);
    // Single-line changes must not double-count the common middle.
    if (aend - ai == 1 && bend - bi == 1)
    {
      suffix = std::min(suffix, std::min(a[ai].size(), b[bi].size()) - p);
    }
  }
  const size_t old_end = old_has && a[aend - 1].size() >= suffix ? a[aend - 1].size() - suffix : 0;
  const size_t new_end = new_has && b[bend - 1].size() >= suffix ? b[bend - 1].size() - suffix : 0;

  d.start_line = (int)bi + 1;
  d.start_col = (int)p + 1;
  d.multiline = (aend - ai > 1) || (bend - bi > 1);
  if (new_has)
  {
    d.end_line = (int)bend;       // exclusive end line (1-based, one past last)
    d.end_col = (int)new_end + 1; // exclusive column on the last line
    d.inserted = lua_join_edit_range(b, bi, bend - 1, p, new_end);
  }
  else
  {
    d.end_line = d.start_line;
    d.end_col = d.start_col;
  }
  if (old_has)
  {
    d.removed = lua_join_edit_range(a, ai, aend - 1, p, old_end);
  }
  if (d.inserted.find('\n') != std::string::npos || d.removed.find('\n') != std::string::npos)
  {
    d.multiline = true;
  }
  d.valid = true;
  return d;
}

void LuaAPI::on_buffer_change(const std::string &f, const std::string &)
{
  last_edit_ = LuaEditDelta{};
  if (editor)
  {
    editor->notify_lsp_change(f);
    // The persistent Outline panel rebuilds lazily from this dirty flag.
    editor->note_outline_edit();
  }
  int index = -1;
  if (editor)
  {
    for (size_t i = 0; i < editor->buffers.size(); i++)
    {
      if (editor->buffers[i].filepath == f)
      {
        index = (int)i;
        break;
      }
    }
    if (index < 0 && !f.empty())
    {
      std::error_code ec;
      for (size_t i = 0; i < editor->buffers.size() && index < 0; i++)
      {
        const std::string &other = editor->buffers[i].filepath;
        if (!other.empty() && fs::exists(other, ec) && fs::exists(f, ec)
            && fs::equivalent(other, f, ec))
        {
          index = (int)i;
        }
      }
    }
    if (index < 0 && f.empty() && editor->current_buffer >= 0
        && editor->current_buffer < (int)editor->buffers.size())
    {
      index = editor->current_buffer;
    }
  }
  // Compute a rich edit delta only when a plugin actually listens to
  // BufChange, and skip huge/lazy buffers to keep the typing hot path cheap.
  if (index >= 0 && index < (int)editor->buffers.size())
  {
    bool has_listener = false;
    for (const auto &x : plugin_autocmds)
    {
      if (x.event == "BufChange")
      {
        has_listener = true;
        break;
      }
    }
    if (has_listener)
    {
      FileBuffer &buf = editor->buffers[(size_t)index];
      if (!buf.is_lazy() && buf.line_count() <= 100000)
      {
        std::string cur;
        for (size_t i = 0; i < buf.lines.size(); i++)
        {
          if (i)
            cur += '\n';
          cur += buf.lines[i];
        }
        // Unnamed buffers share an empty filepath; key them by index.
        const std::string key = f.empty() ? ("\x01" + std::to_string(index)) : f;
        auto it = edit_snapshots_.find(key);
        if (it != edit_snapshots_.end())
        {
          last_edit_ = compute_edit_delta(it->second, cur);
        }
        edit_snapshots_[key] = std::move(cur);
        while (edit_snapshots_.size() > 256)
        {
          edit_snapshots_.erase(edit_snapshots_.begin());
        }
      }
    }
  }
  fire_autocmd("BufChange", f, index);
  last_edit_ = LuaEditDelta{};
}
void LuaAPI::on_buffer_save(const std::string &f)
{
  if (editor)
    editor->notify_lsp_save(f);
  fire_autocmd("BufSave", f, -1);
  emit_buffer_event("buffer.save", f);
}
void LuaAPI::apply_buffer_edit_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const int start_line = (int)luaL_checkinteger(L, 1);
  const int start_col = (int)luaL_checkinteger(L, 2);
  const int end_line = (int)luaL_checkinteger(L, 3);
  const int end_col = (int)luaL_checkinteger(L, 4);
  const char *text = luaL_optstring(L, 5, "");
  const bool ok = editor->host_api
                      ? editor->host_api->core.apply_edit(
                            start_line, start_col, end_line, end_col, text ? text : "")
                      : false;
  lua_pushboolean(L, ok ? 1 : 0);
}

std::string LuaAPI::get_current_buffer()
{
  return editor && editor->host_api ? editor->host_api->core.buffer_content() : "";
}
void LuaAPI::set_current_buffer(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.set_buffer_content(s);
}
std::string LuaAPI::get_selection()
{
  return editor && editor->host_api ? editor->host_api->core.selected_text() : "";
}
void LuaAPI::replace_selection(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.replace_selection(s);
}
void LuaAPI::insert_text(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.insert_text(s);
}
std::pair<int, int> LuaAPI::get_cursor()
{
  return editor && editor->host_api ? editor->host_api->core.cursor() : std::pair<int, int>{0, 0};
}
void LuaAPI::set_cursor(int l, int c)
{
  if (editor && editor->host_api)
    editor->host_api->core.set_cursor(l, c);
}
std::string LuaAPI::current_file()
{
  return editor && editor->host_api ? editor->host_api->core.current_file() : "";
}
void LuaAPI::open_file(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->io.open_file(s);
}
void LuaAPI::save_current_file()
{
  if (editor && editor->host_api)
    editor->host_api->io.save_current_file();
}
void LuaAPI::execute_command(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->io.execute_command(s);
}
int LuaAPI::resolve_buffer_arg(lua_State *L, int arg_index)
{
  if (!editor || editor->buffers.empty())
    return -1;
  int idx = -1;
  if (arg_index <= lua_gettop(L) && !lua_isnil(L, arg_index))
  {
    if (lua_isnumber(L, arg_index))
    {
      idx = (int)lua_tointeger(L, arg_index) - 1;
    }
    else if (lua_isstring(L, arg_index))
    {
      std::string path = lua_tostring(L, arg_index);
      for (size_t i = 0; i < editor->buffers.size(); i++)
      {
        if (editor->buffers[i].filepath == path)
        {
          idx = (int)i;
          break;
        }
      }
    }
  }
  else
  {
    idx = editor->current_buffer;
  }
  if (idx < 0 || idx >= (int)editor->buffers.size())
    return -1;
  return idx;
}

void LuaAPI::push_diagnostics(lua_State *L)
{
  int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  int n = 1;
  for (const auto &d : buf.diagnostics)
  {
    lua_newtable(L);
    lua_pushinteger(L, d.line + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, d.col + 1);
    lua_setfield(L, -2, "col");
    lua_pushinteger(L, d.end_line + 1);
    lua_setfield(L, -2, "end_line");
    lua_pushinteger(L, d.end_col + 1);
    lua_setfield(L, -2, "end_col");
    lua_pushinteger(L, d.severity);
    lua_setfield(L, -2, "severity");
    lua_pushstring(L, lua_diag_severity_name(d.severity));
    lua_setfield(L, -2, "severity_name");
    lua_pushstring(L, d.message.c_str());
    lua_setfield(L, -2, "message");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::set_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int t = lua_type(L, 2);
  if (t != LUA_TSTRING && t != LUA_TNUMBER && t != LUA_TBOOLEAN)
  {
    luaL_error(L, "set_var value must be a string, number, or boolean");
    return;
  }
  lua_pushvalue(L, 2);
  const char *s = lua_tostring(L, -1);
  std::string value = s ? s : "";
  lua_pop(L, 1);
  int idx = resolve_buffer_arg(L, 3);
  if (idx < 0)
    return;
  editor->buffers[(size_t)idx].lua_vars[name] = std::move(value);
}

void LuaAPI::push_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int idx = resolve_buffer_arg(L, 2);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  auto it = editor->buffers[(size_t)idx].lua_vars.find(name);
  if (it == editor->buffers[(size_t)idx].lua_vars.end())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, it->second.c_str());
}

void LuaAPI::delete_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int idx = resolve_buffer_arg(L, 2);
  if (idx < 0)
    return;
  editor->buffers[(size_t)idx].lua_vars.erase(name);
}

void LuaAPI::push_buffer_current(lua_State *L)
{
  if (!editor || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushinteger(L, editor->current_buffer + 1);
}

void LuaAPI::push_buffer_count(lua_State *L)
{
  lua_pushinteger(L, editor ? (long long)editor->buffers.size() : 0);
}

void LuaAPI::push_buffer_text(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy())
  {
    lua_pushnil(L);
    return;
  }
  std::string text;
  for (size_t i = 0; i < buf.lines.size(); i++)
  {
    if (i)
      text += '\n';
    text += buf.lines[i];
  }
  lua_pushlstring(L, text.data(), text.size());
}

void LuaAPI::push_buffer_meta(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  lua_push_str_field(L, "path", buf.filepath);
  std::string name;
  if (!buf.filepath.empty())
  {
    name = fs::path(buf.filepath).filename().string();
  }
  lua_push_str_field(L, "name", name);
  lua_push_bool_field(L, "modified", buf.modified);
  lua_push_int_field(L, "line_count", (long long)buf.line_count());
}

void LuaAPI::push_buffer_selection(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  lua_push_bool_field(L, "active", buf.selection.active);
  lua_push_int_field(L, "start_line", (long long)buf.selection.start.y + 1);
  lua_push_int_field(L, "start_col", (long long)buf.selection.start.x + 1);
  lua_push_int_field(L, "end_line", (long long)buf.selection.end.y + 1);
  lua_push_int_field(L, "end_col", (long long)buf.selection.end.x + 1);
}

void LuaAPI::push_buffer_bookmarks(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  int n = 1;
  for (const int line : buf.bookmarks)
  {
    lua_pushinteger(L, line + 1);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_buffer_folds(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  int n = 1;
  for (const FoldRange &r : buf.fold_ranges)
  {
    lua_newtable(L);
    lua_push_int_field(L, "start", (long long)r.start_line + 1);
    lua_push_int_field(L, "end", (long long)r.end_line + 1);
    lua_push_bool_field(L, "collapsed", r.collapsed);
    lua_rawseti(L, -2, n++);
  }
}

static const char *const kLuaTokenNames[] = {"none",                   // 0
                                             "keyword",                // 1
                                             "string",                 // 2
                                             "comment",                // 3
                                             "number",                 // 4
                                             "type",                   // 5
                                             "function",               // 6
                                             "variable",               // 7
                                             "constant",               // 8
                                             "builtin",                // 9
                                             "operator",               // 10
                                             "punctuation",            // 11
                                             "tag",                    // 12
                                             "attribute",              // 13
                                             "namespace",              // 14
                                             "module",                 // 15
                                             "parameter",              // 16
                                             "field",                  // 17
                                             "keyword.control",        // 18
                                             "keyword.storage",        // 19
                                             "keyword.preproc",        // 20
                                             "function.method",        // 21
                                             "function.constructor",   // 22
                                             "type.builtin",           // 23
                                             "constant.macro",         // 24
                                             "string.escape",          // 25
                                             "punctuation.bracket",    // 26
                                             "punctuation.delimiter"}; // 27

// Resolve a token id to the active theme color, mirroring the renderer's own
// mapping so Lua callers see the exact highlight color on screen.
static int lua_syntax_fg(const Theme &theme, int token)
{
  switch (token)
  {
  case 1:
    return theme.fg_keyword;
  case 2:
    return theme.fg_string;
  case 3:
    return theme.fg_comment;
  case 4:
    return theme.fg_number;
  case 5:
    return theme.fg_type;
  case 6:
    return theme.fg_function;
  case 7:
    return theme.fg_variable;
  case 8:
    return theme.fg_constant;
  case 9:
    return theme.fg_builtin;
  case 10:
    return theme.fg_operator;
  case 11:
    return theme.fg_punctuation;
  case 12:
    return theme.fg_tag;
  case 13:
    return theme.fg_attribute;
  case 14:
    return theme.fg_namespace;
  case 15:
    return theme.fg_module;
  case 16:
    return theme.fg_parameter;
  case 17:
    return theme.fg_field;
  case 18:
    return theme.fg_keyword_control;
  case 19:
    return theme.fg_keyword_storage;
  case 20:
    return theme.fg_keyword_preproc;
  case 21:
    return theme.fg_function_method;
  case 22:
    return theme.fg_function_constructor;
  case 23:
    return theme.fg_type_builtin;
  case 24:
    return theme.fg_constant_macro;
  case 25:
    return theme.fg_string_escape;
  case 26:
    return theme.fg_punctuation_bracket;
  case 27:
    return theme.fg_punctuation_delimiter;
  default:
    return theme.fg_command;
  }
}

void LuaAPI::push_buffer_tokens(lua_State *L)
{
  if (!editor)
  {
    lua_newtable(L);
    return;
  }
  // Read args BEFORE pushing the result table: lua_newtable shifts the stack,
  // so the optional buffer-id argument would otherwise be masked by the table
  // itself (never nil), making resolve_buffer_arg fail and this always return
  // an empty token list.
  const int line_no = (int)luaL_checkinteger(L, 1) - 1;
  int idx = editor->current_buffer;
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
  {
    idx = resolve_buffer_arg(L, 2);
  }
  if (idx < 0 || idx >= (int)editor->buffers.size())
  {
    lua_newtable(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (line_no < 0 || line_no >= (int)buf.line_count())
  {
    lua_newtable(L);
    return;
  }
  lua_newtable(L);
  const auto &colors = editor->get_line_syntax_colors(buf, line_no);
  const std::string &text = buf.line(line_no);
  const size_t n_colors = colors.size();
  int n = 1;
  size_t i = 0;
  while (i < n_colors)
  {
    const int token = colors[i].first == 1 ? colors[i].second : 0;
    size_t j = i + 1;
    while (j < n_colors)
    {
      const int next = colors[j].first == 1 ? colors[j].second : 0;
      if (next != token)
        break;
      j++;
    }
    if (token > 0 && token < 28)
    {
      const size_t span_end = std::min(j, (size_t)std::max(0, (int)text.size()));
      if (i < span_end)
      {
        lua_newtable(L);
        lua_push_int_field(L, "start", (long long)i);
        lua_push_int_field(L, "end", (long long)span_end);
        lua_push_int_field(L, "token", token);
        lua_push_str_field(L, "name", kLuaTokenNames[token]);
        lua_push_str_field(L, "text", text.substr(i, span_end - i));
        lua_push_int_field(L, "fg", lua_syntax_fg(editor->theme, token));
        lua_rawseti(L, -2, n++);
      }
    }
    i = j;
  }
}

// Runs a one-shot Lua callback (registered under `id` in `callbacks`) with a
// single table argument built by `build`. Returns true when the callback
// existed and ran.

void LuaAPI::push_buffer_lines(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy())
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  int n = 1;
  for (const std::string &line : buf.lines)
  {
    lua_pushlstring(L, line.data(), line.size());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_buffer_filetype(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_pushstring(L, editor->get_file_extension(buf.filepath).c_str());
}

void LuaAPI::push_buffer_get_line(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int line = (int)luaL_checkinteger(L, 1) - 1;
  const int idx = resolve_buffer_arg(L, 2);
  if (idx < 0 || line < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  if (line >= buf.line_count())
  {
    lua_pushnil(L);
    return;
  }
  const std::string &s = buf.line(line);
  lua_pushlstring(L, s.data(), s.size());
}
