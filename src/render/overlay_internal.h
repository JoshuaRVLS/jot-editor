// Shared text helpers for the overlay popups (completion, signature,
// telescope, image viewer), defined in overlay_shared.cpp.
#pragma once

#include "core/editor.h"
#include <string>

namespace overlay_internal
{
std::string one_line_text(const std::string &text);
std::string clip_text(const std::string &text, int max_w);
std::string clip_path_left(const std::string &text, int max_w);
int syntax_preview_color(const Theme &theme, int token);

// Whether a box anchored to the caret goes above it or below it: above when the
// space above fits the box plus its two-cell gap, below when only that side
// fits, and the roomier side when neither does. Above is the preference (the
// completion list usually owns the space below), but a caret near the top of the
// window has to put the box under the line being typed instead of over it.
bool overlay_box_above(int cursor_y, int box_h, int min_y, int pane_bottom);
} // namespace overlay_internal