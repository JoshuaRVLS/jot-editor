// Lua LSP bridge: one-shot deliver of hover / definition / symbol /
// completion results to Lua callbacks, the hover popup runtime, the per-server
// payload pushers (clients, diagnostics, last results, emit_* events) and the
// runtime lsp.*_from_lua commands. Split out of api_core.cpp.
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

static void lua_push_diag_row(lua_State *L, const Diagnostic &d)
{
  lua_newtable(L);
  lua_push_int_field(L, "line", (long long)d.line + 1);
  lua_push_int_field(L, "column", (long long)d.col + 1);
  lua_push_int_field(L, "end_line", (long long)d.end_line + 1);
  lua_push_int_field(L, "end_col", (long long)d.end_col + 1);
  lua_push_str_field(L, "message", d.message);
  lua_push_int_field(L, "severity", d.severity);
  lua_push_str_field(L, "severity_name", lua_diag_severity_name(d.severity));
}

static void lua_push_location(lua_State *L, const LSPLocation &loc)
{
  lua_newtable(L);
  lua_push_str_field(L, "path", loc.filepath);
  lua_push_int_field(L, "line", (long long)loc.line + 1);
  lua_push_int_field(L, "column", (long long)loc.character + 1);
  lua_push_int_field(L, "end_line", (long long)loc.end_line + 1);
  lua_push_int_field(L, "end_column", (long long)loc.end_character + 1);
}

static void lua_push_symbol_row(lua_State *L, const LSPSymbol &s)
{
  lua_newtable(L);
  lua_push_str_field(L, "name", s.name);
  lua_push_str_field(L, "kind", s.kind);
  lua_push_str_field(L, "detail", s.detail);
  lua_push_str_field(L, "path", s.filepath);
  lua_push_int_field(L, "line", (long long)s.line + 1);
  lua_push_int_field(L, "column", (long long)s.character + 1);
  lua_push_int_field(L, "end_line", (long long)s.end_line + 1);
  lua_push_int_field(L, "end_column", (long long)s.end_character + 1);
}

bool LuaAPI::try_deliver_lsp_hover(const LSPHoverResult &hover)
{
  if (pending_lsp_hover.empty())
    return false;
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    pending_lsp_hover.clear();
    return false;
  }
  const FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (buf.filepath != hover.origin_filepath || buf.cursor.y != hover.origin_line
      || buf.cursor.x != hover.origin_character)
  {
    return false;
  }
  const std::string id = pending_lsp_hover;
  pending_lsp_hover.clear();
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_str_field(L, "contents", hover.contents);
                                lua_push_str_field(L, "path", hover.origin_filepath);
                                lua_push_int_field(L, "line", (long long)hover.origin_line + 1);
                                lua_push_int_field(
                                    L, "column", (long long)hover.origin_character + 1);
                              });
}

void LuaAPI::set_lsp_hover_ui_handler(lua_State *L, int fn_index)
{
  if (lsp_hover_ui_ref_ != LUA_NOREF)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, lsp_hover_ui_ref_);
  }
  if (fn_index > 0)
  {
    lua_pushvalue(L, fn_index);
    lsp_hover_ui_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else
  {
    lsp_hover_ui_ref_ = LUA_NOREF;
  }
}

bool LuaAPI::has_lsp_hover_ui() const
{
  return lsp_hover_ui_ref_ != LUA_NOREF && lua_state != nullptr;
}

bool LuaAPI::present_lsp_hover(const std::string &contents,
                               const std::string &filepath,
                               int line,
                               int character,
                               const std::string &kind,
                               int anchor_x,
                               int anchor_y)
{
  if (!has_lsp_hover_ui())
  {
    return false;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, lsp_hover_ui_ref_);
  if (!lua_isfunction(L, -1))
  {
    lua_settop(L, top);
    return false;
  }
  lua_newtable(L);
  lua_push_str_field(L, "contents", contents);
  lua_push_str_field(L, "path", filepath);
  lua_push_int_field(L, "line", (long long)line + 1);
  lua_push_int_field(L, "column", (long long)character + 1);
  lua_push_str_field(L, "kind", kind);
  lua_push_int_field(L, "anchor_x", (long long)anchor_x);
  lua_push_int_field(L, "anchor_y", (long long)anchor_y);
  // Theme colors mirror the native hover popup: command text on the panel
  // background, panel border for the frame. The `colors` table maps syntax
  // token kind names to their theme colors so the Lua UI can highlight code
  // fences the same way the editor does.
  int fg = 7, bg = 0, border = 8;
  if (editor)
  {
    const Theme &t = editor->get_theme();
    fg = t.fg_command >= 0 ? t.fg_command : 7;
    bg = t.bg_panel_border >= 0 ? t.bg_panel_border : 0;
    border = t.fg_panel_border >= 0 ? t.fg_panel_border : fg;
  }
  lua_push_int_field(L, "fg", fg);
  lua_push_int_field(L, "bg", bg);
  lua_push_int_field(L, "border_fg", border);
  lua_push_int_field(L, "border", border); // legacy alias
  lua_newtable(L);
  const Theme &t = editor ? editor->get_theme() : Theme{};
  lua_push_int_field(L, "keyword", t.fg_keyword);
  lua_push_int_field(L, "string", t.fg_string);
  lua_push_int_field(L, "comment", t.fg_comment);
  lua_push_int_field(L, "number", t.fg_number);
  lua_push_int_field(L, "type", t.fg_type);
  lua_push_int_field(L, "function", t.fg_function);
  lua_push_int_field(L, "variable", t.fg_variable);
  lua_push_int_field(L, "constant", t.fg_constant);
  lua_push_int_field(L, "builtin", t.fg_builtin);
  lua_push_int_field(L, "operator", t.fg_operator);
  lua_push_int_field(L, "punctuation", t.fg_punctuation);
  lua_push_int_field(L, "tag", t.fg_tag);
  lua_push_int_field(L, "attribute", t.fg_attribute);
  lua_push_int_field(L, "namespace", t.fg_namespace);
  lua_push_int_field(L, "module", t.fg_module);
  lua_push_int_field(L, "parameter", t.fg_parameter);
  lua_push_int_field(L, "field", t.fg_field);
  lua_push_int_field(L, "keyword_control", t.fg_keyword_control);
  lua_push_int_field(L, "keyword_storage", t.fg_keyword_storage);
  lua_push_int_field(L, "keyword_preproc", t.fg_keyword_preproc);
  lua_push_int_field(L, "function_method", t.fg_function_method);
  lua_push_int_field(L, "function_constructor", t.fg_function_constructor);
  lua_push_int_field(L, "type_builtin", t.fg_type_builtin);
  lua_push_int_field(L, "constant_macro", t.fg_constant_macro);
  lua_push_int_field(L, "string_escape", t.fg_string_escape);
  lua_push_int_field(L, "punctuation_bracket", t.fg_punctuation_bracket);
  lua_push_int_field(L, "punctuation_delimiter", t.fg_punctuation_delimiter);
  lua_setfield(L, -2, "colors");
  const int ok = lua_pcall(L, 1, 1, 0);
  bool consumed = false;
  if (ok != LUA_OK)
  {
    // Never write to stderr here: if it reaches the terminal it inserts a raw
    // line into the live screen and makes the editor "jump". Surface it in the
    // message bar instead.
    if (editor)
    {
      std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
      if (msg.size() > 80)
        msg.resize(80);
      editor->set_message("Lua hover UI error: " + msg);
    }
  }
  else
  {
    consumed = lua_toboolean(L, -1) != 0;
  }
  lua_settop(L, top);
  return consumed;
}

void LuaAPI::notify_lsp_hover_closed()
{
  if (!has_lsp_hover_ui())
  {
    return;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, lsp_hover_ui_ref_);
  if (!lua_isfunction(L, -1))
  {
    lua_settop(L, top);
    return;
  }
  lua_pushnil(L); // handler receives nil to mean "hide"
  if (lua_pcall(L, 1, 0, 0) != LUA_OK)
  {
    if (editor)
    {
      std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
      if (msg.size() > 80)
        msg.resize(80);
      editor->set_message("Lua hover UI close error: " + msg);
    }
  }
  lua_settop(L, top);
}

bool LuaAPI::load_hover_ui_runtime(lua_State *L)
{
  return load_bundled_lua_file(L, "features/hover.lua", "Hover UI");
}

bool LuaAPI::try_deliver_lsp_definition(const LSPDefinitionResult &definition)
{
  if (pending_lsp_definition.empty())
    return false;
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    pending_lsp_definition.clear();
    return false;
  }
  const FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (buf.filepath != definition.origin_filepath || buf.cursor.y != definition.origin_line
      || buf.cursor.x != definition.origin_character)
  {
    return false;
  }
  const std::string id = pending_lsp_definition;
  pending_lsp_definition.clear();
  return lua_deliver_one_shot(
      id,
      lua_callbacks,
      lua_state,
      [&](lua_State *L)
      {
        lua_newtable(L);
        lua_push_str_field(L, "path", definition.origin_filepath);
        lua_push_int_field(L, "line", (long long)definition.origin_line + 1);
        lua_push_int_field(L, "column", (long long)definition.origin_character + 1);
        lua_newtable(L);
        int n = 1;
        for (const LSPLocation &loc : definition.locations)
        {
          lua_push_location(L, loc);
          lua_rawseti(L, -2, n++);
        }
        lua_setfield(L, -2, "locations");
      });
}

bool LuaAPI::try_deliver_lsp_symbols(const LSPDocumentSymbolResult &symbols)
{
  if (pending_lsp_symbols.empty())
    return false;
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    pending_lsp_symbols.clear();
    return false;
  }
  const FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (buf.filepath != symbols.filepath)
    return false;
  const std::string id = pending_lsp_symbols;
  pending_lsp_symbols.clear();
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_str_field(L, "path", symbols.filepath);
                                lua_newtable(L);
                                int n = 1;
                                for (const LSPSymbol &s : symbols.symbols)
                                {
                                  lua_push_symbol_row(L, s);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "symbols");
                              });
}

void LuaAPI::lsp_request_from_lua(lua_State *L, int kind)
{
  if (!editor)
    return;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_pushvalue(L, 1);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const std::string id = "lsp." + std::to_string(kind) + "." + std::to_string(ref);
  lua_callbacks[id] = ref;
  switch (kind)
  {
  case 0:
    pending_lsp_hover = id;
    editor->request_lsp_hover();
    break;
  case 1:
    pending_lsp_definition = id;
    editor->request_lsp_definition();
    break;
  case 2:
    pending_lsp_symbols = id;
    editor->request_document_symbols();
    break;
  default:
    pending_lsp_completion = id;
    editor->request_lsp_completion(true);
    break;
  }
}

static const char *lua_completion_kind_name(int kind)
{
  switch (kind)
  {
  case 1:
    return "Text";
  case 2:
    return "Method";
  case 3:
    return "Function";
  case 4:
    return "Constructor";
  case 5:
    return "Field";
  case 6:
    return "Variable";
  case 7:
    return "Class";
  case 8:
    return "Interface";
  case 9:
    return "Module";
  case 10:
    return "Property";
  case 11:
    return "Unit";
  case 12:
    return "Value";
  case 13:
    return "Enum";
  case 14:
    return "Keyword";
  case 15:
    return "Snippet";
  case 16:
    return "Color";
  case 17:
    return "File";
  case 18:
    return "Reference";
  case 19:
    return "Folder";
  case 20:
    return "EnumMember";
  case 21:
    return "Constant";
  case 22:
    return "Struct";
  case 23:
    return "Event";
  case 24:
    return "Operator";
  case 25:
    return "TypeParameter";
  default:
    return "Unknown";
  }
}

static void lua_push_completion_row(lua_State *L, const LSPCompletionItem &item)
{
  lua_newtable(L);
  lua_push_str_field(L, "label", item.label);
  lua_push_str_field(L, "insert_text", item.insert_text);
  lua_push_str_field(L, "detail", item.detail);
  lua_push_str_field(L, "documentation", item.documentation);
  lua_push_str_field(L, "filter_text", item.filter_text);
  lua_push_str_field(L, "sort_text", item.sort_text);
  lua_push_int_field(L, "kind", item.kind);
  lua_push_str_field(L, "kind_name", lua_completion_kind_name(item.kind));
  lua_push_int_field(L, "insert_text_format", item.insert_text_format);
  lua_push_bool_field(L, "deprecated", item.deprecated);
  lua_push_bool_field(L, "preselect", item.preselect);
  lua_newtable(L);
  int cn = 1;
  for (const std::string &c : item.commit_characters)
  {
    lua_pushstring(L, c.c_str());
    lua_rawseti(L, -2, cn++);
  }
  lua_setfield(L, -2, "commit_characters");
  lua_push_bool_field(L, "has_edit_range", item.has_text_edit_range);
  lua_push_int_field(L, "edit_start_line", (long long)item.edit_start_line + 1);
  lua_push_int_field(L, "edit_start_char", (long long)item.edit_start_char + 1);
  lua_push_int_field(L, "edit_end_line", (long long)item.edit_end_line + 1);
  lua_push_int_field(L, "edit_end_char", (long long)item.edit_end_char + 1);
}

bool LuaAPI::try_deliver_lsp_completion(const std::string &filepath,
                                        const std::vector<LSPCompletionItem> &items)
{
  if (pending_lsp_completion.empty())
    return false;
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    pending_lsp_completion.clear();
    return false;
  }
  const FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (buf.filepath != filepath)
    return false;
  const std::string id = pending_lsp_completion;
  pending_lsp_completion.clear();
  const Cursor anchor = editor->lsp_completion_anchor;
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_str_field(L, "path", filepath);
                                lua_push_int_field(L, "anchor_line", (long long)anchor.y + 1);
                                lua_push_int_field(L, "anchor_col", (long long)anchor.x + 1);
                                lua_newtable(L);
                                int n = 1;
                                for (const LSPCompletionItem &item : items)
                                {
                                  lua_push_completion_row(L, item);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "items");
                              });
}

void LuaAPI::push_lsp_completions(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  lua_push_bool_field(L, "visible", editor->lsp_completion_visible);
  lua_push_str_field(L, "path", editor->lsp_completion_filepath);
  lua_push_str_field(L, "prefix", editor->lsp_completion_prefix);
  lua_push_int_field(L, "anchor_line", (long long)editor->lsp_completion_anchor.y + 1);
  lua_push_int_field(L, "anchor_col", (long long)editor->lsp_completion_anchor.x + 1);
  lua_push_int_field(L,
                     "selected",
                     editor->lsp_completion_visible ? (long long)editor->lsp_completion_selected + 1
                                                    : 0);
  lua_push_int_field(L, "total", (long long)editor->lsp_completion_all_items.size());
  lua_newtable(L);
  int n = 1;
  for (const LSPCompletionItem &item : editor->lsp_completion_items)
  {
    lua_push_completion_row(L, item);
    lua_rawseti(L, -2, n++);
  }
  lua_setfield(L, -2, "items");
}

// ---------------------------------------------------------------------------
// Ambient event bus (jot.events) and viewport mirror (jot.viewport).

void LuaAPI::push_lsp_clients(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &client : editor->lsp_clients)
  {
    if (!client)
      continue;
    lua_newtable(L);
    lua_push_str_field(L, "language", client->get_language());
    lua_push_str_field(L, "root", client->get_root_path());
    lua_push_bool_field(L, "running", client->is_running());
    lua_push_bool_field(L, "initialized", client->is_initialized());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::emit_lsp_hover(const LSPHoverResult &hover)
{
  if (!has_event_subscribers("lsp.hover"))
    return;
  emit_event_bus("lsp.hover",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "contents", hover.contents);
                   lua_push_str_field(L, "path", hover.origin_filepath);
                   lua_push_int_field(L, "line", (long long)hover.origin_line + 1);
                   lua_push_int_field(L, "column", (long long)hover.origin_character + 1);
                 });
}

void LuaAPI::emit_lsp_definition(const LSPDefinitionResult &definition)
{
  if (!has_event_subscribers("lsp.definition"))
    return;
  emit_event_bus("lsp.definition",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "path", definition.origin_filepath);
                   lua_push_int_field(L, "line", (long long)definition.origin_line + 1);
                   lua_push_int_field(L, "column", (long long)definition.origin_character + 1);
                   lua_newtable(L);
                   int n = 1;
                   for (const LSPLocation &loc : definition.locations)
                   {
                     lua_push_location(L, loc);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "locations");
                 });
}

void LuaAPI::emit_lsp_symbols(const LSPDocumentSymbolResult &symbols)
{
  if (!has_event_subscribers("lsp.symbols"))
    return;
  emit_event_bus("lsp.symbols",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "path", symbols.filepath);
                   lua_newtable(L);
                   int n = 1;
                   for (const LSPSymbol &s : symbols.symbols)
                   {
                     lua_push_symbol_row(L, s);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "symbols");
                 });
}

void LuaAPI::emit_lsp_completion(const std::string &filepath,
                                 const std::vector<LSPCompletionItem> &items)
{
  if (!has_event_subscribers("lsp.completion"))
    return;
  const Cursor anchor = editor ? editor->lsp_completion_anchor : Cursor{0, 0};
  emit_event_bus("lsp.completion",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "path", filepath);
                   lua_push_int_field(L, "anchor_line", (long long)anchor.y + 1);
                   lua_push_int_field(L, "anchor_col", (long long)anchor.x + 1);
                   lua_newtable(L);
                   int n = 1;
                   for (const LSPCompletionItem &item : items)
                   {
                     lua_push_completion_row(L, item);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "items");
                 });
}

void LuaAPI::push_lsp_diagnostics(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &client : editor->lsp_clients)
  {
    if (!client)
      continue;
    lua_newtable(L);
    lua_push_str_field(L, "language", client->get_language());
    lua_push_str_field(L, "root", client->get_root_path());
    const auto &files = client->last_diagnostics();
    lua_newtable(L);
    int fn = 1;
    for (const auto &entry : files)
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", entry.first);
      lua_newtable(L);
      int dn = 1;
      for (const Diagnostic &d : entry.second)
      {
        lua_push_diag_row(L, d);
        lua_rawseti(L, -2, dn++);
      }
      lua_setfield(L, -2, "diagnostics");
      lua_rawseti(L, -2, fn++);
    }
    lua_setfield(L, -2, "files");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_lsp_last_results(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &client : editor->lsp_clients)
  {
    if (!client)
      continue;
    lua_newtable(L);
    lua_push_str_field(L, "language", client->get_language());
    lua_push_str_field(L, "root", client->get_root_path());
    const LSPHoverResult &hover = client->last_hover();
    if (hover.contents.empty())
    {
      lua_pushnil(L);
    }
    else
    {
      lua_newtable(L);
      lua_push_str_field(L, "contents", hover.contents);
      lua_push_str_field(L, "path", hover.origin_filepath);
      lua_push_int_field(L, "line", (long long)hover.origin_line + 1);
      lua_push_int_field(L, "column", (long long)hover.origin_character + 1);
    }
    lua_setfield(L, -2, "hover");
    lua_newtable(L);
    int dn = 1;
    for (const LSPDefinitionResult &def : client->last_definitions())
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", def.origin_filepath);
      lua_push_int_field(L, "line", (long long)def.origin_line + 1);
      lua_push_int_field(L, "column", (long long)def.origin_character + 1);
      lua_newtable(L);
      int ln = 1;
      for (const LSPLocation &loc : def.locations)
      {
        lua_push_location(L, loc);
        lua_rawseti(L, -2, ln++);
      }
      lua_setfield(L, -2, "locations");
      lua_rawseti(L, -2, dn++);
    }
    lua_setfield(L, -2, "definitions");
    lua_newtable(L);
    int sn = 1;
    for (const LSPDocumentSymbolResult &res : client->last_document_symbols())
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", res.filepath);
      lua_newtable(L);
      int rn = 1;
      for (const LSPSymbol &s : res.symbols)
      {
        lua_push_symbol_row(L, s);
        lua_rawseti(L, -2, rn++);
      }
      lua_setfield(L, -2, "symbols");
      lua_rawseti(L, -2, sn++);
    }
    lua_setfield(L, -2, "symbols");
    lua_rawseti(L, -2, n++);
  }
}

// ---------------------------------------------------------------------------
// Batch 5: jot.timer / jot.debugger live state / jot.theme.palette /
// jot.buffer.lines / jot.clipboard.get
// ---------------------------------------------------------------------------

void LuaAPI::push_lsp_disabled(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const std::string &s : editor->lsp_disabled_servers)
  {
    lua_pushstring(L, s.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::lsp_set_enabled_from_lua(lua_State *L)
{
  if (!editor)
    return;
  editor->set_lsp_server_enabled(luaL_checkstring(L, 1), lua_toboolean(L, 2));
}

void LuaAPI::lsp_install_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->install_lsp_server(luaL_checkstring(L, 1)));
}

void LuaAPI::lsp_remove_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->remove_lsp_server(luaL_checkstring(L, 1)));
}

void LuaAPI::lsp_restart_all_from_lua(lua_State *L)
{
  if (editor)
    editor->restart_all_lsp_clients();
}

void LuaAPI::emit_diagnostics_changed(const std::string &path, const std::vector<Diagnostic> &items)
{
  if (!has_event_subscribers("diagnostics.changed"))
    return;
  emit_event_bus("diagnostics.changed",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "path", path);
                   lua_push_int_field(L, "count", (long long)items.size());
                   lua_newtable(L);
                   int n = 1;
                   for (const Diagnostic &d : items)
                   {
                     lua_push_diag_row(L, d);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "items");
                 });
}
