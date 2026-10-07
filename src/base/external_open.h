// Handing a link to the desktop (src/base/external_open.h).
//
// Which browser a URL means is the desktop's decision, not an editor setting.
// The opener is backgrounded with its output dropped: xdg-open can sit there
// until the browser exits, and the frame loop must not wait for it.

#ifndef EXTERNAL_OPEN_H
#define EXTERNAL_OPEN_H

#include "base/shell_util.h"

#include <cstdlib>
#include <string>

namespace external_open
{
  // The platform's opener for `url`, or empty when the machine has none on
  // PATH -- the caller then reports that instead of pretending it opened.
  inline std::string url_command(const std::string &url)
  {
#ifdef _WIN32
    // `start` opens through the shell's own registration; the empty title
    // argument keeps a quoted URL from being read as the window title.
    return "cmd.exe /c start \"\" " + shell_util::shell_quote(url);
#elif defined(__APPLE__)
    if (shell_util::command_exists("open"))
      return "open " + shell_util::shell_quote(url);
    return std::string();
#else
    // xdg-open is the desktop's own handler; gio opens through the portal on
    // desktops that ship without xdg-utils.
    if (shell_util::command_exists("xdg-open"))
      return "xdg-open " + shell_util::shell_quote(url);
    if (shell_util::command_exists("gio"))
      return "gio open " + shell_util::shell_quote(url);
    return std::string();
#endif
  }

  // Dispatches the opener. False when there is none (the caller then reports
  // the URL instead of pretending); a launched opener is not waited for.
  inline bool open_url(const std::string &url)
  {
    const std::string command = url_command(url);
    if (command.empty())
      return false;
#ifdef _WIN32
    std::system((command + shell_util::detached_redirect()).c_str());
#else
    std::system((command + shell_util::detached_redirect() + " &").c_str());
#endif
    return true;
  }
} // namespace external_open

#endif // EXTERNAL_OPEN_H
