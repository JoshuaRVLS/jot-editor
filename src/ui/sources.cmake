set(JOT_UI_SOURCES
  ui/components.cpp
  ui/cursor_blink.cpp
  ui/cursor_probe.cpp
  gui/gui_fit.cpp
  ui/smooth_scroll.cpp
  ui/tab_order.cpp
  ui/text.cpp
  ui/ui.cpp
  ui/xterm_palette.cpp
)

set(JOT_UI_POSIX_SOURCES
  ui/input_reader.cpp
  ui/terminal.cpp
)

set(JOT_UI_WINDOWS_SOURCES
  ui/terminal_win32.cpp
)
