// Editor-side LSP signature help: detecting when the caret is inside a call's
// argument list, requesting fresh help, and adopting the server's answer.
#include "editor.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include <algorithm>
#include <string>

namespace
{
  // Column of the innermost '(' still open just before `before_x` on the
  // buffer's cursor line, or -1 when the caret is not inside a call's
  // argument list. Nested calls resolve to their innermost open paren so a
  // signature popup tracks the call actually being typed.
  int innermost_open_paren_col(const FileBuffer &buf, int before_x)
  {
    if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
    {
      return -1;
    }
    const std::string &line = buf.line(buf.cursor.y);
    int limit = std::clamp(before_x, 0, (int)line.size());
    int depth = 0;
    for (int i = limit - 1; i >= 0; i--)
    {
      const char c = line[i];
      if (c == ')')
      {
        depth++;
      }
      else if (c == '(')
      {
        if (depth > 0)
        {
          depth--;
        }
        else
        {
          return i;
        }
      }
    }
    return -1;
  }
} // namespace

void Editor::hide_lsp_signature()
{
  if (!lsp_signature_visible && lsp_signature_filepath.empty())
  {
    return;
  }
  lsp_signature_visible = false;
  lsp_signature_open_paren_line = -1;
  lsp_signature_open_paren_col = 0;
  lsp_signature_filepath.clear();
  lsp_signature_result = {};
}

void Editor::refresh_lsp_signature_if_in_call()
{
  auto &buf = get_buffer();
  if (buf.is_lazy() || buf.filepath.empty() || lsp_signature_visible)
  {
    return; // an active popup is already tracking its call
  }
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
  {
    return;
  }
  // Only auto-show inside a call's argument list, and not while the user is
  // still typing the callee name right before the '(' — require the caret to
  // sit past the open paren with the call text already behind it.
  const int open_col = innermost_open_paren_col(buf, buf.cursor.x);
  if (open_col < 0)
  {
    return;
  }
  const std::string &line = buf.line(buf.cursor.y);
  const int text_before = buf.cursor.x - (open_col + 1);
  if (text_before < 1)
  {
    // Directly after '(' with nothing typed yet: the explicit '(' trigger
    // already fired, so leave it alone rather than pester the server.
    return;
  }
  bool has_arg_char = false;
  for (int i = open_col + 1; i < buf.cursor.x && i < (int)line.size(); i++)
  {
    if (line[i] != ' ' && line[i] != '\t')
    {
      has_arg_char = true;
      break;
    }
  }
  if (!has_arg_char)
  {
    return;
  }
  request_lsp_signature_help();
}

void Editor::request_lsp_signature_help(char trigger_character)
{
  auto &buf = get_buffer();
  if (buf.is_lazy() || buf.filepath.empty())
  {
    return;
  }
  // Only useful while the caret sits inside a call's argument list (right
  // after an unmatched '(' on the current line).
  const int open_col = innermost_open_paren_col(buf, buf.cursor.x);
  if (open_col < 0)
  {
    hide_lsp_signature();
    return;
  }
  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    return;
  }
  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_signature_help(buf.filepath, buf.cursor.y, buf.cursor.x, trigger_character))
  {
    return;
  }
  // Keep whatever is on screen until a fresh answer lands (or the caret moves
  // out of the call), so retyping an argument does not make the popup blink.
  lsp_signature_visible = true;
  lsp_signature_open_paren_line = buf.cursor.y;
  lsp_signature_open_paren_col = open_col;
  lsp_signature_filepath = buf.filepath;
  needs_redraw = true;
}

void Editor::handle_lsp_signature_result(const LSPSignatureHelpResult &signature_help)
{
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  if (lsp_signature_filepath.empty())
  {
    return; // no outstanding request: the popup was already dismissed
  }
  auto &buf = get_buffer();
  if (!lsp_internal::same_path(buf.filepath, signature_help.origin_filepath))
  {
    return;
  }
  // Only adopt the answer when the caret is still inside the call that asked
  // for it (past the recorded open paren, same line).
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
  {
    return;
  }
  if (buf.cursor.y != lsp_signature_open_paren_line)
  {
    hide_lsp_signature();
    return;
  }
  const std::string &line = buf.line(buf.cursor.y);
  if (lsp_signature_open_paren_col >= (int)line.size()
      || line[(size_t)lsp_signature_open_paren_col] != '(' || buf.cursor.x <= lsp_signature_open_paren_col)
  {
    hide_lsp_signature();
    return;
  }
  if (signature_help.signatures.empty())
  {
    hide_lsp_signature();
    return;
  }
  lsp_signature_result = signature_help;
  lsp_signature_visible = true;
  needs_redraw = true;
}