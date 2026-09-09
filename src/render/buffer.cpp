// Buffer content rendering: the main viewport paint pass plus the small
// indent-column and fold-row helpers it uses. Bracket and diagnostic helpers
// live in bracket.cpp and diagnostics.cpp.
#include "blank_guides.h"
#include "column_utils.h"
#include "editor.h"
#include "folding.h"
#include "render/buffer_internal.h"
#include "tree_sitter/manager.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

using namespace buffer_internal;

namespace buffer_internal
{
  int leading_indent_visual_column(const std::string &line, int tab_size)
  {
    int indent_end = 0;
    while (indent_end < (int)line.size() && (line[indent_end] == ' ' || line[indent_end] == '\t'))
    {
      indent_end++;
    }
    return compute_visual_column(line, indent_end, tab_size);
  }
} // namespace buffer_internal

namespace
{
  int visible_row_for_line(const std::vector<FoldRange> &ranges,
                           int first_line,
                           int target_line,
                           int viewport_h,
                           int line_count)
  {
    for (int row = 0; row < viewport_h; row++)
    {
      int line = Folding::buffer_line_for_visible_offset(ranges, first_line, row, line_count);
      if (line >= 0 && line == target_line && !Folding::is_line_hidden(ranges, line))
      {
        return row;
      }
    }
    return -1;
  }
} // namespace

void Editor::render_buffer_content(const SplitPane &pane, int buffer_id)
{
  auto &buf = get_buffer(buffer_id);
  int x = pane.x;
  int y = pane.y + tab_height;
  int w = std::max(1, pane.w);
  int h = std::max(0, pane.h - tab_height - 1);
  if (h <= 0)
  {
    return;
  }
  // Over a selection-colored cell (extra caret, active selection) that
  // renders a block the same color as the selection background -- invisible
  // on dark themes. The cursor cell is therefore painted with the default
  // pair so the caret reads as a normal high-contrast block while parked on
  // a caret/selection.
  std::string cursor_style_raw = config.get("cursor_style", "bar");
  std::transform(
      cursor_style_raw.begin(), cursor_style_raw.end(), cursor_style_raw.begin(), ::tolower);
  const bool block_cursor = cursor_style_raw == "block" || cursor_style_raw == "steady_block"
                            || cursor_style_raw == "steadyblock";
  const bool inlay_hints_enabled = config.get_bool("lsp_inlay_hints", true);
  const bool inlay_type_hints_enabled = config.get_bool("lsp_inlay_type_hints", true);

  UIRect pane_rect = {x, y, w, h};
  ui->fill_rect(pane_rect, " ", theme.fg_default, theme.bg_default);

  if (buf.is_lazy())
  {
    int center = buf.scroll_offset + h / 2;
    if (center >= 0 && center < (int)buf.line_count())
    {
      buf.scroll_hint(center);
    }
  }

  if (show_minimap && w > 20)
    w = std::max(1, w - minimap_width);
  if (w > 3)
    w = std::max(1, w - 1);

  if (buf.is_placeholder && !buf.modified && buf.filepath.empty() && buf.line_count() == 1
      && buf.line(0).empty())
  {
    std::string prompt = "Open or create empty file to start editing.";
    const int max_prompt_w = std::max(1, w - 4);
    if ((int)prompt.size() > max_prompt_w)
    {
      prompt = max_prompt_w > 3 ? prompt.substr(0, max_prompt_w - 3) + "..."
                                : prompt.substr(0, max_prompt_w);
    }
    const int prompt_x = x + std::max(0, (w - (int)prompt.size()) / 2);
    const int prompt_y = y + h / 2;
    ui->draw_text(prompt_x, prompt_y, prompt, theme.fg_comment, theme.bg_default);
    return;
  }

  int line_num_width = 8;
  ActiveBracketGuide bracket_guide = build_active_bracket_guide(buf, tab_size);

  refresh_folds(buf);
  // Clamp before any depth math: scroll_offset can arrive stale (left over
  // from a previous cursor/scroll state for a smaller viewport or folded
  // region), and the seed below must describe the rows that are actually
  // about to be drawn. Seeding from the unclamped value painted the whole
  // viewport at the wrong absolute depth until the next frame.
  buf.scroll_offset =
      Folding::clamp_scroll_offset(buf.fold_ranges, buf.scroll_offset, h, (int)buf.line_count());

  // Re-anchor decorations through the pending edit (if any) once, before any
  // row reads their positions. Between edits this is a flag check only.
  if (buf.decoration_dirty)
  {
    ensure_decorations_anchored(buf);
  }

  // Seed the rainbow depth at the viewport top. In-memory buffers use the
  // absolute per-line depth prefix (stable under scrolling); lazy buffers
  // keep the bounded backscan so the cache never demand-loads the whole
  // file.
  int bracket_depth = 0;
  if (!buf.is_lazy())
  {
    bracket_depth = bracket_depth_at_line_start(buf, buf.scroll_offset);
  }
  else
  {
    const int scan_start = std::max(0, buf.scroll_offset - kBracketDepthScanLimitLines);
    // Also cap the backscan by bytes: for files made of giant single lines
    // this would otherwise walk megabytes every frame just to seed bracket
    // depth.
    constexpr std::size_t kBracketDepthScanMaxBytes = 1u << 20;
    std::size_t scanned_bytes = 0;
    for (int scan_line = scan_start;
         scan_line < std::min(buf.scroll_offset, (int)buf.line_count()); scan_line++)
    {
      const std::string &scan_line_text = buf.line(scan_line);
      if (scanned_bytes + scan_line_text.size() > kBracketDepthScanMaxBytes)
      {
        break;
      }
      scanned_bytes += scan_line_text.size();
      for (char c : scan_line_text)
      {
        apply_bracket_depth_delta(c, bracket_depth);
      }
    }
  }
  std::vector<int> visual_cols;

  int prev_line_idx = buf.scroll_offset - 1;
  for (int i = 0; i < h; i++)
  {
    int line_idx = Folding::buffer_line_for_visible_offset(
        buf.fold_ranges, buf.scroll_offset, i, (int)buf.line_count());
    // The per-row walk normally carries depth across consecutive visible
    // lines (row N+1 = row N + 1 exactly when nothing is folded). Whenever
    // that continuity breaks -- folded/hidden lines collapse a range, or a
    // stale scroll_offset snapped the viewport -- re-anchor the depth to
    // the absolute prefix value for the line actually being drawn, so a
    // bracket's color is always a pure function of its file position.
    if (!buf.is_lazy() && line_idx >= 0 && line_idx < (int)buf.line_count()
        && line_idx != prev_line_idx + 1)
    {
      bracket_depth = bracket_depth_at_line_start(buf, line_idx);
    }
    prev_line_idx = line_idx;
    int draw_y = y + i;

    if (line_idx >= 0 && line_idx < (int)buf.line_count()
        && !Folding::is_line_hidden(buf.fold_ranges, line_idx))
    {
      // The cursor row tints only the line-number gutter so the active row
      // reads at a glance without washing out the code itself. The code area
      // keeps the plain pane background -- selection, search hits, syntax
      // colors and decorations are never fought by a row tint.
      const bool row_is_cursor_line =
          highlight_cursor_line && line_idx == buf.cursor.y && pane.active;
      const int gutter_bg = row_is_cursor_line ? theme.bg_cursor_line : theme.bg_default;
      // Paint the whole gutter band (fold column, number and number-right
      // spacing) in one pass so breakpoint/severity markers and the number
      // drawn next sit on a seamless tint.
      if (row_is_cursor_line)
      {
        int gutter_end = std::min(x + 1 + line_num_width, x + w); // exclusive
        for (int fill_c = x + 1; fill_c < gutter_end; fill_c++)
        {
          ui->draw_text(fill_c, draw_y, " ", theme.fg_default, gutter_bg);
        }
      }

      int line_diag_severity = line_diagnostic_severity(buf, line_idx);
      int diag_fg = line_diag_severity > 0 ? diagnostic_severity_color(theme, line_diag_severity)
                                           : theme.fg_line_num;
      if (!buf.filepath.empty() && has_debugger_breakpoint(buf.filepath, line_idx))
      {
        ui->draw_text(x + 1, draw_y, "●", theme.fg_status_error, gutter_bg, true);
      }
      else if (!buf.filepath.empty() && is_debugger_breakpoint_hover(pane.buffer_id, line_idx))
      {
        ui->draw_text(x + 1, draw_y, "●", theme.fg_comment, gutter_bg);
      }
      else if (line_diag_severity > 0)
      {
        // VSCode-like gutter accent: a solid color block instead of W/E glyphs.
        ui->draw_text(x + 1, draw_y, " ", diag_fg, diag_fg, true);
      }
      else
      {
        ui->draw_text(x + 1, draw_y, " ", theme.fg_line_num, gutter_bg);
      }

      char num_buf[16];
      int display_line_number = line_idx + 1;
      if (relative_line_numbers && line_idx != buf.cursor.y)
      {
        display_line_number = std::abs(line_idx - buf.cursor.y);
      }
      snprintf(num_buf, sizeof(num_buf), "%4d ", display_line_number);
      int ln_bg = row_is_cursor_line ? theme.bg_cursor_line : theme.bg_line_num;
      int ln_fg = theme.fg_line_num;
      if (line_idx == buf.cursor.y)
      {
        ln_fg = theme.fg_cursor_line_num;
      }
      else if (line_diag_severity > 0)
      {
        ln_fg = diag_fg;
      }
      ui->draw_text(x + 3, draw_y, num_buf, ln_fg, ln_bg);
      int fold_index = -1;
      bool folded_header = Folding::is_line_folded_header(buf.fold_ranges, line_idx, &fold_index);
      bool foldable_header =
          folded_header || Folding::fold_starting_at_line(buf.fold_ranges, line_idx) >= 0;
      if (foldable_header)
      {
        ui->draw_text(x + 2, draw_y, folded_header ? "▸" : "▾", theme.fg_comment, gutter_bg);
      }

      const std::string &line = buf.line(line_idx);
      int scroll_x = ui_clamp_to_utf8_boundary(line, buf.scroll_x);
      int current_x = x + 1 + line_num_width;
      int visible_len = w - 2 - line_num_width;
      if (folded_header)
      {
        int hidden_count = Folding::hidden_line_count_for_header(buf.fold_ranges, line_idx);
        std::string suffix = "  … " + std::to_string(hidden_count) + " lines";
        int suffix_x = current_x + std::max(0, visible_len - (int)suffix.size());
        if (suffix_x > current_x)
        {
          ui->draw_text(suffix_x, draw_y, suffix, theme.fg_comment, theme.bg_default);
          visible_len = std::max(0, suffix_x - current_x - 1);
        }
      }
      int clamped_scroll_x =
          ui_clamp_to_utf8_boundary(line, std::clamp(scroll_x, 0, (int)line.size()));
      // Only the visible window (plus a wide-glyph margin) needs per-frame
      // work. Walking entire multi-KB single lines (minified/generated code)
      // on every row of every frame made scrolling freeze.
      const int render_limit =
          std::min((int)line.size(), clamped_scroll_x + (visible_len + 2) * 4 + 8);
      visual_cols = build_visual_columns(line, tab_size, render_limit);
      int start_visual = visual_cols[clamped_scroll_x];
      int leading_ws_end = 0;
      while (leading_ws_end < (int)line.length()
             && (line[leading_ws_end] == ' ' || line[leading_ws_end] == '\t'))
      {
        leading_ws_end++;
      }

      // Inlay hints on this row (parameter-name virtual text on existing
      // code, clangd-style). Each hint inserts label cells before its byte
      // column, shifting the rest of the line right. `visual_col` is the
      // already-shifted screen column; the byte/visual helpers below answer
      // "how many hint cells sit before position p" for the glyph walk and
      // the selection/guide overlays.
      struct RowHint
      {
        int byte_col = 0;
        int visual_col = 0;
        int width = 0;
      };
      std::vector<RowHint> row_hints;
      if (inlay_hints_enabled && !folded_header)
      {
        // Stale hints stay drawn while a refresh is in flight; only a
        // missing cache (no server answer yet) suppresses them.
        auto cache_it = lsp_inlay_hint_caches.find(buf.filepath);
        if (cache_it != lsp_inlay_hint_caches.end())
        {
          int inserted_cells = 0;
          for (const auto &hint : cache_it->second.hints)
          {
            const bool is_param = hint.kind == 2;
            const bool is_type = hint.kind == 1;
            if (hint.line != line_idx || (!is_param && !is_type)
                || (is_type && !inlay_type_hints_enabled) || hint.label.empty()
                || hint.character < 0 || hint.character > (int)line.size())
            {
              continue;
            }
            RowHint rh;
            rh.byte_col = hint.character;
            rh.visual_col = compute_visual_column(line, hint.character, tab_size) + inserted_cells;
            std::string label = hint.label;
            if (hint.padding_left)
            {
              label = " " + label;
            }
            if (hint.padding_right)
            {
              label += " ";
            }
            rh.width = ui_cell_count(label);
            // Draw the label at its (shifted) column, truncated to the pane.
            const int label_vis = rh.visual_col - start_visual;
            if (label_vis >= 0 && label_vis < visible_len && rh.width > 0)
            {
              const std::string txt = ui_truncate_cells(label, visible_len - label_vis);
              if (!txt.empty())
              {
                ui->draw_text(current_x + label_vis,
                              draw_y,
                              txt,
                              theme.fg_comment,
                              theme.bg_default,
                              false,
                              true); // italic, like an editor's dimmed hints
              }
            }
            row_hints.push_back(std::move(rh));
            inserted_cells += rh.width;
          }
        }
      }
      // Hint cells inserted before byte column `b` (glyph at `b` starts
      // after them, so hints AT the column count too).
      auto hint_cells_at_byte = [&](int b)
      {
        int n = 0;
        for (const auto &rh : row_hints)
        {
          if (rh.byte_col <= b)
          {
            n += rh.width;
          }
          else
          {
            break;
          }
        }
        return n;
      };
      // Hint cells whose shifted column lies at or before visual `v` (for
      // visual-positioned overlays like the bracket guide).
      auto hint_cells_before_visual = [&](int v)
      {
        int n = 0;
        for (const auto &rh : row_hints)
        {
          if (rh.visual_col <= v)
          {
            n += rh.width;
          }
          else
          {
            break;
          }
        }
        return n;
      };
      auto active_guide_on_row = [&]()
      {
        return bracket_guide.active && line_idx > bracket_guide.start_line
               && line_idx < bracket_guide.end_line;
      };

      // Decorations on this row, found by binary search over the buffer's
      // (row, col, priority, id)-sorted vector. Rows without decorations pay
      // a single branch per frame; rows with them pay one scan per visible
      // character (bounded by the row's decoration count).
      int row_deco_lo = 0;
      int row_deco_hi = 0;
      {
        auto it = std::lower_bound(buf.decorations.begin(),
                                   buf.decorations.end(),
                                   line_idx,
                                   [](const Decoration &d, int row)
                                   {
                                     return d.row < row;
                                   });
        row_deco_lo = (int)(it - buf.decorations.begin());
        row_deco_hi = row_deco_lo;
        while (row_deco_hi < (int)buf.decorations.size()
               && buf.decorations[row_deco_hi].row == line_idx)
        {
          row_deco_hi++;
        }
      }
      // Walk cursor over the row's decoration slice: advances past spans that
      // end as the character walk moves right, so each decoration is examined
      // at most once per row.
      size_t deco_cursor = (size_t)row_deco_lo;

      auto is_in_selection = [&](int char_idx)
      {
        Cursor p = {char_idx, line_idx};
        auto in_range = [&](const Selection &sel)
        {
          if (!sel.active)
            return false;
          Cursor s = sel.start;
          Cursor e = sel.end;
          if (s.y > e.y || (s.y == e.y && s.x > e.x))
            std::swap(s, e);
          if (p.y > s.y && p.y < e.y)
            return true;
          if (p.y == s.y && p.y == e.y)
            return (p.x >= s.x && p.x < e.x);
          if (p.y == s.y)
            return (p.x >= s.x);
          if (p.y == e.y)
            return (p.x < e.x);
          return false;
        };
        if (in_range(buf.selection))
          return true;
        for (const auto &c : buf.extra_carets)
        {
          if (in_range(c))
            return true;
        }
        return false;
      };

      struct SelectionRowSpan
      {
        bool active = false;
        bool full_line = false;
        int start = 0;
        int end = 0;
      };
      auto selection_row_span = [&]()
      {
        SelectionRowSpan span;
        auto span_for = [&](const Selection &sel, SelectionRowSpan &out) {
          if (!sel.active)
            return false;
          Cursor s = sel.start;
          Cursor e = sel.end;
          if (s.y > e.y || (s.y == e.y && s.x > e.x))
            std::swap(s, e);
          if (line_idx < s.y || line_idx > e.y)
            return false;
          int start_x = 0;
          int end_x = (int)line.size();
          if (line_idx == s.y)
            start_x = std::clamp(s.x, 0, (int)line.size());
          if (line_idx == e.y)
            end_x = std::clamp(e.x, 0, (int)line.size());
          if (start_x > end_x)
            std::swap(start_x, end_x);
          out.start = out.active ? std::min(out.start, start_x) : start_x;
          out.end = out.active ? std::max(out.end, end_x) : end_x;
          out.active = true;
          return true;
        };
        span_for(buf.selection, span);
        for (const auto &c : buf.extra_carets)
          span_for(c, span);
        if (!span.active)
          return span;
        span.full_line = (span.start == 0 && span.end == (int)line.size());
        return span;
      };

      if (scroll_x < (int)line.length())
      {
        // Colors only matter up to the visible window; huge single lines are
        // highlighted per-window instead of per-line.
        const auto &colors = get_line_syntax_colors(buf, line_idx, render_limit);
        int line_bracket_depth = bracket_depth;
        std::vector<Editor::SearchMatch> search_hits;
        Editor::SearchMatch active_search_match{-1, -1, 0};
        if (show_search && !search_query.empty())
        {
          auto it = std::lower_bound(
              search_results.begin(), search_results.end(), Editor::SearchMatch{line_idx, 0, 0});
          while (it != search_results.end() && it->line == line_idx)
          {
            search_hits.push_back(*it);
            ++it;
          }
          if (search_result_index >= 0 && search_result_index < (int)search_results.size()
              && search_results[search_result_index].line == line_idx)
          {
            active_search_match = search_results[search_result_index];
          }
        }
        size_t next_search_hit = 0;

        auto draw_chunk = [&](int start_idx, int len, int color)
        {
          if (len <= 0)
            return;

          const int chunk_end =
              ui_clamp_to_utf8_boundary(line, std::min((int)line.size(), start_idx + len));
          int char_idx =
              ui_clamp_to_utf8_boundary(line, std::clamp(start_idx, 0, (int)line.size()));
          // Hint cells inserted before the chunk's first character (the
          // walk below advances hint_cursor monotonically, so each chunk
          // seeds its own offset and no hint is counted twice).
          int hint_offset = 0;
          size_t hint_cursor = 0;
          while (hint_cursor < row_hints.size() && row_hints[hint_cursor].byte_col <= char_idx)
          {
            hint_offset += row_hints[hint_cursor].width;
            hint_cursor++;
          }

          while (char_idx < chunk_end)
          {
            while (hint_cursor < row_hints.size() && row_hints[hint_cursor].byte_col <= char_idx)
            {
              hint_offset += row_hints[hint_cursor].width;
              hint_cursor++;
            }
            if (char_idx < 0 || char_idx >= (int)line.size())
              break;
            int next_idx = ui_next_grapheme_boundary(line, char_idx);
            if (next_idx <= char_idx)
              next_idx = char_idx + 1;

            char c = line[char_idx];
            const bool tokenized = char_idx < (int)colors.size() && colors[char_idx].first == 1;
            const int token_type = tokenized ? colors[char_idx].second : 0;
            const bool skip_bracket_logic =
                (token_type == TS_TOKEN_STRING || token_type == TS_TOKEN_COMMENT);
            int bracket_color = -1;
            if (!skip_bracket_logic && is_open_bracket(c))
            {
              bracket_color = rainbow_bracket_color(theme, line_bracket_depth);
              line_bracket_depth++;
            }
            else if (!skip_bracket_logic && is_close_bracket(c))
            {
              line_bracket_depth = std::max(0, line_bracket_depth - 1);
              bracket_color = rainbow_bracket_color(theme, line_bracket_depth);
            }

            if (char_idx < scroll_x)
            {
              char_idx = next_idx;
              continue;
            }
            int vis_idx = visual_cols[char_idx] - start_visual;
            if (vis_idx + hint_offset >= visible_len)
              break;
            int char_w = std::max(1, visual_cols[next_idx] - visual_cols[char_idx]);

            int fg = color;
            int bg = theme.bg_default;

            bool in_sel = is_in_selection(char_idx);
            if (in_sel)
            {
              bg = theme.bg_selection;
              fg = theme.fg_selection;
              // See block_cursor above: keep the block visible on the
              // main-cursor cell instead of blending into the selection.
              if (block_cursor && pane.active && line_idx == buf.cursor.y
                  && char_idx == buf.cursor.x)
              {
                bg = theme.bg_default;
                fg = theme.fg_default;
              }
            }

            if (!search_hits.empty())
            {
              while (next_search_hit < search_hits.size()
                     && char_idx
                            >= search_hits[next_search_hit].col + search_hits[next_search_hit].len)
              {
                next_search_hit++;
              }
              if (next_search_hit < search_hits.size()
                  && char_idx >= search_hits[next_search_hit].col
                  && char_idx < search_hits[next_search_hit].col + search_hits[next_search_hit].len)
              {
                const bool is_active_match =
                    search_hits[next_search_hit].line == active_search_match.line
                    && search_hits[next_search_hit].col == active_search_match.col;
                if (is_active_match)
                {
                  // The match the cursor will jump to reads as the "current
                  // target": bright/contrasting instead of the plain
                  // search-hit tint so it never blends with the rest.
                  bg = theme.bg_search_current;
                  fg = theme.fg_search_current;
                }
                else
                {
                  bg = theme.bg_search_match;
                  fg = theme.fg_search_match;
                }
              }
            }

            bool in_leading_indent = char_idx < leading_ws_end;
            if (in_leading_indent)
            {
              int guide_fg = in_sel ? theme.fg_selection : theme.fg_line_num;
              int char_visual = visual_cols[char_idx];
              for (int fill = 0; fill < char_w && vis_idx + hint_offset + fill < visible_len; fill++)
              {
                int cell_visual = char_visual + fill;
                const bool active_guide =
                    active_guide_on_row() && cell_visual == bracket_guide.visual_column;
                int cell_guide_fg = (show_indent_guides && active_guide && !in_sel)
                                        ? theme.fg_bracket_match
                                        : guide_fg;
                std::string guide = " ";
                if (show_indent_guides
                    && (active_guide || (tab_size > 0 && cell_visual % tab_size == 0)))
                {
                  guide = "│";
                }
                ui->draw_text(current_x + vis_idx + hint_offset + fill,
                              draw_y,
                              guide,
                              cell_guide_fg,
                              bg);
              }
              char_idx = next_idx;
              continue;
            }

            if (bracket_color != -1 && !in_sel
                && !(next_search_hit < search_hits.size()
                     && char_idx >= search_hits[next_search_hit].col
                     && char_idx
                            < search_hits[next_search_hit].col + search_hits[next_search_hit].len))
            {
              fg = bracket_color;
            }

            // Anchored decoration overlay: spans draw over syntax and brackets
            // but under selection and search matches. Among the spans covering
            // this character the highest priority wins; deco_cursor skips spans
            // that already ended as the walk moved right.
            int deco_fg = -1;
            int deco_bg = -1;
            int deco_underline = 0;
            int deco_underline_fg = -1;
            if (row_deco_lo < row_deco_hi)
            {
              int best = -1;
              for (size_t di = deco_cursor; di < (size_t)row_deco_hi
                                            && buf.decorations[di].col <= char_idx;
                   di++)
              {
                const Decoration &d = buf.decorations[di];
                if (d.width > 0 && char_idx < d.col + d.width)
                {
                  if (d.priority > best)
                  {
                    best = d.priority;
                    deco_fg = d.fg;
                    deco_bg = d.bg;
                    if (!d.hl.empty())
                    {
                      theme_group_color(d.hl, deco_fg, deco_bg);
                    }
                    deco_underline = d.underline;
                    deco_underline_fg = d.underline_fg;
                    if (d.underline != 0 && deco_underline_fg == -1 && !d.underline_hl.empty())
                    {
                      int ufg = -1;
                      int ubg = -1;
                      theme_group_color(d.underline_hl, ufg, ubg);
                      deco_underline_fg = ufg;
                    }
                    if (deco_underline != 0 && deco_underline_fg == -1 && d.fg != -1)
                    {
                      deco_underline_fg = d.fg;
                    }
                  }
                }
                else if (char_idx >= d.col + d.width)
                {
                  deco_cursor = di + 1;
                }
              }
            }
            if ((deco_fg != -1 || deco_bg != -1) && !in_sel)
            {
              if (deco_fg != -1)
              {
                fg = deco_fg;
              }
              if (deco_bg != -1)
              {
                bg = deco_bg;
              }
            }
            // VSCode-style Ctrl+hover goto-definition underline: straight
            // underline in the function color over the tracked token. Applies
            // under decorations (they win when present) but over plain
            // syntax, and never inside an active selection.
            int ctrl_underline = 0;
            int ctrl_underline_fg = -1;
            if (ctrl_hover_active && ctrl_hover_buffer == buffer_id && !in_sel && pane.active
                && line_idx == ctrl_hover_line && char_idx >= ctrl_hover_start
                && char_idx < ctrl_hover_end)
            {
              ctrl_underline = 1;
              ctrl_underline_fg = theme.fg_function;
            }
            if (ctrl_underline != 0)
            {
              if (deco_underline == 0)
              {
                deco_underline = ctrl_underline;
                if (deco_underline_fg == -1)
                {
                  deco_underline_fg = ctrl_underline_fg;
                }
              }
            }

            if (c == '\t')
            {
              for (int fill = 0; fill < char_w && vis_idx + hint_offset + fill < visible_len; fill++)
              {
                ui->draw_text(current_x + vis_idx + hint_offset + fill,
                              draw_y,
                              " ",
                              fg,
                              bg,
                              false,
                              false,
                              deco_underline,
                              deco_underline_fg);
              }
            }
            else
            {
              ui->draw_text(current_x + vis_idx + hint_offset,
                            draw_y,
                            line.substr(char_idx, next_idx - char_idx),
                            fg,
                            bg,
                            false,
                            false,
                            deco_underline,
                            deco_underline_fg);
            }
            char_idx = next_idx;
          }
        };

        if (colors.empty())
        {
          draw_chunk(0, (int)line.length(), theme.fg_default);
        }
        else
        {
          int chunk_start = 0;
          int last_type = -1;
          int last_token = 0;

          // Color runs only matter up to the visible window, so stop chunk
          // detection at render_limit instead of walking the whole line.
          for (int i = 0; i <= render_limit;)
          {
            int current_type = -1;
            int current_token = 0;

            if (i < render_limit && i < (int)colors.size())
            {
              current_token = colors[i].first;
              current_type = colors[i].second;
            }

            bool changed =
                (current_token != last_token) || (current_token == 1 && current_type != last_type);

            if (i > 0 && (changed || i >= render_limit))
            {
              int len = i - chunk_start;
              int color = theme.fg_default;

              if (last_token == 1)
              {
                switch (last_type)
                {
                case TS_TOKEN_KEYWORD:
                  color = theme.fg_keyword;
                  break;
                case TS_TOKEN_STRING:
                  color = theme.fg_string;
                  break;
                case TS_TOKEN_COMMENT:
                  color = theme.fg_comment;
                  break;
                case TS_TOKEN_NUMBER:
                  color = theme.fg_number;
                  break;
                case TS_TOKEN_TYPE:
                  color = theme.fg_type;
                  break;
                case TS_TOKEN_FUNCTION:
                  color = theme.fg_function;
                  break;
                case TS_TOKEN_VARIABLE:
                  color = theme.fg_variable;
                  break;
                case TS_TOKEN_CONSTANT:
                  color = theme.fg_constant;
                  break;
                case TS_TOKEN_BUILTIN:
                  color = theme.fg_builtin;
                  break;
                case TS_TOKEN_OPERATOR:
                  color = theme.fg_operator;
                  break;
                case TS_TOKEN_PUNCTUATION:
                  color = theme.fg_punctuation;
                  break;
                case TS_TOKEN_TAG:
                  color = theme.fg_tag;
                  break;
                case TS_TOKEN_ATTRIBUTE:
                  color = theme.fg_attribute;
                  break;
                case TS_TOKEN_NAMESPACE:
                  color = theme.fg_namespace;
                  break;
                case TS_TOKEN_MODULE:
                  color = theme.fg_module;
                  break;
                case TS_TOKEN_PARAMETER:
                  color = theme.fg_parameter;
                  break;
                case TS_TOKEN_FIELD:
                  color = theme.fg_field;
                  break;
                case TS_TOKEN_KEYWORD_CONTROL:
                  color = theme.fg_keyword_control;
                  break;
                case TS_TOKEN_KEYWORD_STORAGE:
                  color = theme.fg_keyword_storage;
                  break;
                case TS_TOKEN_KEYWORD_PREPROC:
                  color = theme.fg_keyword_preproc;
                  break;
                case TS_TOKEN_FUNCTION_METHOD:
                  color = theme.fg_function_method;
                  break;
                case TS_TOKEN_FUNCTION_CONSTRUCTOR:
                  color = theme.fg_function_constructor;
                  break;
                case TS_TOKEN_TYPE_BUILTIN:
                  color = theme.fg_type_builtin;
                  break;
                case TS_TOKEN_CONSTANT_MACRO:
                  color = theme.fg_constant_macro;
                  break;
                case TS_TOKEN_STRING_ESCAPE:
                  color = theme.fg_string_escape;
                  break;
                case TS_TOKEN_PUNCTUATION_BRACKET:
                  color = theme.fg_punctuation_bracket;
                  break;
                case TS_TOKEN_PUNCTUATION_DELIMITER:
                  color = theme.fg_punctuation_delimiter;
                  break;
                default:
                  color = theme.fg_default;
                  break;
                }
              }

              draw_chunk(chunk_start, len, color);
              chunk_start = i;
            }

            last_token = current_token;
            last_type = current_type;
            if (i >= render_limit)
              break;
            int next_i = ui_next_grapheme_boundary(line, i);
            i = next_i > i ? next_i : i + 1;
          }
        }

        // End-of-line virtual text from anchored decorations (inline
        // diagnostics, etc.). Drawn after the content so it hugs the line's
        // text end; the first decoration with virtual text wins (highest
        // priority in the row's sort order). Folded headers keep their
        // "… N lines" suffix instead.
        if (row_deco_lo < row_deco_hi && !folded_header)
        {
          for (int di = row_deco_lo; di < row_deco_hi; di++)
          {
            const Decoration &d = buf.decorations[di];
            if (d.virt_text.empty())
            {
              continue;
            }
            int vfg = d.virt_fg;
            int vbg = d.virt_bg;
            if (!d.virt_hl.empty())
            {
              theme_group_color(d.virt_hl, vfg, vbg);
            }
            if (vfg == -1)
            {
              vfg = theme.fg_comment;
            }
            if (vbg == -1)
            {
              vbg = theme.bg_default;
            }
            int line_vis_end = visible_len;
            if ((int)line.size() < (int)visual_cols.size())
            {
              line_vis_end = std::max(0, visual_cols[line.size()] - start_visual);
            }
            line_vis_end += hint_cells_at_byte((int)line.size());
            int avail = visible_len - line_vis_end;
            if (avail <= 0)
            {
              break;
            }
            std::string txt = ui_truncate_cells(d.virt_text, avail);
            if (txt.empty())
            {
              break;
            }
            ui->draw_text(current_x + line_vis_end, draw_y, txt, vfg, vbg);
            break;
          }
        }
        bracket_depth = line_bracket_depth;
      }
      else
      {
        for (char c : line)
        {
          apply_bracket_depth_delta(c, bracket_depth);
        }
      }

      auto selected_span = selection_row_span();
      for (const auto &caret : buf.extra_carets)
      {
        if (!caret.active && caret.start.y == line_idx)
        {
          SelectionRowSpan bare;
          bare.active = true;
          bare.full_line = false;
          bare.start = std::clamp(caret.start.x, 0, (int)line.size());
          bare.end = bare.start;
          if (!selected_span.active)
          {
            selected_span = bare;
          }
        }
      }
      if (selected_span.active)
      {
        // Selection spans are byte ranges; hints shift everything at or past
        // each end, so the highlight follows the shifted text.
        const int sel_start_offset = hint_cells_at_byte(selected_span.start);
        const int sel_end_offset = hint_cells_at_byte(selected_span.end);
        int selected_start_visual =
            compute_visual_column(line, selected_span.start, tab_size) + sel_start_offset;
        int selected_end_visual =
            compute_visual_column(line, selected_span.end, tab_size) + sel_end_offset;
        int tail_start = selected_span.full_line ? std::max(selected_end_visual, start_visual)
                                                 : std::max(selected_start_visual, start_visual);
        bool select_empty_cell =
            selected_span.start == selected_span.end && !selected_span.full_line;
        if (select_empty_cell)
        {
          tail_start = std::max(selected_start_visual, start_visual);
          selected_end_visual = std::max(selected_end_visual + 1, tail_start + 1);
        }
        else if (selected_span.full_line)
        {
          // A row selected end-to-end highlights its code, not the whole
          // window: cap the fill at the end of the line text (like the
          // cursor-row tint) instead of running it to the right edge. Lines
          // longer than the viewport still cover the full width.
          int line_end_cell = visible_len;
          if ((int)line.size() < (int)visual_cols.size())
          {
            line_end_cell = std::max(0, visual_cols[line.size()] - start_visual);
          }
          line_end_cell += hint_cells_at_byte((int)line.size());
          selected_end_visual = std::max(tail_start, std::min(line_end_cell, visible_len));
        }
        if (!selected_span.full_line && !select_empty_cell)
        {
          selected_end_visual = std::max(selected_end_visual, tail_start);
        }
        int tail_cells = selected_end_visual - tail_start;
        if (tail_cells > 0)
        {
          int tail_x = current_x + (tail_start - start_visual);
          int max_tail = std::max(0, visible_len - (tail_start - start_visual));
          int draw_cells = std::min(tail_cells, max_tail);
          // The tail repaint only fixes the BACKGROUND of the selected
          // range: the characters were already painted with selection colors
          // by the per-character walk above (is_in_selection). Repainting
          // the glyph as well would draw a blank " " over the code and hide
          // it behind a solid highlight box.
          //
          // The main-cursor cell keeps the default colors so the block
          // cursor stays visible on a bare caret (see block_cursor above).
          const int cursor_cell_visual =
              (line_idx == buf.cursor.y)
                  ? compute_visual_column(line, buf.cursor.x, tab_size)
                        + hint_cells_at_byte(buf.cursor.x) - start_visual
                                             : -1;
          // Extra carets blink in software (they are painted cells, not
          // terminal cursors): during the hidden half of the phase the
          // point-caret highlight drops back to the normal text colors.
          std::vector<int> point_caret_visuals;
          if (!blink_visible)
          {
            for (const auto &caret : buf.extra_carets)
            {
              if (!caret.active && caret.end.y == line_idx)
              {
                point_caret_visuals.push_back(
                    compute_visual_column(line, caret.end.x, tab_size)
                    + hint_cells_at_byte(caret.end.x) - start_visual);
              }
            }
          }
          for (int fill = 0; fill < draw_cells; fill++)
          {
            const int cell_visual = (tail_start - start_visual) + fill;
            const bool cursor_cell = block_cursor && pane.active
                                     && cell_visual == cursor_cell_visual;
            const bool blink_hidden =
                std::find(point_caret_visuals.begin(), point_caret_visuals.end(), cell_visual)
                != point_caret_visuals.end();
            if (cursor_cell || blink_hidden)
            {
              ui->draw_text(tail_x + fill,
                            draw_y,
                            " ",
                            theme.fg_default,
                            theme.bg_default);
            }
            else
            {
              // Preserve the glyph painted by the character walk; only
              // ensure the selection background on this cell. draw_text
              // with the cell's own character keeps fg/bold/underline from
              // the syntax pass and just swaps in the selection bg.
              const UICell *existing = ui->cell_at(tail_x + fill, draw_y);
              const std::string glyph =
                  (existing && !existing->ch.empty()) ? existing->ch : " ";
              const int glyph_fg = existing ? existing->fg : theme.fg_selection;
              ui->draw_text(tail_x + fill,
                            draw_y,
                            glyph,
                            glyph_fg,
                            theme.bg_selection,
                            existing ? existing->bold : false,
                            existing ? existing->italic : false,
                            existing ? existing->underline : 0,
                            existing ? existing->underline_fg : -1);
            }
          }
        }
      }

      if (show_indent_guides && active_guide_on_row() && leading_ws_end == (int)line.size())
      {
        int guide_vis_idx =
            bracket_guide.visual_column - start_visual
            + hint_cells_before_visual(bracket_guide.visual_column);
        if (guide_vis_idx >= 0 && guide_vis_idx < visible_len)
        {
          ui->draw_text(
              current_x + guide_vis_idx, draw_y, "│", theme.fg_bracket_match, theme.bg_default);
        }
      }

      // Blank lines inherit the next non-blank line's indent guides
      // (indent-blankline's blankline rule) so the guide column stays a
      // continuous vertical line across empty rows. Trailing blanks at
      // EOF draw nothing, and a blank run just before a closer (`}`, `)`,
      // `]`, `end`) keeps the previous indent level instead. The same
      // tab-stop rule as the character walk; the bracket-match column is
      // skipped since the pass above already painted it.
      if (show_indent_guides && tab_size > 0 && line.empty())
      {
        const int src_idx = blank_guides::guide_source_line(
            (int)buf.line_count(), [&](int i) -> const std::string & { return buf.line(i); },
            line_idx);
        if (src_idx >= 0)
        {
          const std::string &src_line = buf.line(src_idx);
          const int src_ws_end = blank_guides::leading_ws(src_line);
          if (src_ws_end > 0)
          {
            const std::vector<int> src_visual =
                build_visual_columns(src_line, tab_size, src_ws_end);
            for (int col = start_visual; col < src_visual[src_ws_end]; col++)
            {
              if (col % tab_size != 0)
              {
                continue;
              }
              if (active_guide_on_row() && col == bracket_guide.visual_column)
              {
                continue;
              }
              const int vis_idx = col - start_visual;
              if (vis_idx >= visible_len)
              {
                break;
              }
              ui->draw_text(
                  current_x + vis_idx, draw_y, "│", theme.fg_line_num, theme.bg_default);
            }
          }
        }
      }
    }
    else
    {
      ui->draw_text(x + 1, draw_y, "~", theme.fg_line_num, theme.bg_default);
    }
  }

  if (pane.active)
  {
    int cursor_visible_row = visible_row_for_line(
        buf.fold_ranges, buf.scroll_offset, buf.cursor.y, h, (int)buf.line_count());
    // The bundled Lua feature lua/features/decorations.lua renders inline
    // diagnostics as anchored decorations (spans + end-of-line virtual text)
    // while this config key is enabled; the native cursor-line popup would
    // duplicate it, so it is suppressed then.
    const Diagnostic *active_diag =
        !config.get_bool("decorations_inline_diagnostics", true)
            ? find_line_diagnostic(buf, buf.cursor.y, buf.cursor.x)
            : nullptr;
    if (active_diag && diagnostic_covers_line(*active_diag, buf.cursor.y)
        && cursor_visible_row >= 0)
    {
      const int code_start_x = x + 1 + line_num_width;
      const int code_end_x = x + w - 2;

      int anchor_col = buf.cursor.x;
      if (buf.cursor.y < active_diag->line)
      {
        anchor_col = active_diag->col;
      }
      else if (buf.cursor.y > active_diag->end_line)
      {
        anchor_col = active_diag->end_col;
      }
      else if (buf.cursor.y == active_diag->line && buf.cursor.y == active_diag->end_line)
      {
        anchor_col = std::clamp(buf.cursor.x, active_diag->col, active_diag->end_col);
      }
      else if (buf.cursor.y == active_diag->line)
      {
        anchor_col = std::max(buf.cursor.x, active_diag->col);
      }
      else if (buf.cursor.y == active_diag->end_line)
      {
        anchor_col = std::min(buf.cursor.x, active_diag->end_col);
      }

      int cursor_line = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
      const std::string &anchor_line = buf.line(cursor_line);
      int anchor_visual = compute_visual_column(anchor_line, anchor_col, tab_size);
      int scroll_visual = compute_visual_column(anchor_line, buf.scroll_x, tab_size);
      int anchor_x = code_start_x + (anchor_visual - scroll_visual);
      anchor_x = std::clamp(anchor_x, code_start_x, code_end_x);
      int anchor_y = y + cursor_visible_row;
      int line_end_visual = compute_visual_column(anchor_line, (int)anchor_line.size(), tab_size);
      int line_end_x = code_start_x + (line_end_visual - scroll_visual);
      // Draw diagnostics only in trailing whitespace area, never over code.
      int inline_x = std::max(anchor_x + 2, line_end_x + 1);
      int inline_y = std::max(y, std::min(anchor_y, y + h - 1));
      if (inline_x <= code_end_x)
      {
        int available = code_end_x - inline_x + 1;
        if (available >= 8)
        {
          std::string msg = compact_diagnostic_text(active_diag->message);
          std::string inline_text = diagnostic_severity_label(active_diag->severity) + ": " + msg;
          if ((int)inline_text.length() > available)
          {
            if (available > 3)
            {
              inline_text = inline_text.substr(0, (size_t)(available - 3)) + "...";
            }
            else
            {
              inline_text = inline_text.substr(0, (size_t)available);
            }
          }
          ui->draw_text(inline_x, inline_y, inline_text, theme.fg_comment, theme.bg_default);
        }      }
    }
  }
 }
