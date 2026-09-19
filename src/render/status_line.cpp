// Editor status line: workspace/file labels, git and LSP status, mode,
// cursor position, and the plugin-registered status segments.
#include "editor.h"
#include "jot/file_icons.h"
#include "jot/lua/api.h"
#include "tools/lsp/install.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
  std::string status_path_basename(const std::string &path, const std::string &fallback)
  {
    if (path.empty())
      return fallback;
    std::filesystem::path p(path);
    std::string name = p.filename().string();
    if (!name.empty())
      return name;
    name = p.root_path().string();
    return name.empty() ? path : name;
  }

  std::string status_workspace_label(const std::string &root_dir)
  {
    if (root_dir.empty() || root_dir == ".")
      return "No workspace";
    return status_path_basename(root_dir, root_dir);
  }

  struct StatusSegment
  {
    std::string text;
    int fg = 7;
    int bg = 0;
    bool bold = false;
    bool optional = false;
    int priority = 0;
    // Optional leading glyph (file-type icon, git mark) rendered with its
    // own color in front of `text`; -1 color means use `fg`. Kept separate
    // from `text` so truncation can never eat the icon.
    std::string symbol;
    int symbol_fg = -1;
  };

  int status_layout_width(const std::vector<StatusSegment> &segments)
  {
    if (segments.empty())
      return 0;
    int width = 0;
    for (const auto &segment : segments)
    {
      width += ui_cell_count(segment.symbol) + ui_cell_count(segment.text);
    }
    width += std::max(0, (int)segments.size() - 1);
    return width;
  }

  void status_drop_optional_to_fit(std::vector<StatusSegment> &segments, int max_w)
  {
    while (status_layout_width(segments) > max_w)
    {
      auto removable = segments.end();
      for (auto it = segments.begin(); it != segments.end(); ++it)
      {
        if (!it->optional)
          continue;
        if (removable == segments.end() || it->priority < removable->priority)
        {
          removable = it;
        }
      }
      if (removable == segments.end())
        break;
      segments.erase(removable);
    }
  }

  void status_draw_clipped(
      UI *ui, int x, int y, int w, const std::string &text, int fg, int bg, bool bold = false)
  {
    if (w <= 0)
      return;
    ui->draw_text(x, y, ui_take_cells(text, w), fg, bg, bold);
  }

    int status_draw_segmented_at(
        UI *ui, int x, int y, int w, const std::vector<StatusSegment> &segments)
    {
      int pos = x;
      const int end = x + std::max(0, w);
      for (size_t i = 0; i < segments.size() && pos < end; i++)
      {
        const auto &segment = segments[i];
        const int remaining = end - pos;
        // Symbol first (own color, survives truncation), then the label.
        if (!segment.symbol.empty() && pos < end)
        {
          std::string symbol = ui_take_cells(segment.symbol, remaining);
          int sf = segment.symbol_fg >= 0 ? segment.symbol_fg : segment.fg;
          ui->draw_text(pos, y, symbol, sf, segment.bg, segment.bold);
          pos += ui_cell_count(symbol);
        }
        if (pos < end)
        {
          std::string text = ui_take_cells(segment.text, end - pos);
          ui->draw_text(pos, y, text, segment.fg, segment.bg, segment.bold);
          pos += ui_cell_count(text);
        }
  
        if (pos < end && i + 1 < segments.size())
        {
          ui->draw_text(pos, y, "", segment.bg, segments[i + 1].bg, true);
          pos++;
        }
      }
      return pos;
    }
  
} // namespace

void Editor::render_status_line()
{
  // Zen focus mode suppresses the status line entirely (rows reclaimed by the
  // pane area via status_height = 0); drop the Lua statusline float too so it
  // does not linger over the last pane row.
  if (zen_mode)
  {
    if (lua_api)
    {
      lua_api->emit_lua_ui_close("status_line");
    }
    return;
  }
  int y = ui->get_height() - status_height;
  int w = ui->get_render_width();

  UIRect rect = {0, y, w, status_height};
  ui->fill_rect(rect, " ", theme.fg_status, theme.bg_status);

  if (w <= 0)
  {
    return;
  }

  const int content_x = 0;
  const int content_w = w;

  std::vector<StatusSegment> left_segments;
  std::vector<StatusSegment> right_segments;
  FileBuffer *active_buf = nullptr;
  if (!buffers.empty() && current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    active_buf = &buffers[current_buffer];
  }

  std::string file_label = "Home";
  std::string file_symbol = "󰋜";
  int file_symbol_fg = -1; // -1: use the segment fg (home icon stays flat)
  bool modified = false;
  if (!show_home_menu && active_buf)
  {
    file_label = status_path_basename(active_buf->filepath, "[No Name]");
    modified = active_buf->modified;
    if (active_buf->filepath.empty())
    {
      file_symbol = "󰈔"; // unsaved buffer: keep the flat file glyph
    }
    else
    {
      // Per-language glyph + brand color (cpp/py/...), shared with the
      // file explorer; unknown types fall back to a neutral file icon.
      const jot_icons::FileTypeIcon type_icon =
          jot_icons::file_type_icon(active_buf->filepath);
      file_symbol = type_icon.glyph;
      file_symbol_fg = type_icon.color;
    }
  }
  left_segments.push_back({" " + file_label + (modified ? " +" : "") + " ",
                           theme.fg_status_file,
                           theme.bg_status_file,
                           true,
                           false,
                           100,
                           " " + file_symbol,
                           file_symbol_fg});

  std::string cursor_label = " Ready ";
  if (!show_home_menu && active_buf)
  {
    cursor_label = " " + std::to_string(active_buf->cursor.y + 1) + ":"
                   + std::to_string(active_buf->cursor.x + 1) + " ";
  }
  left_segments.push_back({cursor_label, theme.fg_status, theme.bg_status, true, false, 100});

  if (!show_home_menu && active_buf && active_buf->selection.active)
  {
    int lines = std::abs(active_buf->selection.end.y - active_buf->selection.start.y) + 1;
    int cols = std::abs(active_buf->selection.end.x - active_buf->selection.start.x);
    std::string sel =
        lines > 1 ? " Sel " + std::to_string(lines) + "L " : " Sel " + std::to_string(cols) + "C ";
    left_segments.push_back({sel, theme.fg_status_info, theme.bg_status_info, true, true, 90});
  }

  int diag_error = 0;
  int diag_warning = 0;
  int diag_info = 0;
  int diag_hint = 0;
  if (active_buf)
  {
    for (const auto &diag : active_buf->diagnostics)
    {
      if (diag.severity == 1)
        diag_error++;
      else if (diag.severity == 2)
        diag_warning++;
      else if (diag.severity == 3)
        diag_info++;
      else if (diag.severity == 4)
        diag_hint++;
    }
  }
  else
  {
    for (const auto &entry : workspace_diagnostic_severity)
    {
      if (entry.second == 1)
        diag_error++;
      else if (entry.second == 2)
        diag_warning++;
      else if (entry.second == 3)
        diag_info++;
      else if (entry.second == 4)
        diag_hint++;
    }
  }
  if (diag_error || diag_warning || diag_info || diag_hint)
  {
    std::string diag_text;
    if (diag_error)
      diag_text += "  " + std::to_string(diag_error);
    if (diag_warning)
      diag_text += "  " + std::to_string(diag_warning);
    if (diag_info)
      diag_text += "  " + std::to_string(diag_info);
    if (diag_hint)
      diag_text += "  " + std::to_string(diag_hint);
    diag_text += " ";
    int diag_fg = diag_error ? theme.fg_status_error
                             : (diag_warning ? theme.fg_status_warning : theme.fg_status_info);
    int diag_bg = diag_error ? theme.bg_status_error
                             : (diag_warning ? theme.bg_status_warning : theme.bg_status_info);
    right_segments.push_back({diag_text, diag_fg, diag_bg, true, true, 80});
  }

  if (has_git_repo())
  {
    // Branch chip: the git glyph is drawn in a warm accent (visible on both
    // dark and light status bars), the branch + change counts keep the
    // status-info tone. Symbols survive width truncation, the branch text
    // is the part that shortens first.
    std::string git = " " + ui_truncate_cells(git_branch, 18);
    if (git_staged_count > 0)
    {
      git += " +" + std::to_string(git_staged_count);
    }
    if (git_unstaged_count > 0)
    {
      git += " ~" + std::to_string(git_unstaged_count);
    }
    if (git_untracked_count > 0)
    {
      git += " ?" + std::to_string(git_untracked_count);
    }
    if (git_conflict_count > 0)
    {
      git += " !" + std::to_string(git_conflict_count);
    }
    git += " ";
    right_segments.push_back({git,
                              theme.fg_status_info,
                              theme.bg_status_info,
                              true,
                              true,
                              70,
                              " ",
                              208});
  }

  if (!lsp_clients.empty())
  {
    int running_clients = 0;
    std::string first_running;
    for (const auto &client : lsp_clients)
    {
      if (client && client->is_running())
      {
        running_clients++;
        if (first_running.empty())
        {
          first_running = client->describe();
        }
      }
    }
    std::string lsp_text;
    int lsp_fg = theme.fg_status_muted;
    int lsp_bg = theme.bg_status_muted;
    if (running_clients > 0)
    {
      lsp_text = "  " + ui_truncate_cells(first_running.empty() ? "LSP" : first_running, 18);
      if (running_clients > 1)
      {
        lsp_text += " x" + std::to_string(running_clients);
      }
      lsp_text += " ";
      lsp_fg = theme.fg_status_info;
      lsp_bg = theme.bg_status_info;
    }
    else
    {
      lsp_text = "  LSP off ";
    }
    right_segments.push_back({lsp_text, lsp_fg, lsp_bg, false, true, 60});
  }

  // LSP progress: what the servers are working on ("indexing", "3/12 files"),
  // the way helix puts a spinner beside the file name. The frame comes from the
  // clock rather than the render count, so it advances at a fixed rate however
  // often the frame happens to be repainted.
  if (!lsp_clients.empty())
  {
    std::vector<LSPProgress> running;
    for (const auto &client : lsp_clients)
    {
      if (!client || !client->is_running())
      {
        continue;
      }
      for (auto &entry : client->active_progress())
      {
        running.push_back(std::move(entry));
      }
    }
    if (!running.empty())
    {
      static const char *kSpinner[] = {"\u280b", "\u2819", "\u2839", "\u2838", "\u283c",
                                       "\u2834", "\u2826", "\u2827", "\u2807", "\u280f"};
      const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
      std::string text = running.front().message.empty() ? running.front().title
                                                         : running.front().message;
      if (running.front().percentage >= 0)
      {
        text += " " + std::to_string(running.front().percentage) + "%";
      }
      if (running.size() > 1)
      {
        text += " +" + std::to_string(running.size() - 1);
      }
      right_segments.push_back({"  " + std::string(kSpinner[(size_t)((now / 100) % 10)]) + " "
                                    + ui_truncate_cells(text, 24) + " ",
                                theme.fg_status_info,
                                theme.bg_status_info,
                                false,
                                true,
                                65});
    }
  }

  // Discord presence chip: only while the feature is on and something is worth
  // reporting (connected, connecting, or an error from Discord such as a
  // missing asset key). :discord status carries the full detail.
  const std::string &discord_status = discord.status();
  if (config.get_bool("discord_show_status", true) && !discord_status.empty()
      && discord_status != "off")
  {
    std::string text;
    int fg = theme.fg_status_muted;
    int bg = theme.bg_status_muted;
    if (discord_status == "on")
    {
      text = " Discord ";
      fg = theme.fg_status_info;
      bg = theme.bg_status_info;
    }
    else if (discord_status == "idle")
    {
      text = " Discord idle ";
    }
    else if (discord_status == "excluded")
    {
      text = " Discord off (workspace) ";
    }
    else if (discord_status == "error")
    {
      text = " Discord error ";
      fg = theme.fg_status_message;
      bg = theme.bg_status_error;
    }
    else
    {
      text = " Discord... ";
    }
    right_segments.push_back({text, fg, bg, false, true, 45, "\U000F066F", 88});
  }

  // Lua-registered status segments (see jot.status.register). They render on
  // the flat status background and drop first when space runs low.
  if (lua_api)
  {
    for (const auto &seg : lua_api->render_status_segments())
    {
      StatusSegment s;
      s.text = " " + seg.text + " ";
      s.fg = (seg.fg >= 0 && seg.fg <= 255) ? seg.fg : theme.fg_status;
      s.bg = theme.bg_status;
      s.bold = false;
      s.optional = true;
      s.priority = seg.priority;
      if (seg.side == "left")
      {
        left_segments.push_back(std::move(s));
      }
      else
      {
        right_segments.push_back(std::move(s));
      }
    }
  }

  // Message / context content (used by both the Lua handler and the native
  // fallback so the single bar always matches).
  std::string status_context_label = "  " + status_workspace_label(root_dir);

  // The bar is one row (status_height), so what used to be a second row -- the
  // transient `:message` or, normally, the workspace label -- rides as a
  // segment after the left block. A message is kept (it is momentary and worth
  // the space); the workspace label is optional and drops first.
  if (!message.empty())
  {
    left_segments.push_back(
        {"  " + message, theme.fg_status_message, theme.bg_status, true, false, 95});
  }
  else
  {
    left_segments.push_back(
        {status_context_label, theme.fg_status_muted, theme.bg_status, false, true, 20});
  }

  // Hand the raw model to a Lua UI handler when one is registered; it owns
  // layout, drop-to-fit and painting. Native fallback below keeps the exact
  // same behavior when Lua is disabled or no handler exists.
  if (lua_api && lua_api->has_lua_ui_handler("status_line"))
  {
    StatusView view;
    view.x = content_x;
    view.y = y;
    view.w = content_w;
    view.h = status_height;
    view.message = message;
    view.context = status_context_label;
    view.has_selection = (!show_home_menu && active_buf && active_buf->selection.active);
    if (view.has_selection)
    {
      view.sel_lines = std::abs(active_buf->selection.end.y - active_buf->selection.start.y) + 1;
      view.sel_cols = std::abs(active_buf->selection.end.x - active_buf->selection.start.x);
    }
    for (const auto &s : left_segments)
    {
      view.segments.push_back(
          {s.text, s.fg, s.bg, s.bold, s.optional, s.priority, "left", s.symbol, s.symbol_fg});
    }
    for (const auto &s : right_segments)
    {
      view.segments.push_back(
          {s.text, s.fg, s.bg, s.bold, s.optional, s.priority, "right", s.symbol, s.symbol_fg});
    }
    if (lua_api->emit_status(view))
    {
      return;
    }
  }

  const int min_gap = content_w >= 40 ? 2 : 1;
  status_drop_optional_to_fit(right_segments, std::max(0, content_w / 2));
  int right_w = status_layout_width(right_segments);
  int left_budget = std::max(0, content_w - right_w - (right_w > 0 ? min_gap : 0));
  status_drop_optional_to_fit(left_segments, left_budget);

  if (status_layout_width(left_segments) > left_budget && left_segments.size() > 2)
  {
    auto logo = std::find_if(left_segments.begin(),
                             left_segments.end(),
                             [](const StatusSegment &segment)
                             { return segment.text.find("jot") != std::string::npos; });
    if (logo != left_segments.end())
    {
      left_segments.erase(logo);
    }
  }

  if (status_layout_width(left_segments) > left_budget && left_segments.size() > 2)
  {
    int excess = status_layout_width(left_segments) - left_budget;
    size_t file_index = 0;
    for (size_t i = 0; i < left_segments.size(); i++)
    {
      if (left_segments[i].text.find(file_label) != std::string::npos)
      {
        file_index = i;
        break;
      }
    }
    StatusSegment &file_segment = left_segments[file_index];
    int target = std::max(4, ui_cell_count(file_segment.text) - excess);
    file_segment.text = ui_truncate_cells(file_segment.text, target);
  }

  while (status_layout_width(left_segments) > left_budget && !left_segments.empty())
  {
    StatusSegment &last = left_segments.back();
    int target = ui_cell_count(last.text) - (status_layout_width(left_segments) - left_budget);
    if (target <= 0)
    {
      left_segments.pop_back();
    }
    else
    {
      last.text = ui_take_cells(last.text, target);
      break;
    }
  }

  right_w = status_layout_width(right_segments);
  int right_x = content_x + std::max(0, content_w - right_w);
  int left_w = std::max(0, right_x - (right_w > 0 ? min_gap : 0));
  status_draw_segmented_at(ui, content_x, y, left_w - content_x, left_segments);
  if (right_w > 0)
  {
    status_draw_segmented_at(ui, right_x, y, right_w, right_segments);
  }
}

