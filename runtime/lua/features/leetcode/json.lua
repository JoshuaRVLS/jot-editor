-- The LeetCode feature's JSON, which is the runtime's JSON (jot.lib.json): the
-- GraphQL replies it reads and the request bodies it writes. It used to be a
-- second implementation of the same encoder and decoder, which is one copy too
-- many; the module name stays because client.lua and the tests require it.
return require("jot.lib.json")
