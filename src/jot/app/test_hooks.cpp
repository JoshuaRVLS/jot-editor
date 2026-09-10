// Headless test hooks for the Lua-rendered surfaces. Kept in their own file so
// the production paths stay free of test-only logic.
#include "editor.h"
#include "jot/lua/api.h"

UI *Editor::ui_for_test()
{
  return ui;
}

void Editor::render_frame_for_test()
{
  render_frame();
}

int Editor::lua_float_count_for_test(const std::string &surface) const
{
  if (!lua_api)
  {
    return 0;
  }
  int count = 0;
  for (const auto &entry : lua_api->float_windows)
  {
    if (!entry.second.hide && entry.second.surface == surface)
    {
      count++;
    }
  }
  return count;
}
