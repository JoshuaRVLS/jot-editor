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
  # Only standard-library headers go in the precompiled header: they are stable
  # across edits, so the PCH stays valid while the project's own sources change.
  # The list is the headers five or more of the engine's own files include, so
  # it is worth parsing once per target rather than once per file.
  if(JOT_ENABLE_PCH)
    target_precompile_headers(${target_name} PRIVATE
      <algorithm>
      <array>
      <atomic>
      <cctype>
      <cerrno>
      <chrono>
      <cmath>
      <csignal>
      <cstddef>
      <cstdint>
      <cstdio>
      <cstdlib>
      <cstring>
      <ctime>
      <filesystem>
      <fstream>
      <functional>
      <iostream>
      <limits>
      <map>
      <memory>
      <optional>
      <set>
      <sstream>
      <stdexcept>
      <string>
      <system_error>
      <thread>
      <unordered_map>
      <unordered_set>
      <utility>
      <vector>
    )
  endif()
  target_include_directories(${target_name} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/base
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/input
    ${PROJECT_SOURCE_DIR}/src/render
    ${PROJECT_SOURCE_DIR}/src/text
    ${PROJECT_SOURCE_DIR}/src/extras
    ${PROJECT_SOURCE_DIR}/src/syntax
    ${PROJECT_SOURCE_DIR}/src/dap
    ${PROJECT_SOURCE_DIR}/src/lsp
    ${PROJECT_SOURCE_DIR}/src/terminal
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/src/workspace
    ${PROJECT_SOURCE_DIR}/src/git
    ${PROJECT_SOURCE_DIR}/src/lua
    ${PROJECT_SOURCE_DIR}/src/buffers
    ${PROJECT_SOURCE_DIR}/src/panes
    ${PROJECT_SOURCE_DIR}/src/commands
    ${PROJECT_SOURCE_DIR}/src/events
    ${PROJECT_SOURCE_DIR}/src/surfaces
    ${PROJECT_SOURCE_DIR}/src/gui
  )

  if(LIBUV_INCLUDE_DIRS)
    target_include_directories(${target_name} SYSTEM PRIVATE ${LIBUV_INCLUDE_DIRS})
  endif()
  # A system curl lives in the compiler's own include path; a fetched one does
  # not, and its static archive also needs CURL_STATICLIB on the consumers.
  # The object libraries take the usage requirements only -- an object library
  # has no link step of its own, and jot_engine links the library itself.
  if(TARGET CURL::libcurl)
    target_link_libraries(${target_name} PRIVATE CURL::libcurl)
  endif()
endfunction()

# Every dependency below is folded into the one jot executable, so each is asked
# for a static archive. Upstream tree-sitter's lib/CMakeLists.txt defaults
# BUILD_SHARED_LIBS to ON, which would leave a release needing
# libtree-sitter.dll (or .so) beside the binary, and utf8proc follows the same
# variable. Forced and set before the first fetch: each dependency reads the
# value when its own CMakeLists runs, and a stale ON in an existing cache would
# otherwise keep producing DLLs.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build fetched dependencies as static libraries" FORCE)

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
        # We link uv_a (the static target); upstream also builds and installs a
        # shared uv, which would put a libuv.dll in every package for a library
        # the exe does not load.
        set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
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

# libcurl drives the REST surfaces (the LeetCode bridge, the .http client,
# the provider adapters). A Linux install finds it as a system package; the
# mingw-w64 cross build has no curl of its own to install, so it is fetched the
# way libuv and utf8proc are (JOT_FETCH_DEPS). curl's CMake build makes the
# same CURL::libcurl target either way.
find_package(CURL QUIET)
if(NOT CURL_FOUND)
  if(WIN32 AND JOT_FETCH_DEPS)
    include(FetchContent)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
      curl
      GIT_REPOSITORY https://github.com/curl/curl.git
      GIT_TAG curl-8_11_1
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(curl)
    if(NOT TARGET CURL::libcurl)
      message(FATAL_ERROR "libcurl was fetched but no CURL::libcurl target was produced.")
    endif()
  else()
    message(FATAL_ERROR "libcurl is required. Install libcurl development files or set JOT_FETCH_DEPS=ON.")
  endif()
endif()

# The embedded Lua runtime: LuaJIT, and nothing else.
#
# LuaJIT is Lua 5.1 plus the JIT, and the bundled runtime is written to exactly
# that dialect -- docs/LUA_API.md, "The scripting dialect" -- with
# lua/lua_compat.h standing in for the four C entry points it lacks. One engine
# means one set of answers: a feature file behaves the same on every machine.
#
# A system LuaJIT first: a distribution package keeps the build offline and is
# the same runtime the distro's own tools link against. A machine without one
# builds the pinned release from source, with JOT_FETCH_DEPS.
if(WIN32)
  find_path(LUAJIT_INCLUDE_DIR luajit.h PATH_SUFFIXES luajit-2.1 luajit-2.0)
  find_library(LUAJIT_LIBRARY NAMES luajit-5.1 luajit)
  if(LUAJIT_INCLUDE_DIR AND LUAJIT_LIBRARY)
    set(LUAJIT_FOUND TRUE)
    set(LUAJIT_INCLUDE_DIRS ${LUAJIT_INCLUDE_DIR})
    set(LUAJIT_LIBRARIES ${LUAJIT_LIBRARY})
  endif()
else()
  pkg_check_modules(LUAJIT QUIET luajit)
endif()

if(LUAJIT_FOUND)
  set(LUA_INCLUDE_DIRS ${LUAJIT_INCLUDE_DIRS})
  set(LUA_LIBRARIES ${LUAJIT_LIBRARIES})
  if(LUAJIT_VERSION)
    message(STATUS "Embedded Lua runtime: LuaJIT ${LUAJIT_VERSION} (${LUAJIT_LIBRARIES})")
  else()
    message(STATUS "Embedded Lua runtime: LuaJIT (${LUAJIT_LIBRARIES})")
  endif()
else()
  if(NOT JOT_FETCH_DEPS)
    message(FATAL_ERROR "LuaJIT is required. Install its development files or set JOT_FETCH_DEPS=ON.")
  endif()
  # LuaJIT has no CMake build of its own: it generates the interpreter's tables
  # with a host build of its own tools, so this wraps `src/Makefile` instead of
  # reimplementing it.
  include(FetchContent)
  FetchContent_Declare(luajit
    GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
    GIT_TAG v2.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(luajit)
  include(ProcessorCount)
  ProcessorCount(JOT_LUAJIT_JOBS)
  if(JOT_LUAJIT_JOBS EQUAL 0)
    set(JOT_LUAJIT_JOBS 1)
  endif()
  find_program(JOT_MAKE_EXECUTABLE NAMES make gmake REQUIRED)
  set(JOT_LUAJIT_LIB "${luajit_SOURCE_DIR}/src/libluajit.a")
  set(JOT_LUAJIT_MAKE_ARGS libluajit.a)
  if(WIN32)
    # The Windows release cross-builds with mingw, so the target system is
    # Windows and the tools carry the toolchain prefix off the compiler CMake
    # was given. LuaJIT builds its host tools from HOST_CC either way, and
    # BUILDMODE=static is what keeps a lua51.dll out of the import table.
    get_filename_component(JOT_LUAJIT_CC "${CMAKE_C_COMPILER}" NAME)
    string(REGEX REPLACE "gcc(\\.exe)?$" "" JOT_LUAJIT_CROSS "${JOT_LUAJIT_CC}")
    find_program(JOT_HOST_CC NAMES cc gcc clang REQUIRED)
    list(APPEND JOT_LUAJIT_MAKE_ARGS
      HOST_CC=${JOT_HOST_CC}
      CROSS=${JOT_LUAJIT_CROSS}
      TARGET_SYS=Windows
      BUILDMODE=static)
  else()
    # CFLAGS=-fPIC because the archive lands in an executable this project
    # builds as PIE.
    list(APPEND JOT_LUAJIT_MAKE_ARGS CFLAGS=-fPIC)
  endif()
  add_custom_command(
    OUTPUT "${JOT_LUAJIT_LIB}"
    COMMAND "${JOT_MAKE_EXECUTABLE}" -C src ${JOT_LUAJIT_MAKE_ARGS} -j${JOT_LUAJIT_JOBS}
    WORKING_DIRECTORY "${luajit_SOURCE_DIR}"
    COMMENT "Building LuaJIT (${luajit_SOURCE_DIR})"
    VERBATIM)
  add_custom_target(jot_luajit_make DEPENDS "${JOT_LUAJIT_LIB}")
  add_library(jot_luajit STATIC IMPORTED GLOBAL)
  set_target_properties(jot_luajit PROPERTIES IMPORTED_LOCATION "${JOT_LUAJIT_LIB}")
  # LuaJIT writes its generated headers (luajit.h and the arch tables) into
  # src/ while that make runs, so the include dir and the archive are one
  # dependency: the interface target here, the build its prerequisite.
  add_library(jot_luajit_dep INTERFACE)
  target_include_directories(jot_luajit_dep INTERFACE "${luajit_SOURCE_DIR}/src")
  target_link_libraries(jot_luajit_dep INTERFACE jot_luajit ${CMAKE_DL_LIBS})
  add_dependencies(jot_luajit_dep jot_luajit_make)
  set(LUA_INCLUDE_DIRS "${luajit_SOURCE_DIR}/src")
  set(LUA_LIBRARIES jot_luajit_dep)
  message(STATUS "Embedded Lua runtime: LuaJIT (built from source at ${luajit_SOURCE_DIR})")
endif()

# lua/lua_compat.h reads the runtime's version out of <lua.h>, so the shims a
# translation unit gets are chosen by the headers it compiles against instead
# of by a definition threaded to every target that touches Lua.

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
# Propagate to the app/test/benchmark dirs (add_subdirectory scopes). The
# benchmark target compiles ui/text.cpp directly, so it needs utf8proc on its
# own link line rather than inheriting it from the engine target.
set(UTF8PROC_INCLUDE_DIRS "${UTF8PROC_INCLUDE_DIRS}" PARENT_SCOPE)
set(UTF8PROC_LIBRARIES "${UTF8PROC_LIBRARIES}" PARENT_SCOPE)
set(UTF8PROC_LIBRARY_DIRS "${UTF8PROC_LIBRARY_DIRS}" PARENT_SCOPE)
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
    # no CMake project - the build lives in lib/, so add that directly.
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
  # Every consumer (app, tests, benchmarks) sees src/ and each module dir, so
  # headers are included by their module name ("syntax/folding.h") or bare
  # depending on the module. There is deliberately no separate include/ tree:
  # it used to hold three empty shims that only renamed headers already here,
  # into two roots at once.
  target_include_directories(${target_name} PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/base
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/input
    ${PROJECT_SOURCE_DIR}/src/render
    ${PROJECT_SOURCE_DIR}/src/text
    ${PROJECT_SOURCE_DIR}/src/extras
    ${PROJECT_SOURCE_DIR}/src/syntax
    ${PROJECT_SOURCE_DIR}/src/dap
    ${PROJECT_SOURCE_DIR}/src/lsp
    ${PROJECT_SOURCE_DIR}/src/terminal
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/src/workspace
    ${PROJECT_SOURCE_DIR}/src/git
    ${PROJECT_SOURCE_DIR}/src/lua
    ${PROJECT_SOURCE_DIR}/src/buffers
    ${PROJECT_SOURCE_DIR}/src/panes
    ${PROJECT_SOURCE_DIR}/src/commands
    ${PROJECT_SOURCE_DIR}/src/events
    ${PROJECT_SOURCE_DIR}/src/surfaces
    ${PROJECT_SOURCE_DIR}/src/gui
  )
endfunction()

include(base/sources.cmake)
include(core/sources.cmake)
include(text/sources.cmake)
include(syntax/sources.cmake)
include(buffers/sources.cmake)
include(panes/sources.cmake)
include(commands/sources.cmake)
include(events/sources.cmake)
include(surfaces/sources.cmake)
include(dap/sources.cmake)
include(lsp/sources.cmake)
include(terminal/sources.cmake)
include(workspace/sources.cmake)
include(git/sources.cmake)
include(extras/sources.cmake)
include(input/sources.cmake)
include(render/sources.cmake)
include(lua/sources.cmake)
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
  # Ligature shaping (gui/gui_ligatures.cpp): optional, like the rest of the
  # GUI. Without harfbuzz the frontend still builds and paints every cell with
  # its own cmap glyph -- joins render as the plain characters, which is what
  # the frontend always did.
  pkg_check_modules(HARFBUZZ QUIET harfbuzz)
  if(SDL2_FOUND AND FREETYPE2_FOUND)
    set(JOT_GUI_ENABLED TRUE)
    set(JOT_GUI_ENABLED TRUE CACHE INTERNAL "GUI frontend compiled into the engine" FORCE)
    # Official window icon: the raw RGBA bytes (packaging/jot-icon-128.rgba,
    # produced by packaging/make_gui_icon.py from packaging/windows/icon.png)
    # are embedded at build time so SDL_SetWindowIcon never needs an installed
    # data file. The script validates the byte count against SIZE.
    set(JOT_GUI_ICON_RGRA "${PROJECT_SOURCE_DIR}/packaging/jot-icon-128.rgba")
    set(JOT_GUI_ICON_CPP "${CMAKE_CURRENT_BINARY_DIR}/gui_icon_data.cpp")
    add_custom_command(
      OUTPUT "${JOT_GUI_ICON_CPP}"
      COMMAND ${CMAKE_COMMAND}
        -DINPUT=${JOT_GUI_ICON_RGRA}
        -DSIZE=128
        -DOUTPUT=${JOT_GUI_ICON_CPP}
        -P ${PROJECT_SOURCE_DIR}/cmake/embed_icon.cmake
      DEPENDS "${JOT_GUI_ICON_RGRA}" "${PROJECT_SOURCE_DIR}/cmake/embed_icon.cmake"
      COMMENT "Embedding the jot window icon"
      VERBATIM)
    # The test suite has a GUI keysym test that needs SDL's key constants; hand
    # the include dirs up the way TREE_SITTER_INCLUDE_DIRS is handed up above.
    set(SDL2_INCLUDE_DIRS "${SDL2_INCLUDE_DIRS}" PARENT_SCOPE)
    # The font catalog test reads faces through FreeType directly.
    set(FREETYPE2_INCLUDE_DIRS "${FREETYPE2_INCLUDE_DIRS}" PARENT_SCOPE)
    # Shaped glyph ids are public API of gui.h (ensure_shaped_glyph), so any
    # target compiling against it needs harfbuzz's include dir too.
    set(HARFBUZZ_INCLUDE_DIRS "${HARFBUZZ_INCLUDE_DIRS}" PARENT_SCOPE)
    set(HARFBUZZ_LIBRARIES "${HARFBUZZ_LIBRARIES}" PARENT_SCOPE)
    set(HARFBUZZ_CFLAGS_OTHER "${HARFBUZZ_CFLAGS_OTHER}" PARENT_SCOPE)
    if(HARFBUZZ_FOUND)
      message(STATUS "GUI ligature shaping enabled (harfbuzz ${HARFBUZZ_VERSION})")
    else()
      message(STATUS "harfbuzz not found; the GUI will not shape ligatures")
    endif()
    list(APPEND JOT_UI_SOURCES
         gui/font_catalog.cpp
         gui/gui_setup.cpp
         gui/gui_glyphs.cpp
         gui/gui_batch.cpp
         gui/gui_colors.cpp
         gui/gui_utf8.cpp
         gui/gui_anim.cpp
         gui/gui_render.cpp
         gui/gui_cursor.cpp
         gui/gui_input.cpp
         gui/gui_ligatures.cpp
         "${JOT_GUI_ICON_CPP}")
    list(APPEND JOT_EVENTS_SOURCES events/gui_events.cpp)
  else()
    message(STATUS "JOT_GUI requested but SDL2/freetype2 not found; building terminal-only")
    set(JOT_GUI_ENABLED FALSE CACHE INTERNAL "GUI frontend compiled into the engine" FORCE)
  endif()
endif()

# Module object libraries
add_library(jot_base_obj OBJECT ${JOT_BASE_SOURCES})
jot_configure_object_target(jot_base_obj)

# What used to be one JOT_EDITOR_SOURCES list is now one list per domain, and
# they are still compiled into the same object library: the module boundary is
# the directory and its sources.cmake, not the archive it lands in.
add_library(jot_editor_obj OBJECT
  ${JOT_CORE_SOURCES}
  ${JOT_BUFFERS_SOURCES}
  ${JOT_PANES_SOURCES}
  ${JOT_COMMANDS_SOURCES}
  ${JOT_EVENTS_SOURCES}
  ${JOT_SURFACES_SOURCES}
)
jot_configure_object_target(jot_editor_obj)
target_include_directories(jot_editor_obj SYSTEM PRIVATE ${LIBUV_INCLUDE_DIRS})
target_compile_options(jot_editor_obj PRIVATE ${LIBUV_CFLAGS_OTHER})
# Unconditional (not tied to JOT_GUI): engine sources that are always compiled
# include gui/gui.h, which pulls in glad/gl.h.
target_link_libraries(jot_editor_obj PRIVATE jot_glad)

add_library(jot_text_obj OBJECT ${JOT_TEXT_SOURCES})
jot_configure_object_target(jot_text_obj)

add_library(jot_syntax_obj OBJECT ${JOT_SYNTAX_SOURCES})
if(JOT_TREESITTER_ENABLED)
  target_compile_definitions(jot_syntax_obj PRIVATE
    JOT_TREESITTER=1
  )
  target_include_directories(jot_syntax_obj SYSTEM PRIVATE
    ${TREE_SITTER_INCLUDE_DIRS}
  )
  target_compile_options(jot_syntax_obj PRIVATE
    ${TREE_SITTER_CFLAGS_OTHER}
  )
endif()
jot_configure_object_target(jot_syntax_obj)

add_library(jot_extras_obj OBJECT ${JOT_EXTRAS_SOURCES})
jot_configure_object_target(jot_extras_obj)

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

add_library(jot_dap_obj OBJECT ${JOT_DAP_SOURCES})
jot_configure_object_target(jot_dap_obj)

add_library(jot_workspace_obj OBJECT ${JOT_WORKSPACE_SOURCES})
jot_configure_object_target(jot_workspace_obj)

add_library(jot_git_obj OBJECT ${JOT_GIT_SOURCES})
jot_configure_object_target(jot_git_obj)

add_library(jot_lsp_obj OBJECT ${JOT_LSP_SOURCES})
if(WIN32)
  target_sources(jot_lsp_obj PRIVATE ${JOT_LSP_WINDOWS_SOURCES})
else()
  target_sources(jot_lsp_obj PRIVATE ${JOT_LSP_POSIX_SOURCES})
endif()
jot_configure_object_target(jot_lsp_obj)

add_library(jot_terminal_obj OBJECT ${JOT_TERMINAL_SOURCES})
if(WIN32)
  target_sources(jot_terminal_obj PRIVATE ${JOT_TERMINAL_WINDOWS_SOURCES})
endif()
# libvterm is vendored (third_party/libvterm) and used on every platform so
# the integrated terminal renders identically on POSIX and Windows.
target_include_directories(jot_terminal_obj SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/libvterm/include)
jot_configure_object_target(jot_terminal_obj)

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
    ${SDL2_INCLUDE_DIRS} ${FREETYPE2_INCLUDE_DIRS} ${HARFBUZZ_INCLUDE_DIRS}
  )
  target_compile_options(jot_ui_obj PRIVATE
    ${SDL2_CFLAGS_OTHER} ${FREETYPE2_CFLAGS_OTHER} ${HARFBUZZ_CFLAGS_OTHER}
  )
  # jot_glad carries its own include dir (glad/gl.h) and the loader object, so
  # the GUI sources need nothing from the system's GL headers or library.
  target_link_libraries(jot_ui_obj PRIVATE
    ${SDL2_LIBRARIES} ${FREETYPE2_LIBRARIES} ${HARFBUZZ_LIBRARIES} jot_glad
  )
endif()
jot_configure_object_target(jot_ui_obj)

# Module static libraries (for larger-project style ownership and reuse)
add_library(jot_base STATIC $<TARGET_OBJECTS:jot_base_obj>)
add_library(jot_editor STATIC $<TARGET_OBJECTS:jot_editor_obj>)
add_library(jot_text STATIC $<TARGET_OBJECTS:jot_text_obj>)
add_library(jot_syntax STATIC $<TARGET_OBJECTS:jot_syntax_obj>)
add_library(jot_extras STATIC $<TARGET_OBJECTS:jot_extras_obj>)
add_library(jot_input STATIC $<TARGET_OBJECTS:jot_input_obj>)
add_library(jot_render STATIC $<TARGET_OBJECTS:jot_render_obj>)
add_library(jot_tools STATIC $<TARGET_OBJECTS:jot_tools_obj>)
add_library(jot_dap STATIC $<TARGET_OBJECTS:jot_dap_obj>)
add_library(jot_lsp STATIC $<TARGET_OBJECTS:jot_lsp_obj>)
add_library(jot_workspace STATIC $<TARGET_OBJECTS:jot_workspace_obj>)
add_library(jot_git STATIC $<TARGET_OBJECTS:jot_git_obj>)
add_library(jot_terminal STATIC $<TARGET_OBJECTS:jot_terminal_obj>)
add_library(jot_lua_api STATIC $<TARGET_OBJECTS:jot_lua_api_obj>)
target_link_libraries(jot_lua_api PUBLIC ${LUA_LIBRARIES})
# The bridge objects compile against the resolved Lua headers above; make
# the include dirs PUBLIC so consumers that pull in the bridge's headers
# (which include lua.h/lualib.h) see the same headers they link against,
# instead of whatever the default include path happens to provide (a newer
# Lua on the system breaks the build with incompatible declarations).
target_include_directories(jot_lua_api SYSTEM PUBLIC ${LUA_INCLUDE_DIRS})
add_library(jot_ui STATIC $<TARGET_OBJECTS:jot_ui_obj>)

jot_expose_module_api(jot_base)
jot_expose_module_api(jot_editor)
jot_expose_module_api(jot_text)
jot_expose_module_api(jot_syntax)
jot_expose_module_api(jot_extras)
jot_expose_module_api(jot_input)
jot_expose_module_api(jot_render)
jot_expose_module_api(jot_tools)
jot_expose_module_api(jot_dap)
jot_expose_module_api(jot_lsp)
jot_expose_module_api(jot_workspace)
jot_expose_module_api(jot_git)
jot_expose_module_api(jot_terminal)
jot_expose_module_api(jot_lua_api)
jot_expose_module_api(jot_ui)

# Aggregated engine library consumed by the app target.
# Built from the same object libraries to avoid duplicate compilation.
add_library(jot_engine STATIC
  $<TARGET_OBJECTS:jot_base_obj>
  $<TARGET_OBJECTS:jot_editor_obj>
  $<TARGET_OBJECTS:jot_text_obj>
  $<TARGET_OBJECTS:jot_syntax_obj>
  $<TARGET_OBJECTS:jot_extras_obj>
  $<TARGET_OBJECTS:jot_input_obj>
  $<TARGET_OBJECTS:jot_render_obj>
  $<TARGET_OBJECTS:jot_tools_obj>
  $<TARGET_OBJECTS:jot_dap_obj>
  $<TARGET_OBJECTS:jot_lsp_obj>
  $<TARGET_OBJECTS:jot_workspace_obj>
  $<TARGET_OBJECTS:jot_git_obj>
  $<TARGET_OBJECTS:jot_terminal_obj>
  $<TARGET_OBJECTS:jot_lua_api_obj>
  $<TARGET_OBJECTS:jot_ui_obj>
)
target_link_libraries(jot_engine PUBLIC ${LUA_LIBRARIES} CURL::libcurl)
if(APPLE)
  target_link_libraries(jot_engine PUBLIC "-framework Security" "-framework CoreFoundation")
endif()
# Same header/lib consistency for the aggregated engine and its consumers
# (the app, the test suite): the linked Lua's headers must be on the
# include path, not whatever /usr/include happens to contain.
target_include_directories(jot_engine SYSTEM PUBLIC ${LUA_INCLUDE_DIRS})

jot_expose_module_api(jot_engine)
if(JOT_TREESITTER_ENABLED)
  foreach(target_name IN ITEMS jot_base_obj jot_editor_obj jot_text_obj jot_syntax_obj jot_input_obj jot_render_obj jot_lua_api_obj jot_tools_obj jot_dap_obj jot_lsp_obj jot_workspace_obj jot_git_obj jot_terminal_obj jot_extras_obj jot_ui_obj jot_base jot_editor jot_text jot_syntax jot_extras jot_input jot_render jot_tools jot_dap jot_lsp jot_workspace jot_git jot_terminal jot_lua_api jot_ui jot_engine)
    target_compile_definitions(${target_name} PUBLIC JOT_TREESITTER=1)
  endforeach()
  # jot_extras_obj compiles the wiring files that include editor.h, which reaches
  # syntax/manager.h and its <tree_sitter/api.h>. The define loop above already
  # names it; this one has to as well, or the include only resolves where the
  # tree-sitter headers happen to sit on the default search path (Linux) and the
  # mingw cross-build stops on bible_client.cpp.
  foreach(target_name IN ITEMS jot_base_obj jot_editor_obj jot_text_obj jot_syntax_obj jot_input_obj jot_render_obj jot_lua_api_obj jot_tools_obj jot_dap_obj jot_lsp_obj jot_workspace_obj jot_git_obj jot_terminal_obj jot_extras_obj jot_ui_obj)
    target_include_directories(${target_name} SYSTEM PRIVATE ${TREE_SITTER_INCLUDE_DIRS})
  endforeach()
  target_link_libraries(jot_syntax PUBLIC
    ${TREE_SITTER_LIBRARIES}
    ${CMAKE_DL_LIBS}
  )
  target_link_directories(jot_syntax PUBLIC
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

# The GUI frontend is optional, so the always-compiled code that reaches for it
# needs a compile-time switch. Without one a JOT_GUI=OFF build fails to link:
# the referencing translation units are still compiled, but the GUI sources that
# define UIGui -- and with them its vtable and typeinfo -- are not, so the
# references are undefined rather than merely unused. Defined on the object
# targets *and* the libraries they are folded into, because $<TARGET_OBJECTS:>
# does not carry usage requirements across.
if(JOT_GUI_ENABLED)
  foreach(target_name IN ITEMS jot_base_obj jot_editor_obj jot_text_obj jot_syntax_obj jot_input_obj jot_render_obj jot_lua_api_obj jot_tools_obj jot_dap_obj jot_lsp_obj jot_workspace_obj jot_git_obj jot_terminal_obj jot_extras_obj jot_ui_obj jot_base jot_editor jot_text jot_syntax jot_extras jot_input jot_render jot_tools jot_dap jot_lsp jot_workspace jot_git jot_terminal jot_lua_api jot_ui jot_engine)
    target_compile_definitions(${target_name} PUBLIC JOT_GUI=1)
    # A second switch, only when harfbuzz was found: the shaper compiles to a
    # stub without it, and UIGui::ligatures_supported() answers for it at run
    # time so the settings menu can say the toggle does nothing here.
    if(HARFBUZZ_FOUND)
      target_compile_definitions(${target_name} PUBLIC JOT_GUI_LIGATURES=1)
    endif()
  endforeach()
endif()
target_link_libraries(jot_terminal PUBLIC jot_vterm)
target_link_libraries(jot_engine PUBLIC jot_vterm)
# Always propagated, not just when the GUI is enabled: gui/gui.h is included
# by engine sources that are always compiled (editor.cpp, settings.cpp) and its
# inline atlas guard calls gl* functions.
target_link_libraries(jot_engine PUBLIC jot_glad)
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
  target_link_libraries(jot_ui PUBLIC ${SDL2_LIBRARIES} ${FREETYPE2_LIBRARIES} ${HARFBUZZ_LIBRARIES} jot_glad)
  target_link_directories(jot_ui PUBLIC ${SDL2_LIBRARY_DIRS} ${FREETYPE2_LIBRARY_DIRS})
  target_link_libraries(jot_engine PUBLIC ${SDL2_LIBRARIES} ${FREETYPE2_LIBRARIES} ${HARFBUZZ_LIBRARIES} jot_glad)
  target_link_directories(jot_engine PUBLIC ${SDL2_LIBRARY_DIRS} ${FREETYPE2_LIBRARY_DIRS})
endif()
target_link_libraries(jot_editor PUBLIC ${LIBUV_LIBRARIES})
target_link_directories(jot_editor PUBLIC ${LIBUV_LIBRARY_DIRS})
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
  target_link_libraries(jot_editor PUBLIC ${LIBUV_WIN32_SYSTEM_LIBS})
  target_link_libraries(jot_engine PUBLIC ${LIBUV_WIN32_SYSTEM_LIBS})
  target_link_libraries(jot_text PUBLIC user32)
  target_link_libraries(jot_engine PUBLIC user32)
endif()
