// Editor-side LSP status: the modal listing running/starting clients, its
// input handling, the install-usage hint, and per-language diagnostic counts.
#include "editor.h"
#include "jot/integrations/lsp/common.h"
#include <algorithm>
#include <string>
#include <vector>

void Editor::open_lsp_status_modal()
{
  show_lsp_status_modal = true;
  lsp_status_scroll = 0;
  needs_redraw = true;
}

void Editor::show_lsp_status()
{
  int running_clients = 0;
  int attaching = 0;
  std::vector<std::string> names;
  for (const auto &client : lsp_clients)
  {
    if (!client)
    {
      continue;
    }
    if (client->is_running())
    {
      running_clients++;
    }
    else
    {
      attaching++;
    }
    if (names.size() < 4 && !client->get_language().empty())
    {
      names.push_back(client->get_language());
    }
  }

  std::string message;
  if (running_clients == 0 && attaching == 0)
  {
    message = "LSP inactive: no language servers running";
  }
  else
  {
    message = "LSP: " + std::to_string(running_clients) + " running";
    if (attaching > 0)
    {
      message += ", " + std::to_string(attaching) + " starting";
    }
    if (!names.empty())
    {
      message += " (";
      for (size_t i = 0; i < names.size(); i++)
      {
        if (i > 0)
        {
          message += ", ";
        }
        message += names[i];
      }
      message += ")";
    }
  }
  set_message(message);
  open_lsp_status_modal();
}

bool Editor::handle_lsp_status_input(int ch)
{
  if (!show_lsp_status_modal)
  {
    return false;
  }
  if (ch == 27 || ch == 'q' || ch == 'Q')
  {
    show_lsp_status_modal = false;
    needs_redraw = true;
    return true;
  }
  int delta = 0;
  if (ch == 1008 || ch == 'k' || ch == 'K')
  {
    delta = -1;
  }
  else if (ch == 1009 || ch == 'j' || ch == 'J')
  {
    delta = 1;
  }
  else if (ch == 1001)
  {
    delta = 8;
  }
  else if (ch == 1012)
  {
    lsp_status_scroll = 0;
    needs_redraw = true;
    return true;
  }
  else if (ch == 1013)
  {
    lsp_status_scroll = 1000000;
    needs_redraw = true;
    return true;
  }
  if (delta != 0)
  {
    lsp_status_scroll = std::max(0, lsp_status_scroll + delta);
    needs_redraw = true;
    return true;
  }
  return true;
}

std::string Editor::lsp_install_usage_hint() const
{
  return lsp_internal::lsp_server_usage_hint(lua_api);
}

void Editor::lsp_server_diagnostic_counts(const std::string &language,
                                          int *errors,
                                          int *warnings,
                                          int *infos,
                                          int *hints) const
{
  if (errors)
    *errors = 0;
  if (warnings)
    *warnings = 0;
  if (infos)
    *infos = 0;
  if (hints)
    *hints = 0;
  for (const auto &buf : buffers)
  {
    if (buf.filepath.empty() || lsp_internal::detect_lsp_language(buf.filepath) != language)
    {
      continue;
    }
    for (const auto &d : buf.diagnostics)
    {
      if (d.severity == 1 && errors)
        (*errors)++;
      else if (d.severity == 2 && warnings)
        (*warnings)++;
      else if (d.severity == 3 && infos)
        (*infos)++;
      else if (hints)
        (*hints)++;
    }
  }
}