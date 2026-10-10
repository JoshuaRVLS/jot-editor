#ifndef EDITOR_MODELS_H
#define EDITOR_MODELS_H

// Umbrella for the models the surfaces are built from: picker rows, menus, the
// terminal/install jobs, the debugger's session state, the jumplist and the
// mouse surfaces. They used to share this 450-line header; each now lives under
// src/core/ with the rest of the model types (see types.h for the map),
// and this header keeps the old include working.
//
//   model/quick_pick.h   palette/picker rows, settings rows
//   model/search.h       one search hit
//   model/menu.h         menu-bar rows and the home menu's
//   model/input.h        mouse selection mode, context-menu surface/items
//   model/tasks.h        terminal tasks, tree-sitter and LSP install jobs
//   model/debugger.h     one debug session's state
//   model/session.h      jumplist entries, closed-buffer snapshots

#include "core/types.h"

#include "dap/debugger.h"
#include "core/input.h"
#include "core/menu.h"
#include "core/quick_pick.h"
#include "core/search.h"
#include "core/session.h"
#include "core/tasks.h"

#endif
