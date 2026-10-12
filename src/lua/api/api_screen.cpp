// Lua bridge: the raw layer (jot.screen, jot.input, jot.term).
//
// Everything the editor draws ends up as cells in the UI grid, and every
// keystroke and mouse report arrives as one Event. Those two are the bottom of
// the stack: floats, the UI kit, the sidebar, the status line and the panes are
// all written on top of them in Lua or in C++. This file exposes the bottom
// itself, so a plugin that needs something the editor has no concept of -- a
// game, a canvas, a chart, a custom overlay, an input mode of its own -- can
// build it against the same primitives instead of asking for a new API.
//
// Three namespaces:
//   jot.screen  read and write the cell grid, and register the per-frame
//               painter that runs once the editor has drawn.
//   jot.input   the raw event stream, above every mode (including the modals).
//   jot.term    the terminal behind the grid, for protocol bytes the cell model
//               cannot express.
//
// All three are main-thread only, like every other callback the bridge runs:
// the grid and the terminal are the render thread's, and the hooks are invoked
// from the event loop's own dispatch.
#include "core/editor.h"
#include "events/keybind_catalog.h"
#include "lua/api/api.h"
#include "lua/api/api_internal.h"
#include "ui/text.h"
#include "ui/ui.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace
{
  // The ink a call asked for: the theme's default colours when the script named
  // none, exactly what any other painter starts from.
  UICell cell_from_style(const std::string &cluster, const LuaAPI::ScreenStyle &style, const Theme &th)
  {
    UICell cell;
    cell.ch = cluster;
    cell.fg = style.fg >= 0 ? style.fg : th.fg_default;
    cell.bg = style.bg >= 0 ? style.bg : th.bg_default;
    cell.bold = style.bold;
    cell.italic = style.italic;
    cell.dim = style.dim;
    cell.reverse = style.reverse;
    cell.underline = style.underline;
    cell.underline_fg = style.underline_fg;
    return cell;
  }

  // How many grid cells one grapheme cluster covers (1 for text, 2 for the wide
  // East Asian forms). ui_cell_count answers 0 for an empty cluster, which is
  // what a control byte sanitizes to: it still takes a column here, so a run of
  // them cannot collapse into one cell and shift the rest of the text left.
  int cluster_cells(const std::string &cluster)
  {
    return std::max(1, ui_cell_count(cluster));
  }

  // The key payload every key handler gets, base hook and mode alike. `mode` is
  // the stack handle of the owner being called, 0 for the base hook, so a
  // handler can tell whether it is the one on top (and, in a shared callback,
  // which layer it is acting for).
  void push_key_event(
      lua_State *L, int ch, bool ctrl, bool shift, bool alt, int original_ch, int mode)
  {
    lua_newtable(L);
    // The raw key code: a printable key is its own byte, Enter is 13, Esc 27 and
    // the function keys live in KeyCode's high range. `char` is the same code
    // when it is a printable ASCII byte and nil otherwise, so a script that
    // wants to type what it does not consume does not have to test the range.
    lua_pushinteger(L, ch);
    lua_setfield(L, -2, "key");
    if (ch >= 32 && ch < 127)
    {
      lua_pushlstring(L, (const char *)&ch, 1);
      lua_setfield(L, -2, "char");
    }
    lua_pushinteger(L, original_ch);
    lua_setfield(L, -2, "original_key");
    // The chord name the rest of the system uses for this key ("F2", "Ctrl+K",
    // "S"): matching on the raw code means hard-coding KeyCode's high range, so
    // the name is the field a hook normally switches on.
    const std::string chord =
        jot::keybind_detail::chord_name(ch, ctrl, shift, alt, original_ch);
    lua_pushlstring(L, chord.data(), chord.size());
    lua_setfield(L, -2, "name");
    lua_pushboolean(L, ctrl);
    lua_setfield(L, -2, "ctrl");
    lua_pushboolean(L, shift);
    lua_setfield(L, -2, "shift");
    lua_pushboolean(L, alt);
    lua_setfield(L, -2, "alt");
    lua_pushinteger(L, mode);
    lua_setfield(L, -2, "mode");
  }

  // Calls the handler sitting on the stack directly under its event table, with
  // that one argument, and answers whether it consumed the event. An error is
  // reported and the event counts as declined, so a broken handler costs its own
  // key instead of the session (the caller sets the stack back either way).
  bool call_event_handler(lua_State *L, const char *what)
  {
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
      std::cerr << "Lua " << what << " handler error: " << lua_tostring(L, -1) << "\n";
      return false;
    }
    return lua_toboolean(L, -1) != 0;
  }

  // The mouse payload, same contract.
  void push_mouse_event(lua_State *L,
                        int x,
                        int y,
                        int button,
                        bool pressed,
                        bool released,
                        bool motion,
                        bool ctrl,
                        bool shift,
                        bool alt,
                        int mode)
  {
    lua_newtable(L);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, button);
    lua_setfield(L, -2, "button");
    lua_pushboolean(L, pressed);
    lua_setfield(L, -2, "pressed");
    lua_pushboolean(L, released);
    lua_setfield(L, -2, "released");
    lua_pushboolean(L, motion);
    lua_setfield(L, -2, "motion");
    lua_pushboolean(L, ctrl);
    lua_setfield(L, -2, "ctrl");
    lua_pushboolean(L, shift);
    lua_setfield(L, -2, "shift");
    lua_pushboolean(L, alt);
    lua_setfield(L, -2, "alt");
    lua_pushinteger(L, mode);
    lua_setfield(L, -2, "mode");
  }
} // namespace

int LuaAPI::screen_write(int x, int y, const std::string &text, const ScreenStyle &style)
{
  if (!editor || !editor->ui || text.empty())
    return 0;
  UI *ui = editor->ui;
  if (y < 0 || y >= ui->get_height())
    return 0;
  const Theme &th = editor->get_theme();
  int col = x;
  int painted = 0;
  int at = 0;
  const int len = (int)text.size();
  while (at < len && col < ui->get_width())
  {
    int end = ui_next_grapheme_boundary(text, at);
    if (end <= at)
    {
      end = at + 1; // a byte that is not a boundary start: step over it
    }
    // A newline ends the run. A raw writer that silently wrapped would paint a
    // multi-row string on one row, past the right edge, out of the caller's
    // sight: stop instead, so the grid always shows what was asked for.
    if (text[at] == '\n' || text[at] == '\r')
    {
      break;
    }
    std::string cluster;
    ui_sanitize_cell_range(text, at, end, cluster);
    at = end;
    const int cells = cluster_cells(cluster);
    const UICell cell = cell_from_style(cluster, style, th);
    ui->paint_cell(col, y, cell);
    for (int fill = 1; fill < cells; fill++)
    {
      UICell continuation = cell;
      continuation.ch = "";
      ui->paint_cell(col + fill, y, continuation);
    }
    if (col >= 0)
    {
      painted += std::min(cells, std::max(0, ui->get_width() - col));
    }
    col += cells;
  }
  return painted;
}

int LuaAPI::screen_fill(
    int x, int y, int w, int h, const std::string &text, const ScreenStyle &style)
{
  if (!editor || !editor->ui || w <= 0 || h <= 0)
    return 0;
  UI *ui = editor->ui;
  const Theme &th = editor->get_theme();
  // A fill is one cluster repeated, not the string tiled along the row: the
  // glyph is the first cluster of `text` (a space when it is empty).
  std::string cluster = " ";
  if (!text.empty() && text[0] != '\n' && text[0] != '\r')
  {
    int end = ui_next_grapheme_boundary(text, 0);
    if (end <= 0)
    {
      end = 1;
    }
    ui_sanitize_cell_range(text, 0, std::min(end, (int)text.size()), cluster);
    if (cluster.empty())
    {
      cluster = " ";
    }
  }
  const int cells = cluster_cells(cluster);
  const UICell cell = cell_from_style(cluster, style, th);
  UICell continuation = cell;
  continuation.ch = "";
  const int x1 = std::min(ui->get_width(), x + w);
  const int y1 = std::min(ui->get_height(), y + h);
  int painted = 0;
  for (int row = std::max(0, y); row < y1; row++)
  {
    for (int col = std::max(0, x); col < x1; col += cells)
    {
      ui->paint_cell(col, row, cell);
      for (int fill = 1; fill < cells; fill++)
      {
        ui->paint_cell(col + fill, row, continuation);
      }
      painted += cells;
    }
  }
  return painted;
}

void LuaAPI::screen_cell_to_lua(lua_State *L, int x, int y)
{
  if (!editor || !editor->ui)
  {
    lua_pushnil(L);
    return;
  }
  const UICell *cell = editor->ui->cell_at(x, y);
  if (!cell)
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  lua_pushlstring(L, cell->ch.data(), cell->ch.size());
  lua_setfield(L, -2, "text");
  jot_lua::lua_push_int_field(L, "fg", cell->fg);
  jot_lua::lua_push_int_field(L, "bg", cell->bg);
  jot_lua::lua_push_bool_field(L, "bold", cell->bold);
  jot_lua::lua_push_bool_field(L, "italic", cell->italic);
  jot_lua::lua_push_bool_field(L, "dim", cell->dim);
  jot_lua::lua_push_bool_field(L, "reverse", cell->reverse);
  jot_lua::lua_push_int_field(L, "underline", cell->underline);
  jot_lua::lua_push_int_field(L, "underline_fg", cell->underline_fg);
  // The exact 24-bit colour behind the cell, when it carries one (a hex colour
  // the script painted, or a theme slot that was not a palette entry). -1 says
  // the cell is a palette index, which is what the fg/bg fields above hold.
  lua_pushinteger(L, cell->fg_rgb == kNoRgb ? -1 : (lua_Integer)cell->fg_rgb);
  lua_setfield(L, -2, "fg_rgb");
  lua_pushinteger(L, cell->bg_rgb == kNoRgb ? -1 : (lua_Integer)cell->bg_rgb);
  lua_setfield(L, -2, "bg_rgb");
}

void LuaAPI::screen_text_to_lua(lua_State *L, int x, int y, int w)
{
  std::string out;
  if (editor && editor->ui && w > 0)
  {
    for (int i = 0; i < w; i++)
    {
      const UICell *cell = editor->ui->cell_at(x + i, y);
      if (!cell)
      {
        break;
      }
      out += cell->ch.empty() ? " " : cell->ch;
    }
  }
  lua_pushlstring(L, out.data(), out.size());
}

void LuaAPI::screen_size_to_lua(lua_State *L)
{
  const bool grid = editor && editor->ui && editor->ui->has_grid();
  lua_pushinteger(L, grid ? editor->ui->get_width() : 0);
  lua_pushinteger(L, grid ? editor->ui->get_height() : 0);
}

void LuaAPI::screen_redraw()
{
  if (editor)
  {
    editor->host().render.request_redraw();
  }
}

void LuaAPI::set_paint_hook(lua_State *L, int fn_index)
{
  if (paint_hook_ref_ >= 0)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, paint_hook_ref_);
    paint_hook_ref_ = LUA_NOREF;
  }
  if (lua_isnoneornil(L, fn_index))
  {
    screen_redraw(); // the cells it painted go away with the next frame
    return;
  }
  luaL_checktype(L, fn_index, LUA_TFUNCTION);
  lua_pushvalue(L, fn_index);
  paint_hook_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
}

bool LuaAPI::run_paint_hook()
{
  if (paint_hook_ref_ < 0 || !lua_state || !editor)
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, paint_hook_ref_);
  lua_pushinteger(L, editor->ui && editor->ui->has_grid() ? editor->ui->get_width() : 0);
  lua_pushinteger(L, editor->ui && editor->ui->has_grid() ? editor->ui->get_height() : 0);
  const int want_next_frame = lua_pcall(L, 2, 1, 0);
  bool again = false;
  if (want_next_frame != LUA_OK)
  {
    std::cerr << "Lua paint hook error: " << lua_tostring(L, -1) << "\n";
    // A painter that throws on every frame would flood the log and paint
    // nothing: drop it, the way a broken autocmd is reported once. The script
    // re-registers it if it wants another try.
    luaL_unref(L, LUA_REGISTRYINDEX, paint_hook_ref_);
    paint_hook_ref_ = LUA_NOREF;
  }
  else
  {
    again = lua_toboolean(L, -1) != 0;
  }
  lua_settop(L, top);
  return again;
}

int LuaAPI::push_input_mode_from_lua(lua_State *L, int arg_index)
{
  InputMode mode;
  if (lua_isfunction(L, arg_index))
  {
    lua_pushvalue(L, arg_index);
    mode.key_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else if (lua_istable(L, arg_index))
  {
    mode.name = jot_lua::table_string(L, arg_index, "name", "");
    for (const char *field : {"on_key", "on_mouse"})
    {
      lua_getfield(L, arg_index, field);
      if (lua_isfunction(L, -1))
      {
        const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (std::string(field) == "on_key")
        {
          mode.key_ref = ref;
        }
        else
        {
          mode.mouse_ref = ref;
        }
      }
      else
      {
        lua_pop(L, 1);
      }
    }
    if (mode.key_ref < 0 && mode.mouse_ref < 0)
    {
      luaL_error(L, "jot.input.push_mode: the table needs on_key or on_mouse");
      return 0;
    }
  }
  else
  {
    luaL_error(L, "jot.input.push_mode: expected a function or a table");
    return 0;
  }
  mode.handle = next_input_mode_handle_++;
  input_modes_.push_back(mode);
  return mode.handle;
}

bool LuaAPI::pop_input_mode_from_lua(lua_State *L)
{
  if (input_modes_.empty())
  {
    return false;
  }
  const int wanted = (int)luaL_optinteger(L, 1, 0);
  auto remove_at = [this, L](std::vector<InputMode>::iterator it)
  {
    if (lua_state)
    {
      lua_State *S = static_cast<lua_State *>(lua_state);
      for (int ref : {it->key_ref, it->mouse_ref})
      {
        if (ref >= 0)
        {
          luaL_unref(S, LUA_REGISTRYINDEX, ref);
        }
      }
    }
    input_modes_.erase(it);
  };
  if (wanted <= 0)
  {
    remove_at(input_modes_.end() - 1); // no handle named: the top one
    return true;
  }
  for (auto it = input_modes_.begin(); it != input_modes_.end(); ++it)
  {
    if (it->handle == wanted)
    {
      remove_at(it);
      return true;
    }
  }
  return false;
}

void LuaAPI::input_modes_to_lua(lua_State *L)
{
  lua_newtable(L);
  int index = 1;
  // Top first: the order they are asked in, so a script can see who owns the
  // keyboard right now by looking at entry 1.
  for (auto it = input_modes_.rbegin(); it != input_modes_.rend(); ++it)
  {
    lua_newtable(L);
    jot_lua::lua_push_int_field(L, "handle", it->handle);
    lua_pushlstring(L, it->name.data(), it->name.size());
    lua_setfield(L, -2, "name");
    lua_rawseti(L, -2, index++);
  }
}

void LuaAPI::set_raw_key_hook(lua_State *L, int fn_index)
{
  if (raw_key_ref_ >= 0)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, raw_key_ref_);
    raw_key_ref_ = LUA_NOREF;
  }
  if (lua_isnoneornil(L, fn_index))
  {
    return;
  }
  luaL_checktype(L, fn_index, LUA_TFUNCTION);
  lua_pushvalue(L, fn_index);
  raw_key_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
}

void LuaAPI::set_raw_mouse_hook(lua_State *L, int fn_index)
{
  if (raw_mouse_ref_ >= 0)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, raw_mouse_ref_);
    raw_mouse_ref_ = LUA_NOREF;
  }
  if (lua_isnoneornil(L, fn_index))
  {
    return;
  }
  luaL_checktype(L, fn_index, LUA_TFUNCTION);
  lua_pushvalue(L, fn_index);
  raw_mouse_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
}

bool LuaAPI::raw_key_input(int ch, bool ctrl, bool shift, bool alt, int original_ch)
{
  if (!lua_state || !editor || !editor->event_loop_.is_main_thread())
    return false;
  if (raw_key_ref_ < 0 && input_modes_.empty())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  // Top down: the innermost owner gets the key first and the first one that
  // returns true ends the walk. A mode under it only sees the key the mode above
  // declined, which is what makes nesting compose.
  for (auto it = input_modes_.rbegin(); it != input_modes_.rend(); ++it)
  {
    if (it->key_ref < 0)
      continue;
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->key_ref);
    push_key_event(L, ch, ctrl, shift, alt, original_ch, it->handle);
    if (call_event_handler(L, "key"))
    {
      lua_settop(L, top);
      return true;
    }
    lua_settop(L, top);
  }
  if (raw_key_ref_ < 0)
    return false;
  lua_rawgeti(L, LUA_REGISTRYINDEX, raw_key_ref_);
  push_key_event(L, ch, ctrl, shift, alt, original_ch, 0);
  const bool consume = call_event_handler(L, "key");
  lua_settop(L, top);
  return consume;
}

bool LuaAPI::raw_mouse_input(int x,
                             int y,
                             int button,
                             bool pressed,
                             bool released,
                             bool motion,
                             bool ctrl,
                             bool shift,
                             bool alt)
{
  if (!lua_state || !editor || !editor->event_loop_.is_main_thread())
    return false;
  if (raw_mouse_ref_ < 0 && input_modes_.empty())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  for (auto it = input_modes_.rbegin(); it != input_modes_.rend(); ++it)
  {
    if (it->mouse_ref < 0)
      continue;
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->mouse_ref);
    push_mouse_event(L, x, y, button, pressed, released, motion, ctrl, shift, alt, it->handle);
    if (call_event_handler(L, "mouse"))
    {
      lua_settop(L, top);
      return true;
    }
    lua_settop(L, top);
  }
  if (raw_mouse_ref_ < 0)
    return false;
  lua_rawgeti(L, LUA_REGISTRYINDEX, raw_mouse_ref_);
  push_mouse_event(L, x, y, button, pressed, released, motion, ctrl, shift, alt, 0);
  const bool consume = call_event_handler(L, "mouse");
  lua_settop(L, top);
  return consume;
}

bool LuaAPI::term_write(const std::string &bytes)
{
  if (!editor || !editor->ui || bytes.empty())
    return false;
  // The frame's own byte stream, written after the cells and before the caret
  // (see UI::render). It is the same buffered write the grid rides, so a
  // sequence lands in this frame and in order, never in a separate block.
  editor->ui->set_frame_graphics(editor->ui->frame_graphics_ + bytes);
  return true;
}

void LuaAPI::term_info_to_lua(lua_State *L)
{
  const bool ui = editor && editor->ui;
  lua_pushinteger(L, ui ? editor->ui->get_width() : 0);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, ui ? editor->ui->get_height() : 0);
  lua_setfield(L, -2, "height");
  // The terminal's own size in cells, which differs from the grid's only while
  // a text scale is in effect (a scaled grid cell covers scale x scale of them).
  lua_pushinteger(L, editor ? editor->terminal.get_width() : 0);
  lua_setfield(L, -2, "columns");
  lua_pushinteger(L, editor ? editor->terminal.get_height() : 0);
  lua_setfield(L, -2, "rows");
  lua_pushinteger(L, ui ? editor->ui->get_cell_scale() : 1);
  lua_setfield(L, -2, "cell_scale");
  lua_pushboolean(L, editor && editor->terminal.supports_truecolor());
  lua_setfield(L, -2, "truecolor");
  lua_pushinteger(L, editor ? editor->terminal.cell_pixel_width() : 0);
  lua_setfield(L, -2, "cell_pixel_width");
  lua_pushinteger(L, editor ? editor->terminal.cell_pixel_height() : 0);
  lua_setfield(L, -2, "cell_pixel_height");
}

void LuaAPI::clear_raw_hooks()
{
  lua_State *L = static_cast<lua_State *>(lua_state);
  if (L)
  {
    for (int *ref : {&paint_hook_ref_, &raw_key_ref_, &raw_mouse_ref_})
    {
      if (*ref >= 0)
      {
        luaL_unref(L, LUA_REGISTRYINDEX, *ref);
      }
    }
    for (InputMode &mode : input_modes_)
    {
      for (int ref : {mode.key_ref, mode.mouse_ref})
      {
        if (ref >= 0)
        {
          luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }
      }
    }
  }
  input_modes_.clear();
  paint_hook_ref_ = LUA_NOREF;
  raw_key_ref_ = LUA_NOREF;
  raw_mouse_ref_ = LUA_NOREF;
}
