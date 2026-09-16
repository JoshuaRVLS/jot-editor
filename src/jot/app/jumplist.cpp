// The jumplist: the places the cursor has been, and the commands that walk them.
//
// Helix and vim both make "where was I" a first-class motion, and jot only had a
// one-way LSP stack that go-to-definition pushed to. This generalises it: any
// navigation that moves you somewhere else records where it landed, so Ctrl+O
// walks back through everything (a definition, a picker, a search hit, another
// file) and Ctrl+I walks forward again.
//
// The history is a list plus a cursor (`jump_index`). Recording a jump truncates
// anything ahead of the cursor first, so jumping after going back forks the
// history instead of leaving a stale forward tail.
#include "editor.h"
#include "tools/string_util.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
  // Deep enough to retrace a session's worth of navigation, small enough that
  // the picker stays readable and the copy-on-record stays cheap.
  constexpr size_t kJumpHistoryMax = 100;
} // namespace

// JumpLocation, not Editor::JumpLocation: the in-class alias is private, and the
// definition's types are named at namespace scope.
JumpLocation Editor::capture_jump_location()
{
  JumpLocation loc;
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return loc;
  }
  const auto &buf = get_buffer();
  loc.filepath = buf.filepath;
  loc.cursor = buf.cursor;
  loc.scroll_offset = buf.scroll_offset;
  loc.scroll_x = buf.scroll_x;
  loc.preview = buf.is_preview;
  return loc;
}

void Editor::record_jump()
{
  // A restore moves the cursor through the same paths a jump does; recording it
  // would make Ctrl+O append to the history it is walking.
  if (jump_restoring)
  {
    return;
  }
  const JumpLocation loc = capture_jump_location();
  if (loc.filepath.empty())
  {
    // Nothing to come back to: an untitled buffer has no location to name.
    return;
  }
  if (jump_index >= 0 && jump_index + 1 < (int)jump_history.size())
  {
    jump_history.resize((size_t)jump_index + 1);
  }
  jump_history.push_back(loc);
  if (jump_history.size() > kJumpHistoryMax)
  {
    const size_t overflow = jump_history.size() - kJumpHistoryMax;
    jump_history.erase(jump_history.begin(), jump_history.begin() + (long)overflow);
  }
  jump_index = (int)jump_history.size() - 1;
}

bool Editor::jump_to(const JumpLocation &loc)
{
  if (loc.filepath.empty())
  {
    return false;
  }
  const bool was_restoring = jump_restoring;
  jump_restoring = true;
  // The cursor can only be placed once the buffer exists, so arm the jump and
  // let open_file (and the call below, for a file that was already open) apply
  // it -- the same deferred path go-to-definition uses.
  jump_pending_location = loc;
  jump_pending = true;
  open_file(loc.filepath, loc.preview);
  const bool moved = apply_pending_jump();
  jump_restoring = was_restoring;
  needs_redraw = true;
  return moved;
}

void Editor::jump_back()
{
  if (jump_history.empty() || jump_index < 0)
  {
    set_message("No jump history yet");
    return;
  }
  if (jump_index == 0)
  {
    set_message("Start of jump history");
    return;
  }
  jump_index--;
  const JumpLocation loc = jump_history[(size_t)jump_index];
  if (jump_to(loc))
  {
    set_message("Jump back: " + fs::path(loc.filepath).filename().string() + ":"
                + std::to_string(loc.cursor.y + 1));
  }
}

void Editor::jump_forward()
{
  if (jump_history.empty() || jump_index < 0)
  {
    set_message("No jump history yet");
    return;
  }
  if (jump_index + 1 >= (int)jump_history.size())
  {
    set_message("End of jump history");
    return;
  }
  jump_index++;
  const JumpLocation loc = jump_history[(size_t)jump_index];
  if (jump_to(loc))
  {
    set_message("Jump forward: " + fs::path(loc.filepath).filename().string() + ":"
                + std::to_string(loc.cursor.y + 1));
  }
}

void Editor::show_jumplist_picker()
{
  std::vector<QuickPickItem> items;
  // Newest first: the place you just left is the one you are most likely to want.
  for (int i = (int)jump_history.size() - 1; i >= 0; i--)
  {
    const JumpLocation &loc = jump_history[(size_t)i];
    QuickPickItem item;
    item.filepath = loc.filepath;
    item.line = std::max(0, loc.cursor.y);
    item.col = std::max(0, loc.cursor.x);
    item.label = fs::path(loc.filepath).filename().string();
    item.detail = loc.filepath + ":" + std::to_string(loc.cursor.y + 1) + ":"
                  + std::to_string(loc.cursor.x + 1) + (i == jump_index ? "  (current)" : "");
    // The source line, when the file happens to be open; a closed file's preview
    // would mean reading it from disk on a keystroke.
    for (const auto &buf : buffers)
    {
      if (buf.filepath == loc.filepath && loc.cursor.y >= 0 && loc.cursor.y < (int)buf.line_count())
      {
        item.preview = string_util::trim_copy(buf.line(loc.cursor.y));
        break;
      }
    }
    items.push_back(std::move(item));
  }
  open_quick_pick(QUICK_PICK_JUMPLIST, "Jumplist", std::move(items));
  if (quick_pick_all_items.empty())
  {
    set_message("No jump history yet");
  }
}
