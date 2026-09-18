#include "config.h"
#include "tools/string_util.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace
{
  fs::path config_root_path()
  {
    const char *override_home = getenv("JOT_CONFIG_HOME");
    if (override_home && *override_home)
    {
      return fs::path(override_home);
    }
#ifdef _WIN32
    const char *app_data = getenv("APPDATA");
    if (app_data && *app_data)
    {
      return fs::path(app_data) / "jot";
    }
    const char *profile = getenv("USERPROFILE");
    if (profile && *profile)
    {
      return fs::path(profile) / ".config" / "jot";
    }
#else
    const char *home = getenv("HOME");
    if (home && *home)
    {
      return fs::path(home) / ".config" / "jot";
    }
#endif
    return {};
  }

  std::string strip_inline_comment(const std::string &line)
  {
    bool in_single = false;
    bool in_double = false;
    for (size_t i = 0; i < line.size(); i++)
    {
      char c = line[i];
      if (c == '"' && !in_single)
      {
        in_double = !in_double;
        continue;
      }
      if (c == '\'' && !in_double)
      {
        in_single = !in_single;
        continue;
      }
      if (!in_single && !in_double && c == '#')
      {
        return line.substr(0, i);
      }
    }
    return line;
  }
} // namespace

Config::Config()
{
  fs::path config_root = config_root_path();
  if (!config_root.empty())
  {
    fs::create_directories(config_root / "configs");
    config_path = (config_root / "configs" / "settings.conf").string();
  }
  load_defaults();
}

void Config::load_defaults()
{
  settings["explorer_width"] = "25";
  settings["minimap_width"] = "15";
  settings["show_explorer"] = "true";
  settings["show_minimap"] = "true";
  // Columns left unpainted on the right edge. 0 (the default) uses the full
  // width; raise it only if a terminal corrupts the frame when its last column
  // is written. See Terminal::render_margin_.
  settings["render_margin"] = "0";
  settings["tab_size"] = "2";
  settings["show_indent_guides"] = "true";
  settings["auto_indent"] = "true";
  settings["smart_paste_indent"] = "true";
  settings["auto_save"] = "false";
  settings["auto_save_interval_ms"] = "2000";
  settings["prettier_on_save"] = "true";
  settings["clang_format_on_save"] = "true";
  settings["auto_detect_indent"] = "false";
  settings["show_line_numbers"] = "true";
  settings["relative_line_numbers"] = "false";
  settings["word_wrap"] = "false";
  // Block by default: a bar reads as a thin sliver at the cell sizes a HiDPI
  // display or a large font produce. The resolvers below fall back to the same
  // value, so a config file that predates this key still gets a block.
  settings["cursor_style"] = "block";
  // Half of the blink cycle, in ms (cursor_blink.h drives it). 500 reads as a
  // calm ~1 Hz blink; the old 300 was a jumpy 1.7 Hz. 0 disables blinking.
  settings["cursor_blink_ms"] = "500";
  // Inline colour preview (features/color_codes.cpp): paint colour literals in
  // the colour they name. mode is background | foreground | virtualtext.
  settings["colorizer"] = "true";
  settings["colorizer_mode"] = "background";
  settings["colorizer_hex"] = "true";          // #RGB / #RGBA / #RRGGBB
  settings["colorizer_hex_alpha"] = "false";   // #RRGGBBAA
  settings["colorizer_hex_qml"] = "false";     // #AARRGGBB (QML/Android order)
  settings["colorizer_hex_no_hash"] = "false"; // RRGGBB with no "#"
  settings["colorizer_hex_0x"] = "false";      // 0xRGB / 0xRRGGBB / 0xAARRGGBB
  settings["colorizer_names"] = "true";        // CSS names, e.g. red / LightBlue
  settings["colorizer_tailwind"] = "false";    // text-orange-500, bg-slate-50
  settings["colorizer_xcolor"] = "false";      // LaTeX xcolor, e.g. red!30
  // rgb()/rgba()/hsl()/hsla()/hwb()/lab()/lch()/oklch()/hsluv()/color().
  settings["colorizer_functions"] = "true";
  settings["colorizer_xterm"] = "false";     // #xNN and ANSI SGR escapes
  settings["colorizer_ls_colors"] = "false"; // LS_COLORS/SGR snippets, =38;5;196
  settings["colorizer_css_vars"] = "false";  // --name: value and var(--name)
  settings["colorizer_sass"] = "false";      // $name: value and $name references
  // Restrict matches to string/comment bytes (off = every byte, like upstream).
  settings["colorizer_only_in_strings"] = "false";
  // Extensions to leave alone, e.g. ".min.css,.map".
  settings["colorizer_exclude_filetypes"] = "";
  // LSP completion rows: split the label into name / arguments / type and
  // right-align the type (see features/ui/completion_label.lua).
  settings["completion_rich_labels"] = "true";
  settings["completion_align_type"] = "true";
  settings["completion_dim_arguments"] = "true";
  // 24-bit colour output: auto detects from COLORTERM/TERM, on/off force it.
  settings["truecolor"] = "auto";
  settings["gui_font_size"] = "16";
  // Empty keeps the built-in font; any installed family name switches the
  // GUI to it (:font lists what is available).
  settings["gui_font_family"] = "";
  settings["highlight_cursor_line"] = "true";
  settings["render_fps"] = "120";
  settings["idle_fps"] = "60";
  // Smooth scrolling (features/smooth_scroll.cpp, neoscroll.nvim's model): the
  // wheel eases the viewport over a few frames instead of jumping a notch per
  // event. Off by default -- it changes how every wheel event feels, so it is
  // opted into rather than out of. `smooth_scroll_easing` takes upstream's
  // easing names (linear, quadratic, cubic, quartic, quintic, circular, sine)
  // and `smooth_scroll_duration_multiplier` is upstream's duration_multiplier:
  // the per-notch duration is 100ms times it. When it is on, the defaults for
  // those two are upstream's own, so "linear" and 1.0 are its motion.
  settings["smooth_scroll"] = "false";
  settings["smooth_scroll_easing"] = "linear";
  settings["smooth_scroll_duration_multiplier"] = "1.0";
  settings["lsp_change_debounce_ms"] = "120";
  settings["lsp_completion_max_items"] = "8";
  settings["lsp_completion_nerd_icons"] = "true";
  settings["lsp_completion_ghost_text"] = "true";
  settings["lsp_inlay_hints"] = "true";
  settings["lsp_inlay_type_hints"] = "true";
  settings["decorations_inline_diagnostics"] = "true";
  // End-of-line diagnostic message next to the squiggle (features/decorations.lua).
  settings["diagnostics_virtual_text"] = "true";
  // Dim C/C++ branches the preprocessor would skip, and the symbols that
  // count as defined when deciding (features/cpp_inactive.lua).
  settings["cpp_dim_inactive"] = "true";
  settings["cpp_defined_macros"] = "";
  settings["color_scheme"] = "dark";
  settings["right_panel_width"] = "42";
  settings["zen_content_width"] = "100";
  settings["terminal_height"] = "10";
  settings["debugger_height"] = "12";
  // Discord Rich Presence (ported from iCrawl/discord-vscode; the template
  // placeholders are documented in packaging/discord-presence/ASSETS.md).
  // On by default, like the extension's "enabled": true, and toggleable live
  // from :settings or :discord enable/disable.
  settings["discord_rpc"] = "true";
  settings["discord_app_id"] = "1513610110256021524";
  settings["discord_details_idling"] = "Idling";
  settings["discord_details_editing"] = "Editing {file_name}";
  settings["discord_details_debugging"] = "Debugging {file_name}";
  settings["discord_lower_details_idling"] = "Idling";
  settings["discord_lower_details_editing"] = "Workspace: {workspace}";
  settings["discord_lower_details_debugging"] = "Debugging: {workspace}";
  settings["discord_lower_details_no_workspace"] = "No workspace";
  settings["discord_large_image"] = "Editing a {LANG} file";
  settings["discord_large_image_idling"] = "Idling";
  settings["discord_small_image"] = "{app_name}";
  settings["discord_app_image"] = "jot";
  settings["discord_idle_image"] = "jot";
  settings["discord_debug_image"] = "debug";
  settings["discord_swap_images"] = "false";
  settings["discord_remove_details"] = "false";
  settings["discord_remove_lower_details"] = "false";
  settings["discord_remove_timestamp"] = "false";
  settings["discord_remove_repository_button"] = "false";
  settings["discord_idle_timeout"] = "0";
  settings["discord_show_status"] = "true";
  settings["discord_exclude_workspaces"] = "";
  settings["image_viewer_backend"] = "auto";
  settings["treesitter_library_paths"] = "";
  settings["treesitter_query_paths"] = "";
  settings["treesitter_language_overrides"] = "";
  // Lua feature modules read these with fallback defaults; registering them
  // here surfaces them in the :settings menu (and settings.conf) so they are
  // discoverable and editable like any built-in setting.
  settings["toast.duration_ms"] = "3000";
  settings["toast.fade_ms"] = "250";
  settings["toast.gap"] = "0";
  settings["toast.margin"] = "1";
  settings["toast.max_visible"] = "5";
  settings["toast.max_width"] = "56";
  settings["update.build_dir"] = "";
  settings["update.check_on_startup"] = "true";
}

void Config::parse_line(const std::string &line)
{
  std::string normalized = strip_inline_comment(line);
  normalized = string_util::trim_copy(normalized);
  if (normalized.empty())
  {
    return;
  }

  size_t eq = normalized.find('=');
  if (eq == std::string::npos)
    return;

  std::string key = string_util::trim_copy(normalized.substr(0, eq));
  std::string value = string_util::trim_copy(normalized.substr(eq + 1));

  if (value.size() >= 2
      && ((value.front() == '"' && value.back() == '"')
          || (value.front() == '\'' && value.back() == '\'')))
  {
    value = value.substr(1, value.size() - 2);
  }

  if (!key.empty())
  {
    settings[key] = value;
  }
}

void Config::load()
{
  if (config_path.empty())
    return;

  std::ifstream file(config_path);
  if (!file.is_open())
  {
    fs::path root = config_root_path();
    if (!root.empty())
    {
      file.open(root / "config");
    }
  }
  if (!file.is_open())
  {
    save();
    return;
  }

  std::string line;
  while (std::getline(file, line))
  {
    parse_line(line);
  }
  file.close();
}

void Config::save()
{
  if (config_path.empty())
    return;

  std::ofstream file(config_path);
  if (!file.is_open())
    return;

  file << "# jot configuration file\n\n";

  for (const auto &[key, value] : settings)
  {
    file << key << "=" << value << "\n";
  }

  file.close();
}

std::string Config::get(const std::string &key, const std::string &default_val)
{
  auto it = settings.find(key);
  return (it != settings.end()) ? it->second : default_val;
}

void Config::set(const std::string &key, const std::string &value)
{
  settings[key] = value;
}

void Config::set_int(const std::string &key, int value)
{
  settings[key] = std::to_string(value);
}

void Config::set_bool(const std::string &key, bool value)
{
  settings[key] = value ? "true" : "false";
}

int Config::get_int(const std::string &key, int default_val) const
{
  auto it = settings.find(key);
  if (it == settings.end())
    return default_val;
  try
  {
    return std::stoi(it->second);
  }
  catch (...)
  {
    return default_val;
  }
}

double Config::get_double(const std::string &key, double default_val)
{
  auto it = settings.find(key);
  if (it == settings.end())
  {
    return default_val;
  }
  try
  {
    return std::stod(it->second);
  }
  catch (...)
  {
    return default_val;
  }
}

bool Config::get_bool(const std::string &key, bool default_val)
{
  auto it = settings.find(key);
  if (it == settings.end())
    return default_val;
  std::string val = it->second;
  std::transform(
      val.begin(), val.end(), val.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  if (val == "true" || val == "1" || val == "yes" || val == "on")
  {
    return true;
  }
  if (val == "false" || val == "0" || val == "no" || val == "off")
  {
    return false;
  }
  return default_val;
}

std::vector<std::string> Config::get_list(const std::string &key, char delimiter, bool trim_items)
{
  auto it = settings.find(key);
  if (it == settings.end() || it->second.empty())
  {
    return {};
  }

  std::vector<std::string> out;
  std::stringstream ss(it->second);
  std::string item;
  while (std::getline(ss, item, delimiter))
  {
    if (trim_items)
    {
      item = string_util::trim_copy(item);
    }
    if (!item.empty())
    {
      out.push_back(item);
    }
  }
  return out;
}

bool Config::has(const std::string &key) const
{
  return settings.find(key) != settings.end();
}

void Config::unset(const std::string &key)
{
  settings.erase(key);
}

std::vector<std::string> Config::keys() const
{
  std::vector<std::string> out;
  out.reserve(settings.size());
  for (const auto &[key, value] : settings)
  {
    (void)value;
    out.push_back(key);
  }
  return out;
}
