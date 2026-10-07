#ifndef JOT_STATE_WEB_INDEX_STATE_H
#define JOT_STATE_WEB_INDEX_STATE_H

#include "features/web_completion.h"
#include <string>

// The workspace's own CSS vocabulary -- the class names the markup uses and the
// custom properties the style sheets declare -- kept so a completion inside
// `class="..."` or `var(--)` can offer them (see features/web_completion.h for
// the scan itself).
//
// The scan runs on a worker thread (jot/app/web_index.cpp); what lives here is
// the index it leaves behind and the bookkeeping that keeps a result from a
// workspace that has since changed out of it. The shape mirrors CppDefsState,
// which is the same pattern over the same worker queue.
struct WebIndexState
{
  WebCompletion::Index web_index;
  bool web_index_scan_running = false;
  bool web_index_scan_pending = false;
  // The epoch is bumped per request and the root is the workspace it was started
  // for, so a result that lands after a workspace switch (or after a newer
  // request) is dropped rather than published.
  unsigned long long web_index_scan_epoch = 0;
  std::string web_index_scan_root;
};

#endif
