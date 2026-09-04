#include "editor.h"
#include "lua_bridge/api.h"

#include <algorithm>
#include <sstream>

namespace
{
  std::string clip(std::string text, int width)
  {
    if (width <= 0)
    {
      return "";
    }
    if ((int)text.size() <= width)
    {
      return text;
    }
    if (width <= 2)
    {
      return text.substr(0, (size_t)width);
    }
    return text.substr(0, (size_t)(width - 2)) + "..";
  }

  std::vector<std::string> split_lines(const std::string &text, int max_lines)
  {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string line;
    while ((int)out.size() < max_lines && std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      out.push_back(line);
    }
    return out;
  }
} // namespace

void Editor::render_debugger_panel()
{
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_DEBUG || !ui)
  {
    return;
  }
  int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return;
  }
  int panel_x = std::max(0, ui->get_render_width() - panel_w);
  int panel_y = topbar_height();
  int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  UIRect panel = {panel_x, panel_y, panel_w, panel_h};

  ui->fill_rect(panel, " ", theme.fg_terminal, theme.bg_terminal);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal);

  ui->draw_text(panel_x + 1,
                panel_y,
                " Debug ",
                theme.fg_terminal_tab_focused,
                theme.bg_terminal_tab_focused,
                true);

  // Hand the model to a Lua UI handler when one is registered; it owns the
  // paint. Native fallback below stays byte-identical.
  SidePanelView view;
  view.x = panel_x;
  view.y = panel_y;
  view.w = panel_w;
  view.h = panel_h;
  view.title = " Debug ";

  auto emit_view = [&]() -> bool
  {
    if (lua_api && lua_api->has_lua_ui_handler("side_panel"))
    {
      return lua_api->emit_side_panel(view);
    }
    return false;
  };

  int tab_x = panel_x + 1;
  int tab_y = panel_y + 1;
  for (int i = 0; i < (int)debugger_session_state.size(); i++)
  {
    const auto &state = debugger_session_state[i];
    std::string status = state.stopped ? " paused" : (state.running ? " run" : " done");
    std::string label = " " + state.name + status + " ";
    if (tab_x + (int)label.size() >= panel_x + panel_w - 1)
    {
      break;
    }
    bool active = i == current_debugger_session;
    view.tabs.push_back({label, active});
    ui->draw_text(tab_x,
                  tab_y,
                  label,
                  active ? theme.fg_terminal_tab_focused : theme.fg_terminal_tab_inactive,
                  active ? theme.bg_terminal_tab_focused : theme.bg_terminal_tab_inactive,
                  active);
    tab_x += (int)label.size() + 1;
  }

  int content_y = panel_y + 2;
  int content_h = std::max(1, panel_h - 3);
  int content_x = panel_x + 1;
  int content_w = std::max(1, panel_w - 2);

  if (debugger_sessions.empty() || debugger_session_state.empty() || current_debugger_session < 0
      || current_debugger_session >= (int)debugger_session_state.size())
  {
    std::vector<std::string> lines = {"No debug session",
                                      "",
                                      ":debug <program> [args]",
                                      ":debugconfig <name>",
                                      ":debugattach <pid>",
                                      "",
                                      "Breakpoints: click gutter"};
    int row = 0;
    for (const auto &line : lines)
    {
      if (row >= content_h)
      {
        break;
      }
      int fg = row == 0 ? theme.fg_status_info : theme.fg_comment;
      SidePanelRowView r;
      r.text = clip(line, content_w);
      r.fg = fg;
      r.bg = theme.bg_terminal;
      r.bold = row == 0;
      view.rows.push_back(std::move(r));
      ui->draw_text(
          content_x, content_y + row, clip(line, content_w), fg, theme.bg_terminal, row == 0);
      row++;
    }
    if (!debugger_configs.empty() && row + 1 < content_h)
    {
      SidePanelRowView r;
      r.text = "Configs";
      r.fg = theme.fg_status_info;
      r.bg = theme.bg_terminal;
      r.bold = true;
      view.rows.push_back(std::move(r));
      ui->draw_text(
          content_x, content_y + row, "Configs", theme.fg_status_info, theme.bg_terminal, true);
      row++;
      for (const auto &cfg : debugger_configs)
      {
        if (row >= content_h)
        {
          break;
        }
        SidePanelRowView rc;
        rc.text = clip("  " + cfg.name, content_w);
        rc.fg = theme.fg_terminal;
        rc.bg = theme.bg_terminal;
        view.rows.push_back(std::move(rc));
        ui->draw_text(content_x,
                      content_y + row,
                      clip("  " + cfg.name, content_w),
                      theme.fg_terminal,
                      theme.bg_terminal);
        row++;
      }
    }
    if (emit_view())
    {
      return;
    }
    return;
  }

  int x1 = content_x;
  int x2 = content_x;
  int x3 = content_x;
  int x4 = content_x;
  int col1_w = content_w;
  int col2_w = content_w;
  int col3_w = content_w;
  int col4_w = content_w;

  const auto &state = debugger_session_state[current_debugger_session];
  {
    SidePanelRowView r;
    r.text = "Threads / Stack";
    r.fg = theme.fg_status_info;
    r.bg = theme.bg_terminal;
    r.bold = true;
    view.rows.push_back(std::move(r));
  }
  ui->draw_text(x1, content_y, "Threads / Stack", theme.fg_status_info, theme.bg_terminal, true);

  int row = 1;
  for (const auto &thread : state.threads)
  {
    if (row >= content_h)
    {
      break;
    }
    std::string prefix = thread.id == state.active_thread_id ? "> " : "  ";
    {
      SidePanelRowView r;
      r.text = clip(prefix + "T" + std::to_string(thread.id) + " " + thread.name, col1_w);
      r.fg = theme.fg_terminal;
      r.bg = theme.bg_terminal;
      view.rows.push_back(std::move(r));
    }
    ui->draw_text(x1,
                  content_y + row,
                  clip(prefix + "T" + std::to_string(thread.id) + " " + thread.name, col1_w),
                  theme.fg_terminal,
                  theme.bg_terminal);
    row++;
    for (const auto &frame : thread.frames)
    {
      if (row >= content_h)
      {
        break;
      }
      std::string loc = frame.filepath.empty()
                            ? frame.name
                            : get_filename(frame.filepath) + ":" + std::to_string(frame.line + 1);
      {
        SidePanelRowView r;
        r.text = clip("  #" + loc, col1_w);
        r.fg = theme.fg_comment;
        r.bg = theme.bg_terminal;
        view.rows.push_back(std::move(r));
      }
      ui->draw_text(
          x1, content_y + row, clip("  #" + loc, col1_w), theme.fg_comment, theme.bg_terminal);
      row++;
    }
  }

  if (row < content_h)
  {
    row++;
  }
  if (row < content_h)
  {
    {
      SidePanelRowView r;
      r.text = "Variables";
      r.fg = theme.fg_status_info;
      r.bg = theme.bg_terminal;
      r.bold = true;
      view.rows.push_back(std::move(r));
    }
    ui->draw_text(x2, content_y + row, "Variables", theme.fg_status_info, theme.bg_terminal, true);
    row++;
  }
  for (const auto &var : state.variables)
  {
    if (row >= content_h)
    {
      break;
    }
    std::string text = var.name + " = " + var.value;
    if (!var.type.empty())
    {
      text += " : " + var.type;
    }
    {
      SidePanelRowView r;
      r.text = clip(text, col2_w);
      r.fg = theme.fg_terminal;
      r.bg = theme.bg_terminal;
      view.rows.push_back(std::move(r));
    }
    ui->draw_text(x2, content_y + row, clip(text, col2_w), theme.fg_terminal, theme.bg_terminal);
    row++;
  }

  if (row < content_h)
  {
    row++;
  }
  if (row < content_h)
  {
    {
      SidePanelRowView r;
      r.text = "Memory";
      r.fg = theme.fg_status_info;
      r.bg = theme.bg_terminal;
      r.bold = true;
      view.rows.push_back(std::move(r));
    }
    ui->draw_text(x3, content_y + row, "Memory", theme.fg_status_info, theme.bg_terminal, true);
    row++;
  }
  for (const auto &mem : state.memory_rows)
  {
    if (row >= content_h)
    {
      break;
    }
    {
      SidePanelRowView r;
      r.text = clip(mem.address + "  " + mem.bytes + "  " + mem.ascii, col3_w);
      r.fg = theme.fg_terminal;
      r.bg = theme.bg_terminal;
      view.rows.push_back(std::move(r));
    }
    ui->draw_text(x3,
                  content_y + row,
                  clip(mem.address + "  " + mem.bytes + "  " + mem.ascii, col3_w),
                  theme.fg_terminal,
                  theme.bg_terminal);
    row++;
  }

  if (row < content_h)
  {
    row++;
  }
  if (row < content_h)
  {
    {
      SidePanelRowView r;
      r.text = "Disassembly / Output";
      r.fg = theme.fg_status_info;
      r.bg = theme.bg_terminal;
      r.bold = true;
      view.rows.push_back(std::move(r));
    }
    ui->draw_text(
        x4, content_y + row, "Disassembly / Output", theme.fg_status_info, theme.bg_terminal, true);
    row++;
  }
  for (const auto &inst : state.instructions)
  {
    if (row >= content_h)
    {
      break;
    }
    {
      SidePanelRowView r;
      r.text = clip(inst.address + "  " + inst.instruction, col4_w);
      r.fg = theme.fg_terminal;
      r.bg = theme.bg_terminal;
      view.rows.push_back(std::move(r));
    }
    ui->draw_text(x4,
                  content_y + row,
                  clip(inst.address + "  " + inst.instruction, col4_w),
                  theme.fg_terminal,
                  theme.bg_terminal);
    row++;
  }

  if (row < content_h)
  {
    auto lines = split_lines(state.output, content_h - row);
    for (const auto &line : lines)
    {
      if (row >= content_h)
      {
        break;
      }
      {
        SidePanelRowView r;
        r.text = clip(line, col4_w);
        r.fg = theme.fg_comment;
        r.bg = theme.bg_terminal;
        view.rows.push_back(std::move(r));
      }
      ui->draw_text(x4, content_y + row, clip(line, col4_w), theme.fg_comment, theme.bg_terminal);
      row++;
    }
  }
  if (!state.last_error.empty())
  {
    view.error = clip(" " + state.last_error, panel_w - 2);
    ui->draw_text(panel_x + 1,
                  panel_y + panel_h - 1,
                  clip(" " + state.last_error, panel_w - 2),
                  theme.fg_status_error,
                  theme.bg_status_error,
                  true);
  }
  if (emit_view())
  {
    return;
  }
}
