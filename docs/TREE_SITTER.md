# Tree-sitter

Jot keeps Tree-sitter language policy in `lua/treesitter/registry.lua`. Every
entry declares parser URL, symbol, library names, extensions, aliases, source
subdirectory, and query path. Highlight queries are stored in
`lua/treesitter/queries/<language>/highlights.scm`.

Startup loads Lua policy before syntax detection. Native C++ provides dynamic
library loading, ABI checks, parsers, trees, queries, incremental parsing,
caches, and capture rendering. It has no compiled language catalog or query
fallback. A bad Lua file or query disables only its language and regex syntax
continues to work.

## Install Layout

Default parser root:

```text
POSIX:   ${XDG_DATA_HOME:-$HOME/.local/share}/jot/treesitter
Windows: %LOCALAPPDATA%/jot/treesitter
  parsers/<parser library>
  queries/<language>/highlights.scm
```

`:tsinstall <language>` and `jot.treesitter.install_command(language)` use
metadata registered by Lua. `JOT_TREESITTER_PREFIX` explicitly replaces the
root; it is checked for writability before clone/build. `:tsreload` reloads Lua
metadata and queries, then clears all native runtime caches.
