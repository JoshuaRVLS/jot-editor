cmake_minimum_required(VERSION 3.14)
project(jot)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Python3
find_package(Python3 COMPONENTS Development REQUIRED)

# Get Python3 compile flags from python3-config
execute_process(
    COMMAND python3-config --cflags
    OUTPUT_VARIABLE PYTHON_CFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Extract include directories from PYTHON_CFLAGS and add them as SYSTEM includes
# This avoids compilation errors with GCC 15 and std::regex/placement new
message(STATUS "Parsing Python CFLAGS: ${PYTHON_CFLAGS}")
string(REGEX MATCHALL "-I([^ ]+)" PYTHON_INCLUDE_FLAGS "${PYTHON_CFLAGS}")
set(PYTHON_INCLUDES "")
foreach(INC_FLAG ${PYTHON_INCLUDE_FLAGS})
    # Remove -I prefix
    string(SUBSTRING "${INC_FLAG}" 2 -1 INC_DIR)
    list(APPEND PYTHON_INCLUDES "${INC_DIR}")
endforeach()

# Add system includes
# if(PYTHON_INCLUDES)
#     list(REMOVE_DUPLICATES PYTHON_INCLUDES)
#     message(STATUS "Adding Python SYSTEM includes: ${PYTHON_INCLUDES}")
#     include_directories(SYSTEM ${PYTHON_INCLUDES})
# endif()
# MOVED to target_include_directories below

# Remove -I flags from PYTHON_CFLAGS to avoid adding them twice or as non-system
string(REGEX REPLACE "-I[^ ]+" "" PYTHON_OTHER_FLAGS "${PYTHON_CFLAGS}")

# Add remaining Python cflags ONLY to jot target
string(STRIP "${PYTHON_OTHER_FLAGS}" PYTHON_OTHER_FLAGS)
separate_arguments(PYTHON_OTHER_FLAGS_LIST UNIX_COMMAND "${PYTHON_OTHER_FLAGS}")

# Source files — GLOB_RECURSE picks up all subdirectories automatically
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# python_api_impl.cpp is #included by python_api_bindings.cpp (unity build)
# so it must NOT be compiled as a standalone translation unit
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/python_api_impl.cpp")

# Add executable
add_executable(jot ${SOURCES})
target_compile_options(jot PRIVATE ${PYTHON_OTHER_FLAGS_LIST}) # Apply flags locally

# Include all source subdirectories (flat #include "file.h" style works everywhere)
target_include_directories(jot PRIVATE
    src
    src/core
    src/input
    src/render
    src/edit
    src/features
    src/ui
    src/plugins
    src/tools
)

if(PYTHON_INCLUDES)
    list(REMOVE_DUPLICATES PYTHON_INCLUDES)
    target_include_directories(jot SYSTEM PRIVATE ${PYTHON_INCLUDES})
endif()


# Link libraries
# Get Python3 link flags
execute_process(
    COMMAND python3-config --ldflags --embed
    OUTPUT_VARIABLE PYTHON_LDFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Strip whitespace and split arguments for correct linking
string(STRIP "${PYTHON_LDFLAGS}" PYTHON_LDFLAGS)
separate_arguments(PYTHON_LDFLAGS_LIST UNIX_COMMAND "${PYTHON_LDFLAGS}")

target_link_libraries(jot PRIVATE stdc++fs ${PYTHON_LDFLAGS_LIST})

install(TARGETS jot
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY python/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/jot/python
    FILES_MATCHING PATTERN "*.py"
)

install(DIRECTORY .configs/configs
    DESTINATION ${CMAKE_INSTALL_DATADIR}/jot
)

# Tests
# enable_testing()
# add_executable(jot_tests tests/test_main.cpp tests/test_features.cpp src/features.cpp)
# target_include_directories(jot_tests PRIVATE src)
# target_compile_options(jot_tests PRIVATE ${PYTHON_OTHER_FLAGS_LIST})
# add_test(NAME FeaturesTest COMMAND jot_tests)
