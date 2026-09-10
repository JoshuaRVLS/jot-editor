// LuaAPI editor-level queries and actions: document symbols, recent
// files and workspaces, popup, theme palette, clipboard, motion
// commands, sidebar info, and search/picker state.
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

#include "tools/symbols/index.h"
#include "ui/components.h"
#include "ui/text.h"
#include <string>
#include <vector>

void LuaAPI::push_symbols(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy() || buf.filepath.empty() || buf.line_count() == 0)
    return;
  const std::vector<SymbolMatch> symbols =
      SymbolIndex::extract_document_symbols(buf.lines, buf.filepath);
  int n = 1;
  for (const auto &s : symbols)
  {
    lua_newtable(L);
    lua_push_str_field(L, "name", s.name);
    lua_push_str_field(L, "kind", s.kind);
    lua_push_str_field(L, "detail", s.detail);
    lua_push_int_field(L, "line", (long long)s.line + 1);
    lua_push_int_field(L, "column", (long long)s.column + 1);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::clipboard_copy_from_lua(lua_State *L)
{
  if (editor)
    editor->copy();
}
void LuaAPI::clipboard_cut_from_lua(lua_State *L)
{
  if (editor)
    editor->cut();
}
void LuaAPI::clipboard_paste_from_lua(lua_State *L)
{
  if (editor)
    editor->paste();
}
void LuaAPI::clipboard_set_from_lua(lua_State *L)
{
  if (editor)
  {
    const char *text = lua_tostring(L, 1);
    editor->set_clipboard_text(text ? text : "");
  }
}

void LuaAPI::push_workspace_path(lua_State *L)
{
  if (!editor || editor->root_dir.empty())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->root_dir.c_str());
}

void LuaAPI::push_recent_files(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &p : editor->recent_files)
  {
    lua_pushstring(L, p.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_recent_workspaces(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &p : editor->recent_workspaces)
  {
    lua_pushstring(L, p.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::popup_from_lua(lua_State *L)
{
  if (!editor)
    return;
  editor->show_popup(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""));
}

// Canonical token-type names, indexed by the shared TreeSitterTokenKind ids
// used by both the regex highlighter and the tree-sitter query engine.
void LuaAPI::push_search_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  lua_push_bool_field(L, "visible", editor->show_search);
  lua_push_str_field(L, "query", editor->search_query);
  lua_push_bool_field(L, "case_sensitive", editor->search_case_sensitive);
  lua_push_bool_field(L, "whole_word", editor->search_whole_word);
  lua_push_bool_field(L, "regex", editor->search_regex);
  lua_push_bool_field(L, "replace", editor->search_replace_visible);
  lua_push_str_field(L, "replace_text", editor->search_replace_text);
  lua_push_bool_field(L, "scoped", editor->search_scoped_to_selection);
  lua_push_int_field(L, "result_count", (long long)editor->search_results.size());
  lua_push_int_field(L,
                     "result_index",
                     editor->search_result_index >= 0 ? (long long)editor->search_result_index + 1
                                                      : 0);
}

void LuaAPI::push_search_matches(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const SearchMatch &m : editor->search_results)
  {
    lua_newtable(L);
    lua_push_int_field(L, "line", (long long)m.line + 1);
    lua_push_int_field(L, "column", (long long)m.col + 1);
    lua_push_int_field(L, "len", m.len);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_picker_active(lua_State *L)
{
  lua_pushboolean(L, editor && editor->show_quick_pick);
}

void LuaAPI::push_picker_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor || !editor->show_quick_pick)
    return;
  lua_push_str_field(L, "title", editor->quick_pick_title);
  lua_push_str_field(L, "query", editor->quick_pick_query);
  lua_push_int_field(L, "selected", (long long)editor->quick_pick_selected + 1);
  lua_push_int_field(L, "visible", (long long)editor->quick_pick_items.size());
  lua_push_int_field(L, "total", (long long)editor->quick_pick_all_items.size());
}

void LuaAPI::push_picker_items(lua_State *L)
{
  lua_newtable(L);
  if (!editor || !editor->show_quick_pick)
    return;
  int n = 1;
  for (const QuickPickItem &item : editor->quick_pick_items)
  {
    lua_newtable(L);
    lua_push_str_field(L, "label", item.label);
    lua_push_str_field(L, "detail", item.detail);
    lua_push_str_field(L, "preview", item.preview);
    lua_push_str_field(L, "filepath", item.filepath);
    lua_push_int_field(L, "line", item.line >= 0 ? (long long)item.line + 1 : 0);
    lua_push_int_field(L, "column", item.col >= 0 ? (long long)item.col + 1 : 0);
    lua_push_int_field(L, "severity", item.severity);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::picker_accept_from_lua(lua_State *L)
{
  if (!editor || !editor->show_quick_pick)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->accept_quick_pick();
  lua_pushboolean(L, 1);
}

void LuaAPI::picker_close_from_lua(lua_State *L)
{
  if (!editor || !editor->show_quick_pick)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->close_quick_pick();
  lua_pushboolean(L, 1);
}

void LuaAPI::push_theme_palette(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const Theme &t = editor->theme;
  lua_newtable(L);
  auto color = [&](const char *slot, int fg, int bg)
  {
    lua_newtable(L);
    lua_push_int_field(L, "fg", fg);
    if (bg >= 0)
      lua_push_int_field(L, "bg", bg);
    lua_setfield(L, -2, slot);
  };
  auto flag = [&](const char *slot, bool v) { lua_push_bool_field(L, slot, v); };
  color("default", t.fg_default, t.bg_default);
  color("keyword", t.fg_keyword, t.bg_keyword);
  color("string", t.fg_string, t.bg_string);
  color("comment", t.fg_comment, t.bg_comment);
  color("number", t.fg_number, t.bg_number);
  color("function", t.fg_function, t.bg_function);
  color("type", t.fg_type, t.bg_type);
  color("variable", t.fg_variable, t.bg_variable);
  color("constant", t.fg_constant, t.bg_constant);
  color("builtin", t.fg_builtin, t.bg_builtin);
  color("operator", t.fg_operator, t.bg_operator);
  color("punctuation", t.fg_punctuation, t.bg_punctuation);
  color("tag", t.fg_tag, t.bg_tag);
  color("attribute", t.fg_attribute, t.bg_attribute);
  color("namespace", t.fg_namespace, t.bg_namespace);
  color("module", t.fg_module, t.bg_module);
  color("parameter", t.fg_parameter, t.bg_parameter);
  color("field", t.fg_field, t.bg_field);
  color("keyword_control", t.fg_keyword_control, t.bg_keyword_control);
  color("keyword_storage", t.fg_keyword_storage, t.bg_keyword_storage);
  color("keyword_preproc", t.fg_keyword_preproc, t.bg_keyword_preproc);
  color("function_method", t.fg_function_method, t.bg_function_method);
  color("function_constructor", t.fg_function_constructor, t.bg_function_constructor);
  color("type_builtin", t.fg_type_builtin, t.bg_type_builtin);
  color("constant_macro", t.fg_constant_macro, t.bg_constant_macro);
  color("string_escape", t.fg_string_escape, t.bg_string_escape);
  color("punctuation_bracket", t.fg_punctuation_bracket, t.bg_punctuation_bracket);
  color("punctuation_delimiter", t.fg_punctuation_delimiter, t.bg_punctuation_delimiter);
  color("panel_border", t.fg_panel_border, t.bg_panel_border);
  color("selection", t.fg_selection, t.bg_selection);
  color("line_num", t.fg_line_num, t.bg_line_num);
  color("cursor", t.fg_cursor, t.bg_cursor);
  color("cursor_line_num", t.fg_cursor_line_num, -1);
  color("cursor_line", -1, t.bg_cursor_line);
  color("status", t.fg_status, t.bg_status);
  color("status_message", t.fg_status_message, -1);
  color("status_logo", t.fg_status_logo, t.bg_status_logo);
  color("status_file", t.fg_status_file, t.bg_status_file);
  color("status_info", t.fg_status_info, t.bg_status_info);
  color("status_warning", t.fg_status_warning, t.bg_status_warning);
  color("status_error", t.fg_status_error, t.bg_status_error);
  color("status_muted", t.fg_status_muted, t.bg_status_muted);
  color("command", t.fg_command, t.bg_command);
  color("search_match", t.fg_search_match, t.bg_search_match);
  color("search_current", t.fg_search_current, t.bg_search_current);
  color("minimap", t.fg_minimap, t.bg_minimap);
  color("sidebar", t.fg_sidebar, t.bg_sidebar);
  color("sidebar_directory", t.fg_sidebar_directory, -1);
  color("sidebar_selected", t.fg_sidebar_selected, t.bg_sidebar_selected);
  color(
      "sidebar_selected_inactive", t.fg_sidebar_selected_inactive, t.bg_sidebar_selected_inactive);
  color("sidebar_border", t.fg_sidebar_border, -1);
  color("tab_active", t.fg_tab_active, t.bg_tab_active);
  color("tab_inactive", t.fg_tab_inactive, t.bg_tab_inactive);
  color("tab_close", t.fg_tab_close, -1);
  color("tab_separator", t.fg_tab_separator, -1);
  color("active_border", t.fg_active_border, t.bg_active_border);
  color("image_border", t.fg_image_border, t.bg_image_border);
  color("diagnostic_error", t.fg_diagnostic_error, -1);
  color("diagnostic_warning", t.fg_diagnostic_warning, -1);
  color("diagnostic_info", t.fg_diagnostic_info, -1);
  color("diagnostic_hint", t.fg_diagnostic_hint, -1);
  color("bracket1", t.fg_bracket1, -1);
  color("bracket2", t.fg_bracket2, -1);
  color("bracket3", t.fg_bracket3, -1);
  color("bracket4", t.fg_bracket4, -1);
  color("bracket5", t.fg_bracket5, -1);
  color("bracket6", t.fg_bracket6, -1);
  color("bracket_match", t.fg_bracket_match, t.bg_bracket_match);
  color("telescope", t.fg_telescope, t.bg_telescope);
  color("telescope_selected", t.fg_telescope_selected, t.bg_telescope_selected);
  color("telescope_preview", t.fg_telescope_preview, t.bg_telescope_preview);
  color("terminal", t.fg_terminal, t.bg_terminal);
  color("terminal_tab_inactive", t.fg_terminal_tab_inactive, t.bg_terminal_tab_inactive);
  color("terminal_tab_active", t.fg_terminal_tab_active, t.bg_terminal_tab_active);
  color("terminal_tab_focused", t.fg_terminal_tab_focused, t.bg_terminal_tab_focused);
  color("terminal_tab_close", t.fg_terminal_tab_close, -1);
  color("terminal_tab_plus", t.fg_terminal_tab_plus, -1);
  color("terminal_tab_separator", t.fg_terminal_tab_separator, -1);
  color("git_modified", t.fg_git_modified, t.bg_git_modified);
  color("git_added", t.fg_git_added, t.bg_git_added);
  color("git_deleted", t.fg_git_deleted, t.bg_git_deleted);
  color("git_renamed", t.fg_git_renamed, t.bg_git_renamed);
  color("git_untracked", t.fg_git_untracked, t.bg_git_untracked);
  color("git_conflict", t.fg_git_conflict, t.bg_git_conflict);
  flag("syntax_variable_explicit", t.syntax_variable_explicit);
  flag("syntax_constant_explicit", t.syntax_constant_explicit);
  flag("syntax_builtin_explicit", t.syntax_builtin_explicit);
  flag("syntax_operator_explicit", t.syntax_operator_explicit);
  flag("syntax_punctuation_explicit", t.syntax_punctuation_explicit);
  flag("syntax_tag_explicit", t.syntax_tag_explicit);
  flag("syntax_attribute_explicit", t.syntax_attribute_explicit);
  flag("syntax_namespace_explicit", t.syntax_namespace_explicit);
  flag("syntax_module_explicit", t.syntax_module_explicit);
  flag("syntax_parameter_explicit", t.syntax_parameter_explicit);
  flag("syntax_field_explicit", t.syntax_field_explicit);
  flag("syntax_keyword_control_explicit", t.syntax_keyword_control_explicit);
  flag("syntax_keyword_storage_explicit", t.syntax_keyword_storage_explicit);
  flag("syntax_keyword_preproc_explicit", t.syntax_keyword_preproc_explicit);
  flag("syntax_function_method_explicit", t.syntax_function_method_explicit);
  flag("syntax_function_constructor_explicit", t.syntax_function_constructor_explicit);
  flag("syntax_type_builtin_explicit", t.syntax_type_builtin_explicit);
  flag("syntax_constant_macro_explicit", t.syntax_constant_macro_explicit);
  flag("syntax_string_escape_explicit", t.syntax_string_escape_explicit);
  flag("syntax_punctuation_bracket_explicit", t.syntax_punctuation_bracket_explicit);
  flag("syntax_punctuation_delimiter_explicit", t.syntax_punctuation_delimiter_explicit);
}

void LuaAPI::push_clipboard_text(lua_State *L)
{
  lua_pushstring(L, editor ? editor->clipboard.c_str() : "");
}

void LuaAPI::motion_from_lua(lua_State *L, int kind)
{
  if (!editor)
    return;
  switch (kind)
  {
  case 0:
    editor->move_word_forward(false);
    break;
  case 1:
    editor->move_word_backward(false);
    break;
  case 2:
    editor->move_to_line_smart_start(false);
    break;
  case 3:
    editor->move_to_line_end(false);
    break;
  case 4:
    editor->move_to_file_start(false);
    break;
  case 5:
    editor->move_to_file_end(false);
    break;
  case 6:
    editor->jump_to_matching_bracket();
    break;
  case 7:
    editor->select_current_function();
    break;
  default:
    return;
  }
  editor->needs_redraw = true;
}

void LuaAPI::push_sidebar_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const bool git = editor->active_sidebar_view == editor->SIDEBAR_VIEW_GIT;
  lua_push_bool_field(L, "visible", editor->show_sidebar);
  lua_push_int_field(L, "width", editor->sidebar_width);
  lua_push_str_field(L, "view", git ? "git" : "explorer");
  lua_push_int_field(
      L, "selected", git ? editor->git_sidebar_selected : editor->file_tree_selected);
  lua_push_int_field(L, "scroll", git ? editor->git_sidebar_scroll : editor->file_tree_scroll);
}

void LuaAPI::sidebar_set_view_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const std::string view = luaL_checkstring(L, 1);
  editor->active_sidebar_view =
      view == "git" ? editor->SIDEBAR_VIEW_GIT : editor->SIDEBAR_VIEW_EXPLORER;
  editor->show_sidebar = true;
  editor->needs_redraw = true;
}

