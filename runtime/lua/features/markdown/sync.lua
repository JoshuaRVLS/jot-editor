-- Two-way scroll sync between the editor and the preview page.
--
-- Editor -> preview: the first visible buffer line is pushed over the SSE
-- `sync` event; the page scrolls to the matching `data-line` anchor.
-- Preview -> editor: the page POSTs its top line to `/sync`; the transport
-- queues it and this module drains the queue on a timer.
local M = {}

-- The line the browser last asked the editor to scroll to. Pushing it
-- straight back makes the page move, read its own move as an editor scroll,
-- and scroll again -- the two ends trade the same number and the view
-- jitters. Cleared as soon as the editor is somewhere else, so a genuine
-- later scroll to that same line still syncs.
local echoed = nil

-- First visible line of the focused pane's buffer, or nil.
function M.editor_line()
  local ok, info = pcall(jot.viewport.info)
  if not ok or type(info) ~= "table" or type(info.buffer) ~= "table" then
    return nil
  end
  return tonumber(info.buffer.first_line)
end

-- Path of the buffer the focused pane shows, or nil.
function M.focused_path()
  local ok, meta = pcall(jot.buffer.meta)
  if not ok or type(meta) ~= "table" then
    return nil
  end
  return meta.path
end

-- False while the editor sits on a line the browser just asked for.
function M.should_push(line)
  if line == echoed then
    return false
  end
  echoed = nil
  return true
end

-- Pushes the editor's top line to the browser.
function M.push_editor_scroll()
  local line = M.editor_line()
  if not line or not jot.preview or not jot.preview.sync then
    return false
  end
  pcall(jot.preview.sync, line)
  return true
end

-- Applies the newest line the browser reported. Returns it when applied.
function M.drain_browser_scroll()
  if not jot.preview or not jot.preview.take_scroll then
    return nil
  end
  local ok, line = pcall(jot.preview.take_scroll)
  if not ok or type(line) ~= "number" then
    return nil
  end
  echoed = line
  pcall(jot.viewport.scroll_top, line)
  return line
end

return M
