#include "commands/utils.h"
#include "cpp_assist.h"
#include "editor.h"
#include "host_api.h"
#include "jot/lua/api.h"
#include "jot/workspace/git_run.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace CommandLineUtils;


bool Editor::execute_ex_command(const std::string &input_line)
{
  std::string line = trim_copy(input_line);
  if (!line.empty() && line[0] == ':')
  {
    line.erase(0, 1);
    line = trim_copy(line);
  }

  auto has_unsaved_buffers = [&]()
  {
    for (const auto &b : buffers)
    {
      if (b.modified)
        return true;
    }
    return false;
  };

  std::string cmd;
  std::string arg;
  std::istringstream iss(line);
  iss >> cmd;
  std::getline(iss, arg);
  arg = trim_copy(arg);
  std::string lcmd = to_lower_copy(cmd);

  auto goto_line_col = [&](int line_1based, int col_1based)
  {
    auto &buf = get_buffer();
    if (buf.line_count() == 0)
    {
      return;
    }
    buf.cursor.y = std::clamp(line_1based - 1, 0, (int)buf.line_count() - 1);
    int line_len = (int)buf.line(buf.cursor.y).length();
    buf.cursor.x = std::clamp(col_1based - 1, 0, line_len);
    clear_selection();
    ensure_cursor_visible();
    set_message("Jumped to line " + std::to_string(buf.cursor.y + 1) + ", col "
                + std::to_string(buf.cursor.x + 1));
  };
  auto resolve_path = [&](const std::string &raw) -> fs::path
  {
    std::error_code ec;
    fs::path p(raw);
    if (p.is_relative())
    {
      fs::path base = root_dir.empty() ? fs::current_path(ec) : fs::path(root_dir);
      if (ec)
      {
        ec.clear();
        base = fs::path(".");
      }
      p = base / p;
    }
    p = fs::absolute(p, ec);
    if (ec)
    {
      return fs::path(raw);
    }
    return p.lexically_normal();
  };
  auto find_existing_header_for_source = [&](const fs::path &source)
  {
    fs::path direct = CppAssist::counterpart_path_for(source);
    std::error_code ec;
    if (fs::exists(direct, ec))
    {
      return direct;
    }
    for (const char *ext : {".hpp", ".h", ".hh", ".hxx"})
    {
      fs::path candidate = source;
      candidate.replace_extension(ext);
      ec.clear();
      if (fs::exists(candidate, ec))
      {
        return candidate;
      }
    }
    return direct;
  };
  auto starts_with_path = [&](const std::string &child, const std::string &parent)
  {
    if (child.size() < parent.size())
    {
      return false;
    }
    if (child.compare(0, parent.size(), parent) != 0)
    {
      return false;
    }
    return child.size() == parent.size() || child[parent.size()] == '/'
           || child[parent.size()] == '\\';
  };
  auto close_buffers_for_path = [&](const std::string &target_abs, bool is_dir)
  {
    std::error_code ec;
    const std::string norm_target = fs::path(target_abs).lexically_normal().string();
    const std::string dir_prefix = norm_target + std::string(1, fs::path::preferred_separator);
    for (int i = (int)buffers.size() - 1; i >= 0; --i)
    {
      if (buffers[i].filepath.empty())
      {
        continue;
      }
      fs::path bp = fs::absolute(buffers[i].filepath, ec);
      if (ec)
      {
        ec.clear();
        continue;
      }
      std::string buf_path = bp.lexically_normal().string();
      bool match = (!is_dir && buf_path == norm_target)
                   || (is_dir && starts_with_path(buf_path, dir_prefix));
      if (match)
      {
        close_buffer_at(i);
      }
    }
  };

  int parsed_line = 0, parsed_col = 1;
  if (lcmd.empty())
  {
    // Nothing to execute, just close.
  }
  else if (parse_line_col(lcmd, parsed_line, parsed_col))
  {
    goto_line_col(parsed_line, parsed_col);
  }
  else if (lcmd == "reload")
  {
    // Reload everything configurable at runtime: settings.conf overlay +
    // config.lua (live-applied), Lua plugins, and tree-sitter policy.
    reload_config();
    if (lua_api)
    {
      lua_api->reload_plugins();
    }
    reload_tree_sitter();
    refresh_command_palette();
    needs_redraw = true;
  }
  else if (lcmd == "reloadconfig")
  {
    // Config-only reload: settings.conf overlay + config.lua, live-applied.
    reload_config();
    refresh_command_palette();
    needs_redraw = true;
  }
  else if (lcmd == "reloadplugins")
  {
    if (lua_api)
    {
      lua_api->reload_plugins();
      refresh_command_palette();
      needs_redraw = true;
    }
    else
    {
      set_message("Python plugins unavailable");
    }
  }
  else if (lcmd == "plugins")
  {
    if (!lua_api)
    {
      set_message("Python plugins unavailable");
    }
    else
    {
      std::stringstream out;
      out << "Plugins\n";
      for (const auto &status : lua_api->load_status())
      {
        out << (status.loaded ? "[ok] " : "[err] ") << status.name;
        if (!status.error.empty())
        {
          out << " - " << status.error;
        }
        out << "\n";
      }
      out << "\nCommands: " << lua_api->commands().size() << "\n";
      for (const auto &command : lua_api->commands())
      {
        out << "  :" << command.name;
        if (!command.detail.empty())
        {
          out << " - " << command.detail;
        }
        out << "\n";
      }
      out << "\nKeymaps: " << lua_api->keymaps().size() << "\n";
      for (const auto &keymap : lua_api->keymaps())
      {
        out << "  " << keymap.key;
        if (!keymap.detail.empty())
        {
          out << " - " << keymap.detail;
        }
        else if (!keymap.command.empty())
        {
          out << " - " << keymap.command;
        }
        out << "\n";
      }
      out << "\nPanels: " << lua_api->panels().size() << "\n";
      for (const auto &panel : lua_api->panels())
      {
        out << "  " << panel.name;
        if (!panel.title.empty())
        {
          out << " - " << panel.title;
        }
        out << "\n";
      }
      show_popup(limit_lines(out.str(), 24), "Plugins");
    }
  }
  else if (lcmd == "pluginpanel")
  {
    if (!lua_api)
    {
      set_message("Python plugins unavailable");
    }
    else if (arg.empty())
    {
      std::string names;
      for (const auto &panel : lua_api->panels())
      {
        if (!names.empty())
        {
          names += ", ";
        }
        names += panel.name;
      }
      set_message(names.empty() ? "No plugin panels" : "Plugin panels: " + names);
    }
    else
    {
      host_api->io.show_plugin_panel(arg);
    }
  }
  else if (lcmd == "q" || lcmd == "quit")
  {
    if (has_unsaved_buffers())
    {
      show_quit_prompt = true;
    }
    else
    {
      running = false;
    }
  }
  else if (lcmd == "q!" || lcmd == "quit!")
  {
    running = false;
  }
  else if (lcmd == "w" || lcmd == "write" || lcmd == "save")
  {
    if (!arg.empty())
    {
      get_buffer().filepath = arg;
    }
    save_file();
  }
  else if (lcmd == "wq" || lcmd == "x" || lcmd == "xit")
  {
    if (!arg.empty())
    {
      get_buffer().filepath = arg;
    }
    save_file();
    running = false;
  }
  else if (lcmd == "e" || lcmd == "edit" || lcmd == "open")
  {
    if (arg.empty())
    {
      set_message("Usage: :e <file>");
    }
    else
    {
      open_file(arg);
    }
  }
  else if (lcmd == "new" || lcmd == "enew")
  {
    create_new_buffer();
  }
  else if (lcmd == "bd" || lcmd == "bdelete" || lcmd == "close")
  {
    close_buffer();
  }
  else if (lcmd == "sp" || lcmd == "split" || lcmd == "splith")
  {
    std::string dir = to_lower_copy(trim_copy(arg));
    if (dir == "left" || dir == "l")
    {
      split_pane_left();
    }
    else if (dir == "right" || dir == "r" || dir.empty())
    {
      split_pane_right();
    }
    else if (dir == "up" || dir == "u" || dir == "top" || dir == "t")
    {
      split_pane_up();
    }
    else if (dir == "down" || dir == "d" || dir == "bottom" || dir == "b")
    {
      split_pane_down();
    }
    else
    {
      set_message("Usage: :split [left|right|up|down]");
    }
  }
  else if (lcmd == "vsp" || lcmd == "splitv")
  {
    std::string dir = to_lower_copy(trim_copy(arg));
    if (dir == "left" || dir == "l")
    {
      split_pane_left();
    }
    else
    {
      split_pane_right();
    }
  }
  else if (lcmd == "splitleft" || lcmd == "spleft")
  {
    split_pane_left();
  }
  else if (lcmd == "splitright" || lcmd == "spright")
  {
    split_pane_right();
  }
  else if (lcmd == "splitup" || lcmd == "spup")
  {
    split_pane_up();
  }
  else if (lcmd == "splitdown" || lcmd == "spdown")
  {
    split_pane_down();
  }
  else if (lcmd == "bn" || lcmd == "nextpane")
  {
    next_pane();
  }
  else if (lcmd == "bp" || lcmd == "prevpane")
  {
    prev_pane();
  }
  else if (lcmd == "focusleft")
  {
    if (!focus_pane_direction('h'))
    {
      set_message("No pane to the left");
    }
  }
  else if (lcmd == "focusright")
  {
    if (!focus_pane_direction('l'))
    {
      set_message("No pane to the right");
    }
  }
  else if (lcmd == "focusup")
  {
    if (!focus_pane_direction('k'))
    {
      set_message("No pane above");
    }
  }
  else if (lcmd == "focusdown")
  {
    if (!focus_pane_direction('j'))
    {
      set_message("No pane below");
    }
  }
  else if (lcmd == "equalizepanes" || lcmd == "eqp" || lcmd == "wq")
  {
    equalize_panes();
  }
  else if (lcmd == "panezoom" || lcmd == "zoom" || lcmd == "wz")
  {
    toggle_pane_zoom();
  }
  else if (lcmd == "swappanes" || lcmd == "wsw")
  {
    swap_panes();
  }
  else if (lcmd == "wincmd")
  {
    std::string dir = to_lower_copy(trim_copy(arg));
    char focus_dir = '\0';
    if (dir == "h" || dir == "left")
    {
      focus_dir = 'h';
    }
    else if (dir == "j" || dir == "down")
    {
      focus_dir = 'j';
    }
    else if (dir == "k" || dir == "up")
    {
      focus_dir = 'k';
    }
    else if (dir == "l" || dir == "right")
    {
      focus_dir = 'l';
    }
    else if (dir == "=")
    {
      equalize_panes();
      return true;
    }
    else if (dir == "z")
    {
      toggle_pane_zoom();
      return true;
    }
    else if (dir == "x")
    {
      swap_panes();
      return true;
    }
    else if (dir == "q")
    {
      close_pane();
      return true;
    }
    if (focus_dir == '\0')
    {
      set_message("Usage: :wincmd h|j|k|l|=|z|x|q");
    }
    else if (!focus_pane_direction(focus_dir))
    {
      set_message("No pane in that direction");
    }
  }
  else if (lcmd == "newlinebelow" || lcmd == "nlbelow")
  {
    insert_line_below();
  }
  else if (lcmd == "newlineabove" || lcmd == "nlabove")
  {
    insert_line_above();
  }
  else if (lcmd == "minimap")
  {
    toggle_minimap();
  }
  else if (lcmd == "term" || lcmd == "terminal")
  {
    toggle_integrated_terminal();
  }
  else if (lcmd == "termnew" || lcmd == "terminalnew")
  {
    create_integrated_terminal();
  }
  else if (lcmd == "task")
  {
    if (arg.empty())
    {
      show_terminal_tasks();
    }
    else
    {
      run_terminal_task(arg, false);
    }
  }
  else if (lcmd == "tasknew")
  {
    if (arg.empty())
    {
      set_message("Usage: :tasknew <name>");
    }
    else
    {
      run_terminal_task(arg, true);
    }
  }
  else if (lcmd == "taskrerun")
  {
    rerun_last_terminal_task();
  }
  else if (lcmd == "debugpanel")
  {
    toggle_debugger_panel();
  }
  else if (lcmd == "zen")
  {
    if (toggle_zen_mode())
    {
      set_message("Zen mode: on (F12 to exit)");
    }
    else
    {
      set_message("Zen mode: off");
    }
  }
  else if (lcmd == "settings")
  {
    // Opens the cell-based settings menu (works in terminal and GUI):
    // lists every config key, bools toggle, ints/strings edit inline.
    toggle_settings_menu();
  }
  else if (lcmd == "debug" || lcmd == "debuggdb")
  {
    start_debugger_command("gdb", arg);
  }
  else if (lcmd == "debuglldb")
  {
    start_debugger_command("lldb", arg);
  }
  else if (lcmd == "debugconfig")
  {
    run_debugger_config(arg);
  }
  else if (lcmd == "debugattach")
  {
    attach_debugger_command("gdb", arg);
  }
  else if (lcmd == "debugstop")
  {
    stop_debugger_session();
  }
  else if (lcmd == "debugrestart")
  {
    restart_debugger_session();
  }
  else if (lcmd == "debugcontinue")
  {
    continue_debugger_session();
  }
  else if (lcmd == "debugpause")
  {
    pause_debugger_session();
  }
  else if (lcmd == "debugstep")
  {
    step_debugger_in();
  }
  else if (lcmd == "debugnext")
  {
    step_debugger_next();
  }
  else if (lcmd == "debugout")
  {
    step_debugger_out();
  }
  else if (lcmd == "debugthreads")
  {
    show_debugger_threads();
  }
  else if (lcmd == "debugmemory")
  {
    // :debugmemory [expr] [bytes] — a trailing integer is the read size.
    std::string expr = trim_copy(arg);
    int bytes = 128;
    size_t split = expr.find_last_of(" \t");
    if (split != std::string::npos)
    {
      std::string tail = trim_copy(expr.substr(split + 1));
      if (!tail.empty())
      {
        try
        {
          bytes = std::stoi(tail);
          expr = trim_copy(expr.substr(0, split));
        }
        catch (...)
        {
          bytes = 128;
        }
      }
    }
    request_debugger_memory(expr, bytes);
  }
  else if (lcmd == "debugdisasm")
  {
    request_debugger_disassembly(arg);
  }
  else if (lcmd == "find" || lcmd == "ff")
  {
    std::string target = trim_copy(arg);
    if (target.empty())
    {
      target = telescope_launch_root();
    }
    telescope.open(target);
    telescope.scan_async(task_queue_.get(), [this] { needs_redraw = true; });
    show_command_palette = false;
    command_palette_query.clear();
    command_palette_results.clear();
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    command_palette_selected = 0;
    needs_redraw = true;
    return false;
  }
  else if (lcmd == "grep" || lcmd == "projectsearch" || lcmd == "searchall")
  {
    show_project_search(arg);
    return false;
  }
  else if (lcmd == "mkfile")
  {
    if (arg.empty())
    {
      set_message("Usage: :mkfile <path>");
    }
    else
    {
      std::error_code ec;
      fs::path p = resolve_path(arg);
      if (fs::exists(p, ec))
      {
        set_message("File already exists: " + p.string());
      }
      else
      {
        fs::create_directories(p.parent_path(), ec);
        ec.clear();
        std::ofstream out(p.string());
        if (!out.is_open())
        {
          set_message("Failed to create file: " + p.string());
        }
        else
        {
          out.close();
          open_file(p.string());
          if (show_sidebar)
          {
            load_file_tree(root_dir);
          }
          set_message("Created file: " + p.filename().string());
        }
      }
    }
  }
  else if (lcmd == "cpppair")
  {
    if (arg.empty())
    {
      set_message("Usage: :cpppair <path>");
    }
    else
    {
      std::error_code ec;
      fs::path first = resolve_path(arg);
      fs::path header;
      fs::path source;
      if (CppAssist::is_header_path(first))
      {
        header = first;
        source = CppAssist::counterpart_path_for(first);
      }
      else if (CppAssist::is_source_path(first))
      {
        source = first;
        header = CppAssist::counterpart_path_for(first);
      }
      else
      {
        header = first;
        header.replace_extension(".hpp");
        source = first;
        source.replace_extension(".cpp");
      }
      if (!CppAssist::is_header_path(header) || !CppAssist::is_source_path(source))
      {
        set_message("cpppair expects a C++ header or source path");
      }
      else
      {
        bool created_any = false;
        fs::create_directories(header.parent_path(), ec);
        fs::create_directories(source.parent_path(), ec);
        if (!fs::exists(header, ec))
        {
          std::ofstream out(header.string());
          if (out.is_open())
          {
            out << CppAssist::header_skeleton(header);
            created_any = true;
          }
        }
        ec.clear();
        if (!fs::exists(source, ec))
        {
          std::ofstream out(source.string());
          if (out.is_open())
          {
            out << CppAssist::source_skeleton(header);
            created_any = true;
          }
        }
        if (show_sidebar)
        {
          load_file_tree(root_dir);
        }
        open_file(header.string());
        set_message(created_any ? "Created C++ pair" : "C++ pair already exists");
      }
    }
  }
  else if (lcmd == "cppimpl")
  {
    std::error_code ec;
    fs::path target = arg.empty() ? fs::path(get_buffer().filepath) : resolve_path(arg);
    if (target.empty())
    {
      set_message("Usage: :cppimpl [header-or-source]");
    }
    else
    {
      fs::path header =
          CppAssist::is_header_path(target) ? target : find_existing_header_for_source(target);
      fs::path source =
          CppAssist::is_source_path(target) ? target : CppAssist::counterpart_path_for(header);
      if (!CppAssist::is_header_path(header) || !CppAssist::is_source_path(source))
      {
        set_message("cppimpl expects a C++ header or source file");
      }
      else if (!fs::exists(header, ec))
      {
        set_message("Header not found: " + header.string());
      }
      else
      {
        std::ifstream hin(header.string());
        std::stringstream hbuf;
        hbuf << hin.rdbuf();
        std::string source_text;
        bool source_exists = fs::exists(source, ec);
        if (source_exists)
        {
          std::ifstream sin(source.string());
          std::stringstream sbuf;
          sbuf << sin.rdbuf();
          source_text = sbuf.str();
        }
        auto result = CppAssist::generate_missing_implementations(
            hbuf.str(), source_text, header, source, source_exists);
        if (result.generated_count == 0 && source_exists)
        {
          set_message("No missing implementations");
        }
        else
        {
          fs::create_directories(source.parent_path(), ec);
          std::ofstream out(source.string(), std::ios::trunc);
          if (!out.is_open())
          {
            set_message("Failed to write source: " + source.string());
          }
          else
          {
            out << result.source_text;
            out.close();
            if (show_sidebar)
            {
              load_file_tree(root_dir);
            }
            open_file(source.string());
            set_message("Generated " + std::to_string(result.generated_count)
                        + " implementation(s)");
          }
        }
      }
    }
  }
  else if (lcmd == "mkdir")
  {
    if (arg.empty())
    {
      set_message("Usage: :mkdir <path>");
    }
    else
    {
      std::error_code ec;
      fs::path p = resolve_path(arg);
      if (fs::exists(p, ec))
      {
        set_message("Path already exists: " + p.string());
      }
      else if (fs::create_directories(p, ec))
      {
        if (show_sidebar)
        {
          load_file_tree(root_dir);
        }
        set_message("Created folder: " + p.filename().string());
      }
      else
      {
        set_message("Failed to create folder: " + p.string());
      }
    }
  }
  else if (lcmd == "rename")
  {
    std::istringstream riss(arg);
    std::string from_raw;
    std::string to_raw;
    riss >> from_raw >> to_raw;
    if (from_raw.empty() || to_raw.empty())
    {
      set_message("Usage: :rename <old_path> <new_path>");
    }
    else
    {
      std::error_code ec;
      fs::path from = resolve_path(from_raw);
      fs::path to = resolve_path(to_raw);
      if (!fs::path(to_raw).has_parent_path())
      {
        to = from.parent_path() / fs::path(to_raw);
      }
      to = to.lexically_normal();

      if (!fs::exists(from, ec))
      {
        set_message("Source not found: " + from.string());
      }
      else if (fs::exists(to, ec))
      {
        set_message("Destination exists: " + to.string());
      }
      else
      {
        fs::create_directories(to.parent_path(), ec);
        ec.clear();
        fs::rename(from, to, ec);
        if (ec)
        {
          set_message("Rename failed: " + ec.message());
        }
        else
        {
          const std::string from_s = from.lexically_normal().string();
          const std::string to_s = to.lexically_normal().string();
          for (auto &b : buffers)
          {
            if (b.filepath.empty())
            {
              continue;
            }
            std::error_code bec;
            fs::path bp = fs::absolute(b.filepath, bec);
            if (bec)
            {
              continue;
            }
            if (bp.lexically_normal().string() == from_s)
            {
              b.filepath = to_s;
            }
          }
          if (show_sidebar)
          {
            load_file_tree(root_dir);
          }
          set_message("Renamed to: " + to.filename().string());
        }
      }
    }
  }
  else if (lcmd == "rm")
  {
    if (arg.empty())
    {
      set_message("Usage: :rm <path>");
    }
    else
    {
      std::error_code ec;
      fs::path p = resolve_path(arg);
      if (!fs::exists(p, ec))
      {
        set_message("Path not found: " + p.string());
      }
      else
      {
        bool is_dir = fs::is_directory(p, ec);
        close_buffers_for_path(p.string(), is_dir);
        if (is_dir)
        {
          fs::remove_all(p, ec);
        }
        else
        {
          fs::remove(p, ec);
        }
        if (ec)
        {
          set_message("Delete failed: " + ec.message());
        }
        else
        {
          if (show_sidebar)
          {
            load_file_tree(root_dir);
          }
          set_message("Deleted: " + p.filename().string());
        }
      }
    }
  }
  else
  {
    return execute_ex_command_tail(lcmd, arg, line);
  }
  return true;
}
