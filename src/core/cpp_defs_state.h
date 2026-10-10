#ifndef JOT_STATE_CPP_DEFS_STATE_H
#define JOT_STATE_CPP_DEFS_STATE_H

#include "syntax/cpp_definitions.h" // ScanStats
#include "text/text_features.h"     // Diagnostic
#include <map>
#include <string>
#include <vector>

// The C++ definition checks and the diagnostics they leave behind: a
// declaration with no body in the workspace, and a signature
// implemented more than once (see syntax/cpp_definitions.h).
//
// The scan runs on a worker thread (commands/cpp_definitions.cpp), so what lives
// here is the state around it: the per-file diagnostics every diagnostics
// surface reads, and the bookkeeping that keeps a result from a workspace that
// has since changed -- or from a scan an edit has made obsolete -- out of them.
//
// Split out of editor_state.h (which is now the umbrella over src/core/).
struct CppDefsState
{
  // filepath -> diagnostics from the definition checks, in the same per-file
  // shape the LSP slices use, so refresh_lsp_diagnostics_for, the explorer's
  // badges and the Problems list merge it identically.
  std::map<std::string, std::vector<Diagnostic>> cpp_def_diags;

  bool cpp_defs_enabled = true;
  // One scan at a time: a request that arrives while one is in flight only
  // marks that another is wanted, and it runs the moment the current one lands.
  bool cpp_defs_scan_running = false;
  bool cpp_defs_scan_pending = false;
  // The `:cppcheck` path wants the result shown (panel focused, summary in the
  // status line); a scan after a save only wants the diagnostics.
  bool cpp_defs_announce = false;
  // Which way a landing scan should move the caret: 0 none, +1 the next finding,
  // -1 the previous. `:cppcheck` leaves +1 behind, and `next`/`prev` issued
  // while a scan is in flight record theirs instead of jumping against the stale
  // rows -- so the jump the user asked for is what happens, once, when the scan
  // that was already on its way lands.
  int cpp_defs_pending_jump = 0;
  // Which scan a landing result belongs to: the epoch is bumped per request and
  // the root is the workspace it was started for, so a result that arrives after
  // a workspace switch (or after a newer scan) is dropped rather than published.
  unsigned long long cpp_defs_scan_epoch = 0;
  std::string cpp_defs_scan_root;
  // Revision checks avoid parsing idle buffers; signature checks avoid workspace
  // walks for body-only edits that leave every finding unchanged.
  std::string cpp_defs_dirty_signature;
  std::string cpp_defs_dirty_revision;
  CppDefinitions::ScanStats cpp_defs_stats;
  long long cpp_defs_last_scan_ms = 0;
};

#endif
