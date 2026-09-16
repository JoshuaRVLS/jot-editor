// Which-key popup: pending key chord and the actions it can complete.
//
// The panel is a transient input hint, so it is drawn as a small dock in the
// bottom-left corner of the editor, directly above the status line: at the edge
// of vision, where the eye already goes for state, instead of centred over the
// middle of the file. It is sized to its contents rather than to a fraction of
// the window, because a four-item menu does not need ninety columns.
//
// The frame stays -- it is the only surface signal available, the theme has no
// dedicated overlay background -- but nothing else is spent on chrome: no divider
// row, no empty footer row, no ellipsis repeated in both the label and the group
// marker, and no key count unless the list is actually scrolled.
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

void Editor::render_which_key_panel()
{
  // Which-key style helper: a pressed chord ("Alt+D") prefixes longer keymap
  // sequences ("Alt+D a f") -- the panel lists the next-chord options.
  if (!show_which_key)
  {
    return;
  }

  std::string path;
  std::vector<PluginKeymapChild> children;
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
  const std::string crumb = path;
  children = lua_api->plugin_keymap_children(path, "editor");
  if (children.empty())
  {
    // Keymaps were reloaded/cleared while the helper was open.
    close_which_key();
    return;
  }

  const auto cells = [](const std::string &text)
  {
    int n = 0;
    for (unsigned char c : text)
    {
      n += c < 128 ? 1 : 2;
    }
    return n;
  };
  const auto key_label = [](const PluginKeymapChild &child)
  { return child.key == " " ? std::string("Space") : child.key; };

  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();
  const int editor_left = show_sidebar ? effective_sidebar_width() : 0;

  Theme panel_theme = theme;
  panel_theme.bg_command = theme.bg_panel_border;

  // Content-sized: the widest key and the widest description decide the width, so
  // a short menu stays short.
  const std::string group_title = lua_api->plugin_keymap_group_title(path, "editor");
  std::string title = " " + crumb;
  if (!group_title.empty())
  {
    title += "  " + group_title;
  }
  title += " ";

  // A row that repeats the title above it ("Delete" over "Delete around") is
  // noise, because the header has already said it. The keymap keeps the full
  // self-describing label -- that is what a standalone listing shows, and what a
  // submenu uses as its own title -- and the menu drops the repeated prefix.
  const std::string title_prefix = group_title.empty() ? std::string() : group_title + " ";
  std::vector<std::string> details;
  details.reserve(children.size());
  for (const auto &child : children)
  {
    std::string detail = child.detail;
    if (!title_prefix.empty() && detail.size() > title_prefix.size()
        && detail.compare(0, title_prefix.size(), title_prefix) == 0)
    {
      detail = detail.substr(title_prefix.size());
    }
    details.push_back(detail);
  }

  int key_w = 0;
  int detail_w = 0;
  for (size_t i = 0; i < children.size(); i++)
  {
    key_w = std::max(key_w, cells(key_label(children[i])));
    detail_w = std::max(detail_w, cells(details[i]));
  }
  key_w = std::clamp(key_w, 1, 12);
  detail_w = std::clamp(detail_w, 8, 40);

  constexpr int marker_w = 2; // "▸ " on a submenu, "  " on a leaf
  constexpr int gutter = 3;
  // Both borders, the selection caret, a gap so the caret never touches the group
  // marker, and the trailing padding.
  const int chrome = 1 + 1 + 1 + marker_w + gutter + 1;
  const int avail = std::max(20, screen_w - editor_left - 2);
  int w = chrome + key_w + detail_w;
  w = std::max(w, std::min(cells(title) + 4, avail));
  w = std::min(w, avail);

  // One row per key plus the two border rows: the rows carry the content and the
  // title rides on the top border, so nothing else is needed.
  const int rows_total = (int)children.size();
  // A hint dock, not a wall: past eight rows the list scrolls (and says how many
  // keys are left) rather than growing to fill the screen.
  constexpr int kMaxDockRows = 8;
  const int room = std::min(kMaxDockRows, std::max(3, screen_h - status_height - 4));
  const int max_rows = std::min(rows_total, room);
  const int h = max_rows + 2;
  const int x = editor_left + 1;
  const int y = std::max(1, screen_h - status_height - h);

  const UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui, rect,
                {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border,
                 panel_theme.bg_command});

  // How many keys lie past the visible rows, and only when there are some.
  if (rows_total > max_rows)
  {
    const std::string more = " " + std::to_string(rows_total - max_rows) + " more ";
    ui->draw_text(std::max(x + 1, x + w - (int)more.size() - 2), y, more, theme.fg_comment,
                  panel_theme.bg_command);
  }
  ui_draw_panel_title(*ui, rect, ui_truncate_cells(title, std::max(4, w - 2)), theme.fg_command,
                      panel_theme.bg_command);

  const int selected = std::clamp(which_key_selected, 0, rows_total - 1);
  int start_idx = std::max(0, selected - max_rows + 1);
  if (start_idx + max_rows > rows_total)
  {
    start_idx = std::max(0, rows_total - max_rows);
  }

  // The caret column is left of the marker with a gap between them, so the two
  // glyphs never read as one.
  const int marker_x = x + 3;
  const int key_x = marker_x + marker_w;
  const int detail_x = key_x + key_w + gutter;
  const int detail_budget = std::max(1, x + w - 2 - detail_x);
  for (int row = 0; row < max_rows; row++)
  {
    const int idx = start_idx + row;
    if (idx < 0 || idx >= rows_total)
    {
      break;
    }
    const auto &child = children[(size_t)idx];
    const bool is_selected = idx == selected;
    const int fg = is_selected ? theme.fg_selection : theme.fg_command;
    const int bg = is_selected ? theme.bg_selection : panel_theme.bg_command;
    const int row_y = y + 1 + row;

    ui->fill_rect({x + 1, row_y, std::max(1, w - 2), 1}, " ", fg, bg);
    if (is_selected)
    {
      ui->draw_text(x + 1, row_y, "▎", theme.fg_selection, bg);
    }

    // A submenu is marked, so a key that opens more keys never looks like a key
    // that runs a command; the keys themselves are the accents.
    ui->draw_text(marker_x, row_y, child.group ? "▸ " : "  ",
                  child.group ? theme.fg_keyword : theme.fg_comment, bg);
    ui->draw_text(key_x, row_y, ui_truncate_cells(key_label(child), key_w),
                  child.group ? theme.fg_keyword : fg, bg, !child.group);
    ui->draw_text(detail_x, row_y, ui_truncate_cells(details[(size_t)idx], detail_budget),
                  child.group ? theme.fg_comment : fg, bg);
  }
}
