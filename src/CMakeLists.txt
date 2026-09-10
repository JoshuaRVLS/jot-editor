add_compile_definitions(JOT_DEFAULT_DATA_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/jot")
add_compile_definitions(JOT_LUA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/runtime/lua")

# Embed the bundled runtime files (runtime/lua/*.lua + tree-sitter queries)
# into the binary so it always carries a working copy of the Lua UI /
# features, regenerated whenever they change. Runtime loaders try the user
# config dir and the developer source dir first; the embedded bytes are the
# final fallback (served from memory, or extracted into the cache dir for
# runtimes that need real files on disk).
file(GLOB_RECURSE JOT_LUA_BUNDLED CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/runtime/lua/*.lua" "${PROJECT_SOURCE_DIR}/runtime/lua/*.scm")
set(JOT_LUA_EMBED_CPP "${CMAKE_CURRENT_BINARY_DIR}/embedded_lua.cpp")
add_custom_command(
  OUTPUT "${JOT_LUA_EMBED_CPP}"
  COMMAND ${CMAKE_COMMAND}
    -DLUA_DIR=${PROJECT_SOURCE_DIR}/runtime/lua
    -DOUTPUT=${JOT_LUA_EMBED_CPP}
    -P ${PROJECT_SOURCE_DIR}/cmake/embed_lua.cmake
  DEPENDS ${JOT_LUA_BUNDLED} "${PROJECT_SOURCE_DIR}/cmake/embed_lua.cmake"
  COMMENT "Embedding bundled Lua sources"
  VERBATIM)

function(jot_configure_object_target target_name)
  target_include_directories(${target_name} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/jot
    ${PROJECT_SOURCE_DIR}/src/input
    ${PROJECT_SOURCE_DIR}/src/render
    ${PROJECT_SOURCE_DIR}/src/edit
    ${PROJECT_SOURCE_DIR}/src/features
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/src/jot/lua
    ${PROJECT_SOURCE_DIR}/src/tools
  )

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
  pkg_check_modules(TERMKEY REQUIRED termkey)
  pkg_check_modules(LIBUV REQUIRED libuv)
  pkg_check_modules(UTF8PROC QUIET libutf8proc)
  if(NOT UTF8PROC_FOUND)
    pkg_check_modules(UTF8PROC QUIET utf8proc)
  endif()
endif()

find_path(LUA_INCLUDE_DIR lua.h PATH_SUFFIXES lua5.4 lua54 lua5.3 lua53)
find_library(LUA_LIBRARY NAMES lua5.4 lua54 lua5.3 lua53 lua)
if(LUA_INCLUDE_DIR AND LUA_LIBRARY)
  set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
  set(LUA_LIBRARIES ${LUA_LIBRARY})
else()
  if(NOT JOT_FETCH_DEPS)
    message(FATAL_ERROR "Lua 5.3 or newer is required. Install Lua development files or set JOT_FETCH_DEPS=ON.")
  endif()
  include(FetchContent)
  FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(lua)
  set(LUA_INCLUDE_DIRS ${lua_SOURCE_DIR}/src)
  add_library(jot_lua STATIC
    ${lua_SOURCE_DIR}/src/lapi.c ${lua_SOURCE_DIR}/src/lauxlib.c
    ${lua_SOURCE_DIR}/src/lbaselib.c ${lua_SOURCE_DIR}/src/lcode.c
    ${lua_SOURCE_DIR}/src/lcorolib.c ${lua_SOURCE_DIR}/src/lctype.c
    ${lua_SOURCE_DIR}/src/ldblib.c ${lua_SOURCE_DIR}/src/ldebug.c
    ${lua_SOURCE_DIR}/src/ldo.c ${lua_SOURCE_DIR}/src/ldump.c
    ${lua_SOURCE_DIR}/src/lfunc.c ${lua_SOURCE_DIR}/src/lgc.c
    ${lua_SOURCE_DIR}/src/linit.c ${lua_SOURCE_DIR}/src/liolib.c
    ${lua_SOURCE_DIR}/src/llex.c ${lua_SOURCE_DIR}/src/lmathlib.c
    ${lua_SOURCE_DIR}/src/lmem.c ${lua_SOURCE_DIR}/src/loadlib.c
    ${lua_SOURCE_DIR}/src/lobject.c ${lua_SOURCE_DIR}/src/lopcodes.c
    ${lua_SOURCE_DIR}/src/loslib.c ${lua_SOURCE_DIR}/src/lparser.c
    ${lua_SOURCE_DIR}/src/lstate.c ${lua_SOURCE_DIR}/src/lstring.c
    ${lua_SOURCE_DIR}/src/lstrlib.c ${lua_SOURCE_DIR}/src/ltable.c
    ${lua_SOURCE_DIR}/src/ltablib.c ${lua_SOURCE_DIR}/src/ltm.c
    ${lua_SOURCE_DIR}/src/lundump.c ${lua_SOURCE_DIR}/src/lutf8lib.c
    ${lua_SOURCE_DIR}/src/lvm.c ${lua_SOURCE_DIR}/src/lzio.c)
  target_include_directories(jot_lua PUBLIC ${LUA_INCLUDE_DIRS})
  set(LUA_LIBRARIES jot_lua)
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
  if(NOT TREE_SITTER_FOUND AND JOT_FETCH_DEPS)
    # No system tree-sitter (Arch/CachyOS split the lib into a separate
    # package that install.sh may not have been allowed to install): build
    # the amalgamated runtime from source instead of giving up and falling
    # back to regex highlighting. NOTE: the top-level tree-sitter repo has
    # no CMake project — the build lives in lib/, so add that directly.
    include(FetchContent)
    FetchContent_Declare(tree_sitter_runtime
      GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
      GIT_TAG v0.25.3
      GIT_SHALLOW TRUE
      SOURCE_SUBDIR lib
    )
    FetchContent_MakeAvailable(tree_sitter_runtime)
    if(TARGET tree-sitter)
      set(TREE_SITTER_FOUND TRUE)
      set(TREE_SITTER_LIBRARIES tree-sitter)
      set(TREE_SITTER_INCLUDE_DIRS "${tree_sitter_runtime_SOURCE_DIR}/lib/include")
      # Upstream marks its includes PRIVATE; expose them to our targets.
      target_include_directories(tree-sitter PUBLIC
        "$<BUILD_INTERFACE:${tree_sitter_runtime_SOURCE_DIR}/lib/include>")
      # Install the bundled runtime alongside jot so later reconfigures
      # find it via pkg-config instead of re-fetching: the .pc advertises
      # the same prefix jot installs to.
      install(TARGETS tree-sitter
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
      install(DIRECTORY "${tree_sitter_runtime_SOURCE_DIR}/lib/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
      set(TREE_SITTER_PC "${CMAKE_CURRENT_BINARY_DIR}/tree-sitter.pc")
      file(WRITE "${TREE_SITTER_PC}"
        "prefix=${CMAKE_INSTALL_PREFIX}\n"
        "libdir=\${prefix}/${CMAKE_INSTALL_LIBDIR}\n"
        "includedir=\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}\n"
        "\n"
        "Name: tree-sitter\n"
        "Description: An incremental parsing system for programming tools\n"
        "URL: https://tree-sitter.github.io/tree-sitter/\n"
        "Version: 0.25.3\n"
        "Libs: -L\${libdir} -ltree-sitter\n"
        "Cflags: -I\${includedir}\n")
      install(FILES "${TREE_SITTER_PC}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
      set(TREE_SITTER_LIBRARY_DIRS "")
      set(TREE_SITTER_CFLAGS_OTHER "")
    endif()
  endif()
  if(TREE_SITTER_FOUND)
    set(JOT_TREESITTER_ENABLED TRUE)
    # Propagate to the app/test/benchmark dirs (add_subdirectory scopes).
    set(JOT_TREESITTER_ENABLED TRUE PARENT_SCOPE)
    set(TREE_SITTER_INCLUDE_DIRS "${TREE_SITTER_INCLUDE_DIRS}" PARENT_SCOPE)
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
    ${PROJECT_SOURCE_DIR}/src/jot
    ${PROJECT_SOURCE_DIR}/src/input
    ${PROJECT_SOURCE_DIR}/src/render
    ${PROJECT_SOURCE_DIR}/src/edit
    ${PROJECT_SOURCE_DIR}/src/features
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/src/python_bridge
    ${PROJECT_SOURCE_DIR}/src/tools
  )
endfunction()

include(jot/sources.cmake)
include(edit/sources.cmake)
include(features/sources.cmake)
include(input/sources.cmake)
include(render/sources.cmake)
include(tools/sources.cmake)
include(jot/lua/sources.cmake)
include(ui/sources.cmake)

# SDL2/OpenGL GUI frontend ("jot --gui"). The GUI sources are compiled
# into the engine so the single jot binary serves both backends; when
# SDL2/freetype are missing the build simply continues without them.
# No system OpenGL is required: the GL entry points come from the vendored
# glad (third_party/glad), which resolves them at runtime through SDL, so the
# GUI also builds where only GL 1.1 headers exist.
# JOT_GUI_ENABLED is recorded in the cache so install.sh can tell whether
# the installed binary supports --gui.
set(JOT_GUI_ENABLED FALSE)
if(JOT_GUI AND NOT WIN32)
  pkg_check_modules(SDL2 QUIET sdl2)
  pkg_check_modules(FREETYPE2 QUIET freetype2)
  if(SDL2_FOUND AND FREETYPE2_FOUND)
    set(JOT_GUI_ENABLED TRUE)
    set(JOT_GUI_ENABLED TRUE CACHE INTERNAL "GUI frontend compiled into the engine" FORCE)
    # Vendored OpenGL loader for the GUI frontend: the system GL headers/library
    # only expose the modern API on some platforms (Windows' opengl32.dll stops
    # at GL 1.1), and glad resolves every entry point at runtime through SDL, so
    # nothing needs to link a system GL library. Added here rather than at the
    # top level so a terminal-only build never compiles it.
    add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/glad
                     ${CMAKE_CURRENT_BINARY_DIR}/glad)
    list(APPEND JOT_UI_SOURCES
         ui/gui/gui_setup.cpp
         ui/gui/gui_glyphs.cpp
         ui/gui/gui_batch.cpp
         ui/gui/gui_colors.cpp
         ui/gui/gui_utf8.cpp
         ui/gui/gui_anim.cpp
         ui/gui/gui_render.cpp
         ui/gui/gui_cursor.cpp
         ui/gui/gui_input.cpp)
    list(APPEND JOT_ENGINE_SOURCES jot/event/gui_events.cpp)
  else()
    message(STATUS "JOT_GUI requested but SDL2/freetype2 not found; building terminal-only")
    set(JOT_GUI_ENABLED FALSE CACHE INTERNAL "GUI frontend compiled into the engine" FORCE)
  endif()
endif()

# Module object libraries
add_library(jot_engine_obj OBJECT ${JOT_ENGINE_SOURCES})
jot_configure_object_target(jot_engine_obj)
target_include_directories(jot_engine_obj SYSTEM PRIVATE ${LIBUV_INCLUDE_DIRS})
target_compile_options(jot_engine_obj PRIVATE ${LIBUV_CFLAGS_OTHER})

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
# libvterm is vendored (third_party/libvterm) and used on every platform so
# the integrated terminal renders identically on POSIX and Windows.
target_include_directories(jot_tools_obj SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/libvterm/include)

add_library(jot_lua_api_obj OBJECT ${JOT_LUA_SOURCES} "${JOT_LUA_EMBED_CPP}")
target_include_directories(jot_lua_api_obj PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
jot_configure_object_target(jot_lua_api_obj)
target_include_directories(jot_lua_api_obj SYSTEM PRIVATE ${LUA_INCLUDE_DIRS})

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
if(JOT_GUI_ENABLED)
  target_include_directories(jot_ui_obj SYSTEM PRIVATE
    ${SDL2_INCLUDE_DIRS} ${FREETYPE2_INCLUDE_DIRS}
  )
  target_compile_options(jot_ui_obj PRIVATE
    ${SDL2_CFLAGS_OTHER} ${FREETYPE2_CFLAGS_OTHER}
  )
  # jot_glad carries its own include dir (glad/gl.h) and the loader object, so
  # the GUI sources need nothing from the system's GL headers or library.
  target_link_libraries(jot_ui_obj PRIVATE
    ${SDL2_LIBRARIES} ${FREETYPE2_LIBRARIES} jot_glad
  )
  # Engine translation units (editor.cpp, settings.cpp, gui_events.cpp) include
  # ui/gui/gui.h too, so they need the vendored header as well. This used to work
  # by accident: the system GL header sat in a default include path, which a
  # vendored one does not.
  target_link_libraries(jot_engine_obj PRIVATE jot_glad)
endif()
jot_configure_object_target(jot_ui_obj)

# Module static libraries (for larger-project style ownership and reuse)
add_library(jot_core STATIC $<TARGET_OBJECTS:jot_engine_obj>)
add_library(jot_edit STATIC $<TARGET_OBJECTS:jot_edit_obj>)
add_library(jot_features STATIC $<TARGET_OBJECTS:jot_features_obj>)
add_library(jot_input STATIC $<TARGET_OBJECTS:jot_input_obj>)
add_library(jot_render STATIC $<TARGET_OBJECTS:jot_render_obj>)
add_library(jot_tools STATIC $<TARGET_OBJECTS:jot_tools_obj>)
add_library(jot_lua_api STATIC $<TARGET_OBJECTS:jot_lua_api_obj>)
target_link_libraries(jot_lua_api PUBLIC ${LUA_LIBRARIES})
# The bridge objects compile against the resolved Lua headers above; make
# the include dirs PUBLIC so consumers that pull in the bridge's headers
# (which include lua.h/lualib.h) see the same headers they link against,
# instead of whatever the default include path happens to provide (a newer
# Lua on the system breaks the build with incompatible declarations).
target_include_directories(jot_lua_api SYSTEM PUBLIC ${LUA_INCLUDE_DIRS})
add_library(jot_ui STATIC $<TARGET_OBJECTS:jot_ui_obj>)

jot_expose_module_api(jot_core)
jot_expose_module_api(jot_edit)
jot_expose_module_api(jot_features)
jot_expose_module_api(jot_input)
jot_expose_module_api(jot_render)
jot_expose_module_api(jot_tools)
jot_expose_module_api(jot_lua_api)
jot_expose_module_api(jot_ui)

# Aggregated engine library consumed by the app target.
# Built from the same object libraries to avoid duplicate compilation.
add_library(jot_engine STATIC
  $<TARGET_OBJECTS:jot_engine_obj>
  $<TARGET_OBJECTS:jot_edit_obj>
  $<TARGET_OBJECTS:jot_features_obj>
  $<TARGET_OBJECTS:jot_input_obj>
  $<TARGET_OBJECTS:jot_render_obj>
  $<TARGET_OBJECTS:jot_tools_obj>
  $<TARGET_OBJECTS:jot_lua_api_obj>
  $<TARGET_OBJECTS:jot_ui_obj>
)
target_link_libraries(jot_engine PUBLIC ${LUA_LIBRARIES})
# Same header/lib consistency for the aggregated engine and its consumers
# (the app, the test suite): the linked Lua's headers must be on the
# include path, not whatever /usr/include happens to contain.
target_include_directories(jot_engine SYSTEM PUBLIC ${LUA_INCLUDE_DIRS})

jot_expose_module_api(jot_engine)
if(JOT_TREESITTER_ENABLED)
  foreach(target_name IN ITEMS jot_engine_obj jot_edit_obj jot_input_obj jot_render_obj jot_lua_api_obj jot_tools_obj jot_ui_obj jot_core jot_edit jot_features jot_input jot_render jot_tools jot_lua_api jot_ui jot_engine)
    target_compile_definitions(${target_name} PUBLIC JOT_TREESITTER=1)
  endforeach()
  foreach(target_name IN ITEMS jot_engine_obj jot_edit_obj jot_input_obj jot_render_obj jot_lua_api_obj jot_tools_obj jot_ui_obj)
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
target_link_libraries(jot_tools PUBLIC jot_vterm)
target_link_libraries(jot_engine PUBLIC jot_vterm)
if(NOT WIN32)
  target_link_libraries(jot_ui PUBLIC ${TERMKEY_LIBRARIES})
  target_link_directories(jot_ui PUBLIC ${TERMKEY_LIBRARY_DIRS})
  target_link_libraries(jot_engine PUBLIC ${TERMKEY_LIBRARIES})
  target_link_directories(jot_engine PUBLIC ${TERMKEY_LIBRARY_DIRS})
endif()
target_link_libraries(jot_ui PUBLIC ${UTF8PROC_LIBRARIES})
target_link_directories(jot_ui PUBLIC ${UTF8PROC_LIBRARY_DIRS})
target_link_libraries(jot_engine PUBLIC ${UTF8PROC_LIBRARIES})
target_link_directories(jot_engine PUBLIC ${UTF8PROC_LIBRARY_DIRS})
if(JOT_GUI_ENABLED)
  target_link_libraries(jot_ui PUBLIC ${SDL2_LIBRARIES} ${FREETYPE2_LIBRARIES} jot_glad)
  target_link_directories(jot_ui PUBLIC ${SDL2_LIBRARY_DIRS} ${FREETYPE2_LIBRARY_DIRS})
  target_link_libraries(jot_engine PUBLIC ${SDL2_LIBRARIES} ${FREETYPE2_LIBRARIES} jot_glad)
  target_link_directories(jot_engine PUBLIC ${SDL2_LIBRARY_DIRS} ${FREETYPE2_LIBRARY_DIRS})
endif()
target_link_libraries(jot_core PUBLIC ${LIBUV_LIBRARIES})
target_link_directories(jot_core PUBLIC ${LIBUV_LIBRARY_DIRS})
target_link_libraries(jot_engine PUBLIC ${LIBUV_LIBRARIES})
target_link_directories(jot_engine PUBLIC ${LIBUV_LIBRARY_DIRS})
if(WIN32)
  # Static libuv needs its Windows system dependencies linked explicitly:
  # the vcpkg config does not propagate them, and source builds only export
  # their own targets. Without these the link fails with unresolved
  # ws2_32/dbghelp/iphlpapi/userenv symbols.
  set(LIBUV_WIN32_SYSTEM_LIBS
      ws2_32 psapi userenv iphlpapi dbghelp
      advapi32 user32 shell32 ole32)
  target_link_libraries(jot_core PUBLIC ${LIBUV_WIN32_SYSTEM_LIBS})
  target_link_libraries(jot_engine PUBLIC ${LIBUV_WIN32_SYSTEM_LIBS})
  target_link_libraries(jot_edit PUBLIC user32)
  target_link_libraries(jot_engine PUBLIC user32)
endif()
