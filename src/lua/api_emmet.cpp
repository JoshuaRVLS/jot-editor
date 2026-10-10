// `jot.emmet.expand()`: the abbreviation at the cursor, expanded.
//
// The trigger lives in the bundled snippet keymap rather than in the native key
// path, so it runs at the same point as the user's own snippet triggers and can
// be overridden like any other keymap entry (runtime/lua/features/snippet/
// keymaps.lua calls it from Tab, after the triggers and before the editor's own
// indentation). All this layer does is answer whether the expansion happened:
// true consumes the Tab, false lets it fall through.
#include "core/editor.h"
#include "lua/api.h"
#include "lua/bindings_internal.h"

bool LuaAPI::expand_emmet()
{
  return editor != nullptr && editor->expand_emmet_abbreviation();
}

namespace lua_bind
{
  int l_emmet_expand(lua_State *L)
  {
    lua_pushboolean(L, api(L).expand_emmet() ? 1 : 0);
    return 1;
  }
} // namespace lua_bind
