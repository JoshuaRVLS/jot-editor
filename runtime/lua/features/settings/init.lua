-- The settings menu's catalog, registered with jot.policy.
--
-- The menu labels its rows and picks how each is edited from the table in
-- features/settings/catalog.lua from now on; the native table in
-- src/surfaces/settings.cpp answers only when nothing is registered here, which
-- is a build or a test with no Lua runtime.
--
-- Registering is the whole of it, and only when the name is free: a config's own
-- catalog registers before this file loads, and shipping a fallback is no reason
-- to overwrite it. To change one row, declare just that key:
--
--   -- config.lua or a plugin
--   jot.policy.set("settings.catalog", {
--     tab_size = { label = "Indent width", type = "int", step = 1, min = 1, max = 16 },
--   })
--
-- A key with no entry still gets a row, labelled with the key itself, so a
-- plugin's own config key needs nothing here.

local catalog = require("jot_settings.catalog")

if not jot.policy.has("settings.catalog") then
  jot.policy.set("settings.catalog", catalog)
end
