# The buffers module: opening, saving and reloading files, the buffer list,
# the undo stack, autosave, recent files and the per-buffer fold state.
set(JOT_BUFFERS_SOURCES
  buffers/auto_save.cpp
  buffers/buffers.cpp
  buffers/file.cpp
  buffers/fold_state.cpp
  buffers/recent.cpp
  buffers/undo.cpp
)
