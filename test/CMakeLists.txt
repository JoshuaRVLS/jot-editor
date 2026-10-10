add_executable(jot_tests
  test_string_util.cpp
  test_line_ops.cpp
  test_productivity.cpp
  test_smooth_scroll.cpp
  test_status_clock.cpp
  test_status_chips.cpp
  test_paste_indent.cpp
  test_coding_time.cpp
  test_wakatime.cpp
  test_folding.cpp
  test_fold_click.cpp
  test_features.cpp
  test_markup_tags.cpp
  test_alt_chords.cpp
  test_emmet.cpp
  test_web_completion.cpp
  test_html_preview.cpp
  test_http_file.cpp
  test_bible.cpp
  test_bible_client.cpp
  test_leetcode_services.cpp
  test_cpp_assist.cpp
  test_cpp_definitions.cpp
  test_cpp_flags.cpp
  test_cursor.cpp
  test_caret_bounds.cpp
  test_cursor_blink.cpp
  test_cursor_blink_wiring.cpp
  test_cursor_colors.cpp
  test_pane_edges.cpp
  test_pane_row_budget.cpp
  test_pane_tabs.cpp
  test_command_palette.cpp
  test_tab_order.cpp
  test_winbar.cpp
  test_color_codes.cpp
  test_color_formats.cpp
  test_color_space.cpp
  test_colorizer_render.cpp
  test_truecolor_cells.cpp
  test_cursor_scroll.cpp
  test_jumplist.cpp
  test_textobjects.cpp
  test_keymap_grammar.cpp
  test_debugger.cpp
  test_imageviewer.cpp
  test_syntax.cpp
  test_terminal.cpp
  test_input_reader.cpp
  test_autoclose.cpp
  test_cpp_indent.cpp
  test_save_hygiene.cpp
  test_undo_redo_chords.cpp
  test_undo_noop.cpp
  test_undo_memory.cpp
  test_syntax_tree_memory.cpp
  test_syntax_tree_rewrites.cpp
  test_lsp.cpp
  test_lsp_sync.cpp
  test_lsp_quiet_diagnostics.cpp
  test_lsp_install.cpp
  test_lsp_windows_spawn.cpp
  test_lsp_policy.cpp
  test_popup.cpp
  test_prompt_modal.cpp
  test_completion_import.cpp
  test_lsp_completion_args.cpp
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
  test_home_open.cpp
  test_home_surface.cpp
  test_lua_decorations.cpp
  test_lua_todo_comments.cpp
  test_lua_file_bindings.cpp
  test_lua_edit_snapshots.cpp
  test_lua_loader.cpp
  test_lua_toast.cpp
  test_git_panel.cpp
  test_git_status_digest.cpp
  test_multicursor.cpp
  test_mouse_hover.cpp
  test_url_span.cpp
  test_link_click.cpp
  test_zen_mode.cpp
  test_settings_menu.cpp
  test_themes.cpp
  test_terminal_panel.cpp
  test_terminal_float.cpp
  test_terminal_links.cpp
  test_bottom_panel.cpp
  test_win_probe.cpp
  test_inlay_hints.cpp
  test_commenting.cpp
  test_sidebar_guides.cpp
  test_sidebar_sections.cpp
  test_blank_guides.cpp
  test_bracket_match.cpp
  test_replace_inside.cpp
  test_word_highlight.cpp
  test_outline_tree.cpp
  test_outline_mouse.cpp
  test_diagnostic_line_bg.cpp
  test_right_panel.cpp
  test_markdown_http.cpp
  test_lua_markdown.cpp
  test_lua_leetcode.cpp
  test_ai.cpp
  test_lua_snippet.cpp
  test_completion_matching.cpp
  test_completion_rank.cpp
  test_completion_ghost.cpp
  test_overlay_placement.cpp
  test_completion_selection.cpp
  test_tag_completion.cpp
  test_gui_fit.cpp
  test_gui_scroll_math.cpp
  test_resize_sync.cpp
  test_key_decode.cpp
  test_exe_path.cpp
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
  JOT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  JOT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}/test"
)

include(Catch)
catch_discover_tests(jot_tests)

# The GUI frontend tests (test_gui_keys.cpp for key translation,
# test_gui_text_input.cpp for composing text and the window title) need SDL's
# keysym constants and the frontend's own objects, so they are only built when
# the GUI is part of the engine. JOT_GUI_ENABLED and SDL2_INCLUDE_DIRS are
# published by src/CMakeLists.txt. The font catalog test needs FreeType for the
# same reason: it reads installed faces.
if(JOT_GUI_ENABLED)
  target_sources(jot_tests PRIVATE test_gui_keys.cpp test_gui_text_input.cpp test_font_catalog.cpp)
  target_include_directories(jot_tests SYSTEM PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${FREETYPE2_INCLUDE_DIRS}
  )
endif()
