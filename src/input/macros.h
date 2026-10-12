// ---------------------------------------------------------------------------
// Macros: record, replay and list the keystroke registers (input/macros.cpp)
// ---------------------------------------------------------------------------
//
// A macro is the keystrokes between "start" and "stop", replayed through the
// frontend's key path. The recorder is fed from the one place every key passes
// through -- the EVENT_KEY branch of handle_terminal_event, before any mode can
// consume the key -- so a recording holds chords a Lua keymap handled as well as
// plain typing.
//
// A fragment of the Editor class body, included by src/core/editor.h. It is not
// a standalone header: no include guard, no includes, and the members sit in
// class scope exactly as if they were written in editor.h.
private:
// Called once per key from the frontend's key path, first thing: appends the
// key to the register being recorded. The macro chords themselves are filtered
// out (recording "Alt+Z q" would stop the recording when it is replayed).
void macro_note_key(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch);
// The native Alt+Z prefix: "q" records/stops, "p" plays the last register,
// "l" lists them. Returns true when the key was consumed. The Lua keymap
// (runtime/lua/features/keymaps.lua) is consulted before this and handles the
// same three chords in a build that has Lua.
bool handle_macro_key(int ch, bool is_ctrl, bool is_shift, bool is_alt);
// Starts a recording, or stops the one running (`:macrorecord`, `Alt+Z q`).
// An empty name keeps the current register.
void macro_record_toggle(const std::string &register_name);
void macro_stop();
// Replays one register `count` times (`:macroplay`, `Alt+Z p`). An empty name
// plays the last register played, else the one recordings go to. `count` <= 0
// means once. Refuses while a replay is in flight.
bool macro_play(const std::string &register_name, int count, bool announce);
// "Macros: @q 12 keys · @a 3 keys" (or the empty story) for `:macrolist` and
// the status line.
std::string macro_list_text() const;
