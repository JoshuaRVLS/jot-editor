add_executable(jot main.cpp)

target_link_libraries(jot PRIVATE jot_engine ${PYTHON_LDFLAGS_LIST})

# Old GCC libstdc++ may require explicit filesystem linkage.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(jot PRIVATE stdc++fs)
endif()

# forkpty/openpty lives in libutil on Linux and many non-Apple Unix systems.
if(UNIX AND NOT APPLE)
  target_link_libraries(jot PRIVATE util)
endif()
