# The commands module: the ex command line and the commands it runs -- line
# commands, folding, the jumplist, bookmarks, navigation, save and rename
# prompts, the help and C++ definition lookups.
set(JOT_COMMANDS_SOURCES
  commands/actions.cpp
  commands/bookmarks.cpp
  commands/cpp_definitions.cpp
  commands/ex.cpp
  commands/ex_tail.cpp
  commands/execute.cpp
  commands/folding.cpp
  commands/help.cpp
  commands/jumplist.cpp
  commands/line_commands.cpp
  commands/navigation.cpp
  commands/rename_prompt.cpp
  commands/save_prompt.cpp
  commands/utils.cpp
)
