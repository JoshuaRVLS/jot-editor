cmake_minimum_required(VERSION 3.16)
project(jot LANGUAGES C CXX)

include(GNUInstallDirs)

# Bundled third-party dependencies (built before the engine subdirectory).
add_subdirectory(third_party/libvterm)
# glad provides the OpenGL entry points the GUI frontend calls (the system GL
# headers/library only expose the modern API on some platforms: Windows'
# opengl32.dll stops at GL 1.1). It is built unconditionally rather than only
# when the GUI is enabled because ui/gui/gui.h is included by engine translation
# units that are always compiled (editor.cpp, settings.cpp), so the header has
# to be on the include path regardless of JOT_GUI. gl.c needs no system GL.
add_subdirectory(third_party/glad)

# Use a clean, portable install RPATH that points to the same prefix's lib dir.
# Do NOT use CMAKE_INSTALL_RPATH_USE_LINK_PATH: it bakes build-time link
# search paths (e.g. ~/.local/lib) into the installed binary, which breaks
# distribution to other systems. Windows does not use ELF/Mach-O RPATHs.
if(NOT WIN32)
  set(CMAKE_SKIP_BUILD_RPATH FALSE)
  set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
  if(NOT CMAKE_INSTALL_RPATH)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_EXPORT_COMPILE_COMMANDS AND NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  add_custom_target(clangd_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_BINARY_DIR}/compile_commands.json"
      "${CMAKE_SOURCE_DIR}/compile_commands.json"
    COMMENT "Updating compile_commands.json for clangd"
  )
endif()

# JOT_SANITIZE=ON: build with AddressSanitizer + UndefinedBehaviorSanitizer
# for diagnosing native crashes. Set JOT_ASAN_OPTIONS and JOT_UBSAN_OPTIONS
# to control runtime behavior. Off by default.
option(JOT_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
option(JOT_TREESITTER "Enable Tree-sitter parser-based syntax highlighting when dependencies are available" ON)
option(JOT_BUILD_BENCHMARKS "Build the opt-in jot benchmark suite" OFF)
option(JOT_GUI "Compile the SDL2/OpenGL GUI frontend into the engine (available via 'jot --gui') when SDL2 + FreeType are available" ON)
option(JOT_ENABLE_CCACHE "Use ccache as the C/C++ compiler launcher when available" ON)
option(JOT_FETCH_DEPS "Fetch missing third-party dependencies when CMake packages are unavailable" ON)
# Precompiled headers: each object library parses the standard headers its
# sources share once per target instead of once per translation unit. Measured
# on this engine: about 10% off a real compile (3.20 s to 2.87 s for one
# translation unit of lua/api), against 1.5 s to build the header per target.
# It is a poor fit for ccache, which cannot cache a translation unit that uses a
# precompiled header unless the header's baked-in defines and the compiler's
# __DATE__/__TIME__ are allowed to go unverified -- without that, every compile
# is uncacheable and a warm rebuild of one library goes from 1.8 s to 16.7 s.
# The two are wired together below rather than left to fight.
option(JOT_ENABLE_PCH "Precompile the standard-library headers the engine's object libraries share" ON)
set(JOT_CCACHE_ACTIVE OFF)
if(JOT_ENABLE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER OR CMAKE_C_COMPILER_LAUNCHER STREQUAL "")
      set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
    endif()
    if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "")
      set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C++ compiler launcher" FORCE)
    endif()
    set(JOT_CCACHE_ACTIVE ON)
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
  else()
    message(STATUS "ccache not found; compiler launcher disabled")
  endif()
endif()
# A precompiled header bakes its own defines and the compiler's __DATE__/__TIME__
# into the .gch, and ccache cannot see either from the command line, so it
# refuses to cache every translation unit that uses one. Letting those two
# checks go unverified, scoped to jot's own compiles by wrapping the launcher,
# puts the cache back (2.6 s, every call a hit) without touching the cache
# behaviour of anything else built on the machine.
if(JOT_ENABLE_PCH AND JOT_CCACHE_ACTIVE AND CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER
      "${CMAKE_COMMAND};-E;env;CCACHE_SLOPPINESS=pch_defines,time_macros;${CCACHE_PROGRAM}"
      CACHE STRING "C compiler launcher" FORCE)
  set(CMAKE_CXX_COMPILER_LAUNCHER
      "${CMAKE_COMMAND};-E;env;CCACHE_SLOPPINESS=pch_defines,time_macros;${CCACHE_PROGRAM}"
      CACHE STRING "C++ compiler launcher" FORCE)
endif()
# mold is the linker used when it is installed. It links the engine and the
# test binary far faster than the default GNU ld, which is what dominates an
# incremental rebuild. mold does not run on Windows or macOS, so it is only
# looked for on the other platforms, and skipped silently when absent (like
# ccache).
option(JOT_ENABLE_MOLD "Link with mold when it is installed (Linux and BSD only)" ON)
if(JOT_ENABLE_MOLD AND NOT WIN32 AND NOT APPLE)
  find_program(MOLD_PROGRAM mold)
  if(MOLD_PROGRAM)
    add_link_options(-fuse-ld=mold)
    message(STATUS "mold linker enabled: ${MOLD_PROGRAM}")
  else()
    message(STATUS "mold not found; using the default linker")
  endif()
endif()
if(JOT_SANITIZE)
  add_compile_options(-fsanitize=address,undefined
                      -fno-omit-frame-pointer
                      -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

if(APPLE)
  add_compile_definitions(JOT_PLATFORM_POSIX=1 JOT_PLATFORM_MACOS=1)
elseif(UNIX)
  add_compile_definitions(JOT_PLATFORM_POSIX=1)
elseif(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
      "Native Windows support currently targets 64-bit MSVC only. "
      "Reconfigure with -A x64 -T host=x64 and use a 64-bit Python install. "
      "Delete the existing build directory before switching architectures.")
  endif()
  add_compile_definitions(
    JOT_PLATFORM_WINDOWS=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    Py_NO_LINK_LIB
  )
else()
  message(WARNING "Unknown platform. Build may work, but terminal integrations are only guaranteed on POSIX.")
endif()

add_subdirectory(src)
add_subdirectory(apps/jot)

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  add_subdirectory(test)
endif()
if(JOT_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

install(CODE [[
  set(jot_binary "$<TARGET_FILE:jot>")
  if(NOT EXISTS "${jot_binary}")
    message(FATAL_ERROR "jot executable is missing: ${jot_binary}. Run cmake --build first.")
  endif()
  file(SIZE "${jot_binary}" jot_binary_size)
  if(jot_binary_size EQUAL 0)
    message(FATAL_ERROR "jot executable is empty: ${jot_binary}. Delete it and rebuild target 'jot' before installing.")
  endif()
]])

install(TARGETS jot
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# The bundled colour schemes are replaced wholesale, not merged: jot ships two
# themes of its own, so a merge would leave the palettes this build no longer
# carries sitting in the install tree, where they would still be listed by the
# theme chooser. Only the data directory's own copy is cleared -- themes the
# user wrote live in their config directory and are never touched.
install(CODE "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/jot/configs/colors\")")

install(DIRECTORY .configs/configs
  DESTINATION ${CMAKE_INSTALL_DATADIR}/jot
)

install(DIRECTORY runtime/lua
  DESTINATION ${CMAKE_INSTALL_DATADIR}/jot
)

# Vendored LSP payloads. Releases point this at a directory laid out as
# <bin-name>/... (e.g. clangd/ unpacked from the official clangd archive) and
# the tree lands under share/jot/payload, where jot's installer links it into
# its managed bin dir with no network access at all (see
# runtime/lua/lsp/managers/payload.lua). Source builds leave it empty and keep
# using :lspinstall's download managers.
set(JOT_LSP_PAYLOAD_DIR "" CACHE PATH "Vendored LSP payloads to install under share/jot/payload")
if(JOT_LSP_PAYLOAD_DIR)
  if(NOT IS_DIRECTORY "${JOT_LSP_PAYLOAD_DIR}")
    message(FATAL_ERROR "JOT_LSP_PAYLOAD_DIR is not a directory: ${JOT_LSP_PAYLOAD_DIR}")
  endif()
  install(DIRECTORY "${JOT_LSP_PAYLOAD_DIR}/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/jot/payload
  )
endif()

# ── Uninstall target ──────────────────────────────────────────────────────────
# `cmake --build build --target uninstall` removes every file CMake installed.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY
)
add_custom_target(uninstall
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)
