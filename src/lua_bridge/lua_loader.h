// Runtime loading of the bundled Lua sources (lua/*.lua). Resolution order
// is always:
//   1. the user config dir  ($JOT_CONFIG_HOME / ~/.config/jot / %APPDATA%/jot)
//   2. the install data dir (JOT_DEFAULT_DATA_DIR)
//   3. the developer source dir (JOT_LUA_SOURCE_DIR — where the repo lives)
//   4. the copy embedded into the binary at build time
//
// That keeps the developer's edit-without-recompile loop (source dir beats
// the embedded default), lets users/community override via their config dir,
// and guarantees a shipped binary always carries a working copy of the Lua
// UI / features even when no lua/ directory exists on the machine.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Dirs that may hold an override of the bundled Lua files, most specific
// first. Only existing directories are returned.
std::vector<std::filesystem::path> jot_lua_override_dirs();

// Full candidate paths for a bundled file (relative like "features/ui.lua"),
// most specific first, restricted to directories that actually exist.
std::vector<std::filesystem::path> jot_lua_candidate_paths(const std::string &rel_path);

// Resolves a bundled Lua file to a real path on disk (user/install/source),
// or falls back to extracting the embedded copy into the user config dir.
// Returns an empty path when nothing is available.
std::filesystem::path jot_lua_resolve_path(const std::string &rel_path);