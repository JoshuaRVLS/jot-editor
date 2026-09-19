#ifndef EDITOR_STATE_H
#define EDITOR_STATE_H

// The editor's state, split by domain under src/jot/state/ and gathered here by
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
//   state/pane_state.h        buffers, split tree, tabs, pane/scrollbar drags
//   state/workspace_state.h   sidebar, workspace session, git summary + panel
//   state/panel_state.h       bottom/right docks, minimap, terminal selection
//   state/surface_state.h     palette, pickers, prompts, menus, popup
//   state/lsp_state.h         clients, diagnostics, completion/hover/inlay
//   state/input_state.h       mouse selection, click/hover, keystroke bookkeeping
//   state/view_state.h        layout metrics, theme, paint caches, message line
//   state/navigation_state.h  jump history and the armed jump
//   state/engine_state.h      config, terminals, debugger, UI, Lua host
//
// New code that needs one slice is encouraged to include that header directly
// (the groups are independent: no group includes another).

#include "jot/state/engine_state.h"
#include "jot/state/input_state.h"
#include "jot/state/lsp_state.h"
#include "jot/state/navigation_state.h"
#include "jot/state/pane_state.h"
#include "jot/state/panel_state.h"
#include "jot/state/surface_state.h"
#include "jot/state/view_state.h"
#include "jot/state/workspace_state.h"

struct EditorState : EngineState,
                     InputState,
                     LspUiState,
                     NavigationState,
                     PaneState,
                     PanelState,
                     SurfaceState,
                     ViewState,
                     WorkspaceState
{
};

#endif
