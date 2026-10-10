# The core module: the Editor class itself, its state groups and its model
# types (see docs/ARCHITECTURE.md). Everything else under src/ is allowed to
# include this module; nothing here may include a domain that sits above it.
#
# The class body is still assembled from fragments -- core/*_state.h and the
# headers in core/api/ are pieces of it rather than headers that stand alone --
# which is why tools/check_headers.py records them in its baseline.
set(JOT_CORE_SOURCES
  core/decorations_rebase.cpp
  core/editor.cpp
  core/host_api.cpp
  core/relaunch.cpp
  core/resize.cpp
  core/smooth_scroll.cpp
  core/test_hooks.cpp
  core/utils.cpp
)
