// The official jot artwork (packaging/windows/icon.png) as raw RGBA8888 for
// SDL_SetWindowIcon. The bytes are generated at build time from
// packaging/jot-icon-128.rgba by cmake/embed_icon.cmake; regenerate that file
// with packaging/make_gui_icon.py after an artwork change. Keeping the pixels
// in the binary means the window icon never depends on an installed data file.
#pragma once

namespace jot_gui
{
// RGBA8888, kWindowIconSize x kWindowIconSize pixels, straight alpha with the
// corners of the tile already rounded away.
extern const unsigned char kWindowIconPixels[];
extern const int kWindowIconSize;
} // namespace jot_gui
