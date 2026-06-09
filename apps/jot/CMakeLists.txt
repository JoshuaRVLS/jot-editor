add_executable(jot main.cpp)

target_link_libraries(jot PRIVATE jot_engine ${PYTHON_LDFLAGS_LIST})

# Use a clean, portable install RPATH so the installed jot binary can find
# runtime libraries in ${CMAKE_INSTALL_LIBDIR} relative to the binary.
# Do NOT use INSTALL_RPATH_USE_LINK_PATH: that would bake build-time link
# search paths (e.g. ~/.local/lib) into the installed binary.
set_target_properties(jot PROPERTIES
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
  INSTALL_RPATH_USE_LINK_PATH FALSE
)

# Old GCC libstdc++ may require explicit filesystem linkage.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(jot PRIVATE stdc++fs)
endif()

# forkpty/openpty lives in libutil on Linux and many non-Apple Unix systems.
if(UNIX AND NOT APPLE)
  target_link_libraries(jot PRIVATE util)
endif()
