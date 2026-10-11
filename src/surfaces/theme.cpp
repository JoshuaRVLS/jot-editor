#include "base/string_util.h"
#include "core/editor.h"
#include "lua/api/api.h"
#include <algorithm>
#include <cctype>

namespace
{
  // jot ships `jot` -- a port of gruber-darker (John Gruber's scheme, via
  // https://github.com/blazkowolf/gruber-darker.nvim: the #181818 ground, the
  // #e4e4e4 ink, yellow keywords and niagara-blue functions) -- plus the flexoki
  // pair ported from kepano's Flexoki palette (flexoki-dark / flexoki-light,
  // MIT licensed, https://stephango.com/flexoki). Every name the bundled catalog
  // has been keyed on before -- `dark` and `light`, and the `jot-dark` /
  // `jot-light` pair that replaced them -- resolves to `jot`, so a config
  // written before the change keeps the colour scheme it asked for.
  const char *legacy_theme_alias(const std::string &name)
  {
    const std::string needle = string_util::lower_copy(name);
    if (needle == "dark" || needle == "light" || needle == "jot-dark" || needle == "jot-light")
      return "jot";
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
    // No Lua runtime to read the bundled files with: still answer with every
    // scheme the install ships. `apply_theme` reports the failure itself.
    themes.push_back("flexoki-dark");
    themes.push_back("flexoki-light");
    themes.push_back("jot");
    return themes;
  }

  std::sort(themes.begin(),
            themes.end(),
            [](const std::string &a, const std::string &b)
            { return string_util::lower_copy(a) < string_util::lower_copy(b); });

  auto unique_end =
      std::unique(themes.begin(),
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
  // The names the removed catalog was keyed on resolve to the jot theme that
  // replaced them: a config written as `color_scheme = "jot-dark"` or
  // `color_scheme = "light"` keeps working, including on an install that still
  // has an old file lying in its data directory. A file the user wrote under
  // that name is theirs, so their theme wins and the alias is left alone.
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
