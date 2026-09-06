set(JOT_TOOLS_SOURCES
  tools/debugger/client.cpp
  tools/imageviewer.cpp
  tools/lsp/install.cpp
  tools/symbols/index.cpp
  tools/telescope.cpp
  tools/telescope_async.cpp
  tools/terminal/integrated.cpp
  tools/terminal/terminal_session.cpp
  tools/workspace/search.cpp
)

set(JOT_TOOLS_POSIX_SOURCES
  tools/discord_rpc.cpp
  tools/lsp/client.cpp
)

set(JOT_TOOLS_WINDOWS_SOURCES
  tools/discord_rpc_win32.cpp
  tools/lsp/client.cpp
  tools/terminal/win32_terminal_session.cpp
)
