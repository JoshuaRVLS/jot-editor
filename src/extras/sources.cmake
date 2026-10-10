# The extras module: the optional features the core is not allowed to include.
# The bible verse, coding time, discord presence, emmet expansion, the LeetCode
# bridge and the WakaTime heartbeat all hook into the editor from the outside,
# and so do the four that came out of the editor and tools directories: the REST
# client, the workspace symbol index, the markdown preview server and its HTTP
# framing, and the LeetCode keyring.
set(JOT_EXTRAS_SOURCES
  extras/bible.cpp
  extras/bible_client.cpp
  extras/coding_time/probe.cpp
  extras/coding_time/wiring.cpp
  extras/discord_session.cpp
  extras/emmet/expand.cpp
  extras/emmet/wiring.cpp
  extras/leetcode_credentials.cpp
  extras/leetcode_http.cpp
  extras/markdown/http_message.cpp
  extras/markdown/preview_server.cpp
  extras/rest_client.cpp
  extras/status_clock.cpp
  extras/wakatime.cpp
  extras/web_index.cpp
)

# What used to be src/tools/: discord presence and its IPC client, the
# telescope file finder and the image viewer. They keep their own object
# library rather than folding into jot_extras_obj -- the targets are not part
# of this refactor -- but the list lives here now, because tools/ does not.
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
