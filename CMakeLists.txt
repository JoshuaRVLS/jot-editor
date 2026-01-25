cmake_minimum_required(VERSION 3.14)
project(jcode)

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
if(PYTHON_INCLUDES)
    list(REMOVE_DUPLICATES PYTHON_INCLUDES)
    message(STATUS "Adding Python SYSTEM includes: ${PYTHON_INCLUDES}")
    include_directories(SYSTEM ${PYTHON_INCLUDES})
endif()

# Remove -I flags from PYTHON_CFLAGS to avoid adding them twice or as non-system
string(REGEX REPLACE "-I[^ ]+" "" PYTHON_OTHER_FLAGS "${PYTHON_CFLAGS}")

# Add remaining Python cflags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PYTHON_OTHER_FLAGS}")

# Source files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# Add executable
add_executable(jcode ${SOURCES})

# Include src directory
# target_include_directories(jcode PRIVATE src)


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

target_link_libraries(jcode PRIVATE stdc++fs ${PYTHON_LDFLAGS_LIST})
