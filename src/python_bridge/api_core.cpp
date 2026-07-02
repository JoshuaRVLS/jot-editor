// Python headers must be first so their macros precede C++ standard headers.
#include "python_bridge/python_headers.h"

#include "editor.h"
#include "host_api.h"
#include "python_bridge/api.h"
#include "python_bridge/api_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
fs::path get_executable_path() {
#ifdef _WIN32
  std::vector<char> buffer(MAX_PATH, '\0');
  DWORD len = GetModuleFileNameA(nullptr, buffer.data(), (DWORD)buffer.size());
  if (len == 0) {
    return {};
  }
  while (len == buffer.size()) {
    buffer.resize(buffer.size() * 2, '\0');
    len = GetModuleFileNameA(nullptr, buffer.data(), (DWORD)buffer.size());
    if (len == 0) {
      return {};
    }
  }
  return fs::path(std::string(buffer.data(), len));
#else
  std::vector<char> buffer(4096, '\0');
  ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (len <= 0) {
    return {};
  }
  buffer[static_cast<size_t>(len)] = '\0';
  return fs::path(buffer.data());
#endif
}

std::vector<fs::path> get_runtime_python_dirs() {
  std::vector<fs::path> dirs;
  auto push_unique = [&dirs](const fs::path &p) {
    if (p.empty()) {
      return;
    }
    for (const auto &existing : dirs) {
      if (existing == p) {
        return;
      }
    }
    dirs.push_back(p);
  };

  const char *env_runtime = getenv("JOT_PYTHON_PATH");
  if (env_runtime && *env_runtime) {
    push_unique(fs::path(env_runtime));
  }

  const char *xdg_data_home = getenv("XDG_DATA_HOME");
  if (xdg_data_home && *xdg_data_home) {
    push_unique(fs::path(xdg_data_home) / "jot" / "python");
  } else {
#ifdef _WIN32
    const char *local_app_data = getenv("LOCALAPPDATA");
    if (local_app_data && *local_app_data) {
      push_unique(fs::path(local_app_data) / "jot" / "python");
    }
#else
    const char *home = getenv("HOME");
    if (home && *home) {
      push_unique(fs::path(home) / ".local" / "share" / "jot" / "python");
    }
#endif
  }

  fs::path exe_path = get_executable_path();
  if (!exe_path.empty()) {
    fs::path exe_dir = exe_path.parent_path();
    push_unique(exe_dir.parent_path() / "share" / "jot" / "python");
    push_unique(exe_dir.parent_path() / "src" / "python");
  }

  push_unique(fs::current_path() / "src" / "python");
  return dirs;
}

fs::path get_user_config_root() {
  const char *jot_config = getenv("JOT_CONFIG_HOME");
  if (jot_config && *jot_config) {
    return fs::path(jot_config);
  }
  const char *home = getenv("HOME");
#ifdef _WIN32
  const char *app_data = getenv("APPDATA");
  if (app_data && *app_data) {
    return fs::path(app_data) / "jot";
  }
  home = getenv("USERPROFILE");
#endif
  if (!home) {
    return {};
  }
  return fs::path(home) / ".config" / "jot";
}

void append_python_path(const fs::path &path) {
  if (!fs::exists(path) || !fs::is_directory(path)) {
    return;
  }

  std::string escaped = path.string();
  size_t pos = 0;
  while ((pos = escaped.find('\\', pos)) != std::string::npos) {
    escaped.replace(pos, 1, "\\\\");
    pos += 2;
  }
  pos = 0;
  while ((pos = escaped.find('\'', pos)) != std::string::npos) {
    escaped.replace(pos, 1, "\\'");
    pos += 2;
  }
  PyRun_SimpleString(
      ("import sys\n"
       "p = '" +
       escaped +
       "'\n"
       "if p not in sys.path:\n"
       "    sys.path.append(p)\n")
          .c_str());
}

std::string py_error_string() {
  if (!PyErr_Occurred()) {
    return "";
  }
  PyObject *ptype = nullptr;
  PyObject *pvalue = nullptr;
  PyObject *ptraceback = nullptr;
  PyErr_Fetch(&ptype, &pvalue, &ptraceback);
  PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);

  std::string message = "Python error";
  if (pvalue) {
    PyObject *text = PyObject_Str(pvalue);
    if (text) {
      const char *utf8 = PyUnicode_AsUTF8(text);
      if (utf8) {
        message = utf8;
      }
      Py_DECREF(text);
    }
  }

  Py_XDECREF(ptype);
  Py_XDECREF(pvalue);
  Py_XDECREF(ptraceback);
  return message;
}

std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return value;
}

std::string trim_copy(const std::string &value) {
  size_t start = 0;
  while (start < value.size() && std::isspace((unsigned char)value[start])) {
    start++;
  }
  size_t end = value.size();
  while (end > start && std::isspace((unsigned char)value[end - 1])) {
    end--;
  }
  return value.substr(start, end - start);
}

std::string canonical_key_name(const std::string &raw) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : raw) {
    if (c == '+') {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else if (!std::isspace((unsigned char)c)) {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }

  bool ctrl = false;
  bool alt = false;
  bool shift = false;
  std::string key;
  for (std::string part : parts) {
    std::string lowered = lower_copy(part);
    if (lowered == "ctrl" || lowered == "control") {
      ctrl = true;
    } else if (lowered == "alt" || lowered == "meta") {
      alt = true;
    } else if (lowered == "shift") {
      shift = true;
    } else {
      key = part;
    }
  }

  if (key.empty()) {
    key = raw;
  }

  std::string lowered_key = lower_copy(key);
  if (lowered_key == "esc" || lowered_key == "escape") {
    key = "Esc";
  } else if (lowered_key == "enter" || lowered_key == "return") {
    key = "Enter";
  } else if (lowered_key == "tab") {
    key = "Tab";
  } else if (lowered_key == "backspace" || lowered_key == "bs") {
    key = "Backspace";
  } else if (lowered_key == "delete" || lowered_key == "del") {
    key = "Delete";
  } else if (lowered_key == "up" || lowered_key == "down" ||
             lowered_key == "left" || lowered_key == "right" ||
             lowered_key == "home" || lowered_key == "end") {
    key = std::string(1, (char)std::toupper((unsigned char)lowered_key[0])) +
          lowered_key.substr(1);
  } else if (key.size() == 1) {
    key = std::string(1, (char)std::toupper((unsigned char)key[0]));
  }

  std::string out;
  if (ctrl) {
    out += "Ctrl+";
  }
  if (alt) {
    out += "Alt+";
  }
  if (shift) {
    out += "Shift+";
  }
  out += key;
  return out;
}
} // namespace

PythonAPI::PythonAPI(Editor *ed)
    : editor(ed), py_module(nullptr), python_initialized(false) {
  PythonBridgeInternal::set_active_api(this);
}

PythonAPI::~PythonAPI() {
  cleanup();
  PythonBridgeInternal::set_active_api(nullptr);
}

bool PythonAPI::init() {
  if (python_initialized)
    return true;

  // Register built-in module before Py_Initialize so jot_api can import it.
  if (!PythonBridgeInternal::register_internal_module()) {
    std::cerr << "Failed to register _jot_internal module" << std::endl;
    return false;
  }

  Py_Initialize();
  if (!Py_IsInitialized()) {
    return false;
  }

  python_initialized = true;

  PyRun_SimpleString("import os\nimport sys\nsys.path.append(os.getcwd())\n");

  std::vector<fs::path> runtime_dirs = get_runtime_python_dirs();
  for (const auto &dir : runtime_dirs) {
    append_python_path(dir);
  }

  fs::path config_root = get_user_config_root();
  if (!config_root.empty()) {
    fs::create_directories(config_root / "configs" / "colors");
    fs::create_directories(config_root / "themes");

    append_python_path(config_root / "configs");
    append_python_path(config_root / "configs" / "colors");
    append_python_path(config_root / "themes");
  }

  for (const auto &dir : runtime_dirs) {
    if (fs::exists(dir / "jot_api.py")) {
      import_jot_api_module();
      break;
    }
  }

  if (!py_module && !import_jot_api_module()) {
    std::cerr << "Failed to import jot_api module" << std::endl;
    PyErr_Print();
    PyErr_Clear();
    return false;
  }

  load_plugins();

  return true;
}

void PythonAPI::cleanup() {
  if (py_module) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_module));
    py_module = nullptr;
  }

  if (python_initialized) {
    Py_Finalize();
    python_initialized = false;
  }
}

bool PythonAPI::import_jot_api_module() {
  if (py_module) {
    return true;
  }

  PyObject *module = PyImport_ImportModule("jot_api");
  if (!module) {
    return false;
  }

  py_module = module;
  return true;
}

void PythonAPI::on_buffer_open(const std::string &filepath) {
  if (editor)
    editor->notify_lsp_open(filepath);
  fire_autocmd("BufOpen", filepath, -1);
}

void PythonAPI::on_buffer_change(const std::string &filepath,
                                 const std::string &content) {
  (void)content;
  if (editor)
    editor->notify_lsp_change(filepath);
  fire_autocmd("BufChange", filepath, -1);
}

void PythonAPI::on_buffer_save(const std::string &filepath) {
  if (editor)
    editor->notify_lsp_save(filepath);
  fire_autocmd("BufSave", filepath, -1);
}

void PythonAPI::clear_plugin_state() {
  plugin_commands.clear();
  plugin_keymaps.clear();
  plugin_autocmds.clear();
  plugin_panels.clear();
  plugin_load_status.clear();

  PyObject *api = PyImport_ImportModule("jot_api");
  if (!api) {
    PyErr_Clear();
    return;
  }
  PyObject *reset = PyObject_GetAttrString(api, "_reset_plugin_state");
  if (reset && PyCallable_Check(reset)) {
    PyObject *res = PyObject_CallObject(reset, nullptr);
    if (!res) {
      PyErr_Print();
      PyErr_Clear();
    }
    Py_XDECREF(res);
  }
  Py_XDECREF(reset);
  Py_DECREF(api);
}

bool PythonAPI::load_script_path(const std::string &module_name,
                                 const std::string &path) {
  PyObject *runpy = PyImport_ImportModule("runpy");
  if (!runpy) {
    plugin_load_status.push_back(
        {module_name, path, false, py_error_string()});
    PyErr_Clear();
    return false;
  }
  append_python_path(fs::path(path).parent_path());
  PyObject *result = PyObject_CallMethod(runpy, "run_path", "sOs",
                                         path.c_str(), Py_None,
                                         module_name.c_str());
  Py_DECREF(runpy);
  if (!result) {
    plugin_load_status.push_back(
        {module_name, path, false, py_error_string()});
    PyErr_Clear();
    if (editor) {
      editor->set_message("Plugin failed: " + module_name);
    }
    return false;
  }
  Py_DECREF(result);
  plugin_load_status.push_back({module_name, path, true, ""});
  return true;
}

void PythonAPI::load_plugins() {
  fs::path config_root = get_user_config_root();
  fs::path root = config_root / "plugins";
  fs::create_directories(root);
  append_python_path(config_root);
  append_python_path(root);
  clear_plugin_state();

  fs::path init = config_root / "init.py";
  if (fs::exists(init)) {
    load_script_path("jot_init", init.string());
  }

  std::vector<fs::path> files;
  if (fs::exists(root)) {
    for (const auto &entry : fs::directory_iterator(root)) {
      if (entry.is_regular_file() && entry.path().extension() == ".py") {
        files.push_back(entry.path());
      } else if (entry.is_directory()) {
        fs::path plugin_py = entry.path() / "plugin.py";
        if (fs::exists(plugin_py)) {
          files.push_back(plugin_py);
        }
      }
    }
  }
  std::sort(files.begin(), files.end());

  for (const auto &path : files) {
    std::string module = "jot_plugin_" + path.stem().string();
    if (path.filename() == "plugin.py" && path.has_parent_path()) {
      module = "jot_plugin_" + path.parent_path().filename().string();
    }
    load_script_path(module, path.string());
  }

  fire_autocmd("EditorEnter", "", -1);
}

void PythonAPI::reload_plugins() {
  load_plugins();
  fire_autocmd("PluginReload", "", -1);
  if (editor) {
    editor->set_message("Reloaded " +
                        std::to_string(plugin_load_status.size()) +
                        " plugin file(s)");
  }
}

bool PythonAPI::call_callback_string(const std::string &callback,
                                     const std::string &arg) {
  PyObject *api = PyImport_ImportModule("jot_api");
  if (!api) {
    PyErr_Print();
    PyErr_Clear();
    return false;
  }
  PyObject *registry = PyObject_GetAttrString(api, "_plugin_callbacks");
  if (!registry || !PyDict_Check(registry)) {
    Py_XDECREF(registry);
    Py_DECREF(api);
    return false;
  }
  PyObject *fn = PyDict_GetItemString(registry, callback.c_str());
  bool called = false;
  if (fn && PyCallable_Check(fn)) {
    PyObject *res = PyObject_CallFunction(fn, "s", arg.c_str());
    if (!res) {
      PyErr_Print();
      PyErr_Clear();
    } else {
      called = true;
      Py_DECREF(res);
    }
  }
  Py_XDECREF(registry);
  Py_DECREF(api);
  return called;
}

void PythonAPI::py_register_command(const std::string &name,
                                    const std::string &callback,
                                    const std::string &detail) {
  if (name.empty() || callback.empty()) {
    return;
  }
  std::string normalized = lower_copy(name);
  for (auto &command : plugin_commands) {
    if (command.name == normalized) {
      command.callback = callback;
      command.detail = detail;
      return;
    }
  }
  plugin_commands.push_back({normalized, callback, detail});
}

bool PythonAPI::run_plugin_command(const std::string &name,
                                   const std::string &arg) {
  for (const auto &cmd : plugin_commands) {
    if (cmd.name != name)
      continue;
    call_callback_string(cmd.callback, arg);
    return true;
  }
  return false;
}

bool PythonAPI::run_plugin_keymap(const std::string &key) {
  for (const auto &keymap : plugin_keymaps) {
    if (keymap.key != key) {
      continue;
    }
    if (!keymap.command.empty()) {
      if (editor) {
        std::string command = trim_copy(keymap.command);
        if (!command.empty() && command[0] == ':') {
          editor->show_command_palette = false;
          editor->command_palette_query.clear();
          editor->command_palette_results.clear();
          editor->command_palette_selected = 0;
          editor->command_palette_theme_mode = false;
          editor->command_palette_theme_original.clear();
          editor->execute_ex_command(command);
        } else if (editor->host_api) {
          editor->host_api->io.execute_command(command);
        }
      }
    } else {
      call_callback_string(keymap.callback, "");
    }
    return true;
  }
  return false;
}

void PythonAPI::fire_autocmd(const std::string &event,
                             const std::string &filepath, int buffer) {
  if (plugin_autocmds.empty()) {
    return;
  }
  std::string payload = event + "\n" + filepath + "\n" + std::to_string(buffer);
  for (const auto &autocmd : plugin_autocmds) {
    if (autocmd.event == event) {
      call_callback_string(autocmd.callback, payload);
    }
  }
}

std::vector<std::string> PythonAPI::plugin_panel_lines(const std::string &name) {
  for (const auto &panel : plugin_panels) {
    if (panel.name != name) {
      continue;
    }
    PyObject *api = PyImport_ImportModule("jot_api");
    if (!api) {
      PyErr_Clear();
      return {};
    }
    PyObject *registry = PyObject_GetAttrString(api, "_plugin_callbacks");
    PyObject *fn = registry && PyDict_Check(registry)
                       ? PyDict_GetItemString(registry, panel.callback.c_str())
                       : nullptr;
    std::vector<std::string> lines;
    if (fn && PyCallable_Check(fn)) {
      PyObject *res = PyObject_CallFunction(fn, "s", name.c_str());
      if (res && PyList_Check(res)) {
        Py_ssize_t count = PyList_Size(res);
        for (Py_ssize_t i = 0; i < count; i++) {
          PyObject *item = PyList_GetItem(res, i);
          PyObject *text = PyObject_Str(item);
          if (text) {
            const char *utf8 = PyUnicode_AsUTF8(text);
            lines.push_back(utf8 ? utf8 : "");
            Py_DECREF(text);
          }
        }
      } else if (res) {
        PyObject *text = PyObject_Str(res);
        if (text) {
          const char *utf8 = PyUnicode_AsUTF8(text);
          std::istringstream stream(utf8 ? utf8 : "");
          std::string line;
          while (std::getline(stream, line)) {
            lines.push_back(line);
          }
          Py_DECREF(text);
        }
      } else {
        PyErr_Print();
        PyErr_Clear();
      }
      Py_XDECREF(res);
    }
    Py_XDECREF(registry);
    Py_DECREF(api);
    return lines;
  }
  return {};
}

std::vector<std::string>
PythonAPI::plugin_picker_items(const std::string &callback) {
  std::vector<std::string> items;
  PyObject *api = PyImport_ImportModule("jot_api");
  if (!api) {
    PyErr_Clear();
    return items;
  }
  PyObject *registry = PyObject_GetAttrString(api, "_plugin_callbacks");
  PyObject *fn = registry && PyDict_Check(registry)
                     ? PyDict_GetItemString(registry, callback.c_str())
                     : nullptr;
  if (fn && PyCallable_Check(fn)) {
    PyObject *res = PyObject_CallFunction(fn, "s", "");
    if (res && PyList_Check(res)) {
      Py_ssize_t count = PyList_Size(res);
      for (Py_ssize_t i = 0; i < count; i++) {
        PyObject *item = PyList_GetItem(res, i);
        PyObject *text = PyObject_Str(item);
        if (text) {
          const char *utf8 = PyUnicode_AsUTF8(text);
          items.push_back(utf8 ? utf8 : "");
          Py_DECREF(text);
        }
      }
    } else if (!res) {
      PyErr_Print();
      PyErr_Clear();
    }
    Py_XDECREF(res);
  }
  Py_XDECREF(registry);
  Py_DECREF(api);
  return items;
}

bool PythonAPI::run_plugin_callback(const std::string &callback,
                                    const std::string &arg) {
  return call_callback_string(callback, arg);
}

void PythonAPI::py_register_keymap(const std::string &key,
                                   const std::string &callback,
                                   const std::string &command,
                                   const std::string &detail,
                                   const std::string &mode) {
  if (key.empty() || (callback.empty() && command.empty())) {
    return;
  }
  std::string normalized = canonical_key_name(key);
  for (auto &keymap : plugin_keymaps) {
    if (keymap.key == normalized && keymap.mode == mode) {
      keymap.callback = callback;
      keymap.command = command;
      keymap.detail = detail;
      return;
    }
  }
  plugin_keymaps.push_back({normalized, callback, command, detail, mode});
}

void PythonAPI::py_register_autocmd(const std::string &event,
                                    const std::string &callback) {
  if (event.empty() || callback.empty()) {
    return;
  }
  plugin_autocmds.push_back({event, callback});
}

void PythonAPI::py_register_panel(const std::string &name,
                                  const std::string &callback,
                                  const std::string &title) {
  if (name.empty() || callback.empty()) {
    return;
  }
  for (auto &panel : plugin_panels) {
    if (panel.name == name) {
      panel.callback = callback;
      panel.title = title;
      return;
    }
  }
  plugin_panels.push_back({name, callback, title});
}

std::string PythonAPI::py_get_current_buffer() {
  if (!editor || !editor->host_api) {
    return "";
  }
  return editor->host_api->core.buffer_content();
}

void PythonAPI::py_set_current_buffer(const std::string &text) {
  if (!editor || !editor->host_api) {
    return;
  }
  editor->host_api->core.set_buffer_content(text);
}

std::string PythonAPI::py_get_selection() {
  if (!editor || !editor->host_api) {
    return "";
  }
  return editor->host_api->core.selected_text();
}

void PythonAPI::py_replace_selection(const std::string &text) {
  if (editor && editor->host_api) {
    editor->host_api->core.replace_selection(text);
  }
}

void PythonAPI::py_insert_text(const std::string &text) {
  if (editor && editor->host_api) {
    editor->host_api->core.insert_text(text);
  }
}

std::string PythonAPI::py_get_cursor() {
  if (!editor || !editor->host_api) {
    return "0:0";
  }
  auto cursor = editor->host_api->core.cursor();
  return std::to_string(cursor.first) + ":" + std::to_string(cursor.second);
}

void PythonAPI::py_set_cursor(int line, int col) {
  if (editor && editor->host_api) {
    editor->host_api->core.set_cursor(line, col);
  }
}

std::string PythonAPI::py_current_file() {
  if (!editor || !editor->host_api) {
    return "";
  }
  return editor->host_api->core.current_file();
}

void PythonAPI::py_open_file(const std::string &path) {
  if (editor && editor->host_api) {
    editor->host_api->io.open_file(path);
  }
}

void PythonAPI::py_save_current_file() {
  if (editor && editor->host_api) {
    editor->host_api->io.save_current_file();
  }
}

void PythonAPI::py_execute_command(const std::string &command) {
  if (editor && editor->host_api) {
    editor->host_api->io.execute_command(command);
  }
}

void PythonAPI::py_run_job(const std::string &command, const std::string &cwd,
                           const std::string &label) {
  if (editor && editor->host_api) {
    editor->host_api->io.run_job(command, cwd, label);
  }
}

void PythonAPI::py_show_picker(const std::string &title,
                               const std::string &items_callback,
                               const std::string &select_callback) {
  if (editor && editor->host_api) {
    editor->host_api->io.show_plugin_picker(title, items_callback,
                                            select_callback);
  }
}

void PythonAPI::py_show_panel(const std::string &name) {
  if (editor && editor->host_api) {
    editor->host_api->io.show_plugin_panel(name);
  }
}
