// Lookup for the bundled Lua sources embedded into the binary at build time
// (see cmake/embed_lua.cmake). The runtime loaders use these bytes as the
// final fallback after the user data dir and the developer source dir, so a
// shipped binary always carries a working copy of the Lua UI / features.
#pragma once

#include <cstddef>

namespace jot_embedded
{
  // Returns the byte contents of a bundled Lua file (relative path such as
  // "features/ui.lua") or nullptr when the file is not embedded. The caller
  // must NOT free the returned pointer.
  const unsigned char *find(const char *rel_path, size_t *out_size);
}