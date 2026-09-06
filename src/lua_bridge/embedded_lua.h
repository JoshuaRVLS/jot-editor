// Lookup for the bundled runtime files embedded into the binary at build
// time (see cmake/embed_lua.cmake): the Lua UI / features plus the
// tree-sitter query files. The runtime loaders use these bytes as the final
// fallback after the user config dir and the developer source dir, so a
// shipped binary always carries a working copy of the runtime.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace jot_embedded
{
  // Returns the byte contents of a bundled runtime file (relative path such
  // as "features/ui.lua" or "treesitter/queries/lua/highlights.scm") or
  // nullptr when the file is not embedded. The caller must NOT free the
  // returned pointer.
  const unsigned char *find(const char *rel_path, size_t *out_size);

  // Names of every bundled runtime file, sorted, in no particular relation
  // to find(). Used to materialize whole subtrees (e.g. the treesitter or
  // lsp-installer Lua) into the cache dir when nothing exists on disk.
  std::vector<std::string> list_files();
}