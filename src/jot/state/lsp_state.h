#ifndef JOT_STATE_LSP_STATE_H
#define JOT_STATE_LSP_STATE_H

#include "jot/model/buffer.h" // Cursor
#include "jot/model/tasks.h"  // LspInstallJob
#include "tools/lsp/client.h" // LSPClient, Diagnostic, completion/signature/inlay models
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// Everything the editor keeps about its language servers and the UI they drive:
// the clients and their install/attach bookkeeping, the merged diagnostics, the
// completion and signature popups, the hover popup, the Ctrl+hover definition
// affordance, the inlay-hint cache, the armed go-to-definition jump, and the
// LSP status modal.
//
// Split out of editor_state.h (which is now the umbrella over src/jot/state/);
// the members and their comments moved verbatim.
struct LspUiState
{
  std::vector<std::unique_ptr<LSPClient>> lsp_clients;
  std::unordered_map<std::string, long long> lsp_pending_changes;
  std::vector<LspInstallJob> lsp_install_jobs;
  // Binaries whose vendored-payload install was already started automatically
  // (see Editor::auto_install_bundled_lsp), so a failed attempt stays a single
  // try instead of re-running on every buffer open.
  std::set<std::string> lsp_bundled_auto_installs;
  std::set<std::string> lsp_disabled_servers;
  // LSP published diagnostics kept per (server|root -> filepath) so several
  // servers attached to one buffer merge instead of clobbering each other.
  std::map<std::string, std::map<std::string, std::vector<Diagnostic>>> lsp_diag_slices_;
  int lsp_change_debounce_ms = 0;

  bool show_lsp_status_modal = false;
  int lsp_status_scroll = 0;
  int lsp_status_hover_row = -1;

  // Hover popup, driven by the mouse (arm on rest, ask after the deadline).
  bool lsp_mouse_hover_enabled = false;
  bool lsp_mouse_hover_pending = false;
  bool lsp_mouse_hover_visible = false;
  long long lsp_mouse_hover_deadline_ms;
  int lsp_mouse_hover_pane = 0;
  int lsp_mouse_hover_buffer = 0;
  int lsp_mouse_hover_line = 0;
  int lsp_mouse_hover_col = 0;
  int lsp_mouse_hover_token_start = 0;
  int lsp_mouse_hover_token_end = 0;
  int lsp_mouse_hover_screen_x = 0;
  int lsp_mouse_hover_screen_y = 0;
  std::string lsp_mouse_hover_filepath;
  // VSCode-style Ctrl+hover goto-definition affordance: while Ctrl is held
  // and the mouse rests on a word, the token under the cursor is underlined
  // (straight underline in the definition-link color) to signal that
  // Ctrl+click will jump to its definition. Cleared on any motion without
  // Ctrl, click, keypress or scroll. Buffer id + token range so stale
  // state never paints after buffer switches.
  bool ctrl_hover_active = false;
  int ctrl_hover_buffer = -1;
  int ctrl_hover_line = -1;
  int ctrl_hover_start = -1;
  int ctrl_hover_end = -1;

  bool lsp_completion_visible = false;
  bool lsp_completion_manual_request = false;
  int lsp_completion_selected = 0;
  Cursor lsp_completion_anchor;
  Cursor lsp_completion_replace_start;
  std::string lsp_completion_filepath;
  // Server that produced the current items (LSPClient::server_id), used by the
  // completion popup to pick per-server label presentation.
  std::string lsp_completion_server;
  std::string lsp_completion_prefix;
  // nvim-cmp-style ghost text: the selected item's insert text minus the
  // typed prefix, previewed dimmed at the cursor while the popup is open.
  std::string lsp_completion_ghost_text;
  std::vector<LSPCompletionItem> lsp_completion_all_items;
  std::vector<LSPCompletionItem> lsp_completion_items;

  bool lsp_signature_visible = false;
  // Position of the '(' that opened the call shown by the signature popup.
  // While the caret stays past this paren on the same line the popup keeps
  // tracking the call.
  int lsp_signature_open_paren_line = 0;
  int lsp_signature_open_paren_col = 0;
  std::string lsp_signature_filepath;
  LSPSignatureHelpResult lsp_signature_result;

  // Per-file cache of textDocument/inlayHint results (parameter-name virtual
  // text on existing code). start_line/end_line are the requested range;
  // dirty means the file changed since the last answer; in_flight guards
  // against stacking requests for the same file.
  struct LspInlayHintCache
  {
    int start_line = -1;
    int end_line = -1;
    bool in_flight = false;
    bool dirty = true;
    std::vector<LSPInlayHint> hints;
  };
  std::map<std::string, LspInlayHintCache> lsp_inlay_hint_caches;

  // Go-to-definition arms its landing spot and applies it inside open_file, the
  // same way a jumplist restore does.
  bool lsp_definition_jump_pending = false;
  LSPLocation lsp_definition_pending_location;
  // What the pending jump is called ("Declaration", "Type definition", ...) so
  // the deferred landing message names the navigation that produced it.
  std::string lsp_navigation_jump_label = "Definition";
};

#endif
