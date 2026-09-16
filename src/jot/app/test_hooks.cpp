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

// Mirrors what the terminal and GUI backends do with a key event: decode the raw
// code (modifier bits and the 0x8000 uppercase bit) into the key/ctrl/shift/alt
// fields, then hand those to the dispatcher. Keeping the two steps together is
// the point -- a bug in the decode shows up as a mis-routed binding.
void Editor::raw_key_for_test(int raw_ch)
{
  const KeyEvent ev = decode_key_event(raw_ch);
  handle_input(ev.key, ev.ctrl, ev.shift, ev.alt, ev.key);
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

void Editor::seed_lsp_diagnostic_for_test(const std::string &client_key,
                                          const std::string &path,
                                          int line,
                                          int severity,
                                          const std::string &message)
{
  Diagnostic diagnostic;
  diagnostic.line = line;
  diagnostic.col = 0;
  diagnostic.end_line = line;
  diagnostic.end_col = 1;
  diagnostic.severity = severity;
  diagnostic.message = message;
  lsp_diag_slices_[client_key][path].push_back(diagnostic);
}
