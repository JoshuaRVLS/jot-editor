# The base module: primitives that depend on nothing above them. Nothing here
# may include editor.h, a module header, or any UI header.
set(JOT_BASE_SOURCES
  base/event_loop.cpp
  base/file_util.cpp
  base/process_job.cpp
  base/task_queue.cpp
)
