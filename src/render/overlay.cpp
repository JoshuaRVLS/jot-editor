#include "bracket.h"
#include "editor.h"
#include "folding.h"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace {
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

std::string completion_kind_icon(int kind, bool use_nerd_icons) {
  if (!use_nerd_icons) {
    switch (kind) {
    case 2:
      return "[M] "; // Method
    case 3:
      return "[F] "; // Function
    case 4:
      return "[C] "; // Constructor
    case 5:
      return "[Fd] "; // Field
    case 6:
      return "[V] "; // Variable
    case 7:
      return "[Cl] "; // Class
    case 8:
      return "[I] "; // Interface
    case 9:
      return "[Mo] "; // Module
    case 10:
      return "[P] "; // Property
    case 12:
      return "[Val] "; // Value
    case 14:
      return "[K] "; // Keyword
    default:
      return "[?] ";
    }
  }

  switch (kind) {
  case 2:
    return "󰊕 "; // Method
  case 3:
    return "󰊕 "; // Function
  case 4:
    return " "; // Constructor
  case 5:
    return "󰇽 "; // Field
  case 6:
    return "󰀫 "; // Variable
  case 7:
    return "󰠱 "; // Class
  case 8:
    return " "; // Interface
  case 9:
    return " "; // Module
  case 10:
    return "󰜢 "; // Property
  case 12:
    return "󰎠 "; // Value
  case 14:
    return "󰌋 "; // Keyword
  default:
    return "󰘍 ";
  }
}

std::string completion_kind_name(int kind) {
  switch (kind) {
  case 2:
    return "method";
  case 3:
    return "function";
  case 4:
    return "ctor";
  case 5:
    return "field";
  case 6:
    return "variable";
  case 7:
    return "class";
  case 8:
    return "interface";
  case 9:
    return "module";
  case 10:
    return "property";
  case 12:
    return "value";
  case 14:
    return "keyword";
  default:
    return "symbol";
  }
}

std::string one_line_text(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  bool last_space = false;
  for (char c : text) {
    unsigned char uc = (unsigned char)c;
    if (c == '\n' || c == '\r' || c == '\t' || std::isspace(uc)) {
      if (!last_space && !out.empty()) {
        out.push_back(' ');
        last_space = true;
      }
      continue;
    }
    out.push_back(c);
    last_space = false;
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

std::string clip_text(const std::string &text, int max_w) {
  if (max_w <= 0) {
    return "";
  }
  if ((int)text.size() <= max_w) {
    return text;
  }
  if (max_w <= 2) {
    return text.substr(0, max_w);
  }
  return text.substr(0, max_w - 2) + "..";
}

std::string clip_path_left(const std::string &text, int max_w) {
  if (max_w <= 0) {
    return "";
  }
  if ((int)text.size() <= max_w) {
    return text;
  }
  if (max_w <= 3) {
    return text.substr(0, max_w);
  }
  return ".." + text.substr(text.size() - (max_w - 2));
}

std::string telescope_icon(bool directory, bool use_nerd_icons) {
  if (!use_nerd_icons) {
    return directory ? "[D] " : "[F] ";
  }
  return directory ? " " : "󰈔 ";
}

struct TelescopeLayout {
  bool valid = false;
  bool show_preview = false;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int inner_x = 0;
  int inner_y = 0;
  int inner_w = 0;
  int inner_h = 0;
  int query_x = 0;
  int query_y = 0;
  int query_w = 0;
  int body_y = 0;
  int body_h = 0;
  int list_x = 0;
  int list_y = 0;
  int list_w = 0;
  int list_h = 0;
  int preview_x = 0;
  int preview_y = 0;
  int preview_w = 0;
  int preview_h = 0;
  int footer_y = 0;
};

int syntax_preview_color(const Theme &theme, int token) {
  switch (token) {
  case 1:
    return theme.fg_keyword;
  case 2:
    return theme.fg_string;
  case 3:
    return theme.fg_comment;
  case 4:
    return theme.fg_number;
  case 5:
    return theme.fg_type;
  case 6:
    return theme.fg_function;
  default:
    return theme.fg_telescope_preview;
  }
}

TelescopeLayout telescope_layout_for(const Editor &editor, UI *ui,
                                     int tab_height, int status_height,
                                     bool show_sidebar, int sidebar_w) {
  TelescopeLayout layout;
  int h = ui->get_height();
  int w = ui->get_render_width();
  if (w < 4 || h < 5) {
    return layout;
  }

  int content_x = show_sidebar ? std::max(0, sidebar_w) : 0;
  int content_w = std::max(1, w - content_x);
  int top_bound = std::max(0, tab_height + 1);
  int bottom_bound = std::max(top_bound + 1, h - status_height - 1);
  int usable_h = std::max(1, bottom_bound - top_bound);

  layout.w = std::clamp(content_w - 2, std::min(content_w, 42), content_w);
  if (content_w >= 72) {
    layout.w = std::min(content_w - 2, std::max(72, content_w * 9 / 10));
  }
  layout.h = std::clamp(usable_h - 1, std::min(usable_h, 10), usable_h);
  if (usable_h >= 18) {
    layout.h = std::min(usable_h - 1, std::max(16, usable_h * 5 / 6));
  }

  layout.x = content_x + std::max(0, (content_w - layout.w + 1) / 2);
  layout.x = std::clamp(layout.x, content_x,
                        std::max(content_x, content_x + content_w - layout.w));
  layout.y = top_bound + std::max(0, (usable_h - layout.h) / 2);
  layout.inner_x = layout.x + 1;
  layout.inner_y = layout.y + 1;
  layout.inner_w = std::max(1, layout.w - 2);
  layout.inner_h = std::max(1, layout.h - 2);
  layout.query_x = layout.inner_x + 1;
  layout.query_y = layout.inner_y + 2;
  layout.query_w = std::max(1, layout.inner_w - 2);
  layout.footer_y = layout.y + layout.h - 2;
  layout.body_y = layout.inner_y + 4;
  layout.body_h = std::max(1, layout.footer_y - layout.body_y);
  layout.show_preview = layout.inner_w >= 64 && layout.body_h >= 4;
  int list_panel_w =
      layout.show_preview ? std::max(26, layout.inner_w * 42 / 100)
                          : layout.inner_w;
  layout.list_x = layout.inner_x + 1;
  layout.list_y = layout.body_y;
  layout.list_w =
      layout.show_preview ? std::max(1, list_panel_w - 2)
                          : std::max(1, layout.inner_w - 2);
  layout.list_h = layout.body_h;
  if (layout.show_preview) {
    layout.preview_x = layout.inner_x + list_panel_w + 2;
    layout.preview_y = layout.body_y;
    layout.preview_w =
        std::max(1, layout.inner_x + layout.inner_w - layout.preview_x - 1);
    layout.preview_h = layout.body_h;
  }
  layout.valid = true;
  (void)editor;
  return layout;
}
} // namespace

void Editor::render_lsp_completion() {
  if (!lsp_completion_visible || lsp_completion_items.empty() ||
      panes.empty()) {
    return;
  }

  auto &pane = get_pane();
  auto &buf = get_buffer(pane.buffer_id);
  if (!lsp_completion_filepath.empty() &&
      buf.filepath != lsp_completion_filepath) {
    hide_lsp_completion();
    return;
  }
  if (buf.cursor.y != lsp_completion_replace_start.y ||
      buf.cursor.x < lsp_completion_replace_start.x) {
    hide_lsp_completion();
    return;
  }
  refresh_lsp_completion_filter();
  if (!lsp_completion_visible || lsp_completion_items.empty()) {
    return;
  }

  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  const int line_num_width = 8;
  const bool use_nerd_icons = config.get_bool("lsp_completion_nerd_icons", true);
  int visible_h = std::max(1, pane.h - tab_height - 1);
  int visible_w = std::max(12, draw_w - 2 - line_num_width);
  const int max_items_cfg =
      std::clamp(config.get_int("lsp_completion_max_items", 8), 3, 20);
  int max_items = std::min(max_items_cfg, (int)lsp_completion_items.size());
  int selected = std::clamp(lsp_completion_selected, 0,
                            (int)lsp_completion_items.size() - 1);
  int start_idx = std::max(0, selected - max_items + 1);
  if (selected < start_idx) {
    start_idx = selected;
  }
  if (start_idx + max_items > (int)lsp_completion_items.size()) {
    start_idx = std::max(0, (int)lsp_completion_items.size() - max_items);
  }

  int longest_label = 12;
  int longest_meta = 8;
  for (int i = start_idx; i < start_idx + max_items; i++) {
    if (i < 0 || i >= (int)lsp_completion_items.size()) {
      continue;
    }
    longest_label = std::max(longest_label,
                             (int)lsp_completion_items[i].label.size() + 4);
    std::string meta = completion_kind_name(lsp_completion_items[i].kind);
    if (!lsp_completion_items[i].detail.empty()) {
      meta += " " + one_line_text(lsp_completion_items[i].detail);
    } else if (!lsp_completion_items[i].documentation.empty()) {
      meta += " " + one_line_text(lsp_completion_items[i].documentation);
    }
    longest_meta = std::max(longest_meta, std::min(32, (int)meta.size()));
  }

  int box_w = std::clamp(longest_label + longest_meta + 8, 28,
                         std::min(visible_w, 82));
  int footer_h = 1;
  int box_h = max_items + footer_h;

  int safe_cursor_y = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
  const std::string &line = buf.line(safe_cursor_y);
  int cursor_visual = compute_visual_column(line, buf.cursor.x, tab_size);
  int scroll_visual = compute_visual_column(line, buf.scroll_x, tab_size);
  int cursor_x = pane.x + 1 + line_num_width + (cursor_visual - scroll_visual);
  int cursor_row = 0;
  const int viewport_h = std::max(1, pane.h - tab_height - 1);
  for (int row = 0; row < viewport_h; row++) {
    int line = Folding::buffer_line_for_visible_offset(
        buf.fold_ranges, buf.scroll_offset, row, (int)buf.line_count());
    if (line >= 0 && line == buf.cursor.y &&
        !Folding::is_line_hidden(buf.fold_ranges, line)) {
      cursor_row = row;
      break;
    }
  }
  int cursor_y = pane.y + tab_height + cursor_row;

  int min_x = pane.x + 1 + line_num_width;
  int max_x = pane.x + draw_w - box_w - 1;
  if (max_x < min_x) {
    max_x = min_x;
  }

  int min_y = pane.y + tab_height;
  int max_y = pane.y + visible_h - box_h;
  if (max_y < min_y) {
    max_y = min_y;
  }

  auto clamp_box_x = [&](int x) { return std::clamp(x, min_x, max_x); };
  auto clamp_box_y = [&](int y) { return std::clamp(y, min_y, max_y); };
  auto border_hits_cursor = [&](int x, int y) {
    int left = x - 1;
    int right = x + box_w;
    int top = y - 1;
    int bottom = y + box_h;
    return cursor_x >= left && cursor_x <= right && cursor_y >= top &&
           cursor_y <= bottom;
  };

  // Try multiple placements and pick the first where full bordered popup
  // doesn't touch the cursor cell.
  std::vector<std::pair<int, int>> candidates = {
      {cursor_x + 2, cursor_y + 2},
      {cursor_x + 2, cursor_y - box_h - 2},
      {cursor_x - box_w - 2, cursor_y + 2},
      {cursor_x - box_w - 2, cursor_y - box_h - 2},
      {cursor_x + 2, cursor_y},
      {cursor_x - box_w - 2, cursor_y},
      {cursor_x, cursor_y + 2},
      {cursor_x, cursor_y - box_h - 2},
  };

  int box_x = clamp_box_x(cursor_x + 2);
  int box_y = clamp_box_y(cursor_y + 2);
  for (const auto &cand : candidates) {
    int cx = clamp_box_x(cand.first);
    int cy = clamp_box_y(cand.second);
    if (!border_hits_cursor(cx, cy)) {
      box_x = cx;
      box_y = cy;
      break;
    }
  }

  UIRect rect = {box_x, box_y, box_w, box_h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  UIRect border_rect = {box_x - 1, box_y - 1, box_w + 2, box_h + 2};
  ui->draw_border(border_rect, theme.fg_panel_border, theme.bg_command);

  int label_w = std::clamp(longest_label, 12, std::max(12, box_w * 55 / 100));
  int meta_x = box_x + 2 + label_w + 1;
  int meta_w = std::max(0, box_w - (meta_x - box_x) - 1);

  for (int row = 0; row < max_items; row++) {
    int item_idx = start_idx + row;
    if (item_idx < 0 || item_idx >= (int)lsp_completion_items.size()) {
      break;
    }

    const auto &item = lsp_completion_items[item_idx];
    bool selected_row = (item_idx == selected);
    int fg = selected_row ? theme.fg_selection : theme.fg_command;
    int bg = selected_row ? theme.bg_selection : theme.bg_command;
    std::string icon = completion_kind_icon(item.kind, use_nerd_icons);
    std::string label = clip_text(icon + item.label, label_w);
    std::string meta = completion_kind_name(item.kind);
    if (item.deprecated) {
      meta += " deprecated";
    }
    std::string detail =
        !item.detail.empty() ? one_line_text(item.detail)
                             : one_line_text(item.documentation);
    if (!detail.empty()) {
      meta += "  " + detail;
    }
    meta = clip_text(meta, meta_w);

    UIRect row_rect = {box_x, box_y + row, box_w, 1};
    ui->fill_rect(row_rect, " ", fg, bg);
    ui->draw_text(box_x + 1, box_y + row, label, fg, bg, selected_row);
    if (meta_w > 0) {
      ui->draw_text(meta_x, box_y + row, meta,
                    selected_row ? theme.fg_selection : theme.fg_comment, bg,
                    selected_row);
    }
  }

  UIRect footer_rect = {box_x, box_y + max_items, box_w, 1};
  ui->fill_rect(footer_rect, " ", theme.fg_comment, theme.bg_command);
  std::string footer = std::to_string(std::clamp(selected + 1, 1,
                                                (int)lsp_completion_items.size())) +
                       "/" + std::to_string(lsp_completion_items.size());
  if (!lsp_completion_prefix.empty()) {
    footer += "  " + lsp_completion_prefix;
  }
  if ((int)lsp_completion_all_items.size() > (int)lsp_completion_items.size()) {
    footer += "  filtered";
  }
  ui->draw_text(box_x + 1, box_y + max_items,
                clip_text(footer, std::max(0, box_w - 2)), theme.fg_comment,
                theme.bg_command);
}

void Editor::render_telescope() {
  TelescopeLayout layout =
      telescope_layout_for(*this, ui, tab_height, status_height, show_sidebar,
                           show_sidebar ? effective_sidebar_width() : 0);
  if (!layout.valid)
    return;

  const bool use_nerd_icons = config.get_bool("lsp_completion_nerd_icons", true);

  UIRect rect = {layout.x, layout.y, layout.w, layout.h};
  ui->fill_rect(rect, " ", theme.fg_telescope, theme.bg_telescope);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_telescope);

  const auto &results = telescope.get_results();
  int selected = telescope.get_selected_index();
  int result_count = telescope.get_result_count();
  telescope.ensure_selected_visible(layout.list_h);

  std::string title = " Find Files ";
  std::string count = std::to_string(result_count) + " match";
  if (result_count != 1) {
    count += "es";
  }
  if (!results.empty()) {
    count += "  " + std::to_string(std::clamp(selected + 1, 1, result_count)) +
             "/" + std::to_string(result_count);
  }
  std::string root = telescope.get_root_dir();
  if ((int)root.length() > layout.inner_w - 2) {
    root = clip_path_left(root, layout.inner_w - 2);
  }
  ui->draw_text(layout.inner_x + 1, layout.y, title, theme.fg_telescope,
                theme.bg_telescope, true);
  ui->draw_text(std::max(layout.inner_x + 1,
                         layout.x + layout.w - (int)count.size() - 2),
                layout.y, count, theme.fg_comment, theme.bg_telescope);
  ui->draw_text(layout.inner_x + 1, layout.inner_y,
                clip_text(root, layout.inner_w - 2),
                theme.fg_comment, theme.bg_telescope);

  std::string query = "  > " + telescope.get_query();
  if (telescope.get_query().empty()) {
    query += "type to filter files";
  }
  UIRect query_rect = {layout.query_x, layout.query_y, layout.query_w, 1};
  if (layout.inner_h >= 3) {
    ui->fill_rect(query_rect, " ", theme.fg_telescope, theme.bg_command);
    ui->draw_text(layout.query_x, layout.query_y,
                  clip_text(query, layout.query_w),
                  theme.fg_telescope, theme.bg_command, true);
  }

  if (layout.show_preview) {
    int separator_x = layout.preview_x - 2;
    for (int row = layout.body_y; row < layout.body_y + layout.body_h; row++) {
      ui->draw_text(separator_x, row, "│", theme.fg_panel_border,
                    theme.bg_telescope);
    }
    ui->draw_text(layout.preview_x, layout.preview_y, "Preview",
                  theme.fg_telescope, theme.bg_telescope, true);
  }

  int start_idx = telescope.get_list_scroll_offset();
  int end_idx = std::min((int)results.size(), start_idx + layout.list_h);

  if (results.empty()) {
    std::string empty = telescope.get_query().empty()
                            ? "No files found in this workspace."
                            : "No files match the current query.";
    ui->draw_text(layout.list_x,
                  layout.list_y + std::max(0, layout.list_h / 2),
                  clip_text(empty, layout.list_w),
                  theme.fg_comment, theme.bg_telescope);
  }

  for (int i = start_idx; i < end_idx; i++) {
    int row_y = layout.list_y + (i - start_idx);
    int fg = theme.fg_telescope, bg = theme.bg_telescope;

    if (i == selected) {
      fg = theme.fg_telescope_selected;
      bg = theme.bg_telescope_selected;
      UIRect row_rect = {layout.list_x - 1, row_y, layout.list_w + 1, 1};
      ui->fill_rect(row_rect, " ", fg, bg);
    }

    std::string icon = telescope_icon(results[i].is_directory, use_nerd_icons);
    std::string name = results[i].name;
    std::string parent =
        results[i].parent_path == "." ? "" : "  " + results[i].parent_path;
    int parent_w = std::min((int)parent.size(), std::max(0, layout.list_w / 2));
    int name_w = std::max(1, layout.list_w - (int)icon.size() - parent_w);
    std::string row = icon + clip_text(name, name_w);
    if (parent_w > 0) {
      row += clip_path_left(parent, parent_w);
    }
    ui->draw_text(layout.list_x, row_y, clip_text(row, layout.list_w), fg, bg,
                  i == selected);
  }

  if (layout.show_preview) {
    int preview_inner_w = layout.preview_w;
    if (!results.empty() && selected >= 0 && selected < (int)results.size()) {
      auto preview = telescope.get_selected_preview();
      int preview_scroll = telescope.get_preview_scroll_offset();
      std::string title_line = clip_path_left(preview.title, preview_inner_w);
      ui->draw_text(layout.preview_x, layout.preview_y + 1, title_line,
                    theme.fg_telescope_preview, theme.bg_telescope_preview,
                    true);
      if (!preview.detail.empty() && layout.body_h > 2) {
        ui->draw_text(layout.preview_x, layout.preview_y + 2,
                      clip_text(preview.detail, preview_inner_w),
                      theme.fg_comment, theme.bg_telescope_preview);
      }

      int line_start_y = layout.preview_y + 4;
      int preview_lines_h =
          std::max(0, layout.preview_y + layout.preview_h - line_start_y);
      SyntaxHighlighter preview_highlighter;
      preview_highlighter.set_language(
          std::filesystem::path(telescope.get_selected_path())
              .extension()
              .string());
      for (int i = 0; i < preview_lines_h &&
                      preview_scroll + i < (int)preview.lines.size();
           i++) {
        int line_idx = preview_scroll + i;
        std::string line = preview.lines[line_idx];
        if (preview.is_directory || preview.skipped) {
          ui->draw_text(layout.preview_x, line_start_y + i,
                        clip_text(line, preview_inner_w), theme.fg_comment,
                        theme.bg_telescope_preview);
        } else {
          char line_no[16];
          std::snprintf(line_no, sizeof(line_no), "%3d ", line_idx + 1);
          int text_w = std::max(1, preview_inner_w - 4);
          ui->draw_text(layout.preview_x, line_start_y + i, line_no,
                        theme.fg_comment, theme.bg_telescope_preview);
          std::string clipped = clip_text(line, text_w);
          auto colors = preview_highlighter.get_colors(clipped);
          int chunk_start = 0;
          int chunk_token = 0;
          for (int c = 0; c <= (int)clipped.size(); c++) {
            int token = 0;
            if (c < (int)colors.size() && colors[c].first == 1) {
              token = colors[c].second;
            }
            if (c == 0) {
              chunk_token = token;
            }
            if (c == (int)clipped.size() || token != chunk_token) {
              if (c > chunk_start) {
                ui->draw_text(layout.preview_x + 4 + chunk_start,
                              line_start_y + i,
                              clipped.substr(chunk_start, c - chunk_start),
                              syntax_preview_color(theme, chunk_token),
                              theme.bg_telescope_preview);
              }
              chunk_start = c;
              chunk_token = token;
            }
          }
        }
      }
    } else {
      ui->draw_text(layout.preview_x, layout.preview_y + 2,
                    "Select a file to preview.",
                    theme.fg_comment, theme.bg_telescope);
    }
  }

  std::string footer = results.empty() ? "No selection"
                                       : telescope.get_selected_relative_path();
  if (!results.empty() && layout.show_preview) {
    auto preview = telescope.get_selected_preview();
    if (!preview.detail.empty()) {
      footer += "  " + preview.detail;
    }
  }
  ui->draw_text(layout.inner_x + 1, layout.footer_y,
                clip_text(footer, layout.inner_w - 2), theme.fg_comment,
                theme.bg_telescope);
}

void Editor::render_image_viewer() {
  int w = ui->get_render_width();
  int h = ui->get_height();
  if (w < 4 || h <= status_height + tab_height)
    return;

  int area_x = show_sidebar ? effective_sidebar_width() : 0;
  int area_y = tab_height;
  int area_w = std::max(1, w - area_x);
  int area_h = std::max(1, h - status_height - tab_height);

  UIRect clear_area = {area_x, area_y, area_w, area_h};
  ui->fill_rect(clear_area, " ", theme.fg_default, theme.bg_default);

  int img_w = std::clamp(area_w * 3 / 4, std::min(area_w, 40), area_w);
  int img_h = std::clamp(area_h * 3 / 4, std::min(area_h, 12), area_h);
  int img_x = area_x + std::max(0, (area_w - img_w) / 2);
  int img_y = area_y + std::max(0, (area_h - img_h) / 2);

  image_viewer.render(img_x, img_y, img_w, img_h,
                      theme.fg_image_border, theme.bg_image_border);

  int x = image_viewer.get_view_x();
  int y = image_viewer.get_view_y();
  int vw = image_viewer.get_view_w();
  int vh = image_viewer.get_view_h();
  if (vw <= 2 || vh <= 2) {
    return;
  }

  UIRect panel = {x, y, vw, vh};
  ui->fill_rect(panel, " ", theme.fg_default, theme.bg_image_border);
  ui->draw_border(panel, image_viewer.get_border_fg(), image_viewer.get_border_bg());

  std::string status = image_viewer.get_status_text();
  if (!status.empty()) {
    ui->draw_text(x + 2, y, " " + status + " ", theme.fg_comment,
                  theme.bg_default);
  }

  if (image_viewer.uses_real_graphics()) {
    return;
  }

  const auto &lines = image_viewer.get_preview_lines();
  int text_x = x + 1;
  int text_y = y + 1;
  int text_w = std::max(1, vw - 2);
  int text_h = std::max(1, vh - 2);

  int line_y = text_y;
  for (int i = 0; i < text_h && i < (int)lines.size(); i++) {
    std::string line = lines[i];
    if ((int)line.size() > text_w) {
      line = line.substr(0, text_w);
    }
    ui->draw_text(text_x, line_y++, line, theme.fg_default, theme.bg_default);
  }

  if (image_viewer.has_color_preview_data() && line_y < text_y + text_h) {
    line_y += 1;
    const auto &pixels = image_viewer.get_color_preview_bg();
    int max_rows = std::max(0, text_y + text_h - line_y);
    int rows = std::min((int)pixels.size(), max_rows);
    for (int py = 0; py < rows; py++) {
      int cols = std::min((int)pixels[py].size(), text_w);
      for (int px = 0; px < cols; px++) {
        int bg = pixels[py][px];
        ui->draw_text(text_x + px, line_y + py, " ", theme.fg_default, bg);
      }
    }
  }
}
