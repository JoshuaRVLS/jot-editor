cmake_minimum_required(VERSION 3.14)
project(jot LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
  add_compile_definitions(JOT_PLATFORM_POSIX=1 JOT_PLATFORM_MACOS=1)
elseif(UNIX)
  add_compile_definitions(JOT_PLATFORM_POSIX=1)
elseif(WIN32)
  message(FATAL_ERROR "Windows is not supported yet. Current support targets Linux and macOS (POSIX terminals).")
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
