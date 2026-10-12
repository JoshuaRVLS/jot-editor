#include "base/process_job.h"
#include "base/shell_util.h"
#include "core/editor.h"
#include "text/ansi_spans.h"
#include "ui/xterm_palette.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
  std::string task_trim(std::string s)
  {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
      return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
  }

  void skip_json_ws(const std::string &text, size_t &pos)
  {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
    {
      pos++;
    }
  }

  bool parse_json_string_value(const std::string &text, size_t &pos, std::string &out)
  {
    if (pos >= text.size() || text[pos] != '"')
    {
      return false;
    }
    pos++;
    out.clear();
    while (pos < text.size())
    {
      char c = text[pos++];
      if (c == '"')
      {
        return true;
      }
      if (c != '\\')
      {
        out.push_back(c);
        continue;
      }
      if (pos >= text.size())
      {
        return false;
      }
      char esc = text[pos++];
      switch (esc)
      {
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

  bool skip_json_object_or_array(const std::string &text, size_t &pos, char open, char close)
  {
    if (pos >= text.size() || text[pos] != open)
    {
      return false;
    }
    pos++;
    while (pos < text.size())
    {
      skip_json_ws(text, pos);
      if (pos < text.size() && text[pos] == close)
      {
        pos++;
        return true;
      }
      if (open == '{')
      {
        std::string key;
        if (!parse_json_string_value(text, pos, key))
        {
          return false;
        }
        skip_json_ws(text, pos);
        if (pos >= text.size() || text[pos] != ':')
        {
          return false;
        }
        pos++;
        if (!skip_json_value(text, pos))
        {
          return false;
        }
      }
      else
      {
        if (!skip_json_value(text, pos))
        {
          return false;
        }
      }
      skip_json_ws(text, pos);
      if (pos < text.size() && text[pos] == ',')
      {
        pos++;
        continue;
      }
      if (pos < text.size() && text[pos] == close)
      {
        pos++;
        return true;
      }
      return false;
    }
    return false;
  }

  bool skip_json_value(const std::string &text, size_t &pos)
  {
    skip_json_ws(text, pos);
    if (pos >= text.size())
    {
      return false;
    }
    if (text[pos] == '"')
    {
      std::string ignored;
      return parse_json_string_value(text, pos, ignored);
    }
    if (text[pos] == '{')
    {
      return skip_json_object_or_array(text, pos, '{', '}');
    }
    if (text[pos] == '[')
    {
      return skip_json_object_or_array(text, pos, '[', ']');
    }
    while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']')
    {
      pos++;
    }
    return true;
  }

  std::map<std::string, std::string> parse_tasks_object(const std::string &text)
  {
    std::map<std::string, std::string> tasks;
    size_t pos = 0;
    skip_json_ws(text, pos);
    if (pos >= text.size() || text[pos] != '{')
    {
      return tasks;
    }
    pos++;

    while (pos < text.size())
    {
      skip_json_ws(text, pos);
      if (pos < text.size() && text[pos] == '}')
      {
        break;
      }

      std::string key;
      if (!parse_json_string_value(text, pos, key))
      {
        return tasks;
      }
      skip_json_ws(text, pos);
      if (pos >= text.size() || text[pos] != ':')
      {
        return tasks;
      }
      pos++;
      skip_json_ws(text, pos);

      if (key == "tasks" && pos < text.size() && text[pos] == '{')
      {
        pos++;
        while (pos < text.size())
        {
          skip_json_ws(text, pos);
          if (pos < text.size() && text[pos] == '}')
          {
            pos++;
            break;
          }
          std::string task_name;
          std::string command;
          if (!parse_json_string_value(text, pos, task_name))
          {
            return tasks;
          }
          skip_json_ws(text, pos);
          if (pos >= text.size() || text[pos] != ':')
          {
            return tasks;
          }
          pos++;
          skip_json_ws(text, pos);
          if (parse_json_string_value(text, pos, command))
          {
            task_name = task_trim(task_name);
            command = task_trim(command);
            if (!task_name.empty() && !command.empty())
            {
              tasks[task_name] = command;
            }
          }
          else if (!skip_json_value(text, pos))
          {
            return tasks;
          }
          skip_json_ws(text, pos);
          if (pos < text.size() && text[pos] == ',')
          {
            pos++;
          }
        }
      }
      else if (!skip_json_value(text, pos))
      {
        return tasks;
      }

      skip_json_ws(text, pos);
      if (pos < text.size() && text[pos] == ',')
      {
        pos++;
      }
    }

    return tasks;
  }

  std::string config_home_path()
  {
    const char *override_home = std::getenv("JOT_CONFIG_HOME");
    if (override_home && *override_home)
    {
      return (fs::path(override_home) / "configs" / "tasks.json").string();
    }
#ifdef _WIN32
    const char *app_data = std::getenv("APPDATA");
    if (app_data && *app_data)
    {
      return (fs::path(app_data) / "jot" / "configs" / "tasks.json").string();
    }
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    if (!home || !*home)
    {
      return "";
    }
    return (fs::path(home) / ".config" / "jot" / "configs" / "tasks.json").string();
  }

  std::string normalize_task_path(const fs::path &path)
  {
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec)
    {
      abs = path;
    }
    return abs.lexically_normal().string();
  }
} // namespace

void Editor::load_terminal_tasks()
{
  terminal_tasks.clear();

  auto load_file = [&](const fs::path &path, const std::string &kind, const std::string &cwd)
  {
    std::ifstream in(path);
    if (!in.is_open())
    {
      return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto parsed = parse_tasks_object(ss.str());
    for (const auto &[name, command] : parsed)
    {
      auto it = std::find_if(terminal_tasks.begin(),
                             terminal_tasks.end(),
                             [&](const TerminalTask &task) { return task.name == name; });
      TerminalTask task{name, command, normalize_task_path(path), kind, cwd};
      if (it == terminal_tasks.end())
      {
        terminal_tasks.push_back(std::move(task));
      }
      else
      {
        *it = std::move(task);
      }
    }
  };

  std::string global = config_home_path();
  if (!global.empty())
  {
    load_file(global, "global", root_dir.empty() ? "." : root_dir);
  }

  std::error_code ec;
  fs::path local_root = root_dir.empty() ? fs::current_path(ec) : fs::path(root_dir);
  if (ec)
  {
    ec.clear();
    local_root = fs::path(".");
  }
  load_file(local_root / ".jot" / "tasks.json", "local", normalize_task_path(local_root));

  std::sort(terminal_tasks.begin(),
            terminal_tasks.end(),
            [](const TerminalTask &a, const TerminalTask &b) { return a.name < b.name; });
}

std::vector<std::string> Editor::list_terminal_task_names()
{
  load_terminal_tasks();
  std::vector<std::string> names;
  names.reserve(terminal_tasks.size());
  for (const auto &task : terminal_tasks)
  {
    names.push_back(task.name);
  }
  return names;
}

void Editor::show_terminal_tasks()
{
  load_terminal_tasks();
  if (terminal_tasks.empty())
  {
    set_message("No tasks found in .jot/tasks.json or ~/.config/jot/configs/tasks.json");
    return;
  }

  std::string text = "Tasks:\n";
  for (const auto &task : terminal_tasks)
  {
    text += "  " + task.name + " [" + task.source_kind + "]  " + task.command + "\n";
  }
  show_popup(text, "Tasks");
  needs_redraw = true;
}

bool Editor::run_terminal_task(const std::string &name, bool force_new)
{
  std::string wanted = task_trim(name);
  if (wanted.empty())
  {
    show_terminal_tasks();
    return false;
  }

  load_terminal_tasks();
  auto it = std::find_if(terminal_tasks.begin(),
                         terminal_tasks.end(),
                         [&](const TerminalTask &task) { return task.name == wanted; });
  if (it == terminal_tasks.end())
  {
    set_message("Task not found: " + wanted);
    return false;
  }

  int existing = -1;
  if (!force_new)
  {
    std::string label = "task:" + it->name;
    for (int i = 0; i < (int)integrated_terminals.size(); i++)
    {
      if (integrated_terminals[i] && integrated_terminals[i]->get_label() == label)
      {
        existing = i;
        break;
      }
    }
  }

  if (existing >= 0)
  {
    close_integrated_terminal(existing);
  }

  create_integrated_terminal("task:" + it->name, it->cwd);
  IntegratedTerminal *term = get_integrated_terminal();
  if (!term || !term->is_active())
  {
    set_message("Failed to open task terminal");
    return false;
  }

  term->send_text(it->command + "\r");
  last_terminal_task_name = it->name;
  set_message("Running task: " + it->name);
  needs_redraw = true;
  return true;
}

bool Editor::rerun_last_terminal_task()
{
  if (last_terminal_task_name.empty())
  {
    set_message("No task has been run");
    return false;
  }
  return run_terminal_task(last_terminal_task_name, false);
}

// ---------------------------------------------------------------------------
// The compile session (:compile)
// ---------------------------------------------------------------------------
//
// Emacs' M-x compile, with the parts that need Emacs left out: the status-line
// prompt decides the command, the job runs in the project root with its output
// streaming into a `[Compile]` buffer shown in a split below the pane that
// asked for it, and the locations that output names are parsed as they arrive,
// so :cnext / :cprev (and Enter on a row) jump while the build is still
// running.
//
// The job is a background shell with a terminal of its own
// (process_job::spawn_background_pty_shell) rather than a panel: a build that
// takes a minute must not hold the shell panel, and a log the editor reads back
// at its own pace is what makes the incremental parse possible at all. The pty
// is what lets the same prompt run a program as well as a compiler -- a built
// executable reads its stdin and prints its own colours, because it sees a tty
// -- and what makes a compiler colour its diagnostics without being asked. The
// escapes in that output are stripped as the rows arrive and turned into
// coloured spans (text/ansi_spans.h), so the log shows the colours rather than
// the sequences. The job leads its own process group, which is how :ckill
// reaches the grandchildren a `make` leaves behind.

namespace
{
  long long compile_now_ms()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // The clock Emacs' own compilation buffer stamps its header and footer with.
  std::string compile_clock_now()
  {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16];
    std::snprintf(
        buffer, sizeof(buffer), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
    return std::string(buffer);
  }

  // The file a finding names, as the editor means it: the tool prints it
  // relative to the directory it ran in (usually the project root), and
  // sometimes absolute.
  std::string compile_resolve_path(const std::string &printed, const std::string &cwd)
  {
    const fs::path path(printed);
    if (path.is_absolute())
    {
      return path.lexically_normal().string();
    }
    return (fs::path(cwd) / path).lexically_normal().string();
  }

  // The theme group a finding's row is tinted with: the slots the editor
  // squiggles its own diagnostics with, so one look says which rows are the
  // errors.
  const char *compile_severity_group(int severity)
  {
    switch (severity)
    {
    case 1:
      return "DiagnosticError";
    case 2:
      return "DiagnosticWarn";
    case 3:
      return "DiagnosticInfo";
    default:
      return "DiagnosticHint";
    }
  }

  // A decoration colour for a colour an SGR sequence named: a palette entry
  // travels as its index, an exact 24-bit value as an id in the UI's colour
  // table (ui/xterm_palette.h) -- the same domain Decoration fg/bg already use,
  // so the renderer needs no new case for either.
  int compile_span_color(const ansi_spans::Color &colour)
  {
    if (!colour.set)
    {
      return -1;
    }
    if (colour.index >= 0)
    {
      return colour.index;
    }
    return jot_ui::exact_color_id(
        (unsigned char)colour.r, (unsigned char)colour.g, (unsigned char)colour.b);
  }

  // Which finding a jump from the caret lands on: the next one in the same
  // file, else the first in a file that sorts after this one -- or, going back,
  // the last in a file that sorts before it -- wrapping around when there is
  // none in that direction. The rule :diagnext walks its list by, so a jump
  // means the same thing in both lists.
  int compile_finding_for_jump(const std::vector<CompileFinding> &findings,
                               const std::string &file,
                               const Cursor &cursor,
                               int direction)
  {
    int selected = -1;
    if (direction >= 0)
    {
      for (int i = 0; i < (int)findings.size(); i++)
      {
        const CompileFinding &finding = findings[(size_t)i];
        if (finding.filepath == file)
        {
          const int line = std::max(0, finding.line - 1);
          const int col = std::max(0, finding.col - 1);
          if (line > cursor.y || (line == cursor.y && col > cursor.x))
          {
            selected = i;
            break;
          }
        }
        else if (selected < 0 && finding.filepath > file)
        {
          selected = i;
        }
      }
      return selected < 0 ? 0 : selected;
    }
    for (int i = (int)findings.size() - 1; i >= 0; i--)
    {
      const CompileFinding &finding = findings[(size_t)i];
      if (finding.filepath == file)
      {
        const int line = std::max(0, finding.line - 1);
        const int col = std::max(0, finding.col - 1);
        if (line < cursor.y || (line == cursor.y && col < cursor.x))
        {
          selected = i;
          break;
        }
      }
      else if (selected < 0 && finding.filepath < file)
      {
        selected = i;
      }
    }
    return selected < 0 ? (int)findings.size() - 1 : selected;
  }
} // namespace

int Editor::compile_ensure_buffer()
{
  for (int i = 0; i < (int)buffers.size(); i++)
  {
    if (is_compile_log_path(buffers[(size_t)i].filepath))
    {
      compile.buffer_id = i;
      return i;
    }
  }
  create_new_buffer();
  FileBuffer &buf = get_buffer();
  buf.filepath = "[Compile]";
  // A build log is something to read, not something to write: every edit path
  // refuses through edit_allowed(), the same way the Bible tab's does.
  buf.read_only = true;
  compile.buffer_id = current_buffer;
  return current_buffer;
}

std::string Editor::default_compile_command()
{
  if (!compile.command.empty())
  {
    return compile.command;
  }
  return config.get("build_command", "");
}

void Editor::compile_run(const std::string &raw_command)
{
  std::string command = task_trim(raw_command);
  if (command.empty())
  {
    command = default_compile_command();
  }
  if (command.empty())
  {
    set_message("No build command: :compile <command>", true);
    return;
  }
  // One build at a time: running it again is an instruction about the same
  // build, not a request for a second one.
  compile_stop();

  std::error_code ec;
  const fs::path root =
      (root_dir.empty() || root_dir == ".") ? fs::current_path(ec) : fs::path(root_dir);
  const std::string cwd = root.lexically_normal().string();
  const std::string log_path = process_job::make_job_log_path("compile");
  // The job runs in the project root rather than wherever the editor was
  // started: the command is about this project, and it is the directory the
  // findings' paths are relative to.
#ifdef _WIN32
  const std::string job_command = "cd /d " + shell_util::shell_quote(cwd) + " && " + command;
#else
  const std::string job_command = "cd " + shell_util::shell_quote(cwd) + " && " + command;
#endif
  // Under a terminal of its own, so the command may be a program rather than a
  // compiler: it reads its stdin (a program that asks a question sees EOF, the
  // way the silent job's /dev/null answered it) and sees a tty, which is what
  // makes a tool colour its output on its own.
  const int pid = process_job::spawn_background_pty_shell(job_command, log_path);

  // The pane the user is reading keeps its buffer: the compilation buffer is
  // created for the split below it, not in place of what is on screen.
  const int reading_pane =
      (current_pane >= 0 && current_pane < (int)panes.size()) ? current_pane : -1;
  const int reading_buffer =
      reading_pane >= 0 ? panes[(size_t)reading_pane].buffer_id : current_buffer;
  const int compile_index = compile_ensure_buffer();
  if (reading_pane >= 0 && reading_buffer >= 0 && reading_buffer < (int)buffers.size()
      && reading_buffer != compile_index)
  {
    current_pane = reading_pane;
    pane_show_buffer(reading_buffer);
    detach_pane_tab(reading_pane, compile_index);
  }

  FileBuffer &buf = buffers[(size_t)compile_index];
  std::vector<std::string> header;
  header.push_back("Compilation started at " + compile_clock_now());
  header.push_back("$ " + command + "   (in " + cwd + ")");
  buf.replace_lines(0, (int)buf.line_count(), header);
  buf.clear_undo_history();
  buf.decorations.clear();
  buf.decoration_base_valid = false;
  buf.cursor = {0, 0};
  buf.preferred_x = 0;
  buf.selection = {{0, 0}, {0, 0}, false};
  buf.scroll_offset = 0;
  buf.scroll_x = 0;
  buf.modified = false;

  compile.buffer_id = compile_index;
  compile.command = command;
  compile.cwd = cwd;
  compile.log_path = log_path;
  compile.log_offset = 0;
  compile.pending.clear();
  compile.findings.clear();
  compile.finding_rows.clear();
  compile.selected = -1;
  compile.output_rows = 0;
  compile.exit_code = 0;
  compile.pid = pid;
  compile.running = pid >= 0;
  compile.has_run = true;
  compile.follow_tail = true;
  compile.started_ms = compile_now_ms();
  compile.finished_ms = 0;
  compile.parser.reset();
  compile.ansi.reset();

  // The pane a run reuses is one that still holds the log. A pane the reader
  // has opened a file into -- the run before this one left the caret in the
  // log, and `:e` took that pane over -- is somebody else's pane now, so the
  // log asks for a split of its own rather than taking a tab in it.
  const bool pane_alive =
      compile.pane_index >= 0 && compile.pane_index < (int)panes.size()
      && find_local_tab_index(panes[(size_t)compile.pane_index], compile_index) >= 0;
  if (pane_alive)
  {
    activate_pane(compile.pane_index);
    pane_show_buffer(compile_index);
  }
  else
  {
    const int pane = split_pane_at(current_pane, 0, 1, compile_index, false, false);
    if (pane >= 0)
    {
      compile.pane_index = pane;
      // Emacs' compilation window is a strip, not half the frame: take the
      // bottom third of the pane area, as far as its floor allows.
      const int area_h = std::max(1, compute_pane_area().h);
      const int target = std::clamp(area_h / 3, 6, 24);
      resize_current_pane_direction('k', std::max(0, area_h / 2 - target));
    }
  }

  needs_redraw = true;
  if (pid < 0)
  {
    // Nothing to poll: report it the way a command that could not start ends.
    compile_finish(-1);
    return;
  }
  set_message("Compiling: " + command, true);
}

void Editor::compile_append_output(const std::string &chunk)
{
  if (compile.buffer_id < 0 || compile.buffer_id >= (int)buffers.size())
  {
    return;
  }
  // A chunk is whatever the job had written since the last poll: line by line,
  // with the tail of a line that is not finished yet kept for the next one.
  compile.pending += chunk;
  // A carriage return with no newline after it is a progress bar rewriting the
  // row it is already on, so only the text after the last one is still there.
  const size_t rewrite = compile.pending.rfind('\r');
  if (rewrite != std::string::npos && compile.pending.find('\n', rewrite) == std::string::npos)
  {
    compile.pending.erase(0, rewrite + 1);
  }
  std::vector<std::string> lines;
  size_t start = 0;
  while (true)
  {
    const size_t newline = compile.pending.find('\n', start);
    if (newline == std::string::npos)
    {
      break;
    }
    lines.push_back(compile.pending.substr(start, newline - start));
    start = newline + 1;
  }
  if (start > 0)
  {
    compile.pending.erase(0, start);
  }
  compile_append_rows(lines);
}

void Editor::compile_flush_pending()
{
  // Whatever the job left without a final newline is still output: a program
  // printing a result, a progress bar's last state. It becomes a row of its
  // own rather than sitting in the pending buffer for good.
  if (compile.pending.empty())
  {
    return;
  }
  std::vector<std::string> lines;
  lines.push_back(std::move(compile.pending));
  compile.pending.clear();
  compile_append_rows(lines);
}

void Editor::compile_append_rows(const std::vector<std::string> &raw_lines)
{
  if (raw_lines.empty() || compile.buffer_id < 0 || compile.buffer_id >= (int)buffers.size())
  {
    return;
  }
  FileBuffer &buf = buffers[(size_t)compile.buffer_id];
  const int first_row = (int)buf.line_count();

  std::vector<std::string> lines;
  std::vector<ansi_spans::Row> rows;
  lines.reserve(raw_lines.size());
  rows.reserve(raw_lines.size());
  for (const std::string &raw : raw_lines)
  {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    // A finished line can carry rewrites too (a terminal's `\r\n` is not the
    // only way a program ends one), and only the last is the row it left.
    const size_t rewrite = line.rfind('\r');
    if (rewrite != std::string::npos)
    {
      line.erase(0, rewrite + 1);
    }
    ansi_spans::Row row = compile.ansi.feed(line);
    lines.push_back(std::move(row.text));
    rows.push_back(std::move(row));
    compile.output_rows++;
  }

  buf.replace_lines(first_row, 0, lines);
  for (int i = 0; i < (int)lines.size(); i++)
  {
    const std::string &text = lines[(size_t)i];
    CompileFinding finding;
    if (compile.parser.feed(text, finding))
    {
      finding.filepath = compile_resolve_path(finding.filepath, compile.cwd);
      compile.findings.push_back(finding);
      compile.finding_rows.push_back(first_row + i);
      // A row that names a problem is tinted with the severity slot the editor
      // uses for its own findings, so the errors are the rows the eye lands on.
      Decoration decoration;
      decoration.id = ++compile.decoration_id;
      decoration.row = first_row + i;
      decoration.col = 0;
      decoration.width = (int)text.size();
      decoration.hl = compile_severity_group(finding.severity);
      decoration.priority = 1;
      add_decoration(buf, decoration);
    }
    // The colours the command itself printed, painted over that tint: the tool
    // said what colour this text is, and it said it about this run's output.
    for (const ansi_spans::Span &span : rows[(size_t)i].spans)
    {
      Decoration decoration;
      decoration.id = ++compile.decoration_id;
      decoration.row = first_row + i;
      decoration.col = span.col;
      decoration.width = span.width;
      decoration.fg = compile_span_color(span.fg);
      decoration.bg = compile_span_color(span.bg);
      decoration.dim = span.dim;
      decoration.underline = span.underline ? 1 : 0;
      decoration.priority = 2;
      add_decoration(buf, decoration);
    }
  }
  compile_follow_tail();
  needs_redraw = true;
}

void Editor::compile_follow_tail()
{
  if (!compile.follow_tail || compile.buffer_id < 0 || compile.buffer_id >= (int)buffers.size()
      || compile.pane_index < 0 || compile.pane_index >= (int)panes.size())
  {
    return;
  }
  const SplitPane &pane = panes[(size_t)compile.pane_index];
  FileBuffer &buf = buffers[(size_t)compile.buffer_id];
  const int offset = std::max(0, (int)buf.line_count() - std::max(1, pane_viewport_h(pane)));
  // The live scroll fields belong to the active pane, so a background build
  // writes its own pane's stored view instead of dragging the view the user is
  // reading along with it.
  if (pane.active && current_pane == compile.pane_index)
  {
    buf.scroll_offset = offset;
  }
  else
  {
    panes[(size_t)compile.pane_index].view_scroll_offset = offset;
  }
}

void Editor::poll_compile_job()
{
  if (compile.log_path.empty() || (compile.pid < 0 && !compile.running))
  {
    return;
  }
  std::string chunk;
  compile.log_offset = process_job::read_appended(compile.log_path, compile.log_offset, chunk);
  if (!chunk.empty())
  {
    compile_append_output(chunk);
  }
  if (compile.pid >= 0)
  {
    const int status = process_job::reap_child(compile.pid);
    if (status >= 0)
    {
      compile_finish(status);
    }
  }
}

void Editor::compile_finish(int status)
{
  // Whatever the job wrote between the last poll and its exit still arrives: a
  // build's last line is usually the one that says it failed.
  if (!compile.log_path.empty())
  {
    std::string chunk;
    compile.log_offset = process_job::read_appended(compile.log_path, compile.log_offset, chunk);
    if (!chunk.empty())
    {
      compile_append_output(chunk);
    }
  }
  // The job is over, so a tail it never ended with a newline is a row now.
  compile_flush_pending();
  compile.running = false;
  compile.pid = -1;
  compile.exit_code = status;
  compile.finished_ms = compile_now_ms();

  if (compile.buffer_id >= 0 && compile.buffer_id < (int)buffers.size())
  {
    FileBuffer &buf = buffers[(size_t)compile.buffer_id];
    std::string tail;
    if (status < 0)
    {
      tail = "Compilation could not be started";
    }
    else if (status == 0)
    {
      tail = "Compilation finished at " + compile_clock_now();
    }
    else
    {
      tail = "Compilation exited abnormally with code " + std::to_string(status) + " at "
             + compile_clock_now();
    }
    buf.replace_lines((int)buf.line_count(), 0, {tail});
    compile_follow_tail();
  }

  int errors = 0;
  for (const CompileFinding &finding : compile.findings)
  {
    if (finding.severity == 1)
    {
      errors++;
    }
  }
  const int total = (int)compile.findings.size();
  if (status == 0 && total == 0)
  {
    set_message("Compilation finished", true);
  }
  else if (status == 0)
  {
    set_message("Compilation finished: " + std::to_string(total) + " location(s), "
                    + std::to_string(errors) + " error(s)",
                true);
  }
  else
  {
    set_message("Compilation failed: " + std::to_string(errors) + " error(s), "
                    + std::to_string(total) + " location(s)",
                true);
  }
  needs_redraw = true;
}

void Editor::compile_stop()
{
  if (compile.pid < 0)
  {
    return;
  }
  const bool ended = process_job::terminate_child(compile.pid);
  // The pid stays until a poll reaps it, so a killed build's last output is
  // still read rather than lost with the process.
  compile_flush_pending();
  compile.running = false;
  if (compile.buffer_id >= 0 && compile.buffer_id < (int)buffers.size())
  {
    FileBuffer &buf = buffers[(size_t)compile.buffer_id];
    buf.replace_lines(
        (int)buf.line_count(), 0, {ended ? "Compilation killed" : "Compilation still running"});
    compile_follow_tail();
  }
  if (ended)
  {
    set_message("Compilation killed", true);
  }
}

bool Editor::compile_open_finding(int index)
{
  if (index < 0 || index >= (int)compile.findings.size())
  {
    return false;
  }
  const CompileFinding finding = compile.findings[(size_t)index];
  compile.selected = index;

  // The caret has to land in the source, not in the log: when the compilation
  // pane is the one with focus -- it is, right after :compile -- the jump opens
  // in a pane that is already showing a file.
  if (compile_buffer_focused())
  {
    for (int i = 0; i < (int)panes.size(); i++)
    {
      if (i == compile.pane_index)
      {
        continue;
      }
      const int shown = panes[(size_t)i].buffer_id;
      if (shown >= 0 && shown < (int)buffers.size()
          && is_compile_log_path(buffers[(size_t)shown].filepath))
      {
        continue;
      }
      activate_pane(i);
      break;
    }
  }

  JumpLocation location;
  location.filepath = finding.filepath;
  location.cursor = {std::max(0, finding.col - 1), std::max(0, finding.line - 1)};
  const bool landed = jump_to(location);
  set_message(get_filename(finding.filepath) + ":" + std::to_string(std::max(1, finding.line)),
              true);
  if (landed)
  {
    // A jump like any other: Ctrl+O comes back to where it started.
    record_jump();
  }
  needs_redraw = true;
  return landed;
}

bool Editor::compile_jump(int direction)
{
  if (compile.findings.empty())
  {
    set_message("No compile findings: run :compile first", true);
    return false;
  }
  const FileBuffer &buf = get_buffer();
  const int selected =
      compile_finding_for_jump(compile.findings, buf.filepath, buf.cursor, direction);
  return compile_open_finding(selected);
}

bool Editor::compile_jump_from_row(int row)
{
  for (int i = 0; i < (int)compile.finding_rows.size(); i++)
  {
    if (compile.finding_rows[(size_t)i] == row)
    {
      return compile_open_finding(i);
    }
  }
  set_message("No compile finding on this line", true);
  return false;
}

bool Editor::compile_buffer_focused() const
{
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return false;
  }
  return is_compile_log_path(buffers[(size_t)current_buffer].filepath);
}

int Editor::compile_finding_count() const
{
  return (int)compile.findings.size();
}
