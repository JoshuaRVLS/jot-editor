-- The Bible reader's reference policy, in Lua.
--
-- `:bible` reads a reference out of its argument (the book, the chapter, the
-- verse span) and prints the passage's name in the tab strip and the toast.
-- Both are policy: a reader is the one who would want another spelling to
-- work or the label to read differently, and neither should cost a rebuild.
-- Both are ported here from src/extras/bible.cpp, which keeps its own copy as
-- the fallback for a build without this module or a session that unregisters
-- it.
--
-- Registering is the whole of it: jot.policy.set names the piece of native
-- policy this module takes over, and C++ asks here first from then on. An
-- answer naming a book the native canon does not have is refused -- a
-- reference is only accepted once its id resolves there -- so a wrong edit to
-- this file costs a fallback rather than a passage the fetch cannot address.
--
-- This is the default provider, and it only claims the name if it is free: a
-- config's own provider registers before this file loads, and shipping a
-- fallback is no reason to overwrite it.

local reference = require("jot_bible.reference")

if not jot.policy.has("bible.reference") then
  jot.policy.set("bible.reference", reference)
end
