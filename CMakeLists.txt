cmake_minimum_required(VERSION 3.14)
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

# JOT_SANITIZE=ON: build with AddressSanitizer + UndefinedBehaviorSanitizer
# for diagnosing native crashes. Set JOT_ASAN_OPTIONS and JOT_UBSAN_OPTIONS
# to control runtime behavior. Off by default.
option(JOT_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
option(JOT_TREESITTER "Enable Tree-sitter parser-based syntax highlighting when dependencies are available" ON)
option(JOT_BUILD_BENCHMARKS "Build the opt-in jot benchmark suite" OFF)
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
  add_compile_definitions(JOT_PLATFORM_WINDOWS=1)
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
  add_subdirectory(tests)
endif()
if(JOT_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

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
