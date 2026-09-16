// Which-key chord panel: open/close and prefix-group input handling.
#include "editor.h"
#include "jot/event/keymap_candidates.h"
#include "jot/lua/api.h"

#include <algorithm>
#include <cctype>
#include <cstring>

void Editor::open_which_key(const std::string &chord)
{
  if (!lua_api || chord.empty())
  {
    return;
  }
  which_key_path = {chord};
  which_key_selected = 0;
  show_which_key = true;
}

void Editor::close_which_key()
{
  show_which_key = false;
  which_key_path.clear();
  which_key_selected = 0;
}

bool Editor::handle_which_key_input(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch)
{
  (void)is_ctrl;
  (void)is_shift;
  (void)is_alt;
  if (!show_which_key)
  {
    return false;
  }

  // Prefix-group view: a pressed chord ("Ctrl+T") prefixes longer Lua keymap
  // sequences ("Ctrl+T N") — the panel lists the next-chord options.
  if (!show_which_key)
  {
    return false;
  }

  if (!lua_api || which_key_path.empty())
  {
    close_which_key();
    return true;
  }

  if (ch == 27)
  {
    close_which_key();
    return true;
  }
  // Bare-modifier synthetic codes are inert here (see handle_terminal_event);
  // while a group is open they are simply consumed.
  if (ch == 1021 || ch == 1022)
  {
    return true;
  }
  // Backspace steps up a level; at the top level it closes the helper.
  if (ch == 8 || ch == 127)
  {
    if (which_key_path.size() > 1)
    {
      which_key_path.pop_back();
      which_key_selected = 0;
    }
    else
    {
      close_which_key();
    }
    return true;
  }

  // Recompute the rows for the current path every key so newly registered
  // keymaps show up immediately (configs can add keymaps from within a
  // keymap action).
  std::string path;
  for (size_t i = 0; i < which_key_path.size(); i++)
  {
    if (i > 0)
    {
      path += ' ';
    }
    path += which_key_path[i];
  }
  auto children = lua_api->plugin_keymap_children(path, "editor");
  if (children.empty())
  {
    close_which_key();
    return true;
  }

  if (ch == 1008)
  { // Up
    which_key_selected = std::max(0, which_key_selected - 1);
    return true;
  }
  if (ch == 1009)
  { // Down
    which_key_selected = std::min((int)children.size() - 1, which_key_selected + 1);
    return true;
  }

  auto pick = [&](const PluginKeymapChild &child) -> bool
  {
    const std::string next_path = path + " " + child.key;
    const bool deeper = !lua_api->plugin_keymap_children(next_path, "editor").empty();
    if (deeper)
    {
      which_key_path.push_back(child.key);
      which_key_selected = 0;
      return true;
    }
    close_which_key();
    lua_api->run_plugin_keymap(next_path, "editor");
    return true;
  };

  auto candidates = event_internal::plugin_key_candidates(ch, is_ctrl, is_shift, is_alt, original_ch);
  // A child token is matched by its bare key. When the user keeps Ctrl held
  // while pressing the next chord (e.g. holds Ctrl through "Ctrl+T" then
  // presses "N"), the chord arrives as "Ctrl+N" — match the letter part too.
  std::vector<std::string> match_forms = candidates;
  for (const auto &candidate : candidates)
  {
    for (const char *mod : {"Ctrl+", "Alt+", "Shift+"})
    {
      const size_t len = std::strlen(mod);
      if (candidate.size() > len && candidate.compare(0, len, mod) == 0)
      {
        match_forms.push_back(candidate.substr(len));
      }
    }
  }
  // Case-insensitive on purpose. A pressed key is canonicalised ("a" arrives as
  // "A", and a chord as "Ctrl+N"), so an exact comparison silently refuses any
  // child registered in the case the user actually types -- and it does so by
  // closing the panel and typing the key, which looks like the keymap was never
  // registered at all.
  const auto same_token = [](const std::string &a, const std::string &b)
  {
    if (a.size() != b.size())
    {
      return false;
    }
    for (size_t i = 0; i < a.size(); i++)
    {
      if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
      {
        return false;
      }
    }
    return true;
  };
  for (const auto &form : match_forms)
  {
    for (const auto &child : children)
    {
      if (same_token(child.key, form))
      {
        return pick(child);
      }
    }
  }

  // Enter runs the currently highlighted row.
  if (ch == '\n' || ch == 13)
  {
    const int sel = std::clamp(which_key_selected, 0, (int)children.size() - 1);
    return pick(children[(size_t)sel]);
  }

  // Unmatched key: dismiss the helper and let the normal editor path handle
  // the key (plugin/built-in chord or plain text insertion).
  close_which_key();
  return false;
}
