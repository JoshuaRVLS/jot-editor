# The terminal module: the pty-backed terminal sessions and the panel that
# draws them. libvterm lives under this target, not under tools.
set(JOT_TERMINAL_SOURCES
  terminal/integrated.cpp
  terminal/terminal_session.cpp
)

set(JOT_TERMINAL_WINDOWS_SOURCES
  terminal/win32_terminal_session.cpp
)
