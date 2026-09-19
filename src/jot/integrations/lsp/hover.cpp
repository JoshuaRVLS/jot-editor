// Editor-side LSP hover and definition navigation: dwell-triggered mouse
// hover with a delay, cursor-triggered hover, the native popup (with
// diagnostics leading the content), and jump-to-definition with a back stack.
#include "editor.h"
#include "jot/editor_models.h"
#include "jot/lua/api.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
  constexpr int kLspMouseHoverDelayMs = 450;

  // How a navigation kind reads in a message: lowercase for the request
  // ("LSP declaration requested"), capitalised when it labels a jump.
  std::string navigation_name(LSPNavigationKind kind)
  {
    switch (kind)
    {
    case LSPNavigationKind::Declaration:
      return "declaration";
    case LSPNavigationKind::TypeDefinition:
      return "type definition";
    case LSPNavigationKind::Implementation:
      return "implementation";
    case LSPNavigationKind::Definition:
      break;
    }
    return "definition";
  }

  std::string navigation_display_name(LSPNavigationKind kind)
  {
    std::string name = navigation_name(kind);
    if (!name.empty())
    {
      name[0] = (char)std::toupper((unsigned char)name[0]);
    }
    return name;
  }

  const char *diag_severity_hover_label(int severity)
  {
    switch (severity)
    {
      case 1:
        return "Error";
      case 2:
        return "Warning";
      case 3:
        return "Info";
      case 4:
        return "Hint";
      default:
        return "Diagnostic";
    }
  }

  // VSCode-style: the diagnostics whose range covers (line, col) lead the
  // hover popup, so hovering a squiggle shows the error message even when
  // the LSP server has no hover content at that spot. Renders up to 4
  // diagnostics, then a "… and N more" tail.
  std::string diagnostics_at_position_text(const std::vector<FileBuffer> &buffers,
                                           const std::string &filepath,
                                           int line,
                                           int col)
  {
    if (filepath.empty())
    {
      return {};
    }
    const FileBuffer *buf = nullptr;
    for (const auto &b : buffers)
    {
      if (lsp_internal::same_path(b.filepath, filepath))
      {
        buf = &b;
        break;
      }
    }
    if (!buf)
    {
      return {};
    }
    std::string out;
    int shown = 0;
    int total = 0;
    for (const auto &d : buf->diagnostics)
    {
      bool covers = false;
      if (line >= d.line && line <= d.end_line)
      {
        if (line == d.line && line == d.end_line)
        {
          covers = col >= d.col && col <= d.end_col;
        }
        else if (line == d.line)
        {
          covers = col >= d.col;
        }
        else if (line == d.end_line)
        {
          covers = col <= d.end_col;
        }
        else
        {
          covers = true;
        }
      }
      if (!covers)
      {
        continue;
      }
      total++;
      if (shown < 4)
      {
        if (shown > 0)
        {
          out += "\n";
        }
        out += std::string(diag_severity_hover_label(d.severity)) + ": " + d.message;
        shown++;
      }
    }
    if (total > shown)
    {
      if (shown > 0)
      {
        out += "\n";
      }
      out += "… and " + std::to_string(total - shown) + " more";
    }
    return out;
  }

  std::string compact_lsp_popup_text(const std::string &text, int max_lines, int max_cols)
  {
    std::string out;
    std::string line;
    std::istringstream stream(text);
    int lines = 0;
    while (lines < max_lines && std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      if ((int)line.size() > max_cols)
      {
        // Cut on a character boundary: slicing at a byte index can land inside
        // a multi-byte character, and the orphaned continuation bytes then
        // render as "?" in the hover (draw_text substitutes for an invalid
        // cluster). max_cols counts columns, so this is already approximate --
        // staying inside the character is what matters.
        size_t cut = (size_t)std::max(0, max_cols - 1);
        while (cut > 0 && cut < line.size()
               && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80)
        {
          --cut;
        }
        line = line.substr(0, cut) + "...";
      }
      if (!out.empty())
      {
        out.push_back('\n');
      }
      out += line;
      lines++;
    }
    if (std::getline(stream, line))
    {
      out += "\n...";
    }
    return out;
  }

  bool lsp_popup_markdown_fence(const std::string &line)
  {
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
      start++;
    }
    return line.compare(start, 3, "```") == 0;
  }

  std::pair<int, int> lsp_popup_size(const std::string &text)
  {
    int max_w = 0;
    int lines = 0;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
      if (lsp_popup_markdown_fence(line))
      {
        continue;
      }
      max_w = std::max(max_w, ui_cell_count(line));
      lines++;
    }
    return {max_w + 2, std::max(1, lines) + 2};
  }

  std::pair<int, int> place_lsp_popup(int anchor_x,
                                      int anchor_y,
                                      int popup_w,
                                      int popup_h,
                                      int render_w,
                                      int screen_h,
                                      int status_h)
  {
    constexpr int top_chrome_h = 1;
    int usable_w = std::max(1, render_w);
    int bottom_exclusive = std::max(top_chrome_h + 1, screen_h - std::max(0, status_h));

    int x = std::clamp(anchor_x, 0, std::max(0, usable_w - popup_w));
    int y = anchor_y + 1;
    if (y + popup_h > bottom_exclusive)
    {
      y = anchor_y - popup_h - 1;
    }
    y = std::clamp(y, top_chrome_h, std::max(top_chrome_h, bottom_exclusive - popup_h));
    return {x, y};
  }
} // namespace

void Editor::request_lsp_hover()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  if (buf.filepath.empty())
  {
    set_message("Save file first to use LSP");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    set_message("No LSP server for this file");
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_hover(buf.filepath, buf.cursor.y, buf.cursor.x))
  {
    set_message("LSP hover request failed");
    return;
  }
  set_message("LSP hover requested");
}

void Editor::request_lsp_hover_at(int pane_index,
                                  int buffer_id,
                                  const Cursor &pos,
                                  int token_start,
                                  int token_end,
                                  int screen_x,
                                  int screen_y)
{
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
  {
    cancel_lsp_mouse_hover();
    return;
  }
  const auto &buf = buffers[buffer_id];
  if (buf.is_lazy() || buf.filepath.empty())
  {
    cancel_lsp_mouse_hover();
    return;
  }
  if (pos.y < 0 || pos.y >= (int)buf.line_count() || token_start < 0 || token_end <= token_start)
  {
    cancel_lsp_mouse_hover();
    return;
  }

  if (lsp_mouse_hover_pending && lsp_mouse_hover_buffer == buffer_id
      && lsp_mouse_hover_line == pos.y && lsp_mouse_hover_token_start == token_start
      && lsp_mouse_hover_token_end == token_end)
  {
    lsp_mouse_hover_screen_x = screen_x;
    lsp_mouse_hover_screen_y = screen_y;
    return;
  }
  if (lsp_mouse_hover_visible && lsp_mouse_hover_buffer == buffer_id
      && lsp_mouse_hover_line == pos.y && lsp_mouse_hover_token_start == token_start
      && lsp_mouse_hover_token_end == token_end)
  {
    return;
  }

  if (lsp_mouse_hover_visible)
  {
    hide_popup();
  }
  lsp_mouse_hover_visible = false;
  lsp_mouse_hover_pending = true;
  lsp_mouse_hover_deadline_ms = lsp_internal::now_ms() + kLspMouseHoverDelayMs;
  lsp_mouse_hover_pane = pane_index;
  lsp_mouse_hover_buffer = buffer_id;
  lsp_mouse_hover_line = pos.y;
  lsp_mouse_hover_col = pos.x;
  lsp_mouse_hover_token_start = token_start;
  lsp_mouse_hover_token_end = token_end;
  lsp_mouse_hover_screen_x = screen_x;
  lsp_mouse_hover_screen_y = screen_y;
  lsp_mouse_hover_filepath = buf.filepath;
}

void Editor::close_lua_hover_ui()
{
  if (lua_api)
  {
    lua_api->notify_lsp_hover_closed();
  }
}

void Editor::cancel_lsp_mouse_hover(bool hide_popup_now)
{
  // Any key, click, drag, paste or hover replacement dismisses the current
  // hover; when Lua renders the hover UI, mirror that by closing its float.
  close_lua_hover_ui();
  lsp_mouse_hover_pending = false;
  lsp_mouse_hover_deadline_ms = 0;
  lsp_mouse_hover_pane = -1;
  lsp_mouse_hover_buffer = -1;
  lsp_mouse_hover_line = -1;
  lsp_mouse_hover_col = -1;
  lsp_mouse_hover_token_start = -1;
  lsp_mouse_hover_token_end = -1;
  lsp_mouse_hover_screen_x = -1;
  lsp_mouse_hover_screen_y = -1;
  lsp_mouse_hover_filepath.clear();
  if (hide_popup_now && lsp_mouse_hover_visible)
  {
    hide_popup();
    lsp_mouse_hover_visible = false;
  }
}

void Editor::maybe_fire_lsp_mouse_hover()
{
  if (!lsp_mouse_hover_pending || lsp_internal::now_ms() < lsp_mouse_hover_deadline_ms)
  {
    return;
  }
  if (show_context_menu || show_command_palette || search.visible() || telescope.is_active()
      || mouse_selecting || mouse_drag_started)
  {
    return;
  }
  if (lsp_mouse_hover_buffer < 0 || lsp_mouse_hover_buffer >= (int)buffers.size())
  {
    cancel_lsp_mouse_hover();
    return;
  }

  auto &buf = buffers[lsp_mouse_hover_buffer];
  if (buf.is_lazy() || !lsp_internal::same_path(buf.filepath, lsp_mouse_hover_filepath))
  {
    cancel_lsp_mouse_hover();
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    cancel_lsp_mouse_hover(false);
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_hover(buf.filepath, lsp_mouse_hover_line, lsp_mouse_hover_col))
  {
    cancel_lsp_mouse_hover(false);
    return;
  }
  lsp_mouse_hover_pending = false;
}

// The shared body of definition / declaration / type definition /
// implementation: same position lookup, different method and label.
void Editor::request_lsp_navigation(LSPNavigationKind kind)
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  if (buf.filepath.empty())
  {
    set_message("Save file first to use LSP");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    set_message("No LSP server for this file");
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_navigation(buf.filepath, buf.cursor.y, buf.cursor.x, kind))
  {
    set_message("LSP " + navigation_name(kind) + " request failed");
    return;
  }
  set_message("LSP " + navigation_name(kind) + " requested");
}

void Editor::request_lsp_definition()
{
  request_lsp_navigation(LSPNavigationKind::Definition);
}

void Editor::request_lsp_declaration()
{
  request_lsp_navigation(LSPNavigationKind::Declaration);
}

void Editor::request_lsp_type_definition()
{
  request_lsp_navigation(LSPNavigationKind::TypeDefinition);
}

void Editor::request_lsp_implementation()
{
  request_lsp_navigation(LSPNavigationKind::Implementation);
}

void Editor::switch_lsp_source_header()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  if (buf.filepath.empty())
  {
    set_message("Save file first to switch header/source");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    set_message("No LSP server for this file");
    return;
  }

  if (!client->request_switch_source_header(buf.filepath))
  {
    set_message("LSP switch source/header request failed");
    return;
  }
  set_message("Looking for the paired header/source");
}

void Editor::handle_lsp_switch_source_header_result(const std::string &filepath)
{
  if (filepath.empty())
  {
    set_message("No paired header/source found");
    return;
  }
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  // Same tab policy as a definition jump: a different file opens as a preview
  // tab, so switching back and forth does not pile up tabs.
  const bool same_file = lsp_internal::same_path(get_buffer().filepath, filepath);
  open_file(filepath, !same_file);
  // Landing somewhere else is a jump like any other: Ctrl+O comes back here.
  record_jump();
  set_message("Switched to " + get_filename(filepath));
}

void Editor::handle_lsp_hover_result(const LSPHoverResult &hover)
{
  // A Lua one-shot sink (jot.lsp.request_hover) consumes this result first;
  // the native popup only shows when Lua did not take it.
  if (lua_api && lua_api->try_deliver_lsp_hover(hover))
  {
    return;
  }
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  // VSCode-style: diagnostics covering the hover position lead the popup, so
  // hovering the squiggle shows the error message even when the server has
  // no hover content. The merged text flows to the Lua hover UI and the
  // native popup alike.
  const std::string diag_text = diagnostics_at_position_text(
      buffers, hover.origin_filepath, hover.origin_line, hover.origin_character);
  std::string contents = hover.contents;
  if (!diag_text.empty())
  {
    contents = diag_text;
    if (!hover.contents.empty())
    {
      contents += "\n\n" + hover.contents;
    }
  }
  if (lsp_mouse_hover_buffer >= 0 && lsp_internal::same_path(hover.origin_filepath, lsp_mouse_hover_filepath)
      && hover.origin_line == lsp_mouse_hover_line && hover.origin_character == lsp_mouse_hover_col)
  {
    if (contents.empty())
    {
      lsp_mouse_hover_visible = false;
      close_lua_hover_ui();
      needs_redraw = true;
      return;
    }
    int popup_x = lsp_mouse_hover_screen_x + 2;
    int popup_y = lsp_mouse_hover_screen_y;
    // Lua hover UI (jot.lsp.hover_ui) renders the popup when registered; the
    // native popup only shows when no handler consumed the result. The Lua
    // float clamps itself to the screen, so raw anchor coordinates are used.
    if (lua_api && lua_api->has_lsp_hover_ui()
        && lua_api->present_lsp_hover(contents,
                                      hover.origin_filepath,
                                      hover.origin_line,
                                      hover.origin_character,
                                      "mouse",
                                      popup_x,
                                      popup_y))
    {
      lsp_mouse_hover_visible = true;
      needs_redraw = true;
      return;
    }
    std::string text = compact_lsp_popup_text(contents, 14, 96);
    if (ui)
    {
      auto [popup_w, popup_h] = lsp_popup_size(text);
      auto [placed_x, placed_y] = place_lsp_popup(popup_x,
                                                  popup_y,
                                                  popup_w,
                                                  popup_h,
                                                  ui->get_render_width(),
                                                  ui->get_height(),
                                                  status_height);
      popup_x = placed_x;
      popup_y = placed_y;
    }
    else
    {
      popup_y += 1;
    }
    show_hover_popup(text, popup_x, popup_y);
    lsp_mouse_hover_visible = true;
    return;
  }

  auto &buf = get_buffer();
  if (!lsp_internal::same_path(buf.filepath, hover.origin_filepath) || buf.cursor.y != hover.origin_line
      || buf.cursor.x != hover.origin_character)
  {
    return;
  }
  if (contents.empty())
  {
    set_message("No hover information");
    return;
  }

  const SplitPane &pane = get_pane();
  constexpr int line_num_width = 7;
  int row = hover.origin_line - buf.scroll_offset;
  int max_row = std::max(0, pane.h - tab_height);
  int anchor_y = pane.y + tab_height + std::clamp(row, 0, max_row);
  int popup_x =
      pane.x + 1 + line_num_width + std::max(0, hover.origin_character - buf.scroll_x) + 2;
  // Lua hover UI: same hook as the mouse path, anchored at the cursor line.
  if (lua_api && lua_api->has_lsp_hover_ui()
      && lua_api->present_lsp_hover(contents,
                                    hover.origin_filepath,
                                    hover.origin_line,
                                    hover.origin_character,
                                    "cursor",
                                    popup_x,
                                    anchor_y))
  {
    needs_redraw = true;
    return;
  }
  std::string text = compact_lsp_popup_text(contents, 14, 96);
  if (ui)
  {
    auto [popup_w, popup_h] = lsp_popup_size(text);
    auto [placed_x, placed_y] = place_lsp_popup(popup_x,
                                                anchor_y,
                                                popup_w,
                                                popup_h,
                                                ui->get_render_width(),
                                                ui->get_height(),
                                                status_height);
    popup_x = placed_x;
    anchor_y = placed_y;
  }
  else
  {
    anchor_y += 1;
  }
  show_hover_popup(text, popup_x, anchor_y);
}

void Editor::handle_lsp_definition_result(const LSPDefinitionResult &definition)
{
  // Lua one-shot sink (jot.lsp.request_definition) takes precedence over the
  // native jump when it is waiting for this result.
  if (lua_api && lua_api->try_deliver_lsp_definition(definition))
  {
    return;
  }
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  auto &buf = get_buffer();
  if (!lsp_internal::same_path(buf.filepath, definition.origin_filepath) || buf.cursor.y != definition.origin_line
      || buf.cursor.x != definition.origin_character)
  {
    return;
  }
  if (definition.locations.empty())
  {
    set_message("No " + navigation_name(definition.navigation) + " found");
    return;
  }

  lsp_navigation_jump_label = navigation_display_name(definition.navigation);
  lsp_definition_pending_location = definition.locations.front();
  lsp_definition_jump_pending = true;
  const bool same_file = lsp_internal::same_path(buf.filepath, lsp_definition_pending_location.filepath);
  open_file(lsp_definition_pending_location.filepath, !same_file);
  if (apply_pending_lsp_definition_jump())
  {
    // The definition is a jump like any other: Ctrl+O comes back here.
    record_jump();
  }
}

bool Editor::apply_pending_lsp_definition_jump()
{
  if (!lsp_definition_jump_pending || buffers.empty() || current_buffer < 0
      || current_buffer >= (int)buffers.size())
  {
    return false;
  }

  auto &buf = get_buffer();
  if (!lsp_internal::same_path(buf.filepath, lsp_definition_pending_location.filepath))
  {
    return false;
  }

  buf.cursor.y =
      std::clamp(lsp_definition_pending_location.line, 0, std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x =
      std::clamp(lsp_definition_pending_location.character, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  clear_selection();
  ensure_cursor_visible();
  lsp_definition_jump_pending = false;
  set_message(lsp_navigation_jump_label + ": " + get_filename(buf.filepath) + ":"
              + std::to_string(buf.cursor.y + 1));
  needs_redraw = true;
  return true;
}

// Places the cursor for a jumplist restore. Silent about the outcome: the
// caller knows whether it is going back or forward and says so.
bool Editor::apply_pending_jump()
{
  if (!jump_pending || buffers.empty() || current_buffer < 0
      || current_buffer >= (int)buffers.size())
  {
    return false;
  }

  auto &buf = get_buffer();
  if (!lsp_internal::same_path(buf.filepath, jump_pending_location.filepath))
  {
    return false;
  }

  buf.cursor.y =
      std::clamp(jump_pending_location.cursor.y, 0, std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x =
      std::clamp(jump_pending_location.cursor.x, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  buf.scroll_offset = std::max(0, jump_pending_location.scroll_offset);
  buf.scroll_x = std::max(0, jump_pending_location.scroll_x);
  clear_selection();
  ensure_cursor_visible();
  jump_pending = false;
  needs_redraw = true;
  return true;
}