# The events module: the main loop, the terminal and GUI event translation,
# and the which-key and keymap-candidate lookups that sit behind them.
# events/gui_events.cpp is appended by src/CMakeLists.txt, and only when the
# GUI frontend is enabled: it is the one file here that needs SDL.
set(JOT_EVENTS_SOURCES
  events/main_loop.cpp
  events/terminal_events.cpp
  events/which_key.cpp
)
