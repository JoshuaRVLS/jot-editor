// Git panel renderer (src/render/git_panel.cpp).
//
// Paints the lazygit-style git panel in the right dock: view tabs (Files /
// Branches / Commits / Stash), a branch header, status-colored rows with
// per-language Nerd Fonts file icons, section headers with counts and a
// focused border when the panel owns keyboard focus. Hands the model to the
// Lua side_panel handler when one is registered; the native fallback stays
// byte-identical.

#include "editor.h"
#include "jot/file_icons.h"
#include "jot/lua/api.h"
#include "jot/workspace/git_panel_models.h"
#include "ui/text.h"

#include <algorithm>
#include <filesystem>

namespace
{
  namespace fs = std::filesystem;

  // Nerd Fonts glyphs (classic FontAwesome codepoints, present in every
  // Nerd Fonts build).
  constexpr const char *kIconConflict = "\uF071";  // warning triangle
  constexpr const char *kIconStaged = "\uF00C";    // check
  constexpr const char *kIconUnstaged = "\uF044";  // pencil
  constexpr const char *kIconUntracked = "\uF15B"; // file
  constexpr const char *kIconBranch = "\uE725";    // cod git-branch
  constexpr const char *kIconCommit = "\uE731";    // cod git-commit
  constexpr const char *kIconStash = "\uF187";     // archive

  // Status-pair letter -> theme color, matching the sidebar / diff panels.
  int status_fg(const Theme &theme, const std::string &xy)
  {
    if (jot_git_panel::status_section(xy) == "conflict")
    {
      return theme.fg_git_conflict;
    }
    const char primary = !xy.empty() && xy[0] != ' '
                             ? xy[0]
                             : (!xy.empty() && xy.size() > 1 ? xy[1] : ' ');
    switch (primary)
    {
    case 'A':
      return theme.fg_git_added;
    case 'D':
      return theme.fg_git_deleted;
    case 'R':
      return theme.fg_git_renamed;
    case '?':
      return theme.fg_git_untracked;
    case 'M':
    default:
      return theme.fg_git_modified;
    }
  }

  struct SectionStyle
  {
    const char *glyph;
    int fg;
  };
  SectionStyle section_style(const Theme &theme, const std::string &section)
  {
    if (section == "conflict")
      return {kIconConflict, theme.fg_git_conflict};
    if (section == "staged")
      return {kIconStaged, theme.fg_git_added};
    if (section == "unstaged")
      return {kIconUnstaged, theme.fg_git_modified};
    return {kIconUntracked, theme.fg_git_untracked};
  }
} // namespace

void Editor::render_git_panel()
{
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_GIT || !ui)
  {
    return;
  }
  int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return;
  }

  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = topbar_height();
  const int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  UIRect panel = {panel_x, panel_y, panel_w, panel_h};

  const bool focused = focus_state == FOCUS_RIGHT_PANEL;
  const int border_fg = focused ? theme.fg_active_border : theme.fg_panel_border;
  ui->fill_rect(panel, " ", theme.fg_terminal, theme.bg_terminal);
  ui->draw_border(panel, border_fg, theme.bg_terminal);

  const int content_x = panel_x + 1;
  const int content_y = panel_y + 2;
  const int content_w = std::max(1, panel_w - 2);
  const int content_h = std::max(1, panel_h - 3);

  SidePanelView view;
  view.mode = "git";
  view.x = panel_x;
  view.y = panel_y;
  view.w = panel_w;
  view.h = panel_h;
  view.title = " Git Panel ";

  using namespace jot_git_panel;
  const jot_git_panel::State &state = git_panel;
  const bool files_view = state.view == View::Files;

  // View tabs (lazygit panel numbers: 2 files, 3 branches, 4 commits, 5 stash).
  const std::vector<std::pair<std::string, View>> kTabs = {
      {" \uE725 2 Files ", View::Files},
      {" \uE725 3 Branches ", View::Branches},
      {" \uE731 4 Commits ", View::Commits},
      {" \uF187 5 Stash ", View::Stash},
  };
  int tab_x = panel_x + 1;
  const int tab_y = panel_y + 1;
  for (const auto &tab : kTabs)
  {
    if (tab_x + (int)tab.first.size() >= panel_x + panel_w - 1)
    {
      break;
    }
    const bool active = state.view == tab.second;
    view.tabs.push_back({tab.first, active});
    ui->draw_text(tab_x,
                  tab_y,
                  tab.first,
                  active ? theme.fg_terminal_tab_focused : theme.fg_terminal_tab_inactive,
                  active ? theme.bg_terminal_tab_focused : theme.bg_terminal_tab_inactive,
                  active);
    tab_x += (int)tab.first.size();
  }

  // Header: repo name + current branch with drift + change counts.
  std::string repo = "<repo>";
  if (!git_root.empty())
  {
    std::error_code ec;
    repo = fs::path(git_root).filename().string();
    if (repo.empty())
    {
      repo = git_root;
    }
  }
  std::string branch_part = git_branch.empty() ? "(no branch)" : git_branch;
  if (git_ahead > 0 || git_behind > 0)
  {
    std::string drift;
    if (git_ahead > 0)
      drift += "\xe2\x86\x91" + std::to_string(git_ahead); // ↑
    if (git_behind > 0)
      drift += "\xe2\x86\x93" + std::to_string(git_behind); // ↓
    branch_part += " " + drift;
  }
  std::string counts;
  if (files_view)
  {
    if (git_conflict_count > 0)
      counts += " " + std::to_string(git_conflict_count) + " conflict"
                + (git_conflict_count > 1 ? "s" : "");
    if (git_staged_count > 0)
      counts += " " + std::to_string(git_staged_count) + " staged";
    if (git_unstaged_count > 0)
      counts += " " + std::to_string(git_unstaged_count) + " unstaged";
    if (git_untracked_count > 0)
      counts += " " + std::to_string(git_untracked_count) + " untracked";
  }
  else
  {
    counts += " (" + std::to_string(state.row_count()) + ")";
  }
  const std::string header = repo + " " + branch_part + counts;
  view.header = ui_truncate_cells(header, content_w);
  view.header_fg = theme.fg_status_info;
  // Branch name + drift in accent, repo in the muted header color.
  const int branch_cells = std::min((int)ui_cell_count(repo + " " + branch_part),
                                    std::max(1, content_w));
  ui->draw_text(content_x, panel_y + 2, view.header, view.header_fg, theme.bg_terminal);
  if (focused)
  {
    ui->draw_text(content_x,
                  panel_y + 2,
                  ui_truncate_cells(repo + " " + branch_part, branch_cells),
                  theme.fg_status_file,
                  theme.bg_terminal,
                  true);
  }

  // Flatten the current view into renderable rows and window them by scroll.
  const std::vector<FlatRow> flat = build_flat_rows(state);
  const int body_y = content_y + 1;
  const int body_h = std::max(0, content_h - 1);

  if (flat.empty())
  {
    view.note = files_view ? "No changes — clean working tree"
                           : "Nothing here — press r to refresh";
    view.note_fg = theme.fg_comment;
    if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
    {
      return;
    }
    ui->draw_text(content_x, content_y, view.note, view.note_fg, theme.bg_terminal);
    return;
  }

  const int max_scroll = std::max(0, (int)flat.size() - body_h);
  git_panel.scroll = std::clamp(git_panel.scroll, 0, max_scroll);

  for (int row = 0; row < body_h; row++)
  {
    const int flat_index = git_panel.scroll + row;
    if (flat_index >= (int)flat.size())
    {
      break;
    }
    const FlatRow &fr = flat[(size_t)flat_index];

    SidePanelRowView r;
    r.bg = theme.bg_terminal;
    if (fr.section)
    {
      const SectionStyle style = section_style(theme, fr.label);
      const int count = (int)std::count_if(
          state.files.begin(),
          state.files.end(),
          [&](const FileRow &f) { return status_section(f.status) == fr.label; });
      r.kind = "git_section";
      r.text = std::string(style.glyph) + " " + fr.label + " (" + std::to_string(count) + ")";
      r.fg = style.fg;
      r.bold = true;
    }
    else
    {
      r.text = fr.label;
      const bool selected = fr.index == state.selected;
      r.selected = selected;
      switch (state.view)
      {
      case View::Files:
      {
        const FileRow &file = state.files[(size_t)fr.index];
        r.kind = "git_file";
        r.fg = status_fg(theme, file.status);
        const jot_icons::FileTypeIcon fti = jot_icons::file_type_icon(file.rel_path);
        r.icon = fti.glyph;
        r.icon_fg = fti.color;
        break;
      }
      case View::Branches:
      {
        const BranchRow &branch = state.branches[(size_t)fr.index];
        r.kind = "git_branch";
        r.icon = kIconBranch;
        if (branch.current)
        {
          r.fg = theme.fg_status_file;
          r.bold = true;
          r.icon_fg = theme.fg_status_file;
        }
        else
        {
          r.fg = theme.fg_terminal;
          r.icon_fg = theme.fg_comment;
        }
        r.detail = branch.tracking;
        break;
      }
      case View::Commits:
      {
        const CommitRow &commit = state.commits[(size_t)fr.index];
        r.kind = "git_commit";
        r.icon = kIconCommit;
        r.icon_fg = theme.fg_comment;
        r.fg = theme.fg_terminal;
        r.lead_fg = theme.fg_status_info;
        r.lead_len = (int)commit.hash.size();
        break;
      }
      case View::Stash:
      {
        const StashRow &stash = state.stashes[(size_t)fr.index];
        r.kind = "git_stash";
        r.icon = kIconStash;
        r.icon_fg = theme.fg_comment;
        r.fg = theme.fg_terminal;
        r.lead_fg = theme.fg_status_warning;
        r.lead_len = (int)stash.ref.size();
        break;
      }
      }
      if (selected)
      {
        r.fg = theme.fg_tab_active;
        if (r.icon_fg < 0)
        {
          r.icon_fg = theme.fg_tab_active;
        }
      }
    }
    view.rows.push_back(std::move(r));
  }

  // Pending two-step confirmation hint in the last row.
  if (!state.pending_confirm.empty())
  {
    SidePanelRowView hint;
    hint.text = " press d again to confirm ";
    hint.kind = "git_hint";
    hint.fg = theme.fg_status_warning;
    hint.bg = theme.bg_terminal;
    view.rows.push_back(std::move(hint));
  }

  if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
  {
    return;
  }

  // Native fallback: identical rows, plus a key-hint footer line.
  const int draw_rows = (int)view.rows.size();
  for (int row = 0; row < draw_rows && row < body_h; row++)
  {
    const SidePanelRowView &r = view.rows[row];
    int draw_x = content_x;
    if (!r.icon.empty())
    {
      ui->draw_text(draw_x,
                    body_y + row,
                    r.icon,
                    r.icon_fg >= 0 ? r.icon_fg : r.fg,
                    r.selected ? theme.bg_tab_active : r.bg);
      draw_x += (int)ui_cell_count(r.icon) + 1;
    }
    std::string text = ui_truncate_cells(r.text, content_w - (draw_x - content_x));
    int lead_fg = r.lead_fg;
    const int lead_bytes = r.lead_len;
    if (lead_fg >= 0 && lead_bytes > 0 && (int)text.size() > lead_bytes)
    {
      // Lead (commit hash / stash ref) is ASCII, so bytes == cells.
      const std::string lead = text.substr(0, (size_t)lead_bytes);
      ui->draw_text(draw_x,
                    body_y + row,
                    lead,
                    lead_fg,
                    r.selected ? theme.bg_tab_active : r.bg,
                    r.bold);
      draw_x += (int)ui_cell_count(lead);
      text = text.substr((size_t)lead_bytes);
    }
    if (!r.detail.empty())
    {
      const int detail_w = std::min((int)ui_cell_count(r.detail), std::max(1, content_w - 6));
      const int pad = std::max(0, content_w - (int)ui_cell_count(text) - detail_w - 1);
      text = ui_truncate_cells(text, std::max(1, content_w - detail_w - 1)) + std::string(pad, ' ')
             + r.detail;
    }
    ui->draw_text(draw_x,
                  body_y + row,
                  ui_truncate_cells(text, std::max(1, content_w - (draw_x - content_x))),
                  r.fg,
                  r.selected ? theme.bg_tab_active : r.bg,
                  r.bold);
  }
  if (body_h > draw_rows)
  {
    ui->draw_text(content_x,
                  body_y + draw_rows,
                  "space stage/checkout  a/A all  c commit  d discard  s stash  y copy  r refresh",
                  theme.fg_comment,
                  theme.bg_terminal);
  }
}