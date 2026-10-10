// The editor's half of Emmet: find the abbreviation at the cursor, ask the pure
// expander (features/emmet.*) what it means, and hand the result to the snippet
// engine so it lands with its tabstops alive.
//
// Everything that decides *whether* to expand lives here rather than in the
// expander, because it is about the editor: the setting, the file type, and the
// cursor's position on the line. The expander only knows about text, and refuses
// anything it cannot parse -- which is the property the caller depends on, since
// this runs from Tab and a refusal has to leave ordinary indentation alone.
#include "core/editor.h"
#include "extras/emmet/expand.h"
#include "lua/api.h"
#include "text_features.h"
#include "ui/text.h"
#include "workspace/html.h"

bool Editor::expand_emmet_abbreviation()
{
  if (!config.get_bool("emmet", true))
    return false;

  auto &buf = get_buffer();
  if (buf.filepath.empty() || !Emmet::is_supported_file(buf.filepath))
    return false;
  if (!lua_api)
    return false;
  if (buf.is_lazy())
    buf.materialize();
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size())
    return false;

  const bool css = Emmet::is_css_file(buf.filepath);
  const bool jsx = HtmlFeatures::is_jsx_extension(buf.filepath);
  const std::string &line = buf.line(buf.cursor.y);
  const int cursor = ui_clamp_to_utf8_boundary(line, buf.cursor.x);
  const std::string abbr = Emmet::abbreviation_before(line, cursor, css);
  if (abbr.empty())
    return false;

  // One level of indentation for the lines a markup expansion adds, and the
  // current line's own leading whitespace for a CSS chain's continuation lines.
  const std::string indent_unit = EditorFeatures::get_indent_string(tab_size, tab_size);
  std::string leading;
  for (char c : line)
  {
    if (c != ' ' && c != '\t')
      break;
    leading += c;
  }

  Emmet::Expansion expansion;
  const bool ok = css ? Emmet::expand_css(abbr, leading, expansion)
                      : Emmet::expand_markup(abbr, indent_unit, jsx, expansion);
  if (!ok || expansion.snippet.empty())
    return false;

  // The snippet engine replaces the abbreviation and expands the body, so the
  // `$0` and `${n}` stops in it are live: that is the whole reason the expander
  // emits snippet text rather than plain text. Its positions are 1-based and
  // end-exclusive, the same convention the LSP snippet path uses.
  return lua_api->run_lsp_snippet_handler(expansion.snippet,
                                          buf.cursor.y + 1,
                                          cursor - (int)abbr.size() + 1,
                                          buf.cursor.y + 1,
                                          cursor + 1);
}
