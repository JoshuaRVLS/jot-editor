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
    ${PROJECT_SOURCE_DIR}/src/plugins
    ${PROJECT_SOURCE_DIR}/src/tools
  )

  if(PYTHON_INCLUDES)
    target_include_directories(${target_name} SYSTEM PRIVATE ${PYTHON_INCLUDES})
  endif()
endfunction()

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
    ${PROJECT_SOURCE_DIR}/src/plugins
    ${PROJECT_SOURCE_DIR}/src/tools
  )
endfunction()

# Module object libraries
add_library(jot_core_obj OBJECT
  core/editor.cpp
  core/editor_bookmarks.cpp
  core/editor_event_loop.cpp
  core/editor_file.cpp
  core/editor_git.cpp
  core/editor_home.cpp
  core/editor_host_api.cpp
  core/editor_integrated_terminal.cpp
  core/editor_lsp.cpp
  core/editor_panes.cpp
  core/editor_popup.cpp
  core/editor_theme.cpp
  core/editor_undo.cpp
  core/editor_utils.cpp
  core/editor_workspace.cpp
)
jot_configure_object_target(jot_core_obj)

add_library(jot_edit_obj OBJECT
  edit/editor_clipboard.cpp
  edit/editor_cursor.cpp
  edit/editor_edit.cpp
  edit/editor_lines.cpp
  edit/editor_search.cpp
  edit/editor_sidebar.cpp
)
jot_configure_object_target(jot_edit_obj)

add_library(jot_features_obj OBJECT
  features/autoclose.cpp
  features/bracket.cpp
  features/config.cpp
  features/editor_features.cpp
  features/syntax.cpp
)
jot_configure_object_target(jot_features_obj)

add_library(jot_input_obj OBJECT
  input/editor_command_actions.cpp
  input/editor_command_line.cpp
  input/editor_command_navigation.cpp
  input/editor_mode_dispatch.cpp
  input/editor_mode_insert.cpp
  input/editor_mode_normal.cpp
  input/editor_mode_transitions.cpp
  input/editor_mode_vim.cpp
  input/editor_mode_visual.cpp
  input/editor_mouse.cpp
)
jot_configure_object_target(jot_input_obj)

add_library(jot_render_obj OBJECT
  render/editor_render.cpp
  render/editor_render_buffer.cpp
  render/editor_render_easter.cpp
  render/editor_render_minimap.cpp
  render/editor_render_overlay.cpp
  render/editor_render_ui.cpp
)
jot_configure_object_target(jot_render_obj)

add_library(jot_tools_obj OBJECT
  tools/imageviewer.cpp
  tools/integrated_terminal.cpp
  tools/lsp_client.cpp
  tools/telescope.cpp
)
jot_configure_object_target(jot_tools_obj)

add_library(jot_plugins_obj OBJECT
  plugins/python_api_bindings.cpp
)
jot_configure_object_target(jot_plugins_obj)

add_library(jot_ui_obj OBJECT
  ui/terminal.cpp
  ui/ui.cpp
)
jot_configure_object_target(jot_ui_obj)

# Module static libraries (for larger-project style ownership and reuse)
add_library(jot_core STATIC $<TARGET_OBJECTS:jot_core_obj>)
add_library(jot_edit STATIC $<TARGET_OBJECTS:jot_edit_obj>)
add_library(jot_features STATIC $<TARGET_OBJECTS:jot_features_obj>)
add_library(jot_input STATIC $<TARGET_OBJECTS:jot_input_obj>)
add_library(jot_render STATIC $<TARGET_OBJECTS:jot_render_obj>)
add_library(jot_tools STATIC $<TARGET_OBJECTS:jot_tools_obj>)
add_library(jot_plugins STATIC $<TARGET_OBJECTS:jot_plugins_obj>)
add_library(jot_ui STATIC $<TARGET_OBJECTS:jot_ui_obj>)

jot_expose_module_api(jot_core)
jot_expose_module_api(jot_edit)
jot_expose_module_api(jot_features)
jot_expose_module_api(jot_input)
jot_expose_module_api(jot_render)
jot_expose_module_api(jot_tools)
jot_expose_module_api(jot_plugins)
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
  $<TARGET_OBJECTS:jot_plugins_obj>
  $<TARGET_OBJECTS:jot_ui_obj>
)

jot_expose_module_api(jot_engine)
