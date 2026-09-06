-- Type definitions for the `jot` global exposed by the embedded Lua runtime.
--
-- This file is NEVER executed by jot. It exists so lua-language-server can
-- offer completions, hover docs and type checks for the jot.* API in user
-- scripts (init.lua, config.lua, plugins). It mirrors docs/LUA_API.md and
-- the bindings in src/lua_bridge/; keep the three in sync.
--
-- The editor registers the directory containing this file as
-- `Lua.workspace.library` for the lua language server, which turns the
-- top-level `jot = {}` below into a known global and kills the
-- "Undefined global `jot`" diagnostics.

---@class jot.api.editor
---@field execute fun(command: string) Execute a native ex command (e.g. ":reload").
---@field request_redraw fun()
---@field info fun(): table

---@class jot.api.buffer
---@field get_text fun(): string
---@field set_text fun(text: string)
---@field get_selection fun(): table
---@field replace_selection fun(text: string)
---@field insert fun(text: string)
---@field cursor fun(): integer, integer Current (line, column), 1-based.
---@field set_cursor fun(line: integer, column: integer)
---@field list fun(): table[]
---@field current fun(): integer 1-based index of the current buffer.
---@field count fun(): integer
---@field text fun(index: integer): string
---@field lines fun(index: integer): string[]
---@field meta fun(index: integer): table
---@field selection fun(index: integer): table
---@field bookmarks fun(index: integer): table
---@field folds fun(index: integer): table
---@field tokens fun(index: integer): table
---@field select fun(start_line: integer, start_col: integer, end_line: integer, end_col: integer)
---@field clear_selection fun()
---@field switch fun(index: integer)
---@field filetype fun(index: integer): string
---@field get_line fun(line: integer): string
---@field set_var fun(key: string, value: any)
---@field get_var fun(key: string): any
---@field del_var fun(key: string)

---@class jot.api.cursor
---@field get fun(): integer, integer
---@field set fun(line: integer, column: integer)
---@field select_all fun()
---@field select_line fun()

---@class jot.api.clipboard
---@field copy fun()
---@field cut fun()
---@field paste fun()
---@field get fun(): string

---@class jot.api.motion
---@field word_next fun()
---@field word_prev fun()
---@field line_start fun()
---@field line_end fun()
---@field file_start fun()
---@field file_end fun()
---@field matching_bracket fun()
---@field select_function fun()

---@class jot.api.sidebar
---@field info fun(): table
---@field set_view fun(view: string)

---@class jot.api.picker
---@field active fun(): boolean
---@field info fun(): table
---@field items fun(): table[]
---@field accept fun()
---@field close fun()

---@class jot.api.events
---@field subscribe fun(event: string, fn: fun(...)): string
---@field unsubscribe fun(id: string)

---@class jot.api.timer
---@field set_timeout fun(ms: integer, fn: fun()) timer handle
---@field set_interval fun(ms: integer, fn: fun()) timer handle
---@field clear fun(id: any)

---@class jot.api.viewport
---@field info fun(): table
---@field line_at fun(x: integer, y: integer): integer
---@field scroll_top fun(lines: integer)
---@field scroll_lines fun(lines: integer)
---@field scroll_col fun(cols: integer)
---@field reveal fun(line: integer)

---@class jot.api.filetree
---@field root fun(): string
---@field tree fun(): table
---@field children fun(path: string): table

---@class jot.api.file
---@field current_file fun(): string?
---@field open fun(path: string)
---@field save fun()
---@field save_buffer fun(index: integer)
---@field close fun()
---@field new fun()
---@field open_workspace fun(path: string)
---@field recent fun(): table

---@class jot.api.pane
---@field layout fun(): table
---@field list fun(): table
---@field split_horizontal fun()
---@field split_vertical fun()
---@field focus_next fun()
---@field focus_previous fun()
---@field resize fun(width: integer, height: integer)

---@class jot.api.workspace
---@field open fun(path: string)
---@field path fun(): string
---@field recent fun(): table
---@field execute fun(command: string)

---@class jot.api.symbols
---@field list fun(buffer?: integer): table[]

---@class jot.api.config
---@field get fun(key: string, default?: any): any
---@field get_number fun(key: string, default?: number): number?
---@field get_bool fun(key: string, default?: boolean): boolean?
---@field set fun(key: string, value: string|number|boolean)
---@field unset fun(key: string)
---@field has fun(key: string): boolean
---@field keys fun(): string[]
---@field path fun(): string

---@class jot.api.theme
---@field list fun(): table[]
---@field apply fun(name: string)
---@field current fun(): string
---@field set_color fun(group: string, fg: integer, bg: integer)
---@field palette fun(): table

---@class jot.api.diagnostics
---@field get fun(): table

---@class jot.api.marks
---@field set fun(name: string, line?: integer)
---@field get fun(name: string): table
---@field jump fun(name: string)
---@field del fun(name: string)
---@field list fun(): table

---@class jot.api.status
---@field register fun(name: string, spec: table)
---@field unregister fun(name: string)

---@class jot.api.edit
---@field execute fun(command: string)
---@field undo fun()
---@field redo fun()
---@field insert_newline fun()
---@field delete fun()
---@field indent fun()
---@field outdent fun()
---@field comment fun()
---@field duplicate fun()
---@field move_up fun()
---@field move_down fun()
---@field join fun()
---@field uppercase fun()
---@field lowercase fun()
---@field replace fun()
---@field surround fun()
---@field increment fun()
---@field select_all fun()
---@field select_line fun()
---@field search fun()
---@field format fun()

---@class jot.api.search
---@field execute fun(command: string)
---@field info fun(): table
---@field matches fun(): table
---@field next fun()
---@field previous fun()

---@class jot.api.folds
---@field execute fun(command: string)
---@field toggle fun()
---@field fold fun()
---@field unfold fun()
---@field all fun()

---@class jot.api.bookmarks
---@field execute fun(command: string)
---@field toggle fun()
---@field next fun()
---@field previous fun()

---@class jot.api.terminal
---@field execute fun(command: string)
---@field toggle fun()
---@field new fun()
---@field list fun(): table
---@field spawn fun(command: string)
---@field close fun()
---@field activate fun()
---@field write fun(text: string)

---@class jot.api.tasks
---@field execute fun(command: string)
---@field show fun() ---@field list fun(): table
---@field run fun(name: string, cwd?: string)
---@field rerun fun()

---@class jot.api.lsp
---@field execute fun(command: string)
---@field clients fun(): table[]
---@field diagnostics fun(): table
---@field results fun(): table
---@field completions fun(): table
---@field request_hover fun()
---@field request_definition fun()
---@field request_symbols fun()
---@field request_completion fun()
---@field disabled fun(): string[]
---@field set_enabled fun(server: string, enabled: boolean)
---@field install fun(server: string)
---@field remove fun(server: string)
---@field restart_all fun()
---@field definition fun()
---@field back fun()
---@field completion fun()

---@class jot.api.debugger
---@field execute fun(command: string)
---@field configs fun(): table
---@field run_config fun(name: string)
---@field state fun(): table
---@field breakpoints fun(): table
---@field toggle_breakpoint fun()
---@field has_breakpoint fun(): boolean
---@field request_stack fun()
---@field request_variables fun()
---@field request_threads fun()
---@field continue fun()
---@field pause fun()
---@field step_in fun()
---@field step_over fun()
---@field step_out fun()
---@field stop fun()

---@class jot.api.git
---@field execute fun(command: string)
---@field info fun(): table?
---@field status fun(path?: string): table
---@field stage fun(path?: string)
---@field unstage fun(path?: string)
---@field stage_all fun()
---@field unstage_all fun()
---@field commit fun()
---@field refresh fun()
---@field diff fun(): table

---@class jot.api.treesitter
---@field execute fun(command: string)
---@field register_language fun(name: string, extension: string, ...)
---@field language_for_extension fun(extension: string): string?
---@field status fun(): table
---@field parser fun(language: string)
---@field parse fun(language: string, text: string)
---@field query fun(language: string)
---@field captures fun(language: string): table
---@field set_query fun(language: string, query: string)
---@field set_capture_color fun(capture: string, fg: integer, bg: integer)
---@field disable_language fun(language: string)
---@field install_command fun(language: string): string
---@field reload fun()

---@class jot.api.image
---@field execute fun(command: string)
---@field open fun(path: string)

---@class jot.api.job
---@field run fun(command: string, cwd?: string): table
---@field capture fun(command: string, cwd?: string): string, integer

---@class jot.api.ui
---@field picker fun(title: string, on_pick: fun(item: table))
---@field show_picker fun(title: string, on_pick: fun(item: table))
---@field panel fun(name: string, ...)
---@field show_panel fun(name: string, ...)
---@field register_panel fun(name: string, spec: table)
---@field buffer fun(...)
---@field handler fun(name: string, fn: fun(...))
---@field float fun(...)

---@class jot.api.keymap
---@field set fun(chords: string, fn: fun()|string, detail?: string)

---@class jot.api.decoration
---@field set fun(buffer: integer, decoration: table)
---@field delete fun(id: integer)
---@field clear fun(buffer: integer)
---@field list fun(buffer: integer): table[]

---@class jot.api.syntax
---@field highlight fun(...)

---@class jot.api
---@field editor jot.api.editor
---@field buffer jot.api.buffer
---@field cursor jot.api.cursor
---@field clipboard jot.api.clipboard
---@field motion jot.api.motion
---@field sidebar jot.api.sidebar
---@field picker jot.api.picker
---@field events jot.api.events
---@field timer jot.api.timer
---@field viewport jot.api.viewport
---@field filetree jot.api.filetree
---@field file jot.api.file
---@field pane jot.api.pane
---@field workspace jot.api.workspace
---@field symbols jot.api.symbols
---@field config jot.api.config
---@field theme jot.api.theme
---@field diagnostics jot.api.diagnostics
---@field marks jot.api.marks
---@field status jot.api.status
---@field edit jot.api.edit
---@field search jot.api.search
---@field folds jot.api.folds
---@field bookmarks jot.api.bookmarks
---@field terminal jot.api.terminal
---@field tasks jot.api.tasks
---@field lsp jot.api.lsp
---@field debugger jot.api.debugger
---@field git jot.api.git
---@field treesitter jot.api.treesitter
---@field image jot.api.image
---@field job jot.api.job
---@field ui jot.api.ui
---@field keymap jot.api.keymap
---@field decoration jot.api.decoration
---@field syntax jot.api.syntax
---@field execute fun(command: string) Execute a native ex command (":reload", ...).
---@field open_file fun(path: string) Open a file in a new buffer.
---@field save fun() Save the current buffer.
---@field notify fun(message: string) Show a status message.
---@field notify_transient fun(message: string) Show a transient status message.
---@field command fun(name: string, fn: fun()) Register a command.
---@field autocmd fun(event: string, fn: fun(...)) Register an event handler.
---@field register_keymap fun(chords: string, fn: fun()|string, detail?: string)

---@type jot.api
jot = {}

-- The `vim` alias mirrors `vim=jot` set by the runtime.
---@type jot.api
vim = {}