// ---------------------------------------------------------------------------
// LSP surfaces and navigation
// ---------------------------------------------------------------------------
//
// The pickers LSP answers feed (quick pick, symbols, diagnostics), the
// completion / signature / hover popups, go-to-definition and the jumplist.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
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
  void hide_lsp_completion();
  bool refresh_lsp_completion_filter();
  void update_lsp_completion_ghost();
  bool apply_selected_lsp_completion();
  void accept_telescope_selection();
  void render_lsp_completion();
  void render_lsp_signature();
