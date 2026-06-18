cmake_minimum_required(VERSION 3.16)
project(jot LANGUAGES C CXX)

# Use a clean, portable install RPATH that points to the same prefix's lib dir.
# Do NOT use CMAKE_INSTALL_RPATH_USE_LINK_PATH: it bakes build-time link
# search paths (e.g. ~/.local/lib) into the installed binary, which breaks
# distribution to other systems.
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
if(NOT CMAKE_INSTALL_RPATH)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_custom_target(clangd_compile_commands ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${CMAKE_SOURCE_DIR}/compile_commands.json"
  COMMENT "Updating compile_commands.json for clangd"
)

# JOT_SANITIZE=ON: build with AddressSanitizer + UndefinedBehaviorSanitizer
# for diagnosing native crashes. Set JOT_ASAN_OPTIONS and JOT_UBSAN_OPTIONS
# to control runtime behavior. Off by default.
option(JOT_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
option(JOT_TREESITTER "Enable Tree-sitter parser-based syntax highlighting when dependencies are available" ON)
option(JOT_BUILD_BENCHMARKS "Build the opt-in jot benchmark suite" OFF)
option(JOT_ENABLE_CCACHE "Use ccache as the C/C++ compiler launcher when available" ON)
if(JOT_ENABLE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER OR CMAKE_C_COMPILER_LAUNCHER STREQUAL "")
      set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
    endif()
    if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "")
      set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C++ compiler launcher" FORCE)
    endif()
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
  else()
    message(STATUS "ccache not found; compiler launcher disabled")
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
  message(FATAL_ERROR "Windows is not supported. Use Linux, macOS, or WSL.")
else()
  message(WARNING "Unknown platform. Build may work, but terminal integrations are only guaranteed on POSIX.")
endif()

find_package(Python3 COMPONENTS Development REQUIRED)

include(cmake/JotPython.cmake)
jot_collect_python_embed_flags(PYTHON_OTHER_FLAGS_LIST PYTHON_INCLUDES PYTHON_LDFLAGS_LIST)

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
  add_subdirectory(tests)
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

install(DIRECTORY src/python/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/jot/python
  FILES_MATCHING
    PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "*.pyo" EXCLUDE
)

install(DIRECTORY .configs/configs
  DESTINATION ${CMAKE_INSTALL_DATADIR}/jot
)

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
