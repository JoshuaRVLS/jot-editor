# The lsp module: the Language Server Protocol client, its wire format, the
# completion ranking that reads it, and the editor-side glue that drives them
# (attach, completion, hover, diagnostics, code actions, format, inlay hints,
# references, rename, signature help, status) beside the pieces of the Editor
# class they add to: lsp_state.h, lsp_ui.h and lsp_attach_data.h.
# lsp/install.cpp is the protocol side of installing a server;
# lsp/install_editor.cpp is the editor command that asks for one.
set(JOT_LSP_SOURCES
  lsp/attach.cpp
  lsp/code_actions.cpp
  lsp/completion.cpp
  lsp/completion_rank.cpp
  lsp/diagnostics.cpp
  lsp/format.cpp
  lsp/hover.cpp
  lsp/inlay_hints.cpp
  lsp/install.cpp
  lsp/install_editor.cpp
  lsp/lifecycle.cpp
  lsp/references.cpp
  lsp/rename.cpp
  lsp/signature.cpp
  lsp/status.cpp
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
