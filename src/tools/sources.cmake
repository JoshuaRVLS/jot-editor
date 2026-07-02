set(JOT_TOOLS_SOURCES
  tools/debugger/client.cpp
  tools/imageviewer.cpp
  tools/symbols/index.cpp
  tools/telescope.cpp
  tools/telescope_async.cpp
  tools/workspace/search.cpp
)

set(JOT_TOOLS_POSIX_SOURCES
  tools/discord_rpc.cpp
  tools/lsp/client.cpp
  tools/terminal/integrated.cpp
)

set(JOT_TOOLS_WINDOWS_SOURCES
  tools/discord_rpc_win32.cpp
  tools/lsp/client.cpp
  tools/terminal/integrated_win32.cpp
)
