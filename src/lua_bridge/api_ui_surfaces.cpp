// Lua UI surface bridge: the jot.ui.handler registry (which surface renders
// through Lua) and every emit_* method that serializes native state into a
// payload table for those handlers. Split out of api_core.cpp.
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"
#include "ui/components.h"
#include "ui/text.h"

#include <algorithm>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

bool LuaAPI::register_lua_ui_handler(const std::string &name, lua_State *L, int fn_index)
{
  lua_pushvalue(L, fn_index);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  auto it = lua_ui_handlers_.find(name);
  if (it != lua_ui_handlers_.end() && it->second >= 0)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, it->second);
  }
  lua_ui_handlers_[name] = ref;
  return true;
}

void LuaAPI::clear_lua_ui_handler(const std::string &name)
{
  auto it = lua_ui_handlers_.find(name);
  if (it == lua_ui_handlers_.end())
  {
    return;
  }
  if (it->second >= 0 && lua_state)
  {
    luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, it->second);
  }
  lua_ui_handlers_.erase(it);
}

bool LuaAPI::has_lua_ui_handler(const std::string &name) const
{
  auto it = lua_ui_handlers_.find(name);
  return it != lua_ui_handlers_.end() && it->second >= 0;
}

bool LuaAPI::emit_lua_ui(const std::string &name,
                         const std::function<void(lua_State *, int)> &fill_payload)
{
  if (!lua_state || !editor)
  {
    return false;
  }
  auto it = lua_ui_handlers_.find(name);
  if (it == lua_ui_handlers_.end() || it->second < 0)
  {
    return false;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
  if (!lua_isfunction(L, -1))
  {
    lua_settop(L, top);
    return false;
  }
  if (fill_payload)
  {
    lua_newtable(L);
    fill_payload(L, lua_gettop(L));
  }
  else
  {
    lua_pushnil(L); // surface closed
  }
  const int ok = lua_pcall(L, 1, 1, 0);
  bool consumed = false;
  if (ok != LUA_OK)
  {
    std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
    if (msg.size() > 80)
      msg.resize(80);
    editor->set_message("Lua UI error (" + name + "): " + msg);
  }
  else
  {
    consumed = lua_toboolean(L, -1) != 0;
  }
  lua_settop(L, top);
  return consumed;
}

bool LuaAPI::emit_lua_ui_close(const std::string &name)
{
  return emit_lua_ui(name, nullptr);
}

void LuaAPI::push_ui_colors(lua_State *L, int t)
{
  const Theme &th = editor ? editor->get_theme() : Theme{};
  lua_newtable(L);
  const int c = lua_gettop(L);
  lua_set_int_field(L, c, "fg", th.fg_command);
  lua_set_int_field(L, c, "bg", th.bg_command);
  lua_set_int_field(L, c, "default_fg", th.fg_default);
  lua_set_int_field(L, c, "default_bg", th.bg_default);
  // Sidebar / home + status line slots.
  lua_set_int_field(L, c, "sidebar_fg", th.fg_sidebar);
  lua_set_int_field(L, c, "sidebar_bg", th.bg_sidebar);
  lua_set_int_field(L, c, "sidebar_dir", th.fg_sidebar_directory);
  lua_set_int_field(L, c, "sidebar_sel_fg", th.fg_sidebar_selected);
  lua_set_int_field(L, c, "sidebar_sel_bg", th.bg_sidebar_selected);
  lua_set_int_field(L, c, "sidebar_sel_inactive_fg", th.fg_sidebar_selected_inactive);
  lua_set_int_field(L, c, "sidebar_sel_inactive_bg", th.bg_sidebar_selected_inactive);
  lua_set_int_field(L, c, "sidebar_border", th.fg_sidebar_border);
  lua_set_int_field(L, c, "active_border", th.fg_active_border);
  lua_set_int_field(L, c, "diag_error", th.fg_diagnostic_error);
  lua_set_int_field(L, c, "diag_warning", th.fg_diagnostic_warning);
  lua_set_int_field(L, c, "diag_info", th.fg_diagnostic_info);
  lua_set_int_field(L, c, "diag_hint", th.fg_diagnostic_hint);
  lua_set_int_field(L, c, "status_fg", th.fg_status);
  lua_set_int_field(L, c, "status_bg", th.bg_status);
  lua_set_int_field(L, c, "status_file_fg", th.fg_status_file);
  lua_set_int_field(L, c, "status_file_bg", th.bg_status_file);
  lua_set_int_field(L, c, "status_message", th.fg_status_message);
  lua_set_int_field(L, c, "status_muted_fg", th.fg_status_muted);
  lua_set_int_field(L, c, "status_muted_bg", th.bg_status_muted);
  lua_set_int_field(L, c, "status_logo_fg", th.fg_status_logo);
  lua_set_int_field(L, c, "status_logo_bg", th.bg_status_logo);
  lua_set_int_field(L, c, "status_info_bg", th.bg_status_info);
  lua_set_int_field(L, c, "status_warning_bg", th.bg_status_warning);
  lua_set_int_field(L, c, "status_error_bg", th.bg_status_error);
  lua_set_int_field(L, c, "panel_bg", th.bg_panel_border);
  lua_set_int_field(L, c, "border", th.fg_panel_border);
  lua_set_int_field(L, c, "selection_fg", th.fg_selection);
  lua_set_int_field(L, c, "selection_bg", th.bg_selection);
  lua_set_int_field(L, c, "comment", th.fg_comment);
  lua_set_int_field(L, c, "accent", th.fg_keyword);
  lua_set_int_field(L, c, "error", th.fg_status_error);
  lua_set_int_field(L, c, "warning", th.fg_status_warning);
  lua_set_int_field(L, c, "info", th.fg_status_info);
  // Telescope slots.
  lua_set_int_field(L, c, "t_fg", th.fg_telescope);
  lua_set_int_field(L, c, "t_bg", th.bg_telescope);
  lua_set_int_field(L, c, "t_sel_fg", th.fg_telescope_selected);
  lua_set_int_field(L, c, "t_sel_bg", th.bg_telescope_selected);
  lua_set_int_field(L, c, "t_prev_fg", th.fg_telescope_preview);
  lua_set_int_field(L, c, "t_prev_bg", th.bg_telescope_preview);
  // Syntax colors (token kind name -> theme color), matching the hover payload
  // so code previews highlight with the same colors as the editor.
  lua_set_int_field(L, c, "keyword", th.fg_keyword);
  lua_set_int_field(L, c, "string", th.fg_string);
  lua_set_int_field(L, c, "comment", th.fg_comment);
  lua_set_int_field(L, c, "number", th.fg_number);
  lua_set_int_field(L, c, "type", th.fg_type);
  lua_set_int_field(L, c, "function", th.fg_function);
  lua_set_int_field(L, c, "variable", th.fg_variable);
  lua_set_int_field(L, c, "constant", th.fg_constant);
  lua_set_int_field(L, c, "builtin", th.fg_builtin);
  lua_set_int_field(L, c, "operator", th.fg_operator);
  lua_set_int_field(L, c, "punctuation", th.fg_punctuation);
  lua_set_int_field(L, c, "tag", th.fg_tag);
  lua_set_int_field(L, c, "attribute", th.fg_attribute);
  lua_set_int_field(L, c, "namespace", th.fg_namespace);
  lua_set_int_field(L, c, "module", th.fg_module);
  lua_set_int_field(L, c, "parameter", th.fg_parameter);
  lua_set_int_field(L, c, "field", th.fg_field);
  lua_set_int_field(L, c, "keyword_control", th.fg_keyword_control);
  lua_set_int_field(L, c, "keyword_storage", th.fg_keyword_storage);
  lua_set_int_field(L, c, "keyword_preproc", th.fg_keyword_preproc);
  lua_set_int_field(L, c, "function_method", th.fg_function_method);
  lua_set_int_field(L, c, "function_constructor", th.fg_function_constructor);
  lua_set_int_field(L, c, "type_builtin", th.fg_type_builtin);
  lua_set_int_field(L, c, "constant_macro", th.fg_constant_macro);
  lua_set_int_field(L, c, "string_escape", th.fg_string_escape);
  lua_set_int_field(L, c, "punctuation_bracket", th.fg_punctuation_bracket);
  lua_set_int_field(L, c, "punctuation_delimiter", th.fg_punctuation_delimiter);
  lua_setfield(L, t, "colors");
}

bool LuaAPI::emit_command_palette(const PaletteView &view)
{
  return emit_lua_ui("command_palette",
                     [&](lua_State *L, int t)
                     {
                       lua_set_str_field(L, t, "query", view.query);
                       lua_set_int_field(L, t, "selected", view.selected);
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_int_field(L, t, "screen_w", view.screen_w);
                       lua_set_int_field(L, t, "screen_h", view.screen_h);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.results.size(); i++)
                       {
                         const PaletteItemView &item = view.results[i];
                         lua_newtable(L);
                         const int it = lua_gettop(L);
                         lua_set_str_field(L, it, "label", item.label);
                         lua_set_str_field(L, it, "category", item.category);
                         lua_set_str_field(L, it, "detail", item.detail);
                         lua_newtable(L);
                         const int m = lua_gettop(L);
                         for (size_t k = 0; k < item.match.size(); k++)
                         {
                           lua_pushinteger(L, item.match[k]);
                           lua_rawseti(L, m, (lua_Integer)k + 1);
                         }
                         lua_setfield(L, it, "match");
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "results");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_quick_pick(const QuickPickView &view)
{
  return emit_lua_ui("quick_pick",
                     [&](lua_State *L, int t)
                     {
                       lua_set_str_field(L, t, "title", view.title);
                       lua_set_str_field(L, t, "query", view.query);
                       lua_set_int_field(L, t, "selected", view.selected);
                       lua_set_int_field(L, t, "all_count", view.all_count);
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.items.size(); i++)
                       {
                         const QuickPickItemView &item = view.items[i];
                         lua_newtable(L);
                         const int it = lua_gettop(L);
                         lua_set_str_field(L, it, "label", item.label);
                         lua_set_str_field(L, it, "detail", item.detail);
                         lua_set_str_field(L, it, "preview", item.preview);
                         lua_set_int_field(L, it, "severity", item.severity);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "items");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_popup(const PopupView &view)
{
  return emit_lua_ui("popup",
                     [&](lua_State *L, int t)
                     {
                       lua_set_str_field(L, t, "title", view.title);
                       lua_set_int_field(L, t, "scroll", view.scroll);
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.lines.size(); i++)
                       {
                         lua_pushlstring(L, view.lines[i].data(), view.lines[i].size());
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "lines");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_prompt(const std::string &name, const PromptView &view)
{
  return emit_lua_ui(name,
                     [&](lua_State *L, int t)
                     {
                       lua_set_str_field(L, t, "input", view.input);
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_tree_sitter_status(const TsStatusView &view)
{
  return emit_lua_ui("tree_sitter_status",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "scroll", view.scroll);
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.rows.size(); i++)
                       {
                         const TsStatusRowView &row = view.rows[i];
                         lua_newtable(L);
                         const int it = lua_gettop(L);
                         lua_set_bool_field(L, it, "section", row.section);
                         lua_set_str_field(L, it, "label", row.label);
                         lua_set_str_field(L, it, "detail", row.detail);
                         lua_set_int_field(L, it, "color", row.color);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "rows");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_lsp_manager(const LspManagerView &view)
{
  return emit_lua_ui("lsp_manager",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "selected", view.selected);
                       lua_set_int_field(L, t, "scroll", view.scroll);
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_int_field(L, t, "label_w", view.label_w);
                       lua_set_int_field(L, t, "state_x", view.state_x);
                       lua_set_int_field(L, t, "action_x", view.action_x);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.rows.size(); i++)
                       {
                         const LspManagerRowView &row = view.rows[i];
                         lua_newtable(L);
                         const int it = lua_gettop(L);
                         lua_set_str_field(L, it, "server", row.server);
                         lua_set_str_field(L, it, "label", row.label);
                         lua_set_str_field(L, it, "state", row.state);
                         lua_set_int_field(L, it, "state_color", row.state_color);
                         lua_newtable(L);
                         const int act = lua_gettop(L);
                         for (size_t k = 0; k < row.actions.size(); k++)
                         {
                           const LspActionView &a = row.actions[k];
                           lua_newtable(L);
                           const int ai = lua_gettop(L);
                           lua_set_str_field(L, ai, "action", a.action);
                           lua_set_str_field(L, ai, "label", a.label);
                           lua_set_str_field(L, ai, "variant", a.variant);
                           lua_set_bool_field(L, ai, "enabled", a.enabled);
                           lua_set_bool_field(L, ai, "focused", a.focused);
                           lua_set_int_field(L, ai, "x", a.x);
                           lua_set_int_field(L, ai, "y", a.y);
                           lua_set_int_field(L, ai, "w", a.w);
                           lua_rawseti(L, act, (lua_Integer)k + 1);
                         }
                         lua_setfield(L, it, "actions");
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "rows");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_telescope(const TelescopeView &view)
{
  return emit_lua_ui("telescope",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_int_field(L, t, "inner_x", view.inner_x);
                       lua_set_int_field(L, t, "inner_y", view.inner_y);
                       lua_set_int_field(L, t, "inner_w", view.inner_w);
                       lua_set_int_field(L, t, "inner_h", view.inner_h);
                       lua_set_int_field(L, t, "query_x", view.query_x);
                       lua_set_int_field(L, t, "query_y", view.query_y);
                       lua_set_int_field(L, t, "query_w", view.query_w);
                       lua_set_int_field(L, t, "body_y", view.body_y);
                       lua_set_int_field(L, t, "body_h", view.body_h);
                       lua_set_int_field(L, t, "list_x", view.list_x);
                       lua_set_int_field(L, t, "list_y", view.list_y);
                       lua_set_int_field(L, t, "list_w", view.list_w);
                       lua_set_int_field(L, t, "list_h", view.list_h);
                       lua_set_int_field(L, t, "preview_x", view.preview_x);
                       lua_set_int_field(L, t, "preview_y", view.preview_y);
                       lua_set_int_field(L, t, "preview_w", view.preview_w);
                       lua_set_int_field(L, t, "preview_h", view.preview_h);
                       lua_set_int_field(L, t, "footer_y", view.footer_y);
                       lua_set_bool_field(L, t, "show_preview", view.show_preview);
                       lua_set_str_field(L, t, "query", view.query);
                       lua_set_str_field(L, t, "root", view.root);
                       lua_set_str_field(L, t, "title", view.title);
                       lua_set_int_field(L, t, "selected", view.selected);
                       lua_set_int_field(L, t, "list_scroll", view.list_scroll);
                       lua_set_int_field(L, t, "result_count", view.result_count);
                       lua_set_bool_field(L, t, "scan_pending", view.scan_pending);
                       lua_set_str_field(L, t, "focus", view.focus);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.results.size(); i++)
                       {
                         const TelescopeResultView &r = view.results[i];
                         lua_newtable(L);
                         const int it = lua_gettop(L);
                         lua_set_str_field(L, it, "name", r.name);
                         lua_set_str_field(L, it, "parent_path", r.parent_path);
                         lua_set_bool_field(L, it, "is_directory", r.is_directory);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "results");
                       lua_newtable(L); // preview
                       const int pv = lua_gettop(L);
                       lua_set_str_field(L, pv, "title", view.preview.title);
                       lua_set_str_field(L, pv, "detail", view.preview.detail);
                       lua_set_int_field(L, pv, "start_line", view.preview.start_line);
                       lua_set_str_field(L, pv, "extension", view.preview.extension);
                       lua_set_bool_field(L, pv, "is_directory", view.preview.is_directory);
                       lua_set_bool_field(L, pv, "skipped", view.preview.skipped);
                       lua_newtable(L);
                       const int pl = lua_gettop(L);
                       for (size_t i = 0; i < view.preview.lines.size(); i++)
                       {
                         lua_pushlstring(
                             L, view.preview.lines[i].data(), view.preview.lines[i].size());
                         lua_rawseti(L, pl, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, pv, "lines");
                       lua_setfield(L, t, "preview");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_lsp_completion(const CompletionView &view)
{
  return emit_lua_ui("lsp_completion",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_int_field(L, t, "max_items", view.max_items);
                       lua_set_int_field(L, t, "start", view.start);
                       lua_set_int_field(L, t, "selected", view.selected);
                       lua_set_int_field(L, t, "total", view.total);
                       lua_set_int_field(L, t, "all_total", view.all_total);
                       lua_set_bool_field(L, t, "filtered", view.filtered);
                       lua_set_str_field(L, t, "prefix", view.prefix);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.items.size(); i++)
                       {
                         const CompletionItemView &it = view.items[i];
                         lua_newtable(L);
                         const int ii = lua_gettop(L);
                         lua_set_str_field(L, ii, "label", it.label);
                         lua_set_int_field(L, ii, "kind", it.kind);
                         lua_set_str_field(L, ii, "kind_name", it.kind_name);
                         lua_set_str_field(L, ii, "kind_icon", it.kind_icon);
                         lua_set_bool_field(L, ii, "deprecated", it.deprecated);
                         lua_set_str_field(L, ii, "detail", it.detail);
                         lua_set_str_field(L, ii, "documentation", it.documentation);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "items");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_context_menu(const ContextMenuView &view)
{
  return emit_lua_ui("context_menu",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_int_field(L, t, "selected", view.selected);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.items.size(); i++)
                       {
                         const ContextMenuItemView &it = view.items[i];
                         lua_newtable(L);
                         const int ii = lua_gettop(L);
                         lua_set_str_field(L, ii, "label", it.label);
                         lua_set_bool_field(L, ii, "enabled", it.enabled);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "items");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_menu_dropdown(const MenuDropdownView &view)
{
  return emit_lua_ui("menu_dropdown",
                     [&](lua_State *L, int t)
                     {
                       lua_set_str_field(L, t, "menu_label", view.menu_label);
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_int_field(L, t, "selected", view.selected);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.items.size(); i++)
                       {
                         const MenuItemView &it = view.items[i];
                         lua_newtable(L);
                         const int ii = lua_gettop(L);
                         lua_set_str_field(L, ii, "label", it.label);
                         lua_set_bool_field(L, ii, "enabled", it.enabled);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "items");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_home(const HomeView &view)
{
  return emit_lua_ui("home_screen",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "panel_x", view.panel_x);
                       lua_set_int_field(L, t, "panel_y", view.panel_y);
                       lua_set_int_field(L, t, "panel_w", view.panel_w);
                       lua_set_int_field(L, t, "panel_h", view.panel_h);
                       lua_set_str_field(L, t, "wordmark", view.wordmark);
                       lua_set_str_field(L, t, "tagline", view.tagline);
                       lua_set_str_field(L, t, "context", view.context);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.rows.size(); i++)
                       {
                         const HomeEntryView &r = view.rows[i];
                         lua_newtable(L);
                         const int ri = lua_gettop(L);
                         lua_set_str_field(L, ri, "label", r.label);
                         lua_set_str_field(L, ri, "secondary", r.secondary);
                         lua_set_int_field(L, ri, "x", r.x);
                         lua_set_int_field(L, ri, "y", r.y);
                         lua_set_int_field(L, ri, "w", r.w);
                         lua_set_bool_field(L, ri, "section", r.section);
                         lua_set_bool_field(L, ri, "selected", r.selected);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "rows");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_search(const SearchView &view)
{
  return emit_lua_ui("search_panel",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_str_field(L, t, "query", view.query);
                       lua_set_str_field(L, t, "replace_text", view.replace_text);
                       lua_set_str_field(L, t, "count", view.count);
                       lua_set_bool_field(L, t, "replace_visible", view.replace_visible);
                       lua_set_bool_field(L, t, "focus_replace", view.focus_replace);
                       lua_set_bool_field(L, t, "case_sensitive", view.case_sensitive);
                       lua_set_bool_field(L, t, "whole_word", view.whole_word);
                       lua_set_bool_field(L, t, "regex", view.regex);
                       lua_set_bool_field(L, t, "scoped_to_selection", view.scoped_to_selection);
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_status(const StatusView &view)
{
  return emit_lua_ui("status_line",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_str_field(L, t, "message", view.message);
                       lua_set_str_field(L, t, "context", view.context);
                       lua_set_bool_field(L, t, "has_selection", view.has_selection);
                       lua_set_int_field(L, t, "sel_lines", view.sel_lines);
                       lua_set_int_field(L, t, "sel_cols", view.sel_cols);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.segments.size(); i++)
                       {
                         const StatusSegmentView &s = view.segments[i];
                         lua_newtable(L);
                         const int si = lua_gettop(L);
                         lua_set_str_field(L, si, "text", s.text);
                         lua_set_int_field(L, si, "fg", s.fg);
                         lua_set_int_field(L, si, "bg", s.bg);
                         lua_set_bool_field(L, si, "bold", s.bold);
                         lua_set_bool_field(L, si, "optional", s.optional);
                         lua_set_int_field(L, si, "priority", s.priority);
                         lua_set_str_field(L, si, "side", s.side);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "segments");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_sidebar(const SidebarPanelView &view)
{
  return emit_lua_ui("sidebar",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_int_field(L, t, "content_x", view.content_x);
                       lua_set_int_field(L, t, "content_w", view.content_w);
                       lua_set_int_field(L, t, "rail_w", view.rail_w);
                       lua_set_int_field(L, t, "border_fg", view.border_fg);
                       lua_set_int_field(L, t, "bg", view.bg);
                       lua_set_bool_field(L, t, "git_view", view.git_view);
                       lua_set_bool_field(L, t, "resizing", view.resizing);
                       lua_set_int_field(L, t, "rail_explorer_row", view.rail_explorer_row);
                       lua_set_int_field(L, t, "rail_git_row", view.rail_git_row);
                       lua_set_str_field(L, t, "header", view.header);
                       lua_set_int_field(L, t, "header_x", view.header_x);
                       lua_set_int_field(L, t, "header_y", view.header_y);
                       lua_set_int_field(L, t, "header_fg", view.header_fg);
                       lua_set_str_field(L, t, "footer", view.footer);
                       lua_set_int_field(L, t, "footer_x", view.footer_x);
                       lua_set_int_field(L, t, "footer_y", view.footer_y);
                       lua_set_int_field(L, t, "footer_fg", view.footer_fg);
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.rows.size(); i++)
                       {
                         const SidebarPanelRowView &r = view.rows[i];
                         lua_newtable(L);
                         const int ri = lua_gettop(L);
                         lua_set_int_field(L, ri, "x", r.x);
                         lua_set_int_field(L, ri, "y", r.y);
                         lua_set_int_field(L, ri, "w", r.w);
                         lua_set_str_field(L, ri, "text", r.text);
                         lua_set_int_field(L, ri, "text_x", r.text_x);
                         lua_set_str_field(L, ri, "symbol", r.symbol);
                         lua_set_int_field(L, ri, "symbol_x", r.symbol_x);
                         lua_set_int_field(L, ri, "symbol_fg", r.symbol_fg);
                         lua_set_bool_field(L, ri, "symbol_bold", r.symbol_bold);
                         lua_set_int_field(L, ri, "fg", r.fg);
                         lua_set_int_field(L, ri, "bg", r.bg);
                         lua_set_bool_field(L, ri, "bold", r.bold);
                         lua_set_bool_field(L, ri, "active_file", r.active_file);
                         lua_set_int_field(L, ri, "badge_x", r.badge_x);
                         lua_set_str_field(L, ri, "badge", r.badge);
                         lua_set_int_field(L, ri, "badge_fg", r.badge_fg);
                         lua_set_int_field(L, ri, "badge2_x", r.badge2_x);
                         lua_set_str_field(L, ri, "badge2", r.badge2);
                         lua_set_int_field(L, ri, "badge2_fg", r.badge2_fg);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "rows");
                       push_ui_colors(L, t);
                     });
}

bool LuaAPI::emit_side_panel(const SidePanelView &view)
{
  return emit_lua_ui("side_panel",
                     [&](lua_State *L, int t)
                     {
                       lua_set_int_field(L, t, "x", view.x);
                       lua_set_int_field(L, t, "y", view.y);
                       lua_set_int_field(L, t, "w", view.w);
                       lua_set_int_field(L, t, "h", view.h);
                       lua_set_str_field(L, t, "title", view.title);
                       lua_set_str_field(L, t, "header", view.header);
                       lua_set_int_field(L, t, "header_fg", view.header_fg);
                       lua_set_str_field(L, t, "note", view.note);
                       lua_set_int_field(L, t, "note_fg", view.note_fg);
                       lua_set_str_field(L, t, "error", view.error);
                       lua_newtable(L);
                       const int tabs = lua_gettop(L);
                       for (size_t i = 0; i < view.tabs.size(); i++)
                       {
                         lua_newtable(L);
                         const int ti = lua_gettop(L);
                         lua_set_str_field(L, ti, "label", view.tabs[i].label);
                         lua_set_bool_field(L, ti, "active", view.tabs[i].active);
                         lua_rawseti(L, tabs, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "tabs");
                       lua_newtable(L);
                       const int arr = lua_gettop(L);
                       for (size_t i = 0; i < view.rows.size(); i++)
                       {
                         const SidePanelRowView &r = view.rows[i];
                         lua_newtable(L);
                         const int ri = lua_gettop(L);
                         lua_set_str_field(L, ri, "text", r.text);
                         lua_set_str_field(L, ri, "detail", r.detail);
                         lua_set_int_field(L, ri, "fg", r.fg);
                         lua_set_int_field(L, ri, "bg", r.bg);
                         lua_set_bool_field(L, ri, "bold", r.bold);
                         lua_set_bool_field(L, ri, "selected", r.selected);
                         lua_rawseti(L, arr, (lua_Integer)i + 1);
                       }
                       lua_setfield(L, t, "rows");
                       push_ui_colors(L, t);
                     });
}

void LuaAPI::ui_set_cursor(int x, int y)
{
  if (editor && editor->ui)
  {
    editor->ui->set_cursor(x, y);
  }
}

void LuaAPI::ui_hide_cursor()
{
  if (editor && editor->ui)
  {
    editor->ui->hide_cursor();
  }
}
