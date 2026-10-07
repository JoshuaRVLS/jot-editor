# Refactor status

Phase 0 measured the tree. This records what has moved since, what the tree
looks like now, and what is deliberately not done yet.

## Modules that exist

| module | target | contents |
| --- | --- | --- |
| `base/` | `jot_base` | string, path, shell and link helpers, atomic file writes, the event loop, the task queue, process jobs |
| `text/` | `jot_text` | cursor, lines, search, sort, transforms, text objects, line providers, insertion rules |
| `syntax/` | `jot_syntax` | language detection, highlighting, folding, the tree-sitter runtime, the C++ and colour scanners, the symbol index |
| `dap/` | `jot_dap` | the debug adapter protocol client and its wire format |
| `lsp/` | `jot_lsp` | the language server protocol client, its wire format, the completion ranking |
| `terminal/` | `jot_terminal` | pty-backed sessions and their panel (libvterm lives here) |

`src/edit/` became `src/text/`, and `jot_edit` became `jot_text`, so the build
graph now says what the module order says. `features/` and `tools/` lost the
files that were only visiting: `tools/` is down to discord, telescope, the image
viewer and workspace search.

Every move that crossed a directory also rewrote the callers' include strings to
name the module (`base/string_util.h`, `syntax/folding.h`, `lsp/client.h`), so
the boundary is visible in the source rather than only in the build graph.

## What the checkers say

Two rules are now enforced in CI, both as ratchets: the recorded debt is allowed
to shrink and never allowed to grow.

- `tools/check_module_deps.py` resolves every quoted include the way the
  compiler does and fails on an include that points from a lower module to a
  higher one. Today: **206 upward includes**, in the
  `docs/refactor/module-dep-baseline.txt` baseline. The pairs worth looking at
  first: `render -> editor` 43, `editor -> lua` 28, `text -> editor` 25,
  `input -> editor` 23, `render -> lua` 22, `text -> lua` 17.
- `tools/check_headers.py` compiles every header alone with the flags from
  `build/compile_commands.json`. Today: **152 of 168 stand alone**. The 16 that
  do not are the `src/jot/editor/api/` fragments, which are pieces of a class
  body rather than headers, so they cannot compile alone by construction.
  `src/jot/lua/view/plugins.h` genuinely needed `view/shared.h` for `FloatSpan`
  and now includes it.

Both were checked with teeth: a `#include "jot/editor.h"` added to
`src/base/file_util.h` fails the module check with exit 1, and dropping the
`shared.h` include from `plugins.h` fails the header check at line 79 with
`'FloatSpan' was not declared in this scope`.

## The one blocker, restated

136 of 271 `.cpp` files in `src/` define `Editor::` methods. That is why no
further module can be cut cleanly: moving those files relabels a directory
without creating a boundary, because the moved file still includes `editor.h`
and the module still points upward.

This is phase 0 decision 1, and the answer taken here is phase 0's
recommendation: cut modules as their ownership moves, not before. It is visible
in two places where a naive move was declined:

- `src/jot/integrations/lsp/*.cpp` stayed put. They are 13 of 13 `Editor::`
  method files; only the protocol client moved, so `lsp/` has no `editor.h` edge.
- `git/` was not created. `git.cpp` is 14 `Editor::` methods, `git_panel.cpp` is
  24, `git_panel_paint.cpp` and `git_diff.cpp` are 1 each; only `git_run.h` and
  `git_panel_models.h` are leaf headers. A two-header module is not worth the
  `git mv`, and the rest has to wait for the facade.

`jot/` internals (`model/`, `state/`, `app/`, `integrations/`, `surfaces/`) are
provisionally mapped to the `editor` module by both checkers, which is why
`render -> editor` and `text -> editor` dominate the baseline.

## Not done yet

- **`src/jot/app` -> `editor/`.** 33 files, of which `bible_client.cpp`,
  `coding_time.cpp`, `discord_session.cpp` and `emmet.cpp` belong to `extras/`
  and have to split out first.
- **The `Editor` facade** and the 16 `editor/api/` fragments. This is the work
  that unblocks everything else, and it is the largest single piece left.
- **`render/`, `ui/`, `input/`, `workspace/`, `extras/`, `lua/`** module cuts.
- **`.vscode/` and `CMakeSettings.json`** are still tracked: phase 0 marked them
  as ask-first, and nobody has been asked.
- **The WakaTime key** is still plain text in `<config root>/configs/settings.conf`
  (phase 0 section 7). Moving it to the keyring the way LeetCode works is a
  behaviour change and belongs in its own change set.

## Known red, verified not ours

`test/test_home_open.cpp:292` fails in every full run of the suite (956 cases,
954 passed, 1 failed). It fails identically on the tree before this work.

`fold_probe.py` and `cpp_defs_probe.py` fail. They were run against a clean
`git worktree` of the commit before the syntax module moved and fail there too,
with the same messages (`fold probe: FAIL (2)`, `cpp defs probe: FAIL -
:cppcheck next did not walk to the header's finding: ''`). They are not caused
by the moves.
