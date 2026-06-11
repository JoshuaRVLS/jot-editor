#include "bracket.h"
#include "editor.h"
#include <cstdio>
#include <filesystem>
#include <functional>
#include <unordered_map>

namespace {
constexpr int kLineNumberGutterWidth = 7;
std::string ellipsize_right(const std::string &s, int max_len) {
  if (max_len <= 0) {
    return "";
  }
  if ((int)s.size() <= max_len) {
    return s;
  }
  if (max_len <= 3) {
    return s.substr(0, (size_t)max_len);
  }
  return s.substr(0, (size_t)(max_len - 3)) + "...";
}

std::string file_tab_base_name(const FileBuffer &buffer) {
  std::string base;
  if (!buffer.filepath.empty()) {
    base = std::filesystem::path(buffer.filepath).filename().string();
  }
  return base.empty() ? "[No Name]" : base;
}

int tab_advance(int visual_col, int tab_size) {
  const int ts = std::max(1, tab_size);
  const int rem = visual_col % ts;
  return rem == 0 ? ts : (ts - rem);
}

int compute_visual_column(const std::string &line, int logical_col,
                          int tab_size) {
  int clamped = std::clamp(logical_col, 0, (int)line.size());
  int visual = 0;
  for (int i = 0; i < clamped; i++) {
    if (line[i] == '\t') {
      visual += tab_advance(visual, tab_size);
    } else {
      visual += 1;
    }
  }
  return visual;
}

void compute_code_cursor_screen_pos(const SplitPane &pane, const FileBuffer &buf,
                                    bool show_minimap, int minimap_width,
                                    int tab_size, int tab_height,
                                    int &display_x, int &display_y) {
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  const int code_start_x = pane.x + 1 + kLineNumberGutterWidth;
  const int code_end_x = pane.x + draw_w - 2;
  const int min_y = pane.y + tab_height;
  int max_y = pane.y + pane.h - 1;

  display_y = buf.cursor.y - buf.scroll_offset + pane.y + tab_height;

  int logical_cursor_x = buf.cursor.x;
  int logical_scroll_x = buf.scroll_x;
  if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.line_count()) {
    const std::string &line = buf.line(buf.cursor.y);
    int cursor_visual = compute_visual_column(line, logical_cursor_x, tab_size);
    int scroll_visual = compute_visual_column(line, logical_scroll_x, tab_size);
    display_x = code_start_x + (cursor_visual - scroll_visual);
  } else {
    display_x = code_start_x + (logical_cursor_x - logical_scroll_x);
  }

  if (max_y < min_y)
    max_y = min_y;
  if (display_y < min_y)
    display_y = min_y;
  if (display_y > max_y)
    display_y = max_y;

  if (code_end_x < code_start_x) {
    display_x = code_start_x;
    return;
  }
  if (display_x < code_start_x)
    display_x = code_start_x;
  if (display_x > code_end_x)
    display_x = code_end_x;
}
} // namespace

void Editor::render() {
  IntegratedTerminal *active_terminal = get_integrated_terminal();

  if (!needs_redraw) {
    if (show_home_menu) {
      ui->hide_cursor();
      ui->flush_cursor();
      return;
    }

    // Keep cursor visibility in sync even when no redraw is needed.
    if (show_context_menu) {
      ui->hide_cursor();
      ui->flush_cursor();
      return;
    }

    if (show_command_palette || show_search || show_save_prompt ||
        show_quit_prompt) {
      if (show_command_palette) {
        int x = std::min(ui->get_width() - 1,
                         std::max(1, (int)command_palette_query.length() + 1));
        int y = ui->get_height() - 1;
        ui->set_cursor(x, y);
      }
      ui->flush_cursor();
      return;
    }
    if (show_integrated_terminal && active_terminal &&
        active_terminal->is_focused()) {
      place_integrated_terminal_cursor();
      return;
    }
    if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
      ui->hide_cursor();
      ui->flush_cursor();
      return;
    }
    if (!telescope.is_active() && !panes.empty()) {
      auto &pane = get_pane();
      auto &buf = get_buffer(pane.buffer_id);
      int display_x = 0;
      int display_y = 0;
      compute_code_cursor_screen_pos(pane, buf, show_minimap, minimap_width,
                                     tab_size, tab_height,
                                     display_x, display_y);
      ui->set_cursor(display_x, display_y);
      ui->flush_cursor();
    }
    return;
  }

  ui->clear();
  int w = ui->get_render_width();

  if (show_home_menu) {
    render_home_menu();
    render_status_line();
    ui->hide_cursor();
    ui->render();
    needs_redraw = false;
    return;
  }

  render_tabs();
  update_pane_layout();

  if (telescope.is_active()) {
    if (show_sidebar) {
      render_sidebar();
    }
    render_panes();
    render_lsp_completion();
    render_integrated_terminal();
    render_telescope();
    render_status_line();
    ui->hide_cursor();
    ui->render();
    needs_redraw = false;
    return;
  } else {
    if (image_viewer.is_active()) {
      int img_w = w / 2;
      int editor_w = w - img_w;
      if (!panes.empty()) {
        panes[0].w = editor_w;
      }
      render_image_viewer();
    } else if (show_save_prompt) {
      render_save_prompt();
    } else if (show_quit_prompt) {
      render_quit_prompt();
    } else {
      if (show_sidebar) {
        render_sidebar();
      }
      render_panes();
      render_lsp_completion();
      render_integrated_terminal();
    }

    render_status_line();
    render_command_palette();
    render_search_panel();
    render_popup();
    render_context_menu();

    if (easter_egg_timer > 0) {
      render_easter_egg();
      easter_egg_timer--;
      needs_redraw = true;
    }

    // Set cursor state BEFORE ui->render() so the full-row paint emits the
    // correct cursor at the end of the frame.
    if (show_context_menu) {
      ui->hide_cursor();
    } else if (show_command_palette || show_search || show_save_prompt ||
        show_quit_prompt) {
      if (show_command_palette) {
        int x = std::min(ui->get_width() - 1,
                         std::max(1, (int)command_palette_query.length() + 1));
        int y = ui->get_height() - 1;
        ui->set_cursor(x, y);
      }
    } else if (show_integrated_terminal && active_terminal &&
               active_terminal->is_focused()) {
      place_integrated_terminal_cursor();
    } else if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
      ui->hide_cursor();
    } else if (!telescope.is_active()) {
      if (!panes.empty()) {
        auto &pane = get_pane();
        auto &buf = get_buffer(pane.buffer_id);
        int display_x = 0;
        int display_y = 0;
        compute_code_cursor_screen_pos(pane, buf, show_minimap, minimap_width,
                                       tab_size, tab_height,
                                       display_x, display_y);
        ui->set_cursor(display_x, display_y);
      }
    }

    ui->render();
    needs_redraw = false;
  }
}

void Editor::render_panes() {
  for (const auto &pane : panes) {
    render_pane(pane);
  }
  render_pane_resize_guides();
}

void Editor::render_pane_resize_guides() {
  if (!pane_resize_dragging || pane_resize_node < 0 ||
      pane_resize_node >= (int)pane_tree.size()) {
    return;
  }

  int total_w = std::max(1, ui->get_render_width());
  int reserved_terminal_h = 0;
  if (show_integrated_terminal && !integrated_terminals.empty()) {
    reserved_terminal_h =
        std::clamp(integrated_terminal_height, 5, std::max(5, ui->get_height() / 2));
  }
  int total_h =
      std::max(1, ui->get_height() - status_height - reserved_terminal_h);
  int max_sidebar = std::max(0, total_w - 20);
  int origin_x = show_sidebar ? std::min(sidebar_width, max_sidebar) : 0;
  int available_w = std::max(1, total_w - origin_x);

  std::function<void(int, int, int, int, int)> draw_node =
      [&](int node_index, int x, int y, int w, int h) {
        if (node_index < 0 || node_index >= (int)pane_tree.size() || w <= 1 ||
            h <= 1) {
          return;
        }
        const PaneTreeNode &node = pane_tree[node_index];
        if (node.leaf) {
          return;
        }

        float ratio = std::clamp(node.ratio, 0.1f, 0.9f);
        if (node.vertical) {
          int first_w = std::max(1, (int)(w * ratio));
          if (w >= 2) {
            first_w = std::min(first_w, w - 1);
          }
          if (node_index == pane_resize_node) {
            int bx = x + first_w - 1;
            for (int row = y; row < y + h; row++) {
              ui->draw_text(bx, row, "│", theme.fg_active_border,
                            theme.bg_active_border, true);
            }
          }
          int second_w = std::max(1, w - first_w);
          draw_node(node.first, x, y, first_w, h);
          draw_node(node.second, x + first_w, y, second_w, h);
        } else {
          int first_h = std::max(1, (int)(h * ratio));
          if (h >= 2) {
            first_h = std::min(first_h, h - 1);
          }
          if (node_index == pane_resize_node) {
            int by = y + first_h - 1;
            for (int col = x; col < x + w; col++) {
              ui->draw_text(col, by, "─", theme.fg_active_border,
                            theme.bg_active_border, true);
            }
          }
          int second_h = std::max(1, h - first_h);
          draw_node(node.first, x, y, w, first_h);
          draw_node(node.second, x, y + first_h, w, second_h);
        }
      };

  draw_node(pane_root, origin_x, 0, available_w, total_h);
}

Editor::FileTabLayout Editor::build_file_tab_layout(const SplitPane &pane,
                                                    int draw_w) {
  FileTabLayout layout;
  layout.x = pane.x + 1;
  layout.y = pane.y;
  layout.w = std::max(1, draw_w - 2);

  std::vector<int> tab_ids = pane.tab_buffer_ids;
  if (tab_ids.empty() && pane.buffer_id >= 0 &&
      pane.buffer_id < (int)buffers.size()) {
    tab_ids.push_back(pane.buffer_id);
  }

  std::vector<std::string> base_names(buffers.size());
  std::unordered_map<std::string, int> base_count;
  for (int id : tab_ids) {
    if (id < 0 || id >= (int)buffers.size()) {
      continue;
    }
    std::string base = file_tab_base_name(buffers[id]);
    base_names[id] = base;
    base_count[base]++;
  }

  int hidden_total = 0;
  for (int id : tab_ids) {
    if (id >= 0 && id < (int)buffers.size()) {
      hidden_total++;
    }
  }

  int tab_x = layout.x;
  for (int tab_i = 0; tab_i < (int)tab_ids.size(); tab_i++) {
    int id = tab_ids[tab_i];
    if (id < 0 || id >= (int)buffers.size()) {
      continue;
    }

    std::string name = base_names[id];
    if (base_count[name] > 1 && !buffers[id].filepath.empty()) {
      std::filesystem::path p(buffers[id].filepath);
      std::string parent = p.parent_path().filename().string();
      if (!parent.empty()) {
        name += " <" + parent + ">";
      }
    }

    const int remaining_valid = hidden_total - (int)layout.segments.size();
    std::string overflow = remaining_valid > 1
                               ? ("… +" + std::to_string(remaining_valid - 1))
                               : "";
    const int overflow_reserve = overflow.empty() ? 0 : (int)overflow.size() + 1;
    const int hard_end = layout.x + layout.w - overflow_reserve;
    int available = hard_end - tab_x;
    if (available < 7) {
      layout.hidden_count = remaining_valid;
      break;
    }

    std::string marker = buffers[id].modified ? " ●" : "";
    std::string label_text = name + marker;
    int max_label_w = std::max(5, std::min(24, available - 2));
    int max_name_w = std::max(1, max_label_w - 3);
    std::string text = " " + ellipsize_right(label_text, max_name_w) + "  ";

    int need = (int)text.size() + 2; // close control + separator
    if (tab_x + need > hard_end) {
      layout.hidden_count = remaining_valid;
      break;
    }

    FileTabSegment segment;
    segment.buffer_id = id;
    segment.x = tab_x;
    segment.label_x = tab_x;
    segment.label = text;
    segment.close_x = tab_x + (int)text.size();
    segment.end_x = segment.close_x + 2;
    segment.active = (id == pane.buffer_id);
    segment.modified = buffers[id].modified;
    segment.preview = buffers[id].is_preview;
    layout.segments.push_back(std::move(segment));
    tab_x += need;
  }

  if (layout.hidden_count > 0) {
    layout.overflow_label = "… +" + std::to_string(layout.hidden_count);
    layout.overflow_x =
        std::max(layout.x, layout.x + layout.w - (int)layout.overflow_label.size());
  }

  return layout;
}

void Editor::render_pane(const SplitPane &pane) {
  int draw_w = std::max(1, pane.w);
  if (pane.h <= 0)
    return;

  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  render_buffer_content(pane, pane.buffer_id);

  if (show_minimap && pane.w > 20) {
    render_minimap(pane.x + draw_w, pane.y + 1, minimap_width, pane.h - 1,
                   pane.buffer_id);
  }

  UIRect rect = {pane.x, pane.y, draw_w, pane.h};
  int border_fg = pane.active ? theme.fg_active_border : theme.fg_panel_border;
  int border_bg = pane.active ? theme.bg_active_border : theme.bg_panel_border;
  ui->draw_border(rect, border_fg, border_bg);

  // Pane-local file tabs.
  {
    FileTabLayout tabs = build_file_tab_layout(pane, draw_w);
    UIRect tab_rect = {tabs.x, tabs.y, tabs.w, 1};
    ui->fill_rect(tab_rect, " ", theme.fg_status, theme.bg_status);

    for (const auto &tab : tabs.segments) {
      int fg = tab.active ? theme.fg_tab_active : theme.fg_tab_inactive;
      int bg = tab.active ? theme.bg_tab_active : theme.bg_tab_inactive;
      ui->draw_text(tab.label_x, tabs.y, tab.label, fg, bg, tab.active,
                    tab.preview);
      ui->draw_text(tab.close_x, tabs.y, "×", theme.fg_tab_close, bg);
      ui->draw_text(tab.close_x + 1, tabs.y, "│", theme.fg_tab_separator,
                    theme.bg_status);
    }

    if (!tabs.overflow_label.empty()) {
      ui->draw_text(tabs.overflow_x, tabs.y, tabs.overflow_label,
                    theme.fg_comment, theme.bg_status);
    }
  }

  render_scrollbar(pane, draw_w);
}

void Editor::render_scrollbar(const SplitPane &pane, int draw_w) {
  if (draw_w < 3) {
    return;
  }

  auto &buf = get_buffer(pane.buffer_id);

  const int track_x = pane.x + draw_w - 1;
  const int track_y = pane.y + tab_height;
  const int track_h = std::max(0, pane.h - tab_height - 1);
  if (track_h <= 0) {
    return;
  }

  const int total_lines = std::max(1, (int)buf.line_count());
  const int visible_lines = std::max(1, track_h);
  const int max_scroll = std::max(0, total_lines - visible_lines);
  const int clamped_scroll = std::clamp(buf.scroll_offset, 0, max_scroll);

  int thumb_h = track_h;
  if (max_scroll > 0) {
    thumb_h = std::max(1, (visible_lines * visible_lines) / total_lines);
    thumb_h = std::min(track_h, thumb_h);
  }

  int thumb_y = track_y;
  if (max_scroll > 0 && track_h > thumb_h) {
    thumb_y = track_y + (clamped_scroll * (track_h - thumb_h)) / max_scroll;
  }

  for (int i = 0; i < track_h; i++) {
    ui->draw_text(track_x, track_y + i, "│", theme.fg_panel_border,
                  theme.bg_default);
  }

  const int thumb_fg = pane.active ? theme.fg_active_border : theme.fg_line_num;
  for (int i = 0; i < thumb_h; i++) {
    ui->draw_text(track_x, thumb_y + i, "█", thumb_fg, theme.bg_default);
  }
}
