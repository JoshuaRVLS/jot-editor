// Runtime loading of the bundled runtime files (lua/*.lua plus the
// tree-sitter .scm queries). Resolution order is always:
//   1. the user config dir  ($JOT_CONFIG_HOME / ~/.config/jot / %APPDATA%/jot)
//   2. the developer source dir (JOT_LUA_SOURCE_DIR — where the repo lives)
//   3. the install data dir (JOT_DEFAULT_DATA_DIR, plus a Windows fallback)
//   4. the cache dir, auto-extracted from the binary's embedded bytes
//      (JOT_CACHE_HOME / ~/.cache/jot / %LOCALAPPDATA%\jot\cache)
//
// The config dir is user territory: jot never writes there, so any file that
// exists is a hand-written override and is respected verbatim (a legacy
// `.embedded` marker still marks copies jot itself materialized in the
// pre-cache era — those are refreshed when the bundled runtime moves on).
// The cache dir is disposable and kept in sync with the embedded bytes, so
// a shipped binary always carries a working copy of the runtime even when
// no lua/ directory exists on the machine.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Dirs that may hold an override of the bundled runtime files, most
// specific first. Only existing directories are returned.
std::vector<std::filesystem::path> jot_lua_override_dirs();

// Full candidate paths for a bundled file (relative like "features/ui.lua"),
// most specific first, restricted to directories that actually exist.
std::vector<std::filesystem::path> jot_lua_candidate_paths(const std::string &rel_path);

// Ensures the embedded copy of a bundled file exists in the cache dir
// (extracting or refreshing it), returning its path, or an empty path when
// the file is not embedded. Never consults or writes the user config dir.
std::filesystem::path jot_lua_cache_path(const std::string &rel_path);

// Resolves a bundled file to a real path on disk: the first override copy
// (user/dev/install) wins; otherwise the embedded bytes are extracted into
// the cache dir and that path is returned. Empty when nothing is available.
std::filesystem::path jot_lua_resolve_path(const std::string &rel_path);

// Git checkout this binary was built from, when one can be located: an
// explicit $JOT_SOURCE_DIR override wins, then the developer source dir the
// build embedded (JOT_LUA_SOURCE_DIR, <repo>/runtime/lua) is walked up to
// its repo root. Empty for plain installed binaries. Powers the Lua :update
// feature (fetch/compare/rebuild against the source clone).
std::filesystem::path jot_lua_repo_root();