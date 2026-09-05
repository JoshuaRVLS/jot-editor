add_executable(jot_tests
  test_productivity.cpp
  test_folding.cpp
  test_features.cpp
  test_cpp_assist.cpp
  test_cursor.cpp
  test_debugger.cpp
  test_imageviewer.cpp
  test_syntax.cpp
  test_terminal.cpp
  test_lsp.cpp
  test_lsp_install.cpp
  test_popup.cpp
  test_tree_sitter_install.cpp
  test_telescope.cpp
  test_ui_text.cpp
  test_lua_float.cpp
  test_lua_hover_ui.cpp
  test_lua_keybind_helper.cpp
  test_lua_ui_kit.cpp
  test_lua_ui_telescope_alignment.cpp
  test_tree_sitter_lua.cpp
  test_lua_treesitter_module.cpp
  test_tree_sitter_incremental.cpp
  test_decorations.cpp
  test_lua_decorations.cpp
  test_win_probe.cpp
)

target_link_libraries(jot_tests PRIVATE
  Catch2::Catch2WithMain
  jot_engine
)

target_include_directories(jot_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
)
target_compile_definitions(jot_tests PRIVATE
  JOT_LUA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/lua"
)

include(Catch)
catch_discover_tests(jot_tests)
