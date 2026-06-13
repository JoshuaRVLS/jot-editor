function(jot_configure_object_target target_name)
  target_compile_options(${target_name} PRIVATE ${PYTHON_OTHER_FLAGS_LIST})

  target_include_directories(${target_name} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/input
    ${PROJECT_SOURCE_DIR}/src/render
    ${PROJECT_SOURCE_DIR}/src/edit
    ${PROJECT_SOURCE_DIR}/src/features
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/src/python_bridge
    ${PROJECT_SOURCE_DIR}/src/tools
  )

  if(PYTHON_INCLUDES)
    target_include_directories(${target_name} SYSTEM PRIVATE ${PYTHON_INCLUDES})
  endif()
endfunction()

find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED vterm)
if(JOT_TREESITTER)
  pkg_check_modules(TREE_SITTER tree-sitter)
  if(TREE_SITTER_FOUND)
    set(JOT_TREESITTER_ENABLED TRUE)
    message(STATUS "Tree-sitter runtime found; parser grammars will be loaded at runtime")
  else()
    set(JOT_TREESITTER_ENABLED FALSE)
    message(STATUS "Tree-sitter runtime not found; regex syntax highlighting fallback only")
  endif()
endif()

function(jot_expose_module_api target_name)
  target_include_directories(${target_name} PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/input
    ${PROJECT_SOURCE_DIR}/src/render
    ${PROJECT_SOURCE_DIR}/src/edit
    ${PROJECT_SOURCE_DIR}/src/features
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/src/python_bridge
    ${PROJECT_SOURCE_DIR}/src/tools
  )
endfunction()

set(JOT_TREE_SITTER_LANGUAGE_SOURCES
  features/tree_sitter/languages/asm.cpp
  features/tree_sitter/languages/bash.cpp
  features/tree_sitter/languages/bibtex.cpp
  features/tree_sitter/languages/c.cpp
  features/tree_sitter/languages/c_sharp.cpp
  features/tree_sitter/languages/cmake.cpp
  features/tree_sitter/languages/cpp.cpp
  features/tree_sitter/languages/css.cpp
  features/tree_sitter/languages/csv.cpp
  features/tree_sitter/languages/dart.cpp
  features/tree_sitter/languages/diff.cpp
  features/tree_sitter/languages/dockerfile.cpp
  features/tree_sitter/languages/elixir.cpp
  features/tree_sitter/languages/erlang.cpp
  features/tree_sitter/languages/fish.cpp
  features/tree_sitter/languages/fortran.cpp
  features/tree_sitter/languages/gdscript.cpp
  features/tree_sitter/languages/git_config.cpp
  features/tree_sitter/languages/gitcommit.cpp
  features/tree_sitter/languages/glsl.cpp
  features/tree_sitter/languages/go.cpp
  features/tree_sitter/languages/graphql.cpp
  features/tree_sitter/languages/haskell.cpp
  features/tree_sitter/languages/hcl.cpp
  features/tree_sitter/languages/html.cpp
  features/tree_sitter/languages/ini.cpp
  features/tree_sitter/languages/java.cpp
  features/tree_sitter/languages/javascript.cpp
  features/tree_sitter/languages/json.cpp
  features/tree_sitter/languages/kotlin.cpp
  features/tree_sitter/languages/latex.cpp
  features/tree_sitter/languages/lua.cpp
  features/tree_sitter/languages/make.cpp
  features/tree_sitter/languages/markdown.cpp
  features/tree_sitter/languages/nix.cpp
  features/tree_sitter/languages/ocaml.cpp
  features/tree_sitter/languages/perl.cpp
  features/tree_sitter/languages/php.cpp
  features/tree_sitter/languages/proto.cpp
  features/tree_sitter/languages/python.cpp
  features/tree_sitter/languages/query.cpp
  features/tree_sitter/languages/r.cpp
  features/tree_sitter/languages/regex.cpp
  features/tree_sitter/languages/ruby.cpp
  features/tree_sitter/languages/rust.cpp
  features/tree_sitter/languages/scala.cpp
  features/tree_sitter/languages/sql.cpp
  features/tree_sitter/languages/svelte.cpp
  features/tree_sitter/languages/swift.cpp
  features/tree_sitter/languages/systemverilog.cpp
  features/tree_sitter/languages/toml.cpp
  features/tree_sitter/languages/tsx.cpp
  features/tree_sitter/languages/typescript.cpp
  features/tree_sitter/languages/verilog.cpp
  features/tree_sitter/languages/vim.cpp
  features/tree_sitter/languages/vimdoc.cpp
  features/tree_sitter/languages/vue.cpp
  features/tree_sitter/languages/wgsl.cpp
  features/tree_sitter/languages/xml.cpp
  features/tree_sitter/languages/yaml.cpp
  features/tree_sitter/languages/zig.cpp
  features/tree_sitter/languages/zsh.cpp
)

# Module object libraries
add_library(jot_core_obj OBJECT
  core/debugger.cpp
  core/editor.cpp
  core/bookmarks.cpp
  core/event_loop.cpp
  core/file.cpp
  core/folding.cpp
  core/git.cpp
  core/home.cpp
  core/host_api.cpp
  core/in_memory_line_provider.cpp
  core/integrated_terminal.cpp
  core/lazy_line_provider.cpp
  core/lsp.cpp
  core/panes.cpp
  core/popup.cpp
  core/quick_pick.cpp
  core/task_queue.cpp
  core/tasks.cpp
  core/tree_sitter_commands.cpp
  core/theme.cpp
  core/undo.cpp
  core/utils.cpp
  core/workspace.cpp
)
jot_configure_object_target(jot_core_obj)

add_library(jot_edit_obj OBJECT
  edit/clipboard.cpp
  edit/cursor.cpp
  edit/edit.cpp
  edit/feature_datetime.cpp
  edit/feature_duplicate.cpp
  edit/feature_join_lines.cpp
  edit/feature_increment.cpp
  edit/feature_path_clipboard.cpp
  edit/feature_replace.cpp
  edit/feature_reverse_lines.cpp
  edit/feature_shuffle_lines.cpp
  edit/feature_sort_desc.cpp
  edit/feature_stats.cpp
  edit/feature_surround.cpp
  edit/feature_trim_blank.cpp
  edit/feature_unique_lines.cpp
  edit/lines.cpp
  edit/search.cpp
  edit/sidebar.cpp
  edit/transform.cpp
)
jot_configure_object_target(jot_edit_obj)

add_library(jot_features_obj OBJECT
  features/autoclose.cpp
  features/bracket.cpp
  features/config.cpp
  features/cpp_assist.cpp
  features/folding.cpp
  features/text_features.cpp
  features/syntax.cpp
  features/tree_sitter_catalog.cpp
  features/tree_sitter_install.cpp
  features/tree_sitter_language_spec.cpp
  features/tree_sitter_manager.cpp
  features/tree_sitter_queries.cpp
  features/tree_sitter_runtime.cpp
  ${JOT_TREE_SITTER_LANGUAGE_SOURCES}
)
if(JOT_TREESITTER_ENABLED)
  target_compile_definitions(jot_features_obj PRIVATE
    JOT_TREESITTER=1
  )
  target_include_directories(jot_features_obj SYSTEM PRIVATE
    ${TREE_SITTER_INCLUDE_DIRS}
  )
  target_compile_options(jot_features_obj PRIVATE
    ${TREE_SITTER_CFLAGS_OTHER}
  )
endif()
jot_configure_object_target(jot_features_obj)

add_library(jot_input_obj OBJECT
  input/command_actions.cpp
  input/command_execute.cpp
  input/command_help.cpp
  input/command_palette.cpp
  input/command_palette_input.cpp
  input/command_palette_suggestions.cpp
  input/command_utils.cpp
  input/ex_commands.cpp
  input/menu_bar.cpp
  input/command_navigation.cpp
  input/mode_dispatch.cpp
  input/mode_insert.cpp
  input/mode_normal.cpp
  input/mode_transitions.cpp
  input/mode_vim.cpp
  input/mode_visual.cpp
  input/mouse.cpp
)
jot_configure_object_target(jot_input_obj)

add_library(jot_render_obj OBJECT
  render/render.cpp
  render/render_buffer.cpp
  render/render_debugger.cpp
  render/render_easter.cpp
  render/render_minimap.cpp
  render/render_overlay.cpp
  render/render_ui.cpp
)
jot_configure_object_target(jot_render_obj)

add_library(jot_tools_obj OBJECT
  tools/discord_rpc.cpp
  tools/debugger_client.cpp
  tools/imageviewer.cpp
  tools/symbol_index.cpp
  tools/telescope.cpp
  tools/workspace_search.cpp
)
if(WIN32)
  target_sources(jot_tools_obj PRIVATE
    platform/windows/tools/integrated_terminal.cpp
    platform/windows/tools/lsp_client.cpp
  )
else()
  target_sources(jot_tools_obj PRIVATE
    tools/integrated_terminal.cpp
    tools/lsp_client.cpp
  )
endif()
jot_configure_object_target(jot_tools_obj)
target_include_directories(jot_tools_obj SYSTEM PRIVATE ${VTERM_INCLUDE_DIRS})
target_compile_options(jot_tools_obj PRIVATE ${VTERM_CFLAGS_OTHER})

add_library(jot_python_bridge_obj OBJECT
  python_bridge/python_api_bindings.cpp
)
jot_configure_object_target(jot_python_bridge_obj)

add_library(jot_ui_obj OBJECT
  ui/ui_components.cpp
  ui/ui.cpp
  ui/ui_text.cpp
)
if(WIN32)
  target_sources(jot_ui_obj PRIVATE platform/windows/ui/terminal.cpp)
else()
  target_sources(jot_ui_obj PRIVATE ui/terminal.cpp)
endif()
jot_configure_object_target(jot_ui_obj)

# Module static libraries (for larger-project style ownership and reuse)
add_library(jot_core STATIC $<TARGET_OBJECTS:jot_core_obj>)
add_library(jot_edit STATIC $<TARGET_OBJECTS:jot_edit_obj>)
add_library(jot_features STATIC $<TARGET_OBJECTS:jot_features_obj>)
add_library(jot_input STATIC $<TARGET_OBJECTS:jot_input_obj>)
add_library(jot_render STATIC $<TARGET_OBJECTS:jot_render_obj>)
add_library(jot_tools STATIC $<TARGET_OBJECTS:jot_tools_obj>)
add_library(jot_python_bridge STATIC $<TARGET_OBJECTS:jot_python_bridge_obj>)
add_library(jot_ui STATIC $<TARGET_OBJECTS:jot_ui_obj>)

jot_expose_module_api(jot_core)
jot_expose_module_api(jot_edit)
jot_expose_module_api(jot_features)
jot_expose_module_api(jot_input)
jot_expose_module_api(jot_render)
jot_expose_module_api(jot_tools)
jot_expose_module_api(jot_python_bridge)
jot_expose_module_api(jot_ui)

# Aggregated engine library consumed by the app target.
# Built from the same object libraries to avoid duplicate compilation.
add_library(jot_engine STATIC
  $<TARGET_OBJECTS:jot_core_obj>
  $<TARGET_OBJECTS:jot_edit_obj>
  $<TARGET_OBJECTS:jot_features_obj>
  $<TARGET_OBJECTS:jot_input_obj>
  $<TARGET_OBJECTS:jot_render_obj>
  $<TARGET_OBJECTS:jot_tools_obj>
  $<TARGET_OBJECTS:jot_python_bridge_obj>
  $<TARGET_OBJECTS:jot_ui_obj>
)

jot_expose_module_api(jot_engine)
if(JOT_TREESITTER_ENABLED)
  foreach(target_name IN ITEMS jot_core_obj jot_edit_obj jot_input_obj jot_render_obj jot_python_bridge_obj jot_tools_obj jot_ui_obj jot_core jot_edit jot_features jot_input jot_render jot_tools jot_python_bridge jot_ui jot_engine)
    target_compile_definitions(${target_name} PUBLIC JOT_TREESITTER=1)
  endforeach()
  foreach(target_name IN ITEMS jot_core_obj jot_edit_obj jot_input_obj jot_render_obj jot_python_bridge_obj jot_tools_obj jot_ui_obj)
    target_include_directories(${target_name} SYSTEM PRIVATE ${TREE_SITTER_INCLUDE_DIRS})
  endforeach()
  target_link_libraries(jot_features PUBLIC
    ${TREE_SITTER_LIBRARIES}
    ${CMAKE_DL_LIBS}
  )
  target_link_directories(jot_features PUBLIC
    ${TREE_SITTER_LIBRARY_DIRS}
  )
  target_link_libraries(jot_engine PUBLIC
    ${TREE_SITTER_LIBRARIES}
    ${CMAKE_DL_LIBS}
  )
  target_link_directories(jot_engine PUBLIC
    ${TREE_SITTER_LIBRARY_DIRS}
  )
endif()
target_link_libraries(jot_tools PUBLIC ${VTERM_LIBRARIES})
target_link_directories(jot_tools PUBLIC ${VTERM_LIBRARY_DIRS})
target_link_libraries(jot_engine PUBLIC ${VTERM_LIBRARIES})
target_link_directories(jot_engine PUBLIC ${VTERM_LIBRARY_DIRS})
