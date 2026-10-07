// ---------------------------------------------------------------------------
// LSP surfaces and navigation
// ---------------------------------------------------------------------------
//
// The pickers LSP answers feed (quick pick, symbols, diagnostics), the
// completion / signature / hover popups, go-to-definition and the jumplist.
//
// A fragment of the Editor class body, included by src/editor/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
bool handle_quick_pick_input(int ch);
void open_quick_pick(QuickPickKind kind,
                     const std::string &title,
                     std::vector<QuickPickItem> items,
                     const std::string &query = "");
void close_quick_pick();
void refresh_quick_pick();
int quick_pick_match_score(const std::string &query, const QuickPickItem &item) const;
void accept_quick_pick();
void show_project_search(const std::string &query = "");
void show_diagnostics_picker();
bool goto_next_diagnostic(int direction);
std::vector<QuickPickItem> diagnostic_quick_pick_items() const;
void show_symbol_picker();
void request_document_symbols();
void handle_document_symbols_result(const LSPDocumentSymbolResult &result);
std::vector<QuickPickItem> fallback_symbol_items();
void request_lsp_completion(bool manual, char trigger_character = '\0');
void request_lsp_signature_help(char trigger_character = '\0');
// Re-fires signature help when the caret sits inside an open call's argument
// list, even when the popup is not up yet (e.g. auto-close already inserted
// the closing ')' and the user is typing the first argument). No-op when the
// caret is outside any open call.
void refresh_lsp_signature_if_in_call();
void hide_lsp_signature();
void request_lsp_hover();
void request_lsp_hover_at(int pane_index,
                          int buffer_id,
                          const Cursor &pos,
                          int token_start,
                          int token_end,
                          int screen_x,
                          int screen_y);
void cancel_lsp_mouse_hover(bool hide_popup = true);
void maybe_fire_lsp_mouse_hover();
// Dismisses a Lua-rendered hover float (notifies the jot.lsp.hover_ui
// handler); no-op when the Lua hover UI is not registered.
void close_lua_hover_ui();
// Location lookups: definition, declaration, type definition, implementation
// all share one request/reply path and differ only in the method sent.
void request_lsp_definition();
void request_lsp_declaration();
void request_lsp_type_definition();
void request_lsp_implementation();
void request_lsp_navigation(LSPNavigationKind kind);
// clangd's switchSourceHeader: opens the paired header/source, or reports
// that there is none.
void switch_lsp_source_header();
void handle_lsp_switch_source_header_result(const std::string &filepath);
void lsp_rename_symbol(const std::string &new_name);
void request_lsp_references();
void handle_lsp_references_results();
void request_lsp_code_actions();
void handle_lsp_code_action_results();
bool apply_selected_lsp_code_action();
void handle_lsp_hover_result(const LSPHoverResult &hover);
void handle_lsp_signature_result(const LSPSignatureHelpResult &signature_help);
void handle_lsp_definition_result(const LSPDefinitionResult &definition);
bool apply_pending_lsp_definition_jump();
// Applies a jumplist restore once the target file is open.
bool apply_pending_jump();
// Jumplist. record_jump() goes at the end of any navigation that moves the
// cursor somewhere else (a picker, a definition, a search hit, another file);
// back/forward then walk those places.
JumpLocation capture_jump_location();
void record_jump();
bool jump_to(const JumpLocation &loc);
void jump_back();
void jump_forward();
void show_jumplist_picker();
// Workspace-wide LSP pickers: symbols by name across the project, and the
// diagnostics every attached server has reported (open files or not).
void show_workspace_symbols_picker();
void request_workspace_symbols(const std::string &query);
void handle_workspace_symbols_result(const LSPDocumentSymbolResult &result);
void show_workspace_diagnostics_picker();
std::vector<QuickPickItem> workspace_diagnostic_quick_pick_items() const;
// The definition checks' findings alone, in the order a jump walks them (file,
// then line) rather than the panel's severity-first order.
std::vector<QuickPickItem> cpp_definition_quick_pick_items() const;
// Steps to the next (or previous) definition finding from the caret, opening
// its file; `:cppcheck next|prev`, and what an announced scan lands on.
bool goto_next_cpp_definition_issue(int direction);
// The Problems view's header row: the totals, the severity tally and how many
// findings each file holds, truncated to the panel's width.
std::string problems_summary_header(int max_width) const;
void hide_lsp_completion();
// Records the site a completion belongs to -- the identifier token under the
// caret that accepting will replace, the path, and the prefix as typed so far.
// The request path calls this before asking the server, and the filter reads
// the same fields back on every keystroke, so what is filtered against is what
// the request was made with.
void arm_lsp_completion(const std::string &filepath, bool manual);
bool refresh_lsp_completion_filter();
// Whether the popup ranks by what the user picks and what the file already
// holds (`lsp_completion_learn`). Off is the plain order, and nothing touches
// the learned table below.
bool completion_rank_enabled();
// How fast a learned habit fades (`lsp_completion_learn_half_life_days`, 0 to
// never forget), in seconds.
int64_t completion_half_life_seconds();
// The learned table, read from the config home once per editor and written back
// on shutdown. A missing file is an empty table, not an error.
void load_completion_usage();
void save_completion_usage();
// The words of the buffer being completed, read once per popup: accepting an
// item changes them, and the next list ranks against the text it landed in.
void rebuild_completion_context();
// One acceptance, counted for the language of the file being completed.
void record_completion_use(const std::string &name);
// The learned relevance of one row's name: a habit of the user's, plus the
// file's own words (which decay with distance from the caret). 0 when learning
// is off, which is what leaves the list in its plain order.
int completion_rank_points(const std::string &name);
// The workspace's own CSS vocabulary (workspace/web_completion.h): the class
// names and custom properties every file in the tree declares, offered inside
// `class="..."` and `var(--)`. The scan runs on the worker queue and its
// result is applied on the main thread; a request that arrives before the
// queue exists (a workspace opened during startup) is remembered and started
// with the first frame.
void request_web_index_scan();
void apply_web_index(WebCompletion::Index index);
// Appends the half of the index the caret's context calls for. Returns true
// when anything was added, which is what tells the caller the popup has rows
// even if no server answers.
bool append_web_index_completions(std::vector<LSPCompletionItem> &items);
void update_lsp_completion_ghost();
// How long a keystroke keeps the completion preview off screen
// (`lsp_completion_ghost_delay_ms`; 0 previews at once).
int lsp_completion_preview_delay_ms() const;
// The preview is there, but the typing that would flicker it has not paused
// yet: the painter leaves the caret's row alone and the frame loop waits for
// the deadline.
bool lsp_completion_preview_withheld() const;
// The last frame withheld the preview and the wait is over, so the frame that
// reveals it has to be asked for. Carries the "was withheld" half of that
// state, so it is called once per frame (never from the painter).
bool lsp_completion_preview_due_soon();
bool apply_selected_lsp_completion();
void accept_telescope_selection();
void render_lsp_completion();
void render_lsp_signature();
