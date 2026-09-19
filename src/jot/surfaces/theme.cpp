#include "editor.h"
#include "jot/lua/api.h"
#include "tools/string_util.h"
#include <algorithm>
#include <cctype>

namespace
{
  // jot ships two themes, both of them its own: `jot-dark` (warm charcoal,
  // amber keywords, teal functions) and `jot-light` (the same ink on warm
  // paper). The names they replaced -- `dark` and `light`, which the whole
  // bundled catalog used to be keyed off -- still resolve, so a config written
  // before the change keeps the colour scheme it asked for.
  const char *legacy_theme_alias(const std::string &name)
  {
    const std::string needle = string_util::lower_copy(name);
    if (needle == "dark" || needle == "jot-dark")
      return "jot-dark";
    if (needle == "light" || needle == "jot-light")
      return "jot-light";
    return nullptr;
  }
} // namespace

std::vector<std::string> Editor::list_available_themes()
{
  std::vector<std::string> themes;
  if (lua_api)
  {
    themes = lua_api->list_themes();
  }

  if (themes.empty())
  {
    themes.push_back("jot-dark");
    themes.push_back("jot-light");
    return themes;
  }

  std::sort(themes.begin(),
            themes.end(),
            [](const std::string &a, const std::string &b)
            { return string_util::lower_copy(a) < string_util::lower_copy(b); });

  auto unique_end = std::unique(themes.begin(),
                                themes.end(),
                                [](const std::string &a, const std::string &b)
                                { return string_util::lower_copy(a) == string_util::lower_copy(b); });
  themes.erase(unique_end, themes.end());
  return themes;
}

bool Editor::apply_theme(const std::string &name, bool persist, bool announce)
{
  const std::string requested = string_util::trim_copy(name);
  if (requested.empty())
  {
    set_message("Theme name is empty");
    return false;
  }

  std::string resolved = requested;
  const std::string needle = string_util::lower_copy(requested);
  const auto themes = list_available_themes();
  for (const auto &theme_name : themes)
  {
    if (string_util::lower_copy(theme_name) == needle)
    {
      resolved = theme_name;
      break;
    }
  }
  // The two names the removed catalog was keyed on resolve to the jot theme
  // that replaced them: a config written as `color_scheme = "light"` keeps
  // working, including on an install that still has the old file lying in its
  // data directory. A file the user wrote under that name is theirs, so their
  // theme wins and the alias is left alone.
  if (const char *alias = legacy_theme_alias(requested))
  {
    if (!lua_api || !lua_api->theme_file_is_user_owned(requested))
    {
      resolved = alias;
    }
  }

  if (!lua_api)
  {
    set_message("Lua theme runtime unavailable");
    return false;
  }

  Theme previous_theme = theme;
  const std::string previous_theme_name = current_theme_name;

  // Start from defaults, then let Python colorscheme override highlight groups.
  theme = Theme();
  if (!lua_api->apply_colorscheme(resolved))
  {
    theme = previous_theme;
    current_theme_name = previous_theme_name;
    set_message("Unknown theme: " + requested);
    return false;
  }
  theme.normalize_syntax_palette();

  current_theme_name = resolved;
  if (persist)
  {
    config.set("color_scheme", resolved);
    // Write the config file immediately so the choice survives the next
    // session (config.set only mutates the in-memory map).
    config.save();
  }

  if (ui)
  {
    ui->set_default_colors(theme.fg_default, theme.bg_default);
    ui->set_cursor_colors(theme.fg_cursor, theme.bg_cursor);
    ui->invalidate();
  }
  needs_redraw = true;
  if (announce)
  {
    set_message("Theme: " + resolved);
  }
  if (lua_api)
    lua_api->emit_theme_switched(resolved);
  return true;
}
