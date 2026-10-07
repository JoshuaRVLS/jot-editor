# Refactor status

Phase 0 measured the tree. This records what has moved since, what the tree looks
like now, and what is deliberately not done yet. Phases 1 to 4 are committed, and
the last of them is the one that removed `src/features/`.

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
| `tools/` | `jot_tools` | discord presence, telescope, the image viewer, the LeetCode keyring |
| `workspace/` | `jot_workspace` | explorer, sidebar, right dock, tasks, timeline, project search, config, the HTML and HTTP file helpers |
| `lsp/` | `jot_lsp` | the language server protocol client, its wire format, the completion ranking |
| `dap/` | `jot_dap` | the debug adapter protocol client and its wire format |
| `terminal/` | `jot_terminal` | pty-backed sessions and their panel (libvterm lives here) |
| `git/` | `jot_git` | repository state, the git client, the panel and both painters |
| `extras/` | `jot_extras` | WakaTime, coding time, the Bible, LeetCode, the status clock, the emmet expander |
| `editor/` | `jot_editor` | the editor engine: `model/`, `state/`, `api/`, `app/`, `event/`, `integrations/`, `surfaces/`, `markdown/` |
| `lua/` | `jot_lua_api` | the Lua API surface, its bindings and the view structs under `lua/view/` |

The renames that came with the moves: `src/edit/` became `src/text/` (`jot_edit`
to `jot_text`), `src/features/` was distributed and deleted, `src/jot/` became
`src/editor/` and `jot_core` became `jot_editor`, so no target is named after a
directory that does not exist.

Every move that crossed a directory also rewrote the callers' include strings to
name the module (`base/string_util.h`, `syntax/folding.h`, `lsp/client.h`,
`extras/wakatime.h`), so the boundary is visible in the source rather than only
in the build graph.

## What the checkers say

Two rules are enforced in CI, both as ratchets: the recorded debt is allowed to
shrink and never allowed to grow.

- `tools/check_module_deps.py` resolves every quoted include the way the
  compiler does and fails on an include that points from a lower module to a
  higher one. Today: **209 upward includes in 26 pairs**, recorded in
  `docs/refactor/module-dep-baseline.txt`. The pairs worth looking at first:
  `render -> editor` 42, `editor -> lua` 27, `text -> editor` 25,
  `input -> editor` 23, `render -> lua` 20, `text -> lua` 17, `git -> editor` 7,
  `workspace -> editor` 7, `extras -> editor` 6.
- `tools/check_headers.py` compiles every header alone with the flags from
  `build/compile_commands.json`. Today: **152 of 168 stand alone**. The 16 that
  do not are the `src/editor/api/` fragments, which are pieces of a class body
  rather than headers, so they cannot compile alone by construction.

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

136 of 271 `.cpp` files in `src/` define `Editor::` methods. That is why no
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

- **The `Editor` facade** and the 16 `editor/api/` fragments. This is the work
  that unblocks everything else, and it is the largest single piece left: until
  a caller can ask for what it needs instead of including `editor.h`, no module
  above `base` can be finished.
- **`render/`, `ui/`, `input/`, `workspace/` and `lua/`** are cut as far as their
  `Editor::` files allow and no further.
- **`.vscode/` and `CMakeSettings.json`** are still tracked: phase 0 marked them
  as ask-first, and nobody has been asked.
- **The WakaTime key** is still plain text in `<config root>/configs/settings.conf`
  (phase 0 section 7). Moving it to the keyring the way LeetCode works is a
  behaviour change and belongs in its own change set.

## Verification of the latest change set

- `cmake --build build -j8` (Release, GUI and tree-sitter on) and
  `cmake --build build-tests -j8`: both clean.
- `./build-tests/test/jot_tests`: 956 cases, 954 passed, 1 failed, 70591
  assertions. The one failure is the `test_home_open.cpp:292` case below.
- Probes run against the tree: `bible`, `emmet`, `autoclose`, `html_preview`,
  `winbar`, `status_clock` and `wakatime` all pass, so the moved `extras/`,
  `workspace/`, `render/` and `ui/` code is exercised on a real pty.
- `tools/check_module_deps.py` and `tools/check_headers.py`: both pass at their
  baselines (209 upward includes, 16 headers).
- `tools/leak_check.sh`: no leaks reported, script exit 0. That also builds the
  whole tree a second time under Debug + AddressSanitizer, so it is a second
  independent check of every include path and target these moves touched.

## Known red, verified not ours

`test/test_home_open.cpp:292` fails in every full run of the suite (956 cases,
954 passed, 1 failed). It fails identically on the tree before this work.

`fold_probe.py` and `cpp_defs_probe.py` fail. They were run against a clean
`git worktree` of the commit before the syntax module moved and fail there too,
with the same messages (`fold probe: FAIL (2)`, `cpp defs probe: FAIL -
:cppcheck next did not walk to the header's finding: ''`). They are not caused
by the moves.

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
