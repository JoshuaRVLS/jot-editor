// GL declarations for RmlUi's backward-compatible GL3 renderer, used in
// place of its bundled glad loader (compiled with RMLUI_GL3_CUSTOM_LOADER
// pointing here). The renderer then calls the system libGL directly --
// exactly how the rest of the GUI compiles (GL_GLEXT_PROTOTYPES) -- so no
// glad loader is embedded and no symbol interposition can hijack jot's
// own GL calls.
#pragma once

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>