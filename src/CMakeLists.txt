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
  if(LIBUV_INCLUDE_DIRS)
    target_include_directories(${target_name} SYSTEM PRIVATE ${LIBUV_INCLUDE_DIRS})
  endif()
endfunction()

if(WIN32)
  find_package(unofficial-libuv CONFIG QUIET)
  if(TARGET unofficial::libuv::libuv)
    set(LIBUV_LIBRARIES unofficial::libuv::libuv)
    get_target_property(LIBUV_INCLUDE_DIRS unofficial::libuv::libuv INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT LIBUV_INCLUDE_DIRS)
      set(LIBUV_INCLUDE_DIRS "")
    endif()
    set(LIBUV_LIBRARY_DIRS "")
    set(LIBUV_CFLAGS_OTHER "")
  else()
    find_path(LIBUV_INCLUDE_DIR uv.h)
    find_library(LIBUV_LIBRARY NAMES uv libuv)
    if(NOT LIBUV_INCLUDE_DIR OR NOT LIBUV_LIBRARY)
      if(JOT_FETCH_DEPS)
        include(FetchContent)
        set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
          libuv
          GIT_REPOSITORY https://github.com/libuv/libuv.git
          GIT_TAG v1.51.0
        )
        FetchContent_MakeAvailable(libuv)
        if(TARGET uv_a)
          set(LIBUV_INCLUDE_DIRS ${libuv_SOURCE_DIR}/include)
          set(LIBUV_LIBRARIES uv_a)
          set(LIBUV_LIBRARY_DIRS "")
          set(LIBUV_CFLAGS_OTHER "")
        elseif(TARGET uv)
          set(LIBUV_INCLUDE_DIRS ${libuv_SOURCE_DIR}/include)
          set(LIBUV_LIBRARIES uv)
          set(LIBUV_LIBRARY_DIRS "")
          set(LIBUV_CFLAGS_OTHER "")
        else()
          message(FATAL_ERROR "libuv was fetched but no usable CMake target was produced.")
        endif()
      else()
        message(FATAL_ERROR "libuv is required. Install it with vcpkg or set JOT_FETCH_DEPS=ON.")
      endif()
    else()
      set(LIBUV_INCLUDE_DIRS ${LIBUV_INCLUDE_DIR})
      set(LIBUV_LIBRARIES ${LIBUV_LIBRARY})
      set(LIBUV_LIBRARY_DIRS "")
      set(LIBUV_CFLAGS_OTHER "")
    endif()
  endif()

  find_package(utf8proc CONFIG QUIET)
  if(TARGET utf8proc::utf8proc)
    set(UTF8PROC_LIBRARIES utf8proc::utf8proc)
    set(UTF8PROC_INCLUDE_DIRS "")
    set(UTF8PROC_LIBRARY_DIRS "")
    set(UTF8PROC_CFLAGS_OTHER "")
    set(UTF8PROC_FOUND TRUE)
  endif()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VTERM REQUIRED vterm)
  pkg_check_modules(TERMKEY REQUIRED termkey)
  pkg_check_modules(LIBUV REQUIRED libuv)
  pkg_check_modules(UTF8PROC QUIET libutf8proc)
  if(NOT UTF8PROC_FOUND)
    pkg_check_modules(UTF8PROC QUIET utf8proc)
  endif()
endif()
if(NOT UTF8PROC_FOUND)
  find_path(UTF8PROC_INCLUDE_DIR utf8proc.h)
  find_library(UTF8PROC_LIBRARY NAMES utf8proc libutf8proc)
  if(NOT UTF8PROC_INCLUDE_DIR OR NOT UTF8PROC_LIBRARY)
    if(WIN32 AND JOT_FETCH_DEPS)
      include(FetchContent)
      set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
      FetchContent_Declare(
        utf8proc
        GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
        GIT_TAG v2.10.0
      )
      FetchContent_MakeAvailable(utf8proc)
      if(TARGET utf8proc)
        set(UTF8PROC_INCLUDE_DIRS ${utf8proc_SOURCE_DIR})
        set(UTF8PROC_LIBRARIES utf8proc)
        set(UTF8PROC_LIBRARY_DIRS "")
        set(UTF8PROC_CFLAGS_OTHER "")
        set(UTF8PROC_FOUND TRUE)
      elseif(TARGET utf8proc::utf8proc)
        set(UTF8PROC_INCLUDE_DIRS ${utf8proc_SOURCE_DIR})
        set(UTF8PROC_LIBRARIES utf8proc::utf8proc)
        set(UTF8PROC_LIBRARY_DIRS "")
        set(UTF8PROC_CFLAGS_OTHER "")
        set(UTF8PROC_FOUND TRUE)
      else()
        message(FATAL_ERROR "utf8proc was fetched but no usable CMake target was produced.")
      endif()
    else()
      message(FATAL_ERROR "libutf8proc is required. Install libutf8proc/utf8proc development files or set JOT_FETCH_DEPS=ON.")
    endif()
  else()
    set(UTF8PROC_INCLUDE_DIRS ${UTF8PROC_INCLUDE_DIR})
    set(UTF8PROC_LIBRARIES ${UTF8PROC_LIBRARY})
    set(UTF8PROC_LIBRARY_DIRS "")
    set(UTF8PROC_CFLAGS_OTHER "")
  endif()
endif()
if(JOT_TREESITTER)
  if(WIN32)
    find_package(tree-sitter CONFIG QUIET)
    if(TARGET tree-sitter::tree-sitter)
      set(TREE_SITTER_FOUND TRUE)
      set(TREE_SITTER_LIBRARIES tree-sitter::tree-sitter)
      set(TREE_SITTER_INCLUDE_DIRS "")
      set(TREE_SITTER_LIBRARY_DIRS "")
      set(TREE_SITTER_CFLAGS_OTHER "")
    endif()
  else()
    pkg_check_modules(TREE_SITTER tree-sitter)
  endif()
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
if(WIN32)
  target_sources(jot_tools_obj PRIVATE ${JOT_TOOLS_WINDOWS_SOURCES})
else()
  target_sources(jot_tools_obj PRIVATE ${JOT_TOOLS_POSIX_SOURCES})
endif()
jot_configure_object_target(jot_tools_obj)
if(NOT WIN32)
  target_include_directories(jot_tools_obj SYSTEM PRIVATE ${VTERM_INCLUDE_DIRS})
  target_compile_options(jot_tools_obj PRIVATE ${VTERM_CFLAGS_OTHER})
endif()

add_library(jot_python_bridge_obj OBJECT ${JOT_PYTHON_BRIDGE_SOURCES})
jot_configure_object_target(jot_python_bridge_obj)

add_library(jot_ui_obj OBJECT ${JOT_UI_SOURCES})
if(WIN32)
  target_sources(jot_ui_obj PRIVATE ${JOT_UI_WINDOWS_SOURCES})
else()
  target_sources(jot_ui_obj PRIVATE ${JOT_UI_POSIX_SOURCES})
endif()
target_include_directories(jot_ui_obj SYSTEM PRIVATE
  ${UTF8PROC_INCLUDE_DIRS}
)
target_compile_options(jot_ui_obj PRIVATE
  ${UTF8PROC_CFLAGS_OTHER}
)
target_link_libraries(jot_ui_obj PRIVATE ${UTF8PROC_LIBRARIES})
if(NOT WIN32)
  target_include_directories(jot_ui_obj SYSTEM PRIVATE ${TERMKEY_INCLUDE_DIRS})
  target_compile_options(jot_ui_obj PRIVATE ${TERMKEY_CFLAGS_OTHER})
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
if(NOT WIN32)
  target_link_libraries(jot_tools PUBLIC ${VTERM_LIBRARIES})
  target_link_directories(jot_tools PUBLIC ${VTERM_LIBRARY_DIRS})
  target_link_libraries(jot_engine PUBLIC ${VTERM_LIBRARIES})
  target_link_directories(jot_engine PUBLIC ${VTERM_LIBRARY_DIRS})
  target_link_libraries(jot_ui PUBLIC ${TERMKEY_LIBRARIES})
  target_link_directories(jot_ui PUBLIC ${TERMKEY_LIBRARY_DIRS})
  target_link_libraries(jot_engine PUBLIC ${TERMKEY_LIBRARIES})
  target_link_directories(jot_engine PUBLIC ${TERMKEY_LIBRARY_DIRS})
endif()
target_link_libraries(jot_ui PUBLIC ${UTF8PROC_LIBRARIES})
target_link_directories(jot_ui PUBLIC ${UTF8PROC_LIBRARY_DIRS})
target_link_libraries(jot_engine PUBLIC ${UTF8PROC_LIBRARIES})
target_link_directories(jot_engine PUBLIC ${UTF8PROC_LIBRARY_DIRS})
target_link_libraries(jot_core PUBLIC ${LIBUV_LIBRARIES})
target_link_directories(jot_core PUBLIC ${LIBUV_LIBRARY_DIRS})
target_link_libraries(jot_engine PUBLIC ${LIBUV_LIBRARIES})
target_link_directories(jot_engine PUBLIC ${LIBUV_LIBRARY_DIRS})
if(WIN32)
  target_link_libraries(jot_edit PUBLIC user32)
  target_link_libraries(jot_engine PUBLIC user32)
endif()
