#include "editor.h"
#include "core/keybind_catalog.h"
#include "lua_bridge/api.h"
#include "tree_sitter/manager.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <tuple>
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
  };

  struct PaletteLayout
  {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
  };

  // Shared geometry for the command palette panel, so the renderer and the
  // terminal-cursor placement always agree on where the input row is.
  PaletteLayout command_palette_layout(int screen_w, int screen_h, size_t result_count)
  {
    const int max_items = std::min(8, (int)result_count);
    int w = std::min(std::max(64, screen_w - 12), 116);
    int h = 5 + max_items;
    if (screen_w < 66)
    {
      w = std::max(36, screen_w - 2);
    }
    h = std::clamp(h, 7, std::max(7, screen_h - 4));
    return {std::max(0, (screen_w - w) / 2), std::max(1, (screen_h - h) / 3), w, h};
  }

  struct TreeSitterStatusRenderRow
  {
    std::string section;
    std::string language;
    std::string detail;
    int color = 7;
  };

  std::string trim_hover_fence_language(std::string lang)
  {
    size_t start = 0;
    while (start < lang.size() && std::isspace((unsigned char)lang[start]))
    {
      start++;
    }
    size_t end = start;
    while (end < lang.size() && !std::isspace((unsigned char)lang[end]))
    {
      end++;
    }
    lang = lang.substr(start, end - start);
    std::transform(lang.begin(),
                   lang.end(),
                   lang.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return lang;
  }

  bool hover_markdown_fence_language(const std::string &line, std::string *language)
  {
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
      start++;
    }
    if (line.compare(start, 3, "```") != 0)
    {
      return false;
    }
    if (language)
    {
      *language = trim_hover_fence_language(line.substr(start + 3));
    }
    return true;
  }

  std::string hover_language_extension(const std::string &lang)
  {
    if (lang == "c++" || lang == "cpp" || lang == "cc" || lang == "cxx")
    {
      return ".cpp";
    }
    if (lang == "c")
    {
      return ".c";
    }
    if (lang == "python" || lang == "py")
    {
      return ".py";
    }
    if (lang == "javascript" || lang == "js")
    {
      return ".js";
    }
    if (lang == "jsx")
    {
      return ".jsx";
    }
    if (lang == "typescript" || lang == "ts")
    {
      return ".ts";
    }
    if (lang == "tsx")
    {
      return ".tsx";
    }
    if (lang == "rust" || lang == "rs")
    {
      return ".rs";
    }
    if (lang == "go" || lang == "golang")
    {
      return ".go";
    }
    if (lang == "bash" || lang == "sh" || lang == "shell" || lang == "zsh")
    {
      return ".sh";
    }
    if (lang == "json")
    {
      return ".json";
    }
    if (lang == "html")
    {
      return ".html";
    }
    if (lang == "css")
    {
      return ".css";
    }
    if (lang == "xml")
    {
      return ".xml";
    }
    if (lang == "yaml" || lang == "yml")
    {
      return ".yaml";
    }
    if (lang == "toml")
    {
      return ".toml";
    }
    if (lang == "markdown" || lang == "md")
    {
      return ".md";
    }
    if (lang == "cmake")
    {
      return ".cmake";
    }
    if (lang == "make" || lang == "makefile")
    {
      return ".make";
    }
    if (lang == "dockerfile")
    {
      return ".dockerfile";
    }
    return "";
  }

  int hover_syntax_color(const Theme &theme, int token)
  {
    switch (token)
    {
    case TS_TOKEN_KEYWORD:
      return theme.fg_keyword;
    case TS_TOKEN_STRING:
      return theme.fg_string;
    case TS_TOKEN_COMMENT:
      return theme.fg_comment;
    case TS_TOKEN_NUMBER:
      return theme.fg_number;
    case TS_TOKEN_TYPE:
      return theme.fg_type;
    case TS_TOKEN_FUNCTION:
      return theme.fg_function;
    case TS_TOKEN_VARIABLE:
      return theme.fg_variable;
    case TS_TOKEN_CONSTANT:
      return theme.fg_constant;
    case TS_TOKEN_BUILTIN:
      return theme.fg_builtin;
    case TS_TOKEN_OPERATOR:
      return theme.fg_operator;
    case TS_TOKEN_PUNCTUATION:
      return theme.fg_punctuation;
    case TS_TOKEN_TAG:
      return theme.fg_tag;
    case TS_TOKEN_ATTRIBUTE:
      return theme.fg_attribute;
    case TS_TOKEN_NAMESPACE:
      return theme.fg_namespace;
    case TS_TOKEN_MODULE:
      return theme.fg_module;
    case TS_TOKEN_PARAMETER:
      return theme.fg_parameter;
    case TS_TOKEN_FIELD:
      return theme.fg_field;
    case TS_TOKEN_KEYWORD_CONTROL:
      return theme.fg_keyword_control;
    case TS_TOKEN_KEYWORD_STORAGE:
      return theme.fg_keyword_storage;
    case TS_TOKEN_KEYWORD_PREPROC:
      return theme.fg_keyword_preproc;
    case TS_TOKEN_FUNCTION_METHOD:
      return theme.fg_function_method;
    case TS_TOKEN_FUNCTION_CONSTRUCTOR:
      return theme.fg_function_constructor;
    case TS_TOKEN_TYPE_BUILTIN:
      return theme.fg_type_builtin;
    case TS_TOKEN_CONSTANT_MACRO:
      return theme.fg_constant_macro;
    case TS_TOKEN_STRING_ESCAPE:
      return theme.fg_string_escape;
    case TS_TOKEN_PUNCTUATION_BRACKET:
      return theme.fg_punctuation_bracket;
    case TS_TOKEN_PUNCTUATION_DELIMITER:
      return theme.fg_punctuation_delimiter;
    default:
      return theme.fg_command;
    }
  }

  void draw_hover_code_line(UI *ui,
                            int x,
                            int y,
                            int w,
                            const std::string &line,
                            const std::string &extension,
                            const Theme &theme)
  {
    if (w <= 0)
    {
      return;
    }
    std::string clipped = ui_truncate_cells(line, w);
    if (extension.empty())
    {
      ui->draw_text(x, y, clipped, theme.fg_command, theme.bg_command);
      return;
    }

    SyntaxHighlighter highlighter;
    highlighter.set_language(extension);
    auto colors = highlighter.get_colors(clipped);
    int chunk_start = 0;
    int chunk_token = 0;

    for (int i = 0; i <= (int)clipped.size(); i++)
    {
      int token = 0;
      if (i < (int)colors.size() && colors[i].first == 1)
      {
        token = colors[i].second;
      }
      if (i == 0)
      {
        chunk_token = token;
      }
      if (i == (int)clipped.size() || token != chunk_token)
      {
        if (i > chunk_start)
        {
          ui->draw_text(x + chunk_start,
                        y,
                        clipped.substr(chunk_start, i - chunk_start),
                        hover_syntax_color(theme, chunk_token),
                        theme.bg_command);
        }
        chunk_start = i;
        chunk_token = token;
      }
    }
  }

  int status_layout_width(const std::vector<StatusSegment> &segments)
  {
    if (segments.empty())
      return 0;
    int width = 0;
    for (const auto &segment : segments)
    {
      width += ui_cell_count(segment.text);
    }
    width += std::max(0, (int)segments.size() - 1);
    return width;
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
      std::string text = ui_take_cells(segment.text, remaining);
      ui->draw_text(pos, y, text, segment.fg, segment.bg, segment.bold);
      pos += ui_cell_count(text);

      if (pos < end && i + 1 < segments.size())
      {
        ui->draw_text(pos, y, "", segment.bg, segments[i + 1].bg, true);
        pos++;
      }
    }
    return pos;
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

  std::string ts_display_name(const std::string &language)
  {
    std::string out = language;
    std::replace(out.begin(), out.end(), '_', '-');
    return out;
  }

  void
  ts_add_section(std::vector<TreeSitterStatusRenderRow> &rows, const std::string &title, int count)
  {
    rows.push_back({title, "", std::to_string(count), 8});
  }

  // Draw `text`, rendering characters whose byte offsets appear in `match`
  // (sorted, ascending) in `match_fg` + bold and the rest in `fg` (bold only
  // when `plain_bold`). Splits only on UTF-8 character boundaries and advances
  // the pen by cell width, so wide glyphs keep column alignment.
  void ui_draw_marked_text(UI &ui,
                           int x,
                           int y,
                           const std::string &text,
                           const std::vector<int> &match,
                           int fg,
                           int bg,
                           int match_fg,
                           bool plain_bold = false)
  {
    if (match.empty())
    {
      ui.draw_text(x, y, text, fg, bg, plain_bold);
      return;
    }
    std::vector<char> hit(text.size(), 0);
    for (int m : match)
    {
      if (m >= 0 && m < (int)text.size())
      {
        hit[(size_t)m] = 1;
      }
    }
    int cx = x;
    int i = 0;
    while (i < (int)text.size())
    {
      int len = std::max(1, ui_utf8_char_len(text, i));
      bool is_hit = hit[(size_t)i] != 0;
      int run = i + len;
      while (run < (int)text.size())
      {
        int l = std::max(1, ui_utf8_char_len(text, run));
        if ((hit[(size_t)run] != 0) != is_hit)
        {
          break;
        }
        run += l;
      }
      std::string seg = text.substr((size_t)i, (size_t)(run - i));
      ui.draw_text(cx, y, seg, is_hit ? match_fg : fg, bg, is_hit || plain_bold);
      cx += ui_cell_count(seg);
      i = run;
    }
  }
} // namespace

void Editor::render_tree_sitter_status_modal()
{
  if (!show_tree_sitter_status_modal)
  {
    return;
  }

  std::set<std::string> active;
  std::set<std::string> installing;
  for (auto &buf : buffers)
  {
    if (buf.syntax_engine == SYNTAX_ENGINE_UNKNOWN && !buf.filepath.empty() && buf.line_count() > 0)
    {
      int line_idx = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
      get_line_syntax_colors(buf, line_idx);
    }
    if (buf.syntax_engine == SYNTAX_ENGINE_TREESITTER)
    {
#ifdef JOT_TREESITTER
      if (!buf.ts_language_id.empty())
      {
        active.insert(buf.ts_language_id);
      }
      else
#endif
          if (!buf.syntax_language_label.empty())
      {
        active.insert(buf.syntax_language_label);
      }
    }
  }

  std::vector<TreeSitterStatusRenderRow> installing_rows;
  for (const auto &job : tree_sitter_install_jobs)
  {
    if (job.running)
    {
      installing.insert(job.language);
      installing_rows.push_back({"",
                                 ts_display_name(job.language),
                                 job.progress.empty() ? "running" : job.progress,
                                 theme.fg_status_warning});
    }
    else if (job.failed)
    {
      installing_rows.push_back({"",
                                 ts_display_name(job.language),
                                 job.progress.empty() ? "failed" : job.progress,
                                 theme.fg_status_error});
    }
  }

  std::vector<TreeSitterStatusRenderRow> active_rows;
  std::vector<TreeSitterStatusRenderRow> installed_rows;
  std::vector<TreeSitterStatusRenderRow> uninstalled_rows;

  for (const auto &lang : ts_manager_.language_names())
  {
    if (active.find(lang) != active.end())
    {
      active_rows.push_back(
          {"", ts_display_name(lang), "active in open buffer", theme.fg_status_info});
      continue;
    }
    if (installing.find(lang) != installing.end())
    {
      continue;
    }
#ifdef JOT_TREESITTER
    TreeSitterRuntimeStatus status = ts_manager_.runtime_status_for_language(lang);
    if (status.parser_loaded)
    {
      std::string detail = status.query_loaded ? status.query_message : status.parser_message;
      installed_rows.push_back(
          {"", ts_display_name(lang), detail.empty() ? "parser loaded" : detail, theme.fg_command});
    }
    else
    {
      uninstalled_rows.push_back(
          {"",
           ts_display_name(lang),
           status.parser_message.empty() ? "not installed" : status.parser_message,
           theme.fg_comment});
    }
#else
    uninstalled_rows.push_back(
        {"", ts_display_name(lang), "Tree-sitter runtime not available", theme.fg_comment});
#endif
  }

  std::vector<TreeSitterStatusRenderRow> rows;
  ts_add_section(rows, "Active", (int)active_rows.size());
  rows.insert(rows.end(), active_rows.begin(), active_rows.end());
  ts_add_section(rows, "Installing", (int)installing_rows.size());
  rows.insert(rows.end(), installing_rows.begin(), installing_rows.end());
  ts_add_section(rows, "Installed", (int)installed_rows.size());
  rows.insert(rows.end(), installed_rows.begin(), installed_rows.end());
  ts_add_section(rows, "Uninstalled", (int)uninstalled_rows.size());
  rows.insert(rows.end(), uninstalled_rows.begin(), uninstalled_rows.end());

  int screen_w = ui->get_render_width();
  int screen_h = ui->get_height();
  int w = std::min(std::max(48, screen_w - 8), 92);
  int h = std::min(std::max(12, screen_h - 6), 28);
  if (screen_w < 54)
  {
    w = std::max(20, screen_w - 2);
  }
  if (screen_h < 16)
  {
    h = std::max(8, screen_h - 2);
  }
  int x = std::max(0, (screen_w - w) / 2);
  int y = std::max(1, (screen_h - h) / 2);

  // Modal overlay: dim the editor underneath (same as popup / LSP manager).
  ui->dim_rect({0, 0, screen_w, screen_h});

  // Lua UI handler takes over rendering when registered; geometry is passed
  // unchanged so wheel-scroll and click-outside-to-close keep working.
  if (lua_api && lua_api->has_lua_ui_handler("tree_sitter_status"))
  {
    TsStatusView view;
    view.scroll = tree_sitter_status_scroll;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.rows.reserve(rows.size());
    for (const auto &row : rows)
    {
      TsStatusRowView v;
      v.section = !row.section.empty();
      v.label = v.section ? row.section : row.language;
      v.detail = row.detail;
      v.color = row.color;
      view.rows.push_back(std::move(v));
    }
    if (lua_api->emit_tree_sitter_status(view))
    {
      return;
    }
  }

  // Same panel surface convention as the popup / LSP manager / palette.
  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});
  ui_draw_panel_title(*ui, rect, " Tree-sitter", theme.fg_command, panel_theme.bg_command);

  int list_h = std::max(0, h - 4);
  int max_scroll = std::max(0, (int)rows.size() - list_h);
  tree_sitter_status_scroll = std::clamp(tree_sitter_status_scroll, 0, max_scroll);

  int lang_w = std::max(12, std::min(24, w / 3));
  for (int i = 0; i < list_h; i++)
  {
    int idx = tree_sitter_status_scroll + i;
    if (idx < 0 || idx >= (int)rows.size())
    {
      break;
    }
    const auto &row = rows[idx];
    int row_y = y + 2 + i;
    if (!row.section.empty())
    {
      std::string title = row.section + " (" + row.detail + ")";
      ui->draw_text(x + 1,
                    row_y,
                    ui_truncate_cells(title, w - 2),
                    theme.fg_comment,
                    panel_theme.bg_command,
                    true);
      continue;
    }
    std::string lang = ui_truncate_cells(row.language, lang_w);
    std::string detail = ui_truncate_cells(row.detail, w - lang_w - 5);
    // Language names must stay readable on the flat panel background. The
    // status chip colors (fg_status_info / fg_status_warning) default to 0
    // (black) when a theme does not define them, which is invisible here, so
    // fall back to the panel text color instead of trusting row.color.
    int name_fg = row.color != 0 ? row.color : theme.fg_command;
    ui->draw_text(x + 2, row_y, lang, name_fg, panel_theme.bg_command, true);
    ui->draw_text(x + 2 + lang_w, row_y, detail, theme.fg_comment, panel_theme.bg_command);
  }

  std::string footer = "Esc close  Up/Down scroll";
  if (max_scroll > 0)
  {
    footer +=
        "  " + std::to_string(tree_sitter_status_scroll + 1) + "/" + std::to_string(max_scroll + 1);
  }
  ui_draw_footer(
      *ui, rect, ui_truncate_cells(footer, w - 2), theme.fg_comment, panel_theme.bg_command);
}

void Editor::render_status_line()
{
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
  std::string file_icon = "󰋜";
  bool modified = false;
  if (!show_home_menu && active_buf)
  {
    file_label = status_path_basename(active_buf->filepath, "[No Name]");
    modified = active_buf->modified;
    file_icon = active_buf->filepath.empty() ? "󰈔" : "󰈙";
  }
  left_segments.push_back({" " + file_icon + " " + file_label + (modified ? " +" : "") + " ",
                           theme.fg_status_file,
                           theme.bg_status_file,
                           true,
                           false,
                           100});

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
    std::string git = "  " + ui_truncate_cells(git_branch, 18);
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
    right_segments.push_back({git, theme.fg_status_info, theme.bg_status_info, true, true, 70});
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

  // Message / context row content (used by both the Lua handler and the
  // native fallback so the second status line always matches).
  std::string status_context_label = "  " + status_workspace_label(root_dir);

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
      view.segments.push_back({s.text, s.fg, s.bg, s.bold, s.optional, s.priority, "left"});
    }
    for (const auto &s : right_segments)
    {
      view.segments.push_back({s.text, s.fg, s.bg, s.bold, s.optional, s.priority, "right"});
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

  // Message / context row.
  if (!message.empty())
  {
    status_draw_clipped(ui,
                        content_x,
                        y + 1,
                        content_w,
                        "  " + message,
                        theme.fg_status_message,
                        theme.bg_status,
                        true);
  }
  else
  {
    status_draw_clipped(ui,
                        content_x,
                        y + 1,
                        content_w,
                        ui_truncate_cells(status_context_label, std::max(0, content_w)),
                        theme.fg_status_muted,
                        theme.bg_status);
  }
}

void Editor::render_command_palette()
{
  if (!show_command_palette)
    return;

  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();

  // Modal overlay: dim the editor underneath, matching the popup / LSP
  // manager / telescope treatment, then draw the panel on top.
  ui->dim_rect({0, 0, screen_w, screen_h});

  // Layout mirrors quick-pick: a centered floating panel instead of a
  // bottom-docked strip.
  const PaletteLayout layout =
      command_palette_layout(screen_w, screen_h, command_palette_results.size());
  const int w = layout.w;
  const int h = layout.h;
  const int x = layout.x;
  const int y = layout.y;

  // A registered Lua UI handler (jot.ui.handler("command_palette", fn))
  // renders the whole surface from the state below; the native render is
  // skipped when it returns true.
  if (lua_api && lua_api->has_lua_ui_handler("command_palette"))
  {
    PaletteView view;
    view.query = command_palette_query;
    view.selected = command_palette_selected;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.screen_w = screen_w;
    view.screen_h = screen_h;
    view.results.reserve(command_palette_results.size());
    for (const auto &r : command_palette_results)
    {
      PaletteItemView item;
      item.label = r.label;
      item.category = r.category;
      item.detail = r.detail;
      item.match = r.match;
      view.results.push_back(std::move(item));
    }
    if (lua_api->emit_command_palette(view))
    {
      return;
    }
  }

  // Panel surface uses the theme's panel-background slot (bg_panel_border),
  // the same convention as the popup and LSP manager.
  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});
  ui_draw_panel_title(*ui, rect, " Command Palette", theme.fg_command, panel_theme.bg_command);

  std::string count = std::to_string(command_palette_results.size())
                      + (command_palette_results.size() == 1 ? " result" : " results");
  ui->draw_text(std::max(x + 1, x + w - (int)count.size() - 1),
                y,
                count,
                theme.fg_comment,
                panel_theme.bg_command);

  // Input row.
  int input_y = y + 1;
  UIRect input_rect = {x + 1, input_y, std::max(1, w - 2), 1};
  ui->fill_rect(input_rect, " ", theme.fg_selection, theme.bg_selection);
  std::string query = command_palette_query;
  if (query.empty() || query[0] != ':')
  {
    query = ":" + query;
  }
  ui->draw_text(x + 1,
                input_y,
                ui_truncate_cells(query, w - 3),
                theme.fg_selection,
                theme.bg_selection,
                true);

  // Divider between the input and the list.
  int div_y = y + 2;
  ui->fill_rect(
      {x + 1, div_y, std::max(1, w - 2), 1}, "─", theme.fg_panel_border, panel_theme.bg_command);

  const int max_items = std::min(8, (int)command_palette_results.size());
  int list_y = y + 3;
  if (!command_palette_results.empty())
  {
    int selected = std::clamp(command_palette_selected, 0, (int)command_palette_results.size() - 1);
    int start_idx = std::max(0, selected - max_items + 1);
    if (start_idx + max_items > (int)command_palette_results.size())
    {
      start_idx = std::max(0, (int)command_palette_results.size() - max_items);
    }

    for (int row = 0; row < max_items; row++)
    {
      int idx = start_idx + row;
      if (idx < 0 || idx >= (int)command_palette_results.size())
      {
        break;
      }
      int row_y = list_y + row;
      bool is_selected = (idx == selected);
      int fg = is_selected ? theme.fg_selection : theme.fg_command;
      int bg = is_selected ? theme.bg_selection : panel_theme.bg_command;

      UIRect row_rect = {x + 1, row_y, std::max(1, w - 2), 1};
      ui->fill_rect(row_rect, " ", fg, bg);

      // Accent bar on the selected row.
      if (is_selected)
      {
        ui->draw_text(x + 1, row_y, "▎", theme.fg_selection, bg);
      }

      const auto &suggestion = command_palette_results[idx];
      std::string category = suggestion.category;
      if ((int)category.size() > 12)
      {
        category = category.substr(0, 9) + "...";
      }

      int cat_w = std::min(14, std::max(8, w / 6));
      int detail_w = std::max(0, w / 3);
      int label_w = std::max(8, w - cat_w - detail_w - 5);

      // Truncate the label first, then drop match offsets that fell off the
      // truncated tail so highlighting never points outside the drawn text.
      int label_max = std::max(0, label_w - 2);
      std::string label = ui_truncate_cells(suggestion.label, label_max);
      std::vector<int> match;
      for (int m : suggestion.match)
      {
        if (m >= 0 && m < (int)label.size())
        {
          match.push_back(m);
        }
      }
      std::string detail = suggestion.detail;
      detail = ui_truncate_cells(detail, detail_w);

      int text_x = x + 2 + (is_selected ? 1 : 0);
      // Matched characters pop in the theme accent color (plain rows) or
      // stay bold black-on-cyan (selected rows) while the rest stays plain,
      // so the emphasis is visible in both states.
      int match_fg = is_selected ? theme.fg_selection : theme.fg_keyword;
      ui_draw_marked_text(*ui, text_x, row_y, label, match, fg, bg, match_fg);
      if (w > 40)
      {
        ui->draw_text(
            x + 1 + std::max(0, w - cat_w - detail_w - 2), row_y, category, theme.fg_comment, bg);
        ui->draw_text(x + 1 + std::max(0, w - detail_w - 2), row_y, detail, theme.fg_comment, bg);
      }
    }
  }
  else
  {
    std::string empty = command_palette_query.empty()
                            ? "Type a command or search..."
                            : "No matches for \"" + command_palette_query + "\"";
    ui->draw_text(
        x + 2, list_y, ui_truncate_cells(empty, w - 4), theme.fg_comment, panel_theme.bg_command);
  }

  ui_draw_footer(*ui,
                 rect,
                 "Enter run   Tab complete   Esc close   Up/Down move   "
                 "PgUp/PgDn page",
                 theme.fg_comment,
                 panel_theme.bg_command);
}

void Editor::render_quick_pick()
{
  if (!show_quick_pick)
  {
    return;
  }

  int screen_w = ui->get_render_width();
  int screen_h = ui->get_height();

  // Modal overlay: dim the editor underneath, matching the palette / popup /
  // LSP manager treatment, then draw the panel on top.
  ui->dim_rect({0, 0, screen_w, screen_h});

  int w = std::min(std::max(56, screen_w - 10), 112);
  int h = std::min(std::max(12, screen_h - 8), 26);
  if (screen_w < 62)
  {
    w = std::max(22, screen_w - 2);
  }
  if (screen_h < 16)
  {
    h = std::max(8, screen_h - 2);
  }
  int x = std::max(0, (screen_w - w) / 2);
  int y = std::max(1, (screen_h - h) / 3);

  // Lua UI handler takes over rendering when registered.
  if (lua_api && lua_api->has_lua_ui_handler("quick_pick"))
  {
    QuickPickView view;
    view.title = quick_pick_title;
    view.query = quick_pick_query;
    view.selected = quick_pick_selected;
    view.all_count = (int)quick_pick_all_items.size();
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.items.reserve(quick_pick_items.size());
    for (const auto &item : quick_pick_items)
    {
      QuickPickItemView v;
      v.label = item.label;
      v.detail = item.detail;
      v.preview = item.preview;
      v.severity = item.severity;
      view.items.push_back(std::move(v));
    }
    if (lua_api->emit_quick_pick(view))
    {
      return;
    }
  }

  // Same panel surface convention as the palette / popup / LSP manager.
  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});
  std::string title = quick_pick_title.empty() ? " Quick Pick" : " " + quick_pick_title;
  ui_draw_panel_title(
      *ui, rect, ui_truncate_cells(title, w - 2), theme.fg_command, panel_theme.bg_command);

  std::string count =
      std::to_string(quick_pick_items.size()) + "/" + std::to_string(quick_pick_all_items.size());
  ui->draw_text(std::max(x + 1, x + w - (int)count.size() - 1),
                y,
                count,
                theme.fg_comment,
                panel_theme.bg_command);

  int input_y = y + 1;
  std::string query = "> " + quick_pick_query;
  ui->draw_text(x + 1,
                input_y,
                ui_truncate_cells(query, w - 2),
                theme.fg_selection,
                theme.bg_selection,
                true);

  // Divider between the input and the list (same treatment as the palette).
  ui->fill_rect(
      {x + 1, y + 2, std::max(1, w - 2), 1}, "─", theme.fg_panel_border, panel_theme.bg_command);

  int list_y = y + 3;
  int list_h = std::max(0, h - 5);
  int selected = std::clamp(quick_pick_selected, 0, std::max(0, (int)quick_pick_items.size() - 1));
  int start_idx = std::max(0, selected - list_h + 1);
  if (start_idx + list_h > (int)quick_pick_items.size())
  {
    start_idx = std::max(0, (int)quick_pick_items.size() - list_h);
  }

  if (quick_pick_items.empty())
  {
    std::string empty = quick_pick_query.empty() ? "Type to filter or search"
                                                 : "No matches for \"" + quick_pick_query + "\"";
    ui->draw_text(
        x + 2, list_y, ui_truncate_cells(empty, w - 4), theme.fg_comment, panel_theme.bg_command);
  }

  for (int row = 0; row < list_h; row++)
  {
    int idx = start_idx + row;
    if (idx < 0 || idx >= (int)quick_pick_items.size())
    {
      break;
    }
    const auto &item = quick_pick_items[(size_t)idx];
    bool is_selected = idx == selected;
    int fg = is_selected ? theme.fg_selection : theme.fg_command;
    int bg = is_selected ? theme.bg_selection : panel_theme.bg_command;
    int row_y = list_y + row;
    ui->fill_rect({x + 1, row_y, std::max(1, w - 2), 1}, " ", fg, bg);

    // Accent bar on the selected row, matching the palette.
    if (is_selected)
    {
      ui->draw_text(x + 1, row_y, "▎", theme.fg_selection, bg);
    }

    int detail_w = w >= 72 ? std::max(16, w / 3) : 0;
    int label_w = std::max(8, w - detail_w - 5);
    std::string label = ui_truncate_cells(item.label, label_w - 2);

    // Highlight the query substring inside the label when it appears
    // verbatim (case-insensitive); fuzzy scatter is left to the palette.
    std::vector<int> match;
    if (!quick_pick_query.empty() && !label.empty())
    {
      std::string hay = label;
      std::string needle = quick_pick_query;
      for (char &c : hay)
      {
        c = (char)std::tolower((unsigned char)c);
      }
      for (char &c : needle)
      {
        c = (char)std::tolower((unsigned char)c);
      }
      size_t pos = hay.find(needle);
      if (pos != std::string::npos && needle.size() == quick_pick_query.size())
      {
        for (size_t k = 0; k < needle.size(); k++)
        {
          match.push_back((int)(pos + k));
        }
      }
    }

    int text_x = x + 2 + (is_selected ? 1 : 0);
    int match_fg = is_selected ? theme.fg_selection : theme.fg_keyword;
    ui_draw_marked_text(*ui, text_x, row_y, label, match, fg, bg, match_fg);
    if (detail_w > 0 && !item.detail.empty())
    {
      ui->draw_text(x + w - detail_w - 1,
                    row_y,
                    ui_truncate_cells(item.detail, detail_w),
                    theme.fg_comment,
                    bg);
    }
  }

  std::string footer = "Enter open   Esc close   Up/Down move   PgUp/PgDn page";
  if (selected >= 0 && selected < (int)quick_pick_items.size()
      && !quick_pick_items[(size_t)selected].preview.empty())
  {
    footer = quick_pick_items[(size_t)selected].preview;
  }
  ui_draw_footer(
      *ui, rect, ui_truncate_cells(footer, w - 2), theme.fg_comment, panel_theme.bg_command);
}

void Editor::render_which_key_panel()
{
  // Which-key style helper docked just above the status line. Two modes:
  //   - prefix groups: a pressed chord ("Ctrl+T") prefixes longer Lua keymap
  //     sequences ("Ctrl+T N") — the panel lists the next-chord options;
  //   - held modifier (Windows Terminal): Ctrl is pressed alone — the panel
  //     lists every Ctrl+… binding (built-ins plus Lua overrides/groups).
  if (!show_which_key)
  {
    return;
  }

  const bool modifier_view = !which_key_modifier.empty();
  std::string crumb; // breadcrumb shown in the title ("Ctrl+T" / "Ctrl")
  std::string path;
  std::vector<PluginKeymapChild> children;
  if (modifier_view)
  {
    crumb = which_key_modifier;
    std::vector<jot::KeymapRef> refs;
    if (lua_api)
    {
      for (const auto &km : lua_api->keymaps())
      {
        if (km.mode != "global" && km.mode != "editor")
        {
          continue;
        }
        refs.push_back({km.key, km.detail, !km.callback.empty() || !km.command.empty()});
      }
    }
    for (const auto &row : jot::compose_ctrl_rows(refs))
    {
      PluginKeymapChild c;
      c.key = row.key;
      c.detail = row.detail;
      c.group = row.group;
      children.push_back(std::move(c));
    }
  }
  else
  {
    if (!lua_api || which_key_path.empty())
    {
      close_which_key();
      return;
    }
    for (size_t i = 0; i < which_key_path.size(); i++)
    {
      if (i > 0)
      {
        path += ' ';
      }
      path += which_key_path[i];
    }
    crumb = path;
    children = lua_api->plugin_keymap_children(path, "editor");
    if (children.empty())
    {
      // Keymaps were reloaded/cleared while the helper was open.
      close_which_key();
      return;
    }
  }

  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  int w = std::clamp(screen_w - 8, 46, 92);
  if (screen_w < 56)
  {
    w = std::max(30, screen_w - 2);
  }
  const int max_rows = std::max(3, std::min((int)children.size(),
                                            std::max(3, screen_h - status_height - 8)));
  int h = max_rows + 3;
  int x = std::max(1, (screen_w - w) / 2);
  int y = std::max(1, screen_h - status_height - h - 1);

  UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui,
                rect,
                {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border,
                 panel_theme.bg_command});

  // Title: breadcrumb path ("Ctrl+T" / "Ctrl") plus the optional group title.
  std::string title = " " + crumb;
  if (!modifier_view && lua_api)
  {
    const std::string group_title =
        lua_api->plugin_keymap_group_title(which_key_path.back(), "editor");
    if (!group_title.empty())
    {
      title += "  ·  " + group_title;
    }
  }
  ui_draw_panel_title(*ui, rect, ui_truncate_cells(title, w - 2), theme.fg_command,
                      panel_theme.bg_command);

  // Hint on the right of the title row.
  const char *unit = modifier_view ? "binding" : "key";
  std::string hint = children.size() > 1
                         ? std::to_string(children.size()) + " " + unit + "s"
                         : "1 " + std::string(unit);
  ui->draw_text(std::max(x + 1, x + w - (int)hint.size() - 1),
                y,
                hint,
                theme.fg_comment,
                panel_theme.bg_command);

  // Divider.
  ui->fill_rect({x + 1, y + 1, std::max(1, w - 2), 1}, "─", theme.fg_panel_border,
                panel_theme.bg_command);

  int key_w = 0;
  for (const auto &child : children)
  {
    const std::string display = child.key == " " ? "Space" : child.key;
    int cells = 0;
    for (unsigned char c : display)
    {
      cells += c < 128 ? 1 : 2;
    }
    key_w = std::max(key_w, cells);
  }
  key_w = std::clamp(key_w + 3, 6, std::max(6, w / 3));
  int detail_w = std::max(0, w - key_w - 5);

  int selected = std::clamp(which_key_selected, 0, (int)children.size() - 1);
  int start_idx = std::max(0, selected - max_rows + 1);
  if (start_idx + max_rows > (int)children.size())
  {
    start_idx = std::max(0, (int)children.size() - max_rows);
  }

  int list_y = y + 2;
  for (int row = 0; row < max_rows; row++)
  {
    int idx = start_idx + row;
    if (idx < 0 || idx >= (int)children.size())
    {
      break;
    }
    const auto &child = children[(size_t)idx];
    bool is_selected = idx == selected;
    int fg = is_selected ? theme.fg_selection : theme.fg_command;
    int bg = is_selected ? theme.bg_selection : panel_theme.bg_command;
    int row_y = list_y + row;

    ui->fill_rect({x + 1, row_y, std::max(1, w - 2), 1}, " ", fg, bg);
    if (is_selected)
    {
      ui->draw_text(x + 1, row_y, "▎", theme.fg_selection, bg);
    }

    // Key in the accent color, detail beside it; subgroups get a marker.
    std::string key_text = child.key == " " ? "Space" : child.key;
    key_text = child.group ? "▸ " + key_text : key_text;
    ui->draw_text(x + 3 + (is_selected ? 1 : 0), row_y, ui_truncate_cells(key_text, key_w),
                  child.group ? theme.fg_keyword : fg, bg, !child.group);
    std::string detail = child.detail;
    std::string marker = child.group ? " …" : "";
    ui->draw_text(x + 3 + key_w, row_y, ui_truncate_cells(detail + marker, detail_w),
                  child.group ? theme.fg_comment : fg, bg);
  }

  std::string footer;
  if (modifier_view)
  {
    footer = "press a letter to run · release Ctrl to close";
  }
  else if (which_key_path.size() > 1)
  {
    footer = "Esc close · Backspace up · Enter run";
  }
  else
  {
    footer = "Esc close · Backspace up · arrow keys to move";
  }
  ui_draw_footer(
      *ui, rect, ui_truncate_cells(footer, w - 2), theme.fg_comment, panel_theme.bg_command);
}

void Editor::render_search_panel()
{
  if (!show_search)
    return;

  int w = std::min(72, std::max(42, ui->get_render_width() / 2));
  int h = search_replace_visible ? 5 : 4;
  int x = ui->get_width() - w - 2;
  int y = topbar_height() + tab_height;

  if (x < 0)
    x = 0;
  if (x + w > ui->get_width())
    w = std::max(20, ui->get_width() - x);

  // A registered Lua UI handler paints the search panel from this state; the
  // native rect and row geometry stay the source of truth so the input caret
  // (placed natively) lands on the Lua-drawn fields.
  if (lua_api && lua_api->has_lua_ui_handler("search_panel"))
  {
    SearchView view;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.query = search_query;
    view.replace_text = search_replace_text;
    view.replace_visible = search_replace_visible;
    view.focus_replace = search_replace_visible && search_focus_replace;
    view.case_sensitive = search_case_sensitive;
    view.whole_word = search_whole_word;
    view.regex = search_regex;
    view.scoped_to_selection = search_scoped_to_selection;
    if (search_result_index >= 0 && !search_results.empty())
    {
      view.count =
          std::to_string(search_result_index + 1) + "/" + std::to_string(search_results.size());
    }
    else
    {
      view.count = "0/0";
    }
    if (lua_api->emit_search(view))
    {
      return;
    }
  }

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui, rect, {theme.fg_command, theme.bg_command, theme.fg_panel_border, theme.bg_command});

  std::string count = "0/0";
  if (search_result_index >= 0 && !search_results.empty())
  {
    count = std::to_string(search_result_index + 1) + "/" + std::to_string(search_results.size());
  }
  else if (!search_query.empty())
  {
    count = "0/0";
  }

  std::string chips;
  chips += search_case_sensitive ? " Aa " : " aa ";
  chips += search_whole_word ? " W " : " w ";
  if (search_regex)
  {
    chips += " .* ";
  }
  if (search_scoped_to_selection)
  {
    chips += " Sel ";
  }
  chips += " " + count + " ";
  ui_draw_panel_title(*ui,
                      rect,
                      search_scoped_to_selection ? " Find in Selection" : " Find",
                      theme.fg_command,
                      theme.bg_command);
  ui->draw_text(
      std::max(x + 1, x + w - (int)chips.size() - 1), y, chips, theme.fg_comment, theme.bg_command);

  int label_w = 9;
  int input_w = std::max(1, w - label_w - 3);
  int find_fg = search_focus_replace ? theme.fg_command : theme.fg_selection;
  int find_bg = search_focus_replace ? theme.bg_command : theme.bg_selection;
  ui->draw_text(x + 1, y + 1, "Find", theme.fg_comment, theme.bg_command);
  ui->draw_text(x + label_w,
                y + 1,
                ui_truncate_cells(search_query, input_w),
                find_fg,
                find_bg,
                !search_focus_replace);

  if (search_replace_visible)
  {
    int replace_fg = search_focus_replace ? theme.fg_selection : theme.fg_command;
    int replace_bg = search_focus_replace ? theme.bg_selection : theme.bg_command;
    ui->draw_text(x + 1, y + 2, "Replace", theme.fg_comment, theme.bg_command);
    ui->draw_text(x + label_w,
                  y + 2,
                  ui_truncate_cells(search_replace_text, input_w),
                  replace_fg,
                  replace_bg,
                  search_focus_replace);
  }

  std::string footer = search_replace_visible
                           ? (search_scoped_to_selection
                                  ? "Enter next  Up prev  Tab field  ^R one  ^R+Shift all in sel"
                                  : "Enter next  Up prev  Tab field  ^R one  ^R+Shift all")
                           : "Enter next  Up prev  Tab case  ^H replace  ^E regex";
  ui->draw_text(
      x + 1, y + h - 2, ui_truncate_cells(footer, w - 3), theme.fg_comment, theme.bg_command);
}

void Editor::place_command_palette_cursor()
{
  if (!show_command_palette)
    return;
  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();
  const PaletteLayout layout =
      command_palette_layout(screen_w, screen_h, command_palette_results.size());
  // Mirrors render_command_palette: the input row is one below the panel
  // title, the query is drawn with a leading ":" at x + 1, truncated to the
  // input width.
  std::string query = command_palette_query;
  if (query.empty() || query[0] != ':')
  {
    query = ":" + query;
  }
  const std::string drawn = ui_truncate_cells(query, std::max(0, layout.w - 3));
  ui->set_cursor(layout.x + 1 + ui_cell_count(drawn), layout.y + 1);
}

void Editor::place_search_cursor()
{
  if (!show_search)
    return;
  int w = std::min(72, std::max(42, ui->get_render_width() / 2));
  int x = std::max(0, ui->get_width() - w - 2);
  if (x + w > ui->get_width())
    w = std::max(20, ui->get_width() - x);
  const int label_w = 9;
  const int input_w = std::max(1, w - label_w - 3);
  const bool replace = search_replace_visible && search_focus_replace;
  const std::string &input = replace ? search_replace_text : search_query;
  const int cursor_x = x + label_w + std::min(input_w - 1, std::max(0, ui_cell_count(input)));
  const int cursor_y = topbar_height() + tab_height + (replace ? 2 : 1);
  ui->set_cursor(cursor_x, cursor_y);
}

void Editor::render_context_menu()
{
  if (!show_context_menu || context_menu_items.empty())
    return;

  int w = std::max(1, context_menu_w);
  int h = std::max(1, context_menu_h);
  int x = context_menu_x;
  int y = context_menu_y;

  if (x + w > ui->get_render_width())
    x = std::max(0, ui->get_render_width() - w);
  if (y + h > ui->get_height())
    y = std::max(0, ui->get_height() - h);

  // A registered Lua UI handler paints the menu from this state; the native
  // rect (already clamped) is passed through so mouse hits stay aligned.
  if (lua_api && lua_api->has_lua_ui_handler("context_menu"))
  {
    ContextMenuView view;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.selected = context_menu_selected;
    for (const auto &item : context_menu_items)
    {
      ContextMenuItemView iv;
      iv.label = item.label;
      iv.enabled = item.enabled;
      view.items.push_back(std::move(iv));
    }
    if (lua_api->emit_context_menu(view))
    {
      return;
    }
  }

  // Same panel surface convention as the popup / palette / quick-pick.
  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  std::vector<UISelectableRow> rows;
  rows.reserve(context_menu_items.size());
  for (size_t i = 0; i < context_menu_items.size(); i++)
  {
    rows.push_back({context_menu_items[i].label,
                    (int)i == context_menu_selected,
                    context_menu_items[i].enabled});
  }
  ui_draw_selectable_rows(*ui,
                          x + 1,
                          y + 1,
                          std::max(1, w - 2),
                          std::max(0, h - 2),
                          rows,
                          {theme.fg_command,
                           panel_theme.bg_command,
                           theme.fg_selection,
                           theme.bg_selection,
                           theme.fg_comment,
                           panel_theme.bg_command});
}

std::vector<Editor::MenuBarMenu> Editor::build_menu_bar_model() const
{
  return {
      {"File",
       {{"New File", MENU_ACTION_NEW_FILE},
        {"Open File...", MENU_ACTION_OPEN_FINDER},
        {"Save", MENU_ACTION_SAVE},
        {"Save As...", MENU_ACTION_SAVE_AS},
        {"Close File", MENU_ACTION_CLOSE_FILE},
        {"Quit", MENU_ACTION_QUIT}}},
      {"Edit",
       {{"Undo", MENU_ACTION_UNDO},
        {"Redo", MENU_ACTION_REDO},
        {"Cut", MENU_ACTION_CUT},
        {"Copy", MENU_ACTION_COPY},
        {"Paste", MENU_ACTION_PASTE},
        {"Find", MENU_ACTION_COMMAND, "Toggle Search"},
        {"Format Document", MENU_ACTION_COMMAND, "Format Document"},
        {"Trim Trailing Whitespace", MENU_ACTION_COMMAND, "Trim Trailing Whitespace"}}},
      {"Selection",
       {{"Select All", MENU_ACTION_SELECT_ALL},
        {"Select Line", MENU_ACTION_SELECT_LINE},
        {"Duplicate Line", MENU_ACTION_DUPLICATE_LINE},
        {"Move Line Up", MENU_ACTION_MOVE_LINE_UP},
        {"Move Line Down", MENU_ACTION_MOVE_LINE_DOWN},
        {"Toggle Comment", MENU_ACTION_TOGGLE_COMMENT}}},
      {"View",
       {{"Command Palette", MENU_ACTION_COMMAND_PALETTE},
        {"Explorer", MENU_ACTION_TOGGLE_SIDEBAR},
        {"Toggle Minimap", MENU_ACTION_TOGGLE_MINIMAP},
        {"Color Theme", MENU_ACTION_THEME},
        {"Home", MENU_ACTION_HOME}}},
      {"Go",
       {{"Go to Line...", MENU_ACTION_COMMAND, ":line "},
        {"Go to Definition", MENU_ACTION_LSP_DEFINITION},
        {"Back", MENU_ACTION_LSP_BACK}}},
      {"Debug",
       {{"Start Debugging", MENU_ACTION_COMMAND, ":debug "},
        {"Continue", MENU_ACTION_DEBUG_CONTINUE},
        {"Pause", MENU_ACTION_DEBUG_PAUSE},
        {"Step Over", MENU_ACTION_DEBUG_STEP_OVER},
        {"Step Into", MENU_ACTION_DEBUG_STEP_IN},
        {"Step Out", MENU_ACTION_DEBUG_STEP_OUT},
        {"Stop", MENU_ACTION_DEBUG_STOP},
        {"Debug Panel", MENU_ACTION_TOGGLE_DEBUG_PANEL}}},
      {"Terminal",
       {{"Toggle Terminal", MENU_ACTION_TOGGLE_TERMINAL},
        {"New Terminal", MENU_ACTION_NEW_TERMINAL},
        {"Run Task...", MENU_ACTION_TASKS},
        {"Rerun Last Task", MENU_ACTION_RERUN_TASK}}},
      {"Help",
       {{"Help", MENU_ACTION_HELP},
        {"Install Language Server...", MENU_ACTION_COMMAND, ":lspinstall "},
        {"Remove Language Server...", MENU_ACTION_COMMAND, ":lspremove "},
        {"Tree-sitter Status", MENU_ACTION_COMMAND, ":tsstatus"},
        {"Git Status", MENU_ACTION_COMMAND, ":gitstatus"}}},
  };
}

void Editor::render_menu_bar()
{
  if (!kTopBarVisible)
  {
    return;
  }
  int w = ui ? ui->get_render_width() : 0;
  if (w <= 0)
  {
    return;
  }

  UIRect row = {0, 0, w, 1};
  ui->fill_rect(row, " ", theme.fg_status, theme.bg_status);
  menu_bar_segments.clear();

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  int x = 0;
  for (int i = 0; i < (int)menus.size(); i++)
  {
    std::string label = " " + menus[i].label + " ";
    int label_w = (int)label.size();
    if (x + label_w > w)
    {
      break;
    }
    bool active = show_menu_bar_dropdown && i == menu_bar_active;
    int fg = active ? theme.fg_selection : theme.fg_status;
    int bg = active ? theme.bg_selection : theme.bg_status;
    ui->draw_text(x, 0, label, fg, bg, active);
    menu_bar_segments.push_back({i, x, x + label_w});
    x += label_w;
  }
}

void Editor::render_menu_dropdown()
{
  if (!kTopBarVisible || !show_menu_bar_dropdown || !ui)
  {
    return;
  }

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  if (menu_bar_active < 0 || menu_bar_active >= (int)menus.size())
  {
    return;
  }

  const auto &menu = menus[menu_bar_active];
  if (menu.items.empty())
  {
    return;
  }

  int label_x = 0;
  for (const auto &segment : menu_bar_segments)
  {
    if (segment.menu_index == menu_bar_active)
    {
      label_x = segment.x;
      break;
    }
  }

  int max_label = 0;
  for (const auto &item : menu.items)
  {
    max_label = std::max(max_label, (int)item.label.size());
  }

  int w = std::max(18, max_label + 4);
  int h = (int)menu.items.size() + 2;
  int x = std::clamp(label_x, 0, std::max(0, ui->get_render_width() - w));
  int y = topbar_height();
  int max_h = std::max(1, ui->get_height() - y - status_height);
  h = std::min(h, max_h);

  // A registered Lua UI handler paints the dropdown from this state; the rect
  // stays native so the bar highlight and mouse hover keep their hit regions.
  if (lua_api && lua_api->has_lua_ui_handler("menu_dropdown"))
  {
    MenuDropdownView view;
    view.menu_label = menu.label;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.selected = menu_bar_selected;
    for (const auto &item : menu.items)
    {
      MenuItemView iv;
      iv.label = item.label;
      iv.enabled = item.enabled;
      view.items.push_back(std::move(iv));
    }
    if (lua_api->emit_menu_dropdown(view))
    {
      return;
    }
  }

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  std::vector<UISelectableRow> rows;
  rows.reserve(menu.items.size());
  for (int i = 0; i < (int)menu.items.size(); i++)
  {
    rows.push_back(
        {menu.items[(size_t)i].label, i == menu_bar_selected, menu.items[(size_t)i].enabled});
  }
  ui_draw_selectable_rows(*ui,
                          x + 1,
                          y + 1,
                          std::max(1, w - 2),
                          std::max(0, h - 2),
                          rows,
                          {theme.fg_command,
                           panel_theme.bg_command,
                           theme.fg_selection,
                           theme.bg_selection,
                           theme.fg_comment,
                           panel_theme.bg_command});
}

void Editor::render_save_prompt()
{
  int h = ui->get_height();
  int w = ui->get_render_width();

  std::string prompt = "Save As: type filename, Enter=save, Esc=cancel";
  int x = std::max(0, w / 2 - ui_cell_count(prompt) / 2);
  int y = h / 2;

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {std::max(0, x - 2), std::max(0, y - 1), std::min(w, ui_cell_count(prompt) + 4), 4};

  if (lua_api && lua_api->has_lua_ui_handler("save_prompt"))
  {
    PromptView view;
    view.input = save_prompt_input;
    view.x = rect.x;
    view.y = rect.y;
    view.w = rect.w;
    view.h = rect.h;
    if (lua_api->emit_prompt("save_prompt", view))
    {
      return;
    }
  }

  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  ui->draw_text(x, y, prompt, theme.fg_command, panel_theme.bg_command);

  std::string disp = "Filename: " + save_prompt_input;
  ui->draw_text(x,
                std::min(h - 1, y + 1),
                ui_truncate_cells(disp, std::max(1, w - x - 1)),
                theme.fg_command,
                panel_theme.bg_command);
}

void Editor::place_save_prompt_cursor()
{
  if (!show_save_prompt)
    return;
  const int h = ui->get_height();
  const int w = ui->get_render_width();
  const std::string prompt = "Save As: type filename, Enter=save, Esc=cancel";
  const int x = std::max(0, w / 2 - ui_cell_count(prompt) / 2);
  const int y = h / 2;
  const std::string prefix = "Filename: ";
  ui->set_cursor(x + std::min(std::max(0, w - x - 1), ui_cell_count(prefix + save_prompt_input)),
                 std::min(h - 1, y + 1));
}

void Editor::render_quit_prompt()
{
  int h = ui->get_height();
  int w = ui->get_render_width();

  std::string prompt = "Unsaved changes! Quit anyway? (y/n)";
  int x = w / 2 - prompt.length() / 2;
  int y = h / 2;

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x - 2, y - 1, (int)prompt.length() + 4, 3};

  if (lua_api && lua_api->has_lua_ui_handler("quit_prompt"))
  {
    PromptView view;
    view.x = rect.x;
    view.y = rect.y;
    view.w = rect.w;
    view.h = rect.h;
    if (lua_api->emit_prompt("quit_prompt", view))
    {
      return;
    }
  }

  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  ui->draw_text(x, y, prompt, theme.fg_command, panel_theme.bg_command);
}

void Editor::sync_lua_ui_surfaces()
{
  if (!lua_api)
  {
    lua_ui_prev_command_palette = false;
    lua_ui_prev_quick_pick = false;
    lua_ui_prev_popup = false;
    lua_ui_prev_save_prompt = false;
    lua_ui_prev_quit_prompt = false;
    lua_ui_prev_tree_sitter_status = false;
    lua_ui_prev_telescope = false;
    lua_ui_prev_lsp_completion = false;
    lua_ui_prev_context_menu = false;
    lua_ui_prev_menu_dropdown = false;
    lua_ui_prev_search = false;
    lua_ui_prev_home = false;
    lua_ui_prev_sidebar = false;
    lua_ui_prev_side_panel = false;
    return;
  }
  auto sync = [&](bool visible, bool &prev, const char *name)
  {
    if (prev && !visible)
    {
      lua_api->emit_lua_ui_close(name);
    }
    prev = visible;
  };
  sync(show_command_palette, lua_ui_prev_command_palette, "command_palette");
  sync(show_quick_pick, lua_ui_prev_quick_pick, "quick_pick");
  sync(popup.visible && popup.presentation == POPUP_MODAL, lua_ui_prev_popup, "popup");
  sync(show_save_prompt, lua_ui_prev_save_prompt, "save_prompt");
  sync(show_quit_prompt, lua_ui_prev_quit_prompt, "quit_prompt");
  sync(show_tree_sitter_status_modal, lua_ui_prev_tree_sitter_status, "tree_sitter_status");
  sync(telescope.is_active(), lua_ui_prev_telescope, "telescope");
  sync(lsp_completion_visible && !lsp_completion_items.empty(),
       lua_ui_prev_lsp_completion,
       "lsp_completion");
  sync(show_context_menu, lua_ui_prev_context_menu, "context_menu");
  sync(show_menu_bar_dropdown, lua_ui_prev_menu_dropdown, "menu_dropdown");
  sync(show_search, lua_ui_prev_search, "search_panel");
  sync(show_home_menu, lua_ui_prev_home, "home_screen");
  sync(show_sidebar, lua_ui_prev_sidebar, "sidebar");
  sync(show_right_panel, lua_ui_prev_side_panel, "side_panel");
}

void Editor::render_popup()
{
  if (!popup.visible)
    return;

  // Modal popups (help, keymap listings, ...) hand off to a Lua UI handler
  // when registered; hover popups keep the native path (their Lua counterpart
  // is the lsp.hover_ui handler).
  if (popup.presentation == POPUP_MODAL && lua_api && lua_api->has_lua_ui_handler("popup"))
  {
    PopupView view;
    view.title = popup.title;
    view.scroll = popup.scroll;
    view.x = popup.x;
    view.y = popup.y;
    view.w = popup.w;
    view.h = popup.h;
    view.lines = popup.lines;
    if (lua_api->emit_popup(view))
    {
      return;
    }
  }

  UIRect rect = {popup.x, popup.y, popup.w, popup.h};
  Theme popup_theme = theme;
  popup_theme.bg_command = theme.bg_panel_border;
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, popup_theme.bg_command, theme.fg_panel_border, popup_theme.bg_command});
  if (!popup.title.empty())
  {
    ui_draw_panel_title(
        *ui, rect, " " + popup.title + " ", theme.fg_command, popup_theme.bg_command);
  }

  bool in_code_block = false;
  std::string code_extension;
  int draw_row = 0;
  int content_row = 0;
  for (int i = 0; i < (int)popup.lines.size(); i++)
  {
    std::string fence_language;
    if (hover_markdown_fence_language(popup.lines[i], &fence_language))
    {
      in_code_block = !in_code_block;
      code_extension = in_code_block ? hover_language_extension(fence_language) : "";
      continue;
    }

    if (content_row++ < popup.scroll)
    {
      continue;
    }
    if (draw_row >= popup.h - 2)
      break;
    if (in_code_block)
    {
      draw_hover_code_line(ui,
                           popup.x + 1,
                           popup.y + 1 + draw_row,
                           popup.w - 2,
                           popup.lines[i],
                           code_extension,
                           popup_theme);
    }
    else
    {
      ui->draw_text(popup.x + 1,
                    popup.y + 1 + draw_row,
                    ui_truncate_cells(popup.lines[i], popup.w - 2),
                    theme.fg_command,
                    popup_theme.bg_command);
    }
    draw_row++;
  }

  if (popup.content_lines > popup.h - 2)
  {
    const int first = popup.scroll + 1;
    const int last = std::min(popup.content_lines, popup.scroll + popup.h - 2);
    const std::string count = std::to_string(first) + "-" + std::to_string(last) + "/"
                              + std::to_string(popup.content_lines);
    ui->draw_text(popup.x + std::max(1, popup.w - ui_cell_count(count) - 1),
                  popup.y + popup.h - 1,
                  count,
                  theme.fg_comment,
                  popup_theme.bg_command);
  }
}

void Editor::render_tabs()
{
  // Global shared tabs are intentionally disabled.
  // Each pane renders its own local tab header.
}

// ---------------------------------------------------------------------------
// Easter egg: Konami code (↑↑↓↓←→←→) rainbow popup
// ---------------------------------------------------------------------------
