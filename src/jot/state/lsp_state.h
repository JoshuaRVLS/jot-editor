#ifndef JOT_STATE_LSP_STATE_H
#define JOT_STATE_LSP_STATE_H

#include "features/completion_rank.h" // what the popup learned
#include "jot/model/buffer.h"         // Cursor
#include "jot/model/tasks.h"          // LspInstallJob
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
  // The typing-pause hold on live diagnostics: when a file was last edited as
  // far as the servers are concerned, when it was last saved, and the files
  // whose fresh findings are waiting for the pause. A server's answer paints
  // the moment it arrives only for a file nobody is typing in; for the one
  // under the hands it waits `lsp_diagnostics_quiet_ms` past the last edit (and
  // a save lifts the hold at once). See maybe_paint_held_lsp_diagnostics.
  int lsp_diagnostics_quiet_ms = 0;
  std::map<std::string, long long> lsp_last_edit_ms_;
  std::map<std::string, long long> lsp_last_save_ms_;
  std::set<std::string> lsp_diagnostics_held_;

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
  // nvim-cmp-style ghost text: the selected item's insert text minus the typed
  // prefix, previewed dimmed at the cursor while the popup is open. Only the
  // items that really continue what is typed produce one (ghost_text_for), the
  // painter only draws it where the caret owns the rest of the row, and only once
  // the typing has paused (lsp_completion_typing_ms, below).
  std::string lsp_completion_ghost_text;
  // Steady-clock ms of the last keystroke that changed the word being completed:
  // the preview lands `lsp_completion_ghost_delay_ms` after it, so it never
  // flickers along with the typing that keeps resetting it. The frame loop
  // carries the other half of that -- whether the last frame withheld it -- so
  // that the frame which reveals it is asked for (preview_due_soon).
  long long lsp_completion_typing_ms = 0;
  bool lsp_completion_preview_was_withheld = false;
  std::vector<LSPCompletionItem> lsp_completion_all_items;
  std::vector<LSPCompletionItem> lsp_completion_items;
  // What the popup learned about this user (features/completion_rank.h): the
  // names accepted, counted per language, and the words of the buffer being
  // completed. The table is read from the config home on the first completion and
  // written back on shutdown, like the workspace session; the context is rebuilt
  // once per word, because accepting an item is what changes the words it reads.
  CompletionRank::Usage completion_usage;
  CompletionRank::Context completion_context;
  // The word the context was read for: where the identifier being completed
  // starts. Another letter of that word reuses the read -- the words around the
  // caret have not changed in a way the ranking cares about -- and the next word
  // makes the popup read the buffer again.
  Cursor completion_context_anchor;
  // How many times the words have been read back. The read is only needed once
  // per word (see arm_lsp_completion), so a case can pin that a keystroke in the
  // middle of one does not read the whole window again.
  unsigned long long completion_context_rebuilds = 0;
  bool completion_usage_loaded = false;
  bool completion_usage_dirty = false;
  std::string completion_usage_language;
  // The rect the popup painted on the last frame, border included, in screen
  // cells (x/y are -1 / w is 0 while it is not on screen), plus the window it
  // painted in it: the item index of its first row and how many rows there are.
  // The pointer hit-tests against these: the placement and the scrolling window
  // live in the painter, and a mouse event lands on the picture the user is
  // looking at -- the frame that was painted last.
  int lsp_completion_box_x = -1;
  int lsp_completion_box_y = -1;
  int lsp_completion_box_w = 0;
  int lsp_completion_box_h = 0;
  int lsp_completion_box_start = 0;
  int lsp_completion_box_rows = 0;

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
