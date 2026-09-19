#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

// Umbrella for the editor's core model types: the plain data the editor state
// and the render/edit layers exchange. Each one now lives in the file that owns
// it, under src/jot/model/ -- this header is what keeps the old include working
// (and is what a module that wants the whole core model should still include).
//
// The split is by domain, not by size: a reader looking for the theme walks to
// model/theme.h, one looking at a pane to model/panes.h. New code is free to
// include the narrow header instead.
//
//   model/theme.h        the palette and the syntax slots it addresses
//   model/syntax.h       syntax engine kinds, one rule, the per-line cache
//   model/fold_ranges.h  fold ranges with the index prepared from them
//   model/decorations.h  anchored highlight / virtual-text marks
//   model/buffer.h       cursor, selection, undo state, one open file
//   model/panes.h        a pane, its tab strip, the pane tree
//   model/popup.h        the popup surface
//   model/panels.h       panel views and their models
//   model/explorer.h     explorer rows and their cache
//
// editor_models.h folds in the rest (pickers, menus, tasks, sessions, the
// debugger and mouse surfaces), so the two old names keep working unchanged.

#include "jot/model/buffer.h"
#include "jot/model/decorations.h"
#include "jot/model/explorer.h"
#include "jot/model/fold_ranges.h"
#include "jot/model/panels.h"
#include "jot/model/panes.h"
#include "jot/model/popup.h"
#include "jot/model/syntax.h"
#include "jot/model/theme.h"

#endif
