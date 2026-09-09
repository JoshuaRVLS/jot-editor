// Per-pane file tab strip rendering.
#include "editor.h"
#include "jot/file_icons.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

void Editor::render_tabs()
{
  // Global shared tabs are intentionally disabled.
  // Each pane renders its own local tab header.
}

// ---------------------------------------------------------------------------
// Easter egg: Konami code (↑↑↓↓←→←→) rainbow popup
// ---------------------------------------------------------------------------
