#include "editor.h"
#include "tree_sitter/install.h"
#include "tree_sitter/catalog.h"

#include <algorithm>
#include <cctype>

namespace {
std::string trim_copy(const std::string &s) {
  size_t a = 0;
  while (a < s.size() && std::isspace((unsigned char)s[a])) {
    ++a;
  }
  size_t b = s.size();
  while (b > a && std::isspace((unsigned char)s[b - 1])) {
    --b;
  }
  return s.substr(a, b - a);
}

bool parse_tree_sitter_marker(const std::string &line, std::string &phase,
                              std::string &language) {
  const std::string marker = "[jot:treesitter] ";
  size_t pos = line.find(marker);
  if (pos == std::string::npos) {
    return false;
  }
  std::string rest = trim_copy(line.substr(pos + marker.size()));
  size_t space = rest.find(' ');
  if (space == std::string::npos) {
    phase = rest;
    language.clear();
    return true;
  }
  phase = rest.substr(0, space);
  language = trim_copy(rest.substr(space + 1));
  size_t exit_pos = language.find(" exit=");
  if (exit_pos != std::string::npos) {
    language.erase(exit_pos);
  }
  return true;
}
} // namespace

bool Editor::install_tree_sitter_language(const std::string &language) {
  TreeSitterInstallCommand install =
      TreeSitterInstall::command_for_language(language);
  if (!install.supported) {
    set_message(install.message);
    return false;
  }

  show_tree_sitter_status_modal = true;
  tree_sitter_status_scroll = 0;
  create_integrated_terminal("tsinstall:" + install.language);
  int terminal_index = current_integrated_terminal;
  IntegratedTerminal *term = get_integrated_terminal(terminal_index);
  if (!term || !term->is_active()) {
    set_message("Failed to open Tree-sitter install terminal");
    return false;
  }

  activate_integrated_terminal(terminal_index, false);
  auto existing = std::find_if(
      tree_sitter_install_jobs.begin(), tree_sitter_install_jobs.end(),
      [&](const TreeSitterInstallJob &job) {
        return job.language == install.language && job.running;
      });
  if (existing != tree_sitter_install_jobs.end()) {
    existing->terminal_index = terminal_index;
    existing->progress = "starting";
    existing->failed = false;
    existing->succeeded = false;
  } else {
    TreeSitterInstallJob job;
    job.language = install.language;
    job.terminal_index = terminal_index;
    job.progress = "starting";
    tree_sitter_install_jobs.push_back(std::move(job));
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

#ifdef JOT_TREESITTER
  const std::string ext = tree_sitter_extension_for_buffer(buf);
  TreeSitterRuntimeStatus status = ts_manager_.runtime_status_for_extension(ext);
#endif
  if (buf.syntax_engine == SYNTAX_ENGINE_TREESITTER) {
    std::string label =
        buf.syntax_language_label.empty() ? "tree-sitter"
                                          : buf.syntax_language_label;
    std::string message = "Tree-sitter active: " + label;
#ifdef JOT_TREESITTER
    if (status.used_builtin_query &&
        status.query_message.find("runtime query failed") != std::string::npos) {
      message += " (" + status.query_message + ")";
    }
#endif
    set_message(message);
  } else if (buf.syntax_engine == SYNTAX_ENGINE_REGEX) {
    std::string label =
        buf.syntax_language_label.empty() ? "regex" : buf.syntax_language_label;
    std::string message = "Tree-sitter fallback: Regex " + label;
#ifdef JOT_TREESITTER
    if (status.has_language) {
      message += " (" + status.language_id + ": ";
      if (!status.parser_loaded) {
        message += status.parser_message;
      } else if (!status.query_loaded) {
        message += status.query_message;
      } else {
        message += "Tree-sitter unavailable";
      }
      message += ")";
    }
#endif
    set_message(message);
  } else {
    set_message("Tree-sitter inactive: Syntax off");
  }
  show_tree_sitter_status_modal = true;
  tree_sitter_status_scroll = 0;
  needs_redraw = true;
}

void Editor::reload_tree_sitter() {
#ifdef JOT_TREESITTER
  for (auto &buf : buffers) {
    if (buf.ts_tree) {
      ts_tree_delete(buf.ts_tree);
      buf.ts_tree = nullptr;
    }
    if (buf.ts_parser) {
      ts_parser_delete(buf.ts_parser);
      buf.ts_parser = nullptr;
    }
    buf.ts_language_id.clear();
    invalidate_syntax_cache(buf);
  }
  ts_manager_.reload();
  set_message("Tree-sitter reloaded");
#else
  set_message("Tree-sitter inactive: runtime not available");
#endif
}

void Editor::poll_tree_sitter_installs() {
  bool changed = false;
  for (auto &job : tree_sitter_install_jobs) {
    if (!job.running) {
      continue;
    }
    IntegratedTerminal *term = get_integrated_terminal(job.terminal_index);
    if (!term) {
      job.running = false;
      job.failed = true;
      job.progress = "terminal closed";
      changed = true;
      continue;
    }

    std::vector<std::string> lines = term->get_recent_lines(80);
    for (const auto &line : lines) {
      std::string phase;
      std::string lang;
      if (!parse_tree_sitter_marker(line, phase, lang)) {
        continue;
      }
      if (!lang.empty() && TreeSitterCatalog::normalize_language_name(lang) !=
                             job.language) {
        continue;
      }
      if (phase == "start") {
        job.progress = "starting";
      } else if (phase == "clone") {
        job.progress = "cloning";
      } else if (phase == "build") {
        job.progress = "building";
      } else if (phase == "link") {
        job.progress = "linking parser";
      } else if (phase == "query") {
        job.progress = "installing queries";
      } else if (phase == "success") {
        job.progress = "installed";
        job.running = false;
        job.succeeded = true;
        job.failed = false;
#ifdef JOT_TREESITTER
        reload_tree_sitter();
        for (auto &buf : buffers) {
          if (!buf.filepath.empty()) {
            init_ts_for_buffer(buf);
          }
        }
#endif
        set_message("Tree-sitter installed: " + job.language);
      } else if (phase == "failed") {
        job.progress = "failed";
        job.running = false;
        job.failed = true;
        job.succeeded = false;
        set_message("Tree-sitter install failed: " + job.language);
      }
      changed = true;
    }

    if (job.running && !term->is_active()) {
      job.running = false;
      if (!job.succeeded) {
        job.failed = true;
        job.progress = "failed";
      }
      changed = true;
    }
  }

  if (changed || show_tree_sitter_status_modal) {
    needs_redraw = true;
  }
}

bool Editor::handle_tree_sitter_status_input(int ch) {
  if (!show_tree_sitter_status_modal) {
    return false;
  }
  if (ch == 27 || ch == 'q' || ch == 'Q') {
    show_tree_sitter_status_modal = false;
    needs_redraw = true;
    return true;
  }
  int delta = 0;
  if (ch == 1008 || ch == 'k' || ch == 'K') {
    delta = -1;
  } else if (ch == 1009 || ch == 'j' || ch == 'J') {
    delta = 1;
  } else if (ch == 1001) {
    delta = 8;
  } else if (ch == 1012) {
    tree_sitter_status_scroll = 0;
    needs_redraw = true;
    return true;
  } else if (ch == 1013) {
    tree_sitter_status_scroll = 1000000;
    needs_redraw = true;
    return true;
  }
  if (delta != 0) {
    tree_sitter_status_scroll =
        std::max(0, tree_sitter_status_scroll + delta);
    needs_redraw = true;
    return true;
  }
  return true;
}
