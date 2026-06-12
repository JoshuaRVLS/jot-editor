#include "editor.h"
#include "tree_sitter_install.h"

#include <algorithm>

bool Editor::install_tree_sitter_language(const std::string &language) {
  TreeSitterInstallCommand install =
      TreeSitterInstall::command_for_language(language);
  if (!install.supported) {
    set_message(install.message);
    return false;
  }

  create_integrated_terminal("tsinstall:" + install.language);
  IntegratedTerminal *term = get_integrated_terminal();
  if (!term || !term->is_active()) {
    set_message("Failed to open Tree-sitter install terminal");
    return false;
  }

  term->send_text(install.command + "\r");
  set_message(install.message);
  needs_redraw = true;
  return true;
}

void Editor::show_tree_sitter_status() {
  FileBuffer &buf = get_buffer();
  if (!buf.filepath.empty() && buf.line_count() > 0) {
    int line_idx = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
    get_line_syntax_colors(buf, line_idx);
  }

  if (buf.syntax_engine == SYNTAX_ENGINE_TREESITTER) {
    std::string label =
        buf.syntax_language_label.empty() ? "tree-sitter"
                                          : buf.syntax_language_label;
    set_message("Tree-sitter active: " + label);
  } else if (buf.syntax_engine == SYNTAX_ENGINE_REGEX) {
    std::string label =
        buf.syntax_language_label.empty() ? "regex" : buf.syntax_language_label;
    set_message("Tree-sitter fallback: Regex " + label);
  } else {
    set_message("Tree-sitter inactive: Syntax off");
  }
}
