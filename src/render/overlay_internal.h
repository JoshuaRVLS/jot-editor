// Shared text helpers for the overlay popups (completion, signature,
// telescope, image viewer), defined in overlay_shared.cpp.
#pragma once

#include "editor.h"
#include <string>

namespace overlay_internal
{
std::string one_line_text(const std::string &text);
std::string clip_text(const std::string &text, int max_w);
std::string clip_path_left(const std::string &text, int max_w);
int syntax_preview_color(const Theme &theme, int token);
} // namespace overlay_internal