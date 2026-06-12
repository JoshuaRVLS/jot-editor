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
    message(STATUS "Tree-sitter runtime found; parser highlighting enabled for installed grammars")
    set(JOT_TS_GRAMMAR_PACKAGES
      c:tree-sitter-c
      cpp:tree-sitter-cpp
      python:tree-sitter-python
      javascript:tree-sitter-javascript
      typescript:tree-sitter-typescript
      rust:tree-sitter-rust
      go:tree-sitter-go
      json:tree-sitter-json
      html:tree-sitter-html
      css:tree-sitter-css
      bash:tree-sitter-bash
      lua:tree-sitter-lua
      markdown:tree-sitter-markdown
      toml:tree-sitter-toml
      yaml:tree-sitter-yaml
    )
    foreach(grammar_spec IN LISTS JOT_TS_GRAMMAR_PACKAGES)
      string(REPLACE ":" ";" grammar_parts "${grammar_spec}")
      list(GET grammar_parts 0 grammar_name)
      list(GET grammar_parts 1 grammar_pkg)
      string(TOUPPER "${grammar_name}" grammar_upper)
      string(REPLACE "-" "_" grammar_upper "${grammar_upper}")
      set(grammar_var "TREE_SITTER_${grammar_upper}")
      set(grammar_found FALSE)
      pkg_check_modules(${grammar_var} ${grammar_pkg})
      if(${grammar_var}_FOUND)
        list(APPEND JOT_TS_GRAMMAR_DEFINES "JOT_TS_HAS_${grammar_upper}=1")
        list(APPEND JOT_TS_GRAMMAR_LIBRARIES ${${grammar_var}_LIBRARIES})
        list(APPEND JOT_TS_GRAMMAR_LIBRARY_DIRS ${${grammar_var}_LIBRARY_DIRS})
        list(APPEND JOT_TS_GRAMMAR_INCLUDE_DIRS ${${grammar_var}_INCLUDE_DIRS})
        list(APPEND JOT_TS_GRAMMAR_CFLAGS ${${grammar_var}_CFLAGS_OTHER})
        set(grammar_found TRUE)
        message(STATUS "Tree-sitter grammar found by pkg-config: ${grammar_name}")
      else()
        string(REPLACE "-" "_" grammar_lib_alt "${grammar_pkg}")
        string(REPLACE "-" "_" grammar_header_alt "${grammar_pkg}")
        find_library(JOT_TS_${grammar_upper}_LIBRARY
          NAMES ${grammar_pkg} ${grammar_lib_alt}
        )
        find_path(JOT_TS_${grammar_upper}_INCLUDE_DIR
          NAMES
            tree_sitter/${grammar_pkg}.h
            tree_sitter/${grammar_header_alt}.h
        )
        if(JOT_TS_${grammar_upper}_LIBRARY AND JOT_TS_${grammar_upper}_INCLUDE_DIR)
          list(APPEND JOT_TS_GRAMMAR_DEFINES "JOT_TS_HAS_${grammar_upper}=1")
          list(APPEND JOT_TS_GRAMMAR_LIBRARIES ${JOT_TS_${grammar_upper}_LIBRARY})
          list(APPEND JOT_TS_GRAMMAR_INCLUDE_DIRS ${JOT_TS_${grammar_upper}_INCLUDE_DIR})
          set(grammar_found TRUE)
          message(STATUS "Tree-sitter grammar found directly: ${grammar_name}")
        endif()
      endif()
      if(NOT grammar_found)
        message(STATUS "Tree-sitter grammar missing: ${grammar_name}; regex fallback will be used")
      endif()
    endforeach()
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
  features/tree_sitter_install.cpp
  features/tree_sitter_manager.cpp
)
if(JOT_TREESITTER_ENABLED)
  target_compile_definitions(jot_features_obj PRIVATE
    JOT_TREESITTER=1
    ${JOT_TS_GRAMMAR_DEFINES}
  )
  target_include_directories(jot_features_obj SYSTEM PRIVATE
    ${TREE_SITTER_INCLUDE_DIRS}
    ${JOT_TS_GRAMMAR_INCLUDE_DIRS}
  )
  target_compile_options(jot_features_obj PRIVATE
    ${TREE_SITTER_CFLAGS_OTHER}
    ${JOT_TS_GRAMMAR_CFLAGS}
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
  tools/telescope.cpp
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
  ui/ui.cpp
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
    ${JOT_TS_GRAMMAR_LIBRARIES}
  )
  target_link_directories(jot_features PUBLIC
    ${TREE_SITTER_LIBRARY_DIRS}
    ${JOT_TS_GRAMMAR_LIBRARY_DIRS}
  )
  target_link_libraries(jot_engine PUBLIC
    ${TREE_SITTER_LIBRARIES}
    ${JOT_TS_GRAMMAR_LIBRARIES}
  )
  target_link_directories(jot_engine PUBLIC
    ${TREE_SITTER_LIBRARY_DIRS}
    ${JOT_TS_GRAMMAR_LIBRARY_DIRS}
  )
endif()
target_link_libraries(jot_tools PUBLIC ${VTERM_LIBRARIES})
target_link_directories(jot_tools PUBLIC ${VTERM_LIBRARY_DIRS})
target_link_libraries(jot_engine PUBLIC ${VTERM_LIBRARIES})
target_link_directories(jot_engine PUBLIC ${VTERM_LIBRARY_DIRS})
