-- Snippet session: the live expansion.
--
-- Owns everything between "we inserted the snippet text" and "the user jumped
-- out of the last tabstop": tabstop ranges, the jump order, choice switching,
-- mirroring (typing in one occurrence of `$1` updates every other one),
-- repeats, restores, placeholder highlighting and the region bookkeeping that
-- keeps all of the above correct while the buffer is edited underneath.
--
-- Positions are flat byte offsets over the document (see doc.lua), so an edit
-- that inserts or removes newlines shifts everything by construction instead
-- of needing per-line fixups.
local doc = require("jot_snip.doc")
local nodes = require("jot_snip.nodes")
local env = require("jot_snip.env")

local M = {}

local state = {
  active = false,
  nodes = nil,
  snippet = nil,
  filetype = nil,
  ctx = nil,
  lines = {},
  starts = {},
  region_start = 0,
  region_end = 0,
  jumps = {},          -- ordered list of occurrence entries
  occurrences = {},    -- every occurrence, document order
  mirrors = {},        -- [index][copy] -> { occurrence, ... }
  values = {},         -- [index] -> current text
  cursor = 0,          -- flat offset of the caret while jumping
  pos = 0,             -- index into `jumps` (0 = not inside a stop)
  suppress = 0,        -- >0 while the engine itself writes to the buffer
  self_edit = nil,     -- signature of the last engine write (see apply)
  marks = {},          -- decoration ids
  last_window = nil,   -- last jumped-from/into entry (rep/restore bookkeeping)
  repeats = {},        -- repeat node -> { copy = n }
  restores = {},       -- occurrence -> stored text
  history = {},
}

-- Session lifecycle listeners (keymaps.lua binds the choice keys only while
-- a session is live, and init.lua keeps the status segment honest).
local listeners = { start = {}, exit = {} }

local function notify(kind)
  for _, fn in ipairs(listeners[kind]) do
    pcall(fn)
  end
end

function M.on_start(fn)
  listeners.start[#listeners.start + 1] = fn
end

function M.on_exit(fn)
  listeners.exit[#listeners.exit + 1] = fn
end

local TAB_BG, TAB_ACTIVE_BG, TAB_FG = nil, nil, nil

local function theme_colors()
  if TAB_BG ~= nil then
    return
  end
  TAB_BG, TAB_ACTIVE_BG, TAB_FG = 236, 24, 7
  local ok, palette = pcall(jot.theme.palette)
  if ok and type(palette) == "table" then
    local selection = palette.selection or {}
    local cursor_line = palette.cursor_line or {}
    local search = palette.search_match or {}
    TAB_ACTIVE_BG = selection.bg or cursor_line.bg or search.bg or TAB_ACTIVE_BG
    TAB_FG = selection.fg or palette.default and palette.default.fg or TAB_FG
    TAB_BG = search.bg or cursor_line.bg or TAB_BG
  end
end

-- ---------------------------------------------------------------------------
-- Buffer helpers
-- ---------------------------------------------------------------------------

local function refresh()
  local lines = jot.buffer.lines()
  if not lines then
    local meta = jot.buffer.meta() or {}
    lines = {}
    for i = 1, math.max(1, meta.line_count or 1) do
      lines[i] = jot.buffer.get_line(i) or ""
    end
  end
  if #lines == 0 then
    lines = { "" }
  end
  state.lines = lines
  state.starts = doc.line_starts(lines)
end

local function to_pos(offset)
  return doc.position(state.starts, state.lines, offset)
end

local function region_text()
  return doc.text_between(state.starts, state.lines, state.region_start, state.region_end)
end

-- How many bytes the region text was before the write (so the next BufChange
-- can tell an engine write from a user edit: it echoes back the region we
-- replaced, at exactly the offset we replaced it at).
local function apply(from, to, text)
  local a = to_pos(from)
  local b = to_pos(to)
  state.suppress = state.suppress + 1
  state.self_edit = { from = from, text = text or "" }
  local ok = pcall(jot.buffer.apply_edit, a.line, a.col, b.line, b.col, text or "")
  state.suppress = state.suppress - 1
  if not ok then
    return false
  end
  refresh()
  return true
end

local function place_cursor(offset)
  local p = to_pos(offset)
  jot.cursor.set(p.line, p.col)
end

local function select_range(from, to)
  local a = to_pos(from)
  local b = to_pos(to)
  jot.buffer.select(a.line, a.col, b.line, b.col)
end

-- ---------------------------------------------------------------------------
-- Highlighting
-- ---------------------------------------------------------------------------

local function clear_marks()
  for _, id in ipairs(state.marks) do
    pcall(jot.decoration.delete, id)
  end
  state.marks = {}
end

local function paint_marks()
  clear_marks()
  local highlight = jot.config.get_bool("snippet_highlight", true)
  if not highlight or #state.occurrences == 0 then
    return
  end
  theme_colors()
  local active = state.jumps[state.pos]
  for _, occ in ipairs(state.occurrences) do
    local a = to_pos(occ.start)
    local b = to_pos(occ.stop)
    local width = 0
    if b.line == a.line then
      width = b.col - a.col
    else
      width = #state.lines[a.line] - (a.col - 1) + 1
    end
    local is_active = occ == active
    local ok, id = pcall(jot.decoration.set, {
      row = a.line,
      col = a.col,
      width = math.max(0, width),
      bg = is_active and TAB_ACTIVE_BG or TAB_BG,
      priority = is_active and 60 or 40,
    })
    if ok and type(id) == "number" and id ~= 0 then
      state.marks[#state.marks + 1] = id
    end
  end
end

-- ---------------------------------------------------------------------------
-- Rendering
-- ---------------------------------------------------------------------------

-- Resolves a node's text, walking node lists into the same output buffer so
-- nested tabstops land in document order. `out` is { text = "", stops = {} }.
local function render_nodes(list, out, copy, ctx)
  local function emit(text)
    out.text = out.text .. (text or "")
  end

  local function value_of(node)
    local idx = node.pos
    if idx == nil then
      idx = node.id
    end
    if state.values[idx] ~= nil then
      return state.values[idx]
    end
    local text = ""
    if node.default ~= nil then
      if type(node.default) == "string" then
        text = node.default
      elseif nodes.is_node(node.default) then
        local sub = { text = "", stops = {} }
        render_nodes({ node.default }, sub, copy, ctx)
        text = sub.text
      else
        local sub = { text = "", stops = {} }
        render_nodes(node.default, sub, copy, ctx)
        text = sub.text
      end
    end
    if node.indent_snippet and node.indent_text then
      text = (text:gsub("\n", "\n" .. node.indent_text))
    end
    state.values[idx] = text
    return text
  end

  -- Function/dynamic nodes resolve their argnodes to the text they currently
  -- hold, which is what makes `${1/(.*)/\1/}` transforms update live.
  local function resolve_args(node)
    local args = {}
    for i, arg in ipairs(node.argnodes or {}) do
      if nodes.is_node(arg) then
        local idx = arg.pos or arg.id
        if state.values[idx] ~= nil then
          args[i] = state.values[idx]
        else
          local sub = { text = "", stops = {} }
          render_nodes({ arg }, sub, copy, ctx)
          args[i] = sub.text
        end
      else
        args[i] = tostring(arg)
      end
    end
    return args
  end

  for _, node in ipairs(list) do
    if node.type == "text" then
      emit(node.text)
    elseif node.type == "insert" then
      local start = #out.text
      local text = value_of(node)
      emit(text)
      out.stops[#out.stops + 1] =
          { node = node, index = node.pos or node.id, copy = copy, start = start, stop = #out.text }
    elseif node.type == "choice" then
      local start = #out.text
      local item = node.items[node.index or 1] or { nodes = {} }
      local inner = { text = "", stops = {} }
      render_nodes(item.nodes, inner, copy, ctx)
      emit(inner.text)
      for _, stop in ipairs(inner.stops) do
        stop.start = stop.start + start
        stop.stop = stop.stop + start
        out.stops[#out.stops + 1] = stop
      end
      out.stops[#out.stops + 1] = {
        node = node,
        index = node.pos or node.id,
        copy = copy,
        start = start,
        stop = #out.text,
        choice = true,
      }
    elseif node.type == "variable" then
      local value = env.get(node.name, ctx)
      if (value == nil or value == "") and node.default ~= nil then
        local sub = { text = "", stops = {} }
        render_nodes(node.default, sub, copy, ctx)
        value = sub.text
        for _, stop in ipairs(sub.stops) do
          out.stops[#out.stops + 1] = stop
        end
      end
      emit(value or "")
    elseif node.type == "function" then
      local ok, value = pcall(node.fn, resolve_args(node), nil, node.user_args)
      emit(ok and tostring(value or "") or "")
    elseif node.type == "dynamic" then
      local ok, value = pcall(node.fn, resolve_args(node), node.state, node.user_args)
      if ok and type(value) == "table" and not nodes.is_node(value) then
        render_nodes(value, out, copy, ctx)
      elseif ok and nodes.is_node(value) then
        render_nodes({ value }, out, copy, ctx)
      elseif ok then
        emit(tostring(value or ""))
      end
    elseif node.type == "nested" or node.type == "snippet" or node.type == "multi" then
      render_nodes(node.nodes or {}, out, copy, ctx)
    elseif node.type == "restore" then
      local start = #out.text
      local text = node.stored
      if text == nil then
        local sub = { text = "", stops = {} }
        render_nodes(node.nodes or {}, sub, copy, ctx)
        text = sub.text
        for _, stop in ipairs(sub.stops) do
          out.stops[#out.stops + 1] = stop
        end
      end
      emit(text)
      node.stored = text
      out.restores = out.restores or {}
      out.restores[#out.restores + 1] = {
        node = node,
        start = start,
        stop = #out.text,
      }
    elseif node.type == "repeat" then
      local sub = { text = "", stops = {} }
      render_nodes(node.nodes or {}, sub, copy, ctx)
      local start = #out.text
      emit(sub.text)
      for _, stop in ipairs(sub.stops) do
        stop.start = stop.start + start
        stop.stop = stop.stop + start
        out.stops[#out.stops + 1] = stop
      end
      out.repeats = out.repeats or {}
      out.repeats[#out.repeats + 1] = {
        node = node,
        start = start,
        stop = #out.text,
        copy = copy,
      }
    elseif node.type == "format" then
      local parts = {}
      for _, part in ipairs(node.parts or {}) do
        if type(part) == "table" and part.type == "text" then
          parts[#parts + 1] = part.text
        else
          local sub = { text = "", stops = {} }
          render_nodes({ part }, sub, copy, ctx)
          parts[#parts + 1] = sub.text
        end
      end
      emit(table.concat(parts))
    end
  end
end

-- Sorts jump entries: numeric tabstops ascending, then keyed ones in
-- first-appearance order, then `$0` (the exit) last.
local function build_jumps(stops)
  local seen = {}
  local jumps = {}
  local mirrors = {}
  local occurrences = {}

  for _, stop in ipairs(stops) do
    local occ = {
      node = stop.node,
      index = stop.index,
      copy = stop.copy,
      start = stop.start,
      stop = stop.stop,
      choice = stop.choice,
    }
    occurrences[#occurrences + 1] = occ
    local key = tostring(stop.index) .. "#" .. tostring(stop.copy or 1)
    if seen[key] then
      local list = mirrors[stop.index]
      mirrors[stop.index] = list
      table.insert(list, occ)
    else
      seen[key] = occ
      mirrors[stop.index] = mirrors[stop.index] or {}
      mirrors[stop.index][#mirrors[stop.index] + 1] = occ
      jumps[#jumps + 1] = occ
    end
  end

  local numeric, keyed, exit = {}, {}, {}
  for _, occ in ipairs(jumps) do
    local index = occ.index
    if type(index) == "number" then
      if index == 0 then
        exit[#exit + 1] = occ
      else
        numeric[#numeric + 1] = occ
      end
    else
      keyed[#keyed + 1] = occ
    end
  end
  table.sort(numeric, function(a, b)
    return a.index < b.index
  end)

  local ordered = {}
  for _, occ in ipairs(numeric) do
    ordered[#ordered + 1] = occ
  end
  for _, occ in ipairs(keyed) do
    ordered[#ordered + 1] = occ
  end
  for _, occ in ipairs(exit) do
    ordered[#ordered + 1] = occ
  end

  -- Keep the FIRST occurrence of each index as the jump target, now ordered.
  local first = {}
  for _, occ in ipairs(occurrences) do
    local key = tostring(occ.index) .. "#" .. tostring(occ.copy or 1)
    first[key] = first[key] or occ
  end
  local final = {}
  for _, occ in ipairs(ordered) do
    local key = tostring(occ.index) .. "#" .. tostring(occ.copy or 1)
    final[#final + 1] = first[key]
  end
  return final, mirrors, occurrences
end

-- ---------------------------------------------------------------------------
-- Session lifecycle
-- ---------------------------------------------------------------------------

function M.active()
  return state.active
end

function M.values()
  return state.values
end

-- Starts a session over `body` (a node list). `opts` carries the snippet
-- definition, the filetype, the environment context and the optional range
-- the snippet replaces (defaults to the caret).
function M.start(body, opts)
  opts = opts or {}
  refresh()
  local ctx = opts.ctx or {}
  state.active = true
  state.nodes = body
  state.snippet = opts.snippet
  state.filetype = opts.filetype
  state.ctx = ctx
  state.values = {}
  state.suppress = 0
  state.self_edit = nil
  state.repeats = {}
  state.restores = {}
  state.pos = 0
  state.last_window = nil

  local rendered = { text = "", stops = {} }
  render_nodes(body, rendered, 1, ctx)

  local start_offset
  if opts.start_line then
    start_offset = doc.offset(state.starts, state.lines, opts.start_line, opts.start_col or 1)
  else
    start_offset = doc.offset(state.starts, state.lines, jot.cursor.get())
  end
  local end_offset
  if opts.end_line then
    end_offset = doc.offset(state.starts, state.lines, opts.end_line, opts.end_col or 1)
  else
    end_offset = start_offset
  end

  if not apply(start_offset, end_offset, rendered.text) then
    state.active = false
    return false
  end

  state.region_start = start_offset
  state.region_end = start_offset + #rendered.text

  local jumps, mirrors, occurrences = build_jumps(rendered.stops)
  local delta = start_offset - 0
  for _, occ in ipairs(occurrences) do
    occ.start = occ.start + delta
    occ.stop = occ.stop + delta
  end
  state.jumps = jumps
  state.mirrors = {}
  for index, list in pairs(mirrors) do
    state.mirrors[index] = {}
    for i, occ in ipairs(list) do
      state.mirrors[index][i] = occ
    end
  end
  state.occurrences = occurrences
  state.repeats = {}
  for _, rep in ipairs(rendered.repeats or {}) do
    state.repeats[#state.repeats + 1] = {
      node = rep.node,
      start = rep.start + delta,
      stop = rep.stop + delta,
      copy = rep.copy,
    }
    rep.node.expanded = 1
  end

  if #state.jumps == 0 then
    return M.exit(false)
  end
  local started = M.jump(1)
  if started then
    notify("start")
  end
  return started
end

-- The first occurrence of a tabstop index.
function M.locate(index)
  for _, occ in ipairs(state.occurrences) do
    if occ.index == index then
      return occ
    end
  end
  return nil
end

function M.jumps()
  return state.jumps
end

function M.occurrences()
  return state.occurrences
end

-- The mirrored occurrences of an entry (itself included, first).
function M.mirror_list(occ)
  local list = {}
  for _, other in ipairs(state.occurrences) do
    if other.index == occ.index and (other.copy or 1) == (occ.copy or 1) then
      list[#list + 1] = other
    end
  end
  return list
end

local function set_active(entry)
  state.pos = 0
  for i, candidate in ipairs(state.jumps) do
    if candidate == entry then
      state.pos = i
      break
    end
  end
  -- Select the entry in the buffer and put the caret at its end, mirroring
  -- the VSCode/LuaSnip placeholder selection.
  if entry then
    select_range(entry.start, entry.stop)
    place_cursor(entry.stop)
    state.cursor = entry.stop
  end
  paint_marks()
  jot.editor.request_redraw()
end

function M.current()
  return state.jumps[state.pos]
end

function M.current_index()
  local entry = M.current()
  return entry and entry.index or nil
end

function M.jumpable(dir)
  if not state.active then
    return false
  end
  dir = dir or 1
  local target = state.pos + dir
  if target < 1 then
    return false
  end
  if target > #state.jumps then
    return false
  end
  return true
end

function M.locally_jumpable(dir)
  return M.jumpable(dir)
end

-- Moves `dir` entries; dir may be an absolute 1-based index (LuaSnip allows
-- jump(n, true) for that).
function M.jump(dir, absolute)
  if not state.active then
    return false
  end
  dir = dir or 1
  local target
  if absolute then
    target = dir
  else
    target = state.pos + dir
    if state.pos == 0 then
      target = dir > 0 and 1 or 1
    end
  end
  if target < 1 then
    return false
  end
  if target > #state.jumps then
    -- Past the last stop: hand repeated copies over before leaving.
    if M.expand_repeat() then
      return true
    end
    return M.exit(true)
  end
  local previous = state.jumps[state.pos]
  local entry = state.jumps[target]
  M.store_restore(previous)
  state.last_window = { from = previous, to = entry }
  set_active(entry)
  return true
end

function M.jump_destination(dir)
  dir = dir or 1
  local target = state.pos + dir
  if target < 1 or target > #state.jumps then
    return nil
  end
  return state.jumps[target]
end

function M.exit(move_cursor)
  if not state.active then
    return false
  end
  local end_offset = state.region_end
  state.active = false
  state.self_edit = nil
  state.nodes = nil
  state.jumps = {}
  state.occurrences = {}
  state.mirrors = {}
  state.values = {}
  state.pos = 0
  clear_marks()
  pcall(jot.buffer.clear_selection)
  if move_cursor then
    place_cursor(math.min(end_offset, doc.total(state.starts, state.lines)))
  end
  jot.editor.request_redraw()
  notify("exit")
  return true
end

-- ---------------------------------------------------------------------------
-- Choices
-- ---------------------------------------------------------------------------

function M.choice_active()
  local entry = M.current()
  return entry ~= nil and entry.choice == true
end

local function current_choice_entry()
  local entry = M.current()
  if entry and entry.choice then
    return entry
  end
  return nil
end

function M.change_choice(dir)
  local entry = current_choice_entry()
  if not entry then
    -- Switch every choice node inside the session at once (LuaSnip's
    -- `<C-E>` outside a choice stop cycles the first choice).
    local changed = false
    for _, occ in ipairs(state.occurrences) do
      if occ.choice and occ.node.items and #occ.node.items > 1 then
        occ.node.index = ((occ.node.index or 1) % #occ.node.items) + 1
        changed = true
        break
      end
    end
    if changed then
      return M.rerender()
    end
    return false
  end
  local item_count = #(entry.node.items or {})
  if item_count == 0 then
    return false
  end
  dir = dir or 1
  local index = ((entry.node.index or 1) - 1 + dir) % item_count + 1
  entry.node.index = index
  return M.rerender()
end

-- Rebuilds the session text from scratch (used after a choice switch): the
-- region is re-rendered with the current node state, which keeps transforms
-- and nested stops consistent.
function M.rerender(keep_cursor)
  if not state.active then
    return false
  end
  local start = state.region_start
  local text = region_text()
  local caret = state.cursor and (state.cursor - start) or #text
  if keep_cursor then
    caret = math.max(0, math.min(caret, #text))
  else
    caret = nil
  end

  -- Preserve the per-tabstop text the user has typed, then drop the mirrors so
  -- the re-render starts from the node defaults/current choice.
  state.suppress = state.suppress + 1
  local rendered = { text = "", stops = {} }
  render_nodes(state.nodes, rendered, 1, state.ctx)
  state.suppress = state.suppress - 1

  if not apply(state.region_start, state.region_end, rendered.text) then
    return false
  end
  local delta = state.region_start
  for _, occ in ipairs(rendered.stops) do
    occ.start = occ.start + delta
    occ.stop = occ.stop + delta
  end
  local jumps, mirrors, occurrences = build_jumps(rendered.stops)
  state.jumps = jumps
  state.mirrors = mirrors
  state.occurrences = occurrences
  state.region_end = state.region_start + #rendered.text

  if caret then
    state.cursor = state.region_start + caret
    place_cursor(state.cursor)
  end
  local entry = M.current()
  if entry then
    select_range(entry.start, entry.stop)
    place_cursor(entry.stop)
  end
  paint_marks()
  jot.editor.request_redraw()
  return true
end

-- ---------------------------------------------------------------------------
-- Repeats and restores
-- ---------------------------------------------------------------------------

-- Appends the next `rep` copy when the last stop of the current copy is left.
function M.expand_repeat()
  for _, rep in ipairs(state.repeats) do
    local node = rep.node
    local expanded = node.expanded or 1
    if expanded < (node.times or 1) then
      -- Only when leaving the last stop that belongs to this repeat block.
      local last = 0
      for _, occ in ipairs(state.occurrences) do
        if occ.start >= rep.start and occ.stop <= rep.stop and occ.copy == rep.copy then
          last = math.max(last, occ.stop)
        end
      end
      if last == 0 or rep.stop ~= last then
        -- The block grew; recompute its end from its stops.
      end
      local out = { text = "", stops = {} }
      render_nodes(node.nodes or {}, out, expanded + 1, state.ctx)
      local at = rep.stop
      if not apply(at, at, out.text) then
        return false
      end
      local delta = at
      for _, stop in ipairs(out.stops) do
        stop.start = stop.start + delta
        stop.stop = stop.stop + delta
      end
      local jumps, mirrors, occurrences = build_jumps(out.stops)
      local first = jumps[1]
      for _, occ in ipairs(occurrences) do
        state.occurrences[#state.occurrences + 1] = occ
      end
      for index, list in pairs(mirrors) do
        state.mirrors[index] = state.mirrors[index] or {}
        for _, occ in ipairs(list) do
          state.mirrors[index][#state.mirrors[index] + 1] = occ
        end
      end
      for _, jump in ipairs(jumps) do
        state.jumps[#state.jumps + 1] = jump
      end
      state.region_end = state.region_end + #out.text
      rep.stop = rep.stop + #out.text
      rep.copy = expanded + 1
      node.expanded = expanded + 1
      if first then
        set_active(first)
      end
      return true
    end
  end
  return false
end

-- Remembers a restore node's text when leaving it, and writes it back when
-- jumping into it again (LuaSnip's `restore` behavior).
function M.store_restore(entry)
  if not entry then
    return
  end
  for _, restore in ipairs(state.restores) do
    if entry.start >= restore.start and entry.stop <= restore.stop then
      restore.node.stored = doc.text_between(state.starts, state.lines, restore.start, restore.stop)
      return
    end
  end
end

function M.restore_all()
  if #state.restores == 0 then
    return
  end
  for _, restore in ipairs(state.restores) do
    if restore.node.stored then
      apply(restore.start, restore.stop, restore.node.stored)
      restore.stop = restore.start + #restore.node.stored
    end
  end
end

-- ---------------------------------------------------------------------------
-- Editing inside the session
-- ---------------------------------------------------------------------------

local function shift(value, from, to, inserted)
  return doc.shift(value, from, to, inserted)
end

-- Consumes the pending engine-write signature; true when `event` is the echo
-- of our own apply() rather than a user edit.
local function is_self_edit(event, e_start, inserted)
  local signature = state.self_edit
  state.self_edit = nil
  if not signature then
    return false
  end
  return e_start == signature.from and inserted == signature.text
end

-- Handles one BufChange. Returns true when it was consumed.
function M.on_change(event)
  if not state.active or state.suppress > 0 or not event then
    return false
  end
  if event.edit_start_line == nil then
    return false
  end
  refresh()
  local e_start = doc.offset(state.starts, state.lines, event.edit_start_line, event.edit_start_col)
  local e_end = doc.offset(state.starts, state.lines, event.edit_end_line or event.edit_start_line,
                            event.edit_end_col or event.edit_start_col)
  local inserted = event.edit_inserted or ""
  if is_self_edit(event, e_start, inserted) then
    return true
  end
  local removed = event.edit_removed or ""
  local delta = #inserted - #removed

  -- An edit before the region just moves the whole session.
  if e_end <= state.region_start then
    state.region_start = state.region_start + delta
    state.region_end = state.region_end + delta
    for _, occ in ipairs(state.occurrences) do
      occ.start = occ.start + delta
      occ.stop = occ.stop + delta
    end
    for _, rep in ipairs(state.repeats) do
      rep.start = rep.start + delta
      rep.stop = rep.stop + delta
    end
    for _, restore in ipairs(state.restores) do
      restore.start = restore.start + delta
      restore.stop = restore.stop + delta
    end
    state.cursor = state.cursor and (state.cursor + delta)
    return false
  end

  -- An edit that crosses the session start (or starts inside but ends after
  -- the end) invalidates the bookkeeping: drop the session rather than drift.
  if e_start < state.region_start or e_start > state.region_end then
    M.exit(false)
    return false
  end

  -- Region text as it now stands (the edit is already applied in the buffer).
  local new_region_end = state.region_end + delta
  local text = doc.text_between(state.starts, state.lines, state.region_start, new_region_end)

  -- Which occurrence owns the edit?
  local target = nil
  local active = state.jumps[state.pos]
  local function contains(occ, offset)
    return offset >= occ.start and offset <= occ.stop
  end
  if active and contains(active, e_start) then
    target = active
  else
    for _, occ in ipairs(state.occurrences) do
      if contains(occ, e_start) then
        target = occ
        break
      end
    end
  end

  if not target then
    -- Typed between stops: shift what follows and keep going.
    for _, occ in ipairs(state.occurrences) do
      occ.start = shift(occ.start, e_start, e_end, #inserted)
      occ.stop = shift(occ.stop, e_start, e_end, #inserted)
    end
    for _, rep in ipairs(state.repeats) do
      rep.start = shift(rep.start, e_start, e_end, #inserted)
      rep.stop = shift(rep.stop, e_start, e_end, #inserted)
    end
    for _, restore in ipairs(state.restores) do
      restore.start = shift(restore.start, e_start, e_end, #inserted)
      restore.stop = shift(restore.stop, e_start, e_end, #inserted)
    end
    state.region_end = new_region_end
    state.cursor = shift(state.cursor or e_start, e_start, e_end, #inserted)
    paint_marks()
    return false
  end

  -- Splice the same change into every occurrence of the tabstop.
  local rel_from = math.max(0, e_start - target.start)
  local rel_to = math.max(rel_from, e_end - target.start)
  local splices = {}
  for _, occ in ipairs(state.occurrences) do
    if occ.index == target.index and (occ.copy or 1) == (target.copy or 1) then
      local from = occ.start - state.region_start
      local to = occ.stop - state.region_start
      local sub_from = from + math.min(rel_from, to - from)
      local sub_to = from + math.min(rel_to, to - from)
      splices[#splices + 1] = { from = sub_from, to = sub_to, text = inserted, occ = occ }
      occ.text_start = sub_from
      occ.text_stop = sub_to
    end
  end
  table.sort(splices, function(a, b)
    return a.from > b.from
  end)

  local new_text = text
  local caret = nil
  for _, splice in ipairs(splices) do
    if splice.from < 0 or splice.to > #new_text or splice.from > splice.to then
      M.exit(false)
      return false
    end
    new_text = new_text:sub(1, splice.from) .. splice.text .. new_text:sub(splice.to + 1)
    local shift_delta = #splice.text - (splice.to - splice.from)
    if shift_delta ~= 0 then
      for _, occ in ipairs(state.occurrences) do
        local a = occ.start - state.region_start
        local b = occ.stop - state.region_start
        if a >= splice.to then
          a = a + shift_delta
        elseif a > splice.from then
          a = splice.from + #splice.text
        end
        if b >= splice.to then
          b = b + shift_delta
        elseif b > splice.from then
          b = splice.from + #splice.text
        end
        occ.start = a + state.region_start
        occ.stop = b + state.region_start
      end
      for _, rep in ipairs(state.repeats) do
        rep.stop = rep.stop + shift_delta
      end
      for _, restore in ipairs(state.restores) do
        restore.stop = restore.stop + shift_delta
      end
      if caret then
        caret = caret + shift_delta
      end
    end
    -- Keep the edited occurrence's caret where the user typed.
    if splice.occ == target then
      caret = state.region_start + splice.from + #splice.text
    end
    splice.occ.start = state.region_start + splice.from
    splice.occ.stop = state.region_start + splice.from + #splice.text
  end

  -- One write rebuilds the region with every mirror updated at once.
  if not apply(state.region_start, new_region_end, new_text) then
    return false
  end
  state.region_end = state.region_start + #new_text
  state.values[target.index] = doc.text_between(
      state.starts, state.lines, target.start, target.stop)

  -- Re-find the region end after the write (mirrors may have changed length).
  local entry = state.jumps[state.pos]
  if entry then
    select_range(entry.start, entry.stop)
  end
  if caret and caret >= 0 then
    state.cursor = math.min(caret, doc.total(state.starts, state.lines))
    place_cursor(state.cursor)
  end
  paint_marks()
  jot.editor.request_redraw()
  return true
end

-- Called when the caret leaves the region without an edit (CursorMoved):
-- LuaSnip offers to exit; we keep the session but stop selecting when the
-- caret is outside, and drop it once it is far away.
function M.check_region()
  if not state.active then
    return
  end
  refresh()
  local line, col = jot.cursor.get()
  local offset = doc.offset(state.starts, state.lines, line, col)
  if offset > state.region_end then
    M.exit(false)
  end
end

function M.region()
  return state.region_start, state.region_end
end

function M.push_history(entry)
  table.insert(state.history, 1, entry)
  while #state.history > 32 do
    table.remove(state.history)
  end
end

function M.last_snippet()
  return state.history[1]
end

return M
