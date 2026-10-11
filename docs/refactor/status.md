# Refactor status

Phase 0 measured the tree. This records what has moved since, what the tree looks
like now, and what is deliberately not done yet. Phases 1 to 4 are committed, and
the last of them is the one that removed `src/features/`. Phase 5 -- the `Editor`
facade -- has started: the first collaborator is out of the fragments, `api/rest.h`
is gone, and the first batch of `Editor::` implementations has come out of a
module below `editor/`.

Since then the tree was flattened: `src/editor/` and `src/tools/` are gone, and
their contents are top-level domains (`core/`, `buffers/`, `panes/`, `commands/`,
`events/`, `surfaces/`, and additions to `lsp/`, `dap/`, `syntax/`, `terminal/`
and `extras/`). Nothing under `src/` is deeper than two levels. See **The
flattening** below for what moved where and what it cost.

## Modules that exist

Every directory under `src/` is a module, every module has one static library,
and the libraries are aggregated into `jot_engine`.

| module | target | contents |
| --- | --- | --- |
| `base/` | `jot_base` | string, path, shell and link helpers, atomic file writes, the event loop, the task queue, process jobs |
| `text/` | `jot_text` | cursor, lines, search, sort, transforms, text objects, line providers, insertion rules |
| `syntax/` | `jot_syntax` | language detection, highlighting, folding, the tree-sitter runtime, the C++ and colour scanners, the symbol index |
| `ui/` | `jot_ui` | the cell grid, terminal and GUI backends, smooth scrolling, the tab order |
| `render/` | `jot_render` | frame, buffer, panels, popups, minimap, the winbar model |
| `input/` | `jot_input` | modes, mouse routing, the palette, command dispatch |
| `buffers/` | `jot_editor` | opening, saving and reloading files, the buffer list, undo, autosave, recent files, fold state |
| `panes/` | `jot_editor` | the split layout, navigation, resizing and pane tabs |
| `commands/` | `jot_editor` | the ex command line and everything it runs |
| `events/` | `jot_editor` | the main loop, terminal and GUI events, which-key |
| `surfaces/` | `jot_editor` | home screen, popups, pickers, settings, theme chooser |
| `gui/` | `jot_ui` | the SDL3/OpenGL front end (moved out of `ui/`) |
| `workspace/` | `jot_workspace` | explorer, sidebar, right dock, tasks, timeline, project search, config, the HTML and HTTP file helpers |
| `lsp/` | `jot_lsp` | the language server protocol client, its wire format, the completion ranking |
| `dap/` | `jot_dap` | the debug adapter protocol client and its wire format |
| `terminal/` | `jot_terminal` | pty-backed sessions and their panel (libvterm lives here) |
| `git/` | `jot_git` | repository state, the git client, the panel and both painters |
| `extras/` | `jot_extras` | WakaTime, coding time, the Bible, LeetCode, the status clock, the emmet expander |
| `core/` | `jot_editor` | the `Editor` class, its state groups (`core/*_state.h`), its model types and the fragments of the class body |
| `lua/` | `jot_lua_api` | the Lua API surface (`lua/api/`), its bindings (`lua/bindings/`) and the view structs under `lua/view/` |

## The flattening

`src/editor/` had become a second `src/`: it carried its own `app/`, `model/`,
`state/`, `event/`, `surfaces/`, `integrations/` and `markdown/`, so the tree was
deep in the one place it should be flat, and half the modules could not be cut
because the file that belonged on one side of the boundary sat inside `editor/`.
It is gone. Its contents went to the domain that owns them, `src/tools/` went
into `extras/`, and `editor/` and `tools/` no longer exist:

- the class, `host_api`, the two outside-collaborator headers and every
  `model/` and `state/` type became `core/`, with the debugger model going to
  `dap/` and the LSP and web-index state to `lsp/` and `extras/`;
- `app/` split into `buffers/`, `panes/` and `commands/`, and `event/` and
  `surfaces/` became domains of their own;
- `integrations/` split four ways: the LSP glue to `lsp/`, the debugger to
  `dap/`, the syntax and tree-sitter wiring to `syntax/`, the terminal to
  `terminal/`;
- `markdown/`, the REST client, the symbol index, the discord controller and the
  Bible and coding-time fragments joined `extras/`, along with everything the
  old `tools/` held;
- the fragments of the class body left `api/` for the domain that implements
  them (`render/paint.h`, `lsp/lsp_ui.h`, `commands/commands.h`,
  `workspace/explorer.h`, `buffers/buffers.h`, `panes/panes.h`), except
  `public_api.h` and the two test-hook headers, which are the deliberate public
  surface and stay together in `core/api/`;
- `lua/` split into `lua/api/` and `lua/bindings/`, and the nine `color_*` pairs
  moved to `syntax/color/`.

The moves were done by a script that rewrote every quoted include the way the
compiler resolves it, in the same pass as the move, so a step either built or
failed to resolve -- never half-moved. Each step was built and tested before the
next one started. `src/CMakeLists.txt` keeps its targets; what changed is that
the one `JOT_EDITOR_SOURCES` list is now one list per domain, each in the
`sources.cmake` of the directory it names, compiled into the same object library
as before.

The renames that came with the moves: `src/edit/` became `src/text/` (`jot_edit`
to `jot_text`), `src/features/` was distributed and deleted, `src/jot/` became
`src/editor/` and `jot_core` became `jot_editor`, so no target is named after a
directory that does not exist.

One exception is left, and it is deliberate: `jot_tools`, with its
`JOT_TOOLS_SOURCES` list, still exists although `src/tools/` does not, because
the change set that dissolved the directory was scoped to paths and targets, and
renaming a target is not a path. Folding it into `jot_extras` is a follow-up.

Every move that crossed a directory also rewrote the callers' include strings to
name the module (`base/string_util.h`, `syntax/folding.h`, `lsp/client.h`,
`extras/wakatime.h`), so the boundary is visible in the source rather than only
in the build graph.

## What the checkers say

Two rules are enforced in CI, both as ratchets: the recorded debt is allowed to
shrink and never allowed to grow.

- `tools/check_module_deps.py` resolves every quoted include the way the
  compiler does and fails on an include that points from a lower module to a
  higher one. Today: **228 upward includes**, recorded in
  `docs/refactor/module-dep-baseline.txt`.

  The number went **up** from the 197 this file used to report, and the honest
  reason is that the check was blind: `MODULE_ORDER` had no entry for `core/`,
  `buffers/`, `panes/`, `commands/`, `events/`, `surfaces/` or `gui/`, and
  `module_of` returns `None` for a directory that is not in the order, so every
  file in them -- and every edge into them -- was skipped. Half the tree was
  passing the check by not being seen, the same failure mode as the `lua` entry
  in `SYSTEM_INCLUDE_ROOTS` recorded below. With the modules ranked, the 228 are
  mostly one shape: a file in `buffers/`, `panes/`, `commands/`, `events/` or
  `surfaces/` that includes `core/editor.h`, which is the `Editor::` method
  implementations the facade is meant to remove from the callers' reach.
- `tools/check_headers.py` compiles every header alone with the flags from
  `build/compile_commands.json`. Today: **156 of 171 stand alone**. The 15 that
  do not are the fragments of the class body (`core/editing.h`,
  `core/panels_api.h`, `core/runtime.h`, `core/api/*`, `buffers/buffers.h`,
  `commands/commands.h`, `commands/cpp.h`, `panes/panes.h`, `render/paint.h`,
  `workspace/explorer.h`, `lsp/lsp_ui.h`, `extras/bible_api.h`,
  `extras/coding_time.h`), which are pieces of a class body rather than
  headers, so they cannot compile alone by construction. That list only shrinks
  by taking members out of the `Editor` class; `api/rest.h` was the first to
  leave it.

The first of the two also reports an include that resolves to no file at all,
which is how a bulk move that misses a rewrite announces itself: the build
otherwise finds it one compiler error at a time.

A bug in that same tool is worth recording, because it made the ratchet weaker
instead of louder: its exception list for system include roots named `lua`, and
once `src/lua/` existed that entry stopped the rule from ever seeing the module,
hiding 76 upward includes. The list is empty now and a guard refuses any entry
that names a directory under `src/`.

Both checkers were checked with teeth: a `#include "jot/editor.h"` added to
`src/base/file_util.h` fails the module check with exit 1, and dropping the
`shared.h` include from `plugins.h` fails the header check at line 79 with
`'FloatSpan' was not declared in this scope`.

## The one blocker, restated

130 of 267 `.cpp` files in `src/` define `Editor::` methods. That is why no
further module can be cut cleanly: moving those files relabels a directory
without creating a boundary, because the moved file still includes `editor.h`
and the module still points upward.

This is phase 0 decision 1, and the answer taken here is phase 0's
recommendation: cut modules as their ownership moves, not before. The cost of
ignoring it is visible in `extras/`: `bible_client.cpp`, `discord_session.cpp`
and the two `wiring.cpp` files are `Editor::` code that belongs next to the
feature it drives, and moving them there cost seven new `extras -> editor`
baseline lines. The same argument is why `src/editor/integrations/lsp/*.cpp`
stayed in `editor/` while `lsp/` took only the protocol client, and why `git/`
took its painters only once the panel stopped leaving a header behind.

`editor/` internals (`model/`, `state/`, `app/`, `integrations/`, `surfaces/`)
are mapped to the `editor` module by both checkers, which is why `render ->
editor`, `text -> editor` and `input -> editor` dominate the baseline.

## Not done yet

- **The `Editor` facade**, and the 15 `editor/api/` fragments still inside it.
  This is the work that unblocks everything else, and it is the largest single
  piece left: until a caller can ask for what it needs instead of including
  `editor.h`, no module above `base` can be finished.

  The 197 upward includes split three ways, and the split is what says how each
  one gets paid:

  | including file | what it is | edges |
  | --- | --- | --- |
  | in `editor/` | `editor -> lua`, the bridge's own headers | 28 |
  | below `editor/` | an `Editor::` method implementation | 143 |
  | below `editor/` | a real consumer of `Editor` | 26 |

  The 143 are one shape: a file that defines `Editor::` methods is editor code,
  so it either moves into `editor/` (where `surfaces/` and `app/` already hold
  the paint and the commands) or it keeps a pure half in its own module and
  leaves a wrapper behind in `editor/`. The 26 want what the facade is for: a
  narrow header that answers the question the caller actually has, so the file
  never sees the class at all.

  `text/` is the module this has been tried on first, and 14 of its 24 `.cpp`
  files still define `Editor::` methods: `edit.cpp`, `cursor.cpp`,
  `multicursor.cpp`, `clipboard.cpp`, `sidebar.cpp`, `lines.cpp`,
  `textobject.cpp`, `surround.cpp`, `transform.cpp`, `replace.cpp`,
  `increment.cpp`, `duplicate.cpp`, `datetime.cpp` and `path_clipboard.cpp`.
  The small ones go the way the eight above did; the four big ones (`edit`,
  `cursor`, `multicursor`, `clipboard`) are where the editor's own state is the
  subject and want a different cut.

  The fragments go the way `SearchController` and `DiscordController` went --
  state and behaviour into one class with its own header, `Editor` keeps one of
  them, and the class reaches the shared editor state through the `Editor` it
  was built with. `RestClient` (`editor/rest_client.h`) is the third
  collaborator and the first whose header is not a fragment: `api/rest.h` and
  `state/rest_state.h` are both gone, and `editor/app/rest_client.cpp` defines
  `RestClient::` methods over `editor_` in place of the `Editor::` methods over
  the state.
- **`render/`, `ui/`, `input/`, `workspace/` and `lua/`** are cut as far as their
  `Editor::` files allow and no further.
- **`.vscode/` and `CMakeSettings.json`** are still tracked: phase 0 marked them
  as ask-first, and nobody has been asked.
- **The WakaTime key** is still plain text in `<config root>/configs/settings.conf`
  (phase 0 section 7). Moving it to the keyring the way LeetCode works is a
  behaviour change and belongs in its own change set.

## Verification of the latest change set

Phase 5's first cut at the `Editor::` implementations below `editor/`: the eight
line commands that lived in `src/text/` are now a pure unit in `text/` and a
wrapper in `editor/app/line_commands.cpp`.

- `cmake --build build -j8` (Release, GUI and tree-sitter on) and
  `cmake --build build-tests -j8`: both clean.
- `./build-tests/test/jot_tests`: 966 cases, 964 passed, 1 failed, 70628
  assertions. The one failure is the `test_home_open.cpp:292` case below, which
  fails the same way on the tree before this change; the 10 cases and 37
  assertions the run gained are `test/test_line_ops.cpp`.
- That file is also the coverage this slice did not have before. The rules cases
  assert the algorithms directly (case-folded stable sort either way round,
  unique keeping the first of each line, trim never emptying the buffer, the
  join spacing rule, the stats counts and the selection span). The editor cases
  then run `:trimblank`, `:joinlines` and `:sortdesc` through a real editor and
  assert the buffer that comes out, which is what pins the range the wrapper
  works out from the selection. Writing them paid for itself immediately: the
  first `:sortdesc` case selected one line and the command correctly refused, so
  the case was wrong and the command was not.
- `tools/check_module_deps.py`: 197 upward includes, all on the baseline (was
  209). The baseline diff is exactly the move: 13 lines gone with the seven
  `src/text/*.cpp` files that were deleted, and one line added for
  `editor/app/line_commands.cpp -> lua/api.h`, the `lua_api->on_buffer_change`
  call every one of those commands makes. `text -> editor` is 25 to 18 and
  `text -> lua` 17 to 11.
- `tools/check_headers.py`: 154 of 169 stand alone, 15 on the baseline. Both new
  headers -- `text/line_ops.h` and `text/buffer_stats.h` -- stand alone.
- `tools/leak_check.sh build-asan '[line_ops],[buffer_stats]'`: no leaks
  reported, script exit 0, and it builds the changed targets a second time under
  Debug + AddressSanitizer.

## Known red, verified not ours

None of the three below is red on the current tree.

`test/test_home_open.cpp:292` passes again: the whole suite is 1003 cases and
71108 assertions, exit 0 (was 956 cases, 1 failed).

`fold_probe.py` passes; the ranges it asserts on moved to an index cached on the
buffer and that index invalidates on every write.

`cpp_defs_probe.py` passes, and the reason it did not was the probe rather than
the editor: it read the status line by grepping for the LSP chip's old "cpp @
/path" label, which `src/render/status_line.cpp` had already trimmed to the
language alone. The jumps it was checking had been landing correctly the whole
time -- `:cppcheck` on the repeated body in `shapes.cpp:5:5`, then `:cppcheck
next` on the header's declaration at `shapes.h:3:5`. It now reads the row the
status line is drawn on (the screen's last) instead of a label that no longer
exists.

`tabline_probe.py` is timing-flaky on this machine rather than red: it passed 3
of 4 runs against this tree and 3 of 4 against a clean build of the commit
before the extras distribution, with the same two failure modes (a tab that
never opened because the finder was still scanning, and a click that landed
while the editor was busy). The Alt+digit, Alt+0 and jump-letter paths it
asserts were read back from a passing run on this tree, and the probe's own
header documents the timing it is up against.

`keymap_probe.py`, `terminal_cwd_probe.py` and `wakatime_probe.py` fail under
AddressSanitizer because the sanitizer tree is unoptimised and they are
clock-sensitive; the same reason the leak check reads the leak report instead of
the suite's exit status.
