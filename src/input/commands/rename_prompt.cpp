// The interactive rename prompt: LSP rename with the new name typed in place.
//
// :lsprename <name> already existed, but it requires knowing the new name before
// opening it, which is the wrong shape for an editor -- helix prompts for it
// after you say what to rename. The prompt opens pre-filled with the identifier
// under the cursor, so the common case (fix a typo in a name) is one edit and
// Enter.
//
// Same shape as the Save As prompt (commands/save_prompt.cpp): one string, five
// branches, no list filtering. Rendering lives in render/prompts.cpp, matching
// how save_prompt is split.
#include "editor.h"

#include <algorithm>
#include <cctype>

namespace
{
  bool is_word_byte(char c)
  {
    return std::isalnum((unsigned char)c) || c == '_';
  }
} // namespace

// The identifier under the cursor, so the prompt starts from the current name.
// The same span walk exists in edit/multicursor.cpp (Ctrl+D) and edit/cursor.cpp
// (word motions); this is the third caller, small enough to stay local rather
// than grow a shared header for it.
std::string Editor::identifier_under_cursor()
{
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return {};
  }
  auto &buf = get_buffer();
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
  {
    return {};
  }
  const std::string line = buf.line(buf.cursor.y);
  int start = std::clamp(buf.cursor.x, 0, (int)line.size());
  int end = start;
  while (start > 0 && is_word_byte(line[(size_t)start - 1]))
  {
    start--;
  }
  while (end < (int)line.size() && is_word_byte(line[(size_t)end]))
  {
    end++;
  }
  return line.substr((size_t)start, (size_t)(end - start));
}

void Editor::open_rename_prompt()
{
  if (buffers.empty() || get_buffer().filepath.empty())
  {
    set_message("Save file first to rename with LSP");
    return;
  }
  rename_prompt_input = identifier_under_cursor();
  show_rename_prompt = true;
  hide_lsp_completion();
  needs_redraw = true;
}

void Editor::handle_rename_prompt(int ch)
{
  if (ch == 27)
  {
    show_rename_prompt = false;
    set_message("Rename cancelled");
    needs_redraw = true;
  }
  else if (ch == '\n' || ch == 13)
  {
    show_rename_prompt = false;
    needs_redraw = true;
    if (rename_prompt_input.empty())
    {
      set_message("Rename cancelled (empty name)");
      return;
    }
    lsp_rename_symbol(rename_prompt_input);
  }
  else if (ch == 127 || ch == 8)
  {
    if (!rename_prompt_input.empty())
    {
      rename_prompt_input.pop_back();
      needs_redraw = true;
    }
  }
  else if (ch >= 32 && ch < 127)
  {
    rename_prompt_input += (char)ch;
    needs_redraw = true;
  }
}
