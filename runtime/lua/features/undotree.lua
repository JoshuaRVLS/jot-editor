-- Undo tree (:Undotree): every state a buffer has been in, in the right dock,
-- newest first. undo.cpp owns the states and the walking; this file draws them
-- and decides what a key does.
--
-- Browsing never moves the buffer. j/k put the cursor on a state and the
-- changes it would bring back are listed under it; Enter (or a click on the
-- row) is what walks the buffer there. That is the whole reason the panel
-- exists: to see what an undo would do before taking it. J and K are the other
-- half: they move the buffer a step at a time (J forward to the newest child,
-- K back to the parent), the way the editor's own redo and undo do.
--
-- Keys arrive on the panel callback itself, as an event with action = "key":
-- the editor only offers them while the dock's plugin tab holds focus and no
-- prompt or palette is up, so the panel never takes a letter out of a search
-- box it cannot see.
local PANEL = "Undotree"

local textlib = require("jot.lib.text")

-- The cursor's state, kept between frames. The panel callback runs on every
-- painted frame, so the tree, the two texts of the selected pair and the diff
-- are cached under a key that changes only when something did.
local selected = nil
local cached_key = nil
local cached_rows = nil
-- The g of a "g j" chord, which selects the parent of the state in front.
local pending_g = false

local MAX_DIFF_LINES = 4000
local MAX_DIFF_ROWS = 40

local function palette()
  local theme = {}
  pcall(function() theme = jot.theme.palette() or {} end)
  local function pick(slot, fallback)
    local value = type(theme[slot]) == "table" and theme[slot].fg
    if type(value) == "number" then return value end
    return fallback
  end
  return {
    fg = pick("default", -1),
    comment = pick("comment", 8),
    accent = pick("keyword", 6),
    added = pick("git_added", 2),
    removed = pick("git_deleted", 1),
    saved = pick("status_warning", 3),
    border = pick("panel_border", 8),
  }
end

-- A state's age, in the shortest form that still says how long ago it was.
local function age_label(time, now)
  local seconds = math.max(0, math.floor(now - (tonumber(time) or now)))
  if seconds < 60 then return seconds .. "s" end
  if seconds < 3600 then return math.floor(seconds / 60) .. "m" end
  if seconds < 86400 then return math.floor(seconds / 3600) .. "h" end
  return math.floor(seconds / 86400) .. "d"
end

-- Where each state sits in the drawing: how many forks are above it, and how
-- many states it leads to. A run of edits with no undo behind it is a straight
-- line, so it stays at column zero and only the other branch moves over.
local function layout(states)
  local by_seq = {}
  local children = {}
  for _, node in ipairs(states) do
    by_seq[node.seq] = node
    children[node.seq] = 0
  end
  for _, node in ipairs(states) do
    if node.parent and children[node.parent] then
      children[node.parent] = children[node.parent] + 1
    end
  end
  local forks = {}
  for seq, count in pairs(children) do
    forks[seq] = count > 1
  end
  local function depth(node)
    local levels = 0
    local parent = node.parent
    local guard = 0
    while parent and by_seq[parent] and guard < 10000 do
      if forks[parent] then levels = levels + 1 end
      parent = by_seq[parent].parent
      guard = guard + 1
    end
    return levels
  end
  local tips = {}
  for _, node in ipairs(states) do
    tips[node.seq] = children[node.seq] == 0
  end
  return depth, tips
end

-- The lines that differ between two states, without the ones both hold: the
-- walk is jot.lib.text.diff, shared with the inline assistant. The two texts
-- are a few states apart, so past the cap below the panel says the pair is too
-- big rather than spending the frame on it; the walk's own budget is raised out
-- of the way because this cap is the tighter one.
local function changed_lines(a, b)
  if #a + #b > MAX_DIFF_LINES then return nil, true end
  local rows = textlib.diff(a, b, { max_cells = MAX_DIFF_LINES * MAX_DIFF_LINES })
  if not rows then return nil, true end
  local out = {}
  for _, row in ipairs(rows) do
    if row.kind ~= "same" then out[#out + 1] = row end
  end
  return out, false
end

-- The changes the selected state would bring back: what the buffer holds now
-- against what that state held, the older line marked first.
local function change_rows(colors, tree, node)
  local rows = {}
  local here = jot.undo.lines(tree.current)
  local there = jot.undo.lines(node.seq)
  if not here or not there then
    rows[#rows + 1] = {
      text = "  Changes",
      detail = "not readable",
      fg = colors.comment,
      bold = true,
    }
    return rows, 0
  end
  local lines, capped = changed_lines(here, there)
  if capped then
    rows[#rows + 1] = {
      text = "  Changes",
      detail = "too many lines",
      fg = colors.comment,
      bold = true,
    }
    return rows, 0
  end
  local count = 0
  for _, line in ipairs(lines) do
    if line.kind ~= "same" then count = count + 1 end
  end
  rows[#rows + 1] = {
    text = "  Changes",
    detail = count == 1 and "1 line" or (count .. " lines"),
    fg = colors.comment,
    bold = true,
  }
  if count == 0 then
    return rows, 0
  end
  local shown = 0
  for _, line in ipairs(lines) do
    if line.kind ~= "same" and shown < MAX_DIFF_ROWS then
      shown = shown + 1
      rows[#rows + 1] = {
        text = "  " .. (line.kind == "del" and "- " or "+ ") .. line.text,
        fg = line.kind == "del" and colors.removed or colors.added,
      }
    end
  end
  if count > shown then
    rows[#rows + 1] = {
      text = "  " .. (count - shown) .. " more changed lines",
      fg = colors.comment,
    }
  end
  return rows, count
end

local function build_rows()
  local colors = palette()
  local tree = jot.undo.tree() or { states = {}, current = 0, last = 0, writes = 0 }
  local states = tree.states or {}
  local rows = {}

  if #states == 0 then
    rows[#rows + 1] = { text = "No states yet", fg = colors.accent, bold = true }
    rows[#rows + 1] = { text = "The first change to this buffer", fg = colors.comment }
    rows[#rows + 1] = { text = "adds the state it started from.", fg = colors.comment }
    return rows
  end

  if not selected then selected = tree.current end

  -- Reading the tree is native and cheap; the copies this panel makes are not,
  -- so everything below is skipped until one of these numbers moves.
  local key = table.concat({
    tostring(tree.current), tostring(tree.last), tostring(tree.writes),
    tostring(#states), tostring(selected), tostring(math.floor(os.time() / 20)),
  }, ":")
  if key == cached_key and cached_rows then return cached_rows end

  local now = os.time()
  local depth, tips = layout(states)
  local by_seq = {}
  for _, node in ipairs(states) do by_seq[node.seq] = node end

  -- Newest first, the way every other list in the editor reads.
  local ordered = {}
  for _, node in ipairs(states) do ordered[#ordered + 1] = node end
  table.sort(ordered, function(left, right) return left.seq > right.seq end)

  for _, node in ipairs(ordered) do
    local levels = depth(node)
    -- Star for the state the buffer is on, o for a state nothing grew from (a
    -- branch you undid away from), bar for one in the middle of a line.
    local glyph = tips[node.seq] and "o" or "|"
    if node.current then
      glyph = "*"
    end
    local detail = age_label(node.time, now)
    if node.saved == 1 then
      detail = "saved " .. detail
    elseif node.saved == 2 then
      detail = "written " .. detail
    end
    rows[#rows + 1] = {
      text = string.rep("  ", levels) .. glyph .. " " .. tostring(node.seq),
      detail = detail,
      fg = node.current and colors.accent or colors.fg,
      bold = node.current,
      selected = node.seq == selected,
      action = "goto:" .. tostring(node.seq),
    }
    if node.seq == selected and not node.current then
      local changes = change_rows(colors, tree, node)
      for _, row in ipairs(changes) do rows[#rows + 1] = row end
    end
  end

  cached_key = key
  cached_rows = rows
  return rows
end

local function current_seq()
  local tree = jot.undo.tree()
  return tree and tree.current or nil
end

local function select_delta(delta)
  local tree = jot.undo.tree()
  local states = tree and tree.states or {}
  if #states == 0 then return end
  if not selected then selected = tree.current end
  -- The list is sorted by sequence number, newest first: moving down the panel
  -- walks back through the states in the order the panel shows them.
  local ordered = {}
  for _, node in ipairs(states) do ordered[#ordered + 1] = node end
  table.sort(ordered, function(left, right) return left.seq > right.seq end)
  for index, node in ipairs(ordered) do
    if node.seq == selected then
      local next_index = math.min(#ordered, math.max(1, index + delta))
      selected = ordered[next_index].seq
      break
    end
  end
  jot.editor.request_redraw()
end

local function node_of(tree, seq)
  for _, node in ipairs(tree.states or {}) do
    if node.seq == seq then return node end
  end
  return nil
end

-- g j: put the cursor on the parent of the state it is on, without touching
-- the buffer. The parent is one branch older, so this walks the cursor across
-- a fork rather than down the list.
local function select_parent()
  local tree = jot.undo.tree()
  local node = tree and node_of(tree, selected)
  if node and node.parent then
    selected = node.parent
  end
  jot.editor.request_redraw()
end

-- The newest child of a state, which is where the editor's own redo goes: the
-- last edit that grew from it, not the first branch it left behind.
local function newest_child(tree, seq)
  local best = nil
  for _, node in ipairs(tree.states or {}) do
    if node.parent == seq and (not best or node.seq > best) then
      best = node.seq
    end
  end
  return best
end

local function walk_to(seq)
  if not seq then return end
  if jot.undo.goto(seq) then
    selected = seq
  end
  jot.editor.request_redraw()
end

-- J and K move the buffer and apply the state they land on: K steps back to the
-- parent, J forward to the newest child. That is the tree's own undo and redo
-- (Ctrl+Z and Ctrl+Y do the same from the keyboard) drawn as a move through the
-- history, and the cursor follows the buffer so the starred row stays in sight.
local function step_older()
  local tree = jot.undo.tree()
  local node = tree and node_of(tree, tree.current)
  if node and node.parent then
    walk_to(node.parent)
  end
end

local function step_newer()
  local tree = jot.undo.tree()
  if tree then
    walk_to(newest_child(tree, tree.current))
  end
end

local function panel_shown()
  local info = jot.viewport.info()
  local dock = info and info.right_panel
  return dock and dock.visible and dock.plugin == PANEL
end

local function toggle()
  if panel_shown() then
    jot.ui.close_panel()
    return
  end
  selected = current_seq()
  cached_key = nil
  pending_g = false
  -- The second argument hands the dock the keyboard: j/k/Enter are this
  -- panel's, and everything it does not use still reaches the buffer.
  jot.ui.panel(PANEL, true)
  jot.editor.request_redraw()
end

-- The one irreversible thing the panel can do, so it says what it did rather
-- than leaving a buffer that quietly has no history behind it.
local function clear_history()
  jot.undo.clear()
  selected = nil
  cached_key = nil
  jot.ui.show_message("Undo history cleared")
  jot.editor.request_redraw()
end

-- One key, answered: true means the panel used it and the editor stops there,
-- false hands it back (typing, Ctrl chords, everything else).
local function on_key(event)
  local name = event.key or ""
  -- A lowercase letter reaches the panel as its own letter ("J" for j) and a
  -- capital one carries the shift bit ("Shift+S" for S), so the two halves of
  -- the table are told apart by that flag: j and k walk the cursor, J and K
  -- walk the buffer.
  if name == "J" then
    if pending_g then
      pending_g = false
      select_parent()
    else
      select_delta(1)
    end
    return true
  end
  if name == "Shift+J" then
    step_newer()
    return true
  end
  if name == "K" then
    select_delta(-1)
    return true
  end
  if name == "Shift+K" then
    step_older()
    return true
  end
  local used = false
  if name == "G" then
    pending_g = true
    used = true
  elseif name == "Enter" then
    walk_to(selected)
    used = true
  elseif name == "Shift+S" then
    -- The tree is read fresh on the next paint. Nothing is cached across a
    -- command, so this is for the frames after an edit made outside the panel.
    cached_key = nil
    jot.editor.request_redraw()
    used = true
  end
  pending_g = false
  return used
end

local function panel_callback(_name, event)
  if type(event) == "table" and type(event.action) == "string" then
    if event.action == "key" then
      return on_key(event)
    end
    local seq = tonumber(event.action:match("^goto:(%d+)$"))
    if seq then walk_to(seq) end
  end
  return build_rows()
end

-- Reading the tree from Lua asks the native side to reconcile the edit a
-- command just made, so a panel opened right after a keystroke shows it.
jot.ui.register_panel(PANEL, panel_callback, PANEL)
jot.command("Undotree", toggle, "Show the undo tree for this buffer in the right dock")
jot.command("UndotreeClear", clear_history, "Drop this buffer's undo history")
jot.keymap.set("Alt+Shift+U", "", "Undo tree")
jot.keymap.set("Alt+Shift+U T", toggle, "Undo tree: toggle the panel")
jot.keymap.set("Alt+Shift+U S", function()
  selected = current_seq()
  cached_key = nil
  jot.ui.panel(PANEL, true)
  jot.editor.request_redraw()
end, "Undo tree: jump to the state in front")

return {
  toggle = toggle,
  clear = clear_history,
  rows = build_rows,
  panel = PANEL,
}
