set(JOT_TOOLS_SOURCES
  extras/discord_presence.cpp
  extras/imageviewer.cpp
  extras/telescope.cpp
  extras/telescope_async.cpp
)

set(JOT_TOOLS_POSIX_SOURCES
  extras/discord_rpc.cpp
  extras/discord_rpc_transport_unix.cpp
)

set(JOT_TOOLS_WINDOWS_SOURCES
  extras/discord_rpc.cpp
  extras/discord_rpc_win32.cpp
)
