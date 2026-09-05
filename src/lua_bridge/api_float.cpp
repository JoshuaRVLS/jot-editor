// Lua float-window / scratch-buffer bridge: the model backing jot.ui.float and
// jot.buffer scratch surfaces, plus key/mouse dispatch to Lua callbacks and the
// render pass that paints floats over the editor. Split out of api_core.cpp.
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"
#include "ui/components.h"
#include "ui/text.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

int LuaAPI::create_scratch_buffer(bool listed, bool scratch)
{
  int id = next_scratch_buffer++;
  scratch_buffers.emplace(id, LuaScratchBuffer{id, listed, scratch, true, {""}});
  return id;
}
bool LuaAPI::set_scratch_lines(
    int id, int start, int end, bool strict, const std::vector<std::string> &v)
{
  auto it = scratch_buffers.find(id);
  if (it == scratch_buffers.end() || !it->second.valid)
    return false;
  auto &l = it->second.lines;
  int n = (int)l.size();
  if (start < 0)
    start = n + start;
  if (end < 0)
    end = n + end;
  if (strict && (start < 0 || end < start || start > n || end > n))
    return false;
  start = std::clamp(start, 0, n);
  end = std::clamp(end, start, n);
  l.erase(l.begin() + start, l.begin() + end);
  l.insert(l.begin() + start, v.begin(), v.end());
  if (l.empty())
    l.push_back("");
  return true;
}
std::vector<std::string> LuaAPI::get_scratch_lines(int id, int start, int end, bool strict) const
{
  auto it = scratch_buffers.find(id);
  if (it == scratch_buffers.end() || !it->second.valid)
    return {};
  auto l = it->second.lines;
  int n = (int)l.size();
  if (start < 0)
    start = n + start;
  if (end < 0)
    end = n + end;
  if (strict && (start < 0 || end < start || start > n || end > n))
    return {};
  start = std::clamp(start, 0, n);
  end = std::clamp(end, start, n);
  return {l.begin() + start, l.begin() + end};
}
bool LuaAPI::delete_scratch_buffer(int id)
{
  auto it = scratch_buffers.find(id);
  if (it == scratch_buffers.end())
    return false;
  for (auto wi = float_windows.begin(); wi != float_windows.end();)
    if (wi->second.buffer == id)
    {
      if (lua_state)
      {
        if (wi->second.key_callback >= 0)
          luaL_unref((lua_State *)lua_state, LUA_REGISTRYINDEX, wi->second.key_callback);
        if (wi->second.mouse_callback >= 0)
          luaL_unref((lua_State *)lua_state, LUA_REGISTRYINDEX, wi->second.mouse_callback);
      }
      wi = float_windows.erase(wi);
    }
    else
      ++wi;
  it->second.valid = false;
  scratch_buffers.erase(it);
  return true;
}
bool LuaAPI::configure_float(int id, lua_State *L, int ti)
{
  auto it = float_windows.find(id);
  if (it == float_windows.end())
    return false;
  auto &f = it->second;
  f.x = table_int(L, ti, "col", f.x);
  f.y = table_int(L, ti, "row", f.y);
  f.col = f.x;
  f.row = f.y;
  f.w = table_int(L, ti, "width", f.w);
  f.h = table_int(L, ti, "height", f.h);
  f.relative = table_string(L, ti, "relative", f.relative);
  f.anchor = table_string(L, ti, "anchor", f.anchor);
  f.border = table_string(L, ti, "border", f.border);
  if (f.w < 1 || f.h < 1
      || (f.relative != "editor" && f.relative != "cursor" && f.relative != "win"
          && f.relative != "mouse")
      || (f.border != "none" && f.border != "single" && f.border != "double"
          && f.border != "rounded" && f.border != "custom"))
    return false;
  f.zindex = table_int(L, ti, "zindex", f.zindex);
  f.focusable = table_bool(L, ti, "focusable", f.focusable);
  f.mouse = table_bool(L, ti, "mouse", f.mouse);
  f.hide = table_bool(L, ti, "hide", f.hide);
  f.style_minimal = table_bool(L, ti, "style_minimal", f.style_minimal);
  f.strip = table_bool(L, ti, "strip", f.strip);
  f.title = table_string(L, ti, "title", f.title);
  f.footer = table_string(L, ti, "footer", f.footer);
  f.fg = table_int(L, ti, "fg", f.fg);
  f.bg = table_int(L, ti, "bg", f.bg);
  f.border_fg = table_int(L, ti, "border_fg", f.border_fg);
  f.title_fg = table_int(L, ti, "title_fg", f.title_fg);
  f.footer_fg = table_int(L, ti, "footer_fg", f.footer_fg);
  lua_getfield(L, ti, "style");
  if (lua_istable(L, -1))
  {
    f.style_minimal = table_bool(L, -1, "minimal", f.style_minimal);
    f.fg = table_int(L, -1, "fg", f.fg);
    f.bg = table_int(L, -1, "bg", f.bg);
    f.border_fg = table_int(L, -1, "border_fg", f.border_fg);
    f.title_fg = table_int(L, -1, "title_fg", f.title_fg);
    f.footer_fg = table_int(L, -1, "footer_fg", f.footer_fg);
  }
  lua_pop(L, 1);
  lua_getfield(L, ti, "border_chars");
  if (lua_istable(L, -1))
    for (int i = 0; i < 8; i++)
    {
      lua_rawgeti(L, -1, i + 1);
      if (lua_isstring(L, -1))
        f.custom_border[i] = lua_tostring(L, -1);
      lua_pop(L, 1);
    }
  lua_pop(L, 1);
  for (const char *key : {"on_key", "key_callback"})
  {
    lua_getfield(L, ti, key);
    if (lua_isfunction(L, -1))
    {
      if (f.key_callback >= 0)
        luaL_unref(L, LUA_REGISTRYINDEX, f.key_callback);
      lua_pushvalue(L, -1);
      f.key_callback = luaL_ref(L, LUA_REGISTRYINDEX);
      lua_pop(L, 1);
      break;
    }
    lua_pop(L, 1);
  }
  for (const char *key : {"on_mouse", "mouse_callback"})
  {
    lua_getfield(L, ti, key);
    if (lua_isfunction(L, -1))
    {
      if (f.mouse_callback >= 0)
        luaL_unref(L, LUA_REGISTRYINDEX, f.mouse_callback);
      lua_pushvalue(L, -1);
      f.mouse_callback = luaL_ref(L, LUA_REGISTRYINDEX);
      lua_pop(L, 1);
      break;
    }
    lua_pop(L, 1);
  }
  return true;
}
bool LuaAPI::set_float_spans(int window, int line, lua_State *L, int spans_index)
{
  auto it = float_windows.find(window);
  if (it == float_windows.end())
  {
    return false;
  }
  auto &f = it->second;
  f.spans.erase(line);
  if (!lua_istable(L, spans_index))
  {
    return true;
  }
  const int n = (int)lua_rawlen(L, spans_index);
  if (n == 0)
  {
    return true;
  }
  std::vector<FloatSpan> out;
  out.reserve((size_t)n);
  for (int i = 1; i <= n; i++)
  {
    lua_rawgeti(L, spans_index, i);
    if (lua_istable(L, -1))
    {
      FloatSpan s;
      s.start = table_int(L, -1, "start", 0);
      s.len = table_int(L, -1, "len", 0);
      s.fg = table_int(L, -1, "fg", f.fg);
      s.bg = table_int(L, -1, "bg", -1);
      if (s.len > 0)
        out.push_back(s);
    }
    lua_pop(L, 1);
  }
  if (!out.empty())
  {
    f.spans[line] = std::move(out);
  }
  return true;
}
int LuaAPI::open_float(int buffer, bool enter, lua_State *L, int ti)
{
  if (scratch_buffers.find(buffer) == scratch_buffers.end())
    return 0;
  LuaFloatWindow f;
  f.handle = next_float_window++;
  f.buffer = buffer;
  f.enter = enter;
  f.surface = current_emit_surface_;
  f.creation_order = next_float_order++;
  float_windows.emplace(f.handle, f);
  if (!configure_float(f.handle, L, ti))
  {
    float_windows.erase(f.handle);
    return 0;
  }
  if (enter)
    current_float_window = f.handle;
  return f.handle;
}
bool LuaAPI::close_float(int id, bool)
{
  auto it = float_windows.find(id);
  if (it == float_windows.end())
    return false;
  if (lua_state)
  {
    if (it->second.key_callback >= 0)
      luaL_unref((lua_State *)lua_state, LUA_REGISTRYINDEX, it->second.key_callback);
    if (it->second.mouse_callback >= 0)
      luaL_unref((lua_State *)lua_state, LUA_REGISTRYINDEX, it->second.mouse_callback);
  }
  float_windows.erase(it);
  if (current_float_window == id)
    current_float_window = 0;
  return true;
}
bool LuaAPI::is_float_valid(int id) const
{
  return float_windows.find(id) != float_windows.end();
}
void LuaAPI::clear_floats()
{
  while (!float_windows.empty())
    close_float(float_windows.begin()->first, true);
  scratch_buffers.clear();
  current_float_window = 0;
}
bool LuaAPI::float_input(int ch, bool ctrl, bool shift, bool alt)
{
  if (!lua_state || !editor || !editor->event_loop_.is_main_thread())
    return false;
  std::vector<LuaFloatWindow *> fs;
  for (auto &x : float_windows)
    fs.push_back(&x.second);
  std::sort(fs.begin(),
            fs.end(),
            [](auto *a, auto *b)
            {
              return a->zindex != b->zindex ? a->zindex > b->zindex
                                            : a->creation_order > b->creation_order;
            });
  for (auto *f : fs)
  {
    if (f->hide || !f->focusable || f->key_callback < 0)
      continue;
    int handle = f->handle;
    lua_State *L = (lua_State *)lua_state;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, f->key_callback);
    lua_newtable(L);
    lua_pushinteger(L, ch);
    lua_setfield(L, -2, "key");
    lua_pushboolean(L, ctrl);
    lua_setfield(L, -2, "ctrl");
    lua_pushboolean(L, shift);
    lua_setfield(L, -2, "shift");
    lua_pushboolean(L, alt);
    lua_setfield(L, -2, "alt");
    lua_pushinteger(L, handle);
    lua_setfield(L, -2, "window");
    int ok = lua_pcall(L, 1, 1, 0) == LUA_OK;
    bool consume = ok && lua_toboolean(L, -1);
    if (!ok)
      std::cerr << "Lua float key callback error: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top);
    if (consume)
    {
      current_float_window = handle;
      return true;
    }
  }
  return false;
}
bool LuaAPI::float_mouse(int x,
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
  for (auto it = float_windows.rbegin(); it != float_windows.rend(); ++it)
  {
    auto &f = it->second;
    if (f.hide || f.mouse_callback < 0)
      continue;
    int handle = f.handle;
    int bx = f.x, by = f.y;
    if (x < bx || x >= bx + f.w || y < by || y >= by + f.h)
      continue;
    lua_State *L = (lua_State *)lua_state;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, f.mouse_callback);
    lua_newtable(L);
    lua_pushinteger(L, x - bx);
    lua_setfield(L, -2, "col");
    lua_pushinteger(L, y - by);
    lua_setfield(L, -2, "row");
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
    int ok = lua_pcall(L, 1, 1, 0) == LUA_OK;
    bool consume = ok && lua_toboolean(L, -1);
    if (!ok)
      std::cerr << "Lua float mouse callback error: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top);
    if (consume)
    {
      current_float_window = handle;
      return true;
    }
  }
  return false;
}
void LuaAPI::render_floats()
{
  if (!editor || !editor->ui)
    return;
  // Modal scrim: the native modal surfaces (command palette, quick pick,
  // modal popups, TS-status / LSP manager / telescope) dim the whole grid
  // with UI::dim_rect *before* this pass runs. Every cell a float repaints
  // is written with dim=false, so a background float (sidebar, status line,
  // side panel) would silently wipe the scrim over its own region every
  // frame. Re-apply the dim over every float except the modal surface's own
  // panel (its handler is running under emit_lua_ui with that surface name,
  // and it draws on top of the dim like the native modal panels do).
  const bool modal_dim_active = editor->show_command_palette || editor->show_quick_pick
      || (editor->popup.visible && editor->popup.presentation == POPUP_MODAL)
      || editor->show_tree_sitter_status_modal || editor->show_lsp_manager_modal
      || editor->telescope.is_active();
  const auto modal_surface_open = [&](const std::string &s) -> bool
  {
    return (s == "command_palette" && editor->show_command_palette)
        || (s == "quick_pick" && editor->show_quick_pick)
        || (s == "popup" && editor->popup.visible && editor->popup.presentation == POPUP_MODAL)
        || (s == "tree_sitter_status" && editor->show_tree_sitter_status_modal)
        || (s == "lsp_manager" && editor->show_lsp_manager_modal)
        || (s == "telescope" && editor->telescope.is_active());
  };
  int rw = editor->ui->get_render_width(),
      rh = std::max(1, editor->ui->get_height() - editor->status_height);
  std::vector<LuaFloatWindow *> fs;
  for (auto &x : float_windows)
    if (!x.second.hide)
      fs.push_back(&x.second);
  std::sort(fs.begin(),
            fs.end(),
            [](auto *a, auto *b)
            {
              return a->zindex != b->zindex ? a->zindex < b->zindex
                                            : a->creation_order < b->creation_order;
            });
  for (auto *f : fs)
  {
    int x = f->col, y = f->row;
    if (f->relative == "cursor" && !editor->panes.empty())
    {
      auto &p = editor->get_pane();
      auto &b = editor->get_buffer(p.buffer_id);
      x = p.x + 9 + b.cursor.x;
      y = p.y + editor->tab_height + b.cursor.y - b.scroll_offset;
    }
    else if (f->relative == "win" && !editor->panes.empty())
    {
      auto &p = editor->get_pane();
      x = p.x + f->col;
      y = p.y + f->row;
    }
    if (f->anchor.find('S') != std::string::npos)
      y -= f->h;
    if (f->anchor.find('E') != std::string::npos)
      x -= f->w;
    x = std::clamp(x, 0, std::max(0, rw - f->w));
    // A strip float may occupy the status rows at the screen bottom; all
    // other floats stay above the status line.
    const int max_y = f->strip ? (editor->ui->get_height() - f->h) : (rh - f->h);
    y = std::clamp(y, 0, std::max(0, max_y));
    f->x = x;
    f->y = y;
    // Strip floats may occupy the status rows, so their rect spans the full
    // screen height; every other float stays above the status line.
    const int max_h = f->strip ? editor->ui->get_height() : rh;
    UIRect r{x, y, std::min(f->w, rw - x), std::min(f->h, max_h - y)};
    editor->ui->fill_rect(r, " ", f->fg, f->bg);
    const int border_fg = f->border_fg >= 0 ? f->border_fg : f->fg;
    if (f->border != "none")
    {
      std::array<std::string, 8> b = {"─", "│", "─", "│", "┌", "┐", "┘", "└"};
      if (f->border == "double")
        b = {"═", "║", "═", "║", "╔", "╗", "╝", "╚"};
      if (f->border == "rounded")
        b = {"─", "│", "─", "│", "╭", "╮", "╯", "╰"};
      if (f->border == "custom")
        b = f->custom_border;
      editor->ui->draw_text(x, y, b[4], border_fg, f->bg);
      editor->ui->draw_text(x + f->w - 1, y, b[5], border_fg, f->bg);
      editor->ui->draw_text(x, y + f->h - 1, b[7], border_fg, f->bg);
      editor->ui->draw_text(x + f->w - 1, y + f->h - 1, b[6], border_fg, f->bg);
      for (int i = 1; i < f->w - 1; i++)
      {
        editor->ui->draw_text(x + i, y, b[0], border_fg, f->bg);
        editor->ui->draw_text(x + i, y + f->h - 1, b[2], border_fg, f->bg);
      }
      for (int i = 1; i < f->h - 1; i++)
      {
        editor->ui->draw_text(x, y + i, b[3], border_fg, f->bg);
        editor->ui->draw_text(x + f->w - 1, y + i, b[1], border_fg, f->bg);
      }
    }
    auto bi = scratch_buffers.find(f->buffer);
    if (bi == scratch_buffers.end())
      continue;
    const int inset = (f->border == "none" ? 0 : 1);
    int ix = x + inset;
    int iy = y + inset;
    int iw = std::max(0, r.w - (f->border == "none" ? 0 : 2));
    int ih = std::max(0, r.h - (f->border == "none" ? 0 : 2));
    for (int i = 0; i < ih && i < (int)bi->second.lines.size(); i++)
    {
      const std::string &line = bi->second.lines[i];
      const std::string clipped = ui_truncate_cells(line, iw);
      auto sit = f->spans.find(i + 1);
      if (sit == f->spans.end() || sit->second.empty())
      {
        editor->ui->draw_text(ix, iy + i, clipped, f->fg, f->bg);
        continue;
      }
      auto spans = sit->second;
      // The compositor (ui.lua) emits row backgrounds first and text glyphs
      // after, so when a background fill and a glyph share a start byte the
      // fill must paint first and the glyph overdraw it. A stable sort keeps
      // that order for equal starts; only the start key is compared.
      std::stable_sort(spans.begin(),
                       spans.end(),
                       [](const FloatSpan &a, const FloatSpan &b) { return a.start < b.start; });
      // A full-line span (start 0, covers the clipped line) paints the row's
      // background, like a selection highlight. Its own text is still drawn
      // below so narrower spans can overdraw it.
      int line_bg = f->bg;
      for (const auto &sp : spans)
      {
        if (sp.bg < 0 || sp.start > 0)
          continue;
        const int e = std::clamp(sp.start + sp.len, 0, (int)clipped.size());
        if (e >= (int)clipped.size())
          line_bg = sp.bg;
      }
      // Span starts are byte offsets, but draw_text columns are terminal
      // *cells*: a wide glyph (CJK name, the '│' separator, ...) occupies 2
      // cells yet 3 bytes, so placing a segment at ix + byte drifts it right
      // by the byte surplus accumulated before it -- vertical lines then
      // zigzag from row to row depending on the content to their left.
      // Map each byte offset to its cell column before drawing.
      const auto cell_col = [&clipped](int byte)
      {
        const int b = ui_clamp_to_utf8_boundary(clipped, std::clamp(byte, 0, (int)clipped.size()));
        return ui_cell_count(clipped.substr(0, (size_t)b));
      };
      int pos = 0;
      for (const auto &sp : spans)
      {
        const int s = std::clamp(sp.start, 0, (int)clipped.size());
        const int e = std::clamp(sp.start + sp.len, s, (int)clipped.size());
        if (e <= s)
          continue;
        const int span_bg = sp.bg >= 0 ? sp.bg : line_bg;
        if (s > pos)
          editor->ui->draw_text(
              ix + cell_col(pos), iy + i, clipped.substr(pos, s - pos), f->fg, line_bg);
        editor->ui->draw_text(ix + cell_col(s), iy + i, clipped.substr(s, e - s), sp.fg, span_bg);
        // Never rewind: glyph spans that start inside an already-painted
        // background fill overdraw their own extent, but the bytes between
        // them and the next span must keep the fill's background. Rewinding
        // made the next glyph's `s > pos` gap redraw paint over the fill
        // (e.g. the row background of a git-modified file in the explorer
        // vanished between the file name and its M badge).
        pos = std::max(pos, e);
      }
      if (pos < (int)clipped.size())
        editor->ui->draw_text(ix + cell_col(pos), iy + i, clipped.substr(pos), f->fg, line_bg);
    }
    const int title_fg = f->title_fg >= 0 ? f->title_fg : f->fg;
    if (!f->title.empty())
    {
      const std::string title_text = ui_truncate_cells(" " + f->title + " ", std::max(0, r.w - 2));
      editor->ui->draw_text(x + 1, y, title_text, title_fg, f->bg, true);
    }
    const int footer_fg = f->footer_fg >= 0 ? f->footer_fg : f->fg;
    if (!f->footer.empty())
    {
      const std::string footer_text =
          ui_truncate_cells(" " + f->footer + " ", std::max(0, r.w - 2));
      editor->ui->draw_text(x + 1, y + r.h - 1, footer_text, footer_fg, f->bg);
    }
    if (modal_dim_active && !modal_surface_open(f->surface))
    {
      editor->ui->dim_rect(r);
    }
  }
}
