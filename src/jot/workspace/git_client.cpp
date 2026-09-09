// lazygit integration (src/jot/workspace/git_client.cpp).
//
// A deliberately small file: :lazygit launches the lazygit TUI inside jot's
// integrated terminal, rooted at the workspace (or the current file's
// directory when jot was started on a single file). lazygit is an external
// binary — jot never vendors it — so when it is missing the command says so
// and points at the official install paths.

#include "editor.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace
{
  namespace fs = std::filesystem;

  // True when an executable named `name` exists in any PATH entry.
  bool executable_on_path(const std::string &name)
  {
    const char *path_env = std::getenv("PATH");
    if (!path_env || !*path_env)
    {
      return false;
    }
    const char sep =
#ifdef _WIN32
        ';'
#else
        ':'
#endif
        ;
    std::istringstream iss(path_env);
    std::string dir;
    while (std::getline(iss, dir, sep))
    {
      std::error_code ec;
      const fs::path candidate = fs::path(dir) / name;
      if (fs::exists(candidate, ec) && !fs::is_directory(candidate, ec))
      {
        return true;
      }
    }
    return false;
  }
} // namespace

void Editor::open_git_client()
{
  // Anchor the terminal at the workspace root; when jot was started on a
  // single file (no workspace), fall back to that file's directory.
  std::string cwd = root_dir;
  if (cwd.empty() && current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    const std::string &filepath = buffers[(size_t)current_buffer].filepath;
    if (!filepath.empty())
    {
      cwd = fs::path(filepath).parent_path().string();
    }
  }
  if (cwd.empty())
  {
    set_message("Open a workspace or file first, then run :lazygit");
    return;
  }

  if (!executable_on_path("lazygit"))
  {
    set_message("lazygit not found — install it (brew install lazygit, or "
                "https://github.com/jesseduffield/lazygit)");
    return;
  }

  // Reuse the running lazygit tab instead of stacking a new one per call.
  for (int i = 0; i < (int)integrated_terminals.size(); i++)
  {
    if (integrated_terminals[i] && integrated_terminals[i]->get_label() == "lazygit")
    {
      activate_integrated_terminal(i, true);
      set_message("lazygit already open — focused");
      needs_redraw = true;
      return;
    }
  }

  create_integrated_terminal("lazygit", cwd);
  IntegratedTerminal *term = get_integrated_terminal();
  if (!term || !term->is_active())
  {
    set_message("Failed to open the terminal for lazygit");
    return;
  }
  term->send_text("lazygit\r");
  set_message("lazygit — quit with q to return to jot");
  needs_redraw = true;
}