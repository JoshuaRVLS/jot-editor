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
pkg_check_modules(TERMKEY REQUIRED termkey)
pkg_check_modules(LIBUV REQUIRED libuv)
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

include(features/tree_sitter/languages/sources.cmake)
include(core/sources.cmake)
include(edit/sources.cmake)
include(features/sources.cmake)
include(input/sources.cmake)
include(render/sources.cmake)
include(tools/sources.cmake)
include(python_bridge/sources.cmake)
include(ui/sources.cmake)

# Module object libraries
add_library(jot_core_obj OBJECT ${JOT_CORE_SOURCES})
jot_configure_object_target(jot_core_obj)
target_include_directories(jot_core_obj SYSTEM PRIVATE ${LIBUV_INCLUDE_DIRS})
target_compile_options(jot_core_obj PRIVATE ${LIBUV_CFLAGS_OTHER})

add_library(jot_edit_obj OBJECT ${JOT_EDIT_SOURCES})
jot_configure_object_target(jot_edit_obj)

add_library(jot_features_obj OBJECT ${JOT_FEATURES_SOURCES})
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

add_library(jot_input_obj OBJECT ${JOT_INPUT_SOURCES})
jot_configure_object_target(jot_input_obj)

add_library(jot_render_obj OBJECT ${JOT_RENDER_SOURCES})
jot_configure_object_target(jot_render_obj)

add_library(jot_tools_obj OBJECT ${JOT_TOOLS_SOURCES})
target_sources(jot_tools_obj PRIVATE ${JOT_TOOLS_POSIX_SOURCES})
jot_configure_object_target(jot_tools_obj)
target_include_directories(jot_tools_obj SYSTEM PRIVATE ${VTERM_INCLUDE_DIRS})
target_compile_options(jot_tools_obj PRIVATE ${VTERM_CFLAGS_OTHER})

add_library(jot_python_bridge_obj OBJECT ${JOT_PYTHON_BRIDGE_SOURCES})
jot_configure_object_target(jot_python_bridge_obj)

add_library(jot_ui_obj OBJECT ${JOT_UI_SOURCES})
target_sources(jot_ui_obj PRIVATE ${JOT_UI_POSIX_SOURCES})
target_include_directories(jot_ui_obj SYSTEM PRIVATE ${TERMKEY_INCLUDE_DIRS})
target_compile_options(jot_ui_obj PRIVATE ${TERMKEY_CFLAGS_OTHER})
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
target_link_libraries(jot_ui PUBLIC ${TERMKEY_LIBRARIES})
target_link_directories(jot_ui PUBLIC ${TERMKEY_LIBRARY_DIRS})
target_link_libraries(jot_engine PUBLIC ${TERMKEY_LIBRARIES})
target_link_directories(jot_engine PUBLIC ${TERMKEY_LIBRARY_DIRS})
target_link_libraries(jot_core PUBLIC ${LIBUV_LIBRARIES})
target_link_directories(jot_core PUBLIC ${LIBUV_LIBRARY_DIRS})
target_link_libraries(jot_engine PUBLIC ${LIBUV_LIBRARIES})
target_link_directories(jot_engine PUBLIC ${LIBUV_LIBRARY_DIRS})
