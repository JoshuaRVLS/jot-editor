# The base module: primitives that depend on nothing above them. Nothing here
# may include editor.h, a module header, or any UI header.
set(JOT_BASE_SOURCES
  base/file_util.cpp
)
