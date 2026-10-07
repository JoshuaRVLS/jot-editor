# Phase 1: the base module

Phase 0 asked for decisions before any file moved. This phase answers the two
that blocked it, creates the first real module, and records the one housekeeping
item that turned out to be load-bearing.

## What moved

`src/base/` is new. It is the only directory in the tree with no edge pointing
up into another module.

| was | now |
| --- | --- |
| `src/tools/string_util.h` | `src/base/string_util.h` |
| `src/tools/shell_util.h` | `src/base/shell_util.h` |
| `src/tools/url_span.h` | `src/base/url_span.h` |
| `src/tools/external_open.h` | `src/base/external_open.h` |
| `src/tools/file_util.{h,cpp}` | `src/base/file_util.{h,cpp}` |
| `src/jot/event_loop.h` | `src/base/event_loop.h` |
| `src/jot/task_queue.h` | `src/base/task_queue.h` |
| `src/jot/event/event_loop.cpp` | `src/base/event_loop.cpp` |
| `src/jot/event/task_queue.cpp` | `src/base/task_queue.cpp` |
| `src/jot/app/process_job.{h,cpp}` | `src/base/process_job.{h,cpp}` |

Every caller now writes the path with its module prefix:
`#include "base/string_util.h"`, not `#include "tools/string_util.h"`. That is
mechanical over 64 files, and it is what makes the module boundary visible in
the source instead of only in the build.

`src/base/sources.cmake` declares `JOT_BASE_SOURCES`; `src/CMakeLists.txt` adds
`jot_base_obj` and `jot_base` beside the other module targets and folds the
objects into `jot_engine`.

## The include that was not needed

`src/jot/event/event_loop.cpp` included `editor.h`, `jot/keybind_catalog.h` and
`jot/lua/api.h`. None of the three was used: the only mention of the Editor in
the file was the comment `// Editor Event Loop` above a blank line. Removing
them lets the base module compile without reaching up the tree at all, which is
the property the whole phase exists to establish.

## `.configs/configs` is deliberate, not a doubled directory

Phase 0 flagged `.configs/configs/` as "one `configs/` level too many". Reading
the consumers says otherwise, so it was left alone:

- `src/jot/lua/api_theme.cpp` looks for `<repo>/.configs/configs/colors` by
  name, and its comment states the mapping: "the bundled themes are
  `<repo>/.configs/configs/colors`".
- `install(DIRECTORY .configs/configs DESTINATION ${CMAKE_INSTALL_DATADIR}/jot)`
  puts those bytes at `<datadir>/jot/configs/colors`, which is where
  `api_theme.cpp` looks next, and where the runtime's own config directory
  (`config_root/configs/`) lives.

The nesting mirrors the installed tree one for one. Collapsing it would mean
rewriting the install rule to re-add the `configs` component and teaching
`api_theme.cpp` a second dev-tree shape, with no change to what ships. It is
left as is; if the layout should change, it is a behaviour change and belongs in
its own change set with the runtime paths updated in lockstep.

## Unchanged on purpose

- `src/jot/app` still holds the `Editor` implementation. Its rename to
  `editor/` is a move of 33 files that also has to split `extras` out of it
  (`bible_client.cpp`, `coding_time.cpp`, `discord_session.cpp`, `emmet.cpp`),
  so it wants its own phase rather than riding along with the base cut.
- `.vscode/` and `CMakeSettings.json` were a "ask first" item in phase 0 and
  are still tracked.
- `src/jot/lua/view/plugins.h` needed a real fix: it declares
  `std::map<int, std::vector<FloatSpan>>` while `FloatSpan` lives in
  `view/shared.h`, so it only compiled when `api.h` happened to include
  `shared.h` first. It now includes `shared.h` itself.

## Verification

- `cmake --build build -j8` and `cmake --build build-tests -j8`: both clean.
- `./build-tests/test/jot_tests`: 956 cases, 954 passed, 1 failed. The single
  failure is `test/test_home_open.cpp:292`, which fails identically before this
  phase and is unrelated to it.
- `python3 test/string_util_probe.py build/apps/jot/jot`: PASS.
- `python3 test/markdown_preview_probe.py build/apps/jot/jot`: PASS, which
  exercises the moved event loop through the preview server.
- The `plugins.h` fix was checked with teeth: drop the include and the header
  fails with `'FloatSpan' was not declared in this scope` at line 79; put it
  back and it compiles.
