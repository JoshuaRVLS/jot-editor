set(JOT_TOOLS_SOURCES
  tools/discord_presence.cpp
  tools/imageviewer.cpp
  tools/lsp/install.cpp
  tools/telescope.cpp
  tools/telescope_async.cpp
  tools/workspace/search.cpp
)

set(JOT_TOOLS_POSIX_SOURCES
  tools/discord_rpc.cpp
  tools/discord_rpc_transport_unix.cpp
  tools/lsp/client.cpp
  tools/lsp/json.cpp
  tools/lsp/messages.cpp
  tools/lsp/protocol.cpp
  tools/lsp/requests.cpp
)

set(JOT_TOOLS_WINDOWS_SOURCES
  tools/discord_rpc.cpp
  tools/discord_rpc_win32.cpp
  tools/lsp/client.cpp
  tools/lsp/json.cpp
  tools/lsp/messages.cpp
  tools/lsp/protocol.cpp
  tools/lsp/requests.cpp
)
