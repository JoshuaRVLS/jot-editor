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
  core/bookmarks.cpp
  core/event_loop.cpp
  core/file.cpp
  core/git.cpp
  core/home.cpp
  core/host_api.cpp
  core/integrated_terminal.cpp
  core/lsp.cpp
  core/panes.cpp
  core/popup.cpp
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
  features/text_features.cpp
  features/syntax.cpp
)
jot_configure_object_target(jot_features_obj)

add_library(jot_input_obj OBJECT
  input/command_actions.cpp
  input/command_palette.cpp
  input/command_utils.cpp
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
  render/render_easter.cpp
  render/render_minimap.cpp
  render/render_overlay.cpp
  render/render_ui.cpp
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
