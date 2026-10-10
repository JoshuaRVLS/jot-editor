# The terminal module: the pty-backed terminal sessions, the panel that draws
# them, and the editor commands that open one and float it. libvterm lives
# under this target, not under extras.
set(JOT_TERMINAL_SOURCES
  terminal/integrated.cpp
  terminal/integrated_terminal.cpp
  terminal/terminal_float.cpp
  terminal/terminal_session.cpp
)

set(JOT_TERMINAL_WINDOWS_SOURCES
  terminal/win32_terminal_session.cpp
)
