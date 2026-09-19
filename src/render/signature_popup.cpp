// The LSP signature-help popup overlay.
#include "bracket.h"
#include "column_utils.h"
#include "editor.h"
#include "folding.h"
#include "jot/lua/api.h"
#include "render/overlay_internal.h"
#include "ui/text.h"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>

using namespace overlay_internal;

void Editor::render_lsp_signature()
{
  if (!lsp_signature_visible || lsp_signature_result.signatures.empty() || panes.empty())
  {
    return;
  }

  auto &pane = get_pane();
  auto &buf = get_buffer(pane.buffer_id);
  if (buf.filepath != lsp_signature_filepath)
  {
    hide_lsp_signature();
    return;
  }

  // The popup tracks the call that asked for it: the caret must still sit past
  // the recorded '(' on the same line. Any edit that closed or moved the paren
  // dismisses it here, so no key handler needs to know about every possible
  // way the call could disappear.
  if (buf.cursor.y != lsp_signature_open_paren_line || buf.cursor.y < 0
      || buf.cursor.y >= (int)buf.line_count())
  {
    hide_lsp_signature();
    return;
  }
  const std::string &paren_line = buf.line(buf.cursor.y);
  if (lsp_signature_open_paren_col >= (int)paren_line.size()
      || paren_line[(size_t)lsp_signature_open_paren_col] != '('
      || buf.cursor.x <= lsp_signature_open_paren_col)
  {
    hide_lsp_signature();
    return;
  }

  const int sig_idx = std::clamp(lsp_signature_result.active_signature, 0,
                                 (int)lsp_signature_result.signatures.size() - 1);
  const LSPSignature &sig = lsp_signature_result.signatures[(size_t)sig_idx];

  // Pick the parameter documentation when the signature has none of its own.
  std::string doc = sig.documentation;
  int hl_start = -1;
  int hl_len = -1;
  if (sig.active_parameter >= 0
      && sig.active_parameter < (int)sig.parameters.size())
  {
    const LSPSignatureParameter &param = sig.parameters[(size_t)sig.active_parameter];
    if (doc.empty())
    {
      doc = param.documentation;
    }
    if (param.label_start >= 0 && param.label_end > param.label_start
        && param.label_end <= (int)sig.label.size())
    {
      hl_start = param.label_start;
      hl_len = param.label_end - param.label_start;
    }
    else if (!param.label.empty())
    {
      const size_t found = sig.label.find(param.label);
      if (found != std::string::npos)
      {
        hl_start = (int)found;
        hl_len = (int)param.label.size();
      }
    }
  }

  // Compose the popup rows: label first, then the (capped) documentation.
  std::vector<SignatureLineView> lines;
  SignatureLineView label_line;
  label_line.role = 0;
  label_line.text = sig.label;
  lines.push_back(std::move(label_line));
  std::string remaining_doc = doc;
  int doc_rows = 0;
  while (!remaining_doc.empty() && doc_rows < 6 && lines.size() < 8)
  {
    size_t nl = remaining_doc.find('\n');
    std::string row_text =
        nl == std::string::npos ? remaining_doc : remaining_doc.substr(0, nl);
    remaining_doc = nl == std::string::npos ? "" : remaining_doc.substr(nl + 1);
    if (!row_text.empty() && row_text.back() == '\r')
    {
      row_text.pop_back();
    }
    size_t fence_start = 0;
    while (fence_start < row_text.size()
           && (row_text[fence_start] == ' ' || row_text[fence_start] == '\t'))
    {
      fence_start++;
    }
    if (row_text.compare(fence_start, 3, "```") == 0)
    {
      continue; // markdown code fences are visual noise in a compact popup
    }
    SignatureLineView doc_line;
    doc_line.role = 1;
    doc_line.text = row_text;
    lines.push_back(std::move(doc_line));
    doc_rows++;
  }
  const int signature_total = (int)lsp_signature_result.signatures.size();
  bool wants_footer = signature_total > 1 || sig.active_parameter >= 0;
  if (wants_footer)
  {
    SignatureLineView footer_line;
    footer_line.role = 2;
    if (signature_total > 1)
    {
      footer_line.text = "overload " + std::to_string(sig_idx + 1) + "/"
                         + std::to_string(signature_total);
    }
    else
    {
      footer_line.text = "parameter "
                         + std::to_string(std::max(0, sig.active_parameter) + 1) + "/"
                         + std::to_string((int)sig.parameters.size());
    }
    lines.push_back(std::move(footer_line));
  }

  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20)
  {
    draw_w = std::max(1, draw_w - minimap_width);
  }
  const int line_num_width = 7;
  int visible_h = std::max(1, pane.h - tab_height);
  int visible_w = std::max(12, draw_w - 2 - line_num_width);

  int widest = 0;
  for (const auto &ln : lines)
  {
    widest = std::max(widest, (int)ui_cell_count(ln.text));
  }
  const int box_w = std::clamp(widest + 2, 24, std::min(visible_w, 96));
  const int content_cap = std::max(1, box_w - 2);
  const int box_h = std::max(1, (int)lines.size());

  // Anchor above the caret; fall back to the top of the pane when the popup
  // would overflow (a completion box usually occupies the space below).
  int safe_cursor_y = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
  const std::string &line = buf.line(safe_cursor_y);
  int cursor_visual = compute_visual_column(line, buf.cursor.x, tab_size);
  int scroll_visual = compute_visual_column(line, buf.scroll_x, tab_size);
  int cursor_x =
      pane.x + 1 + line_num_width + (cursor_visual - scroll_visual)
      + lsp_inlay_hint_cells_before(buf.filepath, buf.cursor.y, buf.cursor.x, line);
  const int viewport_h = std::max(1, pane.h - tab_height);
  const auto fold_view = Folding::view_of(buf.fold_ranges);
  const int cursor_row = std::max(0,
                                  fold_view->visible_row_for_line(buf.scroll_offset,
                                                                 buf.cursor.y,
                                                                 viewport_h,
                                                                 (int)buf.line_count()));
  int cursor_y = pane.y + tab_height + cursor_row;

  int min_x = pane.x + 1 + line_num_width;
  int max_x = pane.x + draw_w - box_w - 1;
  if (max_x < min_x)
  {
    max_x = min_x;
  }
  int min_y = pane.y + tab_height;
  int box_x = std::clamp(cursor_x - 2, min_x, max_x);
  int box_y = cursor_y - box_h - 2;
  if (box_y < min_y)
  {
    box_y = min_y;
  }
  int max_y = pane.y + visible_h - box_h;
  if (box_y > max_y)
  {
    box_y = std::max(min_y, max_y);
  }

  // Truncate every row to the content width so nothing bleeds past the
  // border, then clamp the parameter highlight to the label text that is
  // actually shown (offsets stay valid: clip only ever cuts a suffix).
  for (auto &ln : lines)
  {
    if ((int)ui_cell_count(ln.text) > content_cap)
    {
      ln.text = clip_text(ln.text, content_cap);
    }
  }
  if (hl_start >= 0)
  {
    const int label_len = (int)lines[0].text.size();
    if (hl_start >= label_len)
    {
      hl_start = -1;
      hl_len = -1;
    }
    else
    {
      hl_len = std::min(hl_len, label_len - hl_start);
    }
  }

  // A registered Lua UI handler paints the popup from this state (label +
  // highlighted parameter + documentation); the native draw is the fallback.
  if (lua_api && lua_api->has_lua_ui_handler("lsp_signature"))
  {
    SignatureView view;
    view.x = box_x;
    view.y = box_y;
    view.w = box_w;
    view.h = box_h;
    view.active_signature = sig_idx;
    view.signature_total = signature_total;
    view.label_hl_start = hl_start;
    view.label_hl_len = hl_len;
    view.lines = std::move(lines);
    if (lua_api->emit_lsp_signature(view))
    {
      return;
    }
  }

  UIRect rect = {box_x, box_y, box_w, box_h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  UIRect border_rect = {box_x - 1, box_y - 1, box_w + 2, box_h + 2};
  ui->draw_border(border_rect, theme.fg_panel_border, theme.bg_command);
  for (int i = 0; i < (int)lines.size() && i < box_h; i++)
  {
    const int fg = lines[i].role == 0 ? theme.fg_command : theme.fg_comment;
    const std::string text = clip_text(lines[i].text, std::max(0, box_w - 2));
    ui->draw_text(box_x + 1, box_y + i, text, fg, theme.bg_command, false);
    if (lines[i].role == 0 && i == 0 && hl_start >= 0 && hl_len > 0
        && hl_start < (int)text.size())
    {
      const int kept = std::min(hl_len, (int)text.size() - hl_start);
      const std::string hl = text.substr((size_t)hl_start, (size_t)kept);
      if (!hl.empty())
      {
        ui->draw_text(box_x + 1 + (int)ui_cell_count(text.substr(0, (size_t)hl_start)),
                      box_y + i,
                      hl,
                      theme.fg_selection,
                      theme.bg_selection,
                      true);
      }
    }
  }
}
