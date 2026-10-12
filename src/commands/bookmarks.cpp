#include "core/editor.h"

// Bookmarks: a line marked in the buffer, walked with `:bookmarknext` /
// `:bookmarkprev` (Alt+] b / Alt+[ b) and toggled with :bookmark (Alt+C b).
// Reports go to the toast channel -- the status-line message slot is not drawn
// once the Lua UI kit owns the bar, so a bookmark that reported itself there
// said nothing at all -- and each one asks for the repaint the gutter marker
// needs, which the menu-bar path used to get from the menu itself.
void Editor::toggle_bookmark()
{
  auto &buf = get_buffer();
  if (buf.bookmarks.count(buf.cursor.y))
  {
    buf.bookmarks.erase(buf.cursor.y);
    set_message("Bookmark removed: line " + std::to_string(buf.cursor.y + 1), true);
  }
  else
  {
    buf.bookmarks.insert(buf.cursor.y);
    set_message("Bookmark added: line " + std::to_string(buf.cursor.y + 1), true);
  }
  needs_redraw = true;
}

void Editor::next_bookmark()
{
  auto &buf = get_buffer();
  if (buf.bookmarks.empty())
  {
    set_message("No bookmarks in this file (:bookmark marks a line)", true);
    return;
  }

  auto it = buf.bookmarks.upper_bound(buf.cursor.y);
  if (it != buf.bookmarks.end())
  {
    buf.cursor.y = *it;
  }
  else
  {
    // The ends wrap, the way the diagnostic and compile walks do: a reader who
    // keeps pressing the key reaches every bookmark rather than stopping at the
    // last one.
    buf.cursor.y = *buf.bookmarks.begin();
  }
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
}

void Editor::prev_bookmark()
{
  auto &buf = get_buffer();
  if (buf.bookmarks.empty())
  {
    set_message("No bookmarks in this file (:bookmark marks a line)", true);
    return;
  }

  auto it = buf.bookmarks.lower_bound(buf.cursor.y);
  if (it != buf.bookmarks.begin())
  {
    --it;
    buf.cursor.y = *it;
  }
  else
  {
    buf.cursor.y = *buf.bookmarks.rbegin();
  }
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
}
