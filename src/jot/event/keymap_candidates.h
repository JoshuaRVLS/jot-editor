// Plugin keymap helpers shared by the terminal event dispatch and the
// which-key panel: canonical chord names, candidate chord lists (with the
// optional Ctrl+Shift fallback), and the JOT_KEYMAP_DEBUG trace log.
#pragma once

#include "jot/keybind_catalog.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace event_internal
{
inline std::string plugin_key_name(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch)
{
  // Shared with the which-key helper: see keybind_catalog.h. Named keys
  // (Enter/Tab/Esc/Backspace/Space) keep their name under Ctrl instead of
  // collapsing into a Ctrl+letter chord.
  return jot::keybind_detail::chord_name(ch, is_ctrl, is_shift, is_alt, original_ch);
}

inline std::vector<std::string>
plugin_key_candidates(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch)
{
  std::vector<std::string> candidates;
  auto push_unique = [&candidates](std::string key)
  {
    if (!key.empty() && std::find(candidates.begin(), candidates.end(), key) == candidates.end())
    {
      candidates.push_back(std::move(key));
    }
  };

  push_unique(plugin_key_name(ch, is_ctrl, is_shift, is_alt, original_ch));

  int key = original_ch ? original_ch : ch;
  if ((key & 0x8000) != 0)
  {
    key &= 0x7FFF;
  }

  int letter = 0;
  if (key >= 1 && key <= 26)
  {
    letter = 'A' + key - 1;
  }
  else if (key >= 'a' && key <= 'z')
  {
    letter = std::toupper((unsigned char)key);
  }
  else if (key >= 'A' && key <= 'Z')
  {
    letter = key;
  }

  const char *shift_fallback = std::getenv("JOT_KEYMAP_CTRL_SHIFT_FALLBACK");
  const bool use_shift_fallback = shift_fallback && shift_fallback[0] && shift_fallback[0] != '0';
  if (use_shift_fallback && is_ctrl && !is_alt && !is_shift && letter)
  {
    std::string shifted = "Ctrl+Shift+";
    shifted.push_back((char)letter);
    push_unique(std::move(shifted));
  }

  return candidates;
}

inline void log_keymap_debug(int ch,
                             bool is_ctrl,
                             bool is_shift,
                             bool is_alt,
                             int original_ch,
                             const std::vector<std::string> &candidates)
{
  const char *path = std::getenv("JOT_KEYMAP_DEBUG");
  if (!path || !*path)
  {
    return;
  }

  FILE *file = std::fopen(path, "a");
  if (!file)
  {
    return;
  }

  std::fprintf(file,
               "ch=%d original=%d ctrl=%d shift=%d alt=%d candidates=",
               ch,
               original_ch,
               is_ctrl ? 1 : 0,
               is_shift ? 1 : 0,
               is_alt ? 1 : 0);
  for (size_t i = 0; i < candidates.size(); ++i)
  {
    std::fprintf(file, "%s%s", i == 0 ? "" : ",", candidates[i].c_str());
  }
  std::fprintf(file, "\n");
  std::fclose(file);
}
} // namespace event_internal