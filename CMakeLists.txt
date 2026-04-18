cmake_minimum_required(VERSION 3.14)
project(jot LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Keep backwards-compatible CLI name.
install(PROGRAMS $<TARGET_FILE:jot>
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  RENAME jcode
)

install(DIRECTORY python/
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
