local M = {}

function M.configure(native)
  -- Native renderer consumes these policy values without calling Lua per cell.
  native.set_capture_color("comment", 3)
  native.set_capture_color("string", 2)
  native.set_capture_color("keyword", 1)
end

return M
