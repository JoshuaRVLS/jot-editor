#ifndef JOT_RELAUNCH_H
#define JOT_RELAUNCH_H

// Self-restart support, used by the Lua :update flow to swap in a freshly
// rebuilt binary: after a successful pull + rebuild + install, the editor
// replaces itself so the new build loads immediately.
namespace relaunch
{
  // Called from main() before the editor changes anything (cwd, open files),
  // so the original invocation can be replayed later.
  void capture_startup(int argc, char *argv[]);

  // Replaces the current process with a fresh jot on POSIX (exec, keeping the
  // same terminal and pid), or spawns a new instance on Windows (the caller
  // then stops the event loop). Replays the captured executable + args.
  // Returns true when a new instance was started; false when restart is
  // unsupported or spawning failed.
  bool restart_self();
} // namespace relaunch

#endif
