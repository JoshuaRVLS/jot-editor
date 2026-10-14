#ifndef SIGNAL_EXIT_H
#define SIGNAL_EXIT_H

// Leaving because a signal said so, rather than the user typing :q.
//
// SIGTERM, SIGINT and SIGHUP mean the session is over -- a service manager
// stopping the process, a shell closing the terminal, a kill from a script. The
// default action is to die on the spot, which loses everything the editor holds
// only in memory: the coding-time total for the day, the workspace session the
// next launch restores, and the terminal's own state (the alternate screen, the
// cursor, the mouse and bracketed-paste modes).
//
// So the handler does the one thing a signal handler may safely do -- set a flag
// -- and the main loop's 50ms tick notices it and leaves through the door a quit
// uses, with the unwinding that writes all of that out.
//
// A second signal is not deferred again. Restoring the default action and
// re-raising means a session that cannot get out of its own way stays killable,
// which is the failure mode worth avoiding when a process suddenly ignores the
// signal it used to die from.
#include <atomic>
#include <csignal>

namespace signal_exit
{
  // The signals worth leaving for. SIGHUP is absent on Windows, and both
  // SIGINT and SIGTERM exist there (the CRT delivers them for Ctrl+C and for
  // whatever raises them), so the list is filtered rather than the module.
  inline constexpr int kSignals[] = {
#ifdef SIGHUP
      SIGHUP,
#endif
      SIGINT,
      SIGTERM,
  };
  inline constexpr int kSignalCount = (int)(sizeof(kSignals) / sizeof(kSignals[0]));

  // Set from the handler and read by the loop. A bool atomic is lock-free on
  // every platform the editor builds for, which is what makes touching it from
  // a signal handler safe.
  inline std::atomic<bool> &requested_flag()
  {
    static std::atomic<bool> flag{false};
    return flag;
  }

  inline bool requested() { return requested_flag().load(std::memory_order_relaxed); }

  inline void mark_requested() { requested_flag().store(true, std::memory_order_relaxed); }

  extern "C" inline void jot_signal_exit_handler(int sig)
  {
    if (requested())
    {
      std::signal(sig, SIG_DFL);
      std::raise(sig);
      return;
    }
    mark_requested();
  }

  // Installs the handlers. Returns false when one of them could not be
  // installed, which a caller is free to ignore: the session works either way,
  // it just loses this on the way out.
  inline bool install()
  {
    bool ok = true;
    for (int i = 0; i < kSignalCount; i++)
    {
      if (std::signal(kSignals[i], jot_signal_exit_handler) == SIG_ERR)
      {
        ok = false;
      }
    }
    return ok;
  }
} // namespace signal_exit

#endif // SIGNAL_EXIT_H
