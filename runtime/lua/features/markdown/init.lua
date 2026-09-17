-- Markdown preview — a browser preview of the current markdown buffer with
-- live updates and two-way scroll sync.
--
-- Commands:
--   :MarkdownPreview         start (or re-target) the preview
--   :MarkdownPreviewStop     stop it and close the server
--   :MarkdownPreviewToggle   toggle
--
-- Lua API (`jot.md`):
--   jot.md.setup{ auto_start = true, theme = "dark", options = { mermaid = true } }
--   jot.md.start(), jot.md.stop(), jot.md.toggle(), jot.md.refresh()
--   jot.md.is_running(), jot.md.url()
--
-- Every option is stored under `markdown_preview_*` in the normal config
-- store, so `config.lua`, `init.lua` and `:settings` all edit the same values
-- and take effect immediately.
local config = require("jot_md.config")
local render = require("jot_md.render")
local session = require("jot_md.session")

command("MarkdownPreview", function()
  session.start()
end, "Preview the current markdown file in a browser")

command("MarkdownPreviewStop", function()
  session.stop()
end, "Stop the markdown preview")

command("MarkdownPreviewToggle", function()
  session.toggle()
end, "Toggle the markdown preview")

autocmd("BufChange", function(event)
  session.on_buffer_change(event)
end)

autocmd("BufSave", function(event)
  session.on_buffer_save(event)
end)

autocmd("BufOpen", function(event)
  session.on_buffer_open(event)
end)

autocmd("BufClose", function(event)
  session.on_buffer_close(event)
end)

-- Status line segment: a compact marker while a preview is live.
pcall(function()
  jot.status.register("markdown_preview", {
    side = "right",
    priority = 25,
    text = function()
      local snapshot = session.state()
      if not snapshot.running then
        return ""
      end
      -- live while the page is behind the buffer, idle once the render has
      -- caught up. The port lives in the start message instead: it is read
      -- once, not watched.
      return " md " .. (snapshot.live and "live" or "idle")
    end,
  })
end)

jot.md = {
  setup = config.setup,
  start = session.start,
  stop = session.stop,
  toggle = session.toggle,
  refresh = session.refresh,
  is_running = session.is_running,
  url = session.url,
  state = session.state,
  config = config,
  render = render,
}

return true
