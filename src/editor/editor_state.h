#ifndef EDITOR_STATE_H
#define EDITOR_STATE_H

// The editor's state, split by domain under src/editor/state/ and gathered here by
// inheritance: EditorState *is* every one of those groups, so a member is
// reached exactly as before (state.show_sidebar, state.lsp_clients, ...) and
// the 195 files that include this header did not have to change.
//
// The split is by subsystem, not by size: a reader looking for the pane tree
// walks to state/pane_state.h, one looking at the LSP popups to
// state/lsp_state.h. Behaviour that owns its state lives in a controller
// instead (jot/editor/*_controller.h) -- search and Discord so far -- so what
// remains here is data the editor itself reads and writes.
//
//   state/pane_state.h        buffers, split tree, pane-local tab history, drags
//   state/tabline_state.h     the workspace tab strip (row 0) and its order
//   state/winbar_state.h      the breadcrumb winbar and the menu a crumb opens
//   state/workspace_state.h   sidebar, workspace session, git summary + panel
//   state/panel_state.h       bottom/right docks, minimap, terminal selection
//   state/surface_state.h     palette, pickers, prompts, menus, popup
//   state/lsp_state.h         clients, diagnostics, completion/hover/inlay
//   state/cpp_defs_state.h    the C++ definition checks and their diagnostics
//   state/input_state.h       mouse selection, click/hover, keystroke bookkeeping
//   state/view_state.h        layout metrics, theme, paint caches, message line
//   state/navigation_state.h  jump history and the armed jump
//   state/engine_state.h      config, terminals, debugger, UI, Lua host
//   state/rest_state.h        the HTTP client's session (:rest)
//   state/bible_state.h       the Bible reader's session (:bible)
//   state/web_index_state.h   the workspace's CSS vocabulary, for completions
//
// New code that needs one slice is encouraged to include that header directly
// (the groups are independent: no group includes another).

#include "editor/state/bible_state.h"
#include "editor/state/cpp_defs_state.h"
#include "editor/state/engine_state.h"
#include "editor/state/input_state.h"
#include "editor/state/lsp_state.h"
#include "editor/state/navigation_state.h"
#include "editor/state/pane_state.h"
#include "editor/state/panel_state.h"
#include "editor/state/rest_state.h"
#include "editor/state/surface_state.h"
#include "editor/state/tabline_state.h"
#include "editor/state/view_state.h"
#include "editor/state/web_index_state.h"
#include "editor/state/winbar_state.h"
#include "editor/state/workspace_state.h"

struct EditorState : BibleState,
                     CppDefsState,
                     EngineState,
                     InputState,
                     LspUiState,
                     NavigationState,
                     PaneState,
                     PanelState,
                     RestState,
                     SurfaceState,
                     TablineState,
                     ViewState,
                     WebIndexState,
                     WinbarState,
                     WorkspaceState
{
};

#endif
