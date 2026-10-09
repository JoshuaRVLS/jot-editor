#include "config.h"
#include "base/string_util.h"
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
  // Rainbow brackets (render/bracket.cpp): each bracket is painted in a colour
  // that cycles with its nesting depth. Off leaves every bracket in its syntax
  // colour; the active bracket guide and the matching-bracket jump are
  // independent of this.
  settings["rainbow_brackets"] = "true";
  settings["auto_indent"] = "true";
  // Emmet abbreviations (extras/emmet/expand.cpp): Tab expands `div.box>p` in a
  // markup file and `m10-20` in a style sheet. Off is the escape hatch for code
  // that presses Tab at the end of a word that happens to parse.
  settings["emmet"] = "true";
  settings["smart_paste_indent"] = "true";
  // Trailing whitespace is stray blanks in most files, so a save drops it
  // (workspace/save_hygiene.cpp). Off is the escape hatch for a tree where every
  // such line is meant to stay as it is; a file where the spaces are content
  // (markdown's hard line break) is left alone either way.
  settings["trim_trailing_whitespace_on_save"] = "true";
  // The built-in reindent (workspace/save_hygiene.cpp): a save rebases a code
  // file's leading whitespace on `tab_size`, using the editor's own block rules
  // rather than an external formatter. Off is the escape hatch for a file whose
  // indentation is deliberately unusual; a language with no block rules here,
  // or a body inside a multi-line string, is left alone either way.
  settings["reindent_on_save"] = "true";
  // The Bible reader (extras/bible.h, extras/bible_client.cpp): which
  // translation and commentary `:bible` reads, the API root (pointed at a
  // fixture server by tests), and how many verses of context one verse keeps
  // on each side when a passage is shown verse by verse.
  settings["bible_translation"] = "BSB";
  settings["bible_commentary"] = "adam-clarke";
  settings["bible_api_base"] = "https://bible.helloao.org/api";
  settings["bible_context_verses"] = "2";
  settings["auto_save"] = "false";
  settings["auto_save_interval_ms"] = "2000";
  settings["prettier_on_save"] = "true";
  settings["clang_format_on_save"] = "true";
  settings["auto_detect_indent"] = "false";
  settings["show_line_numbers"] = "true";
  settings["relative_line_numbers"] = "false";
  // Statusline time chips (extras/status_clock.cpp): the local wall clock and
  // how long was coded today, in the workspace in front of the user. Both are
  // optional segments, so a narrow bar drops them before the diagnostics and git
  // chips. The coding chip is fed by the local store (extras/coding_time/probe.h)
  // and reports WakaTime's own total for today once that integration below is
  // on and has answered; the key keeps its historical name, and its label says
  // what it now shows.
  settings["status_clock"] = "true";
  settings["status_coding_time"] = "true";
  // WakaTime (extras/wakatime.h): the Extensions category's one integration so
  // far. Off by default, because it starts talking to an account and to
  // wakatime-cli. On, the editor sends heartbeats on saves and file changes and
  // the coding chip above reports the cli's total for today instead of the local
  // store's. The key is a setting rather than an environment variable so it is
  // visible and editable in :settings; left empty, wakatime-cli reads api_key
  // out of ~/.wakatime.cfg itself, so an existing WakaTime setup needs no key
  // here at all. wakatime_api_url points at a self-hosted or WakaTime-compatible
  // server. Turning it on with no cli on PATH installs one into ~/.wakatime
  // (extras/wakatime.h), which is why the default is off.
  settings["wakatime"] = "false";
  settings["wakatime_api_key"] = "";
  settings["wakatime_api_url"] = "";
  settings["word_wrap"] = "false";
  // Block by default: a bar reads as a thin sliver at the cell sizes a HiDPI
  // display or a large font produce. The resolvers below fall back to the same
  // value, so a config file that predates this key still gets a block.
  settings["cursor_style"] = "block";
  // Half of the blink cycle, in ms (cursor_blink.h drives it). 500 reads as a
  // calm ~1 Hz blink; the old 300 was a jumpy 1.7 Hz. 0 disables blinking.
  settings["cursor_blink_ms"] = "500";
  // The AI assistant (features/ai/*.lua): which adapter provides the model and
  // how the request is shaped. The token is never a setting - it is read from
  // the environment variable ai_key_env names, which defaults to the adapter's
  // own (OPENAI_API_KEY and so on). Empty ai_model / ai_base_url / ai_command
  // fall back to the adapter entry, and an empty ai_system_prompt to the
  // bundled one; ai_max_tokens of 0 sends no limit (Anthropic requires one and
  // gets 4096).
  settings["ai_adapter"] = "openai";
  settings["ai_model"] = "";
  settings["ai_base_url"] = "";
  settings["ai_key_env"] = "";
  settings["ai_command"] = "";
  settings["ai_system_prompt"] = "";
  settings["ai_stream"] = "true";
  settings["ai_temperature"] = "0.2";
  settings["ai_max_tokens"] = "0";
  // Inline colour preview (syntax/color_codes.cpp): paint colour literals in
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
  // Filetypes the preview runs in, as file-name suffixes. A hex literal in a
  // C++ comment or an identifier like `red` in prose is not a swatch anyone
  // asked for, so the default is the web-authoring set; an empty value
  // restores the old everywhere behaviour.
  settings["colorizer_filetypes"] =
      ".html,.htm,.jsx,.tsx,.css,.scss,.sass,.less,.mdx,.vue,.svelte,.astro,.php";
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
  // Shape the punctuation runs the font joins ("->", "!=" ...) in the GUI.
  // Terminal frontends do this themselves, so this is only about jot's own
  // window; needs harfbuzz at build time (UIGui::ligatures_supported()).
  settings["gui_ligatures"] = "true";
  settings["highlight_cursor_line"] = "true";
  // Tint every place the identifier under the caret shows in the viewport, VS
  // Code's occurrencesHighlight: a rename's blast radius without a search.
  settings["word_highlight"] = "true";
  // How long the typing has to pause before that tint shows again. A highlight
  // that re-lights on every keystroke's repaint is noise while the hands are
  // still on the keys (VS Code's occurrencesHighlightDelay); the tint waits for
  // the pause instead. 0 paints it at once (the old behaviour).
  settings["word_highlight_delay_ms"] = "500";
  settings["render_fps"] = "120";
  settings["idle_fps"] = "60";
  // Smooth scrolling (ui/smooth_scroll.cpp, neoscroll.nvim's model): the
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
  // How long the typing has to pause before a server's fresh findings paint.
  // While it is typing, the squiggle, the row band and the inline message
  // appearing under the hands on every publish is the noise this holds back;
  // the server still analyses every change, and a save paints at once. 0 shows
  // them the moment they arrive.
  settings["lsp_diagnostics_quiet_ms"] = "700";
  settings["lsp_completion_max_items"] = "8";
  settings["lsp_completion_nerd_icons"] = "true";
  settings["lsp_completion_ghost_text"] = "true";
  // How long the typing has to pause before that preview shows. A preview that
  // redraws with every keystroke is a flicker; 0 previews at once.
  settings["lsp_completion_ghost_delay_ms"] = "150";
  // Rank the popup by the names this user accepts and the words the file already
  // holds (lsp/completion_rank.h). Off means the plain order, and nothing is
  // read from or written to the config home.
  settings["lsp_completion_learn"] = "true";
  // How fast an accepted name fades: its count halves every half-life, so a habit
  // from last month stops outranking what the user is reaching for now. 0 keeps
  // every count forever (the old behaviour).
  settings["lsp_completion_learn_half_life_days"] = "14";
  settings["lsp_inlay_hints"] = "true";
  settings["lsp_inlay_type_hints"] = "true";
  settings["decorations_inline_diagnostics"] = "true";
  // End-of-line diagnostic message next to the squiggle (features/decorations.lua).
  settings["diagnostics_virtual_text"] = "true";
  // TODO/FIXME comment bands plus the jump and workspace-list commands
  // (features/todo_comments.lua). Extra keywords are comma separated and join
  // the default family (upstream's `merge_keywords`).
  settings["todo_comments"] = "true";
  settings["todo_comments_keywords"] = "";
  // Dim C/C++ branches the preprocessor would skip, and the symbols that
  // count as defined when deciding (features/cpp_inactive.lua).
  settings["cpp_dim_inactive"] = "true";
  settings["cpp_defined_macros"] = "";
  // Check the workspace's C++ for declarations with no implementation and
  // signatures with more than one: a workspace scan per open and per save,
  // published as diagnostics (syntax/cpp_definitions.*).
  settings["cpp_definitions"] = "true";
  // jot's dark theme ("amber lamp": the #14100b ground and the #ede4d1 ink cut
  // from the amber's own hue, amber keywords and one teal complement) is what a
  // fresh install opens in; jot-light ("amber paper") is its daylight read.
  settings["color_scheme"] = "jot-dark";
  // The workspace tab strip (row 0): shown by default, hidden once there are
  // this many buffers or fewer (0 = never hide), and where a buffer the strip
  // has not seen before lands in the order ("after_current", "start", "end").
  settings["tabline"] = "true";
  settings["tabline_auto_hide"] = "0";
  settings["tabline_insert"] = "after_current";
  // The breadcrumb winbar: the row a pane spends above its text, with the
  // symbol chain of the cursor's line on it. "auto" keeps it for code files
  // ("on" shows it for every named file, "off" never).
  settings["winbar"] = "auto";
  // The pane's own tab strip (a VSCode editor group, painted on the pane's
  // first row): "auto" spends that row only in a split or with `tabline` off,
  // so a lone pane keeps the layout it had and never paints its list twice;
  // "always" / "off" force the row on or off.
  settings["pane_tabs"] = "auto";
  settings["right_panel_width"] = "42";
  settings["zen_content_width"] = "100";
  settings["terminal_height"] = "10";
  // The floating terminal (Alt+Shift+T, :termfloat): a box centered over the
  // active pane's text rows, sized as a percentage of that area (100 = all of
  // it), clamped to the area and to a 20x5 floor
  // (integrations/terminal_float.cpp).
  settings["terminal_float_width"] = "85";
  settings["terminal_float_height"] = "75";
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
