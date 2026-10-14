-- The AI feature's JSON, which is the runtime's JSON: the request bodies and the
-- streamed replies are one thing to decode and one decoder should do it. This
-- module stays because features/ai/http.lua and the tests require it by that
-- name; the implementation is jot.lib.json (runtime/lua/lib/json.lua), shared
-- with the LeetCode feature and with any plugin that asks for it.
return require("jot.lib.json")
