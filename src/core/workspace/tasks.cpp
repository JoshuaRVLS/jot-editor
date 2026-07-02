#include "editor.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace {
std::string task_trim(std::string s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

void skip_json_ws(const std::string &text, size_t &pos) {
  while (pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[pos]))) {
    pos++;
  }
}

bool parse_json_string_value(const std::string &text, size_t &pos,
                             std::string &out) {
  if (pos >= text.size() || text[pos] != '"') {
    return false;
  }
  pos++;
  out.clear();
  while (pos < text.size()) {
    char c = text[pos++];
    if (c == '"') {
      return true;
    }
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (pos >= text.size()) {
      return false;
    }
    char esc = text[pos++];
    switch (esc) {
    case '"':
    case '\\':
    case '/':
      out.push_back(esc);
      break;
    case 'b':
      out.push_back('\b');
      break;
    case 'f':
      out.push_back('\f');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 't':
      out.push_back('\t');
      break;
    default:
      return false;
    }
  }
  return false;
}

bool skip_json_value(const std::string &text, size_t &pos);

bool skip_json_object_or_array(const std::string &text, size_t &pos,
                               char open, char close) {
  if (pos >= text.size() || text[pos] != open) {
    return false;
  }
  pos++;
  while (pos < text.size()) {
    skip_json_ws(text, pos);
    if (pos < text.size() && text[pos] == close) {
      pos++;
      return true;
    }
    if (open == '{') {
      std::string key;
      if (!parse_json_string_value(text, pos, key)) {
        return false;
      }
      skip_json_ws(text, pos);
      if (pos >= text.size() || text[pos] != ':') {
        return false;
      }
      pos++;
      if (!skip_json_value(text, pos)) {
        return false;
      }
    } else {
      if (!skip_json_value(text, pos)) {
        return false;
      }
    }
    skip_json_ws(text, pos);
    if (pos < text.size() && text[pos] == ',') {
      pos++;
      continue;
    }
    if (pos < text.size() && text[pos] == close) {
      pos++;
      return true;
    }
    return false;
  }
  return false;
}

bool skip_json_value(const std::string &text, size_t &pos) {
  skip_json_ws(text, pos);
  if (pos >= text.size()) {
    return false;
  }
  if (text[pos] == '"') {
    std::string ignored;
    return parse_json_string_value(text, pos, ignored);
  }
  if (text[pos] == '{') {
    return skip_json_object_or_array(text, pos, '{', '}');
  }
  if (text[pos] == '[') {
    return skip_json_object_or_array(text, pos, '[', ']');
  }
  while (pos < text.size() && text[pos] != ',' && text[pos] != '}' &&
         text[pos] != ']') {
    pos++;
  }
  return true;
}

std::map<std::string, std::string> parse_tasks_object(const std::string &text) {
  std::map<std::string, std::string> tasks;
  size_t pos = 0;
  skip_json_ws(text, pos);
  if (pos >= text.size() || text[pos] != '{') {
    return tasks;
  }
  pos++;

  while (pos < text.size()) {
    skip_json_ws(text, pos);
    if (pos < text.size() && text[pos] == '}') {
      break;
    }

    std::string key;
    if (!parse_json_string_value(text, pos, key)) {
      return tasks;
    }
    skip_json_ws(text, pos);
    if (pos >= text.size() || text[pos] != ':') {
      return tasks;
    }
    pos++;
    skip_json_ws(text, pos);

    if (key == "tasks" && pos < text.size() && text[pos] == '{') {
      pos++;
      while (pos < text.size()) {
        skip_json_ws(text, pos);
        if (pos < text.size() && text[pos] == '}') {
          pos++;
          break;
        }
        std::string task_name;
        std::string command;
        if (!parse_json_string_value(text, pos, task_name)) {
          return tasks;
        }
        skip_json_ws(text, pos);
        if (pos >= text.size() || text[pos] != ':') {
          return tasks;
        }
        pos++;
        skip_json_ws(text, pos);
        if (parse_json_string_value(text, pos, command)) {
          task_name = task_trim(task_name);
          command = task_trim(command);
          if (!task_name.empty() && !command.empty()) {
            tasks[task_name] = command;
          }
        } else if (!skip_json_value(text, pos)) {
          return tasks;
        }
        skip_json_ws(text, pos);
        if (pos < text.size() && text[pos] == ',') {
          pos++;
        }
      }
    } else if (!skip_json_value(text, pos)) {
      return tasks;
    }

    skip_json_ws(text, pos);
    if (pos < text.size() && text[pos] == ',') {
      pos++;
    }
  }

  return tasks;
}

std::string config_home_path() {
  const char *override_home = std::getenv("JOT_CONFIG_HOME");
  if (override_home && *override_home) {
    return (fs::path(override_home) / "configs" / "tasks.json").string();
  }
#ifdef _WIN32
  const char *app_data = std::getenv("APPDATA");
  if (app_data && *app_data) {
    return (fs::path(app_data) / "jot" / "configs" / "tasks.json").string();
  }
  const char *home = std::getenv("USERPROFILE");
#else
  const char *home = std::getenv("HOME");
#endif
  if (!home || !*home) {
    return "";
  }
  return (fs::path(home) / ".config" / "jot" / "configs" / "tasks.json").string();
}

std::string normalize_task_path(const fs::path &path) {
  std::error_code ec;
  fs::path abs = fs::absolute(path, ec);
  if (ec) {
    abs = path;
  }
  return abs.lexically_normal().string();
}
} // namespace

void Editor::load_terminal_tasks() {
  terminal_tasks.clear();

  auto load_file = [&](const fs::path &path, const std::string &kind,
                       const std::string &cwd) {
    std::ifstream in(path);
    if (!in.is_open()) {
      return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto parsed = parse_tasks_object(ss.str());
    for (const auto &[name, command] : parsed) {
      auto it = std::find_if(terminal_tasks.begin(), terminal_tasks.end(),
                             [&](const TerminalTask &task) {
                               return task.name == name;
                             });
      TerminalTask task{name, command, normalize_task_path(path), kind, cwd};
      if (it == terminal_tasks.end()) {
        terminal_tasks.push_back(std::move(task));
      } else {
        *it = std::move(task);
      }
    }
  };

  std::string global = config_home_path();
  if (!global.empty()) {
    load_file(global, "global", root_dir.empty() ? "." : root_dir);
  }

  std::error_code ec;
  fs::path local_root = root_dir.empty() ? fs::current_path(ec) : fs::path(root_dir);
  if (ec) {
    ec.clear();
    local_root = fs::path(".");
  }
  load_file(local_root / ".jot" / "tasks.json", "local",
            normalize_task_path(local_root));

  std::sort(terminal_tasks.begin(), terminal_tasks.end(),
            [](const TerminalTask &a, const TerminalTask &b) {
              return a.name < b.name;
            });
}

std::vector<std::string> Editor::list_terminal_task_names() {
  load_terminal_tasks();
  std::vector<std::string> names;
  names.reserve(terminal_tasks.size());
  for (const auto &task : terminal_tasks) {
    names.push_back(task.name);
  }
  return names;
}

void Editor::show_terminal_tasks() {
  load_terminal_tasks();
  if (terminal_tasks.empty()) {
    set_message("No tasks found in .jot/tasks.json or ~/.config/jot/configs/tasks.json");
    return;
  }

  std::string text = "Tasks:\n";
  for (const auto &task : terminal_tasks) {
    text += "  " + task.name + " [" + task.source_kind + "]  " +
            task.command + "\n";
  }
  show_popup(text, 2, tab_height + 1);
  needs_redraw = true;
}

bool Editor::run_terminal_task(const std::string &name, bool force_new) {
  std::string wanted = task_trim(name);
  if (wanted.empty()) {
    show_terminal_tasks();
    return false;
  }

  load_terminal_tasks();
  auto it = std::find_if(terminal_tasks.begin(), terminal_tasks.end(),
                         [&](const TerminalTask &task) {
                           return task.name == wanted;
                         });
  if (it == terminal_tasks.end()) {
    set_message("Task not found: " + wanted);
    return false;
  }

  int existing = -1;
  if (!force_new) {
    std::string label = "task:" + it->name;
    for (int i = 0; i < (int)integrated_terminals.size(); i++) {
      if (integrated_terminals[i] &&
          integrated_terminals[i]->get_label() == label) {
        existing = i;
        break;
      }
    }
  }

  if (existing >= 0) {
    close_integrated_terminal(existing);
  }

  create_integrated_terminal("task:" + it->name, it->cwd);
  IntegratedTerminal *term = get_integrated_terminal();
  if (!term || !term->is_active()) {
    set_message("Failed to open task terminal");
    return false;
  }

  term->send_text(it->command + "\r");
  last_terminal_task_name = it->name;
  set_message("Running task: " + it->name);
  needs_redraw = true;
  return true;
}

bool Editor::rerun_last_terminal_task() {
  if (last_terminal_task_name.empty()) {
    set_message("No task has been run");
    return false;
  }
  return run_terminal_task(last_terminal_task_name, false);
}
