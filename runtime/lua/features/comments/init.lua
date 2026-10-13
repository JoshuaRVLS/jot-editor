-- The comment markers each file type gets, registered with jot.policy.
--
-- `Ctrl+/` asks the table in features/comments/styles.lua now; the native table
-- in src/syntax/comment_style_data.h answers for a build or a test with no Lua
-- runtime, which is also where a field this file leaves out comes from.
--
-- Registering is the whole of it, and only when the name is free: a config's own
-- catalog registers before this file loads, and shipping a fallback is no reason
-- to overwrite it. To teach the editor a type it has never heard of, declare it:
--
--   -- config.lua or a plugin
--   jot.policy.set("text.comments", {
--     [".probe"] = { line = "#" },
--   })
--
-- A type with no entry gets the C-family form (//, /* */ for a selection).

local styles = require("jot_comments.styles")

if not jot.policy.has("text.comments") then
  jot.policy.set("text.comments", styles)
end
