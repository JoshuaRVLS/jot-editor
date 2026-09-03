#include "editor.h"
#include "tools/symbols/index.h"
#include "ui/text.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>

namespace
{

  std::string outline_lower(std::string s)
  {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return (char)std::tolower(c); });
    return s;
  }

  int outline_symbol_color(const Theme &theme, const std::string &kind)
  {
    const std::string k = outline_lower(kind);
    if (k == "function" || k == "method" || k == "constructor" ||
        k == "macro")
    {
      return theme.fg_function;
    }
    if (k == "class" || k == "struct" || k == "union" || k == "interface" ||
        k == "enum" || k == "type" || k == "typedef" || k == "type_alias" ||
        k == "enum_member")
    {
      return theme.fg_type;
    }
    if (k == "namespace" || k == "module" || k == "package")
    {
      return theme.fg_namespace;
    }
    if (k == "variable" || k == "property" || k == "field" ||
        k == "parameter")
    {
      return theme.fg_variable;
    }
    if (k == "constant")
    {
      return theme.fg_constant;
    }
    return theme.fg_command;
  }

} // namespace

void Editor::note_outline_edit() { outline_panel.dirty = true; }

void Editor::ensure_outline_fresh(bool force)
{
  if (!outline_active())
  {
    return;
  }
  if (buffers.empty() || current_buffer < 0 ||
      current_buffer >= (int)buffers.size())
  {
    return;
  }
  FileBuffer &buf = get_buffer();
  if (!force && outline_panel.buffer == current_buffer &&
      !outline_panel.dirty)
  {
    return;
  }
  // While the user is typing in the same buffer, rebuild at most every
  // ~250 ms so the symbol extractor never runs on every keystroke of a
  // large file. A buffer switch always rebuilds immediately.
  if (!force && outline_panel.dirty &&
      outline_panel.buffer == current_buffer)
  {
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now()
                                  .time_since_epoch())
                              .count();
    if (outline_panel.last_rebuild_ms > 0 &&
        now - outline_panel.last_rebuild_ms < 250)
    {
      return;
    }
  }

  outline_panel.symbols.clear();
  if (!buf.is_lazy() && !buf.filepath.empty() && buf.line_count() > 0)
  {
    outline_panel.symbols =
        SymbolIndex::extract_document_symbols(buf.lines, buf.filepath);
  }
  outline_panel.buffer = current_buffer;
  outline_panel.dirty = false;
  outline_panel.last_rebuild_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  outline_panel.selected =
      std::clamp(outline_panel.selected, 0,
                 std::max(0, (int)outline_panel.symbols.size() - 1));
  outline_panel.scroll = 0;
}

void Editor::toggle_outline_panel()
{
  if (outline_active())
  {
    close_outline_panel();
    return;
  }
  show_right_panel = true;
  active_right_panel_tab = RIGHT_PANEL_SYMBOLS;
  active_plugin_panel.clear();
  show_debugger_panel = false;
  needs_redraw = true;
  ensure_outline_fresh(true);
  if (outline_panel.symbols.empty())
  {
    set_message("Outline: no symbols in this file");
  }
  else
  {
    set_message("Outline: " +
                std::to_string(outline_panel.symbols.size()) + " symbols");
  }
}

void Editor::close_outline_panel()
{
  if (outline_active())
  {
    show_right_panel = false;
    active_right_panel_tab = RIGHT_PANEL_DEBUG;
  }
  needs_redraw = true;
}

void Editor::outline_move_selection(int delta)
{
  ensure_outline_fresh();
  if (outline_panel.symbols.empty())
  {
    return;
  }
  outline_panel.selected =
      std::clamp(outline_panel.selected + delta, 0,
                 (int)outline_panel.symbols.size() - 1);
  needs_redraw = true;
}

void Editor::outline_jump_selected()
{
  ensure_outline_fresh(true);
  if (outline_panel.symbols.empty())
  {
    set_message("Outline: no symbols in this file");
    return;
  }
  const int index =
      std::clamp(outline_panel.selected, 0,
                 (int)outline_panel.symbols.size() - 1);
  const SymbolMatch &symbol = outline_panel.symbols[(size_t)index];
  FileBuffer &buf = get_buffer();
  if (buf.line_count() == 0)
  {
    return;
  }
  buf.cursor.y =
      std::clamp(symbol.line, 0, std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x =
      std::clamp(symbol.column, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  clear_selection();
  ensure_cursor_visible();
  needs_redraw = true;
  set_message(get_filename(buf.filepath) + ":" +
              std::to_string(buf.cursor.y + 1) + "  " + symbol.kind + "  " +
              symbol.name);
}

void Editor::render_outline_panel()
{
  if (!outline_active() || !ui)
  {
    return;
  }
  ensure_outline_fresh();

  const int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return;
  }
  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = topbar_height();
  const int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  UIRect panel = {panel_x, panel_y, panel_w, panel_h};

  ui->fill_rect(panel, " ", theme.fg_terminal, theme.bg_terminal);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal);

  const std::string title = " Outline ";
  ui->draw_text(panel_x + 1, panel_y, title, theme.fg_terminal_tab_focused,
                theme.bg_terminal_tab_focused, true);

  const int content_x = panel_x + 1;
  const int content_y = panel_y + 2;
  const int content_w = std::max(1, panel_w - 2);
  const int content_h = std::max(0, panel_h - 3);

  // Header: current file + symbol count.
  std::string file_label = "No file";
  const FileBuffer *buf = nullptr;
  if (current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    buf = &buffers[(size_t)current_buffer];
  }
  if (buf && !buf->filepath.empty())
  {
    file_label = std::filesystem::path(buf->filepath).filename().string();
  }
  const std::string header =
      file_label + "  " + std::to_string(outline_panel.symbols.size()) +
      " symbol" + (outline_panel.symbols.size() == 1 ? "" : "s");
  ui->draw_text(content_x, content_y,
                ui_truncate_cells(header, content_w),
                theme.fg_status_info, theme.bg_terminal, true);

  const int body_y = content_y + 1;
  const int body_h = std::max(0, content_h - 1);

  if (outline_panel.symbols.empty())
  {
    std::string note = "No symbols found";
    if (!buf || buf->filepath.empty())
    {
      note = "Open a file to see its symbols";
    }
    ui->draw_text(content_x, body_y, ui_truncate_cells(note, content_w),
                  theme.fg_comment, theme.bg_terminal);
    return;
  }

  // Keep the selection on screen, then clamp the scroll window.
  int max_scroll =
      std::max(0, (int)outline_panel.symbols.size() - body_h);
  if (outline_panel.selected < outline_panel.scroll)
  {
    outline_panel.scroll = outline_panel.selected;
  }
  if (outline_panel.selected >= outline_panel.scroll + body_h)
  {
    outline_panel.scroll =
        std::max(0, outline_panel.selected - body_h + 1);
  }
  outline_panel.scroll = std::clamp(outline_panel.scroll, 0, max_scroll);

  const int line_w = std::min(content_w / 4, 7); // right-aligned line number
  const int name_w = std::max(1, content_w - line_w);

  for (int row = 0; row < body_h; row++)
  {
    const int index = outline_panel.scroll + row;
    if (index < 0 || index >= (int)outline_panel.symbols.size())
    {
      break;
    }
    const SymbolMatch &symbol = outline_panel.symbols[(size_t)index];
    const bool selected = (index == outline_panel.selected);
    const int row_y = body_y + row;

    int bg = selected ? theme.bg_selection : theme.bg_terminal;
    int fg = selected ? theme.fg_selection
                      : outline_symbol_color(theme, symbol.kind);
    ui->fill_rect({content_x, row_y, content_w, 1}, " ", fg, bg);
    ui->draw_text(content_x, row_y,
                  ui_truncate_cells(symbol.name, name_w), fg, bg,
                  selected);
    const std::string line_no = std::to_string(symbol.line + 1);
    ui->draw_text(content_x + std::max(0, name_w - (int)line_no.size()),
                  row_y, line_no,
                  selected ? theme.fg_selection : theme.fg_comment, bg);
  }
}
