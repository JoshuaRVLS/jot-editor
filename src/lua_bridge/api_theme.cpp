#include "editor.h"
#include "lua_bridge/api.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdlib>

namespace {
std::vector<std::filesystem::path> theme_dirs() {
  namespace fs = std::filesystem; std::vector<fs::path> out;
  const char *home = getenv("HOME"); const char *app = getenv("APPDATA"); const char *cfg = getenv("JOT_CONFIG_HOME");
  fs::path root = cfg && *cfg ? fs::path(cfg) : (app && *app ? fs::path(app)/"jot" : (home ? fs::path(home)/".config"/"jot" : fs::path()));
  if (!root.empty()) { out.push_back(root/"configs"/"colors"); out.push_back(root/"themes"); }
  const char *data = getenv("JOT_DATA_HOME");
  if (data && *data) out.push_back(fs::path(data) / "configs" / "colors");
#ifdef JOT_DEFAULT_DATA_DIR
  out.push_back(fs::path(JOT_DEFAULT_DATA_DIR) / "configs" / "colors");
#endif
  out.push_back(fs::current_path()/".configs"/"configs"/"colors");
  out.push_back(fs::current_path()/"configs"/"colors");
  return out;
}
int value(const std::string &s, const char *key) {
  std::regex r(std::string("\\\"")+key+"\\\"\\s*:\\s*(-?[0-9]+)"); std::smatch m;
  return std::regex_search(s,m,r) ? std::stoi(m[1].str()) : -1;
}
}

void LuaAPI::show_message(const std::string &msg) {
  if (editor) editor->set_message(msg);
}

bool LuaAPI::apply_theme_file(const std::string &name,
                              std::vector<std::string> &stack) {
  for (const auto &dir : theme_dirs()) {
    auto p = dir / (name + ".json");
    std::ifstream in(p);
    if (!in) continue;
    std::string text((std::istreambuf_iterator<char>(in)), {});
    // "extends": "<base>" lets a custom theme inherit every group of a
    // bundled theme (file explorer, status bar, syntax, ...) and override
    // only what it sets. Bases are applied first so overrides win.
    std::smatch base_match;
    std::regex base_re("\\\"extends\\\"\\s*:\\s*\\\"([^\"]+)\\\"");
    if (std::regex_search(text, base_match, base_re) &&
        base_match[1].str() != name) {
      const std::string base = base_match[1].str();
      if (std::find(stack.begin(), stack.end(), base) == stack.end() &&
          stack.size() < 8) {
        stack.push_back(name);
        apply_theme_file(base, stack);
        stack.pop_back();
      }
    }
    std::regex group("\\\"([^\"]+)\\\"\\s*:\\s*\\{([^}]*)\\}");
    for (std::sregex_iterator i(text.begin(), text.end(), group), e;
         i != e; ++i) {
      set_theme_color((*i)[1].str(), value((*i)[2].str(), "fg"),
                      value((*i)[2].str(), "bg"));
    }
    return true;
  }
  return false;
}

bool LuaAPI::apply_colorscheme(const std::string &name) {
  std::vector<std::string> stack;
  return apply_theme_file(name, stack);
}

bool LuaAPI::apply_theme_and_persist(const std::string &name) {
  if (!editor) {
    return false;
  }
  return editor->apply_theme(name);
}
std::vector<std::string> LuaAPI::list_themes() { std::vector<std::string> out; for(const auto&d:theme_dirs()) if(std::filesystem::exists(d)) for(const auto&e:std::filesystem::directory_iterator(d)) if(e.path().extension()==".json") out.push_back(e.path().stem().string()); return out; }

void LuaAPI::set_theme_color(std::string name, int fg, int bg) {
  if (!editor)
    return;
  // The bundled JSON color schemes (.configs/configs/colors/*.json) and the
  // documented set_hl API use Neovim-style highlight group names such as
  // "StatusLineInfo" or "Pmenu". The slot chain below matches lowercase jot
  // names, so translate known groups before matching.
  static const std::unordered_map<std::string, std::string> group_aliases = {
      {"Normal", "normal"},
      {"NormalFloat", "command"},
      {"LineNr", "line_num"},
      {"Comment", "comment"},
      {"Keyword", "keyword"},
      {"String", "string"},
      {"Number", "number"},
      {"Function", "function"},
      {"Type", "type"},
      {"Cursor", "cursor"},
      {"Visual", "selection"},
      {"Search", "search_match"},
      {"StatusLine", "status"},
      {"StatusLineMsg", "status_message"},
      {"StatusLineLogo", "status_logo"},
      {"StatusLineFile", "status_file"},
      {"StatusLineInfo", "status_info"},
      {"StatusLineWarn", "status_warning"},
      {"StatusLineError", "status_error"},
      {"StatusLineMuted", "status_muted"},
      {"FloatBorder", "panel_border"},
      {"WinSeparator", "panel_border"},
      {"WinActiveBorder", "active_border"},
      {"TabLine", "tab_inactive"},
      {"TabLineSel", "tab_active"},
      {"TabLineFill", "tab_separator"},
      {"TabClose", "tab_close"},
      {"Sidebar", "sidebar"},
      {"SidebarDir", "sidebar_directory"},
      {"SidebarSel", "sidebar_selected"},
      {"SidebarSelNC", "sidebar_selected_inactive"},
      {"SidebarBorder", "sidebar_border"},
      {"DiagnosticError", "diagnostic_error"},
      {"DiagnosticWarn", "diagnostic_warning"},
      {"DiagnosticInfo", "diagnostic_info"},
      {"DiagnosticHint", "diagnostic_hint"},
      {"Pmenu", "command"},
      {"PmenuSel", "selection"},
      {"TelescopeNormal", "telescope"},
      {"TelescopeSelection", "telescope_selected"},
      {"TelescopePreviewNormal", "telescope_preview"},
      {"Terminal", "terminal"},
      {"TerminalTab", "terminal_tab_inactive"},
      {"TerminalTabActive", "terminal_tab_active"},
      {"TerminalTabFocused", "terminal_tab_focused"},
      {"TerminalTabClose", "terminal_tab_close"},
      {"TerminalTabPlus", "terminal_tab_plus"},
      {"TerminalTabSeparator", "terminal_tab_separator"},
  };
  auto alias = group_aliases.find(name);
  if (alias != group_aliases.end()) {
    name = alias->second;
  } else if (!name.empty() && name[0] == '@') {
    // Tree-sitter capture style ("@keyword.control", "@property") is
    // accepted as a theme group name; the slot chain matches the rest.
    name = name.substr(1);
  }
  Theme &theme = editor->get_theme();
  auto set_pair = [&](int &slot_fg, int &slot_bg) {
    if (fg != -1)
      slot_fg = fg;
    if (bg != -1)
      slot_bg = bg;
  };
  auto set_fg = [&](int &slot_fg) {
    if (fg != -1)
      slot_fg = fg;
  };
  auto set_bg = [&](int &slot_bg) {
    if (bg != -1)
      slot_bg = bg;
  };
  auto mark_syntax_pair = [&](SyntaxThemeSlot slot) {
    if (fg != -1 || bg != -1)
      theme.mark_syntax_slot_explicit(slot);
  };
  auto mark_syntax_fg = [&](SyntaxThemeSlot slot) {
    if (fg != -1)
      theme.mark_syntax_slot_explicit(slot);
  };

  if (name == "default" || name == "normal") {
    set_pair(theme.fg_default, theme.bg_default);
  } else if (name == "keyword") {
    set_pair(theme.fg_keyword, theme.bg_keyword);
  } else if (name == "string") {
    set_pair(theme.fg_string, theme.bg_string);
  } else if (name == "comment") {
    set_pair(theme.fg_comment, theme.bg_comment);
  } else if (name == "number") {
    set_pair(theme.fg_number, theme.bg_number);
  } else if (name == "function") {
    set_pair(theme.fg_function, theme.bg_function);
  } else if (name == "type") {
    set_pair(theme.fg_type, theme.bg_type);
  } else if (name == "variable") {
    mark_syntax_pair(SyntaxThemeSlot::Variable);
    set_pair(theme.fg_variable, theme.bg_variable);
  } else if (name == "constant") {
    mark_syntax_pair(SyntaxThemeSlot::Constant);
    set_pair(theme.fg_constant, theme.bg_constant);
  } else if (name == "builtin") {
    mark_syntax_pair(SyntaxThemeSlot::Builtin);
    set_pair(theme.fg_builtin, theme.bg_builtin);
  } else if (name == "operator") {
    mark_syntax_pair(SyntaxThemeSlot::Operator);
    set_pair(theme.fg_operator, theme.bg_operator);
  } else if (name == "punctuation") {
    mark_syntax_pair(SyntaxThemeSlot::Punctuation);
    set_pair(theme.fg_punctuation, theme.bg_punctuation);
  } else if (name == "tag") {
    mark_syntax_pair(SyntaxThemeSlot::Tag);
    set_pair(theme.fg_tag, theme.bg_tag);
  } else if (name == "attribute") {
    mark_syntax_pair(SyntaxThemeSlot::Attribute);
    set_pair(theme.fg_attribute, theme.bg_attribute);
  } else if (name == "namespace") {
    mark_syntax_pair(SyntaxThemeSlot::Namespace);
    set_pair(theme.fg_namespace, theme.bg_namespace);
  } else if (name == "module") {
    mark_syntax_pair(SyntaxThemeSlot::Module);
    set_pair(theme.fg_module, theme.bg_module);
  } else if (name == "parameter") {
    mark_syntax_pair(SyntaxThemeSlot::Parameter);
    set_pair(theme.fg_parameter, theme.bg_parameter);
  } else if (name == "field" || name == "property") {
    mark_syntax_pair(SyntaxThemeSlot::Field);
    set_pair(theme.fg_field, theme.bg_field);
  } else if (name == "keyword_control" || name == "keyword.control") {
    mark_syntax_pair(SyntaxThemeSlot::KeywordControl);
    set_pair(theme.fg_keyword_control, theme.bg_keyword_control);
  } else if (name == "keyword_storage" || name == "keyword.storage" ||
             name == "keyword_modifier" || name == "keyword.modifier") {
    mark_syntax_pair(SyntaxThemeSlot::KeywordStorage);
    set_pair(theme.fg_keyword_storage, theme.bg_keyword_storage);
  } else if (name == "keyword_preproc" || name == "keyword_preprocessor" ||
             name == "keyword.directive" || name == "keyword.import") {
    mark_syntax_pair(SyntaxThemeSlot::KeywordPreproc);
    set_pair(theme.fg_keyword_preproc, theme.bg_keyword_preproc);
  } else if (name == "function_method" || name == "method" ||
             name == "function.method") {
    mark_syntax_pair(SyntaxThemeSlot::FunctionMethod);
    set_pair(theme.fg_function_method, theme.bg_function_method);
  } else if (name == "function_constructor" || name == "constructor" ||
             name == "function.constructor") {
    mark_syntax_pair(SyntaxThemeSlot::FunctionConstructor);
    set_pair(theme.fg_function_constructor, theme.bg_function_constructor);
  } else if (name == "type_builtin" || name == "type.builtin") {
    mark_syntax_pair(SyntaxThemeSlot::TypeBuiltin);
    set_pair(theme.fg_type_builtin, theme.bg_type_builtin);
  } else if (name == "constant_macro" || name == "constant.macro" ||
             name == "constant_predefined") {
    mark_syntax_pair(SyntaxThemeSlot::ConstantMacro);
    set_pair(theme.fg_constant_macro, theme.bg_constant_macro);
  } else if (name == "string_escape" || name == "string.escape") {
    mark_syntax_pair(SyntaxThemeSlot::StringEscape);
    set_pair(theme.fg_string_escape, theme.bg_string_escape);
  } else if (name == "punctuation_bracket" ||
             name == "punctuation.bracket") {
    mark_syntax_pair(SyntaxThemeSlot::PunctuationBracket);
    set_pair(theme.fg_punctuation_bracket, theme.bg_punctuation_bracket);
  } else if (name == "punctuation_delimiter" ||
             name == "punctuation.delimiter") {
    mark_syntax_pair(SyntaxThemeSlot::PunctuationDelimiter);
    set_pair(theme.fg_punctuation_delimiter,
             theme.bg_punctuation_delimiter);
  } else if (name == "panel_border" || name == "border") {
    set_pair(theme.fg_panel_border, theme.bg_panel_border);
  } else if (name == "selection" || name == "visual" || name == "bg_selection" ||
             name == "fg_selection") {
    set_pair(theme.fg_selection, theme.bg_selection);
  } else if (name == "line_num" || name == "line_number" ||
             name == "fg_line_num" || name == "bg_line_num") {
    set_pair(theme.fg_line_num, theme.bg_line_num);
  } else if (name == "cursor" || name == "fg_cursor" || name == "bg_cursor") {
    set_pair(theme.fg_cursor, theme.bg_cursor);
  } else if (name == "status" || name == "status_bar" ||
             name == "bg_status_bar" || name == "fg_status_bar" ||
             name == "bg_status" || name == "fg_status") {
    set_pair(theme.fg_status, theme.bg_status);
  } else if (name == "status_message" || name == "statusline_message" ||
             name == "fg_status_message") {
    set_fg(theme.fg_status_message);
  } else if (name == "status_logo" || name == "statusline_logo" ||
             name == "fg_status_logo" || name == "bg_status_logo") {
    set_pair(theme.fg_status_logo, theme.bg_status_logo);
  } else if (name == "status_file" || name == "statusline_file" ||
             name == "fg_status_file" || name == "bg_status_file") {
    set_pair(theme.fg_status_file, theme.bg_status_file);
  } else if (name == "status_info" || name == "statusline_info" ||
             name == "fg_status_info" || name == "bg_status_info") {
    set_pair(theme.fg_status_info, theme.bg_status_info);
  } else if (name == "status_warning" || name == "status_warn" ||
             name == "statusline_warning" || name == "fg_status_warning" ||
             name == "bg_status_warning") {
    set_pair(theme.fg_status_warning, theme.bg_status_warning);
  } else if (name == "status_error" || name == "statusline_error" ||
             name == "fg_status_error" || name == "bg_status_error") {
    set_pair(theme.fg_status_error, theme.bg_status_error);
  } else if (name == "status_muted" || name == "statusline_muted" ||
             name == "fg_status_muted" || name == "bg_status_muted") {
    set_pair(theme.fg_status_muted, theme.bg_status_muted);
  } else if (name == "command" || name == "cmdline" || name == "fg_command" ||
             name == "bg_command") {
    set_pair(theme.fg_command, theme.bg_command);
  } else if (name == "search_match" || name == "search" ||
             name == "incsearch" || name == "fg_search_match" ||
             name == "bg_search_match") {
    set_pair(theme.fg_search_match, theme.bg_search_match);
  } else if (name == "minimap") {
    set_pair(theme.fg_minimap, theme.bg_minimap);
  } else if (name == "sidebar" || name == "fg_sidebar" || name == "bg_sidebar") {
    set_pair(theme.fg_sidebar, theme.bg_sidebar);
  } else if (name == "sidebar_directory" || name == "sidebar_dir") {
    set_fg(theme.fg_sidebar_directory);
  } else if (name == "sidebar_selected" || name == "sidebar_sel" ||
             name == "fg_sidebar_selected" || name == "bg_sidebar_selected") {
    set_pair(theme.fg_sidebar_selected, theme.bg_sidebar_selected);
  } else if (name == "sidebar_selected_inactive" || name == "sidebar_sel_nc") {
    set_pair(theme.fg_sidebar_selected_inactive, theme.bg_sidebar_selected_inactive);
  } else if (name == "sidebar_border" || name == "fg_sidebar_border") {
    set_fg(theme.fg_sidebar_border);
  } else if (name == "tab_active" || name == "tabline_sel" ||
             name == "fg_tab_active" || name == "bg_tab_active") {
    set_pair(theme.fg_tab_active, theme.bg_tab_active);
  } else if (name == "tab_inactive" || name == "tabline" ||
             name == "fg_tab_inactive" || name == "bg_tab_inactive") {
    set_pair(theme.fg_tab_inactive, theme.bg_tab_inactive);
  } else if (name == "tab_close" || name == "fg_tab_close") {
    set_fg(theme.fg_tab_close);
  } else if (name == "tab_separator" || name == "tabline_fill" ||
             name == "fg_tab_separator") {
    set_fg(theme.fg_tab_separator);
  } else if (name == "active_border" || name == "win_active_border" ||
             name == "fg_active_border" || name == "bg_active_border") {
    set_pair(theme.fg_active_border, theme.bg_active_border);
  } else if (name == "image_border") {
    set_pair(theme.fg_image_border, theme.bg_image_border);
  } else if (name == "diagnostic_error" || name == "fg_diagnostic_error") {
    set_fg(theme.fg_diagnostic_error);
  } else if (name == "diagnostic_warning" || name == "diagnostic_warn" ||
             name == "fg_diagnostic_warning") {
    set_fg(theme.fg_diagnostic_warning);
  } else if (name == "diagnostic_info" || name == "fg_diagnostic_info") {
    set_fg(theme.fg_diagnostic_info);
  } else if (name == "diagnostic_hint" || name == "fg_diagnostic_hint") {
    set_fg(theme.fg_diagnostic_hint);
  } else if (name == "bracket_match") {
    set_pair(theme.fg_bracket_match, theme.bg_bracket_match);
  } else if (name == "bracket1" || name == "fg_bracket1") {
    set_fg(theme.fg_bracket1);
  } else if (name == "bracket2" || name == "fg_bracket2") {
    set_fg(theme.fg_bracket2);
  } else if (name == "bracket3" || name == "fg_bracket3") {
    set_fg(theme.fg_bracket3);
  } else if (name == "bracket4" || name == "fg_bracket4") {
    set_fg(theme.fg_bracket4);
  } else if (name == "bracket5" || name == "fg_bracket5") {
    set_fg(theme.fg_bracket5);
  } else if (name == "bracket6" || name == "fg_bracket6") {
    set_fg(theme.fg_bracket6);
  } else if (name == "telescope") {
    set_pair(theme.fg_telescope, theme.bg_telescope);
  } else if (name == "telescope_selected") {
    set_pair(theme.fg_telescope_selected, theme.bg_telescope_selected);
  } else if (name == "telescope_preview") {
    set_pair(theme.fg_telescope_preview, theme.bg_telescope_preview);
  } else if (name == "terminal" || name == "terminal_panel" ||
             name == "fg_terminal" || name == "bg_terminal") {
    set_pair(theme.fg_terminal, theme.bg_terminal);
  } else if (name == "terminal_tab_inactive" ||
             name == "fg_terminal_tab_inactive" ||
             name == "bg_terminal_tab_inactive") {
    set_pair(theme.fg_terminal_tab_inactive, theme.bg_terminal_tab_inactive);
  } else if (name == "terminal_tab_active" ||
             name == "fg_terminal_tab_active" ||
             name == "bg_terminal_tab_active") {
    set_pair(theme.fg_terminal_tab_active, theme.bg_terminal_tab_active);
  } else if (name == "terminal_tab_focused" ||
             name == "fg_terminal_tab_focused" ||
             name == "bg_terminal_tab_focused") {
    set_pair(theme.fg_terminal_tab_focused, theme.bg_terminal_tab_focused);
  } else if (name == "terminal_tab_close" || name == "fg_terminal_tab_close") {
    set_fg(theme.fg_terminal_tab_close);
  } else if (name == "terminal_tab_plus" ||
             name == "fg_terminal_tab_plus" ||
             name == "bg_terminal_tab_plus") {
    set_pair(theme.fg_terminal_tab_plus, theme.bg_terminal_tab_plus);
  } else if (name == "terminal_tab_separator" ||
             name == "fg_terminal_tab_separator") {
    set_fg(theme.fg_terminal_tab_separator);
  } else if (name == "git_modified") {
    set_pair(theme.fg_git_modified, theme.bg_git_modified);
  } else if (name == "git_added" || name == "git_untracked") {
    set_pair(theme.fg_git_added, theme.bg_git_added);
    if (name == "git_untracked") {
      theme.fg_git_untracked = theme.fg_git_added;
      theme.bg_git_untracked = theme.bg_git_added;
    }
  } else if (name == "git_deleted") {
    set_pair(theme.fg_git_deleted, theme.bg_git_deleted);
  } else if (name == "git_renamed") {
    set_pair(theme.fg_git_renamed, theme.bg_git_renamed);
  } else if (name == "git_conflict") {
    set_pair(theme.fg_git_conflict, theme.bg_git_conflict);
  } else if (name == "bg_default") {
    set_bg(theme.bg_default);
  } else if (name == "fg_default") {
    set_fg(theme.fg_default);
  } else if (name == "fg_keyword") {
    set_fg(theme.fg_keyword);
  } else if (name == "fg_string") {
    set_fg(theme.fg_string);
  } else if (name == "fg_comment") {
    set_fg(theme.fg_comment);
  } else if (name == "fg_function") {
    set_fg(theme.fg_function);
  } else if (name == "fg_type") {
    set_fg(theme.fg_type);
  } else if (name == "fg_number") {
    set_fg(theme.fg_number);
  } else if (name == "fg_variable") {
    mark_syntax_fg(SyntaxThemeSlot::Variable);
    set_fg(theme.fg_variable);
  } else if (name == "fg_constant") {
    mark_syntax_fg(SyntaxThemeSlot::Constant);
    set_fg(theme.fg_constant);
  } else if (name == "fg_builtin") {
    mark_syntax_fg(SyntaxThemeSlot::Builtin);
    set_fg(theme.fg_builtin);
  } else if (name == "fg_operator") {
    mark_syntax_fg(SyntaxThemeSlot::Operator);
    set_fg(theme.fg_operator);
  } else if (name == "fg_punctuation") {
    mark_syntax_fg(SyntaxThemeSlot::Punctuation);
    set_fg(theme.fg_punctuation);
  } else if (name == "fg_tag") {
    mark_syntax_fg(SyntaxThemeSlot::Tag);
    set_fg(theme.fg_tag);
  } else if (name == "fg_attribute") {
    mark_syntax_fg(SyntaxThemeSlot::Attribute);
    set_fg(theme.fg_attribute);
  } else if (name == "fg_namespace") {
    mark_syntax_fg(SyntaxThemeSlot::Namespace);
    set_fg(theme.fg_namespace);
  } else if (name == "fg_module") {
    mark_syntax_fg(SyntaxThemeSlot::Module);
    set_fg(theme.fg_module);
  } else if (name == "fg_parameter") {
    mark_syntax_fg(SyntaxThemeSlot::Parameter);
    set_fg(theme.fg_parameter);
  } else if (name == "fg_field" || name == "fg_property") {
    mark_syntax_fg(SyntaxThemeSlot::Field);
    set_fg(theme.fg_field);
  } else if (name == "fg_keyword_control") {
    mark_syntax_fg(SyntaxThemeSlot::KeywordControl);
    set_fg(theme.fg_keyword_control);
  } else if (name == "fg_keyword_storage" ||
             name == "fg_keyword_modifier") {
    mark_syntax_fg(SyntaxThemeSlot::KeywordStorage);
    set_fg(theme.fg_keyword_storage);
  } else if (name == "fg_keyword_preproc" ||
             name == "fg_keyword_preprocessor") {
    mark_syntax_fg(SyntaxThemeSlot::KeywordPreproc);
    set_fg(theme.fg_keyword_preproc);
  } else if (name == "fg_function_method" || name == "fg_method") {
    mark_syntax_fg(SyntaxThemeSlot::FunctionMethod);
    set_fg(theme.fg_function_method);
  } else if (name == "fg_function_constructor" ||
             name == "fg_constructor") {
    mark_syntax_fg(SyntaxThemeSlot::FunctionConstructor);
    set_fg(theme.fg_function_constructor);
  } else if (name == "fg_type_builtin") {
    mark_syntax_fg(SyntaxThemeSlot::TypeBuiltin);
    set_fg(theme.fg_type_builtin);
  } else if (name == "fg_constant_macro") {
    mark_syntax_fg(SyntaxThemeSlot::ConstantMacro);
    set_fg(theme.fg_constant_macro);
  } else if (name == "fg_string_escape") {
    mark_syntax_fg(SyntaxThemeSlot::StringEscape);
    set_fg(theme.fg_string_escape);
  } else if (name == "fg_punctuation_bracket") {
    mark_syntax_fg(SyntaxThemeSlot::PunctuationBracket);
    set_fg(theme.fg_punctuation_bracket);
  } else if (name == "fg_punctuation_delimiter") {
    mark_syntax_fg(SyntaxThemeSlot::PunctuationDelimiter);
    set_fg(theme.fg_punctuation_delimiter);
  }
}
