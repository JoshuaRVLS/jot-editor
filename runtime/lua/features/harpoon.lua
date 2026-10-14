-- Harpoon (features/harpoon.lua): a short per-project list of the files you
-- keep coming back to, and one chord family for jumping between them.
--
-- A port of ThePrimeagen/harpoon's harpoon2 branch. What it keeps: items that
-- carry a file and the position you left it at, `add`, `select`, `remove`,
-- `remove_at`, `clear`, `get`, `prev` and `next` on a list, a quick menu that
-- lists the slots and jumps to the one picked, and a store on disk so the list
-- is still there tomorrow. What it drops: everything that was Neovim. The menu
-- is a float with its own keys rather than an editable scratch buffer, and a
-- jump is jot.file.open plus jot.cursor.set.
--
-- Lists are per project, keyed by the workspace root the way harpoon2 keys its
-- file on the project root. The store is the cache directory the other Lua
-- features already write to; a file marked in one project is not on another
-- project's list.
--
-- Three places this departs from harpoon2, each because the event or surface it
-- leans on is not here:
--   * harpoon2 refreshes a marked file's position on BufLeave. There is no
--     BufLeave, so the position follows CursorMoved (which the editor coalesces)
--     and is written out with the next change to the list, when the menu closes,
--     and on Shutdown.
--   * a file opened by jot.file.open can still be on its worker thread, so a
--     jump remembers where it is going and lands when that file's BufOpen
--     arrives.
--   * harpoon2 walks its menu by editing the buffer under it. Here the menu owns
--     the keys: j/k and the arrows move, a digit or Enter jumps, d drops the row
--     under the cursor, q or Esc closes.

local fs = require("jot.lib.fs")
local json = require("jot.lib.json")

-- A group of letters behind one prefix is how the editor's other families are
-- spelled (Alt+V, Alt+X, Alt+Shift+U). Alt+H is not the prefix here though it
-- would be the mnemonic one: Alt+H is the previous-word motion and Alt+H/Alt+L
-- also focus the pane to the left/right (docs/FEATURES.md, Navigation), and a
-- prefix on it would swallow both. Alt+Shift+H is free.
local CHORD = "Alt+Shift+H"
local DEFAULT_LIST = "files"
-- harpoon2's menu is eight lines tall and scrolls under the cursor past that.
-- Slot numbers keep going past 9: the digits reach the first nine, Enter and the
-- arrows reach the rest.
local MAX_MENU_ROWS = 8
local MENU_MIN_WIDTH = 34
local MENU_MAX_WIDTH = 72
local STORE_VERSION = 1
local DOT = "\xc2\xb7" -- the separator the other footers use

-- The list in front: what the chords and the menu act on. Every entry point that
-- names a list makes that list the one in front, so the chords follow whichever
-- list was last touched instead of always meaning the default one.
local active_list = DEFAULT_LIST
-- The store, read once per project and written back when it changes.
local store = nil
local root = nil
-- harpoon2's prev/next walk an index of their own that starts at the first slot
-- and is not moved by an explicit select. Kept as it is, per list.
local nav_index = {}
-- Where a jump is going when the file it is going to is still being opened.
local pending_jump = nil

local function palette()
  local theme = {}
  pcall(function() theme = jot.theme.palette() or {} end)
  local function pick(slot, field, fallback)
    local value = type(theme[slot]) == "table" and theme[slot][field]
    if type(value) == "number" then return value end
    return fallback
  end
  return {
    fg = pick("default", "fg", -1),
    bg = pick("default", "bg", -1),
    comment = pick("comment", "fg", 8),
    accent = pick("keyword", "fg", 6),
    border = pick("panel_border", "fg", 8),
    selection_fg = pick("selection", "fg", 0),
    selection_bg = pick("selection", "bg", 6),
  }
end

------------------------------------------------------------------ the store

-- The cache directory the Lua features share (the LeetCode cache uses the same
-- one). Per user, not per project: the project is a key inside the file.
local function cache_root()
  local sep = package.config:sub(1, 1)
  local override = os.getenv("JOT_CACHE_HOME")
  if override and override ~= "" then
    return override .. sep .. "harpoon"
  end
  if sep == "\\" then
    local base = os.getenv("LOCALAPPDATA")
    if not base or base == "" then base = os.getenv("APPDATA") end
    return (base or ".") .. "\\jot\\harpoon"
  end
  local xdg = os.getenv("XDG_CACHE_HOME")
  if xdg and xdg ~= "" then
    return xdg .. "/jot/harpoon"
  end
  return (os.getenv("HOME") or ".") .. "/.cache/jot/harpoon"
end

local function store_file()
  return fs.join(cache_root(), "lists.json")
end

-- The project a list belongs to: the workspace root, or the directory of the
-- file in front when no workspace is open, so a session on loose files still
-- gets a list of its own instead of sharing one with every other session.
local function project_root()
  local ok, workspace = pcall(jot.workspace.path)
  if ok and type(workspace) == "string" and workspace ~= "" then
    return workspace
  end
  local file = ""
  pcall(function() file = jot.buffer.current_file() or "" end)
  local parent = fs.parent(file)
  if type(parent) == "string" and parent ~= "" and parent ~= "." then
    return parent
  end
  return "no-workspace"
end

local function fresh_store()
  return { version = STORE_VERSION, projects = {} }
end

local function read_store()
  local ok, text = pcall(jot.file.read, store_file())
  if not ok or type(text) ~= "string" or text == "" then
    return fresh_store()
  end
  local decoded, value = pcall(json.decode, text)
  if not decoded or type(value) ~= "table" or type(value.projects) ~= "table" then
    -- A store that cannot be read is treated as empty. The list is a
    -- convenience and the worst case is marking the files again; refusing to use
    -- the feature, or overwriting the file on a bad read, would be worse.
    return fresh_store()
  end
  value.version = STORE_VERSION
  return value
end

-- Answers (ok, error). A failed write is reported to the caller rather than
-- swallowed: a list that quietly stops persisting is the one bug this store can
-- have that would otherwise never be seen.
local function write_store()
  if not store then return false, "nothing to save" end
  local encoded, text = pcall(json.encode, store)
  if not encoded then return false, "cannot encode the list" end
  local written, error = jot.file.write(store_file(), text)
  if written then return true end
  return false, error or "cannot write the store"
end

local function ensure_loaded()
  local project = project_root()
  if store and root == project then return end
  store = read_store()
  root = project
  nav_index = {}
end

-- A callback registered with jot.keymap is called with an empty string as its
-- argument, so a function that also takes a list name has to read that as "the
-- list in front" rather than as a list with no name.
local function name_or_active(name)
  if type(name) ~= "string" or name == "" then return active_list end
  return name
end

local function items_of(name)
  name = name_or_active(name)
  ensure_loaded()
  local project = store.projects[root]
  if not project then
    project = {}
    store.projects[root] = project
  end
  local list = project[name]
  if not list then
    list = {}
    project[name] = list
  end
  return list
end

local function items() return items_of(active_list) end

local function save(report)
  local ok, error = write_store()
  if not ok and report then
    jot.ui.show_message("Harpoon: " .. error)
  end
  return ok
end

------------------------------------------------------------------- the list

local function same_file(left, right)
  if left == right then return true end
  if left == "" or right == "" then return false end
  local shorter, longer = left, right
  if #shorter > #longer then shorter, longer = longer, shorter end
  if longer:sub(-#shorter) ~= shorter then return false end
  local before = longer:sub(-#shorter - 1, -#shorter - 1)
  return before == "/" or before == "\\"
end

local function current_file()
  local file = ""
  pcall(function() file = jot.buffer.current_file() or "" end)
  return file
end

-- The path a row reads as: relative to the project, which is how harpoon2 shows
-- its marks, and the full path when it is outside the project.
local function display_path(file)
  local relative = fs.relative(file)
  if type(relative) ~= "string" or relative == "" then return file end
  return relative
end

local function slot_of(file, name)
  local list = items_of(name or active_list)
  for index, item in ipairs(list) do
    if same_file(item.file, file) then return index end
  end
  return nil
end

local function add_file(file, line, col, name)
  name = name_or_active(name)
  if file == "" then
    jot.ui.show_message("Harpoon: this buffer has no file to mark")
    return false
  end
  local list = items_of(name)
  -- Marking a file that is already on the list refreshes where it is rather than
  -- adding it twice: the list is a set of files to jump to, and a second row for
  -- one file would only be another way to reach the same place.
  local existing = slot_of(file, name)
  if existing then
    list[existing].line, list[existing].col = line, col
    save(true)
    jot.ui.show_message("Harpoon: " .. display_path(file) .. " is already on the list")
    return false
  end
  list[#list + 1] = { file = file, line = line, col = col }
  save(true)
  jot.ui.show_message("Harpoon: marked " .. display_path(file))
  return true
end

local function add_current()
  local line, col = 1, 1
  local ok, cursor_line, cursor_col = pcall(jot.cursor.get)
  if ok then
    line = tonumber(cursor_line) or 1
    col = tonumber(cursor_col) or 1
  end
  return add_file(current_file(), line, col)
end

local function remove_slot(index)
  local list = items()
  local item = list[index]
  if not item then return false end
  table.remove(list, index)
  save(true)
  jot.ui.show_message("Harpoon: removed " .. display_path(item.file))
  return true
end

-- harpoon2's remove() drops the file in front, whatever slot it holds.
local function remove_current()
  local file = current_file()
  if file == "" then
    jot.ui.show_message("Harpoon: this buffer has no file to drop")
    return false
  end
  local index = slot_of(file)
  if not index then
    jot.ui.show_message("Harpoon: " .. display_path(file) .. " is not on the list")
    return false
  end
  return remove_slot(index)
end

local function clear_list(name)
  name = name_or_active(name)
  local list = items_of(name)
  local count = #list
  if count == 0 then
    jot.ui.show_message("Harpoon: the list is already empty")
    return false
  end
  store.projects[root][name] = {}
  save(true)
  jot.ui.show_message("Harpoon: cleared " .. count .. (count == 1 and " mark" or " marks"))
  return true
end

local function land_pending_jump()
  if not pending_jump then return false end
  local file = current_file()
  if file == "" or not same_file(file, pending_jump.file) then return false end
  jot.cursor.set(pending_jump.line or 1, pending_jump.col or 1)
  pending_jump = nil
  return true
end

local function jump_to(item)
  if type(item) ~= "table" or type(item.file) ~= "string" or item.file == "" then
    return false
  end
  pending_jump = { file = item.file, line = tonumber(item.line), col = tonumber(item.col) }
  jot.file.open(item.file)
  -- A file that is already open fires no BufOpen, so the landing is attempted
  -- here as well; whichever comes first clears the pending jump.
  land_pending_jump()
  return true
end

local function jump_slot(index)
  local item = items()[tonumber(index) or 0]
  if not item then
    jot.ui.show_message("Harpoon: no slot " .. tostring(index) .. " on this list")
    return false
  end
  return jump_to(item)
end

local function step(delta)
  local list = items()
  local count = #list
  if count == 0 then
    jot.ui.show_message("Harpoon: nothing is marked yet")
    return false
  end
  local index = (nav_index[active_list] or 1) + delta
  if index < 1 or index > count then
    if jot.config.get_bool("harpoon_nav_wrap", false) then
      index = ((index - 1) % count) + 1
    else
      -- harpoon2 clamps at the ends by default; wrapping is opt-in.
      index = index < 1 and 1 or count
    end
  end
  nav_index[active_list] = index
  return jump_to(list[index])
end

-- The named variants: a script driving a second list switches the list in front
-- as it goes, so the menu and the chords follow it.
local function jump_slot_in(name, index)
  active_list = name
  return jump_slot(index)
end

local function step_in(name, delta)
  active_list = name
  return step(delta)
end

------------------------------------------------------------------- the menu

local menu = { win = 0, buf = 0, index = 1, top = 1 }
local menu_key
local menu_mouse

local function menu_open()
  return menu.win ~= 0 and jot.ui.float.is_valid(menu.win)
end

local function close_menu()
  if menu_open() then
    jot.ui.float.close(menu.win, true)
  end
  menu.win = 0
  -- The scratch buffer is the menu's alone; the float does not delete it, so
  -- closing without this leaks one buffer per toggle.
  if menu.buf ~= 0 then
    jot.ui.buffer.delete(menu.buf)
    menu.buf = 0
  end
  -- Where the marks moved to while the menu was up is part of the list now.
  save(false)
end

-- The rows the menu shows and the width they need: the slot, the path, and the
-- line the mark sits on when that is past the first line.
local function menu_rows()
  local list = items()
  local rows = {}
  local widest = MENU_MIN_WIDTH - 4
  for index, item in ipairs(list) do
    local path = display_path(item.file)
    local detail = ""
    local line = tonumber(item.line)
    if line and line > 1 then detail = ":" .. line end
    rows[index] = { slot = index, path = path, detail = detail }
    widest = math.max(widest, #path + #detail + 10)
  end
  return rows, math.max(MENU_MIN_WIDTH, math.min(MENU_MAX_WIDTH, widest))
end

-- The float's lines and the spans that ink them. Both are byte offsets into the
-- line, which is why the detail's position is carried out of the padding step
-- rather than recomputed.
local function menu_body(rows, width, colors)
  local lines, spans = {}, {}
  -- The float's border takes a cell on each side, so a row that filled `width`
  -- bytes would be two past the interior and the renderer would clip its tail
  -- (which is where the line detail sits).
  local inner = math.max(1, width - 2)
  local last = math.min(#rows, menu.top + MAX_MENU_ROWS - 1)
  for index = menu.top, last do
    local row = rows[index]
    local label = string.format("%3d  %s", row.slot, row.path)
    local detail_at = 0
    local text = label
    if row.detail ~= "" and #label + #row.detail < inner then
      local pad = math.max(1, inner - #label - #row.detail)
      detail_at = #label + pad
      text = label .. string.rep(" ", pad) .. row.detail
    end
    lines[#lines + 1] = text .. string.rep(" ", math.max(0, inner - #text))
    local selected = index == menu.index
    local row_bg = selected and colors.selection_bg or colors.bg
    local span = {}
    if selected then
      span[#span + 1] = { start = 0, len = 65535, fg = colors.selection_fg, bg = colors.selection_bg }
    end
    -- The slot number is the handle a digit jumps to, so it reads as a handle
    -- and not as part of the path.
    span[#span + 1] = { start = 0, len = 3, fg = colors.accent, bg = row_bg }
    if detail_at > 0 then
      span[#span + 1] = { start = detail_at, len = #row.detail, fg = colors.comment, bg = row_bg }
    end
    spans[#spans + 1] = span
  end
  if #lines == 0 then
    local text = "  nothing is marked here yet"
    lines[1] = text .. string.rep(" ", math.max(0, inner - #text))
    spans[1] = { { start = 0, len = 65535, fg = colors.comment, bg = colors.bg } }
  end
  return lines, spans
end

local function menu_geometry(height, width)
  local viewport = {}
  pcall(function() viewport = jot.viewport.info() or {} end)
  local window = viewport.window or {}
  local pane = viewport.pane or {}
  local window_width = tonumber(window.width) or 100
  local window_height = tonumber(window.height) or 30
  local pane_x = tonumber(pane.x) or 0
  local pane_width = tonumber(pane.width) or (window_width - pane_x)
  local fit = math.max(24, math.min(width, pane_width - 4))
  return pane_x + math.max(1, math.floor((pane_width - fit) / 2)),
         math.max(1, math.floor((window_height - height) / 2)),
         fit
end

local function paint_menu()
  local rows, wanted = menu_rows()
  local shown = math.max(1, math.min(#rows, MAX_MENU_ROWS))
  local height = shown + 2
  local x, y, width = menu_geometry(height, wanted)
  local colors = palette()
  local lines, spans = menu_body(rows, width, colors)
  if not menu_open() then
    menu.buf = jot.ui.buffer.create(false, true)
    menu.win = jot.ui.float.open(menu.buf, {
      relative = "editor", col = x, row = y, width = width, height = height,
      border = "single", focusable = true, zindex = 60,
      title = " Harpoon ",
      footer = "enter jumps  " .. DOT .. "  d removes  " .. DOT .. "  q closes",
      fg = colors.fg, bg = colors.bg, border_fg = colors.border,
      title_fg = colors.accent, footer_fg = colors.comment,
      on_key = menu_key,
      on_mouse = menu_mouse,
    })
    if not menu.win or menu.win == 0 then
      jot.ui.show_message("Harpoon: could not open the menu")
      jot.ui.buffer.delete(menu.buf)
      menu.buf = 0
      return
    end
  else
    jot.ui.float.configure(menu.win, { col = x, row = y, width = width, height = height })
  end
  jot.ui.buffer.set_lines(menu.buf, 0, -1, false, lines)
  for index, span in ipairs(spans) do
    jot.ui.float.set_spans(menu.win, index, span)
  end
end

local function move_menu(delta)
  local count = #items()
  if count == 0 then return end
  menu.index = math.max(1, math.min(count, menu.index + delta))
  if menu.index < menu.top then menu.top = menu.index end
  if menu.index > menu.top + MAX_MENU_ROWS - 1 then
    menu.top = menu.index - MAX_MENU_ROWS + 1
  end
  paint_menu()
end

local function open_menu()
  menu.index = 1
  menu.top = 1
  paint_menu()
end

local function toggle_menu()
  if menu_open() then
    close_menu()
    return false
  end
  open_menu()
  return true
end

------------------------------------------------------------------- the keys

-- The menu's own keys. It holds the keyboard while it is up, so a key it does
-- not use is still answered rather than falling through into the buffer behind
-- the menu.
menu_key = function(event)
  local key = event.key
  if key == 27 or key == 113 then -- Esc, q
    close_menu()
    return true
  end
  if key == 13 or key == 10 then -- Enter
    if jump_slot(menu.index) then close_menu() end
    return true
  end
  if key == 106 or key == 1009 then -- j, Down
    move_menu(1)
    return true
  end
  if key == 107 or key == 1008 then -- k, Up
    move_menu(-1)
    return true
  end
  if key == 100 then -- d
    if remove_slot(menu.index) then
      local count = #items()
      if count == 0 then
        close_menu()
      else
        menu.index = math.min(menu.index, count)
        paint_menu()
      end
    end
    return true
  end
  if key and key >= 49 and key <= 57 then -- 1..9
    if jump_slot(key - 48) then close_menu() end
    return true
  end
  return true
end

menu_mouse = function(event)
  if event.button == 65 then
    move_menu(3)
    return true
  end
  if event.button == 64 then
    move_menu(-3)
    return true
  end
  -- A left click on a row jumps to it. The float's border is row 0, so the first
  -- content row is row 1.
  if event.pressed and event.button % 4 == 0 and event.row >= 1 then
    local index = menu.top + (event.row - 1)
    if items()[index] and jump_slot(index) then
      close_menu()
    end
  end
  return true
end

----------------------------------------------------------------- the wiring

jot.command("HarpoonAdd", function()
  add_current()
end, "Add the file in front to the harpoon list")

jot.command("HarpoonRemove", function()
  remove_current()
end, "Drop the file in front from the harpoon list")

jot.command("HarpoonMenu", function()
  toggle_menu()
end, "Show or hide the harpoon list")

jot.command("HarpoonClear", function()
  clear_list()
end, "Empty the harpoon list for this project")

jot.command("HarpoonNext", function()
  step(1)
end, "Jump to the next harpoon mark")

jot.command("HarpoonPrev", function()
  step(-1)
end, "Jump to the previous harpoon mark")

jot.command("HarpoonJump", function(arg)
  local index = tonumber(arg)
  if not index then
    jot.ui.show_message("Usage: :HarpoonJump <slot>")
    return
  end
  jump_slot(index)
end, "Jump to harpoon slot <n>")

jot.keymap.set(CHORD, "", "Harpoon")
jot.keymap.set(CHORD .. " a", add_current, "Harpoon: mark this file")
jot.keymap.set(CHORD .. " m", toggle_menu, "Harpoon: list")
jot.keymap.set(CHORD .. " d", remove_current, "Harpoon: drop this file")
jot.keymap.set(CHORD .. " c", clear_list, "Harpoon: clear the list")
jot.keymap.set(CHORD .. " n", function() step(1) end, "Harpoon: next mark")
jot.keymap.set(CHORD .. " p", function() step(-1) end, "Harpoon: previous mark")
for slot = 1, 9 do
  jot.keymap.set(CHORD .. " " .. tostring(slot), function() jump_slot(slot) end,
                 "Harpoon: jump to slot " .. slot)
end

-- A mark sits where its file was last left, so the position follows the cursor
-- instead of being frozen at the moment the file was marked.
jot.autocmd("CursorMoved", function(event)
  if not store or type(event.path) ~= "string" or event.path == "" then return end
  local line, col = tonumber(event.line), tonumber(event.column)
  if not line then return end
  for _, project in pairs(store.projects) do
    for _, list in pairs(project) do
      for _, item in ipairs(list) do
        if same_file(item.file, event.path) then
          item.line, item.col = line, col
        end
      end
    end
  end
end)

-- A file opened on a worker thread announces itself when its text is in, which
-- is the moment a pending jump can be applied.
jot.autocmd("BufOpen", function()
  land_pending_jump()
end)

-- The last chance to write the store: the editor is going away.
jot.autocmd("Shutdown", function()
  if store then write_store() end
end)

--------------------------------------------------------------------- the API

-- The harpoon2 shape, hung on `jot.harpoon`, so a chord of your own, an
-- autocmd or a command can drive lists by name:
--
--   jot.harpoon.list("term"):add()
--   jot.harpoon.list():get(1)
--
-- Naming a list makes it the one in front, which is the list the chords and the
-- menu act on until another one is named.
local function list_object(name)
  local object = { name = name }
  function object:items() return items_of(self.name) end
  function object:add() return add_file(current_file(), select(1, jot.cursor.get()), select(2, jot.cursor.get()), self.name) end
  function object:get(index) return items_of(self.name)[tonumber(index) or 0] end
  function object:select(index) return jump_slot_in(self.name, index) end
  function object:remove() return remove_current() end
  function object:remove_at(index)
    active_list = self.name
    return remove_slot(index)
  end
  function object:clear() return clear_list(self.name) end
  function object:prev() return step_in(self.name, -1) end
  function object:next() return step_in(self.name, 1) end
  function object:toggle_quick_menu()
    active_list = self.name
    return toggle_menu()
  end
  return object
end

-- The table lands after this file has run, so a config that wants it reads it
-- from inside a callback (a keymap, an autocmd), not while config.lua is being
-- read: the bundled features load after the user's own files.
jot.harpoon = {
  -- Called as `jot.harpoon:list("term")` or `jot.harpoon.list("term")`: the
  -- first argument is the table itself in the colon form.
  list = function(first, second)
    local name = type(first) == "string" and first or second
    active_list = name or DEFAULT_LIST
    return list_object(active_list)
  end,
  ui = {
    toggle_quick_menu = function() return toggle_menu() end,
  },
  add = add_current,
  remove = remove_current,
  clear = clear_list,
  jump = jump_slot,
  next = function() return step(1) end,
  prev = function() return step(-1) end,
}
