# Phase 0 inventory

Working document for the structural refactor. No files were moved to produce it:
every number below is measured from the tree as it stands.

## How this was measured

- module mapping and include edges: a script that resolves every `#include` of a
  project header against the same include dirs the build uses
  (`src`, `src/jot`, `src/input`, `src/render`, `src/edit`, `src/features`,
  `src/ui`, `src/jot/lua`, `src/tools`), maps both ends to their target module,
  and compares the two against the target order.
- self-containment: every header under `src/` compiled as its own TU with the
  `-I` / `-D` set taken from `compile_commands.json`.
- line counts: `wc -l`.

## Headline numbers

| measure | value |
| --- | --- |
| files in `src/` | 448 (271 `.cpp`, 168 `.h`, 9 other) |
| files per target module | editor 101, lua 49, render 39, text 38, syntax 37, ui 34, extras 32, workspace 27, input 27, lsp 26, base 17, dap 6, terminal 6 |
| target modules with no files yet | `platform` (raw-mode/console code lives inside `ui/terminal*.cpp` today) |
| resolved include edges | 1019 |
| include edges that point *up* the target order | 244 |
| `.cpp` files that define `Editor::` methods | 136 of 271 |
| files over 800 lines | 42 |
| headers that do not compile standalone | 17 of 168 |
| duplicate basenames | 34 |
| same-module basename collisions after the move | 13 |
| `sources.cmake` files | 7 |
| `test/` entries | 230, all in one flat directory |

## 1. The blocker: most sources are `Editor` methods

This is the finding that decides the shape of phases 1-3. The Editor is one
class whose body is textually assembled from fragment headers
(`src/jot/editor/api/*.h`, 16 of them, none self-contained), and most
translation units in the *leaf* directories are its method definitions:

| directory | `.cpp` files | with `Editor::` |
| --- | --- | --- |
| src/edit | 22 | 21 |
| src/render | 30 | 26 |
| src/input (incl. subdirs) | 22 | 20 |
| src/jot/integrations/lsp | 13 | 13 |
| src/jot/workspace | 9 | 9 |
| src/jot/app | 28 | 25 |
| src/jot/surfaces | 6 | 6 |
| src/ui | 8 | 3 |
| src/jot/lua | 42 | 2 |
| src/features | 35 | 0 |
| src/tools | 9 | 0 |

Consequence: `git mv src/edit/ lines into src/jot/text/` moves files, but the
moved files still say `#include "editor.h"` and still define `Editor::`
methods, so `jot_text` would have to link the whole editor. **The leaf modules
cannot be cut until the Editor facade work of phase 5 exists, or the facade
work has to be pulled forward.** Section 9 asks for a decision on this.

The same is true of the fragment headers: `src/jot/editor/api/*.h` are not
headers at all, they are class-body fragments declared `private:`/`public:` and
included from `src/jot/editor.h`. They are the reason 17 of 168 headers fail
the standalone compile.

## 2. File mapping

`new name` is a proposal: unchanged basenames unless a rename is needed. The
`flag` column carries the files that need a decision.

| current path | target module | new name | flag |
| --- | --- | --- | --- |
| `src/edit/clipboard.cpp` | text | `text/clipboard.cpp` |  |
| `src/edit/commenting.h` | text | `text/commenting.h` |  |
| `src/edit/cursor.cpp` | text | `text/cursor.cpp` |  |
| `src/edit/datetime.cpp` | text | `text/datetime.cpp` |  |
| `src/edit/duplicate.cpp` | text | `text/duplicate.cpp` |  |
| `src/edit/edit.cpp` | text | `text/edit.cpp` |  |
| `src/edit/increment.cpp` | text | `text/increment.cpp` |  |
| `src/edit/join_lines.cpp` | text | `text/join_lines.cpp` |  |
| `src/edit/lines.cpp` | text | `text/lines.cpp` |  |
| `src/edit/multicursor.cpp` | text | `text/multicursor.cpp` |  |
| `src/edit/path_clipboard.cpp` | text | `text/path_clipboard.cpp` |  |
| `src/edit/replace.cpp` | text | `text/replace.cpp` |  |
| `src/edit/reverse_lines.cpp` | text | `text/reverse_lines.cpp` |  |
| `src/edit/search.cpp` | text | `text/search.cpp` |  |
| `src/edit/shuffle_lines.cpp` | text | `text/shuffle_lines.cpp` |  |
| `src/edit/sidebar.cpp` | workspace | `workspace/sidebar_paint.cpp` | basename collision: with src/jot/workspace/sidebar.cpp (both -> workspace) |
| `src/edit/sidebar_guides.h` | workspace | `workspace/sidebar_guides.h` |  |
| `src/edit/sort_desc.cpp` | text | `text/sort_desc.cpp` |  |
| `src/edit/stats.cpp` | text | `text/stats.cpp` |  |
| `src/edit/surround.cpp` | text | `text/surround.cpp` |  |
| `src/edit/textobject.cpp` | text | `text/textobject.cpp` |  |
| `src/edit/transform.cpp` | text | `text/transform.cpp` |  |
| `src/edit/trim_blank.cpp` | text | `text/trim_blank.cpp` |  |
| `src/edit/unique_lines.cpp` | text | `text/unique_lines.cpp` |  |
| `src/features/autoclose.cpp` | text | `text/autoclose.cpp` |  |
| `src/features/autoclose.h` | text | `text/autoclose.h` |  |
| `src/features/bible.cpp` | extras | `extras/bible.cpp` | extras (hooks, core must not include it) |
| `src/features/bible.h` | extras | `extras/bible.h` | extras (hooks, core must not include it) |
| `src/features/bracket.cpp` | syntax | `syntax/bracket.cpp` |  |
| `src/features/bracket.h` | syntax | `syntax/bracket.h` |  |
| `src/features/bracket_text_object.cpp` | text | `text/bracket_text_object.cpp` |  |
| `src/features/bracket_text_object.h` | text | `text/bracket_text_object.h` |  |
| `src/features/coding_time.cpp` | extras | `extras/coding_time/probe.cpp` | extras (hooks, core must not include it) |
| `src/features/coding_time.h` | extras | `extras/coding_time/probe.h` | basename collision: with src/jot/editor/api/coding_time.h (both -> extras); extras (hooks, core must not include it) |
| `src/features/color_codes.cpp` | syntax | `syntax/color_codes.cpp` |  |
| `src/features/color_codes.h` | syntax | `syntax/color_codes.h` |  |
| `src/features/color_definitions.cpp` | syntax | `syntax/color_definitions.cpp` |  |
| `src/features/color_definitions.h` | syntax | `syntax/color_definitions.h` |  |
| `src/features/color_functions.cpp` | syntax | `syntax/color_functions.cpp` |  |
| `src/features/color_functions.h` | syntax | `syntax/color_functions.h` |  |
| `src/features/color_names.cpp` | syntax | `syntax/color_names.cpp` |  |
| `src/features/color_names.h` | syntax | `syntax/color_names.h` |  |
| `src/features/color_space.cpp` | syntax | `syntax/color_space.cpp` |  |
| `src/features/color_space.h` | syntax | `syntax/color_space.h` |  |
| `src/features/color_tailwind.cpp` | syntax | `syntax/color_tailwind.cpp` |  |
| `src/features/color_tailwind.h` | syntax | `syntax/color_tailwind.h` |  |
| `src/features/color_terminal_codes.cpp` | syntax | `syntax/color_terminal_codes.cpp` |  |
| `src/features/color_terminal_codes.h` | syntax | `syntax/color_terminal_codes.h` |  |
| `src/features/column_utils.h` | text | `text/column_utils.h` |  |
| `src/features/completion_rank.cpp` | lsp | `lsp/completion_rank.cpp` |  |
| `src/features/completion_rank.h` | lsp | `lsp/completion_rank.h` |  |
| `src/features/config.cpp` | workspace | `workspace/config.cpp` | ambiguous: base (a settings store) vs workspace (it keys off the workspace) |
| `src/features/config.h` | workspace | `workspace/config.h` | ambiguous: base (a settings store) vs workspace (it keys off the workspace) |
| `src/features/cpp_assist.cpp` | syntax | `syntax/cpp_assist.cpp` |  |
| `src/features/cpp_assist.h` | syntax | `syntax/cpp_assist.h` |  |
| `src/features/cpp_definitions.cpp` | syntax | `syntax/cpp_definitions.cpp` |  |
| `src/features/cpp_definitions.h` | syntax | `syntax/cpp_definitions.h` |  |
| `src/features/cpp_definitions_analysis.cpp` | syntax | `syntax/cpp_definitions_analysis.cpp` |  |
| `src/features/cpp_flags.cpp` | syntax | `syntax/cpp_flags.cpp` |  |
| `src/features/cpp_flags.h` | syntax | `syntax/cpp_flags.h` |  |
| `src/features/emmet.cpp` | extras | `extras/emmet/expand.cpp` | extras (hooks, core must not include it) |
| `src/features/emmet.h` | extras | `extras/emmet.h` | extras (hooks, core must not include it) |
| `src/features/folding.cpp` | syntax | `syntax/folding.cpp` |  |
| `src/features/folding.h` | syntax | `syntax/folding.h` |  |
| `src/features/html.cpp` | workspace | `workspace/html.cpp` | ambiguous: workspace (HTML tag completion) vs text |
| `src/features/html.h` | workspace | `workspace/html.h` | ambiguous: workspace (HTML tag completion) vs text |
| `src/features/http_file.cpp` | workspace | `workspace/http_file.cpp` | ambiguous: extras (a REST surface) vs workspace |
| `src/features/http_file.h` | workspace | `workspace/http_file.h` | ambiguous: extras (a REST surface) vs workspace |
| `src/features/language.cpp` | syntax | `syntax/language.cpp` |  |
| `src/features/language.h` | syntax | `syntax/language.h` |  |
| `src/features/leetcode_http.cpp` | extras | `extras/leetcode_http.cpp` | extras (hooks, core must not include it) |
| `src/features/leetcode_http.h` | extras | `extras/leetcode_http.h` | extras (hooks, core must not include it) |
| `src/features/quote_text_object.cpp` | text | `text/quote_text_object.cpp` |  |
| `src/features/quote_text_object.h` | text | `text/quote_text_object.h` |  |
| `src/features/save_hygiene.cpp` | workspace | `workspace/save_hygiene.cpp` | ambiguous: workspace (save-time lint) vs syntax |
| `src/features/save_hygiene.h` | workspace | `workspace/save_hygiene.h` | ambiguous: workspace (save-time lint) vs syntax |
| `src/features/smooth_scroll.cpp` | ui | `ui/smooth_scroll.cpp` |  |
| `src/features/smooth_scroll.h` | ui | `ui/smooth_scroll.h` |  |
| `src/features/status_clock.cpp` | extras | `extras/status_clock.cpp` | ambiguous: extras (per the phase list) or ui (it feeds the status line); extras (hooks, core must not include it) |
| `src/features/status_clock.h` | extras | `extras/status_clock.h` | ambiguous: extras (per the phase list) or ui (it feeds the status line); extras (hooks, core must not include it) |
| `src/features/syntax.cpp` | syntax | `syntax/syntax.cpp` |  |
| `src/features/syntax_highlighter.h` | syntax | `syntax/syntax_highlighter.h` |  |
| `src/features/tab_order.cpp` | ui | `ui/tab_order.cpp` |  |
| `src/features/tab_order.h` | ui | `ui/tab_order.h` |  |
| `src/features/text_features.cpp` | text | `text/text_features.cpp` |  |
| `src/features/text_features.h` | text | `text/text_features.h` |  |
| `src/features/textobjects.cpp` | text | `text/textobjects.cpp` |  |
| `src/features/textobjects.h` | text | `text/textobjects.h` |  |
| `src/features/tree_sitter/install.cpp` | syntax | `syntax/install.cpp` |  |
| `src/features/tree_sitter/install.h` | syntax | `syntax/install.h` |  |
| `src/features/tree_sitter/manager.cpp` | syntax | `syntax/manager.cpp` |  |
| `src/features/tree_sitter/manager.h` | syntax | `syntax/manager.h` |  |
| `src/features/tree_sitter/queries.cpp` | syntax | `syntax/queries.cpp` |  |
| `src/features/tree_sitter/runtime.cpp` | syntax | `syntax/runtime.cpp` |  |
| `src/features/wakatime.cpp` | extras | `extras/wakatime.cpp` | extras (hooks, core must not include it) |
| `src/features/wakatime.h` | extras | `extras/wakatime.h` | extras (hooks, core must not include it) |
| `src/features/web_completion.cpp` | workspace | `workspace/web_completion.cpp` | ambiguous: workspace (it indexes the tree) vs lsp |
| `src/features/web_completion.h` | workspace | `workspace/web_completion.h` | ambiguous: workspace (it indexes the tree) vs lsp |
| `src/features/winbar.cpp` | render | `render/winbar_model.cpp` | basename collision: with src/render/winbar.cpp (both -> render) |
| `src/features/winbar.h` | render | `render/winbar_model.h` |  |
| `src/input/commands/actions.cpp` | input | `input/actions.cpp` |  |
| `src/input/commands/ex.cpp` | input | `input/ex.cpp` |  |
| `src/input/commands/ex_tail.cpp` | input | `input/ex_tail.cpp` |  |
| `src/input/commands/execute.cpp` | input | `input/execute.cpp` |  |
| `src/input/commands/help.cpp` | input | `input/help.cpp` |  |
| `src/input/commands/navigation.cpp` | input | `input/navigation.cpp` |  |
| `src/input/commands/rename_prompt.cpp` | input | `input/rename_prompt.cpp` |  |
| `src/input/commands/save_prompt.cpp` | input | `input/save_prompt.cpp` |  |
| `src/input/commands/utils.cpp` | input | `input/ex_arguments.cpp` |  |
| `src/input/commands/utils.h` | input | `input/ex_arguments.h` |  |
| `src/input/menu_bar.cpp` | input | `input/menu_bar_model.cpp` | basename collision: with src/input/mouse/menu_bar.cpp (both -> input) |
| `src/input/modes/dispatch.cpp` | input | `input/dispatch.cpp` |  |
| `src/input/modes/modeless.cpp` | input | `input/modeless.cpp` |  |
| `src/input/mouse/context_menu.cpp` | input | `input/context_menu.cpp` |  |
| `src/input/mouse/dispatcher.cpp` | input | `input/dispatcher.cpp` |  |
| `src/input/mouse/menu_bar.cpp` | input | `input/menu_bar.cpp` |  |
| `src/input/mouse/mouse_internal.h` | input | `input/detail/selection_clamp.h` |  |
| `src/input/mouse/pane_tabs.cpp` | input | `input/pane_tabs.cpp` |  |
| `src/input/mouse/panels.cpp` | input | `input/panels.cpp` |  |
| `src/input/mouse/telescope.cpp` | input | `input/telescope.cpp` | basename collision: with src/tools/telescope.cpp (both -> input) |
| `src/input/palette/input.cpp` | input | `input/input.cpp` |  |
| `src/input/palette/palette.cpp` | input | `input/palette.cpp` |  |
| `src/input/palette/suggestions.cpp` | input | `input/suggestions.cpp` |  |
| `src/jot/app/auto_save.cpp` | editor | `editor/auto_save.cpp` |  |
| `src/jot/app/bible_client.cpp` | extras | `extras/bible_client.cpp` | extras (hooks, core must not include it) |
| `src/jot/app/bookmarks.cpp` | editor | `editor/bookmarks.cpp` |  |
| `src/jot/app/buffers.cpp` | editor | `editor/buffers.cpp` |  |
| `src/jot/app/coding_time.cpp` | extras | `extras/coding_time/wiring.cpp` | basename collision: with src/features/coding_time.cpp (both -> extras); extras (hooks, core must not include it) |
| `src/jot/app/cpp_definitions.cpp` | editor | `editor/cpp_definitions.cpp` |  |
| `src/jot/app/decorations.cpp` | editor | `editor/decorations.cpp` |  |
| `src/jot/app/decorations.h` | editor | `editor/decoration_wiring.h` | basename collision: with src/jot/model/decorations.h (both -> editor) |
| `src/jot/app/discord_session.cpp` | extras | `extras/discord_session.cpp` | extras (hooks, core must not include it) |
| `src/jot/app/editor.cpp` | editor | `editor/editor.cpp` |  |
| `src/jot/app/emmet.cpp` | extras | `extras/emmet/wiring.cpp` | basename collision: with src/features/emmet.cpp (both -> extras); extras (hooks, core must not include it) |
| `src/jot/app/file.cpp` | editor | `editor/file.cpp` |  |
| `src/jot/app/file_internal.h` | editor | `editor/detail/state_file_fields.h` |  |
| `src/jot/app/fold_state.cpp` | editor | `editor/fold_state.cpp` |  |
| `src/jot/app/folding.cpp` | editor | `editor/folding.cpp` |  |
| `src/jot/app/jumplist.cpp` | editor | `editor/jumplist.cpp` |  |
| `src/jot/app/panes_internal.h` | editor | `editor/detail/pane_geometry.h` |  |
| `src/jot/app/panes_layout.cpp` | editor | `editor/panes_layout.cpp` |  |
| `src/jot/app/panes_navigate.cpp` | editor | `editor/panes_navigate.cpp` |  |
| `src/jot/app/panes_resize.cpp` | editor | `editor/panes_resize.cpp` |  |
| `src/jot/app/panes_tabs.cpp` | editor | `editor/panes_tabs.cpp` |  |
| `src/jot/app/process_job.cpp` | editor | `editor/process_job.cpp` |  |
| `src/jot/app/process_job.h` | editor | `editor/process_job.h` |  |
| `src/jot/app/recent.cpp` | editor | `editor/recent.cpp` |  |
| `src/jot/app/relaunch.cpp` | editor | `editor/relaunch.cpp` |  |
| `src/jot/app/relaunch.h` | editor | `editor/relaunch.h` |  |
| `src/jot/app/resize.cpp` | editor | `editor/resize.cpp` |  |
| `src/jot/app/rest_client.cpp` | editor | `editor/rest_client.cpp` |  |
| `src/jot/app/smooth_scroll.cpp` | editor | `editor/smooth_scroll.cpp` |  |
| `src/jot/app/test_hooks.cpp` | editor | `editor/test_hooks.cpp` |  |
| `src/jot/app/undo.cpp` | editor | `editor/undo.cpp` |  |
| `src/jot/app/utils.cpp` | editor | `editor/buffer_access.cpp` |  |
| `src/jot/app/web_index.cpp` | editor | `editor/web_index.cpp` |  |
| `src/jot/editor.h` | editor | `editor/editor.h` |  |
| `src/jot/editor/api/bible.h` | extras | `extras/bible/wiring.h` | Editor-body fragment (Phase 5); basename collision: with src/features/bible.h; extras (hooks, core must not include it) |
| `src/jot/editor/api/buffers.h` | editor | `editor/buffers.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/coding_time.h` | extras | `extras/coding_time/wiring.h` | Editor-body fragment (Phase 5); basename collision: with src/features/coding_time.h; extras (hooks, core must not include it) |
| `src/jot/editor/api/commands.h` | editor | `editor/commands.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/cpp.h` | editor | `editor/cpp.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/editing.h` | editor | `editor/editing.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/explorer.h` | editor | `editor/explorer.h` | Editor-body fragment (Phase 5); basename collision: with src/jot/model/explorer.h |
| `src/jot/editor/api/lsp_ui.h` | editor | `editor/lsp_ui.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/paint.h` | editor | `editor/paint.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/panels.h` | editor | `editor/panels.h` | Editor-body fragment (Phase 5); basename collision: with src/jot/model/panels.h |
| `src/jot/editor/api/panes.h` | editor | `editor/panes.h` | Editor-body fragment (Phase 5); basename collision: with src/jot/model/panes.h |
| `src/jot/editor/api/public_api.h` | editor | `editor/public_api.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/rest.h` | editor | `editor/rest.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/runtime.h` | editor | `editor/runtime.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/test_hooks.h` | editor | `editor/test_hooks.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/api/test_hooks_ui.h` | editor | `editor/test_hooks_ui.h` | Editor-body fragment (Phase 5) |
| `src/jot/editor/discord_controller.h` | extras | `extras/discord_controller.h` | extras (hooks, core must not include it) |
| `src/jot/editor/search_controller.h` | editor | `editor/search_controller.h` |  |
| `src/jot/editor_models.h` | editor | `editor/editor_models.h` | ambiguous: umbrella re-export across modules; retire it instead of moving |
| `src/jot/editor_state.h` | editor | `editor/editor_state.h` |  |
| `src/jot/event/event_loop.cpp` | base | `base/event_loop.cpp` |  |
| `src/jot/event/gui_events.cpp` | base | `base/gui_events.cpp` |  |
| `src/jot/event/in_memory_line_provider.cpp` | text | `text/in_memory_line_provider.cpp` |  |
| `src/jot/event/keymap_candidates.h` | base | `base/keymap_candidates.h` |  |
| `src/jot/event/lazy_line_provider.cpp` | text | `text/lazy_line_provider.cpp` |  |
| `src/jot/event/main_loop.cpp` | base | `base/main_loop.cpp` |  |
| `src/jot/event/task_queue.cpp` | base | `base/task_queue.cpp` |  |
| `src/jot/event/terminal_events.cpp` | base | `base/terminal_events.cpp` |  |
| `src/jot/event/which_key.cpp` | base | `base/which_key.cpp` |  |
| `src/jot/event_loop.h` | base | `base/event_loop.h` |  |
| `src/jot/file_icons.h` | editor | `editor/file_icons.h` | ambiguous: ui (row icons) vs base (the icon table) |
| `src/jot/host_api.h` | editor | `editor/host_api.h` |  |
| `src/jot/in_memory_line_provider.h` | text | `text/in_memory_line_provider.h` |  |
| `src/jot/integrations/comment_style_data.h` | editor | `editor/comment_style_data.h` |  |
| `src/jot/integrations/debugger_adapters.cpp` | editor | `editor/debugger_adapters.cpp` |  |
| `src/jot/integrations/debugger_breakpoints.cpp` | editor | `editor/debugger_breakpoints.cpp` |  |
| `src/jot/integrations/debugger_config.cpp` | editor | `editor/debugger_config.cpp` |  |
| `src/jot/integrations/debugger_internal.h` | editor | `dap/detail/debugger_support.h` |  |
| `src/jot/integrations/debugger_session.cpp` | editor | `editor/debugger_session.cpp` |  |
| `src/jot/integrations/integrated_terminal.cpp` | editor | `editor/integrated_terminal.cpp` |  |
| `src/jot/integrations/lsp/attach.cpp` | lsp | `lsp/attach.cpp` |  |
| `src/jot/integrations/lsp/code_actions.cpp` | lsp | `lsp/code_actions.cpp` |  |
| `src/jot/integrations/lsp/common.h` | lsp | `lsp/common.h` |  |
| `src/jot/integrations/lsp/completion.cpp` | lsp | `lsp/completion.cpp` |  |
| `src/jot/integrations/lsp/diagnostics.cpp` | lsp | `lsp/diagnostics.cpp` |  |
| `src/jot/integrations/lsp/format.cpp` | lsp | `lsp/format.cpp` |  |
| `src/jot/integrations/lsp/hover.cpp` | lsp | `lsp/hover.cpp` |  |
| `src/jot/integrations/lsp/inlay_hints.cpp` | lsp | `lsp/inlay_hints.cpp` |  |
| `src/jot/integrations/lsp/install.cpp` | lsp | `lsp/install.cpp` | basename collision: with src/tools/lsp/install.cpp (both -> lsp) |
| `src/jot/integrations/lsp/lifecycle.cpp` | lsp | `lsp/lifecycle.cpp` |  |
| `src/jot/integrations/lsp/matching.h` | lsp | `lsp/matching.h` |  |
| `src/jot/integrations/lsp/references.cpp` | lsp | `lsp/references.cpp` |  |
| `src/jot/integrations/lsp/rename.cpp` | lsp | `lsp/rename.cpp` |  |
| `src/jot/integrations/lsp/signature.cpp` | lsp | `lsp/signature.cpp` |  |
| `src/jot/integrations/lsp/status.cpp` | lsp | `lsp/status.cpp` |  |
| `src/jot/integrations/lsp_attach_data.h` | editor | `editor/lsp_attach_data.h` |  |
| `src/jot/integrations/syntax.cpp` | editor | `editor/syntax.cpp` |  |
| `src/jot/integrations/terminal_float.cpp` | editor | `editor/terminal_float.cpp` |  |
| `src/jot/integrations/tree_sitter_commands.cpp` | editor | `editor/tree_sitter_commands.cpp` |  |
| `src/jot/keybind_catalog.h` | input | `input/keybind_catalog.h` |  |
| `src/jot/lazy_line_provider.h` | text | `text/lazy_line_provider.h` |  |
| `src/jot/line_provider.h` | text | `text/line_provider.h` |  |
| `src/jot/lua/api.h` | lua | `lua/api.h` |  |
| `src/jot/lua/api_bindings.cpp` | lua | `lua/api/bindings.cpp` |  |
| `src/jot/lua/api_buffer.cpp` | lua | `lua/api/buffer.cpp` |  |
| `src/jot/lua/api_config.cpp` | lua | `lua/api/config.cpp` |  |
| `src/jot/lua/api_debugger.cpp` | lua | `lua/api/debugger.cpp` |  |
| `src/jot/lua/api_decorations.cpp` | lua | `lua/api/decorations.cpp` |  |
| `src/jot/lua/api_editor.cpp` | lua | `lua/api/editor.cpp` |  |
| `src/jot/lua/api_emmet.cpp` | extras | `extras/api_emmet.cpp` | extras (hooks, core must not include it) |
| `src/jot/lua/api_events.cpp` | lua | `lua/api/events.cpp` |  |
| `src/jot/lua/api_float.cpp` | lua | `lua/api/float.cpp` |  |
| `src/jot/lua/api_git.cpp` | lua | `lua/api/git.cpp` |  |
| `src/jot/lua/api_internal.h` | lua | `lua/detail/lua_table_access.h` |  |
| `src/jot/lua/api_lsp_bridge.cpp` | lua | `lua/api/lsp_bridge.cpp` |  |
| `src/jot/lua/api_lsp_install.cpp` | lua | `lua/api/lsp_install.cpp` |  |
| `src/jot/lua/api_mark.cpp` | lua | `lua/api/mark.cpp` |  |
| `src/jot/lua/api_plugin.cpp` | lua | `lua/api/plugin.cpp` |  |
| `src/jot/lua/api_preview.cpp` | lua | `lua/api/preview.cpp` |  |
| `src/jot/lua/api_runtime.cpp` | lua | `lua/api/runtime.cpp` |  |
| `src/jot/lua/api_status.cpp` | lua | `lua/api/status.cpp` |  |
| `src/jot/lua/api_task.cpp` | lua | `lua/api/task.cpp` |  |
| `src/jot/lua/api_terminal.cpp` | lua | `lua/api/terminal.cpp` |  |
| `src/jot/lua/api_theme.cpp` | lua | `lua/api/theme.cpp` |  |
| `src/jot/lua/api_toast.cpp` | lua | `lua/api/toast.cpp` |  |
| `src/jot/lua/api_treesitter.cpp` | lua | `lua/api/treesitter.cpp` |  |
| `src/jot/lua/api_ui_surfaces.cpp` | lua | `lua/api/ui_surfaces.cpp` |  |
| `src/jot/lua/api_viewport.cpp` | lua | `lua/api/viewport.cpp` |  |
| `src/jot/lua/bindings_buffer.cpp` | lua | `lua/bindings/buffer.cpp` |  |
| `src/jot/lua/bindings_config.cpp` | lua | `lua/bindings/config.cpp` |  |
| `src/jot/lua/bindings_debugger.cpp` | lua | `lua/bindings/debugger.cpp` |  |
| `src/jot/lua/bindings_editor.cpp` | lua | `lua/bindings/editor.cpp` |  |
| `src/jot/lua/bindings_events.cpp` | lua | `lua/bindings/events.cpp` |  |
| `src/jot/lua/bindings_git.cpp` | lua | `lua/bindings/git.cpp` |  |
| `src/jot/lua/bindings_internal.h` | lua | `lua/detail/binding_decls.h` |  |
| `src/jot/lua/bindings_layout.cpp` | lua | `lua/bindings/layout.cpp` |  |
| `src/jot/lua/bindings_leetcode.cpp` | extras | `extras/bindings_leetcode.cpp` | extras (hooks, core must not include it) |
| `src/jot/lua/bindings_lsp.cpp` | lua | `lua/bindings/lsp.cpp` |  |
| `src/jot/lua/bindings_mark.cpp` | lua | `lua/bindings/mark.cpp` |  |
| `src/jot/lua/bindings_plugin.cpp` | lua | `lua/bindings/plugin.cpp` |  |
| `src/jot/lua/bindings_preview.cpp` | lua | `lua/bindings/preview.cpp` |  |
| `src/jot/lua/bindings_state.cpp` | lua | `lua/bindings/state.cpp` |  |
| `src/jot/lua/bindings_syntax.cpp` | lua | `lua/bindings/syntax.cpp` |  |
| `src/jot/lua/bindings_task.cpp` | lua | `lua/bindings/task.cpp` |  |
| `src/jot/lua/bindings_ui.cpp` | lua | `lua/bindings/ui.cpp` |  |
| `src/jot/lua/bindings_viewport.cpp` | lua | `lua/bindings/viewport.cpp` |  |
| `src/jot/lua/embedded_lua.h` | lua | `lua/embedded_lua.h` |  |
| `src/jot/lua/lua_loader.cpp` | lua | `lua/lua_loader.cpp` |  |
| `src/jot/lua/lua_loader.h` | lua | `lua/lua_loader.h` |  |
| `src/jot/lua/view/plugins.h` | lua | `lua/plugins.h` |  |
| `src/jot/lua/view/runtime.h` | lua | `lua/runtime.h` |  |
| `src/jot/lua/view/shared.h` | lua | `lua/shared.h` |  |
| `src/jot/lua/view/surfaces.h` | lua | `lua/surfaces.h` |  |
| `src/jot/markdown/http_message.cpp` | editor | `editor/http_message.cpp` | ambiguous: base (HTTP message parsing) |
| `src/jot/markdown/http_message.h` | editor | `editor/http_message.h` | ambiguous: base (HTTP message parsing) |
| `src/jot/markdown/preview_server.cpp` | editor | `editor/preview_server.cpp` | ambiguous: editor (it is editor wiring) vs extras |
| `src/jot/markdown/preview_server.h` | editor | `editor/preview_server.h` | ambiguous: editor (it is editor wiring) vs extras |
| `src/jot/model/buffer.h` | editor | `editor/buffer.h` | ambiguous: text |
| `src/jot/model/debugger.h` | editor | `editor/debugger.h` | ambiguous: dap |
| `src/jot/model/decorations.h` | editor | `editor/decorations.h` | ambiguous: text (anchored marks on a buffer) |
| `src/jot/model/explorer.h` | editor | `editor/explorer.h` | ambiguous: workspace |
| `src/jot/model/fold_ranges.h` | editor | `editor/fold_ranges.h` | ambiguous: syntax |
| `src/jot/model/input.h` | editor | `editor/input.h` | ambiguous: input |
| `src/jot/model/menu.h` | editor | `editor/menu.h` | ambiguous: input (menus) vs editor |
| `src/jot/model/panels.h` | editor | `editor/panels.h` | ambiguous: holds outline + git-diff + debug panel state: split per module |
| `src/jot/model/panes.h` | editor | `editor/panes.h` | ambiguous: ui (tabs) vs editor (the pane tree) |
| `src/jot/model/popup.h` | editor | `editor/popup.h` | ambiguous: render (the popup surface) vs editor |
| `src/jot/model/quick_pick.h` | editor | `editor/quick_pick.h` | ambiguous: editor (the picker owner) vs render (its rows) |
| `src/jot/model/search.h` | editor | `editor/search.h` | ambiguous: text (the search model) vs render (the panel) |
| `src/jot/model/session.h` | editor | `editor/session.h` | ambiguous: workspace (session restore) vs editor |
| `src/jot/model/sidebar_sections.h` | editor | `editor/sidebar_sections.h` | ambiguous: workspace |
| `src/jot/model/syntax.h` | editor | `editor/syntax.h` | ambiguous: syntax module, but it is a model struct shared with render |
| `src/jot/model/tasks.h` | editor | `editor/tasks.h` | ambiguous: base (task model) vs workspace |
| `src/jot/model/theme.h` | editor | `editor/theme.h` | ambiguous: ui (the palette) or render? theme is read by both |
| `src/jot/state/bible_state.h` | extras | `extras/bible_state.h` | ambiguous: extras/bible; extras (hooks, core must not include it) |
| `src/jot/state/cpp_defs_state.h` | editor | `editor/cpp_defs_state.h` | ambiguous: syntax (the definition index) vs workspace |
| `src/jot/state/engine_state.h` | editor | `editor/engine_state.h` | ambiguous: editor (the Editor's own fields) |
| `src/jot/state/input_state.h` | editor | `editor/input_state.h` | ambiguous: input |
| `src/jot/state/lsp_state.h` | editor | `editor/lsp_state.h` | ambiguous: lsp |
| `src/jot/state/navigation_state.h` | editor | `editor/navigation_state.h` | ambiguous: editor (jumplist) vs text (marks) |
| `src/jot/state/pane_state.h` | editor | `editor/pane_state.h` | ambiguous: editor (pane tree) vs workspace |
| `src/jot/state/panel_state.h` | editor | `editor/panel_state.h` | ambiguous: render (panel visibility) vs editor |
| `src/jot/state/rest_state.h` | editor | `editor/rest_state.h` | ambiguous: editor (the REST client is editor wiring) |
| `src/jot/state/surface_state.h` | editor | `editor/surface_state.h` | ambiguous: editor (prompts/popups the Editor owns) |
| `src/jot/state/tabline_state.h` | editor | `editor/tabline_state.h` | ambiguous: ui (tab strip) vs editor |
| `src/jot/state/view_state.h` | editor | `editor/view_state.h` | ambiguous: render (viewport/scroll) vs editor |
| `src/jot/state/web_index_state.h` | editor | `editor/web_index_state.h` | ambiguous: workspace (the workspace-wide index) |
| `src/jot/state/winbar_state.h` | editor | `editor/winbar_state.h` | ambiguous: render |
| `src/jot/state/workspace_state.h` | editor | `editor/workspace_state.h` | ambiguous: workspace |
| `src/jot/surfaces/home.cpp` | editor | `editor/home.cpp` |  |
| `src/jot/surfaces/host_api.cpp` | editor | `editor/host_api.cpp` |  |
| `src/jot/surfaces/popup.cpp` | editor | `editor/popup.cpp` |  |
| `src/jot/surfaces/quick_pick.cpp` | editor | `editor/quick_pick.cpp` |  |
| `src/jot/surfaces/settings.cpp` | editor | `editor/settings.cpp` |  |
| `src/jot/surfaces/theme.cpp` | editor | `editor/theme.cpp` |  |
| `src/jot/task_queue.h` | base | `base/task_queue.h` |  |
| `src/jot/types.h` | base | `base/types.h` | ambiguous: umbrella re-export across modules; retire it instead of moving |
| `src/jot/workspace/file_tree.cpp` | workspace | `workspace/file_tree.cpp` |  |
| `src/jot/workspace/git.cpp` | git | `git/git.cpp` |  |
| `src/jot/workspace/git_client.cpp` | git | `git/git_client.cpp` |  |
| `src/jot/workspace/git_panel.cpp` | git | `git/git_panel.cpp` |  |
| `src/jot/workspace/git_panel_models.h` | git | `git/git_panel_models.h` |  |
| `src/jot/workspace/git_run.h` | git | `git/git_run.h` |  |
| `src/jot/workspace/right_panel.cpp` | workspace | `workspace/right_panel.cpp` |  |
| `src/jot/workspace/sidebar.cpp` | workspace | `workspace/sidebar.cpp` | basename collision: with src/edit/sidebar.cpp (both -> workspace) |
| `src/jot/workspace/tasks.cpp` | workspace | `workspace/tasks.cpp` |  |
| `src/jot/workspace/timeline.cpp` | workspace | `workspace/timeline.cpp` |  |
| `src/jot/workspace/timeline.h` | workspace | `workspace/timeline.h` |  |
| `src/jot/workspace/workspace.cpp` | workspace | `workspace/workspace.cpp` |  |
| `src/jot/workspace/workspace_internal.h` | workspace | `workspace/detail/session_fields.h` |  |
| `src/render/blank_guides.h` | render | `render/blank_guides.h` |  |
| `src/render/bracket.cpp` | render | `render/bracket.cpp` |  |
| `src/render/buffer.cpp` | render | `render/buffer.cpp` |  |
| `src/render/buffer_internal.h` | render | `render/detail/buffer_decorations.h` |  |
| `src/render/completion_popup.cpp` | render | `render/completion_popup.cpp` |  |
| `src/render/debugger.cpp` | render | `render/debugger.cpp` |  |
| `src/render/diagnostics.cpp` | render | `render/diagnostics.cpp` |  |
| `src/render/easter.cpp` | extras | `extras/easter.cpp` | extras (hooks, core must not include it) |
| `src/render/frame.cpp` | render | `render/frame.cpp` |  |
| `src/render/git_diff.cpp` | git | `git/git_diff.cpp` |  |
| `src/render/git_panel.cpp` | git | `git/git_panel_paint.cpp` |  |
| `src/render/gutter.h` | render | `render/gutter.h` |  |
| `src/render/image_viewer.cpp` | render | `render/image_viewer.cpp` |  |
| `src/render/menus.cpp` | render | `render/menus.cpp` |  |
| `src/render/minimap.cpp` | render | `render/minimap.cpp` |  |
| `src/render/modals.cpp` | render | `render/modals.cpp` |  |
| `src/render/outline.cpp` | render | `render/outline.cpp` |  |
| `src/render/overlay_internal.h` | render | `render/detail/popup_text.h` |  |
| `src/render/overlay_shared.cpp` | render | `render/overlay_shared.cpp` |  |
| `src/render/palette.cpp` | render | `render/palette.cpp` |  |
| `src/render/pane_edges.cpp` | render | `render/pane_edges.cpp` |  |
| `src/render/pane_edges.h` | render | `render/pane_edges.h` |  |
| `src/render/pane_tabs.cpp` | render | `render/pane_tabs.cpp` |  |
| `src/render/plugin_panel.cpp` | render | `render/plugin_panel.cpp` |  |
| `src/render/popup.cpp` | render | `render/popup.cpp` |  |
| `src/render/prompts.cpp` | render | `render/prompts.cpp` |  |
| `src/render/quick_pick.cpp` | render | `render/quick_pick.cpp` |  |
| `src/render/search.cpp` | render | `render/search.cpp` |  |
| `src/render/settings.cpp` | render | `render/settings.cpp` |  |
| `src/render/signature_popup.cpp` | render | `render/signature_popup.cpp` |  |
| `src/render/status_line.cpp` | render | `render/status_line.cpp` |  |
| `src/render/sync.cpp` | render | `render/sync.cpp` |  |
| `src/render/tabs.cpp` | render | `render/tabs.cpp` |  |
| `src/render/telescope_popup.cpp` | render | `render/telescope_popup.cpp` |  |
| `src/render/ui_internal.h` | render | `render/detail/buffer_text.h` |  |
| `src/render/winbar.cpp` | render | `render/winbar.cpp` |  |
| `src/tools/debugger/client.cpp` | dap | `dap/client.cpp` |  |
| `src/tools/debugger/client.h` | dap | `dap/client.h` |  |
| `src/tools/debugger/convert.cpp` | dap | `dap/convert.cpp` |  |
| `src/tools/debugger/internal.h` | dap | `dap/detail/wire_helpers.h` |  |
| `src/tools/debugger/json.cpp` | dap | `dap/json.cpp` |  |
| `src/tools/debugger/process.cpp` | dap | `dap/process.cpp` |  |
| `src/tools/discord_presence.cpp` | extras | `extras/discord_presence.cpp` | extras (hooks, core must not include it) |
| `src/tools/discord_presence.h` | extras | `extras/discord_presence.h` | extras (hooks, core must not include it) |
| `src/tools/discord_presence_data.h` | extras | `extras/discord_presence_data.h` | extras (hooks, core must not include it) |
| `src/tools/discord_rpc.cpp` | extras | `extras/discord_rpc.cpp` | extras (hooks, core must not include it) |
| `src/tools/discord_rpc.h` | extras | `extras/discord_rpc.h` | extras (hooks, core must not include it) |
| `src/tools/discord_rpc_transport.h` | extras | `extras/discord_rpc_transport.h` | extras (hooks, core must not include it) |
| `src/tools/discord_rpc_transport_unix.cpp` | extras | `extras/discord_rpc_transport_unix.cpp` | extras (hooks, core must not include it) |
| `src/tools/discord_rpc_win32.cpp` | base | `base/discord_rpc_win32.cpp` |  |
| `src/tools/external_open.h` | base | `base/external_open.h` |  |
| `src/tools/file_util.cpp` | base | `base/file_util.cpp` |  |
| `src/tools/file_util.h` | base | `base/file_util.h` |  |
| `src/tools/imageviewer.cpp` | render | `render/imageviewer.cpp` | ambiguous: render (the preview paints it) or base (decode only) |
| `src/tools/imageviewer.h` | render | `render/imageviewer.h` |  |
| `src/tools/leetcode_credentials.cpp` | extras | `extras/leetcode_credentials.cpp` | extras (hooks, core must not include it) |
| `src/tools/leetcode_credentials.h` | extras | `extras/leetcode_credentials.h` | extras (hooks, core must not include it) |
| `src/tools/lsp/client.cpp` | lsp | `lsp/client.cpp` |  |
| `src/tools/lsp/client.h` | lsp | `lsp/client.h` |  |
| `src/tools/lsp/install.cpp` | lsp | `lsp/install.cpp` |  |
| `src/tools/lsp/install.h` | lsp | `lsp/install.h` |  |
| `src/tools/lsp/internal.h` | lsp | `lsp/detail/protocol_helpers.h` |  |
| `src/tools/lsp/json.cpp` | lsp | `lsp/json.cpp` |  |
| `src/tools/lsp/messages.cpp` | lsp | `lsp/messages.cpp` |  |
| `src/tools/lsp/protocol.cpp` | lsp | `lsp/protocol.cpp` |  |
| `src/tools/lsp/requests.cpp` | lsp | `lsp/requests.cpp` |  |
| `src/tools/shell_util.h` | base | `base/shell_util.h` |  |
| `src/tools/string_util.h` | base | `base/string_util.h` |  |
| `src/tools/symbols/index.cpp` | syntax | `syntax/index.cpp` | ambiguous: syntax (it parses text) vs text |
| `src/tools/symbols/index.h` | syntax | `syntax/index.h` |  |
| `src/tools/telescope.cpp` | input | `input/telescope.cpp` | ambiguous: input (the picker model) or render (its popup) |
| `src/tools/telescope.h` | input | `input/telescope.h` |  |
| `src/tools/telescope_async.cpp` | input | `input/telescope_async.cpp` | ambiguous: input (the async scan) or base |
| `src/tools/terminal/integrated.cpp` | terminal | `terminal/integrated.cpp` |  |
| `src/tools/terminal/integrated.h` | terminal | `terminal/integrated.h` |  |
| `src/tools/terminal/terminal_session.cpp` | terminal | `terminal/terminal_session.cpp` |  |
| `src/tools/terminal/terminal_session.h` | terminal | `terminal/terminal_session.h` |  |
| `src/tools/terminal/win32_terminal_session.cpp` | terminal | `terminal/win32_terminal_session.cpp` |  |
| `src/tools/terminal/win32_terminal_session.h` | terminal | `terminal/win32_terminal_session.h` |  |
| `src/tools/url_span.h` | base | `base/url_span.h` |  |
| `src/tools/workspace/search.cpp` | workspace | `workspace/search.cpp` | ambiguous: workspace (project walk) vs text (the matcher) |
| `src/tools/workspace/search.h` | workspace | `workspace/search.h` |  |
| `src/ui/components.cpp` | ui | `ui/components.cpp` |  |
| `src/ui/components.h` | ui | `ui/components.h` |  |
| `src/ui/cursor_blink.cpp` | ui | `ui/cursor_blink.cpp` |  |
| `src/ui/cursor_blink.h` | ui | `ui/cursor_blink.h` |  |
| `src/ui/gui/font_catalog.cpp` | ui | `ui/font_catalog.cpp` |  |
| `src/ui/gui/font_catalog.h` | ui | `ui/font_catalog.h` |  |
| `src/ui/gui/gui.h` | ui | `ui/gui.h` |  |
| `src/ui/gui/gui_anim.cpp` | ui | `ui/gui_anim.cpp` |  |
| `src/ui/gui/gui_batch.cpp` | ui | `ui/gui_batch.cpp` |  |
| `src/ui/gui/gui_colors.cpp` | ui | `ui/gui_colors.cpp` |  |
| `src/ui/gui/gui_cursor.cpp` | ui | `ui/gui_cursor.cpp` |  |
| `src/ui/gui/gui_fit.cpp` | ui | `ui/gui_fit.cpp` |  |
| `src/ui/gui/gui_fit.h` | ui | `ui/gui_fit.h` |  |
| `src/ui/gui/gui_glyphs.cpp` | ui | `ui/gui_glyphs.cpp` |  |
| `src/ui/gui/gui_input.cpp` | ui | `ui/gui_input.cpp` |  |
| `src/ui/gui/gui_render.cpp` | ui | `ui/gui_render.cpp` |  |
| `src/ui/gui/gui_scroll_math.h` | ui | `ui/gui_scroll_math.h` |  |
| `src/ui/gui/gui_setup.cpp` | ui | `ui/gui_setup.cpp` |  |
| `src/ui/gui/gui_utf8.cpp` | ui | `ui/gui_utf8.cpp` |  |
| `src/ui/input_reader.cpp` | ui | `ui/input_reader.cpp` | ambiguous: platform (input reading) vs ui |
| `src/ui/input_reader.h` | ui | `ui/input_reader.h` | ambiguous: platform (input reading) vs ui |
| `src/ui/terminal.cpp` | ui | `ui/terminal.cpp` | ambiguous: ui (ANSI renderer) + platform (termios/raw mode) split; 1453 lines |
| `src/ui/terminal.h` | ui | `ui/terminal.h` |  |
| `src/ui/terminal_win32.cpp` | ui | `ui/terminal_win32.cpp` | ambiguous: ui (console renderer) + platform (console mode) split |
| `src/ui/text.cpp` | ui | `ui/text.cpp` |  |
| `src/ui/text.h` | ui | `ui/text.h` |  |
| `src/ui/ui.cpp` | ui | `ui/ui.cpp` |  |
| `src/ui/ui.h` | ui | `ui/ui.h` |  |
| `src/ui/xterm_palette.cpp` | ui | `ui/xterm_palette.cpp` |  |
| `src/ui/xterm_palette.h` | ui | `ui/xterm_palette.h` |  |

## 3. Renames and collisions

Per rule 7 a basename only has to be unique *within* its module, so the 34
duplicates that land in different modules (`search.cpp` in text, render and
workspace; `client.cpp` in lsp and dap) need nothing. These 13 do:

| module | basename | files |
| --- | --- | --- |
| editor | decorations.h | `src/jot/model/decorations.h`, `src/jot/app/decorations.h` |
| editor | explorer.h | `src/jot/model/explorer.h`, `src/jot/editor/api/explorer.h` |
| editor | panels.h | `src/jot/model/panels.h`, `src/jot/editor/api/panels.h` |
| editor | panes.h | `src/jot/model/panes.h`, `src/jot/editor/api/panes.h` |
| extras | bible.h | `src/features/bible.h`, `src/jot/editor/api/bible.h` |
| extras | coding_time.{h,cpp} | `src/features/`, `src/jot/app/`, `src/jot/editor/api/` |
| extras | emmet.cpp | `src/features/emmet.cpp`, `src/jot/app/emmet.cpp` |
| input | menu_bar.cpp | `src/input/menu_bar.cpp`, `src/input/mouse/menu_bar.cpp` |
| input | telescope.cpp | `src/tools/telescope.cpp`, `src/input/mouse/telescope.cpp` |
| lsp | install.cpp | `src/tools/lsp/install.cpp`, `src/jot/integrations/lsp/install.cpp` |
| render | winbar.cpp | `src/features/winbar.cpp`, `src/render/winbar.cpp` |
| workspace | sidebar.cpp | `src/jot/workspace/sidebar.cpp`, `src/edit/sidebar.cpp` |
| git | git_panel.cpp | `src/jot/workspace/git_panel.cpp`, `src/render/git_panel.cpp` |

The fragment headers are resolved by phase 5, not renamed: `bible.h`, `panes.h`,
`panels.h`, `explorer.h`, `coding_time.h` in `editor/api/` disappear when the
Editor stops being assembled from fragments.

## 4. Include-direction violations

244 of 1019 resolved include edges point from a lower module into a higher one.
Grouped by the pair they would create:

| from | to | edges | what it is |
| --- | --- | --- | --- |
| render | editor | 44 | painters call `Editor::` methods (section 1) |
| text | editor | 23 | `src/edit/*.cpp` are `Editor::` methods |
| input | editor | 22 | command/palette handlers are `Editor::` methods |
| render | lua | 22 | the view structs the Lua kit pushes live in `src/jot/lua/view/` |
| editor | lua | 18 | the Editor drives the Lua bridge |
| lsp | editor | 18 | the LSP wiring files are `Editor::` methods |
| text | lua | 16 | view structs again |
| base | editor | 14 | `src/jot/event/main_loop.cpp`, `gui_events.cpp`, `terminal_events.cpp` are editor loops, mapped to base by directory |
| workspace | editor | 12 | sidebar/session wiring |
| workspace | lua | 6 | view structs again |
| editor | extras | 6 | the core includes bible/discord/wakatime/emmet - phase 4 |
| text | syntax | 4 | `edit/cursor.cpp`, `edit/edit.cpp`, `edit/lines.cpp` include `folding.h` / `language.h` |
| text | ui | 4 | `ui/text.h` cell measurement used by `cursor.cpp`, `multicursor.cpp`, `column_utils.h` |
| base | lua | 4 | task/event modules include the Lua view headers |
| input | lua | 3 | palette handlers include the view headers |
| input | workspace | 3 | the picker reads explorer rows |
| base | ui | 3 | process/task include the cell grid |
| render | lsp | 3 | completion popup reads LSP state directly |
| base | input | 2 | task/event modules include `keybind_catalog.h` |
| ui | input | 2 | the tab strip reads input state |
| text | workspace | 1, syntax | ui | 1, syntax | lsp | 1, ui | editor | 1, input | extras | 1, lua | extras | 1, render | workspace | 1, render | extras | 1, base | extras | 1 | single edges |

Excluding the four causes that a later phase removes (Editor methods, the Lua
view structs, the `editor/api` fragments and the extras), the *real* new
violations are small and fixable: text -> syntax (4), text -> ui (4), base -> ui
(3), render -> lsp (3), base -> input (2), ui -> input (2), and 7 single edges.
The full list is in the appendix:

```
src/edit/commenting.h (text) -> src/jot/integrations/comment_style_data.h (editor)
src/edit/cursor.cpp (text) -> src/features/folding.h (syntax)
src/edit/cursor.cpp (text) -> src/ui/text.h (ui)
src/edit/edit.cpp (text) -> src/features/language.h (syntax)
src/edit/edit.cpp (text) -> src/features/html.h (workspace)
src/edit/edit.cpp (text) -> src/ui/text.h (ui)
src/edit/lines.cpp (text) -> src/features/language.h (syntax)
src/edit/multicursor.cpp (text) -> src/ui/text.h (ui)
src/edit/sidebar.cpp (workspace) -> src/jot/file_icons.h (editor)
src/features/color_terminal_codes.cpp (syntax) -> src/ui/xterm_palette.h (ui)
src/features/column_utils.h (text) -> src/ui/text.h (ui)
src/features/cpp_flags.cpp (syntax) -> src/tools/lsp/install.h (lsp)
src/features/tab_order.h (ui) -> src/jot/model/buffer.h (editor)
src/features/text_features.cpp (text) -> src/features/language.h (syntax)
src/features/winbar.cpp (render) -> src/jot/file_icons.h (editor)
src/features/winbar.cpp (render) -> src/jot/model/theme.h (editor)
src/features/winbar.h (render) -> src/jot/model/theme.h (editor)
src/input/commands/ex.cpp (input) -> src/jot/host_api.h (editor)
src/input/commands/ex.cpp (input) -> src/jot/workspace/git_run.h (git)
src/input/commands/ex_tail.cpp (input) -> src/jot/host_api.h (editor)
src/input/commands/ex_tail.cpp (input) -> src/jot/workspace/git_run.h (git)
src/input/palette/suggestions.cpp (input) -> src/features/http_file.h (workspace)
src/jot/event/event_loop.cpp (base) -> src/jot/keybind_catalog.h (input)
src/jot/event/gui_events.cpp (base) -> src/ui/gui/gui.h (ui)
src/jot/event/gui_events.cpp (base) -> src/ui/gui/gui_fit.h (ui)
src/jot/event/keymap_candidates.h (base) -> src/jot/keybind_catalog.h (input)
src/jot/event/main_loop.cpp (base) -> src/ui/cursor_blink.h (ui)
src/jot/integrations/lsp/common.h (lsp) -> src/jot/integrations/lsp_attach_data.h (editor)
src/jot/integrations/lsp/install.cpp (lsp) -> src/jot/app/process_job.h (editor)
src/jot/types.h (base) -> src/jot/model/buffer.h (editor)
src/jot/types.h (base) -> src/jot/model/decorations.h (editor)
src/jot/types.h (base) -> src/jot/model/explorer.h (editor)
src/jot/types.h (base) -> src/jot/model/fold_ranges.h (editor)
src/jot/types.h (base) -> src/jot/model/panels.h (editor)
src/jot/types.h (base) -> src/jot/model/panes.h (editor)
src/jot/types.h (base) -> src/jot/model/popup.h (editor)
src/jot/types.h (base) -> src/jot/model/syntax.h (editor)
src/jot/types.h (base) -> src/jot/model/theme.h (editor)
src/render/completion_popup.cpp (render) -> src/jot/integrations/lsp/matching.h (lsp)
src/render/frame.cpp (render) -> src/jot/file_icons.h (editor)
src/render/git_diff.cpp (git) -> src/jot/file_icons.h (editor)
src/render/git_panel.cpp (git) -> src/jot/file_icons.h (editor)
src/render/modals.cpp (render) -> src/tools/lsp/install.h (lsp)
src/render/pane_tabs.cpp (render) -> src/jot/file_icons.h (editor)
src/render/status_line.cpp (render) -> src/jot/file_icons.h (editor)
src/render/status_line.cpp (render) -> src/tools/lsp/install.h (lsp)
src/render/tabs.cpp (render) -> src/jot/file_icons.h (editor)
src/render/telescope_popup.cpp (render) -> src/jot/file_icons.h (editor)
src/render/ui_internal.h (render) -> src/jot/model/buffer.h (editor)
src/render/ui_internal.h (render) -> src/jot/model/theme.h (editor)
src/render/winbar.cpp (render) -> src/jot/file_icons.h (editor)
src/ui/terminal.cpp (ui) -> src/jot/keybind_catalog.h (input)
src/ui/terminal_win32.cpp (ui) -> src/jot/keybind_catalog.h (input)
```

## 5. Files over 800 lines (42)

| lines | file |
| --- | --- |
| 5176 | `src/features/color_tailwind.cpp` |
| 2006 | `src/features/cpp_definitions.cpp` |
| 1800 | `src/jot/integrations/integrated_terminal.cpp` |
| 1748 | `src/input/mouse/dispatcher.cpp` |
| 1735 | `src/render/buffer.cpp` |
| 1691 | `src/jot/surfaces/home.cpp` |
| 1453 | `src/ui/terminal.cpp` |
| 1424 | `src/ui/ui.cpp` |
| 1333 | `src/edit/sidebar.cpp` |
| 1321 | `src/ui/terminal_win32.cpp` |
| 1250 | `src/jot/workspace/sidebar.cpp` |
| 1234 | `src/jot/editor/api/test_hooks.h` |
| 1226 | `src/ui/gui/gui_render.cpp` |
| 1192 | `src/features/bible.cpp` |
| 1163 | `src/jot/integrations/lsp/completion.cpp` |
| 1160 | `src/tools/lsp/protocol.cpp` |
| 1128 | `src/input/commands/ex_tail.cpp` |
| 1122 | `src/features/emmet.cpp` |
| 1096 | `src/jot/surfaces/settings.cpp` |
| 1074 | `src/jot/surfaces/quick_pick.cpp` |
| 1063 | `src/jot/lua/api_ui_surfaces.cpp` |
| 1015 | `src/render/winbar.cpp` |
| 1002 | `src/tools/imageviewer.cpp` |
| 995 | `src/edit/edit.cpp` |
| 994 | `src/input/commands/ex.cpp` |
| 969 | `src/tools/telescope.cpp` |
| 961 | `src/jot/app/file.cpp` |
| 942 | `src/input/palette/suggestions.cpp` |
| 927 | `src/jot/lua/api_theme.cpp` |
| 918 | `src/jot/app/panes_layout.cpp` |
| 907 | `src/jot/workspace/git_panel.cpp` |
| 905 | `src/features/folding.cpp` |
| 899 | `src/jot/lua/api_buffer.cpp` |
| 890 | `src/features/save_hygiene.cpp` |
| 883 | `src/edit/multicursor.cpp` |
| 875 | `src/jot/app/editor.cpp` |
| 863 | `src/render/frame.cpp` |
| 847 | `src/tools/lsp/client.cpp` |
| 838 | `src/tools/lsp/requests.cpp` |
| 838 | `src/jot/lua/api_lsp_bridge.cpp` |
| 834 | `src/tools/debugger/client.cpp` |
| 807 | `src/features/color_codes.cpp` |

## 6. Headers that are not self-contained

168 headers were compiled as their own TU with the build's own include and
define set; 150 pass, 17 fail:

- 16 fragments, one per file in `src/jot/editor/api/` - by construction they are
  class-body text (`private:` at file scope), so they cannot be self-contained.
  Phase 5 removes them.
- `src/jot/lua/view/plugins.h` uses `FloatSpan`, which `src/jot/lua/view/shared.h`
  defines and `plugins.h` does not include. Independent of the refactor: it is a
  real missing include, fixed with one line whenever that file is touched.
- `src/ui/gui/font_catalog.h` *looks* like a failure but is not: its include path
  arrives as `-isystem /usr/include/freetype2`, which the flag extraction above
  only collected for `-I`. It compiles in the real build.

## 7. Credentials

| surface | where the secret lives | permissions today |
| --- | --- | --- |
| LeetCode session cookie | OS keyring: Windows Credential Manager (`jot.leetcode.session`), macOS Keychain (`jot.leetcode` / `session`), Linux Secret Service via `secret-tool` | owned by the keyring |
| WakaTime API key | plain text `wakatime_api_key` in `<config root>/configs/settings.conf` | whatever the umask gives a new file (typically 0644); `src/tools/file_util.cpp` copies the *existing* file's mode on replace, so nothing ever tightens it |
| WakaTime, legacy | `~/.wakatime.cfg` read-only (`cfg_path()`) | the user's own file |
| Discord | no user secret: only the application id in `src/tools/discord_presence.cpp` | n/a |

So the keyring work phase 4 asks for is already done for LeetCode; the open
question is the WakaTime key, which sits in the same settings file as every
other setting. Section 9 asks what to do about it.

## 8. Housekeeping inventory

| item | state |
| --- | --- |
| `.configs/configs/{colors,debug.json}` | tracked; one `configs/` level too many |
| `CMakeSettings.json` | tracked (MSVC config; superseded by `CMakePresets.json`) |
| `.vscode/settings.json` | tracked |
| `CMakePresets.json` | tracked |
| `apps/jot` | `main.cpp`, the `.rc`, and its own `CMakeLists.txt` |
| `src/jot/app` | 33 files: the `Editor` implementation (not the app) |
| `tools/` (top level) | 12 generator/tooling scripts, no C++ |
| `src/tools/` | 22 C++ files + 4 subdirs of clients and utilities |
| `src/jot/app/test_hooks.cpp` | build-flagged test-only source in the editor dir |
| `sources.cmake` | 7 files, one per directory, all included by `src/CMakeLists.txt` |
| `*_internal.h` | 12 files, all private helpers that rule 3 puts in `<module>/detail/` |
| generic basenames | `utils.cpp`/`utils.h` (2 dirs), `types.h`, `internal.h` (2), `common.h` (lsp) |
| `test/` | 230 entries in one directory: 88 C++ cases + ~140 probes |
| `docs/` | 8 documents, no `adr/` |
| `runtime/lua/` | 281 files in `features/`, `lsp/`, `luals/`, `treesitter/` - stays put |

## 9. Decisions needed before phase 1

1. **The Editor blocker (section 1).** Cut the leaf modules first and accept
   that `jot_text` etc. link the editor for a while, or pull the phase 5 facade
   forward and cut modules as their ownership moves? I recommend the second: a
   module that links the whole editor is a rename, not a boundary, and the
   include checker of phase 6 cannot go green until the facade exists.
2. **`src/jot/event`** splits three ways: `event_loop`/`task_queue`/line
   providers to base/text, `main_loop.cpp`/`gui_events.cpp`/`terminal_events.cpp`
   to editor, `which_key.cpp`/`keymap_candidates.h` to input. Confirm, since it
   is 14 of the 244 edges.
3. **Lua view structs** (`src/jot/lua/view/{surfaces,plugins,shared,runtime}.h`):
   22 + 16 + 6 + 3 edges come from render/text/input/workspace including them.
   Move them to the surfaces that own them (render/, ui/) and let the Lua bridge
   read them, or keep them in `lua/` and accept those edges? I recommend moving
   them: they are plain data, and `lua/` is above `render/` in the order.
4. **Shared models** (`src/jot/model/*`, `src/jot/state/*`): the table flags 20 of
   them. Splitting `model/panels.h` and `state/workspace_state.h` per feature is
   part of phases 3-5; do you want the split done per struct (many small files)
   or per module (one `types.h` per module, which rule 4 bans by name but not
   by shape)? I propose per struct, named by content.
5. **`.vscode/`** and **`CMakeSettings.json`**: phase 1 says ask. Delete both, or
   keep them?
6. **`apps/jot` vs `src/jot/app`**: rename `src/jot/app` to `src/jot/editor/`
   during phase 1 (the files are the Editor implementation) and leave the
   executable in `apps/jot`?
7. **`tools/` (scripts) vs `src/tools/` (C++)**: rename the top level to
   `scripts/`, or move the C++ utilities into their modules and let `tools/`
   keep the scripts? The target layout has no tools module, so I propose: C++
   utilities into `base/` and the modules, scripts stay in `tools/`.
8. **WakaTime key** (section 7): set the settings file to 0600 when it contains
   a key, move the key to the keyring, or leave it? I recommend the keyring, the
   way LeetCode already does it.
9. **800-line files**: 42 of them, headed by `color_tailwind.cpp` (5176 lines,
   generated) and `cpp_definitions.cpp` (2006). Do you want a split of the big
   hand-written ones in a phase of its own, or only warnings as rule 4 says?
