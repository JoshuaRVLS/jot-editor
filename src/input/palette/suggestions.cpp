#include "commands/utils.h"
#include "cpp_assist.h"
#include "editor.h"
#include "python_bridge/api.h"
#include "tree_sitter/install.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace CommandLineUtils;

namespace {
namespace fs = std::filesystem;

struct CommandMeta {
  const char *name;
  const char *category;
  const char *detail;
  int priority;
};

const std::vector<CommandMeta> &command_palette_metadata() {
  static const std::vector<CommandMeta> meta = {
      {"w", "File", "Save current file", 100},
      {"write", "File", "Save current file", 90},
      {"q", "File", "Quit, prompting if buffers are unsaved", 100},
      {"quit", "File", "Quit, prompting if buffers are unsaved", 90},
      {"wq", "File", "Save and quit", 95},
      {"e", "File", "Open file path", 95},
      {"edit", "File", "Open file path", 90},
      {"open", "File", "Open file path", 85},
      {"new", "File", "Create a new buffer", 80},
      {"bd", "File", "Close current buffer", 85},
      {"home", "Session", "Show home menu", 78},
      {"resume", "Session", "Resume last workspace session", 78},
      {"find", "File", "Open file finder at path", 85},
      {"ff", "File", "Open file finder at path", 80},
      {"grep", "Search", "Search text across workspace", 86},
      {"projectsearch", "Search", "Search text across workspace", 82},
      {"searchall", "Search", "Search text across workspace", 80},
      {"mkfile", "Workspace", "Create file in workspace", 80},
      {"mkdir", "Workspace", "Create folder in workspace", 80},
      {"cppimpl", "C++", "Generate missing method implementations", 82},
      {"cpppair", "C++", "Create matching C++ header/source files", 78},
      {"rename", "Workspace", "Rename file or folder", 80},
      {"rm", "Workspace", "Delete file or folder", 75},
      {"recent", "File", "Show recent files", 70},
      {"openrecent", "File", "Open recent file by number or name", 75},
      {"reopen", "File", "Reopen last closed tab", 70},
      {"split", "Pane", "Split pane right by default", 85},
      {"sp", "Pane", "Split pane right by default", 80},
      {"vsp", "Pane", "Vertical split", 80},
      {"splitleft", "Pane", "Split pane left", 75},
      {"splitright", "Pane", "Split pane right", 75},
      {"splitup", "Pane", "Split pane up", 75},
      {"splitdown", "Pane", "Split pane down", 75},
      {"wincmd", "Pane", "Focus pane by h/j/k/l direction", 80},
      {"focusleft", "Pane", "Focus pane left", 70},
      {"focusright", "Pane", "Focus pane right", 70},
      {"focusup", "Pane", "Focus pane up", 70},
      {"focusdown", "Pane", "Focus pane down", 70},
      {"resizeleft", "Pane", "Resize current pane left", 65},
      {"resizeright", "Pane", "Resize current pane right", 65},
      {"resizeup", "Pane", "Resize current pane up", 65},
      {"resizedown", "Pane", "Resize current pane down", 65},
      {"theme", "Appearance", "Apply color theme", 90},
      {"colorscheme", "Appearance", "Apply color theme", 80},
      {"minimap", "Appearance", "Toggle minimap", 70},
      {"term", "Terminal", "Open, focus, or hide terminal", 90},
      {"terminal", "Terminal", "Open, focus, or hide terminal", 80},
      {"termnew", "Terminal", "Create terminal tab", 85},
      {"task", "Terminal", "Run or list terminal tasks", 85},
      {"tasknew", "Terminal", "Run task in a fresh terminal tab", 80},
      {"taskrerun", "Terminal", "Rerun last task", 75},
      {"debug", "Debugger", "Launch program with GDB DAP", 86},
      {"debuggdb", "Debugger", "Launch program with GDB DAP", 84},
      {"debuglldb", "Debugger", "Launch program with LLDB DAP", 84},
      {"debugconfig", "Debugger", "Launch a debug.json session", 82},
      {"debugattach", "Debugger", "Attach debugger to PID", 80},
      {"debugpanel", "Debugger", "Toggle debugger panel", 78},
      {"debugstop", "Debugger", "Stop active debug session", 78},
      {"debugrestart", "Debugger", "Restart active debug session", 76},
      {"debugcontinue", "Debugger", "Continue active debug session", 78},
      {"debugpause", "Debugger", "Pause active debug session", 76},
      {"debugstep", "Debugger", "Step into", 76},
      {"debugnext", "Debugger", "Step over", 76},
      {"debugout", "Debugger", "Step out", 76},
      {"debugthreads", "Debugger", "Refresh threads and stack", 74},
      {"debugmemory", "Debugger", "Read memory", 72},
      {"debugdisasm", "Debugger", "Disassemble instructions", 72},
      {"search", "Edit", "Open search panel", 75},
      {"format", "Edit", "Format document", 75},
      {"trim", "Edit", "Trim trailing whitespace", 75},
      {"upper", "Edit", "Uppercase selection or word", 65},
      {"lower", "Edit", "Lowercase selection or word", 65},
      {"sortlines", "Edit", "Sort selected lines", 65},
      {"sortdesc", "Edit", "Sort selected lines descending", 60},
      {"reverselines", "Edit", "Reverse selected lines", 60},
      {"uniquelines", "Edit", "Deduplicate selected lines", 60},
      {"shufflelines", "Edit", "Shuffle selected lines", 60},
      {"joinlines", "Edit", "Join selected/current lines", 60},
      {"dupe", "Edit", "Duplicate selection or current line", 65},
      {"trimblank", "Edit", "Trim blank lines in selection", 60},
      {"replace", "Edit", "Replace text case-sensitively", 65},
      {"replacei", "Edit", "Replace text case-insensitively", 60},
      {"replaceword", "Edit", "Replace whole words", 60},
      {"replacere", "Edit", "Replace by regex", 60},
      {"surround", "Edit", "Surround selection or word", 60},
      {"unsurround", "Edit", "Remove surrounding pair", 60},
      {"fold", "Edit", "Collapse current block", 65},
      {"collapse", "Edit", "Collapse current block", 60},
      {"unfold", "Edit", "Expand current block", 65},
      {"expand", "Edit", "Expand current block", 60},
      {"togglefold", "Edit", "Toggle current fold", 68},
      {"foldall", "Edit", "Collapse all blocks", 58},
      {"unfoldall", "Edit", "Expand all blocks", 58},
      {"incnum", "Edit", "Increment number at cursor", 55},
      {"decnum", "Edit", "Decrement number at cursor", 55},
      {"copypath", "Clipboard", "Copy current file path", 60},
      {"copyname", "Clipboard", "Copy current file name", 60},
      {"datetime", "Insert", "Insert current date/time", 55},
      {"stats", "Info", "Show buffer statistics", 55},
      {"line", "Navigation", "Go to line[:column]", 80},
      {"goto", "Navigation", "Go to line[:column]", 75},
      {"lspstart", "LSP", "Start LSP for current file", 70},
      {"lspstatus", "LSP", "Show LSP status", 70},
      {"lspstop", "LSP", "Stop all LSP clients", 65},
      {"lsprestart", "LSP", "Restart all LSP clients", 65},
      {"lspinstall", "LSP", "Install LSP server", 65},
      {"lspremove", "LSP", "Remove LSP server", 65},
      {"lspmanager", "LSP", "Show LSP manager", 65},
      {"hover", "LSP", "Show hover information", 68},
      {"lsphover", "LSP", "Show hover information", 64},
      {"definition", "LSP", "Go to definition", 72},
      {"lspdefinition", "LSP", "Go to definition", 68},
      {"lspdef", "LSP", "Go to definition", 68},
      {"gd", "LSP", "Go to definition", 70},
      {"lspback", "LSP", "Return from LSP definition jump", 66},
      {"diagnostics", "Navigation", "Open diagnostics picker", 76},
      {"problems", "Navigation", "Open diagnostics picker", 74},
      {"diagnext", "Navigation", "Go to next diagnostic", 74},
      {"diagnosticnext", "Navigation", "Go to next diagnostic", 72},
      {"diagprev", "Navigation", "Go to previous diagnostic", 72},
      {"symbols", "Navigation", "Open document symbols", 78},
      {"outline", "Navigation", "Open document symbols", 76},
      {"tsinstall", "Tree-sitter", "Install Tree-sitter grammar", 68},
      {"treesitterinstall", "Tree-sitter", "Install Tree-sitter grammar", 64},
      {"tsstatus", "Tree-sitter", "Show syntax engine status", 66},
      {"tsreload", "Tree-sitter", "Reload parser libraries", 64},
      {"treesitterreload", "Tree-sitter", "Reload parser libraries", 60},
      {"gitstatus", "Git", "Show git status", 75},
      {"gitdiff", "Git", "Show git diff for file", 75},
      {"gitdiffstaged", "Git", "Show staged git diff for file", 74},
      {"gitstage", "Git", "Stage current or specified file", 74},
      {"gitunstage", "Git", "Unstage current or specified file", 74},
      {"gitstageall", "Git", "Stage all changes", 70},
      {"gitunstageall", "Git", "Unstage all changes", 70},
      {"gitcommit", "Git", "Commit staged changes", 70},
      {"gitlog", "Git", "Show recent commits", 68},
      {"gitblame", "Git", "Show blame for current line", 70},
      {"gitrefresh", "Git", "Refresh git status", 70},
      {"autosave", "Settings", "Configure auto-save", 70},
      {"help", "Help", "Show help or command list", 80},
      {"h", "Help", "Show help or command list", 70},
      {"gitdiffclose", "Git", "Close git diff panel", 68},
      {"gitdiffrefresh", "Git", "Refresh Open Git Diff", 68}
  };
  return meta;
}

const CommandMeta *find_command_meta(const std::string &name) {
  const std::string key = to_lower_copy(name);
  for (const auto &meta : command_palette_metadata()) {
    if (key == meta.name) {
      return &meta;
    }
  }
  return nullptr;
}

int fuzzy_score(const std::string &value, const std::string &query) {
  if (query.empty()) {
    return 10;
  }

  const std::string hay = to_lower_copy(value);
  const std::string needle = to_lower_copy(query);
  if (hay == needle) {
    return 10000;
  }
  if (starts_with_icase(value, query)) {
    return 8000 - (int)hay.size();
  }

  size_t pos = 0;
  int score = 4000;
  int streak = 0;
  for (char qc : needle) {
    bool found = false;
    for (; pos < hay.size(); pos++) {
      if (hay[pos] == qc) {
        score -= (int)pos;
        score += 20 + streak * 10;
        streak++;
        pos++;
        found = true;
        break;
      }
      streak = 0;
    }
    if (!found) {
      return -1;
    }
  }
  return score - (int)hay.size();
}

std::string strip_leading_colon(std::string s) {
  if (!s.empty() && s[0] == ':') {
    s.erase(s.begin());
  }
  return s;
}

} // namespace

void Editor::refresh_command_palette() {
  command_palette_results.clear();

  const std::string seed = command_palette_theme_mode
                               ? command_palette_theme_original
                               : command_palette_query;
  auto add_result = [&](const std::string &insert_text, std::string label,
                        std::string category, std::string detail, int score) {
    if (insert_text.empty()) {
      return;
    }
    if (label.empty()) {
      label = insert_text;
    }
    CommandPaletteSuggestion suggestion;
    suggestion.insert_text = insert_text;
    suggestion.label = std::move(label);
    suggestion.category = std::move(category);
    suggestion.detail = std::move(detail);
    suggestion.score = score;
    command_palette_results.push_back(std::move(suggestion));
  };
  auto complete_workspace_path = [&](const std::string &path_arg) {
    std::vector<std::string> out;
    std::error_code ec;

    std::string dir_part;
    std::string name_prefix = path_arg;
    size_t slash = path_arg.find_last_of("/\\");
    if (slash != std::string::npos) {
      dir_part = path_arg.substr(0, slash + 1);
      name_prefix = path_arg.substr(slash + 1);
    }

    fs::path base = root_dir.empty() ? fs::path(".") : fs::path(root_dir);
    fs::path list_dir = dir_part.empty() ? base : base / fs::path(dir_part);
    if (!fs::exists(list_dir, ec) || !fs::is_directory(list_dir, ec)) {
      return out;
    }

    for (const auto &entry : fs::directory_iterator(list_dir, ec)) {
      if (ec) {
        break;
      }
      std::string name = entry.path().filename().string();
      if (name.empty()) {
        continue;
      }
      if (!name_prefix.empty() && !starts_with_icase(name, name_prefix) &&
          fuzzy_score(name, name_prefix) < 0) {
        continue;
      }
      std::string suggestion = dir_part + name;
      if (entry.is_directory(ec)) {
        suggestion += "/";
      }
      out.push_back(suggestion);
      if (out.size() >= 64) {
        break;
      }
    }

    std::sort(out.begin(), out.end(),
              [](const std::string &a, const std::string &b) {
                return to_lower_copy(a) < to_lower_copy(b);
              });
    return out;
  };

  bool has_colon = !seed.empty() && seed[0] == ':';
  std::string body = has_colon ? seed.substr(1) : seed;
  std::string trimmed = trim_copy(body);

  std::string cmd;
  std::string arg;
  std::istringstream iss(trimmed);
  iss >> cmd;
  std::getline(iss, arg);
  arg = trim_copy(arg);

  if (cmd.empty() || (trimmed.find(' ') == std::string::npos &&
                      !(seed.size() > 0 && seed.back() == ' '))) {
    std::string needle = trim_copy(strip_leading_colon(seed));
    for (const auto &c : ex_commands()) {
      int score = fuzzy_score(c, needle);
      if (needle.empty() || score >= 0) {
        const CommandMeta *meta = find_command_meta(c);
        add_result(c, c, meta ? meta->category : "Command",
                   meta ? meta->detail : "Run command",
                   score + (meta ? meta->priority : 40));
      }
    }
    if (python_api) {
      for (const auto &command : python_api->commands()) {
        int score = fuzzy_score(command.name, needle);
        if (needle.empty() || score >= 0) {
          add_result(command.name, command.name, "Plugin",
                     command.detail.empty() ? "Run plugin command"
                                            : command.detail,
                     score + 75);
        }
      }
    }
  } else {
    const std::string lcmd = to_lower_copy(cmd);
    auto add_arg = [&](const std::string &value, const std::string &category,
                       const std::string &detail, int base_score = 100) {
      int score = fuzzy_score(value, arg);
      if (arg.empty() || score >= 0) {
        add_result(value, value, category, detail, score + base_score);
      }
    };
    if (lcmd == "theme" || lcmd == "colorscheme" || lcmd == "colo") {
      for (const auto &theme : list_available_themes()) {
        add_arg(theme, "Theme", "Apply color theme", 120);
      }
    } else if (lcmd == "openrecent") {
      for (int i = 0; i < (int)recent_files.size() && i < 12; i++) {
        std::string idx = std::to_string(i + 1);
        add_arg(idx, "Recent", get_filename(recent_files[i]), 130);

        std::string recent_name = get_filename(recent_files[i]);
        if (!recent_name.empty()) {
          add_arg(recent_name, "Recent", recent_files[i], 110);
        }
      }
    } else if (lcmd == "autosave") {
      const std::vector<std::string> opts = {"on",   "off",  "toggle",
                                             "status", "250", "500",
                                             "1000", "2000", "5000", "10000"};
      for (const auto &opt : opts) {
        add_arg(opt, "Auto-save", "Set auto-save mode or interval", 110);
      }
    } else if (lcmd == "wincmd") {
      const std::vector<std::string> opts = {"h", "j", "k", "l",
                                             "left", "down", "up", "right"};
      for (const auto &opt : opts) {
        add_arg(opt, "Pane", "Focus pane direction", 110);
      }
    } else if (lcmd == "task" || lcmd == "tasknew") {
      for (const auto &name : list_terminal_task_names()) {
        add_arg(name, "Task", "Run terminal task", 120);
      }
    } else if (lcmd == "debugconfig") {
      for (const auto &name : list_debugger_config_names()) {
        add_arg(name, "Debugger", "Launch debug session", 120);
      }
    } else if (lcmd == "debug" || lcmd == "debuggdb" ||
               lcmd == "debuglldb") {
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Program", "Debug executable path", 110);
      }
    } else if (lcmd == "lspinstall" || lcmd == "lspremove") {
      const std::vector<std::string> opts = {
          "python", "typescript", "javascript", "jsx", "tsx", "cpp",
          "rust",   "go",         "lua",        "bash", "html"};
      for (const auto &opt : opts) {
        add_arg(opt, "LSP", "Language server", 110);
      }
    } else if (lcmd == "tsinstall" || lcmd == "treesitterinstall") {
      for (const auto &opt : TreeSitterInstall::supported_languages()) {
        add_arg(opt, "Tree-sitter", "Grammar language", 110);
      }
    } else if (lcmd == "gitdiff" || lcmd == "gitdiffstaged" ||
               lcmd == "gitstage" || lcmd == "gitunstage") {
      auto &buf = get_buffer();
      if (!buf.filepath.empty()) {
        std::string rel = to_git_relative_path(buf.filepath);
        if (!rel.empty()) {
          add_arg(rel, "Git", "Current file", 130);
        }
      }
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Path", "Workspace path", 100);
      }
    } else if (lcmd == "find" || lcmd == "ff") {
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Path", "Open finder at path", 100);
      }
    } else if (lcmd == "mkfile" || lcmd == "mkdir" || lcmd == "rm" ||
               lcmd == "cppimpl" || lcmd == "cpppair") {
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Path", "Workspace path", 100);
      }
    } else if (lcmd == "rename") {
      size_t split = arg.find_first_of(" \t");
      if (split == std::string::npos) {
        auto paths = complete_workspace_path(arg);
        for (const auto &path : paths) {
          add_arg(path, "Source", "Path to rename", 110);
        }
      } else {
        std::string right = trim_copy(arg.substr(split + 1));
        auto paths = complete_workspace_path(right);
        for (const auto &path : paths) {
          add_arg(path, "Destination", "New path or name", 100);
        }
      }
    } else if (lcmd == "line" || lcmd == "goto") {
      auto &buf = get_buffer();
      int cur_line = std::max(1, buf.cursor.y + 1);
      int cur_col = std::max(1, buf.cursor.x + 1);
      int last_line = std::max(1, (int)buf.line_count());
      std::vector<std::string> opts = {
          std::to_string(cur_line),
          std::to_string(cur_line) + ":" + std::to_string(cur_col),
          std::to_string(std::max(1, cur_line - 10)),
          std::to_string(std::min(last_line, cur_line + 10)),
          std::to_string(last_line)};
      for (const auto &opt : opts) {
        add_arg(opt, "Line", "Jump target", 120);
      }
    } else if (lcmd == "help" || lcmd == "h") {
      const std::vector<std::string> topics = {"commands", "cmd", "ex", "keys"};
      for (const auto &topic : topics) {
        add_arg(topic, "Help", "Help topic", 120);
      }
      for (const auto &c : ex_commands()) {
        add_arg(c, "Command", "Command help", 80);
      }
    } else if (lcmd == "e" || lcmd == "edit" || lcmd == "open" ||
               lcmd == "w" || lcmd == "write" || lcmd == "wq" ||
               lcmd == "x" || lcmd == "xit") {
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Path", "File path", 110);
      }
    }
  }

  std::sort(command_palette_results.begin(), command_palette_results.end(),
            [](const CommandPaletteSuggestion &a,
               const CommandPaletteSuggestion &b) {
              if (a.score != b.score) {
                return a.score > b.score;
              }
              return to_lower_copy(a.label) < to_lower_copy(b.label);
            });

  std::set<std::string> seen;
  std::vector<CommandPaletteSuggestion> unique;
  for (auto &result : command_palette_results) {
    std::string key = to_lower_copy(result.insert_text);
    if (seen.insert(key).second) {
      unique.push_back(std::move(result));
    }
    if (unique.size() >= 128) {
      break;
    }
  }
  command_palette_results = std::move(unique);
  if (command_palette_selected >= (int)command_palette_results.size()) {
    command_palette_selected = 0;
  }
  if (command_palette_selected < 0) {
    command_palette_selected = 0;
  }
}
