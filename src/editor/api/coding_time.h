// ---------------------------------------------------------------------------
// Coding time (the statusline chip, WakaTime, and the local store)
// ---------------------------------------------------------------------------
//
// One chip reports how long was coded today in the workspace in front of the
// user, and two sources can answer for it: WakaTime's own total for today, read
// out of wakatime-cli (features/wakatime.h), or the local store kept beside the
// config (features/coding_time.h) for when the integration is off, has not
// answered, or has no account to answer with. The Extensions category in
// :settings is where WakaTime is turned on; the local store runs either way,
// because it is what the chip falls back to.
//
// The cli behind all of it is installed on the first enable when PATH has none
// (features/wakatime.h writes the script, the install runs off the keystroke
// path, and the message line says what came of it), and every run after that
// goes to the same managed copy until the user deletes it.
//
// This file is jot/app/coding_time.cpp -- the two sources share the ticks the
// editor hands out (a keypress, a save, a file switch, and the frame loop's
// once-a-second pass), which is why they share a module.
private:
// The single entry point the editor's hooks use: a keypress, a save, a file
// switch. The local total is credited first and with no conditions, because
// with WakaTime off it is what the chip reports -- it has to have been
// counting all along for the fallback to mean anything.
void note_editing_activity(bool is_write);

// The cli's arguments, from the settings in force: read live rather than
// mirrored, so a key typed into :settings takes effect on the next heartbeat.
jot_wakatime::Options wakatime_options();

// `wakatime-cli <args>`, values already quoted by the pure half, run from
// wherever the check above found the binary: PATH, or the managed copy.
std::string wakatime_command(const std::vector<std::string> &args) const;

// Applies the `wakatime` setting: called from apply_config_live (which runs
// before the worker queue exists) and again from run() (after it), so the
// one-time cli check -- which installs the cli when PATH has none -- and the
// api-key check can wait for the queue they need. Idempotent, and a re-enable
// probes again.
void sync_wakatime();

// One heartbeat, when the plugin spec's rule says the event deserves one.
// Runs the cli detached on the worker queue: nobody waits on the answer, and
// a heartbeat that blocked for its timeout would hold the queue (git status,
// LSP scans) for as long as the network felt like it.
void wakatime_heartbeat(bool is_write, long long now_ms);

// Asks the cli for today's total when the cached one is a minute old, at most
// one request at a time. Driven from the frame loop's own gate
// (status_time_due_soon), which is where an answer becomes a repaint.
void wakatime_poll_today();

// The store key for the workspace in front of the user, today: what the
// counter below belongs to. A fresh string each call, since both halves of it
// can move under the editor (the workspace by opening one, the day by
// midnight).
std::string coding_time_current_key() const;

// Folds the time since the last look into the local total for the workspace
// and day in front of the user, rolling the key over at midnight or when the
// workspace changes, and writing the file once it has moved. `activity` says
// this is a tick from the user rather than the frame loop's own pass.
void coding_time_tick(long long now_ms, bool activity);

// Writes the totals out, at most once a minute unless `force` (shutdown, and
// the moment the key rolls over, both of which want it on disk at once).
void flush_coding_time(long long now_ms, bool force);

// The chip's label: WakaTime's total for today when the integration has
// answered, the locally stored total for this workspace otherwise.
std::string status_coding_label(long long now_ms);
