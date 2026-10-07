# The lsp module: the Language Server Protocol client, its wire format and the
# completion ranking that reads it. Language servers are a subprocess protocol
# client; the editor-side wiring that drives them lives above this module.
set(JOT_LSP_SOURCES
  lsp/completion_rank.cpp
  lsp/install.cpp
)

set(JOT_LSP_POSIX_SOURCES
  lsp/client.cpp
  lsp/json.cpp
  lsp/messages.cpp
  lsp/protocol.cpp
  lsp/requests.cpp
)

set(JOT_LSP_WINDOWS_SOURCES
  lsp/client.cpp
  lsp/json.cpp
  lsp/messages.cpp
  lsp/protocol.cpp
  lsp/requests.cpp
)
