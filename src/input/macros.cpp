// Recordable macros: Alt+Z q starts and stops a recording, Alt+Z p replays it,
// :macrorecord / :macroplay / :macrolist reach the same state from the command
// line, and the status line shows the recorder while it is live.
//
// Recording happens in the frontend's key path (events/terminal_events.cpp,
// EVENT_KEY) rather than in handle_input, because that is the only point every
// key passes through: a chord a Lua keymap owns is consumed by the frontend
// and never reaches handle_input, so recording there would silently drop
// Alt+D f, Ctrl+W s and the rest of the chord families. Replay goes back
// through the same entry point for the same reason -- a replayed chord is
// matched by the keymap the live keystroke was matched by, including a user
// config's rebinding.
//
// A macro is therefore keys, not edits. It re-runs against whatever buffer is
// current when it is played, exactly like a keystroke recording in Vim.
#include "core/editor.h"
#include "ui/terminal.h"

#include <cctype>
#include <string>

namespace
{
  // A ceiling on one play, so a register that somehow loops (a keymap that
  // plays while a play is in flight) stops at a reported point instead of
  // freezing the editor.
  constexpr size_t kMaxMacroSteps = 200000;

  // The register a `:macrorecord <name>` / `:macroplay <name>` argument names:
  // the first non-space character, lowercased, when it is a letter.
  char macro_letter_from(const std::string &name)
  {
    for (char c : name)
    {
      if (std::isspace((unsigned char)c))
      {
        continue;
      }
      const char lower = (char)std::tolower((unsigned char)c);
      return (lower >= 'a' && lower <= 'z') ? lower : 0;
    }
    return 0;
  }

  std::string macro_register_name(char letter)
  {
    return std::string("@") + letter;
  }
} // namespace

void Editor::macro_note_key(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch)
{
  if (!macro_recording || macro_replaying)
  {
    return;
  }
  const bool is_z = ch == 'z' || ch == 'Z' || original_ch == 'z' || original_ch == 'Z';
  if (macro_skip_next_key)
  {
    // The letter after Alt+Z: the macro command itself, not part of the macro.
    // Consuming the flag here (rather than in the native prefix handler) is
    // what makes the filter work for both paths -- with Lua loaded the keymap
    // consumes the chord and never reaches that handler.
    macro_skip_next_key = false;
    return;
  }
  if (is_alt && !is_ctrl && is_z)
  {
    macro_skip_next_key = true;
    return;
  }
  macro_registers[macro_register].push_back(MacroKey{ch, is_ctrl, is_shift, is_alt});
}

bool Editor::handle_macro_key(int ch, bool is_ctrl, bool is_shift, bool is_alt)
{
  (void)is_shift;
  const bool is_z = ch == 'z' || ch == 'Z';
  if (!macro_chord_armed)
  {
    if (is_alt && !is_ctrl && is_z)
    {
      macro_chord_armed = true;
      set_message("Macro: q record/stop · p play · l list", true);
      needs_redraw = true;
      return true;
    }
    return false;
  }
  macro_chord_armed = false;
  if (is_alt || is_ctrl)
  {
    // A chord after Alt+Z is not a macro command: let it through untouched
    // rather than eating a global shortcut.
    return false;
  }
  if (ch == 27)
  {
    needs_redraw = true;
    return true;
  }
  switch ((char)std::tolower((unsigned char)ch))
  {
  case 'q':
    macro_record_toggle("");
    return true;
  case 'p':
    macro_play("", 1, true);
    return true;
  case 'l':
    set_message(macro_list_text(), true);
    return true;
  default:
    set_message("Macro: Alt+Z q records · p plays · l lists", true);
    return true;
  }
}

void Editor::macro_record_toggle(const std::string &register_name)
{
  if (macro_recording)
  {
    macro_stop();
    return;
  }
  const char named = macro_letter_from(register_name);
  if (!register_name.empty() && named == 0)
  {
    set_message("Macro: name a register a-z", true);
    return;
  }
  if (named != 0)
  {
    macro_register = named;
  }
  macro_registers[macro_register].clear();
  macro_recording = true;
  macro_skip_next_key = false;
  needs_redraw = true;
  set_message("Recording " + macro_register_name(macro_register)
                  + " -- Alt+Z q to stop, Alt+Z p to play",
              true);
}

void Editor::macro_stop()
{
  if (!macro_recording)
  {
    return;
  }
  macro_recording = false;
  macro_chord_armed = false;
  macro_skip_next_key = false;
  needs_redraw = true;
  const size_t keys = macro_registers[macro_register].size();
  if (keys == 0)
  {
    set_message("Macro: nothing recorded", true);
    return;
  }
  macro_last = macro_register;
  set_message("Recorded " + std::to_string(keys) + " key" + (keys == 1 ? "" : "s") + " as "
                  + macro_register_name(macro_register),
              true);
}

bool Editor::macro_play(const std::string &register_name, int count, bool announce)
{
  const char named = macro_letter_from(register_name);
  if (!register_name.empty() && named == 0)
  {
    set_message("Macro: name a register a-z", true);
    return false;
  }
  char target = named;
  if (target == 0)
  {
    target = macro_last != 0 ? macro_last : macro_register;
  }
  const auto found = macro_registers.find(target);
  if (found == macro_registers.end() || found->second.empty())
  {
    set_message("Macro " + macro_register_name(target) + " is empty", true);
    return false;
  }
  if (macro_replaying)
  {
    set_message("Macro: already replaying", true);
    return false;
  }
  // Playing a register while one is being recorded commits the recording first:
  // the keys of the replay must not land in it, and a user who plays to check
  // what they have then wants a finished take, not one that keeps growing.
  if (macro_recording)
  {
    macro_stop();
  }
  if (count <= 0)
  {
    count = 1;
  }

  // The palette is where a typed `:macroplay` comes from, and it still owns the
  // keyboard while its command runs (the palette closes after
  // execute_ex_command returns): a replay that started there would type its
  // keys into the palette's query instead of the buffer. The command is running
  // now, so the palette has nothing left to ask and the keys belong to the
  // replay. The checks above have already run, so a refused play still leaves
  // the palette up with its line to fix.
  if (show_command_palette)
  {
    show_command_palette = false;
    needs_redraw = true;
  }

  // A copy: the replay can start another recording (which clears a register)
  // or be re-run while an edit to the register is pending.
  const std::vector<MacroKey> keys = found->second;
  macro_replaying = true;
  size_t steps = 0;
  bool overflow = false;
  bool quit = false;
  for (int run = 0; run < count && !overflow; run++)
  {
    for (const MacroKey &key : keys)
    {
      if (++steps > kMaxMacroSteps)
      {
        overflow = true;
        break;
      }
      Event ev{};
      ev.type = EVENT_KEY;
      ev.key = {key.ch, key.ctrl, key.shift, key.alt};
      handle_terminal_event(ev);
      if (!running)
      {
        // The macro quit the editor (a Ctrl+Q in the register). Nothing left to
        // replay, and nothing to report to a user who is on the way out.
        quit = true;
        break;
      }
    }
    if (quit)
    {
      break;
    }
  }
  macro_replaying = false;
  macro_last = target;
  needs_redraw = true;
  if (overflow)
  {
    set_message("Macro " + macro_register_name(target) + " stopped after "
                    + std::to_string(kMaxMacroSteps) + " keys",
                true);
  }
  else if (announce && !quit)
  {
    set_message("Played " + macro_register_name(target)
                    + (count > 1 ? " x" + std::to_string(count) : ""),
                true);
  }
  return true;
}

std::string Editor::macro_list_text() const
{
  if (macro_registers.empty())
  {
    return "Macros: none recorded (Alt+Z q to record)";
  }
  // A register that holds nothing is listed too (`@q 0`): it was named, and a
  // user asking for the list wants to know the recording came out empty.
  std::string text = "Macros:";
  for (const auto &entry : macro_registers)
  {
    text += " @" + std::string(1, entry.first) + " " + std::to_string(entry.second.size());
    if (entry.first == macro_last)
    {
      text += "*";
    }
  }
  return text;
}
