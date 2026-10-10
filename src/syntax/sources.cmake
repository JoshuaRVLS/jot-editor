# The syntax module: language detection, highlighting, folding, the tree-sitter
# runtime and the C++ and colour scanners that feed them.
set(JOT_SYNTAX_SOURCES
  syntax/bracket.cpp
  syntax/color/color_codes.cpp
  syntax/color/color_definitions.cpp
  syntax/color/color_functions.cpp
  syntax/color/color_names.cpp
  syntax/color/color_space.cpp
  syntax/color/color_tailwind.cpp
  syntax/color/color_terminal_codes.cpp
  syntax/cpp_assist.cpp
  syntax/cpp_definitions.cpp
  syntax/cpp_definitions_analysis.cpp
  syntax/cpp_flags.cpp
  syntax/folding.cpp
  syntax/index.cpp
  syntax/install.cpp
  syntax/language.cpp
  syntax/manager.cpp
  syntax/queries.cpp
  syntax/runtime.cpp
  syntax/syntax.cpp
  syntax/syntax_editor.cpp
  syntax/tree_sitter_commands.cpp
)
