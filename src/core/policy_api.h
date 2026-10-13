// ---------------------------------------------------------------------------
// jot.policy reads
// ---------------------------------------------------------------------------
//
// The readers behind a provider table: one entry's field, by name, with the
// caller's own fallback when nothing is registered, the entry is missing, or
// the field is absent or of another type. A surface or an optional feature asks
// these instead of reaching the Lua API, which sits above everything else in
// the tree; the implementation lives in core/editor.cpp next to the other
// places the Editor talks to Lua.
//
// See api_provider.cpp for the mechanism and docs/LUA_API.md for the contract:
// a provider is a named Lua table, its functions are the policy for a decision,
// and its plain fields are the data behind a table the native side would
// otherwise carry compiled in. runtime/lua/features/settings/catalog.lua is the
// first data provider; the native tables it replaced are kept as the fallback
// each reader's caller names.
//
// A fragment of the Editor class body, included by src/core/editor.h. It is not
// a standalone header: no include guard, no includes, and the members sit in
// class scope exactly as if they were written in editor.h (see
// docs/refactor/header-baseline.txt).
public:
// A string field of entry `key` in the provider `name`.
std::string policy_str(const char *name,
                       const std::string &key,
                       const char *field,
                       const std::string &fallback);
// An integer field, same rules; a field of another type answers `fallback`.
int policy_int(const char *name, const std::string &key, const char *field, int fallback);
// Whether the entry has this field at all, which is how a caller tells "no
// range declared" from "a range whose step is the default".
bool policy_has_field(const char *name, const std::string &key, const char *field);
// A field holding an array of strings, in order; empty when there is none.
std::vector<std::string> policy_str_list(const char *name, const std::string &key, const char *field);
