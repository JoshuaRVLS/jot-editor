// Settings menu model + input (cell-based; see render/settings.cpp for the
// paint pass). The menu enumerates config.keys(), so every setting -- the
// built-in defaults, settings.conf overrides and Lua-registered keys from
// jot.config.set -- appears with its current value. Booleans toggle on
// Enter / Left / Right; integers and strings open an inline input row
// (type the new value, Enter applies, Esc cancels). Changes flow through
// apply_settings_value -> config.set + apply_config_live + save, the same
// live-apply pipeline Lua uses.
#include "editor.h"
#include "ui/gui/gui.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace
{
// Friendly labels + types for the built-in settings. Keys absent from this
// table (Lua-registered, plugin-owned) fall back to a string entry using
// the raw key as the label, so the menu stays complete and Lua-extensible.
struct KnownSetting
{
  const char *key;
  const char *label;
  SettingsEntry::Type type;
};

const KnownSetting kKnownSettings[] = {
    {"auto_detect_indent", "Auto-detect indent", SettingsEntry::Type::Bool},
    {"auto_indent", "Auto indent", SettingsEntry::Type::Bool},
    {"auto_save", "Auto save", SettingsEntry::Type::Bool},
    {"auto_save_interval_ms", "Auto save interval (ms)", SettingsEntry::Type::Int},
    {"clang_format_on_save", "Clang-format on save", SettingsEntry::Type::Bool},
    {"color_scheme", "Color scheme", SettingsEntry::Type::String},
    {"colorizer", "Color preview", SettingsEntry::Type::Bool},
    {"colorizer_mode", "Color preview mode", SettingsEntry::Type::String},
    {"colorizer_hex", "Color preview: hex", SettingsEntry::Type::Bool},
    {"colorizer_hex_alpha", "Color preview: 8-digit hex", SettingsEntry::Type::Bool},
    {"colorizer_hex_qml", "Color preview: #AARRGGBB", SettingsEntry::Type::Bool},
    {"colorizer_hex_no_hash", "Color preview: hex without #", SettingsEntry::Type::Bool},
    {"colorizer_hex_0x", "Color preview: 0x hex", SettingsEntry::Type::Bool},
    {"colorizer_names", "Color preview: named", SettingsEntry::Type::Bool},
    {"colorizer_tailwind", "Color preview: Tailwind", SettingsEntry::Type::Bool},
    {"colorizer_xcolor", "Color preview: xcolor", SettingsEntry::Type::Bool},
    {"colorizer_functions", "Color preview: CSS functions", SettingsEntry::Type::Bool},
    {"colorizer_xterm", "Color preview: terminal codes", SettingsEntry::Type::Bool},
    {"colorizer_ls_colors", "Color preview: LS_COLORS", SettingsEntry::Type::Bool},
    {"colorizer_css_vars", "Color preview: CSS variables", SettingsEntry::Type::Bool},
    {"colorizer_sass", "Color preview: Sass variables", SettingsEntry::Type::Bool},
    {"colorizer_only_in_strings", "Color preview: strings only", SettingsEntry::Type::Bool},
    {"colorizer_exclude_filetypes", "Color preview: skip extensions", SettingsEntry::Type::String},
    {"truecolor", "24-bit color (auto/on/off)", SettingsEntry::Type::String},
    {"completion_rich_labels", "Completion: rich labels", SettingsEntry::Type::Bool},
    {"completion_align_type", "Completion: align type", SettingsEntry::Type::Bool},
    {"completion_dim_arguments", "Completion: dim arguments", SettingsEntry::Type::Bool},
    {"cursor_blink_ms", "Cursor blink (ms)", SettingsEntry::Type::Int},
    {"cursor_style", "Cursor style", SettingsEntry::Type::String},
    {"debugger_height", "Debugger panel height", SettingsEntry::Type::Int},
    {"decorations_inline_diagnostics", "Inline diagnostics", SettingsEntry::Type::Bool},
    {"diagnostics_virtual_text", "Diagnostic messages inline", SettingsEntry::Type::Bool},
    {"cpp_dim_inactive", "Dim inactive #ifdef branches", SettingsEntry::Type::Bool},
    {"discord_rpc", "Discord presence", SettingsEntry::Type::Bool},
    {"discord_app_id", "Discord app id", SettingsEntry::Type::String},
    {"discord_details_editing", "Discord details (editing)", SettingsEntry::Type::String},
    {"discord_details_idling", "Discord details (idling)", SettingsEntry::Type::String},
    {"discord_details_debugging", "Discord details (debugging)", SettingsEntry::Type::String},
    {"discord_lower_details_editing", "Discord state (editing)", SettingsEntry::Type::String},
    {"discord_lower_details_idling", "Discord state (idling)", SettingsEntry::Type::String},
    {"discord_large_image", "Discord large image text", SettingsEntry::Type::String},
    {"discord_large_image_idling",
     "Discord large image text (idling)",
     SettingsEntry::Type::String},
    {"discord_small_image", "Discord small image text", SettingsEntry::Type::String},
    {"discord_idle_timeout", "Discord idle timeout (s)", SettingsEntry::Type::Int},
    {"discord_swap_images", "Discord swap images", SettingsEntry::Type::Bool},
    {"discord_remove_details", "Discord hide details", SettingsEntry::Type::Bool},
    {"discord_remove_lower_details", "Discord hide state", SettingsEntry::Type::Bool},
    {"discord_remove_timestamp", "Discord hide elapsed time", SettingsEntry::Type::Bool},
    {"discord_remove_repository_button", "Discord hide repo button", SettingsEntry::Type::Bool},
    {"discord_show_status", "Discord status chip", SettingsEntry::Type::Bool},
    {"explorer_width", "Explorer width", SettingsEntry::Type::Int},
    {"gui_font_family", "GUI font family", SettingsEntry::Type::String},
    {"gui_font_size", "GUI font size (px)", SettingsEntry::Type::Int},
    {"highlight_cursor_line", "Highlight cursor line", SettingsEntry::Type::Bool},
    {"idle_fps", "Idle FPS", SettingsEntry::Type::Int},
    {"image_viewer_backend", "Image viewer backend", SettingsEntry::Type::String},
    {"lsp_change_debounce_ms", "LSP change debounce (ms)", SettingsEntry::Type::Int},
    {"lsp_completion_ghost_text", "LSP ghost text", SettingsEntry::Type::Bool},
    {"lsp_completion_max_items", "LSP completion max items", SettingsEntry::Type::Int},
    {"lsp_completion_nerd_icons", "LSP completion icons", SettingsEntry::Type::Bool},
    {"lsp_inlay_hints", "LSP parameter hints", SettingsEntry::Type::Bool},
    {"lsp_inlay_type_hints", "LSP type hints", SettingsEntry::Type::Bool},
    {"markdown_preview_auto_close", "Markdown preview: auto close", SettingsEntry::Type::Bool},
    {"markdown_preview_auto_start", "Markdown preview: auto start", SettingsEntry::Type::Bool},
    {"markdown_preview_browser", "Markdown preview: browser", SettingsEntry::Type::String},
    {"markdown_preview_custom_css", "Markdown preview: custom CSS", SettingsEntry::Type::String},
    {"markdown_preview_echo_preview_url",
     "Markdown preview: echo URL",
     SettingsEntry::Type::Bool},
    {"markdown_preview_host", "Markdown preview: host", SettingsEntry::Type::String},
    {"markdown_preview_images_path", "Markdown preview: images path", SettingsEntry::Type::String},
    {"markdown_preview_markdown_ext", "Markdown preview: extensions", SettingsEntry::Type::String},
    {"markdown_preview_open_timeout_ms",
     "Markdown preview: open timeout (ms)",
     SettingsEntry::Type::Int},
    {"markdown_preview_option_code_copy", "Markdown preview: code copy", SettingsEntry::Type::Bool},
    {"markdown_preview_option_echarts", "Markdown preview: ECharts", SettingsEntry::Type::Bool},
    {"markdown_preview_option_emoji", "Markdown preview: emoji", SettingsEntry::Type::Bool},
    {"markdown_preview_option_flowchart", "Markdown preview: flowcharts", SettingsEntry::Type::Bool},
    {"markdown_preview_option_highlightjs",
     "Markdown preview: highlight.js",
     SettingsEntry::Type::Bool},
    {"markdown_preview_option_katex", "Markdown preview: KaTeX math", SettingsEntry::Type::Bool},
    {"markdown_preview_option_mermaid", "Markdown preview: Mermaid", SettingsEntry::Type::Bool},
    {"markdown_preview_option_plantuml", "Markdown preview: PlantUML", SettingsEntry::Type::Bool},
    {"markdown_preview_option_source_map",
     "Markdown preview: source map",
     SettingsEntry::Type::Bool},
    {"markdown_preview_option_toc", "Markdown preview: TOC panel", SettingsEntry::Type::Bool},
    {"markdown_preview_option_vega", "Markdown preview: Vega charts", SettingsEntry::Type::Bool},
    {"markdown_preview_page_title", "Markdown preview: page title", SettingsEntry::Type::String},
    {"markdown_preview_port", "Markdown preview: port", SettingsEntry::Type::Int},
    {"markdown_preview_refresh_interval",
     "Markdown preview: refresh (ms)",
     SettingsEntry::Type::Int},
    {"markdown_preview_theme", "Markdown preview: theme", SettingsEntry::Type::String},
    {"minimap_width", "Minimap width", SettingsEntry::Type::Int},
    {"prettier_on_save", "Prettier on save", SettingsEntry::Type::Bool},
    {"relative_line_numbers", "Relative line numbers", SettingsEntry::Type::Bool},
    {"render_fps", "Render FPS", SettingsEntry::Type::Int},
    {"right_panel_width", "Right panel width", SettingsEntry::Type::Int},
    {"show_explorer", "Show explorer", SettingsEntry::Type::Bool},
    {"show_indent_guides", "Indent guides", SettingsEntry::Type::Bool},
    {"show_line_numbers", "Line numbers", SettingsEntry::Type::Bool},
    {"show_minimap", "Show minimap", SettingsEntry::Type::Bool},
    {"smart_paste_indent", "Smart paste indent", SettingsEntry::Type::Bool},
    {"smooth_scroll", "Smooth scrolling", SettingsEntry::Type::Bool},
    {"smooth_scroll_duration_multiplier",
     "Smooth scrolling: duration multiplier",
     SettingsEntry::Type::String},
    {"smooth_scroll_easing", "Smooth scrolling: easing", SettingsEntry::Type::String},
    {"snippet_auto_expand", "Snippets: auto expand", SettingsEntry::Type::Bool},
    {"snippet_backtab_key", "Snippets: jump back key", SettingsEntry::Type::String},
    {"snippet_choice_next_key", "Snippets: next choice key", SettingsEntry::Type::String},
    {"snippet_choice_prev_key", "Snippets: previous choice key", SettingsEntry::Type::String},
    {"snippet_enabled", "Snippets: enabled", SettingsEntry::Type::Bool},
    {"snippet_filetypes", "Snippets: extension overrides", SettingsEntry::Type::String},
    {"snippet_highlight", "Snippets: highlight placeholders", SettingsEntry::Type::Bool},
    {"snippet_history", "Snippets: remember history", SettingsEntry::Type::Bool},
    {"snippet_history_size", "Snippets: history size", SettingsEntry::Type::Int},
    {"snippet_load_snipmate", "Snippets: load snipMate packs", SettingsEntry::Type::Bool},
    {"snippet_load_vscode", "Snippets: load VSCode packs", SettingsEntry::Type::Bool},
    {"snippet_paths", "Snippets: extra paths", SettingsEntry::Type::String},
    {"snippet_tab_key", "Snippets: expand/jump key", SettingsEntry::Type::String},
    {"tab_size", "Tab size", SettingsEntry::Type::Int},
    {"terminal_height", "Terminal panel height", SettingsEntry::Type::Int},
    {"toast.duration_ms", "Toast duration (ms)", SettingsEntry::Type::Int},
    {"toast.fade_ms", "Toast fade (ms)", SettingsEntry::Type::Int},
    {"toast.gap", "Toast gap", SettingsEntry::Type::Int},
    {"toast.margin", "Toast margin", SettingsEntry::Type::Int},
    {"toast.max_visible", "Toast max visible", SettingsEntry::Type::Int},
    {"toast.max_width", "Toast max width", SettingsEntry::Type::Int},
    {"treesitter_language_overrides",
     "Tree-sitter language overrides",
     SettingsEntry::Type::String},
    {"treesitter_library_paths", "Tree-sitter library paths", SettingsEntry::Type::String},
    {"treesitter_query_paths", "Tree-sitter query paths", SettingsEntry::Type::String},
    {"update.build_dir", "Update build dir", SettingsEntry::Type::String},
    {"update.check_on_startup", "Check updates on startup", SettingsEntry::Type::Bool},
    {"word_wrap", "Word wrap", SettingsEntry::Type::Bool},
    {"zen_content_width", "Zen content width", SettingsEntry::Type::Int},
};

SettingsEntry::Type infer_type(const std::string &key, const std::string &value)
{
  for (const KnownSetting &k : kKnownSettings)
  {
    if (key == k.key)
      return k.type;
  }
  // Unknown (Lua-registered) keys: infer from the stored value so the menu
  // still toggles booleans set from Lua.
  if (value == "true" || value == "false")
    return SettingsEntry::Type::Bool;
  if (!value.empty())
  {
    bool numeric = true;
    for (char c : value)
    {
      if (!std::isdigit((unsigned char)c) && c != '-' && c != '.')
      {
        numeric = false;
        break;
      }
    }
    if (numeric)
      return SettingsEntry::Type::Int;
  }
  return SettingsEntry::Type::String;
}
} // namespace

void Editor::rebuild_settings_entries()
{
  settings_entries.clear();
  for (const std::string &key : config.keys())
  {
    const std::string value = config.get(key, "");
    SettingsEntry e;
    e.key = key;
    e.type = infer_type(key, value);
    e.value = value;
    e.label = key; // fallback label; replaced below when known
    for (const KnownSetting &k : kKnownSettings)
    {
      if (key == k.key)
      {
        e.label = k.label;
        break;
      }
    }
    settings_entries.push_back(std::move(e));
  }
  settings_selected = std::clamp(settings_selected, 0,
                                 std::max(0, (int)settings_entries.size() - 1));
  needs_redraw = true;
}

void Editor::toggle_settings_menu()
{
  if (show_settings_menu)
  {
    close_settings_menu();
    return;
  }
  rebuild_settings_entries();
  show_settings_menu = true;
  needs_redraw = true;
}

void Editor::close_settings_menu()
{
  show_settings_menu = false;
  settings_entries.clear();
  settings_selected = 0;
  settings_scroll = 0;
  needs_redraw = true;
}

void Editor::apply_settings_value(const std::string &key, const std::string &value)
{
  config.set(key, value);
  // Font changes need the GUI's live re-fit (same path Ctrl+= uses);
  // everything else applies through the shared live-config pipeline.
  if (key == "gui_font_size")
  {
#ifdef JOT_GUI
    if (auto *gui = gui_ui())
    {
      gui->apply_font_size(std::clamp(config.get_int("gui_font_size", 16), 8, 40));
    }
#endif
  }
  else if (key == "gui_font_family")
  {
    // One place handles rejecting an unknown name, and writes back the family
    // that actually took effect so the file cannot keep a name that resolves
    // to nothing.
    apply_gui_font_family(config.get("gui_font_family", ""));
  }
  apply_config_live();
  config.save();
  // Keep the menu's model in sync with the applied value.
  for (SettingsEntry &e : settings_entries)
  {
    if (e.key == key)
    {
      e.value = config.get(key, "");
      e.editing = false;
      break;
    }
  }
  needs_redraw = true;
}

bool Editor::handle_settings_input(int ch)
{
  if (!show_settings_menu)
    return false;

  if (settings_entries.empty())
  {
    if (ch == 27)
      close_settings_menu();
    return true;
  }

  SettingsEntry &cur = settings_entries[(size_t)std::clamp(
      settings_selected, 0, (int)settings_entries.size() - 1)];

  // Esc: cancel an in-progress edit first, then close the menu.
  if (ch == 27)
  {
    if (cur.editing)
    {
      cur.editing = false;
      needs_redraw = true;
      return true;
    }
    close_settings_menu();
    return true;
  }

  if (cur.editing)
  {
    if (ch == '\n' || ch == 13)
    {
      // Validate ints: empty / non-numeric input cancels the edit.
      if (cur.type == SettingsEntry::Type::Int)
      {
        const std::string &v = cur.edit_input;
        bool ok = !v.empty();
        for (char c : v)
        {
          if (!std::isdigit((unsigned char)c) && c != '-')
          {
            ok = false;
            break;
          }
        }
        if (!ok)
        {
          cur.editing = false;
          needs_redraw = true;
          return true;
        }
      }
      else if (cur.edit_input.empty())
      {
        // Empty string input: treat as cancel (no meaningful change).
        cur.editing = false;
        needs_redraw = true;
        return true;
      }
      apply_settings_value(cur.key, cur.edit_input);
      return true;
    }
    if (ch == 127 || ch == 8)
    {
      if (!cur.edit_input.empty())
        cur.edit_input.pop_back();
      needs_redraw = true;
      return true;
    }
    if (ch >= 32 && ch < 1000)
    {
      cur.edit_input.push_back((char)ch);
      needs_redraw = true;
    }
    return true;
  }

  // Navigation (not editing).
  if (ch == 1008 || ch == 'k' || ch == 'K')
  {
    settings_selected = std::max(0, settings_selected - 1);
    needs_redraw = true;
    return true;
  }
  if (ch == 1009 || ch == 'j' || ch == 'J')
  {
    settings_selected = std::min((int)settings_entries.size() - 1, settings_selected + 1);
    needs_redraw = true;
    return true;
  }
  if (ch == 1015)
  {
    settings_selected = std::max(0, settings_selected - 8);
    needs_redraw = true;
    return true;
  }
  if (ch == 1016)
  {
    settings_selected =
        std::min((int)settings_entries.size() - 1, settings_selected + 8);
    needs_redraw = true;
    return true;
  }
  if (ch == 1012)
  {
    settings_selected = 0;
    needs_redraw = true;
    return true;
  }
  if (ch == 1013)
  {
    settings_selected = (int)settings_entries.size() - 1;
    needs_redraw = true;
    return true;
  }

  // Enter: toggle booleans, start inline edit for ints/strings.
  if (ch == '\n' || ch == 13)
  {
    if (cur.type == SettingsEntry::Type::Bool)
    {
      apply_settings_value(cur.key, cur.value == "true" ? "false" : "true");
    }
    else
    {
      // Fresh input: type the new value from scratch (empty + Enter
      // cancels the edit). Seeding with the old value would force
      // backspacing over it for every change.
      cur.editing = true;
      cur.edit_input.clear();
    }
    needs_redraw = true;
    return true;
  }

  // Left/Right toggle booleans too (vim-style).
  if (ch == 1010 || ch == 1011)
  {
    if (cur.type == SettingsEntry::Type::Bool)
    {
      apply_settings_value(cur.key, cur.value == "true" ? "false" : "true");
      needs_redraw = true;
    }
    return true;
  }

  return true;
}

bool Editor::handle_settings_mouse(int x, int y, bool is_click)
{
  if (!show_settings_menu)
    return false;

  for (int i = 0; i < (int)settings_entries.size(); i++)
  {
    const SettingsEntry &e = settings_entries[(size_t)i];
    if (y == e.row_y && x >= e.row_x && x < e.row_x + e.row_w)
    {
      if (settings_selected != i)
      {
        settings_selected = i;
        needs_redraw = true;
      }
      if (is_click)
      {
        return handle_settings_input('\n');
      }
      return true;
    }
  }

  const bool inside_panel = x >= settings_panel_x && x < settings_panel_x + settings_panel_w
                            && y >= settings_panel_y && y < settings_panel_y + settings_panel_h;
  if (inside_panel)
    return true;
  if (!is_click)
    return true;
  close_settings_menu();
  return false;
}