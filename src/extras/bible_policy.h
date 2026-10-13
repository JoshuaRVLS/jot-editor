#ifndef JOT_FEATURES_BIBLE_POLICY_H
#define JOT_FEATURES_BIBLE_POLICY_H

// The two halves of the Bible reader's reference policy -- reading a typed
// reference and printing its name -- with Lua in front of C++.
//
// Both are policy rather than mechanism: a reader is the one who would want a
// spelling to be recognised or the label to read differently, and neither
// should cost a rebuild. Both live in
// runtime/lua/features/bible/reference.lua, which registers itself with
// jot.policy on load; these wrappers ask it first and use the native
// implementation in extras/bible.cpp when there is no provider, when its answer
// cannot be used, or when the module failed to load.
//
// A provider's answer is accepted only once its book id resolves against the
// native canon, so a Lua edit can add spellings and change labels but can never
// name a book the fetch cannot address. One entry point per native call site is
// what keeps both implementations held to the same answers: callers never reach
// past these wrappers.
#include "extras/bible.h"

class LuaAPI;

// Each answers the same thing its Bible:: counterpart answers; `api` may be
// null (a build or a test with no Lua runtime), which is the plain native path.
bool bible_parse_reference(LuaAPI *api, const std::string &text, Bible::Reference &out);
std::string bible_reference_label(LuaAPI *api, const Bible::Reference &reference);

#endif
