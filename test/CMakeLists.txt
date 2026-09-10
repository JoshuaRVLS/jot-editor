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
  test_lsp_policy.cpp
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
  test_discord_presence.cpp
  test_discord_rpc.cpp
  test_discord_session.cpp
  test_home_menu.cpp
  test_home_surface.cpp
  test_lua_decorations.cpp
  test_lua_loader.cpp
  test_lua_toast.cpp
  test_git_panel.cpp
  test_multicursor.cpp
  test_zen_mode.cpp
  test_settings_menu.cpp
  test_terminal_panel.cpp
  test_win_probe.cpp
  test_inlay_hints.cpp
  test_commenting.cpp
  test_sidebar_guides.cpp
  test_blank_guides.cpp
  test_right_panel.cpp
  test_completion_matching.cpp
  test_gui_fit.cpp
  test_resize_sync.cpp
  test_key_decode.cpp
)

target_link_libraries(jot_tests PRIVATE
  Catch2::Catch2WithMain
  jot_engine
)

target_include_directories(jot_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/test
)
# test_syntax.cpp includes tree_sitter/api.h directly; the fetched/system
# tree-sitter include dirs are only wired onto the engine objects, so mirror
# them here when the parser runtime is enabled.
if(JOT_TREESITTER_ENABLED)
  target_include_directories(jot_tests SYSTEM PRIVATE ${TREE_SITTER_INCLUDE_DIRS})
endif()
target_compile_definitions(jot_tests PRIVATE
  JOT_LUA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/runtime/lua"
  JOT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}/test"
)

include(Catch)
catch_discover_tests(jot_tests)
