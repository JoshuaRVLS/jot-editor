#ifndef JOT_MODEL_THEME_H
#define JOT_MODEL_THEME_H
enum class SyntaxThemeSlot
{
  Variable,
  Constant,
  Builtin,
  Operator,
  Punctuation,
  Tag,
  Attribute,
  Namespace,
  Module,
  Parameter,
  Field,
  KeywordControl,
  KeywordStorage,
  KeywordPreproc,
  FunctionMethod,
  FunctionConstructor,
  TypeBuiltin,
  ConstantMacro,
  StringEscape,
  PunctuationBracket,
  PunctuationDelimiter
};

struct Theme
{
  int fg_default = 7;
  int bg_default = 0;
  int fg_keyword = 6;
  int bg_keyword = 0;
  int fg_string = 2;
  int bg_string = 0;
  int fg_comment = 8; // Grey for comments
  int bg_comment = 0;
  int fg_number = 5;
  int bg_number = 0;
  int fg_function = 3;
  int bg_function = 0;
  int fg_type = 6;
  int bg_type = 0;
  int fg_variable = 7;
  int bg_variable = 0;
  int fg_constant = 5;
  int bg_constant = 0;
  int fg_builtin = 6;
  int bg_builtin = 0;
  int fg_operator = 6;
  int bg_operator = 0;
  int fg_punctuation = 8;
  int bg_punctuation = 0;
  int fg_tag = 1;
  int bg_tag = 0;
  int fg_attribute = 5;
  int bg_attribute = 0;
  int fg_namespace = 6;
  int bg_namespace = 0;
  int fg_module = 5;
  int bg_module = 0;
  int fg_parameter = 7;
  int bg_parameter = 0;
  int fg_field = 6;
  int bg_field = 0;
  int fg_keyword_control = 6;
  int bg_keyword_control = 0;
  int fg_keyword_storage = 6;
  int bg_keyword_storage = 0;
  int fg_keyword_preproc = 5;
  int bg_keyword_preproc = 0;
  int fg_function_method = 3;
  int bg_function_method = 0;
  int fg_function_constructor = 3;
  int bg_function_constructor = 0;
  int fg_type_builtin = 6;
  int bg_type_builtin = 0;
  int fg_constant_macro = 5;
  int bg_constant_macro = 0;
  int fg_string_escape = 6;
  int bg_string_escape = 0;
  int fg_punctuation_bracket = 8;
  int bg_punctuation_bracket = 0;
  int fg_punctuation_delimiter = 8;
  int bg_punctuation_delimiter = 0;
  bool syntax_variable_explicit = false;
  bool syntax_constant_explicit = false;
  bool syntax_builtin_explicit = false;
  bool syntax_operator_explicit = false;
  bool syntax_punctuation_explicit = false;
  bool syntax_tag_explicit = false;
  bool syntax_attribute_explicit = false;
  bool syntax_namespace_explicit = false;
  bool syntax_module_explicit = false;
  bool syntax_parameter_explicit = false;
  bool syntax_field_explicit = false;
  bool syntax_keyword_control_explicit = false;
  bool syntax_keyword_storage_explicit = false;
  bool syntax_keyword_preproc_explicit = false;
  bool syntax_function_method_explicit = false;
  bool syntax_function_constructor_explicit = false;
  bool syntax_type_builtin_explicit = false;
  bool syntax_constant_macro_explicit = false;
  bool syntax_string_escape_explicit = false;
  bool syntax_punctuation_bracket_explicit = false;
  bool syntax_punctuation_delimiter_explicit = false;
  int fg_panel_border = 8; // Grey border
  int bg_panel_border = 0; // Black background (cleaner)
  int fg_selection = 0;
  int bg_selection = 6;
  int fg_line_num = 8; // Grey line numbers
  int bg_line_num = 0;
  int fg_cursor = 0;
  int bg_cursor = 7;
  int fg_cursor_line_num = 15; // Active line number (cursorline row)
  int bg_cursor_line = 8;      // Soft row tint behind the active line
  int fg_search_current = 0;   // Active (jump-to) search match
  int bg_search_current = 11;
  int fg_status = 7;
  int bg_status = 0; // Clean status bar
  int fg_status_message = 7;
  int fg_status_logo = 0;
  int bg_status_logo = 6;
  int fg_status_file = 15;
  int bg_status_file = 4;
  int fg_status_info = 0;
  int bg_status_info = 6;
  int fg_status_warning = 0;
  int bg_status_warning = 3;
  int fg_status_error = 15;
  int bg_status_error = 1;
  int fg_status_muted = 8;
  int bg_status_muted = 0;
  int fg_command = 7;
  int bg_command = 0;
  int fg_search_match = 0;
  int bg_search_match = 3;
  int fg_minimap = 8;
  int bg_minimap = 0;
  int fg_sidebar = 7;
  int bg_sidebar = 0;
  int fg_sidebar_directory = 7;
  int fg_sidebar_selected = 0;
  int bg_sidebar_selected = 4;
  int fg_sidebar_selected_inactive = 0;
  int bg_sidebar_selected_inactive = 5;
  int fg_sidebar_border = 8;
  int fg_tab_active = 7;
  int bg_tab_active = 4;
  int fg_tab_inactive = 8;
  int bg_tab_inactive = 0;
  int fg_tab_close = 1;
  int fg_tab_separator = 8;
  int fg_active_border = 3;
  int bg_active_border = 0;
  int fg_image_border = 7;
  int bg_image_border = 0;
  int fg_diagnostic_error = 1;
  int fg_diagnostic_warning = 3;
  int fg_diagnostic_info = 6;
  int fg_diagnostic_hint = 2;
  int fg_bracket1 = 1;
  int fg_bracket2 = 2;
  int fg_bracket3 = 3;
  int fg_bracket4 = 4;
  int fg_bracket5 = 5;
  int fg_bracket6 = 6;
  int fg_bracket_match = 3;
  int bg_bracket_match = 0;
  int fg_telescope = 7;
  int bg_telescope = 0;
  int fg_telescope_selected = 0;
  int bg_telescope_selected = 6;
  int fg_telescope_preview = 7;
  int bg_telescope_preview = 0;
  // Query (input) row: kept visually distinct from the selection highlight so
  // the typed text stays readable — dark input bg like the command bar rather
  // than the loud selection color. Defaults mirror fg_command/bg_command.
  int fg_telescope_query = 7;
  int bg_telescope_query = 0;
  int fg_terminal = 7;
  int bg_terminal = 0;
  int fg_terminal_tab_inactive = 7;
  int bg_terminal_tab_inactive = 0;
  int fg_terminal_tab_active = 7;
  int bg_terminal_tab_active = 0;
  int fg_terminal_tab_focused = 7;
  int bg_terminal_tab_focused = 6;
  int fg_terminal_tab_close = 1;
  int fg_terminal_tab_plus = 7;
  int bg_terminal_tab_plus = 0;
  int fg_terminal_tab_separator = 8;
  int fg_git_modified = 4;
  int bg_git_modified = 17;
  int fg_git_added = 2;
  int bg_git_added = 22;
  int fg_git_deleted = 1;
  int bg_git_deleted = 52;
  int fg_git_renamed = 6;
  int bg_git_renamed = 23;
  int fg_git_untracked = 2;
  int bg_git_untracked = 22;
  int fg_git_conflict = 1;
  int bg_git_conflict = 52;

  void mark_syntax_slot_explicit(SyntaxThemeSlot slot)
  {
    switch (slot)
    {
    case SyntaxThemeSlot::Variable:
      syntax_variable_explicit = true;
      break;
    case SyntaxThemeSlot::Constant:
      syntax_constant_explicit = true;
      break;
    case SyntaxThemeSlot::Builtin:
      syntax_builtin_explicit = true;
      break;
    case SyntaxThemeSlot::Operator:
      syntax_operator_explicit = true;
      break;
    case SyntaxThemeSlot::Punctuation:
      syntax_punctuation_explicit = true;
      break;
    case SyntaxThemeSlot::Tag:
      syntax_tag_explicit = true;
      break;
    case SyntaxThemeSlot::Attribute:
      syntax_attribute_explicit = true;
      break;
    case SyntaxThemeSlot::Namespace:
      syntax_namespace_explicit = true;
      break;
    case SyntaxThemeSlot::Module:
      syntax_module_explicit = true;
      break;
    case SyntaxThemeSlot::Parameter:
      syntax_parameter_explicit = true;
      break;
    case SyntaxThemeSlot::Field:
      syntax_field_explicit = true;
      break;
    case SyntaxThemeSlot::KeywordControl:
      syntax_keyword_control_explicit = true;
      break;
    case SyntaxThemeSlot::KeywordStorage:
      syntax_keyword_storage_explicit = true;
      break;
    case SyntaxThemeSlot::KeywordPreproc:
      syntax_keyword_preproc_explicit = true;
      break;
    case SyntaxThemeSlot::FunctionMethod:
      syntax_function_method_explicit = true;
      break;
    case SyntaxThemeSlot::FunctionConstructor:
      syntax_function_constructor_explicit = true;
      break;
    case SyntaxThemeSlot::TypeBuiltin:
      syntax_type_builtin_explicit = true;
      break;
    case SyntaxThemeSlot::ConstantMacro:
      syntax_constant_macro_explicit = true;
      break;
    case SyntaxThemeSlot::StringEscape:
      syntax_string_escape_explicit = true;
      break;
    case SyntaxThemeSlot::PunctuationBracket:
      syntax_punctuation_bracket_explicit = true;
      break;
    case SyntaxThemeSlot::PunctuationDelimiter:
      syntax_punctuation_delimiter_explicit = true;
      break;
    }
  }

  void normalize_syntax_palette()
  {
    if (!syntax_variable_explicit)
    {
      fg_variable = fg_default;
      bg_variable = bg_default;
    }
    if (!syntax_parameter_explicit)
    {
      fg_parameter = fg_default;
      bg_parameter = bg_default;
    }
    if (!syntax_field_explicit)
    {
      fg_field = fg_default;
      bg_field = bg_default;
    }
    if (!syntax_punctuation_explicit)
    {
      fg_punctuation = fg_default;
      bg_punctuation = bg_default;
    }
    if (!syntax_operator_explicit)
    {
      fg_operator = fg_keyword;
      bg_operator = bg_keyword;
    }
    if (!syntax_tag_explicit)
    {
      fg_tag = fg_keyword;
      bg_tag = bg_keyword;
    }
    if (!syntax_constant_explicit)
    {
      fg_constant = fg_number;
      bg_constant = bg_number;
    }
    if (!syntax_builtin_explicit)
    {
      fg_builtin = fg_type;
      bg_builtin = bg_type;
    }
    if (!syntax_attribute_explicit)
    {
      fg_attribute = fg_type;
      bg_attribute = bg_type;
    }
    if (!syntax_namespace_explicit)
    {
      fg_namespace = fg_default;
      bg_namespace = bg_default;
    }
    if (!syntax_module_explicit)
    {
      fg_module = fg_default;
      bg_module = bg_default;
    }
    if (!syntax_keyword_control_explicit)
    {
      fg_keyword_control = fg_keyword;
      bg_keyword_control = bg_keyword;
    }
    if (!syntax_keyword_storage_explicit)
    {
      fg_keyword_storage = fg_type;
      bg_keyword_storage = bg_type;
    }
    if (!syntax_keyword_preproc_explicit)
    {
      fg_keyword_preproc = fg_constant;
      bg_keyword_preproc = bg_constant;
    }
    if (!syntax_function_method_explicit)
    {
      fg_function_method = fg_function;
      bg_function_method = bg_function;
    }
    if (!syntax_function_constructor_explicit)
    {
      fg_function_constructor = fg_type;
      bg_function_constructor = bg_type;
    }
    if (!syntax_type_builtin_explicit)
    {
      fg_type_builtin = fg_builtin;
      bg_type_builtin = bg_builtin;
    }
    if (!syntax_constant_macro_explicit)
    {
      fg_constant_macro = fg_constant;
      bg_constant_macro = bg_constant;
    }
    if (!syntax_string_escape_explicit)
    {
      fg_string_escape = fg_builtin;
      bg_string_escape = bg_builtin;
    }
    if (!syntax_punctuation_bracket_explicit)
    {
      fg_punctuation_bracket = fg_punctuation;
      bg_punctuation_bracket = bg_punctuation;
    }
    if (!syntax_punctuation_delimiter_explicit)
    {
      fg_punctuation_delimiter = fg_punctuation;
      bg_punctuation_delimiter = bg_punctuation;
    }
  }
};

#endif
