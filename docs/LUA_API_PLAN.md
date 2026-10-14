# Lua API plan: what Neovim's API has that this one does not

The question behind it: how far is this editor from "everything is a plugin"?
The reference is Neovim's API, because it is the one large editor whose
whole surface is documented for script authors: `:h api` for the `nvim_*`
functions ([API reference](https://neovim.io/doc/user/api/), source
[runtime/doc/api.txt](https://raw.githubusercontent.com/neovim/neovim/master/runtime/doc/api.txt))
and `:h lua` plus `:h lua-guide` for the `vim.*` library a plugin actually
writes against ([Lua reference](https://neovim.io/doc/user/lua/),
[Lua guide](https://neovim.io/doc/user/lua-guide/)).

This is a reading of the two surfaces, not a measurement of either. Every claim
about this editor names a file; every claim about Neovim names a page. Where the
Neovim documentation was unclear (`vim.net` and `vim.pack` are newer than 0.10
and marked experimental, `vim.host` could not be verified at all), the item says
so and is not used as a target.

## What is already covered

The delta below is short, so it helps to say first what is not missing. This
build already has, from `src/lua/api/api_bindings.cpp` and the namespaces
registered beside it:

- buffers and cursor: `get_text`, `set_text`, `apply_edit` (half-open range,
  1-based), `lines`, `get_line`, `meta`, `selection`, the accessors take an
  optional buffer index or path, plus buffer-local variables
  (`set_var` / `get_var` / `del_var`);
- marks, buffer-local for `a`-`z` and global for `A`-`Z`
  ([LUA_API.md](LUA_API.md)), which Neovim splits across mark motion and
  `nvim_buf_set_mark`;
- `jot.autocmd` with nine events, and `BufChange` carries an edit delta
  (`edit_start_line`, `edit_inserted`, `edit_removed`) that Neovim only hands
  out through a `nvim_buf_attach` callback;
- keymaps with multi-chord sequences, groups and a which-key style helper;
- panels, pickers, popups, the command palette, floats with cell spans
  (`set_spans`, `set_color`), input hooks and per-panel keys;
- extmark-style decorations with `virt_text` (`jot.decoration`);
- jobs (`jot.job.run` in a terminal, `jot.job.capture` on a worker), timers,
  LSP, tree-sitter, debugger, git, integrated terminals, tasks, symbols,
  diagnostics, image and markdown/HTML previews, status line segments, themes,
  snippets, the file tree and sidebar, and the undo tree.

Two places here go further than Neovim, which matters for what should *not* be
copied:

- `jot.screen`, `jot.input` and `jot.term` are a raw layer: a script paints any
  cell, sees any key or click ahead of the editor, and writes to the terminal's
  own byte stream. In Neovim that reach exists only for a UI client over the UI
  protocol ([UI events](https://neovim.io/doc/user/api-ui-events/)), not for a
  plugin.
- `jot.policy` lets a module own a named piece of editor policy, with the native
  implementation staying as the fallback. Neovim does this per subsystem
  (`vim.filetype`, `vim.lsp`, `vim.ui` are declared extension points) but has no
  general slot for it.

## What not to copy

- **RPC and remote channels** (`nvim_chan_send`, `vim.rpcrequest`,
  `nvim_exec_lua`): they exist because Neovim is a client/server editor. A
  plugin here runs in the editor process and calls it directly.
- **The Vimscript bridge** (`vim.fn`, `vim.call`, `vim.cmd`): Lua is the only
  script language in this build and `jot.editor.execute(":reload")` is the ex
  surface. A Vimscript compatibility layer would be the largest possible change
  with the least value.
- **Buffer, window and tabpage handles**: Neovim's API is handle-based because
  handles survive addresses. Here a buffer is a 1-based index or a path and the
  accessors take one optionally, which reads better in Lua and is already
  consistent. Do not add a second identity model.
- **`nvim_ui_attach`**: covered better in-process by the raw layer above.

## The gaps, in the order they are worth closing

### P0.1 An API description, generated, with a test that keeps it honest

Neovim publishes machine-readable metadata: `nvim_get_api_info()` over RPC,
`nvim --api-info`, a `since` and `deprecated_since` per function, and an
`api_level` with a stated compatibility contract (new functions may change
before release, deprecated ones are not removed before 2.0)
([API reference](https://neovim.io/doc/user/api/)). Here there is
`jot.api_version` and `jot.capabilities()`, a hand-written array of 41 names in
[bindings_editor.cpp](../src/lua/bindings/bindings_editor.cpp) that already
disagrees with the registered tree: `decoration`, `float`, `preview`, `emmet`,
`leetcode` and `policy` are real namespaces and are not in that list, so a
script that gates on `jot.capabilities()` takes the wrong path on a build that
has them. Nothing lists functions or events at all, and the drift has already
happened in the docs: `jot.buffer.apply_edit` and `jot.decoration` are
registered and absent from [LUA_API.md](LUA_API.md).

Plan: emit the description from the registration helper itself (every namespace
already goes through the same `field(L, ...)` and table calls, so the table is a
reading of the code, not a second list to maintain), publish it as `jot.api`
(`info`, `functions`, `events`, `options`, `capabilities`), add `--api-info` for
a dump that needs no running editor, and add a probe that diffs the dump against
the headings in `docs/LUA_API.md` so a name that is added and not documented
fails a test. `jot.capabilities()` becomes a view of the same table instead of
its own array. Write down the contract Neovim writes down: which surfaces are
stable, which are experimental, and that a name is only removed with a
documented replacement.

How you know it worked: add a binding without documenting it, and the probe goes
red.

### P0.2 A Lua standard library, shipped once

Neovim ships the helpers its own runtime and every plugin lean on: `vim.json`,
`vim.base64`, `vim.text` (`diff`, `hex`), `vim.iter`, `vim.fs` (join, normalize,
root markers, directory walking), `vim.inspect`, `vim.print`, `vim.split`,
`vim.startswith`, `vim.deepcopy`, `vim.islist`, `vim.version`
([Lua reference](https://neovim.io/doc/user/lua/)). Under `runtime/lua/` this
build ships one compat file, `compat/utf8.lua`, and runtime integrations
(`features/`, `lsp/`, `luals/`, `treesitter/`), but no general-purpose library,
and it shows: `features/ai/json.lua` and `features/leetcode/json.lua` are two
copies of one encoder and decoder, and the undo tree panel added in
`runtime/lua/features/undotree.lua` hand-rolls an LCS diff because there is
nothing to call. A second copy is a bug waiting for the two of them to disagree.

Plan: one `runtime/lua/lib/` loaded before the features and before plugins, with
`json`, `base64`, `text` (diff and hex), `fs` (path join, normalize, parent,
exists, glob, workspace-relative), `inspect`, `iter`, `version`, and the string
and table helpers worth having. Collapse the two json copies onto it, move
`compat/utf8.lua` under the same roof, and hold the dialect table at the top of
`docs/LUA_API.md` as the promise about what exists. Pure Lua: no ABI risk, and
`test/lua_engine_probe.py` is where it gets pinned.

### P1.3 Options have no scope, and the schema belongs to the menu

Neovim separates those two things cleanly: `nvim_get_all_options_info` answers
type, default, scope and allowed values for every option, `vim.o`, `vim.go`,
`vim.bo`, `vim.wo` and `vim.opt[_local|_global]` are the scopes, and
`nvim_get_option_value({scope = "local"})` reads one buffer's or one window's
effective value ([Lua reference](https://neovim.io/doc/user/lua/)). Here
`jot.config.get/set/unset/has/keys/path` is one global store with no scope and no
type metadata beyond the `get_number` / `get_bool` readers, so a plugin cannot
set an indent width or a fold method for one file type, cannot read what another
buffer has, and cannot ask what a key is supposed to be.

The schema half already exists, which is what makes this cheap: the label, type,
range, step and enum options of every known key live in
[runtime/lua/features/settings/catalog.lua](../runtime/lua/features/settings/catalog.lua),
owned as the `settings.catalog` policy slot with the native tables in
`src/surfaces/settings.cpp` as the fallback, and
`tools/settings_catalog_import.py` already keeps the two in step. It is the
settings *menu's* catalog, though: nothing exposes it through `jot.config`, and
its shape is rows, not a setting's scope or default.

Plan: make that catalog the one source of truth for name, type, default, allowed
values, scope and whether the key live-applies; add `jot.config.info([key])` and
`jot.config.list()`; add the per-buffer and per-window half
(`jot.buffer.set_option` / `get_option`, `jot.window.set_option` / `get_option`)
for the keys that are genuinely local, starting with indent, tab behaviour, wrap,
line numbers, fold method and file type; generate the settings menu's rows and
the options table in the docs from the catalog. A key a plugin invents still
gets a row today ("a key with no entry still gets a row, labelled with the key
itself"), and that stays true.

### P1.4 Events: patterns, `once`, groups, listing, and user events

`jot.autocmd(name, fn)` takes nine fixed names and hands every one of them to
every listener. Neovim's `nvim_create_autocmd(event, {pattern, once, callback,
group})` adds a filter (a path glob or a file type), a one-shot form, and a group
whose handlers are removed in one call; `nvim_create_augroup`,
`nvim_get_autocmds`, `nvim_del_autocmd` and `nvim_clear_autocmds` are the
lifecycle around them, and `User` events are how two plugins talk to each other
without either importing the other ([API
reference](https://neovim.io/doc/user/api/)).

Plan: an options table on the existing calls, so `jot.autocmd(name, fn)` keeps
working and `jot.autocmd(name, {pattern = "*.lua", once = true}, fn)` is added;
match patterns where the event is already emitted (the emit sites know the path
and the file type); `jot.autocmd.list()` and `jot.autocmd.del(key)`;
`jot.events.emit("user.<name>", payload)` for plugin-to-plugin events, with the
`user.` prefix reserved the way Neovim reserves `User`. Reload semantics stay as
they are: reloading the plugin set drops everything the plugin set registered,
which is what a group would have done explicitly.

### P1.5 Commands take no shape

`jot.command(name, fn, desc)` hands the callback the raw argument string. Neovim's
`nvim_create_user_command` takes `nargs`, `range`, `count`, `bang`, `complete`
and `force`, and `nvim_parse_cmd` and `nvim_get_commands` let a plugin inspect
what exists. `jot.policy` already owns argument *reading* for a named command
(that is one of the examples in [LUA_API.md](LUA_API.md)), so what is missing is
the command's *shape*: how many arguments, whether a range or a bang was given,
what to suggest while it is typed, and a way to list the commands a plugin
registered.

Plan: an options table on `jot.command` (`nargs`, `range`, `bang`, `complete`,
`desc`), `jot.command.list()`, and feed the palette's existing argument hints
from the same table so the explanation box a user reads and the completion the
parser accepts cannot disagree.

### P1.6 Keymaps: no buffer or mode scope, no listing, no delete

`jot.keymap.set(chord, fn, detail[, group])` is global, and the only way to take
a binding away is `:reloadplugins`. Neovim has `nvim_set_keymap` and
`nvim_buf_set_keymap` with modes (`n`, `i`, `v`, `x`, `o`, `t`, `c`), `nowait`,
`silent`, `expr`, and `nvim_get_keymap` / `nvim_del_keymap` to inspect and remove
([API reference](https://neovim.io/doc/user/api/)). The raw `jot.input.on_key`
hook can implement any of that by hand, but every plugin that needs a
buffer-local binding then writes its own mode tracking.

Plan: `jot.keymap.set(chord, fn, detail, {buffer = ..., mode = ..., nowait =
...})`, `jot.keymap.list()`, `jot.keymap.del(chord[, buffer])`, reusing the input
modes already exposed through `jot.input.push_mode` / `pop_mode` / `modes`.

### P2.7 Async filesystem and network: decide, do not drift

Neovim exposes libuv as `vim.uv` (filesystem, networking, timers) and adds
`vim.system()` on top of it ([Lua reference](https://neovim.io/doc/user/lua/)).
Here a plugin has blocking `io` from the Lua standard library, `jot.job.capture`
for a subprocess on a worker thread, `jot.timer`, and no asynchronous file or
socket API at all; the network lives in a native transport per feature (the AI
client, the Bible client, the LeetCode client). That is three clients doing one
job.

Decision to make, not a feature to slip in: either add `jot.net.request` as one
native HTTP transport with a callback and collapse the three per-feature clients
onto it, or state in the docs that network access stays native and a plugin
reaches for `jot.job` with `curl`. Bundling libuv for the general case is a
dependency decision this editor has avoided so far and should keep avoiding
until the three clients are actually consolidated.

### P2.8 Diagnostics and LSP configuration

`jot.lsp` has the widest surface in this build, and the delta against
`vim.lsp` is mostly about configuring the built-in presentation rather than
calling the protocol: Neovim has `vim.diagnostic.config` for severity, signs,
inline text and handlers, and a documented handler table for LSP results
([Diagnostic reference](https://neovim.io/doc/user/diagnostic/)). This does not
need a namespace of its own; it is a handful of keys in the P1.3 options work
plus a handler table where a Lua function may take over a result the editor
would otherwise present natively, which is the `jot.policy` shape that already
exists.

## Order, and why that order

1 and 2 first, because they are pure additions, they are what makes the rest
discoverable, and each ends with a test that keeps it true afterwards. 3 next,
because it is the largest real hole and half of it is already built in the
settings catalog. 4, 5 and 6 together, because all three are the same change
shaped onto an existing call (an options table) and can land with one probe
apiece. 7 only after a decision, and 8 folded into 3.

The house check for every item: a probe under `test/` that drives the real
binary through `run_in_pty` and asserts on the screen for anything a user can
see, `test/lua_engine_probe.py` for what the library answers, and the metadata
diff from P0.1 for what the docs claim. C++ changes only where a native hook is
genuinely needed (the emit sites for event patterns, the option scopes).
